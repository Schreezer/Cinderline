#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Presentation/CinderHelpContent.h"
#include "Presentation/CinderTutorial.h"
#include "Sim/Simulation.h"
#include "Tests/CinderGuidedTestDriver.h"

#include <algorithm>

namespace
{
int32 StepNumber(const FCinderTutorial& Tutorial)
{
    return static_cast<int32>(Tutorial.Step());
}

int32 CountKind(const cinder::Simulation& Sim, cinder::Kind Kind)
{
    return static_cast<int32>(std::count_if(Sim.entities().begin(), Sim.entities().end(), [Kind](const cinder::Entity& Entity)
    {
        return Entity.alive() && Entity.team == 0 && Entity.kind == Kind && Entity.progress >= 1.0f;
    }));
}

cinder::Id FindKind(const cinder::Simulation& Sim, cinder::Kind Kind, int Team = 0, bool bComplete = true)
{
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == Team && Entity.kind == Kind && (!bComplete || Entity.progress >= 1.0f)) return Entity.id;
    return 0;
}

struct FPractice
{
    cinder::Simulation Sim;
    FCinderTutorial Tutorial;
    cinder::Id Target = 0;

    bool Initialize()
    {
        cinder::Config Config;
        Config.map = 0;
        Config.seed = FCinderTutorial::Seed;
        Config.ai = false;
        Sim.reset(Config);
        Target = Sim.debugSpawn(cinder::Kind::Worker, 1, FCinderTutorial::CombatPoint());
        cinder::Command Hold;
        Hold.type = cinder::CommandType::Hold;
        Hold.team = 1;
        Hold.units = {Target};
        if (!Target || !Sim.command(Hold).accepted) return false;
        Tutorial.Start(Sim, Target);
        return Tutorial.IsActive();
    }

    bool Accept(cinder::Command Command)
    {
        const cinder::CommandResult Result = Sim.command(Command);
        if (!Result.accepted) return false;
        Tutorial.AcceptedCommand(Sim, Command);
        return true;
    }

    void Update(float Seconds = 1.0f)
    {
        Sim.update(Seconds);
        Tutorial.Observe(Sim, {});
    }

    bool WaitForStep(ECinderTutorialStep Expected, int32 Seconds)
    {
        for (int32 Second = 0; Second < Seconds && StepNumber(Tutorial) < static_cast<int32>(Expected); ++Second) Update();
        return Tutorial.Step() == Expected;
    }

    bool CompleteIntroduction()
    {
        Tutorial.CameraInput();
        const cinder::Id Worker = FindKind(Sim, cinder::Kind::Worker);
        const cinder::Entity* WorkerEntity = Sim.find(Worker);
        if (!WorkerEntity || !WorkerEntity->resourceTarget) return false;
        Tutorial.Observe(Sim, {Worker});
        cinder::Command Gather;
        Gather.type = cinder::CommandType::Gather;
        Gather.team = 0;
        Gather.units = {Worker};
        Gather.target = WorkerEntity->resourceTarget;
        if (!Accept(Gather)) return false;
        for (int32 Second = 0; Second < 30 && StepNumber(Tutorial) < static_cast<int32>(ECinderTutorialStep::TrainWorker); ++Second) Update();
        return StepNumber(Tutorial) >= static_cast<int32>(ECinderTutorialStep::TrainWorker);
    }

    bool Train(cinder::Kind Kind, int32 Count = 1)
    {
        cinder::Command Command;
        Command.type = cinder::CommandType::AutoTrain;
        Command.team = 0;
        Command.kind = Kind;
        Command.queueIndex = Count;
        return Accept(Command);
    }

    bool Build(cinder::Kind Kind, cinder::Id* OutFoundation = nullptr)
    {
        for (float Y = 350.0f; Y <= 1450.0f; Y += 100.0f)
        {
            for (float X = 750.0f; X <= 1550.0f; X += 100.0f)
            {
                const cinder::Vec2 Point{X, Y};
                if (!Sim.autoBuildStatus(0, Kind, &Point).accepted) continue;
                cinder::Command Command;
                Command.type = cinder::CommandType::AutoBuild;
                Command.team = 0;
                Command.kind = Kind;
                Command.point = Point;
                if (!Accept(Command)) continue;
                if (OutFoundation) *OutFoundation = FindKind(Sim, Kind, 0, false);
                return true;
            }
        }
        return false;
    }

    bool ReachAttackMove()
    {
        if (!CompleteIntroduction() || !Train(cinder::Kind::Worker) || !WaitForStep(ECinderTutorialStep::BuildKiln, 35)) return false;
        if (!Build(cinder::Kind::Foundry) || !WaitForStep(ECinderTutorialStep::TrainEmbers, 110)) return false;
        if (!Train(cinder::Kind::Striker, 3) || !WaitForStep(ECinderTutorialStep::BuildSiphon, 105)) return false;
        if (!Build(cinder::Kind::Processor) || !WaitForStep(ECinderTutorialStep::Scout, 100)) return false;
        const int32 ExistingScouts = CountKind(Sim, cinder::Kind::Scout);
        if (!Train(cinder::Kind::Scout)) return false;
        for (int32 Second = 0; Second < 45 && CountKind(Sim, cinder::Kind::Scout) == ExistingScouts; ++Second) Update();
        const cinder::Id Scout = FindKind(Sim, cinder::Kind::Scout);
        if (!Scout) return false;
        cinder::Command Move;
        Move.type = cinder::CommandType::Move;
        Move.team = 0;
        Move.units = {Scout};
        Move.point = FCinderTutorial::ScoutPoint();
        return Accept(Move) && WaitForStep(ECinderTutorialStep::AttackMove, 30);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTutorialCommandGates,
    "Cinderline.Tutorial.CommandGates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTutorialCommandGates::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FPractice Practice;
    TestTrue(TEXT("The fixed practice scenario starts active"), Practice.Initialize());
    if (!Practice.Tutorial.IsActive()) return false;
    TestTrue(TEXT("Ordinary selection and mining reach worker production"), Practice.CompleteIntroduction());
    if (Practice.Tutorial.Step() != ECinderTutorialStep::TrainWorker) return false;
    TestEqual(TEXT("Worker production lesson opens the persistent TRAIN catalog"),
        static_cast<int32>(Practice.Tutorial.PrimaryAction(Practice.Sim)),
        static_cast<int32>(ECinderTutorialPrimaryAction::OpenTrain));

    cinder::Command Rejected;
    Rejected.type = cinder::CommandType::Train;
    Rejected.team = 0;
    Rejected.kind = cinder::Kind::Worker;
    TestFalse(TEXT("Training without an Anchor selection is rejected"), Practice.Sim.command(Rejected).accepted);
    Practice.Sim.debugSpawn(cinder::Kind::Worker, 0, {1120, 620});
    Practice.Tutorial.Observe(Practice.Sim, {});
    TestEqual(TEXT("A sixth Drudge without an accepted training order does not complete the lesson"),
        StepNumber(Practice.Tutorial), static_cast<int32>(ECinderTutorialStep::TrainWorker));

    TestTrue(TEXT("A global Drudge job is accepted without selecting the Anchor"), Practice.Train(cinder::Kind::Worker));
    const cinder::Id Anchor = FindKind(Practice.Sim, cinder::Kind::Headquarters);
    cinder::Command Cancel;
    Cancel.type = cinder::CommandType::CancelQueue;
    Cancel.team = 0;
    Cancel.units = {Anchor};
    Cancel.queueIndex = 0;
    TestTrue(TEXT("The paid queue item can be canceled under normal rules"), Practice.Sim.command(Cancel).accepted);
    for (int32 Second = 0; Second < 15; ++Second) Practice.Update();
    TestEqual(TEXT("Canceling the accepted queue item prevents training completion"),
        StepNumber(Practice.Tutorial), static_cast<int32>(ECinderTutorialStep::TrainWorker));

    cinder::Config Other;
    Other.seed = FCinderTutorial::Seed + 1;
    Other.ai = false;
    Practice.Sim.reset(Other);
    Practice.Tutorial.TickOpponent(Practice.Sim);
    TestFalse(TEXT("Ticking the tutorial opponent against a different match seed clears practice state"),
        Practice.Tutorial.IsActive());
    Practice.Tutorial.Start(Practice.Sim, 0);
    TestFalse(TEXT("A different seed cannot start practice state"), Practice.Tutorial.IsActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTutorialEmberGate,
    "Cinderline.Tutorial.EmberGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTutorialEmberGate::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FPractice Practice;
    TestTrue(TEXT("The practice fixture initializes"), Practice.Initialize());
    TestTrue(TEXT("The opening Drudge is produced through the Anchor queue"),
        Practice.CompleteIntroduction() && Practice.Train(cinder::Kind::Worker) &&
        Practice.WaitForStep(ECinderTutorialStep::BuildKiln, 35));
    TestEqual(TEXT("Build Kiln action opens the persistent BUILD catalog before placement"),
        static_cast<int32>(Practice.Tutorial.PrimaryAction(Practice.Sim)),
        static_cast<int32>(ECinderTutorialPrimaryAction::OpenBuild));
    Practice.Sim.debugSpawn(cinder::Kind::Foundry, 0, {1080, 520});
    Practice.Tutorial.Observe(Practice.Sim, {});
    TestEqual(TEXT("A completed Kiln reaches Ember production"),
        StepNumber(Practice.Tutorial), static_cast<int32>(ECinderTutorialStep::TrainEmbers));
    TestEqual(TEXT("Ember production lesson opens the persistent TRAIN catalog"),
        static_cast<int32>(Practice.Tutorial.PrimaryAction(Practice.Sim)),
        static_cast<int32>(ECinderTutorialPrimaryAction::OpenTrain));

    Practice.Sim.debugSpawn(cinder::Kind::Striker, 0, {1000, 1050});
    Practice.Sim.debugSpawn(cinder::Kind::Striker, 0, {1060, 1050});
    Practice.Sim.debugSpawn(cinder::Kind::Striker, 0, {1120, 1050});
    Practice.Tutorial.Observe(Practice.Sim, {});
    TestEqual(TEXT("Three Embers without accepted training orders do not complete the objective"),
        StepNumber(Practice.Tutorial), static_cast<int32>(ECinderTutorialStep::TrainEmbers));

    TestTrue(TEXT("Three paid Ember queue items are accepted"), Practice.Train(cinder::Kind::Striker, 3));
    const cinder::Id Kiln = FindKind(Practice.Sim, cinder::Kind::Foundry);
    bool bAllCanceled = true;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        cinder::Command Cancel;
        Cancel.type = cinder::CommandType::CancelQueue;
        Cancel.team = 0;
        Cancel.units = {Kiln};
        Cancel.queueIndex = 0;
        bAllCanceled &= Practice.Sim.command(Cancel).accepted;
    }
    TestTrue(TEXT("All accepted Ember queue items can be canceled"), bAllCanceled);
    for (int32 Second = 0; Second < 30; ++Second) Practice.Update();
    TestEqual(TEXT("Canceled Ember orders do not borrow already present units for completion"),
        StepNumber(Practice.Tutorial), static_cast<int32>(ECinderTutorialStep::TrainEmbers));
    TestTrue(TEXT("The canceled Ember objective accepts three replacement queue items"),
        Practice.Train(cinder::Kind::Striker, 3));
    TestTrue(TEXT("Replacement Embers recover the canceled objective through normal production"),
        Practice.WaitForStep(ECinderTutorialStep::BuildSiphon, 105));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTutorialEarlyActionsAndContent,
    "Cinderline.Tutorial.EarlyActionsAndContent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTutorialEarlyActionsAndContent::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FPractice Practice;
    TestTrue(TEXT("The practice fixture initializes"), Practice.Initialize());
    TestTrue(TEXT("A Drudge may be queued before its objective is current"), Practice.Train(cinder::Kind::Worker));
    for (int32 Second = 0; Second < 20 && CountKind(Practice.Sim, cinder::Kind::Worker) < 6; ++Second) Practice.Update();
    TestEqual(TEXT("Completing an early objective does not bypass the camera lesson"),
        StepNumber(Practice.Tutorial), static_cast<int32>(ECinderTutorialStep::Camera));
    TestTrue(TEXT("The introduction later credits the finished early Drudge"), Practice.CompleteIntroduction());
    Practice.Tutorial.Observe(Practice.Sim, {});
    TestEqual(TEXT("The latched early milestone advances to construction"),
        StepNumber(Practice.Tutorial), static_cast<int32>(ECinderTutorialStep::BuildKiln));

    cinder::Id Foundation = 0;
    TestTrue(TEXT("A normal Kiln foundation can be placed"), Practice.Build(cinder::Kind::Foundry, &Foundation));
    const cinder::Entity* Site = Practice.Sim.find(Foundation);
    cinder::Vec2 FoundationFocus;
    cinder::Id FoundationFocusEntity = 0;
    TestTrue(TEXT("Build Kiln focus resolves the placed foundation"),
        Practice.Tutorial.FocusPoint(Practice.Sim, FoundationFocus, &FoundationFocusEntity));
    TestEqual(TEXT("Placed construction changes the tutorial action to world focus"),
        static_cast<int32>(Practice.Tutorial.PrimaryAction(Practice.Sim)),
        static_cast<int32>(ECinderTutorialPrimaryAction::FocusWorld));
    TestTrue(TEXT("Build Kiln focus selects the exact unfinished Kiln site"),
        Site && FoundationFocusEntity == Foundation && Site->pos.x == FoundationFocus.x
        && Site->pos.y == FoundationFocus.y);
    for (int32 Second = 0; Second < 20 && Site && Site->progress <= 0.0f; ++Second)
    {
        Practice.Update();
        Site = Practice.Sim.find(Foundation);
    }
    const FString EarlierProgress = Practice.Tutorial.Text(Practice.Sim, false).Progress;
    Practice.Update(3.0f);
    const FString LaterProgress = Practice.Tutorial.Text(Practice.Sim, false).Progress;
    TestNotEqual(TEXT("The objective reports changing simulation construction progress"), EarlierProgress, LaterProgress);

    cinder::Command Cancel;
    Cancel.type = cinder::CommandType::CancelBuilding;
    Cancel.team = 0;
    Cancel.units = {Foundation};
    TestTrue(TEXT("The unfinished Kiln can be canceled"), Practice.Sim.command(Cancel).accepted);
    Practice.Tutorial.Observe(Practice.Sim, {});
    TestEqual(TEXT("A canceled foundation does not complete construction training"),
        StepNumber(Practice.Tutorial), static_cast<int32>(ECinderTutorialStep::BuildKiln));
    TestTrue(TEXT("The canceled Kiln objective accepts a replacement foundation"),
        Practice.Build(cinder::Kind::Foundry));
    TestTrue(TEXT("A normally completed replacement Kiln recovers the objective"),
        Practice.WaitForStep(ECinderTutorialStep::TrainEmbers, 110));

    for (int32 Topic = 0; Topic < CinderHelp::TopicCount; ++Topic)
    {
        const FCinderHelpPage Page = CinderHelp::Page(Topic, false);
        TestTrue(TEXT("Every guide topic has a title"), !Page.Title.IsEmpty());
        TestTrue(TEXT("Regular guide pages stay within three sections"), Page.Sections.Num() > 0 && Page.Sections.Num() <= 3);
    }
    TSet<FString> ReferenceNames;
    for (int32 Index = 0; Index < CinderHelp::ReferenceCount; ++Index)
    {
        const FCinderHelpPage Page = CinderHelp::Page(CinderHelp::ReferencePage, false, Index);
        ReferenceNames.Add(Page.Title);
        TestEqual(TEXT("Each reference uses the role, stats, and unlock blocks"), Page.Sections.Num(), 3);
        TestTrue(TEXT("Reference stats and unlock data are populated"), !Page.Sections[1].Body.IsEmpty() && !Page.Sections[2].Body.IsEmpty());
    }
    TestEqual(TEXT("The reference covers every simulation kind once"), ReferenceNames.Num(), CinderHelp::ReferenceCount);
    TestNotEqual(TEXT("Touch and desktop control instructions differ"),
        CinderHelp::Page(0, true).Sections[0].Body, CinderHelp::Page(0, false).Sections[0].Body);
    TestTrue(TEXT("Construction guide teaches automatic reachable-worker assignment"),
        CinderHelp::Page(2, false).Sections[0].Body.Contains(TEXT("assigned")));
    TestTrue(TEXT("Production guide teaches the global TRAIN catalog"),
        CinderHelp::Page(3, false).Sections[0].Body.Contains(TEXT("TRAIN")));
    TestTrue(TEXT("Research guide teaches the global RESEARCH catalog"),
        CinderHelp::Page(5, false).Sections[2].Body.Contains(TEXT("RESEARCH")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTutorialLateCancellationRecovery,
    "Cinderline.Tutorial.LateCancellationRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTutorialLateCancellationRecovery::RunTest(const FString& Parameters)
{
    (void)Parameters;
    cinder::Simulation Sim;
    FCinderTutorial Tutorial;
    if (!TestTrue(TEXT("The authored guided scenario initializes"), Tutorial.InitializeScenario(Sim))) return false;
    FString Failure;
    if (!TestTrue(*FString::Printf(TEXT("Normal commands reach Resonator construction: %s"), *Failure),
        DriveGuidedTutorialToStep(Sim, Tutorial, ECinderTutorialStep::BuildResonator, Failure))) return false;

    if (!TestTrue(TEXT("A normal Resonator foundation can be placed"),
        CinderGuidedTestDriver::BuildStructure(Sim, Tutorial, cinder::Kind::Laboratory, Failure))) return false;
    const cinder::Id Foundation = CinderGuidedTestDriver::IncompleteFriendlyOfKind(Sim, cinder::Kind::Laboratory);
    cinder::Command CancelBuilding;
    CancelBuilding.type = cinder::CommandType::CancelBuilding;
    CancelBuilding.team = 0;
    CancelBuilding.units = {Foundation};
    TestTrue(TEXT("The unfinished Resonator can be canceled under normal rules"),
        Foundation != 0 && Sim.command(CancelBuilding).accepted);
    Tutorial.Observe(Sim, {});
    TestTrue(TEXT("Canceled Resonator construction leaves its objective active"),
        Tutorial.Step() == ECinderTutorialStep::BuildResonator);

    Failure.Reset();
    if (!TestTrue(*FString::Printf(TEXT("A replacement Resonator recovers construction: %s"), *Failure),
        DriveGuidedTutorialToStep(Sim, Tutorial, ECinderTutorialStep::ResearchWeapons, Failure))) return false;
    TestEqual(TEXT("Research lesson opens the persistent RESEARCH catalog"),
        static_cast<int32>(Tutorial.PrimaryAction(Sim)),
        static_cast<int32>(ECinderTutorialPrimaryAction::OpenResearch));
    if (!TestTrue(TEXT("Weapons research can be queued through the replacement Resonator"),
        CinderGuidedTestDriver::QueueWeapons(Sim, Tutorial, Failure))) return false;
    const cinder::Id Laboratory = CinderGuidedTestDriver::CompleteFriendlyOfKind(Sim, cinder::Kind::Laboratory);
    cinder::Command CancelResearch;
    CancelResearch.type = cinder::CommandType::CancelQueue;
    CancelResearch.team = 0;
    CancelResearch.units = {Laboratory};
    CancelResearch.queueIndex = 0;
    TestTrue(TEXT("Queued weapons research can be canceled under normal rules"),
        Laboratory != 0 && Sim.command(CancelResearch).accepted);
    for (int32 Second = 0; Second < 10; ++Second)
        CinderGuidedTestDriver::AdvanceOneSecond(Sim, Tutorial);
    TestTrue(TEXT("Canceled weapons research leaves its objective active"),
        Tutorial.Step() == ECinderTutorialStep::ResearchWeapons && Sim.players()[0].weapons == 0);

    Failure.Reset();
    TestTrue(*FString::Printf(TEXT("Replacement weapons research recovers the objective: %s"), *Failure),
        DriveGuidedTutorialToStep(Sim, Tutorial, ECinderTutorialStep::Reinforce, Failure));
    TestTrue(TEXT("Recovered research completes through the normal paid queue"),
        Tutorial.Step() == ECinderTutorialStep::Reinforce && Sim.players()[0].weapons == 1);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTutorialEndToEnd,
    "Cinderline.Tutorial.EndToEnd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTutorialEndToEnd::RunTest(const FString& Parameters)
{
    (void)Parameters;
    cinder::Simulation Sim;
    FCinderTutorial Tutorial;
    if (!TestTrue(TEXT("The authored guided scenario initializes"), Tutorial.InitializeScenario(Sim))) return false;
    TestTrue(TEXT("Guided training uses its fixed local map and keeps strategic AI disabled"),
        Sim.config().map == 0 && Sim.config().seed == FCinderTutorial::Seed && !Sim.config().ai);
    TestEqual(TEXT("The player begins with no granted combat units"), CountKind(Sim, cinder::Kind::Striker), 0);

    const cinder::Id PracticeTarget = Tutorial.PracticeTarget();
    const cinder::Entity* TargetBefore = Sim.find(PracticeTarget);
    TestTrue(TEXT("The authored practice target is a held hostile Ember"),
        TargetBefore && TargetBefore->alive() && TargetBefore->team == 1
        && TargetBefore->kind == cinder::Kind::Striker && TargetBefore->order == cinder::Order::Hold);
    cinder::Vec2 Focus;
    cinder::Id FocusEntity = TargetBefore ? TargetBefore->id : 1;
    if (TargetBefore && !Sim.visible(0, TargetBefore->pos) && Tutorial.FocusPoint(Sim, Focus, &FocusEntity))
    {
        TestFalse(TEXT("A focus hint never discloses the live position of a hidden enemy"),
            FMath::IsNearlyEqual(Focus.x, TargetBefore->pos.x) && FMath::IsNearlyEqual(Focus.y, TargetBefore->pos.y));
        TestEqual(TEXT("A marker focus clears any entity output instead of selecting a hidden enemy"),
            FocusEntity, static_cast<cinder::Id>(0));
        TestEqual(TEXT("Reading the camera focus does not complete the camera lesson"),
            StepNumber(Tutorial), static_cast<int32>(ECinderTutorialStep::Camera));
    }

    FString Failure;
    const bool bReachedAssault = DriveGuidedTutorialToStep(
        Sim, Tutorial, ECinderTutorialStep::DestroyAnchor, Failure);
    if (!TestTrue(*FString::Printf(TEXT("Ordinary player commands reach the final assault: %s"), *Failure),
        bReachedAssault)) return false;
    FocusEntity = PracticeTarget;
    TestTrue(TEXT("The final assault exposes its authored target marker"),
        Tutorial.FocusPoint(Sim, Focus, &FocusEntity));
    TestEqual(TEXT("The enemy Anchor marker never requests entity selection"),
        FocusEntity, static_cast<cinder::Id>(0));
    TestTrue(TEXT("The final assault focus uses the authored enemy Anchor marker"),
        FMath::IsNearlyEqual(Focus.x, FCinderTutorial::EnemyAnchorPoint().x)
        && FMath::IsNearlyEqual(Focus.y, FCinderTutorial::EnemyAnchorPoint().y));
    TestTrue(TEXT("Paid production and research produced the required six Embers and weapons level one"),
        Sim.players()[0].weapons >= 1 && Sim.players()[0].stats.produced >= 8
        && Sim.players()[0].stats.upgrades >= 1);
    TestEqual(TEXT("The finite tutorial opponent issues four production orders, one raid, and one hold"),
        Tutorial.OpponentOrdersIssued(), 6);

    int32 OpponentTrainCommands = 0;
    int32 OpponentAttackMoves = 0;
    int32 OpponentHolds = 0;
    for (const cinder::RecordedCommand& Recorded : Sim.recording())
    {
        const cinder::Command& Command = Recorded.command;
        if (Command.team != 1) continue;
        if (Command.type == cinder::CommandType::Train && Command.kind == cinder::Kind::Striker)
            ++OpponentTrainCommands;
        else if (Command.type == cinder::CommandType::AttackMove)
            ++OpponentAttackMoves;
        else if (Command.type == cinder::CommandType::Hold)
            ++OpponentHolds;
    }
    TestTrue(TEXT("The opponent recording contains four paid Ember queues and the staged orders"),
        OpponentTrainCommands == 4 && OpponentAttackMoves == 1 && OpponentHolds == 2
        && Sim.players()[1].stats.produced == 4);

    const int32 OrdersAfterRaid = Tutorial.OpponentOrdersIssued();
    const int32 ProducedAfterRaid = Sim.players()[1].stats.produced;
    for (int32 Second = 0; Second < 120; ++Second)
        CinderGuidedTestDriver::AdvanceOneSecond(Sim, Tutorial);
    TestTrue(TEXT("The staged opponent stops after its one finite raid"),
        Tutorial.OpponentOrdersIssued() == OrdersAfterRaid
        && Sim.players()[1].stats.produced == ProducedAfterRaid);

    Failure.Reset();
    const bool bCompleted = DriveGuidedTutorialToStep(
        Sim, Tutorial, ECinderTutorialStep::Complete, Failure);
    if (!TestTrue(*FString::Printf(TEXT("The ordinary final assault wins the scenario: %s"), *Failure),
        bCompleted)) return false;
    TestTrue(TEXT("Training completes only after normal combat destroys the opposing Anchor"),
        Tutorial.IsComplete() && Tutorial.IsActive() && Sim.winner() == 0);

    cinder::Simulation RepeatSim;
    FCinderTutorial RepeatTutorial;
    FString RepeatFailure;
    const bool bRepeated = RepeatTutorial.InitializeScenario(RepeatSim)
        && DriveGuidedTutorialToStep(RepeatSim, RepeatTutorial, ECinderTutorialStep::DestroyAnchor, RepeatFailure);
    if (bRepeated)
        for (int32 Second = 0; Second < 120; ++Second)
            CinderGuidedTestDriver::AdvanceOneSecond(RepeatSim, RepeatTutorial);
    const bool bRepeatedCompletion = bRepeated
        && DriveGuidedTutorialToStep(RepeatSim, RepeatTutorial, ECinderTutorialStep::Complete, RepeatFailure);
    TestTrue(*FString::Printf(TEXT("The identical seeded pilot completes again: %s"), *RepeatFailure),
        bRepeatedCompletion);
    TestTrue(TEXT("The fixed scenario and ordinary command pilot reproduce the same terminal state"),
        bRepeatedCompletion && RepeatTutorial.OpponentOrdersIssued() == Tutorial.OpponentOrdersIssued()
        && RepeatSim.tick() == Sim.tick() && RepeatSim.stateHash() == Sim.stateHash());
    if (!HasAnyErrors())
        AddInfo(FString::Printf(
            TEXT("CINDERLINE_GUIDED_ORDINARY_COMPLETE: winner=%d step=%d complete=%d simulated_seconds=%.1f produced=%d lost=%d upgrades=%d opponent_trains=%d opponent_runtime_orders=%d state_hash=%llu repeat_match=%d; ordinary player commands, paid queues, normal combat, no player debug grants or spawns."),
            Sim.winner(), StepNumber(Tutorial), Tutorial.IsComplete(), Sim.time(),
            Sim.players()[0].stats.produced, Sim.players()[0].stats.lost, Sim.players()[0].stats.upgrades,
            OpponentTrainCommands, Tutorial.OpponentOrdersIssued(),
            static_cast<unsigned long long>(Sim.stateHash()),
            bRepeatedCompletion && RepeatSim.tick() == Sim.tick() && RepeatSim.stateHash() == Sim.stateHash()));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTutorialWaitsForLearner,
    "Cinderline.Tutorial.WaitsForLearner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTutorialWaitsForLearner::RunTest(const FString& Parameters)
{
    (void)Parameters;
    cinder::Simulation Sim;
    FCinderTutorial Tutorial;
    if (!TestTrue(TEXT("The authored guided scenario initializes"), Tutorial.InitializeScenario(Sim))) return false;
    for (int32 Second = 0; Second < 180; ++Second)
        CinderGuidedTestDriver::AdvanceOneSecond(Sim, Tutorial);

    int32 OpponentTrainCommands = 0;
    for (const cinder::RecordedCommand& Recorded : Sim.recording())
        if (Recorded.command.team == 1 && Recorded.command.type == cinder::CommandType::Train)
            ++OpponentTrainCommands;
    TestTrue(TEXT("A learner who waits at the camera lesson is not rushed"),
        Tutorial.Step() == ECinderTutorialStep::Camera && Tutorial.OpponentOrdersIssued() == 0
        && OpponentTrainCommands == 0 && Sim.players()[1].stats.produced == 0 && Sim.winner() < 0);
    const cinder::Entity* PracticeTarget = Sim.find(Tutorial.PracticeTarget());
    TestTrue(TEXT("The authored practice target remains held while the learner waits"),
        PracticeTarget && PracticeTarget->alive() && PracticeTarget->order == cinder::Order::Hold);
    return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
