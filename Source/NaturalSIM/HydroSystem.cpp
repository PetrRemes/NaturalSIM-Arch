#include "HydroSystem.h"
#include "SimWorldManager.h"
#include "CosmosSystem.h"
#include "ManaSystem.h"
#include "Async/ParallelFor.h"
#include "NoiseUtils.h"

UHydroSystem::UHydroSystem() { PrimaryComponentTick.bCanEverTick = false; }
void UHydroSystem::BeginPlay() { Super::BeginPlay(); }

// OPTIMALIZACE 4: FHaloBuffer pøepsán pro práci se Static a Dynamic daty
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

static float GetRiverElev(float GlobalX, float GlobalY, const FChunkGenerationParameters& Params)
{
    if (Params.GlobalTerrainCache.IsValid() && Params.TotalWorldCellsX > 0 && Params.TotalWorldCellsY > 0) {
        int32 X = FMath::Clamp(FMath::RoundToInt(GlobalX), 0, Params.TotalWorldCellsX - 1);
        int32 Y = FMath::Clamp(FMath::RoundToInt(GlobalY), 0, Params.TotalWorldCellsY - 1);
        return Params.GlobalTerrainCache.Get()->GetData()[Y * Params.TotalWorldCellsX + X].Elevation;
    }
    return Params.SeaLevel;
}

struct FRiverHead { FVector2D Pos; FVector2D Momentum; float Volume; int32 Life; };

void UHydroSystem::ProcessChunkWater(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    float SeaLevel = Params.SeaLevel;
    int32 ChunkSize = Params.ChunkSize;

    ParallelFor(ChunkSize, [&](int32 Y) {
        for (int32 X = 0; X < ChunkSize; X++) {
            int32 i = X + Y * ChunkSize;
            if (i >= OutChunk.StaticCells.Num()) continue;

            FCellStaticData& SCell = OutChunk.StaticCells[i];
            FCellDynamicData& DCell = OutChunk.DynamicCells[i];

            float GlobalX = (ChunkCoord.X * (ChunkSize - 1)) + X;
            float GlobalY = (ChunkCoord.Y * (ChunkSize - 1)) + Y;

            if (Params.bIsFullGeneration) {
                DCell.SurfaceWater = 0.0f;
                DCell.WaterInflowBuffer = 0.0f;
                DCell.RiverDischarge = 0.0f;
                DCell.RiverDischargeBuffer = 0.0f;
                DCell.FlowPersistenceBuffer = 0.0f;
                SCell.ChannelWidth = 0.0f;
                SCell.ChannelDepth = 0.0f;
                SCell.BankHeight = 0.0f;
                SCell.WaterType = EWaterType::None;
                SCell.bIsSpring = false;
                SCell.SpringStrength = 0.0f;
                SCell.FlowDirectionGlobalX = -1;
                SCell.FlowDirectionGlobalY = -1;
                DCell.DepositedSediment = 0.0f;
                DCell.SoilStability = 0.0f;
                DCell.WetlandScore = 0.0f;

                if (SCell.Elevation <= SeaLevel) {
                    SCell.WaterType = EWaterType::Ocean;
                    DCell.SurfaceWater = SeaLevel - SCell.Elevation;
                }
                else {
                    DCell.GroundWater = (DCell.Rainfall * 100.0f);

                    float SpringNoise = FMath::PerlinNoise2D(FVector2D(GlobalX * 0.1f, GlobalY * 0.1f));
                    if (SCell.Elevation > SeaLevel + 200.0f && DCell.GroundWater > 50.0f && SpringNoise > 0.85f) {
                        SCell.bIsSpring = true;
                        SCell.SpringStrength = FMath::FRandRange(0.5f, 2.0f);
                        SCell.WaterType = EWaterType::Spring;
                    }
                }
            }
            else {
                if (SCell.Elevation > SeaLevel) {
                    DCell.GroundWater = FMath::Clamp(DCell.GroundWater + (DCell.Rainfall * Params.RainfallMultiplier), 0.0f, 100.0f);
                }
            }
        }
        });

    if (!Params.bIsFullGeneration) return;

    int32 SearchRadius = 4;
    int32 StartX = FMath::Max(0, FMath::RoundToInt(ChunkCoord.X) - SearchRadius);
    int32 EndX = FMath::Min(Params.WorldSizeInChunksX - 1, FMath::RoundToInt(ChunkCoord.X) + SearchRadius);
    int32 StartY = FMath::Max(0, FMath::RoundToInt(ChunkCoord.Y) - SearchRadius);
    int32 EndY = FMath::Min(Params.WorldSizeInChunksY - 1, FMath::RoundToInt(ChunkCoord.Y) + SearchRadius);

    TArray<FRiverHead> ActiveRivers;
    ActiveRivers.Reserve(100);

    for (int32 cx = StartX; cx <= EndX; cx++) {
        for (int32 cy = StartY; cy <= EndY; cy++) {
            FRandomStream RiverStream(Params.MapSeed + (cx * 101) + (cy * 37));
            int32 Spawned = 0;
            int32 TargetRivers = FMath::RoundToInt(Params.RiverSpawnMultiplier);

            for (int32 attempt = 0; attempt < 100; attempt++) {
                int32 rx = (cx * (ChunkSize - 1)) + RiverStream.RandRange(0, ChunkSize - 2);
                int32 ry = (cy * (ChunkSize - 1)) + RiverStream.RandRange(0, ChunkSize - 2);
                float Elev = GetRiverElev(rx, ry, Params);

                if (Elev > SeaLevel + 250.0f && Elev < SeaLevel + 2500.0f) {
                    FRiverHead NewRiver;
                    NewRiver.Pos = FVector2D(rx, ry);
                    NewRiver.Momentum = FVector2D(0.0f, 0.0f);

                    bool bIsMajor = (RiverStream.FRand() < 0.1f);
                    NewRiver.Volume = bIsMajor ? 2.5f : 0.5f;
                    NewRiver.Life = bIsMajor ? 4000 : 1000;

                    ActiveRivers.Add(NewRiver);

                    int32 localX = rx - (FMath::RoundToInt(ChunkCoord.X) * (ChunkSize - 1));
                    int32 localY = ry - (FMath::RoundToInt(ChunkCoord.Y) * (ChunkSize - 1));
                    if (localX >= 0 && localX < ChunkSize && localY >= 0 && localY < ChunkSize) {
                        int32 Idx = localX + localY * ChunkSize;
                        OutChunk.StaticCells[Idx].bIsSpring = true;
                        OutChunk.StaticCells[Idx].SpringStrength = NewRiver.Volume * 1.5f;
                        OutChunk.StaticCells[Idx].WaterType = EWaterType::Spring;
                    }

                    Spawned++;
                    if (Spawned >= TargetRivers) break;
                }
            }
        }
    }

    while (ActiveRivers.Num() > 0)
    {
        FRiverHead River = ActiveRivers.Pop(EAllowShrinking::No);

        while (River.Life > 0)
        {
            int32 cX = FMath::RoundToInt(River.Pos.X);
            int32 cY = FMath::RoundToInt(River.Pos.Y);
            float CurrElev = GetRiverElev(cX, cY, Params);

            if (CurrElev <= SeaLevel) break;

            int32 MyChunkX = cX / (ChunkSize - 1);
            int32 MyChunkY = cY / (ChunkSize - 1);

            if (FMath::Abs(MyChunkX - ChunkCoord.X) <= 1 && FMath::Abs(MyChunkY - ChunkCoord.Y) <= 1) {

                float ElevAboveSea = FMath::Max(0.0f, CurrElev - SeaLevel);
                float LowlandAlpha = FMath::Clamp(1.0f - (ElevAboveSea / 300.0f), 0.0f, 1.0f);

                float BaseCarveRadius = (River.Volume > 2.0f) ? 1.0f : 0.0f;
                if (LowlandAlpha > 0.5f) {
                    BaseCarveRadius += (LowlandAlpha - 0.5f) * 1.0f;
                }
                int32 PaintRange = FMath::CeilToInt(BaseCarveRadius + 1.0f);

                for (int by = -PaintRange; by <= PaintRange; by++) {
                    for (int bx = -PaintRange; bx <= PaintRange; bx++) {
                        int32 paintX = cX + bx;
                        int32 paintY = cY + by;

                        int32 lx = paintX - (FMath::RoundToInt(ChunkCoord.X) * (ChunkSize - 1));
                        int32 ly = paintY - (FMath::RoundToInt(ChunkCoord.Y) * (ChunkSize - 1));

                        if (lx >= 0 && lx < ChunkSize && ly >= 0 && ly < ChunkSize) {
                            int32 Idx = lx + ly * ChunkSize;
                            float centerDist = FMath::Sqrt((float)(bx * bx + by * by));

                            if (centerDist > BaseCarveRadius + 0.5f) continue;

                            float waterAmount = FMath::Max(0.0f, (BaseCarveRadius + 0.5f - centerDist) * 5.0f * River.Volume);
                            OutChunk.DynamicCells[Idx].SurfaceWater += waterAmount;

                            if (OutChunk.StaticCells[Idx].Elevation > SeaLevel) {
                                float carveAlpha = FMath::Clamp(1.0f - (centerDist / (BaseCarveRadius + 0.5f)), 0.0f, 1.0f);
                                float CarveDepth = FMath::Lerp(12.0f, 4.0f, LowlandAlpha);
                                float targetRiverZ = FMath::Max(SeaLevel, CurrElev - CarveDepth);
                                float originalZ = OutChunk.StaticCells[Idx].Elevation;

                                if (originalZ > targetRiverZ) {
                                    float profileExp = FMath::Lerp(1.5f, 3.0f, LowlandAlpha);
                                    float finalAlpha = FMath::Pow(carveAlpha, profileExp) * Params.ErosionMultiplier;
                                    OutChunk.StaticCells[Idx].Elevation = FMath::Lerp(originalZ, targetRiverZ, finalAlpha);
                                }
                            }
                        }
                    }
                }
            }

            if (CurrElev < SeaLevel + 120.0f && River.Volume > 0.6f && River.Life % 20 == 0) {
                float DeltaNoise = FMath::PerlinNoise2D(FVector2D(cX * 0.1f, cY * 0.1f));
                if (DeltaNoise > 0.3f) {
                    FRiverHead Branch = River;
                    Branch.Volume *= 0.5f;
                    River.Volume *= 0.5f;
                    Branch.Momentum = FVector2D(-River.Momentum.Y, River.Momentum.X).GetSafeNormal() + (River.Momentum * 0.5f);
                    ActiveRivers.Push(Branch);
                }
            }

            int32 BestX = cX; int32 BestY = cY;
            float BestScore = 999999.0f;

            FVector2D Forward = River.Momentum.GetSafeNormal();
            if (Forward.IsNearlyZero()) Forward = FVector2D(1.0f, 1.0f).GetSafeNormal();
            FVector2D Right(-Forward.Y, Forward.X);

            float SwayNoise = FMath::PerlinNoise2D(FVector2D(cX * 0.05f, cY * 0.05f));
            float SwayAmount = SwayNoise * 8.0f * Params.RiverMeanderStrength;

            for (int32 dy = -1; dy <= 1; dy++) {
                for (int32 dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int32 nx = cX + dx; int32 ny = cY + dy;

                    float nElev = GetRiverElev(nx, ny, Params);
                    if (nElev > CurrElev + 0.5f) continue;

                    FVector2D Dir(dx, dy); Dir.Normalize();
                    float Alignment = FVector2D::DotProduct(Forward, Dir);
                    if (Alignment < -0.3f) continue;

                    float TurnPenalty = (1.0f - Alignment) * 4.0f;
                    float Sway = FVector2D::DotProduct(Right, Dir) * SwayAmount;

                    float Score = nElev + TurnPenalty - Sway;
                    if (Score < BestScore) {
                        BestScore = Score; BestX = nx; BestY = ny;
                    }
                }
            }

            bool bFoundSpill = false;

            if (BestX == cX && BestY == cY) {

                for (int32 r = 2; r <= 25; r++) {
                    float MinSpillElev = 999999.0f;
                    int32 sX = -1, sY = -1;

                    for (int32 dy = -r; dy <= r; dy++) {
                        for (int32 dx = -r; dx <= r; dx++) {
                            if (FMath::Abs(dx) != r && FMath::Abs(dy) != r) continue;
                            int32 nx = cX + dx; int32 vy = cY + dy;
                            float nElev = GetRiverElev(nx, vy, Params);
                            if (nElev < MinSpillElev) {
                                MinSpillElev = nElev; sX = nx; sY = vy;
                            }
                        }
                    }

                    if (sX != -1 && MinSpillElev < CurrElev) {
                        BestX = sX; BestY = sY; bFoundSpill = true;

                        if (FMath::Abs(MyChunkX - ChunkCoord.X) <= 1 && FMath::Abs(MyChunkY - ChunkCoord.Y) <= 1) {

                            int32 smoothR = r + 6;

                            for (int lY = -smoothR; lY <= smoothR; lY++) {
                                for (int lX = -smoothR; lX <= smoothR; lX++) {
                                    float dist = FMath::Sqrt((float)(lX * lX + lY * lY));
                                    float lakeNoise = FMath::PerlinNoise2D(FVector2D((cX + lX) * 0.2f, (cY + lY) * 0.2f));
                                    float effRadius = r * (0.8f + lakeNoise * 0.4f);
                                    float shoreRadius = effRadius + 6.0f;

                                    if (dist <= shoreRadius) {
                                        int32 px = cX + lX;
                                        int32 py = cY + lY;

                                        int32 locX = px - (FMath::RoundToInt(ChunkCoord.X) * (ChunkSize - 1));
                                        int32 locY = py - (FMath::RoundToInt(ChunkCoord.Y) * (ChunkSize - 1));

                                        if (locX >= 0 && locX < ChunkSize && locY >= 0 && locY < ChunkSize) {
                                            int32 Idx = locX + locY * ChunkSize;
                                            float originalZ = OutChunk.StaticCells[Idx].Elevation;

                                            if (dist <= effRadius) {
                                                OutChunk.DynamicCells[Idx].SurfaceWater += 15.0f;
                                                float depthAlpha = dist / effRadius;
                                                float lakeBedZ = FMath::Max(SeaLevel, CurrElev - FMath::Lerp(12.0f, 2.0f, depthAlpha));
                                                OutChunk.StaticCells[Idx].Elevation = FMath::Min(originalZ, lakeBedZ);
                                            }
                                            else {
                                                float shoreAlpha = (dist - effRadius) / (shoreRadius - effRadius);
                                                float blend = FMath::SmoothStep(0.0f, 1.0f, shoreAlpha);
                                                OutChunk.StaticCells[Idx].Elevation = FMath::Min(originalZ, FMath::Lerp(CurrElev, originalZ, blend));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        break;
                    }
                }
                if (!bFoundSpill) break;
            }

            FVector2D FallDir(BestX - cX, BestY - cY); FallDir.Normalize();

            if (bFoundSpill) {
                River.Momentum = FallDir;
            }
            else {
                FVector2D Variation(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)); Variation.Normalize();
                River.Momentum = (River.Momentum * 0.7f) + (FallDir * 0.2f) + (Variation * 0.1f);
            }

            River.Momentum.Normalize();
            River.Pos = FVector2D(BestX, BestY);
            River.Life--;
        }
    }
}

void UHydroSystem::ProcessHydroSlice(const TArray<FIntPoint>& ChunkKeys, TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, int32 StartIdx, int32 EndIdx, bool bRunGeomorphology, float DeltaDays)
{
    if (!Manager) return;
    int32 Offsets[8][2] = { {0,1}, {1,0}, {0,-1}, {-1,0}, {1,1}, {-1,-1}, {1,-1}, {-1,1} };
    int32 CSize = Manager->ChunkSize - 1;

    float TidalMultiplier = 1.0f;
    if (Manager->CosmosModule) {
        TidalMultiplier = Manager->CosmosModule->CurrentState.GlobalTidalMultiplier;
    }

    int32 GlobalStart = 0;
    int32 GlobalEnd = ChunkKeys.Num();

    ParallelFor(GlobalEnd, [&](int32 idx) {
        FIntPoint Coord = ChunkKeys[idx];
        if (!WorldChunks.Contains(Coord)) return;
        FChunkData& Chunk = WorldChunks[Coord];

        FHaloBuffer Halo;
        Halo.Fetch(Manager, Coord, CSize, Manager->ChunkSize);

        for (int32 Y = 0; Y < CSize; Y++) {
            for (int32 X = 0; X < CSize; X++) {
                int32 i = X + Y * Manager->ChunkSize;
                FCellStaticData& SCell = Chunk.StaticCells[i];
                FCellDynamicData& DCell = Chunk.DynamicCells[i];
                int32 GlobalX = (Coord.X * CSize) + X;
                int32 GlobalY = (Coord.Y * CSize) + Y;

                auto GetNeighbor = [&](int32 dx, int32 dy, const FCellStaticData*& OutS, const FCellDynamicData*& OutD) {
                    Halo.GetNeighbor(X + dx, Y + dy, Chunk, Manager, OutS, OutD);
                    };

                if (SCell.Elevation <= Manager->SeaLevel) {
                    SCell.WaterType = EWaterType::Ocean;
                    DCell.SurfaceWater = Manager->SeaLevel - SCell.Elevation;

                    float LowestHead = SCell.Elevation;
                    int32 LowestGlobalX = -1;
                    int32 LowestGlobalY = -1;

                    for (int32 n = 0; n < 8; n++) {
                        const FCellStaticData* NCellS = nullptr; const FCellDynamicData* NCellD = nullptr;
                        GetNeighbor(Offsets[n][0], Offsets[n][1], NCellS, NCellD);
                        if (NCellS && NCellS->Elevation < LowestHead) {
                            LowestHead = NCellS->Elevation;
                            LowestGlobalX = GlobalX + Offsets[n][0];
                            LowestGlobalY = GlobalY + Offsets[n][1];
                        }
                    }
                    SCell.FlowDirectionGlobalX = LowestGlobalX;
                    SCell.FlowDirectionGlobalY = LowestGlobalY;
                    DCell.WaterFlow = 1.0f;
                    continue;
                }

                float RechargeFactor = 0.4f;
                if (SCell.Bedrock == EBedrockType::Sand) RechargeFactor = 0.8f;
                else if (SCell.Bedrock == EBedrockType::Rock) RechargeFactor = 0.1f;

                float GroundwaterBaseLoss = 0.05f;
                float InfiltratedWater = DCell.Rainfall * RechargeFactor;
                float NewGroundWater = DCell.GroundWater + InfiltratedWater - GroundwaterBaseLoss;

                if (NewGroundWater > 100.0f) {
                    float ExcessWater = NewGroundWater - 100.0f;
                    DCell.SurfaceWater += ExcessWater;
                    DCell.GroundWater = 100.0f;
                }
                else {
                    DCell.GroundWater = FMath::Max(0.0f, NewGroundWater);
                }

                if (SCell.bIsSpring) {
                    SCell.WaterType = EWaterType::Spring;
                }
                else if (DCell.SurfaceWater > 0.5f && SCell.FlowDirectionGlobalX == -1) {
                    SCell.WaterType = EWaterType::Lake;
                }
                else if (DCell.RiverDischarge > 0.2f) {
                    SCell.WaterType = EWaterType::River;
                }
                else if (DCell.SurfaceWater > 0.05f) {
                    SCell.WaterType = EWaterType::Surface;
                }
                else {
                    SCell.WaterType = EWaterType::None;
                }

                if (DCell.SurfaceWater > 0.01f || SCell.bIsSpring) {
                    float MyHead = SCell.Elevation + DCell.SurfaceWater;
                    float LowestHead = MyHead;
                    int32 LowestGlobalX = -1;
                    int32 LowestGlobalY = -1;

                    for (int32 n = 0; n < 8; n++) {
                        const FCellStaticData* NCellS = nullptr; const FCellDynamicData* NCellD = nullptr;
                        GetNeighbor(Offsets[n][0], Offsets[n][1], NCellS, NCellD);
                        if (NCellS && NCellD) {
                            float nHead = NCellS->Elevation + NCellD->SurfaceWater;
                            if (nHead < LowestHead) {
                                LowestHead = nHead;
                                LowestGlobalX = GlobalX + Offsets[n][0];
                                LowestGlobalY = GlobalY + Offsets[n][1];
                            }
                        }
                    }

                    SCell.FlowDirectionGlobalX = LowestGlobalX;
                    SCell.FlowDirectionGlobalY = LowestGlobalY;
                }
                else {
                    SCell.FlowDirectionGlobalX = -1;
                    SCell.FlowDirectionGlobalY = -1;
                }
            }
        }
        });

    const float FlowCoefficient = 0.18f;

    ParallelFor(GlobalEnd, [&](int32 idx) {
        FIntPoint Coord = ChunkKeys[idx];
        if (!WorldChunks.Contains(Coord)) return;
        FChunkData& Chunk = WorldChunks[Coord];

        FHaloBuffer Halo;
        Halo.Fetch(Manager, Coord, CSize, Manager->ChunkSize);

        float LocalFlowCoefficient = Chunk.bGeomorphologyDirty ? 0.8f : FlowCoefficient;

        for (int32 Y = 0; Y < CSize; Y++) {
            for (int32 X = 0; X < CSize; X++) {
                int32 i = X + Y * Manager->ChunkSize;
                FCellStaticData& SCell = Chunk.StaticCells[i];
                FCellDynamicData& DCell = Chunk.DynamicCells[i];

                if (SCell.WaterType == EWaterType::Ocean) continue;

                int32 GlobalX = (Coord.X * CSize) + X;
                int32 GlobalY = (Coord.Y * CSize) + Y;

                auto GetNeighbor = [&](int32 dx, int32 dy, const FCellStaticData*& OutS, const FCellDynamicData*& OutD) {
                    Halo.GetNeighbor(X + dx, Y + dy, Chunk, Manager, OutS, OutD);
                    };

                float SpringInput = 0.0f;
                if (SCell.bIsSpring) {
                    float GroundFactor = FMath::Clamp(DCell.GroundWater / 100.0f, 0.25f, 1.0f);
                    SpringInput = SCell.SpringStrength * GroundFactor;
                    DCell.GroundWater = FMath::Max(0.0f, DCell.GroundWater - SpringInput);
                    DCell.SurfaceWater = FMath::Max(DCell.SurfaceWater, SpringInput);
                }

                float MyOutflow = 0.0f;
                if (SCell.FlowDirectionGlobalX != -1 && SCell.FlowDirectionGlobalY != -1) {
                    int32 FlowDX = SCell.FlowDirectionGlobalX - GlobalX;
                    int32 FlowDY = SCell.FlowDirectionGlobalY - GlobalY;

                    const FCellStaticData* TargetCellS = nullptr; const FCellDynamicData* TargetCellD = nullptr;
                    GetNeighbor(FlowDX, FlowDY, TargetCellS, TargetCellD);

                    if (TargetCellS && TargetCellD) {
                        float Diff = (SCell.Elevation + DCell.SurfaceWater) - (TargetCellS->Elevation + TargetCellD->SurfaceWater);
                        if (Diff > 0.0f) {
                            float MaxTransfer = Diff * 0.25f;
                            float DesiredTransfer = Diff * LocalFlowCoefficient;
                            if (SCell.WaterType == EWaterType::River || SCell.WaterType == EWaterType::Spring || DCell.RiverDischarge > 0.5f || Chunk.bGeomorphologyDirty) {
                                DesiredTransfer = Diff * (Chunk.bGeomorphologyDirty ? 0.8f : 0.25f);
                            }
                            MyOutflow = FMath::Min(DCell.SurfaceWater, FMath::Min(DesiredTransfer, MaxTransfer));
                        }
                    }
                }

                float MyInflow = 0.0f;
                for (int32 n = 0; n < 8; n++) {
                    const FCellStaticData* NCellS = nullptr; const FCellDynamicData* NCellD = nullptr;
                    GetNeighbor(Offsets[n][0], Offsets[n][1], NCellS, NCellD);

                    if (NCellS && NCellD) {
                        if (NCellS->WaterType != EWaterType::Ocean && NCellS->FlowDirectionGlobalX == GlobalX && NCellS->FlowDirectionGlobalY == GlobalY) {
                            float Diff = (NCellS->Elevation + NCellD->SurfaceWater) - (SCell.Elevation + DCell.SurfaceWater);
                            if (Diff > 0.0f) {
                                float MaxTransfer = Diff * 0.25f;
                                float DesiredTransfer = Diff * LocalFlowCoefficient;
                                if (NCellS->WaterType == EWaterType::River || NCellS->WaterType == EWaterType::Spring || NCellD->RiverDischarge > 0.5f || Chunk.bGeomorphologyDirty) {
                                    DesiredTransfer = Diff * (Chunk.bGeomorphologyDirty ? 0.8f : 0.25f);
                                }
                                MyInflow += FMath::Min(NCellD->SurfaceWater, FMath::Min(DesiredTransfer, MaxTransfer));
                            }
                        }
                    }
                }

                float NextWater = DCell.SurfaceWater - MyOutflow + MyInflow + SpringInput;
                NextWater = FMath::Max(0.0f, NextWater - 0.0005f);

                DCell.WaterInflowBuffer = NextWater;
                DCell.WaterFlow = FMath::Lerp(DCell.WaterFlow, MyOutflow, 0.15f);

                if (DCell.WaterFlow > 0.1f) {
                    DCell.FlowPersistenceBuffer = FMath::Min(1.0f, DCell.FlowPersistence + 0.01f);
                }
                else {
                    DCell.FlowPersistenceBuffer = FMath::Max(0.0f, DCell.FlowPersistence - 0.005f);
                }

                float TargetDischarge = MyOutflow + MyInflow;
                DCell.RiverDischargeBuffer = FMath::Lerp(DCell.RiverDischarge, TargetDischarge, 0.05f);

                if (FMath::Abs(DCell.SurfaceWater - NextWater) > 0.5f) {
                    Chunk.AccumulatedWaterChange += FMath::Abs(DCell.SurfaceWater - NextWater);
                }
            }
        }
        });

    ParallelFor(GlobalEnd, [&](int32 idx) {
        FIntPoint Coord = ChunkKeys[idx];
        if (!WorldChunks.Contains(Coord)) return;
        FChunkData& Chunk = WorldChunks[Coord];

        for (int32 Y = 0; Y < CSize; Y++) {
            for (int32 X = 0; X < CSize; X++) {
                int32 i = X + Y * Manager->ChunkSize;
                Chunk.DynamicCells[i].SurfaceWater = Chunk.DynamicCells[i].WaterInflowBuffer;
                Chunk.DynamicCells[i].RiverDischarge = Chunk.DynamicCells[i].RiverDischargeBuffer;
                Chunk.DynamicCells[i].FlowPersistence = Chunk.DynamicCells[i].FlowPersistenceBuffer;
                Chunk.DynamicCells[i].WaterInflowBuffer = 0.0f;
            }
        }

        if (!bRunGeomorphology && Chunk.bGeomorphologyDirty) {
            Chunk.bGeomorphologyDirty = false;
        }
        });

    if (bRunGeomorphology)
    {
        const float ErosionCoefficient = 0.002f;
        const float DepositionRate = 0.08f;

        TArray<uint8> SafeFlags;
        SafeFlags.Init(0, GlobalEnd);

        ParallelFor(GlobalEnd, [&](int32 idx) {
            FIntPoint Coord = ChunkKeys[idx];
            if (!WorldChunks.Contains(Coord)) return;
            FChunkData& Chunk = WorldChunks[Coord];

            FHaloBuffer Halo;
            Halo.Fetch(Manager, Coord, CSize, Manager->ChunkSize);

            for (int32 Y = 0; Y < CSize; Y++) {
                for (int32 X = 0; X < CSize; X++) {
                    int32 i = X + Y * Manager->ChunkSize;
                    FCellStaticData& SCell = Chunk.StaticCells[i];
                    FCellDynamicData& DCell = Chunk.DynamicCells[i];

                    int32 GlobalX = (Coord.X * CSize) + X; int32 GlobalY = (Coord.Y * CSize) + Y;

                    auto GetNeighbor = [&](int32 dx, int32 dy, const FCellStaticData*& OutS, const FCellDynamicData*& OutD) {
                        Halo.GetNeighbor(X + dx, Y + dy, Chunk, Manager, OutS, OutD);
                        };

                    if (SCell.WaterType == EWaterType::Ocean || SCell.Elevation <= Manager->SeaLevel) {
                        if (DCell.Sediment > 0.01f) {
                            float DepositRate = FMath::Clamp(2.0f - TidalMultiplier, 0.05f, 0.5f) * 0.15f;
                            float OceanDeposit = DCell.Sediment * DepositRate * DeltaDays;

                            float SpaceToSeaLevel = Manager->SeaLevel - SCell.Elevation;
                            float MaxAllowedDeposit = FMath::Max(0.0f, SpaceToSeaLevel + 0.15f);
                            float ActualDeposit = FMath::Min(OceanDeposit, MaxAllowedDeposit);

                            DCell.ElevationDelta += ActualDeposit;
                            DCell.DepositedSediment += ActualDeposit;
                            DCell.SedimentDelta -= ActualDeposit;
                            Chunk.AccumulatedTerrainChange += ActualDeposit;

                            if (SCell.Elevation + DCell.ElevationDelta > Manager->SeaLevel) {
                                SCell.WaterType = EWaterType::Surface;
                                SCell.Bedrock = EBedrockType::Sand;
                                SCell.SoilType = ESoilType::Sand;
                                SafeFlags[idx] |= EChunkVisualDirty::Flora | EChunkVisualDirty::Terrain;
                            }
                        }

                        float MySedimentOutflow = 0.0f;
                        if (SCell.FlowDirectionGlobalX != -1) {
                            MySedimentOutflow = FMath::Min(DCell.Sediment + DCell.SedimentDelta, DCell.WaterFlow * 0.9f * DeltaDays);
                        }
                        DCell.SedimentDelta -= MySedimentOutflow;
                        continue;
                    }

                    bool bIsFlooding = DCell.SurfaceWater > (SCell.ChannelDepth + SCell.BankHeight + 0.2f);
                    float MaterialResistance = (SCell.Bedrock == EBedrockType::Sand) ? 0.5f : ((SCell.Bedrock == EBedrockType::Rock) ? 50.0f : 1.0f);

                    if (DCell.SurfaceWater < 0.1f && !bIsFlooding) {
                        float BankErosion = 0.0f;
                        float BankDeposition = 0.0f;

                        for (int32 n = 0; n < 8; n++) {
                            int32 NX = GlobalX + Offsets[n][0]; int32 NY = GlobalY + Offsets[n][1];
                            const FCellStaticData* NCellS = nullptr; const FCellDynamicData* NCellD = nullptr;
                            GetNeighbor(Offsets[n][0], Offsets[n][1], NCellS, NCellD);

                            if (NCellS && NCellD && NCellD->RiverDischarge > 1.0f && NCellD->SurfaceWater > 0.1f) {
                                FVector2D DirToMe(-Offsets[n][0], -Offsets[n][1]); DirToMe.Normalize();
                                FVector2D NOutflow(0, 0);
                                if (NCellS->FlowDirectionGlobalX != -1) {
                                    NOutflow = FVector2D(NCellS->FlowDirectionGlobalX - NX, NCellS->FlowDirectionGlobalY - NY).GetSafeNormal();
                                }

                                if (FVector2D::DotProduct(NOutflow, DirToMe) < 0.5f) {
                                    FVector2D NInflowMom(0, 0);
                                    for (int32 nn = 0; nn < 8; nn++) {
                                        int32 relDX = Offsets[n][0] + Offsets[nn][0]; int32 relDY = Offsets[n][1] + Offsets[nn][1];
                                        const FCellStaticData* UpstreamS = nullptr; const FCellDynamicData* UpstreamD = nullptr;
                                        GetNeighbor(relDX, relDY, UpstreamS, UpstreamD);

                                        if (UpstreamS && UpstreamD && UpstreamS->FlowDirectionGlobalX == NX && UpstreamS->FlowDirectionGlobalY == NY) {
                                            NInflowMom += FVector2D(-Offsets[nn][0], -Offsets[nn][1]) * UpstreamD->WaterFlow;
                                        }
                                    }

                                    if (!NInflowMom.IsNearlyZero()) {
                                        NInflowMom.Normalize();
                                        float HitDot = FVector2D::DotProduct(NInflowMom, DirToMe);

                                        if (HitDot > 0.6f) BankErosion += NCellD->RiverDischarge * HitDot * 0.002f * DeltaDays * Manager->ErosionMultiplier;
                                        else if (HitDot < -0.4f && NCellD->Sediment > 0.1f) BankDeposition += NCellD->Sediment * 0.02f * DeltaDays;
                                    }
                                }
                            }
                        }

                        if (BankErosion > 0.0f) {
                            float ActualErode = FMath::Min(BankErosion / MaterialResistance, 0.05f * DeltaDays);
                            float MaxAllowed = FMath::Max(0.0f, SCell.Elevation - Manager->SeaLevel);
                            ActualErode = FMath::Min(ActualErode, MaxAllowed);

                            DCell.ElevationDelta -= ActualErode;
                            DCell.SedimentDelta += ActualErode;
                            Chunk.AccumulatedTerrainChange += ActualErode;
                        }
                        if (BankDeposition > 0.0f) {
                            float ActualDep = FMath::Min(BankDeposition, 0.05f * DeltaDays);
                            DCell.ElevationDelta += ActualDep;
                            DCell.DepositedSediment += ActualDep;
                            Chunk.AccumulatedTerrainChange += ActualDep;
                        }
                    }

                    float DepthPenalty = FMath::Clamp(1.0f - (SCell.ChannelDepth / 5.0f), 0.05f, 1.0f);
                    float BaseLevelPenalty = FMath::Clamp((SCell.Elevation - Manager->SeaLevel) / 50.0f, 0.0f, 1.0f);
                    float VegetationProtection = FMath::Clamp(0.2f + DCell.ForestDensity * 0.7f + DCell.FloraDensity * 0.2f, 0.0f, 1.0f);

                    float Slope = 0.0f;
                    bool bFlowsToOcean = false;

                    if (SCell.FlowDirectionGlobalX != -1) {
                        int32 FlowDX = SCell.FlowDirectionGlobalX - GlobalX;
                        int32 FlowDY = SCell.FlowDirectionGlobalY - GlobalY;

                        const FCellStaticData* TargetCellS = nullptr; const FCellDynamicData* TargetCellD = nullptr;
                        GetNeighbor(FlowDX, FlowDY, TargetCellS, TargetCellD);

                        if (TargetCellS && TargetCellD) {
                            Slope = FMath::Max(0.0f, (SCell.Elevation + DCell.SurfaceWater) - (TargetCellS->Elevation + TargetCellD->SurfaceWater));
                            if (TargetCellS->WaterType == EWaterType::Ocean || TargetCellS->Elevation <= Manager->SeaLevel) {
                                bFlowsToOcean = true;
                                Slope = FMath::Min(Slope, 0.01f);
                            }
                        }
                    }

                    float ElevAboveSea = FMath::Max(0.0f, SCell.Elevation - Manager->SeaLevel);
                    float LowlandFactor = FMath::Clamp(1.0f - (ElevAboveSea / 250.0f), 0.0f, 1.0f);

                    float CapacityMultiplier = FMath::Lerp(0.8f, 0.1f, LowlandFactor);
                    if (bFlowsToOcean) CapacityMultiplier *= 0.4f;

                    float SedimentCapacity = DCell.WaterFlow * FMath::Max(0.01f, Slope) * CapacityMultiplier;
                    if (bIsFlooding) SedimentCapacity *= 3.0f;

                    if (DCell.Sediment <= SedimentCapacity && DCell.WaterFlow > 0.1f && Slope > 0.02f && !bFlowsToOcean) {
                        float LocalErosionMult = Manager->ErosionMultiplier * FMath::Lerp(1.0f, 0.05f, LowlandFactor) * DepthPenalty * BaseLevelPenalty * (1.0f - VegetationProtection);
                        if (bIsFlooding) LocalErosionMult *= (SCell.ChannelDepth < 1.0f) ? 15.0f : 5.0f;

                        float ErosionPotential = (SedimentCapacity - DCell.Sediment) * ErosionCoefficient * LocalErosionMult;

                        float ActualErode = FMath::Min(ErosionPotential / MaterialResistance, 0.25f * DeltaDays);
                        float MaxAllowedErosion = FMath::Max(0.0f, SCell.Elevation - Manager->SeaLevel);
                        ActualErode = FMath::Min(ActualErode, MaxAllowedErosion);

                        if (ActualErode > 0.0f) {
                            DCell.ElevationDelta -= ActualErode;
                            DCell.SedimentDelta += ActualErode;
                            Chunk.AccumulatedTerrainChange += ActualErode;
                        }
                    }
                    else if (DCell.Sediment > SedimentCapacity && DCell.SurfaceWater > 0.05f) {
                        float Excess = DCell.Sediment - SedimentCapacity;
                        float DepositFactor = FMath::Lerp(0.2f, 1.0f, LowlandFactor) * FMath::Clamp(1.0f - (Slope / 2.0f), 0.5f, 1.0f);
                        float Deposit = Excess * DepositFactor * DepositionRate * DeltaDays;
                        Deposit = FMath::Min(Deposit, 0.05f * DeltaDays);

                        DCell.ElevationDelta += Deposit;
                        DCell.DepositedSediment += Deposit;
                        DCell.SedimentDelta -= Deposit;
                        Chunk.AccumulatedTerrainChange += Deposit;
                    }

                    float MySedimentOutflow = 0.0f;
                    if (SCell.FlowDirectionGlobalX != -1) {
                        float TransportFactor = FMath::Lerp(0.8f, 0.2f, LowlandFactor);
                        MySedimentOutflow = FMath::Min(DCell.Sediment, DCell.WaterFlow * TransportFactor * DeltaDays);
                    }
                    DCell.SedimentDelta -= MySedimentOutflow;
                }
            }
            });

        ParallelFor(GlobalEnd, [&](int32 idx) {
            FIntPoint Coord = ChunkKeys[idx];
            if (!WorldChunks.Contains(Coord)) return;
            FChunkData& Chunk = WorldChunks[Coord];

            for (int32 Y = 0; Y < CSize; Y++) {
                for (int32 X = 0; X < CSize; X++) {
                    int32 i = X + Y * Manager->ChunkSize;
                    Chunk.StaticCells[i].Elevation += Chunk.DynamicCells[i].ElevationDelta;
                    Chunk.DynamicCells[i].Sediment += Chunk.DynamicCells[i].SedimentDelta;
                    Chunk.DynamicCells[i].ElevationDelta = 0.0f;
                    Chunk.DynamicCells[i].SedimentDelta = 0.0f;
                }
            }
            });

        ParallelFor(GlobalEnd, [&](int32 idx) {
            FIntPoint Coord = ChunkKeys[idx];
            if (!WorldChunks.Contains(Coord)) return;
            FChunkData& Chunk = WorldChunks[Coord];

            FHaloBuffer Halo;
            Halo.Fetch(Manager, Coord, CSize, Manager->ChunkSize);

            for (int32 Y = 0; Y < CSize; Y++) {
                for (int32 X = 0; X < CSize; X++) {
                    int32 i = X + Y * Manager->ChunkSize;
                    FCellStaticData& SCell = Chunk.StaticCells[i];
                    FCellDynamicData& DCell = Chunk.DynamicCells[i];

                    int32 GlobalX = (Coord.X * CSize) + X; int32 GlobalY = (Coord.Y * CSize) + Y;

                    auto GetNeighbor = [&](int32 dx, int32 dy, const FCellStaticData*& OutS, const FCellDynamicData*& OutD) {
                        Halo.GetNeighbor(X + dx, Y + dy, Chunk, Manager, OutS, OutD);
                        };

                    float SedimentInflow = 0.0f;
                    for (int32 n = 0; n < 8; n++) {
                        const FCellStaticData* NCellS = nullptr; const FCellDynamicData* NCellD = nullptr;
                        GetNeighbor(Offsets[n][0], Offsets[n][1], NCellS, NCellD);

                        if (NCellS && NCellD && NCellS->FlowDirectionGlobalX == GlobalX && NCellS->FlowDirectionGlobalY == GlobalY) {
                            float N_ElevAboveSea = FMath::Max(0.0f, NCellS->Elevation - Manager->SeaLevel);
                            float N_LowlandFactor = FMath::Clamp(1.0f - (N_ElevAboveSea / 250.0f), 0.0f, 1.0f);
                            float N_TransportFactor = FMath::Lerp(0.8f, 0.2f, N_LowlandFactor);

                            if (NCellS->Elevation <= Manager->SeaLevel) N_TransportFactor = 0.9f;

                            float NFlow = (NCellS->Elevation <= Manager->SeaLevel) ? 1.0f : NCellD->WaterFlow;
                            float NSedimentOutflow = FMath::Min(NCellD->Sediment, NFlow * N_TransportFactor * DeltaDays);
                            SedimentInflow += NSedimentOutflow;
                        }
                    }
                    DCell.Sediment += SedimentInflow;

                    if (SCell.WaterType == EWaterType::Ocean || SCell.Elevation <= Manager->SeaLevel) continue;

                    float TargetWidth = 1.0f + FMath::Clamp(DCell.RiverDischarge * 0.05f, 0.0f, 4.0f);
                    float TargetDepth = 2.0f + FMath::Clamp(DCell.RiverDischarge * 0.15f, 0.0f, 8.0f);

                    bool bIsFlooding = DCell.SurfaceWater > (SCell.ChannelDepth + SCell.BankHeight + 0.2f);
                    if (bIsFlooding) {
                        SCell.ChannelWidth = FMath::Lerp(SCell.ChannelWidth, TargetWidth * 1.2f, 0.15f * DeltaDays);
                        SCell.ChannelDepth = FMath::Lerp(SCell.ChannelDepth, TargetDepth * 1.2f, 0.15f * DeltaDays);
                    }
                    else {
                        SCell.ChannelWidth = FMath::Lerp(SCell.ChannelWidth, TargetWidth, 0.05f * DeltaDays);
                        SCell.ChannelDepth = FMath::Lerp(SCell.ChannelDepth, TargetDepth, 0.05f * DeltaDays);
                    }

                    if (DCell.RiverDischarge > 1.0f && DCell.WaterFlow < 0.5f) {
                        SCell.BankHeight = FMath::Lerp(SCell.BankHeight, DCell.Sediment * 0.2f, 0.02f * DeltaDays);
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

            if (SafeFlags[idx] != 0) {
                Manager->RegisterVisualChange(ChunkKeys[idx], SafeFlags[idx]);
            }
            });
    }
}

void UHydroSystem::ProcessCoastalHydrology(ASimWorldManager* Manager) {}

bool UHydroSystem::TryCreateSpring(ASimWorldManager* Manager, int32 GlobalX, int32 GlobalY)
{
    if (!Manager) return false;

    FCellStaticData* SCell = nullptr;
    FCellDynamicData* DCell = nullptr;
    FIntPoint ChunkCoord;
    if (!Manager->GetMutableCellGlobal(GlobalX, GlobalY, SCell, DCell, ChunkCoord)) return false;

    float GroundWaterReq = 40.0f;
    if (DCell->GroundWater < GroundWaterReq) {
        return false;
    }

    if (!Manager->ManaModule->SpendMana(150.0f, TEXT("Prorazeni pramene"))) return false;

    SCell->bIsSpring = true;
    SCell->WaterType = EWaterType::Spring;
    SCell->SpringStrength = 3.0f;
    DCell->GroundWater -= GroundWaterReq;

    Manager->RegisterVisualChange(ChunkCoord, EChunkVisualDirty::Water | EChunkVisualDirty::Terrain, true);

    int32 Radius = 12;
    for (int32 dy = -Radius; dy <= Radius; dy++) {
        for (int32 dx = -Radius; dx <= Radius; dx++) {
            if (dx == 0 && dy == 0) continue;

            FCellStaticData* NSCell = nullptr;
            FCellDynamicData* NDCell = nullptr;
            FIntPoint NChunkCoord;
            if (Manager->GetMutableCellGlobal(GlobalX + dx, GlobalY + dy, NSCell, NDCell, NChunkCoord)) {

                NDCell->GroundWater = FMath::Max(0.0f, NDCell->GroundWater - 5.0f);

                if (NSCell->bIsSpring && NSCell->Elevation > SCell->Elevation) {
                    NSCell->SpringStrength *= 0.5f;

                    if (NSCell->SpringStrength < 0.5f) {
                        NSCell->bIsSpring = false;
                        NSCell->WaterType = EWaterType::Surface;
                    }
                }
            }
        }
    }

    return true;
}