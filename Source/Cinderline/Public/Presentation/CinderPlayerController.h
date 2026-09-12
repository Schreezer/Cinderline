#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Sim/Simulation.h"
#include "CinderPlayerController.generated.h"

class ACinderBattlefield;
class ACinderCamera;
class UCinderOnlineSubsystem;
class SWidget;

UCLASS()
class CINDERLINE_API ACinderPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void PlayerTick(float DeltaSeconds) override;
    virtual void SetupInputComponent() override;
    ACinderBattlefield* Battlefield() const;
    UCinderOnlineSubsystem* Online() const;
    bool IsOnlineLeavePending() const { return bOnlineLeavePending; }
    bool IsOnlineSurrenderPending() const { return bOnlineSurrenderPending; }
    const std::vector<cinder::Id>& Selection() const { return Selected; }
    bool GroundPoint(FVector2D Screen, cinder::Vec2& Out) const;
    bool IsSelecting() const { return bPointerDown && bDragging && bGestureSelect && !bPointerPlacement; }
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
    bool IsHelpOpen() const { return bHelpOpen; }
    int32 HelpPage() const { return CurrentHelpPage; }
    int32 HelpReference() const { return CurrentHelpReference; }
    bool HelpUsesTouch() const { return bHelpTouch; }
    bool HasCompletedTutorial() const { return bTutorialCompleted; }
    bool IsTutorialRestartPending() const { return bTutorialRestartPending; }
    bool bBuildMenu = false;
    bool bAttackMove = false;
    bool bBoxSelect = false;
    bool bDebug = false;

private:
    friend class FCinderWorldLifecycleIntegration;
    void PointerPressed(FVector2D Position, bool Touch, bool CameraPan = false);
    void PointerMoved(FVector2D Position, float DeltaSeconds);
    void PointerReleased(FVector2D Position);
    void TapWorld(FVector2D Position, bool ForceCommand = false);
    void PanScreen(FVector2D Previous, FVector2D Current);
    void Issue(cinder::Command Command);
    bool IsGameplayActive() const;
    void ResetInteraction(bool bClearSelection);
    void SelectRectangle();
    void Home();
    void Escape();
    void Confirm();
    void AttackMode();
    void Stop();
    void Hold();
    void ToggleHelp();
    void TutorialShortcut();
    void HelpSectionShortcut();
    void OpenHelp(int32 Page);
    void BeginTutorial();
    void UpdateTutorial();
    void OpenOnlinePanel();
    void CloseOnlinePanel();
    void PollOnlineState();
    void ToggleBuild();
    void ZoomIn();
    void ZoomOut();
    void FocusSelection();
    void ArrowUp();
    void ArrowDown();
    void ArrowLeft();
    void ArrowRight();
    void NudgeArrow(int Direction);
    void TouchPressed(ETouchIndex::Type Finger, FVector Location);
    void TouchReleased(ETouchIndex::Type Finger, FVector Location);
    void ApplicationWillEnterBackground();
    void ApplicationHasEnteredForeground();
    void MousePressed();
    void MouseReleased();
    void MouseContext();
    UPROPERTY() TObjectPtr<ACinderBattlefield> Battle;
    UPROPERTY() TObjectPtr<ACinderCamera> Rig;
    std::vector<cinder::Id> Selected;
    bool bPointerDown = false, bDragging = false, bGestureSelect = false, bPointerTouch = false;
    bool bPointerUI = false, bMultiTouch = false, bTwoDown = false, bMousePan = false;
    bool bLongPress = false;
    bool bPointerCameraPan = false;
    bool bPointerPlacement = false;
    bool bBuildMode = false;
    FVector2D PointerStart, PointerLast, PreviousCentroid, PreviousMouse;
    float PointerHeld = 0, PreviousPinch = 0, LastTapTime = -1, FeedbackLife = 0;
    cinder::Kind LastTapKind = cinder::Kind::Resource;
    cinder::Kind PendingBuilding = cinder::Kind::Foundry;
    cinder::Vec2 Placement;
    FString FeedbackText;
    bool bHelpOpen = false, bTutorialRestartPending = false;
    bool bHelpTouch = PLATFORM_IOS || PLATFORM_ANDROID;
    bool bTutorialCompleted = false;
    bool bOnlineLeavePending = false, bOnlineSurrenderPending = false;
    TSharedPtr<SWidget> OnlinePanel;
    uint64 OnlineFeedbackSerial = 0;
    int32 CurrentHelpPage = 0, CurrentHelpReference = 0;
    float ArrowPanCredit[4] = { 0, 0, 0, 0 };
};
