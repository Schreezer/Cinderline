#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderOnlineSubsystem.h"
#include "Presentation/CinderPlayerController.h"
#include "Slate/SceneViewport.h"
#include "Tests/AutomationCommon.h"

namespace
{
struct FPatrolEscortFixture
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
        if (!Test.TestNotNull(TEXT("Patrol/Escort test world exists"), World)) return false;
        World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
        Battle = World->SpawnActor<ACinderBattlefield>();
        if (!Test.TestNotNull(TEXT("Patrol/Escort battlefield spawned"), Battle)) return false;
        if (!WorldOwner.BeginPlayInTestWorld())
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        Controller = World->SpawnActor<ACinderPlayerController>();
        if (!Test.TestNotNull(TEXT("Patrol/Escort controller spawned"), Controller)) return false;
        Controller->ExecuteAction(TEXT("start"), 0);
        return Test.TestTrue(TEXT("Patrol/Escort fixture starts a local match"),
            !Battle->IsMenu() && !Battle->IsOnlineMatch());
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderPatrolEscortIntegration,
    "Cinderline.Integration.PatrolEscortPointerPaths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderPatrolEscortIntegration::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace cinder;
    FPatrolEscortFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderPlayerController& Controller = *Fixture.Controller;
    ACinderBattlefield& Battle = *Fixture.Battle;
    Simulation& Sim = Battle.Sim();
    Config Setup; Setup.ai = false;
    Sim.reset(Setup);
    Controller.ResetInteraction(true);

    const Id FriendlyAnchor = Sim.debugSpawn(Kind::Headquarters, 0, {500, 500});
    const Id EnemyAnchor = Sim.debugSpawn(Kind::Headquarters, 1, {1800, 1800});
    const Id EscortA = Sim.debugSpawn(Kind::Striker, 0, {900, 900});
    const Id EscortB = Sim.debugSpawn(Kind::Striker, 0, {980, 900});
    const Id Leader = Sim.debugSpawn(Kind::Scout, 0, {1120, 980});
    const Id Enemy = Sim.debugSpawn(Kind::Striker, 1, {1320, 1040});
    const Id Resource = Sim.debugSpawn(Kind::Resource, -1, {1380, 1100});
    if (!TestTrue(TEXT("Patrol/Escort actors spawn"), FriendlyAnchor && EnemyAnchor
        && EscortA && EscortB && Leader && Enemy && Resource)) return false;
    Sim.update(Simulation::Step);

    UGameViewportClient* ViewportClient = NewObject<UGameViewportClient>(GEngine);
    ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
    if (!TestNotNull(TEXT("Patrol/Escort fixture creates a viewport client"), ViewportClient)
        || !TestNotNull(TEXT("Patrol/Escort fixture creates a local player"), LocalPlayer)) return false;
    TSharedRef<FSceneViewport> TestViewport = FSceneViewport::Create(ViewportClient, nullptr);
    TestViewport->SetInitialSize(FIntPoint(1280, 720));
    Controller.SetPlayer(LocalPlayer);
    LocalPlayer->PlayerAdded(ViewportClient, 0);
    LocalPlayer->Origin = FVector2D::ZeroVector;
    LocalPlayer->Size = FVector2D(1, 1);
    ON_SCOPE_EXIT { LocalPlayer->PlayerRemoved(); };
    ACinderCamera* Camera = Controller.GetWorld()->SpawnActor<ACinderCamera>();
    if (!TestNotNull(TEXT("Patrol/Escort fixture creates the production camera"), Camera)) return false;
    Controller.Possess(Camera);
    Controller.SetViewTarget(Camera);
    Camera->Focus(FVector(1100, 1000, 0), true);
    if (!Fixture.WorldOwner.TickTestWorld(Simulation::Step))
    {
        Fixture.WorldOwner.ForwardErrorMessages(this);
        return false;
    }
    if (!TestNotNull(TEXT("Patrol/Escort fixture initializes the camera manager"),
        Controller.PlayerCameraManager.Get())) return false;
    Controller.PlayerCameraManager->UpdateCamera(Simulation::Step);

    const auto ProjectEntity = [&](Id IdToProject, FVector2D& Screen)
    {
        const Entity* Entity = Sim.find(IdToProject);
        if (!Entity) return false;
        const Vec2 Point = Battle.RenderPosition(*Entity);
        return Controller.ProjectWorldLocationToScreen(FVector(Point.x, Point.y,
            definition(Entity->kind).air ? 125 : 25), Screen);
    };
    FVector2D LeaderScreen, EnemyScreen, ResourceScreen, GroundScreen;
    if (!TestTrue(TEXT("Production camera projects Patrol/Escort pointer targets"),
        ProjectEntity(Leader, LeaderScreen) && ProjectEntity(Enemy, EnemyScreen)
        && ProjectEntity(Resource, ResourceScreen)
        && Controller.ProjectWorldLocationToScreen(FVector(1500, 700, 0), GroundScreen))) return false;

    Controller.SelectOwnedKind(Kind::Striker);
    Controller.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::P, IE_Pressed, 1.0f));
    Controller.ProcessPlayerInput(Simulation::Step, false);
    Controller.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::P, IE_Released, 0.0f));
    Controller.ProcessPlayerInput(Simulation::Step, false);
    if (!TestTrue(TEXT("Desktop P arms Patrol through the normal player-input pass"),
        Controller.IsPatrolCommandMode())) return false;
    Controller.PointerPressed(EnemyScreen, false, false);
    Controller.PointerReleased(EnemyScreen);
    if (!TestTrue(TEXT("Projected mouse Patrol over an enemy uses its ground point and replaces"),
        Sim.recording().back().command.type == CommandType::Patrol
        && Sim.recording().back().command.target == 0
        && Sim.recording().back().command.queueMode == CommandQueueMode::Replace
        && Sim.find(EscortA)->order == Order::Patrol && Sim.find(EscortB)->order == Order::Patrol
        && !Controller.HasDestinationMode())) return false;

    Controller.SelectOwnedEntity(EscortA);
    Command LeaderMove; LeaderMove.type = CommandType::Move; LeaderMove.team = 0;
    LeaderMove.units = {Leader}; LeaderMove.point = {1600, 980};
    if (!TestTrue(TEXT("Escort leader begins a moving order"), Sim.command(LeaderMove).accepted)) return false;
    Controller.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Pressed, 1.0f));
    Controller.ProcessPlayerInput(Simulation::Step, false);
    Controller.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Released, 0.0f));
    Controller.ProcessPlayerInput(Simulation::Step, false);
    if (!TestTrue(TEXT("Desktop E arms Escort through the normal player-input pass"),
        Controller.IsEscortCommandMode())) return false;
    const std::size_t BeforeInvalidEscort = Sim.recording().size();
    Controller.PointerPressed(GroundScreen, true, false);
    Controller.PointerReleased(GroundScreen);
    TestTrue(TEXT("Projected touch ground rejects Escort without changing selection or intent"),
        Sim.recording().size() == BeforeInvalidEscort
        && Controller.Selection() == std::vector<Id>{EscortA} && Controller.IsEscortCommandMode());

    Controller.PointerPressed(ResourceScreen, true, false);
    Controller.PointerReleased(ResourceScreen);
    TestTrue(TEXT("Projected touch resource also retains explicit Escort targeting"),
        Sim.recording().size() == BeforeInvalidEscort && Controller.IsEscortCommandMode());
    Controller.PointerPressed(LeaderScreen, true, false);
    Sim.update(0.5f);
    Controller.PointerReleased(LeaderScreen);
    if (!TestTrue(TEXT("Projected touch Escort latches the owned leader identity and consumes intent"),
        Sim.recording().size() == BeforeInvalidEscort + 1
        && Sim.recording().back().command.type == CommandType::Escort
        && Sim.recording().back().command.target == Leader
        && Sim.find(EscortA)->order == Order::Escort && !Controller.HasDestinationMode())) return false;

    const Vec2 LeaderGoal = Sim.find(Leader)->goal;
    FVector2D LeaderCurrentScreen;
    if (!TestTrue(TEXT("Production camera reprojects the moving Escort leader"),
        ProjectEntity(Leader, LeaderCurrentScreen))) return false;
    Controller.Selected = {EscortB, Leader};
    Controller.ExecuteAction(TEXT("escort"));
    Controller.PointerPressed(LeaderCurrentScreen, true, false);
    Controller.PointerReleased(LeaderCurrentScreen);
    TestTrue(TEXT("Selected Escort leader remains untouched while other recipients escort it"),
        Sim.find(Leader)->order == Order::Move && Sim.find(Leader)->goal.x == LeaderGoal.x
        && Sim.find(Leader)->goal.y == LeaderGoal.y && Sim.find(EscortB)->order == Order::Escort);

    Controller.SelectOwnedEntity(Leader);
    Controller.ExecuteAction(TEXT("escort"));
    const std::size_t BeforeOnlyLeader = Sim.recording().size();
    Controller.PointerPressed(LeaderCurrentScreen, true, false);
    Controller.PointerReleased(LeaderCurrentScreen);
    TestTrue(TEXT("Touch Escort with only its leader selected rejects and retains targeting"),
        Sim.recording().size() == BeforeOnlyLeader && Sim.find(Leader)->order == Order::Move
        && Controller.IsEscortCommandMode());

    Controller.SelectOwnedEntity(EscortA);
    Controller.PointerPressed(LeaderCurrentScreen, false, false);
    Controller.PointerReleased(LeaderCurrentScreen);
    TestTrue(TEXT("Friendly pointer tap returns to ordinary selection after Escort completes"),
        Controller.Selection() == std::vector<Id>{Leader});

    UCinderOnlineSubsystem* Session = Controller.Online();
    if (!TestNotNull(TEXT("Patrol/Escort fixture exposes online acknowledgements"), Session)) return false;
    const auto ArmPending = [&](uint32 Sequence, ACinderPlayerController::EDestinationMode Mode)
    {
        Controller.ClearDestinationModes();
        Controller.Selected = {EscortA};
        Controller.DestinationMode = Mode;
        Controller.PendingIntentSequence = Sequence;
        Controller.PendingIntentGeneration = Controller.DestinationGeneration;
        Controller.PendingIntentMode = Mode;
        Controller.PendingIntentSelection = Controller.Selected;
        Controller.PendingIntentWasQueueNext = false;
        Session->PendingCommands.Add(Sequence, FPlatformTime::Seconds());
    };
    Battle.bOnlineMatch = true;
    ArmPending(61, ACinderPlayerController::EDestinationMode::Patrol);
    Session->HandleText(TEXT("{\"type\":\"ack\",\"seq\":61,\"accepted\":true,\"message\":\"Patrol accepted\"}"));
    Controller.PollOnlineState();
    TestTrue(TEXT("Patrol authoritative acceptance consumes matching targeting intent"),
        !Controller.IsDestinationPending() && !Controller.HasDestinationMode());

    ArmPending(62, ACinderPlayerController::EDestinationMode::Escort);
    Session->HandleText(TEXT("{\"type\":\"ack\",\"seq\":62,\"accepted\":false,\"message\":\"Escort rejected\"}"));
    Controller.PollOnlineState();
    TestTrue(TEXT("Escort authoritative rejection retains matching targeting intent"),
        !Controller.IsDestinationPending() && Controller.IsEscortCommandMode());

    ArmPending(63, ACinderPlayerController::EDestinationMode::Patrol);
    const std::size_t BeforeDuplicate = Sim.recording().size();
    TestTrue(TEXT("Pending Patrol blocks only its duplicate destination submission"),
        !Controller.IssueDestination({1450, 1200}) && Sim.recording().size() == BeforeDuplicate
        && Controller.IsDestinationPending());
    Battle.bOnlineMatch = false;
    Controller.SelectOwnedEntity(EscortB);
    Battle.bOnlineMatch = true;
    Session->HandleText(TEXT("{\"type\":\"ack\",\"seq\":63,\"accepted\":false,\"message\":\"Late rejection\"}"));
    Controller.PollOnlineState();
    TestTrue(TEXT("Late rejection after selection change cannot re-arm Patrol"),
        !Controller.IsDestinationPending() && !Controller.HasDestinationMode()
        && Controller.Selection() == std::vector<Id>{EscortB});

    ArmPending(64, ACinderPlayerController::EDestinationMode::Escort);
    Controller.ClearDestinationModes();
    Session->HandleText(TEXT("{\"type\":\"ack\",\"seq\":64,\"accepted\":false,\"message\":\"Late rejection\"}"));
    Controller.PollOnlineState();
    TestTrue(TEXT("Late rejection after cancellation cannot re-arm Escort"),
        !Controller.IsDestinationPending() && !Controller.HasDestinationMode());

    ArmPending(65, ACinderPlayerController::EDestinationMode::Patrol);
    Session->ReportUncertainOrders(TEXT("Reconnecting."));
    Controller.PollOnlineState();
    TestTrue(TEXT("Reconnect uncertainty clears a pending Patrol without re-arming it"),
        !Controller.IsDestinationPending() && !Controller.HasDestinationMode());

    Battle.bOnlineMatch = false;
    Controller.Selected = {EscortA};
    Controller.DestinationMode = ACinderPlayerController::EDestinationMode::Escort;
    Battle.bOnlineMatch = true;
    const bool bEscortSentWithoutTransport = Controller.IssueEscortTarget(Leader);
    TestTrue(TEXT("Escort send failure retains targeting without entering pending state"),
        !bEscortSentWithoutTransport && Controller.IsEscortCommandMode()
        && !Controller.IsDestinationPending());
    Battle.bOnlineMatch = false;
    Session->Leave();
    return true;
}

#endif
