#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderPlayerController.h"
#include "Tests/AutomationCommon.h"
#include <algorithm>
#include <functional>

namespace CinderWorldIntegration
{
// Engine's own automation tests use this wrapper to own a transient game world.
// No editor map, local player viewport, or persistent player save is modified.
struct FGameFixture
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
        if (!Test.TestNotNull(TEXT("Transient game world exists"), World)) return false;
        World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
        Battle = World->SpawnActor<ACinderBattlefield>();
        if (!Test.TestNotNull(TEXT("Real battlefield actor spawned"), Battle)) return false;
        if (!WorldOwner.BeginPlayInTestWorld())
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        // PlayerController initialization expects an existing game mode/state.
        Controller = World->SpawnActor<ACinderPlayerController>();
        if (!Test.TestNotNull(TEXT("Real controller actor spawned"), Controller)) return false;
        Test.TestTrue(TEXT("Cinderline game mode starts the world"),
            World->GetAuthGameMode<ACinderGameMode>() != nullptr && World->HasBegunPlay());
        Test.TestTrue(TEXT("Both actors received BeginPlay"),
            Battle->HasActorBegunPlay() && Controller->HasActorBegunPlay());
        Test.TestTrue(TEXT("Controller resolves this world's battlefield"), Controller->Battlefield() == Battle);
        TArray<UInstancedStaticMeshComponent*> Components;
        Battle->GetComponents<UInstancedStaticMeshComponent>(Components);
        bool HasRenderedInstances = false;
        for (const UInstancedStaticMeshComponent* Component : Components)
            if (Component && Component->GetInstanceCount() > 0) HasRenderedInstances = true;
        Test.TestTrue(TEXT("Battlefield BeginPlay populated presentation components"), HasRenderedInstances);
        Test.TestTrue(TEXT("Initial rendering remembers discovered resources"), !Battle->KnownResources().empty());
        WorldOwner.ForwardErrorMessages(&Test);
        return !Test.HasAnyErrors();
    }

    // Invoke the actual adapter tick, including its pause/menu gate and rendering.
    // This deliberately does not stand in for an engine frame-rate or UI test.
    bool TickUntil(const std::function<bool()>& Predicate, float MaximumSeconds)
    {
        constexpr float Delta = 0.1f;
        for (int32 I = 0; I < FMath::CeilToInt(MaximumSeconds / Delta); ++I)
        {
            if (Predicate()) return true;
            Battle->Tick(Delta);
        }
        return Predicate();
    }
};

std::vector<cinder::Id> EntitiesOfKind(const cinder::Simulation& Sim, cinder::Kind Kind)
{
    std::vector<cinder::Id> Result;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.team == 0 && Entity.kind == Kind && Entity.alive()) Result.push_back(Entity.id);
    return Result;
}

cinder::CommandResult IssueAtBattlefield(ACinderBattlefield& Battle, cinder::CommandType Type,
    const std::vector<cinder::Id>& Units, cinder::Kind Kind = cinder::Kind::Worker,
    cinder::Vec2 Point = {}, cinder::Id Target = 0)
{
    cinder::Command Command;
    Command.type = Type; Command.team = 0; Command.units = Units;
    Command.kind = Kind; Command.point = Point; Command.target = Target;
    return Battle.Sim().command(Command);
}

bool FindBuildSite(const cinder::Simulation& Sim, cinder::Id Builder,
    cinder::Vec2 Base, cinder::Vec2& OutSite)
{
    const cinder::Entity* Worker = Sim.find(Builder);
    if (!Worker) return false;
    for (float Radius = 240; Radius <= 560; Radius += 40)
        for (int32 Spoke = 0; Spoke < 24; ++Spoke)
        {
            const float Angle = 2 * PI * Spoke / 24;
            const cinder::Vec2 Point{ Base.x + Radius * FMath::Cos(Angle), Base.y + Radius * FMath::Sin(Angle) };
            const float DX = Worker->pos.x - Point.x, DY = Worker->pos.y - Point.y;
            // Keep enough approach distance to observe travel before construction can start.
            if (DX * DX + DY * DY >= 300 * 300 && DX * DX + DY * DY <= 700 * 700
                && Sim.canPlace(0, cinder::Kind::Foundry, Point))
            {
                OutSite = Point;
                return true;
            }
        }
    return false;
}

struct FTemporarySnapshot
{
    FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("Automation"),
        TEXT("Cinderline"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("fixture.cinder")));
    ~FTemporarySnapshot()
    {
        IFileManager::Get().Delete(*Path, false, true);
        IFileManager::Get().DeleteDirectory(*Directory, false, false);
    }
};

bool HasPendingModes(const ACinderPlayerController& Controller)
{
    return Controller.IsBuildMode() || Controller.bBuildMenu || Controller.bAttackMove || Controller.bBoxSelect;
}

void TryArmModes(ACinderPlayerController& Controller)
{
    Controller.ExecuteAction(TEXT("attack"));
    Controller.ExecuteAction(TEXT("buildmenu"));
    Controller.ExecuteAction(TEXT("box"));
    Controller.ExecuteAction(TEXT("build"), static_cast<int>(cinder::Kind::Foundry));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderWorldLifecycleIntegration,
    "Cinderline.Integration.WorldLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderWorldLifecycleIntegration::RunTest(const FString& Parameters)
{
    using namespace CinderWorldIntegration;
    using namespace cinder;
    FGameFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderBattlefield& Battle = *Fixture.Battle;
    ACinderPlayerController& Controller = *Fixture.Controller;
    TestTrue(TEXT("World begins at the menu"), Battle.IsMenu());
    TryArmModes(Controller);
    TestFalse(TEXT("Menu rejects gameplay targeting and placement modes"), HasPendingModes(Controller));
    const uint64 MenuTick = Battle.Sim().tick();
    Battle.Tick(0.2f);
    TestEqual(TEXT("Menu prevents simulation advancement"), static_cast<uint64>(Battle.Sim().tick()), MenuTick);

    Controller.ExecuteAction(TEXT("start"), 2);
    TestTrue(TEXT("Controller start exits menu and pause"), !Battle.IsMenu() && !Battle.IsPaused());
    TestEqual(TEXT("Controller start chooses map two"), Battle.MapIndex(), 2);
    TestEqual(TEXT("Start resets the simulation clock"), static_cast<uint64>(Battle.Sim().tick()), uint64(0));
    Battle.Tick(Simulation::Step);
    TestTrue(TEXT("Battlefield Tick advances active match"), Battle.Sim().tick() > 0);

    const int Ore = Battle.Sim().players()[0].ore;
    const size_t Recorded = Battle.Sim().recording().size();
    Controller.ExecuteAction(TEXT("train"), static_cast<int>(Kind::Worker));
    TestEqual(TEXT("Empty selection cannot pay for training"), Battle.Sim().players()[0].ore, Ore);
    TestTrue(TEXT("Rejected controller training is not recorded"), Battle.Sim().recording().size() == Recorded);
    TestTrue(TEXT("Rejected controller training produces feedback"), !Controller.Feedback().IsEmpty());

    TryArmModes(Controller);
    TestTrue(TEXT("Active match permits a placement mode"), Controller.IsBuildMode());
    Controller.ExecuteAction(TEXT("pause"));
    const uint64 PausedTick = Battle.Sim().tick();
    TestTrue(TEXT("One pause action cancels modes and pauses the battlefield"), Battle.IsPaused() && !HasPendingModes(Controller));
    TryArmModes(Controller);
    TestFalse(TEXT("Paused match rejects gameplay modes"), HasPendingModes(Controller));
    Controller.ExecuteAction(TEXT("pause"));
    TestTrue(TEXT("Explicit pause remains paused when repeated"), Battle.IsPaused());
    Battle.Tick(0.2f);
    TestEqual(TEXT("Paused battlefield does not advance"), static_cast<uint64>(Battle.Sim().tick()), PausedTick);
    Controller.ExecuteAction(TEXT("resume"));
    Battle.Tick(Simulation::Step);
    TestTrue(TEXT("Controller resume permits advancement"), !Battle.IsPaused() && Battle.Sim().tick() > PausedTick);
    Controller.ExecuteAction(TEXT("attack"));
    TestTrue(TEXT("Active match can arm attack targeting"), Controller.bAttackMove);
    Controller.ExecuteAction(TEXT("pause"));
    TestTrue(TEXT("Pause also cancels attack targeting in one action"), Battle.IsPaused() && !HasPendingModes(Controller));
    Controller.ExecuteAction(TEXT("resume"));

    const auto Headquarters = EntitiesOfKind(Battle.Sim(), Kind::Headquarters);
    if (!TestTrue(TEXT("Starting headquarters exists"), Headquarters.size() == 1)) return false;
    if (!TestTrue(TEXT("Normal paid queue command accepted at battlefield boundary"),
        IssueAtBattlefield(Battle, CommandType::Train, Headquarters, Kind::Worker).accepted)) return false;
    TestTrue(TEXT("Queue command changed match economy before reset"), Battle.Sim().players()[0].ore < Ore);
    TryArmModes(Controller);
    Controller.ExecuteAction(TEXT("menu"));
    TestFalse(TEXT("Menu transition clears all pending gameplay modes"), HasPendingModes(Controller));
    const uint64 ReturnedMenuTick = Battle.Sim().tick();
    Battle.Tick(0.2f);
    TestTrue(TEXT("Returning to menu freezes the current match"), Battle.IsMenu() && Battle.Sim().tick() == ReturnedMenuTick);
    Controller.ExecuteAction(TEXT("start"), 0);
    TryArmModes(Controller);
    TestTrue(TEXT("Reset fixture arms gameplay modes in an active match"), HasPendingModes(Controller));
    Controller.ExecuteAction(TEXT("start"), 0);
    TestFalse(TEXT("Start transition clears all pending gameplay modes"), HasPendingModes(Controller));
    TestEqual(TEXT("New match switches back to map zero"), Battle.MapIndex(), 0);
    TestEqual(TEXT("New match restores normal starting funds"), Battle.Sim().players()[0].ore, 500);
    TestEqual(TEXT("New match resets time again"), static_cast<uint64>(Battle.Sim().tick()), uint64(0));
    TestTrue(TEXT("New match clears old command recording"), Battle.Sim().recording().empty());
    for (const Entity& Entity : Battle.Sim().entities())
        TestTrue(TEXT("New match clears old production queues"), Entity.queue.empty());
    if (!HasAnyErrors()) AddInfo(TEXT("CINDERLINE_UE_INTEGRATION_LIFECYCLE_PASS: transient world, real BeginPlay, controller transitions, paused adapter tick, paid queue reset."));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderEconomyAndProductionIntegration,
    "Cinderline.Integration.EconomyAndProduction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderEconomyAndProductionIntegration::RunTest(const FString& Parameters)
{
    using namespace CinderWorldIntegration;
    using namespace cinder;
    FGameFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderBattlefield& Battle = *Fixture.Battle;
    ACinderPlayerController& Controller = *Fixture.Controller;
    Controller.ExecuteAction(TEXT("start"), 0);
    const auto Workers = EntitiesOfKind(Battle.Sim(), Kind::Worker);
    const auto Headquarters = EntitiesOfKind(Battle.Sim(), Kind::Headquarters);
    if (!TestTrue(TEXT("Normal initial economy exists"), Workers.size() == 5 && Headquarters.size() == 1)) return false;
    const Id Worker = Workers.front(), HQ = Headquarters.front();
    const Vec2 Base = Battle.Sim().find(HQ)->pos;
    if (!TestTrue(TEXT("Workers can be stopped through normal command boundary"),
        IssueAtBattlefield(Battle, CommandType::Stop, Workers).accepted)) return false;
    Id Deposit = 0;
    for (const Entity& Entity : Battle.Sim().entities())
        if (Entity.kind == Kind::Resource && Entity.resource > 0 && Battle.Sim().visible(0, Entity.pos))
        { Deposit = Entity.id; break; }
    if (!TestTrue(TEXT("Starting vision reveals an actual deposit"), Deposit != 0)) return false;
    const float InitialDeposit = Battle.Sim().find(Deposit)->resource;
    const int InitialOre = Battle.Sim().players()[0].ore;
    if (!TestTrue(TEXT("Normal gather command accepted"),
        IssueAtBattlefield(Battle, CommandType::Gather, {Worker}, Kind::Worker, {}, Deposit).accepted)) return false;
    if (!TestTrue(TEXT("Battlefield ticks advance travel and harvesting"), Fixture.TickUntil([&]
        { const Entity* E = Battle.Sim().find(Worker); return E && E->carried > 0; }, 30))) return false;
    TestEqual(TEXT("Carried ore has not been credited early"), Battle.Sim().players()[0].ore, InitialOre);
    if (!TestTrue(TEXT("Worker returns a real deposit through adapter ticks"), Fixture.TickUntil([&]
        { return Battle.Sim().players()[0].stats.gathered > 0; }, 30))) return false;
    TestEqual(TEXT("Delivered ore reaches the player economy"), Battle.Sim().players()[0].ore,
        InitialOre + Battle.Sim().players()[0].stats.gathered);
    TestTrue(TEXT("Harvesting depleted the finite resource"), Battle.Sim().find(Deposit)->resource < InitialDeposit);
    bool ResourcePresentationUpdated = false;
    for (const Entity& Known : Battle.KnownResources())
        if (Known.id == Deposit && Known.resource == Battle.Sim().find(Deposit)->resource) ResourcePresentationUpdated = true;
    TestTrue(TEXT("Adapter resource memory reflects harvested ore"), ResourcePresentationUpdated);
    IssueAtBattlefield(Battle, CommandType::Stop, Workers);

    int Before = Battle.Sim().players()[0].ore;
    if (!TestTrue(TEXT("Paid worker training accepted"),
        IssueAtBattlefield(Battle, CommandType::Train, {HQ}, Kind::Worker).accepted)) return false;
    TestEqual(TEXT("Training charges the defined worker cost"), Battle.Sim().players()[0].ore, Before - definition(Kind::Worker).cost);
    TestEqual(TEXT("Queued worker reserves supply"), Battle.Sim().supply(0), 6);
    const float QueueRemaining = Battle.Sim().find(HQ)->queue.front().remaining;
    Controller.ExecuteAction(TEXT("pause"));
    Battle.Tick(0.2f);
    TestEqual(TEXT("Pause also stops production progress"), Battle.Sim().find(HQ)->queue.front().remaining, QueueRemaining);
    Controller.ExecuteAction(TEXT("resume"));
    if (!TestTrue(TEXT("Adapter ticks finish worker production"), Fixture.TickUntil([&]
        { return EntitiesOfKind(Battle.Sim(), Kind::Worker).size() == 6; }, definition(Kind::Worker).buildTime + 1))) return false;

    Controller.ExecuteAction(TEXT("build"), static_cast<int>(Kind::Foundry));
    TestTrue(TEXT("Public controller action enters placement mode"), Controller.IsBuildMode() && Controller.BuildingKind() == Kind::Foundry);
    Vec2 Site;
    if (!TestTrue(TEXT("Existing worker has a visible legal build site"), FindBuildSite(Battle.Sim(), Worker, Base, Site))) return false;
    Before = Battle.Sim().players()[0].ore;
    if (!TestTrue(TEXT("Normal paid construction command accepted"),
        IssueAtBattlefield(Battle, CommandType::Build, {Worker}, Kind::Foundry, Site).accepted)) return false;
    TestEqual(TEXT("Construction charges the defined cost"), Battle.Sim().players()[0].ore, Before - definition(Kind::Foundry).cost);
    Controller.ExecuteAction(TEXT("cancelplacement"));
    TestFalse(TEXT("Controller leaves placement mode"), Controller.IsBuildMode());
    const auto Foundries = EntitiesOfKind(Battle.Sim(), Kind::Foundry);
    if (!TestTrue(TEXT("Construction creates one real producer"), Foundries.size() == 1)) return false;
    const Id Foundry = Foundries.front();
    const float FoundationProgress = Battle.Sim().find(Foundry)->progress;
    const Vec2 BuilderStart = Battle.Sim().find(Worker)->pos;
    TestEqual(TEXT("Paid foundation retains its assigned builder"), Battle.Sim().constructionWorker(Foundry), Worker);
    TestTrue(TEXT("Construction command sends the Drudge to the foundation"), Battle.Sim().find(Worker)->order == Order::Construct);
    TestFalse(TEXT("Distant builder is not constructing before arrival"), Battle.Sim().constructionActive(Foundry));
    Battle.Tick(0.2f);
    TestEqual(TEXT("Foundation makes no progress while builder approaches"), Battle.Sim().find(Foundry)->progress, FoundationProgress);
    TestFalse(TEXT("First approach ticks have not reached the construction site"), Battle.Sim().constructionActive(Foundry));
    const Vec2 BuilderAfterTravel = Battle.Sim().find(Worker)->pos;
    TestTrue(TEXT("Builder physically travels toward its construction site"),
        BuilderAfterTravel.x != BuilderStart.x || BuilderAfterTravel.y != BuilderStart.y);
    Before = Battle.Sim().players()[0].ore;
    TestFalse(TEXT("Unfinished structure cannot train"), IssueAtBattlefield(Battle, CommandType::Train, {Foundry}, Kind::Striker).accepted);
    TestEqual(TEXT("Rejected unfinished production spends nothing"), Battle.Sim().players()[0].ore, Before);
    bool AdvancedBeforeArrival = false;
    if (!TestTrue(TEXT("Builder reaches the site through battlefield ticks"), Fixture.TickUntil([&]
        {
            const bool Active = Battle.Sim().constructionActive(Foundry);
            if (!Active && Battle.Sim().find(Foundry)->progress > FoundationProgress) AdvancedBeforeArrival = true;
            return Active;
        }, 30))) return false;
    TestFalse(TEXT("Construction remains frozen throughout the builder approach"), AdvancedBeforeArrival);
    if (!TestTrue(TEXT("Nearby assigned builder advances construction"), Fixture.TickUntil([&]
        { return Battle.Sim().find(Foundry)->progress > FoundationProgress; }, 1))) return false;
    const float WorkingProgress = Battle.Sim().find(Foundry)->progress;
    Controller.ExecuteAction(TEXT("pause"));
    Battle.Tick(0.2f);
    TestEqual(TEXT("Match pause freezes active construction"), Battle.Sim().find(Foundry)->progress, WorkingProgress);
    TestEqual(TEXT("Match pause preserves the builder assignment"), Battle.Sim().constructionWorker(Foundry), Worker);
    Controller.ExecuteAction(TEXT("resume"));
    if (!TestTrue(TEXT("Normal stop order releases the builder"),
        IssueAtBattlefield(Battle, CommandType::Stop, {Worker}).accepted)) return false;
    TestEqual(TEXT("Stopped foundation has no assigned builder"), Battle.Sim().constructionWorker(Foundry), Id(0));
    TestFalse(TEXT("Stopped builder leaves construction inactive"), Battle.Sim().constructionActive(Foundry));
    for (int32 Tick = 0; Tick < 20; ++Tick) Battle.Tick(0.1f);
    TestEqual(TEXT("Unassigned foundation remains paused during an active match"), Battle.Sim().find(Foundry)->progress, WorkingProgress);
    Before = Battle.Sim().players()[0].ore;
    if (!TestTrue(TEXT("Normal resume construction order reassigns the Drudge"),
        IssueAtBattlefield(Battle, CommandType::ResumeConstruction, {Worker}, Kind::Worker, {}, Foundry).accepted)) return false;
    TestEqual(TEXT("Resuming an already-paid foundation costs no ore"), Battle.Sim().players()[0].ore, Before);
    TestEqual(TEXT("Resumed foundation restores its builder assignment"), Battle.Sim().constructionWorker(Foundry), Worker);
    TestTrue(TEXT("Resumed Drudge receives a construction order"), Battle.Sim().find(Worker)->order == Order::Construct);
    if (!TestTrue(TEXT("Resumed builder continues real construction"), Fixture.TickUntil([&]
        { return Battle.Sim().constructionActive(Foundry) && Battle.Sim().find(Foundry)->progress > WorkingProgress; }, 5))) return false;
    if (!TestTrue(TEXT("Construction finishes through battlefield tick"), Fixture.TickUntil([&]
        { const Entity* E = Battle.Sim().find(Foundry); return E && E->alive() && E->progress >= 1; }, definition(Kind::Foundry).buildTime + 2))) return false;
    TestEqual(TEXT("Completed construction releases the builder"), Battle.Sim().constructionWorker(Foundry), Id(0));
    TestFalse(TEXT("Completed foundation no longer reports active construction"), Battle.Sim().constructionActive(Foundry));
    TestTrue(TEXT("Builder without an earlier gather job becomes idle after completion"), Battle.Sim().find(Worker)->order == Order::Idle);
    Before = Battle.Sim().players()[0].ore;
    if (!TestTrue(TEXT("Completed producer accepts paid infantry training"),
        IssueAtBattlefield(Battle, CommandType::Train, {Foundry}, Kind::Striker).accepted)) return false;
    TestEqual(TEXT("Infantry cost charged once"), Battle.Sim().players()[0].ore, Before - definition(Kind::Striker).cost);
    if (!TestTrue(TEXT("Adapter ticks produce real infantry"), Fixture.TickUntil([&]
        { return !EntitiesOfKind(Battle.Sim(), Kind::Striker).empty(); }, definition(Kind::Striker).buildTime + 1))) return false;
    const Id Infantry = EntitiesOfKind(Battle.Sim(), Kind::Striker).front();
    Controller.SelectArmy();
    TestTrue(TEXT("Controller selects normally produced infantry without a viewport"),
        std::find(Controller.Selection().begin(), Controller.Selection().end(), Infantry) != Controller.Selection().end());
    Controller.ExecuteAction(TEXT("hold"));
    TestTrue(TEXT("Public controller dispatch reaches selected infantry"), Battle.Sim().find(Infantry)->order == Order::Hold);
    Controller.ExecuteAction(TEXT("stop"));
    const auto SelectedBeforePause = Controller.Selection();
    Controller.ExecuteAction(TEXT("pause"));
    const size_t PausedRecording = Battle.Sim().recording().size();
    Controller.ExecuteAction(TEXT("hold"));
    TestTrue(TEXT("Pause blocks controller command dispatch even with valid selection"),
        Battle.Sim().recording().size() == PausedRecording && Battle.Sim().find(Infantry)->order == Order::Idle);
    TestTrue(TEXT("Pause preserves valid selection"), Controller.Selection() == SelectedBeforePause);
    Controller.ExecuteAction(TEXT("resume"));
    TestTrue(TEXT("Resume preserves valid selection"), Controller.Selection() == SelectedBeforePause);

    // The production SaveMatch/LoadMatch wrappers target the player's real save.
    // Exercise only simulation persistence here, using an owned temporary file.
    FTemporarySnapshot Snapshot;
    if (!TestTrue(TEXT("Temporary snapshot directory created"), IFileManager::Get().MakeDirectory(*Snapshot.Directory, true))) return false;
    const uint64 SavedHash = Battle.Sim().stateHash();
    if (!TestTrue(TEXT("Transient integration snapshot saves"), Battle.Sim().save(TCHAR_TO_UTF8(*Snapshot.Path)))) return false;
    Battle.Tick(0.1f);
    TestTrue(TEXT("Live adapter state advances after snapshot"), Battle.Sim().stateHash() != SavedHash);
    if (!TestTrue(TEXT("Transient snapshot reloads"), Battle.Sim().load(TCHAR_TO_UTF8(*Snapshot.Path)))) return false;
    TestEqual(TEXT("Snapshot restores authoritative state"), static_cast<uint64>(Battle.Sim().stateHash()), SavedHash);
    Battle.RenderState();
    TestTrue(TEXT("Loaded state still renders discovered resources"), !Battle.KnownResources().empty());
    if (!HasAnyErrors()) AddInfo(FString::Printf(TEXT("CINDERLINE_UE_INTEGRATION_ECONOMY_PASS: gathered=%d, produced=%d, built=%d, ore=%d; paid commands, builder travel and pause/resume, actual actor ticks, selected-controller dispatch, temporary snapshot only."),
        Battle.Sim().players()[0].stats.gathered, Battle.Sim().players()[0].stats.produced,
        Battle.Sim().players()[0].stats.built, Battle.Sim().players()[0].ore));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderAIKnowledgeAndPersistenceIntegration,
    "Cinderline.Integration.AIKnowledgeAndPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderAIKnowledgeAndPersistenceIntegration::RunTest(const FString& Parameters)
{
    using namespace CinderWorldIntegration;
    using namespace cinder;
    FGameFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderBattlefield& Battle = *Fixture.Battle;
    Fixture.Controller->ExecuteAction(TEXT("start"), 0);
    if (!TestTrue(TEXT("Knowledge fixture runs the ordinary active opponent"), Battle.Sim().config().ai)) return false;
    const auto Workers = EntitiesOfKind(Battle.Sim(), Kind::Worker);
    const auto Headquarters = EntitiesOfKind(Battle.Sim(), Kind::Headquarters);
    if (!TestTrue(TEXT("Knowledge fixture uses the starting player economy"), Workers.size() == 5 && Headquarters.size() == 1)) return false;
    const Id Worker = Workers.front();
    const Vec2 Home = Battle.Sim().find(Headquarters.front())->pos;
    Vec2 OpponentHome;
    bool HasOpponentHome = false;
    for (const Entity& Entity : Battle.Sim().entities())
        if (Entity.alive() && Entity.team == 1 && Entity.kind == Kind::Headquarters)
        {
            OpponentHome = Entity.pos;
            HasOpponentHome = true;
            break;
        }
    if (!TestTrue(TEXT("Knowledge fixture has the normal opponent Anchor"), HasOpponentHome)) return false;
    auto WorkerSighting = [&]() -> const AISighting*
    {
        const auto& Sightings = Battle.Sim().aiSightings();
        const auto Found = std::find_if(Sightings.begin(), Sightings.end(), [&](const AISighting& Sighting)
            { return Sighting.id == Worker; });
        return Found == Sightings.end() ? nullptr : &*Found;
    };
    TestFalse(TEXT("Opponent cannot initially see the player scout worker"), Battle.Sim().visible(1, Battle.Sim().find(Worker)->pos));
    TestTrue(TEXT("Unseen starting worker has no AI sighting"), WorkerSighting() == nullptr);
    TestEqual(TEXT("Opponent has never observed the player's starting Anchor cell"),
        static_cast<uint64>(Battle.Sim().aiLastObserved(Home)), uint64(0));

    const float DX = Home.x - OpponentHome.x, DY = Home.y - OpponentHome.y;
    const float Distance = FMath::Sqrt(DX * DX + DY * DY);
    const float ApproachRadius = definition(Kind::Headquarters).vision * 0.75f;
    const Vec2 Approach{ OpponentHome.x + DX / Distance * ApproachRadius,
                         OpponentHome.y + DY / Distance * ApproachRadius };
    if (!TestTrue(TEXT("Starting Drudge accepts an ordinary scouting move"),
        IssueAtBattlefield(Battle, CommandType::Move, {Worker}, Kind::Worker, Approach).accepted)) return false;
    if (!TestTrue(TEXT("Opponent observes the Drudge after real travel into its vision"), Fixture.TickUntil([&]
        { return WorkerSighting() != nullptr; }, 70))) return false;
    const Entity* ObservedWorker = Battle.Sim().find(Worker);
    if (!TestTrue(TEXT("Observed scout is still a living starting worker"), ObservedWorker && ObservedWorker->alive())) return false;
    const AISighting Observed = *WorkerSighting();
    TestTrue(TEXT("Opponent sighting records the observed unit kind"), Observed.kind == Kind::Worker);
    TestTrue(TEXT("AI observation follows active match ticks"), Observed.lastSeenTick > 0 && Observed.lastSeenTick <= Battle.Sim().tick());
    TestTrue(TEXT("AI observes the worker through its current team vision"), Battle.Sim().visible(1, ObservedWorker->pos));
    TestTrue(TEXT("AI observation also timestamps the visible map cell"),
        Battle.Sim().aiLastObserved(Observed.pos) >= Observed.lastSeenTick);
    TestTrue(TEXT("Opponent continues its paid economy while observing"), Battle.Sim().players()[1].stats.gathered > 0);

    if (!TestTrue(TEXT("Observed Drudge accepts an ordinary retreat move"),
        IssueAtBattlefield(Battle, CommandType::Move, {Worker}, Kind::Worker, Home).accepted)) return false;
    if (!TestTrue(TEXT("Drudge returns outside opponent vision through actor ticks"), Fixture.TickUntil([&]
        {
            const Entity* Entity = Battle.Sim().find(Worker);
            return Entity && Entity->alive() && !Battle.Sim().visible(1, Entity->pos);
        }, 25))) return false;
    // Cross at least one AI update after vision is lost, then copy the remembered values.
    const float HiddenAt = Battle.Sim().time();
    if (!TestTrue(TEXT("Active opponent gets time to process the lost contact"), Fixture.TickUntil([&]
        { return Battle.Sim().time() >= HiddenAt + 3; }, 4))) return false;
    if (!TestTrue(TEXT("Opponent retains its recent lost-contact sighting"), WorkerSighting() != nullptr)) return false;
    const AISighting Remembered = *WorkerSighting();
    const float MemoryCheckAt = Battle.Sim().time();
    if (!TestTrue(TEXT("Hidden scout keeps moving during later AI updates"), Fixture.TickUntil([&]
        { return Battle.Sim().time() >= MemoryCheckAt + 3; }, 4))) return false;
    const Entity* HiddenWorker = Battle.Sim().find(Worker);
    if (!TestTrue(TEXT("Retreating worker remains alive and hidden"),
        HiddenWorker && HiddenWorker->alive() && !Battle.Sim().visible(1, HiddenWorker->pos))) return false;
    if (!TestTrue(TEXT("Recent mobile sighting survives additional AI decisions"), WorkerSighting() != nullptr)) return false;
    TestEqual(TEXT("Hidden movement does not refresh the AI's last-seen tick"),
        static_cast<uint64>(WorkerSighting()->lastSeenTick), static_cast<uint64>(Remembered.lastSeenTick));
    TestEqual(TEXT("Hidden movement does not reveal a new X position"), WorkerSighting()->pos.x, Remembered.pos.x);
    TestEqual(TEXT("Hidden movement does not reveal a new Y position"), WorkerSighting()->pos.y, Remembered.pos.y);
    const float HiddenDX = HiddenWorker->pos.x - Remembered.pos.x, HiddenDY = HiddenWorker->pos.y - Remembered.pos.y;
    TestTrue(TEXT("Remembered location differs from the worker's actual hidden location"), HiddenDX * HiddenDX + HiddenDY * HiddenDY > 200 * 200);
    TestEqual(TEXT("Scouting contact does not reveal the player's distant Anchor cell"),
        static_cast<uint64>(Battle.Sim().aiLastObserved(Home)), uint64(0));

    FTemporarySnapshot Snapshot;
    if (!TestTrue(TEXT("AI snapshot gets an owned temporary directory"), IFileManager::Get().MakeDirectory(*Snapshot.Directory, true))) return false;
    const uint64 SavedHash = Battle.Sim().stateHash();
    const uint64 SavedObservedCell = Battle.Sim().aiLastObserved(Remembered.pos);
    if (!TestTrue(TEXT("Active AI memory saves to the temporary snapshot"), Battle.Sim().save(TCHAR_TO_UTF8(*Snapshot.Path)))) return false;
    std::vector<uint64> ContinuedHashes;
    for (int32 Tick = 0; Tick < 60; ++Tick)
    {
        Battle.Tick(0.1f);
        ContinuedHashes.push_back(Battle.Sim().stateHash());
    }
    if (!TestTrue(TEXT("AI memory reloads from the temporary snapshot"), Battle.Sim().load(TCHAR_TO_UTF8(*Snapshot.Path)))) return false;
    TestEqual(TEXT("Reload restores authoritative AI state exactly"), static_cast<uint64>(Battle.Sim().stateHash()), SavedHash);
    if (!TestTrue(TEXT("Reload preserves the hidden-worker sighting"), WorkerSighting() != nullptr)) return false;
    TestEqual(TEXT("Reload preserves the remembered sighting time"),
        static_cast<uint64>(WorkerSighting()->lastSeenTick), static_cast<uint64>(Remembered.lastSeenTick));
    TestEqual(TEXT("Reload preserves the remembered X position"), WorkerSighting()->pos.x, Remembered.pos.x);
    TestEqual(TEXT("Reload preserves the remembered Y position"), WorkerSighting()->pos.y, Remembered.pos.y);
    TestEqual(TEXT("Reload preserves AI map observation time"),
        static_cast<uint64>(Battle.Sim().aiLastObserved(Remembered.pos)), SavedObservedCell);
    Battle.RenderState();
    bool IdenticalContinuation = true;
    for (const uint64 ExpectedHash : ContinuedHashes)
    {
        Battle.Tick(0.1f);
        if (Battle.Sim().stateHash() != ExpectedHash) IdenticalContinuation = false;
    }
    TestTrue(TEXT("Loaded active opponent repeats every authoritative hash across six seconds of actor ticks"), IdenticalContinuation);
    TestTrue(TEXT("AI persistence fixture finishes in an active match"), Battle.Sim().winner() < 0);
    if (!HasAnyErrors()) AddInfo(FString::Printf(TEXT("CINDERLINE_UE_INTEGRATION_AI_KNOWLEDGE_PASS: observed_worker=%u, first_seen_tick=%llu, retained_seen_tick=%llu, continuation_ticks=%d; ordinary scouting/retreat commands, active opponent, real actor ticks, temporary snapshot only."),
        Worker, static_cast<unsigned long long>(Observed.lastSeenTick),
        static_cast<unsigned long long>(Remembered.lastSeenTick), static_cast<int32>(ContinuedHashes.size())));
    return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
