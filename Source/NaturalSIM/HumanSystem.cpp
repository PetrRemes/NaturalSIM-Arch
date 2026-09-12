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

        for (int32 i = 0; i < Chunk.StaticCells.Num(); i++) {
            const FCellStaticData& SCell = Chunk.StaticCells[i];
            const FCellDynamicData& DCell = Chunk.DynamicCells[i];

            if (SCell.Elevation > Manager->SeaLevel && SCell.Elevation < 1000.0f && DCell.SurfaceWater < 0.1f && !SCell.bIsVolcano) {
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
        Tribe.TrackingConfidence = 0.0f;

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
    FCellStaticData SCell; FCellDynamicData DCell;
    if (!Manager->GetCellDataAtLocation(FVector(Tribe.Position.X, Tribe.Position.Y, 0.0f), SCell, DCell)) return;

    Tribe.Culture.Pillars.Add(ECulturalPillar::Sociability, 30.0f);
    Tribe.Culture.Pillars.Add(ECulturalPillar::Exploration, 20.0f);
    Tribe.Culture.Pillars.Add(ECulturalPillar::Ecology, 20.0f);

    if (SCell.Biome == EBiomeType::DeciduousForest || SCell.Biome == EBiomeType::ConiferousForest || SCell.Biome == EBiomeType::TropicalForest) {
        Tribe.Culture.Pillars[ECulturalPillar::Ecology] += 40.0f;
        Tribe.Culture.Pillars[ECulturalPillar::Spirituality] += 20.0f;
        Tribe.Knowledge.SubKnowledge.Add(TEXT("Forestry"), 5.0f);
        Tribe.Knowledge.SubKnowledge.Add(TEXT("Foraging"), 10.0f);
        Tribe.Knowledge.Levels[EKnowledgeField::Woodcraft] += 2.0f;
    }
    else if (SCell.Biome == EBiomeType::Desert || SCell.Biome == EBiomeType::Barren || SCell.Biome == EBiomeType::Tundra) {
        Tribe.Culture.Pillars[ECulturalPillar::Exploration] += 40.0f;
        Tribe.Culture.Pillars[ECulturalPillar::Commerce] += 20.0f;
        Tribe.Culture.Pillars[ECulturalPillar::Industry] += 10.0f;
        Tribe.Knowledge.SubKnowledge.Add(TEXT("WaterObservation"), 10.0f);
        Tribe.Knowledge.SubKnowledge.Add(TEXT("Survival"), 10.0f);
        Tribe.Knowledge.Levels[EKnowledgeField::Sociology] += 2.0f;
    }
    else if (SCell.Biome == EBiomeType::Beach || SCell.Biome == EBiomeType::Swamp) {
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

    if (DCell.SurfaceWater > 0.1f || DCell.RiverDischarge > 1.0f) {
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

    FCellStaticData CenterSCell; FCellDynamicData CenterDCell;
    if (!Manager->GetCellGlobal(CenterGX, CenterGY, CenterSCell, CenterDCell)) { return Score; }

    if (CenterSCell.Elevation <= Manager->SeaLevel) {
        Score.CampScore = -5000.0f;
        Score.CityScore = -5000.0f;
        return Score;
    }

    for (int32 dy = -5; dy <= 5; dy++) {
        for (int32 dx = -5; dx <= 5; dx++) {
            FCellStaticData SCell; FCellDynamicData DCell;
            if (Manager->GetCellGlobal(CenterGX + dx, CenterGY + dy, SCell, DCell)) {
                ValidCells++;

                TotalFood += DCell.BerryBushes + (DCell.FloraDensity * 10.0f);
                TotalWood += DCell.WoodAmount;
                if (SCell.Bedrock == EBedrockType::Rock) TotalStone += 10.0f;

                GroundWaterPotential += DCell.GroundWater * 0.1f;
                if (DCell.RiverDischarge > 0.1f) RiverReliability += DCell.RiverDischarge * 2.0f;

                if (DCell.SurfaceWater > 0.1f && DCell.SurfaceWater <= 1.5f && DCell.RiverDischarge > 0.5f) {
                    FordCells++;
                }

                if (DCell.RiverDischarge > 50.0f && FMath::Abs(SCell.Elevation - CenterSCell.Elevation) < 2.0f) {
                    FloodRisk += DCell.RiverDischarge * 0.5f;
                }

                if (SCell.bIsVolcano) PollutionPenalty += 500.0f;
                if (DCell.WaterPollution > 0.1f) PollutionPenalty += DCell.WaterPollution * 150.0f;
                if (DCell.HouseDensity > 0.0f) PollutionPenalty += 1000.0f;

                if (SCell.Elevation > 1500.0f) TerrainCost += 50.0f;
                if (DCell.DangerLevel > 0.5f) TerrainCost += 20.0f;

                if (SCell.Elevation > CenterSCell.Elevation + 5.0f && SCell.Elevation < CenterSCell.Elevation + 30.0f) Defense += 10.0f;

                if (FMath::Abs(SCell.Elevation - CenterSCell.Elevation) < 5.0f && DCell.SurfaceWater < 0.1f && DCell.WaterPollution < 0.2f) {
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

    if (Tribe) {
        float ExhaustionPenalty = Tribe->DaysAtCamp / 150.0f;
        float RequiredFoodForTribe = Tribe->Population * 3.0f;
        if (TotalFood < RequiredFoodForTribe && Tribe->State == ETribeState::Camping) {
            ExhaustionPenalty += 2.0f;
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

    FCellStaticData CurrentSCell; FCellDynamicData CurrentDCell;
    if (!Manager->GetCellGlobal(CurrentGX, CurrentGY, CurrentSCell, CurrentDCell)) { return GlobalDir; }

    for (int k = 0; k < 8; k++) {
        float Angle = (k * PI / 4.0f);
        FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
        FVector2D SamplePos = CurrentPos + Dir * 50.0f;

        int32 SampleGX = FMath::FloorToInt(SamplePos.X / 50.0f);
        int32 SampleGY = FMath::FloorToInt(SamplePos.Y / 50.0f);

        FCellStaticData SampleSCell; FCellDynamicData SampleDCell;
        if (Manager->GetCellGlobal(SampleGX, SampleGY, SampleSCell, SampleDCell)) {
            float Score = FVector2D::DotProduct(GlobalDir, Dir) * 10.0f;

            if (SampleDCell.SurfaceWater > 1.5f || SampleSCell.Elevation <= Manager->SeaLevel) {
                Score -= 5000.0f;
            }
            else if (SampleDCell.SurfaceWater > 0.1f && SampleDCell.SurfaceWater <= 1.5f) {
                Score -= 20.0f;
            }

            if (SampleSCell.bIsVolcano) Score -= 1500.0f;

            float Slope = SampleSCell.Elevation - CurrentSCell.Elevation;
            if (FMath::Abs(Slope) > 15.0f) Score -= FMath::Abs(Slope) * 2.0f;

            Score += SampleDCell.FloraDensity * 2.0f;
            Score *= FMath::FRandRange(0.9f, 1.1f);

            if (Score > BestScore) {
                BestScore = Score;
                BestStep = Dir;
            }
        }
    }

    if (BestScore < -100.0f) {
        FVector2D InlandStep = -GlobalDir;
        float HighestZ = CurrentSCell.Elevation;

        for (int k = 0; k < 8; k++) {
            float Angle = (k * PI / 4.0f);
            FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));

            int32 SampleGX = FMath::FloorToInt((CurrentPos.X + Dir.X * 50.0f) / 50.0f);
            int32 SampleGY = FMath::FloorToInt((CurrentPos.Y + Dir.Y * 50.0f) / 50.0f);

            FCellStaticData SampleSCell; FCellDynamicData SampleDCell;
            if (Manager->GetCellGlobal(SampleGX, SampleGY, SampleSCell, SampleDCell)) {
                if (SampleSCell.Elevation > HighestZ && SampleDCell.SurfaceWater < 0.8f) {
                    HighestZ = SampleSCell.Elevation;
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
                            bool bFoundHerdThisTick = false;
                            float TrackingSkill = Tribe.Knowledge.SubKnowledge.Contains(TEXT("AnimalTracking")) ? Tribe.Knowledge.SubKnowledge[TEXT("AnimalTracking")] : 0.0f;

                            if (TrackingSkill > 5.0f && AnimalGridPtr && (Tribe.Inventory.MeatFood < MaxFood * 0.8f)) {
                                int32 GCX = FMath::FloorToInt(Tribe.Position.X / (ChunkSize * CellSize));
                                int32 GCY = FMath::FloorToInt(Tribe.Position.Y / (ChunkSize * CellSize));
                                float MinDistSq = 9999999999.0f;

                                FVector2D BestHerdPos = Tribe.Position;
                                FVector2D BestHerdDir = FVector2D(1.0f, 0.0f);

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
                                    Tribe.bFollowingHerd = true;
                                    Tribe.LastKnownHerdPosition = BestHerdPos;
                                    Tribe.HerdDirection = BestHerdDir;
                                    Tribe.TrackingConfidence = 100.0f;

                                    float DistanceToHerd = FMath::Sqrt(MinDistSq);
                                    if (DistanceToHerd > 1000.0f) Tribe.TargetRegion = BestHerdPos;
                                    else Tribe.TargetRegion = BestHerdPos + BestHerdDir * 1500.0f;

                                    Tribe.NextStrategicDecisionDay = Manager->CurrentDay + 2.0;
                                }
                            }

                            if (!bFoundHerdThisTick && Tribe.TrackingConfidence > 0.0f) {
                                Tribe.TrackingConfidence -= 15.0f * DeltaTime;
                                Tribe.LastKnownHerdPosition += Tribe.HerdDirection * 30.0f * DeltaTime;

                                Tribe.TargetRegion = Tribe.LastKnownHerdPosition;
                                Tribe.bFollowingHerd = true;
                                Tribe.NextStrategicDecisionDay = Manager->CurrentDay + 1.0;
                            }
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
                    FCellStaticData CCellS; FCellDynamicData CCellD;
                    if (Manager->GetCellGlobal(GlobalX, GlobalY, CCellS, CCellD)) {
                        FinalCampScore = 0.2f;
                        if (CCellD.SurfaceWater < 0.5f && CCellS.Elevation > Manager->SeaLevel) FinalCampScore += 0.2f;
                        if (CCellD.FloraDensity > 0.3f || CCellS.TreeType != ETreeType::None) FinalCampScore += 0.2f;
                        if (CCellS.Biome == EBiomeType::Grassland || CCellS.Biome == EBiomeType::DeciduousForest) FinalCampScore += 0.1f;
                        if (CCellD.DangerLevel > 0.5f || CCellS.bIsVolcano) FinalCampScore -= 0.5f;
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
                FCellStaticData* WalkSCell = nullptr; FCellDynamicData* WalkDCell = nullptr; FIntPoint WalkCoord;
                if (Manager->GetMutableCellGlobal(GlobalX, GlobalY, WalkSCell, WalkDCell, WalkCoord)) {
                    if (WalkDCell->FloraDensity > 0.0f && Tribe.Inventory.FloraFood < MaxFood) {
                        float Gathered = FMath::Min(WalkDCell->FloraDensity, 1.0f * DeltaTime);
                        WalkDCell->FloraDensity -= Gathered;
                        Tribe.Inventory.FloraFood += Gathered * 2.0f;
                        Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Foraging")) += Gathered;
                        Manager->RegisterVisualChange(WalkCoord, EChunkVisualDirty::Flora);
                    }
                    if (WalkDCell->SurfaceWater > 0.1f) Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("RiverObservation")) += 0.5f * DeltaTime;
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

                                                FCellStaticData* TargetSCell = nullptr; FCellDynamicData* TargetDCell = nullptr; FIntPoint TCoord;
                                                int32 AnimalGX = FMath::FloorToInt(Animal.Position.X / CellSize);
                                                int32 AnimalGY = FMath::FloorToInt(Animal.Position.Y / CellSize);

                                                if (Manager->GetMutableCellGlobal(AnimalGX, AnimalGY, TargetSCell, TargetDCell, TCoord)) {
                                                    TargetDCell->AnimalBones += HuntAmount * 5.0f;
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
                            FCellStaticData* CCellS2 = nullptr; FCellDynamicData* CCellD2 = nullptr; FIntPoint CCoord2;
                            if (Manager->GetMutableCellGlobal(GlobalX + dx, GlobalY + dy, CCellS2, CCellD2, CCoord2)) {

                                if (Tribe.Knowledge.Levels.Contains(EKnowledgeField::Agriculture) && Tribe.Knowledge.Levels[EKnowledgeField::Agriculture] >= 5.0f) {
                                    if (CCellS2->BuildingType != EBuildingType::Farm && CCellS2->TreeType == ETreeType::None && CCellD2->SurfaceWater < 0.1f && CCellS2->Elevation > Manager->SeaLevel) {
                                        CCellS2->BuildingType = EBuildingType::Farm;
                                        Manager->RegisterVisualChange(CCoord2, EChunkVisualDirty::Flora);
                                    }
                                }

                                if (CCellS2->BuildingType == EBuildingType::Farm) {
                                    float FarmYield = 15.0f * FMath::Max(0.0f, 1.0f - CCellD2->WaterPollution) * DeltaTime;
                                    Gathered += FarmYield;
                                    Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("PlantCultivation")) += FarmYield;
                                }
                                else {
                                    if (CCellD2->FloraDensity > 0.0f) {
                                        CCellD2->FloraDensity = FMath::Max(0.0f, CCellD2->FloraDensity - (0.05f * DeltaTime));
                                        Manager->RegisterVisualChange(CCoord2, EChunkVisualDirty::Flora);
                                    }
                                    if (Tribe.Inventory.FloraFood < MaxFood) {
                                        if (CCellD2->BerryBushes > 0.0f) {
                                            float Take = FMath::Min(CCellD2->BerryBushes, 2.0f * DeltaTime);
                                            CCellD2->BerryBushes -= Take; Gathered += Take * 2.0f;
                                            Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Foraging")) += Take;
                                        }
                                        else if (CCellD2->FloraDensity > 0.1f) {
                                            float Take = 0.5f * DeltaTime;
                                            CCellD2->FloraDensity -= Take; Gathered += Take;
                                            Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Foraging")) += Take * 0.5f;
                                        }
                                    }
                                    if (CCellS2->TreeType != ETreeType::None && Tribe.Inventory.Wood < MaxWood) {
                                        float Chop = 1.0f * DeltaTime;
                                        CCellD2->WoodAmount = FMath::Max(0.0f, CCellD2->WoodAmount - Chop);
                                        Tribe.Inventory.Wood += Chop;
                                        Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("Woodcutting")) += Chop;
                                    }
                                }

                                if (dx == 0 && dy == 0) {
                                    if (Manager->TechModule) {
                                        float LearnRate = 0.8f * DeltaTime;
                                        switch (CCellS2->Biome) {
                                        case EBiomeType::DeciduousForest: case EBiomeType::ConiferousForest: case EBiomeType::TropicalForest: Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Woodcraft, LearnRate); break;
                                        case EBiomeType::Grassland: Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Agriculture, LearnRate); break;
                                        case EBiomeType::Tundra: case EBiomeType::Barren: Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Masonry, LearnRate); break;
                                        case EBiomeType::Desert: Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Academics, LearnRate); break;
                                        case EBiomeType::Swamp: case EBiomeType::Beach: Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Maritime, LearnRate); break;
                                        }
                                        if (CCellS2->Bedrock == EBedrockType::Rock || CCellS2->bIsVolcano) Manager->TechModule->GainKnowledge(Tribe.Knowledge, EKnowledgeField::Metallurgy, LearnRate * 0.8f);
                                    }

                                    if (CCellD2->SurfaceWater > 0.1f || CCellD2->RiverDischarge > 0.5f) {
                                        Tribe.Knowledge.SubKnowledge.FindOrAdd(TEXT("RiverObservation")) += 1.0f * DeltaTime;
                                    }
                                }
                            }
                        }
                    }

                    if (Manager->TechModule) {
                        FCellStaticData EvalSCell; FCellDynamicData EvalDCell;
                        if (Manager->GetCellGlobal(GlobalX, GlobalY, EvalSCell, EvalDCell)) {
                            Manager->TechModule->EvaluateEntityKnowledge(Tribe.Knowledge, Tribe.Culture, EvalSCell, EvalDCell, Manager, Tribe.TribeID, false, Tribe.Position);
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
            float CurrentCampScore = 0.0f;
            if (bIsDetailedLOD) {
                FRegionScore Region = EvaluateRegion(Manager, Tribe.Position, &Tribe);
                CurrentCampScore = Region.CampScore;
            }
            else {
                CurrentCampScore = 0.5f - (Tribe.DaysAtCamp / 150.0f);
            }

            if (FoodReserve < DailyConsumption * 2.0f || CurrentCampScore < 0.2f) {
                Tribe.State = ETribeState::Migrating;
                Tribe.CampDaysRemaining = 0;
                Tribe.DaysAtCamp = 0.0f;
                Tribe.NextStrategicDecisionDay = 0.0;
                Tribe.TrackingConfidence = 0.0f;
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
            Tribe.TrackingConfidence = 0.0f;

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