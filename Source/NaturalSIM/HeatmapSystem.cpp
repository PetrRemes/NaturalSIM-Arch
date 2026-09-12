#include "HeatmapSystem.h"
#include "SimWorldManager.h"
#include "Async/ParallelFor.h"

UHeatmapSystem::UHeatmapSystem() { PrimaryComponentTick.bCanEverTick = false; }
void UHeatmapSystem::BeginPlay() { Super::BeginPlay(); }

FChunkHeatmap UHeatmapSystem::CalculateChunkHeatmap_Static(const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    FChunkHeatmap NewHeatmap;
    if (Chunk.StaticCells.Num() == 0) return NewHeatmap;

    float TotalElevation = 0.0f; float TotalFlora = 0.0f;
    float TotalTemp = 0.0f; float TotalHum = 0.0f;
    int32 WaterCellCount = 0; int32 MountainCellCount = 0;
    float MinElev = 99999.0f; float MaxElev = -99999.0f;

    for (int i = 0; i < Chunk.StaticCells.Num(); i++) {
        const FCellStaticData& SCell = Chunk.StaticCells[i];
        const FCellDynamicData& DCell = Chunk.DynamicCells[i];

        TotalElevation += SCell.Elevation; TotalFlora += DCell.FloraDensity;
        TotalTemp += DCell.Temperature; TotalHum += DCell.Humidity;
        if (DCell.SurfaceWater >= 1.0f) WaterCellCount++;
        if (SCell.Elevation > Params.SeaLevel + 500.0f) MountainCellCount++;
        if (SCell.Elevation < MinElev) MinElev = SCell.Elevation;
        if (SCell.Elevation > MaxElev) MaxElev = SCell.Elevation;
    }

    int32 CellCount = Chunk.StaticCells.Num();
    NewHeatmap.AvgElevation = TotalElevation / CellCount;
    NewHeatmap.AvgFlora = TotalFlora / CellCount;
    NewHeatmap.AvgTemperature = TotalTemp / CellCount;
    NewHeatmap.AvgHumidity = TotalHum / CellCount;
    NewHeatmap.WaterRatio = (float)WaterCellCount / CellCount;

    float Variance = MaxElev - MinElev;
    float WaterScore = FMath::Clamp(NewHeatmap.WaterRatio * 3.0f, 0.0f, 1.0f);
    float ElevationScore = FMath::Clamp((NewHeatmap.AvgElevation - Params.SeaLevel) / 1000.0f, 0.0f, 1.0f);
    float MountainScore = FMath::Clamp(Variance / 400.0f, 0.0f, 1.0f);

    NewHeatmap.Defensibility = (WaterScore * 0.4f) + (ElevationScore * 0.3f) + (MountainScore * 0.3f);
    return NewHeatmap;
}

void UHeatmapSystem::UpdateClimateHeatmaps(const TMap<FIntPoint, FChunkData>& WorldChunks)
{
    TArray<FIntPoint> Keys;
    ChunkHeatmaps.GetKeys(Keys);

    TArray<FChunkHeatmap> TempResults;
    TempResults.SetNum(Keys.Num());

    ParallelFor(Keys.Num(), [&](int32 idx) {
        FIntPoint Coord = Keys[idx];
        FChunkHeatmap ComputedHeatmap = ChunkHeatmaps[Coord];

        if (const FChunkData* Chunk = WorldChunks.Find(Coord)) {
            if (Chunk->StaticCells.Num() > 0) {
                float TotalTemp = 0.0f; float TotalRain = 0.0f; float TotalFlora = 0.0f;
                for (const FCellDynamicData& DCell : Chunk->DynamicCells) {
                    TotalTemp += DCell.Temperature; TotalRain += DCell.Rainfall; TotalFlora += DCell.FloraDensity;
                }
                int32 Count = Chunk->DynamicCells.Num();
                ComputedHeatmap.AvgTemperature = TotalTemp / Count;
                ComputedHeatmap.AvgRainfall = TotalRain / Count;
                ComputedHeatmap.AvgFlora = TotalFlora / Count;
            }
        }
        TempResults[idx] = ComputedHeatmap;
        });

    for (int32 i = 0; i < Keys.Num(); ++i) {
        ChunkHeatmaps.Add(Keys[i], TempResults[i]);
    }
}

FChunkHeatmap UHeatmapSystem::GetChunkHeatmap(FIntPoint ChunkCoord) const
{
    if (const FChunkHeatmap* FoundHeatmap = ChunkHeatmaps.Find(ChunkCoord)) {
        return *FoundHeatmap;
    }
    return FChunkHeatmap();
}