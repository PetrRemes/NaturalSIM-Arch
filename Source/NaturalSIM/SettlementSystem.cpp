#include "SettlementSystem.h"
#include "SimWorldManager.h"
#include "ManaSystem.h"
#include "HumanSystem.h"
#include "FaunaSystem.h"
#include "HistorySystem.h"
#include "Async/ParallelFor.h"
#include "Containers/Queue.h"

USettlementSystem::USettlementSystem() { PrimaryComponentTick.bCanEverTick = false; }
void USettlementSystem::BeginPlay() { Super::BeginPlay(); }

void USettlementSystem::CreateSettlement(FVector2D InPosition, int32 InPopulation, FTribeInventory InInventory, FLinearColor InColor, FKnowledgeContainer InKnowledge, FCultureProfile InCulture) {
    GlobalSettlementCounter++;
    FSettlementData NewCity;
    NewCity.SettlementID = GlobalSettlementCounter;
    NewCity.Position = InPosition;
    NewCity.Population = InPopulation;
    NewCity.Inventory = InInventory;
    NewCity.Color = InColor;
    NewCity.GatherRadius = 6;
    NewCity.SplitCount = 0;
    NewCity.Knowledge = InKnowledge;
    NewCity.Culture = InCulture;
    NewCity.bHasForcedExpansion = false;
    NewCity.ExpansionPoints = 50.0f;
    Settlements.Add(NewCity);
}

FLinearColor USettlementSystem::GetPoliticalColor(const FSettlementData& S) const {
    if (S.NationID != -1) {
        FRandomStream Stream(S.NationID * 12345);
        FLinearColor NatColor = FLinearColor(Stream.FRandRange(0.2f, 0.9f), Stream.FRandRange(0.2f, 0.9f), Stream.FRandRange(0.2f, 0.9f), 1.0f);
        return FLinearColor::LerpUsingHSV(S.Color, NatColor, 0.8f);
    }
    return S.Color;
}

void USettlementSystem::RecalculatePoliticalColors(ASimWorldManager* Manager) {
    if (!Manager) return;
    for (FSettlementData& S : Settlements) {
        FLinearColor NewColor = GetPoliticalColor(S);
        for (FIntPoint Coord : S.ClaimedCells) {
            FCellStaticData* SCell = nullptr; FCellDynamicData* DCell = nullptr; FIntPoint CC;
            if (Manager->GetMutableCellGlobal(Coord.X, Coord.Y, SCell, DCell, CC)) {
                if (SCell->PoliticalColor != NewColor || SCell->OwnerNationID != S.NationID) {
                    SCell->PoliticalColor = NewColor;
                    SCell->OwnerNationID = S.NationID;
                    Manager->RegisterVisualChange(CC, EChunkVisualDirty::Terrain);
                }
            }
        }
    }
}

void USettlementSystem::ProcessSettlements(TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, float DeltaTime) {
    if (!Manager) return;
    const float CellSize = 50.0f;
    bool bMadeBuildingChange = false;

    SettlementSpatialGrid.Reset();
    float InvGridSize = 1.0f / (Manager->ChunkSize * 50.0f);
    for (int32 i = 0; i < Settlements.Num(); i++) {
        int32 CX = FMath::FloorToInt(Settlements[i].Position.X * InvGridSize);
        int32 CY = FMath::FloorToInt(Settlements[i].Position.Y * InvGridSize);
        SettlementSpatialGrid.FindOrAdd(FIntPoint(CX, CY)).Add(i);
    }

    const TMap<FIntPoint, TArray<int32>>* AnimalGridPtr = nullptr;
    if (Manager->FaunaModule) {
        AnimalGridPtr = &Manager->FaunaModule->AnimalSpatialGrid;
    }

    const int32 SettlementCount = Settlements.Num();
    if (SettlementCount == 0) return;

    struct FAnimalHunt { int32 AnimalIndex; float AmountHerdReduced; float MeatGained; };
    struct FCellDelta {
        int32 GlobalX; int32 GlobalY;
        float WaterPollutionDelta = 0.0f;
        float HouseDensityDelta = 0.0f;
        float DangerLevelDelta = 0.0f;
        float WoodGatherAttempt = 0.0f;
        float FloraGatherAttempt = 0.0f;
        float AshDensityDelta = 0.0f;
        EBuildingType NewBuilding = EBuildingType::None;
        bool bSetBuilding = false;
        bool bClearTree = false;
    };

    struct FSettlementWork {
        int32 SettlementIndex;
        float HeatingNeeded = 0.0f;
        float ScannedFloraCap = 0.0f; float ScannedWoodCap = 0.0f; float ScannedWaterCap = 0.0f; float ScannedStoneCap = 0.0f;
        int32 NumBlacksmiths = 0; int32 NumMarkets = 0; int32 NumFarms = 0; int32 NumLumberCamps = 0; int32 NumMines = 0;
        int32 NumCastles = 0; int32 NumPorts = 0; int32 NumFactories = 0; int32 NumAirports = 0; int32 NumEcoFarms = 0;
        int32 NumPowerPlantsCoal = 0; int32 NumPowerPlantsNuke = 0; int32 NumForestryCenters = 0; int32 NumAICenters = 0;
        int32 NumOilRigs = 0; int32 NumLogisticsCenters = 0;

        bool bNearVolcano = false;
        int32 PredatorKills = 0;

        TArray<FAnimalHunt> Hunts; TArray<FCellDelta> CellDeltas;
        TArray<FIntPoint> NewRoads;
    };

    TArray<FSettlementWork> Works; Works.SetNum(SettlementCount);
    for (int32 i = 0; i < SettlementCount; ++i) Works[i].SettlementIndex = i;

    ParallelFor(SettlementCount, [&](int32 idx) {
        FSettlementWork& Work = Works[idx];
        FSettlementData& LocalCity = Settlements[idx];

        int32 GlobalCX = FMath::FloorToInt(LocalCity.Position.X / CellSize);
        int32 GlobalCY = FMath::FloorToInt(LocalCity.Position.Y / CellSize);

        int32 PopPerHouse = FMath::Max(1, Manager->PopulationPerHouse);
        int32 BuiltHouses = FMath::Max(1, LocalCity.Population / PopPerHouse);

        bool bCanFarm = LocalCity.Knowledge.Levels.Contains(EKnowledgeField::Agriculture) && LocalCity.Knowledge.Levels[EKnowledgeField::Agriculture] >= 2.0f;
        bool bCanMine = LocalCity.Knowledge.Levels.Contains(EKnowledgeField::Masonry) && LocalCity.Knowledge.Levels[EKnowledgeField::Masonry] >= 2.0f;
        bool bCanSmelt = LocalCity.Knowledge.UnlockedTechnologies.Contains("Smelting");
        bool bCanTrade = LocalCity.Knowledge.UnlockedTechnologies.Contains("Currency");
        bool bHasIrrigation = LocalCity.Knowledge.UnlockedTechnologies.Contains("Irrigation");

        bool bCanDefend = LocalCity.Knowledge.UnlockedTechnologies.Contains("Fortifications");
        bool bCanSail = LocalCity.Knowledge.UnlockedTechnologies.Contains("Naval Engineering");
        bool bCanIndustry = LocalCity.Knowledge.UnlockedTechnologies.Contains("Industrial Mass Production");
        bool bCanFly = LocalCity.Knowledge.UnlockedTechnologies.Contains("Aviation");
        bool bCanNuke = LocalCity.Knowledge.UnlockedTechnologies.Contains("Nuclear Fission");
        bool bCanEco = LocalCity.Knowledge.UnlockedTechnologies.Contains("Sustainable Infrastructure");
        bool bCanAI = LocalCity.Knowledge.UnlockedTechnologies.Contains("Artificial Intelligence");
        bool bCanExtractOil = LocalCity.Knowledge.UnlockedTechnologies.Contains("Combustion Engine");

        int32 TargetFarms = bCanFarm ? FMath::Max(1, LocalCity.Population / 20) : 0;
        int32 TargetEcoFarms = bCanEco ? FMath::Max(1, LocalCity.Population / 15) : 0;
        if (bCanEco) TargetFarms = 0;

        int32 TargetLumber = FMath::Max(1, LocalCity.Population / 40);
        int32 TargetForestry = bCanEco ? FMath::Max(1, LocalCity.Population / 30) : 0;
        if (bCanEco) TargetLumber = 0;

        int32 TargetMines = bCanMine ? FMath::Max(1, LocalCity.Population / 60) : 0;
        int32 TargetBlacksmiths = bCanSmelt && !bCanIndustry ? FMath::Max(1, LocalCity.Population / 100) : 0;
        int32 TargetMarkets = bCanTrade ? FMath::Max(1, LocalCity.Population / 150) : 0;
        int32 TargetLogistics = (bCanTrade && LocalCity.Population > 1000) ? FMath::Max(1, LocalCity.Population / 1000) : 0;

        int32 TargetCastles = bCanDefend ? 1 : 0;
        int32 TargetPorts = bCanSail ? FMath::Max(1, LocalCity.Population / 500) : 0;
        int32 TargetFactories = bCanIndustry ? FMath::Max(1, LocalCity.Population / 300) : 0;
        int32 TargetPowerCoal = bCanIndustry && !bCanEco ? FMath::Max(1, LocalCity.Population / 1000) : 0;
        int32 TargetPowerNuke = bCanNuke ? FMath::Max(1, LocalCity.Population / 3000) : 0;
        int32 TargetAirports = bCanFly && LocalCity.Population > 1500 ? 1 : 0;
        int32 TargetAICenters = bCanAI && LocalCity.Population > 5000 ? 1 : 0;
        int32 TargetOilRigs = bCanExtractOil ? FMath::Max(1, LocalCity.Population / 800) : 0;

        int32 BufferCells = 15 + (LocalCity.Population / 15);
        int32 TargetCells = FMath::Clamp(BuiltHouses + TargetFarms + TargetEcoFarms + BufferCells, 10, 300);

        Work.CellDeltas.Reserve(TargetCells);
        Work.Hunts.Reserve(8);

        const float MaxMeatCap = LocalCity.Population * 100.0f;
        if (Manager->FaunaModule && LocalCity.Inventory.MeatFood < MaxMeatCap) {
            float InvGridSizeAnimal = 1.0f / (Manager->ChunkSize * CellSize);
            int32 GCX = FMath::FloorToInt(LocalCity.Position.X * InvGridSizeAnimal);
            int32 GCY = FMath::FloorToInt(LocalCity.Position.Y * InvGridSizeAnimal);
            const float HuntRadiusSq = (LocalCity.GatherRadius * 50.0f) * (LocalCity.GatherRadius * 50.0f);

            for (int32 dx = -1; dx <= 1; ++dx) {
                for (int32 dy = -1; dy <= 1; ++dy) {
                    if (AnimalGridPtr) {
                        if (const TArray<int32>* CellAnimals = AnimalGridPtr->Find(FIntPoint(GCX + dx, GCY + dy))) {
                            for (int32 aIdx : *CellAnimals) {
                                if (!Manager->FaunaModule->Animals.IsValidIndex(aIdx)) continue;
                                const FAnimalData& Animal = Manager->FaunaModule->Animals[aIdx];
                                if (Animal.HerdSize <= 2.0f) continue;
                                if (FVector2D::DistSquared(LocalCity.Position, Animal.Position) >= HuntRadiusSq) continue;

                                float HuntAmount = FMath::Min(LocalCity.Population * 0.2f * DeltaTime, FMath::Min(Animal.HerdSize - 2.0f, Animal.HerdSize * 0.1f));
                                if (HuntAmount > 0.0f) {
                                    FAnimalHunt AH; AH.AnimalIndex = aIdx; AH.AmountHerdReduced = HuntAmount; AH.MeatGained = HuntAmount * 15.0f;
                                    Work.Hunts.Add(AH);
                                    if (Animal.Type == EAnimalType::Predator) Work.PredatorKills++;
                                }
                            }
                        }
                    }
                }
            }
        }

        Work.HeatingNeeded = LocalCity.Population * 0.05f * DeltaTime;
        if (bCanNuke) Work.HeatingNeeded *= 0.1f;

        TArray<FIntPoint> PotentialFarms;
        TArray<FIntPoint> PotentialLumberCamps;
        TArray<FIntPoint> PotentialMines;
        TArray<FIntPoint> PotentialBlacksmiths;
        TArray<FIntPoint> PotentialMarkets;
        TArray<FIntPoint> PotentialCastles;
        TArray<FIntPoint> PotentialPorts;
        TArray<FIntPoint> PotentialFactories;
        TArray<FIntPoint> PotentialAirports;
        TArray<FIntPoint> PotentialNuclear;
        TArray<FIntPoint> PotentialOilRigs;

        int32 HousesPlaced = 0;
        FIntPoint LastChunkCoord(-9999, -9999);
        const FChunkData* LastChunk = nullptr;

        auto GetFastCell = [&](int32 GX, int32 GY, const FCellStaticData*& OutS, const FCellDynamicData*& OutD) -> bool {
            if (GX < 0 || GY < 0) return false;
            int32 CX = GX / (Manager->ChunkSize - 1);
            int32 CY = GY / (Manager->ChunkSize - 1);
            FIntPoint CCoord(CX, CY);

            if (CCoord != LastChunkCoord) {
                LastChunkCoord = CCoord;
                LastChunk = WorldChunks.Find(CCoord);
            }
            if (LastChunk) {
                int32 LX = GX % (Manager->ChunkSize - 1);
                int32 LY = GY % (Manager->ChunkSize - 1);
                int32 Idx = LX + LY * Manager->ChunkSize;
                OutS = &LastChunk->StaticCells[Idx];
                OutD = &LastChunk->DynamicCells[Idx];
                return true;
            }
            return false;
            };

        float CurrentTools = LocalCity.Inventory.Tools;
        float ToolBonus = 1.0f;
        if (CurrentTools > 0.0f) {
            ToolBonus = 1.0f + (FMath::Clamp(CurrentTools / FMath::Max(1.0f, (float)LocalCity.Population), 0.0f, 1.0f) * 2.0f);
        }

        float BaseFarmYield = bHasIrrigation ? 120.0f : 50.0f;
        float CenterElevation = 0.0f;
        const FCellStaticData* CenterSCell = nullptr; const FCellDynamicData* CenterDCell = nullptr;
        if (GetFastCell(GlobalCX, GlobalCY, CenterSCell, CenterDCell)) CenterElevation = CenterSCell->Elevation;

        for (FIntPoint Coord : LocalCity.ClaimedCells) {
            const FCellStaticData* SCell = nullptr; const FCellDynamicData* DCell = nullptr;
            if (!GetFastCell(Coord.X, Coord.Y, SCell, DCell)) continue;

            Work.ScannedFloraCap += DCell->FloraDensity * 10.0f + DCell->BerryBushes;
            Work.ScannedWoodCap += DCell->WoodAmount;
            if (DCell->SurfaceWater > 0.0f || DCell->RiverDischarge > 10.0f) Work.ScannedWaterCap += 10.0f;
            if (SCell->Bedrock == EBedrockType::Rock) Work.ScannedStoneCap += 10.0f;

            if (SCell->bIsVolcano || DCell->Lava > 0.1f) Work.bNearVolcano = true;

            FCellDelta Delta;
            Delta.GlobalX = Coord.X; Delta.GlobalY = Coord.Y;

            if (DCell->SurfaceWater > (SCell->ChannelDepth + SCell->BankHeight + 0.5f)) {
                if (DCell->HouseDensity > 0.0f) {
                    Delta.HouseDensityDelta = -DCell->HouseDensity * 0.5f;
                }
                if (SCell->BuildingType != EBuildingType::None && SCell->BuildingType != EBuildingType::Port) {
                    if (FMath::FRand() < 0.2f * DeltaTime) {
                        Delta.NewBuilding = EBuildingType::None;
                        Delta.bSetBuilding = true;
                    }
                }
            }
            else {
                if (HousesPlaced < BuiltHouses && DCell->SurfaceWater < 1.0f && SCell->Elevation > Manager->SeaLevel && SCell->BuildingType == EBuildingType::None) {
                    if (DCell->HouseDensity < 1.0f) Delta.HouseDensityDelta += 0.2f * DeltaTime;
                    if (SCell->TreeType != ETreeType::None) Delta.bClearTree = true;
                    HousesPlaced++;
                }
            }

            if (SCell->BuildingType == EBuildingType::Farm) {
                Work.NumFarms++;
                if (bCanEco) {
                    Delta.NewBuilding = EBuildingType::EcoFarm; Delta.bSetBuilding = true;
                }
                else {
                    float ActualYield = BaseFarmYield;
                    if (bHasIrrigation && DCell->GroundWater < 50.0f && DCell->SurfaceWater < 0.1f) ActualYield = 40.0f;
                    float Yield = ActualYield * ToolBonus * FMath::Max(0.0f, 1.0f - DCell->WaterPollution) * DeltaTime;
                    Delta.FloraGatherAttempt += Yield;
                    Delta.WaterPollutionDelta += 0.005f * DeltaTime;
                }
            }
            else if (SCell->BuildingType == EBuildingType::EcoFarm) {
                Work.NumEcoFarms++;
                float Yield = 250.0f * ToolBonus * DeltaTime;
                Delta.FloraGatherAttempt += Yield;
                Delta.WaterPollutionDelta -= 0.01f * DeltaTime;
            }
            else if (SCell->BuildingType == EBuildingType::LumberCamp) {
                Work.NumLumberCamps++;
                if (bCanEco) {
                    Delta.NewBuilding = EBuildingType::ForestryCenter; Delta.bSetBuilding = true;
                }
                else {
                    float Yield = FMath::Min(DCell->WoodAmount, 100.0f * ToolBonus * DeltaTime);
                    Delta.WoodGatherAttempt += Yield;
                }
            }
            else if (SCell->BuildingType == EBuildingType::ForestryCenter) {
                Work.NumForestryCenters++;
                float Yield = 80.0f * ToolBonus * DeltaTime;
                Delta.WoodGatherAttempt += Yield * 0.1f;
            }
            else if (SCell->BuildingType == EBuildingType::Mine) {
                Work.NumMines++;
                Delta.DangerLevelDelta += 0.05f * DeltaTime;
                Delta.WaterPollutionDelta += 0.15f * DeltaTime;
            }
            else if (SCell->BuildingType == EBuildingType::Blacksmith) {
                Work.NumBlacksmiths++;
                Delta.WaterPollutionDelta += 0.15f * DeltaTime;
                Delta.AshDensityDelta += 1.0f * DeltaTime;
            }
            else if (SCell->BuildingType == EBuildingType::Market) {
                Work.NumMarkets++;
            }
            else if (SCell->BuildingType == EBuildingType::LogisticsCenter) {
                Work.NumLogisticsCenters++;
            }
            else if (SCell->BuildingType == EBuildingType::Castle) {
                Work.NumCastles++;
            }
            else if (SCell->BuildingType == EBuildingType::Port) {
                Work.NumPorts++;
                Delta.WaterPollutionDelta += 0.1f * DeltaTime;
                Delta.FloraGatherAttempt += 150.0f * ToolBonus * DeltaTime;
            }
            else if (SCell->BuildingType == EBuildingType::Factory) {
                Work.NumFactories++;
            }
            else if (SCell->BuildingType == EBuildingType::PowerPlant_Coal) {
                Work.NumPowerPlantsCoal++;
                Delta.WaterPollutionDelta += 0.3f * DeltaTime;
                Delta.AshDensityDelta += 3.5f * DeltaTime;
            }
            else if (SCell->BuildingType == EBuildingType::PowerPlant_Nuclear) {
                Work.NumPowerPlantsNuke++;
                Delta.DangerLevelDelta += 0.02f * DeltaTime;
            }
            else if (SCell->BuildingType == EBuildingType::Airport) {
                Work.NumAirports++;
                Delta.AshDensityDelta += 0.5f * DeltaTime;
            }
            else if (SCell->BuildingType == EBuildingType::AICenter) {
                Work.NumAICenters++;
            }
            else if (SCell->BuildingType == EBuildingType::OilRig) {
                Work.NumOilRigs++;
                Delta.WaterPollutionDelta += 0.2f * DeltaTime;
            }
            else if (DCell->HouseDensity == 0.0f && SCell->BuildingType == EBuildingType::None) {
                if (DCell->SurfaceWater < 0.1f) {
                    if (SCell->Elevation < Manager->SeaLevel + 400.0f) {
                        PotentialFarms.Add(Coord);
                        if (bCanSmelt) PotentialBlacksmiths.Add(Coord);
                        if (bCanTrade) PotentialMarkets.Add(Coord);
                        if (bCanIndustry && SCell->Elevation > Manager->SeaLevel + 15.0f) PotentialFactories.Add(Coord);

                        if (bCanFly && FMath::Abs(SCell->Elevation - CenterElevation) < 5.0f) PotentialAirports.Add(Coord);
                    }

                    if (SCell->TreeType != ETreeType::None && DCell->WoodAmount > 50.0f) PotentialLumberCamps.Add(Coord);
                    if (SCell->Bedrock == EBedrockType::Rock || SCell->Elevation > Manager->SeaLevel + 500.0f) PotentialMines.Add(Coord);
                    if (bCanDefend && SCell->Elevation > CenterElevation + 20.0f) PotentialCastles.Add(Coord);
                }

                if (SCell->Elevation < Manager->SeaLevel + 15.0f && DCell->SurfaceWater < 0.1f) {
                    for (int n = 0; n < 4; n++) {
                        int32 Offsets[4][2] = { {0,1}, {1,0}, {0,-1}, {-1,0} };
                        const FCellStaticData* NC_S = nullptr; const FCellDynamicData* NC_D = nullptr;
                        if (GetFastCell(Coord.X + Offsets[n][0], Coord.Y + Offsets[n][1], NC_S, NC_D)) {
                            if (NC_S->Elevation <= Manager->SeaLevel || NC_D->SurfaceWater > 0.5f) {
                                if (bCanSail) PotentialPorts.Add(Coord);
                                if (bCanNuke) PotentialNuclear.Add(Coord);
                                break;
                            }
                        }
                    }
                }

                if (bCanExtractOil && (SCell->Biome == EBiomeType::Desert || SCell->WaterType == EWaterType::Ocean)) {
                    PotentialOilRigs.Add(Coord);
                }

                if (DCell->BerryBushes > 0.0f && DCell->SurfaceWater < 0.1f) {
                    float Pick = FMath::Min(DCell->BerryBushes, 5.0f * DeltaTime);
                    Delta.FloraGatherAttempt += Pick;
                }
            }

            if (Delta.HouseDensityDelta != 0.0f || Delta.WoodGatherAttempt > 0.0f || Delta.FloraGatherAttempt > 0.0f || Delta.DangerLevelDelta > 0.0f || Delta.WaterPollutionDelta != 0.0f || Delta.AshDensityDelta > 0.0f || Delta.bSetBuilding) {
                Work.CellDeltas.Add(Delta);
            }
        }

        auto BuildOrganicRoad = [&](FIntPoint Target) {
            TMap<FIntPoint, FIntPoint> CameFrom;
            TMap<FIntPoint, float> GScore;
            TArray<FIntPoint> OpenSet;

            OpenSet.Add(Target);
            GScore.Add(Target, 0.0f);

            FIntPoint EndPos(GlobalCX, GlobalCY);
            bool bFound = false;
            FIntPoint LastNode = Target;

            int32 Iters = 0;
            while (OpenSet.Num() > 0 && Iters < 500) {
                Iters++;
                int32 BestIdx = 0;
                float BestF = 999999.0f;
                for (int i = 0; i < OpenSet.Num(); i++) {
                    float f = GScore[OpenSet[i]] + FVector2D::Distance(FVector2D(OpenSet[i]), FVector2D(EndPos)) * 1.5f;
                    if (f < BestF) { BestF = f; BestIdx = i; }
                }

                FIntPoint Curr = OpenSet[BestIdx];
                OpenSet.RemoveAtSwap(BestIdx);

                const FCellStaticData* CCellS = nullptr; const FCellDynamicData* CCellD = nullptr;
                if (!GetFastCell(Curr.X, Curr.Y, CCellS, CCellD)) continue;

                if (Curr == EndPos || (CCellS->bHasRoad && Curr != Target)) {
                    bFound = true;
                    LastNode = Curr;
                    break;
                }

                FIntPoint Offsets[8] = { {1,0}, {0,1}, {-1,0}, {0,-1}, {1,1}, {-1,1}, {-1,-1}, {1,-1} };
                for (int i = 0; i < 8; i++) {
                    FIntPoint N = Curr + Offsets[i];
                    const FCellStaticData* NCS = nullptr; const FCellDynamicData* NCD = nullptr;
                    if (!GetFastCell(N.X, N.Y, NCS, NCD)) continue;

                    if (NCD->SurfaceWater >= 0.5f || NCS->Elevation <= Manager->SeaLevel) continue;

                    float MoveCost = (i < 4) ? 1.0f : 1.414f;
                    float ElevDiff = FMath::Abs(NCS->Elevation - CCellS->Elevation);
                    if (ElevDiff > 8.0f) continue;

                    float TerrainCost = MoveCost * 10.0f;
                    TerrainCost += ElevDiff * 5.0f;
                    if (NCS->TreeType != ETreeType::None) TerrainCost += 2.0f;
                    if (NCS->Biome == EBiomeType::Swamp) TerrainCost += 20.0f;

                    if (NCS->bHasRoad) TerrainCost *= 0.1f;

                    float TentativeG = GScore[Curr] + TerrainCost;
                    if (!GScore.Contains(N) || TentativeG < GScore[N]) {
                        CameFrom.Add(N, Curr);
                        GScore.Add(N, TentativeG);
                        if (!OpenSet.Contains(N)) OpenSet.Add(N);
                    }
                }
            }

            FIntPoint PathCurr = LastNode;
            while (CameFrom.Contains(PathCurr)) {
                if (PathCurr != EndPos && PathCurr != Target) {
                    const FCellStaticData* PathCellS = nullptr; const FCellDynamicData* PathCellD = nullptr;
                    if (GetFastCell(PathCurr.X, PathCurr.Y, PathCellS, PathCellD) && !PathCellS->bHasRoad) {
                        Work.NewRoads.AddUnique(PathCurr);
                    }
                }
                PathCurr = CameFrom[PathCurr];
            }
            };

        if (Work.NumFarms < TargetFarms && PotentialFarms.Num() > 0 && bCanFarm && !bCanEco) {
            FIntPoint Target = PotentialFarms[FMath::RandRange(0, PotentialFarms.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::Farm; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumEcoFarms < TargetEcoFarms && PotentialFarms.Num() > 0 && bCanEco) {
            FIntPoint Target = PotentialFarms[FMath::RandRange(0, PotentialFarms.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::EcoFarm; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumLumberCamps < TargetLumber && PotentialLumberCamps.Num() > 0 && !bCanEco) {
            FIntPoint Target = PotentialLumberCamps[FMath::RandRange(0, PotentialLumberCamps.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::LumberCamp; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumForestryCenters < TargetForestry && PotentialLumberCamps.Num() > 0 && bCanEco) {
            FIntPoint Target = PotentialLumberCamps[FMath::RandRange(0, PotentialLumberCamps.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::ForestryCenter; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumMines < TargetMines && PotentialMines.Num() > 0 && bCanMine) {
            FIntPoint Target = PotentialMines[FMath::RandRange(0, PotentialMines.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::Mine; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumBlacksmiths < TargetBlacksmiths && PotentialBlacksmiths.Num() > 0) {
            FIntPoint Target = PotentialBlacksmiths[FMath::RandRange(0, PotentialBlacksmiths.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::Blacksmith; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumMarkets < TargetMarkets && PotentialMarkets.Num() > 0) {
            FIntPoint Target = PotentialMarkets[FMath::RandRange(0, PotentialMarkets.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::Market; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumLogisticsCenters < TargetLogistics && PotentialMarkets.Num() > 0) {
            FIntPoint Target = PotentialMarkets[FMath::RandRange(0, PotentialMarkets.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::LogisticsCenter; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumCastles < TargetCastles && PotentialCastles.Num() > 0) {
            FIntPoint Target = PotentialCastles[FMath::RandRange(0, PotentialCastles.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::Castle; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumPorts < TargetPorts && PotentialPorts.Num() > 0) {
            FIntPoint Target = PotentialPorts[FMath::RandRange(0, PotentialPorts.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::Port; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumFactories < TargetFactories && PotentialFactories.Num() > 0) {
            FIntPoint Target = PotentialFactories[FMath::RandRange(0, PotentialFactories.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::Factory; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumPowerPlantsCoal < TargetPowerCoal && PotentialFactories.Num() > 0) {
            FIntPoint Target = PotentialFactories[FMath::RandRange(0, PotentialFactories.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::PowerPlant_Coal; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumPowerPlantsNuke < TargetPowerNuke && PotentialNuclear.Num() > 0) {
            FIntPoint Target = PotentialNuclear[FMath::RandRange(0, PotentialNuclear.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::PowerPlant_Nuclear; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumAirports < TargetAirports && PotentialAirports.Num() > 0) {
            FIntPoint Target = PotentialAirports[FMath::RandRange(0, PotentialAirports.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::Airport; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumAICenters < TargetAICenters && PotentialMarkets.Num() > 0) {
            FIntPoint Target = PotentialMarkets[FMath::RandRange(0, PotentialMarkets.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::AICenter; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        if (Work.NumOilRigs < TargetOilRigs && PotentialOilRigs.Num() > 0) {
            FIntPoint Target = PotentialOilRigs[FMath::RandRange(0, PotentialOilRigs.Num() - 1)];
            FCellDelta Delta; Delta.GlobalX = Target.X; Delta.GlobalY = Target.Y;
            Delta.NewBuilding = EBuildingType::OilRig; Delta.bSetBuilding = true;
            Work.CellDeltas.Add(Delta); BuildOrganicRoad(Target);
        }
        });

    TSet<FIntPoint> DirtyChunks;

    for (const FSettlementWork& Work : Works) {
        if (!Settlements.IsValidIndex(Work.SettlementIndex)) continue;
        FSettlementData& City = Settlements[Work.SettlementIndex];

        City.LocalFoodPotential = Work.ScannedFloraCap;
        City.LocalWoodPotential = Work.ScannedWoodCap;
        City.LocalWaterPotential = Work.ScannedWaterCap;
        City.LocalStonePotential = Work.ScannedStoneCap;

        float ActualWoodGained = 0.0f;
        float ActualFloraGained = 0.0f;
        float ActualStoneGained = 0.0f;
        float ActualOreGained = 0.0f;
        float ActualOilGained = 0.0f;
        float ActualUraniumGained = 0.0f;

        float ToolBonus = 1.0f + (FMath::Clamp(City.Inventory.Tools / FMath::Max(1.0f, (float)City.Population), 0.0f, 1.0f) * 2.0f);
        if (City.Knowledge.UnlockedTechnologies.Contains("Artificial Intelligence")) ToolBonus *= 4.0f;

        float MaintenanceWealth = 0.0f;
        float MaintenanceTools = 0.0f;

        MaintenanceWealth += Work.NumCastles * 20.0f * DeltaTime;
        MaintenanceWealth += Work.NumPorts * 30.0f * DeltaTime;
        MaintenanceWealth += Work.NumFactories * 100.0f * DeltaTime;
        MaintenanceTools += Work.NumFactories * 5.0f * DeltaTime;
        MaintenanceWealth += Work.NumPowerPlantsCoal * 150.0f * DeltaTime;
        MaintenanceWealth += Work.NumPowerPlantsNuke * 500.0f * DeltaTime;
        MaintenanceWealth += Work.NumAirports * 300.0f * DeltaTime;
        MaintenanceWealth += Work.NumOilRigs * 100.0f * DeltaTime;
        MaintenanceWealth += Work.NumAICenters * 1000.0f * DeltaTime;

        if (City.Inventory.Wealth >= MaintenanceWealth && City.Inventory.Tools >= MaintenanceTools) {
            City.Inventory.Wealth -= MaintenanceWealth;
            City.Inventory.Tools -= MaintenanceTools;
        }
        else {
            ToolBonus *= 0.2f;
            City.Inventory.Wealth = FMath::Max(0.0f, City.Inventory.Wealth - MaintenanceWealth);
            City.Inventory.Tools = FMath::Max(0.0f, City.Inventory.Tools - MaintenanceTools);
            City.EcologicalPressure += 10.0f * DeltaTime;
        }

        float DayPollution = 0.0f;
        int32 BuildingsDestroyedByFlood = 0;

        for (const FCellDelta& CD : Work.CellDeltas) {
            FCellStaticData* TargetSCell = nullptr; FCellDynamicData* TargetDCell = nullptr; FIntPoint ChunkC;
            if (Manager->GetMutableCellGlobal(CD.GlobalX, CD.GlobalY, TargetSCell, TargetDCell, ChunkC)) {

                TargetDCell->WaterPollution = FMath::Clamp(TargetDCell->WaterPollution + CD.WaterPollutionDelta, 0.0f, 1.0f);
                TargetDCell->DangerLevel = FMath::Clamp(TargetDCell->DangerLevel + CD.DangerLevelDelta, 0.0f, 1.0f);
                DayPollution += CD.WaterPollutionDelta + CD.AshDensityDelta;

                if (CD.AshDensityDelta > 0.0f) {
                    TargetDCell->AshDensityBuffer += CD.AshDensityDelta;
                    Manager->RegisterVisualChange(ChunkC, EChunkVisualDirty::Cloud);
                }

                if (CD.HouseDensityDelta != 0.0f) {
                    TargetDCell->HouseDensity = FMath::Clamp(TargetDCell->HouseDensity + CD.HouseDensityDelta, 0.0f, 1.0f);
                    Manager->RegisterVisualChange(ChunkC, EChunkVisualDirty::Terrain);
                    bMadeBuildingChange = true;
                    if (CD.HouseDensityDelta < 0.0f) BuildingsDestroyedByFlood++;
                }

                if (CD.bSetBuilding) {
                    TargetSCell->BuildingType = CD.NewBuilding;
                    if (CD.NewBuilding == EBuildingType::Farm || CD.NewBuilding == EBuildingType::Blacksmith || CD.NewBuilding == EBuildingType::Market ||
                        CD.NewBuilding == EBuildingType::Factory || CD.NewBuilding == EBuildingType::PowerPlant_Coal || CD.NewBuilding == EBuildingType::Airport ||
                        CD.NewBuilding == EBuildingType::Castle || CD.NewBuilding == EBuildingType::PowerPlant_Nuclear || CD.NewBuilding == EBuildingType::AICenter ||
                        CD.NewBuilding == EBuildingType::OilRig || CD.NewBuilding == EBuildingType::LogisticsCenter) {
                        TargetSCell->TreeType = ETreeType::None;
                        TargetDCell->WoodAmount = 0.0f;
                        TargetDCell->FloraDensity = 0.0f;
                    }
                    if (CD.NewBuilding == EBuildingType::None) BuildingsDestroyedByFlood++;
                    Manager->RegisterVisualChange(ChunkC, EChunkVisualDirty::Terrain | EChunkVisualDirty::Flora);
                    bMadeBuildingChange = true;
                }

                if (CD.WoodGatherAttempt > 0.0f) {
                    float EfficentChop = FMath::Min(TargetDCell->WoodAmount, CD.WoodGatherAttempt);
                    if (EfficentChop > 0.0f) {
                        TargetDCell->WoodAmount -= EfficentChop;
                        ActualWoodGained += EfficentChop * 0.8f;
                        if (TargetDCell->WoodAmount <= 0.0f) TargetSCell->TreeType = ETreeType::None;
                        Manager->RegisterVisualChange(ChunkC, EChunkVisualDirty::Flora);
                    }
                }

                if (TargetSCell->BuildingType == EBuildingType::Mine) {
                    float Yield = 50.0f * ToolBonus * DeltaTime;
                    ActualStoneGained += Yield;
                    ActualOreGained += Yield * 0.4f;

                    if (TargetSCell->Elevation > Manager->SeaLevel + 1500.0f) {
                        ActualUraniumGained += 5.0f * ToolBonus * DeltaTime;
                    }
                }

                if (TargetSCell->BuildingType == EBuildingType::OilRig) {
                    ActualOilGained += 150.0f * ToolBonus * DeltaTime;
                }

                if (CD.bClearTree && TargetSCell->TreeType != ETreeType::None) {
                    ActualWoodGained += TargetDCell->WoodAmount * 0.8f;
                    TargetSCell->TreeType = ETreeType::None; TargetDCell->FloraDensity *= 0.2f; TargetDCell->WoodAmount = 0.0f;
                    Manager->RegisterVisualChange(ChunkC, EChunkVisualDirty::Flora);
                }

                if (CD.FloraGatherAttempt > 0.0f) {
                    if (TargetSCell->BuildingType == EBuildingType::Farm || TargetSCell->BuildingType == EBuildingType::EcoFarm || TargetSCell->BuildingType == EBuildingType::Port) {
                        ActualFloraGained += CD.FloraGatherAttempt;
                    }
                    else if (TargetDCell->BerryBushes > 0.0f) {
                        float Pick = FMath::Min(TargetDCell->BerryBushes, CD.FloraGatherAttempt);
                        TargetDCell->BerryBushes -= Pick; ActualFloraGained += Pick * 2.0f;
                    }
                }

                if (CD.WaterPollutionDelta != 0.0f || CD.DangerLevelDelta != 0.0f) Manager->RegisterVisualChange(ChunkC, EChunkVisualDirty::Flora);
                DirtyChunks.Add(ChunkC);
            }
        }

        if (BuildingsDestroyedByFlood > 0) {
            float KillRatio = FMath::Clamp(BuildingsDestroyedByFlood * 0.05f, 0.0f, 0.5f);
            City.Population = FMath::Max(0, FMath::RoundToInt(City.Population * (1.0f - KillRatio)));
            City.Inventory.FloraFood *= (1.0f - KillRatio);
            City.Inventory.Wood *= (1.0f - KillRatio);
        }

        for (FIntPoint RCoord : Work.NewRoads) {
            FCellStaticData* RCellS = nullptr; FCellDynamicData* RCellD = nullptr; FIntPoint RCC;
            if (Manager->GetMutableCellGlobal(RCoord.X, RCoord.Y, RCellS, RCellD, RCC)) {
                if (RCellS->OwnerSettlementID == City.SettlementID && !RCellS->bHasRoad && RCellD->SurfaceWater < 0.5f) {
                    RCellS->bHasRoad = true;
                    if (RCellS->TreeType != ETreeType::None) { RCellS->TreeType = ETreeType::None; RCellD->WoodAmount = 0.0f; }
                    Manager->RegisterVisualChange(RCC, EChunkVisualDirty::Terrain | EChunkVisualDirty::Flora);
                }
            }
        }

        float ActualMeatGained = 0.0f;
        if (Manager->FaunaModule) {
            for (const FAnimalHunt& AH : Work.Hunts) {
                if (!Manager->FaunaModule->Animals.IsValidIndex(AH.AnimalIndex)) continue;
                FAnimalData& Animal = Manager->FaunaModule->Animals[AH.AnimalIndex];
                const float ActualReduce = FMath::Min(Animal.HerdSize, AH.AmountHerdReduced);
                if (ActualReduce > 0.0f) {
                    Animal.HerdSize -= ActualReduce; ActualMeatGained += ActualReduce * 15.0f;
                    City.Knowledge.SubKnowledge.FindOrAdd(TEXT("Hunting")) += ActualReduce * 5.0f;

                    int32 AnimGX = FMath::FloorToInt(Animal.Position.X / CellSize);
                    int32 AnimGY = FMath::FloorToInt(Animal.Position.Y / CellSize);
                    FCellStaticData* AnimSCell = nullptr; FCellDynamicData* AnimDCell = nullptr; FIntPoint AnimCoord;
                    if (Manager->GetMutableCellGlobal(AnimGX, AnimGY, AnimSCell, AnimDCell, AnimCoord)) {
                        AnimDCell->AnimalBones += ActualReduce * 5.0f;
                        Manager->RegisterVisualChange(AnimCoord, EChunkVisualDirty::Flora);
                    }
                }
            }
        }

        float WealthProduced = Work.NumMarkets * 10.0f * DeltaTime;
        float ToolsProduced = 0.0f;
        float WeaponsProduced = 0.0f;

        float SmeltCap = Work.NumBlacksmiths * 30.0f * DeltaTime;
        float FactoryMultiplier = 1.0f;

        if (Work.NumFactories > 0) {
            float EnergyNeeded = Work.NumFactories * 150.0f * DeltaTime;
            float EnergyConsumed = 0.0f;

            float OilBurned = FMath::Min(EnergyNeeded, City.Inventory.Oil);
            City.Inventory.Oil -= OilBurned;
            EnergyConsumed += OilBurned;

            if (EnergyConsumed < EnergyNeeded) {
                float WoodBurned = FMath::Min(EnergyNeeded - EnergyConsumed, City.Inventory.Wood);
                City.Inventory.Wood -= WoodBurned;
                EnergyConsumed += WoodBurned;
            }

            FactoryMultiplier = (EnergyConsumed / FMath::Max(0.1f, EnergyNeeded));
            SmeltCap += (Work.NumFactories * 300.0f * DeltaTime) * FactoryMultiplier;
            DayPollution += (Work.NumFactories * 20.0f * FactoryMultiplier) * DeltaTime;
        }

        float OreToProcess = FMath::Min(SmeltCap, City.Inventory.IronOre);
        City.Inventory.IronOre -= OreToProcess;

        float ConversionRate = 0.5f + (FactoryMultiplier * 1.5f);
        ToolsProduced += (OreToProcess * 0.5f) * ConversionRate;
        WeaponsProduced += (OreToProcess * 0.5f) * ConversionRate;

        float NuclearSmeltCap = Work.NumPowerPlantsNuke * 1000.0f * DeltaTime;
        float UraniumToProcess = FMath::Min(NuclearSmeltCap, City.Inventory.Uranium);
        City.Inventory.Uranium -= UraniumToProcess;
        if (UraniumToProcess > 0.0f) {
            WealthProduced += UraniumToProcess * 100.0f;
            ToolsProduced += UraniumToProcess * 50.0f;
        }

        if (Work.NumAirports > 0) WealthProduced += 200.0f * DeltaTime;
        if (Work.NumPowerPlantsNuke > 0) WealthProduced += 300.0f * DeltaTime;
        if (Work.NumAICenters > 0) WealthProduced += 1000.0f * DeltaTime;

        float ToolsUsed = City.Population * 0.02f * DeltaTime;
        City.Inventory.Tools = FMath::Max(0.0f, City.Inventory.Tools - ToolsUsed);

        float FoodNeeded = City.Population * Manager->HumanFoodConsumptionRate * DeltaTime;
        City.Inventory.Wood = FMath::Max(0.0f, City.Inventory.Wood - Work.HeatingNeeded);

        float MeatEaten = FMath::Min(City.Inventory.MeatFood + ActualMeatGained, FoodNeeded);
        float FloraEaten = FoodNeeded - MeatEaten;

        float NetMeat = ActualMeatGained - MeatEaten;
        float NetFlora = ActualFloraGained - FloraEaten;

        City.Inventory.MeatFood += NetMeat;
        City.Inventory.FloraFood += NetFlora;
        City.Inventory.Wood += ActualWoodGained;
        City.Inventory.Stone += ActualStoneGained;
        City.Inventory.IronOre += ActualOreGained;
        City.Inventory.Oil += ActualOilGained;
        City.Inventory.Uranium += ActualUraniumGained;
        City.Inventory.Tools += ToolsProduced;
        City.Inventory.Weapons += WeaponsProduced;
        City.Inventory.Wealth += WealthProduced;

        float LogisticsBonus = Work.NumLogisticsCenters * 10000.0f;
        float MaxFood = (City.Population * 100.0f) + LogisticsBonus;
        float MaxWood = (City.Population * 50.0f) + LogisticsBonus;
        float MaxStone = (City.Population * 50.0f) + LogisticsBonus;
        float MaxOre = (City.Population * 30.0f) + LogisticsBonus;
        float MaxOil = (City.Population * 20.0f) + LogisticsBonus;
        float MaxUranium = (City.Population * 5.0f) + LogisticsBonus;
        float MaxTools = (City.Population * 50.0f) + LogisticsBonus;
        float MaxWeapons = (City.Population * 50.0f) + LogisticsBonus;

        City.Inventory.MeatFood = FMath::Min(City.Inventory.MeatFood, MaxFood);
        City.Inventory.FloraFood = FMath::Min(City.Inventory.FloraFood, MaxFood);
        City.Inventory.Wood = FMath::Min(City.Inventory.Wood, MaxWood);
        City.Inventory.Stone = FMath::Min(City.Inventory.Stone, MaxStone);
        City.Inventory.IronOre = FMath::Min(City.Inventory.IronOre, MaxOre);
        City.Inventory.Oil = FMath::Min(City.Inventory.Oil, MaxOil);
        City.Inventory.Uranium = FMath::Min(City.Inventory.Uranium, MaxUranium);
        City.Inventory.Tools = FMath::Min(City.Inventory.Tools, MaxTools);
        City.Inventory.Weapons = FMath::Min(City.Inventory.Weapons, MaxWeapons);

        City.EcologicalPressure = FMath::Lerp(City.EcologicalPressure, DayPollution * 100.0f, 0.1f);

        if ((City.Inventory.MeatFood + City.Inventory.FloraFood) <= 0.0f && Work.ScannedFloraCap < (City.Population * 2.0f)) {
            City.ResourcePressure += 0.5f * DeltaTime;
        }

        if (Work.bNearVolcano) {
            City.Culture.NatureView = FMath::Min(100.0f, City.Culture.NatureView + 2.0f * DeltaTime);
            City.Culture.Pillars[ECulturalPillar::Spirituality] = FMath::Min(100.0f, City.Culture.Pillars[ECulturalPillar::Spirituality] + 1.0f * DeltaTime);
        }
        if (Work.PredatorKills > 0) {
            City.Culture.DeathView = FMath::Min(100.0f, City.Culture.DeathView + 1.0f * DeltaTime);
            City.Culture.Pillars[ECulturalPillar::Militarism] = FMath::Min(100.0f, City.Culture.Pillars[ECulturalPillar::Militarism] + 2.0f * DeltaTime);
        }
        if (Work.NumMarkets > 0) {
            City.Culture.Toleration = FMath::Min(100.0f, City.Culture.Toleration + 1.0f * DeltaTime);
        }
        if (Work.NumMines > 0 || Work.NumFactories > 0) {
            City.Culture.NatureView = FMath::Max(-100.0f, City.Culture.NatureView - 1.5f * DeltaTime);
            City.Culture.Pillars[ECulturalPillar::Industry] = FMath::Min(100.0f, City.Culture.Pillars[ECulturalPillar::Industry] + 1.0f * DeltaTime);
        }
    }

    for (FSettlementData& S : Settlements) {
        if (S.Population <= 0) continue;

        int32 MaxCellsAllowed = FMath::Max(10, S.Population / 5);
        if (S.ClaimedCells.Num() >= MaxCellsAllowed) {
            S.ExpansionPoints = 0.0f;
            continue;
        }

        if (S.ClaimedCells.Num() == 0) {
            int32 GX = FMath::FloorToInt(S.Position.X / CellSize); int32 GY = FMath::FloorToInt(S.Position.Y / CellSize);
            FCellStaticData* SCell = nullptr; FCellDynamicData* DCell = nullptr; FIntPoint CC;
            if (Manager->GetMutableCellGlobal(GX, GY, SCell, DCell, CC)) {
                SCell->OwnerSettlementID = S.SettlementID; SCell->OwnerNationID = S.NationID; SCell->PoliticalColor = GetPoliticalColor(S);
                S.ClaimedCells.Add(FIntPoint(GX, GY));
                S.BorderCells.Add(FIntPoint(GX + 1, GY)); S.BorderCells.Add(FIntPoint(GX - 1, GY));
                S.BorderCells.Add(FIntPoint(GX, GY + 1)); S.BorderCells.Add(FIntPoint(GX, GY - 1));
                Manager->RegisterVisualChange(CC, EChunkVisualDirty::Terrain);
            }
        }

        float ExpansionRate = (S.Population * 0.05f) + 2.0f;
        if (S.Culture.Pillars.Contains(ECulturalPillar::Expansion)) ExpansionRate += S.Culture.Pillars[ECulturalPillar::Expansion] * 0.1f;
        S.ExpansionPoints += ExpansionRate * DeltaTime;

        int32 ExpansionsThisTick = 0;
        bool bCanSail = S.Knowledge.UnlockedTechnologies.Contains("Naval Engineering");

        while (S.ExpansionPoints > 15.0f && S.BorderCells.Num() > 0 && ExpansionsThisTick < 1) {
            int32 RndIdx = FMath::RandRange(0, S.BorderCells.Num() - 1);
            FIntPoint Target = S.BorderCells[RndIdx]; S.BorderCells.RemoveAtSwap(RndIdx);

            FCellStaticData* SCell = nullptr; FCellDynamicData* DCell = nullptr; FIntPoint CC;
            if (Manager->GetMutableCellGlobal(Target.X, Target.Y, SCell, DCell, CC)) {

                bool bIsValidLand = SCell->Elevation > Manager->SeaLevel;
                bool bIsValidSea = bCanSail && SCell->Elevation <= Manager->SeaLevel && SCell->Elevation > Manager->SeaLevel - 100.0f;

                if (SCell->OwnerSettlementID == -1 && (bIsValidLand || bIsValidSea)) {
                    float Cost = 15.0f;
                    if (SCell->Elevation > Manager->SeaLevel + 400.0f) Cost += 50.0f;
                    if (SCell->TreeType != ETreeType::None) Cost += 10.0f;
                    if (!bIsValidLand) Cost += 150.0f;

                    if (S.ExpansionPoints >= Cost) {
                        S.ExpansionPoints -= Cost;
                        SCell->OwnerSettlementID = S.SettlementID; SCell->OwnerNationID = S.NationID; SCell->PoliticalColor = GetPoliticalColor(S);
                        S.ClaimedCells.Add(Target);

                        S.BorderCells.AddUnique(FIntPoint(Target.X + 1, Target.Y)); S.BorderCells.AddUnique(FIntPoint(Target.X - 1, Target.Y));
                        S.BorderCells.AddUnique(FIntPoint(Target.X, Target.Y + 1)); S.BorderCells.AddUnique(FIntPoint(Target.X, Target.Y - 1));

                        Manager->RegisterVisualChange(CC, EChunkVisualDirty::Terrain | EChunkVisualDirty::Water);
                        ExpansionsThisTick++;
                    }
                    else { S.BorderCells.Add(Target); break; }
                }
            }
        }
    }

    if (Manager) {
        if (bMadeBuildingChange) Manager->bSettlementVisualDirty = true;
        for (const FIntPoint& DC : DirtyChunks) {
            if (FChunkData* DirtiedChunk = WorldChunks.Find(DC)) {
                if (DirtiedChunk->AccumulatedTerrainChange > 1.5f) {
                    DirtiedChunk->AccumulatedTerrainChange = 0.0f;
                    Manager->RegisterVisualChange(DC, EChunkVisualDirty::Terrain);
                }
            }
        }
    }
}

void USettlementSystem::ProcessDailyDemographics(ASimWorldManager* Manager) {
    if (!Manager) return;
    int32 CurrentGlobalPop = Manager->GetTotalPopulation();

    for (int32 i = Settlements.Num() - 1; i >= 0; i--) {
        FSettlementData& City = Settlements[i];
        if (City.InteractionCooldownDays > 0) City.InteractionCooldownDays--;

        for (int32 p = City.Pregnancies.Num() - 1; p >= 0; p--) {
            City.Pregnancies[p].DaysRemaining--;
            if (City.Pregnancies[p].DaysRemaining <= 0) {
                int32 Amount = City.Pregnancies[p].Amount; City.Population += Amount;
                if (Manager->ManaModule) Manager->ManaModule->AccumulateLifeMana(Amount, EManaSourceType::HumanBirth);
                City.Pregnancies.RemoveAt(p);
            }
        }

        float FoodReserve = City.Inventory.FloraFood + City.Inventory.MeatFood;
        float BaseCap = 500.0f;

        if (City.Knowledge.Levels.Contains(EKnowledgeField::Agriculture) && City.Knowledge.Levels[EKnowledgeField::Agriculture] > 5.0f) BaseCap = 2500.0f;
        if (City.Knowledge.UnlockedTechnologies.Contains("Industrial Mass Production")) BaseCap = 15000.0f;
        if (City.Knowledge.UnlockedTechnologies.Contains("Sustainable Infrastructure")) BaseCap = 50000.0f;

        // --- NOVA LOGIKA BUDOV (ZAKLADY) ---
        int32 PopPerHouse = 50;
        int32 TargetHouses = FMath::Clamp(FMath::FloorToInt((float)City.Population / PopPerHouse), 1, 500);

        if (City.Houses.Num() < TargetHouses) {
            float GoldenAngle = 137.507764f * (PI / 180.0f);
            FRandomStream BldStream(FMath::RoundToInt(City.Position.X) * 13 + City.Houses.Num() * 7);

            EBuildingEra CurrentEra = EBuildingEra::Primitive;
            if (City.Knowledge.UnlockedTechnologies.Contains("Industrial Mass Production")) CurrentEra = EBuildingEra::Industrial;
            else if (City.Knowledge.Levels.Contains(EKnowledgeField::Masonry) && City.Knowledge.Levels[EKnowledgeField::Masonry] >= 5.0f) CurrentEra = EBuildingEra::Masonry;

            int32 Attempts = 0;
            int32 PlacedThisTick = 0;

            while (City.Houses.Num() < TargetHouses && Attempts < 50) {
                Attempts++;
                int32 h = City.Houses.Num() + Attempts;
                float r = 25.0f * FMath::Sqrt((float)h);
                float theta = h * GoldenAngle;
                FVector2D HPos = City.Position + FVector2D(FMath::Cos(theta), FMath::Sin(theta)) * r;

                int32 GlobalX = FMath::FloorToInt(HPos.X / 50.0f);
                int32 GlobalY = FMath::FloorToInt(HPos.Y / 50.0f);

                FCellStaticData SCell; FCellDynamicData DCell;
                if (!Manager->GetCellGlobal(GlobalX, GlobalY, SCell, DCell)) continue;

                // OPRAVA 1: STRIKTNÍ KONTROLA VODY (Domy už nebudou ve vodì)
                if (DCell.SurfaceWater > 0.05f || SCell.Elevation <= Manager->SeaLevel + 5.0f || SCell.WaterType != EWaterType::None) continue;
                if (DCell.GlacierIce > 0.5f) continue;

                FHouseFootprint NewHouse;
                NewHouse.Position = HPos;
                NewHouse.Yaw = BldStream.FRandRange(0.0f, 360.0f);
                NewHouse.Scale = BldStream.FRandRange(0.08f, 0.12f);
                NewHouse.Era = CurrentEra;

                City.Houses.Add(NewHouse);
                PlacedThisTick++;

                if (PlacedThisTick > 5) break;
            }
            Manager->bSettlementVisualDirty = true;
        }
        // -----------------------------------

        int32 SplitThreshold = FMath::RoundToInt(BaseCap * 0.8f);
        if (City.Population >= SplitThreshold && City.SplitCount < 5 && CurrentGlobalPop < Manager->MaxGlobalPopulation && FoodReserve > 100.0f && Manager->HumanModule) {
            FTribeData Settlers;
            Settlers.Position = City.Position + FVector2D(FMath::RandRange(-150.0f, 150.0f), FMath::RandRange(-150.0f, 150.0f));
            Settlers.Population = 50; City.Population -= 50; City.SplitCount++; CurrentGlobalPop += 50;
            Settlers.Inventory.FloraFood = 150.0f; Settlers.Inventory.Wood = 50.0f;
            City.Inventory.FloraFood = FMath::Max(0.0f, City.Inventory.FloraFood - 150.0f); City.Inventory.Wood = FMath::Max(0.0f, City.Inventory.Wood - 50.0f);
            Settlers.Knowledge = City.Knowledge; Settlers.Culture = City.Culture; Settlers.TribeColor = City.Color;
            Settlers.State = ETribeState::Migrating; Settlers.CampDaysRemaining = 0;
            FVector2D FleeDir = FVector2D(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)).GetSafeNormal();
            Settlers.TargetRegion = Settlers.Position + (FleeDir * 10000.0f);
            Manager->HumanModule->Tribes.Add(Settlers);
        }

        float HealthCareBonus = 0.0f;
        if (City.Knowledge.UnlockedTechnologies.Contains("Sustainable Infrastructure")) HealthCareBonus = 0.08f;

        float PollutionSicknessRate = FMath::Clamp((City.EcologicalPressure / 1000.0f) - HealthCareBonus, 0.0f, 0.1f);

        if (FoodReserve <= 0.0f) {
            float StarvationRate = 0.50f / 365.0f;
            float RawDeaths = City.Population * (StarvationRate + PollutionSicknessRate);
            int32 Deaths = FMath::FloorToInt(RawDeaths); if (FMath::FRand() < FMath::Fmod(RawDeaths, 1.0f)) Deaths++;
            int32 ActualDeaths = FMath::Max(1, Deaths);
            City.Population -= ActualDeaths;

            if (Manager->ManaModule) Manager->ManaModule->AccumulateLifeMana(ActualDeaths, EManaSourceType::HumanDeath);
            City.Knowledge.SubKnowledge.FindOrAdd(TEXT("Survival")) += 1.0f;

            if (City.Population < 30) {
                if (Manager->HumanModule) {
                    FTribeData Refugees; Refugees.Position = City.Position + FVector2D(FMath::RandRange(-100.0f, 100.0f), FMath::RandRange(-100.0f, 100.0f));
                    Refugees.Population = City.Population; Refugees.State = ETribeState::Migrating;
                    Refugees.Inventory.FloraFood = 20.0f; Refugees.TribeColor = City.Color; Refugees.Knowledge = City.Knowledge; Refugees.Culture = City.Culture;
                    Manager->HumanModule->Tribes.Add(Refugees);
                }
                for (FIntPoint Coord : City.ClaimedCells) {
                    FCellStaticData* SCell = nullptr; FCellDynamicData* DCell = nullptr; FIntPoint CC;
                    if (Manager->GetMutableCellGlobal(Coord.X, Coord.Y, SCell, DCell, CC)) {
                        SCell->OwnerSettlementID = -1; SCell->OwnerNationID = -1; SCell->PoliticalColor = FLinearColor::Transparent; SCell->BuildingType = EBuildingType::None; SCell->bHasRoad = false;
                        Manager->RegisterVisualChange(CC, EChunkVisualDirty::Terrain);
                    }
                }
                Settlements.RemoveAt(i);
                Manager->bSettlementVisualDirty = true;
                continue;
            }
        }
        else {
            float RawDeaths = City.Population * ((0.02f + PollutionSicknessRate) / 365.0f);
            int32 Deaths = FMath::FloorToInt(RawDeaths); if (FMath::FRand() < FMath::Fmod(RawDeaths, 1.0f)) Deaths++;
            City.Population -= Deaths;
            if (Deaths > 0 && Manager->ManaModule) Manager->ManaModule->AccumulateLifeMana(Deaths, EManaSourceType::HumanDeath);

            if (PollutionSicknessRate > 0.05f && Manager->HistoryModule && FMath::FRand() < 0.05f) {
                FString Desc = FString::Printf(TEXT("Settlement %d suffered a massive epidemic due to extreme industrial pollution and tainted water."), City.SettlementID);
                Manager->HistoryModule->LogEvent(Manager->CurrentYear, Manager->CurrentDay, "Epidemic", "Public Health", Desc, City.Position, City.SettlementID);
            }

            if (City.Population <= 0) { Settlements.RemoveAt(i); Manager->bSettlementVisualDirty = true; continue; }
        }

        if (City.Population > 0 && CurrentGlobalPop < Manager->MaxGlobalPopulation) {
            float PopFactor = FMath::Clamp((float)City.Population / BaseCap, 0.0f, 1.0f);
            float AnnualGrowth = FMath::Lerp(0.35f, 0.01f, FMath::Pow(PopFactor, 2.0f));
            float DailyBirthsRaw = (City.Population * AnnualGrowth) / 365.0f;
            int32 BirthAmount = FMath::FloorToInt(DailyBirthsRaw); if (FMath::FRand() < FMath::Fmod(DailyBirthsRaw, 1.0f)) BirthAmount++;
            if (BirthAmount > 0) City.Pregnancies.Add({ 60, BirthAmount });
        }
    }
}

void USettlementSystem::BuildSettlementMesh(TSharedPtr<FChunkMeshData> MeshData, FIntPoint ChunkCoord, ASimWorldManager* Manager)
{
}

bool USettlementSystem::TryBoostCulturalPillar(int32 SettlementID, ECulturalPillar Pillar, ASimWorldManager* Manager)
{
    if (!Manager || !Manager->ManaModule) return false;

    for (FSettlementData& City : Settlements) {
        if (City.SettlementID == SettlementID) {

            if (!Manager->ManaModule->SpendMana(100.0f, TEXT("Inspirace osady"))) return false;

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

            City.Culture.Pillars.FindOrAdd(Pillar) = FMath::Min(100.0f, City.Culture.Pillars[Pillar] + BoostAmount);
            if (AllyPillar != Pillar) {
                City.Culture.Pillars.FindOrAdd(AllyPillar) = FMath::Min(100.0f, City.Culture.Pillars[AllyPillar] + AllyAmount);
            }
            if (OpponentPillar != Pillar) {
                City.Culture.Pillars.FindOrAdd(OpponentPillar) = FMath::Max(0.0f, City.Culture.Pillars[OpponentPillar] - OpponentPenalty);
            }
            return true;
        }
    }
    return false;
}

bool USettlementSystem::TryForceExpansion(int32 SettlementID, FVector2D TargetLoc, ASimWorldManager* Manager)
{
    if (!Manager || !Manager->ManaModule) return false;

    for (FSettlementData& City : Settlements) {
        if (City.SettlementID == SettlementID) {

            if (!Manager->ManaModule->SpendMana(250.0f, TEXT("Nucena expanze osady"))) return false;

            City.bHasForcedExpansion = true;
            City.ForcedExpansionTarget = TargetLoc;

            City.ExpansionPoints += 150.0f;

            return true;
        }
    }
    return false;
}