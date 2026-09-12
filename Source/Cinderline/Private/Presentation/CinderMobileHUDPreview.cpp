#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderHUD.h"
#include "Presentation/CinderPlayerController.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderMobileHUDPreview, Log, All);

namespace
{
constexpr std::uint32_t MobilePreviewSeed = 0x4D4844;
TWeakObjectPtr<UWorld> MobilePreviewWorld;
FTimerHandle MobilePreviewStepTimer;
FTimerHandle MobilePreviewCaptureTimer;
int32 MobilePreviewSweepIndex = 0;
const TCHAR* MobilePreviewStates[] = {
    TEXT("idle"), TEXT("worker"), TEXT("build"), TEXT("placement"), TEXT("producer"),
    TEXT("queue"), TEXT("army"), TEXT("types"), TEXT("training")
};
constexpr int32 MobilePreviewStateCount = UE_ARRAY_COUNT(MobilePreviewStates);

ACinderHUD* FindMobileHUD(UWorld* World)
{
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    return PC ? Cast<ACinderHUD>(PC->GetHUD()) : nullptr;
}

void ClearMobilePreviewTimers()
{
    if (UWorld* World = MobilePreviewWorld.Get())
    {
        World->GetTimerManager().ClearTimer(MobilePreviewStepTimer);
        World->GetTimerManager().ClearTimer(MobilePreviewCaptureTimer);
    }
    MobilePreviewWorld.Reset();
    MobilePreviewSweepIndex = 0;
}

void CaptureMobilePreview(UWorld* World, const FString& State)
{
    ACinderHUD* HUD = FindMobileHUD(World);
    if (!HUD) return;
    HUD->LogMobileLayout();
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("MobileHUD");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = Directory / (State + TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(Filename, false, false);
    UE_LOG(LogCinderMobileHUDPreview, Display,
        TEXT("CINDERLINE_MOBILE_HUD_CAPTURE state=%s file=%s; scripted fixture, not a native gesture test"),
        *State, *Filename);
}

void ScheduleMobilePreviewCapture(UWorld* World, const FString& State)
{
    if (!World) return;
    const TWeakObjectPtr<UWorld> WeakWorld = World;
    World->GetTimerManager().SetTimer(MobilePreviewCaptureTimer,
        FTimerDelegate::CreateLambda([WeakWorld, State]
        {
            if (UWorld* CurrentWorld = WeakWorld.Get()) CaptureMobilePreview(CurrentWorld, State);
        }), 2.0f, false);
}

void StageMobilePreviewSweepState(UWorld* World)
{
    ACinderHUD* HUD = FindMobileHUD(World);
    if (!HUD || MobilePreviewSweepIndex >= MobilePreviewStateCount) return;
    const FString State(MobilePreviewStates[MobilePreviewSweepIndex]);
    HUD->PreviewMobileLayout(State);
    ScheduleMobilePreviewCapture(World, State);
}

void BeginMobilePreviewSweep(UWorld* World)
{
    ClearMobilePreviewTimers();
    ACinderHUD* HUD = FindMobileHUD(World);
    if (!World || !HUD) return;
    ACinderPlayerController* PC = Cast<ACinderPlayerController>(World->GetFirstPlayerController());
    ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
    if (Battle && Battle->IsOnlineMatch())
    {
        UE_LOG(LogCinderMobileHUDPreview, Warning,
            TEXT("CINDERLINE_MOBILE_HUD_PREVIEW refused=online_match; leave the online match before replacing local state"));
        return;
    }
    MobilePreviewWorld = World;
    MobilePreviewSweepIndex = 0;
    StageMobilePreviewSweepState(World);
    const TWeakObjectPtr<UWorld> WeakWorld = World;
    World->GetTimerManager().SetTimer(MobilePreviewStepTimer,
        FTimerDelegate::CreateLambda([WeakWorld]
        {
            UWorld* CurrentWorld = WeakWorld.Get();
            if (!CurrentWorld || ++MobilePreviewSweepIndex >= MobilePreviewStateCount)
            {
                ClearMobilePreviewTimers();
                return;
            }
            StageMobilePreviewSweepState(CurrentWorld);
        }), 3.0f, true);
    UE_LOG(LogCinderMobileHUDPreview, Display,
        TEXT("CINDERLINE_MOBILE_HUD_SWEEP states=%d interval=3.0 capture_delay=2.0 output=%s; scripted fixture, not a native gesture test"),
        MobilePreviewStateCount, *(FPaths::ProjectSavedDir() / TEXT("MobileHUD")));
}

FAutoConsoleCommandWithWorldAndArgs MobilePreviewCommand(
    TEXT("cinder.mobilepreview"),
    TEXT("DEVELOPMENT: replaces the current unsaved local match with a frozen mobile HUD fixture: idle|worker|build|placement|producer|queue|army|types|training|reset|sweep. Never writes a save."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World) return;
        const FString State = Args.IsEmpty() ? TEXT("idle") : Args[0].ToLower();
        if (State == TEXT("sweep")) { BeginMobilePreviewSweep(World); return; }
        ClearMobilePreviewTimers();
        if (ACinderHUD* HUD = FindMobileHUD(World)) HUD->PreviewMobileLayout(State);
    }));

FAutoConsoleCommandWithWorld MobileHUDReportCommand(
    TEXT("cinder.mobilehudreport"),
    TEXT("DEVELOPMENT: logs the current HUD dimensions, scale, selection, buttons, regions, and center hit checks."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (ACinderHUD* HUD = FindMobileHUD(World)) HUD->LogMobileLayout();
    }));
}

void ACinderHUD::PreviewMobileLayout(const FString& RequestedState)
{
    ACinderPlayerController* PC = Cast<ACinderPlayerController>(PlayerOwner);
    ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
    if (!PC || !Battle)
    {
        UE_LOG(LogCinderMobileHUDPreview, Warning, TEXT("CINDERLINE_MOBILE_HUD_PREVIEW refused=no_local_hud_or_battlefield"));
        return;
    }
    if (Battle->IsOnlineMatch())
    {
        UE_LOG(LogCinderMobileHUDPreview, Warning,
            TEXT("CINDERLINE_MOBILE_HUD_PREVIEW refused=online_match; leave the online match before replacing local state"));
        PC->Notify(TEXT("Mobile HUD preview refused during an online match."));
        return;
    }

    const FString State = RequestedState.ToLower();
    const bool bKnownState = State == TEXT("idle") || State == TEXT("worker") || State == TEXT("build")
        || State == TEXT("placement") || State == TEXT("producer") || State == TEXT("queue")
        || State == TEXT("army") || State == TEXT("types") || State == TEXT("training")
        || State == TEXT("reset");
    if (!bKnownState)
    {
        UE_LOG(LogCinderMobileHUDPreview, Warning,
            TEXT("CINDERLINE_MOBILE_HUD_PREVIEW refused=unknown_state value=%s expected=idle,worker,build,placement,producer,queue,army,types,training,reset,sweep"),
            *State);
        return;
    }

    CompactSheet = 0;
    CompactSelectionId = 0;
    PC->bBuildMenu = false;
    Battle->SetActorTickEnabled(true);
    if (State == TEXT("reset"))
    {
        PC->ExecuteAction(TEXT("start"), 0);
        PC->ExecuteAction(TEXT("menu"));
        UE_LOG(LogCinderMobileHUDPreview, Display,
            TEXT("CINDERLINE_MOBILE_HUD_PREVIEW reset=fresh_menu; no save written"));
        return;
    }

    if (State == TEXT("training"))
    {
        PC->ExecuteAction(TEXT("start"), 0);
        PC->ExecuteAction(TEXT("menu"));
        PC->ExecuteAction(TEXT("tutorial"));
        Battle->RenderState();
        if (PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0);
        Battle->SetActorTickEnabled(false);
        PC->Notify(TEXT("MOBILE HUD PREVIEW: TRAINING | development fixture"));
        UE_LOG(LogCinderMobileHUDPreview, Display,
            TEXT("CINDERLINE_MOBILE_HUD_PREVIEW state=training frozen=1 seed=%u; scripted fixture, no save written, not a native gesture test"),
            Battle->Sim().config().seed);
        return;
    }

    PC->ExecuteAction(TEXT("start"), 0);
    cinder::Simulation& Sim = Battle->Sim();
    cinder::Config Config;
    Config.map = 0; Config.ai = false; Config.seed = MobilePreviewSeed;
    Sim.reset(Config);
    Sim.debugResources(0, 10000);
    Battle->ResetPresentation();

    const bool bNeedsProducer = State == TEXT("producer") || State == TEXT("queue");
    const bool bNeedsArmy = State == TEXT("army") || State == TEXT("types");
    if (bNeedsProducer) Sim.debugSpawn(cinder::Kind::Foundry, 0, {900, 850});
    if (bNeedsArmy)
    {
        const cinder::Kind Roster[] = {cinder::Kind::Striker, cinder::Kind::Lancer, cinder::Kind::Scout,
            cinder::Kind::Bastion, cinder::Kind::Mortar, cinder::Kind::Mender, cinder::Kind::Kite};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Roster); ++Index)
            Sim.debugSpawn(Roster[Index], 0, {780.0f + (Index % 4) * 90.0f, 850.0f + (Index / 4) * 95.0f});
    }

    if (ACinderCamera* Rig = Cast<ACinderCamera>(PC->GetPawn()))
    {
        Rig->Zoom(1450.0f - Rig->Distance());
        Rig->Focus(FVector(760, 760, 0), true);
    }
    if (PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0);
    Battle->RenderState();

    if (State == TEXT("worker") || State == TEXT("build") || State == TEXT("placement"))
        PC->SelectKind(cinder::Kind::Worker);
    else if (bNeedsProducer)
        PC->SelectKind(cinder::Kind::Foundry);
    else if (bNeedsArmy)
        PC->ExecuteAction(TEXT("army"));

    if (State == TEXT("build")) PC->bBuildMenu = true;
    else if (State == TEXT("placement"))
    {
        PC->bBuildMenu = true;
        PC->ExecuteAction(TEXT("build"), static_cast<int32>(cinder::Kind::Foundry));
    }
    else if (State == TEXT("producer")) CompactSheet = 1;
    else if (State == TEXT("queue"))
    {
        PC->ExecuteAction(TEXT("train"), static_cast<int32>(cinder::Kind::Striker));
        PC->ExecuteAction(TEXT("train"), static_cast<int32>(cinder::Kind::Scout));
        CompactSheet = 3;
    }
    else if (State == TEXT("types")) CompactSheet = 2;

    CompactSelectionId = PC->Selection().empty() ? 0 : PC->Selection().front();
    Battle->Tick(cinder::Simulation::Step);
    Battle->RenderState();
    if (PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0);
    Battle->SetActorTickEnabled(false);
    PC->Notify(FString::Printf(TEXT("MOBILE HUD PREVIEW: %s | development fixture"), *State.ToUpper()));
    const cinder::Entity* QueueProducer = State == TEXT("queue") && !PC->Selection().empty()
        ? Sim.find(PC->Selection().front()) : nullptr;
    UE_LOG(LogCinderMobileHUDPreview, Display,
        TEXT("CINDERLINE_MOBILE_HUD_PREVIEW state=%s frozen=1 seed=%u selection=%d sheet=%d build_menu=%d placement=%d queue_items=%d; scripted fixture, no save written, not a native gesture test"),
        *State, MobilePreviewSeed, static_cast<int32>(PC->Selection().size()), CompactSheet, PC->bBuildMenu,
        PC->IsBuildMode(), QueueProducer ? static_cast<int32>(QueueProducer->queue.size()) : 0);
}

void ACinderHUD::LogMobileLayout() const
{
    const ACinderPlayerController* PC = Cast<ACinderPlayerController>(PlayerOwner);
    const ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
    FString Selection = TEXT("none");
    if (PC && Battle && !PC->Selection().empty())
    {
        TArray<FString> Items;
        for (cinder::Id Id : PC->Selection())
        {
            const cinder::Entity* Entity = Battle->Sim().find(Id);
            Items.Add(Entity
                ? FString::Printf(TEXT("%u:%s"), Id, UTF8_TO_TCHAR(cinder::definition(Entity->kind).name))
                : FString::Printf(TEXT("%u:missing"), Id));
        }
        Selection = FString::Join(Items, TEXT(","));
    }
    const float DockTop = MobileLayout.Navigation.bIsValid ? MobileLayout.Navigation.Min.Y : Height - 56 * UIScale;
    const FVector2D BottomCenter(Width * 0.5f, DockTop + 22 * UIScale);
    const FVector2D AboveDockCenter(Width * 0.5f, FMath::Max(0.0f, DockTop - 18 * UIScale));
    const bool bBottomContainsUI = ContainsUI(BottomCenter);
    const bool bAboveContainsUI = ContainsUI(AboveDockCenter);
    UE_LOG(LogCinderMobileHUDPreview, Display,
        TEXT("CINDERLINE_MOBILE_HUD_REPORT width=%.1f height=%.1f scale=%.4f compact=%d sheet=%d build_menu=%d placement=%d selection=[%s] buttons=%d regions=%d bottom_center=(%.1f,%.1f) contains_ui=%d clear=%d above_dock_center=(%.1f,%.1f) contains_ui=%d clear=%d"),
        Width, Height, UIScale, bCompactLayout, CompactSheet, PC ? PC->bBuildMenu : 0,
        PC ? PC->IsBuildMode() : 0, *Selection, Buttons.Num(), UIRegions.Num(),
        BottomCenter.X, BottomCenter.Y, bBottomContainsUI, !bBottomContainsUI,
        AboveDockCenter.X, AboveDockCenter.Y, bAboveContainsUI, !bAboveContainsUI);
    for (int32 Index = 0; Index < Buttons.Num(); ++Index)
    {
        const FButton& Button = Buttons[Index];
        UE_LOG(LogCinderMobileHUDPreview, Display,
            TEXT("CINDERLINE_MOBILE_HUD_BUTTON index=%d action=%s argument=%d bounds=(%.1f,%.1f)-(%.1f,%.1f)"),
            Index, *Button.Action, Button.Argument, Button.Bounds.Min.X, Button.Bounds.Min.Y,
            Button.Bounds.Max.X, Button.Bounds.Max.Y);
    }
    for (int32 Index = 0; Index < UIRegions.Num(); ++Index)
    {
        const FBox2D& Region = UIRegions[Index];
        UE_LOG(LogCinderMobileHUDPreview, Display,
            TEXT("CINDERLINE_MOBILE_HUD_REGION index=%d bounds=(%.1f,%.1f)-(%.1f,%.1f)"),
            Index, Region.Min.X, Region.Min.Y, Region.Max.X, Region.Max.Y);
    }
}

#endif
