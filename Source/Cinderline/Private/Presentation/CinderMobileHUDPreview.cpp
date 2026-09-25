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
    TEXT("queue"), TEXT("army"), TEXT("types"), TEXT("tactical-armed"),
    TEXT("tactical-pending"), TEXT("patrol"), TEXT("patrol-pending"),
    TEXT("escort"), TEXT("escort-pending"), TEXT("formation-tight"),
    TEXT("formation-standard"), TEXT("formation-wide"), TEXT("formation-facing"),
    TEXT("formation-pending"), TEXT("formation-accepted"), TEXT("training")
};
constexpr int32 MobilePreviewStateCount = UE_ARRAY_COUNT(MobilePreviewStates);

ACinderHUD* FindMobileHUD(UWorld* World)
{
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    return PC ? Cast<ACinderHUD>(PC->GetHUD()) : nullptr;
}

bool IsTacticalPreviewState(const FString& State)
{
    return State == TEXT("tactical-armed") || State == TEXT("tactical-pending");
}

bool IsSustainedPreviewState(const FString& State)
{
    return State == TEXT("patrol") || State == TEXT("patrol-pending")
        || State == TEXT("escort") || State == TEXT("escort-pending");
}

bool IsFormationPreviewState(const FString& State)
{
    return State.StartsWith(TEXT("formation-"));
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
    // Keep fixture notices out of screenshots so the captures show the actual
    // gameplay HUD and its available battlefield space.
    if (ACinderPlayerController* PC = Cast<ACinderPlayerController>(World->GetFirstPlayerController()))
        PC->Notify(FString());
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
    TEXT("DEVELOPMENT: replaces the current unsaved local match with a frozen HUD fixture. Formation states include formation-tight|formation-standard|formation-wide|formation-facing|formation-pending|formation-accepted. Add shot to capture after two seconds. Never writes a save."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World) return;
        const FString State = Args.IsEmpty() ? TEXT("idle") : Args[0].ToLower();
        if (State == TEXT("sweep")) { BeginMobilePreviewSweep(World); return; }
        ClearMobilePreviewTimers();
        if (ACinderHUD* HUD = FindMobileHUD(World))
        {
            HUD->PreviewMobileLayout(State);
            if (Args.Contains(TEXT("shot"))) ScheduleMobilePreviewCapture(World, State);
        }
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
        || State == TEXT("army") || State == TEXT("types") || State == TEXT("tactical-armed")
        || State == TEXT("tactical-pending") || IsSustainedPreviewState(State) || State == TEXT("training")
        || IsFormationPreviewState(State)
        || State == TEXT("reset");
    if (!bKnownState)
    {
        UE_LOG(LogCinderMobileHUDPreview, Warning,
            TEXT("CINDERLINE_MOBILE_HUD_PREVIEW refused=unknown_state value=%s"),
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
    const bool bNeedsTacticalQueue = IsTacticalPreviewState(State);
    const bool bNeedsSustained = IsSustainedPreviewState(State);
    const bool bNeedsFormation = IsFormationPreviewState(State);
    const bool bEscortPreview = State.StartsWith(TEXT("escort"));
    const bool bPendingSustained = State.EndsWith(TEXT("pending"));
    cinder::Id TacticalPrimary = 0, TacticalSecondary = 0, SustainedLeader = 0;
    if (bNeedsProducer) Sim.debugSpawn(cinder::Kind::Foundry, 0, {900, 850});
    if (bNeedsArmy)
    {
        const cinder::Kind Roster[] = {cinder::Kind::Striker, cinder::Kind::Lancer, cinder::Kind::Scout,
            cinder::Kind::Bastion, cinder::Kind::Mortar, cinder::Kind::Mender, cinder::Kind::Kite};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Roster); ++Index)
            Sim.debugSpawn(Roster[Index], 0, {780.0f + (Index % 4) * 90.0f, 850.0f + (Index / 4) * 95.0f});
    }
    if (bNeedsTacticalQueue)
    {
        TacticalPrimary = Sim.debugSpawn(cinder::Kind::Striker, 0, {820, 820});
        TacticalSecondary = Sim.debugSpawn(cinder::Kind::Striker, 0, {890, 820});
        Sim.update(cinder::Simulation::Step);
        const auto IssueTactical = [&](cinder::CommandType Type, cinder::Vec2 Point,
            cinder::CommandQueueMode QueueMode, std::vector<cinder::Id> Units)
        {
            cinder::Command Command;
            Command.type = Type; Command.point = Point; Command.queueMode = QueueMode;
            Command.units = MoveTemp(Units);
            const cinder::CommandResult Result = Sim.command(Command);
            if (!Result.accepted)
                UE_LOG(LogCinderMobileHUDPreview, Error,
                    TEXT("CINDERLINE_MOBILE_HUD_PREVIEW failed=tactical_command_%d reason=%s"),
                    static_cast<int32>(Type), UTF8_TO_TCHAR(Result.message.c_str()));
            return Result.accepted;
        };
        if (!TacticalPrimary || !TacticalSecondary
            || !IssueTactical(cinder::CommandType::Move, {1080, 820}, cinder::CommandQueueMode::Replace,
                {TacticalPrimary, TacticalSecondary})
            || !IssueTactical(cinder::CommandType::AttackMove, {1270, 960}, cinder::CommandQueueMode::Append,
                {TacticalPrimary, TacticalSecondary})
            || !IssueTactical(cinder::CommandType::Move, {1380, 1080}, cinder::CommandQueueMode::Append,
                {TacticalPrimary, TacticalSecondary})
            || !IssueTactical(cinder::CommandType::AttackMove, {1460, 1040}, cinder::CommandQueueMode::Append,
                {TacticalSecondary})) return;
    }
    if (bNeedsSustained)
    {
        TacticalPrimary = Sim.debugSpawn(cinder::Kind::Striker, 0, {820, 820});
        TacticalSecondary = Sim.debugSpawn(cinder::Kind::Striker, 0, {900, 820});
        if (bEscortPreview) SustainedLeader = Sim.debugSpawn(cinder::Kind::Scout, 0, {1210, 980});
        Sim.update(cinder::Simulation::Step);
        cinder::Command Command;
        Command.type = bEscortPreview ? cinder::CommandType::Escort : cinder::CommandType::Patrol;
        Command.units = {TacticalPrimary, TacticalSecondary};
        if (bEscortPreview) Command.target = SustainedLeader;
        else Command.point = {1320, 1040};
        const cinder::CommandResult Result = Sim.command(Command);
        if (!Result.accepted)
        {
            UE_LOG(LogCinderMobileHUDPreview, Error,
                TEXT("CINDERLINE_MOBILE_HUD_PREVIEW failed=sustained_command_%d reason=%s"),
                static_cast<int32>(Command.type), UTF8_TO_TCHAR(Result.message.c_str()));
            return;
        }
        for (const auto& Future : {
            TPair<cinder::CommandType, cinder::Vec2>(cinder::CommandType::Move, cinder::Vec2{1380, 1080}),
            TPair<cinder::CommandType, cinder::Vec2>(cinder::CommandType::AttackMove, cinder::Vec2{1460, 1040})})
        {
            cinder::Command Queued;
            Queued.type = Future.Key;
            Queued.point = Future.Value;
            Queued.units = {TacticalPrimary, TacticalSecondary};
            Queued.queueMode = cinder::CommandQueueMode::Append;
            const cinder::CommandResult QueuedResult = Sim.command(Queued);
            if (!QueuedResult.accepted)
            {
                UE_LOG(LogCinderMobileHUDPreview, Error,
                    TEXT("CINDERLINE_MOBILE_HUD_PREVIEW failed=sustained_tail_%d reason=%s"),
                    static_cast<int32>(Queued.type), UTF8_TO_TCHAR(QueuedResult.message.c_str()));
                return;
            }
        }
    }
    if (bNeedsFormation)
    {
        std::vector<cinder::Id> FormationUnits;
        for (int32 Index = 0; Index < 8; ++Index)
            FormationUnits.push_back(Sim.debugSpawn(Index % 2 == 0 ? cinder::Kind::Striker : cinder::Kind::Scout,
                0, cinder::Vec2{760.0f + (Index % 4) * 58.0f, 760.0f + (Index / 4) * 62.0f}));
        TacticalPrimary = FormationUnits.empty() ? 0 : FormationUnits.front();
        Sim.update(cinder::Simulation::Step);
        if (State == TEXT("formation-accepted"))
        {
            cinder::Command Command;
            Command.type = cinder::CommandType::Move;
            Command.units = FormationUnits;
            Command.point = {1110, 900};
            Command.spacing = cinder::FormationSpacing::Wide;
            Command.hasArrivalFacing = true;
            Command.arrivalFacing = 0.55f;
            const cinder::CommandResult Result = Sim.command(Command);
            if (!Result.accepted)
            {
                UE_LOG(LogCinderMobileHUDPreview, Error,
                    TEXT("CINDERLINE_MOBILE_HUD_PREVIEW failed=formation_command reason=%s"),
                    UTF8_TO_TCHAR(Result.message.c_str()));
                return;
            }
        }
    }

    if (ACinderCamera* Rig = Cast<ACinderCamera>(PC->GetPawn()))
    {
        const bool bTacticalFraming = bNeedsTacticalQueue || bNeedsSustained || bNeedsFormation;
        Rig->Zoom((bTacticalFraming ? 1600.0f : 1450.0f) - Rig->Distance());
        Rig->Focus(bTacticalFraming ? FVector(800, 700, 0) : FVector(760, 760, 0), true);
    }
    if (PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0);
    Battle->RenderState();

    if (State == TEXT("worker") || State == TEXT("build") || State == TEXT("placement"))
        PC->SelectKind(cinder::Kind::Worker);
    else if (bNeedsProducer)
        PC->SelectKind(cinder::Kind::Foundry);
    else if (bNeedsArmy)
        PC->ExecuteAction(TEXT("army"));
    else if (bNeedsTacticalQueue)
        PC->SelectOwnedKind(cinder::Kind::Striker);
    else if (bNeedsSustained)
        PC->SelectOwnedKind(cinder::Kind::Striker);
    else if (bNeedsFormation)
    {
        PC->SelectOwnedKind(cinder::Kind::Striker);
        PC->SelectOwnedKind(cinder::Kind::Scout);
        PC->ExecuteAction(TEXT("army"));
    }

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
    else if (bNeedsTacticalQueue)
    {
        CompactSheet = 4;
        PC->PreviewTacticalQueueIntent(State == TEXT("tactical-pending"));
    }
    else if (bNeedsSustained)
    {
        CompactSheet = 4;
        PC->PreviewSustainedIntent(bEscortPreview, bPendingSustained);
    }
    else if (bNeedsFormation)
    {
        const cinder::FormationSpacing Spacing = State == TEXT("formation-tight")
            ? cinder::FormationSpacing::Tight : State == TEXT("formation-wide")
            || State == TEXT("formation-facing") || State == TEXT("formation-accepted")
            ? cinder::FormationSpacing::Wide : cinder::FormationSpacing::Standard;
        if (State == TEXT("formation-facing"))
        {
            CompactSheet = 0;
            PC->PreviewFacingGesture({1060, 880}, {1190, 960}, Spacing);
        }
        else if (State != TEXT("formation-accepted"))
        {
            CompactSheet = 4;
            PC->PreviewFormationIntent(Spacing, true, State == TEXT("formation-pending"));
        }
    }

    CompactSelectionId = PC->Selection().empty() ? 0 : PC->Selection().front();
    Battle->Tick(cinder::Simulation::Step);
    Battle->RenderState();
    if (PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0);
    Battle->SetActorTickEnabled(false);
    PC->Notify(bNeedsTacticalQueue
        ? FString::Printf(TEXT("TACTICAL QUEUE PREVIEW: %s / CURRENT + 2 QUEUED"),
            State == TEXT("tactical-pending") ? TEXT("PENDING") : TEXT("ARMED"))
        : bNeedsSustained ? FString::Printf(TEXT("SUSTAINED ORDER PREVIEW: %s%s / CURRENT + 2 QUEUED"),
            bEscortPreview ? TEXT("ESCORT") : TEXT("PATROL"), bPendingSustained ? TEXT(" PENDING") : TEXT(" ARMED"))
        : bNeedsFormation ? FString::Printf(TEXT("FORMATION PREVIEW: %s"), *State.ToUpper())
        : FString::Printf(TEXT("MOBILE HUD PREVIEW: %s | development fixture"), *State.ToUpper()));
    const cinder::Entity* QueueProducer = State == TEXT("queue") && !PC->Selection().empty()
        ? Sim.find(PC->Selection().front()) : nullptr;
    UE_LOG(LogCinderMobileHUDPreview, Display,
        TEXT("CINDERLINE_MOBILE_HUD_PREVIEW state=%s frozen=1 seed=%u selection=%d sheet=%d build_menu=%d placement=%d queue_items=%d; scripted fixture, no save written, not a native gesture test"),
        *State, MobilePreviewSeed, static_cast<int32>(PC->Selection().size()), CompactSheet, PC->bBuildMenu,
        PC->IsBuildMode(), QueueProducer ? static_cast<int32>(QueueProducer->queue.size()) : 0);
    if (bNeedsTacticalQueue)
    {
        const cinder::Entity* Primary = Sim.find(TacticalPrimary);
        const cinder::Entity* Secondary = Sim.find(TacticalSecondary);
        UE_LOG(LogCinderMobileHUDPreview, Display,
            TEXT("CINDERLINE_TACTICAL_QUEUE_PREVIEW state=%s primary=%u current=%d queued=%d secondary=%u other_queued=%d armed=%d pending=%d sheet=%d"),
            *State, TacticalPrimary, Primary ? static_cast<int32>(Primary->order) : -1,
            Primary ? static_cast<int32>(Primary->futureOrders.size()) : -1,
            TacticalSecondary, Secondary ? static_cast<int32>(Secondary->futureOrders.size()) : -1,
            PC->IsQueueNextArmed(), PC->IsQueueNextPending(), CompactSheet);
    }
    if (bNeedsSustained)
    {
        const cinder::Entity* Primary = Sim.find(TacticalPrimary);
        UE_LOG(LogCinderMobileHUDPreview, Display,
            TEXT("CINDERLINE_SUSTAINED_ORDER_PREVIEW state=%s primary=%u order=%d leader=%u pending=%d sheet=%d"),
            *State, TacticalPrimary, Primary ? static_cast<int32>(Primary->order) : -1,
            SustainedLeader, PC->IsDestinationPending(), CompactSheet);
    }
    if (bNeedsFormation)
        UE_LOG(LogCinderMobileHUDPreview, Display,
            TEXT("CINDERLINE_FORMATION_PREVIEW state=%s spacing=%d armed=%d dragging=%d pending=%d sheet=%d"),
            *State, static_cast<int32>(PC->FormationSpacingPreset()), PC->IsFaceNextArmed(),
            PC->IsFacingPointerActive(), PC->IsFacingPending(), CompactSheet);
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
    const float DockTop = MobileLayout.Commands.bIsValid ? MobileLayout.Commands.Min.Y : Height - 56 * UIScale;
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
