#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "TransportSystem.generated.h"

class ASimWorldManager;

// F¡ZE 3: LogistickÈ entity pro fyzick˝ pohyb po mapÏ
UENUM(BlueprintType)
enum class EVehicleType : uint8
{
    Caravan,
    Ship,
    Airplane
};

USTRUCT(BlueprintType)
struct FVehicleData
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) FVector2D Position = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadWrite) FVector2D TargetPosition = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadWrite) EVehicleType Type = EVehicleType::Caravan;
    UPROPERTY(BlueprintReadWrite) int32 OriginCityID = -1;
    UPROPERTY(BlueprintReadWrite) int32 TargetCityID = -1;
    UPROPERTY(BlueprintReadWrite) float Progress = 0.0f;
    UPROPERTY(BlueprintReadWrite) int32 CurrentPathNode = 0;
    UPROPERTY(BlueprintReadWrite) TArray<FVector2D> Path;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UTransportSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UTransportSystem();

    UPROPERTY(BlueprintReadWrite, Category = "Transport")
    TArray<FTradeRoute> TradeRoutes;

    UPROPERTY(BlueprintReadWrite, Category = "Transport")
    TArray<FRoadNetwork> RoadNetworks;

    UPROPERTY(BlueprintReadWrite, Category = "Transport")
    TArray<FVehicleData> ActiveVehicles;

    void PlanNationalRoads(class ASimWorldManager* Manager);

    void EstablishTradeRoute(int32 CityA_ID, int32 CityB_ID, bool bIsNational);
    void ProcessTransport(TArray<FSettlementData>& Settlements, float DeltaTime, class ASimWorldManager* Manager);

    void BuildTransportMesh(TSharedPtr<FChunkMeshData> MeshData, const TArray<FSettlementData>& Settlements, FIntPoint ChunkCoord, ASimWorldManager* Manager);

protected:
    virtual void BeginPlay() override;

private:
    TQueue<TPair<int32, int32>> PendingRoadRequests;

    // F·ze 1: OchrannÈ pamÏùovÈ mnoûiny pro asynchronnÌ v˝poËty, aby se stejn· cesta nehledala 2x
    TSet<FIntPoint> ActiveRoadCalculations;
    TSet<FIntPoint> ActiveSeaCalculations;

    static TArray<FVector2D> CalculateRoadPath_Async(FVector2D PosA, FVector2D PosB, class ASimWorldManager* Manager);
    static TArray<FVector2D> CalculateSeaPath_Async(FVector2D PosA, FVector2D PosB, class ASimWorldManager* Manager);

    void SpawnVehicleForRoute(const FTradeRoute& Route, const TArray<FSettlementData>& Settlements, class ASimWorldManager* Manager);
    void UpdateVehicles(float DeltaTime, class ASimWorldManager* Manager);
};