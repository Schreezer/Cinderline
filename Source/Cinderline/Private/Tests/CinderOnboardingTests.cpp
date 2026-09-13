#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderOnlineSubsystem.h"
#include "Presentation/CinderPlayerController.h"
#include "Sim/Network.h"
#include "Tests/AutomationCommon.h"
#include "Tests/CinderGuidedTestDriver.h"

namespace
{
cinder::Id FindFriendlyKind(const cinder::Simulation& Sim, cinder::Kind Kind)
{
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == Kind) return Entity.id;
    return 0;
}

struct FOnboardingFixture
{
    FTestWorldWrapper WorldOwner;
    ACinderBattlefield* Battle = nullptr;
    ACinderPlayerController* Controller = nullptr;

    bool Initialize(FAutomationTestBase& Test)
    {
        if (!WorldOwner.CreateTestWorld(EWorldType::Game))
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        UWorld* World = WorldOwner.GetTestWorld();
        if (!Test.TestNotNull(TEXT("Onboarding test world exists"), World)) return false;
        World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
        Battle = World->SpawnActor<ACinderBattlefield>();
        if (!Test.TestNotNull(TEXT("Onboarding battlefield spawned"), Battle)) return false;
        if (!WorldOwner.BeginPlayInTestWorld())
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        Controller = World->SpawnActor<ACinderPlayerController>();
        if (!Test.TestNotNull(TEXT("Onboarding controller spawned"), Controller)) return false;
        return Test.TestTrue(TEXT("Onboarding controller resolves the battlefield"),
            Controller->Battlefield() == Battle);
    }
};

struct FTemporaryOnboardingFiles
{
    FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("Automation"),
        TEXT("CinderlineOnboarding"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    FString Missing = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("missing.ini")));
    FString Skipped = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("skipped.ini")));
    FString Accepted = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("accepted.ini")));
    FString Finished = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("finished.ini")));
    FString Completed = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("completed.ini")));

    ~FTemporaryOnboardingFiles()
    {
        IFileManager::Get().DeleteDirectory(*Directory, false, true);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderOnboardingIntegration,
    "Cinderline.Integration.FirstRunTutorialOffer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderOnboardingIntegration::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FOnboardingFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderBattlefield& Battle = *Fixture.Battle;
    ACinderPlayerController& Controller = *Fixture.Controller;
    FTemporaryOnboardingFiles Files;

    TestFalse(TEXT("Automation BeginPlay leaves existing fixtures unblocked until they explicitly load a preference"),
        Controller.IsTutorialOfferPending());
    Controller.LoadTutorialPreference(Files.Missing);
    TestTrue(TEXT("A missing preference presents the first-run training offer"),
        Controller.IsTutorialOfferPending());
    TestFalse(TEXT("A missing preference does not claim completed training"),
        Controller.HasCompletedTutorial());

    const uint64 MenuHash = Battle.Sim().stateHash();
    const cinder::AIDifficulty MenuDifficulty = Controller.SelectedAIDifficulty();
    UCinderOnlineSubsystem* Session = Controller.Online();
    if (!TestNotNull(TEXT("The onboarding fixture has its game-instance online subsystem"), Session)) return false;
    Session->bHasMatch = true;
    Session->bHasSnapshot = true;
    Session->Snapshot = cinder::net::snapshotFor(Battle.Sim(), 0);
    Session->Room = TEXT("TEST");
    Controller.PollOnlineState();
    TestTrue(TEXT("A pending offer keeps an available online snapshot behind the menu"),
        Controller.IsTutorialOfferPending() && Battle.IsMenu() && !Battle.IsOnlineMatch()
        && Battle.Sim().stateHash() == MenuHash);
    Session->bHasMatch = false;
    Session->bHasSnapshot = false;
    Session->Room.Empty();
    Controller.ExecuteAction(TEXT("start"), 2);
    Controller.ExecuteAction(TEXT("help"), 5);
    Controller.ExecuteAction(TEXT("online"));
    Controller.ExecuteAction(TEXT("difficulty"), static_cast<int32>(cinder::AIDifficulty::Expert));
    Controller.ExecuteAction(TEXT("tutorial"));
    TestTrue(TEXT("The offer blocks menu actions and keeps the underlying menu unchanged"),
        Controller.IsTutorialOfferPending() && Battle.IsMenu() && !Battle.IsPaused()
        && Battle.Sim().stateHash() == MenuHash
        && Controller.SelectedAIDifficulty() == MenuDifficulty
        && !Controller.IsHelpOpen() && !Controller.OnlinePanel.IsValid()
        && !Battle.Tutorial().IsActive());

    Controller.ExecuteAction(TEXT("onboardskip"));
    TestTrue(TEXT("Skipping resolves the offer without changing the simulation"),
        !Controller.IsTutorialOfferPending() && !Controller.HasCompletedTutorial()
        && Battle.IsMenu() && Battle.Sim().stateHash() == MenuHash);
    Controller.SaveTutorialPreference(Files.Skipped);
    FString SkippedText;
    TestTrue(TEXT("The test helper writes the skipped preference to its temporary path"),
        FFileHelper::LoadFileToString(SkippedText, *Files.Skipped));
    TestTrue(TEXT("Skipped preferences retain incomplete but resolved state"),
        SkippedText.Contains(TEXT("Completed=False"))
        && SkippedText.Contains(TEXT("OfferResolved=True")));

    Controller.LoadTutorialPreference(Files.Missing);
    TestTrue(TEXT("Reloading a missing preference offers training again"),
        Controller.IsTutorialOfferPending());
    Controller.LoadTutorialPreference(Files.Skipped);
    TestFalse(TEXT("Reloading the skipped preference does not repeat the offer"),
        Controller.IsTutorialOfferPending());
    ACinderCamera* FocusRig = Fixture.WorldOwner.GetTestWorld()->SpawnActor<ACinderCamera>();
    if (!TestNotNull(TEXT("Tutorial focus has a camera rig"), FocusRig)) return false;
    Controller.Rig = FocusRig;
    Controller.ExecuteAction(TEXT("tutorial"));
    TestTrue(TEXT("Guided training remains available after skipping the first-run offer"),
        Battle.Tutorial().IsActive() && !Battle.Sim().config().ai);
    const cinder::Id Anchor = FindFriendlyKind(Battle.Sim(), cinder::Kind::Headquarters);
    Controller.Selected = {Anchor};
    const uint64 CameraFocusHash = Battle.Sim().stateHash();
    const int32 CameraFocusCommands = static_cast<int32>(Battle.Sim().recording().size());
    Controller.ExecuteAction(TEXT("tutorialfocus"));
    TestTrue(TEXT("Showing the camera marker preserves the current friendly selection"),
        Anchor != 0 && Controller.Selection().size() == 1 && Controller.Selection().front() == Anchor);
    TestEqual(TEXT("Showing the base does not complete the camera lesson"),
        static_cast<int32>(Battle.Tutorial().Step()), static_cast<int32>(ECinderTutorialStep::Camera));
    TestTrue(TEXT("Showing a tutorial target issues no order and changes no simulation state"),
        Battle.Sim().stateHash() == CameraFocusHash
        && static_cast<int32>(Battle.Sim().recording().size()) == CameraFocusCommands);
    FString FocusFailure;
    if (!TestTrue(*FString::Printf(TEXT("Normal tutorial play reaches Kiln construction: %s"), *FocusFailure),
        DriveGuidedTutorialToStep(Battle.Sim(), Battle.Tutorial(), ECinderTutorialStep::BuildKiln, FocusFailure))) return false;
    TestEqual(TEXT("Build Kiln uses the global BUILD catalog as its primary action"),
        static_cast<int32>(Battle.Tutorial().PrimaryAction(Battle.Sim())),
        static_cast<int32>(ECinderTutorialPrimaryAction::OpenBuild));
    Controller.Selected = {Anchor};
    const uint64 BuildFocusHash = Battle.Sim().stateHash();
    const int32 BuildFocusCommands = static_cast<int32>(Battle.Sim().recording().size());
    Controller.ExecuteAction(TEXT("tutorialfocus"));
    TestTrue(TEXT("Opening global BUILD preserves the current friendly selection"),
        Anchor != 0 && Controller.Selection().size() == 1 && Controller.Selection().front() == Anchor);
    TestTrue(TEXT("Opening global BUILD issues no order and changes no simulation state"),
        Battle.Sim().stateHash() == BuildFocusHash
        && static_cast<int32>(Battle.Sim().recording().size()) == BuildFocusCommands);
    TestTrue(TEXT("Global BUILD feedback gives the next construction action"),
        Controller.Feedback().Contains(TEXT("Open BUILD"))
        && Controller.Feedback().Contains(TEXT("Kiln"))
        && Controller.Feedback().Contains(TEXT("assigned")));
    Controller.ExecuteAction(TEXT("tutorialend"));
    TestTrue(TEXT("Ending manually opened training returns to the menu"), Battle.IsMenu());

    Controller.LoadTutorialPreference(Files.Missing);
    Controller.Confirm();
    TestTrue(TEXT("Enter accepts the pending offer and starts guided training"),
        !Controller.IsTutorialOfferPending() && Battle.Tutorial().IsActive()
        && !Battle.Sim().config().ai);
    Controller.SaveTutorialPreference(Files.Accepted);
    Battle.Sim().forfeit(1);
    Controller.UpdateTutorial();
    TestTrue(TEXT("An early tutorial victory remains a victory without claiming lesson completion"),
        Battle.Sim().winner() == 0 && !Battle.Tutorial().IsComplete()
        && !Controller.HasCompletedTutorial());
    Controller.ExecuteAction(TEXT("tutorialend"));
    Controller.LoadTutorialPreference(Files.Missing);
    Controller.LoadTutorialPreference(Files.Accepted);
    TestFalse(TEXT("Reloading an accepted offer does not repeat it"),
        Controller.IsTutorialOfferPending());

    Controller.LoadTutorialPreference(Files.Missing);
    Controller.Confirm();
    FString GuidedFailure;
    const bool bFinishedGuidedMatch = DriveGuidedTutorialToStep(
        Battle.Sim(), Battle.Tutorial(), ECinderTutorialStep::Complete, GuidedFailure);
    if (!TestTrue(*FString::Printf(TEXT("Accepted onboarding can finish the ordinary guided match: %s"),
        *GuidedFailure), bFinishedGuidedMatch)) return false;
    Controller.UpdateTutorial();
    TestTrue(TEXT("A real guided victory marks the first-run training complete"),
        Battle.Sim().winner() == 0 && Battle.Tutorial().IsComplete()
        && Controller.HasCompletedTutorial() && !Controller.IsTutorialOfferPending());
    Controller.SaveTutorialPreference(Files.Finished);
    Controller.ExecuteAction(TEXT("tutorialend"));
    Controller.LoadTutorialPreference(Files.Finished);
    TestTrue(TEXT("Completed guided training reloads as completed and resolved"),
        Controller.HasCompletedTutorial() && !Controller.IsTutorialOfferPending());

    Controller.LoadTutorialPreference(Files.Missing);
    const uint64 EscapeHash = Battle.Sim().stateHash();
    Controller.Escape();
    TestTrue(TEXT("Escape skips the pending offer without starting or mutating a match"),
        !Controller.IsTutorialOfferPending() && Battle.IsMenu()
        && !Battle.Tutorial().IsActive() && Battle.Sim().stateHash() == EscapeHash);

    IFileManager::Get().MakeDirectory(*Files.Directory, true);
    TestTrue(TEXT("A legacy completion preference is written to the temporary path"),
        FFileHelper::SaveStringToFile(TEXT("[Training]\nCompleted=True\n"), *Files.Completed));
    Controller.LoadTutorialPreference(Files.Completed);
    TestTrue(TEXT("Legacy completed players migrate without seeing the new offer"),
        Controller.HasCompletedTutorial() && !Controller.IsTutorialOfferPending());
    Controller.SaveTutorialPreference(Files.Completed);
    FString CompletedText;
    TestTrue(TEXT("The migrated completion preference remains readable"),
        FFileHelper::LoadFileToString(CompletedText, *Files.Completed));
    TestTrue(TEXT("Saving a migrated completion adds the resolved marker"),
        CompletedText.Contains(TEXT("Completed=True"))
        && CompletedText.Contains(TEXT("OfferResolved=True")));
    return !HasAnyErrors();
}

#endif
