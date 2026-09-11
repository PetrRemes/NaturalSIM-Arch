

#include "RelationSystem.h"
#include "SimWorldManager.h"
#include "SettlementSystem.h"
#include "NationSystem.h"
#include "TechnologySystem.h"
#include "TransportSystem.h" 
#include "DiplomacySystem.h"
#include "HistorySystem.h"
#include "ManaSystem.h"

URelationSystem::URelationSystem() { PrimaryComponentTick.bCanEverTick = false; }
void URelationSystem::BeginPlay() { Super::BeginPlay(); }

static void ExecuteMarketSwap(FTribeInventory& InvA, FTribeInventory& InvB, float PopA, float PopB) {
    auto Trade = [](float& ResA, float& ResB, float NeedA, float NeedB, float BasePrice, float& WealthA, float& WealthB) {
        float SatA = ResA / FMath::Max(1.0f, NeedA);
        float SatB = ResB / FMath::Max(1.0f, NeedB);

        if (FMath::Abs(SatA - SatB) > 0.2f) {
            bool ASells = SatA > SatB;
            float& SellerRes = ASells ? ResA : ResB;
            float& BuyerRes = ASells ? ResB : ResA;
            float& SellerWealth = ASells ? WealthA : WealthB;
            float& BuyerWealth = ASells ? WealthB : WealthA;
            float SellerSat = ASells ? SatA : SatB;
            float BuyerSat = ASells ? SatB : SatA;

            float TargetSat = (SatA + SatB) / 2.0f;
            float TradeAmount = (SellerSat - TargetSat) * (ASells ? NeedA : NeedB);

            float PriceMultiplier = 1.0f + FMath::Clamp(1.0f - BuyerSat, 0.0f, 4.0f);
            float TotalCost = TradeAmount * BasePrice * PriceMultiplier;

            if (BuyerWealth < TotalCost && TotalCost > 0.0f) {
                TradeAmount *= (BuyerWealth / TotalCost);
                TotalCost = BuyerWealth;
            }

            if (TradeAmount > 0.0f) {
                SellerRes -= TradeAmount;
                BuyerRes += TradeAmount;
                BuyerWealth -= TotalCost;
                SellerWealth += TotalCost;
            }
        }
        };

    float& WA = InvA.Wealth;
    float& WB = InvB.Wealth;

    Trade(InvA.Weapons, InvB.Weapons, PopA * 1.0f, PopB * 1.0f, 6.0f, WA, WB);
    Trade(InvA.Tools, InvB.Tools, PopA * 1.0f, PopB * 1.0f, 5.0f, WA, WB);
    Trade(InvA.IronOre, InvB.IronOre, PopA * 2.0f, PopB * 2.0f, 2.0f, WA, WB);
    Trade(InvA.Stone, InvB.Stone, PopA * 5.0f, PopB * 5.0f, 0.8f, WA, WB);

    Trade(InvA.FloraFood, InvB.FloraFood, PopA * 10.0f, PopB * 10.0f, 1.0f, WA, WB);
    Trade(InvA.MeatFood, InvB.MeatFood, PopA * 10.0f, PopB * 10.0f, 1.5f, WA, WB);
    Trade(InvA.Wood, InvB.Wood, PopA * 5.0f, PopB * 5.0f, 0.5f, WA, WB);
    Trade(InvA.Oil, InvB.Oil, PopA * 1.0f, PopB * 1.0f, 10.0f, WA, WB);
    Trade(InvA.Uranium, InvB.Uranium, PopA * 0.5f, PopB * 0.5f, 50.0f, WA, WB);
}

void URelationSystem::ProcessTribeInteractions(TArray<FTribeData>& Tribes, ASimWorldManager* Manager)
{
    if (!Manager || Tribes.Num() < 2) return;
    float GridCellSize = 1000.0f;
    TribeSpatialGrid.Reset();

    for (int32 i = 0; i < Tribes.Num(); i++) {
        if (Tribes[i].Population <= 0) continue;
        FIntPoint GridCoord(
            FMath::FloorToInt(Tribes[i].Position.X / GridCellSize),
            FMath::FloorToInt(Tribes[i].Position.Y / GridCellSize)
        );
        TribeSpatialGrid.FindOrAdd(GridCoord).Add(i);
    }

    for (int32 i = Tribes.Num() - 1; i >= 0; i--) {
        if (Tribes[i].Population <= 0 || Tribes[i].InteractionCooldownDays > 0) continue;
        FIntPoint GridCoord(
            FMath::FloorToInt(Tribes[i].Position.X / GridCellSize),
            FMath::FloorToInt(Tribes[i].Position.Y / GridCellSize)
        );
        bool bTribeDestroyed = false;

        for (int32 dx = -1; dx <= 1 && !bTribeDestroyed; dx++) {
            for (int32 dy = -1; dy <= 1 && !bTribeDestroyed; dy++) {
                FIntPoint CheckCoord = GridCoord + FIntPoint(dx, dy);

                if (TArray<int32>* CellTribes = TribeSpatialGrid.Find(CheckCoord)) {
                    for (int32 j : *CellTribes) {
                        if (j >= i || Tribes[j].Population <= 0 || Tribes[j].InteractionCooldownDays > 0) continue;
                        if (Tribes[i].Population <= 0) { bTribeDestroyed = true; break; }
                        if (FMath::FRand() > 0.25f) continue;

                        float Dist = FVector2D::Distance(Tribes[i].Position, Tribes[j].Position);
                        if (Dist < 500.0f) {

                            if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::Encounter);

                            if (Manager->TechModule) {
                                Manager->TechModule->GainKnowledge(Tribes[i].Knowledge, EKnowledgeField::Sociology, 2.0f);
                                Manager->TechModule->GainKnowledge(Tribes[j].Knowledge, EKnowledgeField::Sociology, 2.0f);
                            }

                            EBiomeType EncounterBiome = EBiomeType::Grassland;
                            FCellData Cell;
                            if (Manager->GetCellDataAtLocation(FVector(Tribes[i].Position.X, Tribes[i].Position.Y, 0.0f), Cell)) {
                                EncounterBiome = Cell.Biome;
                            }

                            float W_Combat = 30.0f, W_Trade = 30.0f, W_Merge = 30.0f;
                            if (EncounterBiome == EBiomeType::Grassland || EncounterBiome == EBiomeType::Swamp || EncounterBiome == EBiomeType::Beach) W_Merge += 150.0f;
                            else if (EncounterBiome == EBiomeType::DeciduousForest || EncounterBiome == EBiomeType::ConiferousForest || EncounterBiome == EBiomeType::TropicalForest) W_Trade += 70.0f;
                            else W_Combat += 70.0f;

                            float TotalW = W_Combat + W_Trade + W_Merge;
                            float Roll = FMath::FRandRange(0.0f, TotalW);

                            if (Roll < W_Combat) {
                                Manager->Stat_Battles++;
                                if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::Conflict);

                                FTribeData* Winner = (Tribes[i].Population >= Tribes[j].Population) ? &Tribes[i] : &Tribes[j];
                                FTribeData* Loser = (Tribes[i].Population < Tribes[j].Population) ? &Tribes[i] : &Tribes[j];

                                Winner->Inventory.IronOre += Loser->Inventory.IronOre * 0.3f; Loser->Inventory.IronOre *= 0.7f;
                                Winner->Inventory.Tools += Loser->Inventory.Tools * 0.3f; Loser->Inventory.Tools *= 0.7f;
                                Winner->Inventory.Weapons += Loser->Inventory.Weapons * 0.3f; Loser->Inventory.Weapons *= 0.7f;
                                Winner->Inventory.Wealth += Loser->Inventory.Wealth * 0.3f; Loser->Inventory.Wealth *= 0.7f;
                                Winner->Inventory.Oil += Loser->Inventory.Oil * 0.3f; Loser->Inventory.Oil *= 0.7f;
                                Winner->Inventory.Uranium += Loser->Inventory.Uranium * 0.3f; Loser->Inventory.Uranium *= 0.7f;

                                Winner->Population = FMath::Max(1, FMath::RoundToInt(Winner->Population * 0.8f));
                                Loser->Population = FMath::Max(0, FMath::RoundToInt(Loser->Population * 0.7f));
                                if (Manager->TechModule) {
                                    Manager->TechModule->GainKnowledge(Winner->Knowledge, EKnowledgeField::Warfare, 10.0f);
                                    Manager->TechModule->GainKnowledge(Loser->Knowledge, EKnowledgeField::Warfare, 15.0f);
                                }
                                Winner->State = ETribeState::Camping;
                                Winner->CampDaysRemaining = 90;

                                if (Loser->Population > 0) {
                                    Loser->State = ETribeState::Migrating;
                                    FVector2D FleeDir = (Loser->Position - Winner->Position).GetSafeNormal();
                                    if (FleeDir.IsNearlyZero()) FleeDir = FVector2D(1.0f, 0.0f);
                                    Loser->TargetRegion = Loser->Position + (FleeDir * 4000.0f);
                                    Loser->CampDaysRemaining = 0;
                                }
                                Tribes[i].InteractionCooldownDays = 3; Tribes[j].InteractionCooldownDays = 3;
                            }
                            else if (Roll < W_Combat + W_Trade) {
                                Manager->Stat_Trades++;
                                if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::Trade);

                                if (Manager->TechModule) {
                                    Manager->TechModule->GainKnowledge(Tribes[i].Knowledge, EKnowledgeField::Sociology, 5.0f);
                                    Manager->TechModule->GainKnowledge(Tribes[j].Knowledge, EKnowledgeField::Sociology, 5.0f);
                                }
                                for (auto& Pair : Tribes[i].Knowledge.Levels) {
                                    float Shared = (Pair.Value + (Tribes[j].Knowledge.Levels.Contains(Pair.Key) ? Tribes[j].Knowledge.Levels[Pair.Key] : 0.0f)) * 0.5f;
                                    Tribes[i].Knowledge.Levels[Pair.Key] = Shared; Tribes[j].Knowledge.Levels.Add(Pair.Key, Shared);
                                }

                                auto ShareDiscoveries = [&](FTribeData& Receiver, const FTribeData& Sender) {
                                    for (const FName& Concept : Sender.Knowledge.DiscoveredConcepts) {
                                        if (!Receiver.Knowledge.DiscoveredConcepts.Contains(Concept)) {
                                            Receiver.Knowledge.DiscoveredConcepts.Add(Concept);
                                        }
                                    }
                                    };
                                ShareDiscoveries(Tribes[i], Tribes[j]);
                                ShareDiscoveries(Tribes[j], Tribes[i]);

                                ExecuteMarketSwap(Tribes[i].Inventory, Tribes[j].Inventory, Tribes[i].Population, Tribes[j].Population);

                                Tribes[i].State = ETribeState::Migrating; Tribes[j].State = ETribeState::Migrating;
                                FVector2D WalkDir = (Tribes[i].Position - Tribes[j].Position).GetSafeNormal();
                                if (WalkDir.IsNearlyZero()) WalkDir = FVector2D(1.0f, 0.0f);
                                Tribes[i].TargetRegion = Tribes[i].Position + WalkDir * 3000.0f;
                                Tribes[j].TargetRegion = Tribes[j].Position - WalkDir * 3000.0f;
                                Tribes[i].CampDaysRemaining = 0; Tribes[j].CampDaysRemaining = 0;
                                Tribes[i].InteractionCooldownDays = 2; Tribes[j].InteractionCooldownDays = 2;
                            }
                            else {
                                if (Manager->SettlementModule) {
                                    int32 CombinedPop = Tribes[i].Population + Tribes[j].Population;
                                    FTribeInventory CombinedInv;
                                    CombinedInv.FloraFood = Tribes[i].Inventory.FloraFood + Tribes[j].Inventory.FloraFood;
                                    CombinedInv.MeatFood = Tribes[i].Inventory.MeatFood + Tribes[j].Inventory.MeatFood;
                                    CombinedInv.Wood = Tribes[i].Inventory.Wood + Tribes[j].Inventory.Wood;
                                    CombinedInv.Stone = Tribes[i].Inventory.Stone + Tribes[j].Inventory.Stone;
                                    CombinedInv.IronOre = Tribes[i].Inventory.IronOre + Tribes[j].Inventory.IronOre;
                                    CombinedInv.Tools = Tribes[i].Inventory.Tools + Tribes[j].Inventory.Tools;
                                    CombinedInv.Weapons = Tribes[i].Inventory.Weapons + Tribes[j].Inventory.Weapons;
                                    CombinedInv.Wealth = Tribes[i].Inventory.Wealth + Tribes[j].Inventory.Wealth;
                                    CombinedInv.Oil = Tribes[i].Inventory.Oil + Tribes[j].Inventory.Oil;
                                    CombinedInv.Uranium = Tribes[i].Inventory.Uranium + Tribes[j].Inventory.Uranium;

                                    FKnowledgeContainer CombinedKnowledge = Tribes[i].Knowledge;
                                    for (auto& Pair : Tribes[j].Knowledge.Levels) {
                                        if (CombinedKnowledge.Levels.Contains(Pair.Key)) CombinedKnowledge.Levels[Pair.Key] = FMath::Max(CombinedKnowledge.Levels[Pair.Key], Pair.Value);
                                        else CombinedKnowledge.Levels.Add(Pair.Key, Pair.Value);
                                    }
                                    for (const FName& Concept : Tribes[j].Knowledge.DiscoveredConcepts) {
                                        if (!CombinedKnowledge.DiscoveredConcepts.Contains(Concept)) CombinedKnowledge.DiscoveredConcepts.Add(Concept);
                                    }

                                    FVector2D MergePos = (Tribes[i].Position + Tribes[j].Position) * 0.5f;
                                    FLinearColor MergeColor = FLinearColor::LerpUsingHSV(Tribes[i].TribeColor, Tribes[j].TribeColor, 0.5f);
                                    FCultureProfile DominantCulture = (Tribes[i].Population >= Tribes[j].Population) ? Tribes[i].Culture : Tribes[j].Culture;

                                    Manager->SettlementModule->CreateSettlement(MergePos, CombinedPop, CombinedInv, MergeColor, CombinedKnowledge, DominantCulture);

                                    Manager->Stat_SettlementsFounded++;
                                    if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::SettlementFounded);

                                    Tribes[i].Population = 0; Tribes[j].Population = 0;
                                    bTribeDestroyed = true; break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    for (int32 k = Tribes.Num() - 1; k >= 0; k--) {
        if (Tribes[k].Population <= 0) Tribes.RemoveAtSwap(k);
    }
}

void URelationSystem::ProcessSettlementInteractions(TArray<FSettlementData>& Settlements, ASimWorldManager* Manager)
{
    if (!Manager || Settlements.Num() < 2) return;
    float GridCellSize = 5000.0f;
    SettlementSpatialGrid.Reset();

    for (int32 i = 0; i < Settlements.Num(); i++) {
        if (Settlements[i].Population <= 0) continue;
        FIntPoint GridCoord(
            FMath::FloorToInt(Settlements[i].Position.X / GridCellSize),
            FMath::FloorToInt(Settlements[i].Position.Y / GridCellSize)
        );
        SettlementSpatialGrid.FindOrAdd(GridCoord).Add(i);
    }

    auto ShareSettlementDiscoveries = [&](FSettlementData& Receiver, const FSettlementData& Sender) {
        for (const FName& Concept : Sender.Knowledge.DiscoveredConcepts) {
            if (!Receiver.Knowledge.DiscoveredConcepts.Contains(Concept)) {
                Receiver.Knowledge.DiscoveredConcepts.Add(Concept);
            }
        }
        };

    for (int32 i = Settlements.Num() - 1; i >= 0; i--) {
        if (Settlements[i].Population <= 0 || Settlements[i].InteractionCooldownDays > 0) continue;

        FIntPoint GridCoord(
            FMath::FloorToInt(Settlements[i].Position.X / GridCellSize),
            FMath::FloorToInt(Settlements[i].Position.Y / GridCellSize)
        );

        bool bSettlementDestroyed = false;

        for (int32 dx = -1; dx <= 1 && !bSettlementDestroyed; dx++) {
            for (int32 dy = -1; dy <= 1 && !bSettlementDestroyed; dy++) {
                FIntPoint CheckCoord = GridCoord + FIntPoint(dx, dy);

                if (TArray<int32>* CellSettlements = SettlementSpatialGrid.Find(CheckCoord)) {
                    for (int32 j : *CellSettlements) {

                        if (j >= i || Settlements[j].Population <= 0 || Settlements[j].InteractionCooldownDays > 0) continue;

                        bool bIsNationA = Settlements[i].NationID != -1;
                        bool bIsNationB = Settlements[j].NationID != -1;

                        if (bIsNationA && bIsNationB) {
                            if (Settlements[i].NationID == Settlements[j].NationID) {
                                ShareSettlementDiscoveries(Settlements[i], Settlements[j]);
                                ShareSettlementDiscoveries(Settlements[j], Settlements[i]);
                                continue;
                            }

                            if (Manager->DiplomacyModule) {
                                EDiplomaticState DipState = Manager->DiplomacyModule->GetRelationState(Settlements[i].NationID, Settlements[j].NationID);

                                if (DipState == EDiplomaticState::ActiveWar) {
                                    float ResA = Settlements[i].Inventory.FloraFood + Settlements[i].Inventory.MeatFood + Settlements[i].Inventory.Weapons * 5.0f;
                                    float ResB = Settlements[j].Inventory.FloraFood + Settlements[j].Inventory.MeatFood + Settlements[j].Inventory.Weapons * 5.0f;

                                    FSettlementData* Winner = (ResA >= ResB) ? &Settlements[i] : &Settlements[j];
                                    FSettlementData* Loser = (ResA < ResB) ? &Settlements[i] : &Settlements[j];

                                    Winner->Inventory.IronOre += Loser->Inventory.IronOre * 0.4f; Loser->Inventory.IronOre *= 0.6f;
                                    Winner->Inventory.Tools += Loser->Inventory.Tools * 0.4f; Loser->Inventory.Tools *= 0.6f;
                                    Winner->Inventory.Weapons += Loser->Inventory.Weapons * 0.4f; Loser->Inventory.Weapons *= 0.6f;
                                    Winner->Inventory.Wealth += Loser->Inventory.Wealth * 0.4f; Loser->Inventory.Wealth *= 0.6f;
                                    Winner->Inventory.Oil += Loser->Inventory.Oil * 0.4f; Loser->Inventory.Oil *= 0.6f;
                                    Winner->Inventory.Uranium += Loser->Inventory.Uranium * 0.4f; Loser->Inventory.Uranium *= 0.6f;

                                    Loser->Population = FMath::Max(0, FMath::RoundToInt(Loser->Population * 0.8f));

                                    if (Loser->Population == 0) {
                                        bSettlementDestroyed = (Loser == &Settlements[i]);
                                        for (FIntPoint Coord : Loser->ClaimedCells) {
                                            FCellData* C = nullptr; FIntPoint CC;
                                            if (Manager->GetMutableCellGlobal(Coord.X, Coord.Y, C, CC)) {
                                                C->OwnerSettlementID = -1; C->OwnerNationID = -1; C->PoliticalColor = FLinearColor::Transparent;
                                                C->BuildingType = EBuildingType::None; C->bHasRoad = false;
                                                Manager->RegisterVisualChange(CC, EChunkVisualDirty::Terrain);
                                            }
                                        }
                                    }

                                    Manager->Stat_Battles++;
                                    if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::War);

                                    Settlements[i].InteractionCooldownDays = 15; Settlements[j].InteractionCooldownDays = 15;
                                }
                                else if (DipState == EDiplomaticState::TradeAgreement || DipState == EDiplomaticState::Alliance) {
                                    if (Manager->TransportModule) Manager->TransportModule->EstablishTradeRoute(Settlements[i].SettlementID, Settlements[j].SettlementID, true);

                                    ShareSettlementDiscoveries(Settlements[i], Settlements[j]);
                                    ShareSettlementDiscoveries(Settlements[j], Settlements[i]);

                                    ExecuteMarketSwap(Settlements[i].Inventory, Settlements[j].Inventory, Settlements[i].Population, Settlements[j].Population);

                                    Settlements[i].InteractionCooldownDays = 7; Settlements[j].InteractionCooldownDays = 7;
                                    Manager->Stat_Trades++;
                                    if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::Trade);
                                }
                            }
                            if (bSettlementDestroyed) break;
                            continue;
                        }

                        if (Settlements[i].Population <= 0) { bSettlementDestroyed = true; break; }
                        if (FMath::FRand() > 0.25f) continue;

                        float Dist = FVector2D::Distance(Settlements[i].Position, Settlements[j].Position);

                        if (Dist < 12000.0f) {

                            FVector2D MidPoint = (Settlements[i].Position + Settlements[j].Position) * 0.5f;
                            FCellData MidCell;
                            bool bIsSeparatedByGeography = false;

                            if (Manager->GetCellDataAtLocation(FVector(MidPoint.X, MidPoint.Y, 0), MidCell)) {
                                if (MidCell.SurfaceWater > 0.5f || MidCell.Elevation <= Manager->SeaLevel) {
                                    bIsSeparatedByGeography = true;
                                }
                                if (MidCell.Elevation > Manager->SeaLevel + 500.0f) {
                                    bIsSeparatedByGeography = true;
                                }
                            }

                            if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::Encounter);

                            if (Manager->TechModule) {
                                Manager->TechModule->GainKnowledge(Settlements[i].Knowledge, EKnowledgeField::Sociology, 5.0f);
                                Manager->TechModule->GainKnowledge(Settlements[j].Knowledge, EKnowledgeField::Sociology, 5.0f);
                            }

                            float ResA = Settlements[i].Inventory.FloraFood + Settlements[i].Inventory.MeatFood + Settlements[i].Inventory.Wood + Settlements[i].Inventory.Stone + Settlements[i].Inventory.Weapons * 5.0f;
                            float ResB = Settlements[j].Inventory.FloraFood + Settlements[j].Inventory.MeatFood + Settlements[j].Inventory.Wood + Settlements[j].Inventory.Stone + Settlements[j].Inventory.Weapons * 5.0f;

                            EBiomeType BiomeA = EBiomeType::Grassland;
                            FCellData CellA;
                            if (Manager->GetCellDataAtLocation(FVector(Settlements[i].Position.X, Settlements[i].Position.Y, 0), CellA)) BiomeA = CellA.Biome;

                            float W_Combat = 30.0f, W_Trade = 50.0f, W_Merge = 20.0f;

                            W_Merge += FMath::Max(0.0f, (8000.0f - Dist) * 0.02f);

                            if (bIsSeparatedByGeography) {
                                W_Merge = 0.0f;
                                W_Combat *= 0.5f;
                            }
                            else {
                                if (BiomeA == EBiomeType::Grassland || BiomeA == EBiomeType::Beach) W_Merge += 80.0f;
                                else if (BiomeA == EBiomeType::DeciduousForest || BiomeA == EBiomeType::ConiferousForest) W_Trade += 50.0f;
                                else W_Combat += 50.0f;
                            }

                            float Roll = FMath::FRandRange(0.0f, W_Combat + W_Trade + W_Merge);

                            if (Roll < W_Combat) {
                                Manager->Stat_Battles++;
                                if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::Conflict);

                                FSettlementData* Winner = (ResA >= ResB) ? &Settlements[i] : &Settlements[j];
                                FSettlementData* Loser = (ResA < ResB) ? &Settlements[i] : &Settlements[j];

                                Winner->Inventory.FloraFood += Loser->Inventory.FloraFood * 0.3f; Loser->Inventory.FloraFood *= 0.7f;
                                Winner->Inventory.MeatFood += Loser->Inventory.MeatFood * 0.3f; Loser->Inventory.MeatFood *= 0.7f;
                                Winner->Inventory.Wood += Loser->Inventory.Wood * 0.3f; Loser->Inventory.Wood *= 0.7f;
                                Winner->Inventory.Stone += Loser->Inventory.Stone * 0.3f; Loser->Inventory.Stone *= 0.7f;
                                Winner->Inventory.IronOre += Loser->Inventory.IronOre * 0.3f; Loser->Inventory.IronOre *= 0.7f;
                                Winner->Inventory.Tools += Loser->Inventory.Tools * 0.3f; Loser->Inventory.Tools *= 0.7f;
                                Winner->Inventory.Weapons += Loser->Inventory.Weapons * 0.3f; Loser->Inventory.Weapons *= 0.7f;
                                Winner->Inventory.Wealth += Loser->Inventory.Wealth * 0.3f; Loser->Inventory.Wealth *= 0.7f;
                                Winner->Inventory.Oil += Loser->Inventory.Oil * 0.3f; Loser->Inventory.Oil *= 0.7f;
                                Winner->Inventory.Uranium += Loser->Inventory.Uranium * 0.3f; Loser->Inventory.Uranium *= 0.7f;

                                Winner->Population = FMath::Max(1, FMath::RoundToInt(Winner->Population * 0.95f));
                                Loser->Population = FMath::Max(1, FMath::RoundToInt(Loser->Population * 0.8f));

                                if (Manager->TechModule) {
                                    Manager->TechModule->GainKnowledge(Winner->Knowledge, EKnowledgeField::Warfare, 20.0f);
                                    Manager->TechModule->GainKnowledge(Loser->Knowledge, EKnowledgeField::Warfare, 40.0f);
                                }

                                Settlements[i].InteractionCooldownDays = 15; Settlements[j].InteractionCooldownDays = 15;
                            }
                            else if (Roll < W_Combat + W_Trade) {
                                Manager->Stat_Trades++;
                                if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::Trade);

                                if (Manager->TransportModule) {
                                    Manager->TransportModule->EstablishTradeRoute(Settlements[i].SettlementID, Settlements[j].SettlementID, false);
                                }

                                ShareSettlementDiscoveries(Settlements[i], Settlements[j]);
                                ShareSettlementDiscoveries(Settlements[j], Settlements[i]);

                                ExecuteMarketSwap(Settlements[i].Inventory, Settlements[j].Inventory, Settlements[i].Population, Settlements[j].Population);

                                Settlements[i].InteractionCooldownDays = 7; Settlements[j].InteractionCooldownDays = 7;
                            }
                            else {
                                bool bMerged = false;
                                if (Manager->NationModule) {
                                    bMerged = Manager->NationModule->FormNation(Settlements[i], Settlements[j], Manager);
                                }

                                if (bMerged) {
                                    if (bIsNationA || bIsNationB) {
                                        Manager->Stat_CitiesAbsorbed++;
                                        if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::NationFormed);
                                    }
                                    else {
                                        Manager->Stat_NationsFormed++;
                                        if (Manager->ManaModule) Manager->ManaModule->AccumulateEventMana(1.0f, EManaSourceType::NationFormed);
                                    }

                                    if (Manager->SettlementModule) {
                                        Manager->SettlementModule->RecalculatePoliticalColors(Manager);
                                    }

                                    if (Manager->TransportModule) {
                                        Manager->TransportModule->EstablishTradeRoute(Settlements[i].SettlementID, Settlements[j].SettlementID, true);
                                    }

                                    ShareSettlementDiscoveries(Settlements[i], Settlements[j]);
                                    ShareSettlementDiscoveries(Settlements[j], Settlements[i]);

                                    Settlements[i].InteractionCooldownDays = 30; Settlements[j].InteractionCooldownDays = 30;
                                }
                                else {
                                    Settlements[i].InteractionCooldownDays = 5; Settlements[j].InteractionCooldownDays = 5;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}