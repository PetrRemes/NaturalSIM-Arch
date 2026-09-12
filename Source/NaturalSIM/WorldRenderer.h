#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SimWorldTypes.h"
#include "ProceduralMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "WorldRenderer.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UWorldRenderer : public USceneComponent
{
    GENERATED_BODY()

public:
    UWorldRenderer();
    virtual void BeginPlay() override;

    void InitializeRenderer(ASimWorldManager* InManager);
    void ClearAllMeshes();

    UPROPERTY(VisibleAnywhere, Category = "Rendering") UProceduralMeshComponent* TerrainMesh;
    UPROPERTY(VisibleAnywhere, Category = "Rendering") UProceduralMeshComponent* WaterMesh;
    UPROPERTY(VisibleAnywhere, Category = "Rendering") UProceduralMeshComponent* FloraMesh;
    UPROPERTY(VisibleAnywhere, Category = "Rendering") UProceduralMeshComponent* TransportMesh;

    UPROPERTY(VisibleAnywhere, Category = "Rendering") UHierarchicalInstancedStaticMeshComponent* FaunaHISM;
    UPROPERTY(VisibleAnywhere, Category = "Rendering") UHierarchicalInstancedStaticMeshComponent* HumanHISM;
    UPROPERTY(VisibleAnywhere, Category = "Rendering") UHierarchicalInstancedStaticMeshComponent* SettlementHISM;
    UPROPERTY(VisibleAnywhere, Category = "Rendering") UHierarchicalInstancedStaticMeshComponent* CaravanHISM;
    UPROPERTY(VisibleAnywhere, Category = "Rendering") UHierarchicalInstancedStaticMeshComponent* ShipHISM;
    UPROPERTY(VisibleAnywhere, Category = "Rendering") UHierarchicalInstancedStaticMeshComponent* AirplaneHISM;

    UPROPERTY(VisibleAnywhere, Category = "Rendering") UHierarchicalInstancedStaticMeshComponent* CloudHISM;
    UPROPERTY(VisibleAnywhere, Category = "Rendering") UHierarchicalInstancedStaticMeshComponent* RainHISM;
    UPROPERTY(VisibleAnywhere, Category = "Rendering") UHierarchicalInstancedStaticMeshComponent* FogHISM;
    UPROPERTY(VisibleAnywhere, Category = "Rendering") UHierarchicalInstancedStaticMeshComponent* DisasterHISM;

    static FLinearColor GetHeatmapColor(const FCellStaticData& SCell, const FCellDynamicData& DCell, EWorldViewMode ViewMode);

    static void BuildTerrainMesh(TSharedPtr<FChunkMeshData> MeshData, const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params);
    static void BuildWaterMesh(TSharedPtr<FChunkMeshData> MeshData, const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params);
    static void BuildChunkMesh_Async(TSharedPtr<FChunkMeshData> MeshData, const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params, uint8 DirtyFlags, ASimWorldManager* Manager);

    void RenderChunk_GameThread(TSharedPtr<FChunkMeshData> MeshData, FVector2D ChunkCoord, uint8 RenderedFlags);

    void UpdateFastEntities();
    void UpdateSettlementEntities();
    void UpdateTransportEntities();
    void UpdateWeatherEntities();
    void UpdateDisasterEntities();

private:
    ASimWorldManager* WorldManager;
};