#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderHUD.h"
#include "Presentation/CinderPlayerController.h"
#include "Tests/CinderGuidedTestDriver.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderTutorialCoachPreview, Log, All);

namespace
{
struct FCoachPreviewCase
{
    const TCHAR* Name;
    ECinderTutorialStep Step;
    int32 GuideIndex;
    bool bOffscreenRecovery = false;
};

constexpr FCoachPreviewCase CoachCases[] = {
    {TEXT("worker-pointer"), ECinderTutorialStep::SelectWorker, 1},
    {TEXT("mining-pointer"), ECinderTutorialStep::GatherOre, 2},
    {TEXT("build-open"), ECinderTutorialStep::BuildKiln, 1},
    {TEXT("build-choice"), ECinderTutorialStep::BuildKiln, 2},
    {TEXT("build-placement"), ECinderTutorialStep::BuildKiln, 3},
    {TEXT("build-wait"), ECinderTutorialStep::BuildKiln, 4},
    {TEXT("train-open"), ECinderTutorialStep::TrainWorker, 1},
    {TEXT("train-portrait"), ECinderTutorialStep::TrainWorker, 2},
    {TEXT("train-quantity"), ECinderTutorialStep::TrainWorker, 3},
    {TEXT("train-queue"), ECinderTutorialStep::TrainWorker, 4},
    {TEXT("train-wait"), ECinderTutorialStep::TrainWorker, 6},
    {TEXT("research-button"), ECinderTutorialStep::ResearchWeapons, 2},
    {TEXT("rally-chain"), ECinderTutorialStep::Reinforce, 4},
    {TEXT("scout-chain"), ECinderTutorialStep::Scout, 9},
    {TEXT("offscreen-recovery"), ECinderTutorialStep::GatherOre, 1, true},
};

struct FCoachPreviewState
{
    FString Name;
    FCoachPreviewCase Plan{};
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ACinderPlayerController> Controller;
    int32 Attempts = 0;
    int32 WaitSeconds = 0;
    int32 RecoveryPhase = 0;
    int32 WorldActionIndex = INDEX_NONE;
    int32 WorldProjectionAttempts = 0;
    int32 FinalWorldProjectionAttempts = 0;
    cinder::Id WorldEntity = 0;
    ECinderTutorialGuideTarget WorldTarget = ECinderTutorialGuideTarget::None;
    bool bScoutMoveInjected = false;
    bool bFinalWorldShowTapped = false;
};

TUniquePtr<FCoachPreviewState> CoachState;
FTimerHandle CoachTimer;

const TCHAR* TargetName(ECinderTutorialGuideTarget Target)
{
    switch (Target)
    {
    case ECinderTutorialGuideTarget::None: return TEXT("none");
    case ECinderTutorialGuideTarget::Button: return TEXT("button");
    case ECinderTutorialGuideTarget::Entity: return TEXT("entity");
    case ECinderTutorialGuideTarget::Ground: return TEXT("ground");
    case ECinderTutorialGuideTarget::Camera: return TEXT("camera");
    case ECinderTutorialGuideTarget::Wait: return TEXT("wait");
    }
    return TEXT("unknown");
}

void StopCoachTimer()
{
    if (CoachState)
        if (UWorld* World = CoachState->World.Get()) World->GetTimerManager().ClearTimer(CoachTimer);
}

void FailCoach(const FString& Reason)
{
    UE_LOG(LogCinderTutorialCoachPreview, Error,
        TEXT("CINDERLINE_TUTORIAL_COACH_PREVIEW failed=%s state=%s"),
        *Reason, CoachState ? *CoachState->Name : TEXT("none"));
    StopCoachTimer();
}

bool TapHUD(ACinderHUD& HUD, const FCinderTutorialGuide& Guide)
{
    const TOptional<int32> Argument = Guide.ButtonAction == TEXT("globaltrain")
        ? TOptional<int32>() : TOptional<int32>(Guide.ButtonArgument);
    const bool bTapped = HUD.TapPreviewAction(
        Guide.ButtonAction, Argument, TOptional<cinder::Id>(Guide.ButtonEntity));
    UE_LOG(LogCinderTutorialCoachPreview, Display,
        TEXT("CINDERLINE_TUTORIAL_COACH_ACTION kind=button action=%s argument=%d entity=%u accepted=%d"),
        *Guide.ButtonAction, Guide.ButtonArgument, Guide.ButtonEntity, bTapped);
    return bTapped;
}

bool WorldTargetClear(ACinderPlayerController& PC, ACinderHUD& HUD,
    ACinderBattlefield& Battle, const FCinderTutorialGuide& Guide, const TCHAR* Phase)
{
    HUD.LogTutorialGuidance();
    cinder::Vec2 Point = Guide.Point;
    if (Guide.Entity)
    {
        const cinder::Entity* Entity = Battle.Sim().find(Guide.Entity);
        if (!Entity || !Entity->alive()) return false;
        Point = Battle.RenderPosition(*Entity);
    }
    FVector2D Screen(-1, -1);
    int32 ViewWidth = 0, ViewHeight = 0;
    PC.GetViewportSize(ViewWidth, ViewHeight);
    const bool bProjected = PC.ProjectTutorialTarget(Guide.Entity, Guide.Point, Screen);
    const bool bInViewport = bProjected && Screen.X >= 0 && Screen.Y >= 0
        && Screen.X < ViewWidth && Screen.Y < ViewHeight;
    const bool bContainsUI = bInViewport && HUD.ContainsUI(Screen);
    const FVector PawnLocation = PC.GetPawn() ? PC.GetPawn()->GetActorLocation() : FVector::ZeroVector;
    const FVector CameraLocation = PC.PlayerCameraManager
        ? PC.PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
    UE_LOG(LogCinderTutorialCoachPreview, Display,
        TEXT("CINDERLINE_TUTORIAL_COACH_TARGET phase=%s kind=%s action_index=%d entity=%u world=(%.1f,%.1f) projected=%d in_viewport=%d contains_ui=%d screen=(%.1f,%.1f) viewport=(%d,%d) pawn=(%.1f,%.1f,%.1f) camera=(%.1f,%.1f,%.1f)"),
        Phase, TargetName(Guide.Target), Guide.ActionIndex, Guide.Entity, Point.x, Point.y,
        bProjected, bInViewport, bContainsUI, Screen.X, Screen.Y, ViewWidth, ViewHeight,
        PawnLocation.X, PawnLocation.Y, PawnLocation.Z,
        CameraLocation.X, CameraLocation.Y, CameraLocation.Z);
    return bProjected && bInViewport && !bContainsUI && !HUD.NeedsTutorialTargetFocus();
}

bool TapWorld(ACinderPlayerController& PC, ACinderHUD& HUD, ACinderBattlefield& Battle,
    const FCinderTutorialGuide& Guide)
{
    cinder::Vec2 Point = Guide.Point;
    if (Guide.Entity)
    {
        const cinder::Entity* Entity = Battle.Sim().find(Guide.Entity);
        if (!Entity || !Entity->alive()) return false;
        Point = Battle.RenderPosition(*Entity);
    }

    const bool bNewTarget = CoachState->WorldActionIndex != Guide.ActionIndex
        || CoachState->WorldEntity != Guide.Entity || CoachState->WorldTarget != Guide.Target;
    if (bNewTarget)
    {
        CoachState->WorldActionIndex = Guide.ActionIndex;
        CoachState->WorldEntity = Guide.Entity;
        CoachState->WorldTarget = Guide.Target;
        CoachState->WorldProjectionAttempts = 0;
        if (!HUD.TapPreviewAction(TEXT("tutorialshow"))) HUD.FocusTutorialTarget();
        Battle.RenderState();
        const FVector PawnLocation = PC.GetPawn() ? PC.GetPawn()->GetActorLocation() : FVector::ZeroVector;
        UE_LOG(LogCinderTutorialCoachPreview, Display,
            TEXT("CINDERLINE_TUTORIAL_COACH_ACTION phase=focus kind=%s action_index=%d entity=%u world=(%.1f,%.1f) pawn=(%.1f,%.1f,%.1f)"),
            TargetName(Guide.Target), Guide.ActionIndex, Guide.Entity, Point.x, Point.y,
            PawnLocation.X, PawnLocation.Y, PawnLocation.Z);
        // PlayerCameraManager and the HUD's exclusion geometry update on the
        // following frame even when the camera pawn moves instantly.
        return true;
    }

    FVector2D Screen(-1, -1);
    int32 ViewWidth = 0, ViewHeight = 0;
    PC.GetViewportSize(ViewWidth, ViewHeight);
    const bool bProjected = PC.ProjectTutorialTarget(Guide.Entity, Guide.Point, Screen);
    const bool bInViewport = bProjected && Screen.X >= 0 && Screen.Y >= 0
        && Screen.X < ViewWidth && Screen.Y < ViewHeight;
    const bool bContainsUI = bInViewport && HUD.ContainsUI(Screen);
    const FVector PawnLocation = PC.GetPawn() ? PC.GetPawn()->GetActorLocation() : FVector::ZeroVector;
    const FVector CameraLocation = PC.PlayerCameraManager
        ? PC.PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
    ++CoachState->WorldProjectionAttempts;
    UE_LOG(LogCinderTutorialCoachPreview, Display,
        TEXT("CINDERLINE_TUTORIAL_COACH_ACTION phase=project kind=%s action_index=%d entity=%u world=(%.1f,%.1f) projected=%d in_viewport=%d contains_ui=%d screen=(%.1f,%.1f) viewport=(%d,%d) pawn=(%.1f,%.1f,%.1f) camera=(%.1f,%.1f,%.1f) attempt=%d"),
        TargetName(Guide.Target), Guide.ActionIndex, Guide.Entity, Point.x, Point.y,
        bProjected, bInViewport, bContainsUI, Screen.X, Screen.Y, ViewWidth, ViewHeight,
        PawnLocation.X, PawnLocation.Y, PawnLocation.Z,
        CameraLocation.X, CameraLocation.Y, CameraLocation.Z,
        CoachState->WorldProjectionAttempts);
    if (!bProjected || !bInViewport || bContainsUI)
    {
        // Give both the camera transform and HUD geometry a few rendered frames
        // to settle, then fail with the diagnostics above if the target is truly unpickable.
        return CoachState->WorldProjectionAttempts < 8;
    }
    PC.SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
    const bool bPressed = PC.InputKey(FInputKeyEventArgs::CreateSimulated(
        EKeys::LeftMouseButton, IE_Pressed, 1.0f));
    const bool bReleased = PC.InputKey(FInputKeyEventArgs::CreateSimulated(
        EKeys::LeftMouseButton, IE_Released, 0.0f));
    UE_LOG(LogCinderTutorialCoachPreview, Display,
        TEXT("CINDERLINE_TUTORIAL_COACH_ACTION kind=%s entity=%u world=(%.1f,%.1f) screen=(%.1f,%.1f) pressed=%d released=%d"),
        TargetName(Guide.Target), Guide.Entity, Point.x, Point.y, Screen.X, Screen.Y, bPressed, bReleased);
    CoachState->WorldActionIndex = INDEX_NONE;
    CoachState->WorldProjectionAttempts = 0;
    CoachState->WorldEntity = 0;
    CoachState->WorldTarget = ECinderTutorialGuideTarget::None;
    // InputKey's return value reports routing/consumption, not whether the bound
    // controller callback ran. The next guide state verifies the actual effect.
    return true;
}

void CaptureCoach(ACinderPlayerController& PC, ACinderBattlefield& Battle, ACinderHUD& HUD,
    const FCinderTutorialGuide& Guide)
{
    HUD.LogTutorialGuidance();
    HUD.LogMobileLayout();
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("TutorialMicrosteps");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = Directory / (CoachState->Name + TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(Filename, false, false);
    const cinder::Id Selected = PC.Selection().size() == 1 ? PC.Selection()[0] : 0;
    UE_LOG(LogCinderTutorialCoachPreview, Display,
        TEXT("CINDERLINE_TUTORIAL_COACH_PREVIEW state=%s step=%d micro=%d/%d target=%s selection=%u rally_producer=%u tick=%llu frozen=1 file=%s"),
        *CoachState->Name, static_cast<int32>(Battle.Tutorial().Step()), Guide.ActionIndex,
        Guide.ActionCount, TargetName(Guide.Target), Selected, PC.ProductionRallyProducer(),
        Battle.Sim().tick(), *Filename);
    StopCoachTimer();
}

void TickCoachPreview()
{
    if (!CoachState || ++CoachState->Attempts > 160)
    {
        FailCoach(TEXT("timeout"));
        return;
    }
    UWorld* World = CoachState->World.Get();
    ACinderPlayerController* PC = CoachState->Controller.Get();
    ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
    ACinderHUD* HUD = PC ? Cast<ACinderHUD>(PC->GetHUD()) : nullptr;
    if (!World || !PC || PC->GetWorld() != World || !Battle || !HUD
        || Battle->IsMenu() || Battle->IsOnlineMatch() || !Battle->Tutorial().IsActive())
    {
        FailCoach(TEXT("fixture_lost"));
        return;
    }

    if (CoachState->Plan.bOffscreenRecovery)
    {
        if (CoachState->RecoveryPhase == 0)
        {
            if (ACinderCamera* Camera = Cast<ACinderCamera>(PC->GetPawn()))
                Camera->Focus(FVector(FCinderTutorial::EnemyAnchorPoint().x,
                    FCinderTutorial::EnemyAnchorPoint().y, 0), true);
            Battle->RenderState();
            CoachState->RecoveryPhase = 1;
            return;
        }
        if (CoachState->RecoveryPhase == 1)
        {
            HUD->LogTutorialGuidance();
            const FString Directory = FPaths::ProjectSavedDir() / TEXT("TutorialMicrosteps");
            IFileManager::Get().MakeDirectory(*Directory, true);
            const FString BeforeShow = Directory / (CoachState->Name + TEXT("-before-show.png"));
            FScreenshotRequest::RequestScreenshot(BeforeShow, false, false);
            CoachState->RecoveryPhase = 2;
            UE_LOG(LogCinderTutorialCoachPreview, Display,
                TEXT("CINDERLINE_TUTORIAL_COACH_OFFSCREEN needs_show=1 file=%s"), *BeforeShow);
            return;
        }
        if (CoachState->RecoveryPhase == 2)
        {
            if (!HUD->TapPreviewAction(TEXT("tutorialshow")))
            {
                FailCoach(TEXT("show_button_missing"));
                return;
            }
            CoachState->RecoveryPhase = 3;
            UE_LOG(LogCinderTutorialCoachPreview, Display,
                TEXT("CINDERLINE_TUTORIAL_COACH_ACTION kind=button action=tutorialshow argument=0 accepted=1"));
            return;
        }
        if (CoachState->RecoveryPhase == 3)
        {
            const FCinderTutorialGuide VisibleTarget = HUD->CurrentTutorialGuide();
            FVector2D Screen;
            int32 ViewWidth = 0, ViewHeight = 0;
            PC->GetViewportSize(ViewWidth, ViewHeight);
            if (!PC->ProjectTutorialTarget(VisibleTarget.Entity, VisibleTarget.Point, Screen)
                || Screen.X < 0 || Screen.Y < 0 || Screen.X >= ViewWidth || Screen.Y >= ViewHeight)
                return;
            if (!HUD->TapPreviewAction(TEXT("globalcatalog"), TOptional<int32>(8)))
            {
                FailCoach(TEXT("wrong_sheet_button_missing"));
                return;
            }
            CoachState->RecoveryPhase = 4;
            UE_LOG(LogCinderTutorialCoachPreview, Display,
                TEXT("CINDERLINE_TUTORIAL_COACH_ACTION kind=button action=globalcatalog argument=8 accepted=1"));
            return;
        }
        if (CoachState->RecoveryPhase == 4)
        {
            const FCinderTutorialGuide Recovery = HUD->CurrentTutorialGuide();
            if (Recovery.Target != ECinderTutorialGuideTarget::Button
                || Recovery.ButtonAction != TEXT("closesheet") || !TapHUD(*HUD, Recovery))
            {
                FailCoach(TEXT("wrong_sheet_recovery_missing"));
                return;
            }
            CoachState->RecoveryPhase = 5;
            return;
        }
        const FCinderTutorialGuide Recovered = HUD->CurrentTutorialGuide();
        if ((Recovered.Target != ECinderTutorialGuideTarget::Entity
                && Recovered.Target != ECinderTutorialGuideTarget::Ground)
            || !WorldTargetClear(*PC, *HUD, *Battle, Recovered, TEXT("offscreen_final")))
        {
            if (++CoachState->FinalWorldProjectionAttempts >= 8)
            {
                FailCoach(TEXT("offscreen_final_target_unpickable"));
                return;
            }
            Battle->RenderState();
            return;
        }
        CaptureCoach(*PC, *Battle, *HUD, Recovered);
        return;
    }

    const FCinderTutorialGuide Guide = HUD->CurrentTutorialGuide();
    if (CoachState->Name == TEXT("scout-chain") && !CoachState->bScoutMoveInjected
        && Guide.ActionIndex == 8 && Guide.ButtonAction == TEXT("attack"))
    {
        if (!HUD->TapPreviewAction(TEXT("move")))
        {
            FailCoach(TEXT("scout_move_button_missing"));
            return;
        }
        CoachState->bScoutMoveInjected = true;
        UE_LOG(LogCinderTutorialCoachPreview, Display,
            TEXT("CINDERLINE_TUTORIAL_COACH_ACTION kind=button action=move argument=0 accepted=1"));
        Battle->RenderState();
        return;
    }
    if (Guide.ActionIndex >= CoachState->Plan.GuideIndex)
    {
        if (HUD->NeedsTutorialTargetFocus())
        {
            if (!CoachState->bFinalWorldShowTapped)
            {
                if (!HUD->TapPreviewAction(TEXT("tutorialshow")))
                {
                    FailCoach(TEXT("final_show_button_missing"));
                    return;
                }
                CoachState->bFinalWorldShowTapped = true;
                CoachState->FinalWorldProjectionAttempts = 0;
            }
            else if (++CoachState->FinalWorldProjectionAttempts >= 8)
            {
                FailCoach(TEXT("final_show_did_not_reveal_target"));
                return;
            }
            Battle->RenderState();
            return;
        }
        if (Guide.Target == ECinderTutorialGuideTarget::Entity
            || Guide.Target == ECinderTutorialGuideTarget::Ground)
        {
            if (!WorldTargetClear(*PC, *HUD, *Battle, Guide, TEXT("final_capture")))
            {
                if (++CoachState->FinalWorldProjectionAttempts >= 8)
                {
                    FailCoach(TEXT("final_world_target_unpickable"));
                    return;
                }
                Battle->RenderState();
                return;
            }
        }
        CaptureCoach(*PC, *Battle, *HUD, Guide);
        return;
    }

    bool bAdvanced = false;
    if (Guide.Target == ECinderTutorialGuideTarget::Button)
        bAdvanced = TapHUD(*HUD, Guide);
    else if (Guide.Target == ECinderTutorialGuideTarget::Entity
        || Guide.Target == ECinderTutorialGuideTarget::Ground)
        bAdvanced = TapWorld(*PC, *HUD, *Battle, Guide);
    else if (Guide.Target == ECinderTutorialGuideTarget::Wait)
    {
        if (++CoachState->WaitSeconds > 100)
        {
            FailCoach(TEXT("wait_timeout"));
            return;
        }
        Battle->Sim().update(1.0f);
        Battle->Tutorial().TickOpponent(Battle->Sim());
        Battle->Tutorial().Observe(Battle->Sim(), PC->Selection());
        bAdvanced = true;
    }
    if (!bAdvanced)
    {
        FailCoach(FString::Printf(TEXT("cannot_apply_%s_%d"), TargetName(Guide.Target), Guide.ActionIndex));
        return;
    }
    Battle->RenderState();
}

FAutoConsoleCommandWithWorldAndArgs TutorialCoachPreviewCommand(
    TEXT("cinder.TutorialCoachPreview"),
    TEXT("DEVELOPMENT: unattended fresh local menu only. Deterministic tutorial microstep preview; never writes preferences or saves."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        const FString State = Args.Num() == 1 ? Args[0].ToLower() : FString();
        const FCoachPreviewCase* Plan = nullptr;
        for (const FCoachPreviewCase& Candidate : CoachCases)
            if (State == Candidate.Name) { Plan = &Candidate; break; }
        auto* PC = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        if (!Plan)
        {
            UE_LOG(LogCinderTutorialCoachPreview, Warning,
                TEXT("CINDERLINE_TUTORIAL_COACH_PREVIEW refused=invalid_state"));
            return;
        }
        if (!FApp::IsUnattended() || !World || !World->IsGameWorld() || !PC || !Battle
            || !Battle->IsMenu() || Battle->IsOnlineMatch() || PC->IsHelpOpen())
        {
            UE_LOG(LogCinderTutorialCoachPreview, Warning,
                TEXT("CINDERLINE_TUTORIAL_COACH_PREVIEW refused=requires_unattended_fresh_local_menu"));
            return;
        }
        StopCoachTimer();
        CoachState.Reset();

        const FString MissingPreferences = FPaths::ProjectIntermediateDir() / TEXT("Automation")
            / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("-tutorial-coach.ini"));
        PC->LoadTutorialPreference(MissingPreferences);
        PC->ExecuteAction(TEXT("onboardlearn"));
        FString Failure;
        if (!Battle->Tutorial().IsActive()
            || !DriveGuidedTutorialToStep(Battle->Sim(), Battle->Tutorial(), Plan->Step, Failure))
        {
            UE_LOG(LogCinderTutorialCoachPreview, Error,
                TEXT("CINDERLINE_TUTORIAL_COACH_PREVIEW failed=drive reason=%s state=%s"),
                *Failure, *State);
            return;
        }

        Battle->RenderState();
        PC->Notify(FString());
        // The preview advances waits explicitly below. Disabling battlefield tick
        // keeps every captured microstep stable without pausing or showing a modal.
        Battle->SetActorTickEnabled(false);
        if (!Plan->bOffscreenRecovery)
        {
            cinder::Vec2 Focus{600, 600};
            cinder::Id Entity = 0;
            Battle->Tutorial().FocusPoint(Battle->Sim(), Focus, &Entity);
            if (Entity)
                if (const cinder::Entity* Target = Battle->Sim().find(Entity)) Focus = Battle->RenderPosition(*Target);
            if (ACinderCamera* Camera = Cast<ACinderCamera>(PC->GetPawn()))
                Camera->Focus(FVector(Focus.x, Focus.y, 0), true);
        }

        CoachState = MakeUnique<FCoachPreviewState>();
        CoachState->Name = State;
        CoachState->Plan = *Plan;
        CoachState->World = World;
        CoachState->Controller = PC;
        World->GetTimerManager().SetTimer(CoachTimer, FTimerDelegate::CreateStatic(&TickCoachPreview),
            0.25f, true, 0.75f);
    }));
}

#endif
