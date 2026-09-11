#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "HumanSystem.generated.h"

class ASimWorldManager;

USTRUCT()
struct FRegionScore
{
    GENERATED_BODY()
    float CampScore = 0.0f;
    float CityScore = 0.0f;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UHumanSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UHumanSystem();
    virtual void BeginPlay() override;

    UPROPERTY(BlueprintReadWrite, Category = "Demographics")
    TArray<FTribeData> Tribes;

    TMap<FIntPoint, TArray<int32>> TribeSpatialGrid;

    void InitializeHumans(ASimWorldManager* Manager);

    void GenerateInitialProfile(FTribeData& Tribe, ASimWorldManager* Manager);

    FRegionScore EvaluateRegion(ASimWorldManager* Manager, FVector2D CenterPos, const FTribeData* Tribe);
    FVector2D CalculateBestMovementStep(ASimWorldManager* Manager, FVector2D CurrentPos, FVector2D TargetRegion);

    void ProcessHumansSlice(TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, float DeltaTime, int32 StartIdx, int32 EndIdx);
    void ProcessDailyDemographics(ASimWorldManager* Manager);

    void BuildHumanMesh(TSharedPtr<FChunkMeshData> MeshData, ASimWorldManager* Manager);

    UFUNCTION(BlueprintCallable, Category = "Divine Actions")
    bool TryBoostCulturalPillar(int32 TribeID, ECulturalPillar Pillar, ASimWorldManager* Manager);

    UFUNCTION(BlueprintCallable, Category = "Divine Actions")
    bool TryForceMigration(int32 TribeID, FVector2D TargetLoc, ASimWorldManager* Manager);
};