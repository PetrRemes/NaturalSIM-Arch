#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "SimDiplomacyTypes.h"
#include "InternalPoliticsSystem.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UInternalPoliticsSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UInternalPoliticsSystem();

    // FÁZE 3: Zmìna z pomalého O(N^2) pole na okamžitý O(1) TMap
    UPROPERTY(BlueprintReadWrite, Category = "Internal Politics")
    TMap<int64, FInternalProvinceLink> InternalLinks;

    void ProcessInternalRelations(TArray<FSettlementData>& Settlements, ASimWorldManager* Manager);

protected:
    virtual void BeginPlay() override;
};