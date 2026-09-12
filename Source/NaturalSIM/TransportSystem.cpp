#include "TransportSystem.h"
#include "SimWorldManager.h"
#include "SettlementSystem.h"
#include "NationSystem.h"
#include "Async/Async.h" 

UTransportSystem::UTransportSystem() { PrimaryComponentTick.bCanEverTick = false; }
void UTransportSystem::BeginPlay() { Super::BeginPlay(); }

void UTransportSystem::PlanNationalRoads(ASimWorldManager* Manager)
{
    if (!Manager || !Manager->NationModule || !Manager->SettlementModule) return;

    for (const FNationData& Nation : Manager->NationModule->Nations) {
        if (Nation.MemberSettlementIDs.Num() > 1) {
            int32 CapitalID = Nation.MemberSettlementIDs[0];

            for (int32 i = 1; i < Nation.MemberSettlementIDs.Num(); i++) {
                int32 ProvincialID = Nation.MemberSettlementIDs[i];
                EstablishTradeRoute(CapitalID, ProvincialID, true);
            }
        }
    }
}

void UTransportSystem::EstablishTradeRoute(int32 CityA_ID, int32 CityB_ID, bool bIsNational)
{
    bool bTradeExists = false;
    for (FTradeRoute& Route : TradeRoutes) {
        if ((Route.CityA_ID == CityA_ID && Route.CityB_ID == CityB_ID) || (Route.CityA_ID == CityB_ID && Route.CityB_ID == CityA_ID)) {
            if (bIsNational && !Route.bIsNational) {
                Route.bIsNational = true;
                Route.TransferSpeed = 0.5f;
            }
            bTradeExists = true;
            break;
        }
    }

    if (!bTradeExists) {
        FTradeRoute NewRoute;
        NewRoute.CityA_ID = CityA_ID;
        NewRoute.CityB_ID = CityB_ID;
        NewRoute.bIsNational = bIsNational;
        NewRoute.TransferSpeed = bIsNational ? 0.5f : 0.05f;
        NewRoute.RouteType = ETransportMode::Land;
        TradeRoutes.Add(NewRoute);
    }

    int32 ConnectionsA = 0;
    int32 ConnectionsB = 0;
    bool bRoadExists = false;

    for (const FRoadNetwork& Road : RoadNetworks) {
        if ((Road.CityA_ID == CityA_ID && Road.CityB_ID == CityB_ID) || (Road.CityA_ID == CityB_ID && Road.CityB_ID == CityA_ID)) {
            bRoadExists = true; break;
        }
        if (Road.CityA_ID == CityA_ID || Road.CityB_ID == CityA_ID) ConnectionsA++;
        if (Road.CityA_ID == CityB_ID || Road.CityB_ID == CityB_ID) ConnectionsB++;
    }

    if (!bRoadExists && (bIsNational || (ConnectionsA < 2 && ConnectionsB < 2))) {
        PendingRoadRequests.Enqueue(TPair<int32, int32>(CityA_ID, CityB_ID));
    }
}

TArray<FVector2D> UTransportSystem::CalculateRoadPath_Async(FVector2D PosA, FVector2D PosB, ASimWorldManager* Manager)
{
    TArray<FVector2D> OutPath;
    if (!Manager) return OutPath;

    float CellSize = 50.0f;
    FIntPoint StartCoord(FMath::RoundToInt(PosA.X / CellSize), FMath::RoundToInt(PosA.Y / CellSize));
    FIntPoint EndCoord(FMath::RoundToInt(PosB.X / CellSize), FMath::RoundToInt(PosB.Y / CellSize));

    TMap<FIntPoint, FIntPoint> CameFrom;
    TMap<FIntPoint, float> GScore;
    TMap<FIntPoint, float> FScore;
    TArray<FIntPoint> OpenSet;
    TSet<FIntPoint> ClosedSet;

    OpenSet.Add(StartCoord);
    GScore.Add(StartCoord, 0.0f);
    FScore.Add(StartCoord, FVector2D::Distance(FVector2D(StartCoord), FVector2D(EndCoord)));

    int32 Iterations = 0;
    bool bFoundPath = false;
    FIntPoint BestReachable = StartCoord;
    float MinDistToTarget = FScore[StartCoord];

    while (OpenSet.Num() > 0 && Iterations < 3000) {
        Iterations++;

        int32 CurrentIdx = 0;
        float LowestF = FScore[OpenSet[0]];
        for (int32 i = 1; i < OpenSet.Num(); i++) {
            float F = FScore[OpenSet[i]];
            if (F < LowestF) {
                LowestF = F;
                CurrentIdx = i;
            }
        }

        FIntPoint Current = OpenSet[CurrentIdx];

        if (Current == EndCoord || FVector2D::Distance(FVector2D(Current), FVector2D(EndCoord)) < 3.0f) {
            bFoundPath = true;
            EndCoord = Current;
            break;
        }

        OpenSet.RemoveAtSwap(CurrentIdx);
        ClosedSet.Add(Current);

        FCellStaticData CurrentSCell; FCellDynamicData CurrentDCell;
        Manager->GetCellGlobal(Current.X, Current.Y, CurrentSCell, CurrentDCell);

        FIntPoint Neighbors[8] = {
            FIntPoint(0,1), FIntPoint(1,0), FIntPoint(0,-1), FIntPoint(-1,0),
            FIntPoint(1,1), FIntPoint(1,-1), FIntPoint(-1,-1), FIntPoint(-1,1)
        };

        for (int i = 0; i < 8; i++) {
            FIntPoint Neighbor = Current + Neighbors[i];

            if (ClosedSet.Contains(Neighbor)) continue;

            FCellStaticData NSCell; FCellDynamicData NDCell;
            if (!Manager->GetCellGlobal(Neighbor.X, Neighbor.Y, NSCell, NDCell)) continue;

            if (NDCell.SurfaceWater >= 0.2f || NSCell.Elevation <= Manager->SeaLevel) continue;

            float Slope = FMath::Abs(NSCell.Elevation - CurrentSCell.Elevation);
            if (Slope > 8.0f) continue;

            float MoveCost = (i < 4) ? 1.0f : 1.414f;
            float TerrainCost = MoveCost * 10.0f;
            if (Slope > 2.0f) TerrainCost += Slope * 5.0f;
            if (NSCell.TreeType != ETreeType::None) TerrainCost += 5.0f;
            if (NSCell.Biome == EBiomeType::Swamp) TerrainCost += 50.0f;

            float TentativeG = GScore[Current] + TerrainCost;

            if (!GScore.Contains(Neighbor) || TentativeG < GScore[Neighbor]) {
                CameFrom.Add(Neighbor, Current);
                GScore.Add(Neighbor, TentativeG);

                float H = FVector2D::Distance(FVector2D(Neighbor), FVector2D(EndCoord)) * 10.0f;
                FScore.Add(Neighbor, TentativeG + H);

                if (H < MinDistToTarget) {
                    MinDistToTarget = H;
                    BestReachable = Neighbor;
                }

                if (!OpenSet.Contains(Neighbor)) {
                    OpenSet.Add(Neighbor);
                }
            }
        }
    }

    FIntPoint PathCurr = bFoundPath ? EndCoord : BestReachable;

    if (!bFoundPath && FVector2D::Distance(FVector2D(PathCurr), FVector2D(EndCoord)) > 10.0f) {
        return OutPath;
    }

    TArray<FVector2D> TempPath;
    while (CameFrom.Contains(PathCurr)) {
        TempPath.Add(FVector2D(PathCurr.X * CellSize, PathCurr.Y * CellSize));
        PathCurr = CameFrom[PathCurr];
    }
    TempPath.Add(PosA);

    for (int32 i = TempPath.Num() - 1; i >= 0; i--) {
        OutPath.Add(TempPath[i]);
    }

    if (bFoundPath) {
        OutPath.Add(PosB);
    }

    return OutPath;
}

TArray<FVector2D> UTransportSystem::CalculateSeaPath_Async(FVector2D PosA, FVector2D PosB, ASimWorldManager* Manager)
{
    TArray<FVector2D> OutPath;
    if (!Manager) return OutPath;

    float CellSize = 50.0f;
    FIntPoint StartCoord(FMath::RoundToInt(PosA.X / CellSize), FMath::RoundToInt(PosA.Y / CellSize));
    FIntPoint EndCoord(FMath::RoundToInt(PosB.X / CellSize), FMath::RoundToInt(PosB.Y / CellSize));

    TMap<FIntPoint, FIntPoint> CameFrom;
    TMap<FIntPoint, float> GScore;
    TMap<FIntPoint, float> FScore;
    TArray<FIntPoint> OpenSet;
    TSet<FIntPoint> ClosedSet;

    OpenSet.Add(StartCoord);
    GScore.Add(StartCoord, 0.0f);
    FScore.Add(StartCoord, FVector2D::Distance(FVector2D(StartCoord), FVector2D(EndCoord)));

    int32 Iterations = 0;
    bool bFoundPath = false;

    while (OpenSet.Num() > 0 && Iterations < 3000) {
        Iterations++;

        int32 CurrentIdx = 0;
        float LowestF = FScore[OpenSet[0]];
        for (int32 i = 1; i < OpenSet.Num(); i++) {
            float F = FScore[OpenSet[i]];
            if (F < LowestF) {
                LowestF = F;
                CurrentIdx = i;
            }
        }

        FIntPoint Current = OpenSet[CurrentIdx];

        if (Current == EndCoord || FVector2D::Distance(FVector2D(Current), FVector2D(EndCoord)) < 5.0f) {
            bFoundPath = true;
            EndCoord = Current;
            break;
        }

        OpenSet.RemoveAtSwap(CurrentIdx);
        ClosedSet.Add(Current);

        FIntPoint Neighbors[8] = {
            FIntPoint(0,1), FIntPoint(1,0), FIntPoint(0,-1), FIntPoint(-1,0),
            FIntPoint(1,1), FIntPoint(1,-1), FIntPoint(-1,-1), FIntPoint(-1,1)
        };

        for (int i = 0; i < 8; i++) {
            FIntPoint Neighbor = Current + Neighbors[i];

            if (ClosedSet.Contains(Neighbor)) continue;

            FCellStaticData NSCell; FCellDynamicData NDCell;
            if (!Manager->GetCellGlobal(Neighbor.X, Neighbor.Y, NSCell, NDCell)) continue;

            if (NSCell.Elevation > Manager->SeaLevel && NDCell.SurfaceWater < 0.5f) {
                if (Neighbor != EndCoord && Neighbor != StartCoord) {
                    continue;
                }
            }

            float MoveCost = (i < 4) ? 1.0f : 1.414f;
            float TentativeG = GScore[Current] + MoveCost;

            if (!GScore.Contains(Neighbor) || TentativeG < GScore[Neighbor]) {
                CameFrom.Add(Neighbor, Current);
                GScore.Add(Neighbor, TentativeG);
                float H = FVector2D::Distance(FVector2D(Neighbor), FVector2D(EndCoord));
                FScore.Add(Neighbor, TentativeG + H);

                if (!OpenSet.Contains(Neighbor)) {
                    OpenSet.Add(Neighbor);
                }
            }
        }
    }

    if (!bFoundPath) return OutPath;

    FIntPoint PathCurr = EndCoord;
    TArray<FVector2D> TempPath;
    while (CameFrom.Contains(PathCurr)) {
        TempPath.Add(FVector2D(PathCurr.X * CellSize, PathCurr.Y * CellSize));
        PathCurr = CameFrom[PathCurr];
    }
    TempPath.Add(PosA);

    for (int32 i = TempPath.Num() - 1; i >= 0; i--) {
        OutPath.Add(TempPath[i]);
    }
    OutPath.Add(PosB);

    return OutPath;
}

void UTransportSystem::SpawnVehicleForRoute(const FTradeRoute& Route, const TArray<FSettlementData>& Settlements, ASimWorldManager* Manager)
{
    int32 Count = 0;
    for (const FVehicleData& V : ActiveVehicles) {
        if ((V.OriginCityID == Route.CityA_ID && V.TargetCityID == Route.CityB_ID) ||
            (V.OriginCityID == Route.CityB_ID && V.TargetCityID == Route.CityA_ID)) {
            Count++;
        }
    }

    if (Count >= 1) return;

    FVector2D PosA = FVector2D::ZeroVector, PosB = FVector2D::ZeroVector;
    bool bFoundA = false, bFoundB = false;

    for (const FSettlementData& C : Settlements) {
        if (C.SettlementID == Route.CityA_ID) { PosA = C.Position; bFoundA = true; }
        if (C.SettlementID == Route.CityB_ID) { PosB = C.Position; bFoundB = true; }
    }

    if (!bFoundA || !bFoundB) return;

    if (Route.RouteType == ETransportMode::Air) {
        FVehicleData NewV;
        NewV.OriginCityID = Route.CityA_ID;
        NewV.TargetCityID = Route.CityB_ID;
        NewV.Position = PosA;
        NewV.TargetPosition = PosB;
        NewV.CurrentPathNode = 0;
        NewV.Type = EVehicleType::Airplane;
        NewV.Path.Add(PosA);
        NewV.Path.Add(PosB);
        ActiveVehicles.Add(NewV);
    }
    else if (Route.RouteType == ETransportMode::Sea) {
        FIntPoint CalcKey(FMath::Min(Route.CityA_ID, Route.CityB_ID), FMath::Max(Route.CityA_ID, Route.CityB_ID));

        if (ActiveSeaCalculations.Contains(CalcKey)) return;
        ActiveSeaCalculations.Add(CalcKey);

        TWeakObjectPtr<UTransportSystem> WeakThis(this);
        TWeakObjectPtr<ASimWorldManager> WeakManager(Manager);

        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis, WeakManager, Route, PosA, PosB, CalcKey]() {
            TArray<FVector2D> PathRes;
            if (WeakManager.IsValid()) {
                PathRes = CalculateSeaPath_Async(PosA, PosB, WeakManager.Get());
            }

            AsyncTask(ENamedThreads::GameThread, [WeakThis, Route, PathRes, CalcKey, PosA, PosB]() {
                if (WeakThis.IsValid()) {
                    WeakThis->ActiveSeaCalculations.Remove(CalcKey);
                    if (PathRes.Num() > 0) {
                        FVehicleData NewV;
                        NewV.OriginCityID = Route.CityA_ID;
                        NewV.TargetCityID = Route.CityB_ID;
                        NewV.Position = PosA;
                        NewV.TargetPosition = PosB;
                        NewV.CurrentPathNode = 0;
                        NewV.Type = EVehicleType::Ship;
                        NewV.Path = PathRes;
                        WeakThis->ActiveVehicles.Add(NewV);
                    }
                }
                });
            });
    }
    else {
        bool bRoadFound = false;
        TArray<FVector2D> FoundPath;

        for (const FRoadNetwork& Road : RoadNetworks) {
            if (((Road.CityA_ID == Route.CityA_ID && Road.CityB_ID == Route.CityB_ID) ||
                (Road.CityA_ID == Route.CityB_ID && Road.CityB_ID == Route.CityA_ID)) && Road.Path.Num() > 0) {

                FoundPath = Road.Path;
                if (Road.CityA_ID == Route.CityB_ID) {
                    int32 L = 0; int32 R = FoundPath.Num() - 1;
                    while (L < R) { FVector2D Temp = FoundPath[L]; FoundPath[L] = FoundPath[R]; FoundPath[R] = Temp; L++; R--; }
                }
                bRoadFound = true;
                break;
            }
        }

        if (!bRoadFound) return;

        FVehicleData NewV;
        NewV.OriginCityID = Route.CityA_ID;
        NewV.TargetCityID = Route.CityB_ID;
        NewV.Position = PosA;
        NewV.TargetPosition = PosB;
        NewV.CurrentPathNode = 0;
        NewV.Type = EVehicleType::Caravan;
        NewV.Path = FoundPath;
        ActiveVehicles.Add(NewV);
    }
}

void UTransportSystem::UpdateVehicles(float DeltaTime, ASimWorldManager* Manager)
{
    for (int32 i = ActiveVehicles.Num() - 1; i >= 0; i--) {
        FVehicleData& V = ActiveVehicles[i];

        if (V.Path.Num() < 2) {
            ActiveVehicles.RemoveAtSwap(i);
            continue;
        }

        float Speed = 50.0f;
        if (V.Type == EVehicleType::Ship) Speed = 150.0f;
        if (V.Type == EVehicleType::Airplane) Speed = 400.0f;

        FVector2D TargetNode = V.Path[V.CurrentPathNode + 1];
        FVector2D Dir = (TargetNode - V.Position).GetSafeNormal();

        V.Position += Dir * Speed * DeltaTime;

        if (FVector2D::Distance(V.Position, TargetNode) < 15.0f) {
            V.CurrentPathNode++;
            if (V.CurrentPathNode >= V.Path.Num() - 1) {
                int32 L = 0; int32 R = V.Path.Num() - 1;
                while (L < R) { FVector2D Temp = V.Path[L]; V.Path[L] = V.Path[R]; V.Path[R] = Temp; L++; R--; }
                V.CurrentPathNode = 0;
            }
        }
    }
}

void UTransportSystem::ProcessTransport(TArray<FSettlementData>& Settlements, float DeltaTime, ASimWorldManager* Manager)
{
    if (!Manager || Settlements.Num() < 2) return;

    if (Manager->CurrentDay % 10 == 0) {
        PlanNationalRoads(Manager);
    }

    TPair<int32, int32> RoadReq;
    if (PendingRoadRequests.Dequeue(RoadReq)) {
        FIntPoint CalcKey(FMath::Min(RoadReq.Key, RoadReq.Value), FMath::Max(RoadReq.Key, RoadReq.Value));

        if (!ActiveRoadCalculations.Contains(CalcKey)) {
            FVector2D PosA = FVector2D::ZeroVector, PosB = FVector2D::ZeroVector;
            bool bFoundA = false, bFoundB = false;
            for (const FSettlementData& C : Settlements) {
                if (C.SettlementID == RoadReq.Key) { PosA = C.Position; bFoundA = true; }
                if (C.SettlementID == RoadReq.Value) { PosB = C.Position; bFoundB = true; }
            }

            if (bFoundA && bFoundB) {
                ActiveRoadCalculations.Add(CalcKey);
                TWeakObjectPtr<UTransportSystem> WeakThis(this);
                TWeakObjectPtr<ASimWorldManager> WeakManager(Manager);

                AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis, WeakManager, RoadReq, PosA, PosB, CalcKey]() {
                    TArray<FVector2D> PathRes;
                    if (WeakManager.IsValid()) {
                        PathRes = CalculateRoadPath_Async(PosA, PosB, WeakManager.Get());
                    }

                    AsyncTask(ENamedThreads::GameThread, [WeakThis, RoadReq, PathRes, CalcKey]() {
                        if (WeakThis.IsValid()) {
                            WeakThis->ActiveRoadCalculations.Remove(CalcKey);
                            FRoadNetwork NewRoad;
                            NewRoad.CityA_ID = RoadReq.Key;
                            NewRoad.CityB_ID = RoadReq.Value;
                            NewRoad.BuiltNodes = 0;
                            NewRoad.ConstructionProgress = 0.0f;
                            NewRoad.bIsImpossible = (PathRes.Num() == 0);
                            NewRoad.Path = PathRes;
                            WeakThis->RoadNetworks.Add(NewRoad);
                        }
                        });
                    });
            }
        }
    }

    for (FRoadNetwork& Road : RoadNetworks) {
        if (Road.bIsImpossible) continue;

        if (!Road.bIsImpossible && Road.BuiltNodes < Road.Path.Num()) {
            Road.ConstructionProgress += DeltaTime * 2.0f;
            if (Road.ConstructionProgress >= 1.0f) {
                Road.ConstructionProgress = 0.0f;
                FVector2D TargetNode = Road.Path[Road.BuiltNodes];

                float CellSize = 50.0f;
                float ChunkWorldSize = (Manager->ChunkSize - 1) * CellSize;
                int32 CX = FMath::FloorToInt(TargetNode.X / ChunkWorldSize);
                int32 CY = FMath::FloorToInt(TargetNode.Y / ChunkWorldSize);

                FIntPoint ChunkCoord(CX, CY);

                if (FChunkData* TargetChunk = Manager->WorldChunks.Find(ChunkCoord)) {
                    int32 LX = FMath::Clamp(FMath::FloorToInt((TargetNode.X - (CX * ChunkWorldSize)) / CellSize), 0, Manager->ChunkSize - 1);
                    int32 LY = FMath::Clamp(FMath::FloorToInt((TargetNode.Y - (CY * ChunkWorldSize)) / CellSize), 0, Manager->ChunkSize - 1);

                    for (int32 dy = -1; dy <= 1; dy++) {
                        for (int32 dx = -1; dx <= 1; dx++) {
                            int32 nx = LX + dx; int32 ny = LY + dy;
                            if (nx >= 0 && nx < Manager->ChunkSize && ny >= 0 && ny < Manager->ChunkSize) {
                                FCellStaticData& SCell = TargetChunk->StaticCells[nx + ny * Manager->ChunkSize];
                                FCellDynamicData& DCell = TargetChunk->DynamicCells[nx + ny * Manager->ChunkSize];

                                if (SCell.TreeType != ETreeType::None) {
                                    SCell.TreeType = ETreeType::None;
                                    DCell.WoodAmount = 0.0f;
                                    DCell.FloraDensity *= 0.1f;
                                }
                                if (dx == 0 && dy == 0) SCell.bHasRoad = true;
                            }
                        }
                    }

                    TargetChunk->AccumulatedTerrainChange += 50.0f;
                    Manager->RegisterVisualChange(ChunkCoord, EChunkVisualDirty::Flora);
                    Manager->RegisterVisualChange(ChunkCoord, EChunkVisualDirty::Terrain);
                }
                Road.BuiltNodes++;
            }
        }
    }

    UpdateVehicles(DeltaTime, Manager);

    for (FTradeRoute& Route : TradeRoutes) {
        FSettlementData* CityA = nullptr;
        FSettlementData* CityB = nullptr;

        for (FSettlementData& C : Settlements) {
            if (C.SettlementID == Route.CityA_ID) CityA = &C;
            if (C.SettlementID == Route.CityB_ID) CityB = &C;
        }

        if (CityA && CityB) {
            bool bHasAirportA = false, bHasAirportB = false;
            bool bHasPortA = false, bHasPortB = false;

            for (FIntPoint C : CityA->ClaimedCells) {
                FCellStaticData SCell; FCellDynamicData DCell;
                if (Manager->GetCellGlobal(C.X, C.Y, SCell, DCell)) {
                    if (SCell.BuildingType == EBuildingType::Airport) bHasAirportA = true;
                    if (SCell.BuildingType == EBuildingType::Port) bHasPortA = true;
                }
            }
            for (FIntPoint C : CityB->ClaimedCells) {
                FCellStaticData SCell; FCellDynamicData DCell;
                if (Manager->GetCellGlobal(C.X, C.Y, SCell, DCell)) {
                    if (SCell.BuildingType == EBuildingType::Airport) bHasAirportB = true;
                    if (SCell.BuildingType == EBuildingType::Port) bHasPortB = true;
                }
            }

            float SpeedMultiplier = 1.0f;
            float CompletionRatio = 0.1f;
            Route.RouteType = ETransportMode::Land;

            for (const FRoadNetwork& Road : RoadNetworks) {
                if ((Road.CityA_ID == Route.CityA_ID && Road.CityB_ID == Route.CityB_ID) || (Road.CityA_ID == Route.CityB_ID && Road.CityB_ID == Route.CityA_ID)) {
                    if (Road.Path.Num() > 0 && !Road.bIsImpossible) {
                        CompletionRatio = FMath::Max(0.1f, (float)Road.BuiltNodes / Road.Path.Num());
                    }
                    break;
                }
            }

            if (bHasAirportA && bHasAirportB && CityA->Inventory.Oil > 10.0f && CityB->Inventory.Oil > 10.0f) {
                SpeedMultiplier = 10.0f;
                CompletionRatio = 1.0f;
                Route.RouteType = ETransportMode::Air;
                CityA->Inventory.Oil -= 0.5f * DeltaTime;
                CityB->Inventory.Oil -= 0.5f * DeltaTime;
            }
            else if (bHasPortA && bHasPortB) {
                SpeedMultiplier = 5.0f;
                CompletionRatio = 1.0f;
                Route.RouteType = ETransportMode::Sea;
            }

            if (CompletionRatio > 0.9f) {
                SpawnVehicleForRoute(Route, Settlements, Manager);
            }

            float EffectiveSpeed = Route.TransferSpeed * SpeedMultiplier * CompletionRatio * DeltaTime;
            EffectiveSpeed = FMath::Min(EffectiveSpeed, 0.25f);

            float FloraDiff = CityA->Inventory.FloraFood - CityB->Inventory.FloraFood;
            float FloraTransfer = FloraDiff * EffectiveSpeed;
            CityA->Inventory.FloraFood -= FloraTransfer; CityB->Inventory.FloraFood += FloraTransfer;

            float WoodDiff = CityA->Inventory.Wood - CityB->Inventory.Wood;
            float WoodTransfer = WoodDiff * EffectiveSpeed;
            CityA->Inventory.Wood -= WoodTransfer; CityB->Inventory.Wood += WoodTransfer;

            float MeatDiff = CityA->Inventory.MeatFood - CityB->Inventory.MeatFood;
            float MeatTransfer = MeatDiff * EffectiveSpeed;
            CityA->Inventory.MeatFood -= MeatTransfer; CityB->Inventory.MeatFood += MeatTransfer;

            float StoneDiff = CityA->Inventory.Stone - CityB->Inventory.Stone;
            float StoneTransfer = StoneDiff * EffectiveSpeed;
            CityA->Inventory.Stone -= StoneTransfer; CityB->Inventory.Stone += StoneTransfer;

            float WealthDiff = CityA->Inventory.Wealth - CityB->Inventory.Wealth;
            float WealthTransfer = WealthDiff * EffectiveSpeed * 2.0f;
            CityA->Inventory.Wealth -= WealthTransfer; CityB->Inventory.Wealth += WealthTransfer;

            float OilDiff = CityA->Inventory.Oil - CityB->Inventory.Oil;
            float OilTransfer = OilDiff * EffectiveSpeed;
            CityA->Inventory.Oil -= OilTransfer; CityB->Inventory.Oil += OilTransfer;

            float UraniumDiff = CityA->Inventory.Uranium - CityB->Inventory.Uranium;
            float UraniumTransfer = UraniumDiff * EffectiveSpeed;
            CityA->Inventory.Uranium -= UraniumTransfer; CityB->Inventory.Uranium += UraniumTransfer;
        }
    }
}

void UTransportSystem::BuildTransportMesh(TSharedPtr<FChunkMeshData> MeshData, const TArray<FSettlementData>& Settlements, FIntPoint ChunkCoord, ASimWorldManager* Manager)
{
    if (!Manager || !MeshData.IsValid()) return;

    float CellSize = 50.0f;
    float ChunkWorldSize = (Manager->ChunkSize - 1) * CellSize;
    FLinearColor DirtColor = FLinearColor(0.40f, 0.32f, 0.22f, 1.0f);
    FLinearColor StoneColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

    auto AddTri = [&](FVector A, FVector B, FVector C, FLinearColor Color) {
        FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
        float Light = 0.4f + FMath::Max(0.0f, FVector::DotProduct(N, FVector(-0.6f, -0.6f, 0.7f).GetSafeNormal())) * 0.8f;
        FLinearColor FinalColor = Color * Light; FinalColor.A = 1.0f;

        int32 V = MeshData->TransportVertices.Num();
        MeshData->TransportVertices.Add(A); MeshData->TransportVertices.Add(B); MeshData->TransportVertices.Add(C);
        for (int i = 0; i < 3; i++) { MeshData->TransportTriangles.Add(V + i); MeshData->TransportUV0.Add(FVector2D::ZeroVector); MeshData->TransportColors.Add(FinalColor); }
        };

    auto AddQuad = [&](FVector A, FVector B, FVector C, FVector D, FLinearColor Color) {
        AddTri(A, B, C, Color); AddTri(C, D, A, Color);
        };

    auto AddBox = [&](FVector Center, FVector Extent, FLinearColor Color) {
        FVector P[8];
        P[0] = Center + FVector(-Extent.X, -Extent.Y, -Extent.Z); P[1] = Center + FVector(Extent.X, -Extent.Y, -Extent.Z);
        P[2] = Center + FVector(Extent.X, Extent.Y, -Extent.Z); P[3] = Center + FVector(-Extent.X, Extent.Y, -Extent.Z);
        P[4] = Center + FVector(-Extent.X, -Extent.Y, Extent.Z); P[5] = Center + FVector(Extent.X, -Extent.Y, Extent.Z);
        P[6] = Center + FVector(Extent.X, Extent.Y, Extent.Z); P[7] = Center + FVector(-Extent.X, Extent.Y, Extent.Z);

        AddQuad(P[3], P[2], P[1], P[0], Color); AddQuad(P[4], P[5], P[6], P[7], Color);
        AddQuad(P[0], P[1], P[5], P[4], Color); AddQuad(P[1], P[2], P[6], P[5], Color);
        AddQuad(P[2], P[3], P[7], P[6], Color); AddQuad(P[3], P[0], P[4], P[7], Color);
        };

    for (const FRoadNetwork& Road : RoadNetworks) {
        if (Road.BuiltNodes < 2 || Road.bIsImpossible) continue;

        bool bIsNational = false;
        for (const FTradeRoute& R : TradeRoutes) {
            if ((R.CityA_ID == Road.CityA_ID && R.CityB_ID == Road.CityB_ID) || (R.CityA_ID == Road.CityB_ID && R.CityB_ID == Road.CityA_ID)) {
                bIsNational = R.bIsNational; break;
            }
        }

        for (int i = 0; i < Road.BuiltNodes - 1; i++) {
            FVector2D P1 = Road.Path[i];
            FVector2D P2 = Road.Path[i + 1];

            int32 CX1 = FMath::FloorToInt(P1.X / ChunkWorldSize);
            int32 CY1 = FMath::FloorToInt(P1.Y / ChunkWorldSize);

            if (CX1 == ChunkCoord.X && CY1 == ChunkCoord.Y) {
                FCellStaticData SCell1, SCell2; FCellDynamicData DCell1, DCell2;
                float Z1 = Manager->SeaLevel + 5.0f; float Z2 = Manager->SeaLevel + 5.0f;
                if (Manager->GetCellDataAtLocation(FVector(P1.X, P1.Y, 0), SCell1, DCell1)) Z1 = SCell1.Elevation + 2.0f;
                if (Manager->GetCellDataAtLocation(FVector(P2.X, P2.Y, 0), SCell2, DCell2)) Z2 = SCell2.Elevation + 2.0f;

                FVector2D Dir = (P2 - P1).GetSafeNormal();
                FVector2D Right(-Dir.Y, Dir.X);
                float Width = bIsNational ? 25.0f : 12.0f;

                int32 V = MeshData->TransportVertices.Num();
                MeshData->TransportVertices.Add(FVector(P1.X + Right.X * Width, P1.Y + Right.Y * Width, Z1));
                MeshData->TransportVertices.Add(FVector(P1.X - Right.X * Width, P1.Y - Right.Y * Width, Z1));
                MeshData->TransportVertices.Add(FVector(P2.X + Right.X * Width, P2.Y + Right.Y * Width, Z2));
                MeshData->TransportVertices.Add(FVector(P2.X - Right.X * Width, P2.Y - Right.Y * Width, Z2));

                FLinearColor LineColor = bIsNational ? StoneColor : DirtColor;
                for (int c = 0; c < 4; c++) { MeshData->TransportUV0.Add(FVector2D(0, 0)); MeshData->TransportColors.Add(LineColor); }

                MeshData->TransportTriangles.Add(V); MeshData->TransportTriangles.Add(V + 2); MeshData->TransportTriangles.Add(V + 1);
                MeshData->TransportTriangles.Add(V + 1); MeshData->TransportTriangles.Add(V + 2); MeshData->TransportTriangles.Add(V + 3);
            }
        }
    }

    for (const FVehicleData& V : ActiveVehicles) {
        int32 CX = FMath::FloorToInt(V.Position.X / ChunkWorldSize);
        int32 CY = FMath::FloorToInt(V.Position.Y / ChunkWorldSize);

        if (CX == ChunkCoord.X && CY == ChunkCoord.Y) {
            float Z = Manager->SeaLevel;

            if (V.Type == EVehicleType::Caravan) {
                FCellStaticData SC; FCellDynamicData DC;
                if (Manager->GetCellDataAtLocation(FVector(V.Position.X, V.Position.Y, 0), SC, DC)) Z = SC.Elevation;
                AddBox(FVector(V.Position.X, V.Position.Y, Z + 5.0f), FVector(4.0f, 4.0f, 5.0f), FLinearColor(0.6f, 0.4f, 0.2f, 1.0f));
            }
            else if (V.Type == EVehicleType::Ship) {
                AddBox(FVector(V.Position.X, V.Position.Y, Z + 2.0f), FVector(12.0f, 6.0f, 4.0f), FLinearColor(0.8f, 0.8f, 0.8f, 1.0f));
                AddBox(FVector(V.Position.X, V.Position.Y, Z + 8.0f), FVector(4.0f, 4.0f, 6.0f), FLinearColor(0.9f, 0.2f, 0.1f, 1.0f));
            }
            else if (V.Type == EVehicleType::Airplane) {
                Z = Manager->SeaLevel + 1500.0f;
                AddBox(FVector(V.Position.X, V.Position.Y, Z), FVector(15.0f, 4.0f, 2.0f), FLinearColor(0.9f, 0.9f, 0.9f, 1.0f));
                AddBox(FVector(V.Position.X, V.Position.Y, Z), FVector(4.0f, 15.0f, 2.0f), FLinearColor(0.9f, 0.9f, 0.9f, 1.0f));
            }
        }
    }
}