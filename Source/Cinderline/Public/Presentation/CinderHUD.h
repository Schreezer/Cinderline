#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UObject/SoftObjectPtr.h"
#include "Presentation/CinderMobileHUDLayout.h"
#include "CinderHUD.generated.h"

class ACinderPlayerController;
class ACinderBattlefield;
class UTexture2D;
class UFont;
struct FSlateFontInfo;
namespace cinder { struct Entity; }

UCLASS()
class CINDERLINE_API ACinderHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void DrawHUD() override;
    bool ContainsUI(FVector2D Point) const;
    bool HandleTap(FVector2D Point);
    void NextHelpSection();
    int MenuMap() const { return SelectedMap; }
    bool CloseCompactSheet();
#if UE_BUILD_DEVELOPMENT
    void PreviewMobileLayout(const FString& State);
    void LogMobileLayout() const;
#endif
private:
    struct FButton { FBox2D Bounds; FString Action; int Argument = 0; };
    void Panel(float X, float Y, float W, float H, FLinearColor Color);
    void Label(const FString& Text, float X, float Y, FLinearColor Color, float Scale = 1);
    FSlateFontInfo FontForScale(float Scale) const;
    FVector2D MeasureLabel(const FString& Text, float Scale) const;
    float WrappedHeight(const FString& Text, float MaxWidth, float Scale) const;
    float WrappedLabel(const FString& Text, float X, float Y, float MaxWidth, FLinearColor Color, float Scale);
    void SingleLineLabel(const FString& Text, float X, float Y, float MaxWidth, FLinearColor Color, float Scale);
    void Button(const FString& Text, const FString& Action, int Arg, float X, float Y, float W, bool Active = false);
    void DrawMenu(ACinderBattlefield* Battle);
    void DrawHelp(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawCompactMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawTutorialCard(ACinderBattlefield* Battle);
    void DrawTutorialWaypoint(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawProductionQueue(const cinder::Entity* Producer, float X, float Y, float W, int Columns);
    bool GetOnlineNotice(ACinderPlayerController* PC, ACinderBattlefield* Battle, FString& Heading, FString& Detail) const;
    void DrawOnlineNotice(const FString& Heading, const FString& Detail, float X, float Y, float W, bool bCompact);
    void DrawOverlay(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawWorldIndicators(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawMinimap(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    TArray<FButton> Buttons;
    TArray<FBox2D> UIRegions;
    FBox2D Minimap;
    FCinderMobileHUDLayout MobileLayout;
    float UIScale = 1, Margin = 24, Width = 1280, Height = 720;
    float SafeTopOffset = 0, SafeBottomOffset = 0;
    double NextSafeInsetRefreshTime = 0.0;
    int32 SafeInsetRefreshAttempts = 0;
    FVector4 LastReportedSafeInsets = FVector4(-1, -1, -1, -1);
    int SelectedMap = 0;
    int SubgroupPage = 0;
    int CompactSheet = 0;
    int CompactHelpSection = 0;
    int LastHelpPage = -1;
    int LastHelpReference = -1;
    int TutorialCardStep = -1;
    int QueuePage = 0;
    uint32 QueueProducerId = 0;
    uint32 CompactSelectionId = 0;
    bool bCompactLayout = false;
    bool bTutorialDetails = false;
    UPROPERTY(EditDefaultsOnly, Category="Presentation")
    TSoftObjectPtr<UTexture2D> MenuBackdropAsset = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Art/UI/T_CinderBackdrop.T_CinderBackdrop")));
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> MenuBackdrop;
    UPROPERTY(Transient)
    TObjectPtr<UFont> InterfaceFont;
    UPROPERTY(Transient)
    TObjectPtr<UFont> HeadingFont;
};
