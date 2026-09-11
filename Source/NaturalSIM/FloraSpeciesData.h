#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SimWorldTypes.h"
#include "FloraSpeciesData.generated.h"

UCLASS(BlueprintType)
class NATURALSIM_API UFloraSpeciesData : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
    FString SpeciesName = "Novy Druh";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
    uint8 SpeciesID = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
    ETreeType VisualModel = ETreeType::Oak;

    // --- BIOLOGIE ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biology")
    float GrowthSpeed = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biology")
    float MaxLifespanYears = 150.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biology")
    float RootPower = 1.0f;

    // --- EKOLOGIE A ZNEÈIŠTÌNÍ ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ecology")
    float MaxPollutionTolerance = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ecology")
    float PollutionAffinity = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ecology")
    float ToxinAbsorptionRate = 0.0f;
};