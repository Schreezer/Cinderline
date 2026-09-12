#include "Presentation/CinderGameEngine.h"

#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Misc/App.h"
#include "Widgets/SWindow.h"

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
DEFINE_LOG_CATEGORY_STATIC(LogCinderFramePacing, Log, All);

namespace
{
const TCHAR* FrameStateName(ECinderFramePacingState State)
{
    switch (State)
    {
    case ECinderFramePacingState::Gameplay: return TEXT("gameplay");
    case ECinderFramePacingState::Idle: return TEXT("idle");
    case ECinderFramePacingState::Background: return TEXT("background");
    default: return TEXT("bypass");
    }
}
}
#endif

float UCinderGameEngine::ApplyStateCap(float EngineLimit, ECinderFramePacingState State)
{
    float StateLimit = 0;
    switch (State)
    {
    case ECinderFramePacingState::Gameplay: StateLimit = 120; break;
    case ECinderFramePacingState::Idle: StateLimit = 30; break;
    case ECinderFramePacingState::Background: StateLimit = 10; break;
    default: return EngineLimit;
    }
    return EngineLimit > 0 ? FMath::Min(EngineLimit, StateLimit) : StateLimit;
}

ECinderFramePacingState UCinderGameEngine::ClassifyMatchState(const ACinderBattlefield* Battlefield, bool bForeground)
{
    if (!bForeground) return ECinderFramePacingState::Background;
    if (IsValid(Battlefield) && !Battlefield->IsMenu() && !Battlefield->IsPaused() && Battlefield->Sim().winner() < 0)
        return ECinderFramePacingState::Gameplay;
    return ECinderFramePacingState::Idle;
}

ECinderFramePacingState UCinderGameEngine::GetFramePacingState() const
{
#if PLATFORM_DESKTOP
    // Keep editor/PIE, automation, offscreen rendering and explicit timing modes
    // on the engine's existing policy. Mobile frame pacing is configured separately.
    if (GIsEditor || IsRunningCommandlet() || IsRunningDedicatedServer() || FApp::IsUnattended()
        || FApp::IsBenchmarking() || FApp::UseFixedTimeStep() || bUseFixedFrameRate
        || !FApp::CanEverRender() || !FSlateApplication::IsInitialized())
        return ECinderFramePacingState::Bypass;
    if (FSlateApplication::Get().IsRenderingOffScreen() || !GameViewport)
        return ECinderFramePacingState::Bypass;

    UWorld* World = GameViewport->GetWorld();
    if (World && (World->WorldType != EWorldType::Game || World->GetNetMode() != NM_Standalone))
        return ECinderFramePacingState::Bypass;
    const TSharedPtr<SWindow> Window = GameViewport->GetWindow();
    if (!Window.IsValid()) return ECinderFramePacingState::Bypass;
    const TSharedPtr<FGenericWindow> NativeWindow = Window->GetNativeWindow();
    if (!NativeWindow.IsValid()) return ECinderFramePacingState::Bypass;

    const bool bForeground = FSlateApplication::Get().IsActive() && Window->IsActive() && !NativeWindow->IsMinimized();
    const auto* Controller = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
    const ACinderBattlefield* Battlefield = Controller ? Controller->Battlefield() : nullptr;
    if (Battlefield && Battlefield->GetWorld() != World) Battlefield = nullptr;
    return ClassifyMatchState(Battlefield, bForeground);
#else
    return ECinderFramePacingState::Bypass;
#endif
}

float UCinderGameEngine::GetMaxTickRate(float DeltaTime, bool bAllowFrameRateSmoothing) const
{
    const float EngineLimit = Super::GetMaxTickRate(DeltaTime, bAllowFrameRateSmoothing);
    const ECinderFramePacingState State = GetFramePacingState();
    const float EffectiveLimit = ApplyStateCap(EngineLimit, State);
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
