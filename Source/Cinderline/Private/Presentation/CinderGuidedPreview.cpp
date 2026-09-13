#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogCinderGuidedPreview, Log, All);

namespace
{
TWeakObjectPtr<UWorld> GuidedCaptureWorld;
FTimerHandle GuidedCaptureTimer;
FTimerHandle GuidedDetailsTimer;

FAutoConsoleCommandWithWorldAndArgs GuidedPreviewCommand(
    TEXT("cinder.guidedpreview"),
    TEXT("DEVELOPMENT: unattended fresh-menu only. Capture offer|skip|camera|kiln|research|defend|assault|win through normal tutorial commands. Append -details or -focus to tap the lesson control. Never writes preferences or saves."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        const FString State = Args.Num() == 1 ? Args[0].ToLower() : FString();
        const bool bDetails = State.EndsWith(TEXT("-details"));
        const bool bFeedback = State.EndsWith(TEXT("-feedback"));
        const bool bFocus = State.EndsWith(TEXT("-focus"));
        const FString Lesson = bDetails ? State.LeftChop(8) : bFeedback ? State.LeftChop(9)
            : bFocus ? State.LeftChop(6) : State;
        const bool bOffer = Lesson == TEXT("offer");
        const bool bSkip = Lesson == TEXT("skip");
        if ((bDetails || bFeedback || bFocus) && (bOffer || bSkip || Lesson == TEXT("win")))
        {
            UE_LOG(LogCinderGuidedPreview, Warning, TEXT("CINDERLINE_GUIDED_PREVIEW refused=modifier_requires_active_lesson"));
            return;
        }
        ECinderTutorialStep Target = ECinderTutorialStep::Camera;
        if (Lesson == TEXT("kiln")) Target = ECinderTutorialStep::BuildKiln;
        else if (Lesson == TEXT("research")) Target = ECinderTutorialStep::ResearchWeapons;
        else if (Lesson == TEXT("defend")) Target = ECinderTutorialStep::Defend;
        else if (Lesson == TEXT("assault")) Target = ECinderTutorialStep::DestroyAnchor;
        else if (Lesson == TEXT("win")) Target = ECinderTutorialStep::Complete;
        else if (!bOffer && !bSkip && Lesson != TEXT("camera"))
        {
            UE_LOG(LogCinderGuidedPreview, Warning, TEXT("CINDERLINE_GUIDED_PREVIEW refused=invalid_state"));
            return;
        }

        auto* PC = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        if (!FApp::IsUnattended() || !World || !World->IsGameWorld() || !PC || !Battle
            || !Battle->IsMenu() || Battle->IsOnlineMatch() || PC->IsHelpOpen())
        {
            UE_LOG(LogCinderGuidedPreview, Warning,
                TEXT("CINDERLINE_GUIDED_PREVIEW refused=requires_unattended_fresh_local_menu"));
            return;
        }
        if (UWorld* Previous = GuidedCaptureWorld.Get())
        {
            Previous->GetTimerManager().ClearTimer(GuidedCaptureTimer);
            Previous->GetTimerManager().ClearTimer(GuidedDetailsTimer);
        }

        const FString MissingPreferences = FPaths::ProjectIntermediateDir() / TEXT("Automation")
            / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("-training.ini"));
        PC->LoadTutorialPreference(MissingPreferences);
        if (!bOffer)
        {
            PC->ExecuteAction(bSkip ? TEXT("onboardskip") : TEXT("onboardlearn"));
            if (!bSkip)
            {
                FString Failure;
                if (!Battle->Tutorial().IsActive()
                    || !DriveGuidedTutorialToStep(Battle->Sim(), Battle->Tutorial(), Target, Failure))
                {
                    UE_LOG(LogCinderGuidedPreview, Error,
                        TEXT("CINDERLINE_GUIDED_PREVIEW failed=%s state=%s step=%d"),
                        *Failure, *State, static_cast<int32>(Battle->Tutorial().Step()));
                    return;
                }
                // Freeze only this unattended preview's simulation; keep the live HUD
                // and controller observing the real result, without a pause overlay.
                Battle->SetActorTickEnabled(false);
                Battle->RenderState();
                if (Lesson == TEXT("kiln") || Lesson == TEXT("research"))
                {
                    // Reproduce the stale Anchor selection left after worker training.
                    for (const cinder::Entity& Entity : Battle->Sim().entities())
                    {
                        if (Entity.alive() && Entity.team == 0 && Entity.kind == cinder::Kind::Headquarters)
                        {
                            PC->SelectOwnedEntity(Entity.id);
                            break;
                        }
                    }
                }
                if (auto* Rig = Cast<ACinderCamera>(PC->GetPawn()))
                {
                    cinder::Vec2 Focus{600, 600};
                    Battle->Tutorial().FocusPoint(Battle->Sim(), Focus);
                    if (Lesson == TEXT("assault")) Focus = FCinderTutorial::EnemyAnchorPoint();
                    Rig->Focus(FVector(Focus.x, Focus.y, 0), true);
                }
            }
        }

        GuidedCaptureWorld = World;
        const TWeakObjectPtr<UWorld> WeakWorld(World);
        const TWeakObjectPtr<ACinderPlayerController> WeakPC(PC);
        if (bFeedback && Battle->Tutorial().IsActive()) PC->ExecuteAction(TEXT("save"));
        if (bDetails || bFocus)
        {
            World->GetTimerManager().SetTimer(GuidedDetailsTimer, FTimerDelegate::CreateLambda([WeakPC, bFocus]
            {
                ACinderPlayerController* CurrentPC = WeakPC.Get();
                ACinderHUD* HUD = CurrentPC ? Cast<ACinderHUD>(CurrentPC->GetHUD()) : nullptr;
                if (!HUD || !HUD->TapPreviewAction(bFocus ? TEXT("tutorialfocus") : TEXT("tutorialdetails")))
                    UE_LOG(LogCinderGuidedPreview, Error, TEXT("CINDERLINE_GUIDED_PREVIEW failed=lesson_button_not_available"));
            }), 1.0f, false);
        }
        World->GetTimerManager().SetTimer(GuidedCaptureTimer, FTimerDelegate::CreateLambda([WeakWorld, WeakPC, State]
        {
            UWorld* CurrentWorld = WeakWorld.Get();
            ACinderPlayerController* CurrentPC = WeakPC.Get();
            ACinderBattlefield* CurrentBattle = CurrentPC ? CurrentPC->Battlefield() : nullptr;
            ACinderHUD* HUD = CurrentPC ? Cast<ACinderHUD>(CurrentPC->GetHUD()) : nullptr;
            if (!CurrentWorld || !CurrentPC || CurrentPC->GetWorld() != CurrentWorld
                || !CurrentBattle || CurrentBattle->IsOnlineMatch() || !HUD) return;
            HUD->LogMobileLayout();
            const cinder::Id Selected = CurrentPC->Selection().size() == 1 ? CurrentPC->Selection()[0] : 0;
            const cinder::Entity* SelectedEntity = Selected ? CurrentBattle->Sim().find(Selected) : nullptr;
            UE_LOG(LogCinderGuidedPreview, Display, TEXT("CINDERLINE_GUIDED_SELECTION id=%u kind=%s"),
                Selected, SelectedEntity ? UTF8_TO_TCHAR(cinder::definition(SelectedEntity->kind).name) : TEXT("none"));
            const FString Directory = FPaths::ProjectSavedDir() / TEXT("GuidedMatch");
            IFileManager::Get().MakeDirectory(*Directory, true);
            const FString Filename = Directory / (State + TEXT(".png"));
            FScreenshotRequest::RequestScreenshot(Filename, false, false);
            UE_LOG(LogCinderGuidedPreview, Display,
                TEXT("CINDERLINE_GUIDED_PREVIEW state=%s offer=%d menu=%d step=%d complete=%d preference_completed=%d winner=%d opponent_orders=%d tick=%llu file=%s"),
                *State, CurrentPC->IsTutorialOfferPending(), CurrentBattle->IsMenu(),
                static_cast<int32>(CurrentBattle->Tutorial().Step()), CurrentBattle->Tutorial().IsComplete(),
                CurrentPC->HasCompletedTutorial(), CurrentBattle->Sim().winner(),
                CurrentBattle->Tutorial().OpponentOrdersIssued(), CurrentBattle->Sim().tick(), *Filename);
        }), 2.0f, false);
    }));
}

#endif
