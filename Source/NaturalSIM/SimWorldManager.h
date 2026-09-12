#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimWorldTypes.h"
#include "Containers/Queue.h" 
#include "SimWorldManager.generated.h"

struct FChunkRenderTask {
	FIntPoint ChunkCoord;
	TSharedPtr<FChunkMeshData> MeshData;
	uint8 RenderedFlags;
};

UCLASS()
class NATURALSIM_API ASimWorldManager : public AActor
{
	GENERATED_BODY()

public:
	ASimWorldManager();
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UWorldRenderer* RendererModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UWorldGeneratorSystem* GeneratorModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UTectonicSystem* TectonicModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UHydroSystem* HydroModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UClimateSystem* ClimateModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UFloraSystem* FloraModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UFaunaSystem* FaunaModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UHumanSystem* HumanModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class USettlementSystem* SettlementModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UNationSystem* NationModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UTechnologySystem* TechModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UManaSystem* ManaModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class USimulationDirector* SimDirector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UHeatmapSystem* HeatmapModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class URelationSystem* RelationModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UTransportSystem* TransportModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UInternalPoliticsSystem* InternalPoliticsModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UDiplomacySystem* DiplomacyModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UHistorySystem* HistoryModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UCosmosSystem* CosmosModule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation") class UDisasterSystem* DisasterModule;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Cosmos") EStarType StarType = EStarType::YellowDwarf;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Cosmos", meta = (UIMin = "0", UIMax = "12")) int32 NumPlanets = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Cosmos", meta = (UIMin = "0", UIMax = "4")) int32 NumMoons = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Dimensions", meta = (UIMin = "1", UIMax = "20")) int32 WorldSizeInChunksX = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Dimensions", meta = (UIMin = "1", UIMax = "20")) int32 WorldSizeInChunksY = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Dimensions", meta = (UIMin = "10", UIMax = "200")) int32 ChunkSize = 100;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Global") int32 MapSeed = 1337;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Global", meta = (UIMin = "0.0", UIMax = "500.0")) float SeaLevel = 140.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Terrain", meta = (UIMin = "0.1", UIMax = "2.0")) float LowlandFlatness = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Terrain", meta = (UIMin = "100.0", UIMax = "5000.0")) float ElevationMultiplier = 1000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Terrain", meta = (UIMin = "0.0", UIMax = "2.0")) float CoastlineRoughness = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Terrain", meta = (UIMin = "0.0", UIMax = "1.0")) float IslandFrequency = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Tectonics", meta = (UIMin = "1", UIMax = "10")) int32 ContinentCount = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Tectonics", meta = (UIMin = "0.1", UIMax = "5.0")) float ContinentSizeMultiplier = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Tectonics", meta = (UIMin = "500.0", UIMax = "5000.0")) float TectonicMountainHeight = 2500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Tectonics", meta = (UIMin = "0.0", UIMax = "5.0")) float VolcanicActivity = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Tectonics", meta = (UIMin = "0.0", UIMax = "50.0")) float TectonicShift = 10.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Tectonics", meta = (UIMin = "0.0", UIMax = "2000.0")) float LavaHeightBoost = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Noise", meta = (UIMin = "0.001", UIMax = "0.1")) float NoiseScale = 0.005f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Noise", meta = (UIMin = "1", UIMax = "8")) int32 NoiseOctaves = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Noise", meta = (UIMin = "0.1", UIMax = "1.0")) float NoisePersistence = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Hydro", meta = (UIMin = "0.0", UIMax = "5.0")) float RiverSpawnMultiplier = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Hydro", meta = (UIMin = "0.0", UIMax = "5.0")) float RiverMeanderStrength = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Hydro", meta = (UIMin = "0.0", UIMax = "2.0")) float ErosionMultiplier = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Climate", meta = (UIMin = "-20.0", UIMax = "50.0")) float BaseTemperature = 25.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Climate", meta = (UIMin = "0.0", UIMax = "2.0")) float GlobalHumidityRate = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Climate", meta = (UIMin = "0.0", UIMax = "5.0")) float GlobalRainfall = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Climate", meta = (UIMin = "0.1", UIMax = "3.0")) float TemperatureScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Climate", meta = (UIMin = "1.0", UIMax = "50.0")) float RainfallMultiplier = 14.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Climate", meta = (UIMin = "0.0", UIMax = "5.0")) float CloudSpeedMultiplier = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Climate", meta = (UIMin = "0.0", UIMax = "3.0")) float CloudDensityThreshold = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Climate", meta = (UIMin = "1", UIMax = "8")) int32 CloudResolutionStep = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Climate", meta = (UIMin = "0.0", UIMax = "3.0")) float CloudCoverageMultiplier = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Flora", meta = (UIMin = "0.1", UIMax = "5.0")) float FloraGrowthSpeed = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Flora", meta = (UIMin = "0.1", UIMax = "5.0")) float GrasslandSpread = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Ecosystem", meta = (UIMin = "0.0", UIMax = "5.0")) float EcologicalPollutionFactor = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Fauna", meta = (UIMin = "1", UIMax = "1000")) int32 MaxAnimals = 50;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Humans", meta = (UIMin = "100", UIMax = "1000000")) int32 MaxGlobalPopulation = 50000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Humans", meta = (UIMin = "0.001", UIMax = "0.1")) float HumanFoodConsumptionRate = 0.02f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Settlements", meta = (UIMin = "10", UIMax = "500")) int32 PopulationToSettle = 80;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Settlements", meta = (UIMin = "1", UIMax = "20")) int32 PopulationPerHouse = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (UIMin = "1", UIMax = "32")) int32 MaxConcurrentChunks = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance") bool bShowDebugHUD = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance") float RenderBudgetSeconds = 0.0025f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance") int32 MaxRenderChunksPerFrame = 2;

	UPROPERTY(BlueprintReadWrite, Category = "Performance") bool bFaunaVisualDirty = true;
	UPROPERTY(BlueprintReadWrite, Category = "Performance") bool bHumanVisualDirty = true;
	UPROPERTY(BlueprintReadWrite, Category = "Performance") bool bSettlementVisualDirty = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (UIMin = "0.01", UIMax = "10.0")) float RealSecondsPerDay = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (UIMin = "0.1", UIMax = "100.0")) float SimulationSpeedMultiplier = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Time") int32 CurrentDay = 1;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Time") int32 CurrentYear = 2026;

	UPROPERTY(BlueprintReadWrite, Category = "Era") bool bIndustrialEraReached = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "State") EWorldViewMode CurrentViewMode = EWorldViewMode::Normal;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Climate") float GlobalCloudTime = 0.0f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Climate") FVector2D GlobalCloudDrift = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "State") bool bIsSimulationActive = false;

	TMap<FIntPoint, FChunkData> WorldChunks;
	UPROPERTY() TArray<FIntPoint> CachedChunkKeys;
	bool bKeysDirty = false;

	UPROPERTY() TArray<FIntPoint> ActiveChunkKeys;
	UPROPERTY() TArray<FIntPoint> StableChunkKeys;
	void UpdateActiveRegions();

	TQueue<FIntPoint, EQueueMode::Mpsc> ThreadSafeDirtyQueue;
	TSet<FIntPoint> DirtyChunkSet;
	TQueue<FChunkRenderTask, EQueueMode::Mpsc> PendingRenderQueue;
	TSet<FIntPoint> ActiveGeneratingChunks;

	// OPTIMALIZACE 2: Glob·lnÌ pamÏùov˝ blok pro uloûenÌ p¯edpoËÌtan˝ch v˝öek
	TSharedPtr<TArray<struct FPrecomputedTerrain>> GlobalTerrainCache;

	bool GetMutableCellGlobal(int32 GlobalX, int32 GlobalY, FCellData*& OutCell, FIntPoint& OutChunkCoord);
	bool GetCellGlobal(int32 GlobalX, int32 GlobalY, FCellData& OutCell);
	bool GetCellGlobalPtr(int32 GlobalX, int32 GlobalY, const FCellData*& OutCell) const;

	void RegisterVisualChange(FIntPoint ChunkCoord, uint8 DirtyFlag, bool bForceImmediate = false);
	void SyncChunkEdges(const TSet<FIntPoint>& ActiveChunks);
	bool IsTaskValid(int32 Token) const { return Token == GenerationToken; }

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Actions") void RegenerateWorld();
	UFUNCTION(BlueprintCallable, Category = "Actions") void ToggleSimulation();
	UFUNCTION(BlueprintCallable, Category = "Actions") void SetViewMode(EWorldViewMode NewMode);

	UFUNCTION(BlueprintCallable, Category = "Actions") void SetTimeSpeed(float NewSecondsPerDay);
	UFUNCTION(BlueprintCallable, Category = "Actions") void SetSimulationSpeedMultiplier(float NewMultiplier);
	UFUNCTION(BlueprintCallable, Category = "Actions") void TriggerEarthquake(FIntPoint EpicenterChunkCoord, float Radius, float Intensity);

	void NotifyChunkRendered();
	FVector GetPlayerLocation() const;

	bool GetTribeAtLocation(FVector WorldLocation, FTribeData& OutTribe, float Radius = 100.0f);
	bool GetSettlementAtLocation(FVector WorldLocation, FSettlementData& OutSettlement, float Radius = 200.0f);
	bool GetAnimalAtLocation(FVector WorldLocation, FAnimalData& OutAnimal, float Radius = 100.0f);
	bool GetCellDataAtLocation(FVector WorldLocation, FCellData& OutCell);
	int32 GetTotalPopulation() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool ShouldTickIfViewportsOnly() const override;
	virtual void PostActorCreated() override;
#endif

private:
	TQueue<FIntPoint> ChunkGenerationQueue;
	TSet<FIntPoint> QueuedChunksSet;

	TArray<FContinentData> Continents;
	int32 ActiveChunkTasks = 0;
	bool bIsGenerating = false;
	bool bCurrentQueueIsFullGeneration = false;
	int32 GenerationToken = 0;

	float StatUpdateTimer = 0.0f;
	FString CachedHUDText;
	int32 CachedTotalPop = 0;
	float CachedTotalFood = 0.0f;

public:
	int32 Stat_Battles = 0;
	int32 Stat_Trades = 0;
	int32 Stat_SettlementsFounded = 0;
	int32 Stat_NationsFormed = 0;
	int32 Stat_CitiesAbsorbed = 0;
	int32 Stat_WarsDeclared = 0;

private:
	void GenerateContinentCenters();
	void ProcessNextChunk();
	void GenerateChunk(FIntPoint ChunkCoordinate, bool bIsFullGeneration, uint8 DirtyFlags);
	void UpdateDynamicMeshes();
	FChunkGenerationParameters GetGenerationParams() const;
};