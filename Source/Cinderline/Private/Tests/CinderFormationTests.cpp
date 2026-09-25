#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderPlayerController.h"
#include "Slate/SceneViewport.h"
#include "Tests/AutomationCommon.h"

namespace
{
struct FFormationFixture
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
        if (!Test.TestNotNull(TEXT("Formation test world exists"), World)) return false;
        World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
        Battle = World->SpawnActor<ACinderBattlefield>();
        if (!Test.TestNotNull(TEXT("Formation battlefield spawned"), Battle)) return false;
        if (!WorldOwner.BeginPlayInTestWorld())
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        Controller = World->SpawnActor<ACinderPlayerController>();
        if (!Test.TestNotNull(TEXT("Formation controller spawned"), Controller)) return false;
        Controller->ExecuteAction(TEXT("start"), 0);
        return Test.TestTrue(TEXT("Formation fixture starts a local match"),
            !Battle->IsMenu() && !Battle->IsOnlineMatch());
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderFormationIntegration,
    "Cinderline.Integration.FormationPointerPaths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderFormationIntegration::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace cinder;
    FFormationFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderPlayerController& Controller = *Fixture.Controller;
    Simulation& Sim = Fixture.Battle->Sim();
    Config Setup; Setup.ai = false;
    Sim.reset(Setup);
    Controller.ResetInteraction(true);

    const Id FriendlyAnchor = Sim.debugSpawn(Kind::Headquarters, 0, {500, 500});
    const Id EnemyAnchor = Sim.debugSpawn(Kind::Headquarters, 1, {1800, 1800});
    const Id First = Sim.debugSpawn(Kind::Striker, 0, {900, 900});
    const Id Second = Sim.debugSpawn(Kind::Scout, 0, {980, 900});
    if (!TestTrue(TEXT("Formation actors spawn"), FriendlyAnchor && EnemyAnchor && First && Second)) return false;
    Sim.update(Simulation::Step);

    UGameViewportClient* ViewportClient = NewObject<UGameViewportClient>(GEngine);
    ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
    if (!TestNotNull(TEXT("Formation fixture creates viewport client"), ViewportClient)
        || !TestNotNull(TEXT("Formation fixture creates local player"), LocalPlayer)) return false;
    TSharedRef<FSceneViewport> TestViewport = FSceneViewport::Create(ViewportClient, nullptr);
    TestViewport->SetInitialSize(FIntPoint(1280, 720));
    Controller.SetPlayer(LocalPlayer);
    LocalPlayer->PlayerAdded(ViewportClient, 0);
    LocalPlayer->Origin = FVector2D::ZeroVector;
    LocalPlayer->Size = FVector2D(1, 1);
    ON_SCOPE_EXIT { LocalPlayer->PlayerRemoved(); };
    ACinderCamera* Camera = Controller.GetWorld()->SpawnActor<ACinderCamera>();
    if (!TestNotNull(TEXT("Formation fixture creates production camera"), Camera)) return false;
    Controller.Possess(Camera);
    Controller.SetViewTarget(Camera);
    Camera->Focus(FVector(1100, 1000, 0), true);
    if (!Fixture.WorldOwner.TickTestWorld(Simulation::Step))
    {
        Fixture.WorldOwner.ForwardErrorMessages(this);
        return false;
    }
    if (!TestNotNull(TEXT("Formation fixture initializes camera manager"),
        Controller.PlayerCameraManager.Get())) return false;
    Controller.PlayerCameraManager->UpdateCamera(Simulation::Step);
    const auto Project = [&](Vec2 Point, FVector2D& Screen)
    {
        return Controller.ProjectWorldLocationToScreen(FVector(Point.x, Point.y, 0), Screen);
    };
    FVector2D CenterScreen, DirectionScreen;
    if (!TestTrue(TEXT("Production camera projects formation drag"),
        Project({1250, 980}, CenterScreen) && Project({1450, 1080}, DirectionScreen))) return false;

    Controller.Selected = {First, Second};
    Controller.ExecuteAction(TEXT("move"));
    Controller.ExecuteAction(TEXT("space"));
    Controller.ExecuteAction(TEXT("facenext"));
    if (!TestTrue(TEXT("Orders actions arm Wide facing for the next destination"),
        Controller.FormationSpacingPreset() == FormationSpacing::Wide && Controller.IsFaceNextArmed())) return false;

    const std::size_t BeforeShort = Sim.recording().size();
    Controller.PointerPressed(CenterScreen, false, false);
    Controller.PointerReleased(CenterScreen);
    TestTrue(TEXT("Projected short mouse drag issues nothing and retains facing workflow"),
        Sim.recording().size() == BeforeShort && Controller.IsFaceNextArmed()
        && Controller.IsMoveCommandMode() && !Controller.IsFacingPointerActive());

    Controller.PointerPressed(CenterScreen, false, false);
    Controller.PointerMoved(DirectionScreen, Simulation::Step);
    Controller.PointerReleased(DirectionScreen);
    if (!TestTrue(TEXT("Projected mouse facing drag submits one Wide replacing Move"),
        Sim.recording().size() == BeforeShort + 1
        && Sim.recording().back().command.type == CommandType::Move
        && Sim.recording().back().command.queueMode == CommandQueueMode::Replace
        && Sim.recording().back().command.spacing == FormationSpacing::Wide
        && Sim.recording().back().command.hasArrivalFacing
        && !Controller.IsFaceNextArmed() && !Controller.HasDestinationMode())) return false;

    Controller.ExecuteAction(TEXT("move"));
    Controller.ExecuteAction(TEXT("facenext"));
    Controller.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift, IE_Pressed, 1.0f));
    Controller.ProcessPlayerInput(Simulation::Step, false);
    Controller.PointerPressed(CenterScreen, false, false);
    Controller.PointerMoved(DirectionScreen, Simulation::Step);
    Controller.PointerReleased(DirectionScreen);
    Controller.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift, IE_Released, 0.0f));
    Controller.ProcessPlayerInput(Simulation::Step, false);
    if (!TestTrue(TEXT("Facing drag samples desktop Shift on release and appends Move"),
        Sim.recording().back().command.type == CommandType::Move
        && Sim.recording().back().command.queueMode == CommandQueueMode::Append
        && Sim.recording().back().command.hasArrivalFacing)) return false;

    Controller.ExecuteAction(TEXT("attack"));
    Controller.ExecuteAction(TEXT("queuenext"));
    Controller.ExecuteAction(TEXT("facenext"));
    Controller.PointerPressed(CenterScreen, true, false);
    Controller.PointerMoved(DirectionScreen, Simulation::Step);
    Controller.PointerReleased(DirectionScreen);
    if (!TestTrue(TEXT("Projected touch Queue next carries facing and spacing into one appended Attack move"),
        Sim.recording().back().command.type == CommandType::AttackMove
        && Sim.recording().back().command.queueMode == CommandQueueMode::Append
        && Sim.recording().back().command.spacing == FormationSpacing::Wide
        && Sim.recording().back().command.hasArrivalFacing
        && !Controller.IsQueueNextArmed() && !Controller.IsFaceNextArmed())) return false;

    Controller.ExecuteAction(TEXT("move"));
    Controller.ExecuteAction(TEXT("facenext"));
    Controller.PointerPressed(CenterScreen, true, false);
    Controller.TouchPressed(ETouchIndex::Touch2, FVector(DirectionScreen.X, DirectionScreen.Y, 0));
    Controller.PointerReleased(DirectionScreen);
    TestTrue(TEXT("Second touch cancels the formation gesture and consumes its later release"),
        !Controller.IsFaceNextArmed() && !Controller.HasDestinationMode()
        && Sim.recording().back().command.type == CommandType::AttackMove);

    const std::size_t BeforeSelectionCancel = Sim.recording().size();
    Controller.ExecuteAction(TEXT("move"));
    Controller.ExecuteAction(TEXT("facenext"));
    Controller.PointerPressed(CenterScreen, false, false);
    Controller.SelectOwnedEntity(Second);
    Controller.PointerReleased(DirectionScreen);
    TestTrue(TEXT("Selection change cancels facing and prevents the stale release command"),
        Sim.recording().size() == BeforeSelectionCancel && !Controller.HasDestinationMode()
        && Controller.Selection() == std::vector<Id>{Second});

    Controller.ExecuteAction(TEXT("move"));
    Controller.ExecuteAction(TEXT("facenext"));
    Controller.PointerPressed(CenterScreen, false, false);
    Controller.PointerMoved(DirectionScreen, Simulation::Step);
    const std::size_t BeforeMissingRelease = Sim.recording().size();
    Controller.CancelPointerWithoutRelease();
    Controller.PointerPressed(CenterScreen, false, true);
    Controller.PointerMoved(DirectionScreen, Simulation::Step);
    Controller.PointerReleased(DirectionScreen);
    TestTrue(TEXT("Missing-position release cancels facing before the next Option pan"),
        Sim.recording().size() == BeforeMissingRelease && !Controller.IsFaceNextArmed()
        && !Controller.IsFacingPointerActive() && !Controller.HasDestinationMode());

    Controller.Selected = {First};
    Controller.DestinationMode = ACinderPlayerController::EDestinationMode::Move;
    Controller.bFaceNext = true;
    Controller.PendingIntentSequence = 71;
    Controller.PendingIntentGeneration = Controller.DestinationGeneration;
    Controller.PendingIntentMode = Controller.DestinationMode;
    Controller.PendingIntentSelection = Controller.Selected;
    Controller.PendingIntentSpacing = Controller.SpacingPreset;
    Controller.PendingIntentHasArrivalFacing = true;
    Controller.PendingIntentArrivalFacing = 0.5f;
    Controller.PendingIntentPoint = {1250, 980};
    Controller.ResolveDestinationAcknowledgement(71, false);
    TestTrue(TEXT("Matching facing rejection retains the same mode and one-shot facing workflow"),
        !Controller.IsDestinationPending() && Controller.IsMoveCommandMode() && Controller.IsFaceNextArmed());

    Controller.PendingIntentSequence = 72;
    Controller.PendingIntentGeneration = Controller.DestinationGeneration;
    Controller.PendingIntentMode = Controller.DestinationMode;
    Controller.PendingIntentSelection = Controller.Selected;
    Controller.PendingIntentSpacing = Controller.SpacingPreset;
    Controller.PendingIntentHasArrivalFacing = true;
    Controller.ResolveDestinationAcknowledgement(72, true);
    TestTrue(TEXT("Matching facing acceptance consumes mode and one-shot facing workflow"),
        !Controller.IsDestinationPending() && !Controller.HasDestinationMode() && !Controller.IsFaceNextArmed());

    Controller.DestinationMode = ACinderPlayerController::EDestinationMode::Move;
    Controller.bFaceNext = true;
    Controller.PendingIntentSequence = 73;
    Controller.PendingIntentGeneration = Controller.DestinationGeneration;
    Controller.PendingIntentMode = Controller.DestinationMode;
    Controller.PendingIntentSelection = Controller.Selected;
    Controller.PendingIntentSpacing = Controller.SpacingPreset;
    Controller.PendingIntentHasArrivalFacing = true;
    Controller.ClearDestinationModes();
    Controller.ResolveDestinationAcknowledgement(73, false);
    TestTrue(TEXT("Late facing rejection after cancellation consumes correlation without rearming"),
        !Controller.IsDestinationPending() && !Controller.HasDestinationMode() && !Controller.IsFaceNextArmed());
    return true;
}

#endif
