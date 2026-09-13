#include "Presentation/CinderIOSPerformancePolicy.h"

#include "Presentation/CinderGameEngine.h"

namespace
{
ECinderThermalPressure MaxPressure(ECinderThermalPressure A, ECinderThermalPressure B)
{
    return static_cast<ECinderThermalPressure>(FMath::Max(static_cast<uint8>(A), static_cast<uint8>(B)));
}

bool IsIdlePresentation(ECinderFramePacingState State)
{
    return State == ECinderFramePacingState::Menu || State == ECinderFramePacingState::Paused
        || State == ECinderFramePacingState::Results || State == ECinderFramePacingState::Idle;
}
}

void FCinderIOSPerformancePolicy::Reset(ECinderThermalPressure Thermal, bool bLowPowerMode, double NowSeconds)
{
    ObservedPressure = Thermal;
    bLowPower = bLowPowerMode;
    bLatchedLowPower = bLowPowerMode;
    LatchedPressure = MaxPressure(Thermal, bLowPowerMode ? ECinderThermalPressure::Fair : ECinderThermalPressure::Nominal);
    StableSinceSeconds = NowSeconds;
}

void FCinderIOSPerformancePolicy::Update(ECinderThermalPressure Thermal, bool bLowPowerMode, double NowSeconds)
{
    const ECinderThermalPressure PreviousObserved = MaxPressure(
        ObservedPressure, bLowPower ? ECinderThermalPressure::Fair : ECinderThermalPressure::Nominal);
    ObservedPressure = Thermal;
    bLowPower = bLowPowerMode;
    bLatchedLowPower |= bLowPowerMode;
    const ECinderThermalPressure CurrentObserved = MaxPressure(
        Thermal, bLowPowerMode ? ECinderThermalPressure::Fair : ECinderThermalPressure::Nominal);

    if (static_cast<uint8>(CurrentObserved) > static_cast<uint8>(LatchedPressure))
    {
        LatchedPressure = CurrentObserved;
        StableSinceSeconds = NowSeconds;
        return;
    }
    if (CurrentObserved != ECinderThermalPressure::Nominal)
    {
        // A lower pressure is still pressure. Do not recover until the OS has
        // reported fully nominal operation for one continuous cooldown.
        StableSinceSeconds = NowSeconds;
        return;
    }
    if (PreviousObserved != ECinderThermalPressure::Nominal)
    {
        StableSinceSeconds = NowSeconds;
        return;
    }
    if (LatchedPressure != ECinderThermalPressure::Nominal
        && NowSeconds - StableSinceSeconds >= RecoveryCooldownSeconds)
    {
        LatchedPressure = ECinderThermalPressure::Nominal;
        bLatchedLowPower = false;
    }
}

double FCinderIOSPerformancePolicy::StableSeconds(double NowSeconds) const
{
    if (ObservedPressure != ECinderThermalPressure::Nominal || bLowPower) return 0.0;
    return FMath::Max(0.0, NowSeconds - StableSinceSeconds);
}

FCinderIOSPerformanceDecision FCinderIOSPerformancePolicy::Evaluate(
    ECinderFramePacingState State,
    ECinderThermalPressure EffectiveThermal,
    bool bLowPowerMode,
    bool bSupports15FPS,
    bool bSupports20FPS)
{
    FCinderIOSPerformanceDecision Decision;
    if (EffectiveThermal >= ECinderThermalPressure::Serious)
        Decision.Quality = ECinderMobileQuality::Minimum;
    else if (EffectiveThermal >= ECinderThermalPressure::Fair || bLowPowerMode)
        Decision.Quality = ECinderMobileQuality::Reduced;

    if (State == ECinderFramePacingState::Bypass || State == ECinderFramePacingState::Background)
    {
        Decision.FramePace = 0;
        return Decision;
    }

    const int32 IdlePace = bSupports15FPS ? 15 : 30;
    if (IsIdlePresentation(State))
    {
        Decision.FramePace = IdlePace;
        return Decision;
    }

    if (EffectiveThermal == ECinderThermalPressure::Critical)
        Decision.FramePace = bSupports15FPS ? 15 : (bSupports20FPS ? 20 : 30);
    else if (EffectiveThermal == ECinderThermalPressure::Serious || bLowPowerMode)
        Decision.FramePace = bSupports20FPS ? 20 : 30;
    else
        Decision.FramePace = 30;
    return Decision;
}

float FCinderIOSPerformancePolicy::ApplyCap(float EngineLimit, int32 PolicyFramePace)
{
    if (PolicyFramePace <= 0) return EngineLimit;
    return EngineLimit > 0.0f ? FMath::Min(EngineLimit, static_cast<float>(PolicyFramePace))
                              : static_cast<float>(PolicyFramePace);
}

float FCinderIOSPerformancePolicy::ResolveScreenPercentage(float Baseline, ECinderMobileQuality Quality)
{
    if (Quality == ECinderMobileQuality::Full) return Baseline;
    // UE treats nonpositive r.ScreenPercentage as its automatic 100% default.
    // Resolve that sentinel only while degraded so Full can restore it exactly.
    const float ResolvedBaseline = Baseline > 0.0f ? Baseline : 100.0f;
    return FMath::Min(ResolvedBaseline, Quality == ECinderMobileQuality::Reduced ? 85.0f : 70.0f);
}
