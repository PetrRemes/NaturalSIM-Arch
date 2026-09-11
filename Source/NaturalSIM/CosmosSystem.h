#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "CosmosSystem.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UCosmosSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UCosmosSystem();
    virtual void BeginPlay() override;

    UPROPERTY(BlueprintReadOnly, Category = "Astronomy")
    TArray<FOrbitalBody> OrbitalBodies;

    UPROPERTY(BlueprintReadOnly, Category = "Astronomy")
    FCosmosState CurrentState;

    void InitCosmos(ASimWorldManager* Manager);
    void ProcessYearlyCosmos(ASimWorldManager* Manager, float DeltaDays);
};