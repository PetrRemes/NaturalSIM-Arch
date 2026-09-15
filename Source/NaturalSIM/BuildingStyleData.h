#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BuildingStyleData.generated.h"

USTRUCT(BlueprintType)
struct FBuildingMeshSlot {
    GENERATED_BODY()

    // Zde vlozis svuj model (nyni Engine Cube, pozdeji z Blenderu)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Module")
    class UStaticMesh* Mesh = nullptr;

    // Posun pro doladeni Pivotu (aby stena sedela na okrajich)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
    FVector RelativeLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
    FRotator RelativeRotation = FRotator::ZeroRotator;

    // Meritko (z krychle udelame placku pro podlahu nebo stenu)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform")
    FVector RelativeScale = FVector(1.0f);
};

UCLASS(BlueprintType)
class NATURALSIM_API UBuildingStyleData : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style Definition")
    FString StyleName = "Default";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style Rules")
    bool bRequiresMasonry = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style Rules")
    bool bRequiresIndustrial = false;

    // Velikost jedne kosticky mrizky (300 jednotek = 3 metry)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions")
    float GridSize = 300.0f;

    // Vyska jednoho patra
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions")
    float WallHeight = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modules")
    TArray<FBuildingMeshSlot> Floors;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modules")
    TArray<FBuildingMeshSlot> Walls;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modules")
    TArray<FBuildingMeshSlot> WallsWithDoor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modules")
    TArray<FBuildingMeshSlot> WallsWithWindow;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modules")
    TArray<FBuildingMeshSlot> Roofs;
};