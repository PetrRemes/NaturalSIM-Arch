#include "HistorySystem.h"

UHistorySystem::UHistorySystem()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UHistorySystem::LogEvent(int32 Year, double Day, FString Type, FString Title, FString Desc, FVector2D Loc, int32 EntityID)
{
    FScopeLock Lock(&ChronicleMutex);

    FHistoricalEvent Ev;
    Ev.Year = Year;
    Ev.Day = Day;
    Ev.EventType = Type;
    Ev.Title = Title;
    Ev.Description = Desc;
    Ev.Location = Loc;
    Ev.EntityID = EntityID;

    Chronicle.Add(Ev);

    // OPTIMALIZACE: Plovoucí okno (Rolling Buffer). Pokud je záznamù moc, nejstarší se smaže.
    if (Chronicle.Num() > MaxChronicleSize) {
        Chronicle.RemoveAt(0, 1, EAllowShrinking::No);
    }
}