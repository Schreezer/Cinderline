#include "Presentation/CinderPlayerController.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderHUD.h"
#include "Presentation/CinderAudioSubsystem.h"
#include "Presentation/CinderHelpContent.h"
#include "Presentation/CinderOnlineSubsystem.h"
#include "Presentation/CinderOnlinePanel.h"
#include "Engine/GameInstance.h"
#include "Widgets/SWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Components/InputComponent.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "HAL/FileManager.h"
#include <algorithm>

DEFINE_LOG_CATEGORY_STATIC(LogCinderInput, Log, All);

namespace
{
FString TutorialPreferencesPath()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Config/Training.ini"));
}
FString SkirmishPreferencesPath()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Config/Skirmish.ini"));
}
float TouchScale(const APlayerController* Controller)
{
    int W = 0, H = 0; Controller->GetViewportSize(W, H);
    return FMath::Max(1.0f, FMath::Min(W / 667.0f, H / 375.0f));
}
float MouseScale(const APlayerController* Controller)
{
    const UWorld* World = Controller->GetWorld();
    const UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
    return Viewport ? FMath::Max(1.0f, Viewport->GetDPIScale()) : 1.0f;
}

FString TutorialFocusFeedback(ECinderTutorialStep Step, const cinder::Entity* Entity, bool bTouch)
{
    switch (Step)
    {
    case ECinderTutorialStep::Camera:
        return TEXT("Base shown. Pan or zoom to continue.");
    case ECinderTutorialStep::SelectWorker:
        return TEXT("Drudge selected. Give it an ore order.");
    case ECinderTutorialStep::GatherOre:
        return bTouch ? TEXT("Drudge selected. Tap an explored ore deposit.")
            : TEXT("Drudge selected. Secondary-click an explored ore deposit.");
    case ECinderTutorialStep::TrainWorker:
        return TEXT("Open TRAIN and queue a Drudge. Your Anchor will receive the order.");
    case ECinderTutorialStep::BuildKiln:
        return Entity && Entity->kind == cinder::Kind::Foundry
            ? TEXT("Kiln site selected. Keep a Drudge assigned.")
            : TEXT("Open BUILD to place the Kiln. A Drudge will be assigned.");
    case ECinderTutorialStep::TrainEmbers:
        return TEXT("Open TRAIN and queue three Embers.");
    case ECinderTutorialStep::BuildSiphon:
        return Entity && Entity->kind == cinder::Kind::Processor
            ? TEXT("Siphon site selected. Keep a Drudge assigned.")
            : TEXT("Open BUILD to place the Siphon. A Drudge will be assigned.");
    case ECinderTutorialStep::Scout:
        return Entity ? TEXT("Skim selected. Send it toward the scout marker.")
            : TEXT("Open TRAIN and queue a Skim.");
    case ECinderTutorialStep::AttackMove:
        return Entity ? TEXT("Ember selected. Use ATTACK toward the combat marker.")
            : TEXT("Combat target shown. Train a replacement Ember.");
    case ECinderTutorialStep::BuildResonator:
        return Entity && Entity->kind == cinder::Kind::Laboratory
            ? TEXT("Resonator site selected. Keep a Drudge assigned.")
            : TEXT("Open BUILD to place the Resonator. A Drudge will be assigned.");
    case ECinderTutorialStep::ResearchWeapons:
        return TEXT("Open RESEARCH and queue WEAPONS.");
    case ECinderTutorialStep::Reinforce:
        return TEXT("Kiln selected. Set its rally, then open TRAIN for six Embers.");
    case ECinderTutorialStep::Defend:
        return TEXT("Defense marker shown. Open ARMY, choose ALL, then use ATTACK here.");
    case ECinderTutorialStep::DestroyAnchor:
        return TEXT("Enemy Anchor shown. Attack it with your army.");
    case ECinderTutorialStep::Complete:
        return TEXT("Your base is shown.");
    }
    return TEXT("Objective shown.");
}
}

void ACinderPlayerController::BeginPlay()
{
    Super::BeginPlay();
    bShowMouseCursor = true;
    bEnableTouchEvents = true;
    FInputModeGameAndUI InputMode;
    InputMode.SetHideCursorDuringCapture(false);
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(InputMode);
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &ACinderPlayerController::ApplicationWillEnterBackground);
    FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(this, &ACinderPlayerController::ApplicationHasEnteredForeground);
    Rig = Cast<ACinderCamera>(GetPawn());
    if (TActorIterator<ACinderBattlefield> It(GetWorld()); It) Battle = *It;
    // Only an interactive local game controller may arm the first-run offer.
    // Tests and previews can opt in explicitly through LoadTutorialPreference.
    bTutorialOfferResolved = true;
    const bool bEligibleForOffer = GConfig && !FApp::IsUnattended() && !GIsAutomationTesting
        && IsLocalController() && GetNetMode() != NM_DedicatedServer
        && GetWorld() && GetWorld()->IsGameWorld();
    if (bEligibleForOffer)
    {
        LoadTutorialPreference(TutorialPreferencesPath());
        LoadDifficultyPreference(SkirmishPreferencesPath());
        LoadMatchLengthPreference(SkirmishPreferencesPath());
    }
}

void ACinderPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    CloseOnlinePanel();
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.RemoveAll(this);
    FCoreDelegates::ApplicationHasEnteredForegroundDelegate.RemoveAll(this);
    Super::EndPlay(EndPlayReason);
}

ACinderBattlefield* ACinderPlayerController::Battlefield() const { return Battle; }

UCinderOnlineSubsystem* ACinderPlayerController::Online() const
{
    return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCinderOnlineSubsystem>() : nullptr;
}

#if UE_BUILD_DEVELOPMENT
void ACinderPlayerController::PreviewTacticalQueueIntent(bool bPending)
{
    ClearDestinationModes(true);
    DestinationMode = EDestinationMode::Move;
    bQueueNext = true;
    if (!bPending) return;
    PendingIntentSequence = MAX_uint32;
    PendingIntentGeneration = DestinationGeneration;
    PendingIntentMode = DestinationMode;
    PendingIntentSelection = Selected;
    PendingIntentWasQueueNext = true;
}

void ACinderPlayerController::PreviewSustainedIntent(bool bEscort, bool bPending)
{
    ClearDestinationModes(true);
    DestinationMode = bEscort ? EDestinationMode::Escort : EDestinationMode::Patrol;
    if (!bPending) return;
    PendingIntentSequence = MAX_uint32;
    PendingIntentGeneration = DestinationGeneration;
    PendingIntentMode = DestinationMode;
    PendingIntentSelection = Selected;
    PendingIntentWasQueueNext = false;
}

void ACinderPlayerController::PreviewFormationIntent(cinder::FormationSpacing Spacing,
    bool bFacing, bool bPending)
{
    ClearDestinationModes(true);
    SpacingPreset = Spacing;
    DestinationMode = EDestinationMode::Move;
    bFaceNext = bFacing;
    if (!bPending) return;
    PendingIntentSequence = MAX_uint32;
    PendingIntentGeneration = DestinationGeneration;
    PendingIntentMode = DestinationMode;
    PendingIntentSelection = Selected;
    PendingIntentSpacing = Spacing;
    PendingIntentHasArrivalFacing = bFacing;
    PendingIntentArrivalFacing = 0.0f;
    PendingIntentPoint = {1120, 880};
}

void ACinderPlayerController::PreviewFacingGesture(cinder::Vec2 Center,
    cinder::Vec2 Direction, cinder::FormationSpacing Spacing)
{
    ClearDestinationModes(true);
    SpacingPreset = Spacing;
    DestinationMode = EDestinationMode::Move;
    bFaceNext = true;
    bFacingPointer = true;
    bConsumeFacingRelease = true;
    FacingCenterPoint = Center;
    FacingDirection = Direction;
    FacingPointerSpacing = Spacing;
}
#endif

void ACinderPlayerController::LoadTutorialPreference(const FString& Filename)
{
    FConfigFile Settings;
    Settings.Read(Filename);
    bool bCompleted = false;
    bool bOfferResolved = false;
    Settings.GetBool(TEXT("Training"), TEXT("Completed"), bCompleted);
    Settings.GetBool(TEXT("Training"), TEXT("OfferResolved"), bOfferResolved);
    bTutorialCompleted = bCompleted;
    // Completed is the legacy migration marker: players who already finished
    // training should never receive the new first-run invitation.
    bTutorialOfferResolved = bCompleted || bOfferResolved;
}

void ACinderPlayerController::SaveTutorialPreference(const FString& Filename) const
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FConfigFile Settings;
    Settings.Read(Filename);
    Settings.SetBool(TEXT("Training"), TEXT("Completed"), bTutorialCompleted);
    Settings.SetBool(TEXT("Training"), TEXT("OfferResolved"), bTutorialOfferResolved);
    Settings.Write(Filename);
}

void ACinderPlayerController::LoadDifficultyPreference(const FString& Filename)
{
    FConfigFile Settings;
    Settings.Read(Filename);
    int32 Stored = static_cast<int32>(cinder::AIDifficulty::Normal);
    if (Settings.GetInt(TEXT("Skirmish"), TEXT("AIDifficulty"), Stored))
        MenuDifficulty = Stored >= 0
            ? cinder::aiDifficultyAt(static_cast<std::size_t>(Stored))
            : cinder::AIDifficulty::Normal;
}

void ACinderPlayerController::SaveDifficultyPreference(const FString& Filename) const
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FConfigFile Settings;
    Settings.Read(Filename);
    const FString Stored = FString::FromInt(static_cast<int32>(MenuDifficulty));
    Settings.SetString(TEXT("Skirmish"), TEXT("AIDifficulty"), *Stored);
    Settings.Write(Filename);
}

void ACinderPlayerController::LoadMatchLengthPreference(const FString& Filename)
{
    MenuMatchLength = cinder::MatchLength::Standard;
    FConfigFile Settings;
    Settings.Read(Filename);
    FString Stored;
    if (!Settings.GetString(TEXT("Skirmish"), TEXT("MatchLength"), Stored)) return;
    Stored.TrimStartAndEndInline();
    // Exact enum tokens keep malformed or overflowing values from becoming Short.
    for (int32 Index = 0; Index < static_cast<int32>(cinder::MatchLength::Count); ++Index)
        if (Stored == FString::FromInt(Index))
        {
            MenuMatchLength = cinder::matchLengthAt(Index);
            return;
        }
}

void ACinderPlayerController::SaveMatchLengthPreference(const FString& Filename) const
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FConfigFile Settings;
    Settings.Read(Filename);
    const FString Stored = FString::FromInt(static_cast<int32>(MenuMatchLength));
    Settings.SetString(TEXT("Skirmish"), TEXT("MatchLength"), *Stored);
    Settings.Write(Filename);
}

void ACinderPlayerController::OpenOnlinePanel()
{
    if (IsTutorialOfferPending()) return;
    if (OnlinePanel || !Online() || !GetWorld() || !GetWorld()->GetGameViewport()) return;
    if (bOnlineLeavePending || bTutorialRestartPending) return;
    if (Battle && !Battle->IsMenu() && !Battle->IsOnlineMatch()) return;
    bHelpOpen = false;
    ResetInteraction(false);
    if (Battle && !Battle->IsMenu()) Battle->SetPaused(true);
    TWeakObjectPtr<ACinderPlayerController> WeakThis(this);
    OnlinePanel = MakeCinderOnlinePanel(Online(), [WeakThis]() { if (auto* Self = WeakThis.Get()) Self->CloseOnlinePanel(); });
    GetWorld()->GetGameViewport()->AddViewportWidgetContent(OnlinePanel.ToSharedRef(), 50);
    FInputModeUIOnly Mode;
    Mode.SetWidgetToFocus(OnlinePanel); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    FSlateApplication::Get().SetKeyboardFocus(OnlinePanel);
}

void ACinderPlayerController::CloseOnlinePanel()
{
    if (!OnlinePanel) return;
    if (GetWorld() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(OnlinePanel.ToSharedRef());
    OnlinePanel.Reset();
    ResetInteraction(false);
    FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); SetInputMode(Mode);
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

void ACinderPlayerController::PollOnlineState()
{
    // Keep the first-run decision visible. Starting a server snapshot here
    // would hide the menu while the modal continues to block every action.
    if (IsTutorialOfferPending()) return;
    auto* Session = Online();
    if (!Session || !Battle) return;
    if (Session->HasMatch() && Session->LatestSnapshot() && !Battle->IsOnlineMatch())
    {
        if (Battle->StartOnlineMatch(*Session->LatestSnapshot()))
        {
            CloseOnlinePanel(); bHelpOpen = bTutorialRestartPending = bOnlineLeavePending = bCampaignMenuOpen = false;
            ResetInteraction(true); OnlineFeedbackSerial = Session->FeedbackSerial();
            bWorkerPlanNoticeInitialized = false;
            bOnlineEliminationObserved = false;
            Home();
            Notify(TEXT("Online match started. Menus do not pause the game."));
        }
    }
    if (Battle->IsOnlineMatch() && Session->FeedbackSerial() != OnlineFeedbackSerial)
    {
        OnlineFeedbackSerial = Session->FeedbackSerial();
        Notify(Session->OrderFeedback());
        if (!Battle->IsPaused()) UCinderAudioSubsystem::Play(this, Session->LastOrderAccepted() ? ECinderCue::Order_Ack : ECinderCue::Order_Invalid);
    }
    if (Battle->IsOnlineMatch() && PendingIntentSequence)
    {
        FCinderOnlineCommandAcknowledgement Acknowledgement;
        if (Session->ConsumeCommandAcknowledgement(PendingIntentSequence, Acknowledgement))
        {
            Notify(Acknowledgement.Message);
            ResolveDestinationAcknowledgement(PendingIntentSequence, Acknowledgement.bAccepted);
        }
        else if (!Session->IsCommandPending(PendingIntentSequence))
        {
            // A reconnect or match transition discarded the acknowledgement.
            // Its outcome is uncertain, so never re-arm an intent that could
            // duplicate a command already accepted by the authority.
            ClearDestinationModes(true);
            Notify(TEXT("Order outcome is uncertain. Check the refreshed battlefield before trying again."));
        }
    }
    if (Battle->IsOnlineMatch() && Session->IsEliminated() && !bOnlineEliminationObserved)
    {
        bOnlineEliminationObserved = true;
        bOnlineLeavePending = bOnlineSurrenderPending = false;
        ResetInteraction(true);
        Notify(TEXT("You are eliminated. The remaining players can finish the match; you can leave at any time."));
    }
}

void ACinderPlayerController::PollWorkerPlanNotice()
{
    if (!Battle || Battle->IsMenu())
    {
        bWorkerPlanNoticeInitialized = false;
        return;
    }
    const cinder::Simulation& Sim = Battle->Sim();
    const uint64 Serial = Sim.workerPlanNoticeSerial(0);
    const uint64 Tick = Sim.tick();
    if (!bWorkerPlanNoticeInitialized || Tick < WorkerPlanNoticeTick || Serial < WorkerPlanNoticeSerial)
    {
        bWorkerPlanNoticeInitialized = true;
        WorkerPlanNoticeSerial = Serial;
        WorkerPlanNoticeTick = Tick;
        return;
    }
    WorkerPlanNoticeTick = Tick;
    if (Serial == WorkerPlanNoticeSerial) return;
    WorkerPlanNoticeSerial = Serial;
    const std::string& Notice = Sim.workerPlanNotice(0);
    if (Notice.empty()) return;
    Notify(UTF8_TO_TCHAR(Notice.c_str()));
    if (!Battle->IsPaused()) UCinderAudioSubsystem::Play(this, ECinderCue::Order_Invalid);
}

void ACinderPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ACinderPlayerController::MousePressed);
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ACinderPlayerController::MouseReleased);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ACinderPlayerController::MouseContext);
    InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ACinderPlayerController::ZoomIn);
    InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ACinderPlayerController::ZoomOut);
    InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ACinderPlayerController::Home);
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ACinderPlayerController::Escape);
    InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ACinderPlayerController::Confirm);
    InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &ACinderPlayerController::ToggleHelp);
    InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ACinderPlayerController::TutorialShortcut);
    InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ACinderPlayerController::HelpSectionShortcut);
    InputComponent->BindKey(EKeys::O, IE_Pressed, this, &ACinderPlayerController::OpenOnlinePanel);
    InputComponent->BindKey(EKeys::A, IE_Pressed, this, &ACinderPlayerController::AttackMode);
    InputComponent->BindKey(EKeys::M, IE_Pressed, this, &ACinderPlayerController::MoveMode);
    InputComponent->BindKey(EKeys::D, IE_Pressed, this, &ACinderPlayerController::DefendMode);
    InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ACinderPlayerController::PatrolMode);
    InputComponent->BindKey(EKeys::E, IE_Pressed, this, &ACinderPlayerController::EscortMode);
    InputComponent->BindKey(EKeys::S, IE_Pressed, this, &ACinderPlayerController::Stop);
    InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ACinderPlayerController::Hold);
    InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ACinderPlayerController::ToggleBuild);
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ACinderPlayerController::FocusSelection);
    InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &ACinderPlayerController::ArrowUp);
    InputComponent->BindKey(EKeys::Down, IE_Pressed, this, &ACinderPlayerController::ArrowDown);
    InputComponent->BindKey(EKeys::Left, IE_Pressed, this, &ACinderPlayerController::ArrowLeft);
    InputComponent->BindKey(EKeys::Right, IE_Pressed, this, &ACinderPlayerController::ArrowRight);
    InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ACinderPlayerController::SquadOne);
    InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ACinderPlayerController::SquadTwo);
    InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ACinderPlayerController::SquadThree);
    InputComponent->BindTouch(IE_Pressed, this, &ACinderPlayerController::TouchPressed);
    InputComponent->BindTouch(IE_Released, this, &ACinderPlayerController::TouchReleased);
}

bool ACinderPlayerController::GroundPoint(FVector2D Screen, cinder::Vec2& Out) const
{
    FVector Origin, Direction;
    if (!DeprojectScreenPositionToWorld(Screen.X, Screen.Y, Origin, Direction) || FMath::Abs(Direction.Z) < 0.001) return false;
    const float Distance = -Origin.Z / Direction.Z;
    if (Distance < 0) return false;
    const FVector Point = Origin + Direction * Distance;
    Out = { static_cast<float>(Point.X), static_cast<float>(Point.Y) };
    return true;
}

void ACinderPlayerController::PanScreen(FVector2D Previous, FVector2D Current)
{
    if (!IsGameplayActive()) return;
    cinder::Vec2 A, B;
    if (Rig && GroundPoint(Previous, A) && GroundPoint(Current, B))
    {
        Rig->Pan(FVector(A.x - B.x, A.y - B.y, 0));
        if (FVector2D::DistSquared(Previous, Current) > 1) Battle->GuidanceCameraInput();
    }
}

bool ACinderPlayerController::ProjectTutorialTarget(cinder::Id Id, cinder::Vec2 Point, FVector2D& Out) const
{
    if (!Battle || (!Battle->Tutorial().IsActive() && !Battle->Campaign().IsActive())) return false;
    float Z = 0;
    if (Id)
    {
        const cinder::Entity* Entity = Battle->Sim().find(Id);
        if (!Entity || !Entity->alive() || (Entity->team != 0 && !Battle->Sim().visible(0, Entity->pos))) return false;
        Point = Battle->RenderPosition(*Entity);
        Z = cinder::definition(Entity->kind).air ? 125 : 25;
    }
    if (!ProjectWorldLocationToScreen(FVector(Point.x, Point.y, Z), Out)) return false;
    if (!Id) return true;
    const cinder::Entity* Pick = PickEntityAtScreen(Out);
    return Pick && Pick->id == Id;
}

void ACinderPlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    if (!Rig) Rig = Cast<ACinderCamera>(GetPawn());
    if (!Battle)
        if (TActorIterator<ACinderBattlefield> It(GetWorld()); It) Battle = *It;
    PollOnlineState();
    PollWorkerPlanNotice();
    if (!Battle || !Rig) return;
    FeedbackLife -= DeltaSeconds;
    if (FeedbackLife <= 0) FeedbackText.Empty();
    PruneArmyControlState();
    UpdateTutorial();

    float X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;
    bool Down1 = false, Down2 = false;
    GetInputTouchState(ETouchIndex::Touch1, X1, Y1, Down1);
    GetInputTouchState(ETouchIndex::Touch2, X2, Y2, Down2);
    if (bPointerDown && ((bPointerTouch && !Down1) || (!bPointerTouch && !IsInputKeyDown(EKeys::LeftMouseButton))))
    {
        // Focus loss may suppress release callbacks; cancel rather than issuing a stale order.
        CancelPointerWithoutRelease();
    }
    if (Down1 && Down2)
    {
        const FVector2D Centroid((X1 + X2) * 0.5f, (Y1 + Y2) * 0.5f);
        const float Pinch = FVector2D::Distance(FVector2D(X1, Y1), FVector2D(X2, Y2));
        if (bTwoDown && !bPointerUI && IsGameplayActive())
        {
            PanScreen(PreviousCentroid, Centroid);
            Rig->Zoom((PreviousPinch - Pinch) * Rig->Distance() / FMath::Max(PreviousPinch, 60.0f * TouchScale(this)));
            if (FMath::Abs(PreviousPinch - Pinch) > 1) Battle->GuidanceCameraInput();
        }
        PreviousCentroid = Centroid; PreviousPinch = Pinch;
        bTwoDown = true; bMultiTouch = true; ClearPointerTapIntent();
    }
    else
    {
        bTwoDown = false;
        if (Down1 && bPointerTouch && !bMultiTouch) PointerMoved(FVector2D(X1, Y1), DeltaSeconds);
        if (!Down1 && !Down2) bMultiTouch = false;
    }
    float MouseX = 0, MouseY = 0;
    if (GetMousePosition(MouseX, MouseY))
    {
        const FVector2D Mouse(MouseX, MouseY);
        if (bPointerDown && !bPointerTouch) PointerMoved(Mouse, DeltaSeconds);
        const bool PanHeld = IsInputKeyDown(EKeys::MiddleMouseButton);
        if (PanHeld && bMousePan && !bPointerCameraPan && IsGameplayActive()) PanScreen(PreviousMouse, Mouse);
        bMousePan = PanHeld; PreviousMouse = Mouse;
        if (bBuildMode && !bPointerTouch) GroundPoint(Mouse, Placement);
    }
    if (IsGameplayActive())
    {
        FVector Pan = FVector::ZeroVector;
        const FKey Keys[] = { EKeys::Up, EKeys::Down, EKeys::Left, EKeys::Right };
        const FVector Directions[] = { FVector(1, -1, 0), FVector(-1, 1, 0), FVector(-1, -1, 0), FVector(1, 1, 0) };
        for (int I = 0; I < 4; ++I)
        {
            if (!IsInputKeyDown(Keys[I])) { ArrowPanCredit[I] = 0; continue; }
            // The press nudge already advanced one frame; do not double-count it during a hold.
            const float HeldSeconds = FMath::Max(0.0f, DeltaSeconds - ArrowPanCredit[I]);
            ArrowPanCredit[I] = FMath::Max(0.0f, ArrowPanCredit[I] - DeltaSeconds);
            Pan += Directions[I] * Rig->Distance() * HeldSeconds;
        }
        Rig->Pan(Pan);
    }
}

void ACinderPlayerController::ArrowUp()
{
    if (bHelpOpen && CurrentHelpPage == CinderHelp::ReferencePage)
        ExecuteAction(TEXT("helpreference"), CurrentHelpReference - 1);
    else NudgeArrow(0);
}
void ACinderPlayerController::ArrowDown()
{
    if (bHelpOpen && CurrentHelpPage == CinderHelp::ReferencePage)
        ExecuteAction(TEXT("helpreference"), CurrentHelpReference + 1);
    else NudgeArrow(1);
}
void ACinderPlayerController::ArrowLeft()
{
    if (bHelpOpen) ExecuteAction(TEXT("helppage"), CurrentHelpPage - 1); else NudgeArrow(2);
}
void ACinderPlayerController::ArrowRight()
{
    if (bHelpOpen) ExecuteAction(TEXT("helppage"), CurrentHelpPage + 1); else NudgeArrow(3);
}
void ACinderPlayerController::NudgeArrow(int Direction)
{
    if (!Rig || !IsGameplayActive()) return;
    const FVector Directions[] = { FVector(1, -1, 0), FVector(-1, 1, 0), FVector(-1, -1, 0), FVector(1, 1, 0) };
    constexpr float NudgeSeconds = 1.0f / 60.0f;
    Rig->Pan(Directions[Direction] * Rig->Distance() * NudgeSeconds);
    Battle->GuidanceCameraInput();
    ArrowPanCredit[Direction] = NudgeSeconds;
}

void ACinderPlayerController::SquadShortcut(int32 Index)
{
    const bool bAssign = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl)
        || IsInputKeyDown(EKeys::LeftCommand) || IsInputKeyDown(EKeys::RightCommand);
    if (bAssign) AssignSquad(Index); else RecallSquad(Index);
}

void ACinderPlayerController::SquadOne() { SquadShortcut(0); }
void ACinderPlayerController::SquadTwo() { SquadShortcut(1); }
void ACinderPlayerController::SquadThree() { SquadShortcut(2); }

void ACinderPlayerController::ClearPointerTapIntent()
{
    PointerSelectionTarget = PointerContextTarget = PointerCommandTarget = 0;
    bPointerWorldTapLatched = false;
}

void ACinderPlayerController::CancelPointerWithoutRelease()
{
    if (bFacingPointer || bConsumeFacingRelease) ClearDestinationModes();
    bPointerDown = bDragging = bPointerTouch = bLongPress = false;
    bPointerCameraPan = bPointerPlacement = false;
    ClearPointerTapIntent();
}

void ACinderPlayerController::PointerPressed(FVector2D Position, bool Touch, bool CameraPan)
{
    UE_LOG(LogCinderInput, Verbose, TEXT("Press at %.1f,%.1f touch=%d"), Position.X, Position.Y, Touch);
    bPointerDown = true; bPointerTouch = Touch;
    ClearPointerTapIntent();
    PointerStart = PointerLast = Position; PointerHeld = 0; bDragging = false; bLongPress = false;
    auto* HUD = Cast<ACinderHUD>(GetHUD());
    bPointerUI = OnlinePanel.IsValid() || bOnlineLeavePending || bHelpOpen || bTutorialRestartPending || (HUD && HUD->ContainsUI(Position));
    // Latch the intent at mouse-down. Releasing Option/Alt before the button
    // must never turn a camera gesture into selection, an order or placement.
    bPointerCameraPan = !Touch && CameraPan && !bPointerUI && IsGameplayActive();
    bPointerPlacement = !bPointerCameraPan && !bPointerUI && bBuildMode && IsGameplayActive();
    bGestureSelect = !bPointerCameraPan && (!Touch || bBoxSelect);
    if (!bPointerCameraPan && !bPointerUI && !bBuildMode && bFaceNext
        && (IsMoveCommandMode() || IsAttackMoveMode() || IsDefendCommandMode()) && IsGameplayActive())
    {
        cinder::Vec2 Center{};
        bConsumeFacingRelease = true;
        bGestureSelect = false;
        if (GroundPoint(Position, Center) && FMath::IsFinite(Center.x) && FMath::IsFinite(Center.y)
            && Center.x >= 0 && Center.y >= 0
            && Center.x <= Battle->Sim().worldSize() && Center.y <= Battle->Sim().worldSize())
        {
            bFacingPointer = true;
            FacingCenterPoint = FacingDirection = Center;
            FacingPointerSpacing = SpacingPreset;
        }
        else Notify(TEXT("Choose a formation center inside the battlefield."));
    }
    else if (!bPointerCameraPan && !bPointerUI && !bBuildMode && IsEscortCommandMode() && IsGameplayActive())
    {
        const cinder::Entity* Hit = PickEntityAtScreen(Position);
        PointerCommandTarget = Hit ? Hit->id : 0;
    }
    else if (!bPointerCameraPan && !bPointerUI && !bBuildMode && !HasDestinationMode() && IsGameplayActive())
    {
        const cinder::Entity* Hit = PickEntityAtScreen(Position);
        bPointerWorldTapLatched = true;
        const bool bQueuedResumeTarget = Hit && bQueueNext && HasSingleSelectedWorker()
            && Hit->team == 0 && cinder::definition(Hit->kind).building && Hit->progress < 1;
        if (Hit && IsOwnedSelectable(Hit->id) && !bQueuedResumeTarget) PointerSelectionTarget = Hit->id;
        else PointerContextTarget = Hit ? Hit->id : 0;
    }
    GroundPoint(Position, Placement);
}

void ACinderPlayerController::PointerMoved(FVector2D Position, float DeltaSeconds)
{
    if (!bPointerDown || bMultiTouch) return;
    PointerHeld += DeltaSeconds;
    if (bFacingPointer)
    {
        PointerLast = Position;
        cinder::Vec2 Direction{};
        if (GroundPoint(Position, Direction)) FacingDirection = Direction;
        const float Threshold = bPointerTouch ? 10.0f * TouchScale(this) : 7.0f * MouseScale(this);
        bDragging = FVector2D::Distance(Position, PointerStart) > Threshold;
        ClearPointerTapIntent();
        return;
    }
    if (bPointerTouch && !bDragging && !bGestureSelect && !bPointerUI && !bPointerPlacement && IsGameplayActive() && PointerHeld > 0.42f)
    {
        bLongPress = true;
        bGestureSelect = true;
        Notify(TEXT("Drag to select an army"));
    }
    const float Threshold = bPointerTouch ? 10.0f * TouchScale(this) : 7.0f * MouseScale(this);
    if (FVector2D::Distance(Position, PointerStart) > Threshold) bDragging = true;
    if (bDragging) ClearPointerTapIntent();
    if (bDragging && !bGestureSelect && !bPointerUI && IsGameplayActive()) PanScreen(PointerLast, Position);
    PointerLast = Position;
    GroundPoint(Position, Placement);
}

void ACinderPlayerController::PointerReleased(FVector2D Position)
{
    UE_LOG(LogCinderInput, Verbose, TEXT("Release at %.1f,%.1f down=%d ui=%d dragging=%d multi=%d"), Position.X, Position.Y, bPointerDown, bPointerUI, bDragging, bMultiTouch);
    if (!bPointerDown) { ClearPointerTapIntent(); return; }
    PointerLast = Position;
    const float DragThreshold = bPointerTouch ? 10.0f * TouchScale(this) : 7.0f * MouseScale(this);
    if (FVector2D::Distance(Position, PointerStart) > DragThreshold)
    {
        bDragging = true;
        ClearPointerTapIntent();
    }
    if (bConsumeFacingRelease)
    {
        cinder::Vec2 Direction{};
        float ArrivalFacing = 0.0f;
        const bool bValidRelease = bFacingPointer && bDragging && GroundPoint(Position, Direction)
            && FMath::IsFinite(Direction.x) && FMath::IsFinite(Direction.y)
            && Direction.x >= 0 && Direction.y >= 0
            && Direction.x <= Battle->Sim().worldSize() && Direction.y <= Battle->Sim().worldSize()
            && cinder::rules::arrivalFacingFromDirection(
                {Direction.x - FacingCenterPoint.x, Direction.y - FacingCenterPoint.y}, ArrivalFacing);
        const cinder::Vec2 Center = FacingCenterPoint;
        const cinder::FormationSpacing Spacing = FacingPointerSpacing;
        bPointerDown = bDragging = bPointerTouch = bLongPress = false;
        bPointerCameraPan = bPointerPlacement = false;
        ClearFacingPointer();
        ClearPointerTapIntent();
        if (bValidRelease) IssueFacingDestination(Center, ArrivalFacing, Spacing);
        else Notify(TEXT("Drag farther from the formation center to set facing."));
        return;
    }
    if (bPointerCameraPan || (bPointerPlacement && (bDragging || !bBuildMode)) || (bLongPress && !bDragging))
    {
        // Consume even a click below the drag threshold, without changing modes.
        // A placement drag keeps its selected Drudge. A stationary long press
        // arms box selection without issuing the terrain command underneath it.
        bPointerDown = bDragging = bPointerTouch = bLongPress = false;
        bPointerCameraPan = bPointerPlacement = false;
        ClearPointerTapIntent();
        return;
    }
    if (!bMultiTouch)
    {
        if (bPointerUI)
        {
            if (!bDragging && FVector2D::Distance(Position, PointerStart) < (bPointerTouch ? 12.0f * TouchScale(this) : 15.0f * MouseScale(this)))
                if (auto* HUD = Cast<ACinderHUD>(GetHUD())) HUD->HandleTap(PointerStart);
        }
        else if (IsGameplayActive())
        {
            if (bDragging && bGestureSelect) SelectRectangle();
            else if (!bDragging) TapWorld(Position);
        }
    }
    bPointerDown = false; bDragging = false; bPointerTouch = false; bLongPress = false;
    bPointerCameraPan = bPointerPlacement = false;
    ClearPointerTapIntent();
}

void ACinderPlayerController::MousePressed()
{
    float X, Y;
    if (GetMousePosition(X, Y))
        PointerPressed(FVector2D(X, Y), false, IsInputKeyDown(EKeys::LeftAlt) || IsInputKeyDown(EKeys::RightAlt));
}
void ACinderPlayerController::MouseReleased()
{
    float X, Y;
    if (GetMousePosition(X, Y)) PointerReleased(FVector2D(X, Y));
    else CancelPointerWithoutRelease();
}
void ACinderPlayerController::MouseContext()
{
    if (!IsGameplayActive()) return;
    if (bBuildMode)
    {
        bBuildMode = bBuildMenu = bAutomaticBuild = false;
        ClearDestinationModes();
        Notify(TEXT("Placement cancelled."));
        return;
    }
    if (bFaceNext) { ClearDestinationModes(); Notify(TEXT("Formation facing cancelled.")); return; }
    float X, Y; if (GetMousePosition(X, Y))
    {
        auto* HUD = Cast<ACinderHUD>(GetHUD());
        if (!HUD || !HUD->ContainsUI(FVector2D(X, Y))) TapWorld(FVector2D(X, Y), true);
    }
}
void ACinderPlayerController::TouchPressed(ETouchIndex::Type Finger, FVector Location)
{
    const FVector2D Position(Location.X, Location.Y);
    if (Finger == ETouchIndex::Touch1)
    {
        // A fresh primary contact must not inherit a completed gesture before
        // PlayerTick gets a chance to observe that every finger was released.
        float X = 0, Y = 0; bool bSecondDown = false;
        GetInputTouchState(ETouchIndex::Touch2, X, Y, bSecondDown);
        if (!bSecondDown) { bMultiTouch = false; bTwoDown = false; }
        PointerPressed(Position, true);
    }
    else
    {
        ClearDestinationModes();
        bMultiTouch = true; bLongPress = false; bGestureSelect = false; ClearPointerTapIntent();
        if (const auto* HUD = Cast<ACinderHUD>(GetHUD()); HUD && HUD->ContainsUI(Position)) bPointerUI = true;
    }
}
void ACinderPlayerController::TouchReleased(ETouchIndex::Type Finger, FVector Location)
{
    if (Finger == ETouchIndex::Touch1) PointerReleased(FVector2D(Location.X, Location.Y));
}

void ACinderPlayerController::ApplicationWillEnterBackground()
{
    if (IsGameplayActive()) Battle->SetPaused(true);
    ResetInteraction(false);
}

void ACinderPlayerController::ApplicationHasEnteredForeground()
{
    ResetInteraction(false);
    if (auto* Session = Online()) Session->RecoverConnection();
}

cinder::Id ACinderPlayerController::PickProjectedEntity(FVector2D Point,
    const std::vector<FProjectedPickCandidate>& Candidates)
{
    cinder::Id Picked = 0;
    float BestDistance = TNumericLimits<float>::Max();
    for (const FProjectedPickCandidate& Candidate : Candidates)
    {
        if (!Candidate.Id || Candidate.Radius <= 0) continue;
        const float Distance = FVector2D::Distance(Point, Candidate.Center);
        if (Distance >= Candidate.Radius) continue;
        if (Distance < BestDistance || (FMath::IsNearlyEqual(Distance, BestDistance) && Candidate.Id < Picked))
        {
            Picked = Candidate.Id;
            BestDistance = Distance;
        }
    }
    return Picked;
}

const cinder::Entity* ACinderPlayerController::PickEntityAtScreen(FVector2D Point) const
{
    if (!Battle) return nullptr;
    std::vector<FProjectedPickCandidate> Candidates;
    Candidates.reserve(Battle->Sim().entities().size());
    const float PointerRadius = bPointerTouch ? 22.0f * TouchScale(this) : 24.0f * MouseScale(this);
    for (const cinder::Entity& Entity : Battle->Sim().entities())
    {
        if (!Entity.alive() || (Entity.team != 0 && !Battle->Sim().visible(0, Entity.pos))) continue;
        const cinder::Vec2 RenderPoint = Battle->RenderPosition(Entity);
        const float Z = cinder::definition(Entity.kind).air ? 125.0f : 25.0f;
        FVector2D Center;
        if (!ProjectWorldLocationToScreen(FVector(RenderPoint.x, RenderPoint.y, Z), Center)) continue;
        float HitRadius = PointerRadius;
        const float Radius = cinder::definition(Entity.kind).radius;
        FVector2D EdgeX, EdgeY;
        if (ProjectWorldLocationToScreen(FVector(RenderPoint.x + Radius, RenderPoint.y, Z), EdgeX)
            && ProjectWorldLocationToScreen(FVector(RenderPoint.x, RenderPoint.y + Radius, Z), EdgeY))
            HitRadius = FMath::Max(HitRadius, static_cast<float>(FMath::Max(
                FVector2D::Distance(Center, EdgeX), FVector2D::Distance(Center, EdgeY))));
        Candidates.push_back({Entity.id, Center, HitRadius});
    }
    return Battle->Sim().find(PickProjectedEntity(Point, Candidates));
}

bool ACinderPlayerController::IsOwnedSelectable(cinder::Id Id) const
{
    if (!Battle) return false;
    const cinder::Entity* Entity = Battle->Sim().find(Id);
    return Entity && Entity->alive() && Entity->team == 0 && Entity->kind != cinder::Kind::Resource;
}

void ACinderPlayerController::PruneArmyControlState()
{
    if (Battle && (Battle->Sim().winner() != -1 || Battle->Sim().eliminated(0)))
        ClearDestinationModes();
    const std::vector<cinder::Id> PreviousSelection = Selected;
    auto Prune = [this](std::vector<cinder::Id>& Ids, bool bCombatOnly)
    {
        Ids.erase(std::remove_if(Ids.begin(), Ids.end(), [this, bCombatOnly](cinder::Id Id)
        {
            if (!IsOwnedSelectable(Id)) return true;
            const cinder::Entity* Entity = Battle->Sim().find(Id);
            return bCombatOnly && (cinder::definition(Entity->kind).building || Entity->kind == cinder::Kind::Worker);
        }), Ids.end());
        std::vector<cinder::Id> Unique;
        Unique.reserve(Ids.size());
        for (cinder::Id Id : Ids)
            if (std::find(Unique.begin(), Unique.end(), Id) == Unique.end()) Unique.push_back(Id);
        Ids = std::move(Unique);
    };
    Prune(Selected, false);
    if (Selected != PreviousSelection) ClearDestinationModes();
    for (std::vector<cinder::Id>& Group : Squads) Prune(Group, true);
}

void ACinderPlayerController::SelectRectangle()
{
    if (!IsGameplayActive()) return;
    const FBox2D Bounds(FVector2D(FMath::Min(PointerStart.X, PointerLast.X), FMath::Min(PointerStart.Y, PointerLast.Y)), FVector2D(FMath::Max(PointerStart.X, PointerLast.X), FMath::Max(PointerStart.Y, PointerLast.Y)));
    Selected.clear();
    for (const auto& E : Battle->Sim().entities())
    {
        if (!E.alive() || E.team != 0 || cinder::definition(E.kind).building) continue;
        const auto RenderPoint = Battle->RenderPosition(E);
        FVector2D Screen;
        if (ProjectWorldLocationToScreen(FVector(RenderPoint.x, RenderPoint.y, cinder::definition(E.kind).air ? 125 : 25), Screen) && Bounds.IsInside(Screen)) Selected.push_back(E.id);
    }
    bBoxSelect = false;
    ClearDestinationModes();
    if (!Selected.empty()) UCinderAudioSubsystem::Play(this, ECinderCue::UI_Click);
    Notify(FString::Printf(TEXT("%d units selected"), static_cast<int>(Selected.size())));
}

bool ACinderPlayerController::SelectOwnedEntity(cinder::Id Id)
{
    if (!IsGameplayActive() || !IsOwnedSelectable(Id)) return false;
    ResetInteraction(false);
    Selected = {Id};
    return true;
}

void ACinderPlayerController::SelectOwnedKind(cinder::Kind Kind)
{
    if (!IsGameplayActive()) return;
    ResetInteraction(false);
    Selected.clear();
    for (const cinder::Entity& Entity : Battle->Sim().entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == Kind && Entity.kind != cinder::Kind::Resource)
            Selected.push_back(Entity.id);
}

void ACinderPlayerController::SelectKind(cinder::Kind Kind)
{
    if (!IsGameplayActive()) return;
    ResetInteraction(false);
    Selected.clear();
    int W, H; GetViewportSize(W, H);
    for (const auto& E : Battle->Sim().entities())
    {
        if (E.team != 0 || E.kind != Kind || !E.alive()) continue;
        const auto RenderPoint = Battle->RenderPosition(E);
        FVector2D Screen;
        if (ProjectWorldLocationToScreen(FVector(RenderPoint.x, RenderPoint.y, cinder::definition(E.kind).air ? 125 : 25), Screen) && Screen.X >= 0 && Screen.Y >= 0 && Screen.X <= W && Screen.Y <= H) Selected.push_back(E.id);
    }
}

void ACinderPlayerController::SelectArmy()
{
    if (!IsGameplayActive()) return;
    ResetInteraction(false);
    Selected.clear();
    for (const auto& E : Battle->Sim().entities())
        if (E.team == 0 && E.alive() && !cinder::definition(E.kind).building && E.kind != cinder::Kind::Worker) Selected.push_back(E.id);
    Notify(FString::Printf(TEXT("Army selected: %d"), static_cast<int>(Selected.size())));
}

bool ACinderPlayerController::AssignSquad(int32 Index)
{
    if (!IsGameplayActive() || Index < 0 || Index >= SquadCount) return false;
    PruneArmyControlState();
    std::vector<cinder::Id> Members;
    for (cinder::Id Id : Selected)
    {
        const cinder::Entity* Entity = Battle->Sim().find(Id);
        if (Entity && Entity->alive() && Entity->team == 0 && !cinder::definition(Entity->kind).building
            && Entity->kind != cinder::Kind::Worker && Entity->kind != cinder::Kind::Resource)
            Members.push_back(Id);
    }
    if (Members.empty())
    {
        Notify(TEXT("Select combat units before assigning a squad."));
        return false;
    }
    for (std::vector<cinder::Id>& Group : Squads)
        Group.erase(std::remove_if(Group.begin(), Group.end(), [&Members](cinder::Id Id)
        {
            return std::find(Members.begin(), Members.end(), Id) != Members.end();
        }), Group.end());
    Squads[Index] = std::move(Members);
    Notify(FString::Printf(TEXT("Squad %c assigned: %d"), TCHAR('A' + Index), static_cast<int32>(Squads[Index].size())));
    return true;
}

bool ACinderPlayerController::RecallSquad(int32 Index)
{
    if (!IsGameplayActive() || Index < 0 || Index >= SquadCount) return false;
    PruneArmyControlState();
    if (Squads[Index].empty())
    {
        Notify(FString::Printf(TEXT("Squad %c is empty."), TCHAR('A' + Index)));
        return false;
    }
    ResetInteraction(false);
    Selected = Squads[Index];
    Notify(FString::Printf(TEXT("Squad %c selected: %d"), TCHAR('A' + Index), static_cast<int32>(Selected.size())));
    return true;
}

const std::vector<cinder::Id>& ACinderPlayerController::Squad(int32 Index) const
{
    static const std::vector<cinder::Id> Empty;
    return Index >= 0 && Index < SquadCount ? Squads[Index] : Empty;
}

void ACinderPlayerController::TapWorld(FVector2D Position, bool ForceCommand)
{
    if (!IsGameplayActive()) return;
    cinder::Vec2 Ground{};
    const bool bGroundValid = GroundPoint(Position, Ground);
    const cinder::Entity* Hit = PickEntityAtScreen(Position);
    HandleWorldTap(Hit ? Hit->id : 0, bGroundValid ? &Ground : nullptr, ForceCommand);
}

bool ACinderPlayerController::SelectTappedEntity(cinder::Id Id)
{
    if (!IsGameplayActive() || !IsOwnedSelectable(Id)) return false;
    const cinder::Kind Kind = Battle->Sim().find(Id)->kind;
    const float Now = GetWorld()->GetTimeSeconds();
    const bool bDoubleTap = Id == LastTapEntity && Now - LastTapTime < 0.33f;
    ResetInteraction(false);
    if (bDoubleTap) SelectKind(Kind);
    if (!bDoubleTap || Selected.empty()) Selected = {Id};
    LastTapEntity = Id; LastTapTime = Now;
    UCinderAudioSubsystem::Play(this, ECinderCue::UI_Click);
    return true;
}

void ACinderPlayerController::HandleWorldTap(cinder::Id HitId, const cinder::Vec2* GroundTarget, bool ForceCommand)
{
    if (!IsGameplayActive()) return;
    if (IsEscortCommandMode() && bPointerDown) HitId = PointerCommandTarget;
    if (!ForceCommand && !bBuildMode && !HasDestinationMode()
        && (bPointerWorldTapLatched || PointerSelectionTarget))
    {
        // Preserve both directions of tap intent: a moving friendly cannot
        // turn selection into movement, or an empty-ground order into selection.
        if (PointerSelectionTarget)
        {
            if (!SelectTappedEntity(PointerSelectionTarget)) Notify(TEXT("That unit is no longer available."));
            return;
        }
        const cinder::Entity* InitialHit = Battle->Sim().find(PointerContextTarget);
        if (PointerContextTarget && (!InitialHit || !InitialHit->alive()
            || (InitialHit->kind == cinder::Kind::Resource
                ? !Battle->Sim().explored(0, InitialHit->pos)
                : !Battle->Sim().visible(0, InitialHit->pos))))
        {
            Notify(TEXT("That target is no longer available."));
            return;
        }
        HitId = PointerContextTarget;
    }
    cinder::Vec2 Ground = GroundTarget ? *GroundTarget : cinder::Vec2{};
    const bool bGroundValid = GroundTarget && FMath::IsFinite(Ground.x) && FMath::IsFinite(Ground.y)
        && Ground.x >= 0 && Ground.y >= 0
        && Ground.x <= Battle->Sim().worldSize() && Ground.y <= Battle->Sim().worldSize();
    const cinder::Entity* Hit = Battle->Sim().find(HitId);
    if (Hit && (!Hit->alive() || (Hit->team != 0
        && (Hit->kind == cinder::Kind::Resource ? !Battle->Sim().explored(0, Hit->pos) : !Battle->Sim().visible(0, Hit->pos))))) Hit = nullptr;
    const bool bEnemyHit = Hit && Hit->team > 0 && Hit->team < Battle->Sim().playerCount();
    auto InvalidPoint = [this]
    {
        Notify(TEXT("Choose a point inside the battlefield."));
        UCinderAudioSubsystem::Play(this, ECinderCue::Order_Invalid);
    };
    if (bBuildMode)
    {
        if (!bGroundValid) { InvalidPoint(); return; }
        cinder::Command Cmd; Cmd.type = bAutomaticBuild ? cinder::CommandType::AutoBuild : cinder::CommandType::Build;
        Cmd.kind = PendingBuilding; Cmd.point = Ground;
        const bool bQueued = !bAutomaticBuild
            && ResolveQueueMode(Cmd.type, QueueModifierDown(), bQueueNext, true) == cinder::CommandQueueMode::Append;
        Cmd.queueMode = bQueued ? cinder::CommandQueueMode::Append : cinder::CommandQueueMode::Replace;
        if (bQueued && !HasSingleSelectedWorker())
        {
            Notify(TEXT("Select exactly one Drudge to queue construction."));
            UCinderAudioSubsystem::Play(this, ECinderCue::Order_Invalid);
            return;
        }
        if (Issue(Cmd))
        {
            bBuildMenu = false;
            if (bQueued)
            {
                const cinder::Entity* Worker = Battle->Sim().find(Selected.front());
                const int32 Count = Worker ? static_cast<int32>(Worker->futureOrders.size()) : 0;
                Notify(Battle->IsOnlineMatch()
                    ? TEXT("Build plan sent; awaiting server. Place another site or Cancel.")
                    : FString::Printf(TEXT("Plan accepted / %d of 16 future jobs. Pay when work starts. Place another or Cancel."), Count));
            }
            else bBuildMode = bAutomaticBuild = false;
        }
        return;
    }
    if (IsProductionRallyMode())
    {
        const auto* Producer = Battle->Sim().find(RallyProducer);
        // Ore meshes rise above the ground plane. Resolve a worker rally from
        // the picked deposit, not the ray's offset intersection behind it.
        if (Producer && Producer->kind == cinder::Kind::Headquarters
            && Hit && Hit->kind == cinder::Kind::Resource)
            IssueDestination(Hit->pos);
        else if (bGroundValid) IssueDestination(Ground);
        else InvalidPoint();
        return;
    }
    if (IsEscortCommandMode())
    {
        if (Selected.empty())
        {
            Notify(TEXT("Select escorts before choosing their leader."));
            UCinderAudioSubsystem::Play(this, ECinderCue::Order_Invalid);
            return;
        }
        if (!Hit || Hit->team != 0 || Hit->kind == cinder::Kind::Resource
            || cinder::definition(Hit->kind).building)
        {
            Notify(TEXT("Choose an owned mobile unit to escort."));
            UCinderAudioSubsystem::Play(this, ECinderCue::Order_Invalid);
            return;
        }
        IssueEscortTarget(Hit->id);
        return;
    }
    const bool bSelectedDrudge = std::any_of(Selected.begin(), Selected.end(), [&](cinder::Id Id)
    {
        const auto* Worker = Battle->Sim().find(Id);
        return Worker && Worker->alive() && Worker->team == 0 && Worker->kind == cinder::Kind::Worker;
    });
    const bool bSingleSelectedDrudge = HasSingleSelectedWorker();
    const bool bExplicitDestination = HasUnitDestinationMode();
    if (Hit && Hit->team == 0 && cinder::definition(Hit->kind).building && Hit->progress < 1
        && bSelectedDrudge && !bExplicitDestination && (ForceCommand || bQueueNext))
    {
        cinder::Command Cmd; Cmd.type = cinder::CommandType::ResumeConstruction; Cmd.target = Hit->id;
        Cmd.queueMode = ResolveQueueMode(Cmd.type, QueueModifierDown(), bQueueNext, false);
        if (Cmd.queueMode == cinder::CommandQueueMode::Append && !bSingleSelectedDrudge)
        {
            Notify(TEXT("Select exactly one Drudge to queue a construction resume."));
            UCinderAudioSubsystem::Play(this, ECinderCue::Order_Invalid);
            return;
        }
        if (Issue(Cmd))
        {
            bBuildMenu = false;
            if (Cmd.queueMode == cinder::CommandQueueMode::Append && bQueueNext)
            {
                bQueueNext = false;
                Notify(Battle->IsOnlineMatch()
                    ? TEXT("Resume plan sent; awaiting server.")
                    : TEXT("Construction resume added to this Drudge's future plan."));
            }
        }
        return;
    }
    if (Hit && Hit->team == 0 && Hit->kind != cinder::Kind::Resource && !ForceCommand && !bExplicitDestination)
    {
        SelectTappedEntity(Hit->id);
        return;
    }
    if (Selected.empty())
    {
        if (bExplicitDestination) Notify(TEXT("Select a unit before choosing its destination."));
        return;
    }
    if (bQueueNext && bSingleSelectedDrudge && !bExplicitDestination && !Hit)
    {
        Notify(TEXT("Queue plan: tap explored ore, tap an unfinished friendly site, or open Build."));
        return;
    }
    if (!bGroundValid)
    {
        // Targeted orders use the authoritative entity point. A projected unit
        // near the world edge must remain selectable and targetable even when
        // the screen ray lands beyond the ground plane's playable bounds.
        if (Hit && (bExplicitDestination || Hit->kind == cinder::Kind::Resource || bEnemyHit)) Ground = Hit->pos;
        else { if (bExplicitDestination || ForceCommand) InvalidPoint(); return; }
    }
    if (bExplicitDestination)
    {
        IssueDestination(Ground);
        return;
    }
    cinder::Command Cmd; Cmd.point = Ground; Cmd.type = cinder::CommandType::Move;
    const auto* Primary = Battle->Sim().find(Selected.front());
    if (Hit && Hit->kind == cinder::Kind::Resource && ForceCommand
        && Primary && cinder::definition(Primary->kind).building)
    {
        Cmd.type = cinder::CommandType::Rally;
        if (Primary->kind == cinder::Kind::Headquarters) Cmd.point = Hit->pos;
    }
    else if (Hit && Hit->kind == cinder::Kind::Resource) { Cmd.type = cinder::CommandType::Gather; Cmd.target = Hit->id; }
    else if (bEnemyHit) { Cmd.type = cinder::CommandType::Attack; Cmd.target = Hit->id; }
    else if (const auto* First = Battle->Sim().find(Selected.front()); First && cinder::definition(First->kind).building)
    {
        if (!ForceCommand) { ExecuteAction(TEXT("deselect")); return; }
        Cmd.type = cinder::CommandType::Rally;
    }
    const bool bShiftDown = QueueModifierDown();
    Cmd.queueMode = ResolveQueueMode(Cmd.type, bShiftDown, bQueueNext, false);
    if (Cmd.type == cinder::CommandType::Move) Cmd.spacing = SpacingPreset;
    if (Issue(Cmd) && Cmd.queueMode == cinder::CommandQueueMode::Append
        && Cmd.type == cinder::CommandType::Gather && bQueueNext)
    {
        bQueueNext = false;
        Notify(Battle->IsOnlineMatch()
            ? TEXT("Mining plan sent; awaiting server.")
            : TEXT("Mining added to this Drudge's future plan."));
    }
}

bool ACinderPlayerController::Issue(cinder::Command Command, uint32* OutOnlineSequence)
{
    if (!IsGameplayActive()) return false;
    Command.team = 0;
    const bool bAutomatic = Command.type == cinder::CommandType::AutoBuild
        || Command.type == cinder::CommandType::AutoTrain || Command.type == cinder::CommandType::AutoResearch
        || Command.type == cinder::CommandType::AutoRally;
    if (Command.units.empty() && !bAutomatic) Command.units = Selected;
    const auto Result = Battle->SubmitCommand(Command, OutOnlineSequence);
    if (Result.accepted) { LastTapEntity = 0; LastTapTime = -1; }
    if (Result.accepted && !Battle->IsOnlineMatch()) Battle->Tutorial().AcceptedCommand(Battle->Sim(), Command);
    Notify(UTF8_TO_TCHAR(Result.message.c_str()));
    if (!Battle->IsOnlineMatch() || !Result.accepted) UCinderAudioSubsystem::Play(this, Result.accepted ? ECinderCue::Order_Ack : ECinderCue::Order_Invalid);
    return Result.accepted;
}

cinder::CommandQueueMode ACinderPlayerController::ResolveQueueMode(cinder::CommandType Type,
    bool bShiftDown, bool bTouchQueue, bool bExplicitDestination)
{
    const bool bTacticalDestination = Type == cinder::CommandType::Move
        || Type == cinder::CommandType::AttackMove;
    const bool bWorkerPlan = Type == cinder::CommandType::Build
        || Type == cinder::CommandType::ResumeConstruction || Type == cinder::CommandType::Gather;
    return ((bTacticalDestination && (bShiftDown || (bExplicitDestination && bTouchQueue)))
        || (bWorkerPlan && (bShiftDown || bTouchQueue)))
        ? cinder::CommandQueueMode::Append : cinder::CommandQueueMode::Replace;
}

bool ACinderPlayerController::QueueModifierDown() const
{
    return IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
}

bool ACinderPlayerController::HasSingleSelectedWorker() const
{
    if (!Battle || Selected.size() != 1) return false;
    const cinder::Entity* Worker = Battle->Sim().find(Selected.front());
    return Worker && Worker->alive() && Worker->team == 0 && Worker->kind == cinder::Kind::Worker;
}

bool ACinderPlayerController::IsWorkerBuildQueueActive() const
{
    return IsManualBuildMode() && HasSingleSelectedWorker()
        && ResolveQueueMode(cinder::CommandType::Build, QueueModifierDown(), bQueueNext, true)
            == cinder::CommandQueueMode::Append;
}

void ACinderPlayerController::ResolveDestinationAcknowledgement(uint32 Sequence, bool bAccepted)
{
    if (!Sequence || Sequence != PendingIntentSequence) return;
    const bool bStillCurrent = !bPendingIntentCancelled && PendingIntentGeneration == DestinationGeneration
        && PendingIntentMode == DestinationMode && PendingIntentSelection == Selected
        && PendingIntentWasQueueNext == bQueueNext
        && (!PendingIntentHasArrivalFacing || PendingIntentSpacing == SpacingPreset)
        && PendingIntentHasArrivalFacing == bFaceNext;
    DiscardPendingDestinationIntent();
    if (bStillCurrent && bAccepted) ClearDestinationModes();
    // A matching rejection deliberately leaves the same targeting mode armed
    // so the player can correct the destination or chosen leader.
}

bool ACinderPlayerController::IssueDestination(cinder::Vec2 Point)
{
    if (PendingIntentSequence)
    {
        Notify(TEXT("This destination is waiting for the server. Other controls remain available."));
        return false;
    }
    cinder::Command Command;
    Command.point = Point;
    switch (DestinationMode)
    {
    case EDestinationMode::ProductionRally:
        Command.type = cinder::CommandType::AutoRally;
        Command.target = RallyProducer;
        Command.kind = cinder::Kind::Resource;
        break;
    case EDestinationMode::Defend: Command.type = cinder::CommandType::Defend; break;
    case EDestinationMode::Patrol: Command.type = cinder::CommandType::Patrol; break;
    case EDestinationMode::AttackMove: Command.type = cinder::CommandType::AttackMove; break;
    case EDestinationMode::Move: Command.type = cinder::CommandType::Move; break;
    default: return false;
    }
    if (Command.type == cinder::CommandType::Move || Command.type == cinder::CommandType::AttackMove
        || Command.type == cinder::CommandType::Defend) Command.spacing = SpacingPreset;
    const bool bShiftDown = QueueModifierDown();
    Command.queueMode = ResolveQueueMode(Command.type, bShiftDown, bQueueNext, true);
    const bool bTrackedOnlineQueue = Battle->IsOnlineMatch() && bQueueNext
        && Command.queueMode == cinder::CommandQueueMode::Append;
    const bool bTrackAuthoritativeIntent = bTrackedOnlineQueue
        || (Battle->IsOnlineMatch() && Command.type == cinder::CommandType::Patrol);
    return FinishDestinationIssue(Command,
        bTrackedOnlineQueue ? TEXT("Queue next sent. Waiting for the server to accept the waypoint.")
        : TEXT("Patrol sent. Waiting for the server to accept the route."),
        bTrackAuthoritativeIntent);
}

bool ACinderPlayerController::IssueFacingDestination(cinder::Vec2 Point, float ArrivalFacing,
    cinder::FormationSpacing Spacing)
{
    if (PendingIntentSequence)
    {
        Notify(TEXT("This destination is waiting for the server. Other controls remain available."));
        return false;
    }
    cinder::Command Command;
    Command.point = Point;
    Command.spacing = Spacing;
    Command.hasArrivalFacing = true;
    Command.arrivalFacing = ArrivalFacing;
    if (IsMoveCommandMode()) Command.type = cinder::CommandType::Move;
    else if (IsAttackMoveMode()) Command.type = cinder::CommandType::AttackMove;
    else if (IsDefendCommandMode()) Command.type = cinder::CommandType::Defend;
    else return false;
    const bool bShiftDown = QueueModifierDown();
    Command.queueMode = ResolveQueueMode(Command.type, bShiftDown, bQueueNext, true);
    return FinishDestinationIssue(Command,
        TEXT("Formation sent. Waiting for the server to accept its facing."), Battle->IsOnlineMatch());
}

bool ACinderPlayerController::IssueEscortTarget(cinder::Id Target)
{
    if (PendingIntentSequence)
    {
        Notify(TEXT("This destination is waiting for the server. Other controls remain available."));
        return false;
    }
    cinder::Command Command;
    Command.type = cinder::CommandType::Escort;
    Command.target = Target;
    Command.queueMode = cinder::CommandQueueMode::Replace;
    return FinishDestinationIssue(Command,
        TEXT("Escort sent. Waiting for the server to accept the leader."), Battle->IsOnlineMatch());
}

bool ACinderPlayerController::FinishDestinationIssue(cinder::Command Command,
    const TCHAR* PendingFeedback, bool bTrackAuthoritativeIntent)
{
    uint32 OnlineSequence = 0;
    if (!Issue(Command, bTrackAuthoritativeIntent ? &OnlineSequence : nullptr)) return false;
    if (bTrackAuthoritativeIntent && OnlineSequence)
    {
        PendingIntentSequence = OnlineSequence;
        PendingIntentGeneration = DestinationGeneration;
        PendingIntentMode = DestinationMode;
        PendingIntentSelection = Selected;
        PendingIntentWasQueueNext = bQueueNext;
        bPendingIntentCancelled = false;
        PendingIntentQueueMode = Command.queueMode;
        PendingIntentSpacing = Command.spacing;
        PendingIntentHasArrivalFacing = Command.hasArrivalFacing;
        PendingIntentArrivalFacing = Command.arrivalFacing;
        PendingIntentPoint = Command.point;
        Notify(PendingFeedback);
        return true;
    }
    ClearDestinationModes();
    return true;
}
bool ACinderPlayerController::IsGameplayActive() const
{
    return Battle && !OnlinePanel && !bOnlineLeavePending && !bHelpOpen && !bTutorialRestartPending &&
        !Battle->IsMenu() && !Battle->IsPaused() && !Battle->IsMatchOver() && !Battle->Sim().eliminated(0) &&
        (!Battle->IsOnlineMatch() || (Online() && Online()->CanSendOrders()));
}
void ACinderPlayerController::ResetInteraction(bool bClearSelection)
{
    bBuildMode = bBuildMenu = bBoxSelect = false;
    bAutomaticBuild = false;
    ClearDestinationModes();
    if (bClearSelection) DiscardPendingDestinationIntent();
    bPointerDown = bDragging = bGestureSelect = bPointerTouch = bLongPress = false;
    bPointerUI = bMultiTouch = bTwoDown = bMousePan = false;
    bPointerCameraPan = bPointerPlacement = false;
    PointerStart = PointerLast = PreviousCentroid = PreviousMouse = FVector2D::ZeroVector;
    PointerHeld = PreviousPinch = 0;
    LastTapTime = -1;
    LastTapEntity = 0;
    ClearPointerTapIntent();
    PendingBuilding = cinder::Kind::Foundry;
    Placement = {};
    for (float& Credit : ArrowPanCredit) Credit = 0;
    FeedbackText.Empty(); FeedbackLife = 0;
    if (bClearSelection)
    {
        Selected.clear();
        for (std::vector<cinder::Id>& Group : Squads) Group.clear();
    }
}
void ACinderPlayerController::Notify(const FString& Message) { FeedbackText = Message; FeedbackLife = 3.5f; }
void ACinderPlayerController::Home()
{
    if (IsTutorialOfferPending()) return;
    if (!Rig || !Battle || OnlinePanel || bOnlineLeavePending || bHelpOpen || bTutorialRestartPending) return;
    if (IsGameplayActive()) Battle->GuidanceCameraInput();
    for (const auto& E : Battle->Sim().entities()) if (E.team == 0 && E.kind == cinder::Kind::Headquarters && E.alive()) { Rig->Focus(FVector(E.pos.x, E.pos.y, 0)); return; }
}
void ACinderPlayerController::FocusSelection()
{
    if (!IsGameplayActive() || !Rig || Selected.empty()) return;
    Battle->GuidanceCameraInput();
    FVector Sum = FVector::ZeroVector; int Count = 0;
    for (auto Id : Selected) if (const auto* E = Battle->Sim().find(Id)) { Sum += FVector(E->pos.x, E->pos.y, 0); ++Count; }
    if (Count) Rig->Focus(Sum / Count);
}
void ACinderPlayerController::Escape()
{
    if (IsTutorialOfferPending()) { ExecuteAction(TEXT("onboardskip")); return; }
    if (bOnlineLeavePending) { ExecuteAction(TEXT("onlinecancel")); return; }
    if (bHelpOpen) { ExecuteAction(TEXT("helpclose")); return; }
    if (bTutorialRestartPending) { ExecuteAction(TEXT("tutorialcancel")); return; }
    if (bCampaignMenuOpen) { ExecuteAction(TEXT("campaignclose")); return; }
    if (bBuildMode || bBuildMenu || HasDestinationMode() || bBoxSelect)
    { ResetInteraction(false); return; }
    if (auto* HUD = Cast<ACinderHUD>(GetHUD()); HUD && HUD->CloseCompactSheet()) return;
    if (Battle && !Battle->IsMenu()) { ResetInteraction(false); Battle->SetPaused(!Battle->IsPaused()); }
}
void ACinderPlayerController::Confirm()
{
    if (IsTutorialOfferPending()) { ExecuteAction(TEXT("onboardcampaign")); return; }
    if (bOnlineLeavePending) { ExecuteAction(TEXT("onlineconfirm")); return; }
    if (bHelpOpen) { ExecuteAction(TEXT("helpclose")); return; }
    if (bTutorialRestartPending) { ExecuteAction(TEXT("tutorialconfirm")); return; }
    if (!Battle) return;
    if (bCampaignMenuOpen) { ExecuteAction(TEXT("campaignstart"), CampaignMenuMission); return; }
    if (Battle->IsMenu() || Battle->Sim().winner() != -1 || Battle->IsPaused()) UCinderAudioSubsystem::Play(this, ECinderCue::UI_Click);
    if (Battle->IsMenu())
    {
        const auto* HUD = Cast<ACinderHUD>(GetHUD());
        ExecuteAction(TEXT("start"), HUD ? HUD->MenuMap() : 0);
    }
    else if (Battle->IsMatchOver() || Battle->Sim().eliminated(0))
        ExecuteAction(Battle->IsOnlineMatch() ? TEXT("onlineleave") : Battle->Campaign().IsActive() ? TEXT("campaignrestart") : Battle->Tutorial().IsActive() ? TEXT("tutorialrestart") : TEXT("start"), Battle->MapIndex());
    else if (Battle->IsPaused()) ExecuteAction(TEXT("resume"));
}
void ACinderPlayerController::ClearFacingPointer()
{
    bFacingPointer = false;
    bConsumeFacingRelease = false;
    FacingCenterPoint = FacingDirection = {};
    FacingPointerSpacing = SpacingPreset;
}

void ACinderPlayerController::DiscardPendingDestinationIntent()
{
    PendingIntentSequence = 0;
    PendingIntentGeneration = 0;
    PendingIntentMode = EDestinationMode::None;
    PendingIntentSelection.clear();
    PendingIntentWasQueueNext = false;
    bPendingIntentCancelled = false;
    PendingIntentQueueMode = cinder::CommandQueueMode::Replace;
    PendingIntentSpacing = cinder::FormationSpacing::Standard;
    PendingIntentHasArrivalFacing = false;
    PendingIntentArrivalFacing = 0.0f;
    PendingIntentPoint = {};
}

void ACinderPlayerController::ClearDestinationModes(bool bDiscardPending)
{
    ++DestinationGeneration;
    DestinationMode = EDestinationMode::None;
    bQueueNext = false;
    bFaceNext = false;
    ClearFacingPointer();
    if (bDiscardPending) DiscardPendingDestinationIntent();
    else if (PendingIntentSequence) bPendingIntentCancelled = true;
    RallyProducer = 0;
}

void ACinderPlayerController::ToggleDestinationMode(EDestinationMode Mode)
{
    ++DestinationGeneration;
    if (PendingIntentSequence) bPendingIntentCancelled = true;
    bQueueNext = false;
    bFaceNext = false;
    ClearFacingPointer();
    DestinationMode = DestinationMode == Mode ? EDestinationMode::None : Mode;
    RallyProducer = 0;
    bBuildMode = bBuildMenu = false;
}

cinder::CommandResult ACinderPlayerController::BuildPlacementStatus(const cinder::Vec2* Site) const
{
    if (!Battle) return {false, "No active match."};
    if (!bAutomaticBuild)
    {
        if (IsWorkerBuildQueueActive())
        {
            if (!Site) return {true, "Ready to plan a construction site."};
            if (!FMath::IsFinite(Site->x) || !FMath::IsFinite(Site->y)
                || Site->x < 0 || Site->y < 0
                || Site->x > Battle->Sim().worldSize() || Site->y > Battle->Sim().worldSize())
                return {false, "Choose a point inside the battlefield."};
            std::string Reason;
            if (!Battle->Sim().canPlace(0, PendingBuilding, *Site, &Reason)) return {false, Reason};
            return {true, "Valid planned site. Ore will be charged when reached."};
        }
        return Battle->Sim().buildStatus(0, PendingBuilding, Selected, Site);
    }
    const auto Plan = Battle->Sim().autoBuildStatus(0, PendingBuilding, Site);
    return {Plan.accepted, Plan.message};
}

void ACinderPlayerController::BeginGlobalBuild(cinder::Kind Kind)
{
    if (!IsGameplayActive()) return;
    const auto Plan = Battle->Sim().autoBuildStatus(0, Kind);
    if (!Plan.accepted)
    {
        Notify(UTF8_TO_TCHAR(Plan.message.c_str()));
        UCinderAudioSubsystem::Play(this, ECinderCue::Order_Invalid);
        return;
    }
    ClearDestinationModes();
    PendingBuilding = Kind; bBuildMode = bAutomaticBuild = true; bBuildMenu = false;
    Notify(TEXT("Place on clear, visible ground. A reachable idle Drudge or miner will be assigned."));
}

void ACinderPlayerController::QueueTraining(cinder::Kind Kind, int Quantity, cinder::Id Producer)
{
    cinder::Command Command; Command.type = cinder::CommandType::AutoTrain;
    Command.kind = Kind; Command.queueIndex = Quantity; Command.target = Producer;
    Issue(Command);
}

void ACinderPlayerController::QueueResearch(int Upgrade, cinder::Id Producer)
{
    cinder::Command Command; Command.type = cinder::CommandType::AutoResearch;
    Command.queueIndex = Upgrade; Command.target = Producer;
    Issue(Command);
}

void ACinderPlayerController::CancelProduction(cinder::Id Producer, cinder::Id Job)
{
    if (!Producer || !Job) return;
    cinder::Command Command; Command.type = cinder::CommandType::CancelQueue;
    Command.units = {Producer}; Command.target = Job;
    Issue(Command);
}

void ACinderPlayerController::CancelConstruction(cinder::Id Site)
{
    if (!Site) return;
    cinder::Command Command; Command.type = cinder::CommandType::CancelBuilding;
    Command.units = {Site}; Issue(Command);
}

void ACinderPlayerController::BeginProductionRally(cinder::Id Producer)
{
    if (!IsGameplayActive()) return;
    const auto Status = Battle->Sim().autoRallyStatus(0, cinder::Kind::Resource, Producer);
    if (!Status.accepted) { Notify(UTF8_TO_TCHAR(Status.message.c_str())); return; }
    ClearDestinationModes();
    bBuildMode = bBuildMenu = bAutomaticBuild = false;
    DestinationMode = EDestinationMode::ProductionRally;
    RallyProducer = Producer;
    const auto* Facility = Producer ? Battle->Sim().find(Producer) : nullptr;
    Notify(Facility && Facility->kind == cinder::Kind::Headquarters
        ? TEXT("Tap ore to send new Drudges mining, or terrain for a ground rally.")
        : Producer ? TEXT("Tap terrain or the minimap to set this facility's rally.")
        : TEXT("Place an army rally point for future combat units, including new facilities. Existing orders continue."));
}

void ACinderPlayerController::UseDefaultProductionRally(cinder::Id Producer)
{
    if (!IsGameplayActive() || !Producer) return;
    const auto Status = Battle->Sim().autoRallyStatus(0, cinder::Kind::Resource, Producer, true);
    if (!Status.accepted) { Notify(UTF8_TO_TCHAR(Status.message.c_str())); return; }
    cinder::Command Command;
    Command.type = cinder::CommandType::AutoRally; Command.kind = cinder::Kind::Resource;
    Command.target = Producer; Command.queueIndex = 1;
    if (Issue(Command)) ClearDestinationModes();
}

void ACinderPlayerController::FocusArmyRally()
{
    if (!IsGameplayActive()) return;
    const auto& Player = Battle->Sim().players()[0];
    if (!Player.armyRallySet) { Notify(TEXT("Set an army rally point first.")); return; }
    if (Rig)
    {
        Rig->Focus(FVector(Player.armyRally.x, Player.armyRally.y, 0));
        Battle->GuidanceCameraInput();
    }
}

void ACinderPlayerController::FocusProduction(cinder::Id Producer)
{
    if (!IsGameplayActive() || !Battle) return;
    const auto* Entity = Battle->Sim().find(Producer);
    if (!Entity || !cinder::definition(Entity->kind).building || !SelectOwnedEntity(Producer)) return;
    FocusSelection();
}

void ACinderPlayerController::AttackMode()
{
    if (!IsGameplayActive()) return;
    ToggleDestinationMode(EDestinationMode::AttackMove);
}

void ACinderPlayerController::MoveMode()
{
    if (!IsGameplayActive()) return;
    ToggleDestinationMode(EDestinationMode::Move);
}

void ACinderPlayerController::DefendMode()
{
    if (!IsGameplayActive()) return;
    ToggleDestinationMode(EDestinationMode::Defend);
}
void ACinderPlayerController::PatrolMode()
{
    if (!IsGameplayActive()) return;
    ToggleDestinationMode(EDestinationMode::Patrol);
    if (IsPatrolCommandMode()) Notify(TEXT("Patrol: choose the far endpoint. Units repeat from their accepted positions."));
}
void ACinderPlayerController::EscortMode()
{
    if (!IsGameplayActive()) return;
    ToggleDestinationMode(EDestinationMode::Escort);
    if (IsEscortCommandMode()) Notify(TEXT("Escort: choose an owned mobile unit to follow and guard."));
}
void ACinderPlayerController::Stop() { cinder::Command C; C.type = cinder::CommandType::Stop; if (Issue(C)) ClearDestinationModes(); }
void ACinderPlayerController::Hold() { cinder::Command C; C.type = cinder::CommandType::Hold; if (Issue(C)) ClearDestinationModes(); }
void ACinderPlayerController::ToggleBuild()
{
    if (!IsGameplayActive()) return;
    const bool bPreserveWorkerQueue = bQueueNext && HasSingleSelectedWorker();
    bBuildMenu = !bBuildMenu;
    bBuildMode = false;
    ClearDestinationModes();
    bQueueNext = bPreserveWorkerQueue;
}
void ACinderPlayerController::ZoomIn()
{
    if (IsTutorialOfferPending()) return;
    if (Rig && !OnlinePanel && !bOnlineLeavePending && !bHelpOpen && !bTutorialRestartPending)
    { Rig->Zoom(-180); if (IsGameplayActive()) Battle->GuidanceCameraInput(); }
}
void ACinderPlayerController::ZoomOut()
{
    if (IsTutorialOfferPending()) return;
    if (Rig && !OnlinePanel && !bOnlineLeavePending && !bHelpOpen && !bTutorialRestartPending)
    { Rig->Zoom(180); if (IsGameplayActive()) Battle->GuidanceCameraInput(); }
}

void ACinderPlayerController::OpenHelp(int32 Page)
{
    if (!Battle || OnlinePanel || bOnlineLeavePending || bTutorialRestartPending) return;
    ResetInteraction(false);
    CurrentHelpPage = FMath::Clamp(Page, 0, CinderHelp::TopicCount - 1);
    bHelpOpen = true;
    if (!Battle->IsMenu()) Battle->SetPaused(true);
}

void ACinderPlayerController::ToggleHelp()
{
    if (IsTutorialOfferPending()) return;
    if (OnlinePanel || bOnlineLeavePending || bTutorialRestartPending) return;
    if (bHelpOpen) ExecuteAction(TEXT("helpclose")); else OpenHelp(CurrentHelpPage);
}

void ACinderPlayerController::TutorialShortcut()
{
    if (IsTutorialOfferPending()) return;
    if (OnlinePanel || bOnlineLeavePending || bTutorialRestartPending) return;
    if (bHelpOpen) ExecuteAction(TEXT("helpinput"), bHelpTouch ? 0 : 1);
    else if (Battle && Battle->IsMenu()) BeginTutorial();
    else if (Battle && Battle->Tutorial().IsActive() && Battle->IsPaused()) ExecuteAction(TEXT("tutorialrestart"));
}

void ACinderPlayerController::HelpSectionShortcut()
{
    if (bHelpOpen)
        if (auto* HUD = Cast<ACinderHUD>(GetHUD())) HUD->NextHelpSection();
}

void ACinderPlayerController::BeginTutorial()
{
    if (!Battle || Battle->IsOnlineMatch()) return;
    if (const auto* Session = Online(); Session && Session->HasRoom())
    { Notify(TEXT("Leave your online room before starting training.")); return; }
    bHelpOpen = bTutorialRestartPending = bCampaignMenuOpen = false;
    ResetInteraction(true);
    Battle->StartTutorial();
    // Establish the starting view without crediting the camera lesson.
    if (Rig)
    {
        Rig->Focus(FVector(600, 600, 0), true);
        Rig->Zoom(ACinderCamera::DefaultDistance * 0.9f - Rig->Distance());
        Rig->Focus(FVector(600, 600, 0), true);
    }
    Notify(TEXT("Guided battle started. Follow each objective to defeat the opposing Anchor. Your saved skirmish is kept."));
}

void ACinderPlayerController::ResolveTutorialOffer(bool bBeginTutorial)
{
    if (!IsTutorialOfferPending()) return;
    bTutorialOfferResolved = true;
    if (GConfig && !FApp::IsUnattended() && !GIsAutomationTesting && GetWorld() && GetWorld()->IsGameWorld())
        SaveTutorialPreference(TutorialPreferencesPath());
    if (bBeginTutorial) BeginTutorial();
}

void ACinderPlayerController::UpdateTutorial()
{
    if (!Battle || Battle->IsMenu() || Battle->IsPaused() || Battle->IsOnlineMatch()
        || !Battle->Tutorial().IsActive()) return;
    const ECinderTutorialStep Previous = Battle->Tutorial().Step();
    Battle->Tutorial().Observe(Battle->Sim(), Selected);
    if (Previous != Battle->Tutorial().Step())
    {
        UE_LOG(LogCinderInput, Display, TEXT("CINDERLINE_TUTORIAL step=%d complete=%d simulation_tick=%llu"),
            static_cast<int32>(Battle->Tutorial().Step()), Battle->Tutorial().IsComplete(), Battle->Sim().tick());
        UCinderAudioSubsystem::Play(this, ECinderCue::UI_Click);
    }
    if (Battle->Tutorial().IsComplete() && !bTutorialCompleted)
    {
        bTutorialCompleted = true;
        bTutorialOfferResolved = true;
        // Automation must never change the player's completion preference.
        if (GConfig && !FApp::IsUnattended() && !GIsAutomationTesting && GetWorld() && GetWorld()->IsGameWorld())
            SaveTutorialPreference(TutorialPreferencesPath());
    }
}

void ACinderPlayerController::ExecuteAction(const FString& Action, int Argument)
{
    UE_LOG(LogCinderInput, Verbose, TEXT("Action %s argument=%d"), *Action, Argument);
    if (!Battle) return;
    if (IsTutorialOfferPending())
    {
        if (Action == TEXT("onboardcampaign")) { ResolveTutorialOffer(false); OpenCampaignMenu(); }
        else if (Action == TEXT("onboardlearn")) ResolveTutorialOffer(true);
        else if (Action == TEXT("onboardskip")) ResolveTutorialOffer(false);
        return;
    }
    if (Action == TEXT("onlinecancel")) { bOnlineLeavePending = bOnlineSurrenderPending = false; ResetInteraction(false); return; }
    if (Action == TEXT("onlineconfirm"))
    {
        if (bOnlineLeavePending)
        {
            bOnlineLeavePending = false; bHelpOpen = false;
            if (bOnlineSurrenderPending)
            {
                bOnlineSurrenderPending = false;
                const bool bSent = Online() && Online()->Surrender();
                if (bSent) Battle->SetPaused(false);
                ResetInteraction(false);
                Notify(bSent ? TEXT("Surrender sent. Waiting for the server to confirm your elimination.") : TEXT("Reconnect to surrender, or choose Leave to exit the match."));
            }
            else
            {
                if (Online()) Online()->Leave();
                CloseOnlinePanel(); Battle->ReturnToMenu(); ResetInteraction(true);
            }
        }
        return;
    }
    if (bOnlineLeavePending) return;
    if (ExecuteCampaignAction(Action, Argument)) return;
    if (Action == TEXT("difficulty"))
    {
        if (!Battle->IsMenu() || Battle->IsOnlineMatch()) return;
        MenuDifficulty = Argument >= 0
            ? cinder::aiDifficultyAt(static_cast<std::size_t>(Argument))
            : cinder::AIDifficulty::Normal;
        if (!FApp::IsUnattended() && !GIsAutomationTesting)
            SaveDifficultyPreference(SkirmishPreferencesPath());
        return;
    }
    if (Action == TEXT("matchlength"))
    {
        if (!Battle->IsMenu() || Battle->IsOnlineMatch()) return;
        MenuMatchLength = cinder::matchLengthAt(Argument);
        if (!FApp::IsUnattended() && !GIsAutomationTesting)
            SaveMatchLengthPreference(SkirmishPreferencesPath());
        return;
    }
    if (Action == TEXT("online") || Action == TEXT("connection")) { OpenOnlinePanel(); return; }
    if (Action == TEXT("onlineleave") || Action == TEXT("onlinesurrender"))
    {
        if (!Battle->IsOnlineMatch())
        {
            // A session may have started before its first snapshot reaches the
            // battlefield. It must remain possible to cancel that session.
            if (Action == TEXT("onlineleave"))
                if (auto* Session = Online(); Session && Session->CanLeaveSession())
                {
                    Session->Leave(); CloseOnlinePanel(); ResetInteraction(false);
                    Notify(TEXT("Leaving the online session."));
                }
            return;
        }
        if (Battle->Sim().winner() != -1 || (Online() && Online()->IsEliminated()))
        {
            if (Online()) Online()->Leave();
            CloseOnlinePanel(); Battle->ReturnToMenu(); ResetInteraction(true);
        }
        else
        {
            bHelpOpen = false; bOnlineLeavePending = true;
            bOnlineSurrenderPending = Action == TEXT("onlinesurrender");
            ResetInteraction(false); Battle->SetPaused(true);
        }
        return;
    }
    if (Battle->IsOnlineMatch() && (Action == TEXT("start") || Action == TEXT("tutorial") || Action == TEXT("tutorialrestart") ||
        Action == TEXT("tutorialend") || Action == TEXT("save") || Action == TEXT("load") || Action == TEXT("menu")))
    {
        if (Action == TEXT("menu")) ExecuteAction(TEXT("onlineleave"));
        else Notify(TEXT("Online matches are controlled by the server. Leave the match to play offline."));
        return;
    }
    if ((Action == TEXT("start") || Action == TEXT("tutorial") || Action == TEXT("load")) && Online() && Online()->HasRoom())
    { Notify(TEXT("Leave your online room before starting an offline match.")); return; }
    if (Action == TEXT("help")) { OpenHelp(Argument); return; }
    if (Action == TEXT("helpclose"))
    {
        if (bHelpOpen) { bHelpOpen = false; ResetInteraction(false); }
        return; // Reading help never resumes a match implicitly.
    }
    if (Action == TEXT("helppage"))
    {
        if (bHelpOpen) CurrentHelpPage = (Argument % CinderHelp::TopicCount + CinderHelp::TopicCount) % CinderHelp::TopicCount;
        return;
    }
    if (Action == TEXT("helpreference"))
    {
        if (bHelpOpen) CurrentHelpReference = (Argument % CinderHelp::ReferenceCount + CinderHelp::ReferenceCount) % CinderHelp::ReferenceCount;
        return;
    }
    if (Action == TEXT("helpinput")) { if (bHelpOpen) bHelpTouch = Argument != 0; return; }
    if (Action == TEXT("tutorialcancel")) { bTutorialRestartPending = false; ResetInteraction(false); return; }
    if (Action == TEXT("tutorialconfirm"))
    {
        if (bTutorialRestartPending && Battle->Tutorial().IsActive()) BeginTutorial();
        return;
    }
    if (bTutorialRestartPending) return;
    if (Action == TEXT("tutorial") || Action == TEXT("tutorialrestart"))
    {
        if (Battle->IsMenu()) BeginTutorial();
        else if (Battle->Tutorial().IsActive())
        {
            bHelpOpen = false; bTutorialRestartPending = true;
            ResetInteraction(false); Battle->SetPaused(true);
        }
        return;
    }
    if (bHelpOpen) return;
    if (Action == TEXT("tutorialhelp"))
    {
        if (Battle->Tutorial().IsActive()) OpenHelp(Battle->Tutorial().Text(Battle->Sim(), bHelpTouch).HelpPage);
        return;
    }
    if (Action == TEXT("tutorialclearmode"))
    {
        if (IsGameplayActive() && (Battle->Tutorial().IsActive() || Battle->Campaign().IsActive()))
        {
            ClearDestinationModes();
            bBuildMode = bAutomaticBuild = bBuildMenu = false;
        }
        return;
    }
    if (Action == TEXT("tutorialfocus"))
    {
        if (IsGameplayActive() && Battle->Tutorial().IsActive())
        {
            const auto Primary = Battle->Tutorial().PrimaryAction(Battle->Sim());
            const FName Panel = Primary == ECinderTutorialPrimaryAction::OpenBuild ? FName(TEXT("build"))
                : Primary == ECinderTutorialPrimaryAction::OpenTrain ? FName(TEXT("train"))
                : Primary == ECinderTutorialPrimaryAction::OpenResearch ? FName(TEXT("research")) : NAME_None;
            if (!Panel.IsNone())
            {
                if (auto* HUD = Cast<ACinderHUD>(GetHUD())) HUD->OpenGlobalPanel(Panel);
                Notify(TutorialFocusFeedback(Battle->Tutorial().Step(), nullptr, bHelpTouch));
                return;
            }
        }
        cinder::Vec2 Point;
        cinder::Id EntityId = 0;
        if (IsGameplayActive() && Rig && Battle->Tutorial().FocusPoint(Battle->Sim(), Point, &EntityId))
        {
            const cinder::Entity* Focused = nullptr;
            if (EntityId && SelectOwnedEntity(EntityId)) Focused = Battle->Sim().find(EntityId);
            Rig->Focus(FVector(Point.x, Point.y, 0));
            Notify(TutorialFocusFeedback(Battle->Tutorial().Step(), Focused, bHelpTouch));
        }
        return;
    }
    if (Action == TEXT("tutorialend"))
    {
        if (Battle->Tutorial().IsActive()) { Battle->ReturnToMenu(); ResetInteraction(true); }
        return;
    }
    if (Action == TEXT("start"))
    {
        bCampaignMenuOpen = false;
        const cinder::AIDifficulty Difficulty = Battle->IsMenu()
            ? MenuDifficulty : Battle->MatchDifficulty();
        const cinder::MatchLength Length = Battle->IsMenu()
            ? MenuMatchLength : Battle->Sim().config().matchLength;
        Battle->StartMatch(Argument, Difficulty, Length);
        ResetInteraction(true); DiscardPendingDestinationIntent(); Home();
        Notify(TEXT("Select a Drudge, then tap amber ore. Build a Kiln to raise your army."));
    }
    else if (Action == TEXT("menu")) { bCampaignMenuOpen = false; Battle->ReturnToMenu(); ResetInteraction(true); DiscardPendingDestinationIntent(); }
    else if (Action == TEXT("pause")) { ResetInteraction(false); Battle->SetPaused(true); }
    else if (Action == TEXT("resume")) { ResetInteraction(false); Battle->SetPaused(false); }
    else if (Action == TEXT("home")) Home();
    else if (Action == TEXT("focus")) FocusSelection();
    else if (Action == TEXT("rallyfocus")) FocusArmyRally();
    else if (Action == TEXT("army")) SelectArmy();
    else if (Action == TEXT("deselect") && IsGameplayActive())
    {
        ResetInteraction(false);
        Selected.clear();
        Notify(TEXT("Selection cleared. Existing orders continue."));
    }
    else if (Action == TEXT("squadassign")) AssignSquad(Argument);
    else if (Action == TEXT("squad")) RecallSquad(Argument);
    else if (Action == TEXT("workers") && IsGameplayActive())
    {
        ResetInteraction(false);
        SelectKind(cinder::Kind::Worker);
        Notify(Selected.empty() ? TEXT("No Drudges in view. Tap Home to return to your base.")
            : FString::Printf(TEXT("%d Drudges selected"), static_cast<int>(Selected.size())));
    }
    else if (Action == TEXT("box") && IsGameplayActive()) { bBoxSelect = !bBoxSelect; Notify(TEXT("Drag across units to select. Two fingers pan and zoom.")); }
    else if (Action == TEXT("attack")) AttackMode();
    else if (Action == TEXT("move")) MoveMode();
    else if (Action == TEXT("defend")) DefendMode();
    else if (Action == TEXT("patrol")) PatrolMode();
    else if (Action == TEXT("escort")) EscortMode();
    else if (Action == TEXT("space") && IsGameplayActive())
    {
        if (bFacingPointer || PendingIntentSequence)
        {
            Notify(TEXT("Spacing is locked while this formation order is pending."));
            return;
        }
        const int32 Next = (static_cast<int32>(SpacingPreset) + 1) % 3;
        SpacingPreset = static_cast<cinder::FormationSpacing>(Next);
        const TCHAR* Name = SpacingPreset == cinder::FormationSpacing::Tight ? TEXT("Tight")
            : SpacingPreset == cinder::FormationSpacing::Wide ? TEXT("Wide") : TEXT("Standard");
        Notify(FString::Printf(TEXT("Formation spacing: %s. Applies to the next Move, Attack or Defend."), Name));
    }
    else if (Action == TEXT("facenext") && IsGameplayActive())
    {
        if (PendingIntentSequence)
        {
            Notify(TEXT("Formation facing is waiting for the server."));
            return;
        }
        if (!IsMoveCommandMode() && !IsAttackMoveMode() && !IsDefendCommandMode())
        {
            bFaceNext = false;
            Notify(TEXT("Choose Move, Attack or Defend before Face next."));
            UCinderAudioSubsystem::Play(this, ECinderCue::Order_Invalid);
            return;
        }
        ClearFacingPointer();
        bFaceNext = !bFaceNext;
        Notify(bFaceNext ? TEXT("Face next: press the destination, drag toward the facing direction, then release.")
            : TEXT("Face next cancelled. A normal destination tap uses automatic facing."));
    }
    else if (Action == TEXT("queuenext") && IsGameplayActive())
    {
        if (IsQueueNextPending())
        {
            ClearDestinationModes();
            Notify(TEXT("Pending Queue next targeting cancelled. Existing authoritative orders continue."));
        }
        else if (DestinationMode == EDestinationMode::Move || DestinationMode == EDestinationMode::AttackMove)
        {
            bQueueNext = !bQueueNext;
            const TCHAR* ActionName = DestinationMode == EDestinationMode::Move ? TEXT("Move") : TEXT("Attack move");
            Notify(bQueueNext
                ? FString::Printf(TEXT("Queue next: %s. Choose one destination."), ActionName)
                : FString::Printf(TEXT("Queue next cancelled. %s will replace orders."), ActionName));
        }
        else if (HasSingleSelectedWorker() && DestinationMode == EDestinationMode::None)
        {
            bQueueNext = !bQueueNext;
            Notify(bQueueNext
                ? TEXT("Queue plan armed: tap explored ore, tap an unfinished friendly site, or open Build. One order will be added; queued placement stays open for more sites.")
                : TEXT("Queue plan cancelled. The next Drudge order will replace its current plan."));
        }
        else
        {
            bQueueNext = false;
            Notify(TEXT("Choose Move or Attack, or select exactly one Drudge, before arming Queue."));
            UCinderAudioSubsystem::Play(this, ECinderCue::Order_Invalid);
        }
    }
    else if (Action == TEXT("clearorders"))
    {
        cinder::Command C; C.type = cinder::CommandType::ClearOrders;
        Issue(C);
    }
    else if (Action == TEXT("stop")) Stop();
    else if (Action == TEXT("hold")) Hold();
    else if (Action == TEXT("buildmenu")) ToggleBuild();
    else if (Action == TEXT("zoom+")) ZoomIn();
    else if (Action == TEXT("zoom-")) ZoomOut();
    else if (Action == TEXT("kind"))
    {
        ClearDestinationModes();
        const auto Kind = static_cast<cinder::Kind>(Argument);
        Selected.erase(std::remove_if(Selected.begin(), Selected.end(), [&](cinder::Id Id) { const auto* E = Battle->Sim().find(Id); return !E || E->kind != Kind; }), Selected.end());
    }
    else if (Action == TEXT("build") && IsGameplayActive())
    {
        const auto Kind = static_cast<cinder::Kind>(Argument);
        const bool bPlanning = bQueueNext || QueueModifierDown();
        // Only queued construction needs a single builder, because the future
        // orders are owned by one Drudge. Immediate placement must not demand it:
        // checkBuild filters the selection down to workers and chooses the best
        // one itself, preferring idle over mining, and explains an empty or
        // out-of-range selection better than a blanket refusal here can.
        if (bPlanning && !HasSingleSelectedWorker())
        {
            bBuildMode = false;
            Notify(TEXT("Select exactly one Drudge to queue a construction order."));
            UCinderAudioSubsystem::Play(this, ECinderCue::Order_Invalid);
            return;
        }
        const auto Status = bPlanning
            ? cinder::CommandResult{true, "Ready to plan."}
            : Battle->Sim().buildStatus(0, Kind, Selected);
        if (!Status.accepted)
        {
            bBuildMode = false;
            Notify(UTF8_TO_TCHAR(Status.message.c_str()));
            UCinderAudioSubsystem::Play(this, ECinderCue::Order_Invalid);
            return;
        }
        const bool bPreserveQueue = bQueueNext;
        ClearDestinationModes();
        bQueueNext = bPreserveQueue;
        PendingBuilding = Kind; bBuildMode = true; bAutomaticBuild = false;
#if PLATFORM_IOS || PLATFORM_ANDROID
        Notify(bPlanning
            ? TEXT("Tap clear, visible sites to queue them. Ore stays unspent until each site is reached; cancel when done. Maximum 16 future orders.")
            : TEXT("Tap a clear, explored site near your Drudge. Cancel to exit placement."));
#else
        Notify(bPlanning
            ? TEXT("Shift-click clear, visible sites to queue them. Ore stays unspent until reached; secondary click cancels. Maximum 16 future orders.")
            : TEXT("Click a clear, explored site near your Drudge. Hold Shift to append and keep placing; secondary click cancels."));
#endif
    }
    else if (Action == TEXT("cancelplacement"))
    {
        bBuildMode = false; bBuildMenu = false; bAutomaticBuild = false;
        ClearDestinationModes();
    }
    else if (Action == TEXT("cancelrally")) ClearDestinationModes();
    else if (Action == TEXT("train")) { cinder::Command C; C.type = cinder::CommandType::Train; C.kind = static_cast<cinder::Kind>(Argument); Issue(C); }
    else if (Action == TEXT("research")) { cinder::Command C; C.type = cinder::CommandType::Research; C.queueIndex = Argument; Issue(C); }
    else if (Action == TEXT("cancelqueue")) { cinder::Command C; C.type = cinder::CommandType::CancelQueue; C.queueIndex = Argument; Issue(C); }
    else if (Action == TEXT("cancelbuilding")) { cinder::Command C; C.type = cinder::CommandType::CancelBuilding; Issue(C); }
    else if (Action == TEXT("resumeconstruction") && IsGameplayActive())
    {
        auto& Sim = Battle->Sim();
        const auto* Site = Selected.empty() ? nullptr : Sim.find(Selected.front());
        if (!Site || !Site->alive() || Site->team != 0 || !cinder::definition(Site->kind).building || Site->progress >= 1) return;
        if (Sim.constructionWorker(Site->id)) { Notify(TEXT("A Drudge is already assigned to this site")); return; }
        cinder::Id Builder = 0; float Nearest = TNumericLimits<float>::Max();
        for (const auto& Worker : Sim.entities())
        {
            if (!Worker.alive() || Worker.team != 0 || Worker.kind != cinder::Kind::Worker || Worker.order == cinder::Order::Construct) continue;
            const float Distance = FMath::Square(Worker.pos.x - Site->pos.x) + FMath::Square(Worker.pos.y - Site->pos.y);
            if (Distance < Nearest || (Distance == Nearest && Worker.id < Builder)) { Builder = Worker.id; Nearest = Distance; }
        }
        if (!Builder) { Notify(TEXT("No available Drudge. Free a builder, then choose Assign Drudge again.")); return; }
        cinder::Command C; C.type = cinder::CommandType::ResumeConstruction; C.target = Site->id; C.units = { Builder }; Issue(C);
    }
    else if (Action == TEXT("save"))
        Notify(Battle->Campaign().IsActive() ? TEXT("Campaign objectives are checkpointed separately. Your skirmish save is kept.")
            : Battle->Tutorial().IsActive() ? TEXT("Training is not saved. Your skirmish save is kept.")
            : Battle->SaveMatch() ? TEXT("Match saved on this device") : TEXT("Could not save match"));
    else if (Action == TEXT("load"))
    {
        if (Battle->Tutorial().IsActive()) Notify(TEXT("End training, then choose CONTINUE SAVE from the menu."));
        else if (Battle->LoadMatch()) { bCampaignMenuOpen = false; ResetInteraction(true); Home(); Notify(TEXT("Match restored")); }
        else Notify(TEXT("No readable saved match"));
    }
    else if (Action == TEXT("debug")) bDebug = !bDebug;
    else if (Action == TEXT("minimap"))
    {
        const int X = Argument % 10000, Y = Argument / 10000;
        if (IsEscortCommandMode()) Notify(TEXT("Choose an owned mobile unit on the battlefield to escort."));
        else if (HasDestinationMode()) IssueDestination({static_cast<float>(X), static_cast<float>(Y)});
        else if (Rig) { Rig->Focus(FVector(X, Y, 0)); if (IsGameplayActive()) Battle->GuidanceCameraInput(); }
    }
}
