#include "Presentation/CinderPlayerController.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderHUD.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Engine/World.h"
#include "Components/InputComponent.h"
#include <algorithm>

namespace
{
float TouchScale(const APlayerController* Controller)
{
    int W = 0, H = 0; Controller->GetViewportSize(W, H);
    return FMath::Max(1.0f, FMath::Min(W / 667.0f, H / 375.0f));
}
}

void ACinderPlayerController::BeginPlay()
{
    Super::BeginPlay();
    bShowMouseCursor = true;
    bEnableTouchEvents = true;
    SetInputMode(FInputModeGameOnly());
    Rig = Cast<ACinderCamera>(GetPawn());
    for (TActorIterator<ACinderBattlefield> It(GetWorld()); It; ++It) { Battle = *It; break; }
}

ACinderBattlefield* ACinderPlayerController::Battlefield() const { return Battle; }

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
    InputComponent->BindKey(EKeys::A, IE_Pressed, this, &ACinderPlayerController::AttackMode);
    InputComponent->BindKey(EKeys::S, IE_Pressed, this, &ACinderPlayerController::Stop);
    InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ACinderPlayerController::Hold);
    InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ACinderPlayerController::ToggleBuild);
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ACinderPlayerController::FocusSelection);
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
    cinder::Vec2 A, B;
    if (Rig && GroundPoint(Previous, A) && GroundPoint(Current, B)) Rig->Pan(FVector(A.x - B.x, A.y - B.y, 0));
}

void ACinderPlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    if (!Rig) Rig = Cast<ACinderCamera>(GetPawn());
    if (!Battle) for (TActorIterator<ACinderBattlefield> It(GetWorld()); It; ++It) { Battle = *It; break; }
    if (!Battle || !Rig) return;
    FeedbackLife -= DeltaSeconds;
    if (FeedbackLife <= 0) FeedbackText.Empty();
    Selected.erase(std::remove_if(Selected.begin(), Selected.end(), [&](cinder::Id Id) { const auto* E = Battle->Sim().find(Id); return !E || !E->alive(); }), Selected.end());

    float X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;
    bool Down1 = false, Down2 = false;
    GetInputTouchState(ETouchIndex::Touch1, X1, Y1, Down1);
    GetInputTouchState(ETouchIndex::Touch2, X2, Y2, Down2);
    if (bPointerDown && ((bPointerTouch && !Down1) || (!bPointerTouch && !IsInputKeyDown(EKeys::LeftMouseButton))))
    {
        // Focus loss may suppress release callbacks; cancel rather than issuing a stale order.
        bPointerDown = false; bDragging = false; bPointerTouch = false;
    }
    if (Down1 && Down2)
    {
        const FVector2D Centroid((X1 + X2) * 0.5f, (Y1 + Y2) * 0.5f);
        const float Pinch = FVector2D::Distance(FVector2D(X1, Y1), FVector2D(X2, Y2));
        if (bTwoDown && !bPointerUI && !Battle->IsMenu() && !Battle->IsPaused())
        {
            PanScreen(PreviousCentroid, Centroid);
            Rig->Zoom((PreviousPinch - Pinch) * Rig->Distance() / FMath::Max(PreviousPinch, 60.0f * TouchScale(this)));
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
        if (PanHeld && bMousePan && !Battle->IsMenu() && !Battle->IsPaused()) PanScreen(PreviousMouse, Mouse);
        bMousePan = PanHeld; PreviousMouse = Mouse;
        if (bBuildMode && !bPointerTouch) GroundPoint(Mouse, Placement);
    }
    if (!Battle->IsMenu() && !Battle->IsPaused())
    {
        FVector Pan = FVector::ZeroVector;
        const float Speed = Rig->Distance() * DeltaSeconds;
        if (IsInputKeyDown(EKeys::Up)) Pan += FVector(1, -1, 0) * Speed;
        if (IsInputKeyDown(EKeys::Down)) Pan += FVector(-1, 1, 0) * Speed;
        if (IsInputKeyDown(EKeys::Left)) Pan += FVector(-1, -1, 0) * Speed;
        if (IsInputKeyDown(EKeys::Right)) Pan += FVector(1, 1, 0) * Speed;
        Rig->Pan(Pan);
    }
}

void ACinderPlayerController::PointerPressed(FVector2D Position, bool Touch)
{
    bPointerDown = true; bPointerTouch = Touch;
    PointerStart = PointerLast = Position; PointerHeld = 0; bDragging = false;
    bGestureSelect = !Touch || bBoxSelect;
    auto* HUD = Cast<ACinderHUD>(GetHUD());
    bPointerUI = HUD && HUD->ContainsUI(Position);
    GroundPoint(Position, Placement);
}

void ACinderPlayerController::PointerMoved(FVector2D Position, float DeltaSeconds)
{
    if (!bPointerDown || bMultiTouch) return;
    PointerHeld += DeltaSeconds;
    if (bPointerTouch && !bDragging && !bPointerUI && PointerHeld > 0.42f)
    {
        bGestureSelect = true;
        Notify(TEXT("Drag to select an army"));
    }
    const float Threshold = bPointerTouch ? 10.0f * TouchScale(this) : 7.0f;
    if (FVector2D::Distance(Position, PointerStart) > Threshold) bDragging = true;
    if (bDragging && !bGestureSelect && !bPointerUI && Battle && !Battle->IsMenu() && !Battle->IsPaused()) PanScreen(PointerLast, Position);
    PointerLast = Position;
    GroundPoint(Position, Placement);
}

void ACinderPlayerController::PointerReleased(FVector2D Position)
{
    if (!bPointerDown) return;
    PointerLast = Position;
    if (!bMultiTouch)
    {
        if (bPointerUI)
        {
            if (!bDragging && FVector2D::Distance(Position, PointerStart) < (bPointerTouch ? 12.0f * TouchScale(this) : 15.0f))
                if (auto* HUD = Cast<ACinderHUD>(GetHUD())) HUD->HandleTap(PointerStart);
        }
        else if (Battle && !Battle->IsMenu() && !Battle->IsPaused() && Battle->Sim().winner() < 0)
        {
            if (bDragging && bGestureSelect) SelectRectangle();
            else if (!bDragging) TapWorld(Position);
        }
    }
    bPointerDown = false; bDragging = false; bPointerTouch = false;
}

void ACinderPlayerController::MousePressed() { float X, Y; if (GetMousePosition(X, Y)) PointerPressed(FVector2D(X, Y), false); }
void ACinderPlayerController::MouseReleased()
{
    float X, Y;
    if (GetMousePosition(X, Y)) PointerReleased(FVector2D(X, Y));
    else { bPointerDown = false; bDragging = false; }
}
void ACinderPlayerController::MouseContext()
{
    if (!Battle || Battle->IsMenu() || Battle->IsPaused() || Battle->Sim().winner() >= 0) return;
    if (bBuildMode) { bBuildMode = false; return; }
    float X, Y; if (GetMousePosition(X, Y))
    {
        auto* HUD = Cast<ACinderHUD>(GetHUD());
        if (!HUD || !HUD->ContainsUI(FVector2D(X, Y))) TapWorld(FVector2D(X, Y), true);
    }
}
void ACinderPlayerController::TouchPressed(ETouchIndex::Type Finger, FVector Location)
{
    if (Finger == ETouchIndex::Touch1) PointerPressed(FVector2D(Location.X, Location.Y), true);
    else bMultiTouch = true;
}
void ACinderPlayerController::TouchReleased(ETouchIndex::Type Finger, FVector Location)
{
    if (Finger == ETouchIndex::Touch1) PointerReleased(FVector2D(Location.X, Location.Y));
}

void ACinderPlayerController::SelectRectangle()
{
    if (!Battle) return;
    const FBox2D Bounds(FVector2D(FMath::Min(PointerStart.X, PointerLast.X), FMath::Min(PointerStart.Y, PointerLast.Y)), FVector2D(FMath::Max(PointerStart.X, PointerLast.X), FMath::Max(PointerStart.Y, PointerLast.Y)));
    Selected.clear();
    for (const auto& E : Battle->Sim().entities())
    {
        if (!E.alive() || E.team != 0 || cinder::definition(E.kind).building) continue;
        FVector2D Screen;
        if (ProjectWorldLocationToScreen(FVector(E.pos.x, E.pos.y, cinder::definition(E.kind).air ? 125 : 25), Screen) && Bounds.IsInside(Screen)) Selected.push_back(E.id);
    }
    bBoxSelect = false;
    Notify(FString::Printf(TEXT("%d units selected"), static_cast<int>(Selected.size())));
}

void ACinderPlayerController::SelectKind(cinder::Kind Kind)
{
    if (!Battle) return;
    Selected.clear();
    int W, H; GetViewportSize(W, H);
    for (const auto& E : Battle->Sim().entities())
    {
        if (E.team != 0 || E.kind != Kind || !E.alive()) continue;
        FVector2D Screen;
        if (ProjectWorldLocationToScreen(FVector(E.pos.x, E.pos.y, cinder::definition(E.kind).air ? 125 : 25), Screen) && Screen.X >= 0 && Screen.Y >= 0 && Screen.X <= W && Screen.Y <= H) Selected.push_back(E.id);
    }
}
void ACinderPlayerController::SelectArmy()
{
    if (!Battle) return;
    Selected.clear();
    for (const auto& E : Battle->Sim().entities())
        if (E.team == 0 && E.alive() && !cinder::definition(E.kind).building && E.kind != cinder::Kind::Worker) Selected.push_back(E.id);
    Notify(FString::Printf(TEXT("Army selected: %d"), static_cast<int>(Selected.size())));
}

void ACinderPlayerController::TapWorld(FVector2D Position, bool ForceCommand)
{
    if (!Battle) return;
    cinder::Vec2 Ground;
    if (!GroundPoint(Position, Ground)) return;
    if (Ground.x < 0 || Ground.y < 0 || Ground.x > cinder::Simulation::WorldSize || Ground.y > cinder::Simulation::WorldSize) return;
    if (bBuildMode)
    {
        cinder::Command Cmd; Cmd.type = cinder::CommandType::Build; Cmd.kind = PendingBuilding; Cmd.point = Ground;
        Cmd.team = 0; Cmd.units = Selected;
        const auto Result = Battle->Sim().command(Cmd);
        Notify(UTF8_TO_TCHAR(Result.message.c_str()));
        if (Result.accepted) { bBuildMode = false; bBuildMenu = false; }
        return;
    }
    const cinder::Entity* Hit = nullptr;
    float Best = TNumericLimits<float>::Max();
    const float PointerRadius = bPointerTouch ? 22.0f * TouchScale(this) : 24.0f;
    for (const auto& E : Battle->Sim().entities())
    {
        if (!E.alive() || (E.team != 0 && !Battle->Sim().visible(0, E.pos))) continue;
        FVector2D Screen;
        const float Z = cinder::definition(E.kind).air ? 125 : 25;
        if (ProjectWorldLocationToScreen(FVector(E.pos.x, E.pos.y, Z), Screen))
        {
            const float D = FVector2D::Distance(Screen, Position);
            FVector2D EdgeX, EdgeY;
            float HitRadius = PointerRadius;
            const float Radius = cinder::definition(E.kind).radius;
            if (ProjectWorldLocationToScreen(FVector(E.pos.x + Radius, E.pos.y, Z), EdgeX) && ProjectWorldLocationToScreen(FVector(E.pos.x, E.pos.y + Radius, Z), EdgeY))
                HitRadius = FMath::Max(HitRadius, static_cast<float>(FMath::Max(FVector2D::Distance(Screen, EdgeX), FVector2D::Distance(Screen, EdgeY))));
            if (D < HitRadius && D < Best) { Hit = &E; Best = D; }
        }
    }
    if (Hit && Hit->team == 0 && Hit->kind != cinder::Kind::Resource && !ForceCommand && !bAttackMove)
    {
        const float Now = GetWorld()->GetTimeSeconds();
        if (Hit->kind == LastTapKind && Now - LastTapTime < 0.33f) SelectKind(Hit->kind);
        else Selected = { Hit->id };
        LastTapKind = Hit->kind; LastTapTime = Now;
        bBuildMenu = false;
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
    if (!Battle || Battle->IsMenu() || Battle->IsPaused() || Battle->Sim().winner() >= 0) return;
    Command.team = 0;
    if (Command.units.empty()) Command.units = Selected;
    const auto Result = Battle->Sim().command(Command);
    Notify(UTF8_TO_TCHAR(Result.message.c_str()));
}
void ACinderPlayerController::Notify(const FString& Message) { FeedbackText = Message; FeedbackLife = 3.5f; }
void ACinderPlayerController::Home()
{
    if (!Rig || !Battle) return;
    for (const auto& E : Battle->Sim().entities()) if (E.team == 0 && E.kind == cinder::Kind::Headquarters && E.alive()) { Rig->Focus(FVector(E.pos.x, E.pos.y, 0)); return; }
}
void ACinderPlayerController::FocusSelection()
{
    if (!Battle || !Rig || Selected.empty()) return;
    FVector Sum = FVector::ZeroVector; int Count = 0;
    for (auto Id : Selected) if (const auto* E = Battle->Sim().find(Id)) { Sum += FVector(E->pos.x, E->pos.y, 0); ++Count; }
    if (Count) Rig->Focus(Sum / Count);
}
void ACinderPlayerController::Escape()
{
    if (bBuildMode || bBuildMenu || bAttackMove || bBoxSelect) { bBuildMode = bBuildMenu = bAttackMove = bBoxSelect = false; return; }
    if (Battle && !Battle->IsMenu()) Battle->SetPaused(!Battle->IsPaused());
}
void ACinderPlayerController::AttackMode() { bAttackMove = !bAttackMove; bBuildMode = false; }
void ACinderPlayerController::Stop() { cinder::Command C; C.type = cinder::CommandType::Stop; Issue(C); }
void ACinderPlayerController::Hold() { cinder::Command C; C.type = cinder::CommandType::Hold; Issue(C); }
void ACinderPlayerController::ToggleBuild() { bBuildMenu = !bBuildMenu; bBuildMode = false; }
void ACinderPlayerController::ZoomIn() { if (Rig) Rig->Zoom(-180); }
void ACinderPlayerController::ZoomOut() { if (Rig) Rig->Zoom(180); }

void ACinderPlayerController::ExecuteAction(const FString& Action, int Argument)
{
    if (!Battle) return;
    if (Action == TEXT("start")) { Battle->StartMatch(Argument); Selected.clear(); bBuildMenu = bBuildMode = bAttackMove = bBoxSelect = false; Home(); Notify(TEXT("Select a Drudge, then tap amber ore. Build a Kiln to raise your army.")); }
    else if (Action == TEXT("menu")) { Battle->ReturnToMenu(); Selected.clear(); }
    else if (Action == TEXT("pause")) Escape();
    else if (Action == TEXT("resume")) Battle->SetPaused(false);
    else if (Action == TEXT("home")) Home();
    else if (Action == TEXT("focus")) FocusSelection();
    else if (Action == TEXT("army")) SelectArmy();
    else if (Action == TEXT("box")) { bBoxSelect = !bBoxSelect; Notify(TEXT("Drag across units to select. Two fingers pan and zoom.")); }
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
    else if (Action == TEXT("build")) { PendingBuilding = static_cast<cinder::Kind>(Argument); bBuildMode = true; bAttackMove = false; Notify(TEXT("Tap a clear, explored site near your workers. Cancel to exit placement.")); }
    else if (Action == TEXT("cancelplacement")) { bBuildMode = false; bBuildMenu = false; }
    else if (Action == TEXT("train")) { cinder::Command C; C.type = cinder::CommandType::Train; C.kind = static_cast<cinder::Kind>(Argument); Issue(C); }
    else if (Action == TEXT("research")) { cinder::Command C; C.type = cinder::CommandType::Research; C.queueIndex = Argument; Issue(C); }
    else if (Action == TEXT("cancelqueue")) { cinder::Command C; C.type = cinder::CommandType::CancelQueue; C.queueIndex = Argument; Issue(C); }
    else if (Action == TEXT("cancelbuilding")) { cinder::Command C; C.type = cinder::CommandType::CancelBuilding; Issue(C); }
    else if (Action == TEXT("save")) Notify(Battle->SaveMatch() ? TEXT("Match saved on this device") : TEXT("Could not save match"));
    else if (Action == TEXT("load")) { if (Battle->LoadMatch()) { Selected.clear(); Home(); Notify(TEXT("Match restored")); } else Notify(TEXT("No readable saved match")); }
    else if (Action == TEXT("debug")) bDebug = !bDebug;
    else if (Action == TEXT("minimap")) { const int X = Argument % 10000, Y = Argument / 10000; if (Rig) Rig->Focus(FVector(X, Y, 0)); }
}
