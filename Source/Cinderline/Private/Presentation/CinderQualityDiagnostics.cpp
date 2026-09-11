#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

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
        TEXT("CINDERLINE_QUALITY map=%s viewport_px=%dx%d render_target_px=%dx%d dpi_scale=%.3f high_dpi_active=%d foreground=%d rendered_viewport_available=%d"),
        World ? *World->GetMapName() : TEXT("none"), Size.X, Size.Y, TargetSize.X, TargetSize.Y,
        Client ? Client->GetDPIScale() : 0.0f, HighDPIActive,
        FPlatformApplicationMisc::IsThisApplicationForeground(), RenderedViewport(World) != nullptr);

    static const TCHAR* Names[] = {
        TEXT("EnableHighDPIAwareness"), TEXT("r.AntiAliasingMethod"),
        TEXT("r.ScreenPercentage"), TEXT("r.SecondaryScreenPercentage.GameViewport"),
        TEXT("r.DynamicRes.OperationMode"), TEXT("r.TSR.History.ScreenPercentage"),
        TEXT("r.ReflectionMethod"), TEXT("r.SSR.Quality"), TEXT("r.SSR.HalfResSceneColor"),
        TEXT("r.DefaultFeature.Bloom"), TEXT("r.BloomQuality"),
        TEXT("r.DefaultFeature.AmbientOcclusion"), TEXT("r.AmbientOcclusionLevels"),
        TEXT("r.AmbientOcclusionMaxQuality"), TEXT("r.MaxAnisotropy"),
        TEXT("r.ShadowQuality"), TEXT("r.Shadow.MaxCSMResolution"), TEXT("r.Shadow.MaxResolution"),
        TEXT("r.Shadow.CSM.MaxCascades"), TEXT("r.Tonemapper.Sharpen"),
        TEXT("r.VSync"), TEXT("t.MaxFPS"), TEXT("r.MotionBlurQuality"),
        TEXT("r.DepthOfFieldQuality"), TEXT("r.SceneColorFringeQuality")
    };
    for (const TCHAR* Name : Names)
    {
        const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        UE_LOG(LogCinderQuality, Display, TEXT("CINDERLINE_QUALITY_CVAR name=%s value=%s set_by=%s"),
            Name, Variable ? *Variable->GetString() : TEXT("unavailable"),
            Variable ? GetConsoleVariableSetByName(Variable->GetFlags()) : TEXT("unavailable"));
    }
}

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
        StartedAt = FPlatformTime::Seconds();
        PreviousFrameAt = 0;
        bViewportDrawn = false;
        StartSize = Client->Viewport->GetSizeXY();
        DrawHandle = Client->OnEndDraw().AddRaw(this, &FQualityFrameSample::OnViewportDrawn);
        EndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FQualityFrameSample::OnEndFrame);
        CleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FQualityFrameSample::OnWorldCleanup);
        ExitHandle = FCoreDelegates::OnEnginePreExit.AddRaw(this, &FQualityFrameSample::OnEngineExit);
        UE_LOG(LogCinderQuality, Display,
            TEXT("CINDERLINE_FRAME_PROFILE started map=%s warmup_frames=%d sample_frames=%d viewport_px=%dx%d; measures rendered-viewport wall-clock cadence, not GPU timing"),
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
    static constexpr double MaximumSeconds = 120.0;
    TWeakObjectPtr<UWorld> SampleWorld;
    TWeakObjectPtr<UGameViewportClient> SampleViewport;
    FDelegateHandle DrawHandle, EndFrameHandle, CleanupHandle, ExitHandle;
    TArray<double> Samples;
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
        if (WarmupRemaining > 0) { --WarmupRemaining; return; }
        if (!FMath::IsFinite(IntervalMS) || IntervalMS <= 0) { Abort(TEXT("invalid_clock_interval")); return; }
        if (!FPlatformApplicationMisc::IsThisApplicationForeground()) ++BackgroundSamples;
        Samples.Add(IntervalMS);
        if (Samples.Num() < TargetSamples) return;

        double TotalMS = 0;
        for (double Sample : Samples) TotalMS += Sample;
        Samples.Sort();
        const int32 Count = Samples.Num();
        const double MedianMS = (Samples[(Count - 1) / 2] + Samples[Count / 2]) * 0.5;
        const int32 P95Index = FMath::Clamp(FMath::CeilToInt(Count * 0.95f) - 1, 0, Count - 1);
        const double MeanMS = TotalMS / Count;
        UE_LOG(LogCinderQuality, Display,
            TEXT("CINDERLINE_FRAME_PROFILE result map=%s samples=%d warmup_frames=%d viewport_px=%dx%d window_seconds=%.3f mean_ms=%.3f median_ms=%.3f p95_ms=%.3f fps=%.2f foreground=%d background_frames=%d measurement=rendered_viewport_wall_clock gpu_timing=unavailable"),
            *SampleWorld->GetMapName(), Count, WarmupFrames, StartSize.X, StartSize.Y, TotalMS / 1000.0,
            MeanMS, MedianMS, Samples[P95Index], 1000.0 / MeanMS,
            FPlatformApplicationMisc::IsThisApplicationForeground(), BackgroundSamples);
        Detach();
    }
};

FQualityFrameSample FrameSample;
FAutoConsoleCommandWithWorld QualityCommand(
    TEXT("cinder.quality"), TEXT("DEVELOPMENT: log viewport pixels, DPI state and effective rendering CVars without changing settings."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&LogQuality));
FAutoConsoleCommandWithWorldAndArgs ProfileCommand(
    TEXT("cinder.profile"), TEXT("DEVELOPMENT: sample 180 (or 120) rendered frames after 30 warmup frames; cinder.profile cancel stops the active sample. Reports wall-clock cadence, not GPU time."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (Args.Num() == 1 && Args[0].Equals(TEXT("cancel"), ESearchCase::IgnoreCase)) { FrameSample.Cancel(); return; }
        if (Args.Num() > 1 || (Args.Num() == 1 && Args[0] != TEXT("120") && Args[0] != TEXT("180")))
        {
            UE_LOG(LogCinderQuality, Display, TEXT("CINDERLINE_FRAME_PROFILE usage: cinder.profile [120|180|cancel]"));
            return;
        }
        FrameSample.Start(World, Args.Num() == 1 && Args[0] == TEXT("120") ? 120 : 180);
    }));
}

#endif
