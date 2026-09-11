#include "DisasterSystem.h"
#include "SimWorldManager.h"
#include "SettlementSystem.h"
#include "HistorySystem.h"

UDisasterSystem::UDisasterSystem()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UDisasterSystem::BeginPlay()
{
    Super::BeginPlay();
}

void UDisasterSystem::ProcessDisasters(ASimWorldManager* Manager, float DeltaDays)
{
    if (!Manager || !Manager->SettlementModule) return;

    for (int32 i = ActiveWarnings.Num() - 1; i >= 0; i--) {
        ActiveWarnings[i].DaysToImpact -= DeltaDays;

        // FÁZE 2: Fyzické spuštìní katastrofy, jakmile vyprší èas
        if (ActiveWarnings[i].DaysToImpact <= 0.0f && ActiveWarnings[i].DaysToImpact > -DeltaDays) {

            if (ActiveWarnings[i].Type == EDisasterType::Earthquake) {
                float CellSize = 50.0f;
                int32 ChunkSize = Manager->ChunkSize;
                FIntPoint EpicenterChunk(
                    FMath::FloorToInt(ActiveWarnings[i].Epicenter.X / ((ChunkSize - 1) * CellSize)),
                    FMath::FloorToInt(ActiveWarnings[i].Epicenter.Y / ((ChunkSize - 1) * CellSize))
                );

                // Nyní se zemìtøesení fyzicky a deterministicky aplikuje na svìt
                Manager->TriggerEarthquake(EpicenterChunk, ActiveWarnings[i].Radius, ActiveWarnings[i].Severity);
            }
        }

        if (ActiveWarnings[i].DaysToImpact <= -5.0f) {
            ActiveWarnings.RemoveAtSwap(i);
        }
    }

    EvaluateFloodRisks(Manager);
    EvaluateVolcanicRisks(Manager);
}

void UDisasterSystem::EvaluateFloodRisks(ASimWorldManager* Manager)
{
    for (FSettlementData& City : Manager->SettlementModule->Settlements) {
        if (City.Population <= 0) continue;

        bool bHasWarning = false;
        for (const FDisasterWarning& W : ActiveWarnings) {
            if (W.Type == EDisasterType::Flood && W.AffectedSettlementIDs.Contains(City.SettlementID)) {
                bHasWarning = true;
                break;
            }
        }
        if (bHasWarning) continue;

        float TotalRain = 0.0f;
        float MaxSurfaceWater = 0.0f;

        for (FIntPoint Coord : City.ClaimedCells) {
            const FCellData* Cell = nullptr;
            if (Manager->GetCellGlobalPtr(Coord.X, Coord.Y, Cell)) {
                if (Cell->Elevation > Manager->SeaLevel) {
                    TotalRain += Cell->Rainfall;

                    if (Cell->SurfaceWater > Cell->ChannelDepth + Cell->BankHeight) {
                        float SpillOver = Cell->SurfaceWater - (Cell->ChannelDepth + Cell->BankHeight);
                        MaxSurfaceWater = FMath::Max(MaxSurfaceWater, SpillOver);
                    }
                }
            }
        }

        float AvgRain = City.ClaimedCells.Num() > 0 ? (TotalRain / City.ClaimedCells.Num()) : 0.0f;

        if (AvgRain > 2.5f && MaxSurfaceWater > 0.5f) {
            FDisasterWarning Warning;
            Warning.Type = EDisasterType::Flood;
            Warning.Epicenter = City.Position;
            Warning.Severity = AvgRain * MaxSurfaceWater;
            Warning.DaysToImpact = FMath::FRandRange(1.0f, 3.0f);
            Warning.Radius = 5000.0f;
            Warning.AffectedSettlementIDs.Add(City.SettlementID);

            ActiveWarnings.Add(Warning);

            if (Manager->HistoryModule) {
                FString Desc = FString::Printf(TEXT("Senzory a pozorovatele osady %d hlasi kriticke zvednuti hladiny rek. Hrozi masivni povodne!"), City.SettlementID);
                Manager->HistoryModule->LogEvent(Manager->CurrentYear, Manager->CurrentDay, TEXT("Flood Warning"), TEXT("Disaster"), Desc, City.Position, City.SettlementID);
            }
        }
    }
}

void UDisasterSystem::EvaluateVolcanicRisks(ASimWorldManager* Manager)
{
    for (const auto& Pair : Manager->WorldChunks) {
        const FChunkData& Chunk = Pair.Value;

        if (Chunk.BaseTectonicPressure < 0.1f) continue;

        for (int32 i = 0; i < Chunk.MicroCells.Num(); i++) {
            const FCellData& Cell = Chunk.MicroCells[i];

            if (Cell.bIsVolcano && Cell.EruptionDaysRemaining > 0.0f && Cell.EruptionDaysRemaining < 10.0f) {

                int32 GlobalX = (Pair.Key.X * (Manager->ChunkSize - 1)) + (i % Manager->ChunkSize);
                int32 GlobalY = (Pair.Key.Y * (Manager->ChunkSize - 1)) + (i / Manager->ChunkSize);
                FVector2D VolcPos(GlobalX * 50.0f, GlobalY * 50.0f);

                bool bExists = false;
                for (const FDisasterWarning& W : ActiveWarnings) {
                    if (W.Type == EDisasterType::VolcanicEruption && FVector2D::Distance(W.Epicenter, VolcPos) < 1000.0f) {
                        bExists = true;
                        break;
                    }
                }

                if (!bExists) {
                    FDisasterWarning Warning;
                    Warning.Type = EDisasterType::VolcanicEruption;
                    Warning.Epicenter = VolcPos;
                    Warning.Severity = 10.0f;
                    Warning.DaysToImpact = Cell.EruptionDaysRemaining;
                    Warning.Radius = 15000.0f;

                    for (const FSettlementData& City : Manager->SettlementModule->Settlements) {
                        if (FVector2D::Distance(City.Position, VolcPos) < Warning.Radius) {
                            Warning.AffectedSettlementIDs.Add(City.SettlementID);
                        }
                    }

                    ActiveWarnings.Add(Warning);

                    if (Manager->HistoryModule && Warning.AffectedSettlementIDs.Num() > 0) {
                        FString Desc = FString::Printf(TEXT("Zeme se trese a z nedaleke hory stoupa dym. Osady v okoli se pripravuji na moznou erupci za %.1f dni."), Warning.DaysToImpact);
                        Manager->HistoryModule->LogEvent(Manager->CurrentYear, Manager->CurrentDay, TEXT("Eruption Warning"), TEXT("Disaster"), Desc, VolcPos, -1);
                    }
                }
            }
        }
    }
}