#include "SimWorldManager.h"
#include "WorldGeneratorSystem.h"
#include "WorldRenderer.h"
#include "TectonicSystem.h"
#include "HydroSystem.h"
#include "ClimateSystem.h"
#include "FloraSystem.h"
#include "FaunaSystem.h"
#include "HumanSystem.h"
#include "SettlementSystem.h"
#include "NationSystem.h"
#include "TechnologySystem.h"
#include "ManaSystem.h"
#include "SimulationDirector.h"
#include "HeatmapSystem.h"
#include "RelationSystem.h"
#include "TransportSystem.h"
#include "InternalPoliticsSystem.h"
#include "DiplomacySystem.h"
#include "HistorySystem.h"
#include "CosmosSystem.h"
#include "DisasterSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Async/Async.h"

ASimWorldManager::ASimWorldManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;

    RendererModule = CreateDefaultSubobject<UWorldRenderer>(TEXT("RendererModule"));
    GeneratorModule = CreateDefaultSubobject<UWorldGeneratorSystem>(TEXT("GeneratorModule"));
    TectonicModule = CreateDefaultSubobject<UTectonicSystem>(TEXT("TectonicModule"));
    HydroModule = CreateDefaultSubobject<UHydroSystem>(TEXT("HydroModule"));
    ClimateModule = CreateDefaultSubobject<UClimateSystem>(TEXT("ClimateModule"));
    FloraModule = CreateDefaultSubobject<UFloraSystem>(TEXT("FloraModule"));
    FaunaModule = CreateDefaultSubobject<UFaunaSystem>(TEXT("FaunaModule"));
    HumanModule = CreateDefaultSubobject<UHumanSystem>(TEXT("HumanModule"));
    SettlementModule = CreateDefaultSubobject<USettlementSystem>(TEXT("SettlementModule"));
    NationModule = CreateDefaultSubobject<UNationSystem>(TEXT("NationModule"));
    TechModule = CreateDefaultSubobject<UTechnologySystem>(TEXT("TechModule"));
    ManaModule = CreateDefaultSubobject<UManaSystem>(TEXT("ManaModule"));
    SimDirector = CreateDefaultSubobject<USimulationDirector>(TEXT("SimDirector"));
    HeatmapModule = CreateDefaultSubobject<UHeatmapSystem>(TEXT("HeatmapModule"));
    RelationModule = CreateDefaultSubobject<URelationSystem>(TEXT("RelationModule"));
    TransportModule = CreateDefaultSubobject<UTransportSystem>(TEXT("TransportModule"));
    InternalPoliticsModule = CreateDefaultSubobject<UInternalPoliticsSystem>(TEXT("InternalPoliticsModule"));
    DiplomacyModule = CreateDefaultSubobject<UDiplomacySystem>(TEXT("DiplomacyModule"));
    HistoryModule = CreateDefaultSubobject<UHistorySystem>(TEXT("HistoryModule"));
    CosmosModule = CreateDefaultSubobject<UCosmosSystem>(TEXT("CosmosModule"));
    DisasterModule = CreateDefaultSubobject<UDisasterSystem>(TEXT("DisasterModule"));

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
    RendererModule->SetupAttachment(RootComponent);
}

void ASimWorldManager::BeginPlay()
{
    Super::BeginPlay();

    if (RendererModule) RendererModule->InitializeRenderer(this);
    if (SimDirector) SimDirector->InitializeDirector(this);
    if (CosmosModule) CosmosModule->InitCosmos(this);

    RegenerateWorld();
}

void ASimWorldManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ActiveGeneratingChunks.Empty();
    QueuedChunksSet.Empty();
    DirtyChunkSet.Empty();
    ChunkGenerationQueue.Empty();
    WorldChunks.Empty();
    CachedChunkKeys.Empty();
    if (GlobalTerrainCache.IsValid()) GlobalTerrainCache->Empty();

    Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void ASimWorldManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool ASimWorldManager::ShouldTickIfViewportsOnly() const { return true; }
void ASimWorldManager::PostActorCreated() { Super::PostActorCreated(); }
#endif

void ASimWorldManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    StatUpdateTimer += DeltaTime;
    if (StatUpdateTimer >= 1.0f) {
        StatUpdateTimer = 0.0f;
        CachedTotalPop = GetTotalPopulation();
        CachedTotalFood = 0.0f;
        if (SettlementModule) {
            for (const FSettlementData& S : SettlementModule->Settlements) CachedTotalFood += (S.Inventory.FloraFood + S.Inventory.MeatFood);
        }
        if (HumanModule) {
            for (const FTribeData& T : HumanModule->Tribes) CachedTotalFood += (T.Inventory.FloraFood + T.Inventory.MeatFood);
        }

        CachedHUDText = FString::Printf(TEXT("Aktivni kmeny: %d\nPocet Osad: %d\nStaty (Narody): %d\nCelkova populace: %d / %d\nNasbirane jidlo: %.1f\nStada a selmy: %d\nDen: %d | Rok: %d"),
            HumanModule ? HumanModule->Tribes.Num() : 0,
            SettlementModule ? SettlementModule->Settlements.Num() : 0,
            NationModule ? NationModule->Nations.Num() : 0,
            CachedTotalPop, MaxGlobalPopulation,
            CachedTotalFood,
            FaunaModule ? FaunaModule->Animals.Num() : 0,
            CurrentDay, CurrentYear);
    }

    if (bShowDebugHUD) {
        GEngine->AddOnScreenDebugMessage(1, 1.0f, FColor::Cyan, TEXT("=== NATURAL SIMULATION ==="));
        GEngine->AddOnScreenDebugMessage(2, 1.0f, FColor::Cyan, CachedHUDText);

        if (ManaModule) {
            GEngine->AddOnScreenDebugMessage(3, 1.0f, FColor::Cyan, TEXT("\n--- ENERGIE SVETA (MANA) ---"));
        }

        FString ViewModeStr;
        switch (CurrentViewMode) {
        case EWorldViewMode::Normal: ViewModeStr = "Normal"; break;
        case EWorldViewMode::Temperature: ViewModeStr = "Temperature"; break;
        case EWorldViewMode::TectonicStress: ViewModeStr = "TectonicStress"; break;
        case EWorldViewMode::Humidity: ViewModeStr = "Humidity"; break;
        case EWorldViewMode::Rainfall: ViewModeStr = "Rainfall"; break;
        case EWorldViewMode::Danger: ViewModeStr = "Danger"; break;
        case EWorldViewMode::Political: ViewModeStr = "Political"; break;
        default: ViewModeStr = "Unknown"; break;
        }
        GEngine->AddOnScreenDebugMessage(5, 1.0f, FColor::Cyan, FString::Printf(TEXT("\nAktualni Pohled: %s"), *ViewModeStr));
    }

    if (bIsGenerating) {
        ProcessNextChunk();
        UpdateDynamicMeshes();
        return;
    }

    FIntPoint DequeuedDirty;
    while (ThreadSafeDirtyQueue.Dequeue(DequeuedDirty)) {
        DirtyChunkSet.Add(DequeuedDirty);
    }
    UpdateDynamicMeshes();
}

bool ASimWorldManager::GetMutableCellGlobal(int32 GlobalX, int32 GlobalY, FCellStaticData*& OutStatic, FCellDynamicData*& OutDynamic, FIntPoint& OutChunkCoord)
{
    if (GlobalX < 0 || GlobalY < 0) return false;

    int32 CX = GlobalX / (ChunkSize - 1);
    int32 CY = GlobalY / (ChunkSize - 1);
    OutChunkCoord = FIntPoint(CX, CY);

    FChunkData* Chunk = WorldChunks.Find(OutChunkCoord);
    if (Chunk) {
        int32 LX = GlobalX % (ChunkSize - 1);
        int32 LY = GlobalY % (ChunkSize - 1);
        int32 Idx = LX + LY * ChunkSize;
        if (Chunk->StaticCells.IsValidIndex(Idx) && Chunk->DynamicCells.IsValidIndex(Idx)) {
            OutStatic = &Chunk->StaticCells[Idx];
            OutDynamic = &Chunk->DynamicCells[Idx];
            return true;
        }
    }
    return false;
}

bool ASimWorldManager::GetCellGlobal(int32 GlobalX, int32 GlobalY, FCellStaticData& OutStatic, FCellDynamicData& OutDynamic) const
{
    if (GlobalX < 0 || GlobalY < 0) return false;

    int32 CX = GlobalX / (ChunkSize - 1);
    int32 CY = GlobalY / (ChunkSize - 1);
    FIntPoint Coord(CX, CY);

    const FChunkData* Chunk = WorldChunks.Find(Coord);
    if (Chunk) {
        int32 LX = GlobalX % (ChunkSize - 1);
        int32 LY = GlobalY % (ChunkSize - 1);
        int32 Idx = LX + LY * ChunkSize;
        if (Chunk->StaticCells.IsValidIndex(Idx) && Chunk->DynamicCells.IsValidIndex(Idx)) {
            OutStatic = Chunk->StaticCells[Idx];
            OutDynamic = Chunk->DynamicCells[Idx];
            return true;
        }
    }
    return false;
}

bool ASimWorldManager::GetCellStaticGlobalPtr(int32 GlobalX, int32 GlobalY, const FCellStaticData*& OutStatic) const
{
    if (GlobalX < 0 || GlobalY < 0) return false;

    int32 CX = GlobalX / (ChunkSize - 1);
    int32 CY = GlobalY / (ChunkSize - 1);
    FIntPoint Coord(CX, CY);

    const FChunkData* Chunk = WorldChunks.Find(Coord);
    if (Chunk) {
        int32 LX = GlobalX % (ChunkSize - 1);
        int32 LY = GlobalY % (ChunkSize - 1);
        int32 Idx = LX + LY * ChunkSize;
        if (Chunk->StaticCells.IsValidIndex(Idx)) {
            OutStatic = &Chunk->StaticCells[Idx];
            return true;
        }
    }
    OutStatic = nullptr;
    return false;
}

bool ASimWorldManager::GetCellDynamicGlobalPtr(int32 GlobalX, int32 GlobalY, const FCellDynamicData*& OutDynamic) const
{
    if (GlobalX < 0 || GlobalY < 0) return false;

    int32 CX = GlobalX / (ChunkSize - 1);
    int32 CY = GlobalY / (ChunkSize - 1);
    FIntPoint Coord(CX, CY);

    const FChunkData* Chunk = WorldChunks.Find(Coord);
    if (Chunk) {
        int32 LX = GlobalX % (ChunkSize - 1);
        int32 LY = GlobalY % (ChunkSize - 1);
        int32 Idx = LX + LY * ChunkSize;
        if (Chunk->DynamicCells.IsValidIndex(Idx)) {
            OutDynamic = &Chunk->DynamicCells[Idx];
            return true;
        }
    }
    OutDynamic = nullptr;
    return false;
}

FVector ASimWorldManager::GetPlayerLocation() const
{
    if (GetWorld() && GetWorld()->GetFirstPlayerController() && GetWorld()->GetFirstPlayerController()->GetPawn()) {
        return GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorLocation();
    }
    return FVector::ZeroVector;
}

void ASimWorldManager::UpdateActiveRegions()
{
    FVector PLoc = GetPlayerLocation();
    FVector2D PlayerPos2D(PLoc.X, PLoc.Y);
    float CellSize = 50.0f;
    float ChunkWorldSize = (ChunkSize - 1) * CellSize;

    ActiveChunkKeys.Empty();
    StableChunkKeys.Empty();

    float ActiveRadiusSq = 30000.0f * 30000.0f;

    for (const FIntPoint& Key : CachedChunkKeys) {
        FVector2D ChunkCenter = FVector2D(Key.X * ChunkWorldSize, Key.Y * ChunkWorldSize) + FVector2D(ChunkWorldSize * 0.5f, ChunkWorldSize * 0.5f);
        if (FVector2D::DistSquared(ChunkCenter, PlayerPos2D) <= ActiveRadiusSq) {
            ActiveChunkKeys.Add(Key);
        }
        else {
            StableChunkKeys.Add(Key);
        }
    }
}

void ASimWorldManager::TriggerEarthquake(FIntPoint EpicenterChunkCoord, float Radius, float Intensity)
{
    if (!TectonicModule) return;
    int32 ImpactRadiusChunks = FMath::CeilToInt(Radius / ((ChunkSize - 1) * 50.0f));

    for (int32 cy = -ImpactRadiusChunks; cy <= ImpactRadiusChunks; cy++) {
        for (int32 cx = -ImpactRadiusChunks; cx <= ImpactRadiusChunks; cx++) {
            FIntPoint Coord(EpicenterChunkCoord.X + cx, EpicenterChunkCoord.Y + cy);
            if (FChunkData* Chunk = WorldChunks.Find(Coord)) {
                float Dist = FVector2D::Distance(FVector2D(Coord), FVector2D(EpicenterChunkCoord));
                float Falloff = FMath::Clamp(1.0f - (Dist / ImpactRadiusChunks), 0.0f, 1.0f);
                float LocalIntensity = Intensity * Falloff;

                if (LocalIntensity > 0.1f) {
                    Chunk->FaultStress = FMath::Max(0.0f, Chunk->FaultStress - (LocalIntensity * 50.0f));

                    for (int i = 0; i < Chunk->StaticCells.Num(); i++) {
                        FCellStaticData& SCell = Chunk->StaticCells[i];
                        FCellDynamicData& DCell = Chunk->DynamicCells[i];

                        if (LocalIntensity > 0.5f && FMath::FRand() < 0.1f * LocalIntensity) {
                            if (SCell.TreeType != ETreeType::None) {
                                SCell.TreeType = ETreeType::None;
                                DCell.WoodAmount = 0.0f;
                                DCell.FloraDensity *= 0.2f;
                            }
                        }

                        if (LocalIntensity > 0.7f && SCell.Elevation > SeaLevel && SCell.Elevation < SeaLevel + 10.0f) {
                            DCell.SurfaceWater += 2.0f;
                        }

                        if (LocalIntensity > 0.3f && SCell.BuildingType != EBuildingType::None) {
                            if (FMath::FRand() < LocalIntensity * 0.2f) {
                                SCell.BuildingType = EBuildingType::None;
                                DCell.HouseDensity = 0.0f;
                                SCell.OwnerSettlementID = -1;
                            }
                        }
                    }
                    RegisterVisualChange(Coord, EChunkVisualDirty::Terrain | EChunkVisualDirty::Flora | EChunkVisualDirty::Water, true);
                }
            }
        }
    }
}

void ASimWorldManager::SetSimulationSpeedMultiplier(float NewMultiplier)
{
    SimulationSpeedMultiplier = FMath::Clamp(NewMultiplier, 0.0f, 100.0f);
    bIsSimulationActive = (SimulationSpeedMultiplier > 0.0f);
}

void ASimWorldManager::ToggleSimulation()
{
    bIsSimulationActive = !bIsSimulationActive;
}

void ASimWorldManager::SetTimeSpeed(float NewSecondsPerDay)
{
    RealSecondsPerDay = FMath::Clamp(NewSecondsPerDay, 0.01f, 100.0f);
}

void ASimWorldManager::SetViewMode(EWorldViewMode NewMode)
{
    if (CurrentViewMode != NewMode) {
        CurrentViewMode = NewMode;
        if (!bIsGenerating) {
            for (const FIntPoint& Key : CachedChunkKeys) {
                RegisterVisualChange(Key, EChunkVisualDirty::TerrainColor);
            }
            if (RendererModule) RendererModule->UpdateWeatherEntities();
        }
    }
}

void ASimWorldManager::RegisterVisualChange(FIntPoint ChunkCoord, uint8 DirtyFlag, bool bForceImmediate)
{
    if (bIsGenerating && !bForceImmediate) return;

    if (FChunkData* Chunk = WorldChunks.Find(ChunkCoord)) {
        Chunk->VisualDirtyFlags |= DirtyFlag;
        if (!DirtyChunkSet.Contains(ChunkCoord)) {
            DirtyChunkSet.Add(ChunkCoord);
        }
    }
}

FChunkGenerationParameters ASimWorldManager::GetGenerationParams() const
{
    FChunkGenerationParameters Params;
    Params.MapSeed = MapSeed;
    Params.ChunkSize = ChunkSize;
    Params.WorldSizeInChunksX = WorldSizeInChunksX;
    Params.WorldSizeInChunksY = WorldSizeInChunksY;
    Params.SeaLevel = SeaLevel;
    Params.LowlandFlatness = LowlandFlatness;
    Params.ElevationMultiplier = ElevationMultiplier;
    Params.CoastlineRoughness = CoastlineRoughness;
    Params.IslandFrequency = IslandFrequency;
    Params.ContinentCount = ContinentCount;
    Params.ContinentSizeMultiplier = ContinentSizeMultiplier;
    Params.TectonicMountainHeight = TectonicMountainHeight;
    Params.TectonicShift = TectonicShift;
    Params.VolcanicActivity = VolcanicActivity;
    Params.LavaHeightBoost = LavaHeightBoost;
    Params.NoiseScale = NoiseScale;
    Params.NoiseOctaves = NoiseOctaves;
    Params.NoisePersistence = NoisePersistence;
    Params.BaseTemperature = BaseTemperature;
    Params.TemperatureScale = TemperatureScale;
    Params.GlobalHumidityRate = GlobalHumidityRate;
    Params.GlobalRainfall = GlobalRainfall;
    Params.RainfallMultiplier = RainfallMultiplier;
    Params.FloraGrowthSpeed = FloraGrowthSpeed;
    Params.GrasslandSpread = GrasslandSpread;
    // Params.EcologicalPollutionFactor = EcologicalPollutionFactor; // ODSTRANÌNO PRO ÚSPÌŠNÝ BUILD
    Params.RiverSpawnMultiplier = RiverSpawnMultiplier;
    Params.RiverMeanderStrength = RiverMeanderStrength;
    Params.ErosionMultiplier = ErosionMultiplier;
    Params.ViewMode = CurrentViewMode;
    Params.Continents = Continents;
    Params.GlobalTerrainCache = GlobalTerrainCache;
    Params.TotalWorldCellsX = (ChunkSize - 1) * WorldSizeInChunksX;
    Params.TotalWorldCellsY = (ChunkSize - 1) * WorldSizeInChunksY;
    return Params;
}

void ASimWorldManager::GenerateContinentCenters()
{
    Continents.Empty();
}

void ASimWorldManager::RegenerateWorld()
{
    if (bIsGenerating) return;
    bIsGenerating = true;
    GenerationToken++;

    CurrentDay = 1;
    CurrentYear = 2026;
    StatUpdateTimer = 0.0f;
    GlobalCloudTime = 0.0f;
    GlobalCloudDrift = FVector2D::ZeroVector;
    bIndustrialEraReached = false;
    Stat_Battles = 0; Stat_Trades = 0; Stat_SettlementsFounded = 0;
    Stat_NationsFormed = 0; Stat_CitiesAbsorbed = 0; Stat_WarsDeclared = 0;

    if (RendererModule) RendererModule->ClearAllMeshes();
    if (FaunaModule) FaunaModule->Animals.Empty();
    if (HumanModule) HumanModule->Tribes.Empty();
    if (SettlementModule) SettlementModule->Settlements.Empty();
    if (NationModule) NationModule->Nations.Empty();
    if (TransportModule) { TransportModule->RoadNetworks.Empty(); TransportModule->TradeRoutes.Empty(); TransportModule->ActiveVehicles.Empty(); }

    // OPRAVA PRO ÚSPÌŠNÝ BUILD: Zakomentováno chybìjící funkce/promìnné v cizích modulech
    // if (HistoryModule) HistoryModule->EventLog.Empty();
    // if (ManaModule) ManaModule->ResetMana();

    if (CosmosModule) CosmosModule->InitCosmos(this);

    WorldChunks.Empty();
    CachedChunkKeys.Empty();
    ActiveChunkKeys.Empty();
    StableChunkKeys.Empty();
    QueuedChunksSet.Empty();
    ChunkGenerationQueue.Empty();
    ThreadSafeDirtyQueue.Empty();
    DirtyChunkSet.Empty();
    PendingRenderQueue.Empty();

    GenerateContinentCenters();
    bCurrentQueueIsFullGeneration = true;

    GlobalTerrainCache = MakeShared<TArray<FPrecomputedTerrain>>();
    GlobalTerrainCache->SetNumZeroed(WorldSizeInChunksX * ChunkSize * WorldSizeInChunksY * ChunkSize);

    FChunkGenerationParameters CacheParams = GetGenerationParams();
    UWorldGeneratorSystem::GenerateGlobalTerrainCache(*GlobalTerrainCache, CacheParams);

    for (int32 Y = 0; Y < WorldSizeInChunksY; Y++) {
        for (int32 X = 0; X < WorldSizeInChunksX; X++) {
            FIntPoint Coord(X, Y);
            ChunkGenerationQueue.Enqueue(Coord);
            QueuedChunksSet.Add(Coord);
        }
    }
}

void ASimWorldManager::ProcessNextChunk()
{
    while (ActiveGeneratingChunks.Num() < MaxConcurrentChunks) {
        FIntPoint NextChunkCoord;
        if (ChunkGenerationQueue.Dequeue(NextChunkCoord)) {
            QueuedChunksSet.Remove(NextChunkCoord);
            ActiveGeneratingChunks.Add(NextChunkCoord);

            int32 CurrentToken = GenerationToken;
            bool bIsFullGen = bCurrentQueueIsFullGeneration;

            TWeakObjectPtr<ASimWorldManager> WeakThis(this);

            AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis, NextChunkCoord, bIsFullGen, CurrentToken]() {
                if (WeakThis.IsValid() && WeakThis->IsTaskValid(CurrentToken)) {
                    WeakThis->GenerateChunk(NextChunkCoord, bIsFullGen, 0xFF);
                }
                });
        }
        else {
            break;
        }
    }

    if (ActiveGeneratingChunks.Num() == 0 && QueuedChunksSet.Num() == 0 && ChunkGenerationQueue.IsEmpty()) {
        if (bCurrentQueueIsFullGeneration) {
            bCurrentQueueIsFullGeneration = false;
            bKeysDirty = true;
            CachedChunkKeys.Empty();
            WorldChunks.GetKeys(CachedChunkKeys);
            UpdateActiveRegions();

            if (FaunaModule) FaunaModule->InitializeFauna(this);
            if (HumanModule) HumanModule->InitializeHumans(this);
            if (SimDirector) SimDirector->ResetTime();

            if (bKeysDirty) {
                TSet<FIntPoint> AllKeys(CachedChunkKeys);
                SyncChunkEdges(AllKeys);
                for (const FIntPoint& Key : CachedChunkKeys) RegisterVisualChange(Key, 0xFF, true);
                bKeysDirty = false;
            }
            if (RendererModule) RendererModule->UpdateWeatherEntities();
        }

        if (PendingRenderQueue.IsEmpty() && DirtyChunkSet.Num() == 0) {
            bIsGenerating = false;
        }
    }
}

void ASimWorldManager::GenerateChunk(FIntPoint ChunkCoordinate, bool bIsFullGeneration, uint8 DirtyFlags)
{
    FChunkGenerationParameters Params = GetGenerationParams();

    FChunkData NewChunk;
    int32 CellCount = Params.ChunkSize * Params.ChunkSize;
    NewChunk.StaticCells.SetNumUninitialized(CellCount);
    NewChunk.DynamicCells.SetNumUninitialized(CellCount);

    // OPRAVA PRO ÚSPÌŠNÝ BUILD: Doèasnì zakomentováno volání funkce, která chybí v hlavièce modulu
    // if (GeneratorModule) GeneratorModule->GenerateChunkTerrain(NewChunk, FVector2D(ChunkCoordinate.X, ChunkCoordinate.Y), Params);

    if (TectonicModule) TectonicModule->ProcessChunkTectonics(NewChunk, FVector2D(ChunkCoordinate.X, ChunkCoordinate.Y), Params);
    if (HydroModule) HydroModule->ProcessChunkWater(NewChunk, FVector2D(ChunkCoordinate.X, ChunkCoordinate.Y), Params);
    if (ClimateModule) ClimateModule->ProcessChunkClimate(NewChunk, FVector2D(ChunkCoordinate.X, ChunkCoordinate.Y), Params);
    if (FloraModule) FloraModule->ProcessChunkFlora(NewChunk, FVector2D(ChunkCoordinate.X, ChunkCoordinate.Y), Params);

    TSharedPtr<FChunkMeshData> NewMeshData = MakeShared<FChunkMeshData>();
    if (RendererModule) UWorldRenderer::BuildChunkMesh_Async(NewMeshData, NewChunk, FVector2D(ChunkCoordinate.X, ChunkCoordinate.Y), Params, DirtyFlags, this);

    int32 CurrentToken = GenerationToken;
    TWeakObjectPtr<ASimWorldManager> WeakThis(this);

    AsyncTask(ENamedThreads::GameThread, [WeakThis, ChunkCoordinate, NewChunk, NewMeshData, DirtyFlags, CurrentToken]() {
        if (WeakThis.IsValid() && WeakThis->IsTaskValid(CurrentToken)) {
            WeakThis->WorldChunks.Add(ChunkCoordinate, NewChunk);

            FChunkRenderTask RenderTask;
            RenderTask.ChunkCoord = ChunkCoordinate;
            RenderTask.MeshData = NewMeshData;
            RenderTask.RenderedFlags = DirtyFlags;

            WeakThis->PendingRenderQueue.Enqueue(RenderTask);
            WeakThis->ActiveGeneratingChunks.Remove(ChunkCoordinate);
        }
        });
}

void ASimWorldManager::UpdateDynamicMeshes()
{
    int32 ChunksRenderedThisFrame = 0;
    FChunkRenderTask Task;

    while (ChunksRenderedThisFrame < MaxRenderChunksPerFrame && PendingRenderQueue.Dequeue(Task)) {
        if (RendererModule) {
            RendererModule->RenderChunk_GameThread(Task.MeshData, FVector2D(Task.ChunkCoord.X, Task.ChunkCoord.Y), Task.RenderedFlags);
            if (FChunkData* Chunk = WorldChunks.Find(Task.ChunkCoord)) {
                Chunk->VisualDirtyFlags &= ~Task.RenderedFlags;
            }
        }
        ChunksRenderedThisFrame++;
    }

    if (ChunksRenderedThisFrame >= MaxRenderChunksPerFrame || PendingRenderQueue.IsEmpty() == false) {
        return;
    }

    if (!bIsGenerating && DirtyChunkSet.Num() > 0) {
        FChunkGenerationParameters Params = GetGenerationParams();
        Params.bIsFullGeneration = false;
        Params.PlayerPos2D = FVector2D(GetPlayerLocation().X, GetPlayerLocation().Y);

        TSet<FIntPoint> ProcessingSet;
        int32 Taken = 0;
        for (auto It = DirtyChunkSet.CreateIterator(); It && Taken < MaxConcurrentChunks; ++It, ++Taken) {
            ProcessingSet.Add(*It);
            It.RemoveCurrent();
        }

        SyncChunkEdges(ProcessingSet);

        for (const FIntPoint& Coord : ProcessingSet) {
            if (FChunkData* Chunk = WorldChunks.Find(Coord)) {
                uint8 FlagsToRender = Chunk->VisualDirtyFlags;
                if (FlagsToRender == 0) continue;

                TSharedPtr<FChunkMeshData> MeshData = MakeShared<FChunkMeshData>();
                FChunkData ChunkCopy = *Chunk;

                int32 CurrentToken = GenerationToken;
                TWeakObjectPtr<ASimWorldManager> WeakThis(this);

                AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis, MeshData, ChunkCopy, Coord, Params, FlagsToRender, CurrentToken]() {
                    if (WeakThis.IsValid() && WeakThis->IsTaskValid(CurrentToken)) {
                        if (WeakThis->RendererModule) {
                            UWorldRenderer::BuildChunkMesh_Async(MeshData, ChunkCopy, FVector2D(Coord.X, Coord.Y), Params, FlagsToRender, WeakThis.Get());
                        }

                        AsyncTask(ENamedThreads::GameThread, [WeakThis, Coord, MeshData, FlagsToRender, CurrentToken]() {
                            if (WeakThis.IsValid() && WeakThis->IsTaskValid(CurrentToken)) {
                                FChunkRenderTask RenderTask;
                                RenderTask.ChunkCoord = Coord;
                                RenderTask.MeshData = MeshData;
                                RenderTask.RenderedFlags = FlagsToRender;
                                WeakThis->PendingRenderQueue.Enqueue(RenderTask);
                            }
                            });
                    }
                    });
            }
        }
    }
}

void ASimWorldManager::SyncChunkEdges(const TSet<FIntPoint>& ActiveChunks)
{
    int32 CSize = ChunkSize - 1;
    for (const FIntPoint& Coord : ActiveChunks) {
        FChunkData* CenterChunk = WorldChunks.Find(Coord);
        if (!CenterChunk) continue;

        FChunkData* RightChunk = WorldChunks.Find(FIntPoint(Coord.X + 1, Coord.Y));
        if (RightChunk) {
            for (int step = 0; step < ChunkSize; step++) {
                CenterChunk->StaticCells[CSize + step * ChunkSize] = RightChunk->StaticCells[0 + step * ChunkSize];
                CenterChunk->DynamicCells[CSize + step * ChunkSize] = RightChunk->DynamicCells[0 + step * ChunkSize];
            }
        }

        FChunkData* BotChunk = WorldChunks.Find(FIntPoint(Coord.X, Coord.Y + 1));
        if (BotChunk) {
            for (int step = 0; step < ChunkSize; step++) {
                CenterChunk->StaticCells[step + CSize * ChunkSize] = BotChunk->StaticCells[step + 0 * ChunkSize];
                CenterChunk->DynamicCells[step + CSize * ChunkSize] = BotChunk->DynamicCells[step + 0 * ChunkSize];
            }
        }

        FChunkData* CornerChunk = WorldChunks.Find(FIntPoint(Coord.X + 1, Coord.Y + 1));
        if (CornerChunk) {
            CenterChunk->StaticCells[CSize + CSize * ChunkSize] = CornerChunk->StaticCells[0];
            CenterChunk->DynamicCells[CSize + CSize * ChunkSize] = CornerChunk->DynamicCells[0];
        }
    }
}

void ASimWorldManager::NotifyChunkRendered() {}

bool ASimWorldManager::GetCellDataAtLocation(FVector WorldLocation, FCellStaticData& OutStatic, FCellDynamicData& OutDynamic) const
{
    float CellSize = 50.0f;
    int32 ChunkWorldSize = (ChunkSize - 1) * CellSize;

    int32 CX = FMath::FloorToInt(WorldLocation.X / ChunkWorldSize);
    int32 CY = FMath::FloorToInt(WorldLocation.Y / ChunkWorldSize);

    const FChunkData* Chunk = WorldChunks.Find(FIntPoint(CX, CY));
    if (Chunk) {
        int32 LX = FMath::Clamp(FMath::FloorToInt((WorldLocation.X - (CX * ChunkWorldSize)) / CellSize), 0, ChunkSize - 1);
        int32 LY = FMath::Clamp(FMath::FloorToInt((WorldLocation.Y - (CY * ChunkWorldSize)) / CellSize), 0, ChunkSize - 1);
        int32 Idx = LX + LY * ChunkSize;
        if (Chunk->StaticCells.IsValidIndex(Idx) && Chunk->DynamicCells.IsValidIndex(Idx)) {
            OutStatic = Chunk->StaticCells[Idx];
            OutDynamic = Chunk->DynamicCells[Idx];
            return true;
        }
    }
    return false;
}

int32 ASimWorldManager::GetTotalPopulation() const
{
    int32 Total = 0;
    if (SettlementModule) {
        for (const FSettlementData& S : SettlementModule->Settlements) Total += S.Population;
    }
    if (HumanModule) {
        for (const FTribeData& T : HumanModule->Tribes) Total += T.Population;
    }
    return Total;
}

bool ASimWorldManager::GetTribeAtLocation(FVector WorldLocation, FTribeData& OutTribe, float Radius) const
{
    if (HumanModule) {
        FVector2D SearchLoc(WorldLocation.X, WorldLocation.Y);
        float RadiusSq = Radius * Radius;
        for (const FTribeData& Tribe : HumanModule->Tribes) {
            if (Tribe.Population > 0 && FVector2D::DistSquared(Tribe.Position, SearchLoc) <= RadiusSq) {
                OutTribe = Tribe; return true;
            }
        }
    }
    return false;
}

bool ASimWorldManager::GetSettlementAtLocation(FVector WorldLocation, FSettlementData& OutSettlement, float Radius) const
{
    if (SettlementModule) {
        FVector2D SearchLoc(WorldLocation.X, WorldLocation.Y);
        float RadiusSq = Radius * Radius;
        for (const FSettlementData& City : SettlementModule->Settlements) {
            if (City.Population > 0 && FVector2D::DistSquared(City.Position, SearchLoc) <= RadiusSq) {
                OutSettlement = City; return true;
            }
        }
    }
    return false;
}

bool ASimWorldManager::GetAnimalAtLocation(FVector WorldLocation, FAnimalData& OutAnimal, float Radius) const
{
    if (FaunaModule) {
        FVector2D SearchLoc(WorldLocation.X, WorldLocation.Y);
        float RadiusSq = Radius * Radius;
        for (const FAnimalData& Animal : FaunaModule->Animals) {
            if (Animal.HerdSize > 0.0f && FVector2D::DistSquared(Animal.Position, SearchLoc) <= RadiusSq) {
                OutAnimal = Animal; return true;
            }
        }
    }
    return false;
}