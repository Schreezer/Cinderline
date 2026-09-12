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
    if (GConfig && !FApp::IsUnattended() && !GIsAutomationTesting)
        GConfig->GetBool(TEXT("Training"), TEXT("Completed"), bTutorialCompleted, TutorialPreferencesPath());
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

void ACinderPlayerController::OpenOnlinePanel()
{
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
    auto* Session = Online();
    if (!Session || !Battle) return;
    if (Session->HasMatch() && Session->LatestSnapshot() && !Battle->IsOnlineMatch())
    {
        if (Battle->StartOnlineMatch(*Session->LatestSnapshot()))
        {
            CloseOnlinePanel(); bHelpOpen = bTutorialRestartPending = bOnlineLeavePending = false;
            ResetInteraction(true); OnlineFeedbackSerial = Session->FeedbackSerial();
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
    InputComponent->BindKey(EKeys::S, IE_Pressed, this, &ACinderPlayerController::Stop);
    InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ACinderPlayerController::Hold);
    InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ACinderPlayerController::ToggleBuild);
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ACinderPlayerController::FocusSelection);
    InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &ACinderPlayerController::ArrowUp);
    InputComponent->BindKey(EKeys::Down, IE_Pressed, this, &ACinderPlayerController::ArrowDown);
    InputComponent->BindKey(EKeys::Left, IE_Pressed, this, &ACinderPlayerController::ArrowLeft);
    InputComponent->BindKey(EKeys::Right, IE_Pressed, this, &ACinderPlayerController::ArrowRight);
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
        if (FVector2D::DistSquared(Previous, Current) > 1) Battle->Tutorial().CameraInput();
    }
}

void ACinderPlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    if (!Rig) Rig = Cast<ACinderCamera>(GetPawn());
    if (!Battle)
        if (TActorIterator<ACinderBattlefield> It(GetWorld()); It) Battle = *It;
    PollOnlineState();
    if (!Battle || !Rig) return;
    FeedbackLife -= DeltaSeconds;
    if (FeedbackLife <= 0) FeedbackText.Empty();
    Selected.erase(std::remove_if(Selected.begin(), Selected.end(), [&](cinder::Id Id) { const auto* E = Battle->Sim().find(Id); return !E || !E->alive(); }), Selected.end());
    UpdateTutorial();

    float X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;
    bool Down1 = false, Down2 = false;
    GetInputTouchState(ETouchIndex::Touch1, X1, Y1, Down1);
    GetInputTouchState(ETouchIndex::Touch2, X2, Y2, Down2);
    if (bPointerDown && ((bPointerTouch && !Down1) || (!bPointerTouch && !IsInputKeyDown(EKeys::LeftMouseButton))))
    {
        // Focus loss may suppress release callbacks; cancel rather than issuing a stale order.
        bPointerDown = false; bDragging = false; bPointerTouch = false; bLongPress = false;
        bPointerCameraPan = bPointerPlacement = false;
    }
    if (Down1 && Down2)
    {
        const FVector2D Centroid((X1 + X2) * 0.5f, (Y1 + Y2) * 0.5f);
        const float Pinch = FVector2D::Distance(FVector2D(X1, Y1), FVector2D(X2, Y2));
        if (bTwoDown && !bPointerUI && IsGameplayActive())
        {
            PanScreen(PreviousCentroid, Centroid);
            Rig->Zoom((PreviousPinch - Pinch) * Rig->Distance() / FMath::Max(PreviousPinch, 60.0f * TouchScale(this)));
            if (FMath::Abs(PreviousPinch - Pinch) > 1) Battle->Tutorial().CameraInput();
        }
        PreviousCentroid = Centroid; PreviousPinch = Pinch;
        bTwoDown = true; bMultiTouch = true;
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
    Battle->Tutorial().CameraInput();
    ArrowPanCredit[Direction] = NudgeSeconds;
}

void ACinderPlayerController::PointerPressed(FVector2D Position, bool Touch, bool CameraPan)
{
    UE_LOG(LogCinderInput, Verbose, TEXT("Press at %.1f,%.1f touch=%d"), Position.X, Position.Y, Touch);
    bPointerDown = true; bPointerTouch = Touch;
    PointerStart = PointerLast = Position; PointerHeld = 0; bDragging = false; bLongPress = false;
    auto* HUD = Cast<ACinderHUD>(GetHUD());
    bPointerUI = OnlinePanel.IsValid() || bOnlineLeavePending || bHelpOpen || bTutorialRestartPending || (HUD && HUD->ContainsUI(Position));
    // Latch the intent at mouse-down. Releasing Option/Alt before the button
    // must never turn a camera gesture into selection, an order or placement.
    bPointerCameraPan = !Touch && CameraPan && !bPointerUI && IsGameplayActive();
    bPointerPlacement = !bPointerCameraPan && !bPointerUI && bBuildMode && IsGameplayActive();
    bGestureSelect = !bPointerCameraPan && (!Touch || bBoxSelect);
    GroundPoint(Position, Placement);
}

void ACinderPlayerController::PointerMoved(FVector2D Position, float DeltaSeconds)
{
    if (!bPointerDown || bMultiTouch) return;
    PointerHeld += DeltaSeconds;
    if (bPointerTouch && !bDragging && !bGestureSelect && !bPointerUI && !bPointerPlacement && IsGameplayActive() && PointerHeld > 0.42f)
    {
        bLongPress = true;
        bGestureSelect = true;
        Notify(TEXT("Drag to select an army"));
    }
    const float Threshold = bPointerTouch ? 10.0f * TouchScale(this) : 7.0f * MouseScale(this);
    if (FVector2D::Distance(Position, PointerStart) > Threshold) bDragging = true;
    if (bDragging && !bGestureSelect && !bPointerUI && IsGameplayActive()) PanScreen(PointerLast, Position);
    PointerLast = Position;
    GroundPoint(Position, Placement);
}

void ACinderPlayerController::PointerReleased(FVector2D Position)
{
    UE_LOG(LogCinderInput, Verbose, TEXT("Release at %.1f,%.1f down=%d ui=%d dragging=%d multi=%d"), Position.X, Position.Y, bPointerDown, bPointerUI, bDragging, bMultiTouch);
    if (!bPointerDown) return;
    PointerLast = Position;
    if (bPointerCameraPan || (bPointerPlacement && (bDragging || !bBuildMode)) || (bLongPress && !bDragging))
    {
        // Consume even a click below the drag threshold, without changing modes.
        // A placement drag keeps its selected Drudge. A stationary long press
        // arms box selection without issuing the terrain command underneath it.
        bPointerDown = bDragging = bPointerTouch = bLongPress = false;
        bPointerCameraPan = bPointerPlacement = false;
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
    else { bPointerDown = false; bDragging = false; bLongPress = false; bPointerCameraPan = bPointerPlacement = false; }
}
void ACinderPlayerController::MouseContext()
{
    if (!IsGameplayActive()) return;
    if (bBuildMode) { bBuildMode = false; Notify(TEXT("Placement cancelled.")); return; }
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
        bMultiTouch = true; bLongPress = false; bGestureSelect = false;
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
    if (!Selected.empty()) UCinderAudioSubsystem::Play(this, ECinderCue::UI_Click);
    Notify(FString::Printf(TEXT("%d units selected"), static_cast<int>(Selected.size())));
}

void ACinderPlayerController::SelectKind(cinder::Kind Kind)
{
    if (!IsGameplayActive()) return;
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
    Selected.clear();
    for (const auto& E : Battle->Sim().entities())
        if (E.team == 0 && E.alive() && !cinder::definition(E.kind).building && E.kind != cinder::Kind::Worker) Selected.push_back(E.id);
    Notify(FString::Printf(TEXT("Army selected: %d"), static_cast<int>(Selected.size())));
}

void ACinderPlayerController::TapWorld(FVector2D Position, bool ForceCommand)
{
    if (!IsGameplayActive()) return;
    cinder::Vec2 Ground;
    if (!GroundPoint(Position, Ground)) return;
    if (Ground.x < 0 || Ground.y < 0 || Ground.x > cinder::Simulation::WorldSize || Ground.y > cinder::Simulation::WorldSize) return;
    if (bBuildMode)
    {
        cinder::Command Cmd; Cmd.type = cinder::CommandType::Build; Cmd.kind = PendingBuilding; Cmd.point = Ground;
        Cmd.team = 0; Cmd.units = Selected;
        const auto Result = Battle->SubmitCommand(Cmd);
        if (Result.accepted && !Battle->IsOnlineMatch()) Battle->Tutorial().AcceptedCommand(Battle->Sim(), Cmd);
        Notify(UTF8_TO_TCHAR(Result.message.c_str()));
        if (!Battle->IsOnlineMatch() || !Result.accepted) UCinderAudioSubsystem::Play(this, Result.accepted ? ECinderCue::Order_Ack : ECinderCue::Order_Invalid);
        if (Result.accepted) { bBuildMode = false; bBuildMenu = false; }
        return;
    }
    const cinder::Entity* Hit = nullptr;
    float Best = TNumericLimits<float>::Max();
    const float PointerRadius = bPointerTouch ? 22.0f * TouchScale(this) : 24.0f * MouseScale(this);
    for (const auto& E : Battle->Sim().entities())
    {
        if (!E.alive() || (E.team != 0 && !Battle->Sim().visible(0, E.pos))) continue;
        const auto RenderPoint = Battle->RenderPosition(E);
        FVector2D Screen;
        const float Z = cinder::definition(E.kind).air ? 125 : 25;
        if (ProjectWorldLocationToScreen(FVector(RenderPoint.x, RenderPoint.y, Z), Screen))
        {
            const float D = FVector2D::Distance(Screen, Position);
            FVector2D EdgeX, EdgeY;
            float HitRadius = PointerRadius;
            const float Radius = cinder::definition(E.kind).radius;
            if (ProjectWorldLocationToScreen(FVector(RenderPoint.x + Radius, RenderPoint.y, Z), EdgeX) && ProjectWorldLocationToScreen(FVector(RenderPoint.x, RenderPoint.y + Radius, Z), EdgeY))
                HitRadius = FMath::Max(HitRadius, static_cast<float>(FMath::Max(FVector2D::Distance(Screen, EdgeX), FVector2D::Distance(Screen, EdgeY))));
            if (D < HitRadius && D < Best) { Hit = &E; Best = D; }
        }
    }
    const bool bSelectedDrudge = std::any_of(Selected.begin(), Selected.end(), [&](cinder::Id Id)
    {
        const auto* Worker = Battle->Sim().find(Id);
        return Worker && Worker->alive() && Worker->team == 0 && Worker->kind == cinder::Kind::Worker;
    });
    if (Hit && Hit->team == 0 && cinder::definition(Hit->kind).building && Hit->progress < 1 && bSelectedDrudge && !bAttackMove && (ForceCommand || bPointerTouch))
    {
        cinder::Command Cmd; Cmd.type = cinder::CommandType::ResumeConstruction; Cmd.target = Hit->id;
        Issue(Cmd); bBuildMenu = false;
        return;
    }
    if (Hit && Hit->team == 0 && Hit->kind != cinder::Kind::Resource && !ForceCommand && !bAttackMove)
    {
        const float Now = GetWorld()->GetTimeSeconds();
        if (Hit->kind == LastTapKind && Now - LastTapTime < 0.33f) SelectKind(Hit->kind);
        else Selected = { Hit->id };
        LastTapKind = Hit->kind; LastTapTime = Now;
        bBuildMenu = false;
        UCinderAudioSubsystem::Play(this, ECinderCue::UI_Click);
        return;
    }
    if (Selected.empty()) return;
    cinder::Command Cmd; Cmd.point = Ground;
    Cmd.type = bAttackMove ? cinder::CommandType::AttackMove : cinder::CommandType::Move;
    if (Hit && Hit->kind == cinder::Kind::Resource) { Cmd.type = cinder::CommandType::Gather; Cmd.target = Hit->id; }
    else if (Hit && Hit->team == 1) { Cmd.type = cinder::CommandType::Attack; Cmd.target = Hit->id; }
    else if (const auto* First = Battle->Sim().find(Selected.front()); First && cinder::definition(First->kind).building) Cmd.type = cinder::CommandType::Rally;
    Issue(Cmd); bAttackMove = false;
}

void ACinderPlayerController::Issue(cinder::Command Command)
{
    if (!IsGameplayActive()) return;
    Command.team = 0;
    if (Command.units.empty()) Command.units = Selected;
    const auto Result = Battle->SubmitCommand(Command);
    if (Result.accepted && !Battle->IsOnlineMatch()) Battle->Tutorial().AcceptedCommand(Battle->Sim(), Command);
    Notify(UTF8_TO_TCHAR(Result.message.c_str()));
    if (!Battle->IsOnlineMatch() || !Result.accepted) UCinderAudioSubsystem::Play(this, Result.accepted ? ECinderCue::Order_Ack : ECinderCue::Order_Invalid);
}
bool ACinderPlayerController::IsGameplayActive() const
{
    return Battle && !OnlinePanel && !bOnlineLeavePending && !bHelpOpen && !bTutorialRestartPending &&
        !Battle->IsMenu() && !Battle->IsPaused() && Battle->Sim().winner() < 0 &&
        (!Battle->IsOnlineMatch() || (Online() && Online()->CanSendOrders()));
}
void ACinderPlayerController::ResetInteraction(bool bClearSelection)
{
    bBuildMode = bBuildMenu = bAttackMove = bBoxSelect = false;
    bPointerDown = bDragging = bGestureSelect = bPointerTouch = bLongPress = false;
    bPointerUI = bMultiTouch = bTwoDown = bMousePan = false;
    bPointerCameraPan = bPointerPlacement = false;
    PointerStart = PointerLast = PreviousCentroid = PreviousMouse = FVector2D::ZeroVector;
    PointerHeld = PreviousPinch = 0;
    LastTapTime = -1;
    LastTapKind = cinder::Kind::Resource;
    PendingBuilding = cinder::Kind::Foundry;
    Placement = {};
    for (float& Credit : ArrowPanCredit) Credit = 0;
    FeedbackText.Empty(); FeedbackLife = 0;
    if (bClearSelection) Selected.clear();
}
void ACinderPlayerController::Notify(const FString& Message) { FeedbackText = Message; FeedbackLife = 3.5f; }
void ACinderPlayerController::Home()
{
    if (!Rig || !Battle || OnlinePanel || bOnlineLeavePending || bHelpOpen || bTutorialRestartPending) return;
    if (IsGameplayActive()) Battle->Tutorial().CameraInput();
    for (const auto& E : Battle->Sim().entities()) if (E.team == 0 && E.kind == cinder::Kind::Headquarters && E.alive()) { Rig->Focus(FVector(E.pos.x, E.pos.y, 0)); return; }
}
void ACinderPlayerController::FocusSelection()
{
    if (!IsGameplayActive() || !Rig || Selected.empty()) return;
    Battle->Tutorial().CameraInput();
    FVector Sum = FVector::ZeroVector; int Count = 0;
    for (auto Id : Selected) if (const auto* E = Battle->Sim().find(Id)) { Sum += FVector(E->pos.x, E->pos.y, 0); ++Count; }
    if (Count) Rig->Focus(Sum / Count);
}
void ACinderPlayerController::Escape()
{
    if (bOnlineLeavePending) { ExecuteAction(TEXT("onlinecancel")); return; }
    if (bHelpOpen) { ExecuteAction(TEXT("helpclose")); return; }
    if (bTutorialRestartPending) { ExecuteAction(TEXT("tutorialcancel")); return; }
    if (bBuildMode || bBuildMenu || bAttackMove || bBoxSelect) { ResetInteraction(false); return; }
    if (auto* HUD = Cast<ACinderHUD>(GetHUD()); HUD && HUD->CloseCompactSheet()) return;
    if (Battle && !Battle->IsMenu()) { ResetInteraction(false); Battle->SetPaused(!Battle->IsPaused()); }
}
void ACinderPlayerController::Confirm()
{
    if (bOnlineLeavePending) { ExecuteAction(TEXT("onlineconfirm")); return; }
    if (bHelpOpen) { ExecuteAction(TEXT("helpclose")); return; }
    if (bTutorialRestartPending) { ExecuteAction(TEXT("tutorialconfirm")); return; }
    if (!Battle) return;
    if (Battle->IsMenu() || Battle->Sim().winner() >= 0 || Battle->IsPaused()) UCinderAudioSubsystem::Play(this, ECinderCue::UI_Click);
    if (Battle->IsMenu())
    {
        const auto* HUD = Cast<ACinderHUD>(GetHUD());
        ExecuteAction(TEXT("start"), HUD ? HUD->MenuMap() : 0);
    }
    else if (Battle->Sim().winner() >= 0)
        ExecuteAction(Battle->IsOnlineMatch() ? TEXT("onlineleave") : Battle->Tutorial().IsActive() ? TEXT("tutorialrestart") : TEXT("start"), Battle->MapIndex());
    else if (Battle->IsPaused()) ExecuteAction(TEXT("resume"));
}
void ACinderPlayerController::AttackMode() { if (IsGameplayActive()) { bAttackMove = !bAttackMove; bBuildMode = false; } }
void ACinderPlayerController::Stop() { cinder::Command C; C.type = cinder::CommandType::Stop; Issue(C); }
void ACinderPlayerController::Hold() { cinder::Command C; C.type = cinder::CommandType::Hold; Issue(C); }
void ACinderPlayerController::ToggleBuild() { if (IsGameplayActive()) { bBuildMenu = !bBuildMenu; bBuildMode = false; } }
void ACinderPlayerController::ZoomIn()
{
    if (Rig && !OnlinePanel && !bOnlineLeavePending && !bHelpOpen && !bTutorialRestartPending)
    { Rig->Zoom(-180); if (IsGameplayActive()) Battle->Tutorial().CameraInput(); }
}
void ACinderPlayerController::ZoomOut()
{
    if (Rig && !OnlinePanel && !bOnlineLeavePending && !bHelpOpen && !bTutorialRestartPending)
    { Rig->Zoom(180); if (IsGameplayActive()) Battle->Tutorial().CameraInput(); }
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
    if (OnlinePanel || bOnlineLeavePending || bTutorialRestartPending) return;
    if (bHelpOpen) ExecuteAction(TEXT("helpclose")); else OpenHelp(CurrentHelpPage);
}

void ACinderPlayerController::TutorialShortcut()
{
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
    bHelpOpen = bTutorialRestartPending = false;
    ResetInteraction(true);
    Battle->StartTutorial();
    // Establish the starting view without crediting the camera lesson.
    if (Rig)
    {
        Rig->Focus(FVector(600, 600, 0), true);
        Rig->Zoom(1500 - Rig->Distance());
        Rig->Focus(FVector(600, 600, 0), true);
    }
    Notify(TEXT("Training started. No attacking AI. Your saved skirmish is kept."));
}

void ACinderPlayerController::UpdateTutorial()
{
    if (!IsGameplayActive() || !Battle->Tutorial().IsActive()) return;
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
        // Automation must never change the player's completion preference.
        if (GConfig && !FApp::IsUnattended() && !GIsAutomationTesting && GetWorld() && GetWorld()->IsGameWorld())
        {
            const FString Filename = TutorialPreferencesPath();
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
            GConfig->SetBool(TEXT("Training"), TEXT("Completed"), true, Filename);
            GConfig->Flush(false, Filename);
        }
    }
}

void ACinderPlayerController::ExecuteAction(const FString& Action, int Argument)
{
    UE_LOG(LogCinderInput, Verbose, TEXT("Action %s argument=%d"), *Action, Argument);
    if (!Battle) return;
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
                Notify(bSent ? TEXT("Surrender sent. Waiting for the server's result.") : TEXT("Reconnect to surrender, or choose Leave to exit the match."));
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
    if (Action == TEXT("online") || Action == TEXT("connection")) { OpenOnlinePanel(); return; }
    if (Action == TEXT("onlineleave") || Action == TEXT("onlinesurrender"))
    {
        if (!Battle->IsOnlineMatch()) return;
        if (Battle->Sim().winner() >= 0)
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
    if (Action == TEXT("tutorialfocus"))
    {
        cinder::Vec2 Point;
        if (IsGameplayActive() && Rig && Battle->Tutorial().FocusPoint(Battle->Sim(), Point))
        { Rig->Focus(FVector(Point.x, Point.y, 0)); Battle->Tutorial().CameraInput(); }
        return;
    }
    if (Action == TEXT("tutorialend"))
    {
        if (Battle->Tutorial().IsActive()) { Battle->ReturnToMenu(); ResetInteraction(true); }
        return;
    }
    if (Action == TEXT("start")) { Battle->StartMatch(Argument); ResetInteraction(true); Home(); Notify(TEXT("Select a Drudge, then tap amber ore. Build a Kiln to raise your army.")); }
    else if (Action == TEXT("menu")) { Battle->ReturnToMenu(); ResetInteraction(true); }
    else if (Action == TEXT("pause")) { ResetInteraction(false); Battle->SetPaused(true); }
    else if (Action == TEXT("resume")) { ResetInteraction(false); Battle->SetPaused(false); }
    else if (Action == TEXT("home")) Home();
    else if (Action == TEXT("focus")) FocusSelection();
    else if (Action == TEXT("army")) SelectArmy();
    else if (Action == TEXT("workers") && IsGameplayActive())
    {
        ResetInteraction(false);
        SelectKind(cinder::Kind::Worker);
        Notify(Selected.empty() ? TEXT("No Drudges in view. Tap Home to return to your base.")
            : FString::Printf(TEXT("%d Drudges selected"), static_cast<int>(Selected.size())));
    }
    else if (Action == TEXT("box") && IsGameplayActive()) { bBoxSelect = !bBoxSelect; Notify(TEXT("Drag across units to select. Two fingers pan and zoom.")); }
    else if (Action == TEXT("attack")) AttackMode();
    else if (Action == TEXT("stop")) Stop();
    else if (Action == TEXT("hold")) Hold();
    else if (Action == TEXT("buildmenu")) ToggleBuild();
    else if (Action == TEXT("zoom+")) ZoomIn();
    else if (Action == TEXT("zoom-")) ZoomOut();
    else if (Action == TEXT("kind"))
    {
        const auto Kind = static_cast<cinder::Kind>(Argument);
        Selected.erase(std::remove_if(Selected.begin(), Selected.end(), [&](cinder::Id Id) { const auto* E = Battle->Sim().find(Id); return !E || E->kind != Kind; }), Selected.end());
    }
    else if (Action == TEXT("build") && IsGameplayActive())
    {
        const auto Kind = static_cast<cinder::Kind>(Argument);
        const auto Status = Battle->Sim().buildStatus(0, Kind, Selected);
        if (!Status.accepted)
        {
            bBuildMode = false;
            Notify(UTF8_TO_TCHAR(Status.message.c_str()));
            UCinderAudioSubsystem::Play(this, ECinderCue::Order_Invalid);
            return;
        }
        PendingBuilding = Kind; bBuildMode = true; bAttackMove = false;
#if PLATFORM_IOS || PLATFORM_ANDROID
        Notify(TEXT("Tap a clear, explored site near your Drudge. Cancel to exit placement."));
#else
        Notify(TEXT("Click a clear, explored site near your Drudge. Secondary click cancels."));
#endif
    }
    else if (Action == TEXT("cancelplacement")) { bBuildMode = false; bBuildMenu = false; }
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
        if (!Builder) { Notify(TEXT("No available Drudge. Select a builder and tap this site to reassign it.")); return; }
        cinder::Command C; C.type = cinder::CommandType::ResumeConstruction; C.target = Site->id; C.units = { Builder }; Issue(C);
    }
    else if (Action == TEXT("save"))
        Notify(Battle->Tutorial().IsActive() ? TEXT("Training is not saved. Your skirmish save is kept.")
            : Battle->SaveMatch() ? TEXT("Match saved on this device") : TEXT("Could not save match"));
    else if (Action == TEXT("load"))
    {
        if (Battle->Tutorial().IsActive()) Notify(TEXT("End training, then choose CONTINUE SAVE from the menu."));
        else if (Battle->LoadMatch()) { ResetInteraction(true); Home(); Notify(TEXT("Match restored")); }
        else Notify(TEXT("No readable saved match"));
    }
    else if (Action == TEXT("debug")) bDebug = !bDebug;
    else if (Action == TEXT("minimap"))
    {
        const int X = Argument % 10000, Y = Argument / 10000;
        if (Rig) { Rig->Focus(FVector(X, Y, 0)); if (IsGameplayActive()) Battle->Tutorial().CameraInput(); }
    }
}
