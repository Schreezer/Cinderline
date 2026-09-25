#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderOnlineSubsystem.h"
#include "Presentation/CinderPlayerController.h"
#include "Sim/Network.h"
#include "Slate/SceneViewport.h"
#include "Tests/AutomationCommon.h"
#include <algorithm>
#include <iterator>
#include <limits>

namespace
{
struct FArmyControlFixture
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
        if (!Test.TestNotNull(TEXT("Army-control test world exists"), World)) return false;
        World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
        Battle = World->SpawnActor<ACinderBattlefield>();
        if (!Test.TestNotNull(TEXT("Army-control battlefield spawned"), Battle)) return false;
        if (!WorldOwner.BeginPlayInTestWorld())
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        Controller = World->SpawnActor<ACinderPlayerController>();
        if (!Test.TestNotNull(TEXT("Army-control controller spawned"), Controller)) return false;
        if (!Test.TestTrue(TEXT("Controller resolves the transient battlefield"),
            Controller->Battlefield() == Battle)) return false;
        Controller->ExecuteAction(TEXT("start"), 0);
        return Test.TestTrue(TEXT("Fixture starts a playable local match"),
            !Battle->IsMenu() && !Battle->IsOnlineMatch());
    }
};

struct FTemporaryArmySave
{
    FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("Automation"),
        TEXT("CinderlineArmyControl"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("load.cinder")));

    FTemporaryArmySave()
    {
        IFileManager::Get().MakeDirectory(*Directory, true);
    }

    ~FTemporaryArmySave()
    {
        IFileManager::Get().DeleteDirectory(*Directory, false, true);
    }
};

cinder::Id FindEntity(const cinder::Simulation& Sim, int Team, cinder::Kind Kind)
{
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == Team && Entity.kind == Kind) return Entity.id;
    return 0;
}

bool Contains(const std::vector<cinder::Id>& Ids, cinder::Id Id)
{
    return std::find(Ids.begin(), Ids.end(), Id) != Ids.end();
}

bool Equals(const std::vector<cinder::Id>& Actual, std::initializer_list<cinder::Id> Expected)
{
    return Actual == std::vector<cinder::Id>(Expected);
}

void ReplaceSnapshotId(cinder::net::Snapshot& Snapshot, cinder::Id Old, cinder::Id Replacement)
{
    for (cinder::Entity& Entity : Snapshot.entities)
    {
        if (Entity.id == Old) Entity.id = Replacement;
        if (Entity.target == Old) Entity.target = Replacement;
        if (Entity.resourceTarget == Old) Entity.resourceTarget = Replacement;
        if (Entity.builderId == Old) Entity.builderId = Replacement;
    }
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderArmyControlIntegration,
    "Cinderline.Integration.ArmyControl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderArmyControlIntegration::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace cinder;
    using FCandidate = ACinderPlayerController::FProjectedPickCandidate;

    // Candidate centers already contain the unit's projected altitude. This
    // test therefore covers the picking decision independently of whether the
    // corresponding ground-plane ray is inside the playable map.
    const std::vector<FCandidate> EdgeCandidates = {
        {91, FVector2D(1, 240), 18},       // elevated unit projected at the screen edge
        {23, FVector2D(32, 240), 40},
        {0, FVector2D(0, 240), 100},       // invalid handles never win
    };
    TestEqual(TEXT("A near-edge elevated projection wins before ground validation"),
        ACinderPlayerController::PickProjectedEntity(FVector2D(-2, 240), EdgeCandidates), Id(91));
    TestEqual(TEXT("Nearest projected center wins overlapping hit regions"),
        ACinderPlayerController::PickProjectedEntity(FVector2D(19, 240), EdgeCandidates), Id(23));
    const std::vector<FCandidate> TieCandidates = {
        {88, FVector2D(90, 100), 20}, {12, FVector2D(110, 100), 20}
    };
    TestEqual(TEXT("Equal projected distances resolve deterministically by entity ID"),
        ACinderPlayerController::PickProjectedEntity(FVector2D(100, 100), TieCandidates), Id(12));
    TestEqual(TEXT("A point just inside the rendered edge remains selectable"),
        ACinderPlayerController::PickProjectedEntity(FVector2D(109.99, 100), {{7, FVector2D(100, 100), 10}}), Id(7));
    TestEqual(TEXT("A point outside every rendered radius selects nothing"),
        ACinderPlayerController::PickProjectedEntity(FVector2D(110.01, 100), {{7, FVector2D(100, 100), 10}}), Id(0));

    FArmyControlFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderBattlefield& Battle = *Fixture.Battle;
    ACinderPlayerController& Controller = *Fixture.Controller;
    Simulation& Sim = Battle.Sim();

    const Id OwnWorker = FindEntity(Sim, 0, Kind::Worker);
    const Id EnemyWorker = FindEntity(Sim, 1, Kind::Worker);
    const Id Resource = FindEntity(Sim, -1, Kind::Resource);
    if (!TestTrue(TEXT("Fixture has owned, enemy, and resource selection cases"),
        OwnWorker && EnemyWorker && Resource)) return false;
    TestTrue(TEXT("Direct roster selection accepts an owned living entity"),
        Controller.SelectOwnedEntity(OwnWorker));
    TestTrue(TEXT("Direct roster selection records exactly the requested entity"),
        Equals(Controller.Selection(), {OwnWorker}));
    TestFalse(TEXT("Direct roster selection rejects an enemy entity"),
        Controller.SelectOwnedEntity(EnemyWorker));
    TestFalse(TEXT("Direct roster selection rejects an ore entity"),
        Controller.SelectOwnedEntity(Resource));
    TestFalse(TEXT("Direct roster selection rejects an unknown entity"),
        Controller.SelectOwnedEntity(std::numeric_limits<Id>::max()));
    TestTrue(TEXT("Rejected roster handles leave the previous valid selection intact"),
        Equals(Controller.Selection(), {OwnWorker}));

    Controller.SelectOwnedKind(Kind::Worker);
    const std::vector<Id> GlobalWorkers = Controller.Selection();
    Controller.SelectKind(Kind::Worker);
    const std::vector<Id> ViewWorkers = Controller.Selection();
    TestTrue(TEXT("Global roster selection finds every owned worker without a viewport"),
        !GlobalWorkers.empty());
    TestTrue(TEXT("Viewport selection remains a subset of global roster selection"),
        std::all_of(ViewWorkers.begin(), ViewWorkers.end(), [&](Id IdValue)
        { return Contains(GlobalWorkers, IdValue); }));
    TestTrue(TEXT("The headless fixture distinguishes global roster selection from visible selection"),
        ViewWorkers.size() < GlobalWorkers.size());

    Simulation SnapshotSource;
    Config SnapshotConfig; SnapshotConfig.ai = false;
    SnapshotSource.reset(SnapshotConfig);
    const Id SourceScout = SnapshotSource.debugSpawn(Kind::Scout, 0, {900, 900});
    cinder::net::Snapshot HighIdSnapshot = cinder::net::snapshotFor(SnapshotSource, 0);
    const Id OpaqueId = static_cast<Id>(std::numeric_limits<int32>::max()) + Id(57);
    ReplaceSnapshotId(HighIdSnapshot, SourceScout, OpaqueId);
    std::string SnapshotError;
    if (!TestTrue(TEXT("Snapshot-derived roster accepts an opaque ID above INT32_MAX"),
        Sim.applySnapshot(HighIdSnapshot, &SnapshotError)))
    {
        AddError(UTF8_TO_TCHAR(SnapshotError.c_str()));
        return false;
    }
    TestTrue(TEXT("Direct selection preserves the full unsigned roster ID"),
        Controller.SelectOwnedEntity(OpaqueId));
    TestTrue(TEXT("A normal roster prune does not truncate or discard the opaque ID"),
        (Controller.PruneArmyControlState(), Equals(Controller.Selection(), {OpaqueId})));

    Battle.StartMatch(0);
    Controller.ResetInteraction(true);
    const Id Worker = FindEntity(Sim, 0, Kind::Worker);
    const Id Headquarters = FindEntity(Sim, 0, Kind::Headquarters);
    const Id Ore = FindEntity(Sim, -1, Kind::Resource);
    const Id UnitA = Sim.debugSpawn(Kind::Striker, 0, {900, 900});
    const Id UnitB = Sim.debugSpawn(Kind::Lancer, 0, {1040, 900});
    const Id UnitC = Sim.debugSpawn(Kind::Scout, 0, {1180, 900});
    if (!TestTrue(TEXT("Squad fixture entities exist"),
        Worker && Headquarters && Ore && UnitA && UnitB && UnitC)) return false;

    Controller.Selected = {Worker, Headquarters, UnitA, UnitB, UnitA};
    TestTrue(TEXT("Assigning a mixed selection creates a combat-only squad"),
        Controller.AssignSquad(0));
    TestTrue(TEXT("Squad A excludes workers, structures, and duplicate IDs"),
        Equals(Controller.Squad(0), {UnitA, UnitB}));
    Controller.Selected = {UnitB, UnitC};
    TestTrue(TEXT("A second combat squad can be assigned"), Controller.AssignSquad(1));
    TestTrue(TEXT("Reassignment removes shared units from their former squad"),
        Equals(Controller.Squad(0), {UnitA}) && Equals(Controller.Squad(1), {UnitB, UnitC}));
    TestFalse(TEXT("Invalid squad indices cannot be assigned or recalled"),
        Controller.AssignSquad(-1) || Controller.RecallSquad(ACinderPlayerController::SquadCount));

    TestTrue(TEXT("Recalling Squad A selects only its independent unit"),
        Controller.RecallSquad(0) && Equals(Controller.Selection(), {UnitA}));
    const Vec2 MovePoint{1500, 1100};
    Controller.ExecuteAction(TEXT("move"));
    TestTrue(TEXT("Move mode is mutually exclusive with attack and defend modes"),
        Controller.IsMoveCommandMode() && !Controller.IsAttackMoveMode()
        && !Controller.IsDefendCommandMode() && !Controller.IsProductionRallyMode());
    if (!TestTrue(TEXT("Controller submits the explicit single-unit move"),
        Controller.IssueDestination(MovePoint))) return false;
    const RecordedCommand& SingleMove = Sim.recording().back();
    TestTrue(TEXT("The authoritative command contains only the recalled unit"),
        SingleMove.command.type == CommandType::Move && Equals(SingleMove.command.units, {UnitA}));
    TestTrue(TEXT("The selected unit receives the move while the other squads stay independent"),
        Sim.find(UnitA)->order == Order::Move
        && Sim.find(UnitB)->order != Order::Move && Sim.find(UnitC)->order != Order::Move);
    TestTrue(TEXT("A successful destination clears every explicit destination mode"),
        !Controller.IsMoveCommandMode() && !Controller.IsAttackMoveMode()
        && !Controller.IsDefendCommandMode() && !Controller.IsProductionRallyMode());

    TestTrue(TEXT("Recalling Squad B selects its two independent units"),
        Controller.RecallSquad(1) && Equals(Controller.Selection(), {UnitB, UnitC}));
    Controller.ExecuteAction(TEXT("attack"));
    Controller.ExecuteAction(TEXT("defend"));
    TestTrue(TEXT("Defend replaces a previously armed attack-move"),
        Controller.IsDefendCommandMode() && !Controller.IsAttackMoveMode()
        && !Controller.IsMoveCommandMode() && !Controller.IsProductionRallyMode());
    const Vec2 DefendPoint{1700, 1200};
    if (!TestTrue(TEXT("Controller submits Defend through the battlefield adapter"),
        Controller.IssueDestination(DefendPoint))) return false;
    TestTrue(TEXT("Both recalled units enter the authoritative Defend order"),
        Sim.find(UnitB)->order == Order::Defend && Sim.find(UnitC)->order == Order::Defend);
    TestTrue(TEXT("The recorded command preserves the Defend command type and squad IDs"),
        Sim.recording().back().command.type == CommandType::Defend
        && Equals(Sim.recording().back().command.units, {UnitB, UnitC}));

    const float OreBefore = Sim.find(Ore)->resource;
    Controller.ExecuteAction(TEXT("defend"));
    TestTrue(TEXT("A worker can be selected for an explicit move over ore"),
        Controller.SelectOwnedEntity(Worker));
    TestTrue(TEXT("Changing the direct selection cancels a pending destination mode"),
        !Controller.IsAttackMoveMode() && !Controller.IsMoveCommandMode()
        && !Controller.IsDefendCommandMode() && !Controller.IsProductionRallyMode());
    Controller.ExecuteAction(TEXT("move"));
    TestTrue(TEXT("Explicit Move uses the destination even when it is an ore context"),
        Controller.IssueDestination(Sim.find(Ore)->pos)
        && Sim.find(Worker)->order == Order::Move && Sim.find(Worker)->target == 0
        && Sim.find(Ore)->resource == OreBefore);
    TestTrue(TEXT("A combat unit can be selected for an explicit defend over a friendly structure"),
        Controller.SelectOwnedEntity(UnitA));
    Controller.ExecuteAction(TEXT("defend"));
    TestTrue(TEXT("Explicit Defend uses the destination instead of selecting or rallying the structure"),
        Controller.IssueDestination(Sim.find(Headquarters)->pos)
        && Sim.find(UnitA)->order == Order::Defend
        && Equals(Controller.Selection(), {UnitA}));
    Controller.ExecuteAction(TEXT("move"));
    Controller.Escape();
    TestTrue(TEXT("Escape cancels a destination mode while retaining the selected unit"),
        !Controller.IsAttackMoveMode() && !Controller.IsMoveCommandMode()
        && !Controller.IsDefendCommandMode() && !Controller.IsProductionRallyMode()
        && Equals(Controller.Selection(), {UnitA}));

    Controller.Selected = {UnitB, UnitC};
    TestTrue(TEXT("Squad B can be refreshed before authoritative death pruning"),
        Controller.AssignSquad(1));
    cinder::net::Snapshot DeathSnapshot = cinder::net::snapshotFor(Sim, 0);
    auto Dead = std::find_if(DeathSnapshot.entities.begin(), DeathSnapshot.entities.end(),
        [&](const cinder::Entity& Entity) { return Entity.id == UnitC; });
    if (!TestTrue(TEXT("Death snapshot contains the selected squad member"),
        Dead != DeathSnapshot.entities.end())) return false;
    Dead->hp = 0;
    if (!TestTrue(TEXT("Authoritative death snapshot applies"),
        Sim.applySnapshot(DeathSnapshot, &SnapshotError))) return false;
    Controller.PruneArmyControlState();
    TestTrue(TEXT("Dead IDs are removed from selection and saved squads"),
        !Contains(Controller.Selection(), UnitC) && !Contains(Controller.Squad(1), UnitC)
        && Contains(Controller.Squad(1), UnitB));

    Battle.StartMatch(0);
    Controller.ResetInteraction(true);
    const Id PersistentUnit = Sim.debugSpawn(Kind::Striker, 0, {900, 900});
    Controller.Selected = {PersistentUnit};
    TestTrue(TEXT("Lifecycle fixture assigns a persistent squad"), Controller.AssignSquad(2));
    Controller.ExecuteAction(TEXT("pause"));
    TestTrue(TEXT("Pausing preserves selection and squad membership"),
        Battle.IsPaused() && Equals(Controller.Selection(), {PersistentUnit})
        && Equals(Controller.Squad(2), {PersistentUnit}));
    Controller.ExecuteAction(TEXT("help"), 0);
    TestTrue(TEXT("Opening help preserves the paused army-control state"),
        Controller.IsHelpOpen() && Equals(Controller.Selection(), {PersistentUnit})
        && Equals(Controller.Squad(2), {PersistentUnit}));
    Controller.ExecuteAction(TEXT("helpclose"));
    Controller.ExecuteAction(TEXT("resume"));

    // A loaded save may reuse an ID for a different unit. Pruning cannot tell
    // that this valid ID belonged to an old match, so successful load must use
    // the controller's full match-transition reset.
    FTemporaryArmySave LoadFile;
    Simulation LoadSource;
    Config LoadConfig; LoadConfig.map = 0; LoadConfig.ai = false;
    LoadSource.reset(LoadConfig);
    Id ReusedId = 0;
    for (int32 Attempt = 0; Attempt < 512 && ReusedId < PersistentUnit; ++Attempt)
        ReusedId = LoadSource.debugSpawn(Kind::Kite, 0, {1200, 1200});
    if (!TestEqual(TEXT("Load fixture deliberately reuses the old squad ID"),
        ReusedId, PersistentUnit)) return false;
    if (!TestTrue(TEXT("Load fixture saves only to its temporary automation path"),
        LoadSource.save(TCHAR_TO_UTF8(*LoadFile.Path)))) return false;
    if (!TestTrue(TEXT("Battlefield accepts the valid temporary save"),
        Battle.LoadMatchFrom(LoadFile.Path))) return false;
    TestTrue(TEXT("The reused ID now names a different loaded combat unit"),
        Sim.find(PersistentUnit) && Sim.find(PersistentUnit)->kind == Kind::Kite);
    Controller.PruneArmyControlState();
    TestTrue(TEXT("A validity prune alone cannot identify a reused cross-match ID"),
        Equals(Controller.Selection(), {PersistentUnit})
        && Equals(Controller.Squad(2), {PersistentUnit}));
    Controller.ResetInteraction(true);
    TestTrue(TEXT("The successful-load transition clears selection and every saved squad"),
        Controller.Selection().empty() && Controller.Squad(0).empty()
        && Controller.Squad(1).empty() && Controller.Squad(2).empty());

    Controller.Selected = {PersistentUnit};
    if (!TestTrue(TEXT("Loaded combat roster can establish fresh army state"),
        Controller.AssignSquad(2))) return false;

    const FString MissingPreference = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("Automation"),
        TEXT("CinderlineArmyControl"), FGuid::NewGuid().ToString(EGuidFormats::Digits), TEXT("missing.ini"));
    Controller.LoadTutorialPreference(MissingPreference);
    Controller.ExecuteAction(TEXT("move"));
    Controller.ExecuteAction(TEXT("defend"));
    Controller.ExecuteAction(TEXT("squadassign"), 0);
    Controller.ExecuteAction(TEXT("start"), 2);
    TestTrue(TEXT("The first-run modal blocks commands and match reset without erasing army state"),
        Controller.IsTutorialOfferPending() && !Controller.IsMoveCommandMode()
        && !Controller.IsDefendCommandMode()
        && Equals(Controller.Selection(), {PersistentUnit})
        && Equals(Controller.Squad(2), {PersistentUnit}) && Battle.MapIndex() == 0);
    Controller.ExecuteAction(TEXT("onboardskip"));

    cinder::net::Snapshot OnlineSnapshot = cinder::net::snapshotFor(Sim, 0);
    UCinderOnlineSubsystem* Session = Controller.Online();
    if (!TestNotNull(TEXT("Fixture has the online subsystem used by controller polling"), Session)) return false;
    Session->bHasMatch = true;
    Session->bHasSnapshot = true;
    Session->Snapshot = OnlineSnapshot;
    Session->Room = TEXT("ARMY");
    Controller.PollOnlineState();
    TestTrue(TEXT("Starting a new online snapshot clears local selection and all squads"),
        Battle.IsOnlineMatch() && Controller.Selection().empty()
        && Controller.Squad(0).empty() && Controller.Squad(1).empty() && Controller.Squad(2).empty());
    Session->bHasMatch = false;
    Session->bHasSnapshot = false;
    Session->Room.Empty();

    Battle.ReturnToMenu();
    Controller.ExecuteAction(TEXT("start"), 0);
    Controller.Selected = {FindEntity(Sim, 0, Kind::Worker)};
    TestTrue(TEXT("A fresh local match accepts a new local roster selection"),
        !Battle.IsOnlineMatch() && !Controller.Selection().empty());
    Controller.ExecuteAction(TEXT("start"), 1);
    TestTrue(TEXT("Controller new-match action clears selection and every control group"),
        Controller.Selection().empty() && Controller.Squad(0).empty()
        && Controller.Squad(1).empty() && Controller.Squad(2).empty());

    AddInfo(FString::Printf(TEXT("CINDERLINE_ARMY_CONTROL_PASS opaque_id=%u projected_edge=1 single_move=1 defend=1 squad_count=%d online_reset=1"),
        OpaqueId, ACinderPlayerController::SquadCount));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderWorldTapIntegration,
    "Cinderline.Integration.WorldTapSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderWorldTapIntegration::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace cinder;
    FArmyControlFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderBattlefield& Battle = *Fixture.Battle;
    ACinderPlayerController& Controller = *Fixture.Controller;
    Simulation& Sim = Battle.Sim();
    Config Setup; Setup.ai = false;
    Sim.reset(Setup);
    Sim.debugResources(0, 9000);
    Controller.ResetInteraction(true);
    const Id Worker = FindEntity(Sim, 0, Kind::Worker);
    const Id Headquarters = FindEntity(Sim, 0, Kind::Headquarters);
    const Id Ore = FindEntity(Sim, -1, Kind::Resource);
    const Id Soldier = Sim.debugSpawn(Kind::Striker, 0, {1000, 1100});
    const Id Flyer = Sim.debugSpawn(Kind::Kite, 0, {1200, 1100});
    if (!TestTrue(TEXT("World-tap fixture has live units, a producer, and ore"),
        Worker && Headquarters && Ore && Soldier && Flyer)) return false;

    // FTestWorldWrapper deliberately creates no local player or viewport. Add
    // both here so the production projection, hit testing, deprojection, and
    // pointer reducer can be exercised under the NullRHI automation command.
    UGameViewportClient* ViewportClient = NewObject<UGameViewportClient>(GEngine);
    ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
    if (!TestNotNull(TEXT("Pointer fixture creates an engine viewport client"), ViewportClient)
        || !TestNotNull(TEXT("Pointer fixture creates an engine local player"), LocalPlayer)) return false;
    TSharedRef<FSceneViewport> TestViewport = FSceneViewport::Create(ViewportClient, nullptr);
    TestViewport->SetInitialSize(FIntPoint(1280, 720));
    Controller.SetPlayer(LocalPlayer);
    LocalPlayer->PlayerAdded(ViewportClient, 0);
    LocalPlayer->Origin = FVector2D::ZeroVector;
    LocalPlayer->Size = FVector2D(1, 1);
    ON_SCOPE_EXIT
    {
        LocalPlayer->PlayerRemoved();
    };
    ACinderCamera* Camera = Controller.GetWorld()->SpawnActor<ACinderCamera>();
    if (!TestNotNull(TEXT("Pointer fixture creates the production camera"), Camera)) return false;
    Controller.Possess(Camera);
    Controller.SetViewTarget(Camera);
    Camera->Focus(FVector(1100, 1100, 0), true);
    if (!Fixture.WorldOwner.TickTestWorld(Simulation::Step))
    {
        Fixture.WorldOwner.ForwardErrorMessages(this);
        return false;
    }
    if (!TestNotNull(TEXT("Pointer fixture initializes the production camera manager"),
        Controller.PlayerCameraManager.Get())) return false;
    Controller.PlayerCameraManager->UpdateCamera(Simulation::Step);
    if (!TestTrue(TEXT("Pointer fixture advances the production camera cache"),
        Controller.PlayerCameraManager->GetCameraCacheTime() > 0)) return false;

    FVector2D SoldierScreen, FlyerScreen;
    const Vec2 SoldierRender = Battle.RenderPosition(*Sim.find(Soldier));
    const Vec2 FlyerRender = Battle.RenderPosition(*Sim.find(Flyer));
    if (!TestTrue(TEXT("NullRHI viewport projects both mixed-unit centers"),
        Controller.ProjectWorldLocationToScreen(FVector(SoldierRender.x, SoldierRender.y, 25), SoldierScreen)
        && Controller.ProjectWorldLocationToScreen(FVector(FlyerRender.x, FlyerRender.y, 125), FlyerScreen))) return false;
    const FVector2D BoxMin(FMath::Min(SoldierScreen.X, FlyerScreen.X) - 8,
        FMath::Min(SoldierScreen.Y, FlyerScreen.Y) - 8);
    const FVector2D BoxMax(FMath::Max(SoldierScreen.X, FlyerScreen.X) + 8,
        FMath::Max(SoldierScreen.Y, FlyerScreen.Y) + 8);
    Controller.ResetInteraction(true);
    Controller.PointerPressed(BoxMin, false, false);
    Controller.PointerMoved(BoxMax, 0.1f);
    Controller.PointerReleased(BoxMax);
    const std::vector<Id> PointerSelection = Controller.Selection();
    if (!TestTrue(TEXT("A controller pointer drag creates a mixed ground-and-air selection"),
        Contains(PointerSelection, Soldier) && Contains(PointerSelection, Flyer)
        && PointerSelection.size() >= 2)) return false;
    TestTrue(TEXT("The completed pointer drag clears its gesture state"),
        !Controller.bPointerDown && !Controller.bDragging && !Controller.IsSelecting());
    if (!TestTrue(TEXT("The pointer-created mixed selection can be saved as a squad"),
        Controller.AssignSquad(2))) return false;
    const std::vector<Id> PointerSquad = Controller.Squad(2);
    if (!TestTrue(TEXT("The pointer-created squad retains both mixed combat units"),
        Contains(PointerSquad, Soldier) && Contains(PointerSquad, Flyer))) return false;

    const Vec2 PointerMovePoint{1500, 1100};
    FVector2D PointerMoveScreen;
    if (!TestTrue(TEXT("NullRHI viewport projects the contextual move destination"),
        Controller.ProjectWorldLocationToScreen(
            FVector(PointerMovePoint.x, PointerMovePoint.y, 0), PointerMoveScreen))) return false;
    const std::size_t BeforePointerMove = Sim.recording().size();
    Controller.PointerPressed(PointerMoveScreen, false, false);
    Controller.PointerReleased(PointerMoveScreen);
    if (!TestTrue(TEXT("A controller pointer click submits one contextual move for the pointer selection"),
        Sim.recording().size() == BeforePointerMove + 1)) return false;
    const Command PointerMove = Sim.recording().back().command;
    TestEqual(TEXT("The contextual pointer command is Move"), static_cast<int32>(PointerMove.type),
        static_cast<int32>(CommandType::Move));
    TestTrue(TEXT("The contextual move keeps every pointer-selected unit"), PointerMove.units == PointerSelection);
    // Unreal's FSceneView::DeprojectScreenToWorld truncates screen coordinates
    // to whole pixels. Check the commanded point against that actual cursor
    // pixel rather than assuming a fractional projection has an exact inverse.
    FVector2D CommandScreen;
    if (!TestTrue(TEXT("The pointer command projects back through the production camera"),
        Controller.ProjectWorldLocationToScreen(FVector(PointerMove.point.x, PointerMove.point.y, 0), CommandScreen))) return false;
    const FVector2D CursorPixel(static_cast<int32>(PointerMoveScreen.X), static_cast<int32>(PointerMoveScreen.Y));
    TestTrue(FString::Printf(TEXT("Pointer move returns to cursor pixel %.3f,%.3f; actual %.3f,%.3f"),
        CursorPixel.X, CursorPixel.Y, CommandScreen.X, CommandScreen.Y),
        FMath::Abs(CommandScreen.X - CursorPixel.X) < 0.05 && FMath::Abs(CommandScreen.Y - CursorPixel.Y) < 0.05);
    TestTrue(TEXT("All pointer-selected units receive the move order"),
        std::all_of(PointerSelection.begin(), PointerSelection.end(), [&](Id SelectedId)
        {
            return Sim.find(SelectedId) && Sim.find(SelectedId)->order == Order::Move;
        }));
    const uint64 PointerMoveHash = Sim.stateHash();
    const std::size_t PointerMoveCount = Sim.recording().size();
    Controller.ExecuteAction(TEXT("deselect"));
    TestTrue(TEXT("Deselect clears the pointer selection while preserving its squad and move orders"),
        Controller.Selection().empty() && Controller.Squad(2) == PointerSquad
        && std::all_of(PointerSelection.begin(), PointerSelection.end(), [&](Id SelectedId)
        {
            return Sim.find(SelectedId) && Sim.find(SelectedId)->order == Order::Move;
        }));
    TestTrue(TEXT("Deselect after the pointer move leaves authoritative state and recording unchanged"),
        Sim.stateHash() == PointerMoveHash && Sim.recording().size() == PointerMoveCount);

    Command Build; Build.type = CommandType::AutoBuild; Build.kind = Kind::Foundry;
    bool FoundSite = false;
    for (int32 Y = 400; Y <= 1200 && !FoundSite; Y += 80)
        for (int32 X = 400; X <= 1200 && !FoundSite; X += 80)
        {
            const Vec2 Site{static_cast<float>(X), static_cast<float>(Y)};
            if (Sim.autoBuildStatus(0, Kind::Foundry, &Site).accepted)
            {
                Build.point = Site;
                FoundSite = true;
            }
        }
    if (!TestTrue(TEXT("World-tap fixture finds an accessible foundation site"), FoundSite)
        || !TestTrue(TEXT("Foundation is created through an accepted simulation command"),
            Sim.command(Build).accepted)) return false;
    const Id Foundation = FindEntity(Sim, 0, Kind::Foundry);
    if (!TestTrue(TEXT("Friendly foundation remains unfinished and assigned"),
        Foundation && Sim.find(Foundation)->progress < 1 && Sim.constructionWorker(Foundation))) return false;

    const Vec2 MovePoint{1500, 1100};
    const Vec2 OrePoint = Sim.find(Ore)->pos;
    auto NoDestinations = [&]()
    {
        return !Controller.IsMoveCommandMode() && !Controller.IsAttackMoveMode()
            && !Controller.IsDefendCommandMode() && !Controller.IsProductionRallyMode();
    };
    auto CheckUnchanged = [&](const FString& Label, uint64 Hash, std::size_t Recorded)
    {
        return TestTrue(*Label, Sim.stateHash() == Hash && Sim.recording().size() == Recorded);
    };

    for (bool Touch : {false, true})
    {
        const FString Device = Touch ? TEXT("Touch") : TEXT("Desktop");
        Controller.SelectOwnedEntity(Soldier);
        Controller.bPointerTouch = Touch;
        Controller.ExecuteAction(TEXT("move"));
        Controller.LastTapEntity = Flyer;
        Controller.LastTapTime = Controller.GetWorld()->GetTimeSeconds();
        const std::size_t BeforeMove = Sim.recording().size();
        Controller.HandleWorldTap(0, &MovePoint);
        if (!TestTrue(*(Device + TEXT(" ground tap submits the armed Move")),
            Sim.recording().size() == BeforeMove + 1 && Sim.find(Soldier)->order == Order::Move
            && NoDestinations())) return false;
        TestTrue(*(Device + TEXT(" accepted Move clears the prior friendly double-tap history")),
            !Controller.LastTapEntity && Controller.LastTapTime < 0);
        const uint64 MoveHash = Sim.stateHash();
        const std::size_t MoveCount = Sim.recording().size();
        Controller.bPointerTouch = Touch;
        Controller.HandleWorldTap(Flyer, &MovePoint);
        TestTrue(*(Device + TEXT(" friendly tap after Move selects exactly that entity")),
            Equals(Controller.Selection(), {Flyer}));
        CheckUnchanged(Device + TEXT(" friendly tap after Move issues no order"), MoveHash, MoveCount);

        Controller.SelectOwnedEntity(Worker);
        Controller.bPointerTouch = Touch;
        Controller.LastTapEntity = Foundation;
        Controller.LastTapTime = Controller.GetWorld()->GetTimeSeconds();
        const std::size_t BeforeGather = Sim.recording().size();
        Controller.HandleWorldTap(Ore, &OrePoint);
        if (!TestTrue(*(Device + TEXT(" ore tap submits the normal Gather context")),
            Sim.recording().size() == BeforeGather + 1 && Sim.find(Worker)->order == Order::Gather)) return false;
        TestTrue(*(Device + TEXT(" accepted Gather clears the prior foundation double-tap history")),
            !Controller.LastTapEntity && Controller.LastTapTime < 0);
        const uint64 GatherHash = Sim.stateHash();
        const std::size_t GatherCount = Sim.recording().size();
        const Id BuilderBefore = Sim.constructionWorker(Foundation);
        Controller.HandleWorldTap(Foundation, &Build.point);
        TestTrue(*(Device + TEXT(" friendly unfinished foundation selects exactly the structure")),
            Equals(Controller.Selection(), {Foundation}));
        TestTrue(*(Device + TEXT(" foundation selection never reassigns construction")),
            Sim.constructionWorker(Foundation) == BuilderBefore && Sim.find(Worker)->order == Order::Gather);
        CheckUnchanged(Device + TEXT(" foundation tap after Gather does not submit ResumeConstruction"),
            GatherHash, GatherCount);

        Controller.SelectOwnedEntity(Headquarters);
        Controller.bPointerTouch = Touch;
        const Vec2 RallyBefore = Sim.find(Headquarters)->rally;
        const bool OverrideBefore = Sim.find(Headquarters)->rallyOverride;
        const uint64 BuildingHash = Sim.stateHash();
        const std::size_t BuildingCount = Sim.recording().size();
        Controller.HandleWorldTap(0, &MovePoint);
        TestTrue(*(Device + TEXT(" normal terrain tap clears a building selection without changing its rally")),
            Controller.Selection().empty() && NoDestinations()
            && Sim.find(Headquarters)->rally.x == RallyBefore.x && Sim.find(Headquarters)->rally.y == RallyBefore.y
            && Sim.find(Headquarters)->rallyOverride == OverrideBefore);
        CheckUnchanged(Device + TEXT(" building deselection preserves worker and army orders"), BuildingHash, BuildingCount);
    }

    Controller.SelectOwnedEntity(Headquarters);
    const Vec2 ContextRally{1700, 1800};
    const std::size_t BeforeContextRally = Sim.recording().size();
    Controller.HandleWorldTap(0, &ContextRally, true);
    if (!TestTrue(TEXT("An explicit desktop context click still submits one building rally"),
        Sim.recording().size() == BeforeContextRally + 1)) return false;
    TestTrue(TEXT("Desktop right-click rally targets only the selected building and preserves selection"),
        Sim.recording().back().command.type == CommandType::Rally
        && Equals(Sim.recording().back().command.units, {Headquarters})
        && Equals(Controller.Selection(), {Headquarters}) && Sim.find(Headquarters)->rallyOverride
        && Sim.find(Headquarters)->rally.x == ContextRally.x && Sim.find(Headquarters)->rally.y == ContextRally.y);

    Controller.SelectOwnedEntity(Soldier);
    Controller.BeginProductionRally(Headquarters);
    if (!TestTrue(TEXT("The explicit facility Rally action arms with a troop selected"),
        Controller.IsProductionRallyMode())) return false;
    const Vec2 ButtonRally{1800, 1700};
    const Order SoldierBeforeRally = Sim.find(Soldier)->order;
    const std::size_t BeforeButtonRally = Sim.recording().size();
    Controller.HandleWorldTap(0, &ButtonRally);
    if (!TestTrue(TEXT("The explicit Rally mode submits one automatic pinned rally"),
        Sim.recording().size() == BeforeButtonRally + 1)) return false;
    TestTrue(TEXT("The Rally action preserves troops and selection while clearing its one-shot mode"),
        Sim.recording().back().command.type == CommandType::AutoRally
        && Sim.recording().back().command.target == Headquarters && Sim.recording().back().command.units.empty()
        && Sim.find(Headquarters)->rally.x == ButtonRally.x && Sim.find(Headquarters)->rally.y == ButtonRally.y
        && Equals(Controller.Selection(), {Soldier}) && Sim.find(Soldier)->order == SoldierBeforeRally && NoDestinations());

    struct FDestinationCase { const TCHAR* Action; CommandType Type; Order Expected; };
    const FDestinationCase Destinations[] = {
        {TEXT("move"), CommandType::Move, Order::Move},
        {TEXT("attack"), CommandType::AttackMove, Order::AttackMove},
        {TEXT("defend"), CommandType::Defend, Order::Defend},
    };
    auto IsArmed = [&](CommandType Type)
    {
        return Controller.IsMoveCommandMode() == (Type == CommandType::Move)
            && Controller.IsAttackMoveMode() == (Type == CommandType::AttackMove)
            && Controller.IsDefendCommandMode() == (Type == CommandType::Defend)
            && !Controller.IsProductionRallyMode();
    };
    for (const FDestinationCase& Destination : Destinations)
    {
        Controller.SelectOwnedEntity(Soldier);
        Controller.ExecuteAction(Destination.Action);
        const std::size_t Before = Sim.recording().size();
        Controller.HandleWorldTap(Headquarters, &MovePoint);
        if (!TestTrue(*FString::Printf(TEXT("Explicit %s over a friendly hit records one order"), Destination.Action),
            Sim.recording().size() == Before + 1)) return false;
        const Command& Recorded = Sim.recording().back().command;
        TestTrue(*FString::Printf(TEXT("Explicit %s keeps the selected unit and uses the supplied ground point"), Destination.Action),
            Equals(Controller.Selection(), {Soldier}) && Equals(Recorded.units, {Soldier})
            && Recorded.type == Destination.Type && Recorded.point.x == MovePoint.x && Recorded.point.y == MovePoint.y
            && Sim.find(Soldier)->order == Destination.Expected && NoDestinations());

        for (int32 Invalid = 0; Invalid < 3; ++Invalid)
        {
            Controller.ResetInteraction(false);
            Controller.Selected = Invalid == 1 ? std::vector<Id>{Headquarters}
                : Invalid == 2 ? std::vector<Id>{} : std::vector<Id>{Soldier};
            Controller.ExecuteAction(Destination.Action);
            Controller.FeedbackText = TEXT("pending test destination");
            Controller.FeedbackLife = 0;
            const Vec2 InvalidGround{-1, Simulation::WorldSize + 1};
            const uint64 Hash = Sim.stateHash();
            const std::size_t Count = Sim.recording().size();
            Controller.HandleWorldTap(0, Invalid == 0 ? &InvalidGround : &MovePoint);
            TestTrue(*FString::Printf(TEXT("Rejected %s case %d retains its mode and reports feedback"), Destination.Action, Invalid),
                IsArmed(Destination.Type) && !Controller.FeedbackText.IsEmpty()
                && Controller.FeedbackText != TEXT("pending test destination") && Controller.FeedbackLife > 0);
            CheckUnchanged(FString::Printf(TEXT("Rejected %s case %d leaves simulation orders untouched"), Destination.Action, Invalid),
                Hash, Count);
        }
    }

    Controller.SelectOwnedEntity(Soldier);
    Controller.ExecuteAction(TEXT("attack"));
    const std::size_t BeforeMinimap = Sim.recording().size();
    Controller.ExecuteAction(TEXT("minimap"), 1100 * 10000 + 1500);
    if (!TestTrue(TEXT("Armed AttackMove accepts the minimap as a destination"),
        Sim.recording().size() == BeforeMinimap + 1)) return false;
    TestTrue(TEXT("Minimap AttackMove preserves its command type, coordinates, and selection"),
        Sim.recording().back().command.type == CommandType::AttackMove
        && Sim.recording().back().command.point.x == 1500 && Sim.recording().back().command.point.y == 1100
        && Equals(Sim.recording().back().command.units, {Soldier}) && NoDestinations());

    auto SeedLatch = [&](Id Target)
    {
        Controller.ResetInteraction(false);
        Controller.Selected = {Soldier};
        Controller.bPointerDown = true;
        Controller.bPointerTouch = true;
        Controller.bPointerWorldTapLatched = true;
        Controller.PointerStart = Controller.PointerLast = FVector2D(200, 200);
        Controller.PointerSelectionTarget = Target;
        Controller.PointerContextTarget = 0;
    };
    SeedLatch(Flyer);
    const Vec2 OriginalFlyerPosition = Sim.find(Flyer)->pos;
    Command Fly; Fly.type = CommandType::Move; Fly.units = {Flyer}; Fly.point = {1500, 1100};
    if (!TestTrue(TEXT("Latched friendly unit starts moving through a real command"), Sim.command(Fly).accepted)) return false;
    Sim.update(Simulation::Step);
    if (!TestTrue(TEXT("Latched unit changes position between press and release"),
        Sim.find(Flyer)->pos.x != OriginalFlyerPosition.x || Sim.find(Flyer)->pos.y != OriginalFlyerPosition.y)) return false;
    const uint64 MovingHash = Sim.stateHash();
    const std::size_t MovingCount = Sim.recording().size();
    Controller.PointerReleased(Controller.PointerStart);
    TestTrue(TEXT("Stationary release selects the originally latched moving entity without projection"),
        Equals(Controller.Selection(), {Flyer}) && !Controller.PointerSelectionTarget && !Controller.PointerContextTarget
        && !Controller.bPointerWorldTapLatched && !Controller.bPointerDown);
    CheckUnchanged(TEXT("Releasing the moving friendly latch adds no order"), MovingHash, MovingCount);

    SeedLatch(0);
    constexpr int32 CrossingSteps = 8;
    const Vec2 CrossingStart = Sim.find(Flyer)->pos;
    const auto& FlyerDefinition = definition(Sim.find(Flyer)->kind);
    const float CrossingDistance = FlyerDefinition.speed * Simulation::Step * CrossingSteps;
    const Vec2 EmptyGround{CrossingStart.x + CrossingDistance, CrossingStart.y};
    if (!TestTrue(TEXT("The press point initially lies outside the moving friendly unit's footprint"),
        CrossingDistance > FlyerDefinition.radius)) return false;
    // Move accepts arrival within 20 units. Cross the tap point on the way to
    // a farther destination so this fixture requires a real center crossing.
    Command CrossGround; CrossGround.type = CommandType::Move; CrossGround.units = {Flyer};
    CrossGround.point = {EmptyGround.x + 100, EmptyGround.y};
    if (!TestTrue(TEXT("A friendly unit moves toward ground that was empty at press time"),
        Sim.command(CrossGround).accepted)) return false;
    for (int32 Step = 0; Step < CrossingSteps; ++Step) Sim.update(Simulation::Step);
    if (!TestTrue(TEXT("The friendly unit reaches the originally empty ground before release"),
        FMath::Abs(Sim.find(Flyer)->pos.x - EmptyGround.x) < 1
        && FMath::Abs(Sim.find(Flyer)->pos.y - EmptyGround.y) < 1
        && Sim.find(Flyer)->order == Order::Move)) return false;
    const std::size_t BeforeGroundRelease = Sim.recording().size();
    Controller.HandleWorldTap(Flyer, &EmptyGround);
    if (!TestTrue(TEXT("The empty-ground press records one move despite a friendly release hit"),
        Sim.recording().size() == BeforeGroundRelease + 1)) return false;
    const Command& EmptyGroundMove = Sim.recording().back().command;
    TestTrue(TEXT("A friendly unit entering the finger never replaces the selected army or the original ground intent"),
        Equals(Controller.Selection(), {Soldier}) && EmptyGroundMove.type == CommandType::Move
        && Equals(EmptyGroundMove.units, {Soldier}) && EmptyGroundMove.point.x == EmptyGround.x
        && EmptyGroundMove.point.y == EmptyGround.y && Sim.find(Soldier)->order == Order::Move);

    SeedLatch(Foundation);
    Command Cancel; Cancel.type = CommandType::CancelBuilding; Cancel.units = {Foundation};
    if (!TestTrue(TEXT("Ordinary cancellation kills the latched unfinished foundation"),
        Sim.command(Cancel).accepted && !Sim.find(Foundation)->alive() && !Sim.isReplica())) return false;
    const uint64 DeadHash = Sim.stateHash();
    const std::size_t DeadCount = Sim.recording().size();
    Controller.PointerReleased(Controller.PointerStart);
    TestTrue(TEXT("A dead friendly latch is consumed without replacing the old selection"),
        Equals(Controller.Selection(), {Soldier}) && !Controller.PointerSelectionTarget && !Controller.PointerContextTarget
        && !Controller.bPointerWorldTapLatched && !Controller.bPointerDown);
    CheckUnchanged(TEXT("A dead latch cannot fall through into a move for the old selection"), DeadHash, DeadCount);

    for (int32 Gesture = 0; Gesture < 3; ++Gesture)
    {
        SeedLatch(Flyer);
        Controller.bDragging = Gesture == 0;
        Controller.bPointerCameraPan = Gesture == 1;
        Controller.bMultiTouch = Gesture == 2;
        if (Gesture == 2) { Controller.PointerSelectionTarget = 0; Controller.PointerContextTarget = Ore; }
        const uint64 Hash = Sim.stateHash();
        const std::size_t Count = Sim.recording().size();
        Controller.PointerReleased(Controller.PointerStart);
        TestTrue(*FString::Printf(TEXT("Gesture %d ignores and clears the friendly latch"), Gesture),
            Equals(Controller.Selection(), {Soldier}) && !Controller.PointerSelectionTarget && !Controller.PointerContextTarget
            && !Controller.bPointerWorldTapLatched && !Controller.bPointerDown);
        CheckUnchanged(FString::Printf(TEXT("Gesture %d cannot submit a latched world order"), Gesture), Hash, Count);
    }

    SeedLatch(Flyer);
    const uint64 FastDragHash = Sim.stateHash();
    const std::size_t FastDragCount = Sim.recording().size();
    Controller.PointerReleased(Controller.PointerStart + FVector2D(200, 0));
    TestTrue(TEXT("A fast release outside the tap threshold ignores the latch without an intermediate move event"),
        Equals(Controller.Selection(), {Soldier}) && !Controller.PointerSelectionTarget && !Controller.PointerContextTarget
        && !Controller.bPointerWorldTapLatched && !Controller.bPointerDown);
    CheckUnchanged(TEXT("A fast press-release drag adds no world order"), FastDragHash, FastDragCount);

    SeedLatch(0);
    Controller.PointerContextTarget = Ore;
    Controller.ResetInteraction(false);
    TestTrue(TEXT("Interaction reset clears the latched world intent and both target IDs"),
        !Controller.bPointerWorldTapLatched && !Controller.PointerSelectionTarget && !Controller.PointerContextTarget);

    for (const FDestinationCase& Destination : Destinations)
    {
        Controller.ResetInteraction(false);
        Controller.Selected = {Soldier};
        Controller.ExecuteAction(Destination.Action);
        TestTrue(*FString::Printf(TEXT("%s arms exactly its destination mode"), Destination.Action),
            IsArmed(Destination.Type));
        Controller.ExecuteAction(Destination.Action);
        TestTrue(*FString::Printf(TEXT("Repeating %s toggles its destination mode off"), Destination.Action),
            NoDestinations());
    }

    for (int32 SelectionMethod = 0; SelectionMethod < 3; ++SelectionMethod)
    {
        Controller.ResetInteraction(false);
        Controller.Selected = {Soldier};
        Controller.ExecuteAction(Destinations[SelectionMethod].Action);
        if (!TestTrue(*FString::Printf(TEXT("Selection helper %d starts with one real destination mode"), SelectionMethod),
            IsArmed(Destinations[SelectionMethod].Type))) return false;
        const uint64 Hash = Sim.stateHash();
        const std::size_t Count = Sim.recording().size();
        if (SelectionMethod == 0) Controller.SelectArmy();
        else if (SelectionMethod == 1) Controller.SelectKind(Kind::Worker);
        else Controller.SelectRectangle();
        TestTrue(*FString::Printf(TEXT("Selection helper %d cancels pending destinations"), SelectionMethod),
            NoDestinations() && !Controller.RallyProducer);
        CheckUnchanged(FString::Printf(TEXT("Selection helper %d changes no simulation orders"), SelectionMethod), Hash, Count);
    }

    Controller.ResetInteraction(false);
    Controller.Selected = {Soldier};
    if (!TestTrue(TEXT("Deselect fixture assigns a persistent squad"), Controller.AssignSquad(0))) return false;
    Controller.Selected = {Flyer};
    if (!TestTrue(TEXT("Deselect fixture assigns a second persistent squad"), Controller.AssignSquad(1))) return false;
    Command Train; Train.type = CommandType::Train; Train.units = {Headquarters}; Train.kind = Kind::Worker;
    if (!TestTrue(TEXT("Deselect fixture queues a real headquarters production job"), Sim.command(Train).accepted)) return false;
    const QueueItem QueuedBefore = Sim.find(Headquarters)->queue.back();
    const std::size_t QueueSizeBefore = Sim.find(Headquarters)->queue.size();
    const auto SquadsBefore = Controller.Squads;
    const Order SoldierOrder = Sim.find(Soldier)->order;
    const Order FlyerOrder = Sim.find(Flyer)->order;
    Controller.Selected = {Soldier};
    Controller.BeginProductionRally(Headquarters);
    if (!TestTrue(TEXT("Deselect reset covers a genuinely armed production rally"),
        Controller.IsProductionRallyMode() && Controller.ProductionRallyProducer() == Headquarters)) return false;
    Controller.ExecuteAction(TEXT("deselect"));
    TestTrue(TEXT("Deselect clears the production rally through the public action path"),
        Controller.Selection().empty() && NoDestinations() && !Controller.ProductionRallyProducer());

    Controller.Selected = {Soldier, Flyer, Headquarters};
    Controller.BeginGlobalBuild(Kind::Foundry);
    if (!TestTrue(TEXT("Deselect reset covers a genuinely armed automatic build"),
        Controller.IsGlobalBuildMode() && NoDestinations())) return false;
    Controller.bBoxSelect = true;
    Controller.bPointerDown = Controller.bDragging = Controller.bGestureSelect = Controller.bPointerTouch = true;
    Controller.bPointerUI = Controller.bMultiTouch = Controller.bTwoDown = Controller.bMousePan = true;
    Controller.bLongPress = Controller.bPointerCameraPan = Controller.bPointerPlacement = true;
    Controller.bPointerWorldTapLatched = true;
    Controller.PointerSelectionTarget = Flyer;
    Controller.PointerContextTarget = Ore;
    Controller.PointerStart = Controller.PointerLast = Controller.PreviousCentroid = Controller.PreviousMouse = FVector2D(80, 90);
    Controller.PointerHeld = Controller.PreviousPinch = 12;
    Controller.LastTapEntity = Flyer; Controller.LastTapTime = 5;
    Controller.PendingBuilding = Kind::Laboratory; Controller.Placement = {700, 900};
    for (float& Credit : Controller.ArrowPanCredit) Credit = 1;
    const uint64 DeselectHash = Sim.stateHash();
    const std::size_t DeselectCount = Sim.recording().size();
    Controller.ExecuteAction(TEXT("deselect"));
    TestTrue(TEXT("Deselect clears selection, all destination modes, construction, automatic jobs, and rally state"),
        Controller.Selection().empty() && NoDestinations() && !Controller.bBoxSelect && !Controller.bBuildMode
        && !Controller.bBuildMenu && !Controller.bAutomaticBuild && !Controller.RallyProducer);
    TestTrue(TEXT("Deselect clears every active pointer, pan, placement, and multitouch gesture"),
        !Controller.bPointerDown && !Controller.bDragging && !Controller.bGestureSelect && !Controller.bPointerTouch
        && !Controller.bPointerUI && !Controller.bMultiTouch && !Controller.bTwoDown && !Controller.bMousePan
        && !Controller.bLongPress && !Controller.bPointerCameraPan && !Controller.bPointerPlacement
        && !Controller.PointerSelectionTarget && !Controller.PointerContextTarget && !Controller.bPointerWorldTapLatched
        && !Controller.LastTapEntity && Controller.LastTapTime < 0
        && Controller.PointerHeld == 0 && Controller.PreviousPinch == 0);
    TestTrue(TEXT("Deselect clears pending geometry and camera gesture credit"),
        Controller.PointerStart == FVector2D::ZeroVector && Controller.PointerLast == FVector2D::ZeroVector
        && Controller.PreviousCentroid == FVector2D::ZeroVector && Controller.PreviousMouse == FVector2D::ZeroVector
        && Controller.Placement.x == 0 && Controller.Placement.y == 0
        && Controller.PendingBuilding == Kind::Foundry
        && std::all_of(std::begin(Controller.ArrowPanCredit), std::end(Controller.ArrowPanCredit), [](float Credit) { return Credit == 0; }));
    const Entity* ProducerAfter = Sim.find(Headquarters);
    TestTrue(TEXT("Deselect preserves squads, active orders, and the queued job identity"),
        Controller.Squads == SquadsBefore && Sim.find(Soldier)->order == SoldierOrder && Sim.find(Flyer)->order == FlyerOrder
        && ProducerAfter->queue.size() == QueueSizeBefore && ProducerAfter->queue.back().id == QueuedBefore.id
        && ProducerAfter->queue.back().remaining == QueuedBefore.remaining);
    CheckUnchanged(TEXT("Deselect changes neither authoritative state nor command recording"), DeselectHash, DeselectCount);

    Controller.Selected = {Soldier};
    Controller.ExecuteAction(TEXT("defend"));
    if (!TestTrue(TEXT("Elimination fixture starts with one genuinely armed destination"),
        Controller.IsDefendCommandMode())) return false;
    cinder::Simulation EliminatedAuthority = Sim;
    EliminatedAuthority.forfeit(0);
    if (!TestTrue(TEXT("Ordinary authority forfeiture produces a coherent local defeat"),
        EliminatedAuthority.eliminated(0) && EliminatedAuthority.winner() == 1)) return false;
    const cinder::net::Snapshot Eliminated = cinder::net::snapshotFor(EliminatedAuthority, 0);
    std::string EliminationError;
    if (!TestTrue(TEXT("A valid terminal snapshot eliminates the local player"),
        Sim.applySnapshot(Eliminated, &EliminationError)))
    {
        AddError(UTF8_TO_TCHAR(EliminationError.c_str()));
        return false;
    }
    Controller.PruneArmyControlState();
    TestTrue(TEXT("Local elimination clears the pending destination mode"),
        Sim.eliminated(0) && NoDestinations());
    AddInfo(TEXT("CINDERLINE_WORLD_TAP_PASS controller_pointer_mixed_drag_move=1 friendly_touch=1 friendly_desktop=1 destination_modes=3 toggles=3 rejected_modes=9 minimap_attack_move=1 moving_latch=1 empty_ground_latch=1 dead_latch=1 gesture_latches=4 deselect_preserves_jobs=1 elimination_clears_mode=1"));
    return !HasAnyErrors();
}

#endif
