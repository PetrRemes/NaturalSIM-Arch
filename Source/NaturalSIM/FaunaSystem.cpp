#include "FaunaSystem.h"
#include "SimWorldManager.h"
#include "ManaSystem.h"
#include "Async/ParallelFor.h"

UFaunaSystem::UFaunaSystem() { PrimaryComponentTick.bCanEverTick = false; }
void UFaunaSystem::BeginPlay() { Super::BeginPlay(); }

void UFaunaSystem::InitializeFauna(ASimWorldManager* Manager)
{
    if (!Manager) return;
    Animals.Empty();

    TArray<FVector2D> ValidSpawnPoints;
    float CellSize = 50.0f; int32 ChunkSize = Manager->ChunkSize;

    for (const auto& Pair : Manager->WorldChunks) {
        const FChunkData& Chunk = Pair.Value;
        FIntPoint ChunkCoord = Pair.Key;

        for (int32 i = 0; i < Chunk.MicroCells.Num(); i++) {
            const FCellData& Cell = Chunk.MicroCells[i];

            if (Cell.Elevation > Manager->SeaLevel && Cell.Elevation < Manager->SeaLevel + 500.0f && Cell.SurfaceWater < 0.1f && !Cell.bIsVolcano) {
                if (Cell.FloraDensity > 0.2f || Cell.Biome == EBiomeType::Grassland || Cell.Biome == EBiomeType::DeciduousForest) {
                    int32 X = i % ChunkSize; int32 Y = i / ChunkSize;
                    float WorldX = (ChunkCoord.X * (ChunkSize - 1) * CellSize) + (X * CellSize);
                    float WorldY = (ChunkCoord.Y * (ChunkSize - 1) * CellSize) + (Y * CellSize);
                    ValidSpawnPoints.Add(FVector2D(WorldX, WorldY));
                }
            }
        }
    }

    if (ValidSpawnPoints.Num() == 0) return;

    FRandomStream Stream(Manager->MapSeed + 888);
    int32 NumToSpawn = FMath::Min(Manager->MaxAnimals, ValidSpawnPoints.Num() / 10);

    for (int32 i = 0; i < NumToSpawn; i++) {
        FAnimalData Animal;
        int32 RndIdx = Stream.RandRange(0, ValidSpawnPoints.Num() - 1);
        Animal.Position = ValidSpawnPoints[RndIdx];

        float Angle = Stream.FRandRange(0.0f, PI * 2.0f);
        Animal.TargetDirection = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
        Animal.StrategicTarget = Animal.Position + Animal.TargetDirection * Stream.FRandRange(1000.0f, 3000.0f);

        Animal.StrategicTargetScore = 50.0f;
        Animal.NextStrategicDecisionDay = 0.0;
        Animal.LastVisitedRegion = Animal.Position;
        Animal.NextAllowedReturnDay = 0.0;
        Animal.Hunger = 0.0f;
        Animal.Condition = 1.0f;
        Animal.Age = Stream.FRandRange(0.0f, 5.0f);

        float RndType = Stream.FRand();
        if (RndType < 0.6f) {
            Animal.Type = EAnimalType::Herbivore; Animal.HerdSize = Stream.FRandRange(10.0f, 30.0f);
        }
        else if (RndType < 0.85f) {
            Animal.Type = EAnimalType::ForestAnimal; Animal.HerdSize = Stream.FRandRange(5.0f, 15.0f);
        }
        else {
            Animal.Type = EAnimalType::Predator; Animal.HerdSize = Stream.FRandRange(2.0f, 6.0f);
        }

        Animals.Add(Animal);
    }
    UpdateSpatialGrid(Manager);
}

void UFaunaSystem::UpdateSpatialGrid(ASimWorldManager* Manager)
{
    if (!Manager) return;
    AnimalSpatialGrid.Reset();

    float InvGridSize = 1.0f / (Manager->ChunkSize * 50.0f);
    for (int32 i = 0; i < Animals.Num(); i++) {
        if (Animals[i].HerdSize > 0.0f) {
            int32 CX = FMath::FloorToInt(Animals[i].Position.X * InvGridSize);
            int32 CY = FMath::FloorToInt(Animals[i].Position.Y * InvGridSize);
            AnimalSpatialGrid.FindOrAdd(FIntPoint(CX, CY)).Add(i);
        }
    }
}

float UFaunaSystem::EvaluateHabitat(ASimWorldManager* Manager, FVector2D Position, const FAnimalData& Animal)
{
    float Score = 0.0f;
    int32 GlobalX = FMath::FloorToInt(Position.X / 50.0f);
    int32 GlobalY = FMath::FloorToInt(Position.Y / 50.0f);

    const FCellData* CellPtr = nullptr;
    if (Manager->GetCellGlobalPtr(GlobalX, GlobalY, CellPtr)) {

        if (CellPtr->Elevation <= Manager->SeaLevel) return -9999.0f;
        if (CellPtr->SurfaceWater > 1.0f) return -5000.0f;
        if (CellPtr->Elevation > Manager->SeaLevel + 600.0f) return -5000.0f;

        if (CellPtr->DangerLevel > 0.1f || CellPtr->bIsVolcano) Score -= 2000.0f;
        if (CellPtr->HouseDensity > 0.0f) Score -= 1000.0f;

        if (CellPtr->SurfaceWater > 0.05f && CellPtr->SurfaceWater <= 0.8f) Score += 100.0f;
        if (CellPtr->RiverDischarge > 0.5f) Score += 150.0f;

        if (Animal.Type == EAnimalType::Herbivore) {
            Score += CellPtr->FloraDensity * 200.0f;
            if (CellPtr->Biome == EBiomeType::Grassland) Score += 50.0f;
            if (CellPtr->TreeType != ETreeType::None) Score -= 30.0f;
        }
        else if (Animal.Type == EAnimalType::ForestAnimal) {
            Score += CellPtr->WoodAmount * 0.5f;
            Score += CellPtr->BerryBushes * 10.0f;
            if (CellPtr->TreeType != ETreeType::None) Score += 100.0f;
        }
    }
    return Score;
}

void UFaunaSystem::ProcessFaunaSlice(TMap<FIntPoint, FChunkData>& WorldChunks, ASimWorldManager* Manager, float DeltaTime, int32 StartIdx, int32 EndIdx)
{
    if (!Manager) return;
    float CellSize = 50.0f;

    TArray<FAnimalData> SplinterHerds;

    for (int32 i = StartIdx; i < EndIdx; i++) {
        FAnimalData& Animal = Animals[i];
        if (Animal.HerdSize <= 0.0f) continue;

        int32 CurrGX = FMath::FloorToInt(Animal.Position.X / CellSize);
        int32 CurrGY = FMath::FloorToInt(Animal.Position.Y / CellSize);

        Animal.Hunger += 5.0f * DeltaTime;

        if (Animal.Hunger > 60.0f) {
            Animal.Condition -= 0.05f * DeltaTime;
            Animal.Condition = FMath::Max(0.0f, Animal.Condition);
        }
        else if (Animal.Hunger < 20.0f) {
            Animal.Condition += 0.1f * DeltaTime;
            Animal.Condition = FMath::Min(1.0f, Animal.Condition);
        }

        if (Manager->CurrentDay >= Animal.NextStrategicDecisionDay || Animal.Hunger > 50.0f || FVector2D::DistSquared(Animal.Position, Animal.StrategicTarget) < 10000.0f) {

            FVector2D BestTarget = Animal.Position;
            float BestScore = -99999.0f;

            for (int k = 0; k < 12; k++) {
                float Angle = FMath::FRandRange(0.0f, PI * 2.0f);
                float Dist = FMath::FRandRange(800.0f, 2500.0f);
                FVector2D SamplePos = Animal.Position + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Dist;

                float Score = EvaluateHabitat(Manager, SamplePos, Animal);

                if (Manager->CurrentDay < Animal.NextAllowedReturnDay && FVector2D::DistSquared(SamplePos, Animal.LastVisitedRegion) < 1000000.0f) {
                    Score -= 500.0f;
                }

                // OPRAVA PATHTRACINGU: Line-Of-Sight Raycast vùèi vodním plochám
                bool bPathClear = true;
                FVector2D RayStep = (SamplePos - Animal.Position).GetSafeNormal() * CellSize;
                int32 Steps = FMath::FloorToInt(Dist / CellSize);
                FVector2D RayPos = Animal.Position;

                for (int step = 0; step < Steps; step++) {
                    RayPos += RayStep;
                    int32 rx = FMath::FloorToInt(RayPos.X / CellSize);
                    int32 ry = FMath::FloorToInt(RayPos.Y / CellSize);
                    const FCellData* rCell = nullptr;
                    if (Manager->GetCellGlobalPtr(rx, ry, rCell)) {
                        // Pokud je v cestì hlubší øeka nebo moøe, cesta je zablokovaná
                        if (rCell->Elevation <= Manager->SeaLevel || rCell->SurfaceWater > 0.8f) {
                            bPathClear = false;
                            break;
                        }
                    }
                }

                // Brutální penalizace, pokud zvíøe vidí v cestì vodu (nepùjde tam)
                if (!bPathClear) Score -= 15000.0f;

                if (Score > BestScore) {
                    BestScore = Score;
                    BestTarget = SamplePos;
                }
            }

            Animal.LastVisitedRegion = Animal.Position;
            Animal.NextAllowedReturnDay = Manager->CurrentDay + 15.0;

            Animal.StrategicTarget = BestTarget;
            Animal.StrategicTargetScore = BestScore;
            Animal.NextStrategicDecisionDay = Manager->CurrentDay + FMath::FRandRange(2.0f, 7.0f);
        }

        FVector2D MacroDir = (Animal.StrategicTarget - Animal.Position).GetSafeNormal();
        if (MacroDir.IsNearlyZero()) MacroDir = FVector2D(1.0f, 0.0f);

        FVector2D LocalBestDir = MacroDir;
        float LocalBestScore = -99999.0f;

        for (int k = -2; k <= 2; k++) {
            float RayAngle = FMath::Atan2(MacroDir.Y, MacroDir.X) + (k * PI / 4.0f);
            FVector2D RayDir(FMath::Cos(RayAngle), FMath::Sin(RayAngle));
            FVector2D CheckPos = Animal.Position + RayDir * 150.0f;

            float Score = EvaluateHabitat(Manager, CheckPos, Animal);
            Score += (3 - FMath::Abs(k)) * 20.0f;

            if (Score > LocalBestScore) {
                LocalBestScore = Score;
                LocalBestDir = RayDir;
            }
        }

        FVector2D Repulsion = FVector2D::ZeroVector;
        for (int32 j = StartIdx; j < EndIdx; j++) {
            if (i == j || Animals[j].HerdSize <= 0.0f) continue;
            float DistSq = FVector2D::DistSquared(Animal.Position, Animals[j].Position);
            if (DistSq < 25000.0f && DistSq > 0.1f) {
                Repulsion += (Animal.Position - Animals[j].Position).GetSafeNormal() * (25000.0f - DistSq) * 0.00005f;
            }
        }

        LocalBestDir = (LocalBestDir + Repulsion).GetSafeNormal();

        Animal.TargetDirection = FMath::Lerp(Animal.TargetDirection, LocalBestDir, 0.15f).GetSafeNormal();
        if (Animal.TargetDirection.IsNearlyZero()) Animal.TargetDirection = FVector2D(1.0f, 0.0f);

        float MovementSpeed = 10.0f;
        if (Animal.Hunger > 40.0f) MovementSpeed = 30.0f;

        if (Animal.Condition < 0.6f && Animal.Condition > 0.15f) {
            MovementSpeed = 60.0f;
        }
        else if (Animal.Condition <= 0.15f) {
            MovementSpeed = 10.0f;
        }

        if (Animal.Hunger <= 30.0f) {
            float SwayAngle = FMath::PerlinNoise2D(FVector2D(Animal.Position.X * 0.01f, Manager->CurrentDay * 0.1f)) * PI;
            FVector2D SwayDir(FMath::Cos(SwayAngle), FMath::Sin(SwayAngle));
            Animal.TargetDirection = FMath::Lerp(Animal.TargetDirection, SwayDir, 0.4f).GetSafeNormal();
        }

        FVector2D NextPos = Animal.Position + Animal.TargetDirection * MovementSpeed * DeltaTime;

        int32 NextGX = FMath::FloorToInt(NextPos.X / CellSize);
        int32 NextGY = FMath::FloorToInt(NextPos.Y / CellSize);
        const FCellData* NextCell = nullptr;
        if (Manager->GetCellGlobalPtr(NextGX, NextGY, NextCell)) {
            if (NextCell->Elevation <= Manager->SeaLevel || NextCell->SurfaceWater > 0.8f) {
                // OPRAVA PATHTRACINGU: Pokud zvíøe nechtìnì vbìhne do vody, odrazí se
                // a hlavnì okamžitì zahodí svùj cíl, aby nevibrovalo a ihned našlo nový!
                Animal.TargetDirection = -Animal.TargetDirection;
                NextPos = Animal.Position + Animal.TargetDirection * MovementSpeed * DeltaTime;

                Animal.NextStrategicDecisionDay = 0.0;
                Animal.StrategicTargetScore = -99999.0f;
            }
        }

        Animal.Position = NextPos;

        FCellData* CCell = nullptr; FIntPoint CCoord;
        if (Manager->GetMutableCellGlobal(CurrGX, CurrGY, CCell, CCoord)) {

            float EatAmount = Animal.HerdSize * 0.1f * DeltaTime;

            if (Animal.Type == EAnimalType::Herbivore || Animal.Type == EAnimalType::ForestAnimal) {
                if (CCell->FloraDensity > 0.05f) {
                    CCell->FloraDensity = FMath::Max(0.0f, CCell->FloraDensity - EatAmount * 0.2f);
                    Animal.Hunger = FMath::Max(0.0f, Animal.Hunger - EatAmount * 15.0f);
                    CCell->GrazingPressure = FMath::Min(100.0f, CCell->GrazingPressure + EatAmount * 10.0f);

                    Manager->RegisterVisualChange(CCoord, EChunkVisualDirty::Flora | EChunkVisualDirty::Terrain);
                }
                else if (CCell->BerryBushes > 0.0f) {
                    CCell->BerryBushes = FMath::Max(0.0f, CCell->BerryBushes - EatAmount);
                    Animal.Hunger = FMath::Max(0.0f, Animal.Hunger - EatAmount * 25.0f);
                    Manager->RegisterVisualChange(CCoord, EChunkVisualDirty::Flora);
                }
            }

            if (Animal.Type == EAnimalType::Predator) {
                if (CCell->AnimalBones > 0.0f) {
                    CCell->AnimalBones = FMath::Max(0.0f, CCell->AnimalBones - EatAmount);
                    Animal.Hunger = FMath::Max(0.0f, Animal.Hunger - EatAmount * 20.0f);
                    Manager->RegisterVisualChange(CCoord, EChunkVisualDirty::Terrain);
                }
            }
        }

        if (Animal.Condition <= 0.0f) {
            float StarveAmount = FMath::Min(Animal.HerdSize, 3.0f * DeltaTime);
            Animal.HerdSize -= StarveAmount;

            if (CCell) {
                CCell->AnimalBones += StarveAmount * 2.0f;
                Manager->RegisterVisualChange(CCoord, EChunkVisualDirty::Terrain);
            }
        }

        Animal.Age += DeltaTime / 365.0f;
        if (Animal.Age > 12.0f && FMath::FRand() < 0.05f * DeltaTime) {
            Animal.HerdSize -= 1.0f;
        }

        if (Animal.Condition > 0.8f && Animal.Hunger < 20.0f && Animal.HerdSize > 2.0f && Animal.Age > 2.0f) {
            float ReproRate = (Animal.Type == EAnimalType::Predator) ? 0.02f : 0.08f;
            Animal.HerdSize += ReproRate * DeltaTime;
        }

        if (Animal.HerdSize > 50.0f) {
            if ((Animals.Num() + SplinterHerds.Num()) < Manager->MaxAnimals) {
                FAnimalData SplitHerd = Animal;
                SplitHerd.HerdSize = Animal.HerdSize * 0.5f;
                Animal.HerdSize -= SplitHerd.HerdSize;
                SplitHerd.Position += FVector2D(FMath::RandRange(-200.f, 200.f), FMath::RandRange(-200.f, 200.f));
                SplitHerd.StrategicTarget = SplitHerd.Position + FVector2D(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)).GetSafeNormal() * 2000.0f;

                SplinterHerds.Add(SplitHerd);
            }
            else {
                Animal.HerdSize = 50.0f;
            }
        }
    }

    if (SplinterHerds.Num() > 0) {
        for (const FAnimalData& Splinter : SplinterHerds) {
            Animals.Add(Splinter);
        }
    }

    if (EndIdx > StartIdx) {
        Manager->bFaunaVisualDirty = true;
    }
}

bool UFaunaSystem::TrySpawnHerd(ASimWorldManager* Manager, FVector2D SpawnLoc, EAnimalType AnimalType)
{
    if (!Manager || !Manager->ManaModule) return false;

    if (Animals.Num() >= Manager->MaxAnimals) return false;

    if (!Manager->ManaModule->SpendMana(80.0f, TEXT("Stvoreni stada"))) return false;

    FAnimalData Animal;
    Animal.Position = SpawnLoc;
    Animal.Type = AnimalType;
    Animal.HerdSize = (AnimalType == EAnimalType::Predator) ? 5.0f : 25.0f;
    Animal.TargetDirection = FVector2D(1.0f, 0.0f);
    Animal.StrategicTarget = SpawnLoc + FVector2D(2000.0f, 0.0f);
    Animal.Age = 1.0f;
    Animal.Condition = 1.0f;

    Animals.Add(Animal);
    Manager->bFaunaVisualDirty = true;
    return true;
}