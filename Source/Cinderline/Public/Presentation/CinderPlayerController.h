#pragma once
#include <array>
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Sim/AIDifficulty.h"
#include "Sim/MatchLength.h"
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
#if UE_BUILD_DEVELOPMENT
    const TSharedPtr<SWidget>& OnlinePanelForPreview() const { return OnlinePanel; }
#endif
    bool IsOnlineLeavePending() const { return bOnlineLeavePending; }
    bool IsOnlineSurrenderPending() const { return bOnlineSurrenderPending; }
    const std::vector<cinder::Id>& Selection() const { return Selected; }
    bool GroundPoint(FVector2D Screen, cinder::Vec2& Out) const;
    bool ProjectTutorialTarget(cinder::Id Id, cinder::Vec2 Point, FVector2D& Out) const;
    bool IsSelecting() const { return bPointerDown && bDragging && bGestureSelect && !bPointerPlacement; }
    FVector2D SelectionStart() const { return PointerStart; }
    FVector2D SelectionEnd() const { return PointerLast; }
    bool IsBuildMode() const { return bBuildMode; }
    bool IsPlacementGestureActive() const { return bPointerDown && bPointerPlacement; }
    bool IsGlobalBuildMode() const { return bBuildMode && bAutomaticBuild; }
    bool IsAttackMoveMode() const { return DestinationMode == EDestinationMode::AttackMove; }
    bool IsMoveCommandMode() const { return DestinationMode == EDestinationMode::Move; }
    bool IsDefendCommandMode() const { return DestinationMode == EDestinationMode::Defend; }
    bool IsProductionRallyMode() const { return DestinationMode == EDestinationMode::ProductionRally; }
    cinder::Id ProductionRallyProducer() const { return RallyProducer; }
    cinder::CommandResult BuildPlacementStatus(const cinder::Vec2* Site = nullptr) const;
    cinder::Kind BuildingKind() const { return PendingBuilding; }
    cinder::Vec2 PlacementPoint() const { return Placement; }
    const FString& Feedback() const { return FeedbackText; }
    void ExecuteAction(const FString& Action, int Argument = 0);
    void BeginGlobalBuild(cinder::Kind Kind);
    void QueueTraining(cinder::Kind Kind, int Quantity, cinder::Id Producer = 0);
    void QueueResearch(int Upgrade, cinder::Id Producer = 0);
    void CancelProduction(cinder::Id Producer, cinder::Id Job);
    void CancelConstruction(cinder::Id Site);
    void BeginProductionRally(cinder::Id Producer = 0);
    void UseDefaultProductionRally(cinder::Id Producer);
    void FocusArmyRally();
    void FocusProduction(cinder::Id Producer);
    static constexpr int32 SquadCount = 3;
    bool SelectOwnedEntity(cinder::Id Id);
    void SelectOwnedKind(cinder::Kind Kind);
    void SelectKind(cinder::Kind Kind);
    void SelectArmy();
    bool AssignSquad(int32 Index);
    bool RecallSquad(int32 Index);
    const std::vector<cinder::Id>& Squad(int32 Index) const;
    void Notify(const FString& Message);
    bool IsHelpOpen() const { return bHelpOpen; }
    int32 HelpPage() const { return CurrentHelpPage; }
    int32 HelpReference() const { return CurrentHelpReference; }
    bool HelpUsesTouch() const { return bHelpTouch; }
    bool HasCompletedTutorial() const { return bTutorialCompleted; }
    bool IsTutorialRestartPending() const { return bTutorialRestartPending; }
    bool IsTutorialOfferPending() const { return !bTutorialCompleted && !bTutorialOfferResolved; }
    void LoadTutorialPreference(const FString& Filename);
    void SaveTutorialPreference(const FString& Filename) const;
    cinder::AIDifficulty SelectedAIDifficulty() const { return MenuDifficulty; }
    cinder::MatchLength SelectedMatchLength() const { return MenuMatchLength; }
    bool bBuildMenu = false;
    bool bBoxSelect = false;
    bool bDebug = false;

private:
    friend class FCinderWorldLifecycleIntegration;
    friend class FCinderDifficultyIntegration;
    friend class FCinderOnboardingIntegration;
    friend class FCinderArmyControlIntegration;
    friend class FCinderWorldTapIntegration;
    friend class FCinderProductionControlIntegration;
    friend class FCinderOnlineRecoveryIntegration;
    enum class EDestinationMode : uint8
    {
        None,
        AttackMove,
        Move,
        Defend,
        ProductionRally,
    };
    struct FProjectedPickCandidate
    {
        cinder::Id Id = 0;
        FVector2D Center = FVector2D::ZeroVector;
        float Radius = 0;
    };
    static cinder::Id PickProjectedEntity(FVector2D Point,
        const std::vector<FProjectedPickCandidate>& Candidates);
    const cinder::Entity* PickEntityAtScreen(FVector2D Point) const;
    bool IsOwnedSelectable(cinder::Id Id) const;
    void PruneArmyControlState();
    void PointerPressed(FVector2D Position, bool Touch, bool CameraPan = false);
    void PointerMoved(FVector2D Position, float DeltaSeconds);
    void PointerReleased(FVector2D Position);
    void TapWorld(FVector2D Position, bool ForceCommand = false);
    void HandleWorldTap(cinder::Id HitId, const cinder::Vec2* Ground, bool ForceCommand = false);
    bool SelectTappedEntity(cinder::Id Id);
    void ClearPointerTapIntent();
    void PanScreen(FVector2D Previous, FVector2D Current);
    bool Issue(cinder::Command Command);
    bool IssueDestination(cinder::Vec2 Point);
    bool HasDestinationMode() const { return DestinationMode != EDestinationMode::None; }
    bool HasUnitDestinationMode() const
    {
        return DestinationMode == EDestinationMode::AttackMove
            || DestinationMode == EDestinationMode::Move
            || DestinationMode == EDestinationMode::Defend;
    }
    void ToggleDestinationMode(EDestinationMode Mode);
    void ClearDestinationModes();
    bool IsGameplayActive() const;
    void ResetInteraction(bool bClearSelection);
    void SelectRectangle();
    void Home();
    void Escape();
    void Confirm();
    void AttackMode();
    void MoveMode();
    void DefendMode();
    void Stop();
    void Hold();
    void ToggleHelp();
    void TutorialShortcut();
    void HelpSectionShortcut();
    void OpenHelp(int32 Page);
    void BeginTutorial();
    void ResolveTutorialOffer(bool bBeginTutorial);
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
    void SquadOne();
    void SquadTwo();
    void SquadThree();
    void SquadShortcut(int32 Index);
    void TouchPressed(ETouchIndex::Type Finger, FVector Location);
    void TouchReleased(ETouchIndex::Type Finger, FVector Location);
    void ApplicationWillEnterBackground();
    void ApplicationHasEnteredForeground();
    void MousePressed();
    void MouseReleased();
    void MouseContext();
    void LoadDifficultyPreference(const FString& Filename);
    void SaveDifficultyPreference(const FString& Filename) const;
    void LoadMatchLengthPreference(const FString& Filename);
    void SaveMatchLengthPreference(const FString& Filename) const;
    UPROPERTY() TObjectPtr<ACinderBattlefield> Battle;
    UPROPERTY() TObjectPtr<ACinderCamera> Rig;
    std::vector<cinder::Id> Selected;
    std::array<std::vector<cinder::Id>, SquadCount> Squads;
    bool bPointerDown = false, bDragging = false, bGestureSelect = false, bPointerTouch = false;
    bool bPointerUI = false, bMultiTouch = false, bTwoDown = false, bMousePan = false;
    bool bLongPress = false;
    bool bPointerCameraPan = false;
    bool bPointerPlacement = false;
    bool bBuildMode = false;
    bool bAutomaticBuild = false;
    EDestinationMode DestinationMode = EDestinationMode::None;
    cinder::Id RallyProducer = 0;
    FVector2D PointerStart, PointerLast, PreviousCentroid, PreviousMouse;
    float PointerHeld = 0, PreviousPinch = 0, LastTapTime = -1, FeedbackLife = 0;
    cinder::Id LastTapEntity = 0;
    cinder::Id PointerSelectionTarget = 0;
    cinder::Id PointerContextTarget = 0;
    bool bPointerWorldTapLatched = false;
    cinder::Kind PendingBuilding = cinder::Kind::Foundry;
    cinder::Vec2 Placement;
    FString FeedbackText;
    bool bHelpOpen = false, bTutorialRestartPending = false;
    bool bHelpTouch = PLATFORM_IOS || PLATFORM_ANDROID;
    bool bTutorialCompleted = false;
    bool bTutorialOfferResolved = false;
    cinder::AIDifficulty MenuDifficulty = cinder::AIDifficulty::Normal;
    cinder::MatchLength MenuMatchLength = cinder::MatchLength::Standard;
    bool bOnlineLeavePending = false, bOnlineSurrenderPending = false;
    bool bOnlineEliminationObserved = false;
    TSharedPtr<SWidget> OnlinePanel;
    uint64 OnlineFeedbackSerial = 0;
    int32 CurrentHelpPage = 0, CurrentHelpReference = 0;
    float ArrowPanCredit[4] = { 0, 0, 0, 0 };
};
