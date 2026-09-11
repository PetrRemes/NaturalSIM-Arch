#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "DisasterSystem.generated.h"

class ASimWorldManager;

UENUM(BlueprintType)
enum class EDisasterType : uint8
{
    Flood,
    VolcanicEruption,
    Earthquake,
    Drought
};

USTRUCT(BlueprintType)
struct FDisasterWarning
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category = "Disaster") EDisasterType Type = EDisasterType::Flood;
    UPROPERTY(BlueprintReadOnly, Category = "Disaster") FVector2D Epicenter = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category = "Disaster") float Severity = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "Disaster") float DaysToImpact = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "Disaster") float Radius = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "Disaster") TArray<int32> AffectedSettlementIDs;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UDisasterSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UDisasterSystem();
    virtual void BeginPlay() override;

    UPROPERTY(BlueprintReadOnly, Category = "Disasters")
    TArray<FDisasterWarning> ActiveWarnings;

    // Hlavní funkce volaná z Directoru (1x dennì)
    void ProcessDisasters(ASimWorldManager* Manager, float DeltaDays);

private:
    void EvaluateFloodRisks(ASimWorldManager* Manager);
    void EvaluateVolcanicRisks(ASimWorldManager* Manager);
};