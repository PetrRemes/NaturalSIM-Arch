#include "TectonicSystem.h"
#include "SimWorldManager.h"
#include "CosmosSystem.h"
#include "ManaSystem.h"
#include "HistorySystem.h"
#include "DisasterSystem.h"
#include "Async/ParallelFor.h"

UTectonicSystem::UTectonicSystem() { PrimaryComponentTick.bCanEverTick = false; }
void UTectonicSystem::BeginPlay() { Super::BeginPlay(); }

// OPTIMALIZACE 3: Struktura Halo Bufferu (Ghost Cells) zamezující volání GetCellGlobalPtr uvnitø smyèek
struct FHaloBuffer {
    TArray<FCellData> TopEdge, BotEdge, LftEdge, RgtEdge;
    TArray<bool> HasT, HasB, HasL, HasR;
    FCellData TL, TR, BL, BR;
    bool HasTL = false, HasTR = false, HasBL = false, HasBR = false;
    int32 CSize = 0, CS = 0, BaseGX = 0, BaseGY = 0;

    void Fetch(ASimWorldManager* Manager, FIntPoint Coord, int32 InCSize, int32 InCS) {
        CSize = InCSize; CS = InCS;
        BaseGX = Coord.X * CSize; BaseGY = Coord.Y * CSize;
        TopEdge.SetNumUninitialized(CSize); BotEdge.SetNumUninitialized(CSize);
        LftEdge.SetNumUninitialized(CSize); RgtEdge.SetNumUninitialized(CSize);
        HasT.Init(false, CSize); HasB.Init(false, CSize); HasL.Init(false, CSize); HasR.Init(false, CSize);

        for (int x = 0; x < CSize; x++) {
            const FCellData* c;
            HasT[x] = Manager->GetCellGlobalPtr(BaseGX + x, BaseGY - 1, c); if (HasT[x]) TopEdge[x] = *c;
            HasB[x] = Manager->GetCellGlobalPtr(BaseGX + x, BaseGY + CSize, c); if (HasB[x]) BotEdge[x] = *c;
        }
        for (int y = 0; y < CSize; y++) {
            const FCellData* c;
            HasL[y] = Manager->GetCellGlobalPtr(BaseGX - 1, BaseGY + y, c); if (HasL[y]) LftEdge[y] = *c;
            HasR[y] = Manager->GetCellGlobalPtr(BaseGX + CSize, BaseGY + y, c); if (HasR[y]) RgtEdge[y] = *c;
        }
        const FCellData* cor;
        HasTL = Manager->GetCellGlobalPtr(BaseGX - 1, BaseGY - 1, cor); if (HasTL) TL = *cor;
        HasTR = Manager->GetCellGlobalPtr(BaseGX + CSize, BaseGY - 1, cor); if (HasTR) TR = *cor;
        HasBL = Manager->GetCellGlobalPtr(BaseGX - 1, BaseGY + CSize, cor); if (HasBL) BL = *cor;
        HasBR = Manager->GetCellGlobalPtr(BaseGX + CSize, BaseGY + CSize, cor); if (HasBR) BR = *cor;
    }

    const FCellData* GetNeighbor(int32 lx, int32 ly, FChunkData& Chunk, ASimWorldManager* Manager) {
        if (lx >= 0 && lx < CSize && ly >= 0 && ly < CSize) return &Chunk.MicroCells[lx + ly * CS];
        if (lx >= -1 && lx <= CSize && ly >= -1 && ly <= CSize) {
            if (ly < 0) {
                if (lx < 0) return HasTL ? &TL : nullptr;
                if (lx == CSize) return HasTR ? &TR : nullptr;
                return HasT[lx] ? &TopEdge[lx] : nullptr;
            }
            if (ly == CSize) {
                if (lx < 0) return HasBL ? &BL : nullptr;
                if (lx == CSize) return HasBR ? &BR : nullptr;
                return HasB[lx] ? &BotEdge[lx] : nullptr;
            }
            if (lx < 0) return HasL[ly] ? &LftEdge[ly] : nullptr;
            if (lx == CSize) return HasR[ly] ? &RgtEdge[ly] : nullptr;
        }
        // Záložní vrstva (radius 2)
        const FCellData* Fallback = nullptr;
        Manager->GetCellGlobalPtr(BaseGX + lx, BaseGY + ly, Fallback);
        return Fallback;
    }
};

void UTectonicSystem::ProcessChunkTectonics(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    for (int32 i = 0; i < OutChunk.MicroCells.Num(); i++) {
        OutChunk.MicroCells[i].LavaBuffer = OutChunk.MicroCells[i].Lava;

        if (OutChunk.MicroCells[i].bIsVolcano && OutChunk.MicroCells[i].MagmaPressure == 0.0f) {
            OutChunk.MicroCells[i].MagmaPressure = FMath::FRandRange(100.0f, 800.0f);
        }

        OutChunk.MicroCells[i].EruptionDaysRemaining = 0.0f;
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

        // OPTIMALIZACE 3: Naètení hranic chunku do L1 Cache
        FHaloBuffer Halo;
        Halo.Fetch(Manager, ChunkKeys[idx], CSize, Manager->ChunkSize);

        auto GetLowestNeighbor = [&](int32 cx, int32 cy, int32 lx, int32 ly) -> FIntPoint {
            const FCellData* cCell = Halo.GetNeighbor(lx, ly, Chunk, Manager);
            if (!cCell) return FIntPoint(cx, cy);

            float lowestH = cCell->Elevation + cCell->Lava;
            FIntPoint bestP(cx, cy);

            for (int i = 0; i < 8; i++) {
                const FCellData* nCell = Halo.GetNeighbor(lx + Offsets[i][0], ly + Offsets[i][1], Chunk, Manager);
                if (nCell) {
                    float h = nCell->Elevation + nCell->Lava;
                    if (h < lowestH - 0.05f) {
                        lowestH = h;
                        bestP = FIntPoint(cx + Offsets[i][0], cy + Offsets[i][1]);
                    }
                }
            }
            return bestP;
            };

        // OPTIMALIZACE: Nested loops
        for (int32 Y = 0; Y < CSize; Y++) {
            for (int32 X = 0; X < CSize; X++) {
                int32 i = X + Y * Manager->ChunkSize;
                if (i >= Chunk.MicroCells.Num()) continue;

                FCellData& Cell = Chunk.MicroCells[i];
                int32 GlobalX = (ChunkKeys[idx].X * CSize) + X;
                int32 GlobalY = (ChunkKeys[idx].Y * CSize) + Y;

                if (Cell.bIsVolcano) {
                    Cell.MagmaPressure += (TidalMultiplier * Manager->VolcanicActivity * 3.0f * DeltaDays);

                    if (Cell.EruptionDaysRemaining > 0.0f) {
                        Cell.EruptionDaysRemaining -= DeltaDays;

                        float EruptionIntensity = FMath::Clamp(Cell.MagmaPressure / 300.0f, 0.5f, 10.0f);
                        float MagmaReleased = FMath::Min(Cell.MagmaPressure, 150.0f * EruptionIntensity * DeltaDays);
                        Cell.MagmaPressure -= MagmaReleased;

                        float LavaOutput = MagmaReleased * 0.9f;

                        Cell.Lava += LavaOutput;
                        Cell.DangerLevel = 1.0f;
                        Cell.Temperature += 150.0f;
                        Cell.AshDensityBuffer += (MagmaReleased * 0.1f) * DeltaDays;

                        if (Cell.EruptionDaysRemaining <= 0.0f || Cell.MagmaPressure <= 0.0f) {
                            Cell.EruptionDaysRemaining = 0.0f;
                        }
                        bTerrainDirty = true;
                        bCloudDirty = true;
                    }
                    else if (Cell.MagmaPressure > 1000.0f && FMath::FRand() < 0.05f) {
                        Cell.EruptionDaysRemaining = FMath::FRandRange(10.0f, 30.0f);
                        bTerrainDirty = true;
                    }
                }

                float MyLavaDelta = 0.0f;
                float MyHead = Cell.Elevation + Cell.Lava;

                FIntPoint TargetFlow = GetLowestNeighbor(GlobalX, GlobalY, X, Y);

                if (TargetFlow.X != GlobalX || TargetFlow.Y != GlobalY) {
                    int32 flowLx = X + (TargetFlow.X - GlobalX);
                    int32 flowLy = Y + (TargetFlow.Y - GlobalY);
                    const FCellData* TargetCell = Halo.GetNeighbor(flowLx, flowLy, Chunk, Manager);

                    if (TargetCell) {
                        float TargetHead = TargetCell->Elevation + TargetCell->Lava;
                        float Diff = MyHead - TargetHead;

                        float Transfer = FMath::Min(Cell.Lava, Diff * 5.0f * DeltaDays);
                        Transfer = FMath::Min(Transfer, Cell.Lava * 0.9f);
                        MyLavaDelta -= Transfer;
                    }
                }

                for (int32 n = 0; n < 8; n++) {
                    int32 nx = GlobalX + Offsets[n][0];
                    int32 ny = GlobalY + Offsets[n][1];
                    int32 nlx = X + Offsets[n][0];
                    int32 nly = Y + Offsets[n][1];

                    const FCellData* NCell = Halo.GetNeighbor(nlx, nly, Chunk, Manager);

                    if (NCell && NCell->Lava > 0.0f) {
                        FIntPoint NeighborTarget = GetLowestNeighbor(nx, ny, nlx, nly);
                        if (NeighborTarget.X == GlobalX && NeighborTarget.Y == GlobalY) {
                            float nHead = NCell->Elevation + NCell->Lava;
                            float Diff = nHead - MyHead;
                            float Transfer = FMath::Min(NCell->Lava, Diff * 5.0f * DeltaDays);
                            Transfer = FMath::Min(Transfer, NCell->Lava * 0.9f);
                            MyLavaDelta += Transfer;
                        }
                    }
                }
                Cell.LavaBuffer = FMath::Max(0.0f, Cell.Lava + MyLavaDelta);
            }
        }

        for (int32 Y = 0; Y < CSize; Y++) {
            for (int32 X = 0; X < CSize; X++) {
                int32 i = X + Y * Manager->ChunkSize;
                if (i >= Chunk.MicroCells.Num()) continue;

                FCellData& Cell = Chunk.MicroCells[i];

                if (Cell.LavaBuffer > 0.0f) {
                    float CoolingRate = FMath::Lerp(2.0f, 0.01f, FMath::Clamp(Cell.LavaBuffer / 20.0f, 0.0f, 1.0f));
                    float Cooling = FMath::Min(Cell.LavaBuffer, CoolingRate * DeltaDays);

                    Cell.LavaBuffer -= Cooling;

                    float HardenedRock = Cooling * 0.8f;
                    Cell.Elevation += HardenedRock;
                    Chunk.AccumulatedTerrainChange += HardenedRock;

                    if (Cell.LavaBuffer > 5.0f && Cell.Bedrock != EBedrockType::Rock) {
                        float Melting = 1.5f * DeltaDays;
                        Cell.Elevation -= Melting;
                        Chunk.AccumulatedTerrainChange += Melting;
                    }

                    Cell.SoilFertility = FMath::Min(1.0f, Cell.SoilFertility + Cooling * 0.05f);
                    Cell.MineralOre += Cooling * 50.0f;

                    if (Cell.LavaBuffer > 0.1f) {
                        if (Cell.FloraDensity > 0.0f || Cell.TreeType != ETreeType::None) {
                            Cell.FireIntensity = 1.0f;
                            bCloudDirty = true;
                        }
                        Cell.SurfaceWater = 0.0f;
                        Cell.Bedrock = EBedrockType::Rock;
                    }
                }
            }
        }

        for (int32 step = 0; step < Manager->ChunkSize; step++) {
            int32 GlobalRightX = (ChunkKeys[idx].X * CSize) + CSize;
            int32 GlobalRightY = (ChunkKeys[idx].Y * CSize) + step;
            const FCellData* RealRight = nullptr;
            if (Manager->GetCellGlobalPtr(GlobalRightX, GlobalRightY, RealRight)) Chunk.MicroCells[CSize + step * Manager->ChunkSize] = *RealRight;

            int32 GlobalBotX = (ChunkKeys[idx].X * CSize) + step;
            int32 GlobalBotY = (ChunkKeys[idx].Y * CSize) + CSize;
            const FCellData* RealBot = nullptr;
            if (Manager->GetCellGlobalPtr(GlobalBotX, GlobalBotY, RealBot)) Chunk.MicroCells[step + CSize * Manager->ChunkSize] = *RealBot;
        }
        const FCellData* RealCorner = nullptr;
        if (Manager->GetCellGlobalPtr((ChunkKeys[idx].X * CSize) + CSize, (ChunkKeys[idx].Y * CSize) + CSize, RealCorner)) {
            Chunk.MicroCells[CSize + CSize * Manager->ChunkSize] = *RealCorner;
        }

        if (Chunk.AccumulatedTerrainChange > 1.5f) {
            bTerrainDirty = true;
        }

        if (bTerrainDirty) SafeFlags[idx] |= EChunkVisualDirty::Terrain;
        if (bCloudDirty) SafeFlags[idx] |= EChunkVisualDirty::Cloud;
        });

    for (int32 idx = 0; idx < ChunkKeys.Num(); idx++) {
        FChunkData& Chunk = WorldChunks[ChunkKeys[idx]];
        for (int i = 0; i < Chunk.MicroCells.Num(); i++) {
            Chunk.MicroCells[i].Lava = Chunk.MicroCells[i].LavaBuffer;
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
    FCellData* Cell = nullptr; FIntPoint Coord;
    if (Manager->GetMutableCellGlobal(GlobalX, GlobalY, Cell, Coord)) {
        Cell->Elevation += Amount;
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
        for (int i = 0; i < Chunk->MicroCells.Num(); i++) {
            Chunk->MicroCells[i].MagmaPressure = 0.0f;
            if (Chunk->MicroCells[i].EruptionDaysRemaining > 0.0f) {
                Chunk->MicroCells[i].EruptionDaysRemaining = 0.0f;
                Chunk->MicroCells[i].LavaBuffer = 0.0f;
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