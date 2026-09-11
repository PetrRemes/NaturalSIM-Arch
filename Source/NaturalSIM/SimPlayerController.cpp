#include "SimPlayerController.h"
#include "SimWorldManager.h"
#include "ManaSystem.h"
#include "TectonicSystem.h"
#include "HydroSystem.h"
#include "HumanSystem.h"
#include "SettlementSystem.h"
#include "DisasterSystem.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

ASimPlayerController::ASimPlayerController()
{
    bShowMouseCursor = true;
}

void ASimPlayerController::HandleMapClick()
{
    if (!IsLocalPlayerController() || !GetLocalPlayer()) return;

    if (!WorldManager) {
        WorldManager = Cast<ASimWorldManager>(UGameplayStatics::GetActorOfClass(GetWorld(), ASimWorldManager::StaticClass()));
        if (!WorldManager) {
            GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("CHYBA: WorldManager nenalezen na mape!"));
            return;
        }
    }

    FVector WorldLocation, WorldDirection;
    if (DeprojectMousePositionToWorld(WorldLocation, WorldDirection)) {

        // Softwarovy Raymarch (Matematicky prusecik paprsku s nasim voxelovym svetem)
        FVector RayPos = WorldLocation;
        FVector RayStep = WorldDirection * 100.0f; // Krok paprsku (100 jednotek)
        FVector HitLoc = FVector::ZeroVector;
        bool bHit = false;

        // Rychly posun paprsku bliz k zemi, pokud je kamera moc vysoko (optimalizace smycky)
        if (RayPos.Z > 15000.0f && WorldDirection.Z < 0.0f) {
            float DistToTop = (15000.0f - RayPos.Z) / WorldDirection.Z;
            RayPos += WorldDirection * DistToTop;
        }

        // Paprsek leti smerem dolu a hleda naraz do terenu
        for (int i = 0; i < 2000; i++) {
            RayPos += RayStep;

            // Pokud jsme propadli pod mapu (bezpecnostni pojistka)
            if (RayPos.Z < -2000.0f) {
                HitLoc = RayPos;
                bHit = true;
                break;
            }

            FCellData Cell;
            if (WorldManager->GetCellDataAtLocation(RayPos, Cell)) {
                // Pokud paprsek klesl pod nebo na uroven terenu bunky = ZASAH!
                if (RayPos.Z <= Cell.Elevation) {
                    HitLoc = RayPos;
                    bHit = true;
                    break;
                }
            }
            else if (RayPos.Z <= WorldManager->SeaLevel) {
                // Kliknuti mimo vygenerovane chunky (do oceanu)
                HitLoc = RayPos;
                bHit = true;
                break;
            }
        }

        if (bHit) {
            // Vykresleni zelene kulicky v miste kliknuti pro vizualni kontrolu
            DrawDebugSphere(GetWorld(), HitLoc, 80.0f, 12, FColor::Green, false, 2.0f);

            // ZAVOLAME SKRYTI VSECH OKEN PREDTIM, NEZ OTEVREME NOVE
            HideAllPanels();

            // 1. NEJVYSSI PRIORITA: Kontrola kliknuti na oko katastrofy
            if (WorldManager->DisasterModule) {
                for (const FDisasterWarning& Warning : WorldManager->DisasterModule->ActiveWarnings) {
                    // Detekce ve 2D rovine. Kdyz hrac klikne na oko na obloze, paprsek dopadne
                    // na zem presne pod okem. Zamerovaci radius 1500 jednotek (1.5 chunky) je velmi velkorysy.
                    if (FVector2D::Distance(FVector2D(HitLoc.X, HitLoc.Y), Warning.Epicenter) < 1500.0f) {
                        OnDisasterSelected(Warning);
                        return;
                    }
                }
            }

            FSettlementData ClickedSettlement;
            if (WorldManager->GetSettlementAtLocation(HitLoc, ClickedSettlement, 400.0f)) {
                OnSettlementSelected(ClickedSettlement);
                return;
            }

            FTribeData ClickedTribe;
            if (WorldManager->GetTribeAtLocation(HitLoc, ClickedTribe, 150.0f)) {
                OnTribeSelected(ClickedTribe);
                return;
            }

            FAnimalData ClickedAnimal;
            if (WorldManager->GetAnimalAtLocation(HitLoc, ClickedAnimal, 100.0f)) {
                OnAnimalSelected(ClickedAnimal);
                return;
            }

            FCellData ClickedCell;
            if (WorldManager->GetCellDataAtLocation(HitLoc, ClickedCell)) {
                OnCellSelected(ClickedCell);
                return;
            }
        }
    }
}

bool ASimPlayerController::DivineAction_BoostPillar(int32 EntityID, bool bIsSettlement, ECulturalPillar Pillar)
{
    if (!WorldManager || !WorldManager->ManaModule) return false;

    if (EntityID <= 0) {
        UE_LOG(LogTemp, Error, TEXT("POZOR: Pokus o zasah do EntityID = 0! Zkontroluj zapojeni ID v Blueprintu."));
        return false;
    }

    if (bIsSettlement) {
        if (WorldManager->SettlementModule) {
            return WorldManager->SettlementModule->TryBoostCulturalPillar(EntityID, Pillar, WorldManager);
        }
    }
    else {
        if (WorldManager->HumanModule) {
            return WorldManager->HumanModule->TryBoostCulturalPillar(EntityID, Pillar, WorldManager);
        }
    }
    return false;
}

bool ASimPlayerController::DivineAction_ForceExpansion(int32 EntityID, bool bIsSettlement, FVector2D TargetLocation)
{
    if (!WorldManager || !WorldManager->ManaModule) return false;

    if (EntityID <= 0) {
        UE_LOG(LogTemp, Error, TEXT("POZOR: Pokus o nucenou expanzi s EntityID = 0! Zkontroluj Blueprint."));
        return false;
    }

    if (bIsSettlement) {
        if (WorldManager->SettlementModule) {
            return WorldManager->SettlementModule->TryForceExpansion(EntityID, TargetLocation, WorldManager);
        }
    }
    else {
        if (WorldManager->HumanModule) {
            return WorldManager->HumanModule->TryForceMigration(EntityID, TargetLocation, WorldManager);
        }
    }
    return false;
}

bool ASimPlayerController::DivineAction_RaiseTerrain(FVector WorldLocation, float Amount)
{
    if (!WorldManager || !WorldManager->ManaModule || !WorldManager->TectonicModule) return false;

    int32 GlobalX = FMath::FloorToInt(WorldLocation.X / 50.0f);
    int32 GlobalY = FMath::FloorToInt(WorldLocation.Y / 50.0f);

    return WorldManager->TectonicModule->TryRaiseTerrain(WorldManager, GlobalX, GlobalY, Amount);
}

bool ASimPlayerController::DivineAction_CreateSpring(FVector WorldLocation)
{
    if (!WorldManager || !WorldManager->ManaModule || !WorldManager->HydroModule) return false;

    int32 GlobalX = FMath::FloorToInt(WorldLocation.X / 50.0f);
    int32 GlobalY = FMath::FloorToInt(WorldLocation.Y / 50.0f);

    return WorldManager->HydroModule->TryCreateSpring(WorldManager, GlobalX, GlobalY);
}

bool ASimPlayerController::DivineAction_SuppressDisaster(FIntPoint ChunkCoord)
{
    if (!WorldManager || !WorldManager->ManaModule || !WorldManager->TectonicModule) return false;

    return WorldManager->TectonicModule->SuppressDisaster(WorldManager, ChunkCoord);
}