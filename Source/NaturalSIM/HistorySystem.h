#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HistoryTypes.h"
#include "HistorySystem.generated.h"

class ASimWorldManager;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class NATURALSIM_API UHistorySystem : public UActorComponent
{
    GENERATED_BODY()

public:
    UHistorySystem();

    UPROPERTY(BlueprintReadOnly, Category = "History")
    TArray<FHistoricalEvent> Chronicle;

    // OPTIMALIZACE: Maximální velikost kroniky pro ochranu pamìti RAM a UI
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "History")
    int32 MaxChronicleSize = 500;

    void LogEvent(int32 Year, double Day, FString Type, FString Title, FString Desc, FVector2D Loc, int32 EntityID);

private:
    FCriticalSection ChronicleMutex;
};