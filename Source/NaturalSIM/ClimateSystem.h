#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "ClimateSystem.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UClimateSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UClimateSystem();
    virtual void BeginPlay() override;

    // FÁZE 4: Globální zásobník atmosférické vody (fyzikální kolobìh)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Climate")
    float GlobalAtmosphericMoisture = 500000.0f;

    static void ProcessChunkClimate(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params);

    void UpdateDailyClimate(const TArray<FIntPoint>& ChunkKeys, TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, int32 StartIdx, int32 EndIdx);

    static void BuildCloudMesh(TSharedPtr<FChunkMeshData> MeshData, const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params);
};