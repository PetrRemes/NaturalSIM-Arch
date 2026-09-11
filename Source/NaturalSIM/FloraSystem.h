#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h" 
#include "FloraSpeciesData.h"

class ASimWorldManager;

#include "FloraSystem.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UFloraSystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UFloraSystem();
    virtual void BeginPlay() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flora Database")
    TArray<UFloraSpeciesData*> SpeciesDatabase;

    const UFloraSpeciesData* GetSpeciesByID(uint8 ID) const;
    const UFloraSpeciesData* GetBestSpeciesForEnvironment(EBiomeType Biome, float Toxicity, float SoilDepth, FRandomStream& Stream) const;

    static void ProcessChunkFlora(FChunkData& OutChunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params);

    // Návrat k pùvodní signatuøe bez Managera
    static void BuildFloraMesh(TSharedPtr<FChunkMeshData> MeshData, const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params);

    void ProcessDailyGrowth(const TArray<FIntPoint>& ChunkKeys, TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, int32 StartIdx, int32 EndIdx, float DeltaDays);

    static EBiomeType DetermineBiome(float Temperature, float WaterAvailability, float Altitude, float FloraDensity, float GrassSpread);
    static FLinearColor GetBiomeColor(EBiomeType Biome);
};