#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Sim/Simulation.h"
#include "CinderPlayerController.generated.h"

class ACinderBattlefield;
class ACinderCamera;

UCLASS()
class CINDERLINE_API ACinderPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaSeconds) override;
    virtual void SetupInputComponent() override;
    ACinderBattlefield* Battlefield() const;
    const std::vector<cinder::Id>& Selection() const { return Selected; }
    bool GroundPoint(FVector2D Screen, cinder::Vec2& Out) const;
    bool IsSelecting() const { return bPointerDown && bDragging && bGestureSelect; }
    FVector2D SelectionStart() const { return PointerStart; }
    FVector2D SelectionEnd() const { return PointerLast; }
    bool IsBuildMode() const { return bBuildMode; }
    cinder::Kind BuildingKind() const { return PendingBuilding; }
    cinder::Vec2 PlacementPoint() const { return Placement; }
    const FString& Feedback() const { return FeedbackText; }
    void ExecuteAction(const FString& Action, int Argument = 0);
    void SelectKind(cinder::Kind Kind);
    void SelectArmy();
    void Notify(const FString& Message);
    bool bBuildMenu = false;
    bool bAttackMove = false;
    bool bBoxSelect = false;
    bool bDebug = false;

private:
    void PointerPressed(FVector2D Position, bool Touch);
    void PointerMoved(FVector2D Position, float DeltaSeconds);
    void PointerReleased(FVector2D Position);
    void TapWorld(FVector2D Position, bool ForceCommand = false);
    void PanScreen(FVector2D Previous, FVector2D Current);
    void Issue(cinder::Command Command);
    void SelectRectangle();
    void Home();
    void Escape();
    void AttackMode();
    void Stop();
    void Hold();
    void ToggleBuild();
    void ZoomIn();
    void ZoomOut();
    void FocusSelection();
    void TouchPressed(ETouchIndex::Type Finger, FVector Location);
    void TouchReleased(ETouchIndex::Type Finger, FVector Location);
    void MousePressed();
    void MouseReleased();
    void MouseContext();
    UPROPERTY() TObjectPtr<ACinderBattlefield> Battle;
    UPROPERTY() TObjectPtr<ACinderCamera> Rig;
    std::vector<cinder::Id> Selected;
    bool bPointerDown = false, bDragging = false, bGestureSelect = false, bPointerTouch = false;
    bool bPointerUI = false, bMultiTouch = false, bTwoDown = false, bMousePan = false;
    bool bBuildMode = false;
    FVector2D PointerStart, PointerLast, PreviousCentroid, PreviousMouse;
    float PointerHeld = 0, PreviousPinch = 0, LastTapTime = -1, FeedbackLife = 0;
    cinder::Kind LastTapKind = cinder::Kind::Resource;
    cinder::Kind PendingBuilding = cinder::Kind::Foundry;
    cinder::Vec2 Placement;
    FString FeedbackText;
};
