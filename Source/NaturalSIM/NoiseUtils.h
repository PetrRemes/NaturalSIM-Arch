
#pragma once

#include "CoreMinimal.h"

// Perlin FBM helpery jako FORCEINLINE, aby se daly bezpeènì includovat do více TU.
static FORCEINLINE float GetFBM(float X, float Y, float Freq, int32 Octaves, int32 Seed)
{
    float Val = 0.0f; float Amp = 1.0f; float MaxAmp = 0.0f;
    float SeedOffsetX = Seed * 13.37f;
    float SeedOffsetY = Seed * 7.73f;
    for (int i = 0; i < Octaves; i++) {
        Val += (FMath::PerlinNoise2D(FVector2D((X * Freq) + SeedOffsetX, (Y * Freq) + SeedOffsetY)) + 1.0f) * 0.5f * Amp;
        MaxAmp += Amp; Amp *= 0.5f; Freq *= 2.0f;
    }
    return Val / FMath::Max(MaxAmp, 0.001f);
}

static FORCEINLINE float GetRidgedFBM(float X, float Y, float Freq, int32 Octaves, int32 Seed)
{
    float Val = 0.0f; float Amp = 1.0f; float MaxAmp = 0.0f;
    float SeedOffsetX = Seed * 13.37f;
    float SeedOffsetY = Seed * 7.73f;
    for (int i = 0; i < Octaves; i++) {
        float Noise = FMath::PerlinNoise2D(FVector2D((X * Freq) + SeedOffsetX, (Y * Freq) + SeedOffsetY));
        Noise = 1.0f - FMath::Abs(Noise);
        Noise *= Noise;
        Val += Noise * Amp;
        MaxAmp += Amp; Amp *= 0.5f; Freq *= 2.0f;
    }
    return Val / FMath::Max(MaxAmp, 0.001f);
}