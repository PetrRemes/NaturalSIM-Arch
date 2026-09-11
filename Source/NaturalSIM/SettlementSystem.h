#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "SettlementSystem.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API USettlementSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    USettlementSystem();

    UPROPERTY(BlueprintReadWrite, Category = "Simulation")
    TArray<FSettlementData> Settlements;

    TMap<FIntPoint, TArray<int32>> SettlementSpatialGrid;
    int32 GlobalSettlementCounter = 0;

    void CreateSettlement(FVector2D InPosition, int32 InPopulation, FTribeInventory InInventory, FLinearColor InColor, FKnowledgeContainer InKnowledge, FCultureProfile InCulture);
    void ProcessSettlements(TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, float DeltaTime);
    void ProcessDailyDemographics(ASimWorldManager* Manager);

    FLinearColor GetPoliticalColor(const FSettlementData& S) const;
    void RecalculatePoliticalColors(ASimWorldManager* Manager);

    void BuildSettlementMesh(TSharedPtr<FChunkMeshData> MeshData, FIntPoint ChunkCoord, ASimWorldManager* Manager);

    UFUNCTION(BlueprintCallable, Category = "Divine Actions")
    bool TryBoostCulturalPillar(int32 SettlementID, ECulturalPillar Pillar, ASimWorldManager* Manager);

    UFUNCTION(BlueprintCallable, Category = "Divine Actions")
    bool TryForceExpansion(int32 SettlementID, FVector2D TargetLoc, ASimWorldManager* Manager);

protected:
    virtual void BeginPlay() override;
};