#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimWorldTypes.h"
#include "SimulationDirector.generated.h"

class ASimWorldManager;

UENUM()
enum class ERegionMode : uint8 { All, Active, Stable };

UENUM()
enum class ESimSystemTask : uint8 {
    Fauna, Humans, Settlements, Transport, Relations_Encounters,
    Relations_Diplomacy, Relations_Demographics, HydroFlow,
    HydroGeomorphology, Climate, Flora, Tectonics, Cosmos, Disasters
};

USTRUCT()
struct FSystemSchedule {
    GENERATED_BODY()

    ESimSystemTask TaskID;
    ERegionMode RegionMode = ERegionMode::All;
    float IntervalDays;
    int32 Slices;
    float TimeAccumulator;
    int32 CurrentSlice;

    float LastExecutionMs = 0.0f;
    float AverageExecutionMs = 0.0f;
    float WorstExecutionMs = 0.0f;
    int32 TargetSlices = 0;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API USimulationDirector : public UActorComponent
{
    GENERATED_BODY()

public:
    USimulationDirector();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void InitializeDirector(ASimWorldManager* InManager);
    void StartSimulationTimer();
    void StopSimulationTimer();
    void SetTimeSpeed(float NewSecondsPerDay);
    void ResetTime();

    UPROPERTY(EditAnywhere, Category = "Performance") float EntityUpdateRate = 30.0f;
    UPROPERTY(EditAnywhere, Category = "Performance") float MaxSimulationBudgetMs = 8.0f;
    UPROPERTY(EditAnywhere, Category = "Performance") float TargetTaskBudgetMs = 1.0f;

    UFUNCTION(BlueprintPure, Category = "Telemetry")
    FString GetTelemetryString() const;

private:
    ASimWorldManager* Manager;
    TArray<FSystemSchedule> Scheduler;
    int32 CurrentTaskIndex = 0;

    float EntityUpdateAccumulator = 0.0f;
    float TimeOfDayAccumulator = 0.0f;
    int32 RenderCyclePhase = 0;

    void InitializeScheduler();
    void ExecuteTaskSlice(FSystemSchedule& Schedule, float TaskSimDelta);
};