#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "TectonicSystem.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UTectonicSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UTectonicSystem();
    virtual void BeginPlay() override;

    static void ProcessChunkTectonics(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params);
    void ProcessDailyTectonics(const TArray<FIntPoint>& ChunkKeys, TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, float DeltaDays);

    UFUNCTION(BlueprintCallable, Category = "Divine Actions")
    bool TryRaiseTerrain(class ASimWorldManager* Manager, int32 GlobalX, int32 GlobalY, float Amount);

    UFUNCTION(BlueprintCallable, Category = "Divine Actions")
    bool SuppressDisaster(class ASimWorldManager* Manager, FIntPoint ChunkCoord);

    UFUNCTION(BlueprintCallable, Category = "Divine Actions")
    bool EnhancedDisaster(class ASimWorldManager* Manager, FIntPoint ChunkCoord, float ExtraIntensity);

private:
    FCriticalSection EarthquakeMutex;
    TArray<TPair<FIntPoint, float>> PendingEarthquakes;
};