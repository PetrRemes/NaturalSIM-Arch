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

    ParallelFor(OutChunk.MicroCells.Num(), [&](int32 i) {
        FCellData& Cell = OutChunk.MicroCells[i];
        int32 X = i % ChunkSize; int32 Y = i / ChunkSize;
        int32 GlobalX = FMath::RoundToInt(ChunkCoord.X * (ChunkSize - 1)) + X;
        int32 GlobalY = FMath::RoundToInt(ChunkCoord.Y * (ChunkSize - 1)) + Y;

        if (Cell.Elevation <= SeaLevel) {
            Cell.Biome = EBiomeType::Barren; Cell.TreeType = ETreeType::None; Cell.FloraDensity = 0.0f;
            Cell.EdibleFlora = 0.0f; Cell.BerryBushes = 0.0f; Cell.BiomeColor = GetBiomeColor(Cell.Biome);
            return;
        }

        float Altitude = Cell.Elevation - SeaLevel;
        float LocalSlope = FMath::Abs(FMath::PerlinNoise2D(FVector2D(GlobalX * 0.05f, GlobalY * 0.05f)) * 0.3f);

        bool bIsCoast = Altitude <= 12.0f;
        bool bIsShore = Cell.SurfaceWater > 0.01f && Cell.SurfaceWater < 0.2f;
        if ((bIsCoast || bIsShore) && Cell.Bedrock != EBedrockType::Rock && LocalSlope < 0.08f) {
            Cell.Bedrock = EBedrockType::Sand;
        }

        float WaterAvailability = (Cell.GroundWater / 100.0f) + Cell.Rainfall + 0.25f;
        if (Cell.SurfaceWater > 0.05f || Cell.RiverDischarge > 0.5f) {
            WaterAvailability += 1.0f;
        }

        float CoastalFactor = FMath::Clamp(1.0f - (Altitude / 5.0f), 0.0f, 1.0f);
        float RiverFactor = FMath::Clamp(Cell.RiverDischarge / 5.0f, 0.0f, 1.0f);
        Cell.WetlandScore = (Cell.Wetness * 0.35f) + (CoastalFactor * 0.25f) + (RiverFactor * 0.25f);

        bool bIsSpecialBiome = false;

        bool bIsSwampy = Cell.WetlandScore > 0.65f && Altitude < 4.0f && Cell.Bedrock != EBedrockType::Rock;

        if (bIsSwampy) {
            Cell.Biome = EBiomeType::Swamp; Cell.FloraDensity = 0.5f; Cell.TreeType = ETreeType::None; Cell.EdibleFlora = 20.0f;
            bIsSpecialBiome = true;
        }
        else if (Cell.Bedrock == EBedrockType::Sand) {
            Cell.Biome = EBiomeType::Beach; Cell.TreeType = ETreeType::None; Cell.FloraDensity = 0.0f; Cell.EdibleFlora = 0.0f;
            bIsSpecialBiome = true;
        }

        float MixNoise = FMath::PerlinNoise2D(FVector2D(GlobalX * 0.015f, GlobalY * 0.015f)) * 300.0f;
        float EffAlt = FMath::Max(0.0f, Altitude + MixNoise);

        uint32 HashSeed = Params.MapSeed + (GlobalX * 374761393U) + (GlobalY * 668265263U);
        HashSeed = (HashSeed ^ (HashSeed >> 13)) * 1274126177U;
        FRandomStream InitialStream(HashSeed);

        if (!bIsSpecialBiome) {
            Cell.Biome = DetermineBiome(Cell.Temperature, WaterAvailability, EffAlt, Cell.FloraDensity, Params.GrasslandSpread);

            float MaxCapacity = FMath::Clamp(WaterAvailability * 1.5f, 0.0f, 1.0f);
            if (Altitude > 1200.0f) MaxCapacity *= FMath::Clamp(1.0f - ((Altitude - 1200.0f) / 500.0f), 0.1f, 1.0f);

            if (Params.bIsFullGeneration) {
                Cell.FloraDensity = MaxCapacity * InitialStream.FRandRange(0.6f, 1.0f);
                float ClusterNoise = FMath::PerlinNoise2D(FVector2D(GlobalX * 0.05f, GlobalY * 0.05f));

                if (ClusterNoise > -0.2f && (Cell.Biome == EBiomeType::ConiferousForest || Cell.Biome == EBiomeType::DeciduousForest || Cell.Biome == EBiomeType::TropicalForest)) {
                    Cell.WoodAmount = Cell.FloraDensity * 300.0f;
                }
                else {
                    Cell.WoodAmount = 0.0f;
                    if (Cell.Biome == EBiomeType::ConiferousForest || Cell.Biome == EBiomeType::DeciduousForest || Cell.Biome == EBiomeType::TropicalForest) {
                        Cell.Biome = EBiomeType::Grassland;
                    }
                }
                if (WaterAvailability > 0.5f && InitialStream.FRand() > 0.75f) Cell.BerryBushes = 10.0f; else Cell.BerryBushes = 0.0f;
            }

            if (Cell.WoodAmount > 50.0f && (Cell.Biome == EBiomeType::ConiferousForest || Cell.Biome == EBiomeType::DeciduousForest || Cell.Biome == EBiomeType::TropicalForest)) {
                float SpeciesRand = InitialStream.FRand();
                if (Cell.Temperature > 22.0f && EffAlt < 400.0f) {
                    Cell.TreeType = ETreeType::Jungle;
                    Cell.Biome = EBiomeType::TropicalForest;
                }
                else if (Cell.Temperature > 14.0f && EffAlt < 700.0f) {
                    Cell.TreeType = (SpeciesRand > 0.4f) ? ETreeType::Oak : ETreeType::Birch;
                    if (SpeciesRand > 0.85f && EffAlt > 500.0f) Cell.TreeType = ETreeType::Pine;
                    Cell.Biome = EBiomeType::DeciduousForest;
                }
                else {
                    Cell.TreeType = (SpeciesRand > 0.4f) ? ETreeType::Spruce : ETreeType::Pine;
                    if (SpeciesRand > 0.85f && EffAlt < 900.0f) Cell.TreeType = ETreeType::Birch;
                    Cell.Biome = EBiomeType::ConiferousForest;
                }
                Cell.TreeSpeciesID = 0;
                Cell.TreeAge = InitialStream.FRandRange(10.0f, 100.0f);
            }
            else {
                Cell.TreeType = ETreeType::None;
                Cell.WoodAmount = 0.0f;
                Cell.TreeSpeciesID = 0;
            }
        }

        FLinearColor StartColor = GetBiomeColor(Cell.Biome);
        if (Cell.Biome == EBiomeType::Grassland) {
            float Hum = FMath::Clamp(Cell.Humidity, 0.0f, 1.0f);
            StartColor.R *= FMath::Lerp(1.0f, 0.4f, Hum); StartColor.G *= FMath::Lerp(1.0f, 0.7f, Hum); StartColor.B *= FMath::Lerp(1.0f, 0.4f, Hum);
        }

        if (Cell.SnowAmount > 0.05f || Cell.Temperature <= 0.0f) {
            float FreezeAlpha = (Cell.Temperature <= 0.0f) ? FMath::Clamp(-Cell.Temperature / 15.0f, 0.0f, 1.0f) : 0.0f;
            float SnowAlpha = FMath::Clamp((Cell.SnowAmount / 2.0f) + FreezeAlpha, 0.0f, 0.85f);
            float SnowShade = 0.85f + (FMath::PerlinNoise2D(FVector2D(GlobalX * 0.1f, GlobalY * 0.1f)) * 0.15f);
            FLinearColor SnowColor(0.95f * SnowShade, 0.98f * SnowShade, 1.0f * SnowShade, 1.0f);
            StartColor = FMath::Lerp(StartColor, SnowColor, SnowAlpha);
        }

        if (Cell.Lava > 0.0f) {
            StartColor = FMath::Lerp(StartColor, FLinearColor(1.0f, 0.35f, 0.0f, 1.0f), FMath::Clamp(Cell.Lava, 0.0f, 1.0f));
        }

        Cell.BiomeColor = StartColor;

        if (Cell.Bedrock == EBedrockType::Sand) {
            Cell.SoilType = ESoilType::Sand; Cell.SoilDepth = 0.2f; Cell.SoilFertility = 0.05f;
            Cell.SoilStability = 0.8f;
        }
        else if (Cell.Bedrock == EBedrockType::Rock) {
            Cell.SoilType = ESoilType::Rock; Cell.SoilDepth = 0.05f; Cell.SoilFertility = 0.01f;
            Cell.SoilStability = 1.0f;
        }
        else {
            Cell.SoilType = Cell.Biome == EBiomeType::Swamp ? ESoilType::Mud : ESoilType::Dirt;
            Cell.SoilDepth = FMath::FRandRange(0.5f, 1.5f);
            Cell.SoilFertility = FMath::FRandRange(0.3f, 0.7f);
            Cell.SoilStability = Cell.Biome == EBiomeType::Swamp ? 0.3f : 0.9f;
        }
        Cell.SoilMoisture = FMath::Clamp((Cell.GroundWater / 100.0f) * 0.5f + Cell.Rainfall * 0.5f, 0.0f, 1.0f);
        Cell.OrganicMatter = (Cell.TreeType != ETreeType::None || Cell.Biome == EBiomeType::Swamp) ? 0.6f : 0.2f;
        Cell.ForestDensity = Cell.WoodAmount / 250.0f;
        Cell.TreeSeedBank = (Cell.TreeType != ETreeType::None) ? 1.0f : 0.0f;
        Cell.ShrubSeedBank = (Cell.BerryBushes > 0.0f) ? 1.0f : 0.0f;
        Cell.GrassDensity = (Cell.Biome == EBiomeType::Grassland || Cell.Biome == EBiomeType::Swamp) ? 0.8f : 0.2f;
        Cell.ShrubDensity = (Cell.BerryBushes > 0.0f) ? 0.5f : 0.0f;
        Cell.SoilCompaction = 0.0f;
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

        for (int i = 0; i < Chunk.MicroCells.Num(); i++) {
            int32 X = i % Manager->ChunkSize; int32 Y = i / Manager->ChunkSize;
            if (X >= CSize || Y >= CSize) continue;

            FCellData& Cell = Chunk.MicroCells[i];
            int32 GlobalX = (ChunkKeys[idx].X * CSize) + X; int32 GlobalY = (ChunkKeys[idx].Y * CSize) + Y;

            uint32 HashSeed = Manager->MapSeed + (GlobalX * 374761393U) + (GlobalY * 668265263U) + Manager->CurrentDay;
            HashSeed = (HashSeed ^ (HashSeed >> 13)) * 1274126177U;
            FRandomStream CellStream(HashSeed);

            auto GetNeighbor = [&](int32 dx, int32 dy) -> const FCellData* {
                int32 lx = X + dx; int32 ly = Y + dy;
                if (lx >= 0 && lx < CSize && ly >= 0 && ly < CSize) return &Chunk.MicroCells[lx + ly * Manager->ChunkSize];
                const FCellData* ExtCell = nullptr; Manager->GetCellGlobalPtr(GlobalX + dx, GlobalY + dy, ExtCell);
                return ExtCell;
                };

            ETreeType OldTree = Cell.TreeType;
            float OldFlora = Cell.FloraDensity;
            float OldWood = Cell.WoodAmount;
            float Altitude = Cell.Elevation - Manager->SeaLevel;

            Cell.FireIntensityBuffer = Cell.FireIntensity;

            float Toxicity = FMath::Clamp((Cell.WaterPollution * 1.5f) + (Cell.AshDensityBuffer * 0.25f), 0.0f, 1.0f);
            float MixNoise = FMath::PerlinNoise2D(FVector2D(GlobalX * 0.015f, GlobalY * 0.015f)) * 300.0f;
            float EffAlt = FMath::Max(0.0f, Altitude + MixNoise);

            float WaterAvailability = (Cell.GroundWater / 100.0f) * 0.6f + (Cell.Rainfall * 0.4f);
            bool bIsWetland = (Cell.SurfaceWater > 0.02f || Cell.RiverDischarge > 0.5f);
            if (bIsWetland) WaterAvailability += 0.8f;

            EBiomeType TargetBiome = DetermineBiome(Cell.Temperature, WaterAvailability, EffAlt, Cell.FloraDensity, Manager->GrasslandSpread);

            float CoastalFactor = FMath::Clamp(1.0f - (Altitude / 5.0f), 0.0f, 1.0f);
            float RiverFactor = FMath::Clamp(Cell.RiverDischarge / 5.0f, 0.0f, 1.0f);
            Cell.WetlandScore = (Cell.Wetness * 0.35f) + (CoastalFactor * 0.25f) + (Cell.SoilDepth * 0.15f) + (RiverFactor * 0.25f);
            bool bIsSwampy = Cell.WetlandScore > 0.65f && Altitude < 4.0f && Cell.Bedrock != EBedrockType::Rock;

            if (bIsSwampy) TargetBiome = EBiomeType::Swamp;
            else if (Cell.Bedrock == EBedrockType::Sand && Altitude <= 15.0f) TargetBiome = EBiomeType::Beach;
            if (Cell.GlacierIce > 1.0f || Cell.bIsVolcano || Cell.Lava > 0.1f || (Cell.FireIntensity > 0.5f && Cell.WoodAmount <= 0.0f)) {
                TargetBiome = EBiomeType::Barren;
            }

            if (Cell.DepositedSediment > 0.0f) {
                float SoilFormation = FMath::Min(Cell.DepositedSediment, 0.05f * DeltaDays);
                Cell.DepositedSediment -= SoilFormation;
                Cell.SoilDepth = FMath::Min(3.0f, Cell.SoilDepth + SoilFormation);
                Cell.OrganicMatter = FMath::Min(1.0f, Cell.OrganicMatter + SoilFormation * 0.1f);
            }

            Cell.SoilMoisture = FMath::Lerp(Cell.SoilMoisture, FMath::Clamp((Cell.GroundWater / 100.0f) + Cell.Wetness + (Cell.SurfaceWater > 0.05f ? 1.0f : 0.0f), 0.0f, 1.0f), 0.1f * DeltaDays);
            float OrganicGain = ((Cell.ForestDensity * 0.005f) + (Cell.GrassDensity * 0.002f)) * DeltaDays;
            if (bIsSwampy) OrganicGain += 0.015f * DeltaDays;

            Cell.OrganicMatter = FMath::Clamp(Cell.OrganicMatter + OrganicGain, 0.0f, 1.0f);

            if (Cell.SoilType == ESoilType::Sand && Cell.OrganicMatter > 0.3f && Altitude > 15.0f) Cell.SoilType = ESoilType::Dirt;
            Cell.SoilFertility = FMath::Clamp((Cell.OrganicMatter * 0.5f) + (Cell.Sediment * 0.1f), 0.0f, 1.0f);
            Cell.SoilDepth = FMath::Clamp(Cell.SoilDepth + ((Cell.OrganicMatter * 0.01f) + (Cell.Sediment * 0.02f)) * DeltaDays, 0.0f, 2.0f);

            if (Cell.Bedrock == EBedrockType::Rock) {
                Cell.SoilStability = 1.0f;
            }
            else {
                float WaterPenalty = (Cell.SurfaceWater > 0.05f) ? 0.3f : 0.0f;
                float SwampPenalty = bIsSwampy ? 0.4f : 0.0f;
                float RootBonus = (Cell.ForestDensity * 0.3f) + (Cell.GrassDensity * 0.1f);
                Cell.SoilStability = FMath::Clamp(0.8f - WaterPenalty - SwampPenalty + RootBonus + (Cell.SoilDepth * 0.05f), 0.05f, 1.0f);
            }

            if (Cell.Elevation <= Manager->SeaLevel || Cell.SurfaceWater >= 1.5f) {
                if (Cell.TreeType != ETreeType::None) {
                    LocalDeaths += 1.0f * DeltaDays;
                    Cell.TreeType = ETreeType::None; Cell.TreeSpeciesID = 0; Cell.WoodAmount = 0.0f; Cell.ForestDensity = 0.0f;
                    Cell.GrassDensity = 0.0f; Cell.ShrubDensity = 0.0f;
                    LocalDirty |= EChunkVisualDirty::Flora;
                }
                continue;
            }

            float LocalSlope = 0.0f;
            for (int32 dir = 0; dir < 4; dir++) {
                const FCellData* NCell = GetNeighbor(Offsets[dir][0], Offsets[dir][1]);
                if (NCell) { float s = FMath::Abs(Cell.Elevation - NCell->Elevation); if (s > LocalSlope) LocalSlope = s; }
            }
            LocalSlope *= 0.02f;

            if (bIsSwampy) {
                Cell.SoilCompaction = FMath::Max(0.0f, Cell.SoilCompaction - 0.02f * DeltaDays);
                if (Cell.FloraDensity > 0.6f && Cell.SurfaceWater > 0.0f) {
                    Cell.SurfaceWater = FMath::Max(0.0f, Cell.SurfaceWater - (0.05f * Manager->FloraGrowthSpeed * DeltaDays));
                }
            }

            float SoilQuality = (Cell.Bedrock == EBedrockType::Dirt) ? 0.6f : 0.2f;
            SoilQuality += FMath::Clamp(Cell.ClayAmount / 10000.0f, 0.0f, 0.3f);

            float OrographicBonus = 0.0f;
            if (Altitude < 250.0f) OrographicBonus = -0.3f;
            else if (Altitude < 1200.0f) OrographicBonus = 0.5f;
            else OrographicBonus = -1.0f * ((Altitude - 1200.0f) / 400.0f);

            float ForestSuitability = WaterAvailability + SoilQuality - LocalSlope + (FMath::PerlinNoise2D(FVector2D(GlobalX * 0.02f, GlobalY * 0.02f)) * 0.3f) + OrographicBonus;
            float MeadowNoise = FMath::PerlinNoise2D(FVector2D(GlobalX * 0.025f, GlobalY * 0.025f));
            bool bIsMeadow = MeadowNoise > 0.2f;

            if (Cell.GrazingPressure > 0.5f) {
                Cell.GrassDensity = FMath::Max(0.1f, Cell.GrassDensity - (0.05f * DeltaDays));
                Cell.ShrubDensity *= FMath::Pow(0.9f, DeltaDays);
                Cell.TreeSeedBank *= FMath::Pow(0.5f, DeltaDays);
                if (Cell.WoodAmount < 150.0f && Cell.TreeType != ETreeType::None) {
                    Cell.WoodAmount -= Cell.GrazingPressure * DeltaDays;
                    if (Cell.WoodAmount <= 0.0f) {
                        Cell.TreeType = ETreeType::None; Cell.TreeSpeciesID = 0; LocalDeaths += 1.0f * DeltaDays; LocalDirty |= EChunkVisualDirty::Flora;
                    }
                }
            }

            float NeighborSeeds = 0.0f, NeighborShrubs = 0.0f;
            for (int32 dir = 0; dir < 4; dir++) {
                const FCellData* NCell = GetNeighbor(Offsets[dir][0], Offsets[dir][1]);
                if (NCell) {
                    if (NCell->WoodAmount > 100.0f) NeighborSeeds += 0.05f;
                    if (NCell->ShrubDensity > 0.5f) NeighborShrubs += 0.05f;
                }
            }
            Cell.TreeSeedBank = FMath::Clamp(Cell.TreeSeedBank + (NeighborSeeds - 0.01f) * DeltaDays, 0.0f, 1.0f);
            Cell.ShrubSeedBank = FMath::Clamp(Cell.ShrubSeedBank + (NeighborShrubs - 0.01f) * DeltaDays, 0.0f, 1.0f);
            Cell.SeedSpread = Cell.TreeSeedBank;

            float GrowthSpeed = Manager->FloraGrowthSpeed * 0.01f * DeltaDays;

            if (Cell.HouseDensity > 0.0f || Cell.bHasRoad || Cell.BuildingType == EBuildingType::Mine || Cell.BuildingType == EBuildingType::Factory) {
                Cell.FloraDensity = 0.0f; Cell.GrassDensity = 0.0f; Cell.ShrubDensity = 0.0f; Cell.ForestDensity = 0.0f;
                if (Cell.TreeType != ETreeType::None) { Cell.TreeType = ETreeType::None; Cell.TreeSpeciesID = 0; Cell.WoodAmount = 0.0f; LocalDirty |= EChunkVisualDirty::Flora; }
                Cell.HouseDensity = FMath::Max(0.0f, Cell.HouseDensity - 0.005f * DeltaDays);
            }
            else if (Cell.BuildingType != EBuildingType::Farm && Cell.BuildingType != EBuildingType::EcoFarm) {

                float GrassCap = 1.0f - Cell.ForestDensity - (Cell.ShrubDensity * 0.5f);
                if (Cell.Bedrock == EBedrockType::Sand) GrassCap = 0.0f;
                Cell.GrassDensity = FMath::Clamp(Cell.GrassDensity + GrowthSpeed * 5.0f, 0.0f, GrassCap);

                if (!bIsSwampy) {
                    float BushNoise = FMath::PerlinNoise2D(FVector2D(GlobalX * 0.08f, GlobalY * 0.08f));
                    if (BushNoise > 0.4f && Cell.ForestDensity < 0.5f) {
                        Cell.ShrubDensity = FMath::Min(0.8f, Cell.ShrubDensity + GrowthSpeed);
                        if (Cell.BerryBushes < 20.0f) Cell.BerryBushes += GrowthSpeed * 50.0f;
                    }
                    else {
                        Cell.ShrubDensity = FMath::Max(0.0f, Cell.ShrubDensity - GrowthSpeed);
                        Cell.BerryBushes = FMath::Max(0.0f, Cell.BerryBushes - GrowthSpeed * 50.0f);
                    }
                }

                // ====================================================================
                // OPRAVA 1: Zamezení zmìnì ID a barvy u starých stromù (Neviditelná hranice)
                // ====================================================================
                const UFloraSpeciesData* Species = nullptr;
                if (Cell.TreeType != ETreeType::None) {
                    if (Cell.TreeSpeciesID == 0) {
                        switch (Cell.TreeType) {
                        case ETreeType::Oak: Cell.TreeSpeciesID = 1; break;
                        case ETreeType::Birch: Cell.TreeSpeciesID = 2; break;
                        case ETreeType::Spruce: Cell.TreeSpeciesID = 4; break;
                        case ETreeType::Pine: Cell.TreeSpeciesID = 5; break;
                        case ETreeType::Jungle: Cell.TreeSpeciesID = 6; break;
                        default: Cell.TreeSpeciesID = 1; break;
                        }
                    }
                    Species = GetSpeciesByID(Cell.TreeSpeciesID);
                }

                float CurrentGrowthSpeed = GrowthSpeed;
                float DeathRate = 1.0f;

                if (Species) {
                    CurrentGrowthSpeed *= Species->GrowthSpeed;

                    if (Species->ToxinAbsorptionRate > 0.0f && Cell.WaterPollution > 0.0f) {
                        Cell.WaterPollution = FMath::Max(0.0f, Cell.WaterPollution - Species->ToxinAbsorptionRate * DeltaDays);
                        LocalDirty |= EChunkVisualDirty::TerrainColor;
                    }
                    if (Species->RootPower > 1.0f) {
                        Cell.SoilDepth = FMath::Min(3.0f, Cell.SoilDepth + (Species->RootPower * 0.005f * DeltaDays));
                    }

                    Cell.TreeAge += DeltaDays / 365.0f;
                    if (Cell.TreeAge > Species->MaxLifespanYears) DeathRate = 15.0f;

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

                float EffectiveSeedSpread = Cell.SeedSpread;
                if (EffectiveSeedSpread < 0.05f && ForestSuitability > 0.7f && TargetBiome != EBiomeType::Desert && Cell.SoilFertility > 0.3f) {
                    EffectiveSeedSpread = 0.1f;
                }

                if (Cell.TreeType == ETreeType::None && EffectiveSeedSpread > 0.05f && ForestSuitability > 0.6f && !bIsMeadow) {
                    if (CellStream.FRand() < (0.015f * EffectiveSeedSpread * Manager->FloraGrowthSpeed * DeltaDays)) {

                        const UFloraSpeciesData* NewSpecies = GetBestSpeciesForEnvironment(TargetBiome, Toxicity, Cell.SoilDepth, CellStream);
                        if (NewSpecies) {
                            Cell.TreeSpeciesID = NewSpecies->SpeciesID;
                            Cell.TreeType = NewSpecies->VisualModel;
                        }
                        else {
                            if (Cell.Temperature > 22.0f && EffAlt < 400.0f) { Cell.TreeType = ETreeType::Jungle; Cell.TreeSpeciesID = 6; }
                            else if (Cell.Temperature > 14.0f && EffAlt < 700.0f) {
                                if (CellStream.FRand() > 0.4f) { Cell.TreeType = ETreeType::Oak; Cell.TreeSpeciesID = 1; }
                                else { Cell.TreeType = ETreeType::Birch; Cell.TreeSpeciesID = 2; }
                            }
                            else {
                                if (CellStream.FRand() > 0.4f) { Cell.TreeType = ETreeType::Spruce; Cell.TreeSpeciesID = 4; }
                                else { Cell.TreeType = ETreeType::Pine; Cell.TreeSpeciesID = 5; }
                            }
                        }

                        Cell.TreeAge = 0.0f;
                        LocalBirths += 1.0f * DeltaDays;
                        Cell.WoodAmount = 15.0f;
                        LocalDirty |= EChunkVisualDirty::Flora;
                    }
                }

                if (Cell.TreeType != ETreeType::None) {
                    if (ForestSuitability < 0.1f || Cell.GlacierIce > 0.5f || DeathRate > 1.0f) {
                        float BaseDeath = (TargetBiome == EBiomeType::Desert || TargetBiome == EBiomeType::Barren || TargetBiome == EBiomeType::Tundra || Cell.GlacierIce > 0.5f) ? 50.0f : 1.0f;
                        Cell.WoodAmount -= FMath::Max(BaseDeath, DeathRate) * DeltaDays;
                    }
                    else {
                        float AgeFactor = FMath::Clamp(1.0f - (Cell.WoodAmount / 300.0f), 0.1f, 1.0f);
                        Cell.WoodAmount = FMath::Min(300.0f, Cell.WoodAmount + (CurrentGrowthSpeed * 100.0f * AgeFactor * ForestSuitability));
                    }
                    Cell.ForestDensity = FMath::Clamp(Cell.WoodAmount / 250.0f, 0.05f, 1.0f);

                    if (Cell.WoodAmount <= 0.0f) {
                        LocalDeaths += 1.0f * DeltaDays;
                        Cell.TreeType = ETreeType::None;
                        Cell.TreeSpeciesID = 0;
                        Cell.ForestDensity = 0.0f;
                        LocalDirty |= EChunkVisualDirty::Flora;
                    }
                }

                Cell.FloraDensity = FMath::Clamp(Cell.GrassDensity + Cell.ShrubDensity + Cell.ForestDensity, 0.0f, 1.0f);
                if (Cell.GlacierIce > 0.5f) {
                    Cell.GrassDensity = FMath::Max(0.0f, Cell.GrassDensity - 0.5f * DeltaDays);
                    Cell.ShrubDensity = FMath::Max(0.0f, Cell.ShrubDensity - 0.5f * DeltaDays);
                }
            }

            if (Cell.FireIntensity > 0.0f) {
                if (CellStream.FRand() < 0.3f * DeltaDays && Cell.FireIntensity > 0.3f) {
                    int32 dir = CellStream.RandRange(0, 3);
                    FCellData* NCell = nullptr; FIntPoint NCoord;
                    if (Manager->GetMutableCellGlobal(GlobalX + Offsets[dir][0], GlobalY + Offsets[dir][1], NCell, NCoord)) {
                        if (NCell->FloraDensity > 0.2f && NCell->SurfaceWater < 1.0f && NCell->FireIntensity == 0.0f) NCell->FireIntensity = Cell.FireIntensity - 0.2f;
                    }
                }

                Cell.GrassDensity = 0.0f;
                Cell.ShrubDensity = 0.0f;
                Cell.BerryBushes = 0.0f;

                if (Cell.TreeType != ETreeType::None) {
                    LocalDeaths += 1.0f;
                    Cell.TreeType = ETreeType::None;
                    Cell.TreeSpeciesID = 0;
                    Cell.WoodAmount = 0.0f;
                    Cell.ForestDensity = 0.0f;
                    LocalDirty |= EChunkVisualDirty::Flora;
                }

                Cell.FireIntensity -= ((Cell.Rainfall * 0.5f) + 0.5f) * DeltaDays;
                if (Cell.Lava > 0.1f) Cell.FireIntensity = 1.0f;
                if (Cell.FireIntensity < 0.0f) Cell.FireIntensity = 0.0f;

                LocalDirty |= EChunkVisualDirty::Flora;
            }

            if (Toxicity > 0.05f) { Cell.DangerLevel = FMath::Max(Cell.DangerLevel, Toxicity); LocalDirty |= EChunkVisualDirty::Terrain; }
            else if (Cell.DangerLevel > 0.0f) Cell.DangerLevel = FMath::Max(0.0f, Cell.DangerLevel - 0.2f * DeltaDays);

            if (Cell.WaterPollution > 0.0f) { Cell.WaterPollution = FMath::Max(0.0f, Cell.WaterPollution - 0.001f * DeltaDays); LocalDirty |= EChunkVisualDirty::Flora; }
            if (Cell.AnimalBones > 0.0f) { Cell.AnimalBones = FMath::Max(0.0f, Cell.AnimalBones - 0.5f * DeltaDays); LocalDirty |= EChunkVisualDirty::Flora; }

            if (Cell.Biome != TargetBiome) {
                if (TargetBiome == EBiomeType::Swamp) {
                    if (CellStream.FRand() < 0.1f * DeltaDays) {
                        Cell.Biome = TargetBiome;
                        Cell.SoilType = ESoilType::Mud;
                        LocalDirty |= EChunkVisualDirty::Flora | EChunkVisualDirty::TerrainColor;
                    }
                }
                else {
                    Cell.Biome = TargetBiome;
                    LocalDirty |= EChunkVisualDirty::Flora | EChunkVisualDirty::TerrainColor;
                }
            }

            Cell.GrazingPressure = 0.0f;

            if (bRunColorUpdate) {
                FLinearColor TargetColor = GetBiomeColor(Cell.Biome);
                if (Cell.Biome == EBiomeType::Grassland) {
                    float Hum = FMath::Clamp(Cell.Humidity, 0.0f, 1.0f);
                    TargetColor.R *= FMath::Lerp(1.0f, 0.4f, Hum); TargetColor.G *= FMath::Lerp(1.0f, 0.7f, Hum); TargetColor.B *= FMath::Lerp(1.0f, 0.4f, Hum);
                }

                if (Cell.SnowAmount > 0.05f || Cell.Temperature <= 0.0f) {
                    float FreezeAlpha = (Cell.Temperature <= 0.0f) ? FMath::Clamp(-Cell.Temperature / 15.0f, 0.0f, 1.0f) : 0.0f;
                    float SnowAlpha = FMath::Clamp((Cell.SnowAmount / 2.0f) + FreezeAlpha, 0.0f, 0.85f);
                    float SnowShade = 0.85f + (FMath::PerlinNoise2D(FVector2D(GlobalX * 0.1f, GlobalY * 0.1f)) * 0.15f);
                    FLinearColor SnowColor(0.95f * SnowShade, 0.98f * SnowShade, 1.0f * SnowShade, 1.0f);
                    TargetColor = FMath::Lerp(TargetColor, SnowColor, SnowAlpha);
                }

                if (Cell.bIsVolcano && Cell.Lava < 0.1f) {
                    TargetColor = FLinearColor(0.12f, 0.10f, 0.10f, 1.0f);
                }
                else if (Cell.Lava > 0.0f) {
                    TargetColor = FMath::Lerp(TargetColor, FLinearColor(1.0f, 0.35f, 0.0f, 1.0f), FMath::Clamp(Cell.Lava, 0.0f, 1.0f));
                }

                if (!Cell.BiomeColor.Equals(TargetColor, 0.25f)) {
                    Cell.BiomeColor = TargetColor;
                    LocalDirty |= EChunkVisualDirty::TerrainColor;
                }
            }

            int32 OldWoodTier = FMath::FloorToInt(OldWood / 75.0f);
            int32 NewWoodTier = FMath::FloorToInt(Cell.WoodAmount / 75.0f);
            int32 OldFloraTier = FMath::FloorToInt(OldFlora / 0.25f);
            int32 NewFloraTier = FMath::FloorToInt(Cell.FloraDensity / 0.25f);

            if (Cell.TreeType != OldTree || OldWoodTier != NewWoodTier || OldFloraTier != NewFloraTier) {
                LocalDirty |= EChunkVisualDirty::Flora;
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

        if (LocalDirty != 0) SafeFlags[iter] |= LocalDirty;
        });

    for (int32 iter = 0; iter < (EndIdx - StartIdx); iter++) {
        if (SafeFlags[iter] != 0) Manager->RegisterVisualChange(ChunkKeys[StartIdx + iter], SafeFlags[iter]);
    }
}

void UFloraSystem::BuildFloraMesh(TSharedPtr<FChunkMeshData> MeshData, const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    if (!MeshData.IsValid()) return;
    int32 ChunkSize = Params.ChunkSize; float CellSize = 50.0f;

    int32 Step = 1;
    if (Params.bUseLOD) {
        FVector2D ChunkCenter = ChunkCoord * ((ChunkSize - 1) * CellSize) + FVector2D((ChunkSize * CellSize) * 0.5f, (ChunkSize * CellSize) * 0.5f);
        float DistToPlayer = FVector2D::Distance(ChunkCenter, Params.PlayerPos2D);
        if (DistToPlayer > 80000.0f) return;
        else if (DistToPlayer > 40000.0f) Step = 4;
        else if (DistToPlayer > 20000.0f) Step = 2;
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

            int32 Index = FMath::Min(X + (Y * ChunkSize), Chunk.MicroCells.Num() - 1);
            const FCellData& Cell = Chunk.MicroCells[Index];

            float Density = Cell.FloraDensity;
            if (Cell.SurfaceWater >= 1.0f || Cell.Elevation <= Params.SeaLevel) continue;
            if (Density <= 0.02f && Cell.BerryBushes <= 1.0f && Cell.Biome != EBiomeType::Swamp) continue;

            float LocalX = (ChunkCoord.X * (ChunkSize - 1) * CellSize) + (X * CellSize);
            float LocalY = (ChunkCoord.Y * (ChunkSize - 1) * CellSize) + (Y * CellSize);
            float BaseZ = Cell.Elevation + Cell.GlacierIce;
            float CellWetness = (Cell.SnowAmount > 0.05f || Cell.Temperature <= 0.0f) ? 0.0f : Cell.Wetness;

            int32 GlobalX = FMath::RoundToInt(ChunkCoord.X * (ChunkSize - 1)) + X;
            int32 GlobalY = FMath::RoundToInt(ChunkCoord.Y * (ChunkSize - 1)) + Y;
            uint32 HashSeed = Params.MapSeed + (GlobalX * 374761393U) + (GlobalY * 668265263U);
            HashSeed = (HashSeed ^ (HashSeed >> 13)) * 1274126177U;
            FRandomStream Stream(HashSeed);

            LocalX += Stream.FRandRange(-15.0f, 15.0f); LocalY += Stream.FRandRange(-15.0f, 15.0f);

            if (Cell.TreeType != ETreeType::None) {

                float TreeScale = FMath::Clamp(Cell.WoodAmount / 300.0f, 0.15f, 1.3f);
                float H = 0.0f; float W = 0.0f;

                FLinearColor LeafColor(0.1f, 0.5f, 0.1f, 1.0f);
                FLinearColor TrunkColor(0.40f, 0.30f, 0.20f, 1.0f);
                bool bIsConifer = false;

                // ====================================================================
                // OPRAVA 2: ŠKÁLA ZELENÉ URÈENÁ PRO EMISSIVE MATERIÁLY
                // ====================================================================
                switch (Cell.TreeType) {
                case ETreeType::Spruce: H = 220.0f; W = 45.0f; LeafColor = FLinearColor(0.015f, 0.12f, 0.03f, 1.0f); bIsConifer = true; break;
                case ETreeType::Pine:   H = 260.0f; W = 40.0f; LeafColor = FLinearColor(0.02f, 0.16f, 0.05f, 1.0f); bIsConifer = true; break;
                case ETreeType::Oak:    H = 160.0f; W = 70.0f; LeafColor = FLinearColor(0.08f, 0.35f, 0.04f, 1.0f); bIsConifer = false; break;
                case ETreeType::Birch:  H = 180.0f; W = 55.0f; LeafColor = FLinearColor(0.15f, 0.45f, 0.06f, 1.0f); TrunkColor = FLinearColor(0.85f, 0.85f, 0.85f, 1.0f); bIsConifer = false; break;
                case ETreeType::Jungle: H = 280.0f; W = 80.0f; LeafColor = FLinearColor(0.01f, 0.22f, 0.15f, 1.0f); bIsConifer = false; break;
                default: break;
                }

                if (Cell.TreeSpeciesID == 1) { // Dub
                    LeafColor = FLinearColor(0.08f, 0.35f, 0.04f, 1.0f);
                    TrunkColor = FLinearColor(0.25f, 0.18f, 0.12f, 1.0f);
                }
                else if (Cell.TreeSpeciesID == 2) { // Bøíza 
                    LeafColor = FLinearColor(0.15f, 0.45f, 0.06f, 1.0f);
                }
                else if (Cell.TreeSpeciesID == 3) { // Vrba 
                    LeafColor = FLinearColor(0.12f, 0.40f, 0.08f, 1.0f);
                }
                else if (Cell.TreeSpeciesID == 4) { // Smrk (Hluboká lesní tmavá zeleò)
                    LeafColor = FLinearColor(0.015f, 0.12f, 0.03f, 1.0f);
                }
                else if (Cell.TreeSpeciesID == 5) { // Borovice (Tmavá, mírnì do studena)
                    LeafColor = FLinearColor(0.02f, 0.16f, 0.05f, 1.0f);
                    TrunkColor = FLinearColor(0.40f, 0.25f, 0.12f, 1.0f);
                }
                else if (Cell.TreeSpeciesID == 6) { // Mahagon (Tropický do modra/tyrkysova)
                    LeafColor = FLinearColor(0.01f, 0.22f, 0.15f, 1.0f);
                }
                else if (Cell.TreeSpeciesID == 7) { // Akácie (Suchá olivová)
                    LeafColor = FLinearColor(0.18f, 0.30f, 0.08f, 1.0f);
                    TrunkColor = FLinearColor(0.50f, 0.45f, 0.35f, 1.0f);
                }
                else if (Cell.TreeSpeciesID == 8) { // Pajasan 
                    LeafColor = FLinearColor(0.15f, 0.30f, 0.05f, 1.0f);
                    TrunkColor = FLinearColor(0.35f, 0.30f, 0.25f, 1.0f);
                }

                // Pouze lehká odchylka jasu (jen 5%), aby les nepùsobil roztøíštìnì
                float CanopyShade = Stream.FRandRange(0.95f, 1.05f);

                LeafColor.R = FMath::Clamp(LeafColor.R * CanopyShade, 0.0f, 1.0f);
                LeafColor.G = FMath::Clamp(LeafColor.G * CanopyShade, 0.0f, 1.0f);
                LeafColor.B = FMath::Clamp(LeafColor.B * CanopyShade, 0.0f, 1.0f);

                if (Cell.WaterPollution > 0.05f || Cell.AshDensityBuffer > 0.1f) {
                    float ToxAlpha = FMath::Clamp(Cell.WaterPollution * 2.0f + Cell.AshDensityBuffer * 0.2f, 0.0f, 1.0f);
                    FLinearColor ToxicColor(0.6f, 0.5f, 0.1f, 1.0f);
                    if (Cell.TreeSpeciesID == 8) ToxicColor = FLinearColor(0.55f, 0.70f, 0.20f, 1.0f);
                    LeafColor = FMath::Lerp(LeafColor, ToxicColor, ToxAlpha);
                }

                H *= TreeScale * Step; W *= TreeScale * Step;
                if (H <= 1.0f || W <= 1.0f) continue;

                if (Cell.FireIntensity > 0.0f) {
                    LeafColor = FLinearColor(0.9f, 0.3f, 0.0f, 1.0f); TrunkColor = FLinearColor(0.1f, 0.1f, 0.1f, 1.0f);
                }
                else if (Cell.SnowAmount > 0.05f || Cell.Temperature <= 0.0f) {
                    float FreezeAlpha = (Cell.Temperature <= 0.0f) ? FMath::Clamp(-Cell.Temperature / 15.0f, 0.0f, 1.0f) : 0.0f;
                    float SnowAlpha = FMath::Clamp((Cell.SnowAmount / 1.5f) + FreezeAlpha, 0.0f, 0.85f);
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
            else if (Cell.Biome == EBiomeType::Swamp && Stream.FRand() > 0.4f) {
                float SwampBushScale = Stream.FRandRange(0.8f, 1.4f) * Step;
                float W = 25.0f * SwampBushScale; float H = 15.0f * SwampBushScale;
                FVector B1(LocalX - W, LocalY - W, BaseZ); FVector B2(LocalX + W, LocalY - W, BaseZ);
                FVector B3(LocalX, LocalY + W, BaseZ); FVector BTop(LocalX, LocalY, BaseZ + H);

                FLinearColor SwampColor = FLinearColor(0.12f, 0.28f, 0.10f, 1.0f);
                if (Cell.WaterPollution > 0.1f) SwampColor = FMath::Lerp(SwampColor, FLinearColor(0.5f, 0.4f, 0.1f, 1.0f), FMath::Clamp(Cell.WaterPollution * 2.0f, 0.0f, 1.0f));

                if (Cell.SnowAmount > 0.05f || Cell.Temperature <= 0.0f) {
                    float FreezeAlpha = (Cell.Temperature <= 0.0f) ? FMath::Clamp(-Cell.Temperature / 15.0f, 0.0f, 1.0f) : 0.0f;
                    float SnowAlpha = FMath::Clamp((Cell.SnowAmount / 1.5f) + FreezeAlpha, 0.0f, 0.85f);
                    float SnowShade = 0.85f + (FMath::PerlinNoise2D(FVector2D(LocalX * 0.01f, LocalY * 0.01f)) * 0.15f);
                    FLinearColor SnowColor(0.95f * SnowShade, 0.98f * SnowShade, 1.0f * SnowShade, 1.0f);
                    SwampColor = FMath::Lerp(SwampColor, SnowColor, SnowAlpha * 0.9f);
                }

                AddTri(B1, BTop, B2, SwampColor, LocalX, LocalY, BaseZ, H, CellWetness);
                AddTri(B2, BTop, B3, SwampColor, LocalX, LocalY, BaseZ, H, CellWetness);
                AddTri(B3, BTop, B1, SwampColor, LocalX, LocalY, BaseZ, H, CellWetness);
            }
            else if (Cell.BerryBushes > 1.0f) {
                float BushScale = FMath::Clamp(Cell.BerryBushes / 20.0f, 0.5f, 1.2f) * Step;
                float W = 20.0f * BushScale; float H = 25.0f * BushScale;
                FVector B1(LocalX - W, LocalY - W, BaseZ); FVector B2(LocalX + W, LocalY - W, BaseZ);
                FVector B3(LocalX, LocalY + W, BaseZ); FVector BTop(LocalX, LocalY, BaseZ + H);

                FLinearColor BushColor = FLinearColor(0.15f, 0.45f, 0.10f, 1.0f);
                if (Cell.WaterPollution > 0.1f) BushColor = FMath::Lerp(BushColor, FLinearColor(0.6f, 0.5f, 0.1f, 1.0f), FMath::Clamp(Cell.WaterPollution * 2.0f, 0.0f, 1.0f));

                if (Cell.SnowAmount > 0.05f || Cell.Temperature <= 0.0f) {
                    float FreezeAlpha = (Cell.Temperature <= 0.0f) ? FMath::Clamp(-Cell.Temperature / 15.0f, 0.0f, 1.0f) : 0.0f;
                    float SnowAlpha = FMath::Clamp((Cell.SnowAmount / 1.5f) + FreezeAlpha, 0.0f, 0.85f);
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

// OPRAVA 3: Ztlumení barev terénu, aby nestrhávaly pozornost z tmavých stromù
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