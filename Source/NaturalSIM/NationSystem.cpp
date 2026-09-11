#include "NationSystem.h"
#include "SimWorldManager.h"

UNationSystem::UNationSystem() { PrimaryComponentTick.bCanEverTick = false; }

void UNationSystem::AggregateNationData(const TArray<FSettlementData>& Settlements)
{
    NationStates.Empty();

    for (int32 i = Nations.Num() - 1; i >= 0; i--) {
        bool bHasCity = false;
        for (const FSettlementData& City : Settlements) {
            if (City.NationID == Nations[i].NationID && City.Population > 0) {
                bHasCity = true; break;
            }
        }
        if (!bHasCity) {
            Nations.RemoveAt(i);
        }
    }

    for (const FNationData& Nation : Nations) {
        FNationStateDTO DTO;
        DTO.NationID = Nation.NationID;
        DTO.TotalPopulation = 0;
        DTO.TotalWealth = 0.0f;
        DTO.MilitaryPower = 0.0f;

        for (const FSettlementData& City : Settlements) {
            if (City.NationID == Nation.NationID) {
                DTO.TotalPopulation += City.Population;

                float CityWealth = City.Inventory.FloraFood + City.Inventory.MeatFood + City.Inventory.Wood + City.Inventory.Stone;
                DTO.TotalWealth += CityWealth;

                float WarfareLevel = City.Knowledge.Levels.Contains(EKnowledgeField::Warfare) ? City.Knowledge.Levels[EKnowledgeField::Warfare] : 0.0f;
                DTO.MilitaryPower += (City.Population * 0.5f) + (WarfareLevel * 2.0f);

                DTO.BorderSettlementIDs.Add(City.SettlementID);
            }
        }

        if (DTO.TotalPopulation > 0) {
            NationStates.Add(DTO);
        }
    }
}

bool UNationSystem::FormNation(FSettlementData& CityA, FSettlementData& CityB, ASimWorldManager* Manager) {

    if (CityA.NationID != -1 && CityA.NationID == CityB.NationID) return false;
    if (CityA.NationID != -1 && CityB.NationID != -1) return false;

    if (CityA.NationID == -1 && CityB.NationID == -1) {
        if (CityA.Population < 30 || CityB.Population < 30) return false;

        GlobalNationCounter++;
        int32 TargetNationID = GlobalNationCounter;

        FNationData NewNation;
        NewNation.NationID = TargetNationID;
        NewNation.NationName = "Imperium " + FString::FromInt(TargetNationID);

        NewNation.MemberSettlementIDs.Add(CityA.SettlementID);
        NewNation.MemberSettlementIDs.Add(CityB.SettlementID);

        NewNation.NationalCulture.PrimaryColor = FLinearColor::LerpUsingHSV(CityA.Color, CityB.Color, 0.5f);

        CityA.NationID = TargetNationID;
        CityB.NationID = TargetNationID;
        CityA.Color = NewNation.NationalCulture.PrimaryColor;
        CityB.Color = NewNation.NationalCulture.PrimaryColor;

        Nations.Add(NewNation);
        return true;
    }

    int32 ExistingNationID = (CityA.NationID != -1) ? CityA.NationID : CityB.NationID;
    FSettlementData& FreeCity = (CityA.NationID == -1) ? CityA : CityB;
    FSettlementData& StateCity = (CityA.NationID != -1) ? CityA : CityB;

    FreeCity.NationID = ExistingNationID;
    FreeCity.Color = StateCity.Color;

    for (FNationData& N : Nations) {
        if (N.NationID == ExistingNationID) {
            N.MemberSettlementIDs.Add(FreeCity.SettlementID);
            break;
        }
    }
    return true;
}

FCultureProfile UNationSystem::InheritCultureFromMother(const FCultureProfile& MotherCulture, FVector2D ChildPosition, ASimWorldManager* Manager) {
    FCultureProfile ChildCulture = MotherCulture;
    for (auto& Pair : ChildCulture.Pillars) {
        float Mutation = FMath::FRandRange(-3.0f, 3.0f);
        Pair.Value = FMath::Clamp(Pair.Value + Mutation, 0.0f, 100.0f);
    }
    ChildCulture.LanguageDivergence += FMath::FRandRange(0.5f, 2.0f);
    AdaptCultureToEnvironment(ChildCulture, ChildPosition, Manager, 1.0f);
    return ChildCulture;
}

void UNationSystem::AdaptCultureToEnvironment(FCultureProfile& Culture, FVector2D Position, ASimWorldManager* Manager, float DeltaDays) {
    if (!Manager) return;
    float CellSize = 50.0f; int32 ChunkWorldSize = (Manager->ChunkSize - 1) * CellSize;
    int32 CX = FMath::FloorToInt(Position.X / ChunkWorldSize); int32 CY = FMath::FloorToInt(Position.Y / ChunkWorldSize);
    FIntPoint ChunkCoord(CX, CY);

    // OPTIMALIZACE O(1) pøístupu pomocí Find a FIntPoint
    if (FChunkData* Chunk = Manager->WorldChunks.Find(ChunkCoord)) {
        int32 LX = FMath::Clamp(FMath::FloorToInt((Position.X - (CX * ChunkWorldSize)) / CellSize), 0, Manager->ChunkSize - 1);
        int32 LY = FMath::Clamp(FMath::FloorToInt((Position.Y - (CY * ChunkWorldSize)) / CellSize), 0, Manager->ChunkSize - 1);
        int32 Idx = LX + (LY * Manager->ChunkSize);

        if (Chunk->MicroCells.IsValidIndex(Idx)) {
            const FCellData& Cell = Chunk->MicroCells[Idx];
            float Rate = 0.05f * DeltaDays;

            if (Cell.SurfaceWater > 0.0f || Cell.Elevation <= Manager->SeaLevel + 50.0f) {
                Culture.Pillars[ECulturalPillar::Commerce] = FMath::Min(100.0f, Culture.Pillars[ECulturalPillar::Commerce] + Rate * 2.0f);
                Culture.Pillars[ECulturalPillar::Exploration] = FMath::Min(100.0f, Culture.Pillars[ECulturalPillar::Exploration] + Rate * 1.5f);
            }
            if (Cell.Elevation > Manager->SeaLevel + 800.0f || Cell.Bedrock == EBedrockType::Rock) {
                Culture.Pillars[ECulturalPillar::Industry] = FMath::Min(100.0f, Culture.Pillars[ECulturalPillar::Industry] + Rate * 2.0f);
                Culture.Pillars[ECulturalPillar::Militarism] = FMath::Min(100.0f, Culture.Pillars[ECulturalPillar::Militarism] + Rate * 1.0f);
            }
            if (Cell.FloraDensity > 0.6f) {
                Culture.Pillars[ECulturalPillar::Ecology] = FMath::Min(100.0f, Culture.Pillars[ECulturalPillar::Ecology] + Rate * 2.5f);
                Culture.Pillars[ECulturalPillar::Spirituality] = FMath::Min(100.0f, Culture.Pillars[ECulturalPillar::Spirituality] + Rate * 1.2f);
            }
        }
    }
}

FCultureProfile UNationSystem::MergeCultures(const FCultureProfile& CultureA, const FCultureProfile& CultureB) {
    FCultureProfile MergedCulture = CultureA;
    MergedCulture.CultureName = CultureA.CultureName + "-" + CultureB.CultureName;
    for (auto& Pair : MergedCulture.Pillars) {
        float ValueB = CultureB.Pillars.Contains(Pair.Key) ? CultureB.Pillars[Pair.Key] : 0.0f;
        Pair.Value = FMath::Max(Pair.Value, ValueB);
    }
    MergedCulture.LiteracyRate = FMath::Max(CultureA.LiteracyRate, CultureB.LiteracyRate);
    MergedCulture.PressFactor = FMath::Max(CultureA.PressFactor, CultureB.PressFactor);
    return MergedCulture;
}