#include "SimulationDirector.h"
#include "SimWorldManager.h"
#include "WorldRenderer.h"
#include "FaunaSystem.h"
#include "HumanSystem.h"
#include "SettlementSystem.h"
#include "FloraSystem.h"
#include "ClimateSystem.h"
#include "HeatmapSystem.h" 
#include "RelationSystem.h" 
#include "NationSystem.h"           
#include "InternalPoliticsSystem.h" 
#include "DiplomacySystem.h"
#include "TransportSystem.h" 
#include "HydroSystem.h" 
#include "TectonicSystem.h"
#include "CosmosSystem.h"
#include "DisasterSystem.h" 
#include "ManaSystem.h"
#include "TimerManager.h"
#include "Engine/World.h"

USimulationDirector::USimulationDirector() {
    PrimaryComponentTick.bCanEverTick = false;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void USimulationDirector::InitializeDirector(ASimWorldManager* InManager) {
    Manager = InManager;
    InitializeScheduler();
}

void USimulationDirector::StartSimulationTimer() {}
void USimulationDirector::StopSimulationTimer() {}
void USimulationDirector::SetTimeSpeed(float NewSecondsPerDay) {}

void USimulationDirector::ResetTime() {
    InitializeScheduler();
    TimeOfDayAccumulator = 0.0f;
    RenderCyclePhase = 0;
}

FString USimulationDirector::GetTelemetryString() const {
    FString Out = TEXT("\n\n --- SCHEDULER TELEMETRY ---\n");
    for (const FSystemSchedule& S : Scheduler) {
        FString TaskName = UEnum::GetValueAsString(S.TaskID);
        TaskName.Split(TEXT("::"), nullptr, &TaskName);
        FString ModeStr = (S.RegionMode == ERegionMode::Active) ? TEXT("[A]") : ((S.RegionMode == ERegionMode::Stable) ? TEXT("[S]") : TEXT("[*]"));
        Out += FString::Printf(TEXT("%s %s: Avg %.2f ms | Max %.2f ms | Rezy: %d\n"), *TaskName, *ModeStr, S.AverageExecutionMs, S.WorstExecutionMs, S.Slices);
    }
    return Out;
}

void USimulationDirector::InitializeScheduler() {
    Scheduler.Empty();

    Scheduler.Add({ ESimSystemTask::Fauna, ERegionMode::All, 0.0f, 12, 0.0f, 0, 0.f, 0.f, 0.f, 12 });
    Scheduler.Add({ ESimSystemTask::Humans, ERegionMode::All, 0.0f, 12, 0.0f, 0, 0.f, 0.f, 0.f, 12 });

    Scheduler.Add({ ESimSystemTask::HydroFlow, ERegionMode::Active, 1.0f, 16, 0.0f, 0, 0.f, 0.f, 0.f, 16 });
    Scheduler.Add({ ESimSystemTask::HydroFlow, ERegionMode::Stable, 5.0f, 8, 0.0f, 0, 0.f, 0.f, 0.f, 8 });

    Scheduler.Add({ ESimSystemTask::HydroGeomorphology, ERegionMode::Active, 15.0f, 32, 0.0f, 0, 0.f, 0.f, 0.f, 32 });
    Scheduler.Add({ ESimSystemTask::HydroGeomorphology, ERegionMode::Stable, 60.0f, 12, 0.0f, 0, 0.f, 0.f, 0.f, 12 });

    Scheduler.Add({ ESimSystemTask::Tectonics, ERegionMode::Active, 5.0f, 5, 0.0f, 0, 0.f, 0.f, 0.f, 5 });
    Scheduler.Add({ ESimSystemTask::Tectonics, ERegionMode::Stable, 30.0f, 15, 0.0f, 0, 0.f, 0.f, 0.f, 15 });

    Scheduler.Add({ ESimSystemTask::Climate, ERegionMode::Active, 1.0f, 4, 0.0f, 0, 0.f, 0.f, 0.f, 4 });
    Scheduler.Add({ ESimSystemTask::Climate, ERegionMode::Stable, 5.0f, 15, 0.0f, 0, 0.f, 0.f, 0.f, 15 });

    Scheduler.Add({ ESimSystemTask::Flora, ERegionMode::Active, 2.0f, 8, 0.0f, 0, 0.f, 0.f, 0.f, 8 });
    Scheduler.Add({ ESimSystemTask::Flora, ERegionMode::Stable, 15.0f, 30, 0.0f, 0, 0.f, 0.f, 0.f, 30 });

    Scheduler.Add({ ESimSystemTask::Settlements, ERegionMode::All, 1.0f, 1, 0.0f, 0, 0.f, 0.f, 0.f, 1 });
    Scheduler.Add({ ESimSystemTask::Transport, ERegionMode::All, 1.0f, 1, 0.0f, 0, 0.f, 0.f, 0.f, 1 });
    Scheduler.Add({ ESimSystemTask::Relations_Encounters, ERegionMode::All, 1.0f, 1, 0.0f, 0, 0.f, 0.f, 0.f, 1 });
    Scheduler.Add({ ESimSystemTask::Relations_Diplomacy, ERegionMode::All, 1.0f, 1, 0.0f, 0, 0.f, 0.f, 0.f, 1 });
    Scheduler.Add({ ESimSystemTask::Relations_Demographics, ERegionMode::All, 1.0f, 1, 0.0f, 0, 0.f, 0.f, 0.f, 1 });
    Scheduler.Add({ ESimSystemTask::Disasters, ERegionMode::All, 1.0f, 1, 0.0f, 0, 0.f, 0.f, 0.f, 1 });
    Scheduler.Add({ ESimSystemTask::Cosmos, ERegionMode::All, 365.0f, 1, 0.0f, 0, 0.f, 0.f, 0.f, 1 });

    float PhaseOffset = 0.0f;
    for (FSystemSchedule& Sched : Scheduler) {
        Sched.TimeAccumulator = PhaseOffset;
        PhaseOffset += 0.117f;
    }

    CurrentTaskIndex = 0;
}

void USimulationDirector::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!Manager || !Manager->bIsSimulationActive) return;

    float SafeDeltaTime = FMath::Min(DeltaTime, 0.05f);
    float SimDelta = SafeDeltaTime * (1.0f / FMath::Max(0.01f, Manager->RealSecondsPerDay)) * Manager->SimulationSpeedMultiplier;

    Manager->GlobalCloudTime += SimDelta;
    TimeOfDayAccumulator += SimDelta;

    while (TimeOfDayAccumulator >= 1.0f) {
        TimeOfDayAccumulator -= 1.0f;
        Manager->CurrentDay++;

        Manager->UpdateActiveRegions();

        if (Manager->ManaModule) Manager->ManaModule->ProcessDailyMana();

        if (Manager->CurrentDay > 365) {
            Manager->CurrentDay = 1;
            Manager->CurrentYear++;
            if (Manager->CosmosModule) Manager->CosmosModule->ProcessYearlyCosmos(Manager, 365.0f);
        }
    }

    float SeasonAlpha = (Manager->CurrentDay / 365.0f) * PI * 2.0f;
    FVector2D GlobalWind(FMath::Cos(SeasonAlpha), FMath::Sin(SeasonAlpha));
    Manager->GlobalCloudDrift += GlobalWind.GetSafeNormal() * 3.0f * Manager->CloudSpeedMultiplier * SimDelta;

    double StartRealTime = FPlatformTime::Seconds();
    int32 TasksProcessed = 0;

    for (int i = 0; i < Scheduler.Num(); i++) {
        FSystemSchedule& Sched = Scheduler[CurrentTaskIndex];
        Sched.TimeAccumulator += SimDelta;

        float DaysPerSlice = Sched.Slices > 1 ? (Sched.IntervalDays / Sched.Slices) : Sched.IntervalDays;

        if (Sched.IntervalDays <= 0.0f || Sched.TimeAccumulator >= DaysPerSlice) {
            float TaskSimDelta = Sched.IntervalDays <= 0.0f ? (SimDelta * Sched.Slices) : DaysPerSlice;

            double TaskStartMs = FPlatformTime::Seconds();
            ExecuteTaskSlice(Sched, TaskSimDelta);
            double TaskEndMs = FPlatformTime::Seconds();

            float ExecMs = (TaskEndMs - TaskStartMs) * 1000.0f;
            Sched.LastExecutionMs = ExecMs;
            Sched.WorstExecutionMs = FMath::Max(Sched.WorstExecutionMs, ExecMs);
            Sched.AverageExecutionMs = FMath::Lerp(Sched.AverageExecutionMs, ExecMs, 0.15f);

            bool bIsSliceable = (Sched.TaskID == ESimSystemTask::Fauna || Sched.TaskID == ESimSystemTask::Humans ||
                Sched.TaskID == ESimSystemTask::Climate || Sched.TaskID == ESimSystemTask::Tectonics || Sched.TaskID == ESimSystemTask::Flora ||
                Sched.TaskID == ESimSystemTask::HydroFlow || Sched.TaskID == ESimSystemTask::HydroGeomorphology);

            if (bIsSliceable) {
                if (ExecMs > TargetTaskBudgetMs) {
                    int32 ExtraSlicesNeeded = FMath::CeilToInt(ExecMs / TargetTaskBudgetMs);
                    Sched.TargetSlices = FMath::Min(200, Sched.Slices + ExtraSlicesNeeded);
                }
                else if (ExecMs < TargetTaskBudgetMs * 0.4f && Sched.Slices > 12) {
                    // OPRAVA AUTO-SCALERU: Nikdy nedovolíme klesnout pod 12 øezù.
                    // A pokud snižujeme, tak jen s 5% šancí, aby systém nebyl zaskoèen náhlým nárùstem zátìže
                    if (FMath::FRand() < 0.05f) {
                        Sched.TargetSlices = FMath::Max(12, Sched.Slices - 1);
                    }
                }
            }

            if (Sched.IntervalDays > 0.0f) Sched.TimeAccumulator -= DaysPerSlice;
            else Sched.TimeAccumulator = 0.0f;

            Sched.CurrentSlice++;

            if (Sched.CurrentSlice >= Sched.Slices) {
                Sched.CurrentSlice = 0;
                if (bIsSliceable && Sched.TargetSlices > 0 && Sched.TargetSlices != Sched.Slices) Sched.Slices = Sched.TargetSlices;
            }
        }

        CurrentTaskIndex = (CurrentTaskIndex + 1) % Scheduler.Num();
        TasksProcessed++;

        if ((FPlatformTime::Seconds() - StartRealTime) * 1000.0f > MaxSimulationBudgetMs && TasksProcessed >= 1) break;
    }

    EntityUpdateAccumulator += SafeDeltaTime;
    if (EntityUpdateAccumulator >= (1.0f / FMath::Max(1.0f, EntityUpdateRate))) {
        EntityUpdateAccumulator = 0.0f;

        if (Manager->RendererModule) {
            RenderCyclePhase = (RenderCyclePhase + 1) % 5;
            if (RenderCyclePhase == 0 && (Manager->bFaunaVisualDirty || Manager->bHumanVisualDirty)) Manager->RendererModule->UpdateFastEntities();
            else if (RenderCyclePhase == 1 && Manager->bSettlementVisualDirty) { Manager->RendererModule->UpdateSettlementEntities(); Manager->bSettlementVisualDirty = false; }
            else if (RenderCyclePhase == 2 && Manager->TransportModule) Manager->RendererModule->UpdateTransportEntities();
            else if (RenderCyclePhase == 3) Manager->RendererModule->UpdateWeatherEntities();
            else if (RenderCyclePhase == 4) Manager->RendererModule->UpdateDisasterEntities();
        }
    }
}

void USimulationDirector::ExecuteTaskSlice(FSystemSchedule& Schedule, float TaskSimDelta) {
    if (!Manager) return;

    const TArray<FIntPoint>* TargetChunks = &Manager->CachedChunkKeys;
    if (Schedule.RegionMode == ERegionMode::Active) TargetChunks = &Manager->ActiveChunkKeys;
    else if (Schedule.RegionMode == ERegionMode::Stable) TargetChunks = &Manager->StableChunkKeys;

    switch (Schedule.TaskID) {

    case ESimSystemTask::Fauna:
        if (Manager->FaunaModule) {
            int32 Total = Manager->FaunaModule->Animals.Num();
            if (Total > 0) {
                if (Schedule.CurrentSlice == 0) Manager->FaunaModule->UpdateSpatialGrid(Manager);
                int32 Batch = FMath::Max(1, FMath::CeilToInt((float)Total / Schedule.Slices));
                int32 Start = FMath::Min(Schedule.CurrentSlice * Batch, Total);
                int32 End = FMath::Min(Start + Batch, Total);
                if (Start < End) Manager->FaunaModule->ProcessFaunaSlice(Manager->WorldChunks, Manager, TaskSimDelta, Start, End);
            }
        }
        break;

    case ESimSystemTask::Humans:
        if (Manager->HumanModule) {
            int32 Total = Manager->HumanModule->Tribes.Num();
            if (Total > 0) {
                int32 Batch = FMath::Max(1, FMath::CeilToInt((float)Total / Schedule.Slices));
                int32 Start = FMath::Min(Schedule.CurrentSlice * Batch, Total);
                int32 End = FMath::Min(Start + Batch, Total);
                if (Start < End) Manager->HumanModule->ProcessHumansSlice(Manager->WorldChunks, Manager, TaskSimDelta, Start, End);
            }
        }
        break;

    case ESimSystemTask::Settlements:
        if (Manager->SettlementModule) {
            Manager->SettlementModule->ProcessSettlements(Manager->WorldChunks, Manager, TaskSimDelta);
            Manager->bSettlementVisualDirty = true;
        }
        break;

    case ESimSystemTask::Transport:
        if (Manager->TransportModule && Manager->SettlementModule) Manager->TransportModule->ProcessTransport(Manager->SettlementModule->Settlements, TaskSimDelta, Manager);
        break;

    case ESimSystemTask::Relations_Encounters:
        if (Manager->HumanModule && Manager->SettlementModule && Manager->RelationModule) {
            Manager->RelationModule->ProcessTribeInteractions(Manager->HumanModule->Tribes, Manager);
            Manager->RelationModule->ProcessSettlementInteractions(Manager->SettlementModule->Settlements, Manager);
        }
        break;

    case ESimSystemTask::Relations_Diplomacy:
        if (Manager->NationModule && Manager->SettlementModule) {
            Manager->NationModule->AggregateNationData(Manager->SettlementModule->Settlements);
            if (Manager->InternalPoliticsModule) Manager->InternalPoliticsModule->ProcessInternalRelations(Manager->SettlementModule->Settlements, Manager);
            if (Manager->DiplomacyModule) Manager->DiplomacyModule->ProcessDiplomacy(Manager);
        }
        break;

    case ESimSystemTask::Relations_Demographics:
        if (Manager->SettlementModule && Manager->HumanModule) {
            Manager->SettlementModule->ProcessDailyDemographics(Manager);
            Manager->HumanModule->ProcessDailyDemographics(Manager);
        }
        break;

    case ESimSystemTask::HydroFlow:
        if (Manager->HydroModule && TargetChunks->Num() > 0) {
            int32 Total = TargetChunks->Num();
            int32 Batch = FMath::Max(1, FMath::CeilToInt((float)Total / Schedule.Slices));
            int32 Start = FMath::Min(Schedule.CurrentSlice * Batch, Total);
            int32 End = FMath::Min(Start + Batch, Total);
            if (Start < End) Manager->HydroModule->ProcessHydroSlice(*TargetChunks, Manager->WorldChunks, Manager, Start, End, false, 0.0f);
        }
        break;

    case ESimSystemTask::HydroGeomorphology:
        if (Manager->HydroModule && TargetChunks->Num() > 0) {
            int32 Total = TargetChunks->Num();
            int32 Batch = FMath::Max(1, FMath::CeilToInt((float)Total / Schedule.Slices));
            int32 Start = FMath::Min(Schedule.CurrentSlice * Batch, Total);
            int32 End = FMath::Min(Start + Batch, Total);
            if (Start < End) Manager->HydroModule->ProcessHydroSlice(*TargetChunks, Manager->WorldChunks, Manager, Start, End, true, TaskSimDelta);
        }
        break;

    case ESimSystemTask::Flora:
        if (Manager->FloraModule && TargetChunks->Num() > 0) {
            int32 Total = TargetChunks->Num();
            int32 Batch = FMath::Max(1, FMath::CeilToInt((float)Total / Schedule.Slices));
            int32 Start = FMath::Min(Schedule.CurrentSlice * Batch, Total);
            int32 End = FMath::Min(Start + Batch, Total);

            if (Start < End) Manager->FloraModule->ProcessDailyGrowth(*TargetChunks, Manager->WorldChunks, Manager, Start, End, TaskSimDelta);
        }
        break;

    case ESimSystemTask::Climate:
        if (Manager->ClimateModule && TargetChunks->Num() > 0) {
            int32 Total = TargetChunks->Num();
            int32 Batch = FMath::Max(1, FMath::CeilToInt((float)Total / Schedule.Slices));
            int32 Start = FMath::Min(Schedule.CurrentSlice * Batch, Total);
            int32 End = FMath::Min(Start + Batch, Total);

            if (Start < End) Manager->ClimateModule->UpdateDailyClimate(*TargetChunks, Manager->WorldChunks, Manager, Start, End);
            if (Schedule.CurrentSlice == Schedule.Slices - 1 && Manager->HeatmapModule) Manager->HeatmapModule->UpdateClimateHeatmaps(Manager->WorldChunks);
        }
        break;

    case ESimSystemTask::Tectonics:
        if (Manager->TectonicModule && TargetChunks->Num() > 0) {
            int32 Total = TargetChunks->Num();
            int32 Batch = FMath::Max(1, FMath::CeilToInt((float)Total / Schedule.Slices));
            int32 Start = FMath::Min(Schedule.CurrentSlice * Batch, Total);
            int32 End = FMath::Min(Start + Batch, Total);

            if (Start < End) {
                TArray<FIntPoint> SlicedKeys;
                for (int32 i = Start; i < End; i++) SlicedKeys.Add((*TargetChunks)[i]);
                Manager->TectonicModule->ProcessDailyTectonics(SlicedKeys, Manager->WorldChunks, Manager, TaskSimDelta);
            }
        }
        break;

    case ESimSystemTask::Disasters:
        if (Manager->DisasterModule) Manager->DisasterModule->ProcessDisasters(Manager, TaskSimDelta);
        break;

    case ESimSystemTask::Cosmos:
        if (Manager->CosmosModule) {
            float ActualDelta = Schedule.IntervalDays > 0.0f ? Schedule.IntervalDays : TaskSimDelta;
            Manager->CosmosModule->ProcessYearlyCosmos(Manager, ActualDelta);
        }
        break;
    }
}