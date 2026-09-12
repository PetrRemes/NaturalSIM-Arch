#include "TechnologySystem.h"
#include "SimWorldManager.h"
#include "HistorySystem.h"
#include "ManaSystem.h"

UTechnologySystem::UTechnologySystem()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UTechnologySystem::BeginPlay()
{
    Super::BeginPlay();
    RegisterDefaultDiscoveries();
    RegisterDefaultTechTree();
}

void UTechnologySystem::GainKnowledge(FKnowledgeContainer& Container, EKnowledgeField Field, float Amount, float LiteracyBonus, float PressBonus)
{
    float Multiplier = 1.0f + LiteracyBonus + PressBonus;
    float Earned = Amount * Multiplier;

    if (Container.Levels.Contains(Field)) {
        Container.Levels[Field] += Earned;
    }
    else {
        Container.Levels.Add(Field, Earned);
    }
}

void UTechnologySystem::EvaluateEntityKnowledge(FKnowledgeContainer& Knowledge, FCultureProfile& Culture, const FCellStaticData& SCell, const FCellDynamicData& DCell, ASimWorldManager* Manager, int32 EntityID, bool bIsSettlement, FVector2D Location)
{
    if (!Manager) return;

    for (const FDiscoveryDefinition& Disc : KnownDiscoveries)
    {
        if (Knowledge.DiscoveredConcepts.Contains(Disc.DiscoveryName)) continue;

        bool bCanDiscover = true;

        for (const auto& Req : Disc.RequiredKnowledge) {
            if (!Knowledge.Levels.Contains(Req.Key) || Knowledge.Levels[Req.Key] < Req.Value) {
                bCanDiscover = false; break;
            }
        }
        if (!bCanDiscover) continue;

        for (const auto& Req : Disc.RequiredSubKnowledge) {
            if (!Knowledge.SubKnowledge.Contains(Req.Key) || Knowledge.SubKnowledge[Req.Key] < Req.Value) {
                bCanDiscover = false; break;
            }
        }
        if (!bCanDiscover) continue;

        for (const FName& EnvTrig : Disc.EnvironmentalTriggers) {
            if (!CheckEnvironmentalTrigger(EnvTrig, SCell, DCell, Manager)) {
                bCanDiscover = false; break;
            }
        }
        if (!bCanDiscover) continue;

        for (const FName& ExpTrig : Disc.ExperienceTriggers) {
            if (!CheckExperienceTrigger(ExpTrig, Knowledge)) {
                bCanDiscover = false; break;
            }
        }
        if (!bCanDiscover) continue;

        Knowledge.DiscoveredConcepts.Add(Disc.DiscoveryName);

        if (Culture.Pillars.Contains(Disc.CulturalAffinity)) {
            Culture.Pillars[Disc.CulturalAffinity] = FMath::Min(100.0f, Culture.Pillars[Disc.CulturalAffinity] + 2.0f);
        }

        if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::Discovery);

        if (Manager->HistoryModule) {
            FString EntType = bIsSettlement ? "Settlement" : "Tribe";
            FString Desc = FString::Printf(TEXT("%s %d analyzed its environment and experiences to discover: %s."), *EntType, EntityID, *Disc.DiscoveryName.ToString());
            Manager->HistoryModule->LogEvent(Manager->CurrentYear, Manager->CurrentDay, "Discovery", Disc.DiscoveryName.ToString(), Desc, Location, EntityID);
        }
    }

    for (const FTechRequirement& Tech : KnownTechnologies)
    {
        if (Knowledge.UnlockedTechnologies.Contains(Tech.UnlockedTechnologyName)) continue;

        bool bCanUnlock = true;

        for (const auto& Req : Tech.RequiredKnowledge) {
            if (!Knowledge.Levels.Contains(Req.Key) || Knowledge.Levels[Req.Key] < Req.Value) { bCanUnlock = false; break; }
        }
        if (!bCanUnlock) continue;

        for (const auto& Req : Tech.RequiredSubKnowledge) {
            if (!Knowledge.SubKnowledge.Contains(Req.Key) || Knowledge.SubKnowledge[Req.Key] < Req.Value) { bCanUnlock = false; break; }
        }
        if (!bCanUnlock) continue;

        for (const FName& ReqDisc : Tech.RequiredDiscoveries) {
            if (!Knowledge.DiscoveredConcepts.Contains(ReqDisc)) { bCanUnlock = false; break; }
        }
        if (!bCanUnlock) continue;

        for (const FName& ReqTech : Tech.RequiredTechnologies) {
            if (!Knowledge.UnlockedTechnologies.Contains(ReqTech)) { bCanUnlock = false; break; }
        }
        if (!bCanUnlock) continue;

        Knowledge.UnlockedTechnologies.Add(Tech.UnlockedTechnologyName);

        if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::Technology);

        if (Manager->HistoryModule) {
            FString EntType = bIsSettlement ? "Settlement" : "Tribe";
            FString Desc = FString::Printf(TEXT("%s %d successfully adopted the technology of %s into everyday life."), *EntType, EntityID, *Tech.UnlockedTechnologyName.ToString());
            Manager->HistoryModule->LogEvent(Manager->CurrentYear, Manager->CurrentDay, "Technology Adopted", Tech.UnlockedTechnologyName.ToString(), Desc, Location, EntityID);
        }

        if (!Manager->bIndustrialEraReached && Tech.UnlockedTechnologyName == "Industrial Mass Production") {
            Manager->bIndustrialEraReached = true;
            Manager->CurrentYear = 0;
            Manager->CurrentDay = 1;

            if (Manager->HistoryModule) {
                Manager->HistoryModule->LogEvent(0, 1.0, TEXT("New Era"), TEXT("Industrial Revolution"), TEXT("Civilizace objevila prùmyslovou masovou výrobu! Éra vìdy zapoèala a letopoèet byl resetován na Rok 0 Nového Vìku."), Location, EntityID);
            }
        }
    }
}

bool UTechnologySystem::CheckEnvironmentalTrigger(FName Trigger, const FCellStaticData& SCell, const FCellDynamicData& DCell, ASimWorldManager* Manager)
{
    if (Trigger == "River") return (DCell.SurfaceWater > 0.1f || DCell.RiverDischarge > 0.5f);
    if (Trigger == "Forest") return (SCell.Biome == EBiomeType::DeciduousForest || SCell.Biome == EBiomeType::ConiferousForest || SCell.Biome == EBiomeType::TropicalForest);
    if (Trigger == "Coast") return (SCell.Biome == EBiomeType::Beach || SCell.WaterType == EWaterType::Ocean);
    if (Trigger == "Mountain") return (SCell.Elevation > Manager->SeaLevel + 800.0f || SCell.Bedrock == EBedrockType::Rock);
    if (Trigger == "Fertile") return (DCell.FloraDensity > 0.4f);
    if (Trigger == "HarshClimate") return (DCell.Temperature < 5.0f || DCell.Temperature > 35.0f);
    if (Trigger == "ClaySource") return (SCell.ClayAmount > 10.0f);
    if (Trigger == "Desert") return (SCell.Biome == EBiomeType::Desert);
    if (Trigger == "DeepMountain") return (SCell.Elevation > Manager->SeaLevel + 1500.0f);

    return true;
}

bool UTechnologySystem::CheckExperienceTrigger(FName Trigger, const FKnowledgeContainer& Knowledge)
{
    return Knowledge.SubKnowledge.Contains(Trigger) && Knowledge.SubKnowledge[Trigger] > 0.0f;
}

void UTechnologySystem::RegisterDefaultDiscoveries()
{
    KnownDiscoveries.Empty();

    FDiscoveryDefinition D1; D1.DiscoveryName = "Controlled Fire"; D1.RequiredKnowledge.Add(EKnowledgeField::Woodcraft, 1.0f); D1.ExperienceTriggers.Add("Woodcutting"); D1.CulturalAffinity = ECulturalPillar::Science; KnownDiscoveries.Add(D1);
    FDiscoveryDefinition D2; D2.DiscoveryName = "Basic Shelter"; D2.ExperienceTriggers.Add("Survival"); D2.CulturalAffinity = ECulturalPillar::Industry; KnownDiscoveries.Add(D2);
    FDiscoveryDefinition D3; D3.DiscoveryName = "Plant Cultivation"; D3.RequiredKnowledge.Add(EKnowledgeField::Agriculture, 2.0f); D3.EnvironmentalTriggers.Add("Fertile"); D3.ExperienceTriggers.Add("Foraging"); D3.CulturalAffinity = ECulturalPillar::Ecology; KnownDiscoveries.Add(D3);
    FDiscoveryDefinition D4; D4.DiscoveryName = "Seed Selection"; D4.RequiredSubKnowledge.Add("PlantCultivation", 15.0f); D4.CulturalAffinity = ECulturalPillar::Science; KnownDiscoveries.Add(D4);
    FDiscoveryDefinition D5; D5.DiscoveryName = "Animal Behavior"; D5.RequiredSubKnowledge.Add("Hunting", 10.0f); D5.ExperienceTriggers.Add("AnimalTracking"); D5.CulturalAffinity = ECulturalPillar::Ecology; KnownDiscoveries.Add(D5);
    FDiscoveryDefinition D6; D6.DiscoveryName = "River Observation"; D6.EnvironmentalTriggers.Add("River"); D6.RequiredSubKnowledge.Add("RiverObservation", 5.0f); D6.CulturalAffinity = ECulturalPillar::Exploration; KnownDiscoveries.Add(D6);
    FDiscoveryDefinition D7; D7.DiscoveryName = "Stone Cutting"; D7.EnvironmentalTriggers.Add("Mountain"); D7.RequiredKnowledge.Add(EKnowledgeField::Masonry, 1.0f); D7.CulturalAffinity = ECulturalPillar::Industry; KnownDiscoveries.Add(D7);
    FDiscoveryDefinition D8; D8.DiscoveryName = "Floatability"; D8.EnvironmentalTriggers.Add("River"); D8.ExperienceTriggers.Add("Woodcutting"); D8.CulturalAffinity = ECulturalPillar::Exploration; KnownDiscoveries.Add(D8);
    FDiscoveryDefinition D9; D9.DiscoveryName = "Clay Properties"; D9.EnvironmentalTriggers.Add("ClaySource"); D9.RequiredKnowledge.Add(EKnowledgeField::Masonry, 2.0f); D9.CulturalAffinity = ECulturalPillar::Science; KnownDiscoveries.Add(D9);
    FDiscoveryDefinition D10; D10.DiscoveryName = "Food Preservation"; D10.ExperienceTriggers.Add("Survival"); D10.RequiredSubKnowledge.Add("Foraging", 20.0f); D10.CulturalAffinity = ECulturalPillar::Sociability; KnownDiscoveries.Add(D10);
    FDiscoveryDefinition D11; D11.DiscoveryName = "Social Hierarchy"; D11.RequiredKnowledge.Add(EKnowledgeField::Sociology, 5.0f); D11.CulturalAffinity = ECulturalPillar::Expansion; KnownDiscoveries.Add(D11);
    FDiscoveryDefinition D12; D12.DiscoveryName = "Wood Shaping"; D12.EnvironmentalTriggers.Add("Forest"); D12.RequiredSubKnowledge.Add("Woodcutting", 15.0f); D12.CulturalAffinity = ECulturalPillar::Industry; KnownDiscoveries.Add(D12);
    FDiscoveryDefinition D13; D13.DiscoveryName = "Ore Properties"; D13.EnvironmentalTriggers.Add("Mountain"); D13.RequiredKnowledge.Add(EKnowledgeField::Metallurgy, 5.0f); D13.CulturalAffinity = ECulturalPillar::Science; KnownDiscoveries.Add(D13);
    FDiscoveryDefinition D14; D14.DiscoveryName = "Market Dynamics"; D14.RequiredKnowledge.Add(EKnowledgeField::Sociology, 15.0f); D14.CulturalAffinity = ECulturalPillar::Commerce; KnownDiscoveries.Add(D14);

    FDiscoveryDefinition D_Animism; D_Animism.DiscoveryName = "Animism"; D_Animism.ExperienceTriggers.Add("Survival"); D_Animism.CulturalAffinity = ECulturalPillar::Spirituality; KnownDiscoveries.Add(D_Animism);
    FDiscoveryDefinition D_Pantheon; D_Pantheon.DiscoveryName = "Pantheon"; D_Pantheon.RequiredKnowledge.Add(EKnowledgeField::Sociology, 10.0f); D_Pantheon.RequiredDiscoveries.Add("Animism"); D_Pantheon.CulturalAffinity = ECulturalPillar::Spirituality; KnownDiscoveries.Add(D_Pantheon);
    FDiscoveryDefinition D_Philosophy; D_Philosophy.DiscoveryName = "Philosophy"; D_Philosophy.RequiredKnowledge.Add(EKnowledgeField::Academics, 10.0f); D_Philosophy.RequiredDiscoveries.Add("Social Hierarchy"); D_Philosophy.CulturalAffinity = ECulturalPillar::Science; KnownDiscoveries.Add(D_Philosophy);

    FDiscoveryDefinition D15; D15.DiscoveryName = "Defensive Architecture"; D15.RequiredKnowledge.Add(EKnowledgeField::Masonry, 15.0f); D15.RequiredKnowledge.Add(EKnowledgeField::Warfare, 10.0f); D15.CulturalAffinity = ECulturalPillar::Militarism; KnownDiscoveries.Add(D15);
    FDiscoveryDefinition D16; D16.DiscoveryName = "Deep Earth Minerals"; D16.EnvironmentalTriggers.Add("Mountain"); D16.RequiredKnowledge.Add(EKnowledgeField::Metallurgy, 15.0f); D16.CulturalAffinity = ECulturalPillar::Industry; KnownDiscoveries.Add(D16);
    FDiscoveryDefinition D17; D17.DiscoveryName = "Tides and Currents"; D17.EnvironmentalTriggers.Add("Coast"); D17.RequiredKnowledge.Add(EKnowledgeField::Maritime, 15.0f); D17.CulturalAffinity = ECulturalPillar::Exploration; KnownDiscoveries.Add(D17);

    FDiscoveryDefinition D18; D18.DiscoveryName = "Thermodynamics"; D18.RequiredKnowledge.Add(EKnowledgeField::Engineering, 15.0f); D18.RequiredKnowledge.Add(EKnowledgeField::Physics, 10.0f); D18.CulturalAffinity = ECulturalPillar::Science; KnownDiscoveries.Add(D18);
    FDiscoveryDefinition D19; D19.DiscoveryName = "Fossil Fuels"; D19.EnvironmentalTriggers.Add("Desert"); D19.RequiredKnowledge.Add(EKnowledgeField::Chemistry, 10.0f); D19.CulturalAffinity = ECulturalPillar::Industry; KnownDiscoveries.Add(D19);
    FDiscoveryDefinition D20; D20.DiscoveryName = "Electromagnetism"; D20.RequiredKnowledge.Add(EKnowledgeField::Physics, 25.0f); D20.CulturalAffinity = ECulturalPillar::Science; KnownDiscoveries.Add(D20);
    FDiscoveryDefinition D21; D21.DiscoveryName = "Hydrocarbons"; D21.EnvironmentalTriggers.Add("Desert"); D21.RequiredKnowledge.Add(EKnowledgeField::Chemistry, 20.0f); D21.CulturalAffinity = ECulturalPillar::Commerce; KnownDiscoveries.Add(D21);

    FDiscoveryDefinition D22; D22.DiscoveryName = "Aerodynamics"; D22.RequiredKnowledge.Add(EKnowledgeField::Physics, 35.0f); D22.RequiredKnowledge.Add(EKnowledgeField::Aerospace, 10.0f); D22.CulturalAffinity = ECulturalPillar::Exploration; KnownDiscoveries.Add(D22);
    FDiscoveryDefinition D23; D23.DiscoveryName = "Atomic Structure"; D23.RequiredKnowledge.Add(EKnowledgeField::Physics, 50.0f); D23.RequiredKnowledge.Add(EKnowledgeField::Chemistry, 40.0f); D23.CulturalAffinity = ECulturalPillar::Science; KnownDiscoveries.Add(D23);
    FDiscoveryDefinition D24; D24.DiscoveryName = "Logic Gates"; D24.RequiredKnowledge.Add(EKnowledgeField::Engineering, 40.0f); D24.RequiredKnowledge.Add(EKnowledgeField::Computing, 10.0f); D24.CulturalAffinity = ECulturalPillar::Science; KnownDiscoveries.Add(D24);

    FDiscoveryDefinition D25; D25.DiscoveryName = "Orbital Mechanics"; D25.RequiredKnowledge.Add(EKnowledgeField::Aerospace, 40.0f); D25.RequiredKnowledge.Add(EKnowledgeField::Physics, 60.0f); D25.CulturalAffinity = ECulturalPillar::Exploration; KnownDiscoveries.Add(D25);
    FDiscoveryDefinition D26; D26.DiscoveryName = "Ecosystem Dynamics"; D26.RequiredKnowledge.Add(EKnowledgeField::EnvironmentalScience, 30.0f); D26.RequiredKnowledge.Add(EKnowledgeField::Agriculture, 50.0f); D26.CulturalAffinity = ECulturalPillar::Ecology; KnownDiscoveries.Add(D26);
    FDiscoveryDefinition D27; D27.DiscoveryName = "Neural Networks"; D27.RequiredKnowledge.Add(EKnowledgeField::Computing, 60.0f); D27.CulturalAffinity = ECulturalPillar::Science; KnownDiscoveries.Add(D27);
    FDiscoveryDefinition D28; D28.DiscoveryName = "Plasma Containment"; D28.RequiredKnowledge.Add(EKnowledgeField::Physics, 80.0f); D28.RequiredKnowledge.Add(EKnowledgeField::Engineering, 70.0f); D28.CulturalAffinity = ECulturalPillar::Science; KnownDiscoveries.Add(D28);
}

void UTechnologySystem::RegisterDefaultTechTree()
{
    KnownTechnologies.Empty();

    FTechRequirement T1; T1.UnlockedTechnologyName = "Campfire"; T1.RequiredDiscoveries.Add("Controlled Fire"); KnownTechnologies.Add(T1);
    FTechRequirement T2; T2.UnlockedTechnologyName = "Primitive Huts"; T2.RequiredDiscoveries.Add("Basic Shelter"); T2.RequiredSubKnowledge.Add("Woodcutting", 5.0f); KnownTechnologies.Add(T2);
    FTechRequirement T3; T3.UnlockedTechnologyName = "Selective Farming"; T3.RequiredDiscoveries.Add("Seed Selection"); T3.RequiredDiscoveries.Add("Plant Cultivation"); T3.RequiredKnowledge.Add(EKnowledgeField::Agriculture, 5.0f); KnownTechnologies.Add(T3);
    FTechRequirement T4; T4.UnlockedTechnologyName = "Animal Domestication"; T4.RequiredDiscoveries.Add("Animal Behavior"); T4.RequiredKnowledge.Add(EKnowledgeField::Agriculture, 5.0f); KnownTechnologies.Add(T4);
    FTechRequirement T5; T5.UnlockedTechnologyName = "Carpentry"; T5.RequiredDiscoveries.Add("Wood Shaping"); T5.RequiredTechnologies.Add("Campfire"); T5.RequiredKnowledge.Add(EKnowledgeField::Woodcraft, 5.0f); KnownTechnologies.Add(T5);
    FTechRequirement T6; T6.UnlockedTechnologyName = "Basic Rafts"; T6.RequiredDiscoveries.Add("Floatability"); T6.RequiredTechnologies.Add("Carpentry"); T6.RequiredKnowledge.Add(EKnowledgeField::Maritime, 2.0f); KnownTechnologies.Add(T6);
    FTechRequirement T7; T7.UnlockedTechnologyName = "Pottery"; T7.RequiredDiscoveries.Add("Clay Properties"); T7.RequiredTechnologies.Add("Campfire"); T7.RequiredKnowledge.Add(EKnowledgeField::Masonry, 5.0f); KnownTechnologies.Add(T7);
    FTechRequirement T8; T8.UnlockedTechnologyName = "Irrigation"; T8.RequiredDiscoveries.Add("River Observation"); T8.RequiredTechnologies.Add("Selective Farming"); T8.RequiredKnowledge.Add(EKnowledgeField::Agriculture, 10.0f); KnownTechnologies.Add(T8);
    FTechRequirement T9; T9.UnlockedTechnologyName = "Stoneworking"; T9.RequiredDiscoveries.Add("Stone Cutting"); T9.RequiredKnowledge.Add(EKnowledgeField::Masonry, 8.0f); KnownTechnologies.Add(T9);

    FTechRequirement T_Monuments; T_Monuments.UnlockedTechnologyName = "Monuments"; T_Monuments.RequiredDiscoveries.Add("Pantheon"); T_Monuments.RequiredTechnologies.Add("Stoneworking"); KnownTechnologies.Add(T_Monuments);

    FTechRequirement T10; T10.UnlockedTechnologyName = "Food Storage"; T10.RequiredDiscoveries.Add("Food Preservation"); T10.RequiredTechnologies.Add("Pottery"); KnownTechnologies.Add(T10);
    FTechRequirement T11; T11.UnlockedTechnologyName = "Clan Organization"; T11.RequiredDiscoveries.Add("Social Hierarchy"); T11.RequiredKnowledge.Add(EKnowledgeField::Sociology, 10.0f); KnownTechnologies.Add(T11);
    FTechRequirement T12; T12.UnlockedTechnologyName = "Smelting"; T12.RequiredDiscoveries.Add("Ore Properties"); T12.RequiredTechnologies.Add("Campfire"); T12.RequiredKnowledge.Add(EKnowledgeField::Metallurgy, 10.0f); KnownTechnologies.Add(T12);
    FTechRequirement T13; T13.UnlockedTechnologyName = "Currency"; T13.RequiredDiscoveries.Add("Market Dynamics"); T13.RequiredTechnologies.Add("Clan Organization"); T13.RequiredKnowledge.Add(EKnowledgeField::Sociology, 20.0f); KnownTechnologies.Add(T13);

    FTechRequirement T14; T14.UnlockedTechnologyName = "Fortifications"; T14.RequiredDiscoveries.Add("Defensive Architecture"); T14.RequiredTechnologies.Add("Stoneworking"); KnownTechnologies.Add(T14);
    FTechRequirement T15; T15.UnlockedTechnologyName = "Steel Forging"; T15.RequiredDiscoveries.Add("Deep Earth Minerals"); T15.RequiredTechnologies.Add("Smelting"); KnownTechnologies.Add(T15);
    FTechRequirement T16; T16.UnlockedTechnologyName = "Naval Engineering"; T16.RequiredDiscoveries.Add("Tides and Currents"); T16.RequiredTechnologies.Add("Basic Rafts"); T16.RequiredKnowledge.Add(EKnowledgeField::Engineering, 10.0f); KnownTechnologies.Add(T16);
    FTechRequirement T17; T17.UnlockedTechnologyName = "Guilds & Logistics"; T17.RequiredTechnologies.Add("Currency"); T17.RequiredKnowledge.Add(EKnowledgeField::Sociology, 30.0f); KnownTechnologies.Add(T17);
    FTechRequirement T18; T18.UnlockedTechnologyName = "State Borders"; T18.RequiredTechnologies.Add("Guilds & Logistics"); T18.RequiredTechnologies.Add("Fortifications"); KnownTechnologies.Add(T18);

    FTechRequirement T19; T19.UnlockedTechnologyName = "Steam Engine"; T19.RequiredDiscoveries.Add("Thermodynamics"); T19.RequiredDiscoveries.Add("Fossil Fuels"); T19.RequiredTechnologies.Add("Steel Forging"); KnownTechnologies.Add(T19);
    FTechRequirement T20; T20.UnlockedTechnologyName = "Industrial Mass Production"; T20.RequiredTechnologies.Add("Steam Engine"); T20.RequiredKnowledge.Add(EKnowledgeField::Engineering, 30.0f); KnownTechnologies.Add(T20);
    FTechRequirement T21; T21.UnlockedTechnologyName = "Combustion Engine"; T21.RequiredDiscoveries.Add("Hydrocarbons"); T21.RequiredTechnologies.Add("Steam Engine"); T21.RequiredKnowledge.Add(EKnowledgeField::Chemistry, 25.0f); KnownTechnologies.Add(T21);

    FTechRequirement T22; T22.UnlockedTechnologyName = "Electrification"; T22.RequiredDiscoveries.Add("Electromagnetism"); T22.RequiredTechnologies.Add("Industrial Mass Production"); KnownTechnologies.Add(T22);
    FTechRequirement T23; T23.UnlockedTechnologyName = "Aviation"; T23.RequiredDiscoveries.Add("Aerodynamics"); T23.RequiredTechnologies.Add("Combustion Engine"); KnownTechnologies.Add(T23);
    FTechRequirement T24; T24.UnlockedTechnologyName = "Nuclear Fission"; T24.RequiredDiscoveries.Add("Atomic Structure"); T24.RequiredTechnologies.Add("Electrification"); KnownTechnologies.Add(T24);
    FTechRequirement T25; T25.UnlockedTechnologyName = "Early Computing"; T25.RequiredDiscoveries.Add("Logic Gates"); T25.RequiredTechnologies.Add("Electrification"); KnownTechnologies.Add(T25);

    FTechRequirement T26; T26.UnlockedTechnologyName = "Space Exploration"; T26.RequiredDiscoveries.Add("Orbital Mechanics"); T26.RequiredTechnologies.Add("Aviation"); T26.RequiredTechnologies.Add("Early Computing"); KnownTechnologies.Add(T26);
    FTechRequirement T27; T27.UnlockedTechnologyName = "Sustainable Infrastructure"; T27.RequiredDiscoveries.Add("Ecosystem Dynamics"); T27.RequiredTechnologies.Add("Electrification"); KnownTechnologies.Add(T27);
    FTechRequirement T28; T28.UnlockedTechnologyName = "Artificial Intelligence"; T28.RequiredDiscoveries.Add("Neural Networks"); T28.RequiredTechnologies.Add("Early Computing"); KnownTechnologies.Add(T28);
    FTechRequirement T29; T29.UnlockedTechnologyName = "Nuclear Fusion"; T29.RequiredDiscoveries.Add("Plasma Containment"); T29.RequiredTechnologies.Add("Nuclear Fission"); KnownTechnologies.Add(T29);
}