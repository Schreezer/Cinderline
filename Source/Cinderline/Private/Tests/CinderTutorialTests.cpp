#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Presentation/CinderHelpContent.h"
#include "Presentation/CinderTutorial.h"
#include "Sim/Simulation.h"

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

std::vector<cinder::Id> Workers(const cinder::Simulation& Sim)
{
    std::vector<cinder::Id> Result;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == cinder::Kind::Worker && Entity.progress >= 1.0f) Result.push_back(Entity.id);
    return Result;
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
        const cinder::Kind ProducerKind = cinder::definition(Kind).producer;
        const cinder::Id Producer = FindKind(Sim, ProducerKind);
        if (!Producer) return false;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            cinder::Command Command;
            Command.type = cinder::CommandType::Train;
            Command.team = 0;
            Command.units = {Producer};
            Command.kind = Kind;
            if (!Accept(Command)) return false;
        }
        return true;
    }

    bool Build(cinder::Kind Kind, cinder::Id* OutFoundation = nullptr)
    {
        const std::vector<cinder::Id> Selected = Workers(Sim);
        for (float Y = 350.0f; Y <= 1450.0f; Y += 100.0f)
        {
            for (float X = 750.0f; X <= 1550.0f; X += 100.0f)
            {
                const cinder::Vec2 Point{X, Y};
                if (!Sim.buildStatus(0, Kind, Selected, &Point).accepted) continue;
                cinder::Command Command;
                Command.type = cinder::CommandType::Build;
                Command.team = 0;
                Command.units = Selected;
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

    cinder::Command Rejected;
    Rejected.type = cinder::CommandType::Train;
    Rejected.team = 0;
    Rejected.kind = cinder::Kind::Worker;
    TestFalse(TEXT("Training without an Anchor selection is rejected"), Practice.Sim.command(Rejected).accepted);
    Practice.Sim.debugSpawn(cinder::Kind::Worker, 0, {1120, 620});
    Practice.Tutorial.Observe(Practice.Sim, {});
    TestEqual(TEXT("A sixth Drudge without an accepted training order does not complete the lesson"),
        StepNumber(Practice.Tutorial), static_cast<int32>(ECinderTutorialStep::TrainWorker));

    TestTrue(TEXT("A real Drudge queue item is accepted"), Practice.Train(cinder::Kind::Worker));
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
    Practice.Tutorial.Observe(Practice.Sim, {});
    TestFalse(TEXT("Observing a different match seed clears practice state"), Practice.Tutorial.IsActive());
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
    Practice.Sim.debugSpawn(cinder::Kind::Foundry, 0, {1080, 520});
    Practice.Tutorial.Observe(Practice.Sim, {});
    TestEqual(TEXT("A completed Kiln reaches Ember production"),
        StepNumber(Practice.Tutorial), static_cast<int32>(ECinderTutorialStep::TrainEmbers));

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
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTutorialEndToEnd,
    "Cinderline.Tutorial.EndToEnd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTutorialEndToEnd::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FPractice Practice;
    TestTrue(TEXT("The practice fixture initializes"), Practice.Initialize());
    TestTrue(TEXT("Ordinary commands and updates reach the final objective"), Practice.ReachAttackMove());
    if (Practice.Tutorial.Step() != ECinderTutorialStep::AttackMove) return false;

    const cinder::Entity* TargetBefore = Practice.Sim.find(Practice.Target);
    cinder::Vec2 Focus;
    if (TargetBefore && TargetBefore->alive() && !Practice.Sim.visible(0, TargetBefore->pos) && Practice.Tutorial.FocusPoint(Practice.Sim, Focus))
        TestFalse(TEXT("A focus hint never discloses the live position of a hidden enemy"),
            FMath::IsNearlyEqual(Focus.x, TargetBefore->pos.x) && FMath::IsNearlyEqual(Focus.y, TargetBefore->pos.y));

    std::vector<cinder::Id> Embers;
    for (const cinder::Entity& Entity : Practice.Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == cinder::Kind::Striker) Embers.push_back(Entity.id);
    cinder::Command UnreportedAttackMove;
    UnreportedAttackMove.type = cinder::CommandType::AttackMove;
    UnreportedAttackMove.team = 0;
    UnreportedAttackMove.units = Embers;
    UnreportedAttackMove.point = FCinderTutorial::CombatPoint();
    TestTrue(TEXT("The simulation accepts the Ember attack-move"), Practice.Sim.command(UnreportedAttackMove).accepted);
    for (int32 Second = 0; Second < 60; ++Second)
    {
        Practice.Update();
        const cinder::Entity* Target = Practice.Sim.find(Practice.Target);
        if (!Target || !Target->alive()) break;
    }
    const cinder::Entity* Defeated = Practice.Sim.find(Practice.Target);
    TestTrue(TEXT("Ordinary combat defeats the stationary practice target"), !Defeated || !Defeated->alive());
    TestEqual(TEXT("Target death alone cannot replace the accepted-command callback"),
        StepNumber(Practice.Tutorial), static_cast<int32>(ECinderTutorialStep::AttackMove));

    cinder::Command ReportedAttackMove = UnreportedAttackMove;
    ReportedAttackMove.point = FCinderTutorial::ScoutPoint();
    TestTrue(TEXT("A later explicit Ember attack-move is accepted and recorded"), Practice.Accept(ReportedAttackMove));
    TestTrue(TEXT("Out-of-order target defeat and accepted attack-move complete practice"), Practice.Tutorial.IsComplete());
    TestTrue(TEXT("Completed practice remains active for save protection"), Practice.Tutorial.IsActive());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
