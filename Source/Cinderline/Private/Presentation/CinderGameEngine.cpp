#include "Presentation/CinderGameEngine.h"

#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformFramePacer.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "DynamicRHI.h"
#include "Widgets/SWindow.h"
#if PLATFORM_MAC || PLATFORM_IOS
#include "CinderMetalFX.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCinderFramePacing, Log, All);

namespace
{
const TCHAR* FrameStateName(ECinderFramePacingState State)
{
    switch (State)
    {
    case ECinderFramePacingState::Gameplay: return TEXT("gameplay");
    case ECinderFramePacingState::Menu: return TEXT("menu");
    case ECinderFramePacingState::Paused: return TEXT("paused");
    case ECinderFramePacingState::Results: return TEXT("results");
    case ECinderFramePacingState::Idle: return TEXT("idle");
    case ECinderFramePacingState::Background: return TEXT("background");
    default: return TEXT("bypass");
    }
}

const TCHAR* ThermalName(ECinderThermalPressure Pressure)
{
    switch (Pressure)
    {
    case ECinderThermalPressure::Fair: return TEXT("fair");
    case ECinderThermalPressure::Serious: return TEXT("serious");
    case ECinderThermalPressure::Critical: return TEXT("critical");
    default: return TEXT("nominal");
    }
}

const TCHAR* QualityName(ECinderMobileQuality Quality)
{
    switch (Quality)
    {
    case ECinderMobileQuality::Reduced: return TEXT("reduced");
    case ECinderMobileQuality::Minimum: return TEXT("minimum");
    default: return TEXT("full");
    }
}

ECinderThermalPressure ThermalFromSeverity(FCoreDelegates::ETemperatureSeverity Severity)
{
    switch (Severity)
    {
    case FCoreDelegates::ETemperatureSeverity::Bad: return ECinderThermalPressure::Fair;
    case FCoreDelegates::ETemperatureSeverity::Serious: return ECinderThermalPressure::Serious;
    case FCoreDelegates::ETemperatureSeverity::Critical: return ECinderThermalPressure::Critical;
    default: return ECinderThermalPressure::Nominal;
    }
}

ECinderThermalPressure ReadPlatformThermalPressure()
{
    switch (FPlatformMisc::GetDeviceThermalState())
    {
    case EDeviceThermalState::Light: return ECinderThermalPressure::Fair;
    case EDeviceThermalState::Moderate: return ECinderThermalPressure::Serious;
    case EDeviceThermalState::Severe:
    case EDeviceThermalState::Critical:
    case EDeviceThermalState::Emergency:
    case EDeviceThermalState::Shutdown: return ECinderThermalPressure::Critical;
    default: return ECinderThermalPressure::Nominal;
    }
}

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
TAutoConsoleVariable<int32> CVarCinderIOSThermalOverride(
    TEXT("cinder.ios.ThermalOverride"), -1,
    TEXT("Development test override: -1=physical, 0=nominal, 1=fair, 2=serious, 3=critical."), ECVF_Cheat);
TAutoConsoleVariable<int32> CVarCinderIOSLowPowerOverride(
    TEXT("cinder.ios.LowPowerOverride"), -1,
    TEXT("Development test override: -1=physical, 0=disabled, 1=enabled."), ECVF_Cheat);
#endif
}

void UCinderGameEngine::Init(IEngineLoop* InEngineLoop)
{
    Super::Init(InEngineLoop);
#if PLATFORM_IOS
    ActualThermalPressure = ReadPlatformThermalPressure();
    bActualLowPowerMode = FPlatformMisc::IsInLowPowerMode();
    IOSPolicy.Reset(ActualThermalPressure, bActualLowPowerMode, FPlatformTime::Seconds());
    TemperatureHandle = FCoreDelegates::OnTemperatureChange.AddUObject(this, &UCinderGameEngine::HandleTemperatureChanged);
    LowPowerHandle = FCoreDelegates::OnLowPowerMode.AddUObject(this, &UCinderGameEngine::HandleLowPowerModeChanged);
    BackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &UCinderGameEngine::HandleApplicationBackgrounded);
    ForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(this, &UCinderGameEngine::HandleApplicationForegrounded);
    RefreshIOSPerformancePolicy();
#endif
}

void UCinderGameEngine::PreExit()
{
#if PLATFORM_IOS
    FCoreDelegates::OnTemperatureChange.Remove(TemperatureHandle);
    FCoreDelegates::OnLowPowerMode.Remove(LowPowerHandle);
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
    FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);
    ApplyMobileQuality(ECinderMobileQuality::Full);
#endif
    RefreshMetalFXResolution(true);
    if (bCinderDisabledWorldRendering && GameViewport)
    {
        GameViewport->bDisableWorldRendering = false;
        bCinderDisabledWorldRendering = false;
    }
    Super::PreExit();
}

void UCinderGameEngine::Tick(float DeltaSeconds, bool bIdleMode)
{
#if PLATFORM_IOS
    RefreshIOSPerformancePolicy();
#endif
    RefreshMetalFXResolution();
    Super::Tick(DeltaSeconds, bIdleMode);
}

void UCinderGameEngine::RefreshMetalFXResolution(bool bRelease)
{
#if PLATFORM_MAC || PLATFORM_IOS
    if (!FApp::CanEverRender() || IsRunningCommandlet() || GIsEditor) return;
    IConsoleVariable* Screen = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
    IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CinderMetalFX.Enabled"));
    IConsoleVariable* Requested = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CinderMetalFX.ScreenPercentage"));
    if (!Screen || !Enabled || !Requested || !ICinderMetalFXModule::IsAvailable()) return;
    const FCinderMetalFXDiagnostics D = ICinderMetalFXModule::Get().GetDiagnostics();
    // The first game view establishes support. Failed native inputs restore the
    // ordinary resolution policy for this run rather than repeatedly retrying.
    const bool bEligible = !bRelease && Enabled->GetInt() != 0 && D.bRHIIsMetal
        && D.bOSSupported && D.bDeviceSupported && D.bBridgeAvailable
        && D.bInterfaceActive && D.Fallbacks == 0;
    const float Current = Screen->GetFloat();
    const float Desired = MetalFXResolution.Update(Current, Requested->GetFloat(), bEligible);
    if (!FMath::IsNearlyEqual(Current, Desired))
    {
        Screen->SetWithCurrentPriority(Desired);
        UE_LOG(LogCinderFramePacing, Display,
            TEXT("CINDERLINE_METALFX_RESOLUTION eligible=%d requested=%.2f previous=%.2f effective=%.2f"),
            bEligible, Requested->GetFloat(), Current, Desired);
    }
#endif
}

void UCinderGameEngine::RedrawViewports(bool bShouldPresent)
{
    // Super::Tick processes menu input and redraws internally. Resolve the state
    // here, immediately before drawing, so StartMatch restores the first frame.
    UpdateMenuWorldRendering(GetFramePacingState());
    Super::RedrawViewports(bShouldPresent);
}

float UCinderGameEngine::ApplyStateCap(float EngineLimit, ECinderFramePacingState State)
{
    float StateLimit = 0;
    switch (State)
    {
    case ECinderFramePacingState::Gameplay: StateLimit = 120; break;
    case ECinderFramePacingState::Menu:
    case ECinderFramePacingState::Paused:
    case ECinderFramePacingState::Results:
    case ECinderFramePacingState::Idle: StateLimit = 30; break;
    case ECinderFramePacingState::Background: StateLimit = 10; break;
    default: return EngineLimit;
    }
    return EngineLimit > 0 ? FMath::Min(EngineLimit, StateLimit) : StateLimit;
}

ECinderFramePacingState UCinderGameEngine::ClassifyMatchState(const ACinderBattlefield* Battlefield, bool bForeground)
{
    if (!bForeground) return ECinderFramePacingState::Background;
    if (!IsValid(Battlefield)) return ECinderFramePacingState::Idle;
    if (Battlefield->IsMenu()) return ECinderFramePacingState::Menu;
    if (Battlefield->IsPaused()) return ECinderFramePacingState::Paused;
    if (Battlefield->Sim().winner() != -1 || (Battlefield->IsOnlineMatch() && Battlefield->Sim().eliminated(0))) return ECinderFramePacingState::Results;
    return ECinderFramePacingState::Gameplay;
}

ECinderFramePacingState UCinderGameEngine::GetFramePacingState() const
{
    if (GIsEditor || IsRunningCommandlet() || IsRunningDedicatedServer() || FApp::IsUnattended()
        || FApp::IsBenchmarking() || FApp::UseFixedTimeStep() || bUseFixedFrameRate || !FApp::CanEverRender())
        return ECinderFramePacingState::Bypass;

#if PLATFORM_DESKTOP
    if (!FSlateApplication::IsInitialized() || FSlateApplication::Get().IsRenderingOffScreen() || !GameViewport)
        return ECinderFramePacingState::Bypass;
    UWorld* World = GameViewport->GetWorld();
    if (World && (World->WorldType != EWorldType::Game || World->GetNetMode() != NM_Standalone))
        return ECinderFramePacingState::Bypass;
    const TSharedPtr<SWindow> Window = GameViewport->GetWindow();
    if (!Window.IsValid()) return ECinderFramePacingState::Bypass;
    const TSharedPtr<FGenericWindow> NativeWindow = Window->GetNativeWindow();
    if (!NativeWindow.IsValid()) return ECinderFramePacingState::Bypass;
    const bool bForeground = FSlateApplication::Get().IsActive() && Window->IsActive() && !NativeWindow->IsMinimized();
#elif PLATFORM_IOS
    if (!GameViewport) return ECinderFramePacingState::Idle;
    UWorld* World = GameViewport->GetWorld();
    const bool bForeground = bIOSForeground;
#else
    return ECinderFramePacingState::Bypass;
#endif

#if PLATFORM_DESKTOP || PLATFORM_IOS
    const auto* Controller = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
    const ACinderBattlefield* Battlefield = Controller ? Controller->Battlefield() : nullptr;
    if (Battlefield && Battlefield->GetWorld() != World) Battlefield = nullptr;
    return ClassifyMatchState(Battlefield, bForeground);
#endif
}

float UCinderGameEngine::GetMaxTickRate(float DeltaTime, bool bAllowFrameRateSmoothing) const
{
    const float EngineLimit = Super::GetMaxTickRate(DeltaTime, bAllowFrameRateSmoothing);
    const ECinderFramePacingState State = GetFramePacingState();
#if PLATFORM_IOS
    const float EffectiveLimit = FCinderIOSPerformancePolicy::ApplyCap(EngineLimit, RequestedFramePace);
#else
    const float EffectiveLimit = ApplyStateCap(EngineLimit, State);
#endif
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
    if (!bReportedFrameState || LastReportedFrameState != State)
    {
        bReportedFrameState = true;
        LastReportedFrameState = State;
        UE_LOG(LogCinderFramePacing, Display, TEXT("CINDERLINE_FRAME_PACING state=%s effective_fps=%.2f engine_fps=%.2f"),
            FrameStateName(State), EffectiveLimit, EngineLimit);
    }
#endif
    return EffectiveLimit;
}

double UCinderGameEngine::GetThermalStableSeconds() const
{
    return IOSPolicy.StableSeconds(FPlatformTime::Seconds());
}

void UCinderGameEngine::HandleTemperatureChanged(FCoreDelegates::ETemperatureSeverity Severity)
{
    ActualThermalPressure = ThermalFromSeverity(Severity);
    RefreshIOSPerformancePolicy();
}

void UCinderGameEngine::HandleLowPowerModeChanged(bool bEnabled)
{
    bActualLowPowerMode = bEnabled;
    RefreshIOSPerformancePolicy();
}

void UCinderGameEngine::HandleApplicationBackgrounded()
{
    bIOSForeground = false;
    RefreshIOSPerformancePolicy();
}

void UCinderGameEngine::HandleApplicationForegrounded()
{
    bIOSForeground = true;
    RefreshIOSPerformancePolicy();
}

void UCinderGameEngine::RefreshIOSPerformancePolicy()
{
#if PLATFORM_IOS
    ECinderThermalPressure PolicyThermal = ActualThermalPressure;
    bool bPolicyLowPower = bActualLowPowerMode;
    bUsingIOSPolicyOverride = false;
    bool bThermalOverridden = false;
    bool bLowPowerOverridden = false;
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
    const int32 ThermalOverride = CVarCinderIOSThermalOverride.GetValueOnGameThread();
    const int32 LowPowerOverride = CVarCinderIOSLowPowerOverride.GetValueOnGameThread();
    if (ThermalOverride >= 0)
    {
        PolicyThermal = static_cast<ECinderThermalPressure>(FMath::Clamp(ThermalOverride, 0, 3));
        bUsingIOSPolicyOverride = true;
        bThermalOverridden = true;
    }
    if (LowPowerOverride >= 0)
    {
        bPolicyLowPower = LowPowerOverride != 0;
        bUsingIOSPolicyOverride = true;
        bLowPowerOverridden = true;
    }
#endif
    const double Now = FPlatformTime::Seconds();
    const ECinderFramePacingState State = GetFramePacingState();
    if (Now - LastPhysicalTelemetrySeconds >= 10.0)
    {
        // Poll as well as listening to delegates so startup and long validation
        // runs always retain a timestamped physical reading.
        ActualThermalPressure = ReadPlatformThermalPressure();
        bActualLowPowerMode = FPlatformMisc::IsInLowPowerMode();
        LastPhysicalTelemetrySeconds = Now;
        const float DeviceTemperature = FPlatformMisc::GetDeviceTemperature();
        const FString TemperatureReading = DeviceTemperature > 0.0f
            ? FString::Printf(TEXT("%.2f"), DeviceTemperature) : TEXT("unavailable");
        const FCPUTime CPUTime = FPlatformTime::GetCPUTime();
        const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
        const uint32 GPUCycles = RHIGetGPUFrameCycles();
        const FString GPUTime = GPUCycles > 0
            ? FString::Printf(TEXT("%.3f"), FPlatformTime::ToMilliseconds(GPUCycles)) : TEXT("unavailable");
        UE_LOG(LogCinderFramePacing, Display,
            TEXT("CINDERLINE_IOS_PHYSICAL_STATE state=%s thermal=%s raw_thermal=%d low_power=%d device_temperature_c=%s requested_fps=%d actual_pacer_fps=%d quality=%s process_cpu_pct=%.2f process_cpu_pct_one_core=%.2f process_memory_mb=%.2f gpu_frame_ms=%s gpu_scope=latest_completed_engine_frame"),
            FrameStateName(State),
            ThermalName(ActualThermalPressure), static_cast<int32>(FPlatformMisc::GetDeviceThermalState()),
            bActualLowPowerMode, *TemperatureReading, RequestedFramePace,
            FPlatformRHIFramePacer::GetFramePace(), QualityName(MobileQuality),
            CPUTime.CPUTimePct, CPUTime.CPUTimePctRelative,
            static_cast<double>(Memory.UsedPhysical) / (1024.0 * 1024.0), *GPUTime);
        if (!bThermalOverridden) PolicyThermal = ActualThermalPressure;
        if (!bLowPowerOverridden) bPolicyLowPower = bActualLowPowerMode;
    }
    IOSPolicy.Update(PolicyThermal, bPolicyLowPower, Now);
    const FCinderIOSPerformanceDecision Decision = FCinderIOSPerformancePolicy::Evaluate(
        State, IOSPolicy.EffectivePressure(), IOSPolicy.IsLowPowerPolicyActive(),
        FPlatformRHIFramePacer::SupportsFramePace(15), FPlatformRHIFramePacer::SupportsFramePace(20));
    const int32 PreviousRequested = RequestedFramePace;
    const int32 PreviousActual = ActualFramePace;
    const ECinderMobileQuality PreviousQuality = MobileQuality;
    RequestedFramePace = Decision.FramePace;
    ApplyMobileQuality(Decision.Quality);
    if (RequestedFramePace > 0)
    {
        ActualFramePace = FPlatformRHIFramePacer::GetFramePace();
        if (ActualFramePace != RequestedFramePace)
        {
            FPlatformRHIFramePacer::SetFramePace(RequestedFramePace);
            ActualFramePace = FPlatformRHIFramePacer::GetFramePace();
        }
    }

    if (!bReportedIOSPolicy || LastIOSPolicyState != State
        || LastLoggedActualThermal != ActualThermalPressure
        || LastLoggedEffectiveThermal != IOSPolicy.EffectivePressure()
        || bLastLoggedLowPower != bActualLowPowerMode || bLastLoggedOverride != bUsingIOSPolicyOverride
        || PreviousRequested != RequestedFramePace || PreviousActual != ActualFramePace || PreviousQuality != MobileQuality)
    {
        UE_LOG(LogCinderFramePacing, Display,
            TEXT("CINDERLINE_IOS_POLICY state=%s actual_thermal=%s effective_thermal=%s low_power=%d effective_low_power=%d requested_fps=%d actual_pacer_fps=%d quality=%s stable_seconds=%.1f source=%s"),
            FrameStateName(State), ThermalName(ActualThermalPressure), ThermalName(IOSPolicy.EffectivePressure()),
            bActualLowPowerMode, IOSPolicy.IsLowPowerPolicyActive(), RequestedFramePace, ActualFramePace, QualityName(MobileQuality),
            IOSPolicy.StableSeconds(Now), bUsingIOSPolicyOverride ? TEXT("development_override") : TEXT("physical"));
        bReportedIOSPolicy = true;
        LastIOSPolicyState = State;
        LastLoggedActualThermal = ActualThermalPressure;
        LastLoggedEffectiveThermal = IOSPolicy.EffectivePressure();
        bLastLoggedLowPower = bActualLowPowerMode;
        bLastLoggedOverride = bUsingIOSPolicyOverride;
    }
#endif
}

void UCinderGameEngine::ApplyMobileQuality(ECinderMobileQuality Quality)
{
#if PLATFORM_IOS
    // Stable policy frames need no console lookups or quality writes.
    if (bQualityBaselineCaptured && Quality == MobileQuality) return;
    IConsoleVariable* Bloom = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BloomQuality"));
    IConsoleVariable* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShadowQuality"));
    IConsoleVariable* ShadowMaxResolution = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.MaxResolution"));
    IConsoleVariable* ShadowMaxCSMResolution = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.MaxCSMResolution"));
    IConsoleVariable* ShadowCascades = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.CSM.MaxCascades"));
    IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
    if (!bQualityBaselineCaptured && Bloom && Shadows)
    {
        BaselineBloomQuality = Bloom->GetInt();
        BaselineShadowQuality = Shadows->GetInt();
        BaselineShadowMaxResolution = ShadowMaxResolution ? ShadowMaxResolution->GetInt() : 0;
        BaselineShadowMaxCSMResolution = ShadowMaxCSMResolution ? ShadowMaxCSMResolution->GetInt() : 0;
        BaselineShadowCascades = ShadowCascades ? ShadowCascades->GetInt() : 0;
        BaselineScreenPercentage = ScreenPercentage ? ScreenPercentage->GetFloat() : 100.0f;
        bQualityBaselineCaptured = true;
    }
    if (!bQualityBaselineCaptured || Quality == MobileQuality) return;
    MobileQuality = Quality;
    const int32 BloomValue = Quality == ECinderMobileQuality::Full ? BaselineBloomQuality
        : (Quality == ECinderMobileQuality::Reduced ? FMath::Min(BaselineBloomQuality, 1) : 0);
    const int32 ShadowValue = Quality == ECinderMobileQuality::Full ? BaselineShadowQuality
        : (Quality == ECinderMobileQuality::Reduced ? FMath::Min(BaselineShadowQuality, 2) : 0);
    Bloom->Set(BloomValue, ECVF_SetByCode);
    Shadows->Set(ShadowValue, ECVF_SetByCode);
    const int32 MaxResolution = Quality == ECinderMobileQuality::Full ? BaselineShadowMaxResolution
        : FMath::Min(BaselineShadowMaxResolution, 512);
    const int32 MaxCSMResolution = Quality == ECinderMobileQuality::Full ? BaselineShadowMaxCSMResolution
        : FMath::Min(BaselineShadowMaxCSMResolution, 512);
    const int32 Cascades = Quality == ECinderMobileQuality::Full ? BaselineShadowCascades
        : FMath::Min(BaselineShadowCascades, 1);
    if (ShadowMaxResolution && BaselineShadowMaxResolution > 0) ShadowMaxResolution->Set(MaxResolution, ECVF_SetByCode);
    if (ShadowMaxCSMResolution && BaselineShadowMaxCSMResolution > 0) ShadowMaxCSMResolution->Set(MaxCSMResolution, ECVF_SetByCode);
    if (ShadowCascades && BaselineShadowCascades > 0) ShadowCascades->Set(Cascades, ECVF_SetByCode);
    const float ScreenPercentageValue = FCinderIOSPerformancePolicy::ResolveScreenPercentage(BaselineScreenPercentage, Quality);
    if (ScreenPercentage) ScreenPercentage->Set(ScreenPercentageValue, ECVF_SetByCode);
#endif
}

void UCinderGameEngine::UpdateMenuWorldRendering(ECinderFramePacingState State)
{
#if PLATFORM_IOS || PLATFORM_DESKTOP
    if (!GameViewport) return;
    bool bShouldDisable = State == ECinderFramePacingState::Menu;
    if (State == ECinderFramePacingState::Background)
    {
        // Losing focus must not restart an invisible battlefield behind a menu.
        const UWorld* World = GameViewport->GetWorld();
        const auto* Controller = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        const ACinderBattlefield* Battlefield = Controller ? Controller->Battlefield() : nullptr;
        bShouldDisable = IsValid(Battlefield) && Battlefield->GetWorld() == World && Battlefield->IsMenu();
    }
    if (bShouldDisable && !GameViewport->bDisableWorldRendering)
    {
        GameViewport->bDisableWorldRendering = true;
        bCinderDisabledWorldRendering = true;
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
        UE_LOG(LogCinderFramePacing, Display, TEXT("CINDERLINE_WORLD_RENDERING disabled=1 state=%s"), FrameStateName(State));
#endif
    }
    else if (!bShouldDisable && bCinderDisabledWorldRendering)
    {
        GameViewport->bDisableWorldRendering = false;
        bCinderDisabledWorldRendering = false;
#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
        UE_LOG(LogCinderFramePacing, Display, TEXT("CINDERLINE_WORLD_RENDERING disabled=0 state=%s"), FrameStateName(State));
#endif
    }
#endif
}
