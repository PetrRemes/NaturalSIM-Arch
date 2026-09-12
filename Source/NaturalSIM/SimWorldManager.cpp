#include "SimWorldManager.h"
#include "WorldRenderer.h"
#include "WorldGeneratorSystem.h"
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
#include "HeatmapSystem.h"
#include "RelationSystem.h" 
#include "TransportSystem.h" 
#include "InternalPoliticsSystem.h" 
#include "DiplomacySystem.h"        
#include "SimulationDirector.h"
#include "HistorySystem.h"
#include "CosmosSystem.h"
#include "DisasterSystem.h" 
#include "Async/Async.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"

ASimWorldManager::ASimWorldManager()
{
    PrimaryActorTick.bCanEverTick = true;
    RendererModule = CreateDefaultSubobject<UWorldRenderer>(TEXT("RendererModule")); SetRootComponent(RendererModule);
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
}

void ASimWorldManager::BeginPlay() {
    Super::BeginPlay();
    bIsSimulationActive = true;
    RegenerateWorld();
}

void ASimWorldManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bIsSimulationActive = false;
    bIsGenerating = false;
    if (SimDirector) { SimDirector->StopSimulationTimer(); }
    GenerationToken++;
    Super::EndPlay(EndPlayReason);
}

FVector ASimWorldManager::GetPlayerLocation() const {
    if (GetWorld()) {
        if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0)) return PlayerPawn->GetActorLocation();
    }
    return FVector::ZeroVector;
}

int32 ASimWorldManager::GetTotalPopulation() const {
    int32 Total = 0;
    if (HumanModule) { for (const FTribeData& Tribe : HumanModule->Tribes) Total += Tribe.Population; }
    if (SettlementModule) { for (const FSettlementData& City : SettlementModule->Settlements) Total += City.Population; }
    return Total;
}

bool ASimWorldManager::GetMutableCellGlobal(int32 GlobalX, int32 GlobalY, FCellData*& OutCell, FIntPoint& OutChunkCoord) {
    if (GlobalX < 0 || GlobalY < 0) return false;
    int32 CSize = ChunkSize - 1;
    if (CSize <= 0) return false;

    int32 CX = GlobalX / CSize;
    int32 CY = GlobalY / CSize;
    int32 LX = GlobalX % CSize;
    int32 LY = GlobalY % CSize;

    FIntPoint Coord(CX, CY);
    if (FChunkData* Chunk = WorldChunks.Find(Coord)) {
        int32 Idx = LX + LY * ChunkSize;
        if (Chunk->MicroCells.IsValidIndex(Idx)) {
            OutCell = &Chunk->MicroCells[Idx];
            OutChunkCoord = Coord;
            return true;
        }
    }
    return false;
}

bool ASimWorldManager::GetCellGlobal(int32 GlobalX, int32 GlobalY, FCellData& OutCell) {
    FCellData* CellPtr = nullptr;
    FIntPoint DummyCoord;
    if (GetMutableCellGlobal(GlobalX, GlobalY, CellPtr, DummyCoord) && CellPtr != nullptr) {
        OutCell = *CellPtr;
        return true;
    }
    return false;
}

bool ASimWorldManager::GetCellGlobalPtr(int32 GlobalX, int32 GlobalY, const FCellData*& OutCell) const {
    if (GlobalX < 0 || GlobalY < 0) return false;
    int32 CSize = ChunkSize - 1;
    if (CSize <= 0) return false;

    FIntPoint Coord(GlobalX / CSize, GlobalY / CSize);

    if (const FChunkData* Chunk = WorldChunks.Find(Coord)) {
        int32 Idx = (GlobalX % CSize) + (GlobalY % CSize) * ChunkSize;
        if (Chunk->MicroCells.IsValidIndex(Idx)) {
            OutCell = &Chunk->MicroCells[Idx];
            return true;
        }
    }
    return false;
}

void ASimWorldManager::RegisterVisualChange(FIntPoint ChunkCoord, uint8 DirtyFlag, bool bForceImmediate) {
    if (FChunkData* Chunk = WorldChunks.Find(ChunkCoord)) {
        uint8 OldFlags = Chunk->PendingVisualFlags;
        Chunk->PendingVisualFlags |= DirtyFlag;

        if (bForceImmediate) {
            Chunk->LastTerrainRenderDay = -9999.0;
            Chunk->LastTerrainColorRenderDay = -9999.0;
            Chunk->LastWaterRenderDay = -9999.0;
            Chunk->LastFloraRenderDay = -9999.0;
            Chunk->LastCloudRenderDay = -9999.0;
            Chunk->bWeatherChanged = true;
        }

        if (OldFlags != Chunk->PendingVisualFlags || bForceImmediate) {
            ThreadSafeDirtyQueue.Enqueue(ChunkCoord);
        }
    }
}

void ASimWorldManager::SyncChunkEdges(const TSet<FIntPoint>& ActiveChunks) {
    int32 CSize = ChunkSize - 1;
    if (CSize <= 0) return;

    for (FIntPoint Coord : ActiveChunks) {
        FChunkData* Chunk = WorldChunks.Find(Coord);
        if (!Chunk) continue;

        bool bNeedsRender = false;

        auto SyncCell = [&](int32 DestX, int32 DestY, const FCellData& SrcCell) {
            FCellData& DestCell = Chunk->MicroCells[DestX + DestY * ChunkSize];
            if (FMath::Abs(DestCell.Elevation - SrcCell.Elevation) > 0.5f ||
                FMath::Abs(DestCell.SurfaceWater - SrcCell.SurfaceWater) > 0.5f) {
                bNeedsRender = true;
            }
            DestCell = SrcCell;
            };

        if (const FChunkData* RightChunk = WorldChunks.Find(Coord + FIntPoint(1, 0))) {
            for (int32 Y = 0; Y < ChunkSize; Y++) {
                SyncCell(CSize, Y, RightChunk->MicroCells[0 + Y * ChunkSize]);
            }
        }
        if (const FChunkData* BottomChunk = WorldChunks.Find(Coord + FIntPoint(0, 1))) {
            for (int32 X = 0; X < ChunkSize; X++) {
                SyncCell(X, CSize, BottomChunk->MicroCells[X + 0 * ChunkSize]);
            }
        }
        if (const FChunkData* BRChunk = WorldChunks.Find(Coord + FIntPoint(1, 1))) {
            SyncCell(CSize, CSize, BRChunk->MicroCells[0]);
        }

        if (bNeedsRender) {
            RegisterVisualChange(Coord, EChunkVisualDirty::Terrain | EChunkVisualDirty::Water);
        }
    }
}

void ASimWorldManager::UpdateActiveRegions() {
    ActiveChunkKeys.Empty(WorldChunks.Num());
    StableChunkKeys.Empty(WorldChunks.Num());

    for (FIntPoint Key : CachedChunkKeys) {
        FChunkData& Chunk = WorldChunks[Key];
        bool bActive = Chunk.bGeomorphologyDirty || Chunk.FaultStress > 50.0f || Chunk.PendingVisualFlags != 0;

        Chunk.bIsActiveRegion = bActive;
        if (bActive) ActiveChunkKeys.Add(Key);
        else StableChunkKeys.Add(Key);
    }

    float CWorldSize = (ChunkSize - 1) * 50.0f;
    auto ActivateChunkByPos = [&](FVector2D Pos) {
        int32 CX = FMath::FloorToInt(Pos.X / CWorldSize);
        int32 CY = FMath::FloorToInt(Pos.Y / CWorldSize);
        FIntPoint Key(CX, CY);
        if (FChunkData* Chunk = WorldChunks.Find(Key)) {
            if (!Chunk->bIsActiveRegion) {
                Chunk->bIsActiveRegion = true;
                ActiveChunkKeys.Add(Key);
                StableChunkKeys.Remove(Key);
            }
        }
        };

    if (HumanModule) {
        for (const FTribeData& Tribe : HumanModule->Tribes) {
            if (Tribe.Population > 0) ActivateChunkByPos(Tribe.Position);
        }
    }
    if (FaunaModule) {
        for (const FAnimalData& Animal : FaunaModule->Animals) {
            if (Animal.HerdSize > 0) ActivateChunkByPos(Animal.Position);
        }
    }
}

void ASimWorldManager::Tick(float DeltaTime) {
    Super::Tick(DeltaTime);

    if (bKeysDirty) {
        WorldChunks.GetKeys(CachedChunkKeys);
        bKeysDirty = false;
    }

    FIntPoint QueuedDirtyCoord;
    while (ThreadSafeDirtyQueue.Dequeue(QueuedDirtyCoord)) {
        DirtyChunkSet.Add(QueuedDirtyCoord);
    }

    TSet<FIntPoint> EdgeSyncSet;
    for (FIntPoint Coord : DirtyChunkSet) {
        EdgeSyncSet.Add(Coord);
        EdgeSyncSet.Add(Coord + FIntPoint(-1, 0));
        EdgeSyncSet.Add(Coord + FIntPoint(0, -1));
        EdgeSyncSet.Add(Coord + FIntPoint(-1, -1));
    }
    SyncChunkEdges(EdgeSyncSet);

    ProcessNextChunk();
    UpdateDynamicMeshes();

    double RenderStartTime = FPlatformTime::Seconds();
    int32 RenderedThisFrame = 0;
    FChunkRenderTask RTask;

    while (RenderedThisFrame < MaxRenderChunksPerFrame && PendingRenderQueue.Dequeue(RTask)) {
        if (RendererModule && IsValid(RendererModule)) {
            RendererModule->RenderChunk_GameThread(RTask.MeshData, FVector2D(RTask.ChunkCoord.X, RTask.ChunkCoord.Y), RTask.RenderedFlags);
        }
        ActiveGeneratingChunks.Remove(RTask.ChunkCoord);
        RenderedThisFrame++;

        if ((FPlatformTime::Seconds() - RenderStartTime) >= RenderBudgetSeconds) {
            break;
        }
    }

    if (!bIsSimulationActive || !GetWorld()) return;

    if (SimDirector && !bCurrentQueueIsFullGeneration) SimDirector->TickComponent(DeltaTime, LEVELTICK_All, nullptr);

    StatUpdateTimer += DeltaTime;
    if (StatUpdateTimer >= 1.0f) {
        StatUpdateTimer -= 1.0f;

        CachedTotalPop = GetTotalPopulation();
        int32 TribeCount = (HumanModule) ? HumanModule->Tribes.Num() : 0;
        int32 SettlementCount = (SettlementModule) ? SettlementModule->Settlements.Num() : 0;
        int32 AnimalCount = (FaunaModule) ? FaunaModule->Animals.Num() : 0;

        if (ManaModule) {
            ManaModule->UpdateInteractionCapacity(TribeCount, SettlementCount, AnimalCount, CachedTotalPop);
        }

        if (bShowDebugHUD && GEngine) {
            if (!HumanModule || !FaunaModule || !SettlementModule || !SimDirector) {
                GEngine->AddOnScreenDebugMessage(999, 5.0f, FColor::Red, TEXT("CHYBA: Ztracen kontakt s moduly!"));
            }
            else {
                CachedTotalFood = 0.0f;
                float CachedTotalTools = 0.0f;
                float CachedTotalWealth = 0.0f;

                for (const FTribeData& Tribe : HumanModule->Tribes) {
                    CachedTotalFood += Tribe.Inventory.FloraFood + Tribe.Inventory.MeatFood;
                    CachedTotalTools += Tribe.Inventory.Tools;
                    CachedTotalWealth += Tribe.Inventory.Wealth;
                }
                for (const FSettlementData& City : SettlementModule->Settlements) {
                    CachedTotalFood += City.Inventory.FloraFood + City.Inventory.MeatFood;
                    CachedTotalTools += City.Inventory.Tools;
                    CachedTotalWealth += City.Inventory.Wealth;
                }

                int32 NationCount = (NationModule) ? NationModule->Nations.Num() : 0;
                int32 EventCount = (HistoryModule) ? HistoryModule->Chronicle.Num() : 0;

                float M_Current = ManaModule ? ManaModule->GetCurrentMana() : 0.0f;
                float M_Cap = ManaModule ? ManaModule->WorldActivityLevel : 0.0f;
                float M_TotalScaled = ManaModule ? ManaModule->YesterdayTotalScaled : 0.0f;

                CachedHUDText = FString::Printf(TEXT("=== NATURAL SIMULATION ===\n Aktivni kmeny: %d\n Pocet Osad: %d\n Staty (Narody): %d\n Celkova populace: %d / %d\n Nasbirane jidlo: %.1f | Nastroje: %.1f | Bohatstvi: %.1f\n Stada a selmy: %d\n Den: %d | Rok: %d\n Zaznamu v Kronice: %d\n\n --- ENERGIE SVETA (MANA) ---\n Aktualni Mana: %.1f\n Aktivita sveta: %.1f\n Denni prijem: +%.2f\n\n Aktualni Pohled: %d"),
                    TribeCount, SettlementCount, NationCount, CachedTotalPop, MaxGlobalPopulation, CachedTotalFood, CachedTotalTools, CachedTotalWealth, AnimalCount, CurrentDay, CurrentYear, EventCount, M_Current, M_Cap, M_TotalScaled, (int32)CurrentViewMode);

                GEngine->AddOnScreenDebugMessage(1, 1.5f, FColor::Cyan, CachedHUDText, true, FVector2D(1.5f, 1.5f));
            }
        }
    }
}

#if WITH_EDITOR
void ASimWorldManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) {
    Super::PostEditChangeProperty(PropertyChangedEvent);

    if (PropertyChangedEvent.Property != nullptr) {
        FName PropName = PropertyChangedEvent.Property->GetFName();
        if (PropName == GET_MEMBER_NAME_CHECKED(ASimWorldManager, RealSecondsPerDay) ||
            PropName == GET_MEMBER_NAME_CHECKED(ASimWorldManager, SimulationSpeedMultiplier) ||
            PropName == GET_MEMBER_NAME_CHECKED(ASimWorldManager, CurrentViewMode) ||
            PropName == GET_MEMBER_NAME_CHECKED(ASimWorldManager, bShowDebugHUD) ||
            PropName == GET_MEMBER_NAME_CHECKED(ASimWorldManager, bIsSimulationActive) ||
            PropName == GET_MEMBER_NAME_CHECKED(ASimWorldManager, RenderBudgetSeconds) ||
            PropName == GET_MEMBER_NAME_CHECKED(ASimWorldManager, MaxRenderChunksPerFrame) ||
            PropName == GET_MEMBER_NAME_CHECKED(ASimWorldManager, MaxConcurrentChunks))
        {
            return;
        }
    }

    RegenerateWorld();
}
bool ASimWorldManager::ShouldTickIfViewportsOnly() const { return true; }
void ASimWorldManager::PostActorCreated() { Super::PostActorCreated(); RegenerateWorld(); }
#endif

void ASimWorldManager::GenerateContinentCenters() {
    Continents.Empty(); FRandomStream Stream(MapSeed);
    for (int i = 0; i < ContinentCount; i++) {
        FContinentData Cont;
        Cont.OriginCenter = FVector2D(Stream.FRandRange(0.0f, WorldSizeInChunksX * ChunkSize), Stream.FRandRange(0.0f, WorldSizeInChunksY * ChunkSize));
        Cont.DriftDirection = FVector2D(Stream.FRandRange(-1.0f, 1.0f), Stream.FRandRange(-1.0f, 1.0f)).GetSafeNormal();
        Continents.Add(Cont);
    }
}

FChunkGenerationParameters ASimWorldManager::GetGenerationParams() const {
    FChunkGenerationParameters P;
    P.WorldSizeInChunksX = WorldSizeInChunksX; P.WorldSizeInChunksY = WorldSizeInChunksY;
    P.ChunkSize = ChunkSize; P.MapSeed = MapSeed; P.SeaLevel = SeaLevel; P.LowlandFlatness = LowlandFlatness; P.ElevationMultiplier = ElevationMultiplier;
    P.CoastlineRoughness = CoastlineRoughness; P.IslandFrequency = IslandFrequency; P.ContinentCount = ContinentCount; P.ContinentSizeMultiplier = ContinentSizeMultiplier;
    P.TectonicMountainHeight = TectonicMountainHeight; P.VolcanicActivity = VolcanicActivity; P.TectonicShift = TectonicShift; P.LavaHeightBoost = LavaHeightBoost;
    P.NoiseScale = NoiseScale; P.NoiseOctaves = NoiseOctaves; P.NoisePersistence = NoisePersistence;
    P.RiverSpawnMultiplier = RiverSpawnMultiplier; P.RiverMeanderStrength = RiverMeanderStrength; P.ErosionMultiplier = ErosionMultiplier;
    P.BaseTemperature = BaseTemperature; P.GlobalHumidityRate = GlobalHumidityRate; P.GlobalRainfall = GlobalRainfall;
    P.TemperatureScale = TemperatureScale; P.RainfallMultiplier = RainfallMultiplier;
    P.CloudSpeedMultiplier = CloudSpeedMultiplier; P.CloudDensityThreshold = CloudDensityThreshold; P.CloudResolutionStep = CloudResolutionStep;
    P.CloudCoverageMultiplier = CloudCoverageMultiplier;
    P.FloraGrowthSpeed = FloraGrowthSpeed; P.GrasslandSpread = GrasslandSpread;
    P.CurrentDay = CurrentDay; P.CurrentYear = CurrentYear; P.bIsFullGeneration = bCurrentQueueIsFullGeneration; P.Continents = Continents;
    P.bUseLOD = true; FVector PLoc = GetPlayerLocation(); P.PlayerPos2D = FVector2D(PLoc.X, PLoc.Y);

    P.ViewMode = CurrentViewMode;

    // OPTIMALIZACE: Pøedáme Cache do parametrù pro všechny moduly
    P.GlobalTerrainCache = GlobalTerrainCache;
    P.TotalWorldCellsX = WorldSizeInChunksX * ChunkSize;
    P.TotalWorldCellsY = WorldSizeInChunksY * ChunkSize;

    return P;
}

void ASimWorldManager::ProcessNextChunk() {
    if (ChunkGenerationQueue.IsEmpty()) {
        if (ActiveChunkTasks <= 0 && bIsGenerating) {
            bIsGenerating = false;
            if (bCurrentQueueIsFullGeneration) {

                if (HydroModule) {
                    WorldChunks.GetKeys(CachedChunkKeys);

                    // OPTIMALIZACE 2: Konec zamrznutí editoru. Zdlouhavá simulace 50 krokù vody se provede asynchronnì na pozadí!
                    TWeakObjectPtr<ASimWorldManager> WeakThis(this);
                    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis]() {
                        if (!WeakThis.IsValid()) return;
                        ASimWorldManager* Manager = WeakThis.Get();

                        TSet<FIntPoint> AllChunksSet(Manager->CachedChunkKeys);

                        for (int32 i = 0; i < 50; ++i) {
                            Manager->HydroModule->ProcessHydroSlice(Manager->CachedChunkKeys, Manager->WorldChunks, Manager, 0, Manager->CachedChunkKeys.Num(), true, 1.0f);
                            Manager->SyncChunkEdges(AllChunksSet);
                        }

                        // Až voda doteèe, asynchronnì "zaklepeme" na GameThread a spustíme zbytek
                        AsyncTask(ENamedThreads::GameThread, [WeakThis]() {
                            if (!WeakThis.IsValid()) return;
                            ASimWorldManager* FinalMain = WeakThis.Get();

                            if (FinalMain->CosmosModule) FinalMain->CosmosModule->InitCosmos(FinalMain);
                            if (FinalMain->FaunaModule) FinalMain->FaunaModule->InitializeFauna(FinalMain);
                            if (FinalMain->HumanModule) FinalMain->HumanModule->InitializeHumans(FinalMain);
                            FinalMain->bCurrentQueueIsFullGeneration = false;

                            for (auto& Pair : FinalMain->WorldChunks) {
                                FinalMain->RegisterVisualChange(Pair.Key, EChunkVisualDirty::Terrain | EChunkVisualDirty::Water | EChunkVisualDirty::Flora);
                            }

                            if (FinalMain->SimDirector) {
                                FinalMain->SimDirector->InitializeDirector(FinalMain);
                                FinalMain->SimDirector->StartSimulationTimer();
                            }
                            if (FinalMain->RendererModule) { FinalMain->RendererModule->UpdateFastEntities(); }

                            // Hotovo! Obrovský pamìový blok (Cache) už nepotøebujeme, mùžeme RAM uvolnit.
                            FinalMain->GlobalTerrainCache.Reset();
                            });
                        });
                }
                else {
                    if (CosmosModule) CosmosModule->InitCosmos(this);
                    if (FaunaModule) FaunaModule->InitializeFauna(this);
                    if (HumanModule) HumanModule->InitializeHumans(this);
                    bCurrentQueueIsFullGeneration = false;

                    for (auto& Pair : WorldChunks) {
                        RegisterVisualChange(Pair.Key, EChunkVisualDirty::Terrain | EChunkVisualDirty::Water | EChunkVisualDirty::Flora);
                    }

                    if (SimDirector) { SimDirector->InitializeDirector(this); SimDirector->StartSimulationTimer(); }
                    if (RendererModule) { RendererModule->UpdateFastEntities(); }
                    GlobalTerrainCache.Reset();
                }
            }
        }
        return;
    }

    FIntPoint ChunkCoord;
    while (ActiveChunkTasks < MaxConcurrentChunks && ChunkGenerationQueue.Dequeue(ChunkCoord)) {
        QueuedChunksSet.Remove(ChunkCoord);

        uint8 FlagsToRender = 255;
        if (!bCurrentQueueIsFullGeneration) {
            if (FChunkData* Chunk = WorldChunks.Find(ChunkCoord)) {
                FlagsToRender = Chunk->VisualDirtyFlags;
                Chunk->VisualDirtyFlags = 0;
            }
        }

        if (FlagsToRender == 0) continue;

        ActiveGeneratingChunks.Add(ChunkCoord);
        GenerateChunk(ChunkCoord, bCurrentQueueIsFullGeneration, FlagsToRender);
    }
}

void ASimWorldManager::GenerateChunk(FIntPoint ChunkCoordinate, bool bIsFullGeneration, uint8 DirtyFlags) {
    ActiveChunkTasks++; int32 TaskToken = GenerationToken;

    TSharedPtr<FChunkData> LocalChunk = MakeShared<FChunkData>();

    if (bIsFullGeneration) {
        LocalChunk->MicroCells.SetNum(ChunkSize * ChunkSize);
    }
    else if (FChunkData* ExistingChunk = WorldChunks.Find(ChunkCoordinate)) {
        *LocalChunk = *ExistingChunk;
    }

    TWeakObjectPtr<ASimWorldManager> WeakThis(this); FIntPoint SafeCoord = ChunkCoordinate; FChunkGenerationParameters Params = GetGenerationParams();

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis, SafeCoord, LocalChunk, TaskToken, Params, DirtyFlags]() mutable {
        if (!WeakThis.IsValid() || !WeakThis->IsTaskValid(TaskToken)) { AsyncTask(ENamedThreads::GameThread, [WeakThis]() { if (WeakThis.IsValid()) WeakThis->ActiveChunkTasks--; }); return; }

        FChunkGenerationResult Result;
        Result.ChunkCoord = SafeCoord;
        Result.bIsFullGeneration = Params.bIsFullGeneration;
        Result.MeshData = MakeShared<FChunkMeshData>();
        Result.RenderedFlags = DirtyFlags;

        FVector2D CoordAsFloat(SafeCoord.X, SafeCoord.Y);

        if (Params.bIsFullGeneration) {
            UWorldGeneratorSystem::ProcessChunkTerrain(*LocalChunk, CoordAsFloat, Params);
            UTectonicSystem::ProcessChunkTectonics(*LocalChunk, CoordAsFloat, Params);
            UClimateSystem::ProcessChunkClimate(*LocalChunk, CoordAsFloat, Params);
            UHydroSystem::ProcessChunkWater(*LocalChunk, CoordAsFloat, Params);
            UFloraSystem::ProcessChunkFlora(*LocalChunk, CoordAsFloat, Params);
            Result.Heatmap = UHeatmapSystem::CalculateChunkHeatmap_Static(*LocalChunk, CoordAsFloat, Params);
        }

        UWorldRenderer::BuildChunkMesh_Async(Result.MeshData, *LocalChunk, CoordAsFloat, Params, DirtyFlags, WeakThis.Get());

        AsyncTask(ENamedThreads::GameThread, [WeakThis, Result, LocalChunk, TaskToken]() {
            if (!WeakThis.IsValid() || !WeakThis->IsTaskValid(TaskToken)) { if (WeakThis.IsValid()) WeakThis->ActiveChunkTasks--; return; }
            ASimWorldManager* FinalMain = WeakThis.Get();

            if (Result.bIsFullGeneration) {
                FinalMain->WorldChunks.Add(Result.ChunkCoord, MoveTemp(*LocalChunk));
                if (FinalMain->HeatmapModule) FinalMain->HeatmapModule->ChunkHeatmaps.Add(Result.ChunkCoord, Result.Heatmap);
                FinalMain->bKeysDirty = true;
            }
            else if (FChunkData* TargetChunk = FinalMain->WorldChunks.Find(Result.ChunkCoord)) {
                *TargetChunk = MoveTemp(*LocalChunk);
            }

            FinalMain->PendingRenderQueue.Enqueue({ Result.ChunkCoord, Result.MeshData, Result.RenderedFlags });
            FinalMain->ActiveChunkTasks--;
            });
        });
}

void ASimWorldManager::UpdateDynamicMeshes() {
    if (bIsGenerating || bCurrentQueueIsFullGeneration) return;

    TArray<FIntPoint> ToRemove;
    double AbsoluteDay = (CurrentYear * 365.0) + CurrentDay;

    for (FIntPoint Coord : DirtyChunkSet) {
        if (ActiveGeneratingChunks.Contains(Coord) || QueuedChunksSet.Contains(Coord)) {
            continue;
        }

        if (FChunkData* Chunk = WorldChunks.Find(Coord)) {
            uint8 FlagsToBuild = 0;

            bool bWeatherTimePassed = (AbsoluteDay - Chunk->LastWeatherRenderDay) >= 2.0;

            if (Chunk->PendingVisualFlags & EChunkVisualDirty::Terrain) {
                if ((Chunk->bWeatherChanged && bWeatherTimePassed) || Chunk->AccumulatedTerrainChange >= 25.0f || (AbsoluteDay - Chunk->LastTerrainRenderDay) >= 30.0) {
                    FlagsToBuild |= EChunkVisualDirty::Terrain;
                    Chunk->LastTerrainRenderDay = AbsoluteDay;
                    Chunk->LastTerrainColorRenderDay = AbsoluteDay;
                    Chunk->AccumulatedTerrainChange = 0.0f;
                    if (Chunk->bWeatherChanged) Chunk->LastWeatherRenderDay = AbsoluteDay;
                }
            }
            else if (Chunk->PendingVisualFlags & EChunkVisualDirty::TerrainColor) {
                if ((Chunk->bWeatherChanged && bWeatherTimePassed) || (AbsoluteDay - Chunk->LastTerrainColorRenderDay) >= 1.5) {
                    FlagsToBuild |= EChunkVisualDirty::TerrainColor;
                    Chunk->LastTerrainColorRenderDay = AbsoluteDay;
                    if (Chunk->bWeatherChanged) Chunk->LastWeatherRenderDay = AbsoluteDay;
                }
            }

            if (Chunk->PendingVisualFlags & EChunkVisualDirty::Water) {
                if (Chunk->AccumulatedWaterChange >= 10.0f || (AbsoluteDay - Chunk->LastWaterRenderDay) >= 5.0) {
                    FlagsToBuild |= EChunkVisualDirty::Water;
                    Chunk->LastWaterRenderDay = AbsoluteDay;
                    Chunk->AccumulatedWaterChange = 0.0f;
                }
            }

            if (Chunk->PendingVisualFlags & EChunkVisualDirty::Flora) {
                if ((Chunk->bWeatherChanged && bWeatherTimePassed) || (AbsoluteDay - Chunk->LastFloraRenderDay) >= 15.0) {
                    FlagsToBuild |= EChunkVisualDirty::Flora;
                    Chunk->LastFloraRenderDay = AbsoluteDay;
                    if (Chunk->bWeatherChanged) Chunk->LastWeatherRenderDay = AbsoluteDay;
                }
            }

            if (Chunk->PendingVisualFlags & EChunkVisualDirty::Cloud) {
                if ((AbsoluteDay - Chunk->LastCloudRenderDay) >= 2.0) {
                    FlagsToBuild |= EChunkVisualDirty::Cloud;
                    Chunk->LastCloudRenderDay = AbsoluteDay;
                }
            }

            if (FlagsToBuild != 0) {
                Chunk->VisualDirtyFlags = FlagsToBuild;
                Chunk->PendingVisualFlags &= ~FlagsToBuild;
                Chunk->bWeatherChanged = false;

                if (!QueuedChunksSet.Contains(Coord)) {
                    ChunkGenerationQueue.Enqueue(Coord);
                    QueuedChunksSet.Add(Coord);
                }
            }

            if (Chunk->PendingVisualFlags == 0) {
                ToRemove.Add(Coord);
            }
        }
        else {
            ToRemove.Add(Coord);
        }
    }

    for (FIntPoint Coord : ToRemove) {
        DirtyChunkSet.Remove(Coord);
    }
}

void ASimWorldManager::RegenerateWorld() {
    bIsGenerating = true;
    bCurrentQueueIsFullGeneration = true;

    GenerationToken++;

    CurrentDay = 1;
    CurrentYear = 2026;

    ChunkGenerationQueue.Empty();
    QueuedChunksSet.Empty();
    WorldChunks.Empty();
    CachedChunkKeys.Empty();
    ThreadSafeDirtyQueue.Empty();
    DirtyChunkSet.Empty();

    Stat_Battles = 0; Stat_Trades = 0; Stat_SettlementsFounded = 0;
    Stat_NationsFormed = 0; Stat_CitiesAbsorbed = 0; Stat_WarsDeclared = 0;

    PendingRenderQueue.Empty();
    ActiveGeneratingChunks.Empty();

    if (HeatmapModule) HeatmapModule->ChunkHeatmaps.Empty();
    if (TransportModule) TransportModule->TradeRoutes.Empty();
    if (DiplomacyModule) DiplomacyModule->DiplomaticRelations.Empty();
    if (InternalPoliticsModule) InternalPoliticsModule->InternalLinks.Empty();
    if (FaunaModule) FaunaModule->Animals.Empty();
    if (HumanModule) HumanModule->Tribes.Empty();
    if (SettlementModule) SettlementModule->Settlements.Empty();
    if (NationModule) NationModule->Nations.Empty();

    if (HistoryModule) HistoryModule->Chronicle.Empty();
    if (CosmosModule) CosmosModule->OrbitalBodies.Empty();
    if (DisasterModule) DisasterModule->ActiveWarnings.Empty();

    if (SimDirector) SimDirector->ResetTime();
    if (RendererModule) { RendererModule->ClearAllMeshes(); RendererModule->InitializeRenderer(this); }

    GenerateContinentCenters();

    // OPTIMALIZACE 2: Pøed samotným generováním si asynchronnì v jednom zátahu pøedpoèítáme celý šum mapy
    GlobalTerrainCache = MakeShared<TArray<FPrecomputedTerrain>>();
    GlobalTerrainCache->SetNumZeroed(WorldSizeInChunksX * ChunkSize * WorldSizeInChunksY * ChunkSize);

    FChunkGenerationParameters CacheParams = GetGenerationParams();
    CacheParams.TotalWorldCellsX = WorldSizeInChunksX * ChunkSize;
    CacheParams.TotalWorldCellsY = WorldSizeInChunksY * ChunkSize;
    UWorldGeneratorSystem::GenerateGlobalTerrainCache(*GlobalTerrainCache, CacheParams);

    for (int32 y = 0; y < WorldSizeInChunksY; y++) {
        for (int32 x = 0; x < WorldSizeInChunksX; x++) {
            ChunkGenerationQueue.Enqueue(FIntPoint(x, y));
            QueuedChunksSet.Add(FIntPoint(x, y));
        }
    }
}

void ASimWorldManager::NotifyChunkRendered() {}
void ASimWorldManager::ToggleSimulation() { bIsSimulationActive = !bIsSimulationActive; }
void ASimWorldManager::SetTimeSpeed(float NewSecondsPerDay) { RealSecondsPerDay = FMath::Max(0.01f, NewSecondsPerDay); }
void ASimWorldManager::SetSimulationSpeedMultiplier(float NewMultiplier) { SimulationSpeedMultiplier = FMath::Max(0.1f, NewMultiplier); }

void ASimWorldManager::SetViewMode(EWorldViewMode NewMode) {
    if (CurrentViewMode == NewMode) return;
    CurrentViewMode = NewMode;

    for (auto& Pair : WorldChunks) {
        RegisterVisualChange(Pair.Key, EChunkVisualDirty::Terrain | EChunkVisualDirty::Water | EChunkVisualDirty::Flora, true);
    }
}

void ASimWorldManager::TriggerEarthquake(FIntPoint EpicenterChunkCoord, float Radius, float Intensity)
{
    if (!bIsSimulationActive) return;

    if (FChunkData* EpicenterChunk = WorldChunks.Find(EpicenterChunkCoord)) {
        EpicenterChunk->FaultStress = 0.0f;
    }

    float CellSize = 50.0f;
    float ChunkWorldSize = (ChunkSize - 1) * CellSize;

    FVector2D GlobalEpicenter = FVector2D(EpicenterChunkCoord.X, EpicenterChunkCoord.Y) * ChunkWorldSize + FVector2D(FMath::FRandRange(0.2f, 0.8f) * ChunkWorldSize, FMath::FRandRange(0.2f, 0.8f) * ChunkWorldSize);
    int32 ChunkRadius = FMath::CeilToInt(Radius / ChunkWorldSize) + 1;

    bool bTriggerTsunami = false;
    FCellData EpicenterCell;
    int32 EpicenterGlobalX = FMath::FloorToInt(GlobalEpicenter.X / CellSize);
    int32 EpicenterGlobalY = FMath::FloorToInt(GlobalEpicenter.Y / CellSize);

    if (GetCellGlobal(EpicenterGlobalX, EpicenterGlobalY, EpicenterCell)) {
        if (EpicenterCell.Elevation <= SeaLevel || EpicenterCell.SurfaceWater > 5.0f) {
            bTriggerTsunami = true;
        }
    }

    for (int32 cy = EpicenterChunkCoord.Y - ChunkRadius; cy <= EpicenterChunkCoord.Y + ChunkRadius; cy++) {
        for (int32 cx = EpicenterChunkCoord.X - ChunkRadius; cx <= EpicenterChunkCoord.X + ChunkRadius; cx++) {
            FIntPoint Coord(cx, cy);

            if (FChunkData* Chunk = WorldChunks.Find(Coord)) {
                bool bChunkAffected = false;

                for (int32 Y = 0; Y < ChunkSize; Y++) {
                    for (int32 X = 0; X < ChunkSize; X++) {
                        int32 i = X + Y * ChunkSize;
                        if (i >= Chunk->MicroCells.Num()) continue;

                        FCellData& Cell = Chunk->MicroCells[i];

                        FVector2D CellGlobalPos((cx * (ChunkSize - 1) + X) * CellSize, (cy * (ChunkSize - 1) + Y) * CellSize);

                        float DistSq = FVector2D::DistSquared(GlobalEpicenter, CellGlobalPos);
                        float RadiusSq = Radius * Radius;

                        if (DistSq <= RadiusSq) {
                            bChunkAffected = true;
                            Chunk->FaultStress *= FMath::FRandRange(0.0f, 0.2f);

                            float Dist = FMath::Sqrt(DistSq);
                            float Falloff = FMath::Pow(1.0f - (Dist / Radius), 2.0f);
                            float LocalIntensity = Intensity * Falloff;

                            float Shake = FMath::FRandRange(-1.0f, 1.0f) * LocalIntensity * 1.5f;

                            Cell.Elevation += Shake;
                            Cell.DangerLevel = FMath::Min(1.0f, Cell.DangerLevel + LocalIntensity * 2.0f);

                            if (LocalIntensity > 0.7f) {
                                Cell.HouseDensity *= 0.5f;
                                if (FMath::FRand() < 0.2f) Cell.bHasRoad = false;
                                if (FMath::FRand() < 0.3f) Cell.BuildingType = EBuildingType::None;
                                if (Cell.TreeType != ETreeType::None && FMath::FRand() < 0.1f) {
                                    Cell.TreeType = ETreeType::None;
                                    Cell.WoodAmount = 0.0f;
                                }
                            }
                            Chunk->AccumulatedTerrainChange += FMath::Abs(Shake) * 0.5f;
                        }
                    }
                }

                if (bChunkAffected) {
                    RegisterVisualChange(Coord, EChunkVisualDirty::Terrain | EChunkVisualDirty::Water | EChunkVisualDirty::Flora, true);
                }
            }
        }
    }

    if (bTriggerTsunami) {
        float MaxWaveHeight = Intensity * 11.0f;
        float MaxWaveRadiusCells = 150.0f;

        struct FTsunamiNode { FIntPoint Coord; float WaveHeight; int32 Dist; };

        TQueue<FTsunamiNode> WaveQueue;
        TSet<FIntPoint> Visited;

        FIntPoint StartNode(EpicenterGlobalX, EpicenterGlobalY);
        WaveQueue.Enqueue({ StartNode, MaxWaveHeight, 0 });
        Visited.Add(StartNode);

        TSet<FIntPoint> TsunamiDirtyChunks;
        FIntPoint Offsets[8] = { {0,1}, {1,0}, {0,-1}, {-1,0}, {1,1}, {-1,-1}, {1,-1}, {-1,1} };

        while (!WaveQueue.IsEmpty()) {
            FTsunamiNode Curr;
            WaveQueue.Dequeue(Curr);

            FCellData* Cell = nullptr;
            FIntPoint ChunkC;

            if (GetMutableCellGlobal(Curr.Coord.X, Curr.Coord.Y, Cell, ChunkC)) {

                bool bIsLand = Cell->Elevation > SeaLevel;
                float LocalElev = bIsLand ? (Cell->Elevation - SeaLevel) : 0.0f;

                if (LocalElev > Curr.WaveHeight) {
                    continue;
                }

                if (bIsLand) {
                    Cell->SurfaceWater += Curr.WaveHeight * 2.0f;
                    Cell->WaterInflowBuffer += Curr.WaveHeight * 10.0f;

                    Cell->WaterPollution = FMath::Min(1.0f, Cell->WaterPollution + 0.8f);
                    Cell->DangerLevel = 1.0f;

                    if (Curr.WaveHeight > 2.0f) {
                        Cell->HouseDensity = 0.0f;
                        Cell->BuildingType = EBuildingType::None;
                        Cell->bHasRoad = false;
                        if (Cell->TreeType != ETreeType::None) {
                            Cell->TreeType = ETreeType::Deadwood;
                            Cell->WoodAmount *= 0.2f;
                            Cell->FloraDensity = 0.0f;
                        }
                        Cell->BerryBushes = 0.0f;
                        Cell->AnimalBones += 10.0f;
                    }

                    TsunamiDirtyChunks.Add(ChunkC);

                    if (FChunkData* DataChunk = WorldChunks.Find(ChunkC)) {
                        DataChunk->AccumulatedWaterChange += Curr.WaveHeight;
                        DataChunk->AccumulatedTerrainChange += 1.0f;
                    }
                }

                if (Curr.Dist < MaxWaveRadiusCells) {
                    for (int i = 0; i < 8; i++) {
                        FIntPoint N = Curr.Coord + Offsets[i];
                        if (!Visited.Contains(N)) {
                            Visited.Add(N);

                            float NextWaveHeight = Curr.WaveHeight;

                            if (bIsLand) {
                                NextWaveHeight -= 0.45f;
                            }
                            else {
                                NextWaveHeight -= (MaxWaveHeight / MaxWaveRadiusCells);
                            }

                            if (NextWaveHeight > 0.0f) {
                                WaveQueue.Enqueue({ N, NextWaveHeight, Curr.Dist + 1 });
                            }
                        }
                    }
                }
            }
        }

        for (const FIntPoint& DC : TsunamiDirtyChunks) {
            RegisterVisualChange(DC, EChunkVisualDirty::Terrain | EChunkVisualDirty::Water | EChunkVisualDirty::Flora, true);
        }
    }
}

bool ASimWorldManager::GetTribeAtLocation(FVector WorldLocation, FTribeData& OutTribe, float Radius) {
    if (!HumanModule) return false;
    float InvGridSize = 1.0f / (ChunkSize * 50.0f);
    int32 CX = FMath::FloorToInt(WorldLocation.X * InvGridSize);
    int32 CY = FMath::FloorToInt(WorldLocation.Y * InvGridSize);
    float RadiusSq = Radius * Radius;

    for (int32 dy = -1; dy <= 1; dy++) {
        for (int32 dx = -1; dx <= 1; dx++) {
            if (const TArray<int32>* CellTribes = HumanModule->TribeSpatialGrid.Find(FIntPoint(CX + dx, CY + dy))) {
                for (int32 idx : *CellTribes) {
                    if (HumanModule->Tribes.IsValidIndex(idx)) {
                        if (FVector2D::DistSquared(HumanModule->Tribes[idx].Position, FVector2D(WorldLocation.X, WorldLocation.Y)) <= RadiusSq) {
                            OutTribe = HumanModule->Tribes[idx];
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

bool ASimWorldManager::GetSettlementAtLocation(FVector WorldLocation, FSettlementData& OutSettlement, float Radius) {
    if (!SettlementModule) return false;
    float InvGridSize = 1.0f / (ChunkSize * 50.0f);
    int32 CX = FMath::FloorToInt(WorldLocation.X * InvGridSize);
    int32 CY = FMath::FloorToInt(WorldLocation.Y * InvGridSize);
    float RadiusSq = Radius * Radius;

    for (int32 dy = -1; dy <= 1; dy++) {
        for (int32 dx = -1; dx <= 1; dx++) {
            if (const TArray<int32>* CellSetts = SettlementModule->SettlementSpatialGrid.Find(FIntPoint(CX + dx, CY + dy))) {
                for (int32 idx : *CellSetts) {
                    if (SettlementModule->Settlements.IsValidIndex(idx)) {
                        if (FVector2D::DistSquared(SettlementModule->Settlements[idx].Position, FVector2D(WorldLocation.X, WorldLocation.Y)) <= RadiusSq) {
                            OutSettlement = SettlementModule->Settlements[idx];
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

bool ASimWorldManager::GetAnimalAtLocation(FVector WorldLocation, FAnimalData& OutAnimal, float Radius) {
    if (!FaunaModule) return false;
    float InvGridSize = 1.0f / (ChunkSize * 50.0f);
    int32 CX = FMath::FloorToInt(WorldLocation.X * InvGridSize);
    int32 CY = FMath::FloorToInt(WorldLocation.Y * InvGridSize);
    float RadiusSq = Radius * Radius;

    for (int32 dy = -1; dy <= 1; dy++) {
        for (int32 dx = -1; dx <= 1; dx++) {
            if (const TArray<int32>* CellAnimals = FaunaModule->AnimalSpatialGrid.Find(FIntPoint(CX + dx, CY + dy))) {
                for (int32 idx : *CellAnimals) {
                    if (FaunaModule->Animals.IsValidIndex(idx)) {
                        if (FaunaModule->Animals[idx].HerdSize > 0.0f && FVector2D::DistSquared(FaunaModule->Animals[idx].Position, FVector2D(WorldLocation.X, WorldLocation.Y)) <= RadiusSq) {
                            OutAnimal = FaunaModule->Animals[idx];
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

bool ASimWorldManager::GetCellDataAtLocation(FVector WorldLocation, FCellData& OutCell) {
    float CellSize = 50.0f; float ChunkWorldSize = (ChunkSize - 1) * CellSize;
    int32 CX = FMath::FloorToInt(WorldLocation.X / ChunkWorldSize); int32 CY = FMath::FloorToInt(WorldLocation.Y / ChunkWorldSize); FIntPoint ChunkCoord(CX, CY);
    if (FChunkData* Chunk = WorldChunks.Find(ChunkCoord)) {
        int32 LX = FMath::Clamp(FMath::FloorToInt((WorldLocation.X - (CX * ChunkWorldSize)) / CellSize), 0, ChunkSize - 1);
        int32 LY = FMath::Clamp(FMath::FloorToInt((WorldLocation.Y - (CY * ChunkWorldSize)) / CellSize), 0, ChunkSize - 1);
        int32 Idx = LX + LY * ChunkSize;
        if (Chunk->MicroCells.IsValidIndex(Idx)) { OutCell = Chunk->MicroCells[Idx]; return true; }
    }
    return false;
}