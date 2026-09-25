#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UObject/SoftObjectPtr.h"
#include "Presentation/CinderMobileHUDLayout.h"
#include "Presentation/CinderTutorial.h"
#include "Sim/Navigation.h"
#include "CinderHUD.generated.h"

class ACinderPlayerController;
class ACinderBattlefield;
class UTexture2D;
class UFont;
struct FSlateFontInfo;
namespace cinder { enum class Kind : int; struct Entity; }

UCLASS()
class CINDERLINE_API ACinderHUD : public AHUD
{
    GENERATED_BODY()
public:
    ACinderHUD();
    virtual void BeginPlay() override;
    virtual void DrawHUD() override;
    bool ContainsUI(FVector2D Point) const;
    bool HandleTap(FVector2D Point);
    void NextHelpSection();
    int MenuMap() const { return SelectedMap; }
    bool CloseCompactSheet();
    void OpenGlobalPanel(FName Panel, cinder::Id Producer = 0);
    FCinderTutorialGuide CurrentTutorialGuide() const;
    bool FocusTutorialTarget();
    bool NeedsTutorialTargetFocus() const { return bTutorialNeedsShow; }
#if UE_BUILD_DEVELOPMENT
    void PreviewMobileLayout(const FString& State);
    void LogMobileLayout() const;
    bool TapPreviewAction(const FString& Action, TOptional<int32> Argument = {}, TOptional<cinder::Id> Entity = {});
    void LogTutorialGuidance() const;
#endif
private:
    friend class FCinderWorldLifecycleIntegration;
    struct FButton
    {
        FBox2D Bounds;
        FString Action;
        int Argument = 0;
        cinder::Id EntityId = 0;
        cinder::Id JobId = 0;
    };
    struct FMinimapFogRun
    {
        int32 Y = 0;
        int32 StartX = 0;
        int32 EndX = 0;
        uint8 State = 0;
    };
    static bool RefreshFogRuns(uint64 SourceRevision, int32 SourceDimension,
        const TArray<uint8>& SourceCells, uint64& CachedRevision, int32& CachedDimension,
        TArray<FMinimapFogRun>& CachedRuns);
    void Panel(float X, float Y, float W, float H, FLinearColor Color);
    void Surface(float X, float Y, float W, float H, FLinearColor Color, float Radius = 8);
    bool ActionGlyph(const FString& Action, int32 Arg, float X, float Y, float Size, FLinearColor Color);
    void ActionButton(const FString& Text, const FString& Action, int Arg, float X, float Y, float W, bool Active = false);
    void Label(const FString& Text, float X, float Y, FLinearColor Color, float Scale = 1);
    FSlateFontInfo FontForScale(float Scale) const;
    FVector2D MeasureLabel(const FString& Text, float Scale) const;
    float WrappedHeight(const FString& Text, float MaxWidth, float Scale) const;
    float WrappedLabel(const FString& Text, float X, float Y, float MaxWidth, FLinearColor Color, float Scale);
    void SingleLineLabel(const FString& Text, float X, float Y, float MaxWidth, FLinearColor Color, float Scale);
    void Button(const FString& Text, const FString& Action, int Arg, float X, float Y, float W, bool Active = false);
    void EntityButton(const cinder::Entity& Entity, float X, float Y, float W, bool Active, int32 SquadMask = 0);
    void DrawUnitPortrait(cinder::Kind Kind, float X, float Y, float W, float H, bool Active = false);
    void DrawUnitRibbon(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void UnitGlyph(cinder::Kind Kind, float X, float Y, float Size, FLinearColor Color);
    void HealthBar(float Health, float Maximum, float X, float Y, float W, FLinearColor Color);
    void DifficultyButton(const FString& Text, int Arg, float X, float Y, float W, float H, bool Active, const FString& Action = TEXT("difficulty"));
    void DrawMenu(ACinderBattlefield* Battle);
    void DrawTutorialOffer(ACinderPlayerController* PC);
    void DrawHelp(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawCompactMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawSelectionIdentity(ACinderPlayerController* PC, ACinderBattlefield* Battle, bool bCompact);
    void DrawArmyDrawer(ACinderPlayerController* PC, ACinderBattlefield* Battle, bool bCompact);
    void DrawGlobalCatalog(ACinderPlayerController* PC, ACinderBattlefield* Battle, bool bCompact);
    void DrawInfoDrawer(ACinderPlayerController* PC, ACinderBattlefield* Battle, bool bCompact);
    void DrawTutorialCard(ACinderBattlefield* Battle, bool bForceShow = false);
    void DrawTutorialPointer(FVector2D Center, const FBox2D& Bounds, bool bWorld);
    void DrawProductionQueue(const cinder::Entity* Producer, float X, float Y, float W, int Columns);
    bool GetOnlineNotice(ACinderPlayerController* PC, ACinderBattlefield* Battle, FString& Heading, FString& Detail) const;
    void DrawOnlineNotice(const FString& Heading, const FString& Detail, float X, float Y, float W, bool bCompact);
    void DrawOverlay(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawWorldIndicators(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawMinimap(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    TArray<FButton> Buttons;
    TArray<FBox2D> UIRegions;
    TArray<FMinimapFogRun> MinimapFogRuns;
    FBox2D Minimap;
    uint64 MinimapFogRevision = MAX_uint64;
    int32 MinimapFogDimension = 0;
    TWeakObjectPtr<ACinderBattlefield> MinimapFogSource;
    FCinderMobileHUDLayout MobileLayout;
    float UIScale = 1, Margin = 24, Width = 1280, Height = 720;
    float SafeTopOffset = 0, SafeBottomOffset = 0;
    double NextMenuInsetRefreshTime = 0.0;
    bool bMenuInsetQueryPending = false;
    FVector4 MenuInsetFractions = FVector4(0, 0, 0, 0);
    FVector4 LastReportedMenuInsets = FVector4(-1, -1, -1, -1);
    int SelectedMap = 0;
    int SubgroupPage = 0;
    int CompactSheet = 0;
    int ArmyRosterPage = 0;
    int ProductionJobsPage = 0;
    int GlobalTrainKind = 1;
    int GlobalTrainQuantity = 1;
    cinder::Id PinnedProducerId = 0;
    int CompactHelpSection = 0;
    int LastHelpPage = -1;
    int LastHelpReference = -1;
    int TutorialCardStep = -1;
    int QueuePage = 0;
    uint32 QueueProducerId = 0;
    uint32 CompactSelectionId = 0;
    bool bCompactLayout = false;
    bool bTutorialDetails = false;
    bool bTutorialTrainKindChosen = false, bTutorialTrainQuantityChosen = false;
    FBox2D TutorialCardBounds, TutorialTargetBounds;
    FVector2D TutorialPointer = FVector2D::ZeroVector;
    bool bTutorialTargetVisible = false, bTutorialNeedsShow = false, bTutorialTargetClear = false;
    int ArmyPanelTab = 0;
    int InfoPage = 0;
    UPROPERTY(EditDefaultsOnly, Category="Presentation")
    TSoftObjectPtr<UTexture2D> MenuBackdropAsset = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Art/UI/T_CinderBackdrop.T_CinderBackdrop")));
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> MenuBackdrop;
    UPROPERTY(EditDefaultsOnly, Category="Presentation|Unit Portraits")
    TArray<TSoftObjectPtr<UTexture2D>> UnitPortraitAssets;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTexture2D>> UnitPortraits;
    UPROPERTY(Transient)
    TObjectPtr<UFont> InterfaceFont;
    UPROPERTY(Transient)
    TObjectPtr<UFont> HeadingFont;
};
