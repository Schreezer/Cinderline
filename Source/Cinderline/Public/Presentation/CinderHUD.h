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
    void LogCampaignHUD() const;
#endif
private:
    friend class FCinderWorldLifecycleIntegration;
    friend class FCinderHUDTouchSafeZoneIntegration;
    friend class FCinderCampaignHUDIntegration;
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
    // Stride folds Stride x Stride source cells into one run cell using the
    // *least* known state in the block, so a coarser minimap can never promote
    // an unexplored cell to explored. Stride 1 is the exact per-cell behaviour.
    static bool RefreshFogRuns(uint64 SourceRevision, int32 SourceDimension,
        const TArray<uint8>& SourceCells, uint64& CachedRevision, int32& CachedDimension,
        TArray<FMinimapFogRun>& CachedRuns, int32 Stride = 1);
    void Panel(float X, float Y, float W, float H, FLinearColor Color);
    void Surface(float X, float Y, float W, float H, FLinearColor Color, float Radius = 8);
    // Same 20-vertex fan, two colours: perimeter vertices are interpolated by
    // their Y position and the centre vertex takes the midpoint, so a vertical
    // gradient or a radial inset costs zero extra triangles and zero extra
    // draw calls over the flat form.
    void Surface(float X, float Y, float W, float H, FLinearColor TopColor,
        FLinearColor BottomColor, float Radius = 8);
    // Outlined text for world-space labels only. The outline is an extra glyph
    // pass per string, so the 136 panel-backed label sites keep Label().
    void WorldLabel(const FString& Text, float X, float Y, FLinearColor Color, float Scale);
    static uint32 ButtonKey(const FString& Action, int32 Argument, cinder::Id EntityId);
    float PressPulse(const FString& Action, int32 Argument, cinder::Id EntityId = 0) const;
    UTexture2D* SelectionRing();
    void RefreshMinimapTerrain(ACinderBattlefield* Battle);
    bool ActionGlyph(const FString& Action, int32 Arg, float X, float Y, float Size, FLinearColor Color);
    void ActionButton(const FString& Text, const FString& Action, int Arg, float X, float Y, float W,
        bool Active = false, bool bIconOnly = false);
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
    void DrawCampaignMenu(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawCampaignCard(ACinderBattlefield* Battle, bool bForceShow = false);
    void DrawCampaignOverlay(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawHelp(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawCompactMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawVoiceControls(ACinderPlayerController* PC);
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
    // Static per map: a baked relief pass under the fog overlay replaces the
    // 130-380 terrain-less DrawRect calls the minimap used to pay every frame.
    int32 MinimapTerrainMap = MIN_int32;
    uint64 MinimapTerrainGeometry = 0;
    float MinimapTerrainWorldSize = 0;
    // Real-time clock sampled once per frame so every press animation in the
    // frame reads the same instant, and so the pruning pass below is O(1).
    float FrameRealTime = 0;
    // Tap-stamped button identities. Pruned every frame, so the map is bounded
    // by the number of taps inside one 0.12 s press window.
    TMap<uint32, float> PressedAt;
    // Resource chips. Last-seen authoritative values plus the instant each
    // changed drive the flash; OreDisplay is the eased number actually printed.
    int32 LastOre = MIN_int32, LastSupply = MIN_int32, LastCapacity = MIN_int32, LastTier = MIN_int32;
    float OreDisplay = 0;
    float OreChangedAt = 0, OreChangeDelta = 0, SupplyChangedAt = 0, TierChangedAt = 0;
    // Affordability is only known after the catalogs run, which draw below the
    // chips. Latch it for the next frame rather than computing the plans twice.
    bool bOreShortfall = false, bOreShortfallPending = false;
    FCinderMobileHUDLayout MobileLayout;
    float UIScale = 1, Margin = 24, Width = 1280, Height = 720;
    FString VoiceCaptionSource, VoiceCaptionDisplay;
    float VoiceCaptionWidth = 0;
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
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> MinimapTerrain;
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> SelectionRingTexture;
};
