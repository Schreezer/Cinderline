#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderGameEngine.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderPlayerController.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCampaignLifecycleIntegration,
    "Cinderline.Integration.CampaignLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCampaignLifecycleIntegration::RunTest(const FString& Parameters)
{
    FTestWorldWrapper WorldOwner;
    if (!WorldOwner.CreateTestWorld(EWorldType::Game)) { WorldOwner.ForwardErrorMessages(this); return false; }
    UWorld* World = WorldOwner.GetTestWorld();
    World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
    auto* Battle = World->SpawnActor<ACinderBattlefield>();
    if (!TestNotNull(TEXT("Battlefield exists"), Battle)) return false;
    if (!WorldOwner.BeginPlayInTestWorld()) { WorldOwner.ForwardErrorMessages(this); return false; }
    auto* Controller = World->SpawnActor<ACinderPlayerController>();
    if (!TestNotNull(TEXT("Controller exists"), Controller)) return false;
    Controller->ExecuteAction(TEXT("campaignmenu"));
    TestTrue(TEXT("Campaign opens from the main menu without starting simulation"),
        Controller->IsCampaignMenuOpen() && Battle->IsMenu());
    Controller->ExecuteAction(TEXT("campaignselect"), 4);
    TestEqual(TEXT("Later missions are available for practice"), Controller->SelectedCampaignMission(), 4);
    Controller->ExecuteAction(TEXT("campaignstart"), 0);
    if (!TestTrue(TEXT("First mission starts exclusively"), Battle->Campaign().IsActive()
        && !Battle->Tutorial().IsActive() && !Battle->IsMenu() && !Controller->IsCampaignMenuOpen())) return false;
    TestTrue(TEXT("Initial checkpoint is available in unattended memory"), Battle->HasCampaignCheckpoint());
    TestFalse(TEXT("Campaign cannot overwrite skirmish save"), Battle->SaveMatch());
    const uint64 OriginalHash = Battle->Sim().stateHash();
    TestFalse(TEXT("Invalid mission initialization rejected"), Battle->StartCampaignMission(99));
    TestEqual(TEXT("Invalid mission leaves the current simulation intact"), Battle->Sim().stateHash(), OriginalHash);
    TestEqual(TEXT("Invalid mission leaves the current director intact"), Battle->Campaign().MissionIndex(), 0);

    Battle->GuidanceCameraInput();
    Battle->ObserveCampaign();
    TestEqual(TEXT("Deliberate camera action reaches selection lesson"), Battle->Campaign().Phase(), 1);
    const uint64 BoundaryHash = Battle->Sim().stateHash();
    Controller->ExecuteAction(TEXT("campaignhint"));
    TestTrue(TEXT("The hint action records assistance"), Battle->Campaign().WasAssisted());
    Battle->SetPaused(true);
    const uint64 FrozenTick = Battle->Sim().tick();
    Battle->Tick(0.2f);
    TestEqual(TEXT("Paused campaign does not advance simulation"), Battle->Sim().tick(), FrozenTick);
    cinder::Command Train;
    Train.type = cinder::CommandType::AutoTrain;
    Train.kind = cinder::Kind::Worker;
    Train.queueIndex = 1;
    TestFalse(TEXT("Paused director rejects gameplay commands at authority seam"), Battle->SubmitCommand(Train).accepted);
    Battle->SetPaused(false);
    Battle->Sim().update(1);
    TestTrue(TEXT("Retry restores a complete settled boundary"), Battle->ResumeCampaignCheckpoint());
    TestEqual(TEXT("Retry preserves phase"), Battle->Campaign().Phase(), 1);
    TestTrue(TEXT("Retry preserves assistance through the controller and save adapter"), Battle->Campaign().WasAssisted());
    TestEqual(TEXT("Retry restores simulation hash, not just objective label"), Battle->Sim().stateHash(), BoundaryHash);

    Battle->Sim().forfeit(0);
    Battle->ObserveCampaign();
    TestTrue(TEXT("Defeat is terminal but retains campaign mode"), Battle->IsMatchOver() && Battle->Campaign().IsActive());
    TestFalse(TEXT("Loss awards no progress"), Battle->CampaignProgress().IsComplete(0));
    TestFalse(TEXT("Terminal campaign cannot overwrite skirmish save"), Battle->SaveMatch());
    TestFalse(TEXT("Terminal campaign rejects commands"), Battle->SubmitCommand(Train).accepted);
    TestEqual(TEXT("Results receive idle frame pacing"),
        static_cast<int32>(UCinderGameEngine::ClassifyMatchState(Battle, true)),
        static_cast<int32>(ECinderFramePacingState::Results));
    Controller->ExecuteAction(TEXT("campaignretry"));
    TestTrue(TEXT("Retry after defeat returns to running campaign"), Battle->Campaign().IsActive() && !Battle->IsMatchOver());
    Battle->Sim().forfeit(1);
    Battle->ObserveCampaign();
    TestTrue(TEXT("An early normal victory completes the campaign mission"), Battle->CampaignProgress().IsComplete(0));
    TestFalse(TEXT("Completed mission does not advertise stale resume"), Battle->HasCampaignCheckpoint());
    Controller->ExecuteAction(TEXT("campaignexit"));
    TestTrue(TEXT("Debrief exits to campaign selection"), Battle->IsMenu() && Controller->IsCampaignMenuOpen());
    TestFalse(TEXT("Return to menu resets the campaign director"), Battle->Campaign().IsActive());
    Controller->ExecuteAction(TEXT("tutorial"));
    TestTrue(TEXT("Quick tutorial remains available"), Battle->Tutorial().IsActive() && !Battle->Campaign().IsActive());
    Battle->StartMatch(1);
    TestTrue(TEXT("Skirmish resets both teaching directors"), !Battle->Tutorial().IsActive() && !Battle->Campaign().IsActive());
    TestTrue(TEXT("Campaign completion survives switching modes"), Battle->CampaignProgress().IsComplete(0));
    return true;
}
#endif
