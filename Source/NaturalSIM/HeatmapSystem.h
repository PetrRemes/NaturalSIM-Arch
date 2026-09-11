#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "HeatmapSystem.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UHeatmapSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UHeatmapSystem();
    virtual void BeginPlay() override;

    static FChunkHeatmap CalculateChunkHeatmap_Static(const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params);
    void UpdateClimateHeatmaps(const TMap<FIntPoint, FChunkData>& WorldChunks);
    FChunkHeatmap GetChunkHeatmap(FIntPoint ChunkCoord) const;

    UPROPERTY()
    TMap<FIntPoint, FChunkHeatmap> ChunkHeatmaps;
};