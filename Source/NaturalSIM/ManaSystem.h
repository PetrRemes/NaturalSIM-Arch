#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ManaSystem.generated.h"

UENUM(BlueprintType)
enum class EManaSourceType : uint8
{
    FloraBirth,
    FloraDeath,
    FaunaBirth,
    FaunaDeath,
    HumanBirth,
    HumanDeath,
    Encounter,
    Trade,
    Conflict,
    SettlementFounded,
    NationFormed,
    War,
    Disaster,
    Discovery,
    Technology
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UManaSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UManaSystem();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mana|State")
    float CurrentMana;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mana|Scaling")
    float WorldActivityLevel;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Scaling")
    float BaseActivityThreshold = 100.0f;

    float PendingLifeMana;
    float PendingEventMana;

    // Sub-buffery pro detailní telemetrii
    float PendingFlora;
    float PendingFauna;
    float PendingHuman;
    float PendingCiv;

    // Denní statistiky pro HUD
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mana|Statistics") float YesterdayLifeMana;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mana|Statistics") float YesterdayEventMana;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mana|Statistics") float YesterdayFlora;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mana|Statistics") float YesterdayFauna;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mana|Statistics") float YesterdayHuman;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mana|Statistics") float YesterdayCiv;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mana|Statistics") float YesterdayTotalScaled;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_FloraBirth = 0.01f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_FloraDeath = 0.005f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_FaunaBirth = 0.02f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_FaunaDeath = 0.005f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_HumanBirth = 0.05f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_HumanDeath = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_Encounter = 0.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_Trade = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_Conflict = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_Settlement = 5.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_Nation = 15.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_War = 5.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_Disaster = 5.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_Discovery = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Economy") float Val_Technology = 2.0f;

    UFUNCTION(BlueprintCallable, Category = "Mana") void AccumulateLifeMana(float Units, EManaSourceType Source);
    UFUNCTION(BlueprintCallable, Category = "Mana") void AccumulateEventMana(float Units, EManaSourceType Source);
    UFUNCTION(BlueprintCallable, Category = "Mana") bool SpendMana(float Amount, FString Reason);
    UFUNCTION(BlueprintPure, Category = "Mana") float GetCurrentMana() const;

    void UpdateInteractionCapacity(int32 NumTribes, int32 NumSettlements, int32 NumAnimals, int32 TotalPopulation);
    void ProcessDailyMana();

protected:
    virtual void BeginPlay() override;
    float GetManaValue(EManaSourceType Source) const;
};