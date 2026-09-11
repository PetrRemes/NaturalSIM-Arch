#pragma once

#include "CoreMinimal.h"
#include "HistoryTypes.generated.h"

// Záznam o jedné konkrétní historické události
USTRUCT(BlueprintType)
struct FHistoricalEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "History")
    int32 Year = 0;

    UPROPERTY(BlueprintReadOnly, Category = "History")
    double Day = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "History")
    FString EventType;

    UPROPERTY(BlueprintReadOnly, Category = "History")
    FString Title;

    UPROPERTY(BlueprintReadOnly, Category = "History")
    FString Description;

    UPROPERTY(BlueprintReadOnly, Category = "History")
    FVector2D Location = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "History")
    int32 EntityID = -1;
};