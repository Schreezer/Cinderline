#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CinderHUD.generated.h"

class ACinderPlayerController;
class ACinderBattlefield;

UCLASS()
class CINDERLINE_API ACinderHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    bool ContainsUI(FVector2D Point) const;
    bool HandleTap(FVector2D Point);
private:
    struct FButton { FBox2D Bounds; FString Action; int Argument = 0; };
    void Panel(float X, float Y, float W, float H, FLinearColor Color);
    void Label(const FString& Text, float X, float Y, FLinearColor Color, float Scale = 1);
    void Button(const FString& Text, const FString& Action, int Arg, float X, float Y, float W, bool Active = false);
    void DrawMenu(ACinderBattlefield* Battle);
    void DrawMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawCompactMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawOverlay(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawWorldIndicators(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    void DrawMinimap(ACinderPlayerController* PC, ACinderBattlefield* Battle);
    TArray<FButton> Buttons;
    TArray<FBox2D> UIRegions;
    FBox2D Minimap;
    float UIScale = 1, Margin = 24, Width = 1280, Height = 720;
    int SelectedMap = 0;
    int SubgroupPage = 0;
    int CompactSheet = 0;
    bool bCompactLayout = false;
};
