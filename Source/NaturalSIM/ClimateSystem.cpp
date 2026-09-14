#include "ClimateSystem.h"
#include "SimWorldManager.h"
#include "CosmosSystem.h"
#include "Async/ParallelFor.h"

UClimateSystem::UClimateSystem()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UClimateSystem::BeginPlay()
{
    Super::BeginPlay();
}

struct FClimateNeighborhood {
    const FChunkData* Chunks[3][3];
    int32 ChunkDim;
    int32 CSize;

    void Initialize(ASimWorldManager* Manager, FIntPoint CenterCoord, int32 InChunkSize) {
        ChunkDim = InChunkSize;
        CSize = InChunkSize - 1;
        for (int32 cy = -1; cy <= 1; cy++) {
            for (int32 cx = -1; cx <= 1; cx++) {
                Chunks[cy + 1][cx + 1] = Manager->WorldChunks.Find(FIntPoint(CenterCoord.X + cx, CenterCoord.Y + cy));
            }
        }
    }

    FORCEINLINE const FCellDynamicData* GetDynamic(int32 lx, int32 ly) const {
        // OPTIMALIZACE: Rychlý pøístup pro 96 % bunìk uvnitø vlastního chunku
        if (lx >= 0 && lx < CSize && ly >= 0 && ly < CSize) {
            return &Chunks[1][1]->DynamicCells[lx + ly * ChunkDim];
        }

        int32 GridX = 1; int32 GridY = 1;

        if (lx < 0) { GridX = 0; lx += CSize; }
        else if (lx >= CSize) { GridX = 2; lx -= CSize; }

        if (ly < 0) { GridY = 0; ly += CSize; }
        else if (ly >= CSize) { GridY = 2; ly -= CSize; }

        if (const FChunkData* TargetChunk = Chunks[GridY][GridX]) {
            return &TargetChunk->DynamicCells[lx + ly * ChunkDim];
        }
        return nullptr;
    }
};

void UClimateSystem::ProcessChunkClimate(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    float ChunkWorldSize = (Params.ChunkSize - 1) * 50.0f;

    for (int32 Y = 0; Y < Params.ChunkSize; Y++) {
        for (int32 X = 0; X < Params.ChunkSize; X++) {
            int32 i = X + Y * Params.ChunkSize;
            if (i >= OutChunk.StaticCells.Num()) continue;

            FCellStaticData& SCell = OutChunk.StaticCells[i];
            FCellDynamicData& DCell = OutChunk.DynamicCells[i];

            float WorldX = (ChunkCoord.X * ChunkWorldSize) + (X * 50.0f);
            float WorldY = (ChunkCoord.Y * ChunkWorldSize) + (Y * 50.0f);

            float Altitude = FMath::Max(0.0f, SCell.Elevation - Params.SeaLevel);
            float AltPenalty = (Altitude / 100.0f) * 1.5f * Params.TemperatureScale;

            float BaseT = Params.BaseTemperature + (FMath::PerlinNoise2D(FVector2D(WorldX * 0.001f, WorldY * 0.001f)) * 5.0f);
            DCell.Temperature = BaseT - AltPenalty;

            float BaseH = Params.GlobalHumidityRate + (FMath::PerlinNoise2D(FVector2D(WorldY * 0.002f, WorldX * 0.002f)) * 0.5f);
            if (SCell.Elevation <= Params.SeaLevel) BaseH += 0.5f;
            DCell.Humidity = FMath::Clamp(BaseH, 0.0f, 1.0f);

            DCell.Rainfall = DCell.Humidity * Params.GlobalRainfall;
            DCell.Wetness = 0.0f;
            DCell.CloudDensity = 0.0f;
            DCell.AshDensity = 0.0f;
            DCell.AshDensityBuffer = 0.0f;

            if (DCell.Temperature <= 0.0f) {
                DCell.SnowAmount = FMath::Min(5.0f, FMath::Abs(DCell.Temperature) * 0.5f);
            }
            else {
                DCell.SnowAmount = 0.0f;
            }
        }
    }
}

void UClimateSystem::UpdateDailyClimate(const TArray<FIntPoint>& ChunkKeys, TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, int32 StartIdx, int32 EndIdx)
{
    if (!Manager) return;

    float CosmicSeasonality = 1.0f;
    float CosmicIrradiance = 0.0f;
    float CosmicRadiation = 0.0f;

    if (Manager->CosmosModule) {
        CosmicSeasonality = Manager->CosmosModule->CurrentState.SeasonalityMultiplier;
        CosmicIrradiance = Manager->CosmosModule->CurrentState.GlobalIrradianceOffset;
        CosmicRadiation = Manager->CosmosModule->CurrentState.GlobalRadiation;
    }

    float SeasonAlpha = (Manager->CurrentDay / 365.0f) * PI * 2.0f;
    float SeasonTempOffset = FMath::Sin(SeasonAlpha - (PI / 2.0f)) * 15.0f * CosmicSeasonality;
    float SeasonRainMultiplier = 1.0f + (FMath::Sin(SeasonAlpha * 2.0f) * 0.5f);

    FVector2D GlobalWind(FMath::Cos(SeasonAlpha), FMath::Sin(SeasonAlpha));
    GlobalWind.Normalize();
    int32 UpX = FMath::RoundToInt(-GlobalWind.X);
    int32 UpY = FMath::RoundToInt(-GlobalWind.Y);

    float DriftX = Manager->GlobalCloudDrift.X * 50.0f;
    float DriftY = Manager->GlobalCloudDrift.Y * 50.0f;
    float TimeZ = Manager->GlobalCloudTime * 0.015f;

    TArray<uint8> SafeFlags1;
    SafeFlags1.Init(0, EndIdx - StartIdx);

    TArray<float> LocalEvaporationBuffer;
    LocalEvaporationBuffer.Init(0.0f, EndIdx - StartIdx);
    TArray<float> LocalRainfallBuffer;
    LocalRainfallBuffer.Init(0.0f, EndIdx - StartIdx);

    auto BiLerp = [](float v00, float v10, float v01, float v11, float tx, float ty) {
        float top = FMath::Lerp(v00, v10, tx);
        float bot = FMath::Lerp(v01, v11, tx);
        return FMath::Lerp(top, bot, ty);
        };

    float AvailableMoistureFactor = FMath::Clamp(GlobalAtmosphericMoisture / 500000.0f, 0.1f, 1.5f);

    ParallelFor(EndIdx - StartIdx, [&](int32 iter) {
        int32 idx = StartIdx + iter;
        if (!ChunkKeys.IsValidIndex(idx)) return;

        FIntPoint Coord = ChunkKeys[idx];
        FChunkData& Chunk = WorldChunks[Coord];
        bool bCloudDirty = false;
        bool bTerrainDirty = false;
        bool bFloraDirty = false;

        FClimateNeighborhood Halo;
        Halo.Initialize(Manager, Coord, Manager->ChunkSize);

        const int32 GridSegments = 4;
        int32 StepSize = Manager->ChunkSize / GridSegments;
        if (StepSize < 1) StepSize = 1;
        int32 GridNodes = GridSegments + 1;

        float NoiseAmbient[5][5];
        float NoiseMacro[5][5];
        float NoiseDetail[5][5];
        float NoiseStorm[5][5];
        float NoiseCumulus[5][5];

        for (int gy = 0; gy < GridNodes; gy++) {
            for (int gx = 0; gx < GridNodes; gx++) {
                float WorldX = (Coord.X * (Manager->ChunkSize - 1) * 50.0f) + (gx * StepSize * 50.0f);
                float WorldY = (Coord.Y * (Manager->ChunkSize - 1) * 50.0f) + (gy * StepSize * 50.0f);

                NoiseAmbient[gy][gx] = FMath::PerlinNoise2D(FVector2D(WorldX * 0.005f, WorldY * 0.005f)) * 0.1f;

                float SampleX = (WorldX - DriftX) * 0.00015f;
                float SampleY = (WorldY - DriftY) * 0.00015f;

                NoiseMacro[gy][gx] = (FMath::PerlinNoise3D(FVector(SampleX, SampleY, TimeZ * 0.2f)) + 1.0f) * 0.5f;
                NoiseDetail[gy][gx] = (FMath::PerlinNoise3D(FVector(SampleX * 4.0f, SampleY * 4.0f, TimeZ)) + 1.0f) * 0.5f;
                NoiseStorm[gy][gx] = FMath::PerlinNoise3D(FVector(SampleX * 6.0f, SampleY * 6.0f, TimeZ * 2.0f));
                NoiseCumulus[gy][gx] = FMath::PerlinNoise3D(FVector(SampleX * 15.0f, SampleY * 15.0f, TimeZ * 4.0f));
            }
        }

        float ChunkEvaporated = 0.0f;
        float ChunkRained = 0.0f;

        for (int32 Y = 0; Y < Manager->ChunkSize; Y++) {
            for (int32 X = 0; X < Manager->ChunkSize; X++) {
                int32 i = X + Y * Manager->ChunkSize;
                if (i >= Chunk.StaticCells.Num()) continue;

                FCellStaticData& SCell = Chunk.StaticCells[i];
                FCellDynamicData& DCell = Chunk.DynamicCells[i];

                float OldCloud = DCell.CloudDensity;
                float OldRain = DCell.Rainfall;
                float OldSnow = DCell.SnowAmount;
                float OldIce = DCell.GlacierIce;
                float OldHumidity = DCell.Humidity;

                float Altitude = FMath::Max(0.0f, SCell.Elevation - Manager->SeaLevel);
                float AltPenalty = (Altitude / 100.0f) * 1.5f * Manager->TemperatureScale;
                DCell.Temperature = Manager->BaseTemperature + SeasonTempOffset - AltPenalty + CosmicIrradiance;

                if (CosmicRadiation > 0.0f && SCell.Elevation > Manager->SeaLevel && DCell.CloudDensity < 0.2f) {
                    DCell.DangerLevel = FMath::Clamp(DCell.DangerLevel + (CosmicRadiation * 0.01f), 0.0f, 1.0f);
                }

                float Evaporation = 0.0f;
                if (SCell.Elevation <= Manager->SeaLevel || SCell.WaterType == EWaterType::Ocean) Evaporation = 1.0f;
                else if (SCell.WaterType == EWaterType::Lake) Evaporation = 0.75f;
                else if (SCell.WaterType == EWaterType::River || DCell.RiverDischarge > 0.5f || DCell.SurfaceWater > 0.1f) Evaporation = 0.4f;
                else if (SCell.Biome == EBiomeType::Swamp) Evaporation = 0.3f;
                else Evaporation = (DCell.Wetness * 0.15f) + (DCell.GroundWater / 100.0f * 0.02f);

                float Transpiration = 0.0f;
                if (SCell.TreeType != ETreeType::None) Transpiration += 0.2f;
                if (SCell.Biome == EBiomeType::Swamp) Transpiration += 0.3f;
                Transpiration += DCell.FloraDensity * 0.1f;

                float TempEvapMultiplier = FMath::Max(0.1f, (DCell.Temperature + 10.0f) / 30.0f);
                ChunkEvaporated += (Evaporation + Transpiration) * TempEvapMultiplier;

                int32 gx = FMath::Min(X / StepSize, GridSegments - 1);
                int32 gy = FMath::Min(Y / StepSize, GridSegments - 1);
                float tx = (float)(X - (gx * StepSize)) / StepSize;
                float ty = (float)(Y - (gy * StepSize)) / StepSize;

                float AmbientNoiseLocal = BiLerp(NoiseAmbient[gy][gx], NoiseAmbient[gy][gx + 1], NoiseAmbient[gy + 1][gx], NoiseAmbient[gy + 1][gx + 1], tx, ty);

                float TempFactor = FMath::Clamp((DCell.Temperature + 15.0f) / 45.0f, 0.2f, 1.5f);
                float TargetHumidity = Manager->GlobalHumidityRate * 0.3f + ((Evaporation + Transpiration) * TempFactor);
                DCell.Humidity = FMath::Lerp(DCell.Humidity, FMath::Clamp(TargetHumidity + AmbientNoiseLocal, 0.0f, 1.0f), 0.05f);

                float InterpMacro = BiLerp(NoiseMacro[gy][gx], NoiseMacro[gy][gx + 1], NoiseMacro[gy + 1][gx], NoiseMacro[gy + 1][gx + 1], tx, ty);
                float CloudMask = FMath::SmoothStep(0.45f, 0.65f, InterpMacro);
                float CloudBase = 0.0f;

                if (CloudMask > 0.0f) {
                    float InterpDetail = BiLerp(NoiseDetail[gy][gx], NoiseDetail[gy][gx + 1], NoiseDetail[gy + 1][gx], NoiseDetail[gy + 1][gx + 1], tx, ty);
                    CloudBase = InterpDetail * CloudMask * Manager->CloudCoverageMultiplier;

                    if (DCell.SurfaceWater > 0.0f) CloudBase += 0.25f * CloudMask;
                    if (SCell.Elevation > Manager->SeaLevel + 500.0f) CloudBase += 0.20f * CloudMask;
                    if (SCell.TreeType != ETreeType::None) CloudBase += 0.10f * CloudMask;
                }

                CloudBase *= FMath::Lerp(1.0f, SeasonRainMultiplier, 0.6f) * AvailableMoistureFactor;

                if (SeasonRainMultiplier < 0.8f) {
                    if (SCell.Elevation < Manager->SeaLevel + 300.0f) {
                        float InterpStorm = BiLerp(NoiseStorm[gy][gx], NoiseStorm[gy][gx + 1], NoiseStorm[gy + 1][gx], NoiseStorm[gy + 1][gx + 1], tx, ty);
                        if (InterpStorm > 0.85f) CloudBase += 1.5f;
                    }
                    float InterpCumulus = BiLerp(NoiseCumulus[gy][gx], NoiseCumulus[gy][gx + 1], NoiseCumulus[gy + 1][gx], NoiseCumulus[gy + 1][gx + 1], tx, ty);
                    if (InterpCumulus > 0.75f) CloudBase = FMath::Max(CloudBase, Manager->CloudDensityThreshold + 0.05f);
                }

                float NewRainfall = 0.0f;
                float RainThreshold = Manager->CloudDensityThreshold + 0.15f;
                if (CloudBase > RainThreshold) {
                    NewRainfall = (CloudBase - RainThreshold) * 3.5f * Manager->GlobalRainfall * SeasonRainMultiplier;
                }
                float NewCloudDensity = FMath::Max(0.0f, CloudBase - (NewRainfall * 0.45f));

                if (DCell.Temperature <= 0.0f) {
                    DCell.SnowAmount += NewRainfall;
                    ChunkRained += NewRainfall;
                    NewRainfall = 0.0f;
                    if (DCell.SnowAmount < 10.0f) DCell.SnowAmount += DCell.Humidity * 0.05f;

                    if (DCell.SnowAmount > 5.0f) {
                        float SnowPressure = (DCell.SnowAmount - 5.0f) * 0.02f;
                        float MaxGlacier = 100.0f;
                        float GrowthSpace = FMath::Max(0.0f, 1.0f - (DCell.GlacierIce / MaxGlacier));

                        float IceFormed = SnowPressure * GrowthSpace;
                        IceFormed = FMath::Min(IceFormed, DCell.SnowAmount * 0.1f);

                        DCell.SnowAmount -= IceFormed;
                        DCell.GlacierIce += IceFormed;
                    }
                }
                else {
                    ChunkRained += NewRainfall;

                    if (DCell.SnowAmount > 0.0f) {
                        float MeltAmount = FMath::Min(DCell.SnowAmount, DCell.Temperature * 0.05f);
                        DCell.SnowAmount -= MeltAmount;
                        NewRainfall += MeltAmount;
                        DCell.Wetness = FMath::Min(1.0f, DCell.Wetness + MeltAmount);
                    }
                    else if (DCell.GlacierIce > 0.0f) {
                        float IceMeltAmount = FMath::Min(DCell.GlacierIce, DCell.Temperature * 0.02f);
                        DCell.GlacierIce -= IceMeltAmount;
                        NewRainfall += IceMeltAmount;
                        DCell.Wetness = FMath::Min(1.0f, DCell.Wetness + IceMeltAmount);
                    }
                }

                if (NewRainfall > 0.05f && SCell.Elevation > Manager->SeaLevel) {
                    DCell.Wetness = FMath::Min(1.0f, DCell.Wetness + (NewRainfall * 0.8f));
                }
                else if (SCell.Elevation > Manager->SeaLevel) {
                    float EvaporationRate = FMath::Clamp(DCell.Temperature / 80.0f, 0.02f, 0.1f);
                    DCell.Wetness = FMath::Max(0.0f, DCell.Wetness - EvaporationRate);
                }
                else {
                    DCell.Wetness = 1.0f;
                }

                if (FMath::Abs(NewCloudDensity - OldCloud) > 0.1f || FMath::Abs(NewRainfall - OldRain) > 0.1f) bCloudDirty = true;
                if (FMath::Abs(DCell.SnowAmount - OldSnow) > 0.25f || FMath::Abs(DCell.GlacierIce - OldIce) > 0.5f || FMath::Abs(DCell.Humidity - OldHumidity) > 0.15f) {
                    bTerrainDirty = true;
                    if (FMath::Abs(DCell.SnowAmount - OldSnow) > 0.35f || FMath::Abs(DCell.GlacierIce - OldIce) > 1.0f) bFloraDirty = true;
                    Chunk.bWeatherChanged = true;
                }

                float UpwindAsh = 0.0f;
                const FCellDynamicData* UpwindCellD = Halo.GetDynamic(X + UpX, Y + UpY);
                if (UpwindCellD) {
                    UpwindAsh = UpwindCellD->AshDensity;
                }

                DCell.AshDensityBuffer = FMath::Max(0.0f, UpwindAsh * 0.95f - 0.02f);
                if (DCell.EruptionDaysRemaining > 0.0f) DCell.AshDensityBuffer = 5.0f;

                DCell.CloudDensity = NewCloudDensity;
                DCell.Rainfall = NewRainfall;
            }
        }

        LocalEvaporationBuffer[iter] = ChunkEvaporated;
        LocalRainfallBuffer[iter] = ChunkRained;

        if (bCloudDirty) SafeFlags1[iter] |= EChunkVisualDirty::Cloud;
        if (bTerrainDirty) SafeFlags1[iter] |= EChunkVisualDirty::Terrain;
        if (bFloraDirty) SafeFlags1[iter] |= EChunkVisualDirty::Flora;
        });

    for (int32 i = 0; i < (EndIdx - StartIdx); i++) {
        GlobalAtmosphericMoisture += LocalEvaporationBuffer[i] * 10.0f;
        GlobalAtmosphericMoisture -= LocalRainfallBuffer[i] * 50.0f;
    }
    GlobalAtmosphericMoisture = FMath::Clamp(GlobalAtmosphericMoisture, 10000.0f, 2000000.0f);

    for (int32 iter = 0; iter < (EndIdx - StartIdx); iter++) {
        if (SafeFlags1[iter] != 0) {
            Manager->RegisterVisualChange(ChunkKeys[StartIdx + iter], SafeFlags1[iter]);
        }
    }

    TArray<uint8> SafeFlags2;
    SafeFlags2.Init(0, EndIdx - StartIdx);

    ParallelFor(EndIdx - StartIdx, [&](int32 iter) {
        int32 idx = StartIdx + iter;
        if (!ChunkKeys.IsValidIndex(idx)) return;

        FChunkData& Chunk = WorldChunks[ChunkKeys[idx]];
        bool bAshDirty = false;

        for (int i = 0; i < Chunk.DynamicCells.Num(); i++) {
            FCellDynamicData& DCell = Chunk.DynamicCells[i];
            float OldAsh = DCell.AshDensity;
            DCell.AshDensity = DCell.AshDensityBuffer;

            if (FMath::Abs(DCell.AshDensity - OldAsh) > 0.1f) {
                bAshDirty = true;
            }
        }

        if (bAshDirty) SafeFlags2[iter] |= EChunkVisualDirty::Cloud;
        });

    for (int32 iter = 0; iter < (EndIdx - StartIdx); iter++) {
        if (SafeFlags2[iter] != 0) Manager->RegisterVisualChange(ChunkKeys[StartIdx + iter], SafeFlags2[iter]);
    }
}