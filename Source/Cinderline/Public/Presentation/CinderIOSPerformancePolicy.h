#pragma once

#include "CoreMinimal.h"

enum class ECinderFramePacingState : uint8;

enum class ECinderThermalPressure : uint8
{
    Nominal,
    Fair,
    Serious,
    Critical
};

enum class ECinderMobileQuality : uint8
{
    Full,
    Reduced,
    Minimum
};

struct FCinderIOSPerformanceDecision
{
    int32 FramePace = 30;
    ECinderMobileQuality Quality = ECinderMobileQuality::Full;
};

/** Pure, platform-independent policy used by the iOS runtime and automation tests. */
class CINDERLINE_API FCinderIOSPerformancePolicy
{
public:
    static constexpr double RecoveryCooldownSeconds = 60.0;

    void Reset(ECinderThermalPressure Thermal, bool bLowPowerMode, double NowSeconds);
    void Update(ECinderThermalPressure Thermal, bool bLowPowerMode, double NowSeconds);

    ECinderThermalPressure EffectivePressure() const { return LatchedPressure; }
    bool IsLowPowerMode() const { return bLowPower; }
    bool IsLowPowerPolicyActive() const { return bLatchedLowPower; }
    double StableSeconds(double NowSeconds) const;

    static FCinderIOSPerformanceDecision Evaluate(
        ECinderFramePacingState State,
        ECinderThermalPressure EffectiveThermal,
        bool bLowPowerMode,
        bool bSupports15FPS,
        bool bSupports20FPS);
    static float ApplyCap(float EngineLimit, int32 PolicyFramePace);
    static float ResolveScreenPercentage(float Baseline, ECinderMobileQuality Quality);

private:
    ECinderThermalPressure ObservedPressure = ECinderThermalPressure::Nominal;
    ECinderThermalPressure LatchedPressure = ECinderThermalPressure::Nominal;
    bool bLowPower = false;
    bool bLatchedLowPower = false;
    double StableSinceSeconds = 0.0;
};
