#include "HydroSystem.h"
#include "SimWorldManager.h"
#include "CosmosSystem.h"
#include "ManaSystem.h"
#include "Async/ParallelFor.h"
#include "NoiseUtils.h"

UHydroSystem::UHydroSystem() { PrimaryComponentTick.bCanEverTick = false; }
void UHydroSystem::BeginPlay() { Super::BeginPlay(); }

static float GetRiverElev(float GlobalX, float GlobalY, const FChunkGenerationParameters& Params)
{
    float WorldCellsX = FMath::Max(1.0f, (float)(Params.WorldSizeInChunksX * (Params.ChunkSize - 1)));
    float WorldCellsY = FMath::Max(1.0f, (float)(Params.WorldSizeInChunksY * (Params.ChunkSize - 1)));

    float WarpFreq = Params.NoiseScale * 0.3f;
    float WarpAmp = FMath::Max(WorldCellsX, WorldCellsY) * 0.25f;
    float WarpX = GetFBM(GlobalX, GlobalY, WarpFreq, 3, Params.MapSeed) * WarpAmp;
    float WarpY = GetFBM(GlobalY, GlobalX, WarpFreq, 3, Params.MapSeed + 50) * WarpAmp;
    float WarpedX = GlobalX + WarpX;
    float WarpedY = GlobalY + WarpY;

    float ShiftMultiplier = FMath::Max(WorldCellsX, WorldCellsY) * 0.3f;
    float Dist1 = 9999999.0f; float Dist2 = 9999999.0f;

    for (int32 i = 0; i < Params.Continents.Num(); i++) {
        FRandomStream SizeStream(Params.MapSeed + i * 73);
        float RadiusMult = (Params.ContinentCount <= 2) ? 1.2f : (1.8f / FMath::Sqrt((float)FMath::Max(1, Params.ContinentCount))) * SizeStream.FRandRange(0.6f, 1.6f);

        FVector2D EffCenter = Params.Continents[i].OriginCenter + (Params.Continents[i].DriftDirection * (Params.TectonicShift / 50.0f) * ShiftMultiplier);
        float D = FVector2D::Distance(FVector2D(WarpedX, WarpedY), EffCenter) / FMath::Max(0.1f, RadiusMult);

        if (D < Dist1) { Dist2 = Dist1; Dist1 = D; }
        else if (D < Dist2) { Dist2 = D; }
    }

    float CoastSlider = FMath::Clamp(Params.CoastlineRoughness, 0.0f, 1.0f);
    float PlainsSlider = FMath::Clamp(Params.LowlandFlatness, 0.0f, 1.0f);
    float MountSlider = FMath::Clamp(Params.ElevationMultiplier / 6000.0f, 0.0f, 1.0f);
    float TectSlider = FMath::Clamp(Params.TectonicMountainHeight / 6000.0f, 0.0f, 1.0f);

    float ContCountScale = Params.ContinentCount <= 1 ? 1.0f : (1.4f / FMath::Sqrt((float)Params.ContinentCount));
    float MaxDistBase = (WorldCellsX + WorldCellsY) * 0.38f * FMath::Max(0.2f, Params.ContinentSizeMultiplier) * ContCountScale;

    float BaseShape = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(1.0f - (Dist1 / FMath::Max(MaxDistBase, 1.0f)), 0.0f, 1.0f));

    float PlainsExpansion = PlainsSlider * 0.25f;
    float CoastNoise = GetFBM(GlobalX, GlobalY, Params.NoiseScale * 1.5f, 5, Params.MapSeed);
    float CarveAmount = FMath::Lerp(0.05f, 0.35f, CoastSlider);

    float RawTopo = BaseShape - (CoastNoise * CarveAmount) + PlainsExpansion;
    float Topo = FMath::Clamp(RawTopo, 0.0f, 1.0f);

    float EdgeDistX = FMath::Min(GlobalX, WorldCellsX - GlobalX);
    float EdgeDistY = FMath::Min(GlobalY, WorldCellsY - GlobalY);
    float MapEdgeMask = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(FMath::Min(EdgeDistX, EdgeDistY) / 30.0f, 0.0f, 1.0f));
    Topo *= MapEdgeMask;

    float TectonicPressure = 0.0f;
    if (Params.Continents.Num() > 1) {
        float BoundaryThick = FMath::Max(1.0f, MaxDistBase * 0.4f);
        float DistDiff = FMath::Abs(Dist1 - Dist2);
        float CollisionRaw = FMath::Clamp(1.0f - (DistDiff / BoundaryThick), 0.0f, 1.0f);

        float TectNoise = GetFBM(GlobalX, GlobalY, Params.NoiseScale * 0.8f, 3, Params.MapSeed + 500);
        TectonicPressure = FMath::SmoothStep(0.0f, 1.0f, CollisionRaw) * FMath::SmoothStep(0.3f, 0.7f, TectNoise);
    }

    float Elev = 0.0f;
    float T_Deep = 0.2f;
    float CoastWidth = FMath::Lerp(0.05f, 0.25f, CoastSlider);
    float T_Coast = T_Deep + CoastWidth;

    if (Topo < T_Deep) {
        float t = Topo / T_Deep;
        Elev = FMath::Lerp(-800.0f, -10.0f, FMath::SmoothStep(0.0f, 1.0f, t));
    }
    else if (Topo < T_Coast) {
        float t = (Topo - T_Deep) / (T_Coast - T_Deep);
        Elev = FMath::Lerp(-10.0f, 2.0f, FMath::SmoothStep(0.0f, 1.0f, t));
    }
    else {
        float InlandFade = (Topo - T_Coast) / (1.0f - T_Coast);
        float t = FMath::SmoothStep(0.0f, 1.0f, InlandFade);

        float BaseElev = 2.0f;

        float PlainsNoise = GetFBM(GlobalX, GlobalY, Params.NoiseScale * 2.0f, 4, Params.MapSeed + 10);
        float MaxHill = FMath::Lerp(450.0f, 20.0f, PlainsSlider);
        float HillyTerrain = FMath::Pow(PlainsNoise, 1.2f) * MaxHill * t;

        float RandMountNoise = GetFBM(GlobalX, GlobalY, Params.NoiseScale * 1.5f, 3, Params.MapSeed + 300);
        float MountSpreadStart = FMath::Lerp(0.85f, 0.4f, MountSlider);
        float RandMountMask = FMath::SmoothStep(MountSpreadStart, 1.0f, RandMountNoise);

        float MountMask = FMath::Clamp(TectonicPressure + RandMountMask, 0.0f, 1.0f);
        MountMask *= FMath::SmoothStep(0.1f, 0.4f, InlandFade);

        float MountHeight = 0.0f;
        if (MountMask > 0.0f) {
            float Ridge = GetRidgedFBM(GlobalX, GlobalY, Params.NoiseScale * 2.5f, 6, Params.MapSeed + 400);
            float MaxMountHeight = (MountSlider * 3500.0f) + (TectSlider * 3000.0f);
            MountHeight = MountMask * Ridge * MaxMountHeight;
        }

        Elev = BaseElev + HillyTerrain + MountHeight;
    }

    float IslandChance = FMath::Clamp(Params.IslandFrequency, 0.0f, 1.0f);
    if (IslandChance > 0.0f && MapEdgeMask > 0.1f) {
        float IslandNoise = GetFBM(GlobalX, GlobalY, Params.NoiseScale * 5.0f, 3, Params.MapSeed + 200);
        float Thresh = FMath::Clamp(1.0f - (IslandChance * 0.3f), 0.01f, 0.99f);

        float OceanOnly = 1.0f - FMath::SmoothStep(T_Deep - 0.1f, T_Deep + 0.1f, Topo);
        if (OceanOnly > 0.0f && IslandNoise > Thresh) {
            float iMask = FMath::SmoothStep(0.0f, 1.0f, (IslandNoise - Thresh) / (1.0f - Thresh));
            iMask *= OceanOnly;
            float IslandPeak = FMath::Lerp(30.0f, 600.0f, MountSlider);
            Elev = FMath::Max(Elev, FMath::Lerp(-200.0f, IslandPeak, iMask));
        }
    }

    if (Params.VolcanicActivity > 0.0f) {
        int32 CSize = Params.ChunkSize - 1;
        int32 ChunkX = FMath::FloorToInt(GlobalX / CSize);
        int32 ChunkY = FMath::FloorToInt(GlobalY / CSize);

        for (int32 cy = ChunkY - 1; cy <= ChunkY + 1; cy++) {
            for (int32 cx = ChunkX - 1; cx <= ChunkX + 1; cx++) {
                if (cx < 0 || cx >= Params.WorldSizeInChunksX || cy < 0 || cy >= Params.WorldSizeInChunksY) continue;

                FRandomStream VolcStream(Params.MapSeed + (cx * 103) + (cy * 17));

                float VolcanicChance = Params.VolcanicActivity * 0.5f;
                int32 VolcanoesToSpawn = FMath::FloorToInt(VolcanicChance);
                if (VolcStream.FRand() < (VolcanicChance - VolcanoesToSpawn)) {
                    VolcanoesToSpawn++;
                }

                for (int32 i = 0; i < VolcanoesToSpawn; i++) {
                    int32 RandomIndex = VolcStream.RandRange(0, (CSize * CSize) - 1);
                    float VolcX = (cx * CSize) + (RandomIndex % CSize);
                    float VolcY = (cy * CSize) + (RandomIndex / CSize);

                    float VolcZoneNoise = GetFBM(VolcX, VolcY, Params.NoiseScale * 0.8f, 3, Params.MapSeed + 500);
                    if (VolcZoneNoise < 0.55f) continue;

                    float Dist = FVector2D::Distance(FVector2D(GlobalX, GlobalY), FVector2D(VolcX, VolcY));
                    float VolcRadius = 25.0f;

                    if (Dist <= VolcRadius) {
                        float Falloff = FMath::SmoothStep(0.0f, 1.0f, 1.0f - (Dist / VolcRadius));
                        Elev += (Params.LavaHeightBoost * Falloff);
                    }
                }
            }
        }
    }

    return Elev;
}

struct FRiverHead { FVector2D Pos; FVector2D Momentum; float Volume; int32 Life; };

void UHydroSystem::ProcessChunkWater(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    float SeaLevel = Params.SeaLevel;
    int32 ChunkSize = Params.ChunkSize;

    ParallelFor(OutChunk.MicroCells.Num(), [&](int32 i) {
        int32 X = i % ChunkSize;
        int32 Y = i / ChunkSize;
        float GlobalX = (ChunkCoord.X * (ChunkSize - 1)) + X;
        float GlobalY = (ChunkCoord.Y * (ChunkSize - 1)) + Y;

        if (Params.bIsFullGeneration) {
            OutChunk.MicroCells[i].SurfaceWater = 0.0f;
            OutChunk.MicroCells[i].WaterInflowBuffer = 0.0f;
            OutChunk.MicroCells[i].RiverDischarge = 0.0f;
            OutChunk.MicroCells[i].RiverDischargeBuffer = 0.0f;
            OutChunk.MicroCells[i].FlowPersistenceBuffer = 0.0f;
            OutChunk.MicroCells[i].ChannelWidth = 0.0f;
            OutChunk.MicroCells[i].ChannelDepth = 0.0f;
            OutChunk.MicroCells[i].BankHeight = 0.0f;
            OutChunk.MicroCells[i].WaterType = EWaterType::None;
            OutChunk.MicroCells[i].bIsSpring = false;
            OutChunk.MicroCells[i].SpringStrength = 0.0f;
            OutChunk.MicroCells[i].FlowDirectionGlobalX = -1;
            OutChunk.MicroCells[i].FlowDirectionGlobalY = -1;
            OutChunk.MicroCells[i].DepositedSediment = 0.0f;
            OutChunk.MicroCells[i].SoilStability = 0.0f;
            OutChunk.MicroCells[i].WetlandScore = 0.0f;

            if (OutChunk.MicroCells[i].Elevation <= SeaLevel) {
                OutChunk.MicroCells[i].WaterType = EWaterType::Ocean;
                OutChunk.MicroCells[i].SurfaceWater = SeaLevel - OutChunk.MicroCells[i].Elevation;
            }
            else {
                OutChunk.MicroCells[i].GroundWater = (OutChunk.MicroCells[i].Rainfall * 100.0f);

                float SpringNoise = FMath::PerlinNoise2D(FVector2D(GlobalX * 0.1f, GlobalY * 0.1f));
                if (OutChunk.MicroCells[i].Elevation > SeaLevel + 200.0f && OutChunk.MicroCells[i].GroundWater > 50.0f && SpringNoise > 0.85f) {
                    OutChunk.MicroCells[i].bIsSpring = true;
                    OutChunk.MicroCells[i].SpringStrength = FMath::FRandRange(0.5f, 2.0f);
                    OutChunk.MicroCells[i].WaterType = EWaterType::Spring;
                }
            }
        }
        else {
            if (OutChunk.MicroCells[i].Elevation > SeaLevel) {
                OutChunk.MicroCells[i].GroundWater = FMath::Clamp(OutChunk.MicroCells[i].GroundWater + (OutChunk.MicroCells[i].Rainfall * Params.RainfallMultiplier), 0.0f, 100.0f);
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
                        OutChunk.MicroCells[Idx].bIsSpring = true;
                        OutChunk.MicroCells[Idx].SpringStrength = NewRiver.Volume * 1.5f;
                        OutChunk.MicroCells[Idx].WaterType = EWaterType::Spring;
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

                // OPRAVA: Øeky už nesmí být extrémnì široké. Max polomìr je 1.0 (šíøka 3 buòky / 150m)
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

                            // Sníženo množství vkládané vody, aby nevyplavilo bøehy
                            float waterAmount = FMath::Max(0.0f, (BaseCarveRadius + 0.5f - centerDist) * 5.0f * River.Volume);
                            OutChunk.MicroCells[Idx].SurfaceWater += waterAmount;

                            if (OutChunk.MicroCells[Idx].Elevation > SeaLevel) {
                                float carveAlpha = FMath::Clamp(1.0f - (centerDist / (BaseCarveRadius + 0.5f)), 0.0f, 1.0f);

                                // OPRAVA: Koryto se musí vždy hluboce zaøíznout do terénu (4 až 12 metrù!), jinak se voda rozleje po planinì
                                float CarveDepth = FMath::Lerp(12.0f, 4.0f, LowlandAlpha);
                                float targetRiverZ = FMath::Max(SeaLevel, CurrElev - CarveDepth);
                                float originalZ = OutChunk.MicroCells[Idx].Elevation;

                                if (originalZ > targetRiverZ) {
                                    float profileExp = FMath::Lerp(1.5f, 3.0f, LowlandAlpha); // V-tvar v horách, ostré U-koryto v nížinì
                                    float finalAlpha = FMath::Pow(carveAlpha, profileExp) * Params.ErosionMultiplier;
                                    OutChunk.MicroCells[Idx].Elevation = FMath::Lerp(originalZ, targetRiverZ, finalAlpha);
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
                                            float originalZ = OutChunk.MicroCells[Idx].Elevation;

                                            if (dist <= effRadius) {
                                                // Snížení nárazového množství vody do jezera, aby nevznikly okamžité záplavy
                                                OutChunk.MicroCells[Idx].SurfaceWater += 15.0f;
                                                float depthAlpha = dist / effRadius;
                                                float lakeBedZ = FMath::Max(SeaLevel, CurrElev - FMath::Lerp(12.0f, 2.0f, depthAlpha));
                                                OutChunk.MicroCells[Idx].Elevation = FMath::Min(originalZ, lakeBedZ);
                                            }
                                            else {
                                                float shoreAlpha = (dist - effRadius) / (shoreRadius - effRadius);
                                                float blend = FMath::SmoothStep(0.0f, 1.0f, shoreAlpha);
                                                OutChunk.MicroCells[Idx].Elevation = FMath::Min(originalZ, FMath::Lerp(CurrElev, originalZ, blend));
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

            // OPRAVA PRÙRAZU Z JEZERA: Pokud se jezero prolije pøes okraj, øeka musí okamžitì smìøovat ven, jinak se rozleje
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

        for (int i = 0; i < Chunk.MicroCells.Num(); i++) {
            int32 X = i % Manager->ChunkSize;
            int32 Y = i / Manager->ChunkSize;
            if (X >= CSize || Y >= CSize) continue;

            FCellData& Cell = Chunk.MicroCells[i];
            int32 GlobalX = (Coord.X * CSize) + X;
            int32 GlobalY = (Coord.Y * CSize) + Y;

            auto GetNeighbor = [&](int32 dx, int32 dy) -> const FCellData* {
                int32 lx = X + dx;
                int32 ly = Y + dy;
                if (lx >= 0 && lx < CSize && ly >= 0 && ly < CSize) {
                    return &Chunk.MicroCells[lx + ly * Manager->ChunkSize];
                }
                const FCellData* ExtCell = nullptr;
                Manager->GetCellGlobalPtr(GlobalX + dx, GlobalY + dy, ExtCell);
                return ExtCell;
                };

            if (Cell.Elevation <= Manager->SeaLevel) {
                Cell.WaterType = EWaterType::Ocean;
                Cell.SurfaceWater = Manager->SeaLevel - Cell.Elevation;

                float LowestHead = Cell.Elevation;
                int32 LowestGlobalX = -1;
                int32 LowestGlobalY = -1;

                for (int32 n = 0; n < 8; n++) {
                    const FCellData* NCell = GetNeighbor(Offsets[n][0], Offsets[n][1]);
                    if (NCell && NCell->Elevation < LowestHead) {
                        LowestHead = NCell->Elevation;
                        LowestGlobalX = GlobalX + Offsets[n][0];
                        LowestGlobalY = GlobalY + Offsets[n][1];
                    }
                }
                Cell.FlowDirectionGlobalX = LowestGlobalX;
                Cell.FlowDirectionGlobalY = LowestGlobalY;
                Cell.WaterFlow = 1.0f;
                continue;
            }

            float RechargeFactor = 0.4f;
            if (Cell.Bedrock == EBedrockType::Sand) RechargeFactor = 0.8f;
            else if (Cell.Bedrock == EBedrockType::Rock) RechargeFactor = 0.1f;

            float GroundwaterBaseLoss = 0.05f;
            float InfiltratedWater = Cell.Rainfall * RechargeFactor;
            float NewGroundWater = Cell.GroundWater + InfiltratedWater - GroundwaterBaseLoss;

            if (NewGroundWater > 100.0f) {
                float ExcessWater = NewGroundWater - 100.0f;
                Cell.SurfaceWater += ExcessWater;
                Cell.GroundWater = 100.0f;
            }
            else {
                Cell.GroundWater = FMath::Max(0.0f, NewGroundWater);
            }

            if (Cell.bIsSpring) {
                Cell.WaterType = EWaterType::Spring;
            }
            else if (Cell.SurfaceWater > 0.5f && Cell.FlowDirectionGlobalX == -1) {
                Cell.WaterType = EWaterType::Lake;
            }
            else if (Cell.RiverDischarge > 0.2f) {
                Cell.WaterType = EWaterType::River;
            }
            else if (Cell.SurfaceWater > 0.05f) {
                Cell.WaterType = EWaterType::Surface;
            }
            else {
                Cell.WaterType = EWaterType::None;
            }

            if (Cell.SurfaceWater > 0.01f || Cell.bIsSpring) {
                float MyHead = Cell.Elevation + Cell.SurfaceWater;
                float LowestHead = MyHead;
                int32 LowestGlobalX = -1;
                int32 LowestGlobalY = -1;

                for (int32 n = 0; n < 8; n++) {
                    const FCellData* NCell = GetNeighbor(Offsets[n][0], Offsets[n][1]);
                    if (NCell) {
                        float nHead = NCell->Elevation + NCell->SurfaceWater;
                        if (nHead < LowestHead) {
                            LowestHead = nHead;
                            LowestGlobalX = GlobalX + Offsets[n][0];
                            LowestGlobalY = GlobalY + Offsets[n][1];
                        }
                    }
                }

                Cell.FlowDirectionGlobalX = LowestGlobalX;
                Cell.FlowDirectionGlobalY = LowestGlobalY;
            }
            else {
                Cell.FlowDirectionGlobalX = -1;
                Cell.FlowDirectionGlobalY = -1;
            }
        }
        });

    const float FlowCoefficient = 0.18f;

    ParallelFor(GlobalEnd, [&](int32 idx) {
        FIntPoint Coord = ChunkKeys[idx];
        if (!WorldChunks.Contains(Coord)) return;
        FChunkData& Chunk = WorldChunks[Coord];

        float LocalFlowCoefficient = Chunk.bGeomorphologyDirty ? 0.8f : FlowCoefficient;

        for (int i = 0; i < Chunk.MicroCells.Num(); i++) {
            int32 X = i % Manager->ChunkSize;
            int32 Y = i / Manager->ChunkSize;
            if (X >= CSize || Y >= CSize) continue;

            FCellData& Cell = Chunk.MicroCells[i];
            if (Cell.WaterType == EWaterType::Ocean) continue;

            int32 GlobalX = (Coord.X * CSize) + X;
            int32 GlobalY = (Coord.Y * CSize) + Y;

            auto GetNeighbor = [&](int32 dx, int32 dy) -> const FCellData* {
                int32 lx = X + dx;
                int32 ly = Y + dy;
                if (lx >= 0 && lx < CSize && ly >= 0 && ly < CSize) {
                    return &Chunk.MicroCells[lx + ly * Manager->ChunkSize];
                }
                const FCellData* ExtCell = nullptr;
                Manager->GetCellGlobalPtr(GlobalX + dx, GlobalY + dy, ExtCell);
                return ExtCell;
                };

            float SpringInput = 0.0f;
            if (Cell.bIsSpring) {
                float GroundFactor = FMath::Clamp(Cell.GroundWater / 100.0f, 0.25f, 1.0f);
                SpringInput = Cell.SpringStrength * GroundFactor;
                Cell.GroundWater = FMath::Max(0.0f, Cell.GroundWater - SpringInput);
                Cell.SurfaceWater = FMath::Max(Cell.SurfaceWater, SpringInput);
            }

            float MyOutflow = 0.0f;
            if (Cell.FlowDirectionGlobalX != -1 && Cell.FlowDirectionGlobalY != -1) {
                int32 FlowDX = Cell.FlowDirectionGlobalX - GlobalX;
                int32 FlowDY = Cell.FlowDirectionGlobalY - GlobalY;
                const FCellData* TargetCell = nullptr;
                if (FMath::Abs(FlowDX) <= 1 && FMath::Abs(FlowDY) <= 1) {
                    TargetCell = GetNeighbor(FlowDX, FlowDY);
                }
                else {
                    Manager->GetCellGlobalPtr(Cell.FlowDirectionGlobalX, Cell.FlowDirectionGlobalY, TargetCell);
                }

                if (TargetCell) {
                    float Diff = (Cell.Elevation + Cell.SurfaceWater) - (TargetCell->Elevation + TargetCell->SurfaceWater);
                    if (Diff > 0.0f) {
                        float MaxTransfer = Diff * 0.25f;
                        float DesiredTransfer = Diff * LocalFlowCoefficient;
                        if (Cell.WaterType == EWaterType::River || Cell.WaterType == EWaterType::Spring || Cell.RiverDischarge > 0.5f || Chunk.bGeomorphologyDirty) {
                            DesiredTransfer = Diff * (Chunk.bGeomorphologyDirty ? 0.8f : 0.25f);
                        }
                        MyOutflow = FMath::Min(Cell.SurfaceWater, FMath::Min(DesiredTransfer, MaxTransfer));
                    }
                }
            }

            float MyInflow = 0.0f;
            for (int32 n = 0; n < 8; n++) {
                const FCellData* NCell = GetNeighbor(Offsets[n][0], Offsets[n][1]);
                if (NCell) {
                    if (NCell->WaterType != EWaterType::Ocean && NCell->FlowDirectionGlobalX == GlobalX && NCell->FlowDirectionGlobalY == GlobalY) {
                        float Diff = (NCell->Elevation + NCell->SurfaceWater) - (Cell.Elevation + Cell.SurfaceWater);
                        if (Diff > 0.0f) {
                            float MaxTransfer = Diff * 0.25f;
                            float DesiredTransfer = Diff * LocalFlowCoefficient;
                            if (NCell->WaterType == EWaterType::River || NCell->WaterType == EWaterType::Spring || NCell->RiverDischarge > 0.5f || Chunk.bGeomorphologyDirty) {
                                DesiredTransfer = Diff * (Chunk.bGeomorphologyDirty ? 0.8f : 0.25f);
                            }
                            MyInflow += FMath::Min(NCell->SurfaceWater, FMath::Min(DesiredTransfer, MaxTransfer));
                        }
                    }
                }
            }

            float NextWater = Cell.SurfaceWater - MyOutflow + MyInflow + SpringInput;
            NextWater = FMath::Max(0.0f, NextWater - 0.0005f);

            Cell.WaterInflowBuffer = NextWater;
            Cell.WaterFlow = FMath::Lerp(Cell.WaterFlow, MyOutflow, 0.15f);

            if (Cell.WaterFlow > 0.1f) {
                Cell.FlowPersistenceBuffer = FMath::Min(1.0f, Cell.FlowPersistence + 0.01f);
            }
            else {
                Cell.FlowPersistenceBuffer = FMath::Max(0.0f, Cell.FlowPersistence - 0.005f);
            }

            float TargetDischarge = MyOutflow + MyInflow;
            Cell.RiverDischargeBuffer = FMath::Lerp(Cell.RiverDischarge, TargetDischarge, 0.05f);

            if (FMath::Abs(Cell.SurfaceWater - NextWater) > 0.5f) {
                Chunk.AccumulatedWaterChange += FMath::Abs(Cell.SurfaceWater - NextWater);
            }
        }
        });

    ParallelFor(GlobalEnd, [&](int32 idx) {
        FIntPoint Coord = ChunkKeys[idx];
        if (!WorldChunks.Contains(Coord)) return;
        FChunkData& Chunk = WorldChunks[Coord];

        for (int i = 0; i < Chunk.MicroCells.Num(); i++) {
            if ((i % Manager->ChunkSize) >= CSize || (i / Manager->ChunkSize) >= CSize) continue;
            Chunk.MicroCells[i].SurfaceWater = Chunk.MicroCells[i].WaterInflowBuffer;
            Chunk.MicroCells[i].RiverDischarge = Chunk.MicroCells[i].RiverDischargeBuffer;
            Chunk.MicroCells[i].FlowPersistence = Chunk.MicroCells[i].FlowPersistenceBuffer;

            Chunk.MicroCells[i].WaterInflowBuffer = 0.0f;
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

            for (int i = 0; i < Chunk.MicroCells.Num(); i++) {
                int32 X = i % Manager->ChunkSize; int32 Y = i / Manager->ChunkSize;
                if (X >= CSize || Y >= CSize) continue;

                FCellData& Cell = Chunk.MicroCells[i];
                int32 GlobalX = (Coord.X * CSize) + X; int32 GlobalY = (Coord.Y * CSize) + Y;

                auto GetNeighbor = [&](int32 dx, int32 dy) -> const FCellData* {
                    int32 lx = X + dx;
                    int32 ly = Y + dy;
                    if (lx >= 0 && lx < CSize && ly >= 0 && ly < CSize) {
                        return &Chunk.MicroCells[lx + ly * Manager->ChunkSize];
                    }
                    const FCellData* ExtCell = nullptr;
                    Manager->GetCellGlobalPtr(GlobalX + dx, GlobalY + dy, ExtCell);
                    return ExtCell;
                    };

                if (Cell.WaterType == EWaterType::Ocean || Cell.Elevation <= Manager->SeaLevel) {
                    if (Cell.Sediment > 0.01f) {
                        float DepositRate = FMath::Clamp(2.0f - TidalMultiplier, 0.05f, 0.5f) * 0.15f;
                        float OceanDeposit = Cell.Sediment * DepositRate * DeltaDays;

                        float SpaceToSeaLevel = Manager->SeaLevel - Cell.Elevation;
                        float MaxAllowedDeposit = FMath::Max(0.0f, SpaceToSeaLevel + 0.15f);
                        float ActualDeposit = FMath::Min(OceanDeposit, MaxAllowedDeposit);

                        Cell.ElevationDelta += ActualDeposit;
                        Cell.DepositedSediment += ActualDeposit;
                        Cell.SedimentDelta -= ActualDeposit;
                        Chunk.AccumulatedTerrainChange += ActualDeposit;

                        if (Cell.Elevation + Cell.ElevationDelta > Manager->SeaLevel) {
                            Cell.WaterType = EWaterType::Surface;
                            Cell.Bedrock = EBedrockType::Sand;
                            Cell.SoilType = ESoilType::Sand;
                            SafeFlags[idx] |= EChunkVisualDirty::Flora | EChunkVisualDirty::Terrain;
                        }
                    }

                    float MySedimentOutflow = 0.0f;
                    if (Cell.FlowDirectionGlobalX != -1) {
                        MySedimentOutflow = FMath::Min(Cell.Sediment + Cell.SedimentDelta, Cell.WaterFlow * 0.9f * DeltaDays);
                    }
                    Cell.SedimentDelta -= MySedimentOutflow;
                    continue;
                }

                bool bIsFlooding = Cell.SurfaceWater > (Cell.ChannelDepth + Cell.BankHeight + 0.2f);
                float MaterialResistance = (Cell.Bedrock == EBedrockType::Sand) ? 0.5f : ((Cell.Bedrock == EBedrockType::Rock) ? 50.0f : 1.0f);

                if (Cell.SurfaceWater < 0.1f && !bIsFlooding) {
                    float BankErosion = 0.0f;
                    float BankDeposition = 0.0f;

                    for (int32 n = 0; n < 8; n++) {
                        int32 NX = GlobalX + Offsets[n][0]; int32 NY = GlobalY + Offsets[n][1];
                        const FCellData* NCell = GetNeighbor(Offsets[n][0], Offsets[n][1]);

                        if (NCell && NCell->RiverDischarge > 1.0f && NCell->SurfaceWater > 0.1f) {
                            FVector2D DirToMe(-Offsets[n][0], -Offsets[n][1]); DirToMe.Normalize();
                            FVector2D NOutflow(0, 0);
                            if (NCell->FlowDirectionGlobalX != -1) {
                                NOutflow = FVector2D(NCell->FlowDirectionGlobalX - NX, NCell->FlowDirectionGlobalY - NY).GetSafeNormal();
                            }

                            if (FVector2D::DotProduct(NOutflow, DirToMe) < 0.5f) {
                                FVector2D NInflowMom(0, 0);
                                for (int32 nn = 0; nn < 8; nn++) {
                                    int32 relDX = Offsets[n][0] + Offsets[nn][0]; int32 relDY = Offsets[n][1] + Offsets[nn][1];
                                    const FCellData* UpstreamCell = GetNeighbor(relDX, relDY);
                                    if (UpstreamCell && UpstreamCell->FlowDirectionGlobalX == NX && UpstreamCell->FlowDirectionGlobalY == NY) {
                                        NInflowMom += FVector2D(-Offsets[nn][0], -Offsets[nn][1]) * UpstreamCell->WaterFlow;
                                    }
                                }

                                if (!NInflowMom.IsNearlyZero()) {
                                    NInflowMom.Normalize();
                                    float HitDot = FVector2D::DotProduct(NInflowMom, DirToMe);

                                    if (HitDot > 0.6f) BankErosion += NCell->RiverDischarge * HitDot * 0.002f * DeltaDays * Manager->ErosionMultiplier;
                                    else if (HitDot < -0.4f && NCell->Sediment > 0.1f) BankDeposition += NCell->Sediment * 0.02f * DeltaDays;
                                }
                            }
                        }
                    }

                    if (BankErosion > 0.0f) {
                        float ActualErode = FMath::Min(BankErosion / MaterialResistance, 0.05f * DeltaDays);
                        float MaxAllowed = FMath::Max(0.0f, Cell.Elevation - Manager->SeaLevel);
                        ActualErode = FMath::Min(ActualErode, MaxAllowed);

                        Cell.ElevationDelta -= ActualErode;
                        Cell.SedimentDelta += ActualErode;
                        Chunk.AccumulatedTerrainChange += ActualErode;
                    }
                    if (BankDeposition > 0.0f) {
                        float ActualDep = FMath::Min(BankDeposition, 0.05f * DeltaDays);
                        Cell.ElevationDelta += ActualDep;
                        Cell.DepositedSediment += ActualDep;
                        Chunk.AccumulatedTerrainChange += ActualDep;
                    }
                }

                float DepthPenalty = FMath::Clamp(1.0f - (Cell.ChannelDepth / 5.0f), 0.05f, 1.0f);
                float BaseLevelPenalty = FMath::Clamp((Cell.Elevation - Manager->SeaLevel) / 50.0f, 0.0f, 1.0f);
                float VegetationProtection = FMath::Clamp(0.2f + Cell.ForestDensity * 0.7f + Cell.FloraDensity * 0.2f, 0.0f, 1.0f);

                float Slope = 0.0f;
                bool bFlowsToOcean = false;

                if (Cell.FlowDirectionGlobalX != -1) {
                    int32 FlowDX = Cell.FlowDirectionGlobalX - GlobalX;
                    int32 FlowDY = Cell.FlowDirectionGlobalY - GlobalY;
                    const FCellData* TargetCell = nullptr;
                    if (FMath::Abs(FlowDX) <= 1 && FMath::Abs(FlowDY) <= 1) TargetCell = GetNeighbor(FlowDX, FlowDY);
                    else Manager->GetCellGlobalPtr(Cell.FlowDirectionGlobalX, Cell.FlowDirectionGlobalY, TargetCell);

                    if (TargetCell) {
                        Slope = FMath::Max(0.0f, (Cell.Elevation + Cell.SurfaceWater) - (TargetCell->Elevation + TargetCell->SurfaceWater));
                        if (TargetCell->WaterType == EWaterType::Ocean || TargetCell->Elevation <= Manager->SeaLevel) {
                            bFlowsToOcean = true;
                            Slope = FMath::Min(Slope, 0.01f);
                        }
                    }
                }

                float ElevAboveSea = FMath::Max(0.0f, Cell.Elevation - Manager->SeaLevel);
                float LowlandFactor = FMath::Clamp(1.0f - (ElevAboveSea / 250.0f), 0.0f, 1.0f);

                float CapacityMultiplier = FMath::Lerp(0.8f, 0.1f, LowlandFactor);
                if (bFlowsToOcean) CapacityMultiplier *= 0.4f;

                float SedimentCapacity = Cell.WaterFlow * FMath::Max(0.01f, Slope) * CapacityMultiplier;
                if (bIsFlooding) SedimentCapacity *= 3.0f;

                if (Cell.Sediment <= SedimentCapacity && Cell.WaterFlow > 0.1f && Slope > 0.02f && !bFlowsToOcean) {
                    float LocalErosionMult = Manager->ErosionMultiplier * FMath::Lerp(1.0f, 0.05f, LowlandFactor) * DepthPenalty * BaseLevelPenalty * (1.0f - VegetationProtection);
                    if (bIsFlooding) LocalErosionMult *= (Cell.ChannelDepth < 1.0f) ? 15.0f : 5.0f;

                    float ErosionPotential = (SedimentCapacity - Cell.Sediment) * ErosionCoefficient * LocalErosionMult;

                    // OPRAVA DYNAMICKÉ EROZE: Øeka má povoleno vyhrabat až 25cm hlíny dennì, aby zabránila rozlivu
                    float ActualErode = FMath::Min(ErosionPotential / MaterialResistance, 0.25f * DeltaDays);
                    float MaxAllowedErosion = FMath::Max(0.0f, Cell.Elevation - Manager->SeaLevel);
                    ActualErode = FMath::Min(ActualErode, MaxAllowedErosion);

                    if (ActualErode > 0.0f) {
                        Cell.ElevationDelta -= ActualErode;
                        Cell.SedimentDelta += ActualErode;
                        Chunk.AccumulatedTerrainChange += ActualErode;
                    }
                }
                else if (Cell.Sediment > SedimentCapacity && Cell.SurfaceWater > 0.05f) {
                    float Excess = Cell.Sediment - SedimentCapacity;
                    float DepositFactor = FMath::Lerp(0.2f, 1.0f, LowlandFactor) * FMath::Clamp(1.0f - (Slope / 2.0f), 0.5f, 1.0f);
                    float Deposit = Excess * DepositFactor * DepositionRate * DeltaDays;
                    Deposit = FMath::Min(Deposit, 0.05f * DeltaDays);

                    Cell.ElevationDelta += Deposit;
                    Cell.DepositedSediment += Deposit;
                    Cell.SedimentDelta -= Deposit;
                    Chunk.AccumulatedTerrainChange += Deposit;
                }

                float MySedimentOutflow = 0.0f;
                if (Cell.FlowDirectionGlobalX != -1) {
                    float TransportFactor = FMath::Lerp(0.8f, 0.2f, LowlandFactor);
                    MySedimentOutflow = FMath::Min(Cell.Sediment, Cell.WaterFlow * TransportFactor * DeltaDays);
                }
                Cell.SedimentDelta -= MySedimentOutflow;
            }
            });

        ParallelFor(GlobalEnd, [&](int32 idx) {
            FIntPoint Coord = ChunkKeys[idx];
            if (!WorldChunks.Contains(Coord)) return;
            FChunkData& Chunk = WorldChunks[Coord];

            for (int i = 0; i < Chunk.MicroCells.Num(); i++) {
                if ((i % Manager->ChunkSize) >= CSize || (i / Manager->ChunkSize) >= CSize) continue;

                Chunk.MicroCells[i].Elevation += Chunk.MicroCells[i].ElevationDelta;
                Chunk.MicroCells[i].Sediment += Chunk.MicroCells[i].SedimentDelta;
                Chunk.MicroCells[i].ElevationDelta = 0.0f;
                Chunk.MicroCells[i].SedimentDelta = 0.0f;
            }
            });

        ParallelFor(GlobalEnd, [&](int32 idx) {
            FIntPoint Coord = ChunkKeys[idx];
            if (!WorldChunks.Contains(Coord)) return;
            FChunkData& Chunk = WorldChunks[Coord];

            for (int i = 0; i < Chunk.MicroCells.Num(); i++) {
                int32 X = i % Manager->ChunkSize; int32 Y = i / Manager->ChunkSize;
                if (X >= CSize || Y >= CSize) continue;

                FCellData& Cell = Chunk.MicroCells[i];

                int32 GlobalX = (Coord.X * CSize) + X; int32 GlobalY = (Coord.Y * CSize) + Y;

                auto GetNeighbor = [&](int32 dx, int32 dy) -> const FCellData* {
                    int32 lx = X + dx;
                    int32 ly = Y + dy;
                    if (lx >= 0 && lx < CSize && ly >= 0 && ly < CSize) {
                        return &Chunk.MicroCells[lx + ly * Manager->ChunkSize];
                    }
                    const FCellData* ExtCell = nullptr;
                    Manager->GetCellGlobalPtr(GlobalX + dx, GlobalY + dy, ExtCell);
                    return ExtCell;
                    };

                float SedimentInflow = 0.0f;
                for (int32 n = 0; n < 8; n++) {
                    const FCellData* NCell = GetNeighbor(Offsets[n][0], Offsets[n][1]);
                    if (NCell && NCell->FlowDirectionGlobalX == GlobalX && NCell->FlowDirectionGlobalY == GlobalY) {
                        float N_ElevAboveSea = FMath::Max(0.0f, NCell->Elevation - Manager->SeaLevel);
                        float N_LowlandFactor = FMath::Clamp(1.0f - (N_ElevAboveSea / 250.0f), 0.0f, 1.0f);
                        float N_TransportFactor = FMath::Lerp(0.8f, 0.2f, N_LowlandFactor);

                        if (NCell->Elevation <= Manager->SeaLevel) N_TransportFactor = 0.9f;

                        float NFlow = (NCell->Elevation <= Manager->SeaLevel) ? 1.0f : NCell->WaterFlow;
                        float NSedimentOutflow = FMath::Min(NCell->Sediment, NFlow * N_TransportFactor * DeltaDays);
                        SedimentInflow += NSedimentOutflow;
                    }
                }
                Cell.Sediment += SedimentInflow;

                if (Cell.WaterType == EWaterType::Ocean || Cell.Elevation <= Manager->SeaLevel) continue;

                // OPRAVA: Ostøejší zamìøení toku – øeka bude užší, ale hlubší
                float TargetWidth = 1.0f + FMath::Clamp(Cell.RiverDischarge * 0.05f, 0.0f, 4.0f);
                float TargetDepth = 2.0f + FMath::Clamp(Cell.RiverDischarge * 0.15f, 0.0f, 8.0f);

                bool bIsFlooding = Cell.SurfaceWater > (Cell.ChannelDepth + Cell.BankHeight + 0.2f);
                if (bIsFlooding) {
                    Cell.ChannelWidth = FMath::Lerp(Cell.ChannelWidth, TargetWidth * 1.2f, 0.15f * DeltaDays);
                    Cell.ChannelDepth = FMath::Lerp(Cell.ChannelDepth, TargetDepth * 1.2f, 0.15f * DeltaDays);
                }
                else {
                    Cell.ChannelWidth = FMath::Lerp(Cell.ChannelWidth, TargetWidth, 0.05f * DeltaDays);
                    Cell.ChannelDepth = FMath::Lerp(Cell.ChannelDepth, TargetDepth, 0.05f * DeltaDays);
                }

                if (Cell.RiverDischarge > 1.0f && Cell.WaterFlow < 0.5f) {
                    Cell.BankHeight = FMath::Lerp(Cell.BankHeight, Cell.Sediment * 0.2f, 0.02f * DeltaDays);
                }
            }

            for (int32 step = 0; step < Manager->ChunkSize; step++) {
                int32 GlobalRightX = (ChunkKeys[idx].X * CSize) + CSize;
                int32 GlobalRightY = (ChunkKeys[idx].Y * CSize) + step;
                const FCellData* RealRight = nullptr;
                if (Manager->GetCellGlobalPtr(GlobalRightX, GlobalRightY, RealRight)) {
                    Chunk.MicroCells[CSize + step * Manager->ChunkSize] = *RealRight;
                }

                int32 GlobalBotX = (ChunkKeys[idx].X * CSize) + step;
                int32 GlobalBotY = (ChunkKeys[idx].Y * CSize) + CSize;
                const FCellData* RealBot = nullptr;
                if (Manager->GetCellGlobalPtr(GlobalBotX, GlobalBotY, RealBot)) {
                    Chunk.MicroCells[step + CSize * Manager->ChunkSize] = *RealBot;
                }
            }
            const FCellData* RealCorner = nullptr;
            if (Manager->GetCellGlobalPtr((ChunkKeys[idx].X * CSize) + CSize, (ChunkKeys[idx].Y * CSize) + CSize, RealCorner)) {
                Chunk.MicroCells[CSize + CSize * Manager->ChunkSize] = *RealCorner;
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

    FCellData* TargetCell = nullptr;
    FIntPoint ChunkCoord;
    if (!Manager->GetMutableCellGlobal(GlobalX, GlobalY, TargetCell, ChunkCoord)) return false;

    float GroundWaterReq = 40.0f;
    if (TargetCell->GroundWater < GroundWaterReq) {
        UE_LOG(LogTemp, Warning, TEXT("Nelze vytvorit pramen: Nedostatek spodni vody (%.2f < %.2f)"), TargetCell->GroundWater, GroundWaterReq);
        return false;
    }

    if (!Manager->ManaModule->SpendMana(150.0f, TEXT("Prorazeni pramene"))) return false;

    TargetCell->bIsSpring = true;
    TargetCell->WaterType = EWaterType::Spring;
    TargetCell->SpringStrength = 3.0f;
    TargetCell->GroundWater -= GroundWaterReq;

    Manager->RegisterVisualChange(ChunkCoord, EChunkVisualDirty::Water | EChunkVisualDirty::Terrain, true);

    int32 Radius = 12;
    for (int32 dy = -Radius; dy <= Radius; dy++) {
        for (int32 dx = -Radius; dx <= Radius; dx++) {
            if (dx == 0 && dy == 0) continue;

            FCellData* NeighborCell = nullptr;
            FIntPoint NChunkCoord;
            if (Manager->GetMutableCellGlobal(GlobalX + dx, GlobalY + dy, NeighborCell, NChunkCoord)) {

                NeighborCell->GroundWater = FMath::Max(0.0f, NeighborCell->GroundWater - 5.0f);

                if (NeighborCell->bIsSpring && NeighborCell->Elevation > TargetCell->Elevation) {
                    NeighborCell->SpringStrength *= 0.5f;

                    if (NeighborCell->SpringStrength < 0.5f) {
                        NeighborCell->bIsSpring = false;
                        NeighborCell->WaterType = EWaterType::Surface;
                    }
                }
            }
        }
    }

    return true;
}