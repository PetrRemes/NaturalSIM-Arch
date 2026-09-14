#include "WorldRenderer.h"
#include "SimWorldManager.h"
#include "FaunaSystem.h"
#include "HumanSystem.h"
#include "SettlementSystem.h"
#include "ClimateSystem.h"
#include "FloraSystem.h"
#include "TransportSystem.h" 
#include "DisasterSystem.h"

UWorldRenderer::UWorldRenderer()
{
    PrimaryComponentTick.bCanEverTick = false;

    TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
    TerrainMesh->SetupAttachment(this);
    TerrainMesh->bUseAsyncCooking = true;

    WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh")); WaterMesh->SetupAttachment(this); WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FloraMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FloraMesh")); FloraMesh->SetupAttachment(this); FloraMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TransportMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TransportMesh")); TransportMesh->SetupAttachment(this); TransportMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    FaunaHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("FaunaHISM"));
    FaunaHISM->SetupAttachment(this); FaunaHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FaunaHISM->NumCustomDataFloats = 3;

    HumanHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("HumanHISM"));
    HumanHISM->SetupAttachment(this); HumanHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HumanHISM->NumCustomDataFloats = 3;

    SettlementHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("SettlementHISM"));
    SettlementHISM->SetupAttachment(this); SettlementHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SettlementHISM->NumCustomDataFloats = 3;

    CaravanHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("CaravanHISM"));
    CaravanHISM->SetupAttachment(this); CaravanHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CaravanHISM->NumCustomDataFloats = 3; CaravanHISM->SetCastShadow(false);

    ShipHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("ShipHISM"));
    ShipHISM->SetupAttachment(this); ShipHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ShipHISM->NumCustomDataFloats = 3; ShipHISM->SetCastShadow(false);

    AirplaneHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("AirplaneHISM"));
    AirplaneHISM->SetupAttachment(this); AirplaneHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AirplaneHISM->NumCustomDataFloats = 3; AirplaneHISM->SetCastShadow(false);

    CloudHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("CloudHISM"));
    CloudHISM->SetupAttachment(this); CloudHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CloudHISM->NumCustomDataFloats = 3; CloudHISM->SetCastShadow(false);

    RainHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RainHISM"));
    RainHISM->SetupAttachment(this); RainHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RainHISM->NumCustomDataFloats = 3; RainHISM->SetCastShadow(false);

    FogHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("FogHISM"));
    FogHISM->SetupAttachment(this); FogHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FogHISM->NumCustomDataFloats = 3; FogHISM->SetCastShadow(false);

    DisasterHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("DisasterHISM"));
    DisasterHISM->SetupAttachment(this); DisasterHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DisasterHISM->NumCustomDataFloats = 3; DisasterHISM->SetCastShadow(false);

    TerrainMesh->SetCastShadow(false); WaterMesh->SetCastShadow(false);
    FloraMesh->SetCastShadow(false); FaunaHISM->SetCastShadow(false);
    HumanHISM->SetCastShadow(false); SettlementHISM->SetCastShadow(false);
    TransportMesh->SetCastShadow(false);
}

void UWorldRenderer::BeginPlay() {
    Super::BeginPlay();
}

void UWorldRenderer::InitializeRenderer(ASimWorldManager* InManager) { WorldManager = InManager; }

void UWorldRenderer::ClearAllMeshes() {
    if (TerrainMesh) TerrainMesh->ClearAllMeshSections();
    if (WaterMesh) WaterMesh->ClearAllMeshSections();
    if (FloraMesh) FloraMesh->ClearAllMeshSections();
    if (TransportMesh) TransportMesh->ClearAllMeshSections();

    if (FaunaHISM) FaunaHISM->ClearInstances();
    if (HumanHISM) HumanHISM->ClearInstances();
    if (SettlementHISM) SettlementHISM->ClearInstances();
    if (CaravanHISM) CaravanHISM->ClearInstances();
    if (ShipHISM) ShipHISM->ClearInstances();
    if (AirplaneHISM) AirplaneHISM->ClearInstances();

    if (CloudHISM) CloudHISM->ClearInstances();
    if (RainHISM) RainHISM->ClearInstances();
    if (FogHISM) FogHISM->ClearInstances();
    if (DisasterHISM) DisasterHISM->ClearInstances();
}

FLinearColor UWorldRenderer::GetHeatmapColor(const FCellStaticData& SCell, const FCellDynamicData& DCell, EWorldViewMode ViewMode)
{
    if (ViewMode == EWorldViewMode::Temperature) {
        float T = FMath::Clamp(DCell.Temperature, -15.0f, 40.0f);
        float Alpha = (T + 15.0f) / 55.0f;
        if (Alpha < 0.25f) return FMath::Lerp(FLinearColor(0.0f, 0.0f, 0.8f, 1.0f), FLinearColor(0.0f, 0.8f, 1.0f, 1.0f), Alpha / 0.25f);
        else if (Alpha < 0.5f) return FMath::Lerp(FLinearColor(0.0f, 0.8f, 1.0f, 1.0f), FLinearColor(0.0f, 1.0f, 0.0f, 1.0f), (Alpha - 0.25f) / 0.25f);
        else if (Alpha < 0.75f) return FMath::Lerp(FLinearColor(0.0f, 1.0f, 0.0f, 1.0f), FLinearColor(1.0f, 1.0f, 0.0f, 1.0f), (Alpha - 0.5f) / 0.25f);
        else return FMath::Lerp(FLinearColor(1.0f, 1.0f, 0.0f, 1.0f), FLinearColor(1.0f, 0.0f, 0.0f, 1.0f), (Alpha - 0.75f) / 0.25f);
    }
    else if (ViewMode == EWorldViewMode::Humidity) {
        float H = FMath::Clamp(DCell.Humidity, 0.0f, 1.0f);
        return FMath::Lerp(FLinearColor(0.9f, 0.8f, 0.5f, 1.0f), FLinearColor(0.0f, 0.3f, 1.0f, 1.0f), H);
    }
    else if (ViewMode == EWorldViewMode::Rainfall) {
        float R = FMath::Clamp(DCell.Rainfall / 3.0f, 0.0f, 1.0f);
        return FMath::Lerp(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f), FLinearColor(0.1f, 0.0f, 0.7f, 1.0f), R);
    }
    else if (ViewMode == EWorldViewMode::TectonicStress) {
        float Stress = FMath::Pow(FMath::Clamp(DCell.TectonicStress, 0.0f, 1.0f), 2.5f);
        float Magma = FMath::Clamp(DCell.MagmaPressure / 800.0f, 0.0f, 1.0f);

        FLinearColor BaseColor;
        if (Stress < 0.15f) BaseColor = FLinearColor(0.4f, 0.8f, 0.4f, 1.0f);
        else if (Stress < 0.45f) BaseColor = FMath::Lerp(FLinearColor(0.4f, 0.8f, 0.4f, 1.0f), FLinearColor(0.8f, 0.4f, 0.4f, 1.0f), (Stress - 0.15f) / 0.3f);
        else BaseColor = FMath::Lerp(FLinearColor(0.8f, 0.4f, 0.4f, 1.0f), FLinearColor(0.9f, 0.1f, 0.1f, 1.0f), (Stress - 0.45f) / 0.55f);

        if (Magma > 0.05f) BaseColor = FMath::Lerp(BaseColor, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f), Magma);
        if (DCell.Lava > 0.1f || SCell.bIsVolcano) return FLinearColor(1.0f, 0.3f, 0.0f, 1.0f);

        return BaseColor;
    }
    else if (ViewMode == EWorldViewMode::Danger) {
        float D = FMath::Clamp(DCell.DangerLevel, 0.0f, 1.0f);
        if (D < 0.5f) return FMath::Lerp(FLinearColor(0.0f, 0.8f, 0.2f, 1.0f), FLinearColor(1.0f, 1.0f, 0.0f, 1.0f), D * 2.0f);
        else return FMath::Lerp(FLinearColor(1.0f, 1.0f, 0.0f, 1.0f), FLinearColor(1.0f, 0.0f, 0.0f, 1.0f), (D - 0.5f) * 2.0f);
    }
    else if (ViewMode == EWorldViewMode::Political) {
        if (SCell.OwnerSettlementID != -1) {
            FLinearColor Base = SCell.BiomeColor;
            return FLinearColor::LerpUsingHSV(Base, SCell.PoliticalColor, 0.7f);
        }
        if (SCell.Elevation <= 0.0f) return FLinearColor(0.1f, 0.2f, 0.35f, 1.0f);
        return FLinearColor(0.85f, 0.80f, 0.70f, 1.0f);
    }
    return FLinearColor::White;
}

void UWorldRenderer::BuildTerrainMesh(TSharedPtr<FChunkMeshData> MeshData, const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    float CellSize = 50.0f; int32 ChunkSize = Params.ChunkSize; float SeaLevel = Params.SeaLevel;
    FVector2D ChunkCenter = ChunkCoord * ((ChunkSize - 1) * CellSize) + FVector2D((ChunkSize * CellSize) * 0.5f, (ChunkSize * CellSize) * 0.5f);

    int32 Step = 1;
    if (Params.bUseLOD) {
        float DistToPlayer = FVector2D::Distance(ChunkCenter, Params.PlayerPos2D);
        if (DistToPlayer > 60000.0f) Step = 4;
        else if (DistToPlayer > 30000.0f) Step = 2;
    }

    FVector FakeSunDir(-0.6f, -0.6f, 0.7f); FakeSunDir.Normalize();
    float Ambient = 0.30f; float DiffuseMult = 0.90f;

    for (int32 Y = 0; Y < ChunkSize - 1; Y += Step) {
        for (int32 X = 0; X < ChunkSize - 1; X += Step) {

            int32 NextX = FMath::Min(X + Step, ChunkSize - 1); int32 NextY = FMath::Min(Y + Step, ChunkSize - 1);
            int32 IdxTL = X + (Y * ChunkSize); int32 IdxTR = NextX + (Y * ChunkSize);
            int32 IdxBL = X + (NextY * ChunkSize); int32 IdxBR = NextX + (NextY * ChunkSize);

            float Z_TL = Chunk.StaticCells[IdxTL].Elevation + Chunk.DynamicCells[IdxTL].GlacierIce;
            float Z_TR = Chunk.StaticCells[IdxTR].Elevation + Chunk.DynamicCells[IdxTR].GlacierIce;
            float Z_BL = Chunk.StaticCells[IdxBL].Elevation + Chunk.DynamicCells[IdxBL].GlacierIce;
            float Z_BR = Chunk.StaticCells[IdxBR].Elevation + Chunk.DynamicCells[IdxBR].GlacierIce;

            FVector P_TL((ChunkCoord.X * (ChunkSize - 1) + X) * CellSize, (ChunkCoord.Y * (ChunkSize - 1) + Y) * CellSize, Z_TL);
            FVector P_TR((ChunkCoord.X * (ChunkSize - 1) + NextX) * CellSize, (ChunkCoord.Y * (ChunkSize - 1) + Y) * CellSize, Z_TR);
            FVector P_BL((ChunkCoord.X * (ChunkSize - 1) + X) * CellSize, (ChunkCoord.Y * (ChunkSize - 1) + NextY) * CellSize, Z_BL);
            FVector P_BR((ChunkCoord.X * (ChunkSize - 1) + NextX) * CellSize, (ChunkCoord.Y * (ChunkSize - 1) + NextY) * CellSize, Z_BR);

            auto AddTerrainTri = [&](FVector V1, FVector V2, FVector V3, const FCellStaticData& SCell, const FCellDynamicData& DCell)
                {
                    FVector N = FVector::CrossProduct(V2 - V1, V3 - V1).GetSafeNormal();
                    if (N.Z < 0.0f) N = -N;

                    FLinearColor FinalColor = FLinearColor::White;

                    if (Params.ViewMode == EWorldViewMode::Normal) {
                        FinalColor = SCell.BiomeColor;

                        if (DCell.Lava > 0.1f) FinalColor = FLinearColor(1.0f, 0.4f, 0.0f, 1.0f);
                        else if (SCell.bIsVolcano && DCell.Lava < 0.1f) FinalColor = FLinearColor(0.12f, 0.10f, 0.10f, 1.0f);
                        else {
                            bool bIsSteep = N.Z < 0.75f && DCell.SurfaceWater < 1.0f;

                            if (bIsSteep) {
                                FinalColor = FLinearColor(0.35f, 0.30f, 0.25f, 1.0f);
                            }

                            if (SCell.BuildingType == EBuildingType::Farm) {
                                FinalColor = FMath::Lerp(FinalColor, FLinearColor(0.85f, 0.75f, 0.25f, 1.0f), 0.85f);
                            }
                            else if (SCell.BuildingType == EBuildingType::Blacksmith) {
                                FinalColor = FMath::Lerp(FinalColor, FLinearColor(0.15f, 0.15f, 0.15f, 1.0f), 0.9f);
                            }
                            else if (SCell.BuildingType == EBuildingType::Market) {
                                FinalColor = FMath::Lerp(FinalColor, FLinearColor(0.6f, 0.4f, 0.3f, 1.0f), 0.7f);
                            }

                            if (DCell.AnimalBones > 0.1f)
                            {
                                FinalColor = FMath::Lerp(FinalColor, FLinearColor(0.8f, 0.8f, 0.8f, 1.0f), FMath::Clamp(DCell.AnimalBones / 50.0f, 0.0f, 0.8f));
                            }

                            if (DCell.HouseDensity > 0.0f || SCell.bHasRoad || SCell.BuildingType == EBuildingType::Mine || SCell.BuildingType == EBuildingType::LumberCamp)
                            {
                                FLinearColor UrbanCol = FLinearColor(0.40f, 0.35f, 0.30f, 1.0f);
                                FLinearColor BlendedBiome = FMath::Lerp(SCell.BiomeColor, UrbanCol, 0.6f);

                                float BlendAmount = (SCell.bHasRoad || SCell.BuildingType != EBuildingType::None) ? 0.8f : FMath::Clamp(DCell.HouseDensity, 0.0f, 0.85f);
                                FinalColor = FMath::Lerp(FinalColor, BlendedBiome, BlendAmount);
                            }

                            if (DCell.WaterPollution > 0.0f)
                            {
                                float PollutionAlpha = FMath::Clamp(DCell.WaterPollution * 3.0f, 0.0f, 1.0f);
                                FinalColor = FMath::Lerp(FinalColor, FLinearColor(0.20f, 0.18f, 0.15f, 1.0f), PollutionAlpha);
                            }

                            if (DCell.SnowAmount > 0.05f) {
                                float SnowAlpha = FMath::Clamp(DCell.SnowAmount / 1.5f, 0.0f, 1.0f);
                                if (bIsSteep) {
                                    float SteepnessDrop = FMath::Clamp((N.Z - 0.3f) * 2.5f, 0.0f, 1.0f);
                                    SnowAlpha *= SteepnessDrop;
                                }
                                FinalColor = FMath::Lerp(FinalColor, FLinearColor(0.95f, 0.98f, 1.0f, 1.0f), SnowAlpha);
                            }

                            if (DCell.GlacierIce > 0.1f) {
                                float IceAlpha = FMath::Clamp(DCell.GlacierIce / 15.0f, 0.0f, 1.0f);
                                FLinearColor IceColor(0.5f, 0.8f, 0.95f, 1.0f);
                                FinalColor = FMath::Lerp(FinalColor, IceColor, IceAlpha);
                            }
                        }
                    }
                    else {
                        FinalColor = UWorldRenderer::GetHeatmapColor(SCell, DCell, Params.ViewMode);
                        if (DCell.SurfaceWater > 0.1f && Params.ViewMode != EWorldViewMode::TectonicStress && Params.ViewMode != EWorldViewMode::Political) {
                            FinalColor *= 0.6f;
                        }
                    }

                    float Light = Ambient + FMath::Max(0.0f, FVector::DotProduct(N, FakeSunDir)) * DiffuseMult;
                    if (DCell.Lava > 0.1f || DCell.FireIntensity > 0.1f || (Params.ViewMode != EWorldViewMode::Normal && Params.ViewMode != EWorldViewMode::Political)) Light = 1.0f;

                    FinalColor *= Light;

                    if (Params.ViewMode == EWorldViewMode::Normal) {
                        FinalColor.A = FMath::Clamp(DCell.Wetness, 0.0f, 1.0f);
                        if (DCell.SnowAmount > 0.05f || DCell.GlacierIce > 0.1f) {
                            FinalColor.A = 0.0f;
                        }
                    }
                    else {
                        FinalColor.A = 1.0f;
                    }

                    int32 V = MeshData->Vertices.Num();
                    MeshData->Vertices.Add(V1); MeshData->Vertices.Add(V2); MeshData->Vertices.Add(V3);
                    MeshData->Normals.Add(N); MeshData->Normals.Add(N); MeshData->Normals.Add(N);
                    for (int i = 0; i < 3; i++)
                    {
                        MeshData->Triangles.Add(V + i);
                        MeshData->UV0.Add(FVector2D(0, 0));
                        MeshData->VertexColors.Add(FinalColor);
                    }
                };

            AddTerrainTri(P_TL, P_TR, P_BL, Chunk.StaticCells[IdxTL], Chunk.DynamicCells[IdxTL]);
            AddTerrainTri(P_TR, P_BR, P_BL, Chunk.StaticCells[IdxTR], Chunk.DynamicCells[IdxTR]);
        }
    }
}

void UWorldRenderer::BuildWaterMesh(TSharedPtr<FChunkMeshData> MeshData, const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params)
{
    float CellSize = 50.0f; int32 ChunkSize = Params.ChunkSize; float SeaLevel = Params.SeaLevel;
    FVector2D ChunkCenter = ChunkCoord * ((ChunkSize - 1) * CellSize) + FVector2D((ChunkSize * CellSize) * 0.5f, (ChunkSize * CellSize) * 0.5f);

    int32 Step = 1;
    if (Params.bUseLOD) {
        float DistToPlayer = FVector2D::Distance(ChunkCenter, Params.PlayerPos2D);
        if (DistToPlayer > 60000.0f) Step = 4;
        else if (DistToPlayer > 30000.0f) Step = 2;
    }

    if (Params.ViewMode == EWorldViewMode::Political) return;

    for (int32 Y = 0; Y < ChunkSize - 1; Y += Step) {
        for (int32 X = 0; X < ChunkSize - 1; X += Step) {

            int32 NextX = FMath::Min(X + Step, ChunkSize - 1); int32 NextY = FMath::Min(Y + Step, ChunkSize - 1);
            int32 IdxTL = X + (Y * ChunkSize); int32 IdxTR = NextX + (Y * ChunkSize);
            int32 IdxBL = X + (NextY * ChunkSize); int32 IdxBR = NextX + (NextY * ChunkSize);

            const FCellStaticData& STL = Chunk.StaticCells[IdxTL]; const FCellDynamicData& DTL = Chunk.DynamicCells[IdxTL];
            const FCellStaticData& STR = Chunk.StaticCells[IdxTR]; const FCellDynamicData& DTR = Chunk.DynamicCells[IdxTR];
            const FCellStaticData& SBL = Chunk.StaticCells[IdxBL]; const FCellDynamicData& DBL = Chunk.DynamicCells[IdxBL];
            const FCellStaticData& SBR = Chunk.StaticCells[IdxBR]; const FCellDynamicData& DBR = Chunk.DynamicCells[IdxBR];

            if (STL.Elevation <= SeaLevel || DTL.SurfaceWater > 0.1f ||
                STR.Elevation <= SeaLevel || DTR.SurfaceWater > 0.1f ||
                SBL.Elevation <= SeaLevel || DBL.SurfaceWater > 0.1f ||
                SBR.Elevation <= SeaLevel || DBR.SurfaceWater > 0.1f)
            {
                float MaxW = SeaLevel;
                bool bHasWater = false;

                if (STL.Elevation <= SeaLevel || DTL.SurfaceWater > 0.1f) { MaxW = FMath::Max(MaxW, STL.Elevation <= SeaLevel ? SeaLevel : STL.Elevation + DTL.SurfaceWater); bHasWater = true; }
                if (STR.Elevation <= SeaLevel || DTR.SurfaceWater > 0.1f) { MaxW = FMath::Max(MaxW, STR.Elevation <= SeaLevel ? SeaLevel : STR.Elevation + DTR.SurfaceWater); bHasWater = true; }
                if (SBL.Elevation <= SeaLevel || DBL.SurfaceWater > 0.1f) { MaxW = FMath::Max(MaxW, SBL.Elevation <= SeaLevel ? SeaLevel : SBL.Elevation + DBL.SurfaceWater); bHasWater = true; }
                if (SBR.Elevation <= SeaLevel || DBR.SurfaceWater > 0.1f) { MaxW = FMath::Max(MaxW, SBR.Elevation <= SeaLevel ? SeaLevel : SBR.Elevation + DBR.SurfaceWater); bHasWater = true; }

                if (!bHasWater) continue;

                float P_TL_X = (ChunkCoord.X * (ChunkSize - 1) + X) * CellSize; float P_TL_Y = (ChunkCoord.Y * (ChunkSize - 1) + Y) * CellSize;
                float P_TR_X = (ChunkCoord.X * (ChunkSize - 1) + NextX) * CellSize; float P_TR_Y = (ChunkCoord.Y * (ChunkSize - 1) + Y) * CellSize;
                float P_BL_X = (ChunkCoord.X * (ChunkSize - 1) + X) * CellSize; float P_BL_Y = (ChunkCoord.Y * (ChunkSize - 1) + NextY) * CellSize;
                float P_BR_X = (ChunkCoord.X * (ChunkSize - 1) + NextX) * CellSize; float P_BR_Y = (ChunkCoord.Y * (ChunkSize - 1) + NextY) * CellSize;

                float WZ_TL = (STL.Elevation <= SeaLevel) ? SeaLevel : (DTL.SurfaceWater > 0.1f ? STL.Elevation + DTL.SurfaceWater : MaxW);
                float WZ_TR = (STR.Elevation <= SeaLevel) ? SeaLevel : (DTR.SurfaceWater > 0.1f ? STR.Elevation + DTR.SurfaceWater : MaxW);
                float WZ_BL = (SBL.Elevation <= SeaLevel) ? SeaLevel : (DBL.SurfaceWater > 0.1f ? SBL.Elevation + DBL.SurfaceWater : MaxW);
                float WZ_BR = (SBR.Elevation <= SeaLevel) ? SeaLevel : (DBL.SurfaceWater > 0.1f ? SBR.Elevation + DBR.SurfaceWater : MaxW);

                FVector W_TL(P_TL_X, P_TL_Y, WZ_TL); FVector W_TR(P_TR_X, P_TR_Y, WZ_TR);
                FVector W_BL(P_BL_X, P_BL_Y, WZ_BL); FVector W_BR(P_BR_X, P_BR_Y, WZ_BR);

                float AvgElev = (STL.Elevation + STR.Elevation + SBL.Elevation + SBR.Elevation) * 0.25f;
                float AvgSurfaceWater = (DTL.SurfaceWater + DTR.SurfaceWater + DBL.SurfaceWater + DBR.SurfaceWater) * 0.25f;
                float Depth = (AvgElev <= SeaLevel) ? FMath::Max(0.0f, SeaLevel - AvgElev) : AvgSurfaceWater;
                float DepthAlpha = FMath::Clamp(Depth / (AvgElev <= SeaLevel ? 60.0f : 5.0f), 0.0f, 1.0f);

                FLinearColor DeepWater = FLinearColor(0.01f, 0.10f, 0.45f, 0.95f);
                FLinearColor ShallowWater = FLinearColor(0.15f, 0.65f, 0.85f, 0.85f);
                FLinearColor RiverFoam = FLinearColor(0.9f, 0.95f, 1.0f, 0.95f);

                FLinearColor WaterColor = FMath::Lerp(ShallowWater, DeepWater, DepthAlpha);

                float MaxFlow = FMath::Max(FMath::Max(DTL.WaterFlow, DTR.WaterFlow), FMath::Max(DBL.WaterFlow, DBR.WaterFlow));
                float MaxZ = FMath::Max(FMath::Max(WZ_TL, WZ_TR), FMath::Max(WZ_BL, WZ_BR));
                float MinZ = FMath::Min(FMath::Min(WZ_TL, WZ_TR), FMath::Min(WZ_BL, WZ_BR));
                float SlopeDrop = MaxZ - MinZ;

                if (AvgElev > SeaLevel && MaxFlow > 0.5f && SlopeDrop > 6.0f && SlopeDrop < 30.0f) {
                    WaterColor = RiverFoam;
                }
                else if (AvgElev > SeaLevel) {
                    WaterColor = ShallowWater;
                }

                if (Params.ViewMode != EWorldViewMode::Normal) {
                    WaterColor = UWorldRenderer::GetHeatmapColor(STL, DTL, Params.ViewMode);
                    WaterColor.A = 1.0f;
                }

                auto AddWaterTri = [&](FVector V1, FVector V2, FVector V3)
                    {
                        int32 V = MeshData->WaterVertices.Num();
                        MeshData->WaterVertices.Add(V1); MeshData->WaterVertices.Add(V2); MeshData->WaterVertices.Add(V3);
                        FVector N(0.0f, 0.0f, 1.0f);
                        MeshData->WaterNormals.Add(N); MeshData->WaterNormals.Add(N); MeshData->WaterNormals.Add(N);
                        for (int i = 0; i < 3; i++)
                        {
                            MeshData->WaterTriangles.Add(V + i);
                            MeshData->WaterUV0.Add(FVector2D(0, 0));
                            MeshData->WaterColors.Add(WaterColor);
                        }
                    };

                AddWaterTri(W_TL, W_TR, W_BL);
                AddWaterTri(W_TR, W_BR, W_BL);
            }
        }
    }
}

void UWorldRenderer::BuildChunkMesh_Async(TSharedPtr<FChunkMeshData> MeshData, const FChunkData& Chunk, FVector2D ChunkCoord, const FChunkGenerationParameters& Params, uint8 DirtyFlags, ASimWorldManager* Manager)
{
    if (Chunk.StaticCells.Num() == 0 || Chunk.DynamicCells.Num() == 0) return;

    if (DirtyFlags & EChunkVisualDirty::Terrain || DirtyFlags & EChunkVisualDirty::TerrainColor) {
        BuildTerrainMesh(MeshData, Chunk, ChunkCoord, Params);
    }
    if (DirtyFlags & EChunkVisualDirty::Water) {
        BuildWaterMesh(MeshData, Chunk, ChunkCoord, Params);
    }
    if (DirtyFlags & EChunkVisualDirty::Flora) {
        UFloraSystem::BuildFloraMesh(MeshData, Chunk, ChunkCoord, Params);
    }
}

void UWorldRenderer::RenderChunk_GameThread(TSharedPtr<FChunkMeshData> MeshData, FVector2D ChunkCoord, uint8 RenderedFlags)
{
    if (!WorldManager || !MeshData.IsValid()) return;
    int32 ChunkIndex = FMath::FloorToInt(ChunkCoord.X) + (FMath::FloorToInt(ChunkCoord.Y) * WorldManager->WorldSizeInChunksX);

    if (RenderedFlags & EChunkVisualDirty::Terrain || RenderedFlags & EChunkVisualDirty::TerrainColor) {
        if (TerrainMesh && MeshData->Vertices.Num() > 0) {
            // OPTIMALIZACE: false místo true pro kolize uleví Game Threadu o desítky milisekund!
            TerrainMesh->CreateMeshSection_LinearColor(ChunkIndex, MeshData->Vertices, MeshData->Triangles, MeshData->Normals, MeshData->UV0, MeshData->VertexColors, TArray<FProcMeshTangent>(), false);
        }
    }

    if (RenderedFlags & EChunkVisualDirty::Water) {
        if (WaterMesh && MeshData->WaterVertices.Num() > 0) {
            WaterMesh->CreateMeshSection_LinearColor(ChunkIndex, MeshData->WaterVertices, MeshData->WaterTriangles, MeshData->WaterNormals, MeshData->WaterUV0, MeshData->WaterColors, TArray<FProcMeshTangent>(), false);
        }
        else if (WaterMesh) {
            WaterMesh->ClearMeshSection(ChunkIndex);
        }
    }

    if (RenderedFlags & EChunkVisualDirty::Flora) {
        if (FloraMesh && MeshData->FloraVertices.Num() > 0) {
            FloraMesh->CreateMeshSection_LinearColor(ChunkIndex, MeshData->FloraVertices, MeshData->FloraTriangles, MeshData->FloraNormals, MeshData->FloraUV0, MeshData->FloraColors, TArray<FProcMeshTangent>(), false);
        }
        else if (FloraMesh) {
            FloraMesh->ClearMeshSection(ChunkIndex);
        }

        if (TransportMesh) {
            if (WorldManager && WorldManager->TransportModule && WorldManager->SettlementModule) {
                WorldManager->TransportModule->BuildTransportMesh(MeshData, WorldManager->SettlementModule->Settlements, FIntPoint(ChunkCoord.X, ChunkCoord.Y), WorldManager);
            }
            if (MeshData->TransportVertices.Num() > 0) {
                TransportMesh->CreateMeshSection_LinearColor(ChunkIndex, MeshData->TransportVertices, MeshData->TransportTriangles, TArray<FVector>(), MeshData->TransportUV0, MeshData->TransportColors, TArray<FProcMeshTangent>(), false);
            }
            else if (TransportMesh) {
                TransportMesh->ClearMeshSection(ChunkIndex);
            }
        }
    }

    WorldManager->NotifyChunkRendered();
}

void UWorldRenderer::UpdateWeatherEntities()
{
    if (!WorldManager) return;

    TArray<FTransform> CloudTransforms;  CloudTransforms.Reserve(10000);
    TArray<FLinearColor> CloudColors;    CloudColors.Reserve(10000);
    TArray<FTransform> RainTransforms;   RainTransforms.Reserve(5000);
    TArray<FLinearColor> RainColors;     RainColors.Reserve(5000);
    TArray<FTransform> FogTransforms;    FogTransforms.Reserve(5000);
    TArray<FLinearColor> FogColors;      FogColors.Reserve(5000);

    float CellSize = 50.0f;
    float CloudDensityThreshold = WorldManager->CloudDensityThreshold;

    float SeasonAlpha = (WorldManager->CurrentDay / 365.0f) * PI * 2.0f;
    FVector2D GlobalWind(FMath::Cos(SeasonAlpha), FMath::Sin(SeasonAlpha));
    GlobalWind.Normalize();

    FVector PlayerLoc = WorldManager->GetPlayerLocation();
    FVector2D PlayerPos2D(PlayerLoc.X, PlayerLoc.Y);
    float RenderDistSq = 50000.0f * 50000.0f;

    for (const auto& Pair : WorldManager->WorldChunks) {
        FVector2D ChunkCoord = FVector2D(Pair.Key.X, Pair.Key.Y);
        int32 ChunkSize = WorldManager->ChunkSize;
        float ChunkWorldSize = (ChunkSize - 1) * CellSize;

        // OPTIMALIZACE: Chunk-Level Culling odstraní testování milionù bunìk u vzdálených chunkù
        FVector2D ChunkCenter = (ChunkCoord * ChunkWorldSize) + FVector2D(ChunkWorldSize * 0.5f, ChunkWorldSize * 0.5f);
        if (FVector2D::DistSquared(ChunkCenter, PlayerPos2D) > RenderDistSq + (ChunkWorldSize * ChunkWorldSize)) continue;

        const FChunkData& Chunk = Pair.Value;
        int32 Step = WorldManager->CloudResolutionStep;
        float HalfStep = (CellSize * Step) * 0.5f;
        float CloudScaleXY = (HalfStep * 1.9f) / 100.0f;

        for (int32 Y = 0; Y < ChunkSize - 1; Y += Step) {
            for (int32 X = 0; X < ChunkSize - 1; X += Step) {

                float LocalX = (ChunkCoord.X * ChunkWorldSize) + (X * CellSize);
                float LocalY = (ChunkCoord.Y * ChunkWorldSize) + (Y * CellSize);

                int32 Index = X + (Y * ChunkSize);
                if (!Chunk.StaticCells.IsValidIndex(Index)) continue;

                const FCellStaticData& SCell = Chunk.StaticCells[Index];
                const FCellDynamicData& DCell = Chunk.DynamicCells[Index];

                float VisualCloudDensity = DCell.CloudDensity;
                float VisualRainfall = DCell.Rainfall;
                bool bHasAsh = DCell.AshDensity > 0.05f;

                if (SCell.Elevation > WorldManager->SeaLevel && VisualRainfall < 0.01f && !bHasAsh) {
                    int32 WakeX = FMath::Clamp(X + FMath::RoundToInt(GlobalWind.X * 12.0f), 0, ChunkSize - 1);
                    int32 WakeY = FMath::Clamp(Y + FMath::RoundToInt(GlobalWind.Y * 12.0f), 0, ChunkSize - 1);
                    float WakeRain = Chunk.DynamicCells[WakeX + WakeY * ChunkSize].Rainfall;

                    if (WakeRain > 0.15f) {
                        float FogNoise = FMath::PerlinNoise2D(FVector2D(LocalX * 0.001f, LocalY * 0.001f));
                        if (FogNoise > -0.2f) {
                            float FogAlpha = FMath::Clamp(WakeRain * 0.4f * (FogNoise + 0.5f), 0.0f, 0.35f);
                            if (FogAlpha > 0.05f) {
                                FVector FogCenter(LocalX, LocalY, SCell.Elevation + DCell.GlacierIce + 10.0f + HalfStep);
                                FogTransforms.Add(FTransform(FRotator::ZeroRotator, FogCenter, FVector(CloudScaleXY * 0.98f)));
                                FogColors.Add(FLinearColor(0.95f, 0.95f, 0.95f, FogAlpha));
                            }
                        }
                    }
                }

                if (VisualCloudDensity < CloudDensityThreshold && !bHasAsh && DCell.EruptionDaysRemaining <= 0.0f) continue;

                float TerrenZ = FMath::Max(0.0f, SCell.Elevation + DCell.GlacierIce - WorldManager->SeaLevel);
                float CloudBaseZ = WorldManager->SeaLevel + 1200.0f + (TerrenZ * 0.5f);

                CloudBaseZ += FMath::PerlinNoise2D(FVector2D(LocalX * 0.001f, LocalY * 0.001f)) * 50.0f;

                if (DCell.EruptionDaysRemaining > 0.0f) {
                    float Z = SCell.Elevation + DCell.GlacierIce + HalfStep;
                    while (Z < CloudBaseZ) {
                        CloudTransforms.Add(FTransform(FRotator::ZeroRotator, FVector(LocalX, LocalY, Z), FVector(CloudScaleXY * 1.5f)));
                        CloudColors.Add(FLinearColor(0.05f, 0.05f, 0.05f, 0.98f));
                        Z += (HalfStep * 2.0f);
                    }
                }

                if (bHasAsh) {
                    float AshAlpha = FMath::Clamp(DCell.AshDensity / 5.0f, 0.0f, 1.0f);
                    CloudTransforms.Add(FTransform(FRotator::ZeroRotator, FVector(LocalX, LocalY, CloudBaseZ - HalfStep), FVector(CloudScaleXY * 1.2f)));
                    CloudColors.Add(FLinearColor(0.12f, 0.10f, 0.10f, AshAlpha * 0.95f));
                }

                if (VisualCloudDensity >= CloudDensityThreshold) {
                    float Surplus = VisualCloudDensity - CloudDensityThreshold;
                    int32 StackCount = 1;
                    if (Surplus > 0.5f) StackCount = 4;
                    else if (Surplus > 0.3f) StackCount = 3;
                    else if (Surplus > 0.1f) StackCount = 2;

                    float Darkening = FMath::Clamp(Surplus * 1.5f, 0.0f, 1.0f);
                    FLinearColor StormColor = FLinearColor(0.12f, 0.22f, 0.50f, 0.95f);
                    FLinearColor CloudColor = FMath::Lerp(FLinearColor(1.0f, 1.0f, 1.0f, 0.90f), StormColor, Darkening);

                    for (int z = 0; z < StackCount; z++) {
                        FVector Center(LocalX, LocalY, CloudBaseZ + (z * HalfStep * 2.0f));
                        FLinearColor BlockColor = CloudColor * (1.0f - (z * 0.08f));
                        BlockColor.A = CloudColor.A;

                        CloudTransforms.Add(FTransform(FRotator::ZeroRotator, Center, FVector(CloudScaleXY * 0.95f)));
                        CloudColors.Add(BlockColor);
                    }

                    if (VisualRainfall > 0.05f) {
                        float DropZ = CloudBaseZ - HalfStep;
                        float RainHeight = DropZ - (SCell.Elevation + DCell.GlacierIce);
                        FVector RainCenter(LocalX, LocalY, SCell.Elevation + DCell.GlacierIce + (RainHeight * 0.5f));

                        float RainScaleZ = RainHeight / 100.0f;
                        RainTransforms.Add(FTransform(FRotator::ZeroRotator, RainCenter, FVector(CloudScaleXY * 0.4f, CloudScaleXY * 0.4f, RainScaleZ)));
                        RainColors.Add(FLinearColor(0.6f, 0.7f, 0.9f, FMath::Clamp(VisualRainfall * 0.3f, 0.0f, 0.5f)));
                    }
                }
            }
        }
    }

    auto SyncHISM = [](UHierarchicalInstancedStaticMeshComponent* HISM, const TArray<FTransform>& Transforms, const TArray<FLinearColor>& Colors) {
        if (!HISM) return;
        int32 NeededCount = Transforms.Num();
        int32 CurrentCount = HISM->GetInstanceCount();

        if (NeededCount > CurrentCount) {
            TArray<FTransform> MissingTransforms;
            MissingTransforms.SetNumUninitialized(NeededCount - CurrentCount);

            FTransform SafeT;
            SafeT.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
            SafeT.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
            SafeT.SetRotation(FQuat::Identity);

            for (int32 i = 0; i < MissingTransforms.Num(); i++) {
                MissingTransforms[i] = SafeT;
            }
            HISM->AddInstances(MissingTransforms, false);
            CurrentCount = NeededCount;
        }

        TArray<FTransform> BatchTransforms;
        BatchTransforms.SetNumUninitialized(CurrentCount);

        for (int32 i = 0; i < NeededCount; i++) {
            FTransform T = Transforms[i];

            if (T.GetScale3D().IsNearlyZero()) {
                T.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
            }
            if (T.ContainsNaN()) {
                T.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
                T.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
                T.SetRotation(FQuat::Identity);
            }
            BatchTransforms[i] = T;
        }

        FTransform HiddenTransform;
        HiddenTransform.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
        HiddenTransform.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
        HiddenTransform.SetRotation(FQuat::Identity);

        for (int32 i = NeededCount; i < CurrentCount; i++) {
            BatchTransforms[i] = HiddenTransform;
        }

        if (CurrentCount > 0) {
            HISM->BatchUpdateInstancesTransforms(0, BatchTransforms, true, true);
        }

        for (int32 i = 0; i < NeededCount; i++) {
            if (Colors.IsValidIndex(i)) {
                HISM->SetCustomDataValue(i, 0, Colors[i].R, false);
                HISM->SetCustomDataValue(i, 1, Colors[i].G, false);
                HISM->SetCustomDataValue(i, 2, Colors[i].B, false);
            }
        }

        HISM->MarkRenderStateDirty();
        };

    if (CloudHISM) SyncHISM(CloudHISM, CloudTransforms, CloudColors);
    if (RainHISM) SyncHISM(RainHISM, RainTransforms, RainColors);
    if (FogHISM) SyncHISM(FogHISM, FogTransforms, FogColors);
}

void UWorldRenderer::UpdateDisasterEntities()
{
    if (!WorldManager || !DisasterHISM || !WorldManager->DisasterModule) return;

    TArray<FTransform> Transforms;
    TArray<FLinearColor> Colors;

    for (const FDisasterWarning& Warning : WorldManager->DisasterModule->ActiveWarnings) {
        FVector Loc(Warning.Epicenter.X, Warning.Epicenter.Y, WorldManager->SeaLevel + 1200.0f);

        float Pulse = 1.0f + FMath::Sin(WorldManager->GetWorld()->GetTimeSeconds() * 3.0f) * 0.2f;
        float Scale = 5.0f * Pulse;

        Transforms.Add(FTransform(FRotator::ZeroRotator, Loc, FVector(Scale)));

        if (Warning.Type == EDisasterType::Flood) Colors.Add(FLinearColor(0.0f, 0.3f, 1.0f, 1.0f));
        else if (Warning.Type == EDisasterType::VolcanicEruption) Colors.Add(FLinearColor(1.0f, 0.1f, 0.0f, 1.0f));
        else Colors.Add(FLinearColor(1.0f, 0.8f, 0.0f, 1.0f));
    }

    auto SyncHISM = [](UHierarchicalInstancedStaticMeshComponent* HISM, const TArray<FTransform>& Transforms, const TArray<FLinearColor>& Colors) {
        if (!HISM) return;
        int32 NeededCount = Transforms.Num();
        int32 CurrentCount = HISM->GetInstanceCount();

        if (NeededCount > CurrentCount) {
            TArray<FTransform> MissingTransforms;
            MissingTransforms.SetNumUninitialized(NeededCount - CurrentCount);

            FTransform SafeT;
            SafeT.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
            SafeT.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
            SafeT.SetRotation(FQuat::Identity);

            for (int32 i = 0; i < MissingTransforms.Num(); i++) {
                MissingTransforms[i] = SafeT;
            }
            HISM->AddInstances(MissingTransforms, false);
            CurrentCount = NeededCount;
        }

        TArray<FTransform> BatchTransforms;
        BatchTransforms.SetNumUninitialized(CurrentCount);

        for (int32 i = 0; i < NeededCount; i++) {
            FTransform T = Transforms[i];

            if (T.GetScale3D().IsNearlyZero()) {
                T.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
            }
            if (T.ContainsNaN()) {
                T.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
                T.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
                T.SetRotation(FQuat::Identity);
            }
            BatchTransforms[i] = T;
        }

        FTransform HiddenTransform;
        HiddenTransform.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
        HiddenTransform.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
        HiddenTransform.SetRotation(FQuat::Identity);

        for (int32 i = NeededCount; i < CurrentCount; i++) {
            BatchTransforms[i] = HiddenTransform;
        }

        if (CurrentCount > 0) {
            HISM->BatchUpdateInstancesTransforms(0, BatchTransforms, true, true);
        }

        for (int32 i = 0; i < NeededCount; i++) {
            if (Colors.IsValidIndex(i)) {
                HISM->SetCustomDataValue(i, 0, Colors[i].R, false);
                HISM->SetCustomDataValue(i, 1, Colors[i].G, false);
                HISM->SetCustomDataValue(i, 2, Colors[i].B, false);
            }
        }

        HISM->MarkRenderStateDirty();
        };

    SyncHISM(DisasterHISM, Transforms, Colors);
}

void UWorldRenderer::UpdateFastEntities()
{
    if (!WorldManager) return;

    auto SyncHISM = [](UHierarchicalInstancedStaticMeshComponent* HISM, const TArray<FTransform>& Transforms, const TArray<FLinearColor>& Colors) {
        if (!HISM) return;
        int32 NeededCount = Transforms.Num();
        int32 CurrentCount = HISM->GetInstanceCount();

        if (NeededCount > CurrentCount) {
            TArray<FTransform> MissingTransforms;
            MissingTransforms.SetNumUninitialized(NeededCount - CurrentCount);

            FTransform SafeT;
            SafeT.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
            SafeT.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
            SafeT.SetRotation(FQuat::Identity);

            for (int32 i = 0; i < MissingTransforms.Num(); i++) {
                MissingTransforms[i] = SafeT;
            }
            HISM->AddInstances(MissingTransforms, false);
            CurrentCount = NeededCount;
        }

        TArray<FTransform> BatchTransforms;
        BatchTransforms.SetNumUninitialized(CurrentCount);

        for (int32 i = 0; i < NeededCount; i++) {
            FTransform T = Transforms[i];

            if (T.GetScale3D().IsNearlyZero()) {
                T.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
            }
            if (T.ContainsNaN()) {
                T.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
                T.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
                T.SetRotation(FQuat::Identity);
            }
            BatchTransforms[i] = T;
        }

        FTransform HiddenTransform;
        HiddenTransform.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
        HiddenTransform.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
        HiddenTransform.SetRotation(FQuat::Identity);

        for (int32 i = NeededCount; i < CurrentCount; i++) {
            BatchTransforms[i] = HiddenTransform;
        }

        if (CurrentCount > 0) {
            HISM->BatchUpdateInstancesTransforms(0, BatchTransforms, true, true);
        }

        for (int32 i = 0; i < NeededCount; i++) {
            if (Colors.IsValidIndex(i)) {
                HISM->SetCustomDataValue(i, 0, Colors[i].R, false);
                HISM->SetCustomDataValue(i, 1, Colors[i].G, false);
                HISM->SetCustomDataValue(i, 2, Colors[i].B, false);
            }
        }

        HISM->MarkRenderStateDirty();
        };

    FVector PlayerLoc = WorldManager->GetPlayerLocation();
    FVector2D PlayerPos2D(PlayerLoc.X, PlayerLoc.Y);
    float RenderDistSq = 80000.0f * 80000.0f;

    if (WorldManager->bFaunaVisualDirty && FaunaHISM && WorldManager->FaunaModule) {
        TArray<FTransform> Transforms; Transforms.Reserve(500);
        TArray<FLinearColor> Colors; Colors.Reserve(500);

        for (const FAnimalData& Animal : WorldManager->FaunaModule->Animals) {
            if (Animal.HerdSize <= 0.0f) continue;
            if (FVector2D::DistSquared(Animal.Position, PlayerPos2D) > RenderDistSq) continue;

            FCellStaticData SCell; FCellDynamicData DCell;
            int32 GX = FMath::FloorToInt(Animal.Position.X / 50.0f);
            int32 GY = FMath::FloorToInt(Animal.Position.Y / 50.0f);

            if (WorldManager->GetCellGlobal(GX, GY, SCell, DCell)) {
                float Scale = (Animal.Type == EAnimalType::Predator) ? 1.5f : 1.0f + (Animal.HerdSize * 0.02f);
                FVector Loc(Animal.Position.X, Animal.Position.Y, SCell.Elevation + DCell.GlacierIce + 25.0f);
                FQuat Rot = FRotationMatrix::MakeFromX(FVector(Animal.TargetDirection.X, Animal.TargetDirection.Y, 0.0f)).ToQuat();
                Transforms.Add(FTransform(Rot, Loc, FVector(Scale)));

                if (Animal.Type == EAnimalType::Predator) Colors.Add(FLinearColor(0.8f, 0.1f, 0.1f));
                else if (Animal.Type == EAnimalType::ForestAnimal) Colors.Add(FLinearColor(0.1f, 0.3f, 0.1f));
                else Colors.Add(FLinearColor(0.8f, 0.7f, 0.2f));
            }
        }
        SyncHISM(FaunaHISM, Transforms, Colors);
        WorldManager->bFaunaVisualDirty = false;
    }

    if (WorldManager->bHumanVisualDirty && HumanHISM && WorldManager->HumanModule) {
        TArray<FTransform> Transforms; Transforms.Reserve(500);
        TArray<FLinearColor> Colors; Colors.Reserve(500);

        for (const FTribeData& Tribe : WorldManager->HumanModule->Tribes) {
            if (Tribe.Population <= 0) continue;
            if (FVector2D::DistSquared(Tribe.Position, PlayerPos2D) > RenderDistSq) continue;

            FCellStaticData SCell; FCellDynamicData DCell;
            int32 GX = FMath::FloorToInt(Tribe.Position.X / 50.0f);
            int32 GY = FMath::FloorToInt(Tribe.Position.Y / 50.0f);

            if (WorldManager->GetCellGlobal(GX, GY, SCell, DCell)) {
                float Scale = 1.0f + (Tribe.Population * 0.01f);
                FVector Loc(Tribe.Position.X, Tribe.Position.Y, SCell.Elevation + DCell.GlacierIce + 30.0f);
                FVector2D Dir = (Tribe.TargetRegion - Tribe.Position).GetSafeNormal();
                if (Dir.IsNearlyZero()) Dir = FVector2D(1, 0);
                FQuat Rot = FRotationMatrix::MakeFromX(FVector(Dir.X, Dir.Y, 0.0f)).ToQuat();
                Transforms.Add(FTransform(Rot, Loc, FVector(Scale)));
                Colors.Add(Tribe.TribeColor);
            }
        }
        SyncHISM(HumanHISM, Transforms, Colors);
        WorldManager->bHumanVisualDirty = false;
    }
}

void UWorldRenderer::UpdateSettlementEntities()
{
    if (!WorldManager || !SettlementHISM || !WorldManager->SettlementModule) return;

    TArray<FTransform> Transforms; Transforms.Reserve(5000);
    TArray<FLinearColor> Colors; Colors.Reserve(5000);

    float GoldenAngle = 137.507764f * (PI / 180.0f);

    for (const FSettlementData& City : WorldManager->SettlementModule->Settlements) {
        if (City.Population <= 0) continue;

        FRandomStream BldStream(FMath::RoundToInt(City.Position.X) * 73 + FMath::RoundToInt(City.Position.Y) * 37);

        int32 PopPerHouse = 50;
        int32 MaxHouses = FMath::Clamp(FMath::FloorToInt((float)City.Population / PopPerHouse), 1, 150);

        // OPTIMALIZACE: Vogel's Golden Spiral. Zcela odstraòuje O(N^2) kolizní check v cyklu.
        for (int h = 0; h < MaxHouses; h++) {
            float r = 14.0f * FMath::Sqrt((float)h);
            float theta = h * GoldenAngle;
            FVector2D HPos = City.Position + FVector2D(FMath::Cos(theta), FMath::Sin(theta)) * r;

            int32 GlobalX = FMath::FloorToInt(HPos.X / 50.0f);
            int32 GlobalY = FMath::FloorToInt(HPos.Y / 50.0f);

            FCellStaticData SCell; FCellDynamicData DCell;
            if (!WorldManager->GetCellGlobal(GlobalX, GlobalY, SCell, DCell)) continue;
            if (DCell.SurfaceWater >= 1.0f || SCell.Elevation <= WorldManager->SeaLevel || SCell.Elevation > WorldManager->SeaLevel + 800.0f) continue;
            if (DCell.GlacierIce > 0.5f) continue;

            FVector Loc(HPos.X, HPos.Y, SCell.Elevation);
            float RandomYaw = BldStream.FRandRange(0.0f, 360.0f);
            FQuat Rot = FRotator(0.0f, RandomYaw, 0.0f).Quaternion();

            float RandomScale = BldStream.FRandRange(0.15f, 0.25f);

            Transforms.Add(FTransform(Rot, Loc, FVector(RandomScale)));
            Colors.Add(City.Color);
        }

        for (FIntPoint Coord : City.ClaimedCells) {
            FCellStaticData SCell; FCellDynamicData DCell;
            if (WorldManager->GetCellGlobal(Coord.X, Coord.Y, SCell, DCell)) {
                if (SCell.Elevation <= WorldManager->SeaLevel || DCell.SurfaceWater >= 1.0f) continue;

                FVector Loc(Coord.X * 50.0f + 25.0f, Coord.Y * 50.0f + 25.0f, SCell.Elevation);
                float RandomYaw = BldStream.FRandRange(0.0f, 360.0f);
                FQuat Rot = FRotator(0.0f, RandomYaw, 0.0f).Quaternion();

                if (SCell.BuildingType == EBuildingType::Mine) {
                    Transforms.Add(FTransform(Rot, Loc, FVector(0.5f)));
                    Colors.Add(FLinearColor(0.2f, 0.2f, 0.2f, 1.0f));
                }
                else if (SCell.BuildingType == EBuildingType::LumberCamp) {
                    Transforms.Add(FTransform(Rot, Loc, FVector(0.35f)));
                    Colors.Add(FLinearColor(0.35f, 0.20f, 0.10f, 1.0f));
                }
                else if (SCell.BuildingType == EBuildingType::Blacksmith) {
                    Transforms.Add(FTransform(Rot, Loc, FVector(0.4f)));
                    Colors.Add(FLinearColor(0.3f, 0.05f, 0.05f, 1.0f));
                }
                else if (SCell.BuildingType == EBuildingType::Market) {
                    Transforms.Add(FTransform(Rot, Loc, FVector(0.5f)));
                    Colors.Add(FLinearColor(0.9f, 0.7f, 0.1f, 1.0f));
                }
            }
        }
    }

    auto SyncHISM = [](UHierarchicalInstancedStaticMeshComponent* HISM, const TArray<FTransform>& Transforms, const TArray<FLinearColor>& Colors) {
        if (!HISM) return;
        int32 NeededCount = Transforms.Num();
        int32 CurrentCount = HISM->GetInstanceCount();

        if (NeededCount > CurrentCount) {
            TArray<FTransform> MissingTransforms;
            MissingTransforms.SetNumUninitialized(NeededCount - CurrentCount);

            FTransform SafeT;
            SafeT.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
            SafeT.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
            SafeT.SetRotation(FQuat::Identity);

            for (int32 i = 0; i < MissingTransforms.Num(); i++) {
                MissingTransforms[i] = SafeT;
            }
            HISM->AddInstances(MissingTransforms, false);
            CurrentCount = NeededCount;
        }

        TArray<FTransform> BatchTransforms;
        BatchTransforms.SetNumUninitialized(CurrentCount);

        for (int32 i = 0; i < NeededCount; i++) {
            FTransform T = Transforms[i];

            if (T.GetScale3D().IsNearlyZero()) {
                T.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
            }
            if (T.ContainsNaN()) {
                T.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
                T.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
                T.SetRotation(FQuat::Identity);
            }
            BatchTransforms[i] = T;
        }

        FTransform HiddenTransform;
        HiddenTransform.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
        HiddenTransform.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
        HiddenTransform.SetRotation(FQuat::Identity);

        for (int32 i = NeededCount; i < CurrentCount; i++) {
            BatchTransforms[i] = HiddenTransform;
        }

        if (CurrentCount > 0) {
            HISM->BatchUpdateInstancesTransforms(0, BatchTransforms, true, true);
        }

        for (int32 i = 0; i < NeededCount; i++) {
            if (Colors.IsValidIndex(i)) {
                HISM->SetCustomDataValue(i, 0, Colors[i].R, false);
                HISM->SetCustomDataValue(i, 1, Colors[i].G, false);
                HISM->SetCustomDataValue(i, 2, Colors[i].B, false);
            }
        }

        HISM->MarkRenderStateDirty();
        };

    SyncHISM(SettlementHISM, Transforms, Colors);
    WorldManager->bSettlementVisualDirty = false;
}

void UWorldRenderer::UpdateTransportEntities()
{
    if (!WorldManager || !WorldManager->TransportModule) return;

    TArray<FTransform> CaravanTransforms; TArray<FLinearColor> CaravanColors;
    TArray<FTransform> ShipTransforms;    TArray<FLinearColor> ShipColors;
    TArray<FTransform> AirTransforms;     TArray<FLinearColor> AirColors;

    for (const FVehicleData& V : WorldManager->TransportModule->ActiveVehicles) {
        float Z = WorldManager->SeaLevel;
        FVector2D Dir = FVector2D(1, 0);
        if (V.Path.Num() > 1 && V.CurrentPathNode + 1 < V.Path.Num()) {
            Dir = (V.Path[V.CurrentPathNode + 1] - V.Position).GetSafeNormal();
            if (Dir.IsNearlyZero()) Dir = FVector2D(1, 0);
        }
        FQuat Rot = FRotationMatrix::MakeFromX(FVector(Dir.X, Dir.Y, 0.0f)).ToQuat();

        if (V.Type == EVehicleType::Caravan) {
            FCellStaticData SCell; FCellDynamicData DCell;
            int32 GX = FMath::FloorToInt(V.Position.X / 50.0f);
            int32 GY = FMath::FloorToInt(V.Position.Y / 50.0f);
            if (WorldManager->GetCellGlobal(GX, GY, SCell, DCell)) {
                Z = SCell.Elevation + DCell.GlacierIce;
            }
            CaravanTransforms.Add(FTransform(Rot, FVector(V.Position.X, V.Position.Y, Z + 10.0f), FVector(0.5f)));
            CaravanColors.Add(FLinearColor(0.6f, 0.4f, 0.2f, 1.0f));
        }
        else if (V.Type == EVehicleType::Ship) {
            ShipTransforms.Add(FTransform(Rot, FVector(V.Position.X, V.Position.Y, Z + 5.0f), FVector(1.0f)));
            ShipColors.Add(FLinearColor(0.8f, 0.8f, 0.8f, 1.0f));
        }
        else if (V.Type == EVehicleType::Airplane) {
            Z = WorldManager->SeaLevel + 1500.0f;
            AirTransforms.Add(FTransform(Rot, FVector(V.Position.X, V.Position.Y, Z), FVector(2.0f)));
            AirColors.Add(FLinearColor(0.9f, 0.9f, 0.9f, 1.0f));
        }
    }

    auto SyncHISM = [](UHierarchicalInstancedStaticMeshComponent* HISM, const TArray<FTransform>& Transforms, const TArray<FLinearColor>& Colors) {
        if (!HISM) return;
        int32 NeededCount = Transforms.Num();
        int32 CurrentCount = HISM->GetInstanceCount();

        if (NeededCount > CurrentCount) {
            TArray<FTransform> MissingTransforms;
            MissingTransforms.SetNumUninitialized(NeededCount - CurrentCount);

            FTransform SafeT;
            SafeT.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
            SafeT.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
            SafeT.SetRotation(FQuat::Identity);

            for (int32 i = 0; i < MissingTransforms.Num(); i++) {
                MissingTransforms[i] = SafeT;
            }
            HISM->AddInstances(MissingTransforms, false);
            CurrentCount = NeededCount;
        }

        TArray<FTransform> BatchTransforms;
        BatchTransforms.SetNumUninitialized(CurrentCount);

        for (int32 i = 0; i < NeededCount; i++) {
            FTransform T = Transforms[i];

            if (T.GetScale3D().IsNearlyZero()) {
                T.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
            }
            if (T.ContainsNaN()) {
                T.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
                T.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
                T.SetRotation(FQuat::Identity);
            }
            BatchTransforms[i] = T;
        }

        FTransform HiddenTransform;
        HiddenTransform.SetLocation(FVector(0.0f, 0.0f, -50000.0f));
        HiddenTransform.SetScale3D(FVector(0.01f, 0.01f, 0.01f));
        HiddenTransform.SetRotation(FQuat::Identity);

        for (int32 i = NeededCount; i < CurrentCount; i++) {
            BatchTransforms[i] = HiddenTransform;
        }

        if (CurrentCount > 0) {
            HISM->BatchUpdateInstancesTransforms(0, BatchTransforms, true, true);
        }

        for (int32 i = 0; i < NeededCount; i++) {
            if (Colors.IsValidIndex(i)) {
                HISM->SetCustomDataValue(i, 0, Colors[i].R, false);
                HISM->SetCustomDataValue(i, 1, Colors[i].G, false);
                HISM->SetCustomDataValue(i, 2, Colors[i].B, false);
            }
        }

        HISM->MarkRenderStateDirty();
        };

    if (CaravanHISM) SyncHISM(CaravanHISM, CaravanTransforms, CaravanColors);
    if (ShipHISM) SyncHISM(ShipHISM, ShipTransforms, ShipColors);
    if (AirplaneHISM) SyncHISM(AirplaneHISM, AirTransforms, AirColors);
}