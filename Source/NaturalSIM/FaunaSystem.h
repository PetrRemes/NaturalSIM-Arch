#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "FaunaSystem.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UFaunaSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UFaunaSystem();

    UPROPERTY(BlueprintReadWrite, Category = "Simulation")
    TArray<FAnimalData> Animals;

    TMap<FIntPoint, TArray<int32>> AnimalSpatialGrid;

    void InitializeFauna(ASimWorldManager* Manager);
    void UpdateSpatialGrid(ASimWorldManager* Manager);
    void ProcessFaunaSlice(TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, float DeltaTime, int32 StartIdx, int32 EndIdx);

    float EvaluateHabitat(ASimWorldManager* Manager, FVector2D Position, const FAnimalData& Animal);

    UFUNCTION(BlueprintCallable, Category = "Divine Actions")
    bool TrySpawnHerd(ASimWorldManager* Manager, FVector2D SpawnLoc, EAnimalType AnimalType);

protected:
    virtual void BeginPlay() override;
};