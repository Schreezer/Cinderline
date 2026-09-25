#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderHUD.h"
#include "Presentation/CinderPlayerController.h"
#include "Slate/SceneViewport.h"
#include "Tests/AutomationCommon.h"
#include "Tests/CinderGuidedTestDriver.h"
#include "UnrealClient.h"

namespace
{
constexpr int32 ViewWidth = 956;
constexpr int32 ViewHeight = 440;

class FHUDInputRenderTarget final : public FRenderTarget
{
public:
    virtual FIntPoint GetSizeXY() const override { return FIntPoint(ViewWidth, ViewHeight); }
};

struct FHUDInputFixture
{
    FTestWorldWrapper WorldOwner;
    ACinderBattlefield* Battle = nullptr;
    ACinderPlayerController* Controller = nullptr;
    ACinderHUD* HUD = nullptr;
    UGameViewportClient* ViewportClient = nullptr;
    ULocalPlayer* LocalPlayer = nullptr;

    ~FHUDInputFixture()
    {
        if (LocalPlayer && LocalPlayer->ViewportClient) LocalPlayer->PlayerRemoved();
    }

    bool Initialize(FAutomationTestBase& Test)
    {
        if (!WorldOwner.CreateTestWorld(EWorldType::Game))
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        UWorld* World = WorldOwner.GetTestWorld();
        if (!Test.TestNotNull(TEXT("HUD input test world exists"), World)) return false;
        World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
        Battle = World->SpawnActor<ACinderBattlefield>();
        if (!Test.TestNotNull(TEXT("HUD input battlefield spawned"), Battle)) return false;
        if (!WorldOwner.BeginPlayInTestWorld())
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        Controller = World->SpawnActor<ACinderPlayerController>();
        if (!Test.TestNotNull(TEXT("HUD input controller spawned"), Controller)) return false;

        ViewportClient = NewObject<UGameViewportClient>(GEngine);
        LocalPlayer = NewObject<ULocalPlayer>(GEngine);
        if (!Test.TestNotNull(TEXT("HUD input viewport client created"), ViewportClient)
            || !Test.TestNotNull(TEXT("HUD input local player created"), LocalPlayer)) return false;
        TSharedRef<FSceneViewport> Viewport = FSceneViewport::Create(ViewportClient, nullptr);
        Viewport->SetInitialSize(FIntPoint(ViewWidth, ViewHeight));
        Controller->SetPlayer(LocalPlayer);
        LocalPlayer->PlayerAdded(ViewportClient, 0);
        LocalPlayer->Origin = FVector2D::ZeroVector;
        LocalPlayer->Size = FVector2D(1, 1);
        Controller->ClientSetHUD(ACinderHUD::StaticClass());
        HUD = Controller->GetHUD<ACinderHUD>();
        if (!Test.TestNotNull(TEXT("Production HUD is owned by the controller"), HUD)) return false;

        ACinderCamera* Camera = World->SpawnActor<ACinderCamera>();
        if (!Test.TestNotNull(TEXT("HUD input production camera spawned"), Camera)) return false;
        Controller->Possess(Camera);
        Controller->SetViewTarget(Camera);
        Camera->Focus(FVector(700, 700, 0), true);
        if (!WorldOwner.TickTestWorld(cinder::Simulation::Step))
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        if (!Test.TestNotNull(TEXT("HUD input camera manager initialized"),
            Controller->PlayerCameraManager.Get())) return false;
        Controller->PlayerCameraManager->UpdateCamera(cinder::Simulation::Step);

        Battle->StartTutorial();
        FString Failure;
        if (!Test.TestTrue(*FString::Printf(TEXT("Guided fixture reaches Open TRAIN: %s"), *Failure),
            DriveGuidedTutorialToStep(Battle->Sim(), Battle->Tutorial(),
                ECinderTutorialStep::TrainWorker, Failure))) return false;
        const std::vector<cinder::Id> Workers =
            CinderGuidedTestDriver::CompleteFriendly(Battle->Sim(), cinder::Kind::Worker);
        if (!Test.TestTrue(TEXT("Guided fixture retains a selectable Drudge"), !Workers.empty())) return false;
        return Test.TestTrue(TEXT("Guided Drudge is selected through the production controller path"),
            Controller->SelectOwnedEntity(Workers.front()));
    }
};

struct FCanvasState
{
    int32 SizeX = 0;
    int32 SizeY = 0;
    float ClipX = 0;
    float ClipY = 0;
    int32 TransformDepth = 0;
    FMatrix Transform = FMatrix::Identity;
};

FCanvasState CanvasState(const UCanvas& Canvas)
{
    FCanvasState State;
    State.SizeX = Canvas.SizeX;
    State.SizeY = Canvas.SizeY;
    State.ClipX = Canvas.ClipX;
    State.ClipY = Canvas.ClipY;
    State.TransformDepth = Canvas.Canvas->GetTransformStack().Num();
    State.Transform = Canvas.Canvas->GetFullTransform();
    return State;
}

bool SameCanvasState(const FCanvasState& A, const FCanvasState& B)
{
    return A.SizeX == B.SizeX && A.SizeY == B.SizeY
        && FMath::IsNearlyEqual(A.ClipX, B.ClipX) && FMath::IsNearlyEqual(A.ClipY, B.ClipY)
        && A.TransformDepth == B.TransformDepth && A.Transform.Equals(B.Transform, KINDA_SMALL_NUMBER);
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderHUDTouchSafeZoneIntegration,
    "Cinderline.Integration.HUDTouchSafeZone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderHUDTouchSafeZoneIntegration::RunTest(const FString& Parameters)
{
    (void)Parameters;
    if (!TestTrue(TEXT("Slate is initialized for safe-zone input regression"),
        FSlateApplication::IsInitialized())) return false;

    FSlateApplication& Slate = FSlateApplication::Get();
    const bool bHadCustomSafeZone = Slate.IsCustomSafeZoneSet();
    const FMargin PreviousSafeZone = Slate.GetCustomSafeZone();
    ON_SCOPE_EXIT
    {
        if (bHadCustomSafeZone) Slate.SetCustomSafeZone(PreviousSafeZone);
        else Slate.ResetCustomSafeZone();
    };

    FHUDInputFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderHUD& HUD = *Fixture.HUD;
    ACinderPlayerController& Controller = *Fixture.Controller;
    cinder::Simulation& Sim = Fixture.Battle->Sim();
    HUD.UnitPortraits.Reset();
    HUD.MenuBackdrop = nullptr;

    FHUDInputRenderTarget RenderTarget;
    FCanvas RenderCanvas(&RenderTarget, nullptr, Fixture.WorldOwner.GetTestWorld(),
        GMaxRHIFeatureLevel, FCanvas::CDM_DeferDrawing);
    RenderCanvas.SetAllowedModes(0);
    UCanvas* Canvas = NewObject<UCanvas>();
    if (!TestNotNull(TEXT("Actual UCanvas is created"), Canvas)) return false;
    HUD.Canvas = Canvas;

    const auto ApplySafeZone = [&](const FMargin& SafePixels)
    {
        // Populate the display extent used by UCanvas, then convert the desired
        // Android pixel insets to Slate's half-screen ratios.
        Slate.SetCustomSafeZone(FMargin());
        Canvas->Init(ViewWidth, ViewHeight, nullptr, &RenderCanvas);
        Canvas->UpdateSafeZoneData();
        Slate.SetCustomSafeZone(FMargin(
            2.0f * SafePixels.Left / FMath::Max(1, Canvas->CachedDisplayWidth),
            2.0f * SafePixels.Top / FMath::Max(1, Canvas->CachedDisplayHeight),
            2.0f * SafePixels.Right / FMath::Max(1, Canvas->CachedDisplayWidth),
            2.0f * SafePixels.Bottom / FMath::Max(1, Canvas->CachedDisplayHeight)));
        Canvas->UpdateSafeZoneData();
        Canvas->ApplySafeZoneTransform();
    };

    const auto DrawAppliedCanvas = [&](const TCHAR* Label, const FMargin& SafePixels)
    {
        ApplySafeZone(SafePixels);
        const FCanvasState Applied = CanvasState(*Canvas);
        HUD.DrawHUD();
        const FCanvasState Restored = CanvasState(*Canvas);
        TestTrue(*FString::Printf(TEXT("%s draw restores the caller's applied canvas size, clip and transform"), Label),
            SameCanvasState(Applied, Restored));
        TestEqual(*FString::Printf(TEXT("%s draw uses full viewport width"), Label), HUD.Width, static_cast<float>(ViewWidth));
        TestEqual(*FString::Printf(TEXT("%s draw uses full viewport height"), Label), HUD.Height, static_cast<float>(ViewHeight));
        Canvas->PopSafeZoneTransform();
    };

    DrawAppliedCanvas(TEXT("Zero-pad gameplay"), FMargin());

    const FMargin AndroidSafePixels(72, 34, 0, 0);
    ApplySafeZone(AndroidSafePixels);
    const float ActualLeft = Canvas->SafeZonePadX;
    const float ActualTop = Canvas->SafeZonePadY;
    const FCanvasState Applied = CanvasState(*Canvas);
    TestTrue(TEXT("Synthetic Android canvas has a real left/top safe translation"),
        ActualLeft > 0 && ActualTop > 0
        && !RenderCanvas.GetTransform().Equals(FMatrix::Identity, KINDA_SMALL_NUMBER));
    HUD.DrawHUD();
    TestTrue(TEXT("Asymmetric Android draw restores the caller's applied canvas state"),
        SameCanvasState(Applied, CanvasState(*Canvas)));
    TestEqual(TEXT("Asymmetric Android draw keeps the full viewport width"), HUD.Width, static_cast<float>(ViewWidth));
    TestEqual(TEXT("Asymmetric Android draw keeps the full viewport height"), HUD.Height, static_cast<float>(ViewHeight));

    FVector4 ExplicitInsets = FVector4::Zero();
#if PLATFORM_IOS || PLATFORM_ANDROID
    FDisplayMetrics Metrics;
    Slate.GetCachedDisplayMetrics(Metrics);
    const float ScaleX = static_cast<float>(ViewWidth) / FMath::Max(1, Metrics.PrimaryDisplayWidth);
    const float ScaleY = static_cast<float>(ViewHeight) / FMath::Max(1, Metrics.PrimaryDisplayHeight);
    ExplicitInsets = FVector4(Metrics.TitleSafePaddingSize.X * ScaleX, Metrics.TitleSafePaddingSize.Y * ScaleY,
        Metrics.TitleSafePaddingSize.Z * ScaleX, Metrics.TitleSafePaddingSize.W * ScaleY);
#endif
    const FCinderMobileHUDLayout ExpectedLayout = FCinderMobileHUDLayout::Make(
        FVector2D(ViewWidth, ViewHeight), ExplicitInsets, false);
    TestTrue(TEXT("Production layout keeps its explicit safe-inset policy after removing the engine transform"),
        FMath::IsNearlyEqual(HUD.MobileLayout.Left, ExpectedLayout.Left)
        && FMath::IsNearlyEqual(HUD.MobileLayout.Top, ExpectedLayout.Top));

    const float Scale = ExpectedLayout.Scale;
    const FVector2D TrainCenter(
        ExpectedLayout.GlobalActions.Min.X
            + (FCinderMobileHUDLayout::GlobalBuildWidth
                + FCinderMobileHUDLayout::GlobalActionGap
                + FCinderMobileHUDLayout::GlobalTrainWidth * 0.5f) * Scale,
        ExpectedLayout.GlobalActions.Min.Y
            + FCinderMobileHUDLayout::GlobalActionHeight * 0.5f * Scale);
    TestTrue(TEXT("Independently calculated rendered TRAIN center is HUD UI"), HUD.ContainsUI(TrainCenter));
    const FCinderTutorialGuide BeforeGuide = HUD.CurrentTutorialGuide();
    TestTrue(TEXT("Guidance begins at Open TRAIN"),
        BeforeGuide.Target == ECinderTutorialGuideTarget::Button
        && BeforeGuide.ButtonAction == TEXT("globalcatalog") && BeforeGuide.ButtonArgument == 8);

    const std::vector<cinder::Id> SelectionBefore = Controller.Selected;
    const std::size_t RecordingBefore = Sim.recording().size();
    const cinder::Entity* WorkerBefore = Sim.find(SelectionBefore.front());
    if (!TestNotNull(TEXT("Selected Drudge exists before TRAIN tap"), WorkerBefore)) return false;
    const cinder::Order WorkerOrderBefore = WorkerBefore->order;
    const cinder::Vec2 WorkerGoalBefore = WorkerBefore->goal;
    const cinder::Id WorkerTargetBefore = WorkerBefore->target;
    const cinder::Id WorkerResourceBefore = WorkerBefore->resourceTarget;

    Controller.PointerPressed(TrainCenter, true, false);
    Controller.PointerReleased(TrainCenter);
    TestEqual(TEXT("Raw touch opens global TRAIN catalog 8"), HUD.CompactSheet, 8);
    TestTrue(TEXT("Raw TRAIN touch preserves friendly selection"), Controller.Selected == SelectionBefore);
    TestTrue(TEXT("Raw TRAIN touch issues no simulation command"), Sim.recording().size() == RecordingBefore);
    const cinder::Entity* WorkerAfter = Sim.find(SelectionBefore.front());
    TestTrue(TEXT("Raw TRAIN touch leaves the Drudge order unchanged"), WorkerAfter
        && WorkerAfter->order == WorkerOrderBefore
        && WorkerAfter->goal.x == WorkerGoalBefore.x && WorkerAfter->goal.y == WorkerGoalBefore.y
        && WorkerAfter->target == WorkerTargetBefore && WorkerAfter->resourceTarget == WorkerResourceBefore);
    const FCinderTutorialGuide AfterGuide = HUD.CurrentTutorialGuide();
    TestTrue(TEXT("Open TRAIN guidance advances to the Drudge portrait"),
        AfterGuide.Target == ECinderTutorialGuideTarget::Button
        && AfterGuide.ButtonAction == TEXT("trainkind")
        && AfterGuide.ButtonArgument == static_cast<int32>(cinder::Kind::Worker));

    // Field Guide has moved behind the compact Menu icon. Exercise that path
    // with the same raw touch transform as the catalog target above.
    HUD.DrawHUD();
    const FVector2D MenuCenter(ExpectedLayout.Right
        - FCinderMobileHUDLayout::NavigationMenuWidth * 0.5f * Scale,
        ExpectedLayout.Navigation.Min.Y
        + FCinderMobileHUDLayout::NavigationHeight * 0.5f * Scale);
    Controller.PointerPressed(MenuCenter, true, false);
    Controller.PointerReleased(MenuCenter);
    TestTrue(TEXT("Raw MENU icon touch opens the pause menu"), Fixture.Battle->IsPaused());
    HUD.DrawHUD();
    FVector2D GuideCenter = FVector2D::ZeroVector;
    bool bFoundGuide = false;
    for (const ACinderHUD::FButton& Button : HUD.Buttons)
        if (Button.Action == TEXT("help"))
        {
            GuideCenter = Button.Bounds.GetCenter();
            bFoundGuide = true;
            break;
        }
    TestTrue(TEXT("Paused compact menu exposes the Field Guide"), bFoundGuide);
    if (bFoundGuide)
    {
        Controller.PointerPressed(GuideCenter, true, false);
        Controller.PointerReleased(GuideCenter);
        TestTrue(TEXT("Field Guide opens from the compact menu"), Controller.IsHelpOpen());
    }
    Canvas->PopSafeZoneTransform();

    DrawAppliedCanvas(TEXT("Asymmetric help overlay"), AndroidSafePixels);
    Controller.bHelpOpen = false;

    return !HasAnyErrors();
}

#endif
