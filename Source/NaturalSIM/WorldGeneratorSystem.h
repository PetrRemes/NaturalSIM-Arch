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

    // OPTIMALIZACE: Funkce, která bleskurychle naplní Cache pøed generováním samotných chunkù
    static void GenerateGlobalTerrainCache(TArray<FPrecomputedTerrain>& OutCache, const FChunkGenerationParameters& Params);

    static void ProcessChunkTerrain(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params);
};