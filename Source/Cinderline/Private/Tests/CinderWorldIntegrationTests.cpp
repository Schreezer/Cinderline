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
            if (DX * DX + DY * DY <= 700 * 700 && Sim.canPlace(0, cinder::Kind::Foundry, Point))
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
    Before = Battle.Sim().players()[0].ore;
    TestFalse(TEXT("Unfinished structure cannot train"), IssueAtBattlefield(Battle, CommandType::Train, {Foundry}, Kind::Striker).accepted);
    TestEqual(TEXT("Rejected unfinished production spends nothing"), Battle.Sim().players()[0].ore, Before);
    if (!TestTrue(TEXT("Construction finishes through battlefield tick"), Fixture.TickUntil([&]
        { const Entity* E = Battle.Sim().find(Foundry); return E && E->alive() && E->progress >= 1; }, definition(Kind::Foundry).buildTime + 1))) return false;
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
    if (!HasAnyErrors()) AddInfo(FString::Printf(TEXT("CINDERLINE_UE_INTEGRATION_ECONOMY_PASS: gathered=%d, produced=%d, built=%d, ore=%d; normal paid commands, actual actor ticks, selected-controller dispatch, temporary snapshot only."),
        Battle.Sim().players()[0].stats.gathered, Battle.Sim().players()[0].stats.produced,
        Battle.Sim().players()[0].stats.built, Battle.Sim().players()[0].ore));
    return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
