#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderPlayerController.h"
#include "Tests/AutomationCommon.h"

#include <algorithm>
#include <vector>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderProductionControlIntegration,
    "Cinderline.Integration.ProductionControls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderProductionControlIntegration::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace cinder;
    FTestWorldWrapper WorldOwner;
    if (!WorldOwner.CreateTestWorld(EWorldType::Game))
    { WorldOwner.ForwardErrorMessages(this); return false; }
    UWorld* World = WorldOwner.GetTestWorld();
    World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
    auto* Battle = World->SpawnActor<ACinderBattlefield>();
    if (!TestNotNull(TEXT("Transient battlefield exists"), Battle)) return false;
    if (!WorldOwner.BeginPlayInTestWorld())
    { WorldOwner.ForwardErrorMessages(this); return false; }
    auto* PC = World->SpawnActor<ACinderPlayerController>();
    if (!TestNotNull(TEXT("Transient controller exists"), PC)) return false;
    if (PC->IsTutorialOfferPending()) PC->ExecuteAction(TEXT("onboardskip"));
    PC->ExecuteAction(TEXT("start"));
    auto& Sim = Battle->Sim();
    Config Setup; Setup.ai = false; Sim.reset(Setup); Sim.debugResources(0, 9000);
    const Id First = Sim.debugSpawn(Kind::Foundry, 0, {1050, 650});
    const Id Second = Sim.debugSpawn(Kind::Foundry, 0, {1450, 650});
    const Id Lab = Sim.debugSpawn(Kind::Laboratory, 0, {700, 1100});
    const Id Enemy = Sim.debugSpawn(Kind::Foundry, 1, {3400, 3500});
    const Id Soldier = Sim.debugSpawn(Kind::Striker, 0, {900, 1000});
    PC->SelectOwnedEntity(Soldier);
    PC->QueueTraining(Kind::Striker, 6);
    TestEqual(TEXT("Six units are spread across two Kilns"),
        int32(Sim.find(First)->queue.size() + Sim.find(Second)->queue.size()), 6);
    TestTrue(TEXT("Global training preserves the selected army"), PC->Selection() == std::vector<Id>{Soldier});
    TestTrue(TEXT("Automatic command reaches authority without selected army IDs"),
        Sim.recording().back().command.type == CommandType::AutoTrain && Sim.recording().back().command.units.empty());
    PC->QueueTraining(Kind::Scout, 1, Second);
    TestTrue(TEXT("Explicit override queues on the chosen producer"),
        Sim.find(Second)->queue.size() == 4 && Sim.find(Second)->queue.back().kind == Kind::Scout);
    const auto BeforeEnemyAttempt = Sim.stateHash();
    PC->QueueTraining(Kind::Striker, 1, Enemy);
    TestEqual(TEXT("Enemy producer override cannot change gameplay state"), Sim.stateHash(), BeforeEnemyAttempt);
    TestTrue(TEXT("Invalid allocation gives immediate feedback"), !PC->Feedback().IsEmpty());

    const Id CancelledJob = Sim.find(Second)->queue.back().id;
    PC->CancelProduction(Second, CancelledJob);
    TestEqual(TEXT("Global jobs cancel the specific producer's stable job"), int32(Sim.find(Second)->queue.size()), 3);
    const auto BeforeStaleCancel = Sim.stateHash();
    PC->CancelProduction(Second, CancelledJob);
    TestEqual(TEXT("A stale job cannot cancel a different queued unit"), Sim.stateHash(), BeforeStaleCancel);
    TestTrue(TEXT("Job cancellation also preserves selection"), PC->Selection() == std::vector<Id>{Soldier});

    PC->QueueResearch(1);
    TestTrue(TEXT("Research is assigned without selecting a lab"),
        Sim.find(Lab)->queue.size() == 1 && Sim.find(Lab)->queue.front().research);

    PC->BeginProductionRally(Second);
    TestTrue(TEXT("Production rally arms with an army still selected"), PC->IsProductionRallyMode());
    PC->ExecuteAction(TEXT("minimap"), 1500 + 1300 * 10000);
    TestTrue(TEXT("Minimap rally targets the pinned facility and clears the mode"),
        Sim.find(Second)->rally.x == 1500 && Sim.find(Second)->rally.y == 1300
        && Sim.find(Second)->rallyOverride && !PC->IsProductionRallyMode());
    Id Headquarters = 0;
    for (const auto& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == Kind::Headquarters) { Headquarters = Entity.id; break; }
    if (!TestTrue(TEXT("Rally fixture has a headquarters with a separate worker rally"), Headquarters != 0)) return false;

    Id RallyResource = 0;
    for (const auto& Entity : Sim.entities())
        if (Entity.alive() && Entity.kind == Kind::Resource && Entity.resource > 0
            && Sim.explored(0, Entity.pos)) { RallyResource = Entity.id; break; }
    if (!TestTrue(TEXT("Resource rally fixture has explored ore"), RallyResource != 0)) return false;
    const Vec2 ResourceCenter = Sim.find(RallyResource)->pos;
    const Vec2 OffCenterResourceGround{ResourceCenter.x + 41, ResourceCenter.y + 27};
    std::vector<Entity> WorkersBeforeResourceRally;
    for (const auto& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == Kind::Worker)
            WorkersBeforeResourceRally.push_back(Entity);

    if (!TestTrue(TEXT("Resource rally starts from an explicitly selected Anchor"),
        PC->SelectOwnedEntity(Headquarters))) return false;
    const std::size_t BeforeResourceRally = Sim.recording().size();
    PC->BeginProductionRally(Headquarters);
    PC->HandleWorldTap(RallyResource, &OffCenterResourceGround, false);
    if (!TestTrue(TEXT("Anchor resource Rally submits one automatic producer command"),
        Sim.recording().size() == BeforeResourceRally + 1)) return false;
    const auto& ResourceRally = Sim.recording().back().command;
    TestTrue(TEXT("Resource Rally canonicalizes the off-center terrain hit to the ore entity center"),
        ResourceRally.type == CommandType::AutoRally && ResourceRally.target == Headquarters
        && ResourceRally.point.x == ResourceCenter.x && ResourceRally.point.y == ResourceCenter.y
        && Sim.find(Headquarters)->rally.x == ResourceCenter.x
        && Sim.find(Headquarters)->rally.y == ResourceCenter.y
        && !PC->IsProductionRallyMode());
    for (const auto& Before : WorkersBeforeResourceRally)
    {
        const auto* After = Sim.find(Before.id);
        TestTrue(TEXT("Setting a resource Rally preserves every existing Drudge order"),
            After && After->order == Before.order && After->target == Before.target
            && After->resourceTarget == Before.resourceTarget && After->goal.x == Before.goal.x
            && After->goal.y == Before.goal.y && After->returning == Before.returning
            && After->resumeGather == Before.resumeGather);
    }

    PC->QueueTraining(Kind::Worker, 1, Headquarters);
    Id RallyNewborn = 0;
    for (int32 Step = 0; Step < 4000 && !RallyNewborn; ++Step)
    {
        Sim.update(Simulation::Step);
        for (const auto& Entity : Sim.entities())
        {
            if (!Entity.alive() || Entity.team != 0 || Entity.kind != Kind::Worker) continue;
            const bool bExisting = std::any_of(WorkersBeforeResourceRally.begin(), WorkersBeforeResourceRally.end(),
                [&](const cinder::Entity& Before) { return Before.id == Entity.id; });
            if (!bExisting) { RallyNewborn = Entity.id; break; }
        }
    }
    const Entity* Newborn = Sim.find(RallyNewborn);
    if (!TestTrue(TEXT("A Drudge completes from the resource-rallied Anchor"), Newborn != nullptr)) return false;
    TestTrue(TEXT("The newborn Drudge gathers the chosen ore entity"),
        Newborn->order == Order::Gather && Newborn->target == RallyResource
        && Newborn->resourceTarget == RallyResource);

    const Id ContextWorker = WorkersBeforeResourceRally.front().id;
    PC->SelectOwnedEntity(ContextWorker);
    const std::size_t BeforeNormalGather = Sim.recording().size();
    PC->HandleWorldTap(RallyResource, &OffCenterResourceGround, false);
    TestTrue(TEXT("A normal selected-Drudge ore tap remains Gather"),
        Sim.recording().size() == BeforeNormalGather + 1
        && Sim.recording().back().command.type == CommandType::Gather
        && Sim.recording().back().command.target == RallyResource
        && Sim.find(ContextWorker)->order == Order::Gather
        && Sim.find(ContextWorker)->resourceTarget == RallyResource);

    PC->SelectOwnedEntity(Headquarters);
    const std::size_t BeforeContextResourceRally = Sim.recording().size();
    PC->HandleWorldTap(RallyResource, &OffCenterResourceGround, true);
    TestTrue(TEXT("Desktop force-context on ore with an Anchor selected issues Rally instead of Gather"),
        Sim.recording().size() == BeforeContextResourceRally + 1
        && Sim.recording().back().command.type == CommandType::Rally
        && Sim.recording().back().command.units == std::vector<Id>{Headquarters}
        && Sim.recording().back().command.point.x == ResourceCenter.x
        && Sim.recording().back().command.point.y == ResourceCenter.y);

    const Vec2 RallyBeforeDeselect = Sim.find(Headquarters)->rally;
    const std::size_t BeforeBuildingDeselect = Sim.recording().size();
    const Vec2 EmptyGround{1900, 1450};
    PC->HandleWorldTap(0, &EmptyGround, false);
    TestTrue(TEXT("A normal building terrain tap deselects without changing its Rally"),
        PC->Selection().empty() && Sim.recording().size() == BeforeBuildingDeselect
        && Sim.find(Headquarters)->rally.x == RallyBeforeDeselect.x
        && Sim.find(Headquarters)->rally.y == RallyBeforeDeselect.y);

    PC->SelectOwnedEntity(Headquarters);
    PC->BeginProductionRally(Headquarters);
    const Vec2 GroundWorkerRally{2100, 1700};
    const std::size_t BeforeGroundWorkerRally = Sim.recording().size();
    PC->HandleWorldTap(0, &GroundWorkerRally, false);
    if (!TestTrue(TEXT("A pinned headquarters accepts an explicit worker destination"),
        Sim.recording().size() == BeforeGroundWorkerRally + 1)) return false;
    TestTrue(TEXT("Worker rally override can exist before an army default"),
        Sim.recording().back().command.type == CommandType::AutoRally
        && Sim.recording().back().command.point.x == GroundWorkerRally.x
        && Sim.recording().back().command.point.y == GroundWorkerRally.y
        && Sim.find(Headquarters)->rally.x == GroundWorkerRally.x
        && Sim.find(Headquarters)->rally.y == GroundWorkerRally.y
        && Sim.find(Headquarters)->rallyOverride && !Sim.players()[0].armyRallySet
        && !PC->IsProductionRallyMode());
    PC->SelectOwnedEntity(Soldier);
    PC->QueueTraining(Kind::Worker, 1, Headquarters);
    if (!TestTrue(TEXT("Auto Mine fixture has a paid worker job"), !Sim.find(Headquarters)->queue.empty())) return false;
    const QueueItem WorkerJobBeforeReset = Sim.find(Headquarters)->queue.back();
    std::vector<Entity> ExistingWorkers;
    for (const auto& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == Kind::Worker) ExistingWorkers.push_back(Entity);
    if (!TestTrue(TEXT("Auto Mine fixture includes existing workers"), !ExistingWorkers.empty())) return false;
    Command WorkerMove; WorkerMove.type = CommandType::Move; WorkerMove.units = {ExistingWorkers.front().id};
    WorkerMove.point = {1100, 1000};
    if (!TestTrue(TEXT("An existing worker has an intentional movement order"), Sim.command(WorkerMove).accepted)) return false;
    ExistingWorkers.front() = *Sim.find(ExistingWorkers.front().id);
    const int OreBeforeAutoMine = Sim.players()[0].ore;
    const int SupplyBeforeAutoMine = Sim.supply(0);
    const auto AutoMineCount = Sim.recording().size();
    TestTrue(TEXT("Headquarters Auto Mine status requires no team army rally"),
        Sim.autoRallyStatus(0, Kind::Resource, Headquarters, true).accepted);
    PC->UseDefaultProductionRally(Headquarters);
    if (!TestTrue(TEXT("Headquarters Auto Mine records exactly one authoritative command"),
        Sim.recording().size() == AutoMineCount + 1)) return false;
    const auto& AutoMine = Sim.recording().back().command;
    TestTrue(TEXT("Auto Mine carries the exact opaque producer ID and explicit reset operation"),
        AutoMine.type == CommandType::AutoRally && AutoMine.queueIndex == 1 && AutoMine.target == Headquarters
        && AutoMine.kind == Kind::Resource && AutoMine.units.empty() && AutoMine.point.x == 0 && AutoMine.point.y == 0);
    TestTrue(TEXT("Auto Mine clears only the worker override without creating an army rally"),
        !Sim.find(Headquarters)->rallyOverride && !Sim.players()[0].armyRallySet
        && PC->Selection() == std::vector<Id>{Soldier});
    const auto& WorkerJobAfterReset = Sim.find(Headquarters)->queue.back();
    TestTrue(TEXT("Auto Mine preserves the paid queue item, progress, ore, and supply"),
        WorkerJobAfterReset.id == WorkerJobBeforeReset.id && WorkerJobAfterReset.kind == Kind::Worker
        && WorkerJobAfterReset.remaining == WorkerJobBeforeReset.remaining
        && WorkerJobAfterReset.total == WorkerJobBeforeReset.total && WorkerJobAfterReset.cost == WorkerJobBeforeReset.cost
        && Sim.players()[0].ore == OreBeforeAutoMine && Sim.supply(0) == SupplyBeforeAutoMine);
    for (const auto& Before : ExistingWorkers)
    {
        const auto* After = Sim.find(Before.id);
        TestTrue(TEXT("Auto Mine preserves each existing worker's orders and resource assignment"),
            After && After->order == Before.order && After->target == Before.target
            && After->resourceTarget == Before.resourceTarget && After->goal.x == Before.goal.x
            && After->goal.y == Before.goal.y && After->returning == Before.returning
            && After->resumeGather == Before.resumeGather);
    }
    const Vec2 HeadquartersRally = Sim.find(Headquarters)->rally;
    const auto BeforeUnsetDefault = Sim.stateHash();
    const auto UnsetDefaultCount = Sim.recording().size();
    PC->FeedbackText.Empty();
    PC->UseDefaultProductionRally(Second);
    TestTrue(TEXT("Restoring a facility before an army default exists rejects atomically with feedback"),
        Sim.stateHash() == BeforeUnsetDefault && Sim.recording().size() == UnsetDefaultCount
        && Sim.find(Second)->rallyOverride && !PC->Feedback().IsEmpty());
    PC->FeedbackText.Empty();
    PC->FocusArmyRally();
    TestTrue(TEXT("Focusing an unset army rally gives feedback without changing gameplay"),
        !PC->Feedback().IsEmpty() && Sim.stateHash() == BeforeUnsetDefault && Sim.recording().size() == UnsetDefaultCount);
    PC->BeginProductionRally();
    if (!TestTrue(TEXT("Global rally submits without selected producer IDs"), PC->IssueDestination({2400, 2400}))) return false;
    TestTrue(TEXT("The unpinned Kiln inherits the new player army rally"),
        Sim.players()[0].armyRallySet && Sim.players()[0].armyRally.x == 2400 && Sim.players()[0].armyRally.y == 2400
        && Sim.find(First)->rally.x == 2400 && Sim.find(First)->rally.y == 2400 && !Sim.find(First)->rallyOverride);
    TestTrue(TEXT("Changing the army rally preserves an explicit facility override and worker rally"),
        Sim.find(Second)->rally.x == 1500 && Sim.find(Second)->rally.y == 1300 && Sim.find(Second)->rallyOverride
        && Sim.find(Headquarters)->rally.x == HeadquartersRally.x && Sim.find(Headquarters)->rally.y == HeadquartersRally.y);
    const auto BeforeEnemyDefault = Sim.stateHash();
    const auto EnemyDefaultCount = Sim.recording().size();
    PC->FeedbackText.Empty();
    PC->UseDefaultProductionRally(Enemy);
    TestTrue(TEXT("An enemy facility cannot adopt the player's army default"),
        Sim.stateHash() == BeforeEnemyDefault && Sim.recording().size() == EnemyDefaultCount && !PC->Feedback().IsEmpty());
    PC->UseDefaultProductionRally(Second);
    if (!TestTrue(TEXT("Restoring the facility default records exactly one command"),
        Sim.recording().size() == EnemyDefaultCount + 1)) return false;
    const auto& RestoreDefault = Sim.recording().back().command;
    TestTrue(TEXT("The public default action sends the explicit automatic restore command"),
        RestoreDefault.type == CommandType::AutoRally && RestoreDefault.queueIndex == 1
        && RestoreDefault.target == Second && RestoreDefault.units.empty()
        && RestoreDefault.point.x == 0 && RestoreDefault.point.y == 0);
    TestTrue(TEXT("Restoring the default clears the override and inherits the current rally"),
        !Sim.find(Second)->rallyOverride && Sim.find(Second)->rally.x == 2400 && Sim.find(Second)->rally.y == 2400
        && Sim.find(Headquarters)->rally.x == HeadquartersRally.x && Sim.find(Headquarters)->rally.y == HeadquartersRally.y);
    auto* Camera = World->SpawnActor<ACinderCamera>();
    if (!TestNotNull(TEXT("Rally focus fixture has an actual camera rig"), Camera)) return false;
    PC->Rig = Camera;
    Camera->Focus(FVector(1200, 1400, 0), true);
    TestFalse(TEXT("Rally focus fixture starts away from the rally"),
        Camera->GetActorLocation().Equals(FVector(2400, 2400, 0), 1));
    const auto BeforeFocus = Sim.stateHash();
    const auto FocusCount = Sim.recording().size();
    PC->ExecuteAction(TEXT("rallyfocus"));
    Camera->Tick(1.0f);
    TestTrue(TEXT("The rally-focus action moves the actual camera to the army rally"),
        Camera->GetActorLocation().Equals(FVector(2400, 2400, 0), 1));
    TestTrue(TEXT("Camera rally focus preserves selection, gameplay state, and command recording"),
        PC->Selection() == std::vector<Id>{Soldier} && Sim.stateHash() == BeforeFocus && Sim.recording().size() == FocusCount);
    TestTrue(TEXT("Rallying production never moves the selected army"), Sim.find(Soldier)->order == Order::Idle);

    PC->BeginGlobalBuild(Kind::Foundry);
    TestTrue(TEXT("Global placement starts while an army unit is selected"), PC->IsGlobalBuildMode());
    TestTrue(TEXT("Global placement status ignores the unrelated selected army"), PC->BuildPlacementStatus().accepted);
    PC->ExecuteAction(TEXT("cancelplacement"));
    TestFalse(TEXT("Cancelling placement clears automatic mode"), PC->IsGlobalBuildMode());
    PC->BeginProductionRally(); PC->ExecuteAction(TEXT("move"));
    TestTrue(TEXT("Move replaces production rally rather than leaving two destination modes"),
        PC->IsMoveCommandMode() && !PC->IsProductionRallyMode());
    PC->BeginProductionRally(); PC->ExecuteAction(TEXT("cancelrally"));
    TestFalse(TEXT("The explicit cancel action clears production rally mode"),
        PC->IsProductionRallyMode() || PC->ProductionRallyProducer());
    PC->BeginProductionRally(); PC->Escape();
    TestFalse(TEXT("Escape cancels rally without pausing the match"), PC->IsProductionRallyMode() || Battle->IsPaused());

    PC->Selected.clear();
    PC->QueueTraining(Kind::Worker, 1);
    TestTrue(TEXT("Training works with no world selection"),
        Sim.recording().back().command.type == CommandType::AutoTrain && Sim.recording().back().command.units.empty());
    Battle->SetPaused(true);
    const auto BeforePaused = Sim.stateHash();
    PC->QueueTraining(Kind::Worker, 1); PC->BeginProductionRally(); PC->BeginGlobalBuild(Kind::Foundry);
    TestEqual(TEXT("Global commands respect pause gating"), Sim.stateHash(), BeforePaused);
    TestFalse(TEXT("Paused controls do not arm a destination"), PC->IsBuildMode() || PC->IsProductionRallyMode());
    Battle->ReturnToMenu();
    PC->BeginTutorial();
    PC->BeginGlobalBuild(Kind::Foundry);
    TestTrue(TEXT("Tutorial recovery starts with an actual armed placement"), PC->IsGlobalBuildMode());
    const auto BeforeRecovery = Battle->Sim().stateHash();
    const auto SelectionBeforeRecovery = PC->Selection();
    PC->ExecuteAction(TEXT("tutorialclearmode"));
    TestFalse(TEXT("Tutorial recovery clears placement and every destination mode"),
        PC->IsBuildMode() || PC->IsProductionRallyMode() || PC->IsAttackMoveMode()
        || PC->IsMoveCommandMode() || PC->IsDefendCommandMode());
    TestTrue(TEXT("Clearing a tutorial mode preserves selection and gameplay"),
        PC->Selection() == SelectionBeforeRecovery && Battle->Sim().stateHash() == BeforeRecovery && !Battle->IsPaused());
    return !HasAnyErrors();
}
#endif
