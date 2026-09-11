

#pragma once

#include "CoreMinimal.h"
#include "SimWorldTypes.h"
#include "SimDiplomacyTypes.generated.h"

UENUM(BlueprintType) enum class EDiplomaticState : uint8 { Neutral, TradeAgreement, Alliance, ColdWar, ActiveWar };

USTRUCT(BlueprintType)
struct FNationStateDTO {
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 NationID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 TotalPopulation = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float TotalWealth = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MilitaryPower = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<int32> BorderSettlementIDs;
};

USTRUCT(BlueprintType)
struct FInternalProvinceLink {
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CityA_ID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 CityB_ID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Cohesion = 50.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float InfrastructureLevel = 1.0f;
};

USTRUCT(BlueprintType)
struct FDiplomaticRelation {
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 NationA_ID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 NationB_ID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EDiplomaticState State = EDiplomaticState::Neutral;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Tension = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Trust = 50.0f;
};