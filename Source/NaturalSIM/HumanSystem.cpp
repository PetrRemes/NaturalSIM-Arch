#include "HumanSystem.h"
#include "SimWorldManager.h"
#include "ManaSystem.h" 
#include "SettlementSystem.h"
#include "FaunaSystem.h"
#include "HeatmapSystem.h"
#include "TechnologySystem.h"
#include "RelationSystem.h" 
#include "DisasterSystem.h"

UHumanSystem::UHumanSystem() { PrimaryComponentTick.bCanEverTick = false; }
void UHumanSystem::BeginPlay() { Super::BeginPlay(); }

void UHumanSystem::InitializeHumans(ASimWorldManager* Manager)
{
    if (!Manager) { return; }
    Tribes.Empty();

    TArray<FVector2D> ValidLandSpots;
    float CellSize = 50.0f; int32 ChunkSize = Manager->ChunkSize;

    for (const auto& Pair : Manager->WorldChunks) {
        const FChunkData& Chunk = Pair.Value;
        FIntPoint ChunkCoord = Pair.Key;

        for (int32 i = 0; i < Chunk.MicroCells.Num(); i++) {
            const FCellData& Cell = Chunk.MicroCells[i];
            if (Cell.Elevation > Manager->SeaLevel && Cell.Elevation < 1000.0f && Cell.SurfaceWater < 0.1f && !Cell.bIsVolcano) {
                int32 X = i % ChunkSize; int32 Y = i / ChunkSize;
                float WorldX = (ChunkCoord.X * (ChunkSize - 1) * CellSize) + (X * CellSize);
                float WorldY = (ChunkCoord.Y * (ChunkSize - 1) * CellSize) + (Y * CellSize);
                ValidLandSpots.Add(FVector2D(WorldX, WorldY));
            }
        }
    }

    if (ValidLandSpots.Num() == 0) { return; }

    struct FCandidate { FVector2D Pos; float Score; };
    TArray<FCandidate> EvaluatedCandidates;

    for (int32 i = 0; i < 100; i++) {
        int32 RandomSpot = FMath::RandRange(0, ValidLandSpots.Num() - 1);
        FVector2D Spot = ValidLandSpots[RandomSpot];
        FRegionScore RScore = EvaluateRegion(Manager, Spot, nullptr);
        EvaluatedCandidates.Add({ Spot, RScore.CampScore });
    }

    EvaluatedCandidates.Sort([](const FCandidate& A, const FCandidate& B) {
        return A.Score > B.Score;
        });

    int32 TopPoolSize = FMath::Max(1, FMath::RoundToInt(EvaluatedCandidates.Num() * 0.3f));

    for (int32 i = 0; i < 15; i++) {
        FTribeData Tribe;
        int32 Pick = FMath::RandRange(0, TopPoolSize - 1);

        Tribe.TribeID = i + 1;
        Tribe.Position = EvaluatedCandidates[Pick].Pos;
        Tribe.State = ETribeState::Migrating;
        Tribe.CampDaysRemaining = 0;
        Tribe.InteractionCooldownDays = 0;
        Tribe.DaysAtCamp = 0.0f;
        Tribe.bFollowingHerd = false;
        Tribe.bHasForcedTarget = false;
        Tribe.TrackingConfidence = 0.0f; // Inicializace stopování

        Tribe.Population = 40;
        Tribe.Inventory.FloraFood = 500.0f;
        Tribe.Inventory.MeatFood = 200.0f;
        Tribe.Inventory.Wood = 5.0f;
        Tribe.Inventory.Stone = 0.0f;

        Tribe.TargetRegion = Tribe.Position;
        Tribe.NextStrategicDecisionDay = 0.0;

        GenerateInitialProfile(Tribe, Manager);

        Tribes.Add(Tribe);
    }
}

void UHumanSystem::GenerateInitialProfile(FTribeData& Tribe, ASimWorldManager* Manager)
{
    FCellData Cell;
    if (!Manager->GetCellDataAtLocation(FVector(Tribe.Position.X, Tribe.Position.Y, 0.0f), Cell)) return;

    Tribe.Culture.Pillars.Add(ECulturalPillar::Sociability, 30.0f);
    Tribe.Culture.Pillars.Add(ECulturalPillar::Exploration, 20.0f);
    Tribe.Culture.Pillars.Add(ECulturalPillar::Ecology, 20.0f);

    if (Cell.Biome == EBiomeType::DeciduousForest || Cell.Biome == EBiomeType::ConiferousForest || Cell.Biome == EBiomeType::TropicalForest) {
        Tribe.Culture.Pillars[ECulturalPillar::Ecology] += 40.0f;
        Tribe.Culture.Pillars[ECulturalPillar::Spirituality] += 20.0f;
        Tribe.Knowledge.SubKnowledge.Add(TEXT("Forestry"), 5.0f);
        Tribe.Knowledge.SubKnowledge.Add(TEXT("Foraging"), 10.0f);
        Tribe.Knowledge.Levels[EKnowledgeField::Woodcraft] += 2.0f;
    }
    else if (Cell.Biome == EBiomeType::Desert || Cell.Biome == EBiomeType::Barren || Cell.Biome == EBiomeType::Tundra) {
        Tribe.Culture.Pillars[ECulturalPillar::Exploration] += 40.0f;
        Tribe.Culture.Pillars[ECulturalPillar::Commerce] += 20.0f;
        Tribe.Culture.Pillars[ECulturalPillar::Industry] += 10.0f;
        Tribe.Knowledge.SubKnowledge.Add(TEXT("WaterObservation"), 10.0f);
        Tribe.Knowledge.SubKnowledge.Add(TEXT("Survival"), 10.0f);
        Tribe.Knowledge.Levels[EKnowledgeField::Sociology] += 2.0f;
    }
    else if (Cell.Biome == EBiomeType::Beach || Cell.Biome == EBiomeType::Swamp) {
        Tribe.Culture.Pillars[ECulturalPillar::Sociability] += 20.0f;
        Tribe.Culture.Pillars[ECulturalPillar::Commerce] += 30.0f;
        Tribe.Knowledge.SubKnowledge.Add(TEXT("Fishing"), 10.0f);
        Tribe.Knowledge.SubKnowledge.Add(TEXT("Currents"), 5.0f);
        Tribe.Knowledge.Levels[EKnowledgeField::Maritime] += 3.0f;
    }
    else {
        Tribe.Culture.Pillars[ECulturalPillar::Expansion] += 30.0f;
        Tribe.Culture.Pillars[ECulturalPillar::Sociability] += 10.0f;
        Tribe.Knowledge.SubKnowledge.Add(TEXT("PlantCultivation"), 5.0f);
        Tribe.Knowledge.SubKnowledge.Add(TEXT("AnimalTracking"), 10.0f);
        Tribe.Knowledge.Levels[EKnowledgeField::Agriculture] += 2.0f;
    }

    if (Cell.SurfaceWater > 0.1f || Cell.RiverDischarge > 1.0f) {
        Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("RiverObservation")) += 10.0f;
        Tribe.Culture.Pillars[ECulturalPillar::Commerce] += 10.0f;
    }
}

FRegionScore UHumanSystem::EvaluateRegion(ASimWorldManager* Manager, FVector2D CenterPos, const FTribeData* Tribe)
{
    FRegionScore Score;
    if (!Manager) { return Score; }

    float TotalFood = 0.0f, TotalWood = 0.0f, TotalStone = 0.0f;
    float RiverReliability = 0.0f, GroundWaterPotential = 0.0f;
    float Defense = 0.0f, TerrainCost = 0.0f;
    float PollutionPenalty = 0.0f, FloodRisk = 0.0f;
    int32 ValidCells = 0, FlatCells = 0, FordCells = 0;

    int32 CenterGX = FMath::FloorToInt(CenterPos.X / 50.0f);
    int32 CenterGY = FMath::FloorToInt(CenterPos.Y / 50.0f);

    const FCellData* CenterCellPtr = nullptr;
    if (!Manager->GetCellGlobalPtr(CenterGX, CenterGY, CenterCellPtr)) { return Score; }

    if (CenterCellPtr->Elevation <= Manager->SeaLevel) {
        Score.CampScore = -5000.0f;
        Score.CityScore = -5000.0f;
        return Score;
    }

    for (int32 dy = -5; dy <= 5; dy++) {
        for (int32 dx = -5; dx <= 5; dx++) {
            const FCellData* CellPtr = nullptr;
            if (Manager->GetCellGlobalPtr(CenterGX + dx, CenterGY + dy, CellPtr)) {
                ValidCells++;

                TotalFood += CellPtr->BerryBushes + (CellPtr->FloraDensity * 10.0f);
                TotalWood += CellPtr->WoodAmount;
                if (CellPtr->Bedrock == EBedrockType::Rock) TotalStone += 10.0f;

                GroundWaterPotential += CellPtr->GroundWater * 0.1f;
                if (CellPtr->RiverDischarge > 0.1f) RiverReliability += CellPtr->RiverDischarge * 2.0f;

                if (CellPtr->SurfaceWater > 0.1f && CellPtr->SurfaceWater <= 1.5f && CellPtr->RiverDischarge > 0.5f) {
                    FordCells++;
                }

                if (CellPtr->RiverDischarge > 50.0f && FMath::Abs(CellPtr->Elevation - CenterCellPtr->Elevation) < 2.0f) {
                    FloodRisk += CellPtr->RiverDischarge * 0.5f;
                }

                if (CellPtr->bIsVolcano) PollutionPenalty += 500.0f;
                if (CellPtr->WaterPollution > 0.1f) PollutionPenalty += CellPtr->WaterPollution * 150.0f;
                if (CellPtr->HouseDensity > 0.0f) PollutionPenalty += 1000.0f;

                if (CellPtr->Elevation > 1500.0f) TerrainCost += 50.0f;
                if (CellPtr->DangerLevel > 0.5f) TerrainCost += 20.0f;

                if (CellPtr->Elevation > CenterCellPtr->Elevation + 5.0f && CellPtr->Elevation < CenterCellPtr->Elevation + 30.0f) Defense += 10.0f;

                if (FMath::Abs(CellPtr->Elevation - CenterCellPtr->Elevation) < 5.0f && CellPtr->SurfaceWater < 0.1f && CellPtr->WaterPollution < 0.2f) {
                    FlatCells++;
                }
            }
        }
    }

    if (ValidCells == 0) { return Score; }

    float FoodPotential = FMath::Clamp(TotalFood / 500.0f, 0.0f, 1.0f);
    float WaterPotential = FMath::Clamp((RiverReliability + GroundWaterPotential) / 200.0f, 0.0f, 1.0f);
    float WoodPotential = FMath::Clamp(TotalWood / 1000.0f, 0.0f, 1.0f);
    float StonePotential = FMath::Clamp(TotalStone / 50.0f, 0.0f, 1.0f);
    float DefensePotential = FMath::Clamp(Defense / 50.0f, 0.0f, 1.0f);

    if (Tribe && Tribe->Culture.Pillars.Contains(ECulturalPillar::Militarism)) {
        float Militarism = Tribe->Culture.Pillars[ECulturalPillar::Militarism] / 100.0f;
        DefensePotential *= (1.0f + Militarism * 2.0f);
        WoodPotential *= (1.0f + Militarism * 0.5f);
    }

    float OverlapPenalty = 0.0f;
    if (Manager->SettlementModule) {
        for (const FSettlementData& City : Manager->SettlementModule->Settlements) {
            if (City.Population > 0) {
                float DistSq = FVector2D::DistSquared(CenterPos, City.Position);
                if (DistSq < 36000000.0f) {
                    float Closeness = 1.0f - (FMath::Sqrt(DistSq) / 6000.0f);
                    float Soc = Tribe && Tribe->Culture.Pillars.Contains(ECulturalPillar::Sociability) ? (Tribe->Culture.Pillars[ECulturalPillar::Sociability] / 100.0f) : 0.5f;

                    float SocialTolerance = FMath::Lerp(50.0f, 5.0f, Soc);
                    OverlapPenalty += FMath::Pow(Closeness, 2.5f) * SocialTolerance;
                }
            }
        }
    }

    Score.CampScore = (FoodPotential * 0.40f) + (WaterPotential * 0.30f) + (WoodPotential * 0.20f) + (DefensePotential * 0.10f);
    Score.CampScore -= (PollutionPenalty / 100.0f) + (TerrainCost / 200.0f) + OverlapPenalty + (FloodRisk / 100.0f);

    // --- ZMÌNA 2: TLAK NA OPUŠTÌNÍ VYÈERPANÉHO TÁBORA ---
    if (Tribe) {
        float ExhaustionPenalty = Tribe->DaysAtCamp / 150.0f;

        // Pokud kmen vyžral bezprostøední okolí a je táboøení udržitelné pouze pomocí rezerv
        float RequiredFoodForTribe = Tribe->Population * 3.0f;
        if (TotalFood < RequiredFoodForTribe && Tribe->State == ETribeState::Camping) {
            ExhaustionPenalty += 2.0f; // Brutální penalizace, nutí kmen sbalit tábor a jít dál
        }

        Score.CampScore -= ExhaustionPenalty;
    }

    Score.CityScore = (FoodPotential * 0.30f) + (WaterPotential * 0.25f) + (WoodPotential * 0.15f) + (StonePotential * 0.10f) + (DefensePotential * 0.20f);
    Score.CityScore -= (PollutionPenalty / 100.0f) + (TerrainCost / 100.0f) + (OverlapPenalty * 1.5f) + (FloodRisk / 50.0f);

    float FordBonus = FMath::Clamp(FordCells * 15.0f, 0.0f, 150.0f);
    Score.CityScore += (FordBonus / 100.0f);

    float PredictPop = Tribe ? Tribe->Population : 80.0f;
    float DailyCons = PredictPop * Manager->HumanFoodConsumptionRate;
    float NaturalRegen = TotalFood * 0.005f;

    float FarmPotential = 0.0f;
    if (Tribe && Tribe->Knowledge.Levels.Contains(EKnowledgeField::Agriculture) && Tribe->Knowledge.Levels[EKnowledgeField::Agriculture] > 5.0f) {
        FarmPotential = FlatCells * 0.5f;
    }
    else if (PredictPop >= Manager->PopulationToSettle) {
        FarmPotential = FlatCells * 0.2f;
    }

    float Sustainability = (NaturalRegen + FarmPotential) - DailyCons;

    if (Sustainability < -2.0f) Score.CityScore -= 500.0f;
    else Score.CityScore += Sustainability * 15.0f;

    return Score;
}

FVector2D UHumanSystem::CalculateBestMovementStep(ASimWorldManager* Manager, FVector2D CurrentPos, FVector2D TargetRegion)
{
    FVector2D BestStep = FVector2D::ZeroVector;
    float BestScore = -99999.0f;
    FVector2D GlobalDir = (TargetRegion - CurrentPos).GetSafeNormal();

    int32 CurrentGX = FMath::FloorToInt(CurrentPos.X / 50.0f);
    int32 CurrentGY = FMath::FloorToInt(CurrentPos.Y / 50.0f);

    const FCellData* CurrentCellPtr = nullptr;
    if (!Manager->GetCellGlobalPtr(CurrentGX, CurrentGY, CurrentCellPtr)) { return GlobalDir; }

    for (int k = 0; k < 8; k++) {
        float Angle = (k * PI / 4.0f);
        FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
        FVector2D SamplePos = CurrentPos + Dir * 50.0f;

        int32 SampleGX = FMath::FloorToInt(SamplePos.X / 50.0f);
        int32 SampleGY = FMath::FloorToInt(SamplePos.Y / 50.0f);
        const FCellData* SampleCellPtr = nullptr;

        if (Manager->GetCellGlobalPtr(SampleGX, SampleGY, SampleCellPtr)) {
            float Score = FVector2D::DotProduct(GlobalDir, Dir) * 10.0f;

            if (SampleCellPtr->SurfaceWater > 1.5f || SampleCellPtr->Elevation <= Manager->SeaLevel) {
                Score -= 5000.0f;
            }
            else if (SampleCellPtr->SurfaceWater > 0.1f && SampleCellPtr->SurfaceWater <= 1.5f) {
                Score -= 20.0f;
            }

            if (SampleCellPtr->bIsVolcano) Score -= 1500.0f;

            float Slope = SampleCellPtr->Elevation - CurrentCellPtr->Elevation;
            if (FMath::Abs(Slope) > 15.0f) Score -= FMath::Abs(Slope) * 2.0f;

            Score += SampleCellPtr->FloraDensity * 2.0f;
            Score *= FMath::FRandRange(0.9f, 1.1f);

            if (Score > BestScore) {
                BestScore = Score;
                BestStep = Dir;
            }
        }
    }

    if (BestScore < -100.0f) {
        FVector2D InlandStep = -GlobalDir;
        float HighestZ = CurrentCellPtr->Elevation;

        for (int k = 0; k < 8; k++) {
            float Angle = (k * PI / 4.0f);
            FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));

            int32 SampleGX = FMath::FloorToInt((CurrentPos.X + Dir.X * 50.0f) / 50.0f);
            int32 SampleGY = FMath::FloorToInt((CurrentPos.Y + Dir.Y * 50.0f) / 50.0f);
            const FCellData* SampleCellPtr = nullptr;

            if (Manager->GetCellGlobalPtr(SampleGX, SampleGY, SampleCellPtr)) {
                if (SampleCellPtr->Elevation > HighestZ && SampleCellPtr->SurfaceWater < 0.8f) {
                    HighestZ = SampleCellPtr->Elevation;
                    InlandStep = Dir;
                }
            }
        }
        if (!InlandStep.IsNearlyZero()) { return InlandStep.GetSafeNormal(); }
        return FVector2D(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)).GetSafeNormal();
    }
    return BestStep;
}

void UHumanSystem::ProcessHumansSlice(TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, float DeltaTime, int32 StartIdx, int32 EndIdx)
{
    if (!Manager) { return; }
    if (StartIdx == 0) {
        for (int32 k = Tribes.Num() - 1; k >= 0; k--) {
            if (Tribes[k].Population <= 0) Tribes.RemoveAtSwap(k);
        }

        TribeSpatialGrid.Reset();
        float InvGridSize = 1.0f / (Manager->ChunkSize * 50.0f);
        for (int32 i = 0; i < Tribes.Num(); i++) {
            if (Tribes[i].Population > 0) {
                int32 CX = FMath::FloorToInt(Tribes[i].Position.X * InvGridSize);
                int32 CY = FMath::FloorToInt(Tribes[i].Position.Y * InvGridSize);
                TribeSpatialGrid.FindOrAdd(FIntPoint(CX, CY)).Add(i);
            }
        }
    }

    EndIdx = FMath::Min(EndIdx, Tribes.Num());
    float CellSize = 50.0f; int32 ChunkSize = Manager->ChunkSize;

    const TMap<FIntPoint, TArray<int32>>* AnimalGridPtr = nullptr;
    if (Manager->FaunaModule) { AnimalGridPtr = &Manager->FaunaModule->AnimalSpatialGrid; }

    FVector PLoc = Manager->GetPlayerLocation(); FVector2D PlayerPos2D(PLoc.X, PLoc.Y);
    float SimBubbleRadiusSq = 20000.0f * 20000.0f;

    for (int32 i = StartIdx; i < EndIdx; i++) {
        FTribeData& Tribe = Tribes[i];
        if (Tribe.Population <= 0) continue;

        float DistToPlayerSq = FVector2D::DistSquared(Tribe.Position, PlayerPos2D);
        bool bIsDetailedLOD = DistToPlayerSq < SimBubbleRadiusSq;

        int32 GlobalX = FMath::FloorToInt(Tribe.Position.X / CellSize);
        int32 GlobalY = FMath::FloorToInt(Tribe.Position.Y / CellSize);

        float MaxFood = Tribe.Population * 10.0f; float MaxWood = Tribe.Population * 15.0f;

        if (Tribe.Inventory.FloraFood < 0.0f) Tribe.Inventory.FloraFood = 0.0f;
        if (Tribe.Inventory.MeatFood < 0.0f) Tribe.Inventory.MeatFood = 0.0f;
        if (Tribe.Inventory.Wood < 0.0f) Tribe.Inventory.Wood = 0.0f;

        Tribe.Inventory.FloraFood = FMath::Clamp(Tribe.Inventory.FloraFood, 0.0f, MaxFood);
        Tribe.Inventory.MeatFood = FMath::Clamp(Tribe.Inventory.MeatFood, 0.0f, MaxFood);
        Tribe.Inventory.Wood = FMath::Clamp(Tribe.Inventory.Wood, 0.0f, MaxWood);
        Tribe.Inventory.Stone = FMath::Clamp(Tribe.Inventory.Stone, 0.0f, MaxWood);

        if (Tribe.State == ETribeState::Migrating) {

            Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Exploration")) += 0.5f * DeltaTime;
            Tribe.Culture.Pillars[ECulturalPillar::Exploration] = FMath::Min(100.0f, Tribe.Culture.Pillars[ECulturalPillar::Exploration] + 0.1f * DeltaTime);

            if (Manager->CurrentDay >= Tribe.NextStrategicDecisionDay) {

                // KRIZOVÝ ÚTÌK PØED KATASTROFOU
                bool bFleeing = false;
                if (Manager->DisasterModule && Manager->DisasterModule->ActiveWarnings.Num() > 0) {
                    for (const FDisasterWarning& W : Manager->DisasterModule->ActiveWarnings) {
                        if (FVector2D::Distance(Tribe.Position, W.Epicenter) < W.Radius * 1.5f) {
                            bFleeing = true;
                            Tribe.bFollowingHerd = false;
                            Tribe.bHasForcedTarget = false;
                            Tribe.TrackingConfidence = 0.0f;

                            FVector2D FleeDir = (Tribe.Position - W.Epicenter).GetSafeNormal();
                            if (FleeDir.IsNearlyZero()) FleeDir = FVector2D(1.0f, 0.0f);

                            Tribe.TargetRegion = Tribe.Position + (FleeDir * 15000.0f);
                            Tribe.NextStrategicDecisionDay = Manager->CurrentDay + 5.0;
                            break;
                        }
                    }
                }

                if (!bFleeing) {
                    if (Tribe.bHasForcedTarget) {
                        Tribe.TargetRegion = Tribe.ForcedTarget;
                        Tribe.NextStrategicDecisionDay = Manager->CurrentDay + 5.0;
                        Tribe.bFollowingHerd = false;
                        Tribe.TrackingConfidence = 0.0f;
                    }
                    else {
                        if (bIsDetailedLOD) {

                            // --- ZMÌNA 1: TRVALÉ STOPOVÁNÍ STÁD ---
                            bool bFoundHerdThisTick = false;
                            float TrackingSkill = Tribe.Knowledge.SubKnowledge.Contains(TEXT("AnimalTracking")) ? Tribe.Knowledge.SubKnowledge[TEXT("AnimalTracking")] : 0.0f;

                            if (TrackingSkill > 5.0f && AnimalGridPtr && (Tribe.Inventory.MeatFood < MaxFood * 0.8f)) {
                                int32 GCX = FMath::FloorToInt(Tribe.Position.X / (ChunkSize * CellSize));
                                int32 GCY = FMath::FloorToInt(Tribe.Position.Y / (ChunkSize * CellSize));
                                float MinDistSq = 9999999999.0f;

                                FVector2D BestHerdPos = Tribe.Position;
                                FVector2D BestHerdDir = FVector2D(1.0f, 0.0f);

                                // Radar - Hledáme zvíøata v okolí
                                for (int32 dy = -2; dy <= 2; dy++) {
                                    for (int32 dx = -2; dx <= 2; dx++) {
                                        if (const TArray<int32>* CellAnimals = AnimalGridPtr->Find(FIntPoint(GCX + dx, GCY + dy))) {
                                            for (int32 aIdx : *CellAnimals) {
                                                if (Manager->FaunaModule->Animals.IsValidIndex(aIdx)) {
                                                    const FAnimalData& Animal = Manager->FaunaModule->Animals[aIdx];
                                                    if (Animal.HerdSize > 5.0f && (Animal.Type == EAnimalType::Herbivore || Animal.Type == EAnimalType::ForestAnimal)) {
                                                        float DistSq = FVector2D::DistSquared(Tribe.Position, Animal.Position);
                                                        if (DistSq < MinDistSq) {
                                                            MinDistSq = DistSq; BestHerdPos = Animal.Position; BestHerdDir = Animal.TargetDirection;
                                                            bFoundHerdThisTick = true;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                if (bFoundHerdThisTick) {
                                    // Máme zvíøe na dohled, zamìøujeme stopu!
                                    Tribe.bFollowingHerd = true;
                                    Tribe.LastKnownHerdPosition = BestHerdPos;
                                    Tribe.HerdDirection = BestHerdDir;
                                    Tribe.TrackingConfidence = 100.0f; // Maximální jistota

                                    float DistanceToHerd = FMath::Sqrt(MinDistSq);
                                    if (DistanceToHerd > 1000.0f) Tribe.TargetRegion = BestHerdPos;
                                    else Tribe.TargetRegion = BestHerdPos + BestHerdDir * 1500.0f;

                                    Tribe.NextStrategicDecisionDay = Manager->CurrentDay + 2.0;
                                }
                            }

                            // Pokud jsme zvíøe nevidìli, ale máme aktivní stopu, jdeme po ní naslepo dál
                            if (!bFoundHerdThisTick && Tribe.TrackingConfidence > 0.0f) {
                                Tribe.TrackingConfidence -= 15.0f * DeltaTime; // Postupná ztráta stop
                                Tribe.LastKnownHerdPosition += Tribe.HerdDirection * 30.0f * DeltaTime; // Predikce pohybu

                                Tribe.TargetRegion = Tribe.LastKnownHerdPosition;
                                Tribe.bFollowingHerd = true;
                                Tribe.NextStrategicDecisionDay = Manager->CurrentDay + 1.0;
                            }
                            // Pokud jsme stopu nadobro ztratili, volíme nový náhodný cíl
                            else if (!bFoundHerdThisTick && Tribe.TrackingConfidence <= 0.0f) {
                                Tribe.bFollowingHerd = false;
                                FVector2D BestTarget = Tribe.Position;
                                float BestScore = -99999.0f;

                                for (int d = 0; d < 8; d++) {
                                    FVector2D Dir(FMath::Cos(d * PI / 4.0f), FMath::Sin(d * PI / 4.0f));
                                    FVector2D SamplePos = Tribe.Position + Dir * FMath::FRandRange(1500.0f, 8000.0f);
                                    FRegionScore RScore = EvaluateRegion(Manager, SamplePos, &Tribe);

                                    if (RScore.CampScore > BestScore) {
                                        BestScore = RScore.CampScore; BestTarget = SamplePos;
                                    }
                                }
                                Tribe.TargetRegion = BestTarget;
                                Tribe.NextStrategicDecisionDay = Manager->CurrentDay + FMath::RandRange(30, 90);
                            }
                        }
                        else {
                            float Angle = FMath::FRandRange(0.0f, PI * 2.0f);
                            FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
                            Tribe.TargetRegion = Tribe.Position + Dir * FMath::FRandRange(2000.0f, 8000.0f);
                            Tribe.NextStrategicDecisionDay = Manager->CurrentDay + FMath::RandRange(30, 90);
                        }
                    }
                }
            }

            if (FVector2D::DistSquared(Tribe.Position, Tribe.TargetRegion) < 250000.0f) {
                if (Tribe.bHasForcedTarget) Tribe.bHasForcedTarget = false;

                float FinalCampScore = 0.0f;
                if (bIsDetailedLOD) {
                    FRegionScore FinalCheck = EvaluateRegion(Manager, Tribe.Position, &Tribe);
                    FinalCampScore = FinalCheck.CampScore;
                }
                else {
                    const FCellData* CCell = nullptr;
                    if (Manager->GetCellGlobalPtr(GlobalX, GlobalY, CCell)) {
                        FinalCampScore = 0.2f;
                        if (CCell->SurfaceWater < 0.5f && CCell->Elevation > Manager->SeaLevel) FinalCampScore += 0.2f;
                        if (CCell->FloraDensity > 0.3f || CCell->TreeType != ETreeType::None) FinalCampScore += 0.2f;
                        if (CCell->Biome == EBiomeType::Grassland || CCell->Biome == EBiomeType::DeciduousForest) FinalCampScore += 0.1f;
                        if (CCell->DangerLevel > 0.5f || CCell->bIsVolcano) FinalCampScore -= 0.5f;
                    }
                }

                if (FinalCampScore > 0.50f) {
                    Tribe.State = ETribeState::Camping;
                    Tribe.CampDaysRemaining = FMath::RandRange(30, 90);
                    Tribe.DaysAtCamp = 0.0f;
                }
                else {
                    Tribe.NextStrategicDecisionDay = 0.0;
                }
            }

            FVector2D MoveDir;
            if (bIsDetailedLOD) MoveDir = CalculateBestMovementStep(Manager, Tribe.Position, Tribe.TargetRegion);
            else { MoveDir = (Tribe.TargetRegion - Tribe.Position).GetSafeNormal(); if (MoveDir.IsNearlyZero()) MoveDir = FVector2D(1.0f, 0.0f); }

            Tribe.Position += MoveDir * 25.0f * DeltaTime;

            if (bIsDetailedLOD) {
                FCellData* WalkCell = nullptr; FIntPoint WalkCoord;
                if (Manager->GetMutableCellGlobal(GlobalX, GlobalY, WalkCell, WalkCoord)) {
                    if (WalkCell->FloraDensity > 0.0f && Tribe.Inventory.FloraFood < MaxFood) {
                        float Gathered = FMath::Min(WalkCell->FloraDensity, 1.0f * DeltaTime);
                        WalkCell->FloraDensity -= Gathered;
                        Tribe.Inventory.FloraFood += Gathered * 2.0f;
                        Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Foraging")) += Gathered;
                        Manager->RegisterVisualChange(WalkCoord, EChunkVisualDirty::Flora);
                    }
                    if (WalkCell->SurfaceWater > 0.1f) Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("RiverObservation")) += 0.5f * DeltaTime;
                }
            }
            else {
                if (Tribe.Inventory.FloraFood < MaxFood) {
                    float Gathered = 0.8f * DeltaTime;
                    Tribe.Inventory.FloraFood += Gathered * 2.0f;
                    Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Foraging")) += Gathered;
                }
            }
        }
        else if (Tribe.State == ETribeState::Camping) {

            if (Manager->DisasterModule && Manager->DisasterModule->ActiveWarnings.Num() > 0) {
                for (const FDisasterWarning& W : Manager->DisasterModule->ActiveWarnings) {
                    if (FVector2D::Distance(Tribe.Position, W.Epicenter) < W.Radius) {
                        Tribe.State = ETribeState::Migrating;
                        Tribe.CampDaysRemaining = 0;
                        Tribe.DaysAtCamp = 0.0f;
                        Tribe.NextStrategicDecisionDay = 0.0;
                        Tribe.TrackingConfidence = 0.0f;
                        break;
                    }
                }
            }

            if (Tribe.State == ETribeState::Camping) {
                Tribe.DaysAtCamp += DeltaTime;
                float DailyFoodCons = Tribe.Population * Manager->HumanFoodConsumptionRate * DeltaTime;
                float Gathered = 0.0f;

                if (bIsDetailedLOD) {
                    bool bHuntedAnything = false;
                    if (Manager->FaunaModule && Tribe.Inventory.MeatFood < MaxFood) {
                        const float HuntRadiusSq = 250.0f * 250.0f;
                        int32 GridCellSize = 50.0f * (Manager->ChunkSize - 1);
                        int32 GCX = FMath::FloorToInt(Tribe.Position.X / GridCellSize);
                        int32 GCY = FMath::FloorToInt(Tribe.Position.Y / GridCellSize);

                        for (int32 dx = -1; dx <= 1 && !bHuntedAnything; ++dx) {
                            for (int32 dy = -1; dy <= 1 && !bHuntedAnything; ++dy) {
                                FIntPoint SearchCoord(GCX + dx, GCY + dy);
                                if (AnimalGridPtr) {
                                    if (const TArray<int32>* CellAnimals = AnimalGridPtr->Find(SearchCoord)) {
                                        for (int32 aIdx : *CellAnimals) {
                                            if (!Manager->FaunaModule->Animals.IsValidIndex(aIdx)) continue;
                                            FAnimalData& Animal = Manager->FaunaModule->Animals[aIdx];

                                            if (Animal.HerdSize <= 0.0f) continue;
                                            if (FVector2D::DistSquared(Tribe.Position, Animal.Position) < HuntRadiusSq) {
                                                float HuntAmount = FMath::Min(Animal.HerdSize, Tribe.Population * 0.15f * DeltaTime);
                                                Animal.HerdSize -= HuntAmount; Tribe.Inventory.MeatFood += HuntAmount * 2.0f; bHuntedAnything = true;

                                                Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Hunting")) += HuntAmount * 5.0f;
                                                Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("AnimalTracking")) += HuntAmount * 2.0f;

                                                FCellData* TargetCell = nullptr; FIntPoint TCoord;
                                                int32 AnimalGX = FMath::FloorToInt(Animal.Position.X / CellSize);
                                                int32 AnimalGY = FMath::FloorToInt(Animal.Position.Y / CellSize);

                                                if (Manager->GetMutableCellGlobal(AnimalGX, AnimalGY, TargetCell, TCoord)) {
                                                    TargetCell->AnimalBones += HuntAmount * 5.0f;
                                                    Manager->RegisterVisualChange(TCoord, EChunkVisualDirty::Flora);
                                                }
                                                if (Manager->TechModule) Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Textiles, 0.5f * DeltaTime);
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    for (int32 dy = -1; dy <= 1; dy++) {
                        for (int32 dx = -1; dx <= 1; dx++) {
                            FCellData* CCell = nullptr; FIntPoint CCoord;
                            if (Manager->GetMutableCellGlobal(GlobalX + dx, GlobalY + dy, CCell, CCoord)) {

                                if (Tribe.Knowledge.Levels.Contains(EKnowledgeField::Agriculture) && Tribe.Knowledge.Levels[EKnowledgeField::Agriculture] >= 5.0f) {
                                    if (CCell->BuildingType != EBuildingType::Farm && CCell->TreeType == ETreeType::None && CCell->SurfaceWater < 0.1f && CCell->Elevation > Manager->SeaLevel) {
                                        CCell->BuildingType = EBuildingType::Farm;
                                        Manager->RegisterVisualChange(CCoord, EChunkVisualDirty::Flora);
                                    }
                                }

                                if (CCell->BuildingType == EBuildingType::Farm) {
                                    float FarmYield = 15.0f * FMath::Max(0.0f, 1.0f - CCell->WaterPollution) * DeltaTime;
                                    Gathered += FarmYield;
                                    Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("PlantCultivation")) += FarmYield;
                                }
                                else {
                                    if (CCell->FloraDensity > 0.0f) {
                                        CCell->FloraDensity = FMath::Max(0.0f, CCell->FloraDensity - (0.05f * DeltaTime));
                                        Manager->RegisterVisualChange(CCoord, EChunkVisualDirty::Flora);
                                    }
                                    if (Tribe.Inventory.FloraFood < MaxFood) {
                                        if (CCell->BerryBushes > 0.0f) {
                                            float Take = FMath::Min(CCell->BerryBushes, 2.0f * DeltaTime);
                                            CCell->BerryBushes -= Take; Gathered += Take * 2.0f;
                                            Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Foraging")) += Take;
                                        }
                                        else if (CCell->FloraDensity > 0.1f) {
                                            float Take = 0.5f * DeltaTime;
                                            CCell->FloraDensity -= Take; Gathered += Take;
                                            Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Foraging")) += Take * 0.5f;
                                        }
                                    }
                                    if (CCell->TreeType != ETreeType::None && Tribe.Inventory.Wood < MaxWood) {
                                        float Chop = 1.0f * DeltaTime;
                                        CCell->WoodAmount = FMath::Max(0.0f, CCell->WoodAmount - Chop);
                                        Tribe.Inventory.Wood += Chop;
                                        Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Woodcutting")) += Chop;
                                    }
                                }

                                if (dx == 0 && dy == 0) {
                                    if (Manager->TechModule) {
                                        float LearnRate = 0.8f * DeltaTime;
                                        switch (CCell->Biome) {
                                        case EBiomeType::DeciduousForest: case EBiomeType::ConiferousForest: case EBiomeType::TropicalForest: Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Woodcraft, LearnRate); break;
                                        case EBiomeType::Grassland: Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Agriculture, LearnRate); break;
                                        case EBiomeType::Tundra: case EBiomeType::Barren: Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Masonry, LearnRate); break;
                                        case EBiomeType::Desert: Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Academics, LearnRate); break;
                                        case EBiomeType::Swamp: case EBiomeType::Beach: Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Maritime, LearnRate); break;
                                        }
                                        if (CCell->Bedrock == EBedrockType::Rock || CCell->bIsVolcano) Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Metallurgy, LearnRate * 0.8f);
                                    }

                                    if (CCell->SurfaceWater > 0.1f || CCell->RiverDischarge > 0.5f) {
                                        Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("RiverObservation")) += 1.0f * DeltaTime;
                                    }
                                }
                            }
                        }
                    }

                    if (Manager->TechModule) {
                        FCellData EvalCell;
                        if (Manager->GetCellGlobal(GlobalX, GlobalY, EvalCell)) {
                            Manager->TechModule->EvaluateEntityKnowledge(Tribe.Knowledge, Tribe.Culture, EvalCell, Manager, Tribe.TribeID, false, Tribe.Position);
                        }
                    }
                }
                else {
                    if (Tribe.Inventory.MeatFood < MaxFood) {
                        float AbstractHunt = Tribe.Population * 0.10f * DeltaTime;
                        Tribe.Inventory.MeatFood += AbstractHunt * 2.0f;
                        Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Hunting")) += AbstractHunt * 5.0f;
                        Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("AnimalTracking")) += AbstractHunt * 2.0f;
                    }

                    float AbstractFarm = 0.0f;
                    if (Tribe.Knowledge.Levels.Contains(EKnowledgeField::Agriculture) && Tribe.Knowledge.Levels[EKnowledgeField::Agriculture] >= 5.0f) {
                        AbstractFarm = 10.0f * DeltaTime;
                        Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("PlantCultivation")) += AbstractFarm;
                    }

                    float AbstractPick = 2.0f * DeltaTime;
                    Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Foraging")) += AbstractPick;

                    if (Tribe.Inventory.Wood < MaxWood) {
                        float AbstractChop = 1.0f * DeltaTime;
                        Tribe.Inventory.Wood += AbstractChop;
                        Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Woodcutting")) += AbstractChop;
                    }

                    Gathered = AbstractFarm + (AbstractPick * 2.0f);
                }

                Tribe.Inventory.FloraFood += Gathered;
                float CurrentTotalFood = Tribe.Inventory.FloraFood + Tribe.Inventory.MeatFood;
                if (CurrentTotalFood > MaxFood) {
                    float Scale = MaxFood / FMath::Max(1.0f, CurrentTotalFood);
                    Tribe.Inventory.FloraFood *= Scale;
                    Tribe.Inventory.MeatFood *= Scale;
                }

                if (Tribe.Inventory.MeatFood >= DailyFoodCons) { Tribe.Inventory.MeatFood -= DailyFoodCons; }
                else {
                    float Left = DailyFoodCons - Tribe.Inventory.MeatFood;
                    Tribe.Inventory.MeatFood = 0.0f;
                    Tribe.Inventory.FloraFood = FMath::Max(0.0f, Tribe.Inventory.FloraFood - Left);
                }
            }
        }
    }

    if (EndIdx > StartIdx) {
        Manager->bHumanVisualDirty = true;
    }
}

void UHumanSystem::ProcessDailyDemographics(ASimWorldManager* Manager)
{
    if (!Manager) { return; }
    TArray<FTribeData> SplinterTribes;

    FVector PLoc = Manager->GetPlayerLocation();
    FVector2D PlayerPos2D(PLoc.X, PLoc.Y);
    float SimBubbleRadiusSq = 20000.0f * 20000.0f;

    for (int32 i = Tribes.Num() - 1; i >= 0; i--) {
        FTribeData& Tribe = Tribes[i];

        float DistToPlayerSq = FVector2D::DistSquared(Tribe.Position, PlayerPos2D);
        bool bIsDetailedLOD = DistToPlayerSq < SimBubbleRadiusSq;

        if (Tribe.InteractionCooldownDays > 0) Tribe.InteractionCooldownDays--;

        for (int32 p = Tribe.Pregnancies.Num() - 1; p >= 0; p--) {
            Tribe.Pregnancies[p].DaysRemaining--;
            if (Tribe.Pregnancies[p].DaysRemaining <= 0) {
                int32 Amount = Tribe.Pregnancies[p].Amount;
                Tribe.Population += Amount;

                if (Manager->ManaModule) Manager->ManaModule->AccumulateLifeMana(Amount, EManaSourceType::HumanBirth);
                Tribe.Pregnancies.RemoveAt(p);
            }
        }

        float FoodReserve = Tribe.Inventory.FloraFood + Tribe.Inventory.MeatFood;
        float DailyConsumption = Tribe.Population * Manager->HumanFoodConsumptionRate;

        if (FoodReserve <= 0.0f) {
            float StarvationRate = 0.50f / 365.0f;
            float RawDeaths = Tribe.Population * StarvationRate;
            int32 Deaths = FMath::FloorToInt(RawDeaths);
            if (FMath::FRand() < FMath::Fmod(RawDeaths, 1.0f)) Deaths++;

            int32 ActualDeaths = FMath::Max(1, Deaths);
            Tribe.Population -= ActualDeaths;

            if (Manager->ManaModule) Manager->ManaModule->AccumulateLifeMana(ActualDeaths, EManaSourceType::HumanDeath);

            if (Tribe.Population <= 0) { Tribes.RemoveAt(i); continue; }
            Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Survival")) += 2.0f;
        }
        else {
            float NatRate = 0.02f / 365.0f;
            float RawDeaths = Tribe.Population * NatRate;
            int32 Deaths = FMath::FloorToInt(RawDeaths);
            if (FMath::FRand() < FMath::Fmod(RawDeaths, 1.0f)) Deaths++;

            Tribe.Population -= Deaths;

            if (Deaths > 0 && Manager->ManaModule) Manager->ManaModule->AccumulateLifeMana(Deaths, EManaSourceType::HumanDeath);
            if (Tribe.Population <= 0) { Tribes.RemoveAt(i); continue; }
        }

        float BaseCap = 200.0f;
        if (Tribe.Population > 0 && Manager->GetTotalPopulation() < Manager->MaxGlobalPopulation) {
            if (FoodReserve > DailyConsumption * 5.0f) {
                float PopFactor = FMath::Clamp((float)Tribe.Population / BaseCap, 0.0f, 1.0f);
                float AnnualGrowth = FMath::Lerp(0.20f, 0.01f, FMath::Pow(PopFactor, 2.0f));

                float DailyBirthsRaw = (Tribe.Population * AnnualGrowth) / 365.0f;
                int32 BirthAmount = FMath::FloorToInt(DailyBirthsRaw);
                if (FMath::FRand() < FMath::Fmod(DailyBirthsRaw, 1.0f)) BirthAmount++;

                if (BirthAmount > 0) {
                    FPregnancyBatch NewBatch; NewBatch.DaysRemaining = 60; NewBatch.Amount = BirthAmount; Tribe.Pregnancies.Add(NewBatch);
                }
            }
        }

        if (Tribe.State == ETribeState::Camping) {

            // --- ZMÌNA: Pøísnìjší tlak na zrušení tábora ---
            float CurrentCampScore = 0.0f;
            if (bIsDetailedLOD) {
                FRegionScore Region = EvaluateRegion(Manager, Tribe.Position, &Tribe);
                CurrentCampScore = Region.CampScore;
            }
            else {
                CurrentCampScore = 0.5f - (Tribe.DaysAtCamp / 150.0f); // Fallback zhoršování pro low LOD
            }

            // Tábor balí døív: Buï došly zásoby a okolí už nestaèí, NEBO je okolí totálnì vyrabované (Score < 0.2)
            if (FoodReserve < DailyConsumption * 2.0f || CurrentCampScore < 0.2f) {
                Tribe.State = ETribeState::Migrating;
                Tribe.CampDaysRemaining = 0;
                Tribe.DaysAtCamp = 0.0f;
                Tribe.NextStrategicDecisionDay = 0.0;
                Tribe.TrackingConfidence = 0.0f; // Resetujeme stopování pøi zvednutí kempu
            }
            else {
                Tribe.CampDaysRemaining--;
                if (Tribe.CampDaysRemaining <= 0) {
                    float CityScore = 0.0f; float CampScore = CurrentCampScore;
                    if (bIsDetailedLOD) {
                        FRegionScore Region = EvaluateRegion(Manager, Tribe.Position, &Tribe);
                        CityScore = Region.CityScore;
                    }

                    float TotalKnowledge = 0.0f;
                    for (auto& Pair : Tribe.Knowledge.Levels) TotalKnowledge += Pair.Value;

                    if (CityScore > 0.55f && Tribe.Population >= Manager->PopulationToSettle && TotalKnowledge >= 10.0f) {
                        if (Tribe.Knowledge.Levels.Contains(EKnowledgeField::Agriculture) && Tribe.Knowledge.Levels[EKnowledgeField::Agriculture] >= 5.0f) {

                            if (FoodReserve > (DailyConsumption * 15.0f)) {
                                if (Manager->SettlementModule) {
                                    Manager->SettlementModule->CreateSettlement(Tribe.Position, Tribe.Population, Tribe.Inventory, Tribe.TribeColor, Tribe.Knowledge, Tribe.Culture);
                                    Tribe.Population = 0;
                                    continue;
                                }
                            }
                            else { Tribe.CampDaysRemaining = FMath::Max(Tribe.CampDaysRemaining, 30); }
                        }
                    }

                    if (CampScore > 0.50f) { Tribe.CampDaysRemaining = 30; }
                    else { Tribe.State = ETribeState::Migrating; Tribe.DaysAtCamp = 0.0f; Tribe.NextStrategicDecisionDay = 0.0; Tribe.TrackingConfidence = 0.0f; }
                }
            }
        }

        if (Tribe.Population > BaseCap * 0.8f) {
            FTribeData Splinter = Tribe; Splinter.Population = Tribe.Population / 2; Tribe.Population -= Splinter.Population;
            Splinter.Position += FVector2D(FMath::RandRange(-300.f, 300.f), FMath::RandRange(-300.f, 300.f));
            Splinter.State = ETribeState::Migrating; Splinter.CampDaysRemaining = 0; Splinter.DaysAtCamp = 0.0f; Splinter.NextStrategicDecisionDay = 0.0;
            Splinter.TrackingConfidence = 0.0f;
            SplinterTribes.Add(Splinter);
        }
    }

    for (const FTribeData& Splinter : SplinterTribes) { Tribes.Add(Splinter); }
}

void UHumanSystem::BuildHumanMesh(TSharedPtr<FChunkMeshData> MeshData, ASimWorldManager* Manager) {}

bool UHumanSystem::TryBoostCulturalPillar(int32 TribeID, ECulturalPillar Pillar, ASimWorldManager* Manager)
{
    if (!Manager || !Manager->ManaModule) return false;
    for (FTribeData& Tribe : Tribes) {
        if (Tribe.TribeID == TribeID) {

            if (!Manager->ManaModule->SpendMana(50.0f, TEXT("Inspirace kmene"))) return false;

            float BoostAmount = 20.0f;
            float AllyAmount = 10.0f;
            float OpponentPenalty = 15.0f;

            ECulturalPillar AllyPillar = Pillar;
            ECulturalPillar OpponentPillar = Pillar;

            switch (Pillar) {
            case ECulturalPillar::Ecology: AllyPillar = ECulturalPillar::Spirituality; OpponentPillar = ECulturalPillar::Industry; break;
            case ECulturalPillar::Industry: AllyPillar = ECulturalPillar::Expansion; OpponentPillar = ECulturalPillar::Ecology; break;
            case ECulturalPillar::Militarism: AllyPillar = ECulturalPillar::Aggression; OpponentPillar = ECulturalPillar::Sociability; break;
            case ECulturalPillar::Sociability: AllyPillar = ECulturalPillar::Commerce; OpponentPillar = ECulturalPillar::Militarism; break;
            case ECulturalPillar::Science: AllyPillar = ECulturalPillar::Exploration; OpponentPillar = ECulturalPillar::Spirituality; break;
            case ECulturalPillar::Commerce: AllyPillar = ECulturalPillar::Sociability; OpponentPillar = ECulturalPillar::Aggression; break;
            default: break;
            }

            Tribe.Culture.Pillars.FindOrAdd(Pillar) = FMath::Min(100.0f, Tribe.Culture.Pillars[Pillar] + BoostAmount);
            if (AllyPillar != Pillar) {
                Tribe.Culture.Pillars.FindOrAdd(AllyPillar) = FMath::Min(100.0f, Tribe.Culture.Pillars[AllyPillar] + AllyAmount);
            }
            if (OpponentPillar != Pillar) {
                Tribe.Culture.Pillars.FindOrAdd(OpponentPillar) = FMath::Max(0.0f, Tribe.Culture.Pillars[OpponentPillar] - OpponentPenalty);
            }
            return true;
        }
    }
    return false;
}

bool UHumanSystem::TryForceMigration(int32 TribeID, FVector2D TargetLoc, ASimWorldManager* Manager)
{
    if (!Manager || !Manager->ManaModule) return false;
    for (FTribeData& Tribe : Tribes) {
        if (Tribe.TribeID == TribeID) {

            if (!Manager->ManaModule->SpendMana(250.0f, TEXT("Nucena migrace kmene"))) return false;

            Tribe.State = ETribeState::Migrating;
            Tribe.bHasForcedTarget = true;
            Tribe.ForcedTarget = TargetLoc;
            Tribe.CampDaysRemaining = 0;
            Tribe.NextStrategicDecisionDay = 0.0;
            Tribe.TrackingConfidence = 0.0f; // Vyrušení stopování

            if (Tribe.Culture.Pillars.Contains(ECulturalPillar::Exploration)) {
                Tribe.Culture.Pillars[ECulturalPillar::Exploration] = FMath::Max(0.0f, Tribe.Culture.Pillars[ECulturalPillar::Exploration] - 25.0f);
            }
            if (Tribe.Knowledge.SubKnowledge.Contains(TEXT("AnimalTracking"))) {
                Tribe.Knowledge.SubKnowledge[TEXT("AnimalTracking")] = FMath::Max(0.0f, Tribe.Knowledge.SubKnowledge[TEXT("AnimalTracking")] - 10.0f);
            }

            return true;
        }
    }
    return false;
}