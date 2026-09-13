#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Presentation/CinderTutorial.h"
#include "Tests/CinderGuidedTestDriver.h"

#include <utility>
#include <vector>

namespace
{
bool InitializeGuidance(cinder::Simulation& Sim, FCinderTutorial& Tutorial)
{
    return Tutorial.InitializeScenario(Sim) && Tutorial.IsActive();
}

bool IsTarget(const FCinderTutorialGuide& Guide, ECinderTutorialGuideTarget Target)
{
    return Guide.Target == Target;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTutorialGuidanceIntroduction,
    "Cinderline.Tutorial.Guidance.Introduction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTutorialGuidanceIntroduction::RunTest(const FString& Parameters)
{
    (void)Parameters;
    cinder::Simulation Sim;
    FCinderTutorial Tutorial;
    if (!TestTrue(TEXT("Guidance fixture initializes"), InitializeGuidance(Sim, Tutorial))) return false;

    FCinderTutorialContext Context;
    FCinderTutorialGuide Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("The first microstep targets camera input"), IsTarget(Guide, ECinderTutorialGuideTarget::Camera));
    TestEqual(TEXT("The camera lesson has one action"), Guide.ActionCount, 1);

    Tutorial.CameraInput();
    Context.bAttackMove = true;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestEqual(TEXT("An armed command clears before worker selection"),
        Guide.ButtonAction, FString(TEXT("tutorialclearmode")));
    Context.bAttackMove = false;
    Guide = Tutorial.Guide(Sim, Context, true);
    const cinder::Entity* Worker = Sim.find(Guide.Entity);
    TestTrue(TEXT("The worker lesson targets a living friendly worker"),
        IsTarget(Guide, ECinderTutorialGuideTarget::Entity) && Worker && Worker->alive()
        && Worker->team == 0 && Worker->kind == cinder::Kind::Worker);
    TestTrue(TEXT("The first worker instruction introduces the Drudge name"),
        Guide.Instruction.Contains(TEXT("worker")) && Guide.Explanation.Contains(TEXT("called a Drudge")));

    Context.Selection = {Guide.Entity};
    Tutorial.Observe(Sim, Context.Selection);
    Guide = Tutorial.Guide(Sim, Context, true);
    const cinder::Entity* Ore = Sim.find(Guide.Entity);
    TestTrue(TEXT("Mining targets an actual available ore entity"),
        IsTarget(Guide, ECinderTutorialGuideTarget::Entity) && Ore && Ore->alive()
        && Ore->kind == cinder::Kind::Resource && Ore->resource > 0);

    cinder::Command Gather;
    Gather.type = cinder::CommandType::Gather;
    Gather.team = 0;
    Gather.units = Context.Selection;
    Gather.target = Guide.Entity;
    if (!TestTrue(TEXT("The highlighted ore accepts the normal mining order"), Sim.command(Gather).accepted)) return false;
    Tutorial.AcceptedCommand(Sim, Gather);
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("Accepted mining becomes a real-outcome wait"),
        IsTarget(Guide, ECinderTutorialGuideTarget::Wait) && Guide.bWaiting);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTutorialGuidanceRemainingBatch,
    "Cinderline.Tutorial.Guidance.RemainingBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTutorialGuidanceRemainingBatch::RunTest(const FString& Parameters)
{
    (void)Parameters;
    cinder::Simulation Sim;
    FCinderTutorial Tutorial;
    if (!TestTrue(TEXT("Batch guidance fixture initializes"), InitializeGuidance(Sim, Tutorial))) return false;
    FString Failure;
    if (!TestTrue(*FString::Printf(TEXT("Ordinary play reaches Ember training: %s"), *Failure),
        DriveGuidedTutorialToStep(Sim, Tutorial, ECinderTutorialStep::TrainEmbers, Failure))) return false;
    for (int32 Second = 0; Second < 180 && !Sim.autoTrainStatus(0, cinder::Kind::Striker, 3).accepted; ++Second)
        CinderGuidedTestDriver::AdvanceOneSecond(Sim, Tutorial);
    cinder::Command InitialBatch;
    InitialBatch.type = cinder::CommandType::AutoTrain;
    InitialBatch.team = 0;
    InitialBatch.kind = cinder::Kind::Striker;
    InitialBatch.queueIndex = 3;
    if (!TestTrue(TEXT("The original three-Ember batch is accepted"), Sim.command(InitialBatch).accepted)) return false;
    Tutorial.AcceptedCommand(Sim, InitialBatch);
    std::vector<std::pair<cinder::Id, cinder::Id>> Jobs;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0)
            for (const cinder::QueueItem& Item : Entity.queue)
                if (!Item.research && Item.kind == cinder::Kind::Striker) Jobs.push_back({Entity.id, Item.id});
    for (const auto& Job : Jobs)
    {
        cinder::Command Cancel;
        Cancel.type = cinder::CommandType::CancelQueue;
        Cancel.team = 0;
        Cancel.units = {Job.first};
        Cancel.target = Job.second;
        if (!TestTrue(TEXT("Each canceled batch item uses its stable job identity"), Sim.command(Cancel).accepted)) return false;
    }
    if (!TestTrue(TEXT("A completed Ember can model one surviving unit from a canceled batch"),
        Sim.debugSpawn(cinder::Kind::Striker, 0, {1450, 650}) != 0)) return false;
    for (int32 Second = 0; Second < 180 && !Sim.autoTrainStatus(0, cinder::Kind::Striker, 2).accepted; ++Second)
        CinderGuidedTestDriver::AdvanceOneSecond(Sim, Tutorial);
    if (!TestTrue(TEXT("Two replacement Embers become affordable"),
        Sim.autoTrainStatus(0, cinder::Kind::Striker, 2).accepted)) return false;

    FCinderTutorialContext Context;
    Context.Catalog = 8;
    Context.bTrainKindChosen = true;
    Context.TrainKind = cinder::Kind::Striker;
    FCinderTutorialGuide Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("A remaining quantity of two starts from the supported x1 preset"),
        Guide.ButtonAction == TEXT("trainqty") && Guide.ButtonArgument == 1);
    Context.bTrainQuantityChosen = true;
    Context.TrainQuantity = 1;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("Quantity two uses one exact plus action"),
        Guide.ButtonAction == TEXT("trainqtydelta") && Guide.ButtonArgument == 1);
    Context.TrainQuantity = 2;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestEqual(TEXT("The adjusted remaining batch can now be queued"),
        Guide.ButtonAction, FString(TEXT("globaltrain")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTutorialGuidanceInterruptedOrders,
    "Cinderline.Tutorial.Guidance.InterruptedOrders",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTutorialGuidanceInterruptedOrders::RunTest(const FString& Parameters)
{
    (void)Parameters;
    cinder::Simulation Sim;
    FCinderTutorial Tutorial;
    if (!TestTrue(TEXT("Interrupted-order fixture initializes"), InitializeGuidance(Sim, Tutorial))) return false;
    FString Failure;
    if (!TestTrue(*FString::Printf(TEXT("Ordinary play reaches scouting: %s"), *Failure),
        DriveGuidedTutorialToStep(Sim, Tutorial, ECinderTutorialStep::Scout, Failure))) return false;
    if (!TestTrue(TEXT("A normal Skim job can be queued"),
        CinderGuidedTestDriver::QueueUnit(Sim, Tutorial, cinder::Kind::Scout, Failure))) return false;
    if (!TestTrue(TEXT("The queued Skim completes"), CinderGuidedTestDriver::WaitUntil(Sim, Tutorial,
        [&] { return CinderGuidedTestDriver::CompleteFriendlyCount(Sim, cinder::Kind::Scout) > 0; },
        50, TEXT("The paid Skim did not finish training."), Failure))) return false;
    const cinder::Id Scout = CinderGuidedTestDriver::CompleteFriendlyOfKind(Sim, cinder::Kind::Scout);
    cinder::Command ScoutMove;
    ScoutMove.type = cinder::CommandType::Move;
    ScoutMove.team = 0;
    ScoutMove.units = {Scout};
    ScoutMove.point = FCinderTutorial::ScoutPoint();
    if (!TestTrue(TEXT("The normal scout order is accepted"),
        CinderGuidedTestDriver::Issue(Sim, Tutorial, ScoutMove, Failure))) return false;
    FCinderTutorialContext Context;
    Context.Selection = {Scout};
    FCinderTutorialGuide Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("An active scout order waits for its real outcome"),
        IsTarget(Guide, ECinderTutorialGuideTarget::Wait));

    cinder::Command Stop;
    Stop.type = cinder::CommandType::Stop;
    Stop.team = 0;
    Stop.units = {Scout};
    if (!TestTrue(TEXT("The Skim accepts a normal stop"),
        CinderGuidedTestDriver::Issue(Sim, Tutorial, Stop, Failure))) return false;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("A stopped Skim receives an actionable replacement order"),
        IsTarget(Guide, ECinderTutorialGuideTarget::Button) && Guide.ButtonAction == TEXT("attack"));

    ScoutMove.type = cinder::CommandType::AttackMove;
    if (!TestTrue(TEXT("The replacement scout order is accepted"),
        CinderGuidedTestDriver::Issue(Sim, Tutorial, ScoutMove, Failure))) return false;
    if (!TestTrue(TEXT("The replacement order reaches the combat lesson"),
        CinderGuidedTestDriver::WaitForStep(Sim, Tutorial, ECinderTutorialStep::AttackMove, 45, Failure))) return false;

    const std::vector<cinder::Id> Embers = CinderGuidedTestDriver::CompleteFriendly(Sim, cinder::Kind::Striker);
    if (!TestTrue(TEXT("An Ember survives for interrupted attack-move coverage"), !Embers.empty())) return false;
    cinder::Command AttackMove;
    AttackMove.type = cinder::CommandType::AttackMove;
    AttackMove.team = 0;
    AttackMove.units = Embers;
    AttackMove.point = FCinderTutorial::CombatPoint();
    if (!TestTrue(TEXT("The combat attack-move is accepted"),
        CinderGuidedTestDriver::Issue(Sim, Tutorial, AttackMove, Failure))) return false;
    Context.Selection = Embers;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("A live combat order waits for the hostile target"),
        IsTarget(Guide, ECinderTutorialGuideTarget::Wait));
    Stop.units = Embers;
    if (!TestTrue(TEXT("The Ember squad accepts a normal stop"),
        CinderGuidedTestDriver::Issue(Sim, Tutorial, Stop, Failure))) return false;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("A stopped Ember receives a targetable replacement order"),
        IsTarget(Guide, ECinderTutorialGuideTarget::Button) && Guide.ButtonAction == TEXT("attack"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTutorialGuidanceEconomy,
    "Cinderline.Tutorial.Guidance.EconomySequence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTutorialGuidanceEconomy::RunTest(const FString& Parameters)
{
    (void)Parameters;
    cinder::Simulation Sim;
    FCinderTutorial Tutorial;
    if (!TestTrue(TEXT("Economy guidance fixture initializes"), InitializeGuidance(Sim, Tutorial))) return false;
    FString Failure;
    if (!TestTrue(*FString::Printf(TEXT("Ordinary play reaches worker training: %s"), *Failure),
        DriveGuidedTutorialToStep(Sim, Tutorial, ECinderTutorialStep::TrainWorker, Failure))) return false;
    for (int32 Second = 0; Second < 120 && !Sim.autoTrainStatus(0, cinder::Kind::Worker, 1).accepted; ++Second)
        CinderGuidedTestDriver::AdvanceOneSecond(Sim, Tutorial);
    if (!TestTrue(TEXT("Normal mining makes the guided Drudge order affordable"),
        Sim.autoTrainStatus(0, cinder::Kind::Worker, 1).accepted)) return false;

    FCinderTutorialContext Context;
    FCinderTutorialGuide Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("Worker training first opens TRAIN"), IsTarget(Guide, ECinderTutorialGuideTarget::Button)
        && Guide.ButtonAction == TEXT("globalcatalog") && Guide.ButtonArgument == 8);
    Context.Catalog = 8;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("TRAIN next targets the Drudge portrait"), Guide.ButtonAction == TEXT("trainkind")
        && Guide.ButtonArgument == static_cast<int32>(cinder::Kind::Worker));
    Context.bTrainKindChosen = true;
    Context.TrainKind = cinder::Kind::Worker;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("TRAIN requires an explicit quantity choice"),
        Guide.ButtonAction == TEXT("trainqty") && Guide.ButtonArgument == 1);
    Context.bTrainQuantityChosen = true;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestEqual(TEXT("The fourth training action is the real queue button"), Guide.ButtonAction, FString(TEXT("globaltrain")));

    cinder::Command Train;
    Train.type = cinder::CommandType::AutoTrain;
    Train.team = 0;
    Train.kind = cinder::Kind::Worker;
    Train.queueIndex = 1;
    if (!TestTrue(TEXT("The guided global train command is accepted"), Sim.command(Train).accepted)) return false;
    Tutorial.AcceptedCommand(Sim, Train);
    Guide = Tutorial.Guide(Sim, Context, true);
    TestEqual(TEXT("The catalog closes before the waiting instruction"), Guide.ButtonAction, FString(TEXT("closesheet")));
    Context.Catalog = 0;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("Training waits for the produced Drudge"), IsTarget(Guide, ECinderTutorialGuideTarget::Wait) && Guide.bWaiting);

    for (int32 Second = 0; Second < 60 && Tutorial.Step() != ECinderTutorialStep::BuildKiln; ++Second)
        CinderGuidedTestDriver::AdvanceOneSecond(Sim, Tutorial);
    if (!TestEqual(TEXT("The real produced unit advances to Kiln construction"),
        static_cast<int32>(Tutorial.Step()), static_cast<int32>(ECinderTutorialStep::BuildKiln))) return false;
    for (int32 Second = 0; Second < 180 && !Sim.autoBuildStatus(0, cinder::Kind::Foundry).accepted; ++Second)
        CinderGuidedTestDriver::AdvanceOneSecond(Sim, Tutorial);
    if (!TestTrue(TEXT("Normal mining makes Kiln construction available"),
        Sim.autoBuildStatus(0, cinder::Kind::Foundry).accepted)) return false;

    Context = {};
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("Construction first opens BUILD"), Guide.ButtonAction == TEXT("globalcatalog") && Guide.ButtonArgument == 7);
    Context.Catalog = 7;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("BUILD targets the named Kiln option"), Guide.ButtonAction == TEXT("globalbuild")
        && Guide.ButtonArgument == static_cast<int32>(cinder::Kind::Foundry)
        && Guide.Explanation.Contains(TEXT("trains Ember")));
    Context.Catalog = 0;
    Context.bBuildMode = true;
    Context.BuildingKind = cinder::Kind::Foundry;
    const FCinderTutorialGuide FirstSite = Tutorial.Guide(Sim, Context, true);
    const FCinderTutorialGuide CachedSite = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("Build guidance returns a legal authoritative ground target"),
        IsTarget(FirstSite, ECinderTutorialGuideTarget::Ground)
        && Sim.autoBuildStatus(0, cinder::Kind::Foundry, &FirstSite.Point).accepted);
    TestTrue(TEXT("The legal site remains stable while simulation state is unchanged"),
        FirstSite.Point.x == CachedSite.Point.x && FirstSite.Point.y == CachedSite.Point.y);

    cinder::Command Build;
    Build.type = cinder::CommandType::AutoBuild;
    Build.team = 0;
    Build.kind = cinder::Kind::Foundry;
    Build.point = FirstSite.Point;
    if (!TestTrue(TEXT("The highlighted Kiln site accepts normal construction"), Sim.command(Build).accepted)) return false;
    Tutorial.AcceptedCommand(Sim, Build);
    cinder::Id Foundation = 0;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == cinder::Kind::Foundry && Entity.progress < 1.0f)
            Foundation = Entity.id;
    const cinder::Id Builder = Sim.constructionWorker(Foundation);
    cinder::Command Stop;
    Stop.type = cinder::CommandType::Stop;
    Stop.team = 0;
    Stop.units = {Builder};
    if (!TestTrue(TEXT("Stopping the assigned worker creates a real abandoned site"),
        Foundation != 0 && Builder != 0 && Sim.command(Stop).accepted
        && Sim.constructionWorker(Foundation) == 0)) return false;

    Context = {};
    Context.Catalog = 8;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestEqual(TEXT("An unrelated drawer closes before the abandoned site target"),
        Guide.ButtonAction, FString(TEXT("closesheet")));
    Context.Catalog = 0;
    Context.bAttackMove = true;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestEqual(TEXT("An armed command clears before the abandoned site target"),
        Guide.ButtonAction, FString(TEXT("tutorialclearmode")));
    Context.bAttackMove = false;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("Recovery targets the exact unfinished Kiln"),
        IsTarget(Guide, ECinderTutorialGuideTarget::Entity) && Guide.Entity == Foundation);
    Context.Selection = {Foundation};
    Context.bCompact = true;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("Compact recovery opens the actual SITE context sheet"),
        Guide.ButtonAction == TEXT("sheet") && Guide.ButtonArgument == 1);
    Context.Catalog = 1;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestEqual(TEXT("SITE exposes the real ASSIGN DRUDGE action"),
        Guide.ButtonAction, FString(TEXT("resumeconstruction")));
    cinder::Command Resume;
    Resume.type = cinder::CommandType::ResumeConstruction;
    Resume.team = 0;
    Resume.target = Foundation;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == cinder::Kind::Worker && Entity.progress >= 1.0f)
            Resume.units.push_back(Entity.id);
    if (!TestTrue(TEXT("The abandoned site accepts a replacement Drudge"), Sim.command(Resume).accepted)) return false;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("Recovery closes SITE before waiting"),
        Guide.ButtonAction == TEXT("closesheet") && Guide.ActionIndex == 7 && Guide.ActionCount == 8);
    Context.Catalog = 0;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("Recovered construction keeps a stable final wait step"),
        IsTarget(Guide, ECinderTutorialGuideTarget::Wait) && Guide.bWaiting
        && Guide.ActionIndex == 8 && Guide.ActionCount == 8);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTutorialGuidanceArmy,
    "Cinderline.Tutorial.Guidance.ArmySequence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTutorialGuidanceArmy::RunTest(const FString& Parameters)
{
    (void)Parameters;
    cinder::Simulation Sim;
    FCinderTutorial Tutorial;
    if (!TestTrue(TEXT("Army guidance fixture initializes"), InitializeGuidance(Sim, Tutorial))) return false;
    FString Failure;
    if (!TestTrue(*FString::Printf(TEXT("Ordinary play reaches reinforcement: %s"), *Failure),
        DriveGuidedTutorialToStep(Sim, Tutorial, ECinderTutorialStep::Reinforce, Failure))) return false;

    FCinderTutorialContext RallyContext;
    FCinderTutorialGuide RallyGuide = Tutorial.Guide(Sim, RallyContext, true);
    const cinder::Id Kiln = RallyGuide.Entity;
    TestTrue(TEXT("Rally guidance first targets a completed Kiln"),
        IsTarget(RallyGuide, ECinderTutorialGuideTarget::Entity) && Kiln != 0);
    RallyContext.Selection = {Kiln};
    RallyContext.bCompact = true;
    RallyGuide = Tutorial.Guide(Sim, RallyContext, true);
    TestTrue(TEXT("Compact rally opens the selected Kiln TRAIN panel"),
        RallyGuide.ButtonAction == TEXT("globalcatalog") && RallyGuide.ButtonArgument == 8
        && RallyGuide.ButtonEntity == Kiln);
    RallyContext.Catalog = 8;
    RallyContext.PinnedProducer = Kiln;
    RallyGuide = Tutorial.Guide(Sim, RallyContext, true);
    TestTrue(TEXT("The pinned catalog targets RALLY THIS"),
        RallyGuide.ButtonAction == TEXT("productionrally") && RallyGuide.ButtonEntity == Kiln);
    RallyContext.bProductionRally = true;
    RallyGuide = Tutorial.Guide(Sim, RallyContext, true);
    TestTrue(TEXT("Armed pinned rally targets the exact defense point without closing its panel"),
        IsTarget(RallyGuide, ECinderTutorialGuideTarget::Ground)
        && RallyGuide.Point.x == FCinderTutorial::DefensePoint().x
        && RallyGuide.Point.y == FCinderTutorial::DefensePoint().y);

    FCinderTutorialContext DesktopRally;
    DesktopRally.Selection = {Kiln};
    RallyGuide = Tutorial.Guide(Sim, DesktopRally, false);
    TestTrue(TEXT("Desktop rally opens the selected Kiln JOBS panel"),
        RallyGuide.ButtonAction == TEXT("producerjobs") && RallyGuide.ButtonEntity == Kiln);
    DesktopRally.Catalog = 5;
    DesktopRally.ArmyTab = 2;
    DesktopRally.PinnedProducer = Kiln;
    RallyGuide = Tutorial.Guide(Sim, DesktopRally, false);
    TestTrue(TEXT("Desktop JOBS targets the same Kiln rally button"),
        RallyGuide.ButtonAction == TEXT("productionrally") && RallyGuide.ButtonEntity == Kiln);

    Failure.Reset();
    if (!TestTrue(*FString::Printf(TEXT("Ordinary play reaches defense: %s"), *Failure),
        DriveGuidedTutorialToStep(Sim, Tutorial, ECinderTutorialStep::Defend, Failure))) return false;

    FCinderTutorialContext Context;
    FCinderTutorialGuide Guide;
    for (int32 Second = 0; Second < 180; ++Second)
    {
        Guide = Tutorial.Guide(Sim, Context, true);
        if (Guide.Target != ECinderTutorialGuideTarget::Wait) break;
        CinderGuidedTestDriver::AdvanceOneSecond(Sim, Tutorial);
    }
    if (!TestTrue(TEXT("The real raid launch exposes the ARMY action"),
        Tutorial.Step() == ECinderTutorialStep::Defend
        && Guide.Target == ECinderTutorialGuideTarget::Button
        && Guide.ButtonAction == TEXT("army") && Guide.ButtonArgument == 0)) return false;
    Context.Catalog = 5;
    Context.ArmyTab = 2;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("The JOBS tab first returns to the visible ROSTER tab"),
        Guide.ButtonAction == TEXT("armytab") && Guide.ButtonArgument == 0);
    Context.ArmyTab = 0;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestEqual(TEXT("The army drawer targets ALL"), Guide.ButtonAction, FString(TEXT("armyall")));
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != 0 || Entity.progress < 1.0f) continue;
        const cinder::Definition& Definition = cinder::definition(Entity.kind);
        if (!Definition.building && Entity.kind != cinder::Kind::Worker && Entity.kind != cinder::Kind::Resource)
            Context.Selection.push_back(Entity.id);
    }
    Guide = Tutorial.Guide(Sim, Context, true);
    TestEqual(TEXT("The drawer closes before a world command"), Guide.ButtonAction, FString(TEXT("closesheet")));
    Context.Catalog = 0;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestEqual(TEXT("Defense next targets ATTACK"), Guide.ButtonAction, FString(TEXT("attack")));
    Context.bAttackMove = true;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("The final defense interaction targets the exact ground point"),
        IsTarget(Guide, ECinderTutorialGuideTarget::Ground)
        && Guide.Point.x == FCinderTutorial::DefensePoint().x
        && Guide.Point.y == FCinderTutorial::DefensePoint().y);
    cinder::Command Defend;
    Defend.type = cinder::CommandType::AttackMove;
    Defend.team = 0;
    Defend.units = Context.Selection;
    Defend.point = Guide.Point;
    if (!TestTrue(TEXT("The highlighted defense order is accepted"),
        CinderGuidedTestDriver::Issue(Sim, Tutorial, Defend, Failure))) return false;
    Context.bAttackMove = false;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("An active defense order waits for the raiders"),
        IsTarget(Guide, ECinderTutorialGuideTarget::Wait));
    cinder::Command Stop;
    Stop.type = cinder::CommandType::Stop;
    Stop.team = 0;
    Stop.units = Context.Selection;
    if (!TestTrue(TEXT("The defending army accepts a normal stop"),
        CinderGuidedTestDriver::Issue(Sim, Tutorial, Stop, Failure))) return false;
    Guide = Tutorial.Guide(Sim, Context, true);
    TestTrue(TEXT("Stopped defenders return to an actionable ATTACK step"),
        IsTarget(Guide, ECinderTutorialGuideTarget::Button) && Guide.ButtonAction == TEXT("attack"));
    return !HasAnyErrors();
}

#endif
