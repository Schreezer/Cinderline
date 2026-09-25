#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "GPUProfiler.h"
#include "RenderTimer.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"
#include "TimerManager.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderGameEngine.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderQuality, Log, All);

namespace
{
UGameViewportClient* RenderedViewport(UWorld* World)
{
    if (!World || !World->IsGameWorld() || !FApp::CanEverRender() ||
        FApp::IsUnattended() || IsRunningCommandlet()) return nullptr;
    UGameViewportClient* Client = World->GetGameViewport();
    if (!Client || Client->GetWorld() != World || !Client->Viewport) return nullptr;
    const FIntPoint Size = Client->Viewport->GetSizeXY();
    const TSharedPtr<SWindow> Window = Client->GetWindow();
    if (Size.X <= 0 || Size.Y <= 0 || !Window.IsValid() ||
        !Window->IsVisible() || Window->IsWindowMinimized()) return nullptr;
    return Client;
}

const TCHAR* ForegroundStatus()
{
#if PLATFORM_IOS
    // iOS does not implement IsThisApplicationForeground, and FApp::HasFocus
    // defaults to true there instead of reflecting UIApplication state.
    return TEXT("unavailable");
#else
    return FPlatformApplicationMisc::IsThisApplicationForeground() ? TEXT("1") : TEXT("0");
#endif
}

void LogQuality(UWorld* World)
{
    UGameViewportClient* Client = World ? World->GetGameViewport() : nullptr;
    FViewport* Viewport = Client ? Client->Viewport : nullptr;
    const FIntPoint Size = Viewport ? Viewport->GetSizeXY() : FIntPoint::ZeroValue;
    const FIntPoint TargetSize = Viewport ? Viewport->GetRenderTargetTextureSizeXY() : FIntPoint::ZeroValue;
#if PLATFORM_MAC
    const bool HighDPIActive = FPlatformApplicationMisc::IsHighDPIModeEnabled();
#else
    const bool HighDPIActive = FPlatformApplicationMisc::IsHighDPIAwarenessEnabled();
#endif
    UE_LOG(LogCinderQuality, Display,
        TEXT("CINDERLINE_QUALITY map=%s viewport_px=%dx%d render_target_px=%dx%d dpi_scale=%.3f high_dpi_active=%d foreground=%s rendered_viewport_available=%d"),
        World ? *World->GetMapName() : TEXT("none"), Size.X, Size.Y, TargetSize.X, TargetSize.Y,
        Client ? Client->GetDPIScale() : 0.0f, HighDPIActive,
        ForegroundStatus(), RenderedViewport(World) != nullptr);

    const auto* CinderEngine = Cast<UCinderGameEngine>(GEngine);
    const float DeviceTemperature = FPlatformMisc::GetDeviceTemperature();
    const FString TemperatureReading = DeviceTemperature > 0.0f
        ? FString::Printf(TEXT("%.2f"), DeviceTemperature) : TEXT("unavailable");
    UE_LOG(LogCinderQuality, Display,
        TEXT("CINDERLINE_QUALITY_FRAME_LIMIT engine=%s state=%d effective_fps=%.2f; limit is not measured cadence or GPU utilization"),
        GEngine ? *GEngine->GetClass()->GetName() : TEXT("none"),
        CinderEngine ? static_cast<int32>(CinderEngine->GetFramePacingState()) : -1,
        GEngine ? GEngine->GetMaxTickRate(static_cast<float>(FApp::GetDeltaTime())) : 0.0f);
    UE_LOG(LogCinderQuality, Display,
        TEXT("CINDERLINE_QUALITY_IOS_POLICY actual_thermal=%d raw_platform_thermal=%d device_temperature_c=%s effective_thermal=%d low_power=%d effective_low_power=%d requested_frame_pace=%d actual_frame_pace=%d quality=%d stable_seconds=%.1f source=%s world_rendering_disabled=%d"),
        CinderEngine ? static_cast<int32>(CinderEngine->GetActualThermalPressure()) : -1,
        static_cast<int32>(FPlatformMisc::GetDeviceThermalState()), *TemperatureReading,
        CinderEngine ? static_cast<int32>(CinderEngine->GetEffectiveThermalPressure()) : -1,
        CinderEngine && CinderEngine->IsInLowPowerMode(),
        CinderEngine && CinderEngine->IsLowPowerPolicyActive(),
        CinderEngine ? CinderEngine->GetRequestedFramePace() : 0,
        CinderEngine ? CinderEngine->GetActualFramePace() : 0,
        CinderEngine ? static_cast<int32>(CinderEngine->GetMobileQuality()) : -1,
        CinderEngine ? CinderEngine->GetThermalStableSeconds() : 0.0,
        CinderEngine && CinderEngine->IsUsingIOSPolicyOverride() ? TEXT("development_override") : TEXT("physical"),
        Client && Client->bDisableWorldRendering);

    static const TCHAR* Names[] = {
        TEXT("EnableHighDPIAwareness"), TEXT("r.AntiAliasingMethod"),
        TEXT("r.TemporalAA.Quality"), TEXT("r.TemporalAA.HistoryScreenPercentage"),
        TEXT("r.TSR.AsyncCompute"), TEXT("r.TSR.History.UpdateQuality"),
        TEXT("r.ScreenPercentage"), TEXT("r.SecondaryScreenPercentage.GameViewport"),
        TEXT("r.DynamicRes.OperationMode"), TEXT("r.TSR.History.ScreenPercentage"),
        TEXT("r.ReflectionMethod"), TEXT("r.SSR.Quality"), TEXT("r.SSR.HalfResSceneColor"),
        TEXT("r.DefaultFeature.Bloom"), TEXT("r.BloomQuality"),
        TEXT("r.DefaultFeature.AmbientOcclusion"), TEXT("r.AmbientOcclusionLevels"),
        TEXT("r.AmbientOcclusionMaxQuality"), TEXT("r.MaxAnisotropy"),
        TEXT("r.ShadowQuality"), TEXT("r.Shadow.MaxCSMResolution"), TEXT("r.Shadow.MaxResolution"),
        TEXT("r.Shadow.CSM.MaxCascades"), TEXT("r.Tonemapper.Sharpen"),
        TEXT("r.VSync"), TEXT("rhi.SyncInterval"), TEXT("t.MaxFPS"), TEXT("r.MotionBlurQuality"),
        TEXT("r.DepthOfFieldQuality"), TEXT("r.SceneColorFringeQuality"),
        TEXT("r.TranslucencyLightingVolume"),
        TEXT("r.CinderMetalFX.Enabled"), TEXT("r.CinderMetalFX.ScreenPercentage"),
        TEXT("r.TemporalAA.Upsampling")
    };
    for (const TCHAR* Name : Names)
    {
        const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        UE_LOG(LogCinderQuality, Display, TEXT("CINDERLINE_QUALITY_CVAR name=%s value=%s set_by=%s"),
            Name, Variable ? *Variable->GetString() : TEXT("unavailable"),
            Variable ? GetConsoleVariableSetByName(Variable->GetFlags()) : TEXT("unavailable"));
    }
#if PLATFORM_MAC || PLATFORM_IOS
    // Native encode counters distinguish actual MetalFX work from a requested setting.
    IConsoleManager::Get().ProcessUserConsoleInput(TEXT("r.CinderMetalFX.Status"), *GLog, World);
#endif
}

struct FEngineTimingSamples
{
    TArray<double> Values;
    int32 Unavailable = 0;

    void Reset(int32 Capacity)
    {
        Values.Reset(Capacity);
        Unavailable = 0;
    }

    bool Add(double Milliseconds, bool bAvailable = true)
    {
        if (!bAvailable || !FMath::IsFinite(Milliseconds) || Milliseconds <= 0)
        {
            ++Unavailable;
            return false;
        }
        Values.Add(Milliseconds);
        return true;
    }

    void Log(const TCHAR* Metric, const TCHAR* Source, int32 ViewportSamples, const TCHAR* Availability, bool bIncomplete = false) const
    {
        if (Values.IsEmpty())
        {
            UE_LOG(LogCinderQuality, Display,
                TEXT("CINDERLINE_FRAME_PROFILE_TIMING metric=%s status=unavailable samples=0 viewport_samples=%d unavailable_observations=%d mean_ms=unavailable median_ms=unavailable p95_ms=unavailable source=%s availability=%s"),
                Metric, ViewportSamples, Unavailable, Source, Availability);
            return;
        }
        TArray<double> Ordered = Values;
        Ordered.Sort();
        double Total = 0;
        for (double Value : Ordered) Total += Value;
        const int32 Count = Ordered.Num();
        const double Median = (Ordered[(Count - 1) / 2] + Ordered[Count / 2]) * 0.5;
        const int32 P95 = FMath::Clamp(FMath::CeilToInt(Count * 0.95f) - 1, 0, Count - 1);
        UE_LOG(LogCinderQuality, Display,
            TEXT("CINDERLINE_FRAME_PROFILE_TIMING metric=%s status=%s samples=%d viewport_samples=%d unavailable_observations=%d mean_ms=%.3f median_ms=%.3f p95_ms=%.3f source=%s availability=%s"),
            Metric, Unavailable > 0 || bIncomplete ? TEXT("partial") : TEXT("available"), Count, ViewportSamples,
            Unavailable, Total / Count, Median, Ordered[P95], Source, Availability);
    }
};

class FQualityFrameSample
{
public:
    ~FQualityFrameSample() { Detach(); }

    void Start(UWorld* World, int32 Count)
    {
        if (EndFrameHandle.IsValid())
        {
            UE_LOG(LogCinderQuality, Display, TEXT("CINDERLINE_FRAME_PROFILE already_active samples=%d/%d; use cinder.profile cancel to stop"), Samples.Num(), TargetSamples);
            return;
        }
        UGameViewportClient* Client = RenderedViewport(World);
        if (!Client)
        {
            UE_LOG(LogCinderQuality, Display, TEXT("CINDERLINE_FRAME_PROFILE rejected reason=requires_visible_rendered_game_viewport; headless and unattended runs are excluded"));
            return;
        }
        SampleWorld = World;
        SampleViewport = Client;
        TargetSamples = Count;
        WarmupRemaining = WarmupFrames;
        BackgroundSamples = 0;
        Samples.Reset(Count);
        GameTiming.Reset(Count); RenderTiming.Reset(Count); RHITiming.Reset(Count);
        GPUTiming.Reset(Count + GPUHistoryCapacity);
        GPUHistory = FRHIGPUFrameTimeHistory::FState{};
        GPUFramesWithoutCompletion = 0; GPUDisjointEvents = 0;
        GPUPollLimitHits = 0; GPUOverflowDiscarded = 0;
        StartedAt = FPlatformTime::Seconds();
        PreviousFrameAt = 0;
        bViewportDrawn = false;
        StartSize = Client->Viewport->GetSizeXY();
        DrawHandle = Client->OnEndDraw().AddRaw(this, &FQualityFrameSample::OnViewportDrawn);
        EndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FQualityFrameSample::OnEndFrame);
        CleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FQualityFrameSample::OnWorldCleanup);
        ExitHandle = FCoreDelegates::OnEnginePreExit.AddRaw(this, &FQualityFrameSample::OnEngineExit);
        UE_LOG(LogCinderQuality, Display,
            TEXT("CINDERLINE_FRAME_PROFILE started map=%s warmup_frames=%d sample_frames=%d viewport_px=%dx%d; measures viewport wall-clock cadence plus asynchronous raw engine timings"),
            *World->GetMapName(), WarmupFrames, TargetSamples, StartSize.X, StartSize.Y);
        LogQuality(World);
    }

    void Cancel()
    {
        if (EndFrameHandle.IsValid()) Abort(TEXT("cancelled"));
        else UE_LOG(LogCinderQuality, Display, TEXT("CINDERLINE_FRAME_PROFILE idle"));
    }

private:
    static constexpr int32 WarmupFrames = 30;
    // Allows a sustained ten-minute run at either the 15 or 30 FPS policy,
    // with margin for warmup and short stalls.
    static constexpr double MaximumSeconds = 720.0;
    // UE 5.8 retains 16 completed GPU frame timings. A bounded drain allows a
    // concurrently publishing RHI to make progress without an unbounded loop.
    static constexpr int32 GPUHistoryCapacity = 16;
    static constexpr int32 GPUMaxPopsPerViewportFrame = 32;
    TWeakObjectPtr<UWorld> SampleWorld;
    TWeakObjectPtr<UGameViewportClient> SampleViewport;
    FDelegateHandle DrawHandle, EndFrameHandle, CleanupHandle, ExitHandle;
    TArray<double> Samples;
    FEngineTimingSamples GameTiming, RenderTiming, RHITiming, GPUTiming;
    FRHIGPUFrameTimeHistory::FState GPUHistory;
    int32 GPUFramesWithoutCompletion = 0, GPUDisjointEvents = 0;
    int32 GPUPollLimitHits = 0, GPUOverflowDiscarded = 0;
    FIntPoint StartSize = FIntPoint::ZeroValue;
    int32 TargetSamples = 180, WarmupRemaining = 0, BackgroundSamples = 0;
    double StartedAt = 0, PreviousFrameAt = 0;
    bool bViewportDrawn = false;

    void Detach()
    {
        if (DrawHandle.IsValid())
        {
            if (UGameViewportClient* Client = SampleViewport.Get()) Client->OnEndDraw().Remove(DrawHandle);
            DrawHandle.Reset();
        }
        if (EndFrameHandle.IsValid()) FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
        if (CleanupHandle.IsValid()) FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
        if (ExitHandle.IsValid()) FCoreDelegates::OnEnginePreExit.Remove(ExitHandle);
        EndFrameHandle.Reset(); CleanupHandle.Reset(); ExitHandle.Reset();
        SampleWorld.Reset(); SampleViewport.Reset();
        bViewportDrawn = false;
    }

    void Abort(const TCHAR* Reason)
    {
        UE_LOG(LogCinderQuality, Display, TEXT("CINDERLINE_FRAME_PROFILE aborted reason=%s samples=%d/%d"), Reason, Samples.Num(), TargetSamples);
        Detach();
    }

    void ReadGPUTimings(bool bRecord)
    {
        const int32 Before = GPUTiming.Values.Num();
        bool bDrained = false;
        for (int32 Pop = 0; Pop < GPUMaxPopsPerViewportFrame; ++Pop)
        {
            uint64 Cycles = 0;
            const auto Result = GPUHistory.PopFrameCycles(Cycles);
            if (Result == FRHIGPUFrameTimeHistory::EResult::Empty) { bDrained = true; break; }
            if (!bRecord) continue; // Consume historical and warmup completions.
            if (Result == FRHIGPUFrameTimeHistory::EResult::Disjoint) ++GPUDisjointEvents;
            if (GPUTiming.Values.Num() < TargetSamples + GPUHistoryCapacity)
                GPUTiming.Add(FPlatformTime::ToMilliseconds64(Cycles));
            else ++GPUOverflowDiscarded;
        }
        if (bRecord)
        {
            if (!bDrained) ++GPUPollLimitHits;
            if (GPUTiming.Values.Num() == Before) ++GPUFramesWithoutCompletion;
        }
    }

    void OnViewportDrawn() { bViewportDrawn = true; }
    void OnWorldCleanup(UWorld* World, bool, bool)
    {
        if (World == SampleWorld.Get()) Abort(TEXT("world_cleanup"));
    }
    void OnEngineExit() { Abort(TEXT("engine_exit")); }

    void OnEndFrame()
    {
        UGameViewportClient* Client = RenderedViewport(SampleWorld.Get());
        if (!Client || Client != SampleViewport.Get()) { Abort(TEXT("viewport_unavailable")); return; }
        if (Client->Viewport->GetSizeXY() != StartSize) { Abort(TEXT("viewport_resized")); return; }
        const double Now = FPlatformTime::Seconds();
        if (Now - StartedAt > MaximumSeconds) { Abort(TEXT("timeout")); return; }
        // EndFrame alone also runs without a viewport draw. Count only frames
        // where this game's viewport reached OnEndDraw; the clock interval
        // includes real stalls between those draws without inventing GPU time.
        if (!bViewportDrawn) return;
        bViewportDrawn = false;
        const double IntervalMS = (Now - PreviousFrameAt) * 1000.0;
        PreviousFrameAt = Now;
        if (WarmupRemaining > 0)
        {
            ReadGPUTimings(false);
            --WarmupRemaining;
            return;
        }
        if (!FMath::IsFinite(IntervalMS) || IntervalMS <= 0) { Abort(TEXT("invalid_clock_interval")); return; }
#if !PLATFORM_IOS
        if (!FPlatformApplicationMisc::IsThisApplicationForeground()) ++BackgroundSamples;
#endif
        Samples.Add(IntervalMS);
        // These are the same raw published counters used by stat unit, not
        // per-thread CPU utilization. Publication is asynchronous; engine waits
        // and dependent work can contribute, and the values are not frame-paired.
        GameTiming.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
        RenderTiming.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime), GIsThreadedRendering);
        RHITiming.Add(FPlatformTime::ToMilliseconds(GRHIThreadTime), IsRunningRHIInSeparateThread());
        ReadGPUTimings(true);
        if (Samples.Num() < TargetSamples) return;

        double TotalMS = 0;
        for (double Sample : Samples) TotalMS += Sample;
        Samples.Sort();
        const int32 Count = Samples.Num();
        const double MedianMS = (Samples[(Count - 1) / 2] + Samples[Count / 2]) * 0.5;
        const int32 P95Index = FMath::Clamp(FMath::CeilToInt(Count * 0.95f) - 1, 0, Count - 1);
        const double MeanMS = TotalMS / Count;
#if PLATFORM_IOS
        const FString BackgroundFrameStatus(TEXT("unavailable"));
#else
        const FString BackgroundFrameStatus = FString::FromInt(BackgroundSamples);
#endif
        UE_LOG(LogCinderQuality, Display,
            TEXT("CINDERLINE_FRAME_PROFILE result map=%s samples=%d warmup_frames=%d viewport_px=%dx%d window_seconds=%.3f mean_ms=%.3f median_ms=%.3f p95_ms=%.3f fps=%.2f foreground=%s background_frames=%s measurement=rendered_viewport_wall_clock gpu_timing=%s"),
            *SampleWorld->GetMapName(), Count, WarmupFrames, StartSize.X, StartSize.Y, TotalMS / 1000.0,
            MeanMS, MedianMS, Samples[P95Index], 1000.0 / MeanMS,
            ForegroundStatus(), *BackgroundFrameStatus,
            GPUTiming.Values.IsEmpty() ? TEXT("unavailable") : TEXT("completed_engine_frames"));
        GameTiming.Log(TEXT("game_thread"), TEXT("GGameThreadTime"), Count, TEXT("positive_latest_published_counter"));
        RenderTiming.Log(TEXT("render_thread"), TEXT("GRenderThreadTime"), Count, TEXT("separate_thread_and_positive_latest_published_counter"));
        RHITiming.Log(TEXT("rhi_thread"), TEXT("GRHIThreadTime"), Count, TEXT("separate_thread_and_positive_latest_published_counter"));
        GPUTiming.Log(TEXT("gpu"), TEXT("FRHIGPUFrameTimeHistory"), Count, TEXT("fresh_positive_completed_gpu_frames"),
            GPUDisjointEvents > 0 || GPUPollLimitHits > 0 || GPUOverflowDiscarded > 0);
        UE_LOG(LogCinderQuality, Display,
            TEXT("CINDERLINE_FRAME_PROFILE_TIMING_SCOPE scope=global_engine cpu_alignment=latest_published gpu_alignment=asynchronous_completed gpu_frames=%d viewport_frames_without_gpu_completion=%d gpu_disjoint_events=%d gpu_poll_limit_hits=%d gpu_overflow_discarded=%d gpu_frame_bubbles_removed=%d render_thread_separate=%d rhi_thread_separate=%d; CPU engine timings may include waits and are not CPU utilization or pure active work. GPU completions can lag, include other engine viewport work, and are not paired with these viewport frames; zero or missing timings are unavailable, not measured zero. No GPU utilization or synchronized frame attribution is inferred."),
            GPUTiming.Values.Num(), GPUFramesWithoutCompletion, GPUDisjointEvents,
            GPUPollLimitHits, GPUOverflowDiscarded, GRHISupportsFrameCyclesBubblesRemoval,
            GIsThreadedRendering, IsRunningRHIInSeparateThread());
        Detach();
    }
};

FQualityFrameSample FrameSample;
TWeakObjectPtr<UWorld> GPUProfileWorld;
FTimerHandle GPUProfileTimer;
FAutoConsoleCommandWithWorldAndArgs SimulationProfileCommand(
    TEXT("cinder.simprofile"), TEXT("DEVELOPMENT: enable, disable or report offline authoritative simulation-step phase timing. Usage: cinder.simprofile [on|off|status]."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (Args.Num() > 1 || (Args.Num() == 1
            && !Args[0].Equals(TEXT("on"), ESearchCase::IgnoreCase)
            && !Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase)
            && !Args[0].Equals(TEXT("status"), ESearchCase::IgnoreCase)))
        {
            UE_LOG(LogCinderQuality, Display,
                TEXT("CINDERLINE_SIM_PROFILE usage: cinder.simprofile [on|off|status]"));
            return;
        }

        ACinderBattlefield* Battle = nullptr;
        if (World && World->IsGameWorld())
        {
            TActorIterator<ACinderBattlefield> It(World);
            if (It) Battle = *It;
        }
        if (!Battle)
        {
            UE_LOG(LogCinderQuality, Display,
                TEXT("CINDERLINE_SIM_PROFILE rejected reason=no_battlefield"));
            return;
        }

        cinder::Simulation& Sim = Battle->Sim();
        const bool bOn = Args.Num() == 1 && Args[0].Equals(TEXT("on"), ESearchCase::IgnoreCase);
        const bool bOff = Args.Num() == 1 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase);
        if (bOn)
        {
            if (Battle->IsOnlineMatch() || Sim.isReplica())
            {
                UE_LOG(LogCinderQuality, Display,
                    TEXT("CINDERLINE_SIM_PROFILE rejected reason=offline_authority_required online=%d replica=%d"),
                    Battle->IsOnlineMatch() ? 1 : 0, Sim.isReplica() ? 1 : 0);
                return;
            }
            Sim.setProfilingEnabled(true);
            UE_LOG(LogCinderQuality, Display,
                TEXT("CINDERLINE_SIM_PROFILE enabled=1 collected=0; the next completed simulation step will replace the cleared sample"));
            return;
        }
        if (bOff)
        {
            Sim.setProfilingEnabled(false);
            UE_LOG(LogCinderQuality, Display,
                TEXT("CINDERLINE_SIM_PROFILE enabled=0 collected=0; sample cleared"));
            return;
        }

        const cinder::SimulationStepProfile& Profile = Sim.lastStepProfile();
        const bool bMenu = Battle->IsMenu();
        const bool bPaused = Battle->IsPaused();
        const bool bHistorical = Profile.collected
            && (bMenu || bPaused || Sim.winner() != -1 || Profile.tick != Sim.tick());
        const TCHAR* Scope = !Profile.collected ? TEXT("unavailable")
            : bHistorical ? TEXT("last_completed_step_historical") : TEXT("last_completed_step");
        UE_LOG(LogCinderQuality, Display,
            TEXT("CINDERLINE_SIM_PROFILE status enabled=%d collected=%d sample_tick=%llu current_tick=%llu menu=%d paused=%d replica=%d winner=%d historical=%d scope=%s setup_ms=%.3f production_ms=%.3f movement_economy_ms=%.3f vision_ms=%.3f combat_ms=%.3f ai_ms=%.3f completion_ms=%.3f total_ms=%.3f; local simulation-step wall-clock durations, not CPU or GPU utilization and not rendered-frame coverage"),
            Sim.profilingEnabled() ? 1 : 0, Profile.collected ? 1 : 0,
            static_cast<unsigned long long>(Profile.tick), static_cast<unsigned long long>(Sim.tick()),
            bMenu ? 1 : 0, bPaused ? 1 : 0, Sim.isReplica() ? 1 : 0, Sim.winner(),
            bHistorical ? 1 : 0, Scope, Profile.setupMs, Profile.productionMs,
            Profile.movementEconomyMs, Profile.visionMs, Profile.combatMs,
            Profile.aiMs, Profile.completionMs, Profile.totalMs);
    }));
FAutoConsoleCommandWithWorldAndArgs GPUProfileCommand(
    TEXT("cinder.gpuprofile"), TEXT("DEVELOPMENT: capture Unreal's GPU pass timing log after 1-60 seconds (default 10), allowing the scene to warm up. Requires a visible game viewport."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        float Delay = 10.0f;
        if (Args.Num() > 1 || (Args.Num() == 1 && !LexTryParseString(Delay, *Args[0]))
            || !FMath::IsFinite(Delay) || Delay < 1.0f || Delay > 60.0f || !RenderedViewport(World))
        {
            UE_LOG(LogCinderQuality, Display, TEXT("CINDERLINE_GPU_PROFILE rejected; requires visible viewport and delay 1-60 seconds"));
            return;
        }
        if (UWorld* Previous = GPUProfileWorld.Get()) Previous->GetTimerManager().ClearTimer(GPUProfileTimer);
        GPUProfileWorld = World;
        World->GetTimerManager().SetTimer(GPUProfileTimer, FTimerDelegate::CreateLambda([WeakWorld = TWeakObjectPtr<UWorld>(World)]
        {
            UWorld* Current = WeakWorld.Get();
            if (!GEngine || !RenderedViewport(Current)) return;
            UE_LOG(LogCinderQuality, Display, TEXT("CINDERLINE_GPU_PROFILE capture map=%s foreground=%s world_rendering_disabled=%d; whole frame including UI; single instrumented frame, not sustained utilization"),
                *Current->GetMapName(), ForegroundStatus(), Current->GetGameViewport()->bDisableWorldRendering);
            LogQuality(Current);
            GEngine->Exec(Current, TEXT("profilegpu"));
        }), Delay, false);
        UE_LOG(LogCinderQuality, Display, TEXT("CINDERLINE_GPU_PROFILE scheduled delay_seconds=%.1f"), Delay);
    }));
FAutoConsoleCommandWithWorld QualityCommand(
    TEXT("cinder.quality"), TEXT("DEVELOPMENT: log viewport pixels, DPI state and effective rendering CVars without changing settings."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&LogQuality));
FAutoConsoleCommandWithWorldAndArgs ProfileCommand(
    TEXT("cinder.profile"), TEXT("DEVELOPMENT: sample 120, 180 (default), 600, 1200, 9000 or 18000 rendered frames after 30 warmups; the two largest cover ten minutes at 15/30 FPS. cinder.profile cancel stops."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (Args.Num() == 1 && Args[0].Equals(TEXT("cancel"), ESearchCase::IgnoreCase)) { FrameSample.Cancel(); return; }
        if (Args.Num() > 1 || (Args.Num() == 1 && Args[0] != TEXT("120") && Args[0] != TEXT("180")
            && Args[0] != TEXT("600") && Args[0] != TEXT("1200")
            && Args[0] != TEXT("9000") && Args[0] != TEXT("18000")))
        {
            UE_LOG(LogCinderQuality, Display, TEXT("CINDERLINE_FRAME_PROFILE usage: cinder.profile [120|180|600|1200|9000|18000|cancel]"));
            return;
        }
        FrameSample.Start(World, Args.Num() == 1 ? FCString::Atoi(*Args[0]) : 180);
    }));
}

#endif
