#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Presentation/CinderCampaign.h"
#include "Presentation/CinderGuidance.h"
#include "Presentation/CinderTutorial.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderSharedGuidanceContractTest,
    "Cinderline.Campaign.Guidance.SharedContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderSharedGuidanceContractTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    cinder::Simulation Sim;
    FCinderTutorial Tutorial;
    if (!TestTrue(TEXT("The existing tutorial still initializes through the shared guidance header"),
        Tutorial.InitializeScenario(Sim))) return false;

    const FCinderTutorialGuide Guide = Tutorial.Guide(Sim, FCinderTutorialContext{}, true);
    TestEqual(TEXT("The extracted guide retains the tutorial camera target"),
        static_cast<int32>(Guide.Target), static_cast<int32>(ECinderTutorialGuideTarget::Camera));
    TestEqual(TEXT("The existing first lesson remains one action"), Guide.ActionCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCampaignGuidanceFadeTest,
    "Cinderline.Campaign.Guidance.Fade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCampaignGuidanceFadeTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FCinderTutorialContext Context;

    {
        cinder::Simulation Sim;
        FCinderCampaign Campaign;
        if (!TestTrue(TEXT("First Shift initializes"), Campaign.InitializeMission(Sim, 0))) return false;
        const FCinderTutorialGuide Guide = Campaign.Guide(Sim, Context, true);
        TestEqual(TEXT("A demonstration immediately points to its one camera action"),
            static_cast<int32>(Guide.Target), static_cast<int32>(ECinderTutorialGuideTarget::Camera));
        TestEqual(TEXT("The camera demonstration exposes one action"), Guide.ActionCount, 1);
        TestFalse(TEXT("The first campaign objective has concise visible text"), Campaign.Text(Sim).Title.IsEmpty());
    }

    {
        cinder::Simulation Sim;
        FCinderCampaign Campaign;
        if (!TestTrue(TEXT("Eyes Beyond initializes"), Campaign.InitializeMission(Sim, 2))) return false;
        FCinderTutorialGuide Guide = Campaign.Guide(Sim, Context, true);
        TestEqual(TEXT("Previously taught production starts as objective-only coaching"),
            static_cast<int32>(Guide.Target), static_cast<int32>(ECinderTutorialGuideTarget::None));
        TestTrue(TEXT("Unassisted coaching states the concrete outcome goal"),
            Guide.Instruction.Contains(TEXT("Train a Skim")));
        TestTrue(TEXT("Unassisted coaching does not expose the explanation"), Guide.Explanation.IsEmpty());
        TestTrue(TEXT("Unassisted coaching does not expose hint copy"), Campaign.Text(Sim).Hint.IsEmpty());

        Campaign.RequestHint();
        Guide = Campaign.Guide(Sim, Context, true);
        TestTrue(TEXT("Requesting help reveals one actionable target"),
            Guide.Target != ECinderTutorialGuideTarget::None);
        TestFalse(TEXT("Requested help explains why the action matters"), Guide.Explanation.IsEmpty());
        TestTrue(TEXT("Requested help marks the attempt assisted"), Campaign.Text(Sim).bAssisted);
    }

    {
        cinder::Simulation Sim;
        FCinderCampaign Campaign;
        if (!TestTrue(TEXT("Trial by Fire initializes"), Campaign.InitializeMission(Sim, 5))) return false;
        FCinderTutorialGuide Guide = Campaign.Guide(Sim, Context, true);
        TestEqual(TEXT("The final proof has no automatic arrow"),
            static_cast<int32>(Guide.Target), static_cast<int32>(ECinderTutorialGuideTarget::None));
        TestTrue(TEXT("The final proof states its victory condition without requiring HINT"),
            Guide.Instruction.Contains(TEXT("Win an ordinary Standard match")));
        TestTrue(TEXT("The final proof withholds explanation before HINT"), Guide.Explanation.IsEmpty());
        Campaign.RequestHint();
        Guide = Campaign.Guide(Sim, Context, true);
        TestEqual(TEXT("A final-match hint never becomes an automatic arrow"),
            static_cast<int32>(Guide.Target), static_cast<int32>(ECinderTutorialGuideTarget::None));
        TestFalse(TEXT("The requested final-match reminder becomes readable"), Guide.Explanation.IsEmpty());
    }

    {
        cinder::Simulation Sim;
        FCinderCampaign Campaign;
        if (!TestTrue(TEXT("Hold the Line initializes"), Campaign.InitializeMission(Sim, 3))) return false;
        FCinderCampaignState State = Campaign.ExportState();
        State.Phase = 3;
        State.CheckpointPhase = 3;
        State.CompletedObjectiveMask = 0x7;
        State.PhaseStartTick = Sim.tick();
        State.ObjectiveStartTick = Sim.tick();
        State.NextOpponentDecisionTick = Sim.tick() + FCinderCampaign::WavePreparationTicks;
        State.Counters.OpponentTrainOrders = 0;
        State.Authored.ActiveWave.clear();
        State.bWaveProductionFinished = false;
        FString Error;
        if (!TestTrue(TEXT("A valid first-wave boundary imports"), Campaign.ImportState(Sim, State, Error)))
        {
            AddError(Error);
            return false;
        }
        TestTrue(TEXT("The settled wave boundary shows its fixed preparation countdown"),
            Campaign.Text(Sim).Progress.Contains(TEXT("Prepare:")));

        const cinder::Id Attacker = Sim.debugSpawn(cinder::Kind::Striker, 1, State.HomeDefense);
        State = Campaign.ExportState();
        State.Authored.ActiveWave = {Attacker};
        State.Counters.OpponentTrainOrders = 1;
        State.NextOpponentDecisionTick = Sim.tick() + 15;
        if (!TestTrue(TEXT("An active-wave director state imports"), Campaign.ImportState(Sim, State, Error)))
        {
            AddError(Error);
            return false;
        }
        const FString ActiveProgress = Campaign.Text(Sim).Progress;
        TestFalse(TEXT("A recurring opponent decision tick does not restart preparation"),
            ActiveProgress.Contains(TEXT("Prepare:")));
        TestTrue(TEXT("Active-wave progress reports attackers instead"),
            ActiveProgress.Contains(TEXT("Active:")));
    }

    {
        cinder::Simulation Sim;
        FCinderCampaign Campaign;
        if (!TestTrue(TEXT("Keep the Fires Fed initializes"), Campaign.InitializeMission(Sim, 1))) return false;
        FCinderCampaignState State = Campaign.ExportState();
        State.Phase = 5;
        State.CheckpointPhase = 5;
        State.CompletedObjectiveMask = 0x1f;
        State.PhaseStartTick = Sim.tick();
        State.ObjectiveStartTick = Sim.tick();
        State.Authored.RemoteSiphon = 0;
        FString Error;
        if (!TestTrue(TEXT("A remote-delivery recovery boundary imports"),
            Campaign.ImportState(Sim, State, Error)))
        {
            AddError(Error);
            return false;
        }
        Campaign.RequestHint();
        const FCinderTutorialGuide Recovery = Campaign.Guide(Sim, FCinderTutorialContext{}, true);
        TestEqual(TEXT("A lost remote Siphon recovers through the existing BUILD control"),
            Recovery.ButtonAction, FString(TEXT("globalcatalog")));
        TestEqual(TEXT("Siphon recovery opens BUILD"), Recovery.ButtonArgument, 7);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCampaignGuidanceMissionCoverageTest,
    "Cinderline.Campaign.Guidance.MissionCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCampaignGuidanceMissionCoverageTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    for (int32 Mission = 0; Mission < FCinderCampaign::MissionCount; ++Mission)
    {
        cinder::Simulation Sim;
        FCinderCampaign Campaign;
        if (!TestTrue(*FString::Printf(TEXT("Mission %d initializes"), Mission + 1),
            Campaign.InitializeMission(Sim, Mission))) return false;
        const FCinderCampaignText Text = Campaign.Text(Sim);
        TestFalse(*FString::Printf(TEXT("Mission %d has a title"), Mission + 1), Text.Title.IsEmpty());
        TestTrue(*FString::Printf(TEXT("Mission %d exposes a valid phase count"), Mission + 1),
            Text.PhaseCount > 0 && Text.Phase >= 0 && Text.Phase < Text.PhaseCount);

        const FCinderTutorialGuide Before = Campaign.Guide(Sim, FCinderTutorialContext{}, true);
        Campaign.RequestHint();
        const FCinderTutorialGuide After = Campaign.Guide(Sim, FCinderTutorialContext{}, true);
        TestFalse(*FString::Printf(TEXT("Mission %d has guidance copy after HINT"), Mission + 1),
            After.Instruction.IsEmpty());
        const bool bOneTarget =
            (After.Target == ECinderTutorialGuideTarget::Button && !After.ButtonAction.IsEmpty()
                && After.Entity == 0) ||
            (After.Target == ECinderTutorialGuideTarget::Entity && After.Entity != 0
                && After.ButtonAction.IsEmpty()) ||
            (After.Target != ECinderTutorialGuideTarget::Button
                && After.Target != ECinderTutorialGuideTarget::Entity && After.ButtonAction.IsEmpty()
                && After.Entity == 0);
        TestTrue(*FString::Printf(TEXT("Mission %d guidance always presents at most one target"), Mission + 1),
            bOneTarget);
        if (Mission == 5)
            TestEqual(TEXT("Trial by Fire remains quiet after HINT"),
                static_cast<int32>(After.Target), static_cast<int32>(ECinderTutorialGuideTarget::None));
        else
            TestTrue(*FString::Printf(TEXT("Mission %d either starts guided or deliberately waits for HINT"), Mission + 1),
                Before.Target != ECinderTutorialGuideTarget::None || Before.Explanation.IsEmpty());
    }
    return true;
}

#endif
