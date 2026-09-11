#include "DiplomacySystem.h"
#include "SimWorldManager.h"
#include "NationSystem.h"
#include "HistorySystem.h"
#include "SettlementSystem.h"
#include "ManaSystem.h"

UDiplomacySystem::UDiplomacySystem() { PrimaryComponentTick.bCanEverTick = false; }
void UDiplomacySystem::BeginPlay() { Super::BeginPlay(); }

// FÁZE 3: Výpoèet unikátního ID pro hash mapu (Oba národy složené do jednoho 64-bitového klíèe)
static int64 GetPairKey(int32 ID_A, int32 ID_B) {
    int64 MinID = FMath::Min(ID_A, ID_B);
    int64 MaxID = FMath::Max(ID_A, ID_B);
    return (MinID << 32) | (MaxID & 0xFFFFFFFF);
}

EDiplomaticState UDiplomacySystem::GetRelationState(int32 NationA, int32 NationB) {
    int64 Key = GetPairKey(NationA, NationB);
    if (const FDiplomaticRelation* Rel = DiplomaticRelations.Find(Key)) {
        return Rel->State;
    }
    return EDiplomaticState::Neutral;
}

void UDiplomacySystem::ProcessDiplomacy(ASimWorldManager* Manager)
{
    if (!Manager || !Manager->NationModule || Manager->NationModule->NationStates.Num() < 2) return;

    TArray<FNationStateDTO>& States = Manager->NationModule->NationStates;
    TArray<FNationData>& NationsFull = Manager->NationModule->Nations;

    for (int32 i = 0; i < States.Num(); i++) {
        for (int32 j = i + 1; j < States.Num(); j++) {
            FNationStateDTO& StateA = States[i];
            FNationStateDTO& StateB = States[j];

            FNationData* NatDataA = nullptr;
            FNationData* NatDataB = nullptr;
            for (FNationData& N : NationsFull) {
                if (N.NationID == StateA.NationID) NatDataA = &N;
                if (N.NationID == StateB.NationID) NatDataB = &N;
            }

            if (!NatDataA || !NatDataB) continue;

            int64 Key = GetPairKey(StateA.NationID, StateB.NationID);
            FDiplomaticRelation& Relation = DiplomaticRelations.FindOrAdd(Key);

            // Pokud je záznam nový, inicializujeme ho
            if (Relation.NationA_ID == -1) {
                Relation.NationA_ID = StateA.NationID;
                Relation.NationB_ID = StateB.NationID;
                Relation.State = EDiplomaticState::Neutral;
                Relation.Tension = FMath::RandRange(10.0f, 40.0f);
                Relation.Trust = FMath::RandRange(20.0f, 60.0f);
            }

            float CulturalDistance = 0.0f;
            for (auto& PairA : NatDataA->NationalCulture.Pillars) {
                float ValB = NatDataB->NationalCulture.Pillars.Contains(PairA.Key) ? NatDataB->NationalCulture.Pillars[PairA.Key] : 0.0f;
                CulturalDistance += FMath::Abs(PairA.Value - ValB);
            }
            CulturalDistance /= 10.0f;

            float LangDivA = NatDataA->NationalCulture.LanguageDivergence;
            float LangDivB = NatDataB->NationalCulture.LanguageDivergence;
            float CommunicationBarrier = (LangDivA + LangDivB) * 0.5f;

            float FervorA = NatDataA->NationalCulture.ReligiousFervor;
            float FervorB = NatDataB->NationalCulture.ReligiousFervor;

            float TrustDelta = -(CommunicationBarrier * 0.05f) - (CulturalDistance * 0.02f);
            float TensionDelta = (CulturalDistance * 0.03f) + (CommunicationBarrier * 0.02f);

            bool bHolyWarCondition = false;
            if (CulturalDistance > 30.0f) {
                if (FervorA > 80.0f || FervorB > 80.0f) {
                    TensionDelta += 5.0f;
                    TrustDelta -= 5.0f;
                    bHolyWarCondition = true;
                }
                else if (FervorA > 50.0f || FervorB > 50.0f) {
                    TensionDelta += 2.0f;
                    TrustDelta -= 2.0f;
                }
            }

            float MilRatio = StateA.MilitaryPower / FMath::Max(1.0f, StateB.MilitaryPower);

            if (!bHolyWarCondition) {
                if (MilRatio > 2.0f || MilRatio < 0.5f) {
                    TensionDelta += FMath::FRandRange(0.5f, 2.0f);
                    TrustDelta -= FMath::FRandRange(0.1f, 1.0f);
                }
                else {
                    TrustDelta += FMath::FRandRange(0.5f, 1.5f);
                    TensionDelta -= FMath::FRandRange(0.1f, 1.0f);
                }
            }
            else {
                TensionDelta += FMath::FRandRange(2.0f, 4.0f);
            }

            if (CulturalDistance < 15.0f && !bHolyWarCondition) {
                TrustDelta += 2.0f;
                TensionDelta -= 1.5f;
            }

            Relation.Trust = FMath::Clamp(Relation.Trust + TrustDelta, 0.0f, 100.0f);
            Relation.Tension = FMath::Clamp(Relation.Tension + TensionDelta, 0.0f, 100.0f);

            EDiplomaticState OldState = Relation.State;

            if (Relation.Trust > 80.0f && Relation.Tension < 20.0f) {
                Relation.State = EDiplomaticState::Alliance;
            }
            else if (Relation.Trust > 50.0f && Relation.Tension < 40.0f) {
                Relation.State = EDiplomaticState::TradeAgreement;
            }
            else if (Relation.Tension > 85.0f) {
                Relation.State = EDiplomaticState::ActiveWar;

                if (OldState != EDiplomaticState::ActiveWar) {
                    Manager->Stat_WarsDeclared++;

                    if (Manager->HistoryModule) {
                        FVector2D EventLoc = FVector2D::ZeroVector;
                        if (NatDataA->MemberSettlementIDs.Num() > 0 && Manager->SettlementModule) {
                            for (const FSettlementData& S : Manager->SettlementModule->Settlements) {
                                if (S.SettlementID == NatDataA->MemberSettlementIDs[0]) {
                                    EventLoc = S.Position; break;
                                }
                            }
                        }

                        FString Cause = bHolyWarCondition ? TEXT("ideologických a náboženských dùvodù (Svatá Válka)") : TEXT("geopolitických a mocenských dùvodù");
                        FString Desc = FString::Printf(TEXT("Národ %d a Národ %d vyhlásily otevøenou válku z %s. (Kulturní propast: %.1f)"),
                            StateA.NationID, StateB.NationID, *Cause, CulturalDistance);

                        Manager->HistoryModule->LogEvent(Manager->CurrentYear, Manager->CurrentDay, TEXT("War Declared"), TEXT("Diplomacy"), Desc, EventLoc, StateA.NationID);
                    }
                }
            }
            else if (Relation.Tension > 60.0f) {
                Relation.State = EDiplomaticState::ColdWar;
            }
            else {
                Relation.State = EDiplomaticState::Neutral;
            }
        }
    }
}