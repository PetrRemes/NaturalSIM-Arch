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

// OPTIMALIZACE 4: FHaloBuffer aktualizován pro 2 nezávislá SOA pole
struct FHaloBuffer {
    TArray<FCellStaticData> TopEdgeS, BotEdgeS, LftEdgeS, RgtEdgeS;
    TArray<FCellDynamicData> TopEdgeD, BotEdgeD, LftEdgeD, RgtEdgeD;
    TArray<bool> HasT, HasB, HasL, HasR;
    FCellStaticData TLS, TRS, BLS, BRS;
    FCellDynamicData TLD, TRD, BLD, BRD;
    bool HasTL = false, HasTR = false, HasBL = false, HasBR = false;
    int32 CSize = 0, CS = 0, BaseGX = 0, BaseGY = 0;

    void Fetch(ASimWorldManager* Manager, FIntPoint Coord, int32 InCSize, int32 InCS) {
        CSize = InCSize; CS = InCS;
        BaseGX = Coord.X * CSize; BaseGY = Coord.Y * CSize;
        TopEdgeS.SetNumUninitialized(CSize); BotEdgeS.SetNumUninitialized(CSize);
        LftEdgeS.SetNumUninitialized(CSize); RgtEdgeS.SetNumUninitialized(CSize);
        TopEdgeD.SetNumUninitialized(CSize); BotEdgeD.SetNumUninitialized(CSize);
        LftEdgeD.SetNumUninitialized(CSize); RgtEdgeD.SetNumUninitialized(CSize);
        HasT.Init(false, CSize); HasB.Init(false, CSize); HasL.Init(false, CSize); HasR.Init(false, CSize);

        for (int x = 0; x < CSize; x++) {
            HasT[x] = Manager->GetCellGlobal(BaseGX + x, BaseGY - 1, TopEdgeS[x], TopEdgeD[x]);
            HasB[x] = Manager->GetCellGlobal(BaseGX + x, BaseGY + CSize, BotEdgeS[x], BotEdgeD[x]);
        }
        for (int y = 0; y < CSize; y++) {
            HasL[y] = Manager->GetCellGlobal(BaseGX - 1, BaseGY + y, LftEdgeS[y], LftEdgeD[y]);
            HasR[y] = Manager->GetCellGlobal(BaseGX + CSize, BaseGY + y, RgtEdgeS[y], RgtEdgeD[y]);
        }
        HasTL = Manager->GetCellGlobal(BaseGX - 1, BaseGY - 1, TLS, TLD);
        HasTR = Manager->GetCellGlobal(BaseGX + CSize, BaseGY - 1, TRS, TRD);
        HasBL = Manager->GetCellGlobal(BaseGX - 1, BaseGY + CSize, BLS, BLD);
        HasBR = Manager->GetCellGlobal(BaseGX + CSize, BaseGY + CSize, BRS, BRD);
    }

    void GetNeighbor(int32 lx, int32 ly, FChunkData& Chunk, ASimWorldManager* Manager, const FCellStaticData*& OutS, const FCellDynamicData*& OutD) {
        OutS = nullptr; OutD = nullptr;
        if (lx >= 0 && lx < CSize && ly >= 0 && ly < CSize) {
            int32 Idx = lx + ly * CS;
            OutS = &Chunk.StaticCells[Idx];
            OutD = &Chunk.DynamicCells[Idx];
            return;
        }
        if (lx >= -1 && lx <= CSize && ly >= -1 && ly <= CSize) {
            if (ly < 0) {
                if (lx < 0) { if (HasTL) { OutS = &TLS; OutD = &TLD; } return; }
                if (lx == CSize) { if (HasTR) { OutS = &TRS; OutD = &TRD; } return; }
                if (HasT[lx]) { OutS = &TopEdgeS[lx]; OutD = &TopEdgeD[lx]; } return;
            }
            if (ly == CSize) {
                if (lx < 0) { if (HasBL) { OutS = &BLS; OutD = &BLD; } return; }
                if (lx == CSize) { if (HasBR) { OutS = &BRS; OutD = &BRD; } return; }
                if (HasB[lx]) { OutS = &BotEdgeS[lx]; OutD = &BotEdgeD[lx]; } return;
            }
            if (lx < 0) { if (HasL[ly]) { OutS = &LftEdgeS[ly]; OutD = &LftEdgeD[ly]; } return; }
            if (lx == CSize) { if (HasR[ly]) { OutS = &RgtEdgeS[ly]; OutD = &RgtEdgeD[ly]; } return; }
        }
        FIntPoint DummyCoord;
        FCellStaticData* S = nullptr; FCellDynamicData* D = nullptr;
        Manager->GetMutableCellGlobal(BaseGX + lx, BaseGY + ly, S, D, DummyCoord);
        OutS = S; OutD = D;
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

        FHaloBuffer Halo;
        Halo.Fetch(Manager, ChunkKeys[idx], CSize, Manager->ChunkSize);

        auto GetLowestNeighbor = [&](int32 cx, int32 cy, int32 lx, int32 ly) -> FIntPoint {
            const FCellStaticData* cCellS = nullptr; const FCellDynamicData* cCellD = nullptr;
            Halo.GetNeighbor(lx, ly, Chunk, Manager, cCellS, cCellD);
            if (!cCellS || !cCellD) return FIntPoint(cx, cy);

            float lowestH = cCellS->Elevation + cCellD->Lava;
            FIntPoint bestP(cx, cy);

            for (int i = 0; i < 8; i++) {
                const FCellStaticData* nCellS = nullptr; const FCellDynamicData* nCellD = nullptr;
                Halo.GetNeighbor(lx + Offsets[i][0], ly + Offsets[i][1], Chunk, Manager, nCellS, nCellD);
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

                FIntPoint TargetFlow = GetLowestNeighbor(GlobalX, GlobalY, X, Y);

                if (TargetFlow.X != GlobalX || TargetFlow.Y != GlobalY) {
                    int32 flowLx = X + (TargetFlow.X - GlobalX);
                    int32 flowLy = Y + (TargetFlow.Y - GlobalY);

                    const FCellStaticData* TargetCellS = nullptr; const FCellDynamicData* TargetCellD = nullptr;
                    Halo.GetNeighbor(flowLx, flowLy, Chunk, Manager, TargetCellS, TargetCellD);

                    if (TargetCellS && TargetCellD) {
                        float TargetHead = TargetCellS->Elevation + TargetCellD->Lava;
                        float Diff = MyHead - TargetHead;

                        float Transfer = FMath::Min(DCell.Lava, Diff * 5.0f * DeltaDays);
                        Transfer = FMath::Min(Transfer, DCell.Lava * 0.9f);
                        MyLavaDelta -= Transfer;
                    }
                }

                for (int32 n = 0; n < 8; n++) {
                    int32 nx = GlobalX + Offsets[n][0];
                    int32 ny = GlobalY + Offsets[n][1];
                    int32 nlx = X + Offsets[n][0];
                    int32 nly = Y + Offsets[n][1];

                    const FCellStaticData* NCellS = nullptr; const FCellDynamicData* NCellD = nullptr;
                    Halo.GetNeighbor(nlx, nly, Chunk, Manager, NCellS, NCellD);

                    if (NCellS && NCellD && NCellD->Lava > 0.0f) {
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