

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "RelationSystem.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API URelationSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    URelationSystem();

    void ProcessTribeInteractions(TArray<FTribeData>& Tribes, class ASimWorldManager* Manager);
    void ProcessSettlementInteractions(TArray<FSettlementData>& Settlements, class ASimWorldManager* Manager);

    TMap<FIntPoint, TArray<int32>> TribeSpatialGrid;
    TMap<FIntPoint, TArray<int32>> SettlementSpatialGrid;

protected:
    virtual void BeginPlay() override;
};