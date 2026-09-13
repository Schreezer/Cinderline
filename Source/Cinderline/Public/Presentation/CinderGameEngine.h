#pragma once

#include "CoreMinimal.h"
#include "Engine/GameEngine.h"
#include "Misc/CoreDelegates.h"
#include "Presentation/CinderIOSPerformancePolicy.h"
#include "Presentation/CinderMetalFXResolution.h"
#include "CinderGameEngine.generated.h"

class ACinderBattlefield;

enum class ECinderFramePacingState : uint8
{
    Bypass,
    Gameplay,
    Menu,
    Paused,
    Results,
    Idle,
    Background
};

/** Applies platform presentation pacing without changing the 20 Hz simulation. */
UCLASS()
class CINDERLINE_API UCinderGameEngine : public UGameEngine
{
    GENERATED_BODY()

public:
    virtual void Init(IEngineLoop* InEngineLoop) override;
    virtual void PreExit() override;
    virtual void Tick(float DeltaSeconds, bool bIdleMode) override;
    virtual void RedrawViewports(bool bShouldPresent = true) override;
    virtual float GetMaxTickRate(float DeltaTime, bool bAllowFrameRateSmoothing = true) const override;

    ECinderFramePacingState GetFramePacingState() const;
    static ECinderFramePacingState ClassifyMatchState(const ACinderBattlefield* Battlefield, bool bForeground);
    static float ApplyStateCap(float EngineLimit, ECinderFramePacingState State);

    ECinderThermalPressure GetActualThermalPressure() const { return ActualThermalPressure; }
    ECinderThermalPressure GetEffectiveThermalPressure() const { return IOSPolicy.EffectivePressure(); }
    ECinderMobileQuality GetMobileQuality() const { return MobileQuality; }
    bool IsInLowPowerMode() const { return bActualLowPowerMode; }
    bool IsLowPowerPolicyActive() const { return IOSPolicy.IsLowPowerPolicyActive(); }
    bool IsUsingIOSPolicyOverride() const { return bUsingIOSPolicyOverride; }
    int32 GetRequestedFramePace() const { return RequestedFramePace; }
    int32 GetActualFramePace() const { return ActualFramePace; }
    double GetThermalStableSeconds() const;

private:
    void HandleTemperatureChanged(FCoreDelegates::ETemperatureSeverity Severity);
    void HandleLowPowerModeChanged(bool bEnabled);
    void HandleApplicationBackgrounded();
    void HandleApplicationForegrounded();
    void RefreshIOSPerformancePolicy();
    void ApplyMobileQuality(ECinderMobileQuality Quality);
    void UpdateMenuWorldRendering(ECinderFramePacingState State);
    void RefreshMetalFXResolution(bool bRelease = false);

    mutable bool bReportedFrameState = false;
    mutable ECinderFramePacingState LastReportedFrameState = ECinderFramePacingState::Bypass;
    FCinderIOSPerformancePolicy IOSPolicy;
    FCinderMetalFXResolution MetalFXResolution;
    ECinderThermalPressure ActualThermalPressure = ECinderThermalPressure::Nominal;
    ECinderMobileQuality MobileQuality = ECinderMobileQuality::Full;
    bool bActualLowPowerMode = false;
    bool bIOSForeground = true;
    bool bUsingIOSPolicyOverride = false;
    bool bCinderDisabledWorldRendering = false;
    bool bQualityBaselineCaptured = false;
    int32 BaselineBloomQuality = 0;
    int32 BaselineShadowQuality = 0;
    int32 BaselineShadowMaxResolution = 0;
    int32 BaselineShadowMaxCSMResolution = 0;
    int32 BaselineShadowCascades = 0;
    float BaselineScreenPercentage = 100.0f;
    int32 RequestedFramePace = 0;
    int32 ActualFramePace = 0;
    double LastPhysicalTelemetrySeconds = -10.0;
    bool bReportedIOSPolicy = false;
    ECinderFramePacingState LastIOSPolicyState = ECinderFramePacingState::Bypass;
    ECinderThermalPressure LastLoggedActualThermal = ECinderThermalPressure::Nominal;
    ECinderThermalPressure LastLoggedEffectiveThermal = ECinderThermalPressure::Nominal;
    bool bLastLoggedLowPower = false;
    bool bLastLoggedOverride = false;
    FDelegateHandle TemperatureHandle;
    FDelegateHandle LowPowerHandle;
    FDelegateHandle BackgroundHandle;
    FDelegateHandle ForegroundHandle;
};
