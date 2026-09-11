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

void UClimateSystem::ProcessChunkClimate(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    float ChunkWorldSize = (Params.ChunkSize - 1) * 50.0f;
    
    // OPTIMALIZACE: Nested loops
    for (int32 Y = 0; Y < Params.ChunkSize; Y++) {
        for (int32 X = 0; X < Params.ChunkSize; X++) {
            int32 i = X + Y * Params.ChunkSize;
            if (i >= OutChunk.MicroCells.Num()) continue;

            float WorldX = (ChunkCoord.X * ChunkWorldSize) + (X * 50.0f);
            float WorldY = (ChunkCoord.Y * ChunkWorldSize) + (Y * 50.0f);

            float Altitude = FMath::Max(0.0f, OutChunk.MicroCells[i].Elevation - Params.SeaLevel);
            float AltPenalty = (Altitude / 100.0f) * 1.5f * Params.TemperatureScale;

            float BaseT = Params.BaseTemperature + (FMath::PerlinNoise2D(FVector2D(WorldX * 0.001f, WorldY * 0.001f)) * 5.0f);
            OutChunk.MicroCells[i].Temperature = BaseT - AltPenalty;

            float BaseH = Params.GlobalHumidityRate + (FMath::PerlinNoise2D(FVector2D(WorldY * 0.002f, WorldX * 0.002f)) * 0.5f);
            if (OutChunk.MicroCells[i].Elevation <= Params.SeaLevel) BaseH += 0.5f;
            OutChunk.MicroCells[i].Humidity = FMath::Clamp(BaseH, 0.0f, 1.0f);

            OutChunk.MicroCells[i].Rainfall = OutChunk.MicroCells[i].Humidity * Params.GlobalRainfall;
            OutChunk.MicroCells[i].Wetness = 0.0f;
            OutChunk.MicroCells[i].CloudDensity = 0.0f;
            OutChunk.MicroCells[i].AshDensity = 0.0f;
            OutChunk.MicroCells[i].AshDensityBuffer = 0.0f;

            if (OutChunk.MicroCells[i].Temperature <= 0.0f) {
                OutChunk.MicroCells[i].SnowAmount = FMath::Min(5.0f, FMath::Abs(OutChunk.MicroCells[i].Temperature) * 0.5f);
            }
            else {
                OutChunk.MicroCells[i].SnowAmount = 0.0f;
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

        FChunkData& Chunk = WorldChunks[ChunkKeys[idx]];
        bool bCloudDirty = false;
        bool bTerrainDirty = false;
        bool bFloraDirty = false;

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
                float WorldX = (ChunkKeys[idx].X * (Manager->ChunkSize - 1) * 50.0f) + (gx * StepSize * 50.0f);
                float WorldY = (ChunkKeys[idx].Y * (Manager->ChunkSize - 1) * 50.0f) + (gy * StepSize * 50.0f);

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

        // OPTIMALIZACE: Nested loops
        for (int32 Y = 0; Y < Manager->ChunkSize; Y++) {
            for (int32 X = 0; X < Manager->ChunkSize; X++) {
                int32 i = X + Y * Manager->ChunkSize;
                if (i >= Chunk.MicroCells.Num()) continue;

                FCellData& Cell = Chunk.MicroCells[i];
                int32 GlobalX = (ChunkKeys[idx].X * (Manager->ChunkSize - 1)) + X;
                int32 GlobalY = (ChunkKeys[idx].Y * (Manager->ChunkSize - 1)) + Y;

                float OldCloud = Cell.CloudDensity;
                float OldRain = Cell.Rainfall;
                float OldSnow = Cell.SnowAmount;
                float OldIce = Cell.GlacierIce;
                float OldHumidity = Cell.Humidity;

                float Altitude = FMath::Max(0.0f, Cell.Elevation - Manager->SeaLevel);
                float AltPenalty = (Altitude / 100.0f) * 1.5f * Manager->TemperatureScale;
                Cell.Temperature = Manager->BaseTemperature + SeasonTempOffset - AltPenalty + CosmicIrradiance;

                if (CosmicRadiation > 0.0f && Cell.Elevation > Manager->SeaLevel && Cell.CloudDensity < 0.2f) {
                    Cell.DangerLevel = FMath::Clamp(Cell.DangerLevel + (CosmicRadiation * 0.01f), 0.0f, 1.0f);
                }

                float Evaporation = 0.0f;
                if (Cell.Elevation <= Manager->SeaLevel || Cell.WaterType == EWaterType::Ocean) Evaporation = 1.0f;
                else if (Cell.WaterType == EWaterType::Lake) Evaporation = 0.75f;
                else if (Cell.WaterType == EWaterType::River || Cell.RiverDischarge > 0.5f || Cell.SurfaceWater > 0.1f) Evaporation = 0.4f;
                else if (Cell.Biome == EBiomeType::Swamp) Evaporation = 0.3f;
                else Evaporation = (Cell.Wetness * 0.15f) + (Cell.GroundWater / 100.0f * 0.02f);

                float Transpiration = 0.0f;
                if (Cell.TreeType != ETreeType::None) Transpiration += 0.2f;
                if (Cell.Biome == EBiomeType::Swamp) Transpiration += 0.3f;
                Transpiration += Cell.FloraDensity * 0.1f;

                float TempEvapMultiplier = FMath::Max(0.1f, (Cell.Temperature + 10.0f) / 30.0f);
                ChunkEvaporated += (Evaporation + Transpiration) * TempEvapMultiplier;

                int32 gx = FMath::Min(X / StepSize, GridSegments - 1);
                int32 gy = FMath::Min(Y / StepSize, GridSegments - 1);
                float tx = (float)(X - (gx * StepSize)) / StepSize;
                float ty = (float)(Y - (gy * StepSize)) / StepSize;

                float AmbientNoiseLocal = BiLerp(NoiseAmbient[gy][gx], NoiseAmbient[gy][gx + 1], NoiseAmbient[gy + 1][gx], NoiseAmbient[gy + 1][gx + 1], tx, ty);

                float TempFactor = FMath::Clamp((Cell.Temperature + 15.0f) / 45.0f, 0.2f, 1.5f);
                float TargetHumidity = Manager->GlobalHumidityRate * 0.3f + ((Evaporation + Transpiration) * TempFactor);
                Cell.Humidity = FMath::Lerp(Cell.Humidity, FMath::Clamp(TargetHumidity + AmbientNoiseLocal, 0.0f, 1.0f), 0.05f);

                float InterpMacro = BiLerp(NoiseMacro[gy][gx], NoiseMacro[gy][gx + 1], NoiseMacro[gy + 1][gx], NoiseMacro[gy + 1][gx + 1], tx, ty);
                float CloudMask = FMath::SmoothStep(0.45f, 0.65f, InterpMacro);
                float CloudBase = 0.0f;

                if (CloudMask > 0.0f) {
                    float InterpDetail = BiLerp(NoiseDetail[gy][gx], NoiseDetail[gy][gx + 1], NoiseDetail[gy + 1][gx], NoiseDetail[gy + 1][gx + 1], tx, ty);
                    CloudBase = InterpDetail * CloudMask * Manager->CloudCoverageMultiplier;

                    if (Cell.SurfaceWater > 0.0f) CloudBase += 0.25f * CloudMask;
                    if (Cell.Elevation > Manager->SeaLevel + 500.0f) CloudBase += 0.20f * CloudMask;
                    if (Cell.TreeType != ETreeType::None) CloudBase += 0.10f * CloudMask;
                }

                CloudBase *= FMath::Lerp(1.0f, SeasonRainMultiplier, 0.6f) * AvailableMoistureFactor;

                if (SeasonRainMultiplier < 0.8f) {
                    if (Cell.Elevation < Manager->SeaLevel + 300.0f) {
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

                if (Cell.Temperature <= 0.0f) {
                    Cell.SnowAmount += NewRainfall;
                    ChunkRained += NewRainfall;
                    NewRainfall = 0.0f;
                    if (Cell.SnowAmount < 10.0f) Cell.SnowAmount += Cell.Humidity * 0.05f;

                    if (Cell.SnowAmount > 5.0f) {
                        float SnowPressure = (Cell.SnowAmount - 5.0f) * 0.02f;
                        float MaxGlacier = 100.0f;
                        float GrowthSpace = FMath::Max(0.0f, 1.0f - (Cell.GlacierIce / MaxGlacier));

                        float IceFormed = SnowPressure * GrowthSpace;
                        IceFormed = FMath::Min(IceFormed, Cell.SnowAmount * 0.1f);

                        Cell.SnowAmount -= IceFormed;
                        Cell.GlacierIce += IceFormed;
                    }
                }
                else {
                    ChunkRained += NewRainfall;

                    if (Cell.SnowAmount > 0.0f) {
                        float MeltAmount = FMath::Min(Cell.SnowAmount, Cell.Temperature * 0.05f);
                        Cell.SnowAmount -= MeltAmount;
                        NewRainfall += MeltAmount;
                        Cell.Wetness = FMath::Min(1.0f, Cell.Wetness + MeltAmount);
                    }
                    else if (Cell.GlacierIce > 0.0f) {
                        float IceMeltAmount = FMath::Min(Cell.GlacierIce, Cell.Temperature * 0.02f);
                        Cell.GlacierIce -= IceMeltAmount;
                        NewRainfall += IceMeltAmount;
                        Cell.Wetness = FMath::Min(1.0f, Cell.Wetness + IceMeltAmount);
                    }
                }

                if (NewRainfall > 0.05f && Cell.Elevation > Manager->SeaLevel) {
                    Cell.Wetness = FMath::Min(1.0f, Cell.Wetness + (NewRainfall * 0.8f));
                }
                else if (Cell.Elevation > Manager->SeaLevel) {
                    float EvaporationRate = FMath::Clamp(Cell.Temperature / 80.0f, 0.02f, 0.1f);
                    Cell.Wetness = FMath::Max(0.0f, Cell.Wetness - EvaporationRate);
                }
                else {
                    Cell.Wetness = 1.0f;
                }

                if (FMath::Abs(NewCloudDensity - OldCloud) > 0.1f || FMath::Abs(NewRainfall - OldRain) > 0.1f) bCloudDirty = true;
                if (FMath::Abs(Cell.SnowAmount - OldSnow) > 0.25f || FMath::Abs(Cell.GlacierIce - OldIce) > 0.5f || FMath::Abs(Cell.Humidity - OldHumidity) > 0.15f) {
                    bTerrainDirty = true;
                    if (FMath::Abs(Cell.SnowAmount - OldSnow) > 0.35f || FMath::Abs(Cell.GlacierIce - OldIce) > 1.0f) bFloraDirty = true;
                    Chunk.bWeatherChanged = true;
                }

                float UpwindAsh = 0.0f;
                const FCellData* UpwindCell = nullptr;
                if (Manager->GetCellGlobalPtr(GlobalX + UpX, GlobalY + UpY, UpwindCell)) {
                    UpwindAsh = UpwindCell->AshDensity;
                }

                Cell.AshDensityBuffer = FMath::Max(0.0f, UpwindAsh * 0.95f - 0.02f);
                if (Cell.EruptionDaysRemaining > 0.0f) Cell.AshDensityBuffer = 5.0f;

                Cell.CloudDensity = NewCloudDensity;
                Cell.Rainfall = NewRainfall;
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

        for (int i = 0; i < Chunk.MicroCells.Num(); i++) {
            FCellData& Cell = Chunk.MicroCells[i];
            float OldAsh = Cell.AshDensity;
            Cell.AshDensity = Cell.AshDensityBuffer;

            if (FMath::Abs(Cell.AshDensity - OldAsh) > 0.1f) {
                bAshDirty = true;
            }
        }

        if (bAshDirty) SafeFlags2[iter] |= EChunkVisualDirty::Cloud;
        });

    for (int32 iter = 0; iter < (EndIdx - StartIdx); iter++) {
        if (SafeFlags2[iter] != 0) Manager->RegisterVisualChange(ChunkKeys[StartIdx + iter], SafeFlags2[iter]);
    }
}


void UClimateSystem::BuildCloudMesh(TSharedPtr<FChunkMeshData> MeshData, const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    if (!MeshData.IsValid()) return;
    int32 ChunkSize = Params.ChunkSize; float CellSize = 50.0f;
    int32 Step = Params.CloudResolutionStep; float HalfStep = (CellSize * Step) * 0.5f;

    float SeasonAlpha = (Params.CurrentDay / 365.0f) * PI * 2.0f;
    FVector2D GlobalWind(FMath::Cos(SeasonAlpha), FMath::Sin(SeasonAlpha));
    GlobalWind.Normalize();

    auto AddBox = [&](FVector C, float S, FLinearColor Color) {
        FVector P1(C.X - S, C.Y - S, C.Z - S); FVector P2(C.X + S, C.Y - S, C.Z - S);
        FVector P3(C.X + S, C.Y + S, C.Z - S); FVector P4(C.X - S, C.Y + S, C.Z - S);
        FVector T1(C.X - S, C.Y - S, C.Z + S); FVector T2(C.X + S, C.Y - S, C.Z + S);
        FVector T3(C.X + S, C.Y + S, C.Z + S); FVector T4(C.X - S, C.Y + S, C.Z + S);

        auto AddFace = [&](FVector V1, FVector V2, FVector V3, FVector V4) {
            int32 V = MeshData->CloudVertices.Num();
            MeshData->CloudVertices.Add(V1); MeshData->CloudVertices.Add(V2); MeshData->CloudVertices.Add(V3); MeshData->CloudVertices.Add(V4);
            MeshData->CloudTriangles.Add(V); MeshData->CloudTriangles.Add(V + 1); MeshData->CloudTriangles.Add(V + 2);
            MeshData->CloudTriangles.Add(V); MeshData->CloudTriangles.Add(V + 2); MeshData->CloudTriangles.Add(V + 3);
            for (int i = 0; i < 4; i++) { MeshData->CloudUV0.Add(FVector2D::ZeroVector); MeshData->CloudColors.Add(Color); }
            };
        AddFace(P4, P1, P2, P3); AddFace(T1, T4, T3, T2);
        AddFace(P1, T1, T2, P2); AddFace(P2, T2, T3, P3);
        AddFace(P3, T3, T4, P4); AddFace(P4, T4, T1, P1);
        };

    auto AddRainDrop = [&](FVector CTop, FVector CBot, float R, FLinearColor Color) {
        auto AddQuad = [&](FVector V1, FVector V2, FVector V3, FVector V4) {
            int32 V = MeshData->CloudVertices.Num();
            MeshData->CloudVertices.Add(V1); MeshData->CloudVertices.Add(V2); MeshData->CloudVertices.Add(V3); MeshData->CloudVertices.Add(V4);
            MeshData->CloudTriangles.Add(V); MeshData->CloudTriangles.Add(V + 1); MeshData->CloudTriangles.Add(V + 2);
            MeshData->CloudTriangles.Add(V); MeshData->CloudTriangles.Add(V + 2); MeshData->CloudTriangles.Add(V + 3);
            for (int i = 0; i < 4; i++) { MeshData->CloudUV0.Add(FVector2D::ZeroVector); MeshData->CloudColors.Add(Color); }
            };
        FVector R1T(CTop.X - R, CTop.Y - R, CTop.Z); FVector R2T(CTop.X + R, CTop.Y + R, CTop.Z);
        FVector R1B(CBot.X - R, CBot.Y - R, CBot.Z); FVector R2B(CBot.X + R, CBot.Y + R, CBot.Z);
        AddQuad(R1B, R1T, R2T, R2B); AddQuad(R2B, R2T, R1T, R1B);
        };

    for (int32 Y = 0; Y < ChunkSize; Y += Step) {
        for (int32 X = 0; X < ChunkSize; X += Step) {
            int32 Index = FMath::Min(X + (Y * ChunkSize), Chunk.MicroCells.Num() - 1);
            const FCellData& Cell = Chunk.MicroCells[Index];

            float LocalX = (ChunkCoord.X * (ChunkSize - 1) * CellSize) + (X * CellSize);
            float LocalY = (ChunkCoord.Y * (ChunkSize - 1) * CellSize) + (Y * CellSize);

            float VisualCloudDensity = Cell.CloudDensity;
            float VisualRainfall = Cell.Rainfall;
            bool bHasAsh = Cell.AshDensity > 0.05f;

            if (Cell.Elevation > Params.SeaLevel && VisualRainfall < 0.01f && !bHasAsh) {
                int32 WakeX = FMath::Clamp(X + FMath::RoundToInt(GlobalWind.X * 12.0f), 0, ChunkSize - 1);
                int32 WakeY = FMath::Clamp(Y + FMath::RoundToInt(GlobalWind.Y * 12.0f), 0, ChunkSize - 1);
                float WakeRain = Chunk.MicroCells[WakeX + WakeY * ChunkSize].Rainfall;

                if (WakeRain > 0.15f) {
                    float FogNoise = FMath::PerlinNoise2D(FVector2D(LocalX * 0.001f, LocalY * 0.001f));
                    if (FogNoise > -0.2f) {
                        float FogAlpha = FMath::Clamp(WakeRain * 0.4f * (FogNoise + 0.5f), 0.0f, 0.35f);
                        if (FogAlpha > 0.05f) {
                            FVector FogCenter(LocalX, LocalY, Cell.Elevation + 10.0f + HalfStep);
                            FLinearColor FogColor(0.95f, 0.95f, 0.95f, FogAlpha);
                            AddBox(FogCenter, HalfStep * 0.98f, FogColor);
                        }
                    }
                }
            }

            if (VisualCloudDensity < Params.CloudDensityThreshold && !bHasAsh && Cell.EruptionDaysRemaining <= 0.0f) continue;

            float TerrenZ = FMath::Max(0.0f, Cell.Elevation - Params.SeaLevel);
            float CloudBaseZ = Params.SeaLevel + 1200.0f + (TerrenZ * 0.5f);
            CloudBaseZ += FMath::PerlinNoise2D(FVector2D(LocalX * 0.001f, LocalY * 0.001f)) * 50.0f;

            if (Cell.EruptionDaysRemaining > 0.0f) {
                float Z = Cell.Elevation + HalfStep;
                while (Z < CloudBaseZ) {
                    FVector Center(LocalX, LocalY, Z);
                    AddBox(Center, HalfStep * 1.5f, FLinearColor(0.05f, 0.05f, 0.05f, 0.98f));
                    Z += (HalfStep * 2.0f);
                }
            }

            if (bHasAsh) {
                float AshAlpha = FMath::Clamp(Cell.AshDensity / 5.0f, 0.0f, 1.0f);
                FLinearColor AshColor = FLinearColor(0.12f, 0.10f, 0.10f, AshAlpha * 0.95f);
                FVector Center(LocalX, LocalY, CloudBaseZ - HalfStep);
                AddBox(Center, HalfStep * 1.2f, AshColor);
            }

            if (VisualCloudDensity >= Params.CloudDensityThreshold) {
                float Surplus = VisualCloudDensity - Params.CloudDensityThreshold;

                int32 StackCount = 1;
                if (Surplus > 0.5f) StackCount = 4;
                else if (Surplus > 0.3f) StackCount = 3;
                else if (Surplus > 0.1f) StackCount = 2;

                float Darkening = FMath::Clamp(Surplus * 1.5f, 0.0f, 1.0f);
                FLinearColor StormColor = FLinearColor(0.12f, 0.22f, 0.50f, 0.95f);
                FLinearColor CloudColor = FMath::Lerp(FLinearColor(1.0f, 1.0f, 1.0f, 0.90f), StormColor, Darkening);

                for (int z = 0; z < StackCount; z++) {
                    FVector Center(LocalX, LocalY, CloudBaseZ + (z * HalfStep * 2.0f));
                    FLinearColor BlockColor = CloudColor * (1.0f - (z * 0.08f));
                    BlockColor.A = CloudColor.A;
                    AddBox(Center, HalfStep * 0.95f, BlockColor);
                }

                if (VisualRainfall > 0.05f) {
                    FVector RainTop(LocalX, LocalY, CloudBaseZ - HalfStep);
                    FVector RainBot(LocalX, LocalY, Cell.Elevation);
                    FLinearColor RainColor(0.6f, 0.7f, 0.9f, FMath::Clamp(VisualRainfall * 0.3f, 0.0f, 0.5f));
                    AddRainDrop(RainTop, RainBot, HalfStep * 0.4f, RainColor);
                }
            }
        }
    }
}