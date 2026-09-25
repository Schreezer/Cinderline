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
#include "UnrealClient.h"

namespace
{
constexpr int32 CampaignViewWidth = 956;
constexpr int32 CampaignViewHeight = 440;

class FCampaignHUDRenderTarget final : public FRenderTarget
{
public:
    virtual FIntPoint GetSizeXY() const override
    {
        return FIntPoint(CampaignViewWidth, CampaignViewHeight);
    }
};

struct FCampaignHUDFixture
{
    FTestWorldWrapper WorldOwner;
    ACinderBattlefield* Battle = nullptr;
    ACinderPlayerController* Controller = nullptr;
    ACinderHUD* HUD = nullptr;
    ACinderCamera* Camera = nullptr;
    UGameViewportClient* ViewportClient = nullptr;
    ULocalPlayer* LocalPlayer = nullptr;

    ~FCampaignHUDFixture()
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
        if (!Test.TestNotNull(TEXT("Campaign HUD world exists"), World)) return false;
        World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
        Battle = World->SpawnActor<ACinderBattlefield>();
        if (!Test.TestNotNull(TEXT("Campaign HUD battlefield spawned"), Battle)) return false;
        if (!WorldOwner.BeginPlayInTestWorld())
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        Controller = World->SpawnActor<ACinderPlayerController>();
        if (!Test.TestNotNull(TEXT("Campaign HUD controller spawned"), Controller)) return false;

        ViewportClient = NewObject<UGameViewportClient>(GEngine);
        LocalPlayer = NewObject<ULocalPlayer>(GEngine);
        if (!Test.TestNotNull(TEXT("Campaign HUD viewport client created"), ViewportClient)
            || !Test.TestNotNull(TEXT("Campaign HUD local player created"), LocalPlayer)) return false;
        TSharedRef<FSceneViewport> Viewport = FSceneViewport::Create(ViewportClient, nullptr);
        Viewport->SetInitialSize(FIntPoint(CampaignViewWidth, CampaignViewHeight));
        Controller->SetPlayer(LocalPlayer);
        LocalPlayer->PlayerAdded(ViewportClient, 0);
        LocalPlayer->Origin = FVector2D::ZeroVector;
        LocalPlayer->Size = FVector2D(1, 1);
        Controller->ClientSetHUD(ACinderHUD::StaticClass());
        HUD = Controller->GetHUD<ACinderHUD>();
        if (!Test.TestNotNull(TEXT("Campaign uses the production HUD"), HUD)) return false;

        Camera = World->SpawnActor<ACinderCamera>();
        if (!Test.TestNotNull(TEXT("Campaign HUD camera spawned"), Camera)) return false;
        Controller->Possess(Camera);
        Controller->SetViewTarget(Camera);
        Camera->Focus(FVector(700, 700, 0), true);
        if (!WorldOwner.TickTestWorld(cinder::Simulation::Step))
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        if (!Test.TestNotNull(TEXT("Campaign HUD camera manager initialized"),
            Controller->PlayerCameraManager.Get())) return false;
        Controller->PlayerCameraManager->UpdateCamera(cinder::Simulation::Step);
        return Test.TestTrue(TEXT("Campaign controller resolves the production battlefield"),
            Controller->Battlefield() == Battle);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCampaignHUDIntegration,
    "Cinderline.Integration.CampaignHUDTouch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCampaignHUDIntegration::RunTest(const FString& Parameters)
{
    (void)Parameters;
    if (!TestTrue(TEXT("Slate is initialized for campaign safe-zone input regression"),
        FSlateApplication::IsInitialized())) return false;
    FSlateApplication& Slate = FSlateApplication::Get();
    const bool bHadCustomSafeZone = Slate.IsCustomSafeZoneSet();
    const FMargin PreviousSafeZone = Slate.GetCustomSafeZone();
    ON_SCOPE_EXIT
    {
        if (bHadCustomSafeZone) Slate.SetCustomSafeZone(PreviousSafeZone);
        else Slate.ResetCustomSafeZone();
    };

    FCampaignHUDFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderBattlefield& Battle = *Fixture.Battle;
    ACinderPlayerController& Controller = *Fixture.Controller;
    ACinderHUD& HUD = *Fixture.HUD;
    HUD.UnitPortraits.Reset();
    HUD.MenuBackdrop = nullptr;

    FCampaignHUDRenderTarget RenderTarget;
    FCanvas RenderCanvas(&RenderTarget, nullptr, Fixture.WorldOwner.GetTestWorld(),
        GMaxRHIFeatureLevel, FCanvas::CDM_DeferDrawing);
    RenderCanvas.SetAllowedModes(0);
    UCanvas* Canvas = NewObject<UCanvas>();
    if (!TestNotNull(TEXT("Campaign HUD uses an actual UCanvas"), Canvas)) return false;
    HUD.Canvas = Canvas;

    Slate.SetCustomSafeZone(FMargin());
    Canvas->Init(CampaignViewWidth, CampaignViewHeight, nullptr, &RenderCanvas);
    Canvas->UpdateSafeZoneData();
    const FMargin AndroidSafePixels(72, 34, 0, 0);
    Slate.SetCustomSafeZone(FMargin(
        2.0f * AndroidSafePixels.Left / FMath::Max(1, Canvas->CachedDisplayWidth),
        2.0f * AndroidSafePixels.Top / FMath::Max(1, Canvas->CachedDisplayHeight),
        0, 0));

    const auto Draw = [&]()
    {
        Canvas->Init(CampaignViewWidth, CampaignViewHeight, nullptr, &RenderCanvas);
        Canvas->UpdateSafeZoneData();
        Canvas->ApplySafeZoneTransform();
        HUD.DrawHUD();
        Canvas->PopSafeZoneTransform();
    };
    const auto FindButtonCenter = [&](const FString& Action, int32 Argument, FVector2D& Out)
    {
        for (int32 Index = HUD.Buttons.Num() - 1; Index >= 0; --Index)
        {
            const ACinderHUD::FButton& Button = HUD.Buttons[Index];
            if (Button.Action == Action && (Argument == INDEX_NONE || Button.Argument == Argument))
            {
                Out = Button.Bounds.GetCenter();
                return true;
            }
        }
        return false;
    };
    const auto RawTouch = [&](FVector2D Point)
    {
        Controller.PointerPressed(Point, true, false);
        Controller.PointerReleased(Point);
    };

    Controller.ExecuteAction(TEXT("campaignmenu"));
    Draw();
    TestTrue(TEXT("Campaign touch fixture uses a real asymmetric safe-zone translation"),
        Canvas->SafeZonePadX > 0 && Canvas->SafeZonePadY > 0);
    TestTrue(TEXT("Campaign selection opens in compact layout"),
        Controller.IsCampaignMenuOpen() && Battle.IsMenu() && HUD.bCompactLayout);
    FVector2D MissionTile;
    if (!TestTrue(TEXT("Mission three has a real selection tile"),
        FindButtonCenter(TEXT("campaignselect"), 2, MissionTile))) return false;
    RawTouch(MissionTile);
    TestEqual(TEXT("Raw mission-tile touch changes the selected briefing"),
        Controller.SelectedCampaignMission(), 2);

    Draw();
    FVector2D StartButton;
    if (!TestTrue(TEXT("Selected briefing has a real start button"),
        FindButtonCenter(TEXT("campaignstart"), 2, StartButton))) return false;
    RawTouch(StartButton);
    TestTrue(TEXT("Raw briefing touch starts the selected production campaign mission"),
        !Battle.IsMenu() && Battle.Campaign().IsRunning() && Battle.Campaign().MissionIndex() == 2);

    Draw();
    TestTrue(TEXT("The first coaching objective draws a compact card"), HUD.TutorialCardBounds.bIsValid);
    FVector2D HintButton;
    if (!TestTrue(TEXT("An unassisted coaching phase exposes HINT"),
        FindButtonCenter(TEXT("campaignhint"), INDEX_NONE, HintButton))) return false;
    TestFalse(TEXT("The coaching phase begins unassisted"), Battle.Campaign().IsCurrentPhaseAssisted());
    TestTrue(TEXT("An optional HINT has no compulsory arrow or target ring"),
        !HUD.bTutorialTargetVisible && !HUD.TutorialTargetBounds.bIsValid);

    const std::size_t EmptyTapRecording = Battle.Sim().recording().size();
    const int32 EmptyTapPhase = Battle.Campaign().Phase();
    const uint64 EmptyTapAssistance = Battle.Campaign().ExportState().AssistanceMask;
    FVector2D EmptyCardPoint = HUD.TutorialCardBounds.Min + FVector2D(9, 9);
    bool bFoundEmptyCardPoint = false;
    for (float Y = HUD.TutorialCardBounds.Min.Y + 5; Y < HUD.TutorialCardBounds.Max.Y - 5 && !bFoundEmptyCardPoint; Y += 6)
        for (float X = HUD.TutorialCardBounds.Min.X + 5; X < HUD.TutorialCardBounds.Max.X - 5; X += 6)
        {
            const FVector2D Candidate(X, Y);
            bool bButton = false;
            for (const ACinderHUD::FButton& Button : HUD.Buttons)
                if (Button.Bounds.IsInside(Candidate)) { bButton = true; break; }
            if (!bButton) { EmptyCardPoint = Candidate; bFoundEmptyCardPoint = true; break; }
        }
    if (!TestTrue(TEXT("Objective card retains non-button body space"), bFoundEmptyCardPoint)) return false;
    HUD.Minimap = FBox2D(EmptyCardPoint - FVector2D(4), EmptyCardPoint + FVector2D(4));
    TestTrue(TEXT("Regression fixture forces a minimap region below the card body"),
        HUD.TutorialCardBounds.Intersect(HUD.Minimap));
    const FVector EmptyTapCamera = Fixture.Camera->GetActorLocation();
    RawTouch(EmptyCardPoint);
    if (!Fixture.WorldOwner.TickTestWorld(0.25f))
    {
        Fixture.WorldOwner.ForwardErrorMessages(this);
        return false;
    }
    TestTrue(TEXT("Touching card body dispatches no hidden gameplay button or world command"),
        Battle.Sim().recording().size() == EmptyTapRecording
        && Battle.Campaign().Phase() == EmptyTapPhase
        && Battle.Campaign().ExportState().AssistanceMask == EmptyTapAssistance
        && Fixture.Camera->GetActorLocation().Equals(EmptyTapCamera, 0.1f));
    for (const ACinderHUD::FButton& Button : HUD.Buttons)
        if (HUD.TutorialCardBounds.Intersect(Button.Bounds))
            TestTrue(TEXT("Only the card's own action may overlap its opaque bounds"),
                Button.Action == TEXT("campaignhint") || Button.Action == TEXT("campaignshow"));

    RawTouch(HintButton);
    TestTrue(TEXT("Raw HINT touch marks exactly the current coaching phase assisted"),
        Battle.Campaign().IsCurrentPhaseAssisted()
        && Battle.Campaign().ExportState().AssistanceMask == (uint64{1} << Battle.Campaign().Phase()));
    TestTrue(TEXT("HINT never issues a simulation command"),
        Battle.Sim().recording().size() == EmptyTapRecording);

    Draw();
    const FCinderTutorialGuide RevealedGuide = HUD.CurrentTutorialGuide();
    TestTrue(TEXT("HINT reveals one exact production target"),
        RevealedGuide.Target == ECinderTutorialGuideTarget::Button && HUD.bTutorialTargetVisible);
    TestTrue(TEXT("Active pointer target never intersects the objective card"),
        HUD.TutorialTargetBounds.bIsValid
        && !HUD.TutorialCardBounds.Intersect(HUD.TutorialTargetBounds));

    Controller.ExecuteAction(TEXT("campaignexit"));
    Controller.ExecuteAction(TEXT("campaignstart"), 4);
    TestTrue(TEXT("Break the Siege starts through the production campaign action"),
        Battle.Campaign().IsRunning() && Battle.Campaign().MissionIndex() == 4);
    Controller.ExecuteAction(TEXT("campaignhint"));
    Draw();
    HUD.DrawCampaignCard(&Battle, true);
    FVector2D ShowButton;
    if (!TestTrue(TEXT("A forced offscreen authored target exposes SHOW"),
        FindButtonCenter(TEXT("campaignshow"), INDEX_NONE, ShowButton))) return false;
    const FVector CameraBefore = Fixture.Camera->GetActorLocation();
    const std::size_t ShowRecording = Battle.Sim().recording().size();
    const int32 ShowPhase = Battle.Campaign().Phase();
    const uint64 ShowAssistance = Battle.Campaign().ExportState().AssistanceMask;
    RawTouch(ShowButton);
    Fixture.WorldOwner.TickTestWorld(0.25f);
    const FVector CameraAfter = Fixture.Camera->GetActorLocation();
    TestTrue(TEXT("SHOW focuses the authored area without issuing or advancing gameplay"),
        !CameraAfter.Equals(CameraBefore, 0.1f)
        && Battle.Sim().recording().size() == ShowRecording
        && Battle.Campaign().Phase() == ShowPhase
        && Battle.Campaign().ExportState().AssistanceMask == ShowAssistance);
    return !HasAnyErrors();
}

#endif
