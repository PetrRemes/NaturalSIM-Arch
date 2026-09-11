#include "ManaSystem.h"

UManaSystem::UManaSystem()
{
    PrimaryComponentTick.bCanEverTick = false;
    CurrentMana = 0.0f;
    WorldActivityLevel = 0.0f;
    BaseActivityThreshold = 100.0f;

    PendingLifeMana = 0.0f; PendingEventMana = 0.0f;
    PendingFlora = 0.0f; PendingFauna = 0.0f; PendingHuman = 0.0f; PendingCiv = 0.0f;

    YesterdayLifeMana = 0.0f; YesterdayEventMana = 0.0f;
    YesterdayFlora = 0.0f; YesterdayFauna = 0.0f; YesterdayHuman = 0.0f; YesterdayCiv = 0.0f;
    YesterdayTotalScaled = 0.0f;
}

void UManaSystem::BeginPlay()
{
    Super::BeginPlay();
    CurrentMana = 0.0f;
}

void UManaSystem::UpdateInteractionCapacity(int32 NumTribes, int32 NumSettlements, int32 NumAnimals, int32 TotalPopulation)
{
    float TribeWeight = NumTribes * 10.0f;
    float SettWeight = NumSettlements * 25.0f;
    float AnimalWeight = NumAnimals * 0.5f;
    float PopWeight = TotalPopulation * 0.05f;

    WorldActivityLevel = TribeWeight + SettWeight + AnimalWeight + PopWeight;
}

float UManaSystem::GetManaValue(EManaSourceType Source) const
{
    switch (Source) {
    case EManaSourceType::FloraBirth: return Val_FloraBirth;
    case EManaSourceType::FloraDeath: return Val_FloraDeath;
    case EManaSourceType::FaunaBirth: return Val_FaunaBirth;
    case EManaSourceType::FaunaDeath: return Val_FaunaDeath;
    case EManaSourceType::HumanBirth: return Val_HumanBirth;
    case EManaSourceType::HumanDeath: return Val_HumanDeath;
    case EManaSourceType::Encounter: return Val_Encounter;
    case EManaSourceType::Trade: return Val_Trade;
    case EManaSourceType::Conflict: return Val_Conflict;
    case EManaSourceType::SettlementFounded: return Val_Settlement;
    case EManaSourceType::NationFormed: return Val_Nation;
    case EManaSourceType::War: return Val_War;
    case EManaSourceType::Disaster: return Val_Disaster;
    case EManaSourceType::Discovery: return Val_Discovery;
    case EManaSourceType::Technology: return Val_Technology;
    }
    return 0.0f;
}

void UManaSystem::AccumulateLifeMana(float Units, EManaSourceType Source)
{
    if (Units <= 0.0f) return;
    float Val = Units * GetManaValue(Source);
    PendingLifeMana += Val;

    if (Source == EManaSourceType::FloraBirth || Source == EManaSourceType::FloraDeath) PendingFlora += Val;
    else if (Source == EManaSourceType::FaunaBirth || Source == EManaSourceType::FaunaDeath) PendingFauna += Val;
    else if (Source == EManaSourceType::HumanBirth || Source == EManaSourceType::HumanDeath) PendingHuman += Val;
}

void UManaSystem::AccumulateEventMana(float Units, EManaSourceType Source)
{
    if (Units <= 0.0f) return;
    float Val = Units * GetManaValue(Source);
    PendingEventMana += Val;
    PendingCiv += Val;
}

void UManaSystem::ProcessDailyMana()
{
    YesterdayLifeMana = PendingLifeMana; YesterdayEventMana = PendingEventMana;
    YesterdayFlora = PendingFlora; YesterdayFauna = PendingFauna; YesterdayHuman = PendingHuman; YesterdayCiv = PendingCiv;

    float TotalPending = PendingLifeMana + PendingEventMana;

    if (TotalPending > 0.0f) {
        float LogFactor = FMath::Loge(1.0f + (WorldActivityLevel / BaseActivityThreshold));
        float Scale = 1.0f / (1.0f + LogFactor);

        float FinalAmount = TotalPending * Scale;
        FinalAmount = FMath::Max(0.01f, FinalAmount);

        CurrentMana += FinalAmount;
        YesterdayTotalScaled = FinalAmount;
    }
    else {
        YesterdayTotalScaled = 0.0f;
    }

    PendingLifeMana = 0.0f; PendingEventMana = 0.0f;
    PendingFlora = 0.0f; PendingFauna = 0.0f; PendingHuman = 0.0f; PendingCiv = 0.0f;
}

bool UManaSystem::SpendMana(float Amount, FString Reason)
{
    if (Amount <= 0.0f) return true;

    if (CurrentMana >= Amount) {
        CurrentMana -= Amount;
        return true;
    }
    return false;
}

float UManaSystem::GetCurrentMana() const
{
    return CurrentMana;
}