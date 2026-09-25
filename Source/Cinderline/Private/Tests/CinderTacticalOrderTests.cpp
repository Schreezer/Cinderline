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
struct FTacticalOrderFixture
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
        if (!Test.TestNotNull(TEXT("Tactical-order test world exists"), World)) return false;
        World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
        Battle = World->SpawnActor<ACinderBattlefield>();
        if (!Test.TestNotNull(TEXT("Tactical-order battlefield spawned"), Battle)) return false;
        if (!WorldOwner.BeginPlayInTestWorld())
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        Controller = World->SpawnActor<ACinderPlayerController>();
        if (!Test.TestNotNull(TEXT("Tactical-order controller spawned"), Controller)) return false;
        Controller->ExecuteAction(TEXT("start"), 0);
        return Test.TestTrue(TEXT("Tactical-order fixture starts a local match"),
            !Battle->IsMenu() && !Battle->IsOnlineMatch());
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTacticalOrderIntegration,
    "Cinderline.Integration.TacticalOrderPointerPaths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTacticalOrderIntegration::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace cinder;
    FTacticalOrderFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderPlayerController& Controller = *Fixture.Controller;
    ACinderBattlefield& Battle = *Fixture.Battle;
    Simulation& Sim = Battle.Sim();
    Config Setup; Setup.ai = false;
    Sim.reset(Setup);
    Controller.ResetInteraction(true);

    const Id FriendlyAnchor = Sim.debugSpawn(Kind::Headquarters, 0, {500, 500});
    const Id EnemyAnchor = Sim.debugSpawn(Kind::Headquarters, 1, {1800, 1800});
    const Id Unit = Sim.debugSpawn(Kind::Striker, 0, {1000, 1000});
    const Id Other = Sim.debugSpawn(Kind::Worker, 0, {1040, 1000});
    const Id Enemy = Sim.debugSpawn(Kind::Striker, 1, {1120, 1000});
    const Id Resource = Sim.debugSpawn(Kind::Resource, -1, {1160, 1040});
    if (!TestTrue(TEXT("Tactical-order actors spawn"),
        FriendlyAnchor && EnemyAnchor && Unit && Other && Enemy && Resource)) return false;
    Sim.update(Simulation::Step);
    if (!TestTrue(TEXT("Pointer targets are visible or explored to the local player"),
        Sim.visible(0, Sim.find(Enemy)->pos) && Sim.explored(0, Sim.find(Resource)->pos))) return false;

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

    const auto ProjectGround = [&](Vec2 Point, FVector2D& Screen)
    {
        return Controller.ProjectWorldLocationToScreen(FVector(Point.x, Point.y, Battle.GroundHeight(Point)), Screen);
    };
    const auto ProjectEntity = [&](Id EntityId, FVector2D& Screen)
    {
        const Entity* Entity = Sim.find(EntityId);
        if (!Entity) return false;
        const Vec2 RenderPoint = Battle.RenderPosition(*Entity);
        return Controller.ProjectWorldLocationToScreen(FVector(RenderPoint.x, RenderPoint.y,
            Battle.EntityGroundHeight(RenderPoint, Entity->kind)
                + (definition(Entity->kind).air ? 125 : 25)), Screen);
    };

    Controller.SelectOwnedEntity(Unit);
    const Vec2 FirstPoint{1500, 1000};
    FVector2D FirstScreen;
    if (!TestTrue(TEXT("NullRHI viewport projects a ground waypoint"),
        ProjectGround(FirstPoint, FirstScreen))) return false;
    const std::size_t BeforeShiftMove = Sim.recording().size();
    Controller.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift, IE_Pressed, 1.0f));
    // InputKey accumulates platform events. The regular player-input pass
    // evaluates that accumulator into the held-key state read by gameplay.
    Controller.ProcessPlayerInput(Simulation::Step, false);
    if (!TestTrue(TEXT("Controller input state observes the simulated desktop Shift press"),
        Controller.IsInputKeyDown(EKeys::LeftShift))) return false;
    Controller.PointerPressed(FirstScreen, false, false);
    Controller.PointerReleased(FirstScreen);
    Controller.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift, IE_Released, 0.0f));
    Controller.ProcessPlayerInput(Simulation::Step, false);
    if (!TestTrue(TEXT("Contextual pointer Move samples desktop Shift and appends"),
        Sim.recording().size() == BeforeShiftMove + 1
        && Sim.recording().back().command.type == CommandType::Move
        && Sim.recording().back().command.queueMode == CommandQueueMode::Append
        && Sim.find(Unit)->order == Order::Move)) return false;

    Controller.ExecuteAction(TEXT("attack"));
    Controller.ExecuteAction(TEXT("queuenext"));
    FVector2D EnemyScreen;
    if (!TestTrue(TEXT("NullRHI viewport projects the enemy pick target"),
        ProjectEntity(Enemy, EnemyScreen))) return false;
    Controller.PointerPressed(EnemyScreen, true, false);
    Controller.PointerReleased(EnemyScreen);
    if (!TestTrue(TEXT("Armed touch Attack move over an enemy appends its ground waypoint"),
        Sim.recording().back().command.type == CommandType::AttackMove
        && Sim.recording().back().command.queueMode == CommandQueueMode::Append
        && Sim.recording().back().command.target == 0
        && Sim.find(Unit)->futureOrders.size() == 1
        && Sim.find(Unit)->futureOrders.front().order == Order::AttackMove)) return false;
    TestTrue(TEXT("Successful touch queued destination consumes its one-shot intent"),
        !Controller.IsQueueNextArmed() && !Controller.HasDestinationMode());

    Controller.ExecuteAction(TEXT("move"));
    Controller.ExecuteAction(TEXT("queuenext"));
    const std::size_t BeforeRejected = Sim.recording().size();
    const Vec2 Outside{-1, 1000};
    Controller.HandleWorldTap(0, &Outside);
    TestTrue(TEXT("Rejected destination retains the armed one-shot intent"),
        Controller.IsQueueNextArmed() && Controller.IsMoveCommandMode()
        && Sim.recording().size() == BeforeRejected);

    FVector2D ResourceScreen;
    if (!TestTrue(TEXT("NullRHI viewport projects the resource pick target"),
        ProjectEntity(Resource, ResourceScreen))) return false;
    Controller.PointerPressed(ResourceScreen, false, false);
    Controller.PointerReleased(ResourceScreen);
    if (!TestTrue(TEXT("Armed Move pointer over a resource appends ground movement rather than Gather"),
        Sim.recording().back().command.type == CommandType::Move
        && Sim.recording().back().command.queueMode == CommandQueueMode::Append
        && Sim.recording().back().command.target == 0
        && Sim.find(Unit)->futureOrders.size() == 2)) return false;

    Controller.SelectOwnedEntity(FriendlyAnchor);
    Controller.ExecuteAction(TEXT("move"));
    Controller.ExecuteAction(TEXT("queuenext"));
    const std::size_t BeforePointerRejection = Sim.recording().size();
    Controller.PointerPressed(FirstScreen, false, false);
    Controller.PointerReleased(FirstScreen);
    TestTrue(TEXT("Rejected projected pointer destination retains its armed one-shot intent"),
        Sim.recording().size() == BeforePointerRejection && Controller.IsQueueNextArmed()
        && Controller.IsMoveCommandMode());

    Controller.ClearDestinationModes();
    FVector2D OtherScreen;
    if (!TestTrue(TEXT("NullRHI viewport projects the owned selection target"),
        ProjectEntity(Other, OtherScreen))) return false;
    Controller.PointerPressed(OtherScreen, false, false);
    Controller.PointerReleased(OtherScreen);
    if (!TestTrue(TEXT("Production pointer picking selects the projected owned unit"),
        Controller.Selection() == std::vector<Id>{Other})) return false;

    Controller.ExecuteAction(TEXT("queuenext"));
    Controller.PointerPressed(ResourceScreen, true, false);
    Controller.PointerReleased(ResourceScreen);
    if (!TestTrue(TEXT("Touch Queue appends Gather for one Drudge without changing selection"),
        Sim.recording().back().command.type == CommandType::Gather
        && Sim.recording().back().command.queueMode == CommandQueueMode::Append
        && Sim.recording().back().command.units == std::vector<Id>{Other}
        && Controller.Selection() == std::vector<Id>{Other}
        && !Controller.IsQueueNextArmed())) return false;

    Controller.ExecuteAction(TEXT("queuenext"));
    Controller.ExecuteAction(TEXT("build"), static_cast<int32>(Kind::Foundry));
    const int OreBeforePlannedBuild = Sim.players()[0].ore;
    const Vec2 PlannedSite{1320, 1240};
    Controller.HandleWorldTap(0, &PlannedSite);
    if (!TestTrue(TEXT("Touch Queue appends an unpaid build and keeps placement plus Drudge selection"),
        Sim.recording().back().command.type == CommandType::Build
        && Sim.recording().back().command.queueMode == CommandQueueMode::Append
        && Sim.recording().back().command.units == std::vector<Id>{Other}
        && Sim.players()[0].ore == OreBeforePlannedBuild
        && Controller.Selection() == std::vector<Id>{Other}
        && Controller.IsBuildMode() && Controller.IsQueueNextArmed())) return false;
    Controller.ExecuteAction(TEXT("cancelplacement"));

    // Keep the resume site inside the main plateau with its complete footprint
    // clear of the new north retaining wall at y=1550.
    const Id SiteBuilder = Sim.debugSpawn(Kind::Worker, 0, {700, 1370});
    Command StartSite; StartSite.type = CommandType::Build; StartSite.team = 0;
    StartSite.units = {SiteBuilder}; StartSite.kind = Kind::Foundry; StartSite.point = {1000, 1370};
    if (!TestTrue(TEXT("Resume pointer fixture creates a paid unfinished site"),
        Sim.command(StartSite).accepted)) return false;
    Id Foundation = 0;
    for (const Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == Kind::Foundry && Entity.progress < 1)
            Foundation = Entity.id;
    Command PauseSite; PauseSite.type = CommandType::Stop; PauseSite.team = 0; PauseSite.units = {SiteBuilder};
    if (!TestTrue(TEXT("Resume pointer fixture pauses its construction site"),
        Foundation && Sim.command(PauseSite).accepted && Sim.constructionWorker(Foundation) == 0)) return false;
    FVector2D FoundationScreen;
    if (!TestTrue(TEXT("NullRHI viewport projects the queued resume target"),
        ProjectEntity(Foundation, FoundationScreen))) return false;
    Controller.ExecuteAction(TEXT("queuenext"));
    Controller.PointerPressed(FoundationScreen, true, false);
    Controller.PointerReleased(FoundationScreen);
    if (!TestTrue(TEXT("Touch Queue appends Resume for the selected Drudge without selecting the site"),
        Sim.recording().back().command.type == CommandType::ResumeConstruction
        && Sim.recording().back().command.queueMode == CommandQueueMode::Append
        && Sim.recording().back().command.target == Foundation
        && Sim.recording().back().command.units == std::vector<Id>{Other}
        && Controller.Selection() == std::vector<Id>{Other}
        && !Controller.IsQueueNextArmed())) return false;

    Controller.ExecuteAction(TEXT("move"));
    Controller.ExecuteAction(TEXT("queuenext"));
    Controller.SelectOwnedEntity(Unit);
    TestTrue(TEXT("Selection change clears queued targeting intent"),
        Controller.Selection() == std::vector<Id>{Unit}
        && !Controller.IsQueueNextArmed() && !Controller.HasDestinationMode());

    Controller.SelectOwnedEntity(Unit);
    Controller.ExecuteAction(TEXT("move"));
    Controller.ExecuteAction(TEXT("queuenext"));
    Controller.ApplicationWillEnterBackground();
    TestTrue(TEXT("Background pause clears queued targeting intent"),
        Fixture.Battle->IsPaused() && !Controller.IsQueueNextArmed() && !Controller.HasDestinationMode());
    Controller.ExecuteAction(TEXT("resume"));

    Controller.SelectOwnedEntity(Unit);
    Controller.ExecuteAction(TEXT("attack"));
    Controller.ExecuteAction(TEXT("queuenext"));
    Controller.TouchPressed(ETouchIndex::Touch2, FVector(10, 10, 0));
    TestTrue(TEXT("Second touch clears queued targeting intent"),
        !Controller.IsQueueNextArmed() && !Controller.HasDestinationMode());

    const Order CurrentBeforeClear = Sim.find(Unit)->order;
    Controller.ExecuteAction(TEXT("clearorders"));
    TestTrue(TEXT("Clear queued orders keeps the current order"),
        Sim.find(Unit)->order == CurrentBeforeClear && Sim.find(Unit)->futureOrders.empty());

    TestEqual(TEXT("Desktop Shift appends contextual Move"),
        static_cast<int32>(ACinderPlayerController::ResolveQueueMode(CommandType::Move, true, false, false)),
        static_cast<int32>(CommandQueueMode::Append));
    TestEqual(TEXT("Touch Queue next appends explicit Attack move"),
        static_cast<int32>(ACinderPlayerController::ResolveQueueMode(CommandType::AttackMove, false, true, true)),
        static_cast<int32>(CommandQueueMode::Append));
    for (CommandType Type : {CommandType::Build, CommandType::ResumeConstruction, CommandType::Gather})
    {
        TestEqual(TEXT("Desktop Shift appends worker plan commands"),
            static_cast<int32>(ACinderPlayerController::ResolveQueueMode(Type, true, false, false)),
            static_cast<int32>(CommandQueueMode::Append));
        TestEqual(TEXT("Touch Queue appends worker plan commands without a destination mode"),
            static_cast<int32>(ACinderPlayerController::ResolveQueueMode(Type, false, true, false)),
            static_cast<int32>(CommandQueueMode::Append));
    }
    for (CommandType Type : {CommandType::Attack, CommandType::Rally,
        CommandType::Defend, CommandType::AutoRally})
        TestEqual(TEXT("Non-tactical commands always replace"),
            static_cast<int32>(ACinderPlayerController::ResolveQueueMode(Type, true, true, true)),
            static_cast<int32>(CommandQueueMode::Replace));
    TestEqual(TEXT("Touch queue intent does not append contextual movement"),
        static_cast<int32>(ACinderPlayerController::ResolveQueueMode(CommandType::Move, false, true, false)),
        static_cast<int32>(CommandQueueMode::Replace));

    UCinderOnlineSubsystem* Session = Controller.Online();
    if (!TestNotNull(TEXT("Tactical-order fixture exposes the online acknowledgement store"), Session)) return false;
    const auto ArmPending = [&](uint32 Sequence)
    {
        Controller.ClearDestinationModes();
        Controller.Selected = {Unit};
        Controller.DestinationMode = ACinderPlayerController::EDestinationMode::Move;
        Controller.bQueueNext = true;
        Controller.PendingIntentSequence = Sequence;
        Controller.PendingIntentGeneration = Controller.DestinationGeneration;
        Controller.PendingIntentMode = Controller.DestinationMode;
        Controller.PendingIntentSelection = Controller.Selected;
        Controller.PendingIntentWasQueueNext = true;
        Session->PendingCommands.Add(Sequence, FPlatformTime::Seconds());
    };
    Battle.bOnlineMatch = true;
    ArmPending(41);
    Session->HandleText(TEXT("{\"type\":\"ack\",\"seq\":41,\"accepted\":true,\"message\":\"Waypoint accepted\"}"));
    Controller.PollOnlineState();
    TestTrue(TEXT("Controller poll consumes an authoritative acceptance for the matching one-shot intent"),
        !Controller.IsQueueNextPending() && !Controller.IsQueueNextArmed() && !Controller.HasDestinationMode());

    ArmPending(42);
    Session->HandleText(TEXT("{\"type\":\"ack\",\"seq\":42,\"accepted\":false,\"message\":\"Waypoint rejected\"}"));
    Controller.PollOnlineState();
    TestTrue(TEXT("Controller poll retains the matching one-shot intent after authoritative rejection"),
        !Controller.IsQueueNextPending() && Controller.IsQueueNextArmed() && Controller.IsMoveCommandMode());

    ArmPending(43);
    Session->ReportUncertainOrders(TEXT("Reconnecting."));
    Controller.PollOnlineState();
    TestTrue(TEXT("Controller poll clears an uncertain pending intent after reconnect state is reset"),
        !Controller.IsQueueNextPending() && !Controller.IsQueueNextArmed() && !Controller.HasDestinationMode());

    ArmPending(44);
    Battle.bOnlineMatch = false;
    Controller.SelectOwnedEntity(Other);
    Battle.bOnlineMatch = true;
    Session->HandleText(TEXT("{\"type\":\"ack\",\"seq\":44,\"accepted\":false,\"message\":\"Late rejection\"}"));
    Controller.PollOnlineState();
    TestTrue(TEXT("Controller poll cannot re-arm old targeting from a late rejection after selection change"),
        !Controller.IsQueueNextPending() && !Controller.IsQueueNextArmed() && !Controller.HasDestinationMode()
        && Controller.Selection() == std::vector<Id>{Other});

    ArmPending(45);
    Controller.ClearDestinationModes();
    Session->HandleText(TEXT("{\"type\":\"ack\",\"seq\":45,\"accepted\":false,\"message\":\"Late rejection\"}"));
    Controller.PollOnlineState();
    TestTrue(TEXT("Controller poll cannot re-arm old targeting from a late rejection after cancellation"),
        !Controller.IsQueueNextPending() && !Controller.IsQueueNextArmed() && !Controller.HasDestinationMode());

    Battle.bOnlineMatch = false;
    Controller.Selected = {Unit};
    Controller.DestinationMode = ACinderPlayerController::EDestinationMode::Move;
    Controller.bQueueNext = true;
    Battle.bOnlineMatch = true;
    const bool bSubmittedWithoutTransport = Controller.IssueDestination({1400, 1000});
    Battle.bOnlineMatch = false;
    TestTrue(TEXT("Online send failure retains the armed one-shot intent without creating pending state"),
        !bSubmittedWithoutTransport && Controller.IsQueueNextArmed() && Controller.IsMoveCommandMode()
        && !Controller.IsQueueNextPending());

    Session->PendingCommands.Add(50, FPlatformTime::Seconds());
    Session->HandleText(TEXT("{\"type\":\"ack\",\"seq\":50,\"accepted\":false,\"message\":\"Waypoint rejected\"}"));
    FCinderOnlineCommandAcknowledgement Rejection;
    TestTrue(TEXT("Exact sequence acknowledgement is available to its controller"),
        Session->ConsumeCommandAcknowledgement(50, Rejection) && !Rejection.bAccepted
        && Rejection.Message == TEXT("Waypoint rejected") && !Session->IsCommandPending(50));

    for (uint32 Sequence = 1000; Sequence < 1140; ++Sequence)
    {
        Session->PendingCommands.Add(Sequence, FPlatformTime::Seconds());
        Session->HandleText(FString::Printf(
            TEXT("{\"type\":\"ack\",\"seq\":%u,\"accepted\":true}"), Sequence));
    }
    TestTrue(TEXT("Unconsumed acknowledgement results remain bounded and retain the newest sequence"),
        Session->CommandAcknowledgements.Num() == 128
        && !Session->CommandAcknowledgements.Contains(1000)
        && Session->CommandAcknowledgements.Contains(1139));

    Session->Leave();
    TestTrue(TEXT("Leave lifecycle clears pending commands and retained acknowledgement results"),
        Session->PendingCommands.IsEmpty() && Session->CommandAcknowledgements.IsEmpty());
    uint32 FailedSequence = 99;
    Command OfflineCommand; OfflineCommand.type = CommandType::Move;
    TestTrue(TEXT("Failed or zero-sequence send cannot create pending acknowledgement state"),
        !Session->SendCommand(OfflineCommand, &FailedSequence) && FailedSequence == 0
        && Session->PendingCommands.IsEmpty());
    return true;
}

#endif
