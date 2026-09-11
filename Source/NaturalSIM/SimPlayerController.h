#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SimWorldTypes.h"
#include "DisasterSystem.h"
#include "SimPlayerController.generated.h"

class ASimWorldManager;

UCLASS()
class NATURALSIM_API ASimPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    ASimPlayerController();

    UPROPERTY(BlueprintReadOnly, Category = "Simulation")
    ASimWorldManager* WorldManager;

    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void HandleMapClick();

    UFUNCTION(BlueprintImplementableEvent, Category = "Interaction")
    void OnCellSelected(FCellData CellData);

    UFUNCTION(BlueprintImplementableEvent, Category = "Interaction")
    void OnTribeSelected(FTribeData TribeData);

    UFUNCTION(BlueprintImplementableEvent, Category = "Interaction")
    void OnSettlementSelected(FSettlementData SettlementData);

    UFUNCTION(BlueprintImplementableEvent, Category = "Interaction")
    void OnAnimalSelected(FAnimalData AnimalData);

    // NOVE: Event pro zachyceni kliknuti na marker hrozici katastrofy
    UFUNCTION(BlueprintImplementableEvent, Category = "Interaction")
    void OnDisasterSelected(const FDisasterWarning& DisasterData);

    UFUNCTION(BlueprintImplementableEvent, Category = "Interaction")
    void HideAllPanels();

    // =========================================================================
    // BOZSKE ZASAHY (UI WRAPPERY)
    // =========================================================================

    UFUNCTION(BlueprintCallable, Category = "Divine Intervention")
    bool DivineAction_BoostPillar(int32 EntityID, bool bIsSettlement, ECulturalPillar Pillar);

    UFUNCTION(BlueprintCallable, Category = "Divine Intervention")
    bool DivineAction_ForceExpansion(int32 EntityID, bool bIsSettlement, FVector2D TargetLocation);

    UFUNCTION(BlueprintCallable, Category = "Divine Intervention")
    bool DivineAction_RaiseTerrain(FVector WorldLocation, float Amount);

    UFUNCTION(BlueprintCallable, Category = "Divine Intervention")
    bool DivineAction_CreateSpring(FVector WorldLocation);

    UFUNCTION(BlueprintCallable, Category = "Divine Intervention")
    bool DivineAction_SuppressDisaster(FIntPoint ChunkCoord);
};