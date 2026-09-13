#pragma once

#include "CoreMinimal.h"

/** Owns only the temporary MetalFX resolution override, preserving other writers. */
struct FCinderMetalFXResolution
{
    float Update(float Current, float RequestedPercent, bool bEligible)
    {
        if (!bOwnsOverride || !FMath::IsNearlyEqual(Current, LastApplied))
        {
            // Includes thermal policy changes and a newly selected lower user limit.
            Baseline = Current;
        }
        if (!bEligible || !FMath::IsFinite(RequestedPercent) || RequestedPercent <= 0.0f)
        {
            bOwnsOverride = false;
            LastApplied = Baseline;
            return Baseline;
        }
        const float ResolvedBaseline = Baseline > 0.0f ? Baseline : 100.0f;
        LastApplied = FMath::Min(ResolvedBaseline, FMath::Clamp(RequestedPercent, 50.0f, 100.0f));
        bOwnsOverride = true;
        return LastApplied;
    }

private:
    float Baseline = 100.0f;
    float LastApplied = 100.0f;
    bool bOwnsOverride = false;
};
