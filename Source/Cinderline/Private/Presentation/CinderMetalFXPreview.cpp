#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GPUProfiler.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderPlayerController.h"
#include "RenderingThread.h"
#include "TimerManager.h"
#include "UnrealClient.h"

#if PLATFORM_MAC || PLATFORM_IOS
#include "CinderMetalFX.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCinderMetalFXPreview, Log, All);

namespace
{
UGameViewportClient* PreviewViewport(UWorld* World)
{
    if (!World || !World->IsGameWorld() || !FApp::CanEverRender() || IsRunningCommandlet()) return nullptr;
    UGameViewportClient* Client = World->GetGameViewport();
    if (!Client || Client->GetWorld() != World || !Client->Viewport) return nullptr;
    const FIntPoint Size = Client->Viewport->GetSizeXY();
    return Size.X > 0 && Size.Y > 0 ? Client : nullptr;
}

bool EmitMetalFXStatus(UWorld* World, const TCHAR* Phase)
{
    UE_LOG(LogCinderMetalFXPreview, Display,
        TEXT("CINDERLINE_METALFX_STATUS phase=%s dispatch=begin"), Phase);
#if PLATFORM_MAC || PLATFORM_IOS
    const bool bProcessed = IConsoleManager::Get().ProcessUserConsoleInput(
        TEXT("r.CinderMetalFX.Status"), *GLog, World);
#else
    const bool bProcessed = false;
#endif
    UE_LOG(LogCinderMetalFXPreview, Display,
        TEXT("CINDERLINE_METALFX_STATUS phase=%s command_processed=%d"), Phase, bProcessed);
    return bProcessed;
}

class FMetalFXPreviewSample
{
public:
    ~FMetalFXPreviewSample()
    {
        if (bActive) Abort(TEXT("shutdown"));
    }

    void Start(UWorld* World, bool bEnable, int32 Frames, bool bReturnToMenu)
    {
        if (bActive)
        {
            UE_LOG(LogCinderMetalFXPreview, Warning,
                TEXT("CINDERLINE_METALFX_PREVIEW refused=already_active"));
            return;
        }
        auto* PC = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        UGameViewportClient* Client = PreviewViewport(World);
        IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CinderMetalFX.Enabled"));
        IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(
            TEXT("r.CinderMetalFX.ScreenPercentage"));
        const IConsoleVariable* TAAUpsampling = IConsoleManager::Get().FindConsoleVariable(
            TEXT("r.TemporalAA.Upsampling"));
        if (!FApp::IsUnattended() || !PC || !Battle || !Battle->IsMenu() || Battle->IsOnlineMatch()
            || PC->IsHelpOpen() || !Client)
        {
            UE_LOG(LogCinderMetalFXPreview, Warning,
                TEXT("CINDERLINE_METALFX_PREVIEW refused=requires_unattended_fresh_local_rendered_menu"));
            return;
        }
        if (!Enabled || !ScreenPercentage || !TAAUpsampling)
        {
            UE_LOG(LogCinderMetalFXPreview, Warning,
                TEXT("CINDERLINE_METALFX_PREVIEW refused=required_cvar_unavailable"));
            return;
        }
#if PLATFORM_MAC
        if (TAAUpsampling->GetInt() != 0)
        {
            UE_LOG(LogCinderMetalFXPreview, Warning,
                TEXT("CINDERLINE_METALFX_PREVIEW refused=mac_requires_taa_upsampling_0 actual=%d"),
                TAAUpsampling->GetInt());
            return;
        }
#endif

        SampleWorld = World;
        SampleViewport = Client;
        TargetFrames = Frames;
        bRequestedEnabled = bEnable;
        bMenuProbe = bReturnToMenu;
        OriginalEnabled = Enabled->GetString();
        OriginalScreenPercentage = ScreenPercentage->GetString();
        Enabled->SetWithCurrentPriority(bEnable ? 1 : 0);
        ScreenPercentage->SetWithCurrentPriority(80);
        // Establish a clean diagnostics boundary: no encode submitted under the
        // previous setting can complete after the start counter is logged.
        FlushRenderingCommands();
        bSettingsModified = true;
        bActive = true;
        StartedAt = FPlatformTime::Seconds();
        CleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FMetalFXPreviewSample::OnWorldCleanup);
        ExitHandle = FCoreDelegates::OnEnginePreExit.AddRaw(this, &FMetalFXPreviewSample::OnEngineExit);

        EmitMetalFXStatus(World, TEXT("start"));
        if (!GEngine || !GEngine->Exec(World, TEXT("cinder.commandpreview army")) || Battle->IsMenu())
        {
            Abort(TEXT("fixture_start_failed"));
            return;
        }
        UE_LOG(LogCinderMetalFXPreview, Display,
            TEXT("CINDERLINE_METALFX_PREVIEW started mode=%s fixture=army settle_seconds=3.0 warmup_frames=%d sample_frames=%d menu_probe=%d"),
            Mode(), WarmupFrameCount, TargetFrames, bMenuProbe);
        World->GetTimerManager().SetTimer(SettleTimer,
            FTimerDelegate::CreateRaw(this, &FMetalFXPreviewSample::BeginSampling), 3.0f, false);
    }

private:
    static constexpr int32 WarmupFrameCount = 30;
    static constexpr int32 GPUMaxPopsPerFrame = 32;
    static constexpr double TimeoutSeconds = 45.0;

    TWeakObjectPtr<UWorld> SampleWorld;
    TWeakObjectPtr<UGameViewportClient> SampleViewport;
    FTimerHandle SettleTimer;
    FTimerHandle CaptureTimer;
    FDelegateHandle DrawHandle;
    FDelegateHandle EndFrameHandle;
    FDelegateHandle CleanupHandle;
    FDelegateHandle ExitHandle;
    FDelegateHandle ScreenshotHandle;
    FRHIGPUFrameTimeHistory::FState GPUHistory;
    TArray<double> GPUTimings;
    FString OriginalEnabled;
    FString OriginalScreenPercentage;
    FString ScreenshotPath;
    FIntPoint ViewportSize = FIntPoint::ZeroValue;
    FIntPoint RenderTargetSize = FIntPoint::ZeroValue;
    int32 TargetFrames = 120;
    int32 WarmupRemaining = WarmupFrameCount;
    int32 SampledFrames = 0;
    int32 GPUDisjointEvents = 0;
    double StartedAt = 0;
    bool bActive = false;
    bool bSettingsModified = false;
    bool bRequestedEnabled = true;
    bool bMenuProbe = false;
    bool bViewportDrawn = false;
    bool bStatusEmitted = false;
    bool bWorldRenderingDisabled = false;

    const TCHAR* Mode() const { return bRequestedEnabled ? TEXT("on") : TEXT("off"); }

    void BeginSampling()
    {
        UWorld* World = SampleWorld.Get();
        UGameViewportClient* Client = PreviewViewport(World);
        auto* PC = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        if (!bActive || !Client || Client != SampleViewport.Get() || !Battle || Battle->IsMenu())
        {
            Abort(TEXT("fixture_or_viewport_unavailable"));
            return;
        }
        ViewportSize = Client->Viewport->GetSizeXY();
        RenderTargetSize = Client->Viewport->GetRenderTargetTextureSizeXY();
        WarmupRemaining = WarmupFrameCount;
        SampledFrames = 0;
        GPUDisjointEvents = 0;
        GPUTimings.Reset(TargetFrames + 16);
        GPUHistory = FRHIGPUFrameTimeHistory::FState{};
        DrawHandle = Client->OnEndDraw().AddRaw(this, &FMetalFXPreviewSample::OnViewportDrawn);
        EndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FMetalFXPreviewSample::OnEndFrame);
        UE_LOG(LogCinderMetalFXPreview, Display,
            TEXT("CINDERLINE_METALFX_PREVIEW sampling viewport_px=%dx%d render_target_px=%dx%d"),
            ViewportSize.X, ViewportSize.Y, RenderTargetSize.X, RenderTargetSize.Y);
    }

    void OnViewportDrawn() { bViewportDrawn = true; }

    void DrainGPUTimings(bool bRecord)
    {
        for (int32 Pop = 0; Pop < GPUMaxPopsPerFrame; ++Pop)
        {
            uint64 Cycles = 0;
            const auto Result = GPUHistory.PopFrameCycles(Cycles);
            if (Result == FRHIGPUFrameTimeHistory::EResult::Empty) break;
            if (!bRecord) continue;
            if (Result == FRHIGPUFrameTimeHistory::EResult::Disjoint) ++GPUDisjointEvents;
            const double Milliseconds = FPlatformTime::ToMilliseconds64(Cycles);
            if (FMath::IsFinite(Milliseconds) && Milliseconds > 0) GPUTimings.Add(Milliseconds);
        }
    }

    void OnEndFrame()
    {
        UGameViewportClient* Client = PreviewViewport(SampleWorld.Get());
        if (!bActive || !Client || Client != SampleViewport.Get())
        {
            Abort(TEXT("viewport_unavailable"));
            return;
        }
        if (Client->Viewport->GetSizeXY() != ViewportSize)
        {
            Abort(TEXT("viewport_resized"));
            return;
        }
        if (FPlatformTime::Seconds() - StartedAt > TimeoutSeconds)
        {
            Abort(TEXT("timeout"));
            return;
        }
        if (!bViewportDrawn) return;
        bViewportDrawn = false;
        if (WarmupRemaining > 0)
        {
            DrainGPUTimings(false);
            --WarmupRemaining;
            return;
        }
        ++SampledFrames;
        DrainGPUTimings(true);
        if (SampledFrames >= TargetFrames) FinishSampling();
    }

    void FinishSampling()
    {
        RemoveFrameDelegates();
        if (bMenuProbe)
        {
            UWorld* World = SampleWorld.Get();
            auto* PC = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
            if (!PC)
            {
                Abort(TEXT("menu_controller_unavailable"));
                return;
            }
            PC->ExecuteAction(TEXT("menu"));
            World->GetTimerManager().SetTimer(CaptureTimer,
                FTimerDelegate::CreateRaw(this, &FMetalFXPreviewSample::Capture), 0.5f, false);
            return;
        }
        Capture();
    }

    void Capture()
    {
        UWorld* World = SampleWorld.Get();
        UGameViewportClient* Client = PreviewViewport(World);
        if (!bActive || !Client || Client != SampleViewport.Get())
        {
            Abort(TEXT("capture_viewport_unavailable"));
            return;
        }
        RenderTargetSize = Client->Viewport->GetRenderTargetTextureSizeXY();
        bWorldRenderingDisabled = Client->bDisableWorldRendering;
        if (GEngine) GEngine->Exec(World, TEXT("cinder.quality"));
        bStatusEmitted = EmitMetalFXStatus(World, TEXT("finish"));

        const FString Directory = FPaths::ProjectSavedDir() / TEXT("MetalFX");
        IFileManager::Get().MakeDirectory(*Directory, true);
        ScreenshotPath = FPaths::ConvertRelativePathToFull(
            Directory / FString::Printf(TEXT("%s-%dx%d.png"),
                Mode(), ViewportSize.X, ViewportSize.Y));
        if (FScreenshotRequest::IsScreenshotRequested())
        {
            Abort(TEXT("screenshot_request_busy"));
            return;
        }
        ScreenshotHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddRaw(
            this, &FMetalFXPreviewSample::OnScreenshotProcessed);
        FScreenshotRequest::RequestScreenshot(ScreenshotPath, false, false);
    }

    void OnScreenshotProcessed()
    {
        if (!bActive) return;
        TArray<double> Ordered = GPUTimings;
        Ordered.Sort();
        double Total = 0;
        for (double Value : Ordered) Total += Value;
        const int32 Count = Ordered.Num();
        const double Mean = Count > 0 ? Total / Count : 0;
        const double Median = Count > 0
            ? (Ordered[(Count - 1) / 2] + Ordered[Count / 2]) * 0.5 : 0;
        const int32 P95Index = Count > 0
            ? FMath::Clamp(FMath::CeilToInt(Count * 0.95f) - 1, 0, Count - 1) : 0;
        const double P95 = Count > 0 ? Ordered[P95Index] : 0;
        const IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(
            TEXT("r.CinderMetalFX.Enabled"));
        const IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(
            TEXT("r.CinderMetalFX.ScreenPercentage"));
        const IConsoleVariable* TAAUpsampling = IConsoleManager::Get().FindConsoleVariable(
            TEXT("r.TemporalAA.Upsampling"));
        UE_LOG(LogCinderMetalFXPreview, Display,
            TEXT("CINDERLINE_METALFX_PREVIEW result mode=%s enabled=%d screen_percentage=%d taa_upsampling=%d fixture=army warmup_frames=%d sample_frames=%d gpu_samples=%d gpu_mean_ms=%.3f gpu_median_ms=%.3f gpu_p95_ms=%.3f gpu_disjoint=%d viewport_px=%dx%d render_target_px=%dx%d world_rendering_disabled=%d menu_probe=%d status_emitted=%d elapsed_seconds=%.3f screenshot=%s"),
            Mode(), Enabled ? Enabled->GetInt() : -1,
            ScreenPercentage ? ScreenPercentage->GetInt() : -1,
            TAAUpsampling ? TAAUpsampling->GetInt() : -1,
            WarmupFrameCount, SampledFrames, Count, Mean, Median, P95, GPUDisjointEvents,
            ViewportSize.X, ViewportSize.Y, RenderTargetSize.X, RenderTargetSize.Y,
            bWorldRenderingDisabled, bMenuProbe, bStatusEmitted,
            FPlatformTime::Seconds() - StartedAt, *ScreenshotPath);
        Complete();
    }

    void RemoveFrameDelegates()
    {
        if (DrawHandle.IsValid())
        {
            if (UGameViewportClient* Client = SampleViewport.Get()) Client->OnEndDraw().Remove(DrawHandle);
            DrawHandle.Reset();
        }
        if (EndFrameHandle.IsValid())
        {
            FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
            EndFrameHandle.Reset();
        }
        bViewportDrawn = false;
    }

    void RestoreSettings()
    {
        if (!bSettingsModified) return;
        if (IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(
            TEXT("r.CinderMetalFX.Enabled")))
            Enabled->SetWithCurrentPriority(*OriginalEnabled);
        if (IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(
            TEXT("r.CinderMetalFX.ScreenPercentage")))
            ScreenPercentage->SetWithCurrentPriority(*OriginalScreenPercentage);
        bSettingsModified = false;
    }

    void Detach()
    {
        RemoveFrameDelegates();
        if (UWorld* World = SampleWorld.Get())
        {
            World->GetTimerManager().ClearTimer(SettleTimer);
            World->GetTimerManager().ClearTimer(CaptureTimer);
        }
        if (CleanupHandle.IsValid()) FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
        if (ExitHandle.IsValid()) FCoreDelegates::OnEnginePreExit.Remove(ExitHandle);
        if (ScreenshotHandle.IsValid()) FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);
        CleanupHandle.Reset();
        ExitHandle.Reset();
        ScreenshotHandle.Reset();
        RestoreSettings();
        SampleWorld.Reset();
        SampleViewport.Reset();
        bActive = false;
    }

    void Complete() { Detach(); }

    void Abort(const TCHAR* Reason)
    {
        if (!bActive) return;
        UE_LOG(LogCinderMetalFXPreview, Error,
            TEXT("CINDERLINE_METALFX_PREVIEW aborted reason=%s sampled_frames=%d/%d"),
            Reason, SampledFrames, TargetFrames);
        Detach();
    }

    void OnWorldCleanup(UWorld* World, bool, bool)
    {
        if (World == SampleWorld.Get()) Abort(TEXT("world_cleanup"));
    }

    void OnEngineExit() { Abort(TEXT("engine_exit")); }
};

#if PLATFORM_MAC || PLATFORM_IOS
class FMetalFXMenuPreview
{
public:
    ~FMetalFXMenuPreview()
    {
        if (bActive) Abort(TEXT("shutdown"));
    }

    void Start(UWorld* World)
    {
        if (bActive)
        {
            UE_LOG(LogCinderMetalFXPreview, Warning,
                TEXT("CINDERLINE_METALFX_MENU_PREVIEW refused=already_active"));
            return;
        }
        auto* PC = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        if (FApp::IsUnattended() || !PC || !Battle || !Battle->IsMenu() || Battle->IsOnlineMatch()
            || PC->IsHelpOpen()
            || !PreviewViewport(World) || !ICinderMetalFXModule::IsAvailable())
        {
            UE_LOG(LogCinderMetalFXPreview, Warning,
                TEXT("CINDERLINE_METALFX_MENU_PREVIEW refused=requires_normal_local_rendered_menu"));
            return;
        }
        bActive = true;
        MenuWorld = World;
        StartedAt = FPlatformTime::Seconds();
        CleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FMetalFXMenuPreview::OnWorldCleanup);
        ExitHandle = FCoreDelegates::OnEnginePreExit.AddRaw(this, &FMetalFXMenuPreview::OnEngineExit);
        World->GetTimerManager().SetTimer(BeginTimer,
            FTimerDelegate::CreateRaw(this, &FMetalFXMenuPreview::BeginObservation), 1.0f, false);
        UE_LOG(LogCinderMetalFXPreview, Display,
            TEXT("CINDERLINE_METALFX_MENU_PREVIEW scheduled settle_seconds=1.0 observation_seconds=5.0"));
    }

private:
    TWeakObjectPtr<UWorld> MenuWorld;
    FTimerHandle BeginTimer;
    FTimerHandle FinishTimer;
    FDelegateHandle CleanupHandle;
    FDelegateHandle ExitHandle;
    FDelegateHandle ScreenshotHandle;
    FString ScreenshotPath;
    FIntPoint ViewportSize = FIntPoint::ZeroValue;
    FIntPoint RenderTargetSize = FIntPoint::ZeroValue;
    uint64 StartEncodedFrames = 0;
    uint64 FinishEncodedFrames = 0;
    double StartedAt = 0;
    bool bActive = false;
    bool bStatusEmitted = false;

    bool ValidateMenu(UGameViewportClient*& OutClient) const
    {
        UWorld* World = MenuWorld.Get();
        auto* PC = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        OutClient = PreviewViewport(World);
        return PC && Battle && Battle->IsMenu() && !Battle->IsOnlineMatch() && OutClient
            && OutClient->bDisableWorldRendering;
    }

    void BeginObservation()
    {
        UGameViewportClient* Client = nullptr;
        if (!bActive || !ValidateMenu(Client))
        {
            Abort(TEXT("menu_world_rendering_not_disabled"));
            return;
        }
        FlushRenderingCommands();
        StartEncodedFrames = ICinderMetalFXModule::Get().GetDiagnostics().EncodedFrames;
        EmitMetalFXStatus(MenuWorld.Get(), TEXT("menu_start"));
        MenuWorld->GetTimerManager().SetTimer(FinishTimer,
            FTimerDelegate::CreateRaw(this, &FMetalFXMenuPreview::FinishObservation), 5.0f, false);
    }

    void FinishObservation()
    {
        UGameViewportClient* Client = nullptr;
        UWorld* World = MenuWorld.Get();
        if (!bActive || !ValidateMenu(Client))
        {
            Abort(TEXT("menu_or_world_rendering_changed"));
            return;
        }
        FlushRenderingCommands();
        FinishEncodedFrames = ICinderMetalFXModule::Get().GetDiagnostics().EncodedFrames;
        ViewportSize = Client->Viewport->GetSizeXY();
        RenderTargetSize = Client->Viewport->GetRenderTargetTextureSizeXY();
        if (GEngine) GEngine->Exec(World, TEXT("cinder.quality"));
        bStatusEmitted = EmitMetalFXStatus(World, TEXT("menu_finish"));
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("MetalFX");
        IFileManager::Get().MakeDirectory(*Directory, true);
        ScreenshotPath = FPaths::ConvertRelativePathToFull(
            Directory / FString::Printf(TEXT("menu-%dx%d.png"), ViewportSize.X, ViewportSize.Y));
        if (FScreenshotRequest::IsScreenshotRequested())
        {
            Abort(TEXT("screenshot_request_busy"));
            return;
        }
        ScreenshotHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddRaw(
            this, &FMetalFXMenuPreview::OnScreenshotProcessed);
        FScreenshotRequest::RequestScreenshot(ScreenshotPath, false, false);
    }

    void OnScreenshotProcessed()
    {
        if (!bActive) return;
        const uint64 EncodedDelta = FinishEncodedFrames >= StartEncodedFrames
            ? FinishEncodedFrames - StartEncodedFrames : MAX_uint64;
        const bool bPassed = EncodedDelta == 0;
        UE_LOG(LogCinderMetalFXPreview, Display,
            TEXT("CINDERLINE_METALFX_MENU_PREVIEW result passed=%d menu=1 observation_seconds=5.0 encoded_start=%llu encoded_finish=%llu encoded_delta=%llu viewport_px=%dx%d render_target_px=%dx%d world_rendering_disabled=1 status_emitted=%d elapsed_seconds=%.3f screenshot=%s"),
            bPassed, StartEncodedFrames, FinishEncodedFrames, EncodedDelta,
            ViewportSize.X, ViewportSize.Y, RenderTargetSize.X, RenderTargetSize.Y,
            bStatusEmitted, FPlatformTime::Seconds() - StartedAt, *ScreenshotPath);
        if (!bPassed)
        {
            UE_LOG(LogCinderMetalFXPreview, Error,
                TEXT("CINDERLINE_METALFX_MENU_PREVIEW failed=encoding_advanced_while_world_disabled"));
        }
        Detach();
    }

    void Detach()
    {
        if (UWorld* World = MenuWorld.Get())
        {
            World->GetTimerManager().ClearTimer(BeginTimer);
            World->GetTimerManager().ClearTimer(FinishTimer);
        }
        if (CleanupHandle.IsValid()) FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
        if (ExitHandle.IsValid()) FCoreDelegates::OnEnginePreExit.Remove(ExitHandle);
        if (ScreenshotHandle.IsValid()) FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);
        CleanupHandle.Reset();
        ExitHandle.Reset();
        ScreenshotHandle.Reset();
        MenuWorld.Reset();
        bActive = false;
    }

    void Abort(const TCHAR* Reason)
    {
        if (!bActive) return;
        UE_LOG(LogCinderMetalFXPreview, Error,
            TEXT("CINDERLINE_METALFX_MENU_PREVIEW aborted reason=%s"), Reason);
        Detach();
    }

    void OnWorldCleanup(UWorld* World, bool, bool)
    {
        if (World == MenuWorld.Get()) Abort(TEXT("world_cleanup"));
    }

    void OnEngineExit() { Abort(TEXT("engine_exit")); }
};
#endif

FMetalFXPreviewSample MetalFXPreviewSample;
#if PLATFORM_MAC || PLATFORM_IOS
FMetalFXMenuPreview MetalFXMenuPreview;
#endif

FAutoConsoleCommandWithWorldAndArgs MetalFXPreviewCommand(
    TEXT("cinder.metalfxpreview"),
    TEXT("DEVELOPMENT: unattended fresh local menu only. Usage: cinder.metalfxpreview [on|off] [120|180] [menu]. Builds the army fixture, samples completed GPU frame timings, logs MetalFX status and captures Saved/MetalFX/<mode>-<viewport>.png."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        bool bEnabled = true;
        bool bMenuProbe = false;
        int32 Frames = 120;
        bool bModeSeen = false;
        bool bFramesSeen = false;
        bool bValid = Args.Num() <= 3;
        for (const FString& RawArg : Args)
        {
            const FString Arg = RawArg.ToLower();
            if (!bModeSeen && (Arg == TEXT("on") || Arg == TEXT("off")))
            {
                bEnabled = Arg == TEXT("on");
                bModeSeen = true;
            }
            else if (!bFramesSeen && (Arg == TEXT("120") || Arg == TEXT("180")))
            {
                Frames = FCString::Atoi(*Arg);
                bFramesSeen = true;
            }
            else if (!bMenuProbe && Arg == TEXT("menu")) bMenuProbe = true;
            else bValid = false;
        }
        if (!bValid)
        {
            UE_LOG(LogCinderMetalFXPreview, Warning,
                TEXT("CINDERLINE_METALFX_PREVIEW usage=cinder.metalfxpreview_[on|off]_[120|180]_[menu]"));
            return;
        }
        MetalFXPreviewSample.Start(World, bEnabled, Frames, bMenuProbe);
    }));
#if PLATFORM_MAC || PLATFORM_IOS
FAutoConsoleCommandWithWorld MetalFXMenuPreviewCommand(
    TEXT("cinder.metalfxmenupreview"),
    TEXT("DEVELOPMENT: normal local menu only. Read-only five-second check that world-rendering suppression produces no new MetalFX encodes, then logs quality/status and captures Saved/MetalFX/menu-<viewport>.png."),
    FConsoleCommandWithWorldDelegate::CreateRaw(&MetalFXMenuPreview, &FMetalFXMenuPreview::Start));
#endif
}

#endif
