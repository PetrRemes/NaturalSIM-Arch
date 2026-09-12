#include "WorldGeneratorSystem.h"
#include "SimWorldManager.h"
#include "NoiseUtils.h"
#include "Async/ParallelFor.h"

UWorldGeneratorSystem::UWorldGeneratorSystem() { PrimaryComponentTick.bCanEverTick = false; }
void UWorldGeneratorSystem::BeginPlay() { Super::BeginPlay(); }

static void GetZonedTerrain(float GlobalX, float GlobalY, const FChunkGenerationParameters& Params, float& OutElev, bool& OutVolcano, float& OutTectonicPressure, float& OutLava)
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

        float D = FMath::Sqrt(FVector2D::DistSquared(FVector2D(WarpedX, WarpedY), EffCenter)) / FMath::Max(0.1f, RadiusMult);

        if (D < Dist1) { Dist2 = Dist1; Dist1 = D; }
        else if (D < Dist2) { Dist2 = D; }
    }

    float CoastSlider = FMath::Clamp(Params.CoastlineRoughness, 0.0f, 1.0f);
    float PlainsSlider = FMath::Clamp(Params.LowlandFlatness, 0.0f, 1.0f);
    float MountSlider = FMath::Clamp(Params.ElevationMultiplier / 6000.0f, 0.0f, 1.0f);
    float TectSlider = FMath::Clamp(Params.TectonicMountainHeight / 6000.0f, 0.0f, 1.0f);

    float SizeModifier = Params.ContinentCount <= 1 ? 1.0f : (1.4f / FMath::Sqrt((float)Params.ContinentCount));
    float MaxDistBase = (WorldCellsX + WorldCellsY) * 0.38f * FMath::Max(0.2f, Params.ContinentSizeMultiplier) * SizeModifier;

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

    OutTectonicPressure = TectonicPressure;

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

        float OceanOnlyMask = 1.0f - FMath::SmoothStep(T_Deep - 0.1f, T_Deep + 0.1f, Topo);
        if (OceanOnlyMask > 0.0f && IslandNoise > Thresh) {
            float iMask = FMath::SmoothStep(0.0f, 1.0f, (IslandNoise - Thresh) / (1.0f - Thresh));
            iMask *= OceanOnlyMask;
            float IslandPeak = FMath::Lerp(30.0f, 600.0f, MountSlider);
            Elev = FMath::Max(Elev, FMath::Lerp(-200.0f, IslandPeak, iMask));
        }
    }

    OutVolcano = false;
    OutLava = 0.0f;

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

                    float DistSq = FVector2D::DistSquared(FVector2D(GlobalX, GlobalY), FVector2D(VolcX, VolcY));
                    float VolcRadius = 25.0f;
                    float VolcRadiusSq = VolcRadius * VolcRadius;

                    if (DistSq <= VolcRadiusSq) {
                        float Dist = FMath::Sqrt(DistSq);
                        float Falloff = FMath::SmoothStep(0.0f, 1.0f, 1.0f - (Dist / VolcRadius));
                        Elev += (Params.LavaHeightBoost * Falloff);

                        if (Dist <= 3.0f) {
                            OutVolcano = true;
                            OutLava = 100.0f;
                        }
                        else if (Falloff > 0.15f) {
                            float LavaRiverNoise = GetFBM(GlobalX, GlobalY, Params.NoiseScale * 25.0f, 2, Params.MapSeed + 999);
                            if (FMath::Abs(LavaRiverNoise) < 0.12f) {
                                OutLava = Falloff * 100.0f;
                            }
                        }
                    }
                }
            }
        }
    }

    OutElev = Elev;
}

void UWorldGeneratorSystem::GenerateGlobalTerrainCache(TArray<FPrecomputedTerrain>& OutCache, const FChunkGenerationParameters& Params)
{
    ParallelFor(Params.TotalWorldCellsY, [&](int32 Y) {
        for (int32 X = 0; X < Params.TotalWorldCellsX; X++) {
            float Elev, Tect, Lava;
            bool Volc;
            GetZonedTerrain(X, Y, Params, Elev, Volc, Tect, Lava);

            int32 Idx = Y * Params.TotalWorldCellsX + X;
            OutCache[Idx].Elevation = Elev;
            OutCache[Idx].bIsVolcano = Volc;
            OutCache[Idx].TectonicPressure = Tect;
            OutCache[Idx].LavaAmount = Lava;
        }
        });
}

void UWorldGeneratorSystem::ProcessChunkTerrain(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    if (OutChunk.StaticCells.Num() == 0 || OutChunk.DynamicCells.Num() == 0) return;

    int32 ChunkSize = FMath::RoundToInt(FMath::Sqrt((float)OutChunk.StaticCells.Num()));
    float TotalTectPressure = 0.0f;

    for (int32 Y = 0; Y < ChunkSize; Y++)
    {
        for (int32 X = 0; X < ChunkSize; X++)
        {
            int32 i = X + Y * ChunkSize;
            if (i >= OutChunk.StaticCells.Num()) continue;

            FCellStaticData& SCell = OutChunk.StaticCells[i];
            FCellDynamicData& DCell = OutChunk.DynamicCells[i];

            int32 GlobalX = (ChunkCoord.X * (ChunkSize - 1)) + X;
            int32 GlobalY = (ChunkCoord.Y * (ChunkSize - 1)) + Y;

            int32 LookupX = FMath::Clamp(GlobalX, 0, Params.TotalWorldCellsX - 1);
            int32 LookupY = FMath::Clamp(GlobalY, 0, Params.TotalWorldCellsY - 1);

            FPrecomputedTerrain CachedT;
            if (Params.GlobalTerrainCache.IsValid()) {
                CachedT = Params.GlobalTerrainCache.Get()->GetData()[LookupY * Params.TotalWorldCellsX + LookupX];
            }

            SCell.Elevation = CachedT.Elevation;
            SCell.bIsVolcano = CachedT.bIsVolcano;
            DCell.Lava = CachedT.LavaAmount;
            TotalTectPressure += CachedT.TectonicPressure;

            if (SCell.bIsVolcano || DCell.Lava > 0.1f) {
                if (SCell.bIsVolcano) DCell.Lava = 100.0f;
                SCell.Bedrock = EBedrockType::Rock;
            }
            else {
                SCell.Bedrock = (SCell.Elevation > Params.SeaLevel + 300.0f) ? EBedrockType::Rock : EBedrockType::Dirt;
            }

            if (SCell.Elevation <= Params.SeaLevel + 25.0f && SCell.Elevation >= Params.SeaLevel - 15.0f) {
                SCell.Bedrock = EBedrockType::Sand;
            }

            SCell.StoneAmount = 0.0f;
            SCell.MineralOre = 0.0f;
            SCell.ClayAmount = 0.0f;
            SCell.SandAmount = 0.0f;

            if (SCell.Bedrock == EBedrockType::Rock) {
                float StoneNoise = GetFBM(GlobalX, GlobalY, Params.NoiseScale * 8.0f, 3, Params.MapSeed + 111);
                float BaseStone = FMath::Max(0.0f, SCell.Elevation - (Params.SeaLevel + 200.0f));
                SCell.StoneAmount = 5000.0f + (BaseStone * 15.0f) + (StoneNoise * 2000.0f);
            }

            float OreNoise = GetFBM(GlobalX, GlobalY, Params.NoiseScale * 25.0f, 2, Params.MapSeed + 888);
            float OreThreshold = FMath::Lerp(0.92f, 0.70f, CachedT.TectonicPressure);
            if (SCell.bIsVolcano) OreThreshold -= 0.15f;

            if (OreNoise > OreThreshold) {
                SCell.MineralOre = (OreNoise - OreThreshold) * 15000.0f;
            }

            if (SCell.Bedrock == EBedrockType::Sand) {
                float SandNoise = GetFBM(GlobalX, GlobalY, Params.NoiseScale * 5.0f, 2, Params.MapSeed + 666);
                SCell.SandAmount = 2000.0f + (SandNoise * 1500.0f);
            }

            if (SCell.Bedrock == EBedrockType::Dirt && SCell.Elevation < Params.SeaLevel + 60.0f && SCell.Elevation > Params.SeaLevel) {
                float ClayNoise = GetFBM(GlobalX, GlobalY, Params.NoiseScale * 4.0f, 3, Params.MapSeed + 555);
                if (ClayNoise > 0.65f) {
                    SCell.ClayAmount = (ClayNoise - 0.65f) * 8000.0f;
                }
            }
        }
    }

    OutChunk.BaseTectonicPressure = TotalTectPressure / OutChunk.StaticCells.Num();
    OutChunk.FaultStress = FMath::FRandRange(0.0f, 50.0f);
}