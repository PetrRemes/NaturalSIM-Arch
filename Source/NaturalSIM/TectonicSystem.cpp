#include "TectonicSystem.h"
#include "SimWorldTypes.h"
#include "SimWorldManager.h"
#include "CosmosSystem.h"
#include "ManaSystem.h"
#include "HistorySystem.h"
#include "DisasterSystem.h"
#include "Async/ParallelFor.h"

UTectonicSystem::UTectonicSystem() { PrimaryComponentTick.bCanEverTick = false; }
void UTectonicSystem::BeginPlay() { Super::BeginPlay(); }

struct FChunkNeighborhood {
    const FChunkData* Chunks[3][3];
    int32 CS;

    void Initialize(ASimWorldManager* Manager, FIntPoint CenterCoord, int32 InCS) {
        CS = InCS;
        for (int32 cy = -1; cy <= 1; cy++) {
            for (int32 cx = -1; cx <= 1; cx++) {
                Chunks[cy + 1][cx + 1] = Manager->WorldChunks.Find(FIntPoint(CenterCoord.X + cx, CenterCoord.Y + cy));
            }
        }
    }

    FORCEINLINE void GetNeighbor(int32 lx, int32 ly, const FCellStaticData*& OutS, const FCellDynamicData*& OutD) const {
        // FAST-PATH: Zamezení branchingu pro 96 % bunìk uvnitø chunku
        if (lx >= 0 && lx < CS && ly >= 0 && ly < CS) {
            int32 Idx = lx + ly * CS;
            OutS = &Chunks[1][1]->StaticCells[Idx];
            OutD = &Chunks[1][1]->DynamicCells[Idx];
            return;
        }

        int32 GridX = 1; int32 GridY = 1;
        int32 LocalX = lx; int32 LocalY = ly;

        if (lx < 0) { GridX = 0; LocalX = lx + CS; }
        else if (lx >= CS) { GridX = 2; LocalX = lx - CS; }

        if (ly < 0) { GridY = 0; LocalY = ly + CS; }
        else if (ly >= CS) { GridY = 2; LocalY = ly - CS; }

        if (const FChunkData* TargetChunk = Chunks[GridY][GridX]) {
            int32 Idx = LocalX + LocalY * CS;
            OutS = &TargetChunk->StaticCells[Idx];
            OutD = &TargetChunk->DynamicCells[Idx];
        }
        else {
            OutS = nullptr;
            OutD = nullptr;
        }
    }
};

void UTectonicSystem::ProcessChunkTectonics(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    for (int32 i = 0; i < OutChunk.StaticCells.Num(); i++) {
        OutChunk.DynamicCells[i].LavaBuffer = OutChunk.DynamicCells[i].Lava;

        if (OutChunk.StaticCells[i].bIsVolcano && OutChunk.DynamicCells[i].MagmaPressure == 0.0f) {
            OutChunk.DynamicCells[i].MagmaPressure = FMath::FRandRange(100.0f, 800.0f);
        }

        OutChunk.DynamicCells[i].EruptionDaysRemaining = 0.0f;
    }
}

void UTectonicSystem::ProcessDailyTectonics(const TArray<FIntPoint>& ChunkKeys, TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, float DeltaDays)
{
    if (!Manager) return;

    float TidalMultiplier = 1.0f;
    if (Manager->CosmosModule) {
        TidalMultiplier = Manager->CosmosModule->CurrentState.GlobalTidalMultiplier;
    }

    TArray<uint8> SafeFlags;
    SafeFlags.Init(0, ChunkKeys.Num());

    PendingEarthquakes.Empty();
    int32 CSize = Manager->ChunkSize - 1;
    int32 Offsets[8][2] = { {0,1}, {1,0}, {0,-1}, {-1,0}, {1,1}, {-1,-1}, {1,-1}, {-1,1} };

    ParallelFor(ChunkKeys.Num(), [&](int32 idx) {
        FChunkData& Chunk = WorldChunks[ChunkKeys[idx]];
        bool bTerrainDirty = false;
        bool bCloudDirty = false;

        Chunk.FaultStress += (Chunk.BaseTectonicPressure * TidalMultiplier * 0.01f * DeltaDays);

        float EarthquakeThreshold = 100.0f;

        if (Chunk.FaultStress >= EarthquakeThreshold) {
            float Overstress = Chunk.FaultStress - EarthquakeThreshold;
            float Intensity = 0.5f + (Overstress * 0.05f) + FMath::FRandRange(0.0f, 0.8f);

            FScopeLock Lock(&EarthquakeMutex);
            PendingEarthquakes.Add(TPair<FIntPoint, float>(ChunkKeys[idx], Intensity));

            Chunk.FaultStress *= FMath::FRandRange(0.1f, 0.2f);
        }

        FChunkNeighborhood Halo;
        Halo.Initialize(Manager, ChunkKeys[idx], Manager->ChunkSize);

        auto GetLowestNeighbor = [&](int32 cx, int32 cy, int32 lx, int32 ly) -> FIntPoint {
            const FCellStaticData* cCellS = nullptr; const FCellDynamicData* cCellD = nullptr;
            Halo.GetNeighbor(lx, ly, cCellS, cCellD);
            if (!cCellS || !cCellD) return FIntPoint(cx, cy);

            float lowestH = cCellS->Elevation + cCellD->Lava;
            FIntPoint bestP(cx, cy);

            for (int i = 0; i < 8; i++) {
                const FCellStaticData* nCellS = nullptr; const FCellDynamicData* nCellD = nullptr;
                Halo.GetNeighbor(lx + Offsets[i][0], ly + Offsets[i][1], nCellS, nCellD);
                if (nCellS && nCellD) {
                    float h = nCellS->Elevation + nCellD->Lava;
                    if (h < lowestH - 0.05f) {
                        lowestH = h;
                        bestP = FIntPoint(cx + Offsets[i][0], cy + Offsets[i][1]);
                    }
                }
            }
            return bestP;
            };

        for (int32 Y = 0; Y < CSize; Y++) {
            for (int32 X = 0; X < CSize; X++) {
                int32 i = X + Y * Manager->ChunkSize;
                if (i >= Chunk.StaticCells.Num()) continue;

                FCellStaticData& SCell = Chunk.StaticCells[i];
                FCellDynamicData& DCell = Chunk.DynamicCells[i];
                int32 GlobalX = (ChunkKeys[idx].X * CSize) + X;
                int32 GlobalY = (ChunkKeys[idx].Y * CSize) + Y;

                if (SCell.bIsVolcano) {
                    DCell.MagmaPressure += (TidalMultiplier * Manager->VolcanicActivity * 3.0f * DeltaDays);

                    if (DCell.EruptionDaysRemaining > 0.0f) {
                        DCell.EruptionDaysRemaining -= DeltaDays;

                        float EruptionIntensity = FMath::Clamp(DCell.MagmaPressure / 300.0f, 0.5f, 10.0f);
                        float MagmaReleased = FMath::Min(DCell.MagmaPressure, 150.0f * EruptionIntensity * DeltaDays);
                        DCell.MagmaPressure -= MagmaReleased;

                        float LavaOutput = MagmaReleased * 0.9f;

                        DCell.Lava += LavaOutput;
                        DCell.DangerLevel = 1.0f;
                        DCell.Temperature += 150.0f;
                        DCell.AshDensityBuffer += (MagmaReleased * 0.1f) * DeltaDays;

                        if (DCell.EruptionDaysRemaining <= 0.0f || DCell.MagmaPressure <= 0.0f) {
                            DCell.EruptionDaysRemaining = 0.0f;
                        }
                        bTerrainDirty = true;
                        bCloudDirty = true;
                    }
                    else if (DCell.MagmaPressure > 1000.0f && FMath::FRand() < 0.05f) {
                        DCell.EruptionDaysRemaining = FMath::FRandRange(10.0f, 30.0f);
                        bTerrainDirty = true;
                    }
                }

                float MyLavaDelta = 0.0f;
                float MyHead = SCell.Elevation + DCell.Lava;

                if (DCell.Lava > 0.001f) {
                    FIntPoint TargetFlow = GetLowestNeighbor(GlobalX, GlobalY, X, Y);

                    if (TargetFlow.X != GlobalX || TargetFlow.Y != GlobalY) {
                        int32 flowLx = X + (TargetFlow.X - GlobalX);
                        int32 flowLy = Y + (TargetFlow.Y - GlobalY);

                        const FCellStaticData* TargetCellS = nullptr; const FCellDynamicData* TargetCellD = nullptr;
                        Halo.GetNeighbor(flowLx, flowLy, TargetCellS, TargetCellD);

                        if (TargetCellS && TargetCellD) {
                            float TargetHead = TargetCellS->Elevation + TargetCellD->Lava;
                            float Diff = MyHead - TargetHead;

                            float Transfer = FMath::Min(DCell.Lava, Diff * 5.0f * DeltaDays);
                            Transfer = FMath::Min(Transfer, DCell.Lava * 0.9f);
                            MyLavaDelta -= Transfer;
                        }
                    }
                }

                for (int32 n = 0; n < 8; n++) {
                    int32 nx = GlobalX + Offsets[n][0];
                    int32 ny = GlobalY + Offsets[n][1];
                    int32 nlx = X + Offsets[n][0];
                    int32 nly = Y + Offsets[n][1];

                    const FCellStaticData* NCellS = nullptr; const FCellDynamicData* NCellD = nullptr;
                    Halo.GetNeighbor(nlx, nly, NCellS, NCellD);

                    if (NCellS && NCellD && NCellD->Lava > 0.001f) {
                        FIntPoint NeighborTarget = GetLowestNeighbor(nx, ny, nlx, nly);
                        if (NeighborTarget.X == GlobalX && NeighborTarget.Y == GlobalY) {
                            float nHead = NCellS->Elevation + NCellD->Lava;
                            float Diff = nHead - MyHead;
                            float Transfer = FMath::Min(NCellD->Lava, Diff * 5.0f * DeltaDays);
                            Transfer = FMath::Min(Transfer, NCellD->Lava * 0.9f);
                            MyLavaDelta += Transfer;
                        }
                    }
                }
                DCell.LavaBuffer = FMath::Max(0.0f, DCell.Lava + MyLavaDelta);
            }
        }

        for (int32 Y = 0; Y < CSize; Y++) {
            for (int32 X = 0; X < CSize; X++) {
                int32 i = X + Y * Manager->ChunkSize;
                if (i >= Chunk.StaticCells.Num()) continue;

                FCellStaticData& SCell = Chunk.StaticCells[i];
                FCellDynamicData& DCell = Chunk.DynamicCells[i];

                if (DCell.LavaBuffer > 0.0f) {
                    float CoolingRate = FMath::Lerp(2.0f, 0.01f, FMath::Clamp(DCell.LavaBuffer / 20.0f, 0.0f, 1.0f));
                    float Cooling = FMath::Min(DCell.LavaBuffer, CoolingRate * DeltaDays);

                    DCell.LavaBuffer -= Cooling;

                    float HardenedRock = Cooling * 0.8f;
                    SCell.Elevation += HardenedRock;
                    Chunk.AccumulatedTerrainChange += HardenedRock;

                    if (DCell.LavaBuffer > 5.0f && SCell.Bedrock != EBedrockType::Rock) {
                        float Melting = 1.5f * DeltaDays;
                        SCell.Elevation -= Melting;
                        Chunk.AccumulatedTerrainChange += Melting;
                    }

                    DCell.SoilFertility = FMath::Min(1.0f, DCell.SoilFertility + Cooling * 0.05f);
                    SCell.MineralOre += Cooling * 50.0f;

                    if (DCell.LavaBuffer > 0.1f) {
                        if (DCell.FloraDensity > 0.0f || SCell.TreeType != ETreeType::None) {
                            DCell.FireIntensity = 1.0f;
                            bCloudDirty = true;
                        }
                        DCell.SurfaceWater = 0.0f;
                        SCell.Bedrock = EBedrockType::Rock;
                    }
                }
            }
        }

        for (int32 step = 0; step < Manager->ChunkSize; step++) {
            int32 GlobalRightX = (ChunkKeys[idx].X * CSize) + CSize;
            int32 GlobalRightY = (ChunkKeys[idx].Y * CSize) + step;
            const FCellStaticData* RealRightS = nullptr; const FCellDynamicData* RealRightD = nullptr;
            if (Manager->GetCellStaticGlobalPtr(GlobalRightX, GlobalRightY, RealRightS) && Manager->GetCellDynamicGlobalPtr(GlobalRightX, GlobalRightY, RealRightD)) {
                Chunk.StaticCells[CSize + step * Manager->ChunkSize] = *RealRightS;
                Chunk.DynamicCells[CSize + step * Manager->ChunkSize] = *RealRightD;
            }

            int32 GlobalBotX = (ChunkKeys[idx].X * CSize) + step;
            int32 GlobalBotY = (ChunkKeys[idx].Y * CSize) + CSize;
            const FCellStaticData* RealBotS = nullptr; const FCellDynamicData* RealBotD = nullptr;
            if (Manager->GetCellStaticGlobalPtr(GlobalBotX, GlobalBotY, RealBotS) && Manager->GetCellDynamicGlobalPtr(GlobalBotX, GlobalBotY, RealBotD)) {
                Chunk.StaticCells[step + CSize * Manager->ChunkSize] = *RealBotS;
                Chunk.DynamicCells[step + CSize * Manager->ChunkSize] = *RealBotD;
            }
        }
        const FCellStaticData* RealCornerS = nullptr; const FCellDynamicData* RealCornerD = nullptr;
        if (Manager->GetCellStaticGlobalPtr((ChunkKeys[idx].X * CSize) + CSize, (ChunkKeys[idx].Y * CSize) + CSize, RealCornerS) &&
            Manager->GetCellDynamicGlobalPtr((ChunkKeys[idx].X * CSize) + CSize, (ChunkKeys[idx].Y * CSize) + CSize, RealCornerD)) {
            Chunk.StaticCells[CSize + CSize * Manager->ChunkSize] = *RealCornerS;
            Chunk.DynamicCells[CSize + CSize * Manager->ChunkSize] = *RealCornerD;
        }

        if (Chunk.AccumulatedTerrainChange > 1.5f) {
            bTerrainDirty = true;
        }

        if (bTerrainDirty) SafeFlags[idx] |= EChunkVisualDirty::Terrain;
        if (bCloudDirty) SafeFlags[idx] |= EChunkVisualDirty::Cloud;
        });

    for (int32 idx = 0; idx < ChunkKeys.Num(); idx++) {
        FChunkData& Chunk = WorldChunks[ChunkKeys[idx]];
        for (int i = 0; i < Chunk.DynamicCells.Num(); i++) {
            Chunk.DynamicCells[i].Lava = Chunk.DynamicCells[i].LavaBuffer;
        }
        if (SafeFlags[idx] != 0) {
            Manager->RegisterVisualChange(ChunkKeys[idx], SafeFlags[idx]);
        }
    }

    for (const TPair<FIntPoint, float>& EQ : PendingEarthquakes) {
        float Radius = 2000.0f + (EQ.Value * 1500.0f);
        FVector2D EpicenterLoc(EQ.Key.X * (Manager->ChunkSize - 1) * 50.0f, EQ.Key.Y * (Manager->ChunkSize - 1) * 50.0f);

        if (Manager->DisasterModule) {
            FDisasterWarning Warning;
            Warning.Type = EDisasterType::Earthquake;
            Warning.Epicenter = EpicenterLoc;
            Warning.Severity = EQ.Value;
            Warning.Radius = Radius;
            Warning.DaysToImpact = FMath::FRandRange(1.0f, 4.0f);
            Manager->DisasterModule->ActiveWarnings.Add(Warning);
        }

        if (Manager->HistoryModule) {
            FString Desc = FString::Printf(TEXT("Senzory a zvíøata hlásí masivní tenzi v podloží. Oèekává se drtivé zemìtøesení v následujících dnech!"));
            Manager->HistoryModule->LogEvent(Manager->CurrentYear, Manager->CurrentDay, TEXT("Earthquake Warning"), TEXT("Disaster"), Desc, EpicenterLoc, -1);
        }
    }
}

bool UTectonicSystem::TryRaiseTerrain(ASimWorldManager* Manager, int32 GlobalX, int32 GlobalY, float Amount) {
    if (!Manager || !Manager->ManaModule) return false;
    if (!Manager->ManaModule->SpendMana(200.0f, TEXT("Zvednuti terenu"))) return false;

    FCellStaticData* SCell = nullptr; FCellDynamicData* DCell = nullptr; FIntPoint Coord;
    if (Manager->GetMutableCellGlobal(GlobalX, GlobalY, SCell, DCell, Coord)) {
        SCell->Elevation += Amount;
        Manager->RegisterVisualChange(Coord, EChunkVisualDirty::Terrain, true);
        return true;
    }
    return false;
}

bool UTectonicSystem::SuppressDisaster(ASimWorldManager* Manager, FIntPoint ChunkCoord) {
    if (!Manager || !Manager->ManaModule) return false;
    if (!Manager->ManaModule->SpendMana(500.0f, TEXT("Potlaceni katastrofy"))) return false;
    if (FChunkData* Chunk = Manager->WorldChunks.Find(ChunkCoord)) {
        Chunk->FaultStress = 0.0f;
        for (int i = 0; i < Chunk->StaticCells.Num(); i++) {
            Chunk->DynamicCells[i].MagmaPressure = 0.0f;
            if (Chunk->DynamicCells[i].EruptionDaysRemaining > 0.0f) {
                Chunk->DynamicCells[i].EruptionDaysRemaining = 0.0f;
                Chunk->DynamicCells[i].LavaBuffer = 0.0f;
            }
        }
        Manager->RegisterVisualChange(ChunkCoord, EChunkVisualDirty::Terrain | EChunkVisualDirty::Cloud, true);
        return true;
    }
    return false;
}

bool UTectonicSystem::EnhancedDisaster(ASimWorldManager* Manager, FIntPoint ChunkCoord, float ExtraIntensity) {
    if (!Manager || !Manager->ManaModule) return false;
    if (!Manager->ManaModule->SpendMana(300.0f, TEXT("Zesileni katastrofy"))) return false;
    if (FChunkData* Chunk = Manager->WorldChunks.Find(ChunkCoord)) {
        Chunk->FaultStress += ExtraIntensity * 50.0f;
        return true;
    }
    return false;
}