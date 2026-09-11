#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "WorldGeneratorSystem.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UWorldGeneratorSystem : public UActorComponent
{
    GENERATED_BODY()
public:
    UWorldGeneratorSystem();
    virtual void BeginPlay() override;

    static void ProcessChunkTerrain(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params);
};