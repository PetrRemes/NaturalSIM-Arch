#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "SimDiplomacyTypes.h"
#include "DiplomacySystem.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UDiplomacySystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UDiplomacySystem();

    // FÁZE 3: Zmìna z pomalého O(N^2) pole na okamžitý O(1) TMap pro stovky státù
    UPROPERTY(BlueprintReadWrite, Category = "Diplomacy")
    TMap<int64, FDiplomaticRelation> DiplomaticRelations;

    void ProcessDiplomacy(ASimWorldManager* Manager);

    EDiplomaticState GetRelationState(int32 NationA, int32 NationB);

protected:
    virtual void BeginPlay() override;
};