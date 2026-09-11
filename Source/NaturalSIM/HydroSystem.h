#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "HydroSystem.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UHydroSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UHydroSystem();
    virtual void BeginPlay() override;

    static void ProcessChunkWater(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params);

    void ProcessHydroSlice(const TArray<FIntPoint>& ChunkKeys, TMap<FIntPoint, FChunkData>& WorldChunks, class ASimWorldManager* Manager, int32 StartIdx, int32 EndIdx, bool bRunGeomorphology = true, float DeltaDays = 1.0f);

    void ProcessCoastalHydrology(class ASimWorldManager* Manager);

    UFUNCTION(BlueprintCallable, Category = "Divine Actions")
    bool TryCreateSpring(class ASimWorldManager* Manager, int32 GlobalX, int32 GlobalY);
};