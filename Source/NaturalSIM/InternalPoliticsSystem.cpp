#include "InternalPoliticsSystem.h"
#include "SimWorldManager.h"
#include "TechnologySystem.h" 
#include "HistorySystem.h"
#include "ManaSystem.h"

UInternalPoliticsSystem::UInternalPoliticsSystem() { PrimaryComponentTick.bCanEverTick = false; }
void UInternalPoliticsSystem::BeginPlay() { Super::BeginPlay(); }

static int64 GetProvinceKey(int32 ID_A, int32 ID_B) {
    int64 MinID = FMath::Min(ID_A, ID_B);
    int64 MaxID = FMath::Max(ID_A, ID_B);
    return (MinID << 32) | (MaxID & 0xFFFFFFFF);
}

void UInternalPoliticsSystem::ProcessInternalRelations(TArray<FSettlementData>& Settlements, ASimWorldManager* Manager)
{
    if (!Manager || Settlements.Num() < 2) return;

    for (int32 i = 0; i < Settlements.Num(); i++) {
        for (int32 j = i + 1; j < Settlements.Num(); j++) {
            FSettlementData& CityA = Settlements[i];
            FSettlementData& CityB = Settlements[j];

            if (CityA.NationID != -1 && CityA.NationID == CityB.NationID) {
                float Dist = FVector2D::Distance(CityA.Position, CityB.Position);

                if (Dist < 8000.0f) {
                    float FoodA = CityA.Inventory.FloraFood + CityA.Inventory.MeatFood;
                    float FoodB = CityB.Inventory.FloraFood + CityB.Inventory.MeatFood;

                    if (FoodA < CityA.Population && FoodB > CityB.Population * 2.0f) {
                        float HelpAmount = CityB.Population * 0.5f;
                        CityB.Inventory.FloraFood -= HelpAmount;
                        CityA.Inventory.FloraFood += HelpAmount;

                        if (Manager->TechModule) Manager->TechModule->GainKnowledge(CityB.Knowledge, EKnowledgeField::Sociology, 2.0f);
                    }
                    else if (FoodB < CityB.Population && FoodA > CityA.Population * 2.0f) {
                        float HelpAmount = CityA.Population * 0.5f;
                        CityA.Inventory.FloraFood -= HelpAmount;
                        CityB.Inventory.FloraFood += HelpAmount;

                        if (Manager->TechModule) Manager->TechModule->GainKnowledge(CityA.Knowledge, EKnowledgeField::Sociology, 2.0f);
                    }
                }
            }
        }
    }

    TMap<int32, int32> NationCapitals;
    TMap<int32, int32> NationMaxPop;

    for (int32 i = 0; i < Settlements.Num(); i++) {
        int32 NID = Settlements[i].NationID;
        if (NID != -1) {
            int32 CurrentMax = NationMaxPop.Contains(NID) ? NationMaxPop[NID] : -1;
            if (Settlements[i].Population > CurrentMax) {
                NationMaxPop.Add(NID, Settlements[i].Population);
                NationCapitals.Add(NID, i);
            }
        }
    }

    for (int32 i = 0; i < Settlements.Num(); i++) {
        int32 NID = Settlements[i].NationID;

        if (NID != -1 && NationCapitals.Contains(NID) && NationCapitals[NID] != i) {
            FSettlementData& Province = Settlements[i];
            FSettlementData& Capital = Settlements[NationCapitals[NID]];

            int64 Key = GetProvinceKey(Province.SettlementID, Capital.SettlementID);
            FInternalProvinceLink& Link = InternalLinks.FindOrAdd(Key);

            if (Link.CityA_ID == -1) {
                Link.CityA_ID = Province.SettlementID;
                Link.CityB_ID = Capital.SettlementID;
                Link.Cohesion = 100.0f;
                Link.InfrastructureLevel = 1.0f;
            }

            float Dist = FVector2D::Distance(Province.Position, Capital.Position);
            float DistPenalty = (Dist / 10000.0f) * 1.5f;

            float ProvFood = Province.Inventory.FloraFood + Province.Inventory.MeatFood;
            float FoodPenalty = (ProvFood < Province.Population) ? 3.0f : -0.5f;

            float ProvWealth = FMath::Max(1.0f, Province.Inventory.Wealth);
            float CapWealth = Capital.Inventory.Wealth;
            float WealthTension = 0.0f;
            if (CapWealth > ProvWealth * 3.0f) {
                WealthTension = 1.5f;
            }

            Link.Cohesion -= (DistPenalty + FoodPenalty + WealthTension);
            Link.Cohesion = FMath::Clamp(Link.Cohesion, 0.0f, 100.0f);

            if (Link.Cohesion <= 0.0f) {
                Province.NationID = -1;
                Province.Color = Province.Culture.PrimaryColor;

                for (FIntPoint Coord : Province.ClaimedCells) {
                    FCellStaticData* SCell = nullptr; FCellDynamicData* DCell = nullptr; FIntPoint CC;
                    if (Manager->GetMutableCellGlobal(Coord.X, Coord.Y, SCell, DCell, CC)) {
                        SCell->OwnerNationID = -1;
                        SCell->PoliticalColor = Province.Color;
                        Manager->RegisterVisualChange(CC, EChunkVisualDirty::Terrain);
                    }
                }

                if (Manager->HistoryModule) {
                    FString Desc = FString::Printf(TEXT("Provincie (Osada %d) se vzbouøila proti útlaku hlavního mìsta a vyhlásila naprostou nezávislost na Národu %d!"), Province.SettlementID, NID);
                    Manager->HistoryModule->LogEvent(Manager->CurrentYear, Manager->CurrentDay, TEXT("Rebellion"), TEXT("Separatism"), Desc, Province.Position, Province.SettlementID);
                }
                if (Manager->ManaModule) {
                    Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::Conflict);
                }

                Link.Cohesion = 50.0f;
            }
        }
    }
}