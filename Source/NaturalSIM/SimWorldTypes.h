#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "SimWorldTypes.generated.h"

UENUM(BlueprintType)
enum class EWorldViewMode : uint8
{
    Normal,
    Temperature,
    Humidity,
    Rainfall,
    TectonicStress,
    Danger,
    Political
};

UENUM(BlueprintType)
enum class EBuildingType : uint8
{
    None,
    Farm,
    LumberCamp,
    Mine,
    Blacksmith,
    Market,
    Factory,
    Dam,
    HuntingCabin,
    CityWall,
    Castle,
    CustomsOffice,
    LogisticsCenter,
    Port,
    Airport,
    OilRig,
    Resort,
    SpaceCenter,
    PowerPlant_Coal,
    PowerPlant_Nuclear,
    PowerPlant_Fusion,
    PowerPlant_Solar,
    PowerPlant_Wind,
    EcoFarm,
    ForestryCenter,
    AICenter
};

UENUM(BlueprintType)
enum class EBedrockType : uint8
{
    Dirt,
    Rock,
    Sand
};

UENUM(BlueprintType)
enum class ESoilType : uint8
{
    Rock,
    Sand,
    Dirt,
    Clay,
    Mud,
    Peat
};

UENUM(BlueprintType)
enum class EBiomeType : uint8
{
    Barren,
    Desert,
    Beach,
    Tundra,
    Grassland,
    DeciduousForest,
    ConiferousForest,
    TropicalForest,
    Swamp
};

UENUM(BlueprintType)
enum class ETreeType : uint8
{
    None,
    Spruce,
    Pine,
    Oak,
    Birch,
    Jungle,
    Deadwood
};

UENUM(BlueprintType)
enum class ETribeState : uint8
{
    Migrating,
    Camping
};

UENUM(BlueprintType)
enum class EAnimalType : uint8
{
    Herbivore,
    Predator,
    Bird,
    Fish,
    ForestAnimal
};

UENUM(BlueprintType)
enum class ESimulationTaskType : uint8
{
    Fauna,
    Flora,
    Humans,
    Settlements,
    Hydro,
    MeshUpdate,
    HumanDemographics,
    SettlementDemographics,
    TribeRelations,
    SettlementRelations,
    NationAggregation,
    InternalPolitics,
    DiplomaticRelations,
    Tectonics,
    Cosmos
};

UENUM(BlueprintType)
enum class EKnowledgeField : uint8
{
    Woodcraft,
    Masonry,
    Maritime,
    Agriculture,
    Textiles,
    Metallurgy,
    Academics,
    Sociology,
    Warfare,
    Engineering,
    Chemistry,
    Physics,
    Computing,
    Aerospace,
    EnvironmentalScience
};

UENUM(BlueprintType)
enum class ECulturalPillar : uint8
{
    Ecology,
    Militarism,
    Commerce,
    Science,
    Sociability,
    Expansion,
    Aggression,
    Industry,
    Spirituality,
    Exploration
};

UENUM(BlueprintType)
enum class EWaterType : uint8
{
    None,
    Surface,
    River,
    Lake,
    Ocean,
    Spring
};

UENUM(BlueprintType)
enum class ETransportMode : uint8
{
    Land,
    Sea,
    Air
};

UENUM(BlueprintType)
enum class EBeliefFocus : uint8
{
    Animism,
    Ancestors,
    Pantheon,
    Monotheism,
    Philosophy,
    StateCult
};

UENUM(BlueprintType)
enum class EStarType : uint8
{
    YellowDwarf,
    RedDwarf,
    BlueGiant
};

USTRUCT(BlueprintType)
struct FPendingDisaster
{
    GENERATED_BODY()
    UPROPERTY() FIntPoint ChunkCoord = FIntPoint::ZeroValue;
    UPROPERTY() int32 CellX = -1;
    UPROPERTY() int32 CellY = -1;
    UPROPERTY() float DaysRemaining = 365.0f;
    UPROPERTY() float Intensity = 0.0f;
    UPROPERTY() bool bIsEruption = false;
};

USTRUCT(BlueprintType)
struct FChunkHeatmap
{
    GENERATED_BODY()
    UPROPERTY() float AvgElevation = 0.0f;
    UPROPERTY() float AvgFlora = 0.0f;
    UPROPERTY() float WaterRatio = 0.0f;
    UPROPERTY() float Defensibility = 0.0f;
    UPROPERTY() float AvgTemperature = 0.0f;
    UPROPERTY() float AvgHumidity = 0.0f;
    UPROPERTY() float AvgRainfall = 0.0f;
};

USTRUCT(BlueprintType)
struct FCellData
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) float Elevation = 0.0f;
    UPROPERTY(BlueprintReadWrite) float ElevationDelta = 0.0f;
    UPROPERTY(BlueprintReadWrite) float SedimentDelta = 0.0f;
    UPROPERTY(BlueprintReadWrite) float SurfaceWater = 0.0f;
    UPROPERTY(BlueprintReadWrite) float GroundWater = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Rainfall = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Wetness = 0.0f;
    UPROPERTY(BlueprintReadWrite) float SnowAmount = 0.0f;
    UPROPERTY(BlueprintReadWrite) float GlacierIce = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Temperature = 0.0f;
    UPROPERTY(BlueprintReadWrite) EBedrockType Bedrock = EBedrockType::Dirt;

    UPROPERTY(BlueprintReadWrite) ESoilType SoilType = ESoilType::Dirt;
    UPROPERTY(BlueprintReadWrite) float SoilFertility = 0.0f;
    UPROPERTY(BlueprintReadWrite) float SoilMoisture = 0.0f;
    UPROPERTY(BlueprintReadWrite) float OrganicMatter = 0.0f;
    UPROPERTY(BlueprintReadWrite) float SoilDepth = 0.0f;

    UPROPERTY(BlueprintReadWrite) float GrassDensity = 0.0f;
    UPROPERTY(BlueprintReadWrite) float ShrubDensity = 0.0f;
    UPROPERTY(BlueprintReadWrite) float FloraDensity = 0.0f;
    UPROPERTY(BlueprintReadWrite) float ForestDensity = 0.0f;
    UPROPERTY(BlueprintReadWrite) float TreeSeedBank = 0.0f;
    UPROPERTY(BlueprintReadWrite) float ShrubSeedBank = 0.0f;

    UPROPERTY(BlueprintReadWrite) bool bIsVolcano = false;
    UPROPERTY(BlueprintReadWrite) float Lava = 0.0f;
    UPROPERTY(BlueprintReadWrite) float LavaBuffer = 0.0f;
    UPROPERTY(BlueprintReadWrite) FLinearColor BiomeColor = FLinearColor::Green;
    UPROPERTY(BlueprintReadWrite) float WaterPollution = 0.0f;
    UPROPERTY(BlueprintReadWrite) float DangerLevel = 0.0f;
    UPROPERTY(BlueprintReadWrite) float HouseDensity = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Pressure = 1013.0f;
    UPROPERTY(BlueprintReadWrite) float Humidity = 0.0f;
    UPROPERTY(BlueprintReadWrite) FVector2D WindVector = FVector2D(1.0f, 0.5f);

    UPROPERTY(BlueprintReadWrite) float FireIntensity = 0.0f;
    UPROPERTY(BlueprintReadWrite) float FireIntensityBuffer = 0.0f;

    UPROPERTY(BlueprintReadWrite) ETreeType TreeType = ETreeType::None;
    UPROPERTY(BlueprintReadWrite) EBiomeType Biome = EBiomeType::Grassland;
    UPROPERTY(BlueprintReadWrite) float WoodAmount = 0.0f;
    UPROPERTY(BlueprintReadWrite) float EdibleFlora = 0.0f;
    UPROPERTY(BlueprintReadWrite) float BerryBushes = 0.0f;
    UPROPERTY(BlueprintReadWrite) float StoneAmount = 0.0f;
    UPROPERTY(BlueprintReadWrite) float MineralOre = 0.0f;
    UPROPERTY(BlueprintReadWrite) float ClayAmount = 0.0f;
    UPROPERTY(BlueprintReadWrite) float SandAmount = 0.0f;
    UPROPERTY(BlueprintReadWrite) float AnimalBones = 0.0f;
    UPROPERTY(BlueprintReadWrite) float CloudDensity = 0.0f;
    UPROPERTY(BlueprintReadWrite) EWaterType WaterType = EWaterType::None;
    UPROPERTY(BlueprintReadWrite) bool bIsSpring = false;
    UPROPERTY(BlueprintReadWrite) float SpringStrength = 0.0f;

    UPROPERTY(BlueprintReadWrite) float FlowPersistence = 0.0f;
    UPROPERTY(BlueprintReadWrite) float FlowPersistenceBuffer = 0.0f;

    UPROPERTY(BlueprintReadWrite) float WaterFlow = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Sediment = 0.0f;
    UPROPERTY(BlueprintReadWrite) float SedimentInflowBuffer = 0.0f;
    UPROPERTY(BlueprintReadWrite) float DepositedSediment = 0.0f;

    UPROPERTY(BlueprintReadWrite) float RiverDischarge = 0.0f;
    UPROPERTY(BlueprintReadWrite) float RiverDischargeBuffer = 0.0f;

    UPROPERTY(BlueprintReadWrite) float RiverDepth = 0.0f;
    UPROPERTY(BlueprintReadWrite) float RiverWidth = 0.0f;
    UPROPERTY(BlueprintReadWrite) float WaterInflowBuffer = 0.0f;
    UPROPERTY(BlueprintReadWrite) int32 FlowDirectionGlobalX = -1;
    UPROPERTY(BlueprintReadWrite) int32 FlowDirectionGlobalY = -1;
    UPROPERTY(BlueprintReadWrite) float ChannelWidth = 0.0f;
    UPROPERTY(BlueprintReadWrite) float ChannelDepth = 0.0f;
    UPROPERTY(BlueprintReadWrite) float BankHeight = 0.0f;
    UPROPERTY(BlueprintReadWrite) bool bHasRoad = false;

    UPROPERTY(BlueprintReadWrite) EBuildingType BuildingType = EBuildingType::None;

    UPROPERTY(BlueprintReadWrite) float GrazingPressure = 0.0f;
    UPROPERTY(BlueprintReadWrite) float SeedSpread = 0.0f;

    UPROPERTY(BlueprintReadWrite) float SoilCompaction = 0.0f;
    UPROPERTY(BlueprintReadWrite) float SoilStability = 0.0f;
    UPROPERTY(BlueprintReadWrite) float WetlandScore = 0.0f;

    UPROPERTY(BlueprintReadWrite) float MagmaPressure = 0.0f;
    UPROPERTY(BlueprintReadWrite) float TectonicStress = 0.0f;
    UPROPERTY(BlueprintReadWrite) float EruptionDaysRemaining = 0.0f;
    UPROPERTY(BlueprintReadWrite) float AshDensity = 0.0f;
    UPROPERTY(BlueprintReadWrite) float AshDensityBuffer = 0.0f;

    UPROPERTY(BlueprintReadWrite) int32 OwnerSettlementID = -1;
    UPROPERTY(BlueprintReadWrite) int32 OwnerNationID = -1;
    UPROPERTY(BlueprintReadWrite) FLinearColor PoliticalColor = FLinearColor::Transparent;

    UPROPERTY(BlueprintReadWrite) uint8 TreeSpeciesID = 0;
    UPROPERTY(BlueprintReadWrite) float TreeAge = 0.0f;
};

USTRUCT(BlueprintType)
struct FChunkData
{
    GENERATED_BODY()
    UPROPERTY() TArray<FCellData> MicroCells;
    UPROPERTY() bool bIsSimulatingMicro = false;
    UPROPERTY() double LocalChunkWaterVolume = 0.0;
    UPROPERTY() uint8 VisualDirtyFlags = 0;
    UPROPERTY() uint8 PendingVisualFlags = 0;
    UPROPERTY() float AccumulatedTerrainChange = 0.0f;
    UPROPERTY() float AccumulatedWaterChange = 0.0f;

    UPROPERTY() bool bGeomorphologyDirty = false;

    UPROPERTY() double LastTerrainRenderDay = 0.0;
    UPROPERTY() double LastTerrainColorRenderDay = 0.0;

    UPROPERTY() double LastWaterRenderDay = 0.0;
    UPROPERTY() double LastFloraRenderDay = 0.0;
    UPROPERTY() double LastCloudRenderDay = 0.0;
    UPROPERTY() float BaseTectonicPressure = 0.0f;
    UPROPERTY() float FaultStress = 0.0f;
    UPROPERTY() bool bWeatherChanged = false;
    UPROPERTY() double LastWeatherRenderDay = 0.0;

    // NOVÉ (Fáze 3): Pøíznak pro Scheduler, zda chunk obsahuje dynamické prvky (oheò, mìsta, faunu)
    UPROPERTY() bool bIsActiveRegion = false;
};

USTRUCT(BlueprintType)
struct FChunkMeshData
{
    GENERATED_BODY()
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector2D> UV0;
    TArray<FVector> Normals;
    TArray<FProcMeshTangent> Tangents;
    TArray<FLinearColor> VertexColors;

    TArray<FVector> WaterVertices;
    TArray<int32> WaterTriangles;
    TArray<FVector2D> WaterUV0;
    TArray<FVector> WaterNormals;
    TArray<FProcMeshTangent> WaterTangents;
    TArray<FLinearColor> WaterColors;

    TArray<FVector> CloudVertices;
    TArray<int32> CloudTriangles;
    TArray<FVector2D> CloudUV0;
    TArray<FVector> CloudNormals;
    TArray<FProcMeshTangent> CloudTangents;
    TArray<FLinearColor> CloudColors;

    TArray<FVector> FloraVertices;
    TArray<int32> FloraTriangles;
    TArray<FVector2D> FloraUV0;
    TArray<FVector> FloraNormals;
    TArray<FProcMeshTangent> FloraTangents;
    TArray<FLinearColor> FloraColors;

    TArray<FVector> FaunaVertices;
    TArray<int32> FaunaTriangles;
    TArray<FVector2D> FaunaUV0;
    TArray<FVector> FaunaNormals;
    TArray<FProcMeshTangent> FaunaTangents;
    TArray<FLinearColor> FaunaColors;

    TArray<FVector> HumanVertices;
    TArray<int32> HumanTriangles;
    TArray<FVector2D> HumanUV0;
    TArray<FVector> HumanNormals;
    TArray<FProcMeshTangent> HumanTangents;
    TArray<FLinearColor> HumanColors;

    TArray<FVector> SettlementVertices;
    TArray<int32> SettlementTriangles;
    TArray<FVector2D> SettlementUV0;
    TArray<FVector> SettlementNormals;
    TArray<FProcMeshTangent> SettlementTangents;
    TArray<FLinearColor> SettlementColors;

    TArray<FVector> TransportVertices;
    TArray<int32> TransportTriangles;
    TArray<FVector2D> TransportUV0;
    TArray<FVector> TransportNormals;
    TArray<FProcMeshTangent> TransportTangents;
    TArray<FLinearColor> TransportColors;
};

USTRUCT(BlueprintType)
struct FContinentData
{
    GENERATED_BODY()
    UPROPERTY() FVector2D OriginCenter;
    UPROPERTY() FVector2D DriftDirection;
};

USTRUCT(BlueprintType)
struct FChunkGenerationParameters
{
    GENERATED_BODY()
    int32 WorldSizeInChunksX = 4;
    int32 WorldSizeInChunksY = 4;
    int32 ChunkSize = 100;
    int32 MapSeed = 1337;
    float SeaLevel = 0.0f;
    float LowlandFlatness = 0.6f;
    float ElevationMultiplier = 1500.0f;
    float CoastlineRoughness = 0.8f;
    float IslandFrequency = 0.2f;
    int32 ContinentCount = 3;
    float ContinentSizeMultiplier = 2.0f;
    float TectonicMountainHeight = 2500.0f;
    float VolcanicActivity = 0.8f;
    float TectonicShift = 10.0f;
    float LavaHeightBoost = 500.0f;
    float NoiseScale = 0.005f;
    int32 NoiseOctaves = 4;
    float NoisePersistence = 0.5f;
    float RiverSpawnMultiplier = 1.0f;
    float RiverMeanderStrength = 1.0f;
    float ErosionMultiplier = 0.5f;
    float BaseTemperature = 25.0f;
    float GlobalHumidityRate = 0.5f;
    float GlobalRainfall = 1.0f;
    float TemperatureScale = 1.0f;
    float RainfallMultiplier = 14.8f;
    float CloudSpeedMultiplier = 1.0f;
    float CloudDensityThreshold = 0.45f;
    int32 CloudResolutionStep = 2;
    float CloudCoverageMultiplier = 0.4f;
    float FloraGrowthSpeed = 1.0f;
    float GrasslandSpread = 1.0f;
    int32 CurrentDay = 1;
    int32 CurrentYear = 2026;
    bool bIsFullGeneration = false;
    TArray<FContinentData> Continents;
    FVector2D PlayerPos2D = FVector2D::ZeroVector;
    bool bUseLOD = true;

    EWorldViewMode ViewMode = EWorldViewMode::Normal;
};

struct FChunkGenerationResult
{
    FIntPoint ChunkCoord;
    TSharedPtr<FChunkMeshData> MeshData;
    FChunkHeatmap Heatmap;
    bool bIsFullGeneration;
    uint8 RenderedFlags;
};

USTRUCT(BlueprintType)
struct FTribeInventory
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) float FloraFood = 0.0f;
    UPROPERTY(BlueprintReadWrite) float MeatFood = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Wood = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Stone = 0.0f;
    UPROPERTY(BlueprintReadWrite) float IronOre = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Tools = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Weapons = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Wealth = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Oil = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Uranium = 0.0f;
};

USTRUCT(BlueprintType)
struct FPregnancyBatch
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 DaysRemaining = 270;
    UPROPERTY(BlueprintReadWrite) int32 Amount = 0;
};

USTRUCT(BlueprintType)
struct FCultureProfile
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) FString CultureName = "Primitivni kmen";
    UPROPERTY(BlueprintReadWrite) FLinearColor PrimaryColor = FLinearColor::Yellow;
    UPROPERTY(BlueprintReadWrite) TMap<ECulturalPillar, float> Pillars;
    UPROPERTY(BlueprintReadWrite) float LiteracyRate = 0.0f;
    UPROPERTY(BlueprintReadWrite) float PressFactor = 1.0f;
    UPROPERTY(BlueprintReadWrite) float LanguageDivergence = 0.0f;

    UPROPERTY(BlueprintReadWrite) float ReligiousFervor = 10.0f;
    UPROPERTY(BlueprintReadWrite) EBeliefFocus CoreBelief = EBeliefFocus::Animism;
    UPROPERTY(BlueprintReadWrite) float NatureView = 0.0f;
    UPROPERTY(BlueprintReadWrite) float DeathView = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Toleration = 50.0f;

    FCultureProfile()
    {
        Pillars.Add(ECulturalPillar::Ecology, 0.0f);
        Pillars.Add(ECulturalPillar::Militarism, 0.0f);
        Pillars.Add(ECulturalPillar::Commerce, 0.0f);
        Pillars.Add(ECulturalPillar::Science, 0.0f);
        Pillars.Add(ECulturalPillar::Sociability, 0.0f);
        Pillars.Add(ECulturalPillar::Expansion, 0.0f);
        Pillars.Add(ECulturalPillar::Aggression, 0.0f);
        Pillars.Add(ECulturalPillar::Industry, 0.0f);
        Pillars.Add(ECulturalPillar::Spirituality, 0.0f);
        Pillars.Add(ECulturalPillar::Exploration, 0.0f);
    }
};

USTRUCT(BlueprintType)
struct FKnowledgeContainer
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) TMap<EKnowledgeField, float> Levels;
    UPROPERTY(BlueprintReadWrite) TMap<FName, float> SubKnowledge;
    UPROPERTY(BlueprintReadWrite) TArray<FName> DiscoveredConcepts;
    UPROPERTY(BlueprintReadWrite) TArray<FName> UnlockedTechnologies;

    FKnowledgeContainer()
    {
        Levels.Add(EKnowledgeField::Woodcraft, 0.0f);
        Levels.Add(EKnowledgeField::Masonry, 0.0f);
        Levels.Add(EKnowledgeField::Maritime, 0.0f);
        Levels.Add(EKnowledgeField::Agriculture, 0.0f);
        Levels.Add(EKnowledgeField::Textiles, 0.0f);
        Levels.Add(EKnowledgeField::Metallurgy, 0.0f);
        Levels.Add(EKnowledgeField::Academics, 0.0f);
        Levels.Add(EKnowledgeField::Sociology, 0.0f);
        Levels.Add(EKnowledgeField::Warfare, 0.0f);
        Levels.Add(EKnowledgeField::Engineering, 0.0f);
        Levels.Add(EKnowledgeField::Chemistry, 0.0f);
        Levels.Add(EKnowledgeField::Physics, 0.0f);
        Levels.Add(EKnowledgeField::Computing, 0.0f);
        Levels.Add(EKnowledgeField::Aerospace, 0.0f);
        Levels.Add(EKnowledgeField::EnvironmentalScience, 0.0f);
    }
};

USTRUCT(BlueprintType)
struct FDiscoveryDefinition
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) FName DiscoveryName;
    UPROPERTY(BlueprintReadWrite) TMap<EKnowledgeField, float> RequiredKnowledge;
    UPROPERTY(BlueprintReadWrite) TMap<FName, float> RequiredSubKnowledge;
    UPROPERTY(BlueprintReadWrite) TArray<FName> RequiredDiscoveries;
    UPROPERTY(BlueprintReadWrite) TArray<FName> EnvironmentalTriggers;
    UPROPERTY(BlueprintReadWrite) TArray<FName> ExperienceTriggers;
    UPROPERTY(BlueprintReadWrite) ECulturalPillar CulturalAffinity = ECulturalPillar::Science;
};

USTRUCT(BlueprintType)
struct FTechRequirement
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) TMap<EKnowledgeField, float> RequiredKnowledge;
    UPROPERTY(BlueprintReadWrite) TMap<FName, float> RequiredSubKnowledge;
    UPROPERTY(BlueprintReadWrite) TArray<FName> RequiredDiscoveries;
    UPROPERTY(BlueprintReadWrite) TArray<FName> RequiredTechnologies;
    UPROPERTY(BlueprintReadWrite) FName UnlockedTechnologyName;
};

USTRUCT(BlueprintType)
struct FTribeData
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 TribeID = -1;
    UPROPERTY(BlueprintReadWrite) FVector2D Position = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadWrite) int32 Population = 20;
    UPROPERTY(BlueprintReadWrite) ETribeState State = ETribeState::Migrating;
    UPROPERTY(BlueprintReadWrite) int32 CampDaysRemaining = 0;
    UPROPERTY(BlueprintReadWrite) float CampScore = 0.0f;
    UPROPERTY(BlueprintReadWrite) int32 InteractionCooldownDays = 0;
    UPROPERTY(BlueprintReadWrite) FTribeInventory Inventory;
    UPROPERTY() TArray<FPregnancyBatch> Pregnancies;
    UPROPERTY(BlueprintReadWrite) FKnowledgeContainer Knowledge;
    UPROPERTY(BlueprintReadWrite) FCultureProfile Culture;
    UPROPERTY(BlueprintReadWrite) FVector2D TargetRegion = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadWrite) double NextStrategicDecisionDay = 0.0;
    UPROPERTY(BlueprintReadWrite) FLinearColor TribeColor = FLinearColor(1.0f, 0.8f, 0.1f, 1.0f);
    UPROPERTY(BlueprintReadWrite) float DaysAtCamp = 0.0f;
    UPROPERTY(BlueprintReadWrite) bool bFollowingHerd = false;

    UPROPERTY(BlueprintReadWrite) bool bHasForcedTarget = false;
    UPROPERTY(BlueprintReadWrite) FVector2D ForcedTarget = FVector2D::ZeroVector;

    // FÁZE 1: Promìnné pro skuteèné stopování lovné zvìøe
    UPROPERTY(BlueprintReadWrite) FVector2D LastKnownHerdPosition = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadWrite) FVector2D HerdDirection = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadWrite) float TrackingConfidence = 0.0f;
};

USTRUCT(BlueprintType)
struct FSettlementData
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 SettlementID = -1;
    UPROPERTY(BlueprintReadWrite) int32 NationID = -1;
    UPROPERTY(BlueprintReadWrite) FVector2D Position = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadWrite) int32 Population = 0;
    UPROPERTY(BlueprintReadWrite) int32 InteractionCooldownDays = 0;
    UPROPERTY(BlueprintReadWrite) FTribeInventory Inventory;
    UPROPERTY(BlueprintReadWrite) FLinearColor Color = FLinearColor::White;
    UPROPERTY(BlueprintReadWrite) int32 GatherRadius = 6;
    UPROPERTY(BlueprintReadWrite) int32 SplitCount = 0;
    UPROPERTY() TArray<FPregnancyBatch> Pregnancies;
    UPROPERTY(BlueprintReadWrite) FKnowledgeContainer Knowledge;
    UPROPERTY(BlueprintReadWrite) FCultureProfile Culture;
    UPROPERTY(BlueprintReadWrite) float LocalFoodPotential = 0.0f;
    UPROPERTY(BlueprintReadWrite) float LocalWoodPotential = 0.0f;
    UPROPERTY(BlueprintReadWrite) float LocalWaterPotential = 0.0f;
    UPROPERTY(BlueprintReadWrite) float LocalStonePotential = 0.0f;
    UPROPERTY(BlueprintReadWrite) float ResourcePressure = 0.0f;
    UPROPERTY(BlueprintReadWrite) float EcologicalPressure = 0.0f;

    UPROPERTY(BlueprintReadWrite) bool bHasForcedExpansion = false;
    UPROPERTY(BlueprintReadWrite) FVector2D ForcedExpansionTarget = FVector2D::ZeroVector;

    UPROPERTY(BlueprintReadWrite) float ExpansionPoints = 0.0f;
    UPROPERTY() TSet<FIntPoint> ClaimedCells;
    UPROPERTY() TArray<FIntPoint> BorderCells;
};

USTRUCT(BlueprintType)
struct FTradeRoute
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 CityA_ID = -1;
    UPROPERTY(BlueprintReadWrite) int32 CityB_ID = -1;
    UPROPERTY(BlueprintReadWrite) float TransferSpeed = 0.01f;
    UPROPERTY(BlueprintReadWrite) bool bIsNational = false;
    UPROPERTY(BlueprintReadWrite) ETransportMode RouteType = ETransportMode::Land;
};

USTRUCT(BlueprintType)
struct FRoadNetwork
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 CityA_ID = -1;
    UPROPERTY(BlueprintReadWrite) int32 CityB_ID = -1;
    UPROPERTY(BlueprintReadWrite) TArray<FVector2D> Path;
    UPROPERTY(BlueprintReadWrite) int32 BuiltNodes = 0;
    UPROPERTY(BlueprintReadWrite) float ConstructionProgress = 0.0f;
    UPROPERTY(BlueprintReadWrite) bool bIsImpossible = false;
};

USTRUCT(BlueprintType)
struct FAnimalData
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) FVector2D Position = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadWrite) FVector2D TargetDirection = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadWrite) FVector2D StrategicTarget = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadWrite) float StrategicTargetScore = 0.0f;
    UPROPERTY(BlueprintReadWrite) double NextStrategicDecisionDay = 0.0;
    UPROPERTY(BlueprintReadWrite) FVector2D LastVisitedRegion = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadWrite) double NextAllowedReturnDay = 0.0;
    UPROPERTY(BlueprintReadWrite) EAnimalType Type = EAnimalType::Herbivore;
    UPROPERTY(BlueprintReadWrite) float Hunger = 0.0f;
    UPROPERTY(BlueprintReadWrite) float Age = 0.0f;
    UPROPERTY(BlueprintReadWrite) float HerdSize = 15.0f;

    // FÁZE 1: Kondice urèuje fyzické zdraví (1.0 = zdravé, 0.0 = zaèínají umírat)
    UPROPERTY(BlueprintReadWrite) float Condition = 1.0f;
};

USTRUCT()
struct FSimulationTask
{
    GENERATED_BODY()
    ESimulationTaskType TaskType;
    FIntPoint ChunkTarget;
};

USTRUCT(BlueprintType)
struct FNationData
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int32 NationID = 0;
    UPROPERTY(BlueprintReadWrite) FString NationName = "Novy Narod";
    UPROPERTY(BlueprintReadWrite) FCultureProfile NationalCulture;
    UPROPERTY(BlueprintReadWrite) TArray<int32> MemberSettlementIDs;
};

USTRUCT(BlueprintType)
struct FOrbitalBody
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) float Mass = 1.0f;
    UPROPERTY(BlueprintReadWrite) float Distance = 100.0f;
    UPROPERTY(BlueprintReadWrite) float OrbitalPeriod = 365.0f;
    UPROPERTY(BlueprintReadWrite) float CurrentAngle = 0.0f;
    UPROPERTY(BlueprintReadWrite) bool bIsMoon = false;
};

USTRUCT(BlueprintType)
struct FCosmosState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) float GlobalIrradianceOffset = 0.0f;
    UPROPERTY(BlueprintReadWrite) float GlobalTidalMultiplier = 1.0f;
    UPROPERTY(BlueprintReadWrite) float SeasonalityMultiplier = 1.0f;
    UPROPERTY(BlueprintReadWrite) float GlobalRadiation = 0.0f;
};

namespace EChunkVisualDirty
{
    const uint8 None = 0;
    const uint8 Terrain = 1 << 0;
    const uint8 Water = 1 << 1;
    const uint8 Flora = 1 << 2;
    const uint8 Cloud = 1 << 3;
    const uint8 TerrainColor = 1 << 4;
}