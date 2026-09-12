#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "TechnologySystem.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UTechnologySystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UTechnologySystem();
    virtual void BeginPlay() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tech Network")
    TArray<FDiscoveryDefinition> KnownDiscoveries;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tech Network")
    TArray<FTechRequirement> KnownTechnologies;

    void GainKnowledge(FKnowledgeContainer& Container, EKnowledgeField Field, float Amount, float LiteracyBonus = 0.0f, float PressBonus = 0.0f);

    void EvaluateEntityKnowledge(FKnowledgeContainer& Knowledge, FCultureProfile& Culture, const FCellStaticData& SCell, const FCellDynamicData& DCell, ASimWorldManager* Manager, int32 EntityID, bool bIsSettlement, FVector2D Location);

private:
    void RegisterDefaultDiscoveries();
    void RegisterDefaultTechTree();

    bool CheckEnvironmentalTrigger(FName Trigger, const FCellStaticData& SCell, const FCellDynamicData& DCell, ASimWorldManager* Manager);
    bool CheckExperienceTrigger(FName Trigger, const FKnowledgeContainer& Knowledge);
};