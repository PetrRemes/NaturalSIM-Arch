#include "FloraSystem.h"
#include "SimWorldManager.h"
#include "ManaSystem.h"
#include "Async/ParallelFor.h"

UFloraSystem::UFloraSystem() { PrimaryComponentTick.bCanEverTick = false; }
void UFloraSystem::BeginPlay() { Super::BeginPlay(); }

const UFloraSpeciesData* UFloraSystem::GetSpeciesByID(uint8 ID) const
{
    if (ID == 0) return nullptr;
    for (const UFloraSpeciesData* Species : SpeciesDatabase) {
        if (Species && Species->SpeciesID == ID) return Species;
    }
    return nullptr;
}

const UFloraSpeciesData* UFloraSystem::GetBestSpeciesForEnvironment(EBiomeType Biome, float Toxicity, float SoilDepth, FRandomStream& Stream) const
{
    if (SpeciesDatabase.Num() == 0) return nullptr;

    TArray<const UFloraSpeciesData*> ValidCandidates;
    float BestScore = -9999.0f;

    for (const UFloraSpeciesData* Species : SpeciesDatabase) {
        if (!Species) continue;

        float Score = 0.0f;

        if (Toxicity > Species->MaxPollutionTolerance && Species->PollutionAffinity <= 0.0f) continue;
        if (Toxicity > 0.05f && Species->PollutionAffinity > 0.0f) Score += Toxicity * Species->PollutionAffinity * 200.0f;

        if (Species->RootPower > 1.5f && SoilDepth < 0.5f) Score -= 50.0f;
        if (Species->GrowthSpeed > 1.5f && SoilDepth < 0.5f) Score += 30.0f;

        if (Score > BestScore) {
            BestScore = Score;
            ValidCandidates.Empty();
            ValidCandidates.Add(Species);
        }
        else if (FMath::IsNearlyEqual(Score, BestScore, 5.0f)) {
            ValidCandidates.Add(Species);
        }
    }

    if (ValidCandidates.Num() > 0) {
        return ValidCandidates[Stream.RandRange(0, ValidCandidates.Num() - 1)];
    }
    return nullptr;
}

void UFloraSystem::ProcessChunkFlora(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    float SeaLevel = Params.SeaLevel; int32 ChunkSize = Params.ChunkSize;
    if (ChunkSize <= 0) return;

    ParallelFor(ChunkSize, [&](int32 Y) {
        for (int32 X = 0; X < ChunkSize; X++) {
            int32 i = X + Y * ChunkSize;
            if (i >= OutChunk.StaticCells.Num()) continue;

            FCellStaticData& SCell = OutChunk.StaticCells[i];
            FCellDynamicData& DCell = OutChunk.DynamicCells[i];

            int32 GlobalX = FMath::RoundToInt(ChunkCoord.X * (ChunkSize - 1)) + X;
            int32 GlobalY = FMath::RoundToInt(ChunkCoord.Y * (ChunkSize - 1)) + Y;

            if (SCell.Elevation <= SeaLevel) {
                SCell.Biome = EBiomeType::Barren; SCell.TreeType = ETreeType::None; DCell.FloraDensity = 0.0f;
                DCell.EdibleFlora = 0.0f; DCell.BerryBushes = 0.0f; SCell.BiomeColor = GetBiomeColor(SCell.Biome);
                continue;
            }

            float Altitude = SCell.Elevation - SeaLevel;
            float LocalSlope = FMath::Abs(FMath::PerlinNoise2D(FVector2D(GlobalX * 0.05f, GlobalY * 0.05f)) * 0.3f);

            bool bIsCoast = Altitude <= 12.0f;
            bool bIsShore = DCell.SurfaceWater > 0.01f && DCell.SurfaceWater < 0.2f;
            if ((bIsCoast || bIsShore) && SCell.Bedrock != EBedrockType::Rock && LocalSlope < 0.08f) {
                SCell.Bedrock = EBedrockType::Sand;
            }

            float WaterAvailability = (DCell.GroundWater / 100.0f) + DCell.Rainfall + 0.25f;
            if (DCell.SurfaceWater > 0.05f || DCell.RiverDischarge > 0.5f) {
                WaterAvailability += 1.0f;
            }

            float CoastalFactor = FMath::Clamp(1.0f - (Altitude / 5.0f), 0.0f, 1.0f);
            float RiverFactor = FMath::Clamp(DCell.RiverDischarge / 5.0f, 0.0f, 1.0f);
            DCell.WetlandScore = (DCell.Wetness * 0.35f) + (CoastalFactor * 0.25f) + (RiverFactor * 0.25f);

            bool bIsSpecialBiome = false;

            bool bIsSwampy = DCell.WetlandScore > 0.65f && Altitude < 4.0f && SCell.Bedrock != EBedrockType::Rock;

            if (bIsSwampy) {
                SCell.Biome = EBiomeType::Swamp; DCell.FloraDensity = 0.5f; SCell.TreeType = ETreeType::None; DCell.EdibleFlora = 20.0f;
                bIsSpecialBiome = true;
            }
            else if (SCell.Bedrock == EBedrockType::Sand) {
                SCell.Biome = EBiomeType::Beach; SCell.TreeType = ETreeType::None; DCell.FloraDensity = 0.0f; DCell.EdibleFlora = 0.0f;
                bIsSpecialBiome = true;
            }

            float MixNoise = FMath::PerlinNoise2D(FVector2D(GlobalX * 0.015f, GlobalY * 0.015f)) * 300.0f;
            float EffAlt = FMath::Max(0.0f, Altitude + MixNoise);

            uint32 HashSeed = Params.MapSeed + (GlobalX * 374761393U) + (GlobalY * 668265263U);
            HashSeed = (HashSeed ^ (HashSeed >> 13)) * 1274126177U;
            FRandomStream InitialStream(HashSeed);

            if (!bIsSpecialBiome) {
                SCell.Biome = DetermineBiome(DCell.Temperature, WaterAvailability, EffAlt, DCell.FloraDensity, Params.GrasslandSpread);

                float MaxCapacity = FMath::Clamp(WaterAvailability * 1.5f, 0.0f, 1.0f);
                if (Altitude > 1200.0f) MaxCapacity *= FMath::Clamp(1.0f - ((Altitude - 1200.0f) / 500.0f), 0.1f, 1.0f);

                if (Params.bIsFullGeneration) {
                    DCell.FloraDensity = MaxCapacity * InitialStream.FRandRange(0.6f, 1.0f);
                    float ClusterNoise = FMath::PerlinNoise2D(FVector2D(GlobalX * 0.05f, GlobalY * 0.05f));

                    if (ClusterNoise > -0.2f && (SCell.Biome == EBiomeType::ConiferousForest || SCell.Biome == EBiomeType::DeciduousForest || SCell.Biome == EBiomeType::TropicalForest)) {
                        DCell.WoodAmount = DCell.FloraDensity * 300.0f;
                    }
                    else {
                        DCell.WoodAmount = 0.0f;
                        if (SCell.Biome == EBiomeType::ConiferousForest || SCell.Biome == EBiomeType::DeciduousForest || SCell.Biome == EBiomeType::TropicalForest) {
                            SCell.Biome = EBiomeType::Grassland;
                        }
                    }
                    if (WaterAvailability > 0.5f && InitialStream.FRand() > 0.75f) DCell.BerryBushes = 10.0f; else DCell.BerryBushes = 0.0f;
                }

                if (DCell.WoodAmount > 50.0f && (SCell.Biome == EBiomeType::ConiferousForest || SCell.Biome == EBiomeType::DeciduousForest || SCell.Biome == EBiomeType::TropicalForest)) {
                    float SpeciesRand = InitialStream.FRand();
                    if (DCell.Temperature > 22.0f && EffAlt < 400.0f) {
                        SCell.TreeType = ETreeType::Jungle;
                        SCell.Biome = EBiomeType::TropicalForest;
                    }
                    else if (DCell.Temperature > 14.0f && EffAlt < 700.0f) {
                        SCell.TreeType = (SpeciesRand > 0.4f) ? ETreeType::Oak : ETreeType::Birch;
                        if (SpeciesRand > 0.85f && EffAlt > 500.0f) SCell.TreeType = ETreeType::Pine;
                        SCell.Biome = EBiomeType::DeciduousForest;
                    }
                    else {
                        SCell.TreeType = (SpeciesRand > 0.4f) ? ETreeType::Spruce : ETreeType::Pine;
                        if (SpeciesRand > 0.85f && EffAlt < 900.0f) SCell.TreeType = ETreeType::Birch;
                        SCell.Biome = EBiomeType::ConiferousForest;
                    }
                    SCell.TreeSpeciesID = 0;
                    DCell.TreeAge = InitialStream.FRandRange(10.0f, 100.0f);
                }
                else {
                    SCell.TreeType = ETreeType::None;
                    DCell.WoodAmount = 0.0f;
                    SCell.TreeSpeciesID = 0;
                }
            }

            FLinearColor StartColor = GetBiomeColor(SCell.Biome);
            if (SCell.Biome == EBiomeType::Grassland) {
                float Hum = FMath::Clamp(DCell.Humidity, 0.0f, 1.0f);
                StartColor.R *= FMath::Lerp(1.0f, 0.4f, Hum); StartColor.G *= FMath::Lerp(1.0f, 0.7f, Hum); StartColor.B *= FMath::Lerp(1.0f, 0.4f, Hum);
            }

            if (DCell.SnowAmount > 0.05f || DCell.Temperature <= 0.0f) {
                float FreezeAlpha = (DCell.Temperature <= 0.0f) ? FMath::Clamp(-DCell.Temperature / 15.0f, 0.0f, 1.0f) : 0.0f;
                float SnowAlpha = FMath::Clamp((DCell.SnowAmount / 2.0f) + FreezeAlpha, 0.0f, 0.85f);
                float SnowShade = 0.85f + (FMath::PerlinNoise2D(FVector2D(GlobalX * 0.1f, GlobalY * 0.1f)) * 0.15f);
                FLinearColor SnowColor(0.95f * SnowShade, 0.98f * SnowShade, 1.0f * SnowShade, 1.0f);
                StartColor = FMath::Lerp(StartColor, SnowColor, SnowAlpha);
            }

            if (DCell.Lava > 0.0f) {
                StartColor = FMath::Lerp(StartColor, FLinearColor(1.0f, 0.35f, 0.0f, 1.0f), FMath::Clamp(DCell.Lava, 0.0f, 1.0f));
            }

            SCell.BiomeColor = StartColor;

            if (SCell.Bedrock == EBedrockType::Sand) {
                SCell.SoilType = ESoilType::Sand; SCell.SoilDepth = 0.2f; DCell.SoilFertility = 0.05f;
                DCell.SoilStability = 0.8f;
            }
            else if (SCell.Bedrock == EBedrockType::Rock) {
                SCell.SoilType = ESoilType::Rock; SCell.SoilDepth = 0.05f; DCell.SoilFertility = 0.01f;
                DCell.SoilStability = 1.0f;
            }
            else {
                SCell.SoilType = SCell.Biome == EBiomeType::Swamp ? ESoilType::Mud : ESoilType::Dirt;
                SCell.SoilDepth = FMath::FRandRange(0.5f, 1.5f);
                DCell.SoilFertility = FMath::FRandRange(0.3f, 0.7f);
                DCell.SoilStability = SCell.Biome == EBiomeType::Swamp ? 0.3f : 0.9f;
            }
            DCell.SoilMoisture = FMath::Clamp((DCell.GroundWater / 100.0f) * 0.5f + DCell.Rainfall * 0.5f, 0.0f, 1.0f);
            DCell.OrganicMatter = (SCell.TreeType != ETreeType::None || SCell.Biome == EBiomeType::Swamp) ? 0.6f : 0.2f;
            DCell.ForestDensity = DCell.WoodAmount / 250.0f;
            DCell.TreeSeedBank = (SCell.TreeType != ETreeType::None) ? 1.0f : 0.0f;
            DCell.ShrubSeedBank = (DCell.BerryBushes > 0.0f) ? 1.0f : 0.0f;
            DCell.GrassDensity = (SCell.Biome == EBiomeType::Grassland || SCell.Biome == EBiomeType::Swamp) ? 0.8f : 0.2f;
            DCell.ShrubDensity = (DCell.BerryBushes > 0.0f) ? 0.5f : 0.0f;
            DCell.SoilCompaction = 0.0f;
        }
        });
}

void UFloraSystem::ProcessDailyGrowth(const TArray<FIntPoint>& ChunkKeys, TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, int32 StartIdx, int32 EndIdx, float DeltaDays)
{
    if (!Manager || DeltaDays <= 0.0f) return;

    bool bRunColorUpdate = (Manager->CurrentDay % 5 == 0);
    int32 CSize = Manager->ChunkSize - 1;

    FCriticalSection ManaMutex;
    TArray<uint8> SafeFlags;
    SafeFlags.Init(0, EndIdx - StartIdx);

    ParallelFor(EndIdx - StartIdx, [&](int32 iter) {
        int32 idx = StartIdx + iter;
        if (!ChunkKeys.IsValidIndex(idx)) return;
        FChunkData& Chunk = WorldChunks[ChunkKeys[idx]];
        uint8 LocalDirty = 0;

        float LocalBirths = 0.0f;
        float LocalDeaths = 0.0f;
        int32 Offsets[4][2] = { {0,1}, {1,0}, {0,-1}, {-1,0} };

        for (int32 Y = 0; Y < CSize; Y++) {
            for (int32 X = 0; X < CSize; X++) {
                int32 i = X + Y * Manager->ChunkSize;

                FCellStaticData& SCell = Chunk.StaticCells[i];
                FCellDynamicData& DCell = Chunk.DynamicCells[i];
                int32 GlobalX = (ChunkKeys[idx].X * CSize) + X; int32 GlobalY = (ChunkKeys[idx].Y * CSize) + Y;

                uint32 HashSeed = Manager->MapSeed + (GlobalX * 374761393U) + (GlobalY * 668265263U) + Manager->CurrentDay;
                HashSeed = (HashSeed ^ (HashSeed >> 13)) * 1274126177U;
                FRandomStream CellStream(HashSeed);

                auto GetNeighbor = [&](int32 dx, int32 dy, const FCellStaticData*& OutS, const FCellDynamicData*& OutD) {
                    int32 lx = X + dx; int32 ly = Y + dy;
                    if (lx >= 0 && lx < CSize && ly >= 0 && ly < CSize) {
                        int32 nIdx = lx + ly * Manager->ChunkSize;
                        OutS = &Chunk.StaticCells[nIdx];
                        OutD = &Chunk.DynamicCells[nIdx];
                        return;
                    }
                    FIntPoint Dummy; FCellStaticData* S = nullptr; FCellDynamicData* D = nullptr;
                    Manager->GetMutableCellGlobal(GlobalX + dx, GlobalY + dy, S, D, Dummy);
                    OutS = S; OutD = D;
                    };

                ETreeType OldTree = SCell.TreeType;
                float OldFlora = DCell.FloraDensity;
                float OldWood = DCell.WoodAmount;
                float Altitude = SCell.Elevation - Manager->SeaLevel;

                DCell.FireIntensityBuffer = DCell.FireIntensity;

                float Toxicity = FMath::Clamp((DCell.WaterPollution * 1.5f) + (DCell.AshDensityBuffer * 0.25f), 0.0f, 1.0f);
                float MixNoise = FMath::PerlinNoise2D(FVector2D(GlobalX * 0.015f, GlobalY * 0.015f)) * 300.0f;
                float EffAlt = FMath::Max(0.0f, Altitude + MixNoise);

                float WaterAvailability = (DCell.GroundWater / 100.0f) * 0.6f + (DCell.Rainfall * 0.4f);
                bool bIsWetland = (DCell.SurfaceWater > 0.02f || DCell.RiverDischarge > 0.5f);
                if (bIsWetland) WaterAvailability += 0.8f;

                EBiomeType TargetBiome = DetermineBiome(DCell.Temperature, WaterAvailability, EffAlt, DCell.FloraDensity, Manager->GrasslandSpread);

                float CoastalFactor = FMath::Clamp(1.0f - (Altitude / 5.0f), 0.0f, 1.0f);
                float RiverFactor = FMath::Clamp(DCell.RiverDischarge / 5.0f, 0.0f, 1.0f);
                DCell.WetlandScore = (DCell.Wetness * 0.35f) + (CoastalFactor * 0.25f) + (SCell.SoilDepth * 0.15f) + (RiverFactor * 0.25f);
                bool bIsSwampy = DCell.WetlandScore > 0.65f && Altitude < 4.0f && SCell.Bedrock != EBedrockType::Rock;

                if (bIsSwampy) TargetBiome = EBiomeType::Swamp;
                else if (SCell.Bedrock == EBedrockType::Sand && Altitude <= 15.0f) TargetBiome = EBiomeType::Beach;
                if (DCell.GlacierIce > 1.0f || SCell.bIsVolcano || DCell.Lava > 0.1f || (DCell.FireIntensity > 0.5f && DCell.WoodAmount <= 0.0f)) {
                    TargetBiome = EBiomeType::Barren;
                }

                if (DCell.DepositedSediment > 0.0f) {
                    float SoilFormation = FMath::Min(DCell.DepositedSediment, 0.05f * DeltaDays);
                    DCell.DepositedSediment -= SoilFormation;
                    SCell.SoilDepth = FMath::Min(3.0f, SCell.SoilDepth + SoilFormation);
                    DCell.OrganicMatter = FMath::Min(1.0f, DCell.OrganicMatter + SoilFormation * 0.1f);
                }

                DCell.SoilMoisture = FMath::Lerp(DCell.SoilMoisture, FMath::Clamp((DCell.GroundWater / 100.0f) + DCell.Wetness + (DCell.SurfaceWater > 0.05f ? 1.0f : 0.0f), 0.0f, 1.0f), 0.1f * DeltaDays);
                float OrganicGain = ((DCell.ForestDensity * 0.005f) + (DCell.GrassDensity * 0.002f)) * DeltaDays;
                if (bIsSwampy) OrganicGain += 0.015f * DeltaDays;

                DCell.OrganicMatter = FMath::Clamp(DCell.OrganicMatter + OrganicGain, 0.0f, 1.0f);

                if (SCell.SoilType == ESoilType::Sand && DCell.OrganicMatter > 0.3f && Altitude > 15.0f) SCell.SoilType = ESoilType::Dirt;
                DCell.SoilFertility = FMath::Clamp((DCell.OrganicMatter * 0.5f) + (DCell.Sediment * 0.1f), 0.0f, 1.0f);
                SCell.SoilDepth = FMath::Clamp(SCell.SoilDepth + ((DCell.OrganicMatter * 0.01f) + (DCell.Sediment * 0.02f)) * DeltaDays, 0.0f, 2.0f);

                if (SCell.Bedrock == EBedrockType::Rock) {
                    DCell.SoilStability = 1.0f;
                }
                else {
                    float WaterPenalty = (DCell.SurfaceWater > 0.05f) ? 0.3f : 0.0f;
                    float SwampPenalty = bIsSwampy ? 0.4f : 0.0f;
                    float RootBonus = (DCell.ForestDensity * 0.3f) + (DCell.GrassDensity * 0.1f);
                    DCell.SoilStability = FMath::Clamp(0.8f - WaterPenalty - SwampPenalty + RootBonus + (SCell.SoilDepth * 0.05f), 0.05f, 1.0f);
                }

                if (SCell.Elevation <= Manager->SeaLevel || DCell.SurfaceWater >= 1.5f) {
                    if (SCell.TreeType != ETreeType::None) {
                        LocalDeaths += 1.0f * DeltaDays;
                        SCell.TreeType = ETreeType::None; SCell.TreeSpeciesID = 0; DCell.WoodAmount = 0.0f; DCell.ForestDensity = 0.0f;
                        DCell.GrassDensity = 0.0f; DCell.ShrubDensity = 0.0f;
                        LocalDirty |= EChunkVisualDirty::Flora;
                    }
                    continue;
                }

                float LocalSlope = 0.0f;
                for (int32 dir = 0; dir < 4; dir++) {
                    const FCellStaticData* NCellS = nullptr; const FCellDynamicData* NCellD = nullptr;
                    GetNeighbor(Offsets[dir][0], Offsets[dir][1], NCellS, NCellD);
                    if (NCellS) { float s = FMath::Abs(SCell.Elevation - NCellS->Elevation); if (s > LocalSlope) LocalSlope = s; }
                }
                LocalSlope *= 0.02f;

                if (bIsSwampy) {
                    DCell.SoilCompaction = FMath::Max(0.0f, DCell.SoilCompaction - 0.02f * DeltaDays);
                    if (DCell.FloraDensity > 0.6f && DCell.SurfaceWater > 0.0f) {
                        DCell.SurfaceWater = FMath::Max(0.0f, DCell.SurfaceWater - (0.05f * Manager->FloraGrowthSpeed * DeltaDays));
                    }
                }

                float SoilQuality = (SCell.Bedrock == EBedrockType::Dirt) ? 0.6f : 0.2f;
                SoilQuality += FMath::Clamp(SCell.ClayAmount / 10000.0f, 0.0f, 0.3f);

                float OrographicBonus = 0.0f;
                if (Altitude < 250.0f) OrographicBonus = -0.3f;
                else if (Altitude < 1200.0f) OrographicBonus = 0.5f;
                else OrographicBonus = -1.0f * ((Altitude - 1200.0f) / 400.0f);

                float ForestSuitability = WaterAvailability + SoilQuality - LocalSlope + (FMath::PerlinNoise2D(FVector2D(GlobalX * 0.02f, GlobalY * 0.02f)) * 0.3f) + OrographicBonus;
                float MeadowNoise = FMath::PerlinNoise2D(FVector2D(GlobalX * 0.025f, GlobalY * 0.025f));
                bool bIsMeadow = MeadowNoise > 0.2f;

                if (DCell.GrazingPressure > 0.5f) {
                    DCell.GrassDensity = FMath::Max(0.1f, DCell.GrassDensity - (0.05f * DeltaDays));
                    DCell.ShrubDensity *= FMath::Pow(0.9f, DeltaDays);
                    DCell.TreeSeedBank *= FMath::Pow(0.5f, DeltaDays);
                    if (DCell.WoodAmount < 150.0f && SCell.TreeType != ETreeType::None) {
                        DCell.WoodAmount -= DCell.GrazingPressure * DeltaDays;
                        if (DCell.WoodAmount <= 0.0f) {
                            SCell.TreeType = ETreeType::None; SCell.TreeSpeciesID = 0; LocalDeaths += 1.0f * DeltaDays; LocalDirty |= EChunkVisualDirty::Flora;
                        }
                    }
                }

                float NeighborSeeds = 0.0f, NeighborShrubs = 0.0f;
                for (int32 dir = 0; dir < 4; dir++) {
                    const FCellStaticData* NCellS = nullptr; const FCellDynamicData* NCellD = nullptr;
                    GetNeighbor(Offsets[dir][0], Offsets[dir][1], NCellS, NCellD);
                    if (NCellD) {
                        if (NCellD->WoodAmount > 100.0f) NeighborSeeds += 0.05f;
                        if (NCellD->ShrubDensity > 0.5f) NeighborShrubs += 0.05f;
                    }
                }
                DCell.TreeSeedBank = FMath::Clamp(DCell.TreeSeedBank + (NeighborSeeds - 0.01f) * DeltaDays, 0.0f, 1.0f);
                DCell.ShrubSeedBank = FMath::Clamp(DCell.ShrubSeedBank + (NeighborShrubs - 0.01f) * DeltaDays, 0.0f, 1.0f);
                DCell.SeedSpread = DCell.TreeSeedBank;

                float GrowthSpeed = Manager->FloraGrowthSpeed * 0.01f * DeltaDays;

                if (DCell.HouseDensity > 0.0f || SCell.bHasRoad || SCell.BuildingType == EBuildingType::Mine || SCell.BuildingType == EBuildingType::Factory) {
                    DCell.FloraDensity = 0.0f; DCell.GrassDensity = 0.0f; DCell.ShrubDensity = 0.0f; DCell.ForestDensity = 0.0f;
                    if (SCell.TreeType != ETreeType::None) { SCell.TreeType = ETreeType::None; SCell.TreeSpeciesID = 0; DCell.WoodAmount = 0.0f; LocalDirty |= EChunkVisualDirty::Flora; }
                    DCell.HouseDensity = FMath::Max(0.0f, DCell.HouseDensity - 0.005f * DeltaDays);
                }
                else if (SCell.BuildingType != EBuildingType::Farm && SCell.BuildingType != EBuildingType::EcoFarm) {

                    float GrassCap = 1.0f - DCell.ForestDensity - (DCell.ShrubDensity * 0.5f);
                    if (SCell.Bedrock == EBedrockType::Sand) GrassCap = 0.0f;
                    DCell.GrassDensity = FMath::Clamp(DCell.GrassDensity + GrowthSpeed * 5.0f, 0.0f, GrassCap);

                    if (!bIsSwampy) {
                        float BushNoise = FMath::PerlinNoise2D(FVector2D(GlobalX * 0.08f, GlobalY * 0.08f));
                        if (BushNoise > 0.4f && DCell.ForestDensity < 0.5f) {
                            DCell.ShrubDensity = FMath::Min(0.8f, DCell.ShrubDensity + GrowthSpeed);
                            if (DCell.BerryBushes < 20.0f) DCell.BerryBushes += GrowthSpeed * 50.0f;
                        }
                        else {
                            DCell.ShrubDensity = FMath::Max(0.0f, DCell.ShrubDensity - GrowthSpeed);
                            DCell.BerryBushes = FMath::Max(0.0f, DCell.BerryBushes - GrowthSpeed * 50.0f);
                        }
                    }

                    const UFloraSpeciesData* Species = nullptr;
                    if (SCell.TreeType != ETreeType::None) {
                        if (SCell.TreeSpeciesID == 0) {
                            switch (SCell.TreeType) {
                            case ETreeType::Oak: SCell.TreeSpeciesID = 1; break;
                            case ETreeType::Birch: SCell.TreeSpeciesID = 2; break;
                            case ETreeType::Spruce: SCell.TreeSpeciesID = 4; break;
                            case ETreeType::Pine: SCell.TreeSpeciesID = 5; break;
                            case ETreeType::Jungle: SCell.TreeSpeciesID = 6; break;
                            default: SCell.TreeSpeciesID = 1; break;
                            }
                        }
                        Species = GetSpeciesByID(SCell.TreeSpeciesID);
                    }

                    float CurrentGrowthSpeed = GrowthSpeed;
                    float DeathRate = 1.0f;

                    if (Species) {
                        CurrentGrowthSpeed *= Species->GrowthSpeed;

                        if (Species->ToxinAbsorptionRate > 0.0f && DCell.WaterPollution > 0.0f) {
                            DCell.WaterPollution = FMath::Max(0.0f, DCell.WaterPollution - Species->ToxinAbsorptionRate * DeltaDays);
                            LocalDirty |= EChunkVisualDirty::TerrainColor;
                        }
                        if (Species->RootPower > 1.0f) {
                            SCell.SoilDepth = FMath::Min(3.0f, SCell.SoilDepth + (Species->RootPower * 0.005f * DeltaDays));
                        }

                        DCell.TreeAge += DeltaDays / 365.0f;
                        if (DCell.TreeAge > Species->MaxLifespanYears) DeathRate = 15.0f;

                        if (Toxicity > Species->MaxPollutionTolerance) {
                            if (Species->PollutionAffinity > 0.0f) {
                                CurrentGrowthSpeed *= (1.0f + (Toxicity * Species->PollutionAffinity));
                            }
                            else {
                                DeathRate = 25.0f;
                            }
                        }
                    }
                    else if (Toxicity > 0.15f) {
                        DeathRate = 25.0f;
                    }

                    float EffectiveSeedSpread = DCell.SeedSpread;
                    if (EffectiveSeedSpread < 0.05f && ForestSuitability > 0.7f && TargetBiome != EBiomeType::Desert && DCell.SoilFertility > 0.3f) {
                        EffectiveSeedSpread = 0.1f;
                    }

                    if (SCell.TreeType == ETreeType::None && EffectiveSeedSpread > 0.05f && ForestSuitability > 0.6f && !bIsMeadow) {
                        if (CellStream.FRand() < (0.015f * EffectiveSeedSpread * Manager->FloraGrowthSpeed * DeltaDays)) {

                            const UFloraSpeciesData* NewSpecies = GetBestSpeciesForEnvironment(TargetBiome, Toxicity, SCell.SoilDepth, CellStream);
                            if (NewSpecies) {
                                SCell.TreeSpeciesID = NewSpecies->SpeciesID;
                                SCell.TreeType = NewSpecies->VisualModel;
                            }
                            else {
                                if (DCell.Temperature > 22.0f && EffAlt < 400.0f) { SCell.TreeType = ETreeType::Jungle; SCell.TreeSpeciesID = 6; }
                                else if (DCell.Temperature > 14.0f && EffAlt < 700.0f) {
                                    if (CellStream.FRand() > 0.4f) { SCell.TreeType = ETreeType::Oak; SCell.TreeSpeciesID = 1; }
                                    else { SCell.TreeType = ETreeType::Birch; SCell.TreeSpeciesID = 2; }
                                }
                                else {
                                    if (CellStream.FRand() > 0.4f) { SCell.TreeType = ETreeType::Spruce; SCell.TreeSpeciesID = 4; }
                                    else { SCell.TreeType = ETreeType::Pine; SCell.TreeSpeciesID = 5; }
                                }
                            }

                            DCell.TreeAge = 0.0f;
                            LocalBirths += 1.0f * DeltaDays;
                            DCell.WoodAmount = 15.0f;
                            LocalDirty |= EChunkVisualDirty::Flora;
                        }
                    }

                    if (SCell.TreeType != ETreeType::None) {
                        if (ForestSuitability < 0.1f || DCell.GlacierIce > 0.5f || DeathRate > 1.0f) {
                            float BaseDeath = (TargetBiome == EBiomeType::Desert || TargetBiome == EBiomeType::Barren || TargetBiome == EBiomeType::Tundra || DCell.GlacierIce > 0.5f) ? 50.0f : 1.0f;
                            DCell.WoodAmount -= FMath::Max(BaseDeath, DeathRate) * DeltaDays;
                        }
                        else {
                            float AgeFactor = FMath::Clamp(1.0f - (DCell.WoodAmount / 300.0f), 0.1f, 1.0f);
                            DCell.WoodAmount = FMath::Min(300.0f, DCell.WoodAmount + (CurrentGrowthSpeed * 100.0f * AgeFactor * ForestSuitability));
                        }
                        DCell.ForestDensity = FMath::Clamp(DCell.WoodAmount / 250.0f, 0.05f, 1.0f);

                        if (DCell.WoodAmount <= 0.0f) {
                            LocalDeaths += 1.0f * DeltaDays;
                            SCell.TreeType = ETreeType::None;
                            SCell.TreeSpeciesID = 0;
                            DCell.ForestDensity = 0.0f;
                            LocalDirty |= EChunkVisualDirty::Flora;
                        }
                    }

                    DCell.FloraDensity = FMath::Clamp(DCell.GrassDensity + DCell.ShrubDensity + DCell.ForestDensity, 0.0f, 1.0f);
                    if (DCell.GlacierIce > 0.5f) {
                        DCell.GrassDensity = FMath::Max(0.0f, DCell.GrassDensity - 0.5f * DeltaDays);
                        DCell.ShrubDensity = FMath::Max(0.0f, DCell.ShrubDensity - 0.5f * DeltaDays);
                    }
                }

                if (DCell.FireIntensity > 0.0f) {
                    if (CellStream.FRand() < 0.3f * DeltaDays && DCell.FireIntensity > 0.3f) {
                        int32 dir = CellStream.RandRange(0, 3);
                        FCellStaticData* NCellS = nullptr; FCellDynamicData* NCellD = nullptr; FIntPoint NCoord;
                        if (Manager->GetMutableCellGlobal(GlobalX + Offsets[dir][0], GlobalY + Offsets[dir][1], NCellS, NCellD, NCoord)) {
                            if (NCellD->FloraDensity > 0.2f && NCellD->SurfaceWater < 1.0f && NCellD->FireIntensity == 0.0f) NCellD->FireIntensity = DCell.FireIntensity - 0.2f;
                        }
                    }

                    DCell.GrassDensity = 0.0f;
                    DCell.ShrubDensity = 0.0f;
                    DCell.BerryBushes = 0.0f;

                    if (SCell.TreeType != ETreeType::None) {
                        LocalDeaths += 1.0f;
                        SCell.TreeType = ETreeType::None;
                        SCell.TreeSpeciesID = 0;
                        DCell.WoodAmount = 0.0f;
                        DCell.ForestDensity = 0.0f;
                        LocalDirty |= EChunkVisualDirty::Flora;
                    }

                    DCell.FireIntensity -= ((DCell.Rainfall * 0.5f) + 0.5f) * DeltaDays;
                    if (DCell.Lava > 0.1f) DCell.FireIntensity = 1.0f;
                    if (DCell.FireIntensity < 0.0f) DCell.FireIntensity = 0.0f;

                    LocalDirty |= EChunkVisualDirty::Flora;
                }

                if (Toxicity > 0.05f) { DCell.DangerLevel = FMath::Max(DCell.DangerLevel, Toxicity); LocalDirty |= EChunkVisualDirty::Terrain; }
                else if (DCell.DangerLevel > 0.0f) DCell.DangerLevel = FMath::Max(0.0f, DCell.DangerLevel - 0.2f * DeltaDays);

                if (DCell.WaterPollution > 0.0f) { DCell.WaterPollution = FMath::Max(0.0f, DCell.WaterPollution - 0.001f * DeltaDays); LocalDirty |= EChunkVisualDirty::Flora; }
                if (DCell.AnimalBones > 0.0f) { DCell.AnimalBones = FMath::Max(0.0f, DCell.AnimalBones - 0.5f * DeltaDays); LocalDirty |= EChunkVisualDirty::Flora; }

                if (SCell.Biome != TargetBiome) {
                    if (TargetBiome == EBiomeType::Swamp) {
                        if (CellStream.FRand() < 0.1f * DeltaDays) {
                            SCell.Biome = TargetBiome;
                            SCell.SoilType = ESoilType::Mud;
                            LocalDirty |= EChunkVisualDirty::Flora | EChunkVisualDirty::TerrainColor;
                        }
                    }
                    else {
                        SCell.Biome = TargetBiome;
                        LocalDirty |= EChunkVisualDirty::Flora | EChunkVisualDirty::TerrainColor;
                    }
                }

                DCell.GrazingPressure = 0.0f;

                if (bRunColorUpdate) {
                    FLinearColor TargetColor = GetBiomeColor(SCell.Biome);
                    if (SCell.Biome == EBiomeType::Grassland) {
                        float Hum = FMath::Clamp(DCell.Humidity, 0.0f, 1.0f);
                        TargetColor.R *= FMath::Lerp(1.0f, 0.4f, Hum); TargetColor.G *= FMath::Lerp(1.0f, 0.7f, Hum); TargetColor.B *= FMath::Lerp(1.0f, 0.4f, Hum);
                    }

                    if (DCell.SnowAmount > 0.05f || DCell.Temperature <= 0.0f) {
                        float FreezeAlpha = (DCell.Temperature <= 0.0f) ? FMath::Clamp(-DCell.Temperature / 15.0f, 0.0f, 1.0f) : 0.0f;
                        float SnowAlpha = FMath::Clamp((DCell.SnowAmount / 2.0f) + FreezeAlpha, 0.0f, 0.85f);
                        float SnowShade = 0.85f + (FMath::PerlinNoise2D(FVector2D(GlobalX * 0.1f, GlobalY * 0.1f)) * 0.15f);
                        FLinearColor SnowColor(0.95f * SnowShade, 0.98f * SnowShade, 1.0f * SnowShade, 1.0f);
                        TargetColor = FMath::Lerp(TargetColor, SnowColor, SnowAlpha);
                    }

                    if (SCell.bIsVolcano && DCell.Lava < 0.1f) {
                        TargetColor = FLinearColor(0.12f, 0.10f, 0.10f, 1.0f);
                    }
                    else if (DCell.Lava > 0.0f) {
                        TargetColor = FMath::Lerp(TargetColor, FLinearColor(1.0f, 0.35f, 0.0f, 1.0f), FMath::Clamp(DCell.Lava, 0.0f, 1.0f));
                    }

                    if (!SCell.BiomeColor.Equals(TargetColor, 0.25f)) {
                        SCell.BiomeColor = TargetColor;
                        LocalDirty |= EChunkVisualDirty::TerrainColor;
                    }
                }

                int32 OldWoodTier = FMath::FloorToInt(OldWood / 75.0f);
                int32 NewWoodTier = FMath::FloorToInt(DCell.WoodAmount / 75.0f);
                int32 OldFloraTier = FMath::FloorToInt(OldFlora / 0.25f);
                int32 NewFloraTier = FMath::FloorToInt(DCell.FloraDensity / 0.25f);

                if (SCell.TreeType != OldTree || OldWoodTier != NewWoodTier || OldFloraTier != NewFloraTier) {
                    LocalDirty |= EChunkVisualDirty::Flora;
                }
            }
        }

        if ((LocalBirths > 0.0f || LocalDeaths > 0.0f) && Manager->ManaModule) {
            FScopeLock Lock(&ManaMutex);
            if (LocalBirths > 0.0f) Manager->ManaModule->AccumulateLifeMana(LocalBirths * 10.0f, EManaSourceType::FloraBirth);
            if (LocalDeaths > 0.0f) Manager->ManaModule->AccumulateLifeMana(LocalDeaths * 10.0f, EManaSourceType::FloraDeath);
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

        if (LocalDirty != 0) SafeFlags[iter] |= LocalDirty;
        });

    for (int32 iter = 0; iter < (EndIdx - StartIdx); iter++) {
        if (SafeFlags[iter] != 0) Manager->RegisterVisualChange(ChunkKeys[StartIdx + iter], SafeFlags[iter]);
    }
}

EBiomeType UFloraSystem::DetermineBiome(float Temperature, float WaterAvailability, float Altitude, float FloraDensity, float GrassSpread) {
    if (Temperature <= -5.0f) return EBiomeType::Tundra;
    if (Altitude > 2000.0f && Temperature < 0.0f) return EBiomeType::Barren;
    if (WaterAvailability < 0.1f && Temperature >= 45.0f) return EBiomeType::Desert;

    float BaseReq = 0.5f + (GrassSpread * 0.3f);
    float ReqWood = BaseReq;

    if (Altitude < 300.0f) {
        ReqWood = FMath::Lerp(1.5f, BaseReq, Altitude / 300.0f);
    }
    else if (Altitude > 1600.0f) {
        ReqWood = FMath::Lerp(BaseReq, 2.0f, (Altitude - 1600.0f) / 400.0f);
    }

    if (WaterAvailability >= ReqWood) {
        if (Temperature > 22.0f && Altitude < 500.0f) return EBiomeType::TropicalForest;
        if (Temperature > 14.0f && Altitude < 800.0f) return EBiomeType::DeciduousForest;
        return EBiomeType::ConiferousForest;
    }

    if (Altitude > 1600.0f) return EBiomeType::Tundra;
    return EBiomeType::Grassland;
}

FLinearColor UFloraSystem::GetBiomeColor(EBiomeType Biome) {
    switch (Biome) {
    case EBiomeType::Barren: return FLinearColor(0.25f, 0.24f, 0.22f, 1.0f);
    case EBiomeType::Desert: return FLinearColor(0.70f, 0.55f, 0.20f, 1.0f);
    case EBiomeType::Beach: return FLinearColor(0.75f, 0.65f, 0.35f, 1.0f);
    case EBiomeType::Tundra: return FLinearColor(0.65f, 0.70f, 0.75f, 1.0f);
    case EBiomeType::Grassland: return FLinearColor(0.18f, 0.45f, 0.10f, 1.0f);
    case EBiomeType::DeciduousForest: return FLinearColor(0.12f, 0.35f, 0.08f, 1.0f);
    case EBiomeType::ConiferousForest: return FLinearColor(0.08f, 0.25f, 0.10f, 1.0f);
    case EBiomeType::TropicalForest: return FLinearColor(0.08f, 0.40f, 0.15f, 1.0f);
    case EBiomeType::Swamp: return FLinearColor(0.12f, 0.28f, 0.18f, 1.0f);
    default: return FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }
}

void UFloraSystem::BuildFloraMesh(TSharedPtr<FChunkMeshData> MeshData, const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    if (!MeshData.IsValid()) return;
    int32 ChunkSize = Params.ChunkSize; float CellSize = 50.0f;

    int32 Step = 1;
    if (Params.bUseLOD) {
        FVector2D ChunkCenter = ChunkCoord * ((ChunkSize - 1) * CellSize) + FVector2D((ChunkSize * CellSize) * 0.5f, (ChunkSize * CellSize) * 0.5f);
        float DistToPlayer = FVector2D::DistSquared(ChunkCenter, Params.PlayerPos2D);
        if (DistToPlayer > 6400000000.0f) return;
        else if (DistToPlayer > 1600000000.0f) Step = 4;
        else if (DistToPlayer > 400000000.0f) Step = 2;
    }

    FVector FakeSunDir(-0.6f, -0.6f, 0.7f); FakeSunDir.Normalize();
    float Ambient = 0.25f; float DiffuseMult = 0.75f;

    auto AddTri = [&](FVector A, FVector B, FVector C, FLinearColor Color, float LocalX, float LocalY, float BaseZ, float H, float WetnessAlpha) {
        FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
        if (N.IsNearlyZero()) N = FVector(0.0f, 0.0f, 1.0f);

        FVector Center(LocalX, LocalY, BaseZ + (H * 0.5f));
        FVector FaceCenter = (A + B + C) / 3.0f;
        if (FVector::DotProduct(N, FaceCenter - Center) < 0.0f) N = -N;

        float Light = Ambient + FMath::Max(0.0f, FVector::DotProduct(N, FakeSunDir)) * DiffuseMult;
        FLinearColor FinalColor = Color * Light;
        FinalColor.A = FMath::Clamp(WetnessAlpha, 0.0f, 1.0f);

        int32 V = MeshData->FloraVertices.Num();
        MeshData->FloraVertices.Add(A); MeshData->FloraVertices.Add(B); MeshData->FloraVertices.Add(C);
        MeshData->FloraNormals.Add(N); MeshData->FloraNormals.Add(N); MeshData->FloraNormals.Add(N);

        for (int i = 0; i < 3; i++) { MeshData->FloraTriangles.Add(V + i); MeshData->FloraUV0.Add(FVector2D(0, 0)); MeshData->FloraColors.Add(FinalColor); }
        };

    for (int32 Y = 0; Y < ChunkSize; Y += Step) {
        for (int32 X = 0; X < ChunkSize; X += Step) {

            int32 Index = FMath::Min(X + (Y * ChunkSize), Chunk.StaticCells.Num() - 1);
            const FCellStaticData& SCell = Chunk.StaticCells[Index];
            const FCellDynamicData& DCell = Chunk.DynamicCells[Index];

            float Density = DCell.FloraDensity;
            if (DCell.SurfaceWater >= 1.0f || SCell.Elevation <= Params.SeaLevel) continue;
            if (Density <= 0.02f && DCell.BerryBushes <= 1.0f && SCell.Biome != EBiomeType::Swamp) continue;

            float LocalX = (ChunkCoord.X * (ChunkSize - 1) * CellSize) + (X * CellSize);
            float LocalY = (ChunkCoord.Y * (ChunkSize - 1) * CellSize) + (Y * CellSize);
            float BaseZ = SCell.Elevation + DCell.GlacierIce;
            float CellWetness = (DCell.SnowAmount > 0.05f || DCell.Temperature <= 0.0f) ? 0.0f : DCell.Wetness;

            int32 GlobalX = FMath::RoundToInt(ChunkCoord.X * (ChunkSize - 1)) + X;
            int32 GlobalY = FMath::RoundToInt(ChunkCoord.Y * (ChunkSize - 1)) + Y;
            uint32 HashSeed = Params.MapSeed + (GlobalX * 374761393U) + (GlobalY * 668265263U);
            HashSeed = (HashSeed ^ (HashSeed >> 13)) * 1274126177U;
            FRandomStream Stream(HashSeed);

            LocalX += Stream.FRandRange(-15.0f, 15.0f); LocalY += Stream.FRandRange(-15.0f, 15.0f);

            if (SCell.TreeType != ETreeType::None) {

                float TreeScale = FMath::Clamp(DCell.WoodAmount / 300.0f, 0.15f, 1.3f);
                float H = 0.0f; float W = 0.0f;

                FLinearColor LeafColor(0.1f, 0.5f, 0.1f, 1.0f);
                FLinearColor TrunkColor(0.40f, 0.30f, 0.20f, 1.0f);
                bool bIsConifer = false;

                switch (SCell.TreeType) {
                case ETreeType::Spruce: H = 220.0f; W = 45.0f; LeafColor = FLinearColor(0.015f, 0.12f, 0.03f, 1.0f); bIsConifer = true; break;
                case ETreeType::Pine:   H = 260.0f; W = 40.0f; LeafColor = FLinearColor(0.02f, 0.16f, 0.05f, 1.0f); bIsConifer = true; break;
                case ETreeType::Oak:    H = 160.0f; W = 70.0f; LeafColor = FLinearColor(0.08f, 0.35f, 0.04f, 1.0f); bIsConifer = false; break;
                case ETreeType::Birch:  H = 180.0f; W = 55.0f; LeafColor = FLinearColor(0.15f, 0.45f, 0.06f, 1.0f); TrunkColor = FLinearColor(0.85f, 0.85f, 0.85f, 1.0f); bIsConifer = false; break;
                case ETreeType::Jungle: H = 280.0f; W = 80.0f; LeafColor = FLinearColor(0.01f, 0.22f, 0.15f, 1.0f); bIsConifer = false; break;
                default: break;
                }

                if (SCell.TreeSpeciesID == 1) { LeafColor = FLinearColor(0.08f, 0.35f, 0.04f, 1.0f); TrunkColor = FLinearColor(0.25f, 0.18f, 0.12f, 1.0f); }
                else if (SCell.TreeSpeciesID == 2) { LeafColor = FLinearColor(0.15f, 0.45f, 0.06f, 1.0f); }
                else if (SCell.TreeSpeciesID == 3) { LeafColor = FLinearColor(0.12f, 0.40f, 0.08f, 1.0f); }
                else if (SCell.TreeSpeciesID == 4) { LeafColor = FLinearColor(0.015f, 0.12f, 0.03f, 1.0f); }
                else if (SCell.TreeSpeciesID == 5) { LeafColor = FLinearColor(0.02f, 0.16f, 0.05f, 1.0f); TrunkColor = FLinearColor(0.40f, 0.25f, 0.12f, 1.0f); }
                else if (SCell.TreeSpeciesID == 6) { LeafColor = FLinearColor(0.01f, 0.22f, 0.15f, 1.0f); }
                else if (SCell.TreeSpeciesID == 7) { LeafColor = FLinearColor(0.18f, 0.30f, 0.08f, 1.0f); TrunkColor = FLinearColor(0.50f, 0.45f, 0.35f, 1.0f); }
                else if (SCell.TreeSpeciesID == 8) { LeafColor = FLinearColor(0.15f, 0.30f, 0.05f, 1.0f); TrunkColor = FLinearColor(0.35f, 0.30f, 0.25f, 1.0f); }

                float CanopyShade = Stream.FRandRange(0.95f, 1.05f);

                LeafColor.R = FMath::Clamp(LeafColor.R * CanopyShade, 0.0f, 1.0f);
                LeafColor.G = FMath::Clamp(LeafColor.G * CanopyShade, 0.0f, 1.0f);
                LeafColor.B = FMath::Clamp(LeafColor.B * CanopyShade, 0.0f, 1.0f);

                if (DCell.WaterPollution > 0.05f || DCell.AshDensityBuffer > 0.1f) {
                    float ToxAlpha = FMath::Clamp(DCell.WaterPollution * 2.0f + DCell.AshDensityBuffer * 0.2f, 0.0f, 1.0f);
                    FLinearColor ToxicColor(0.6f, 0.5f, 0.1f, 1.0f);
                    if (SCell.TreeSpeciesID == 8) ToxicColor = FLinearColor(0.55f, 0.70f, 0.20f, 1.0f);
                    LeafColor = FMath::Lerp(LeafColor, ToxicColor, ToxAlpha);
                }

                H *= TreeScale * Step; W *= TreeScale * Step;
                if (H <= 1.0f || W <= 1.0f) continue;

                if (DCell.FireIntensity > 0.0f) {
                    LeafColor = FLinearColor(0.9f, 0.3f, 0.0f, 1.0f); TrunkColor = FLinearColor(0.1f, 0.1f, 0.1f, 1.0f);
                }
                else if (DCell.SnowAmount > 0.05f || DCell.Temperature <= 0.0f) {
                    float FreezeAlpha = (DCell.Temperature <= 0.0f) ? FMath::Clamp(-DCell.Temperature / 15.0f, 0.0f, 1.0f) : 0.0f;
                    float SnowAlpha = FMath::Clamp((DCell.SnowAmount / 1.5f) + FreezeAlpha, 0.0f, 0.85f);
                    float SnowShade = 0.85f + (FMath::PerlinNoise2D(FVector2D(LocalX * 0.01f, LocalY * 0.01f)) * 0.15f);
                    FLinearColor SnowColor(0.95f * SnowShade, 0.98f * SnowShade, 1.0f * SnowShade, 1.0f);
                    LeafColor = FMath::Lerp(LeafColor, SnowColor, SnowAlpha * 0.9f);
                    TrunkColor = FMath::Lerp(TrunkColor, SnowColor, SnowAlpha * 0.6f);
                }

                FLinearColor PaintColor = LeafColor;

                if (bIsConifer) {
                    FVector P1(LocalX - W, LocalY - W, BaseZ); FVector P2(LocalX + W, LocalY - W, BaseZ);
                    FVector P3(LocalX + W, LocalY + W, BaseZ); FVector P4(LocalX - W, LocalY + W, BaseZ);
                    FVector PTop(LocalX, LocalY, BaseZ + H);

                    AddTri(P1, PTop, P2, PaintColor, LocalX, LocalY, BaseZ, H, CellWetness);
                    AddTri(P2, PTop, P3, PaintColor, LocalX, LocalY, BaseZ, H, CellWetness);
                    AddTri(P3, PTop, P4, PaintColor, LocalX, LocalY, BaseZ, H, CellWetness);
                    AddTri(P4, PTop, P1, PaintColor, LocalX, LocalY, BaseZ, H, CellWetness);
                }
                else {
                    float TrunkW = W * 0.15f; float TrunkH = H * 0.4f;
                    FVector T1(LocalX - TrunkW, LocalY, BaseZ); FVector T2(LocalX + TrunkW, LocalY, BaseZ); FVector TTop(LocalX, LocalY, BaseZ + TrunkH);
                    AddTri(T1, TTop, T2, TrunkColor, LocalX, LocalY, BaseZ, TrunkH, CellWetness);

                    float CW = W; float CH = H - TrunkH;
                    FVector C_Mid(LocalX, LocalY, BaseZ + TrunkH + CH * 0.5f);
                    FVector C_Top(LocalX, LocalY, BaseZ + H);
                    FVector C_Bot(LocalX, LocalY, BaseZ + TrunkH);

                    FVector CF(LocalX - CW, LocalY, C_Mid.Z); FVector CB(LocalX + CW, LocalY, C_Mid.Z);
                    FVector CL(LocalX, LocalY - CW, C_Mid.Z); FVector CR(LocalX, LocalY + CW, C_Mid.Z);

                    AddTri(CF, C_Top, CL, LeafColor, LocalX, LocalY, BaseZ, H, CellWetness);
                    AddTri(CL, C_Top, CB, LeafColor, LocalX, LocalY, BaseZ, H, CellWetness);
                    AddTri(CB, C_Top, CR, LeafColor, LocalX, LocalY, BaseZ, H, CellWetness);
                    AddTri(CR, C_Top, CF, LeafColor, LocalX, LocalY, BaseZ, H, CellWetness);
                    AddTri(CF, CL, C_Bot, LeafColor, LocalX, LocalY, BaseZ, H, CellWetness);
                    AddTri(CL, CB, C_Bot, LeafColor, LocalX, LocalY, BaseZ, H, CellWetness);
                    AddTri(CB, CR, C_Bot, LeafColor, LocalX, LocalY, BaseZ, H, CellWetness);
                    AddTri(CR, CF, C_Bot, LeafColor, LocalX, LocalY, BaseZ, H, CellWetness);
                }
            }
            else if (SCell.Biome == EBiomeType::Swamp && Stream.FRand() > 0.4f) {
                float SwampBushScale = Stream.FRandRange(0.8f, 1.4f) * Step;
                float W = 25.0f * SwampBushScale; float H = 15.0f * SwampBushScale;
                FVector B1(LocalX - W, LocalY - W, BaseZ); FVector B2(LocalX + W, LocalY - W, BaseZ);
                FVector B3(LocalX, LocalY + W, BaseZ); FVector BTop(LocalX, LocalY, BaseZ + H);

                FLinearColor SwampColor = FLinearColor(0.12f, 0.28f, 0.10f, 1.0f);
                if (DCell.WaterPollution > 0.1f) SwampColor = FMath::Lerp(SwampColor, FLinearColor(0.5f, 0.4f, 0.1f, 1.0f), FMath::Clamp(DCell.WaterPollution * 2.0f, 0.0f, 1.0f));

                if (DCell.SnowAmount > 0.05f || DCell.Temperature <= 0.0f) {
                    float FreezeAlpha = (DCell.Temperature <= 0.0f) ? FMath::Clamp(-DCell.Temperature / 15.0f, 0.0f, 1.0f) : 0.0f;
                    float SnowAlpha = FMath::Clamp((DCell.SnowAmount / 1.5f) + FreezeAlpha, 0.0f, 0.85f);
                    float SnowShade = 0.85f + (FMath::PerlinNoise2D(FVector2D(LocalX * 0.01f, LocalY * 0.01f)) * 0.15f);
                    FLinearColor SnowColor(0.95f * SnowShade, 0.98f * SnowShade, 1.0f * SnowShade, 1.0f);
                    SwampColor = FMath::Lerp(SwampColor, SnowColor, SnowAlpha * 0.9f);
                }

                AddTri(B1, BTop, B2, SwampColor, LocalX, LocalY, BaseZ, H, CellWetness);
                AddTri(B2, BTop, B3, SwampColor, LocalX, LocalY, BaseZ, H, CellWetness);
                AddTri(B3, BTop, B1, SwampColor, LocalX, LocalY, BaseZ, H, CellWetness);
            }
            else if (DCell.BerryBushes > 1.0f) {
                float BushScale = FMath::Clamp(DCell.BerryBushes / 20.0f, 0.5f, 1.2f) * Step;
                float W = 20.0f * BushScale; float H = 25.0f * BushScale;
                FVector B1(LocalX - W, LocalY - W, BaseZ); FVector B2(LocalX + W, LocalY - W, BaseZ);
                FVector B3(LocalX, LocalY + W, BaseZ); FVector BTop(LocalX, LocalY, BaseZ + H);

                FLinearColor BushColor = FLinearColor(0.15f, 0.45f, 0.10f, 1.0f);
                if (DCell.WaterPollution > 0.1f) BushColor = FMath::Lerp(BushColor, FLinearColor(0.6f, 0.5f, 0.1f, 1.0f), FMath::Clamp(DCell.WaterPollution * 2.0f, 0.0f, 1.0f));

                if (DCell.SnowAmount > 0.05f || DCell.Temperature <= 0.0f) {
                    float FreezeAlpha = (DCell.Temperature <= 0.0f) ? FMath::Clamp(-DCell.Temperature / 15.0f, 0.0f, 1.0f) : 0.0f;
                    float SnowAlpha = FMath::Clamp((DCell.SnowAmount / 1.5f) + FreezeAlpha, 0.0f, 0.85f);
                    float SnowShade = 0.85f + (FMath::PerlinNoise2D(FVector2D(LocalX * 0.01f, LocalY * 0.01f)) * 0.15f);
                    FLinearColor SnowColor(0.95f * SnowShade, 0.98f * SnowShade, 1.0f * SnowShade, 1.0f);
                    BushColor = FMath::Lerp(BushColor, SnowColor, SnowAlpha * 0.9f);
                }

                AddTri(B1, BTop, B2, BushColor, LocalX, LocalY, BaseZ, H, CellWetness);
                AddTri(B2, BTop, B3, BushColor, LocalX, LocalY, BaseZ, H, CellWetness);
                AddTri(B3, BTop, B1, BushColor, LocalX, LocalY, BaseZ, H, CellWetness);
            }
        }
    }
}