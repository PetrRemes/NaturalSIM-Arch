#include "CosmosSystem.h"
#include "SimWorldManager.h"

UCosmosSystem::UCosmosSystem()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UCosmosSystem::BeginPlay()
{
    Super::BeginPlay();
}

void UCosmosSystem::InitCosmos(ASimWorldManager* Manager)
{
    if (!Manager) return;
    OrbitalBodies.Empty();

    FRandomStream Stream(Manager->MapSeed + 420);

    for (int i = 0; i < Manager->NumMoons; i++) {
        FOrbitalBody Moon;
        Moon.Mass = Stream.FRandRange(0.5f, 2.0f);
        Moon.Distance = Stream.FRandRange(10.0f, 30.0f);
        Moon.OrbitalPeriod = Stream.FRandRange(27.0f, 100.0f);
        Moon.CurrentAngle = Stream.FRandRange(0.0f, PI * 2.0f);
        Moon.bIsMoon = true;
        OrbitalBodies.Add(Moon);
    }

    for (int i = 0; i < Manager->NumPlanets; i++) {
        FOrbitalBody Planet;
        Planet.Mass = Stream.FRandRange(10.0f, 300.0f);
        Planet.Distance = Stream.FRandRange(100.0f, 1000.0f);
        Planet.OrbitalPeriod = Stream.FRandRange(300.0f, 10000.0f);
        Planet.CurrentAngle = Stream.FRandRange(0.0f, PI * 2.0f);
        Planet.bIsMoon = false;
        OrbitalBodies.Add(Planet);
    }

    CurrentState = FCosmosState();
}

void UCosmosSystem::ProcessYearlyCosmos(ASimWorldManager* Manager, float DeltaDays)
{
    if (!Manager) return;

    FVector2D TidalVector(0.0f, 0.0f);

    for (FOrbitalBody& Body : OrbitalBodies) {
        float AngleStep = (DeltaDays / Body.OrbitalPeriod) * PI * 2.0f;
        Body.CurrentAngle += AngleStep;
        if (Body.CurrentAngle > PI * 2.0f) Body.CurrentAngle -= PI * 2.0f;

        float GravityPull = Body.Mass / (Body.Distance * Body.Distance);
        TidalVector += FVector2D(FMath::Cos(Body.CurrentAngle), FMath::Sin(Body.CurrentAngle)) * GravityPull;
    }

    float AlignmentFactor = TidalVector.Size();
    CurrentState.GlobalTidalMultiplier = 1.0f + (AlignmentFactor * 0.4f);

    float Year = Manager->CurrentYear;

    float EccentricityCycle = FMath::Sin((Year / 1200.0f) * PI * 2.0f);
    float ObliquityCycle = FMath::Sin((Year / 450.0f) * PI * 2.0f);

    CurrentState.GlobalIrradianceOffset = EccentricityCycle * 2.5f;
    CurrentState.SeasonalityMultiplier = 1.0f + (ObliquityCycle * 0.2f);

    if (Manager->StarType == EStarType::RedDwarf) {
        if (FMath::FRand() < 0.05f) {
            CurrentState.GlobalRadiation = 2.0f;
            CurrentState.GlobalIrradianceOffset += 3.0f;
        }
        else {
            CurrentState.GlobalRadiation = FMath::Lerp(CurrentState.GlobalRadiation, 0.0f, 0.1f);
            CurrentState.GlobalIrradianceOffset -= 1.0f;
        }
    }
    else if (Manager->StarType == EStarType::BlueGiant) {
        CurrentState.GlobalIrradianceOffset += 3.0f;
        CurrentState.GlobalRadiation = 0.5f;
    }
    else {
        CurrentState.GlobalRadiation = 0.0f;
    }
}