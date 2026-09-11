#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "SimDiplomacyTypes.h"
#include "NationSystem.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UNationSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UNationSystem();

    UPROPERTY(BlueprintReadWrite, Category = "Nations")
    TArray<FNationData> Nations;

    UPROPERTY(BlueprintReadWrite, Category = "Nations")
    TArray<FNationStateDTO> NationStates;

    void AggregateNationData(const TArray<FSettlementData>& Settlements);

    bool FormNation(FSettlementData& CityA, FSettlementData& CityB, class ASimWorldManager* Manager);

    FCultureProfile InheritCultureFromMother(const FCultureProfile& MotherCulture, FVector2D ChildPosition, class ASimWorldManager* Manager);
    void AdaptCultureToEnvironment(FCultureProfile& Culture, FVector2D Position, class ASimWorldManager* Manager, float DeltaDays);
    FCultureProfile MergeCultures(const FCultureProfile& CultureA, const FCultureProfile& CultureB);

private:
    int32 GlobalNationCounter = 0;
};