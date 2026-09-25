#include "Presentation/CinderHUD.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderPlayerController.h"
#include "Presentation/CinderAudioSubsystem.h"
#include "Presentation/CinderHelpContent.h"
#include "Presentation/CinderOnlineSubsystem.h"
#include "Presentation/CinderTeamColors.h"
#include "Presentation/CinderUnitInfo.h"
#include "Sim/AIDifficulty.h"
#include "Sim/MatchLength.h"
#include "Presentation/CinderTutorial.h"
#include "Presentation/CinderCamera.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "GenericPlatform/GenericApplication.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#if PLATFORM_IOS
#include "IOS/IOSAppDelegate.h"
#include "Async/Async.h"
#include <dispatch/dispatch.h>
#endif
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include <algorithm>
#include <map>

DEFINE_LOG_CATEGORY_STATIC(LogCinderHUD, Log, All);

namespace
{
const FLinearColor Ink(0.004f, 0.010f, 0.018f, 0.97f);
const FLinearColor PanelInk(0.012f, 0.027f, 0.037f, 0.98f);
const FLinearColor White(0.88f, 0.94f, 0.97f);
const FLinearColor Muted(0.38f, 0.50f, 0.55f);
const FLinearColor Mint(0.10f, 0.90f, 0.74f);
const FLinearColor Amber(1.0f, 0.64f, 0.24f);
constexpr int32 SheetNone = 0;
constexpr int32 SheetContext = 1;
constexpr int32 SheetTypes = 2;
constexpr int32 SheetQueue = 3;
constexpr int32 SheetOrders = 4;
constexpr int32 SheetArmy = 5;
constexpr int32 SheetInfo = 6;
constexpr int32 SheetGlobalBuild = 7;
constexpr int32 SheetGlobalTrain = 8;
constexpr int32 SheetGlobalResearch = 9;
#if PLATFORM_MAC
const TCHAR* DesktopControlHint = TEXT("Option+drag pan / Drag select / Scroll zoom");
#else
const TCHAR* DesktopControlHint = TEXT("Alt+drag pan / Drag select / Scroll zoom");
#endif
FString Name(cinder::Kind Kind) { return UTF8_TO_TCHAR(cinder::definition(Kind).name); }
FString CardName(cinder::Kind Kind) { return Kind == cinder::Kind::Mortar ? TEXT("Cinder") : Name(Kind); }
FString ClockString(float Seconds) { return FString::Printf(TEXT("%02d:%02d"), static_cast<int>(Seconds) / 60, static_cast<int>(Seconds) % 60); }
FString RosterOrder(const cinder::Entity& Entity)
{
    using cinder::Order;
    switch (Entity.order)
    {
    case Order::Idle: return TEXT("IDLE");
    case Order::Move: return Entity.navigationExhausted ? TEXT("MOVE BLOCKED") : TEXT("MOVE");
    case Order::Attack: return TEXT("ATTACK");
    case Order::AttackMove: return Entity.navigationExhausted ? TEXT("ATTACK MOVE BLOCKED") : TEXT("ATTACK MOVE");
    case Order::Gather:
        if (Entity.navigationExhausted) return Entity.returning ? TEXT("RETURN BLOCKED") : TEXT("GATHER BLOCKED");
        return Entity.returning ? TEXT("RETURN ORE") : TEXT("GATHER");
    case Order::Hold: return TEXT("HOLD");
    case Order::Construct: return TEXT("BUILD");
    case Order::Defend:
    {
        const float DX = Entity.pos.x - Entity.goal.x;
        const float DY = Entity.pos.y - Entity.goal.y;
        return DX * DX + DY * DY > 25.0f ? TEXT("TO DEFEND") : TEXT("DEFEND");
    }
    case Order::Patrol:
        if (Entity.navigationExhausted) return TEXT("PATROL BLOCKED");
        if (Entity.sustained.phase == cinder::SustainedOrderPhase::Pursuit) return TEXT("PATROL ENGAGE");
        if (Entity.sustained.phase == cinder::SustainedOrderPhase::Return) return TEXT("RETURN TO ENDPOINT");
        return TEXT("PATROL");
    case Order::Escort:
        if (Entity.navigationExhausted) return TEXT("ESCORT BLOCKED");
        if (Entity.sustained.phase == cinder::SustainedOrderPhase::Pursuit) return TEXT("ESCORT ENGAGE");
        if (Entity.sustained.phase == cinder::SustainedOrderPhase::Return) return TEXT("RETURN TO ESCORT");
        return TEXT("ESCORT");
    }
    return TEXT("IDLE");
}
bool SameTacticalPlan(const cinder::Entity& A, const cinder::Entity& B)
{
    const bool bATactical = A.order == cinder::Order::Move || A.order == cinder::Order::AttackMove
        || A.order == cinder::Order::Defend;
    const bool bBTactical = B.order == cinder::Order::Move || B.order == cinder::Order::AttackMove
        || B.order == cinder::Order::Defend;
    const bool bAHasPlan = bATactical || !A.futureOrders.empty();
    const bool bBHasPlan = bBTactical || !B.futureOrders.empty();
    const bool bASustained = A.order == cinder::Order::Patrol || A.order == cinder::Order::Escort;
    const bool bBSustained = B.order == cinder::Order::Patrol || B.order == cinder::Order::Escort;
    if (!bAHasPlan && !bBHasPlan && !bASustained && !bBSustained) return true;
    if (bAHasPlan != bBHasPlan || A.order != B.order
        || A.futureOrders.size() != B.futureOrders.size()) return false;
    if (bATactical && (A.goal.x != B.goal.x || A.goal.y != B.goal.y
        || A.supportTarget != B.supportTarget || A.hasArrivalFacing != B.hasArrivalFacing
        || (A.hasArrivalFacing && A.arrivalFacing != B.arrivalFacing))) return false;
    if (A.order == cinder::Order::Patrol
        && (A.sustained.patrolOrigin.x != B.sustained.patrolOrigin.x
            || A.sustained.patrolOrigin.y != B.sustained.patrolOrigin.y
            || A.sustained.patrolDestination.x != B.sustained.patrolDestination.x
            || A.sustained.patrolDestination.y != B.sustained.patrolDestination.y)) return false;
    if (A.order == cinder::Order::Escort
        && (A.sustained.escortTarget != B.sustained.escortTarget
            || A.sustained.escortOffset.x != B.sustained.escortOffset.x
            || A.sustained.escortOffset.y != B.sustained.escortOffset.y)) return false;
    for (std::size_t Index = 0; Index < A.futureOrders.size(); ++Index)
    {
        const cinder::TacticalOrder& Left = A.futureOrders[Index];
        const cinder::TacticalOrder& Right = B.futureOrders[Index];
        if (Left.order != Right.order || Left.point.x != Right.point.x || Left.point.y != Right.point.y
            || Left.supportTarget != Right.supportTarget || Left.hasArrivalFacing != Right.hasArrivalFacing
            || (Left.hasArrivalFacing && Left.arrivalFacing != Right.arrivalFacing)) return false;
    }
    return true;
}
FString ConstructionStatus(const cinder::Simulation& Sim, const cinder::Entity& Site)
{
    const auto* Worker = Sim.find(Sim.constructionWorker(Site.id));
    const TCHAR* State = Sim.constructionActive(Site.id) ? TEXT("BUILDING")
        : Worker && Worker->navigationExhausted ? TEXT("ROUTE FAILED") : Worker ? TEXT("EN ROUTE") : TEXT("PAUSED");
    return FString::Printf(TEXT("%s %d%%"), State, static_cast<int>(Site.progress * 100));
}
}

ACinderHUD::ACinderHUD()
{
    UnitPortraitAssets.SetNum(15);
    const auto SetPortrait = [this](cinder::Kind Kind, const TCHAR* Path)
    {
        UnitPortraitAssets[static_cast<int32>(Kind)] = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Path));
    };
    SetPortrait(cinder::Kind::Worker, TEXT("/Game/Art/UI/Units/T_CinderPortrait_Drudge.T_CinderPortrait_Drudge"));
    SetPortrait(cinder::Kind::Striker, TEXT("/Game/Art/UI/Units/T_CinderPortrait_Ember.T_CinderPortrait_Ember"));
    SetPortrait(cinder::Kind::Lancer, TEXT("/Game/Art/UI/Units/T_CinderPortrait_Needle.T_CinderPortrait_Needle"));
    SetPortrait(cinder::Kind::Scout, TEXT("/Game/Art/UI/Units/T_CinderPortrait_Skim.T_CinderPortrait_Skim"));
    SetPortrait(cinder::Kind::Bastion, TEXT("/Game/Art/UI/Units/T_CinderPortrait_Anvil.T_CinderPortrait_Anvil"));
    SetPortrait(cinder::Kind::Mortar, TEXT("/Game/Art/UI/Units/T_CinderPortrait_Cinderthrow.T_CinderPortrait_Cinderthrow"));
    SetPortrait(cinder::Kind::Mender, TEXT("/Game/Art/UI/Units/T_CinderPortrait_Mend.T_CinderPortrait_Mend"));
    SetPortrait(cinder::Kind::Kite, TEXT("/Game/Art/UI/Units/T_CinderPortrait_Veil.T_CinderPortrait_Veil"));
    SetPortrait(cinder::Kind::Turret, TEXT("/Game/Art/UI/Units/T_CinderPortrait_Ward.T_CinderPortrait_Ward"));
}

void ACinderHUD::BeginPlay()
{
    Super::BeginPlay();
    // Load once per HUD lifetime; retain the optional texture through garbage collection.
    MenuBackdrop = MenuBackdropAsset.LoadSynchronous();
    // Engine's generic canvas font is only 10 points. Retain a legible UI font
    // independently of the editor's fallback-font settings.
    const auto MakeFont = [this](FName Typeface)
    {
        UFont* Font = NewObject<UFont>(this);
        Font->FontCacheType = EFontCacheType::Runtime;
        Font->RuntimeFontSource = ERuntimeFontSource::CoreStyleDefault;
        Font->LegacyFontName = Typeface;
        Font->LegacyFontSize = 14;
        return Font;
    };
    InterfaceFont = MakeFont(TEXT("Regular"));
    HeadingFont = MakeFont(TEXT("Bold"));
    UnitPortraits.SetNum(UnitPortraitAssets.Num());
    for (int32 Index = 0; Index < UnitPortraitAssets.Num(); ++Index)
        if (!UnitPortraitAssets[Index].IsNull()) UnitPortraits[Index] = UnitPortraitAssets[Index].LoadSynchronous();
}

void ACinderHUD::Panel(float X, float Y, float W, float H, FLinearColor Color) { DrawRect(Color, X, Y, W, H); }
FSlateFontInfo ACinderHUD::FontForScale(float Scale) const
{
    const UFont* Font = Scale >= 1.5f ? HeadingFont.Get() : InterfaceFont.Get();
    if (!Font && GEngine) Font = GEngine->GetMediumFont();
    FSlateFontInfo Info = Font ? Font->GetLegacySlateFontInfo() : FSlateFontInfo();
    const float DPI = Canvas && Canvas->Canvas ? FMath::Max(1.0f, Canvas->Canvas->GetDPIScale()) : 1.0f;
    // Rasterize at the final glyph size. Scaling a small cached glyph blurred
    // both large headings and fractional-size labels, especially on Retina.
    Info.Size = FMath::Clamp(FMath::RoundToInt(14 * Scale * UIScale / DPI), 6, 128);
    return Info;
}
FVector2D ACinderHUD::MeasureLabel(const FString& Text, float Scale) const
{
    if (!Canvas || !Canvas->Canvas) return FVector2D::ZeroVector;
    return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(
        Text, FontForScale(Scale), Canvas->Canvas->GetDPIScale());
}
void ACinderHUD::Label(const FString& Text, float X, float Y, FLinearColor Color, float Scale)
{
    if (!GEngine || !Canvas) return;
    FCanvasTextStringViewItem Item(FVector2D(FMath::RoundToFloat(X), FMath::RoundToFloat(Y)), FStringView(Text), FontForScale(Scale), Color);
    Item.Scale = FVector2D(1);
    Canvas->DrawItem(Item);
}
float ACinderHUD::WrappedHeight(const FString& Text, float MaxWidth, float Scale) const
{
    const float LineHeight = FMath::Max(16 * UIScale, static_cast<float>(MeasureLabel(TEXT("Ag"), Scale).Y) + 3 * UIScale);
    TArray<FString> Words;
    Text.ParseIntoArray(Words, TEXT(" "), true);
    FString Line;
    int32 Lines = 0;
    for (const FString& Word : Words)
    {
        const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
        if (!Line.IsEmpty() && MeasureLabel(Candidate, Scale).X > MaxWidth)
        {
            ++Lines;
            Line = Word;
        }
        else Line = Candidate;
    }
    if (!Line.IsEmpty()) ++Lines;
    return Lines * LineHeight;
}
float ACinderHUD::WrappedLabel(const FString& Text, float X, float Y, float MaxWidth, FLinearColor Color, float Scale)
{
    const float StartY = Y;
    const float LineHeight = FMath::Max(16 * UIScale, static_cast<float>(MeasureLabel(TEXT("Ag"), Scale).Y) + 3 * UIScale);
    TArray<FString> Words;
    Text.ParseIntoArray(Words, TEXT(" "), true);
    FString Line;
    for (const FString& Word : Words)
    {
        const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
        if (!Line.IsEmpty() && MeasureLabel(Candidate, Scale).X > MaxWidth)
        {
            Label(Line, X, Y, Color, Scale);
            Y += LineHeight;
            Line = Word;
        }
        else Line = Candidate;
    }
    if (!Line.IsEmpty())
    {
        Label(Line, X, Y, Color, Scale);
        Y += LineHeight;
    }
    return Y - StartY;
}
void ACinderHUD::SingleLineLabel(const FString& Text, float X, float Y, float MaxWidth, FLinearColor Color, float Scale)
{
    FString VisibleText = Text;
    if (MeasureLabel(VisibleText, Scale).X > MaxWidth)
    {
        do
        {
            VisibleText.LeftChopInline(1);
        } while (!VisibleText.IsEmpty() && MeasureLabel(VisibleText + TEXT("..."), Scale).X > MaxWidth);
        VisibleText += TEXT("...");
    }
    Label(VisibleText, X, Y, Color, Scale);
}
void ACinderHUD::Button(const FString& Text, const FString& Action, int Arg, float X, float Y, float W, bool Active)
{
    const float S = UIScale, H = 44 * S;
    float MouseX = -1, MouseY = -1;
    if (PlayerOwner) PlayerOwner->GetMousePosition(MouseX, MouseY);
    const bool Hover = FBox2D(FVector2D(X,Y), FVector2D(X+W,Y+H)).IsInside(FVector2D(MouseX,MouseY));
    const bool Primary = Action == TEXT("start");
    const bool Warning = Action.StartsWith(TEXT("cancel"));
    const FLinearColor Accent = Warning ? Amber : Mint;
    const float FaceY = Y + 3 * S, FaceH = H - 6 * S;
    Surface(X, FaceY, W, FaceH, Primary ? Mint : Active ? Accent.CopyWithNewOpacity(0.48f)
        : FLinearColor(0.20f, 0.30f, 0.36f, Hover ? 0.58f : 0.28f), 6 * S);
    if (!Primary) Surface(X + S, FaceY + S, W - 2*S, FaceH - 2*S,
        Active ? FLinearColor(0.015f,0.065f,0.073f,0.98f) : FLinearColor(0.012f,0.023f,0.036f,0.95f), 5*S);
    const float FontScale = 0.72f;
    const float TW = MeasureLabel(Text, FontScale).X;
    const float Fit = TW > 0 ? FMath::Min(1.0f, (W-16*S)/TW) : 1;
    const bool bBuild = Action == TEXT("build") || Action == TEXT("globalbuild");
    Label(Text, X + 8*S, Y + (bBuild ? 7 : 14)*S, Primary ? Ink : Active ? Accent : White, FontScale * Fit);
    FButton B; B.Bounds=FBox2D(FVector2D(X,Y),FVector2D(X+W,Y+H)); B.Action=Action; B.Argument=Arg; Buttons.Add(B);
}

void ACinderHUD::ActionButton(const FString& Text, const FString& Action, int Arg, float X, float Y, float W, bool Active)
{
    const float S = UIScale, H = 44*S;
    float MX=-1,MY=-1; if(PlayerOwner) PlayerOwner->GetMousePosition(MX,MY);
    const FBox2D Hit(FVector2D(X,Y), FVector2D(X+W,Y+H));
    const bool Hover=Hit.IsInside(FVector2D(MX,MY));
    const bool Warning=Action.StartsWith(TEXT("cancel"));
    const FLinearColor Accent=Warning ? Amber : Mint;
    Surface(X+2*S,Y+2*S,W-4*S,H-4*S,Active ? Accent.CopyWithNewOpacity(0.55f)
        : FLinearColor(0.21f,0.31f,0.36f,Hover ? 0.5f : 0.24f),8*S);
    Surface(X+3*S,Y+3*S,W-6*S,H-6*S,Active ? FLinearColor(0.012f,0.066f,0.077f,0.96f)
        : FLinearColor(0.008f,0.016f,0.028f,0.91f),7*S);
    const FLinearColor Color=Active ? Accent : White;
    const bool Glyph=ActionGlyph(Action,Arg,X+(W-19*S)*0.5f,Y+6*S,19*S,Color);
    const float FontScale=0.55f;
    const float TW=MeasureLabel(Text,FontScale).X;
    const float Fit=TW>0 ? FMath::Min(1.0f,(W-6*S)/TW) : 1;
    Label(Text,X+(W-TW*Fit)*0.5f,Y+(Glyph ? 28 : 17)*S,Color,FontScale*Fit);
    if(Active) Surface(X+W*0.36f,Y+40*S,W*0.28f,2*S,Accent,S);
    FButton B; B.Bounds=Hit; B.Action=Action; B.Argument=Arg; Buttons.Add(B);
}

void ACinderHUD::HealthBar(float Health, float Maximum, float X, float Y, float W, FLinearColor Color)
{
    Panel(X, Y, W, 3 * UIScale, FLinearColor(0.002f, 0.008f, 0.013f, 0.92f));
    if (Maximum > 0)
        Panel(X, Y, W * FMath::Clamp(Health / Maximum, 0.0f, 1.0f), 3 * UIScale, Color);
}

void ACinderHUD::UnitGlyph(cinder::Kind Kind, float X, float Y, float Size, FLinearColor Color)
{
    const float L = X, R = X + Size, T = Y, B = Y + Size;
    switch (Kind)
    {
    case cinder::Kind::Striker:
        DrawLine(L, B, X + Size * 0.5f, T, Color, 2 * UIScale);
        DrawLine(X + Size * 0.5f, T, R, B, Color, 2 * UIScale);
        DrawLine(L + Size * 0.22f, B - Size * 0.28f, R - Size * 0.22f, B - Size * 0.28f, Color, 2 * UIScale);
        break;
    case cinder::Kind::Lancer:
        DrawLine(X + Size * 0.5f, T, X + Size * 0.5f, B, Color, 2 * UIScale);
        DrawLine(L + Size * 0.18f, T + Size * 0.35f, X + Size * 0.5f, T, Color, 2 * UIScale);
        DrawLine(R - Size * 0.18f, T + Size * 0.35f, X + Size * 0.5f, T, Color, 2 * UIScale);
        break;
    case cinder::Kind::Scout:
        DrawLine(L, Y + Size * 0.5f, R, T, Color, 2 * UIScale);
        DrawLine(R, T, R - Size * 0.25f, B, Color, 2 * UIScale);
        DrawLine(R - Size * 0.25f, B, L, Y + Size * 0.5f, Color, 2 * UIScale);
        break;
    case cinder::Kind::Bastion:
        Panel(L, T, Size, Size, Color.CopyWithNewOpacity(0.22f));
        DrawLine(L, T, R, T, Color, 2 * UIScale); DrawLine(R, T, R, B, Color, 2 * UIScale);
        DrawLine(R, B, L, B, Color, 2 * UIScale); DrawLine(L, B, L, T, Color, 2 * UIScale);
        break;
    case cinder::Kind::Mortar:
        Panel(L, Y + Size * 0.58f, Size, Size * 0.34f, Color.CopyWithNewOpacity(0.30f));
        DrawLine(X + Size * 0.20f, Y + Size * 0.62f, R, T, Color, 3 * UIScale);
        break;
    case cinder::Kind::Mender:
        Panel(X + Size * 0.38f, T, Size * 0.24f, Size, Color);
        Panel(L, Y + Size * 0.38f, Size, Size * 0.24f, Color);
        break;
    case cinder::Kind::Kite:
        DrawLine(L, Y + Size * 0.5f, X + Size * 0.5f, T, Color, 2 * UIScale);
        DrawLine(X + Size * 0.5f, T, R, Y + Size * 0.5f, Color, 2 * UIScale);
        DrawLine(R, Y + Size * 0.5f, X + Size * 0.5f, B, Color, 2 * UIScale);
        DrawLine(X + Size * 0.5f, B, L, Y + Size * 0.5f, Color, 2 * UIScale);
        break;
    default:
        Panel(L + Size * 0.18f, T + Size * 0.18f, Size * 0.64f, Size * 0.64f, Color.CopyWithNewOpacity(0.35f));
        DrawLine(L, Y + Size * 0.5f, R, Y + Size * 0.5f, Color, 2 * UIScale);
        break;
    }
}

void ACinderHUD::DrawUnitPortrait(cinder::Kind Kind, float X, float Y, float W, float H, bool Active)
{
    const int32 Index = static_cast<int32>(Kind);
    UTexture2D* Texture = UnitPortraits.IsValidIndex(Index) ? UnitPortraits[Index].Get() : nullptr;
    Panel(X, Y, W, H, Active ? FLinearColor(0.018f, 0.11f, 0.105f) : FLinearColor(0.018f, 0.035f, 0.043f));
    if (Texture)
    {
        DrawTexture(Texture, X, Y, W, H, 0, 0, 1, 1, FLinearColor::White, BLEND_Translucent);
        Panel(X, Y + H * 0.68f, W, H * 0.32f, FLinearColor(0.002f, 0.008f, 0.013f, 0.60f));
    }
    else
    {
        const float GlyphSize = FMath::Min(W, H) * 0.52f;
        UnitGlyph(Kind, X + (W - GlyphSize) * 0.5f, Y + (H - GlyphSize) * 0.44f,
            GlyphSize, Active ? Mint : White);
    }
    const FLinearColor Edge = Active ? Mint : FLinearColor(0.075f, 0.16f, 0.18f);
    Panel(X, Y, W, UIScale, Edge);
    Panel(X, Y + H - UIScale, W, UIScale, Edge);
    Panel(X, Y, UIScale, H, Edge);
    Panel(X + W - UIScale, Y, UIScale, H, Edge);
}

void ACinderHUD::EntityButton(const cinder::Entity& Entity, float X, float Y, float W, bool Active, int32 SquadMask)
{
    const float S=UIScale,H=64*S;
    Surface(X,Y,W,H,Active ? FLinearColor(0.015f,0.075f,0.085f,0.97f) : PanelInk,6*S);
    DrawUnitPortrait(Entity.kind,X+3*S,Y+3*S,W-6*S,38*S,Active);
    Surface(X+W-29*S,Y+5*S,25*S,12*S,Ink,3*S);
    SingleLineLabel(FString::Printf(TEXT("#%u"),Entity.id),X+W-27*S,Y+6*S,22*S,White,0.47f);
    SingleLineLabel(Name(Entity.kind),X+6*S,Y+42*S,W-12*S,Active ? Mint : White,0.58f);
    if(SquadMask) SingleLineLabel(FString::Printf(TEXT("%c"),TCHAR('A'+FMath::CountTrailingZeros(static_cast<uint32>(SquadMask)))),X+6*S,Y+6*S,18*S,Amber,0.50f);
    HealthBar(Entity.hp,cinder::definition(Entity.kind).hp,X+5*S,Y+H-6*S,W-10*S,Mint);
    FButton Entry; Entry.Bounds=FBox2D(FVector2D(X,Y),FVector2D(X+W,Y+H));
    Entry.Action=TEXT("armyunit"); Entry.EntityId=Entity.id; Buttons.Add(Entry);
}

void ACinderHUD::DifficultyButton(const FString& Text, int Arg, float X, float Y, float W, float H, bool Active, const FString& Action)
{
    float MouseX = -1, MouseY = -1;
    if (PlayerOwner) PlayerOwner->GetMousePosition(MouseX, MouseY);
    const float HitHeight = FMath::Max(H, 44 * UIScale);
    const float HitY = Y - (HitHeight - H) * 0.5f;
    const bool Hover = MouseX >= X && MouseY >= HitY && MouseX < X + W && MouseY < HitY + HitHeight;
    const FLinearColor Face = Active ? FLinearColor(0.018f, 0.11f, 0.105f)
        : Hover ? FLinearColor(0.026f, 0.055f, 0.072f) : PanelInk;
    Panel(X, Y, W, H, Face);
    Panel(X, Y, Active ? 3 * UIScale : UIScale, H, Active ? Mint : FLinearColor(0.075f, 0.16f, 0.18f));
    Panel(X, Y, W, UIScale, Active ? Mint : FLinearColor(0.075f, 0.16f, 0.18f));
    Panel(X, Y + H - UIScale, W, UIScale, FLinearColor(0.018f, 0.055f, 0.065f));
    const float TextWidth = MeasureLabel(Text, 0.68f).X;
    const float Fit = TextWidth > 0 ? FMath::Min(1.0f, (W - 12 * UIScale) / TextWidth) : 1.0f;
    Label(Text, X + 7 * UIScale, Y + (H / UIScale - 11) * 0.5f * UIScale,
        Active ? Mint : White, 0.68f * Fit);
    FButton ButtonEntry;
    ButtonEntry.Bounds = FBox2D(FVector2D(X, HitY), FVector2D(X + W, HitY + HitHeight));
    ButtonEntry.Action = Action;
    ButtonEntry.Argument = Arg;
    Buttons.Add(ButtonEntry);
}

bool ACinderHUD::ContainsUI(FVector2D Point) const
{
    for (const auto& R : UIRegions) if (R.IsInside(Point)) return true;
    for (const auto& B : Buttons) if (B.Bounds.IsInside(Point)) return true;
    return false;
}

bool ACinderHUD::HandleTap(FVector2D Point)
{
    UE_LOG(LogCinderHUD, Verbose, TEXT("Tap %.1f,%.1f canvas=%.0fx%.0f buttons=%d"), Point.X, Point.Y, Width, Height, Buttons.Num());
    auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
    if (!PC) return false;
    for (int I = Buttons.Num() - 1; I >= 0; --I)
    {
        const FButton B = Buttons[I];
        if (!B.Bounds.IsInside(Point)) continue;
        if (PC->IsOnlineLeavePending() && B.Action != TEXT("onlineconfirm") && B.Action != TEXT("onlinecancel")) return true;
        if (PC->IsTutorialRestartPending() && B.Action != TEXT("tutorialconfirm") && B.Action != TEXT("tutorialcancel")) return true;
        if (PC->IsHelpOpen()
            && B.Action != TEXT("helpclose") && B.Action != TEXT("helppage")
            && B.Action != TEXT("helpreference") && B.Action != TEXT("helpinput")
            && B.Action != TEXT("helpsection")) return true;
        UCinderAudioSubsystem::Play(this, ECinderCue::UI_Click);
        if (B.Action == TEXT("map")) SelectedMap = B.Argument;
        else if (B.Action == TEXT("groupsnext")) ++SubgroupPage;
        else if (B.Action == TEXT("queuepage")) QueuePage = FMath::Max(0, QueuePage + B.Argument);
        else if (B.Action == TEXT("rosterpage")) ArmyRosterPage = FMath::Max(0, ArmyRosterPage + B.Argument);
        else if (B.Action == TEXT("armytab")) { ArmyPanelTab = FMath::Clamp(B.Argument, 0, 3); ArmyRosterPage = 0; ProductionJobsPage = 0; }
        else if (B.Action == TEXT("deselect"))
        {
            PC->ExecuteAction(TEXT("deselect"));
            CompactSheet = SheetNone;
            PinnedProducerId = 0;
            bTutorialDetails = false;
        }
        else if (B.Action == TEXT("jobpage")) ProductionJobsPage = FMath::Max(0, ProductionJobsPage + B.Argument);
        else if (B.Action == TEXT("globalcatalog"))
        {
            if (PC->IsBuildMode()) PC->ExecuteAction(TEXT("cancelplacement"));
            if (PC->IsProductionRallyMode()) PC->ExecuteAction(TEXT("cancelrally"));
            CompactSheet = CompactSheet == B.Argument ? SheetNone : B.Argument;
            PC->bBuildMenu = false;
            bTutorialDetails = false;
            PinnedProducerId = B.EntityId;
            bTutorialTrainKindChosen = bTutorialTrainQuantityChosen = false;
        }
        else if (B.Action == TEXT("trainkind")) { GlobalTrainKind = B.Argument; bTutorialTrainKindChosen = true; bTutorialTrainQuantityChosen = false; }
        else if (B.Action == TEXT("trainqty")) { GlobalTrainQuantity = FMath::Clamp(B.Argument, 1, 20); bTutorialTrainQuantityChosen = true; }
        else if (B.Action == TEXT("trainqtydelta")) { GlobalTrainQuantity = FMath::Clamp(GlobalTrainQuantity + B.Argument, 1, 20); bTutorialTrainQuantityChosen = true; }
        else if (B.Action == TEXT("tutorialshow")) FocusTutorialTarget();
        else if (B.Action == TEXT("globalbuild"))
        {
            PC->BeginGlobalBuild(static_cast<cinder::Kind>(B.Argument));
            if (PC->IsBuildMode()) CompactSheet = SheetNone;
        }
        else if (B.Action == TEXT("globaltrain"))
            PC->QueueTraining(static_cast<cinder::Kind>(GlobalTrainKind), GlobalTrainQuantity, PinnedProducerId);
        else if (B.Action == TEXT("globalresearch")) PC->QueueResearch(B.Argument, PinnedProducerId);
        else if (B.Action == TEXT("cancelproduction")) PC->CancelProduction(B.EntityId, B.JobId);
        else if (B.Action == TEXT("cancelconstruction")) PC->CancelConstruction(B.EntityId);
        else if (B.Action == TEXT("productionrally"))
        {
            PC->BeginProductionRally(B.EntityId);
            if (PC->IsProductionRallyMode()) CompactSheet = SheetNone;
        }
        else if (B.Action == TEXT("rallydefault")) PC->UseDefaultProductionRally(B.EntityId);
        else if (B.Action == TEXT("rallyfocus"))
        {
            PC->FocusArmyRally();
            CompactSheet = SheetNone;
        }
        else if (B.Action == TEXT("producerjobs"))
        {
            CompactSheet = SheetArmy;
            ArmyPanelTab = 2;
            ProductionJobsPage = 0;
            PinnedProducerId = B.EntityId;
            PC->bBuildMenu = false;
            bTutorialDetails = false;
        }
        else if (B.Action == TEXT("productionfocus"))
        {
            PC->FocusProduction(B.EntityId);
            CompactSheet = SheetNone;
        }
        else if (B.Action == TEXT("cancelrally")) PC->ExecuteAction(TEXT("cancelrally"));
        else if (B.Action == TEXT("army"))
        {
            if (PC->IsBuildMode()) PC->ExecuteAction(TEXT("cancelplacement"));
            if (PC->IsProductionRallyMode()) PC->ExecuteAction(TEXT("cancelrally"));
            CompactSheet = CompactSheet == SheetArmy ? SheetNone : SheetArmy;
            PC->bBuildMenu = false;
            bTutorialDetails = false;
            PinnedProducerId = 0;
        }
        else if (B.Action == TEXT("info"))
        {
            if (PC->IsBuildMode()) PC->ExecuteAction(TEXT("cancelplacement"));
            const bool bOpening = CompactSheet != SheetInfo;
            CompactSheet = CompactSheet == SheetInfo ? SheetNone : SheetInfo;
            if (bOpening) InfoPage = 0;
            PC->bBuildMenu = false;
            bTutorialDetails = false;
        }
        else if (B.Action == TEXT("infotab")) InfoPage = FMath::Clamp(B.Argument, 0, 2);
        else if (B.Action == TEXT("orders"))
        {
            CompactSheet = CompactSheet == SheetOrders ? SheetNone : SheetOrders;
            PC->bBuildMenu = false;
            bTutorialDetails = false;
        }
        else if (B.Action == TEXT("armyall"))
        {
            PC->SelectArmy();
            PC->ExecuteAction(TEXT("focus"));
            CompactSheet = SheetNone;
        }
        else if (B.Action == TEXT("armytype"))
        {
            PC->SelectOwnedKind(static_cast<cinder::Kind>(B.Argument));
            PC->ExecuteAction(TEXT("focus"));
            CompactSheet = SheetNone;
        }
        else if (B.Action == TEXT("armyunit"))
        {
            if (PC->SelectOwnedEntity(B.EntityId)) PC->ExecuteAction(TEXT("focus"));
            CompactSheet = SheetNone;
        }
        else if (B.Action == TEXT("squadtactics"))
        {
            if (PC->RecallSquad(B.Argument))
            {
                CompactSelectionId = PC->Selection().empty() ? 0 : PC->Selection().front();
                CompactSheet = SheetOrders;
            }
        }
        else if (B.Action == TEXT("squad"))
        {
            PC->ExecuteAction(B.Action, B.Argument);
            PC->ExecuteAction(TEXT("focus"));
            CompactSheet = SheetNone;
        }
        else if (B.Action == TEXT("squadassign")) PC->ExecuteAction(B.Action, B.Argument);
        else if (B.Action == TEXT("helpsection")) CompactHelpSection = FMath::Max(0, B.Argument);
        else if (B.Action == TEXT("tutorialdetails")) { bTutorialDetails = !bTutorialDetails; CompactSheet = 0; PC->bBuildMenu = false; }
        else if (B.Action == TEXT("sheet")) { CompactSheet = CompactSheet == B.Argument ? 0 : B.Argument; PC->bBuildMenu = false; bTutorialDetails = false; }
        else if (B.Action == TEXT("closesheet")) { CompactSheet = 0; PinnedProducerId = 0; PC->bBuildMenu = false; }
        else
        {
            if (B.Action == TEXT("tutorialfocus") || B.Action == TEXT("buildmenu")) bTutorialDetails = false;
            if (B.Action == TEXT("buildmenu") || B.Action == TEXT("kind") || B.Action == TEXT("box")
                || B.Action == TEXT("move") || B.Action == TEXT("attack") || B.Action == TEXT("defend")
                || B.Action == TEXT("patrol") || B.Action == TEXT("escort")
                || B.Action == TEXT("facenext")
                || B.Action == TEXT("stop") || B.Action == TEXT("hold")
                || B.Action == TEXT("focus") || B.Action == TEXT("tutorialfocus")
                || B.Action == TEXT("workers") || B.Action == TEXT("cancelbuilding")) CompactSheet = 0;
            PC->ExecuteAction(B.Action, B.Argument);
        }
        return true;
    }
    if (Minimap.bIsValid && Minimap.IsInside(Point) && PC->Battlefield() && !PC->Battlefield()->IsMenu() && !PC->Battlefield()->IsPaused())
    {
        const float World = PC->Battlefield()->Sim().worldSize();
        const int X = FMath::Clamp(static_cast<int>((Point.X - Minimap.Min.X) / Minimap.GetSize().X * World), 0, static_cast<int>(World));
        const int Y = FMath::Clamp(static_cast<int>((Point.Y - Minimap.Min.Y) / Minimap.GetSize().Y * World), 0, static_cast<int>(World));
        PC->ExecuteAction(TEXT("minimap"), Y * 10000 + X);
        return true;
    }
    return ContainsUI(Point);
}

#if UE_BUILD_DEVELOPMENT
bool ACinderHUD::TapPreviewAction(const FString& Action, TOptional<int32> Argument, TOptional<cinder::Id> Entity)
{
    if (!FApp::IsUnattended()) return false;
    for (int32 Index = Buttons.Num() - 1; Index >= 0; --Index)
    {
        const FButton& Entry = Buttons[Index];
        if (Entry.Action != Action || (Argument.IsSet() && Entry.Argument != Argument.GetValue())
            || (Entity.IsSet() && Entry.EntityId != Entity.GetValue())) continue;
        return HandleTap((Entry.Bounds.Min + Entry.Bounds.Max) * 0.5f);
    }
    return false;
}
#endif

bool ACinderHUD::CloseCompactSheet()
{
    const bool bWasOpen = CompactSheet != 0;
    CompactSheet = 0;
    PinnedProducerId = 0;
    return bWasOpen;
}

void ACinderHUD::OpenGlobalPanel(FName Panel, cinder::Id Producer)
{
    int32 Sheet = SheetNone;
    if (Panel == TEXT("build")) Sheet = SheetGlobalBuild;
    else if (Panel == TEXT("train")) Sheet = SheetGlobalTrain;
    else if (Panel == TEXT("research")) Sheet = SheetGlobalResearch;
    else if (Panel == TEXT("army") || Panel == TEXT("jobs")) Sheet = SheetArmy;
    if (Sheet == SheetNone) return;
    if (auto* PC = Cast<ACinderPlayerController>(PlayerOwner))
    {
        if (PC->IsBuildMode()) PC->ExecuteAction(TEXT("cancelplacement"));
        if (PC->IsProductionRallyMode()) PC->ExecuteAction(TEXT("cancelrally"));
        PC->bBuildMenu = false;
    }
    CompactSheet = Sheet;
    bTutorialDetails = false;
    bTutorialTrainKindChosen = bTutorialTrainQuantityChosen = false;
    PinnedProducerId = Producer;
    if (Panel == TEXT("jobs")) ArmyPanelTab = 2;
}

void ACinderHUD::NextHelpSection()
{
    const auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
    if (!PC || !PC->IsHelpOpen() || !bCompactLayout) return;
    const int32 PageIndex = PC->HelpPage();
    const int32 ReferenceIndex = PC->HelpReference();
    if (LastHelpPage != PageIndex || LastHelpReference != ReferenceIndex)
    {
        CompactHelpSection = 0;
        LastHelpPage = PageIndex;
        LastHelpReference = ReferenceIndex;
    }
    const FCinderHelpPage Page = CinderHelp::Page(PageIndex, PC->HelpUsesTouch(), ReferenceIndex);
    if (!Page.Sections.IsEmpty()) CompactHelpSection = (CompactHelpSection + 1) % Page.Sections.Num();
}

void ACinderHUD::DrawHUD()
{
    Super::DrawHUD();
    Buttons.Reset(); UIRegions.Reset(); Minimap.Init();
    if (!Canvas) return;
    auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
    auto* Battle = PC ? PC->Battlefield() : nullptr;
    const bool bMenu = Battle && Battle->IsMenu();
    // The viewport may already have inset the entire canvas. Only the menu
    // opts out: its artwork is full bleed and its controls handle their own inset.
    // UE's PopSafeZoneTransform only pops when left/top padding is nonzero.
    bool bRestoreCanvasSafeZone = false;
#if PLATFORM_IOS
    if (bMenu && Canvas->Canvas && FSlateApplication::IsInitialized())
    {
        FDisplayMetrics Metrics;
        FSlateApplication::Get().GetCachedDisplayMetrics(Metrics);
        bRestoreCanvasSafeZone = Metrics.TitleSafePaddingSize.X > 0.5f
            || Metrics.TitleSafePaddingSize.Y > 0.5f;
    }
#endif
    if (bRestoreCanvasSafeZone) Canvas->PopSafeZoneTransform();
    ON_SCOPE_EXIT
    {
        if (bRestoreCanvasSafeZone) Canvas->ApplySafeZoneTransform();
    };
    Width = Canvas->SizeX; Height = Canvas->SizeY;
#if PLATFORM_IOS || PLATFORM_ANDROID
    bCompactLayout = true;
#else
    const UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    const float WindowDPI = Viewport ? FMath::Max(1.0f, Viewport->GetDPIScale()) : 1.0f;
    bCompactLayout = Width / Height > 2.0f || Height / WindowDPI < 500;
#if UE_BUILD_DEVELOPMENT
    bCompactLayout |= FParse::Param(FCommandLine::Get(), TEXT("mobilehud"));
#endif
#endif
    FVector4 SafeInsets(0, 0, 0, 0);
    SafeTopOffset = SafeBottomOffset = 0;
    if ((PLATFORM_IOS || PLATFORM_ANDROID) && FSlateApplication::IsInitialized())
    {
        FDisplayMetrics Metrics;
        FSlateApplication::Get().GetCachedDisplayMetrics(Metrics);
        const float ScaleX = Width / FMath::Max(1, Metrics.PrimaryDisplayWidth);
        const float ScaleY = Height / FMath::Max(1, Metrics.PrimaryDisplayHeight);
        const auto& Insets = Metrics.TitleSafePaddingSize;
        SafeInsets = FVector4(Insets.X * ScaleX, Insets.Y * ScaleY, Insets.Z * ScaleX, Insets.W * ScaleY);
    }
#if PLATFORM_IOS
    if (bMenu)
    {
        // Read UIKit without changing Unreal's global display metrics or the
        // gameplay canvas. Poll only while the menu is open to follow rotation.
        const double Now = FPlatformTime::Seconds();
        if (!bMenuInsetQueryPending && Now >= NextMenuInsetRefreshTime)
        {
            bMenuInsetQueryPending = true;
            NextMenuInsetRefreshTime = Now + 0.5;
            const TWeakObjectPtr<ACinderHUD> WeakHUD(this);
            dispatch_async(dispatch_get_main_queue(), ^{
                UIWindow* Window = [[IOSAppDelegate GetDelegate] window];
                const CGSize Size = Window.bounds.size;
                const UIEdgeInsets Insets = Window.safeAreaInsets;
                const FVector4 Fractions = Size.width > 0 && Size.height > 0
                    ? FVector4(Insets.left / Size.width, Insets.top / Size.height,
                        Insets.right / Size.width, Insets.bottom / Size.height)
                    : FVector4(0, 0, 0, 0);
                AsyncTask(ENamedThreads::GameThread, [WeakHUD, Fractions]
                {
                    if (ACinderHUD* HUD = WeakHUD.Get())
                    {
                        HUD->MenuInsetFractions = Fractions;
                        HUD->bMenuInsetQueryPending = false;
                    }
                });
            });
        }
        SafeInsets = FVector4(MenuInsetFractions.X * Width, MenuInsetFractions.Y * Height,
            MenuInsetFractions.Z * Width, MenuInsetFractions.W * Height);
        if (SafeInsets != LastReportedMenuInsets)
        {
            UE_LOG(LogCinderHUD, Display, TEXT("CINDERLINE_MENU_SAFE_AREA canvas=%.0fx%.0f insets_px=(%.1f,%.1f,%.1f,%.1f) background=full_bleed"),
                Width, Height, SafeInsets.X, SafeInsets.Y, SafeInsets.Z, SafeInsets.W);
            LastReportedMenuInsets = SafeInsets;
        }
    }
#endif
#if UE_BUILD_DEVELOPMENT
    if (bMenu && FApp::IsUnattended())
    {
        float MockLeft = 0.0f, MockRight = 0.0f, MockBottom = 0.0f;
        const bool bMocked = FParse::Value(FCommandLine::Get(), TEXT("CinderMatchLengthSafeLeft="), MockLeft)
            | FParse::Value(FCommandLine::Get(), TEXT("CinderMatchLengthSafeRight="), MockRight)
            | FParse::Value(FCommandLine::Get(), TEXT("CinderMatchLengthSafeBottom="), MockBottom);
        if (bMocked)
        {
            SafeInsets.X = FMath::Clamp(MockLeft, 0.0f, Width * 0.35f);
            SafeInsets.Z = FMath::Clamp(MockRight, 0.0f, Width * 0.35f);
            SafeInsets.W = FMath::Clamp(MockBottom, 0.0f, Height * 0.25f);
            UE_LOG(LogCinderHUD, Display,
                TEXT("CINDERLINE_MATCH_LENGTH_SAFE_AREA left=%.1f right=%.1f bottom=%.1f menu=1 unattended=1"),
                SafeInsets.X, SafeInsets.Z, SafeInsets.W);
        }
    }
#endif
    MobileLayout = FCinderMobileHUDLayout::Make(FVector2D(Width, Height), SafeInsets, !bCompactLayout && !bMenu);
    UIScale = bMenu && !bCompactLayout ? FMath::Max(0.45f, FMath::Min(Width / 1280.0f, Height / 720.0f)) : MobileLayout.Scale;
    const bool bNarrowPalette = MobileLayout.Drawer.GetSize().X < 400 * UIScale;
    if (!bMenu && bNarrowPalette && (CompactSheet == SheetGlobalTrain || CompactSheet == SheetArmy || CompactSheet == SheetInfo))
        MobileLayout.Drawer.Max.Y = FMath::Min(MobileLayout.Drawer.Min.Y + (CompactSheet==SheetGlobalTrain ? 216 : CompactSheet==SheetArmy ? 224 : 248) * UIScale,
            MobileLayout.Commands.Min.Y - 8 * UIScale);
    Margin = bCompactLayout ? FMath::Max(MobileLayout.Left, Width - MobileLayout.Right) : FMath::Max(24 * UIScale, Width * 0.035f);
    if (bCompactLayout)
    {
        SafeTopOffset = MobileLayout.Top - 10 * UIScale;
        SafeBottomOffset = Height - MobileLayout.Bottom - 12 * UIScale;
    }
    if (!Battle) { Label(TEXT("Preparing battlefield..."), Margin, Margin, White); return; }
    if (Battle->IsMenu())
    {
        QueuePage = 0; QueueProducerId = 0; CompactSheet = 0; CompactSelectionId = 0;
        ArmyRosterPage = 0; ProductionJobsPage = 0; ArmyPanelTab = 0; InfoPage = 0;
        GlobalTrainKind = static_cast<int32>(cinder::Kind::Striker); GlobalTrainQuantity = 1;
        PinnedProducerId = 0;
        TutorialCardStep = -1; bTutorialDetails = false;
        DrawMenu(Battle);
        if (PC->IsTutorialOfferPending()) DrawTutorialOffer(PC);
        else if (PC->IsHelpOpen()) DrawHelp(PC, Battle);
        return;
    }
    const auto* Selected = PC->Selection().empty() ? nullptr : Battle->Sim().find(PC->Selection().front());
    const uint32 SelectionId = Selected ? Selected->id : 0;
    if (CompactSelectionId != SelectionId) { CompactSelectionId = SelectionId; CompactSheet = 0; }
    const uint32 ProducerId = Selected && cinder::definition(Selected->kind).building ? Selected->id : 0;
    if (QueueProducerId != ProducerId) { QueueProducerId = ProducerId; QueuePage = 0; }
    DrawWorldIndicators(PC, Battle);
    DrawCompactMatch(PC, Battle);
    if (Battle->Tutorial().IsActive() && !Battle->IsPaused() && Battle->Sim().winner() == -1 && !PC->IsHelpOpen())
        DrawTutorialCard(Battle);
    if (PC->IsHelpOpen()) DrawHelp(PC, Battle);
}

void ACinderHUD::DrawMenu(ACinderBattlefield* Battle)
{
    const bool bHasBackdrop = MenuBackdrop && MenuBackdrop->GetSizeX() > 0 && MenuBackdrop->GetSizeY() > 0 && Height > 0;
    if (bHasBackdrop)
    {
        const float TextureAspect = static_cast<float>(MenuBackdrop->GetSizeX()) / MenuBackdrop->GetSizeY();
        const float ViewAspect = Width / Height;
        const float UWidth = FMath::Min(1.0f, ViewAspect / TextureAspect);
        const float VHeight = FMath::Min(1.0f, TextureAspect / ViewAspect);
        DrawTexture(MenuBackdrop, 0, 0, Width, Height, (1 - UWidth) * 0.5f, (1 - VHeight) * 0.5f, UWidth, VHeight, FLinearColor::White, BLEND_Opaque);
    }
    // Keep the illustration clear on the right and provide a quiet reading area.
    const float Strip = FMath::Max(2.0f, 6 * UIScale);
    for (float X = 0; X < Width; X += Strip)
    {
        const float T = FMath::Clamp(X / (Width * 0.82f), 0.0f, 1.0f);
        const float Opacity = bHasBackdrop ? FMath::Lerp(0.94f, 0.04f, T * T * (3 - 2 * T)) : 1.0f;
        Panel(X, 0, FMath::Min(Strip, Width - X), Height, FLinearColor(0.002f, 0.006f, 0.012f, Opacity));
    }
    Panel(0, Height - 58 * UIScale, Width, 58 * UIScale, FLinearColor(0.002f, 0.005f, 0.009f, 0.6f));
    UIRegions.Add(FBox2D(FVector2D::ZeroVector, FVector2D(Width, Height)));
    const TCHAR* MapNames[] = { TEXT("Shattered Rift"), TEXT("Glass Basin"), TEXT("Iron Reach") };
    const TCHAR* MapDescriptions[] = {
        TEXT("Split the front. Control the crossing."),
        TEXT("Circle the basin. Find the open flank."),
        TEXT("Hold the lanes. Break through the ridges.")
    };
    if (bCompactLayout)
    {
        const auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
        // Keep the menu inside the asymmetric safe area. The Dynamic Island
        // swaps sides between the two landscape orientations.
        const float X = MobileLayout.Left, W = MobileLayout.Right - MobileLayout.Left;
        const float SecondaryY = FMath::Min(319 * UIScale + SafeTopOffset, MobileLayout.Bottom - 44 * UIScale);
        const float SkirmishY = SecondaryY - 51 * UIScale;
        const float LengthY = SkirmishY - 51 * UIScale;
        const float DifficultyY = LengthY - 62 * UIScale;
        const float MapY = DifficultyY - 62 * UIScale;
        Label(TEXT("CINDERLINE"), X - UIScale, MapY - 54 * UIScale, White, 2.0f);
        SingleLineLabel(MapDescriptions[SelectedMap], X, MapY - 18 * UIScale, W, Muted, 0.66f);
        const float MapW = (W - 16 * UIScale) / 3;
        for (int I = 0; I < 3; ++I)
            Button(MapNames[I], TEXT("map"), I, X + I * (MapW + 8 * UIScale), MapY, MapW, SelectedMap == I);
        const cinder::AIDifficulty Difficulty = PC ? PC->SelectedAIDifficulty() : cinder::AIDifficulty::Normal;
        SingleLineLabel(FString::Printf(TEXT("AI DIFFICULTY / %s"),
            UTF8_TO_TCHAR(cinder::aiDifficultyDescription(Difficulty))),
            X, DifficultyY - 14 * UIScale, W, White, 0.62f);
        const float DifficultyGap = 4 * UIScale;
        const float DifficultyW = (W - DifficultyGap * 4) / 5;
        const TCHAR* CompactDifficultyNames[] = {
            TEXT("VERY EASY"), TEXT("EASY"), TEXT("NORMAL"), TEXT("HARD"), TEXT("EXPERT")
        };
        for (int I = 0; I < static_cast<int>(cinder::kAIDifficultyCount); ++I)
            DifficultyButton(CompactDifficultyNames[I], I, X + I * (DifficultyW + DifficultyGap),
                DifficultyY, DifficultyW, 44 * UIScale,
                Difficulty == cinder::aiDifficultyAt(static_cast<std::size_t>(I)));
        const cinder::MatchLength Length = PC ? PC->SelectedMatchLength() : cinder::MatchLength::Standard;
        SingleLineLabel(FString::Printf(TEXT("SOLO LENGTH / %s"), UTF8_TO_TCHAR(cinder::matchLengthDescription(Length))),
            X, LengthY - 14 * UIScale, W, White, 0.62f);
        const float LengthW = (W - 8 * UIScale) / 3;
        for (int I = 0; I < static_cast<int>(cinder::MatchLength::Count); ++I)
            DifficultyButton(UTF8_TO_TCHAR(cinder::matchLengthName(cinder::matchLengthAt(I))), I,
                X + I * (LengthW + 4 * UIScale), LengthY, LengthW, 44 * UIScale,
                Length == cinder::matchLengthAt(I), TEXT("matchlength"));
        Button(TEXT("START SKIRMISH"), TEXT("start"), SelectedMap, X, SkirmishY, W * 0.55f - 5 * UIScale, true);
        Button(TEXT("CONTINUE SAVE"), TEXT("load"), 0, X + W * 0.55f + 5 * UIScale, SkirmishY, W * 0.45f - 5 * UIScale);
        const float SecondaryGap = 5 * UIScale;
        const float SecondaryW = (W - 2 * SecondaryGap) / 3;
        Button(PC && PC->HasCompletedTutorial() ? TEXT("REPLAY TRAINING") : TEXT("GUIDED TRAINING"),
            TEXT("tutorial"), 0, X, SecondaryY, SecondaryW, true);
        Button(TEXT("FIELD GUIDE"), TEXT("help"), 0,
            X + SecondaryW + SecondaryGap, SecondaryY, SecondaryW);
        Button(TEXT("MULTIPLAYER"), TEXT("online"), 0,
            X + 2 * (SecondaryW + SecondaryGap), SecondaryY, SecondaryW, true);
        return;
    }
    const auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
    const float X = 72 * UIScale, W = 540 * UIScale;
    Label(TEXT("THE CAIRN ASSEMBLY"), X, 83 * UIScale, Mint, 0.78f);
    Label(TEXT("CINDERLINE"), X - 4 * UIScale, 122 * UIScale, White, 3.65f);
    Label(TEXT("Build your foothold."), X, 219 * UIScale, White, 1.17f);
    Panel(X, 254 * UIScale, W, UIScale, FLinearColor(0.10f, 0.17f, 0.19f, 0.8f));
    Label(TEXT("DEPLOYMENT SECTOR"), X, 269 * UIScale, Muted, 0.68f);
    const float MapW = (W - 16 * UIScale) / 3;
    for (int I = 0; I < 3; ++I)
        Button(MapNames[I], TEXT("map"), I, X + I * (MapW + 8 * UIScale), 292 * UIScale, MapW, SelectedMap == I);
    Label(MapDescriptions[SelectedMap], X, 342 * UIScale, Muted, 0.72f);
    Label(TEXT("AI DIFFICULTY"), X, 364 * UIScale, Muted, 0.62f);
    const cinder::AIDifficulty Difficulty = PC ? PC->SelectedAIDifficulty() : cinder::AIDifficulty::Normal;
    const float DifficultyGap = 6 * UIScale;
    const float DifficultyW = (W - DifficultyGap * 4) / 5;
    for (int I = 0; I < static_cast<int>(cinder::kAIDifficultyCount); ++I)
        DifficultyButton(UTF8_TO_TCHAR(cinder::aiDifficultyName(cinder::aiDifficultyAt(static_cast<std::size_t>(I)))),
            I, X + I * (DifficultyW + DifficultyGap), 384 * UIScale, DifficultyW, 28 * UIScale,
            Difficulty == cinder::aiDifficultyAt(static_cast<std::size_t>(I)));
    SingleLineLabel(UTF8_TO_TCHAR(cinder::aiDifficultyDescription(Difficulty)),
        X, 424 * UIScale, W, White, 0.65f);
    const cinder::MatchLength Length = PC ? PC->SelectedMatchLength() : cinder::MatchLength::Standard;
    Label(TEXT("SOLO LENGTH"), X, 446 * UIScale, Muted, 0.62f);
    const float LengthW = (W - 12 * UIScale) / 3;
    for (int I = 0; I < static_cast<int>(cinder::MatchLength::Count); ++I)
        DifficultyButton(UTF8_TO_TCHAR(cinder::matchLengthName(cinder::matchLengthAt(I))), I,
            X + I * (LengthW + 6 * UIScale), 466 * UIScale, LengthW, 28 * UIScale,
            Length == cinder::matchLengthAt(I), TEXT("matchlength"));
    SingleLineLabel(UTF8_TO_TCHAR(cinder::matchLengthDescription(Length)), X, 506 * UIScale, W, White, 0.65f);
    Button(TEXT("START SKIRMISH   [ENTER]"), TEXT("start"), SelectedMap, X, 534 * UIScale, 302 * UIScale, true);
    Button(TEXT("CONTINUE SAVE"), TEXT("load"), 0, X + 314 * UIScale, 534 * UIScale, 226 * UIScale);
    Button(PC && PC->HasCompletedTutorial() ? TEXT("REPLAY TRAINING  [T]") : TEXT("GUIDED TRAINING  [T]"), TEXT("tutorial"), 0, X, 590 * UIScale, 302 * UIScale, true);
    Button(TEXT("FIELD GUIDE  [F1]"), TEXT("help"), 0, X + 314 * UIScale, 590 * UIScale, 226 * UIScale);
    Button(TEXT("MULTIPLAYER"), TEXT("online"), 0, X, 646 * UIScale, W, true);
    Label(TEXT("SOLO SKIRMISH"), X, Height - 16 * UIScale, Mint, 0.70f);
    SingleLineLabel(FString(DesktopControlHint) + TEXT(" / Right-click command"), 317 * UIScale, Height - 16 * UIScale,
        Width - 317 * UIScale - X, Muted, 0.70f);

}

void ACinderHUD::DrawTutorialOffer(ACinderPlayerController* PC)
{
    if (!PC || !PC->IsTutorialOfferPending()) return;
    Buttons.Reset();
    UIRegions.Reset();
    Minimap.Init();
    Panel(0, 0, Width, Height, FLinearColor(0.002f, 0.008f, 0.014f, 0.78f));
    UIRegions.Add(FBox2D(FVector2D::ZeroVector, FVector2D(Width, Height)));

    const float S = UIScale;
    const float SafeLeft = bCompactLayout ? MobileLayout.Left : Margin;
    const float SafeRight = bCompactLayout ? MobileLayout.Right : Width - Margin;
    const float SafeTop = bCompactLayout ? MobileLayout.Top : Margin;
    const float SafeBottom = bCompactLayout ? MobileLayout.Bottom : Height - Margin;
    const float W = FMath::Min((bCompactLayout ? 590.0f : 560.0f) * S, SafeRight - SafeLeft);
    const float H = FMath::Min((bCompactLayout ? 248.0f : 270.0f) * S, SafeBottom - SafeTop - 12 * S);
    const float X = FMath::Clamp((Width - W) * 0.5f, SafeLeft, SafeRight - W);
    const float Y = FMath::Clamp((Height - H) * 0.5f, SafeTop + 6 * S, SafeBottom - H - 6 * S);

    Panel(X, Y, W, H, FLinearColor(0.004f, 0.018f, 0.026f, 0.98f));
    Panel(X, Y, 5 * S, H, Mint);
    Label(TEXT("YOUR FIRST COMMAND"), X + 24 * S, Y + 18 * S, Mint, 0.66f);
    Label(TEXT("LEARN THE FRONTIER"), X + 24 * S, Y + 40 * S, White, bCompactLayout ? 1.22f : 1.42f);
    WrappedLabel(TEXT("Play a guided example battle that teaches mining, construction, research, reinforcement, and defense."),
        X + 24 * S, Y + 78 * S, W - 48 * S, White, bCompactLayout ? 0.68f : 0.76f);
    WrappedLabel(TEXT("The opponent waits while you learn, then sends one small raid when your army is ready. We guide you through defense and on to victory."),
        X + 24 * S, Y + 124 * S, W - 48 * S, Muted, bCompactLayout ? 0.66f : 0.72f);

    const float Gap = 8 * S;
    const float ButtonW = (W - 48 * S - Gap) * 0.5f;
    const float ButtonY = Y + H - 62 * S;
    Button(TEXT("GET GUIDED TUTORIAL"), TEXT("onboardlearn"), 0,
        X + 24 * S, ButtonY, ButtonW, true);
    Button(TEXT("SKIP FOR NOW"), TEXT("onboardskip"), 0,
        X + 24 * S + ButtonW + Gap, ButtonY, ButtonW);
}

void ACinderHUD::DrawHelp(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    Buttons.Reset();
    UIRegions.Reset();
    Minimap.Init();
    Panel(0, 0, Width, Height, FLinearColor(0.002f, 0.008f, 0.014f, 0.96f));
    UIRegions.Add(FBox2D(FVector2D::ZeroVector, FVector2D(Width, Height)));

    const float S = UIScale;
    const int32 PageIndex = FMath::Clamp(PC->HelpPage(), 0, CinderHelp::TopicCount - 1);
    const int32 ReferenceIndex = FMath::Clamp(PC->HelpReference(), 0, CinderHelp::ReferenceCount - 1);
    const bool bReference = PageIndex == CinderHelp::ReferencePage;
    const bool bTouch = PC->HelpUsesTouch();
    const bool bOnline = Battle->IsOnlineMatch();
    FCinderHelpPage Page = CinderHelp::Page(PageIndex, bTouch, ReferenceIndex);
    if (bOnline && PageIndex == 7)
    {
        for (FCinderHelpSection& Section : Page.Sections)
        {
            if (Section.Heading == TEXT("Skirmish saves"))
            {
                Section.Heading = TEXT("Online match");
                Section.Body = TEXT("Online matches cannot be saved or loaded. The server keeps running while this guide is open.");
            }
        }
    }
    const FString BackLabel = Battle->IsMenu() ? TEXT("BACK TO MENU") : bOnline ? TEXT("BACK TO MATCH MENU") : TEXT("BACK TO PAUSE");
    const FString GuideLabel = Battle->IsMenu() ? TEXT("FIELD GUIDE") : bOnline ? TEXT("FIELD GUIDE / MATCH CONTINUES") : TEXT("FIELD GUIDE / MATCH PAUSED");
    if (LastHelpPage != PageIndex || LastHelpReference != ReferenceIndex)
    {
        CompactHelpSection = 0;
        LastHelpPage = PageIndex;
        LastHelpReference = ReferenceIndex;
    }

    if (bCompactLayout)
    {
        const float X = Margin - 12 * S;
        const float Y = 6 * S + SafeTopOffset;
        const float W = Width - 2 * X;
        const float H = Height - Y - SafeBottomOffset - 6 * S;
        const float Pad = 14 * S;
        Panel(X, Y, W, H, PanelInk);
        Panel(X, Y, 4 * S, H, Mint);
        Label(GuideLabel, X + Pad, Y + 8 * S, Mint, 0.62f);
        SingleLineLabel(Page.Title, X + Pad, Y + 28 * S, W - 150 * S, White, 1.05f);
        Button(BackLabel, TEXT("helpclose"), 0, X + W - 126 * S, Y + 6 * S, 112 * S);
        SingleLineLabel(Page.Subtitle, X + Pad, Y + 52 * S, W - 2 * Pad, Muted, 0.68f);

        const float NavigationY = Y + H - 50 * S;
        const float ReferenceY = NavigationY - 50 * S;
        const float ContentBottom = (bReference ? ReferenceY : NavigationY) - 7 * S;
        const float TextW = W - 2 * Pad;
        if (!Page.Sections.IsEmpty())
        {
            CompactHelpSection = FMath::Clamp(CompactHelpSection, 0, Page.Sections.Num() - 1);
            const FCinderHelpSection& Section = Page.Sections[CompactHelpSection];
            const float SectionY = Y + 76 * S;
            const float ArrowW = 44 * S;
            const float ArrowGap = 6 * S;
            const float ArrowsX = X + W - Pad - ArrowW * 2 - ArrowGap;
            Panel(X + Pad, SectionY - 2 * S, 3 * S, ContentBottom - SectionY, FLinearColor(0.10f, 0.30f, 0.28f, 0.8f));
            SingleLineLabel(FString::Printf(TEXT("%s  /  %d OF %d"), *Section.Heading, CompactHelpSection + 1, Page.Sections.Num()),
                X + Pad + 12 * S, SectionY + 10 * S, ArrowsX - X - Pad - 20 * S, Amber, 0.68f);
            Button(TEXT("<"), TEXT("helpsection"), (CompactHelpSection + Page.Sections.Num() - 1) % Page.Sections.Num(), ArrowsX, SectionY, ArrowW);
            Button(TEXT(">"), TEXT("helpsection"), (CompactHelpSection + 1) % Page.Sections.Num(), ArrowsX + ArrowW + ArrowGap, SectionY, ArrowW);
            WrappedLabel(Section.Body, X + Pad + 12 * S, SectionY + 52 * S, TextW - 12 * S, White, 0.74f);
        }

        if (bReference)
        {
            const int32 Previous = (ReferenceIndex + CinderHelp::ReferenceCount - 1) % CinderHelp::ReferenceCount;
            const int32 Next = (ReferenceIndex + 1) % CinderHelp::ReferenceCount;
            const FString PreviousName = CinderHelp::Page(PageIndex, bTouch, Previous).Title;
            const FString NextName = CinderHelp::Page(PageIndex, bTouch, Next).Title;
            const float RefW = (W - 2 * Pad - 7 * S) * 0.5f;
            Button(FString::Printf(TEXT("< %s"), *PreviousName), TEXT("helpreference"), Previous, X + Pad, ReferenceY, RefW);
            Button(FString::Printf(TEXT("%s >"), *NextName), TEXT("helpreference"), Next, X + Pad + RefW + 7 * S, ReferenceY, RefW);
        }

        const int32 PreviousPage = (PageIndex + CinderHelp::TopicCount - 1) % CinderHelp::TopicCount;
        const int32 NextPage = (PageIndex + 1) % CinderHelp::TopicCount;
        const float TopicW = 112 * S;
        const float ModeW = 92 * S;
        const float Gap = 6 * S;
        const float RowW = TopicW * 2 + ModeW * 2 + Gap * 3;
        const float RowX = X + (W - RowW) * 0.5f;
        Button(TEXT("< TOPIC"), TEXT("helppage"), PreviousPage, RowX, NavigationY, TopicW);
        Button(TEXT("DESKTOP"), TEXT("helpinput"), 0, RowX + TopicW + Gap, NavigationY, ModeW, !bTouch);
        Button(TEXT("TOUCH"), TEXT("helpinput"), 1, RowX + TopicW + ModeW + Gap * 2, NavigationY, ModeW, bTouch);
        Button(TEXT("TOPIC >"), TEXT("helppage"), NextPage,
            RowX + TopicW + ModeW * 2 + Gap * 3, NavigationY, TopicW);
        return;
    }

    const float W = FMath::Min(1160 * S, Width - 48 * S);
    const float H = FMath::Min(640 * S, Height - 40 * S);
    const float X = (Width - W) * 0.5f;
    const float Y = (Height - H) * 0.5f;
    const float SidebarW = 230 * S;
    const float ContentX = X + SidebarW + 30 * S;
    const float ContentW = W - SidebarW - 54 * S;
    Panel(X, Y, W, H, PanelInk);
    Panel(X, Y, 5 * S, H, Mint);
    Panel(X + SidebarW, Y, S, H, FLinearColor(0.08f, 0.15f, 0.17f));
    Label(GuideLabel, X + 20 * S, Y + 18 * S, Mint, 0.68f);
    for (int32 I = 0; I < CinderHelp::TopicCount; ++I)
        Button(CinderHelp::TopicTitle(I), TEXT("helppage"), I, X + 18 * S, Y + (62 + I * 51) * S, SidebarW - 36 * S, I == PageIndex);

    SingleLineLabel(Page.Title, ContentX, Y + 22 * S, ContentW - 178 * S, White, 1.65f);
    Button(BackLabel, TEXT("helpclose"), 0, X + W - 170 * S, Y + 16 * S, 148 * S);
    WrappedLabel(Page.Subtitle, ContentX, Y + 62 * S, ContentW, Muted, 0.80f);
    Panel(ContentX, Y + 96 * S, ContentW, S, FLinearColor(0.08f, 0.20f, 0.21f));

    float TextY = Y + 120 * S;
    for (const FCinderHelpSection& Section : Page.Sections)
    {
        Label(Section.Heading, ContentX, TextY, Amber, 0.78f);
        TextY += 24 * S;
        TextY += WrappedLabel(Section.Body, ContentX, TextY, ContentW, White, 0.84f);
        TextY += 18 * S;
    }

    const float ControlY = Y + H - 58 * S;
    Button(TEXT("DESKTOP"), TEXT("helpinput"), 0, ContentX, ControlY, 126 * S, !bTouch);
    Button(TEXT("TOUCH"), TEXT("helpinput"), 1, ContentX + 134 * S, ControlY, 110 * S, bTouch);
    if (bReference)
    {
        const int32 Previous = (ReferenceIndex + CinderHelp::ReferenceCount - 1) % CinderHelp::ReferenceCount;
        const int32 Next = (ReferenceIndex + 1) % CinderHelp::ReferenceCount;
        const FString PreviousName = CinderHelp::Page(PageIndex, bTouch, Previous).Title;
        const FString NextName = CinderHelp::Page(PageIndex, bTouch, Next).Title;
        const float RefW = (ContentW - 266 * S) * 0.5f;
        Button(FString::Printf(TEXT("< %s"), *PreviousName), TEXT("helpreference"), Previous, ContentX + 258 * S, ControlY, RefW);
        Button(FString::Printf(TEXT("%s >"), *NextName), TEXT("helpreference"), Next,
            ContentX + 266 * S + RefW, ControlY, RefW);
    }
    else
    {
        SingleLineLabel(TEXT("F1 / ESC / ENTER closes  |  LEFT / RIGHT topic  |  T input"),
            ContentX + 270 * S, ControlY + 13 * S, ContentW - 270 * S, Muted, 0.70f);
    }
}

FCinderTutorialGuide ACinderHUD::CurrentTutorialGuide() const
{
    const auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
    const auto* Battle = PC ? PC->Battlefield() : nullptr;
    if (!Battle || !Battle->Tutorial().IsActive()) return {};
    FCinderTutorialContext Context;
    Context.Selection = PC->Selection();
    Context.bCompact = bCompactLayout || PLATFORM_IOS || PLATFORM_ANDROID;
    Context.Catalog = PC->bBuildMenu ? SheetContext : CompactSheet;
    Context.TrainKind = static_cast<cinder::Kind>(GlobalTrainKind);
    Context.TrainQuantity = GlobalTrainQuantity;
    Context.bTrainKindChosen = bTutorialTrainKindChosen;
    Context.bTrainQuantityChosen = bTutorialTrainQuantityChosen;
    Context.bBuildMode = PC->IsBuildMode();
    Context.BuildingKind = PC->BuildingKind();
    Context.bAttackMove = PC->IsAttackMoveMode();
    Context.bMoveCommand = PC->IsMoveCommandMode();
    Context.bDefendCommand = PC->IsDefendCommandMode();
    Context.bPatrolCommand = PC->IsPatrolCommandMode();
    Context.bEscortCommand = PC->IsEscortCommandMode();
    Context.bProductionRally = PC->IsProductionRallyMode();
    Context.PinnedProducer = PC->IsProductionRallyMode() ? PC->ProductionRallyProducer() : PinnedProducerId;
    Context.ArmyTab = ArmyPanelTab;
    return Battle->Tutorial().Guide(Battle->Sim(), Context, bCompactLayout || PLATFORM_IOS || PLATFORM_ANDROID);
}

bool ACinderHUD::FocusTutorialTarget()
{
    auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
    auto* Battle = PC ? PC->Battlefield() : nullptr;
    auto* Rig = PC ? Cast<ACinderCamera>(PC->GetPawn()) : nullptr;
    if (!Battle || !Rig || Battle->IsPaused() || PC->IsHelpOpen() || Battle->Sim().winner() != -1) return false;
    const auto Guide = CurrentTutorialGuide();
    if (Guide.Target != ECinderTutorialGuideTarget::Entity && Guide.Target != ECinderTutorialGuideTarget::Ground) return false;
    cinder::Vec2 Point = Guide.Point;
    if (Guide.Entity)
    {
        const auto* Entity = Battle->Sim().find(Guide.Entity);
        if (!Entity || !Entity->alive()) return false;
        Point = Battle->RenderPosition(*Entity);
    }
    // Finding a target must not perform the selection or issue an order for the player.
    // A closer view keeps a target near the map edge out from under the HUD.
    if (Rig->Distance() > 900) Rig->Zoom(900 - Rig->Distance());
    Rig->Focus(FVector(Point.x, Point.y, 0));
    UE_LOG(LogCinderHUD, Verbose, TEXT("CINDERLINE_TUTORIAL_FOCUS point=(%.1f,%.1f) distance=%.1f"),
        Point.x, Point.y, Rig->Distance());
    return true;
}

void ACinderHUD::DrawTutorialPointer(FVector2D Center, const FBox2D& Bounds, bool bWorld)
{
    const float S = UIScale;
    const float Pulse = 0.76f + 0.24f * FMath::Sin(GetWorld()->GetRealTimeSeconds() * 3.0f);
    const FLinearColor Color = Amber.CopyWithNewOpacity(Pulse);
    if (bWorld)
    {
        const float R = 21 * S;
        for (int32 Segment = 0; Segment < 24; ++Segment)
        {
            const float A = Segment * 2 * PI / 24;
            const float B = (Segment + 1) * 2 * PI / 24;
            DrawLine(Center.X + FMath::Cos(A) * R, Center.Y + FMath::Sin(A) * R,
                Center.X + FMath::Cos(B) * R, Center.Y + FMath::Sin(B) * R, Color, 2 * S);
        }
    }
    else
    {
        const FVector2D Lo = Bounds.Min - FVector2D(2 * S), Hi = Bounds.Max + FVector2D(2 * S);
        DrawLine(Lo.X, Lo.Y, Hi.X, Lo.Y, Color, 2 * S);
        DrawLine(Hi.X, Lo.Y, Hi.X, Hi.Y, Color, 2 * S);
        DrawLine(Hi.X, Hi.Y, Lo.X, Hi.Y, Color, 2 * S);
        DrawLine(Lo.X, Hi.Y, Lo.X, Lo.Y, Color, 2 * S);
    }
    // Point into the side rail from the open battlefield, away from the
    // resource chips directly above it. The arrow never takes input.
    if (!bWorld && Bounds.Max.X <= MobileLayout.GlobalActions.Max.X + S
        && Bounds.Min.Y >= MobileLayout.GlobalActions.Min.Y - S
        && Bounds.Max.Y <= MobileLayout.GlobalActions.Max.Y + S)
    {
        const float TipX = Bounds.Max.X + 5*S;
        DrawLine(TipX+22*S,Center.Y,TipX,Center.Y,Ink,6*S);
        DrawLine(TipX+22*S,Center.Y,TipX,Center.Y,Color,3*S);
        DrawLine(TipX+8*S,Center.Y-7*S,TipX,Center.Y,Color,3*S);
        DrawLine(TipX+8*S,Center.Y+7*S,TipX,Center.Y,Color,3*S);
        return;
    }
    // A short arrow points at the real hit target. It never takes input.
    const float TipY = bWorld ? Center.Y - 24 * S : Bounds.Min.Y + 4 * S;
    const float TailY = TipY - (bWorld ? 24 : 15) * S;
    DrawLine(Center.X, TailY, Center.X, TipY, Ink, 6 * S);
    DrawLine(Center.X, TailY, Center.X, TipY, Color, 3 * S);
    DrawLine(Center.X - 7 * S, TipY - 8 * S, Center.X, TipY, Color, 3 * S);
    DrawLine(Center.X + 7 * S, TipY - 8 * S, Center.X, TipY, Color, 3 * S);
}

void ACinderHUD::DrawTutorialCard(ACinderBattlefield* Battle, bool bForceShow)
{
    auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
    if (!PC || !Battle->Tutorial().IsActive()) return;
    const int32 Step = static_cast<int32>(Battle->Tutorial().Step());
    if (TutorialCardStep != Step)
    {
        TutorialCardStep = Step;
        bTutorialTrainKindChosen = bTutorialTrainQuantityChosen = false;
    }
    const FCinderTutorialGuide Guide = CurrentTutorialGuide();
    if (Guide.Instruction.IsEmpty()) return;
    const float S = UIScale;
    const bool bWorld = Guide.Target == ECinderTutorialGuideTarget::Entity || Guide.Target == ECinderTutorialGuideTarget::Ground;
    TutorialCardBounds.Init(); TutorialTargetBounds.Init();
    bTutorialTargetVisible = bTutorialNeedsShow = bTutorialTargetClear = false;
    TutorialPointer = FVector2D::ZeroVector;
    if (Guide.Target == ECinderTutorialGuideTarget::Button)
    {
        for (const auto& Entry : Buttons)
        {
            if (Entry.Action != Guide.ButtonAction || Entry.Argument != Guide.ButtonArgument || Entry.EntityId != Guide.ButtonEntity) continue;
            TutorialTargetBounds = Entry.Bounds;
            TutorialPointer = Entry.Bounds.GetCenter();
            bTutorialTargetVisible = bTutorialTargetClear = true;
            break;
        }
    }
    else if (bWorld)
    {
        const bool bProjected = PC->ProjectTutorialTarget(Guide.Entity, Guide.Point, TutorialPointer);
        const FBox2D View(FVector2D(Margin + 26 * S, 84 * S + SafeTopOffset),
            FVector2D(Width - Margin - 26 * S, Height - 74 * S - SafeBottomOffset));
        bTutorialTargetVisible = bProjected && View.IsInside(TutorialPointer);
        bTutorialTargetClear = bTutorialTargetVisible && !ContainsUI(TutorialPointer);
        bTutorialNeedsShow = bForceShow || !bTutorialTargetClear;
        TutorialTargetBounds = FBox2D(TutorialPointer - FVector2D(24 * S), TutorialPointer + FVector2D(24 * S));
    }
    const bool bInlineCancel = Guide.ButtonAction == TEXT("tutorialclearmode") && !bTutorialTargetVisible;
    const bool bInlineAction = bTutorialNeedsShow || bInlineCancel;
    const FString Instruction = bTutorialNeedsShow
        ? (bCompactLayout ? TEXT("Tap SHOW to find the highlighted target.") : TEXT("Click SHOW to find the highlighted target.")) : Guide.Instruction;
    const FString Explanation = Guide.Explanation;
    const float Pad = 10 * S;
    const float Top = 65 * S + SafeTopOffset;
    const bool bSheet = CompactSheet != SheetNone || PC->bBuildMenu;
    const float Right = bCompactLayout ? MobileLayout.Right : Width - Margin;
    const float Left = MobileLayout.Drawer.Min.X;
    TArray<FBox2D> Candidates;
    const auto AddCandidate = [&](float X, float W, float Y)
    {
        if (W < 192 * S || X < Left || X + W > Right + 1) return;
        const float TextW = W - 2 * Pad;
        const float H = 24 * S + WrappedHeight(Instruction, TextW, 0.82f)
            + (Explanation.IsEmpty() ? 0 : 4 * S + WrappedHeight(Explanation, TextW, 0.67f))
            + 10 * S + (bInlineAction ? 48 * S : 0);
        if (Y + H > Height - 66 * S - SafeBottomOffset) return;
        Candidates.Add(FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)));
    };
    if (bSheet)
        AddCandidate(MobileLayout.Drawer.Max.X + 8 * S, Right - MobileLayout.Drawer.Max.X - 8 * S, FMath::Max(Top,static_cast<float>(MobileLayout.Navigation.Max.Y)+8*S));
    const float NormalW = FMath::Min(300 * S, Right - Left);
    AddCandidate(Left, NormalW, Top);
    AddCandidate(Right - NormalW, NormalW, Top);
    AddCandidate(Right - 220 * S, 220 * S, Top + 112 * S);
    const auto OverlapArea = [](const FBox2D& A, const FBox2D& B)
    {
        return FMath::Max(0.0, FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X))
            * FMath::Max(0.0, FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y));
    };
    double BestScore = TNumericLimits<double>::Max();
    for (const auto& Candidate : Candidates)
    {
        double Score = 0;
        for (const auto& Region : UIRegions) Score += OverlapArea(Candidate, Region);
        for (const auto& Entry : Buttons) Score += OverlapArea(Candidate, Entry.Bounds);
        if (bTutorialTargetVisible) Score += 20 * OverlapArea(Candidate, TutorialTargetBounds.ExpandBy(12 * S));
        if (Score < BestScore) { BestScore = Score; TutorialCardBounds = Candidate; }
    }
    if (!TutorialCardBounds.bIsValid) return;
    // Card placement is part of hit testing. If every available position
    // would cover the next world click, expose camera recovery instead.
    if (bWorld && !bInlineAction && TutorialCardBounds.Intersect(TutorialTargetBounds))
    {
        DrawTutorialCard(Battle, true);
        return;
    }
    const float X = TutorialCardBounds.Min.X, Y = TutorialCardBounds.Min.Y;
    const float W = TutorialCardBounds.GetSize().X, H = TutorialCardBounds.GetSize().Y;
    Surface(X, Y, W, H, Ink, 8*S);
    Surface(X, Y+8*S, 2*S, H-16*S, Guide.bWaiting ? Mint : Amber, S);
    Label(FString::Printf(TEXT("TRAINING %d/%d   %d/%d"), Step + 1, FCinderTutorial::StepCount,
        Guide.ActionIndex, Guide.ActionCount), X + Pad, Y + 6 * S, Guide.bWaiting ? Mint : Amber, 0.58f);
    float TextY = Y + 24 * S;
    TextY += WrappedLabel(Instruction, X + Pad, TextY, W - 2 * Pad, White, 0.82f);
    if (!Explanation.IsEmpty()) WrappedLabel(Explanation, X + Pad, TextY + 4 * S, W - 2 * Pad, Muted, 0.67f);
    UIRegions.Add(TutorialCardBounds);
    if (bInlineAction)
    {
        Button(bInlineCancel ? TEXT("CANCEL ORDER") : TEXT("SHOW"), bInlineCancel ? TEXT("tutorialclearmode") : TEXT("tutorialshow"), 0,
            X + Pad, Y + H - 48 * S, bInlineCancel ? W - 2 * Pad : 72 * S, true);
        TutorialTargetBounds = Buttons.Last().Bounds;
        TutorialPointer = TutorialTargetBounds.GetCenter();
        bTutorialTargetVisible = true;
        bTutorialTargetClear = true;
        DrawTutorialPointer(TutorialPointer, TutorialTargetBounds, false);
    }
    else if (bTutorialTargetVisible && bTutorialTargetClear)
    {
        if (bWorld && TutorialCardBounds.IsInside(TutorialPointer)) bTutorialTargetClear = false;
        else DrawTutorialPointer(TutorialPointer, TutorialTargetBounds, bWorld);
    }
}

#if UE_BUILD_DEVELOPMENT
void ACinderHUD::LogTutorialGuidance() const
{
    const auto Guide = CurrentTutorialGuide();
    UE_LOG(LogCinderHUD, Display, TEXT("CINDERLINE_TUTORIAL_GUIDE target=%d action=%s argument=%d entity=%u step=%d micro=%d/%d waiting=%d pointer=(%.1f,%.1f) card=(%.1f,%.1f,%.1f,%.1f) target_bounds=(%.1f,%.1f,%.1f,%.1f) target_visible=%d needs_show=%d target_clear=%d"),
        static_cast<int32>(Guide.Target), Guide.ButtonAction.IsEmpty() ? TEXT("none") : *Guide.ButtonAction,
        Guide.ButtonArgument, Guide.Entity ? Guide.Entity : Guide.ButtonEntity, TutorialCardStep,
        Guide.ActionIndex, Guide.ActionCount, Guide.bWaiting, TutorialPointer.X, TutorialPointer.Y,
        TutorialCardBounds.Min.X, TutorialCardBounds.Min.Y, TutorialCardBounds.Max.X, TutorialCardBounds.Max.Y,
        TutorialTargetBounds.Min.X, TutorialTargetBounds.Min.Y, TutorialTargetBounds.Max.X, TutorialTargetBounds.Max.Y,
        bTutorialTargetVisible, bTutorialNeedsShow, bTutorialTargetClear);
    UE_LOG(LogCinderHUD, Display, TEXT("CINDERLINE_TUTORIAL_INSTRUCTION text=%s"), *Guide.Instruction);
}
#endif

bool ACinderHUD::RefreshFogRuns(uint64 SourceRevision, int32 SourceDimension,
    const TArray<uint8>& SourceCells, uint64& CachedRevision, int32& CachedDimension,
    TArray<FMinimapFogRun>& CachedRuns)
{
    if (CachedRevision == SourceRevision && CachedDimension == SourceDimension) return false;
    CachedRevision = SourceRevision;
    CachedDimension = SourceDimension;
    CachedRuns.Reset();
    if (SourceDimension <= 0) return true;

    const bool bValidSnapshot = static_cast<int64>(SourceCells.Num())
        == static_cast<int64>(SourceDimension) * SourceDimension;
    auto StateAt = [&](int32 X, int32 Y)
    {
        if (!bValidSnapshot) return uint8(0);
        return FMath::Min<uint8>(SourceCells[Y * SourceDimension + X], uint8(2));
    };
    CachedRuns.Reserve(SourceDimension * 2);
    for (int32 Y = 0; Y < SourceDimension; ++Y)
    {
        int32 RunStart = 0;
        uint8 RunState = StateAt(0, Y);
        for (int32 X = 1; X <= SourceDimension; ++X)
        {
            const uint8 State = X < SourceDimension ? StateAt(X, Y) : uint8(255);
            if (State == RunState) continue;
            CachedRuns.Add(FMinimapFogRun{Y, RunStart, X, RunState});
            RunStart = X;
            RunState = State;
        }
    }
    return true;
}

void ACinderHUD::DrawMinimap(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const float Size = MobileLayout.Minimap.GetSize().X;
    const FVector2D Origin = MobileLayout.Minimap.Min;
    Minimap = FBox2D(Origin, Origin + FVector2D(Size));
    UIRegions.Add(Minimap);
    Surface(Origin.X - 4 * UIScale, Origin.Y - 4 * UIScale, Size + 8 * UIScale, Size + 8 * UIScale, Ink, 7 * UIScale);
    Panel(Origin.X - UIScale, Origin.Y - UIScale, Size + 2 * UIScale, Size + 2 * UIScale, FLinearColor(0.12f,0.25f,0.30f,0.75f));
    if (MinimapFogSource.Get() != Battle)
    {
        MinimapFogSource = Battle;
        MinimapFogRevision = MAX_uint64;
        MinimapFogDimension = 0;
    }
    RefreshFogRuns(Battle->FogRevision(), Battle->FogDimension(), Battle->FogCells(),
        MinimapFogRevision, MinimapFogDimension, MinimapFogRuns);
    const float Cell = Size / FMath::Max(1, MinimapFogDimension);
    const float CellExtent = Cell + 0.3f;
    const FLinearColor FogColors[] = {
        FLinearColor(0.014f, 0.025f, 0.04f),
        FLinearColor(0.065f, 0.12f, 0.14f),
        FLinearColor(0.11f, 0.25f, 0.25f)
    };
    for (const FMinimapFogRun& Run : MinimapFogRuns)
    {
        // Opaque equal-color cells have the same union as one row run.
        // Keep the original last-cell edge and row order at color boundaries.
        const float Left = Origin.X + Run.StartX * Cell;
        const float LastCellLeft = Origin.X + (Run.EndX - 1) * Cell;
        const float Right = LastCellLeft + CellExtent;
        Panel(Left, Origin.Y + Run.Y * Cell, Right - Left, CellExtent, FogColors[Run.State]);
    }
    const float K = Size / Battle->Sim().worldSize();
    for (const auto& E : Battle->KnownResources())
    {
        if (E.resource <= 0) continue;
        const float Dot = 2.5f * UIScale;
        Panel(Origin.X + E.pos.x * K - Dot * 0.5f, Origin.Y + E.pos.y * K - Dot * 0.5f, Dot, Dot, Amber);
    }
    for (const auto& E : Battle->Sim().entities())
    {
        if (!E.alive() || E.kind == cinder::Kind::Resource) continue;
        if (E.team != 0 && !Battle->Sim().visible(0, E.pos)) continue;
        const bool Resource = E.kind == cinder::Kind::Resource;
        if (Resource && E.resource <= 0) continue;
        const float Dot = (cinder::definition(E.kind).building ? 4.0f : 2.0f) * UIScale;
        Panel(Origin.X + E.pos.x * K - Dot * 0.5f, Origin.Y + E.pos.y * K - Dot * 0.5f, Dot, Dot, Resource ? Amber : CinderTeamColors::Accent(E.team));
    }
    const auto& Player = Battle->Sim().players()[0];
    if (Player.armyRallySet)
    {
        const float FlagX = Origin.X + Player.armyRally.x * K;
        const float FlagY = Origin.Y + Player.armyRally.y * K;
        DrawLine(FlagX, FlagY, FlagX, FlagY - 7 * UIScale, Ink, 3 * UIScale);
        DrawLine(FlagX, FlagY, FlagX, FlagY - 7 * UIScale, Mint, UIScale);
        Panel(FlagX, FlagY - 7 * UIScale, 5 * UIScale, 3 * UIScale, Mint);
    }
    cinder::Vec2 Corners[4];
    const FVector2D Screens[4] = { FVector2D(0, 0), FVector2D(Width, 0), FVector2D(Width, Height), FVector2D(0, Height) };
    bool Valid = true;
    for (int I = 0; I < 4; ++I) Valid &= PC->GroundPoint(Screens[I], Corners[I]);
    if (Valid) for (int I = 0; I < 4; ++I)
    {
        const auto A = Corners[I], B = Corners[(I + 1) % 4];
        DrawLine(Origin.X + FMath::Clamp(A.x * K, 0.0f, Size), Origin.Y + FMath::Clamp(A.y * K, 0.0f, Size), Origin.X + FMath::Clamp(B.x * K, 0.0f, Size), Origin.Y + FMath::Clamp(B.y * K, 0.0f, Size), White, UIScale);
    }

}

void ACinderHUD::DrawWorldIndicators(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const auto& Sim = Battle->Sim();
    auto WorldLine = [&](FVector A, FVector B, FLinearColor C, float Thickness)
    {
        FVector2D SA, SB;
        if (PC->ProjectWorldLocationToScreen(A, SA) && PC->ProjectWorldLocationToScreen(B, SB)) DrawLine(SA.X, SA.Y, SB.X, SB.Y, C, Thickness * UIScale);
    };
    auto WorldCircle = [&](cinder::Vec2 Center, float Radius, FLinearColor Color)
    {
        constexpr int32 Segments = 32;
        for (int32 Index = 0; Index < Segments; ++Index)
        {
            const float A = 2.0f * PI * Index / Segments;
            const float B = 2.0f * PI * (Index + 1) / Segments;
            WorldLine(FVector(Center.x + FMath::Cos(A) * Radius, Center.y + FMath::Sin(A) * Radius, 10),
                FVector(Center.x + FMath::Cos(B) * Radius, Center.y + FMath::Sin(B) * Radius, 10),
                Color, 1.0f);
        }
    };
    const auto DrawRallyFlag = [&](cinder::Vec2 Point, FLinearColor Color, const FString& Text)
    {
        FVector2D P;
        if (!PC->ProjectWorldLocationToScreen(FVector(Point.x, Point.y, 10), P)) return;
        if (P.X < 0 || P.X > Width || P.Y < 0 || P.Y > Height) return;
        const float S = UIScale;
        DrawLine(P.X, P.Y, P.X, P.Y - 28 * S, Ink, 4 * S);
        DrawLine(P.X, P.Y, P.X, P.Y - 28 * S, Color, 2 * S);
        Panel(P.X, P.Y - 28 * S, 15 * S, 10 * S, Ink);
        Panel(P.X + S, P.Y - 27 * S, 13 * S, 8 * S, Color);
        DrawLine(P.X - 5 * S, P.Y, P.X + 5 * S, P.Y, Color, 2 * S);
        if (!Text.IsEmpty())
        {
            Surface(P.X + 19 * S, P.Y - 28 * S, 83 * S, 19 * S, Ink.CopyWithNewOpacity(0.88f), 4 * S);
            SingleLineLabel(Text, P.X + 23 * S, P.Y - 24 * S, 75 * S, Color, 0.48f);
        }
    };
    const auto DrawFacingArrow = [&](cinder::Vec2 Point, float Angle, FLinearColor Color)
    {
        const cinder::Vec2 Tip{Point.x + FMath::Cos(Angle) * 58.0f, Point.y + FMath::Sin(Angle) * 58.0f};
        const cinder::Vec2 Left{Tip.x - FMath::Cos(Angle - 0.55f) * 18.0f,
            Tip.y - FMath::Sin(Angle - 0.55f) * 18.0f};
        const cinder::Vec2 Right{Tip.x - FMath::Cos(Angle + 0.55f) * 18.0f,
            Tip.y - FMath::Sin(Angle + 0.55f) * 18.0f};
        WorldLine(FVector(Point.x, Point.y, 16), FVector(Tip.x, Tip.y, 16), Color, 2.0f);
        WorldLine(FVector(Tip.x, Tip.y, 16), FVector(Left.x, Left.y, 16), Color, 2.0f);
        WorldLine(FVector(Tip.x, Tip.y, 16), FVector(Right.x, Right.y, 16), Color, 2.0f);
    };
    const auto& Player = Sim.players()[0];
    if (Player.armyRallySet)
        DrawRallyFlag(Player.armyRally, Mint,
            PC->IsProductionRallyMode() || (CompactSheet == SheetArmy && ArmyPanelTab == 3)
                ? TEXT("ARMY RALLY") : TEXT(""));
    if (!PC->Selection().empty())
    {
        const auto* Producer = Sim.find(PC->Selection().front());
        if (Producer && Producer->alive() && Producer->team == 0
            && cinder::definition(Producer->kind).building && Producer->rallyOverride)
            DrawRallyFlag(Producer->rally, Amber, TEXT("LOCAL RALLY"));
    }
    if (!PC->Selection().empty())
    {
        const cinder::Entity* Primary = Sim.find(PC->Selection().front());
        if (Primary && Primary->alive() && Primary->team == 0
            && Primary->kind != cinder::Kind::Resource && !cinder::definition(Primary->kind).building)
        {
            const auto DrawWaypoint = [&](cinder::Vec2 Point, cinder::Order Order, const FString& Text)
            {
                FVector2D Screen;
                if (!PC->ProjectWorldLocationToScreen(FVector(Point.x, Point.y, 12), Screen)
                    || Screen.X < 0 || Screen.X > Width || Screen.Y < 0 || Screen.Y > Height) return;
                const bool bAttackMove = Order == cinder::Order::AttackMove;
                const FLinearColor Color = bAttackMove ? Amber : Mint;
                const float R = 8 * UIScale;
                if (bAttackMove)
                {
                    DrawLine(Screen.X - R, Screen.Y - R, Screen.X + R, Screen.Y + R, Ink, 4 * UIScale);
                    DrawLine(Screen.X - R, Screen.Y + R, Screen.X + R, Screen.Y - R, Ink, 4 * UIScale);
                    DrawLine(Screen.X - R, Screen.Y - R, Screen.X + R, Screen.Y + R, Color, 2 * UIScale);
                    DrawLine(Screen.X - R, Screen.Y + R, Screen.X + R, Screen.Y - R, Color, 2 * UIScale);
                }
                else
                {
                    Panel(Screen.X - R, Screen.Y - R, R * 2, R * 2, Ink);
                    Panel(Screen.X - R + 2 * UIScale, Screen.Y - R + 2 * UIScale,
                        R * 2 - 4 * UIScale, R * 2 - 4 * UIScale, Color);
                }
                Surface(Screen.X + 11 * UIScale, Screen.Y - 10 * UIScale, 31 * UIScale, 18 * UIScale,
                    Ink.CopyWithNewOpacity(0.90f), 4 * UIScale);
                SingleLineLabel(Text, Screen.X + 15 * UIScale, Screen.Y - 6 * UIScale,
                    24 * UIScale, Color, 0.48f);
            };

            cinder::Vec2 From = Battle->RenderPosition(*Primary);
            bool bLinkReady = false;
            if (Primary->order == cinder::Order::Move || Primary->order == cinder::Order::AttackMove
                || Primary->order == cinder::Order::Defend)
            {
                WorldLine(FVector(From.x, From.y, 12), FVector(Primary->goal.x, Primary->goal.y, 12),
                    Muted.CopyWithNewOpacity(0.70f), 1.5f);
                DrawWaypoint(Primary->goal, Primary->order, TEXT("NOW"));
                if (Primary->hasArrivalFacing)
                    DrawFacingArrow(Primary->goal, Primary->arrivalFacing, Mint);
                From = Primary->goal;
                bLinkReady = true;
            }
            else if (Primary->order == cinder::Order::Patrol)
            {
                const cinder::SustainedOrderState& State = Primary->sustained;
                WorldLine(FVector(State.patrolOrigin.x, State.patrolOrigin.y, 12),
                    FVector(State.patrolDestination.x, State.patrolDestination.y, 12),
                    Mint.CopyWithNewOpacity(0.72f), 2.0f);
                DrawWaypoint(State.patrolOrigin, cinder::Order::Move, TEXT("A"));
                DrawWaypoint(State.patrolDestination, cinder::Order::Move, TEXT("B"));
                if (State.phase != cinder::SustainedOrderPhase::Travel)
                    WorldCircle(State.pursuitAnchor, cinder::Simulation::SustainedPursuitRadius,
                        Amber.CopyWithNewOpacity(0.22f));
                From = Primary->goal;
                bLinkReady = true;
            }
            else if (Primary->order == cinder::Order::Escort)
            {
                const cinder::SustainedOrderState& State = Primary->sustained;
                WorldLine(FVector(From.x, From.y, 12), FVector(Primary->goal.x, Primary->goal.y, 12),
                    Mint.CopyWithNewOpacity(0.72f), 2.0f);
                DrawWaypoint(Primary->goal, cinder::Order::Move, TEXT("SLOT"));
                if (const cinder::Entity* Leader = Sim.find(State.escortTarget);
                    Leader && Leader->alive() && Leader->team == 0)
                {
                    const cinder::Vec2 LeaderPoint = Battle->RenderPosition(*Leader);
                    WorldLine(FVector(Primary->goal.x, Primary->goal.y, 12),
                        FVector(LeaderPoint.x, LeaderPoint.y, 12), Amber.CopyWithNewOpacity(0.65f), 1.5f);
                    DrawWaypoint(LeaderPoint, cinder::Order::Move, TEXT("LEAD"));
                }
                WorldCircle(Primary->goal, cinder::Simulation::SustainedPursuitRadius,
                    Mint.CopyWithNewOpacity(0.20f));
                From = Primary->goal;
                bLinkReady = true;
            }
            for (std::size_t Index = 0; Index < Primary->futureOrders.size(); ++Index)
            {
                const cinder::TacticalOrder& Order = Primary->futureOrders[Index];
                if (Order.order != cinder::Order::Move && Order.order != cinder::Order::AttackMove) continue;
                if (bLinkReady)
                    WorldLine(FVector(From.x, From.y, 12), FVector(Order.point.x, Order.point.y, 12),
                        Order.order == cinder::Order::AttackMove
                            ? Amber.CopyWithNewOpacity(0.62f) : Mint.CopyWithNewOpacity(0.62f), 1.5f);
                DrawWaypoint(Order.point, Order.order, FString::FromInt(static_cast<int32>(Index + 1)));
                if (Order.hasArrivalFacing)
                    DrawFacingArrow(Order.point, Order.arrivalFacing,
                        Order.order == cinder::Order::AttackMove ? Amber : Mint);
                From = Order.point;
                bLinkReady = true;
            }
        }
    }
    if (PC->Selection().size() > 1)
    {
        int32 Markers = 0;
        for (cinder::Id Id : PC->Selection())
        {
            if (Markers >= 24) break;
            const cinder::Entity* Entity = Sim.find(Id);
            if (!Entity || !Entity->alive() || Entity->team != 0
                || (Entity->order != cinder::Order::Move && Entity->order != cinder::Order::AttackMove
                    && Entity->order != cinder::Order::Defend)) continue;
            FVector2D Screen;
            if (PC->ProjectWorldLocationToScreen(FVector(Entity->goal.x, Entity->goal.y, 13), Screen))
            {
                Panel(Screen.X - 3 * UIScale, Screen.Y - 3 * UIScale, 6 * UIScale, 6 * UIScale,
                    Entity->order == cinder::Order::AttackMove ? Amber : Mint);
                ++Markers;
            }
        }
    }
    if (PC->IsFacingPointerActive())
    {
        const cinder::Vec2 Center = PC->FacingCenter();
        const cinder::Vec2 Direction = PC->FacingDirectionPoint();
        float Angle = 0.0f;
        if (cinder::rules::arrivalFacingFromDirection(
            {Direction.x - Center.x, Direction.y - Center.y}, Angle))
        {
            DrawFacingArrow(Center, Angle, Amber);
            std::vector<cinder::rules::FormationRecipient> Recipients;
            for (cinder::Id Id : PC->Selection())
            {
                const cinder::Entity* Entity = Sim.find(Id);
                if (Entity && Entity->alive() && Entity->team == 0
                    && Entity->kind != cinder::Kind::Resource && !cinder::definition(Entity->kind).building)
                    Recipients.push_back({Entity->id, Entity->pos});
            }
            std::vector<cinder::rules::NominalFormationSlot> Slots;
            if (cinder::rules::nominalFormationSlots(Center, PC->FacingLatchedSpacing(), true,
                Angle, Recipients, Slots))
            {
                const int32 Visible = FMath::Min<int32>(24, static_cast<int32>(Slots.size()));
                for (int32 Index = 0; Index < Visible; ++Index)
                {
                    FVector2D Screen;
                    if (PC->ProjectWorldLocationToScreen(
                        FVector(Slots[Index].point.x, Slots[Index].point.y, 14), Screen))
                        Panel(Screen.X - 3 * UIScale, Screen.Y - 3 * UIScale,
                            6 * UIScale, 6 * UIScale, Mint);
                }
            }
            FVector2D Screen;
            if (PC->ProjectWorldLocationToScreen(FVector(Center.x, Center.y, 18), Screen))
            {
                Surface(Screen.X + 12 * UIScale, Screen.Y + 10 * UIScale, 112 * UIScale,
                    19 * UIScale, Ink.CopyWithNewOpacity(0.90f), 4 * UIScale);
                SingleLineLabel(TEXT("FORMATION GUIDE"), Screen.X + 17 * UIScale,
                    Screen.Y + 14 * UIScale, 102 * UIScale, Amber, 0.47f);
            }
        }
    }
    for (const auto& E : Sim.entities())
    {
        if (!E.alive() || E.kind == cinder::Kind::Resource || (E.team != 0 && !Sim.visible(0, E.pos))) continue;
        const cinder::Vec2 RenderPoint = Battle->RenderPosition(E);
        const auto& D = cinder::definition(E.kind);
        const bool Selected = std::find(PC->Selection().begin(), PC->Selection().end(), E.id) != PC->Selection().end();
        if (E.kind == cinder::Kind::Worker && E.order == cinder::Order::Construct)
        {
            if (const auto* Site = Sim.find(E.target); Site && Sim.constructionWorker(Site->id) == E.id && (E.team == 0 || Sim.visible(0, Site->pos)))
            {
                if (Sim.constructionActive(Site->id))
                {
                    const FVector2D Direction = FVector2D(RenderPoint.x - Site->pos.x, RenderPoint.y - Site->pos.y).GetSafeNormal();
                    const FVector2D Edge = FVector2D(Site->pos.x, Site->pos.y) + Direction * cinder::definition(Site->kind).radius;
                    WorldLine(FVector(RenderPoint.x, RenderPoint.y, 44), FVector(Edge.X, Edge.Y, 56), Amber, 2);
                }
                else if (Selected) WorldLine(FVector(RenderPoint.x, RenderPoint.y, 10), FVector(Site->pos.x, Site->pos.y, 10), Amber.CopyWithNewOpacity(0.45f), 1);
            }
        }
        if (Selected)
        {
            const float R = D.radius + 9;
            for (int I = 0; I < 24; ++I)
            {
                const float A = I * UE_TWO_PI / 24, B = (I + 1) * UE_TWO_PI / 24;
                WorldLine(FVector(RenderPoint.x + FMath::Cos(A) * R, RenderPoint.y + FMath::Sin(A) * R, 7), FVector(RenderPoint.x + FMath::Cos(B) * R, RenderPoint.y + FMath::Sin(B) * R, 7), Mint, 2);
            }
            if (!D.building && E.order != cinder::Order::Idle && PC->bDebug) WorldLine(FVector(RenderPoint.x, RenderPoint.y, 10), FVector(E.goal.x, E.goal.y, 10), Muted, 1);
        }
        if (Selected || E.hp < D.hp || E.progress < 1)
        {
            FVector2D P;
            const float Z = D.air ? 180 : D.building ? 200 : 83;
            if (PC->ProjectWorldLocationToScreen(FVector(RenderPoint.x, RenderPoint.y, Z), P))
            {
                const float BarW = (D.building ? 58 : 34) * UIScale;
                Panel(P.X - BarW / 2, P.Y, BarW, 4 * UIScale, Ink);
                Panel(P.X - BarW / 2, P.Y, BarW * FMath::Clamp(E.hp / D.hp, 0.0f, 1.0f), 4 * UIScale, CinderTeamColors::Accent(E.team));
                if (E.progress < 1) Panel(P.X - BarW / 2, P.Y + 6 * UIScale, BarW * E.progress, 3 * UIScale, Amber);
                if (E.team == 0 && D.building && E.progress < 1)
                    Label(ConstructionStatus(Sim, E), P.X - BarW / 2, P.Y - 19 * UIScale, Amber, 0.62f);
            }
        }
    }
    // One primary-selection range guide is enough for positioning decisions.
    // Friendly range never exposes hidden actors; segments also stay inside
    // currently visible, authored world space.
    if (!PC->Selection().empty())
    {
        const cinder::Entity* Primary = Sim.find(PC->Selection().front());
        if (Primary && Primary->alive() && Primary->team == 0)
        {
            const float Range = CinderUnitInfo::Range(Primary->kind);
            if (Range > 0)
            {
                const cinder::Vec2 Center = Battle->RenderPosition(*Primary);
                constexpr int32 Segments = 48;
                for (int32 Index = 0; Index < Segments; ++Index)
                {
                    const float A = Index * UE_TWO_PI / Segments;
                    const float B = (Index + 1) * UE_TWO_PI / Segments;
                    const cinder::Vec2 From{Center.x + FMath::Cos(A) * Range, Center.y + FMath::Sin(A) * Range};
                    const cinder::Vec2 To{Center.x + FMath::Cos(B) * Range, Center.y + FMath::Sin(B) * Range};
                    const bool bInWorld = From.x >= 0 && From.y >= 0 && To.x >= 0 && To.y >= 0
                        && From.x <= Sim.worldSize() && From.y <= Sim.worldSize()
                        && To.x <= Sim.worldSize() && To.y <= Sim.worldSize();
                    const cinder::Vec2 Mid{(From.x + To.x) * 0.5f, (From.y + To.y) * 0.5f};
                    if (!bInWorld || !Sim.visible(0, From) || !Sim.visible(0, Mid) || !Sim.visible(0, To)) continue;
                    WorldLine(FVector(From.x, From.y, 6), FVector(To.x, To.y, 6), Mint.CopyWithNewOpacity(0.34f), 1);
                }
            }
        }
    }
    if (!Battle->HasWorldEffects())
    {
        // Local effect geometry must not spill into a hidden fog cell. Link geometry
        // uses the simulation's recorded-endpoint and complete-segment visibility gate.
        const float EffectScale = bCompactLayout ? 0.72f : 1.0f;
        const float FogCell = Sim.worldSize() / cinder::Simulation::FogSize;
        auto EffectAreaVisible = [&](FVector Center, float Radius)
        {
            if (Center.X - Radius < 0 || Center.Y - Radius < 0 ||
                Center.X + Radius >= Sim.worldSize() || Center.Y + Radius >= Sim.worldSize()) return false;
            const int MinX = FMath::FloorToInt((Center.X - Radius) / FogCell), MaxX = FMath::FloorToInt((Center.X + Radius) / FogCell);
            const int MinY = FMath::FloorToInt((Center.Y - Radius) / FogCell), MaxY = FMath::FloorToInt((Center.Y + Radius) / FogCell);
            for (int Y = MinY; Y <= MaxY; ++Y) for (int X = MinX; X <= MaxX; ++X)
                if (!Sim.visible(0, {(X + 0.5f) * FogCell, (Y + 0.5f) * FogCell})) return false;
            return true;
        };
        auto EffectRing = [&](FVector Center, float Radius, FLinearColor Color, float Thickness)
        {
            if (!EffectAreaVisible(Center, Radius)) return;
            for (int I = 0; I < 16; ++I)
            {
                const float A = I * UE_TWO_PI / 16, B = (I + 1) * UE_TWO_PI / 16;
                WorldLine(Center + FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, 0),
                          Center + FVector(FMath::Cos(B) * Radius, FMath::Sin(B) * Radius, 0), Color, Thickness);
            }
        };
        auto EffectCross = [&](FVector Center, float Radius, FLinearColor Color, float Thickness)
        {
            if (!EffectAreaVisible(Center, Radius * 2)) return;
            FVector2D Screen, Edge;
            if (!PC->ProjectWorldLocationToScreen(Center, Screen) ||
                !PC->ProjectWorldLocationToScreen(Center + FVector(Radius, 0, 0), Edge)) return;
            const float Pixels = FMath::Clamp(static_cast<float>((Edge - Screen).Size()), 2.5f * UIScale, 7.0f * UIScale);
            DrawLine(Screen.X - Pixels, Screen.Y, Screen.X + Pixels, Screen.Y, Color, Thickness * UIScale);
            DrawLine(Screen.X, Screen.Y - Pixels, Screen.X, Screen.Y + Pixels, Color, Thickness * UIScale);
        };
        auto EffectHeight = [](cinder::Kind Kind)
        {
            const auto& D = cinder::definition(Kind);
            return D.air ? 125.0f + D.radius : D.building ? 35.0f + D.radius * 0.45f : 20.0f + D.radius * 0.8f;
        };
        for (const auto& FX : Sim.effects())
        {
            if (FX.life <= 0 || FX.duration <= 0) continue;
            const bool SourceVisible = Sim.effectVisible(FX, 0, true);
            const bool TargetVisible = Sim.effectVisible(FX, 0, false);
            if (!SourceVisible && !TargetVisible) continue;
            const float Age = FMath::Clamp(1.0f - FX.life / FX.duration, 0.0f, 1.0f);
            const float Fade = (1.0f - Age) * (1.0f - Age * 0.4f);
            const float Phase = static_cast<float>(FX.id % 31) * 0.37f;
            const FLinearColor Shot = CinderTeamColors::Accent(FX.team);
            const FVector From(FX.from.x, FX.from.y, EffectHeight(FX.sourceKind));
            const FVector To(FX.to.x, FX.to.y, EffectHeight(FX.targetKind));
            const auto PointOnLink = [&](float Fraction) { return FMath::Lerp(From, To, Fraction); };

            if (FX.type == cinder::EffectType::Weapon)
            {
                const bool Heavy = FX.sourceKind == cinder::Kind::Bastion || FX.sourceKind == cinder::Kind::Turret;
                if (SourceVisible && Age < 0.30f)
                {
                    // Symmetric flash gives no direction toward an unseen victim.
                    const float Flash = FMath::Max(0.0f, 1.0f - Age * 3.5f);
                    EffectCross(From, (Heavy ? 13 : 8) * EffectScale, Shot.CopyWithNewOpacity(Flash), Heavy ? 2.2f : 1.5f);
                    if (Heavy) EffectRing(From, (8 + Age * 19) * EffectScale, Shot.CopyWithNewOpacity(Flash * 0.6f), 1.2f);
                }
                if (!Sim.effectLinkVisible(FX, 0)) continue;
                const float Head = FMath::Clamp(Age / 0.78f, 0.0f, 1.0f);
                if (FX.sourceKind == cinder::Kind::Lancer)
                {
                    // Needle: narrow sustained beam with a bright traveling core.
                    WorldLine(From, To, Shot.CopyWithNewOpacity(Fade * 0.18f), 4.2f * EffectScale);
                    WorldLine(From, To, Shot.CopyWithNewOpacity(Fade), 1.5f);
                    WorldLine(PointOnLink(FMath::Max(0.0f, Head - 0.09f)), PointOnLink(Head), White.CopyWithNewOpacity(Fade), 1.0f);
                }
                else if (FX.sourceKind == cinder::Kind::Mortar)
                {
                    // Presentation-only ballistic arc; authoritative damage remains immediate.
                    const float Rise = FMath::Clamp(static_cast<float>((To - From).Size2D()) * 0.32f, 100.0f, 250.0f) * EffectScale;
                    const auto Arc = [&](float T) { return PointOnLink(T) + FVector(0, 0, FMath::Sin(T * UE_PI) * Rise); };
                    const float Tail = FMath::Max(0.0f, Head - 0.20f);
                    for (int I = 0; I < 6; ++I)
                        WorldLine(Arc(FMath::Lerp(Tail, Head, I / 6.0f)), Arc(FMath::Lerp(Tail, Head, (I + 1) / 6.0f)),
                                  Amber.CopyWithNewOpacity(Fade * (0.20f + I * 0.12f)), 2.2f);
                    EffectCross(Arc(Head), 8 * EffectScale, White.CopyWithNewOpacity(Fade), 1.6f);
                }
                else
                {
                    const float Length = Heavy ? 0.14f : FX.sourceKind == cinder::Kind::Scout ? 0.055f : 0.09f;
                    const float Thickness = Heavy ? 2.8f : FX.sourceKind == cinder::Kind::Worker ? 1.0f : 1.7f;
                    if (Heavy) WorldLine(PointOnLink(FMath::Max(0.0f, Head - Length)), PointOnLink(Head), Shot.CopyWithNewOpacity(Fade * 0.18f), 5 * EffectScale);
                    WorldLine(PointOnLink(FMath::Max(0.0f, Head - Length)), PointOnLink(Head), Shot.CopyWithNewOpacity(Fade), Thickness);
                    if (FX.sourceKind == cinder::Kind::Kite || FX.sourceKind == cinder::Kind::Striker)
                    {
                        const float Second = FMath::Max(0.0f, Head - (FX.sourceKind == cinder::Kind::Kite ? 0.20f : 0.12f));
                        WorldLine(PointOnLink(FMath::Max(0.0f, Second - Length * 0.6f)), PointOnLink(Second), Shot.CopyWithNewOpacity(Fade * 0.60f), 1.2f);
                    }
                }
            }
            else if (FX.type == cinder::EffectType::Heal)
            {
                const FLinearColor Green(0.20f, 1.0f, 0.64f);
                const float Pulse = 0.75f + 0.25f * FMath::Sin(Age * UE_TWO_PI);
                if (SourceVisible) EffectRing(From, (9 + Age * 12) * EffectScale, Green.CopyWithNewOpacity(Fade * 0.6f), 1.2f);
                if (Sim.effectLinkVisible(FX, 0))
                {
                    WorldLine(From, To, Green.CopyWithNewOpacity(Fade * 0.22f), 3 * EffectScale);
                    WorldLine(From, To, Green.CopyWithNewOpacity(Fade * 0.8f), 1.2f);
                    const float Head = FMath::Clamp(Age / 0.85f, 0.0f, 1.0f);
                    WorldLine(PointOnLink(FMath::Max(0.0f, Head - 0.07f)), PointOnLink(Head), White.CopyWithNewOpacity(Fade), 1.8f);
                }
                if (TargetVisible) EffectCross(To + FVector(0, 0, 12), (10 + Pulse * 5) * EffectScale, Green.CopyWithNewOpacity(Fade * Pulse), 2.0f);
            }
            else if (FX.type == cinder::EffectType::Impact && TargetVisible)
            {
                // Fixed target-local styling never identifies a hidden shooter or weapon.
                const FLinearColor Spark(1.0f, 0.82f, 0.50f);
                const float Radius = (4 + Age * 20) * EffectScale;
                EffectRing(To, Radius, Spark.CopyWithNewOpacity(Fade * 0.75f), 1.4f);
                if (EffectAreaVisible(To, Radius * 1.4f)) for (int I = 0; I < 4; ++I)
                {
                    const float Angle = Phase + I * UE_TWO_PI / 4;
                    const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.4f);
                    WorldLine(To + Direction * Radius * 0.65f, To + Direction * Radius * 1.35f, Spark.CopyWithNewOpacity(Fade), 1.4f);
                }
            }
            else if (FX.type == cinder::EffectType::Death && TargetVisible)
            {
                const auto& Victim = cinder::definition(FX.targetKind);
                const FVector Center(FX.to.x, FX.to.y, Victim.air ? EffectHeight(FX.targetKind) : 8.0f);
                const float Footprint = FMath::Clamp(Victim.radius * (Victim.building ? 1.05f : 1.35f), 25.0f, 135.0f) * EffectScale;
                const float Radius = Footprint * (0.20f + Age * 0.90f);
                const FLinearColor Fire(1.0f, 0.52f, 0.22f);
                EffectRing(Center, Radius, Fire.CopyWithNewOpacity(Fade), Victim.building ? 2.1f : 1.5f);
                EffectRing(Center + FVector(0, 0, 12), Radius * 0.58f, Amber.CopyWithNewOpacity(Fade * 0.55f), 1.0f);
                if (EffectAreaVisible(Center, Footprint * 1.25f)) for (int I = 0; I < (Victim.building ? 6 : 4); ++I)
                {
                    const float Angle = Phase + I * UE_TWO_PI / (Victim.building ? 6 : 4);
                    const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius,
                                         FMath::Sin(Age * UE_PI) * (Victim.building ? 55 : 28));
                    WorldLine(Center + Offset * 0.80f, Center + Offset, Fire.CopyWithNewOpacity(Fade), 2 * EffectScale);
                }
            }
        }
    }
    if (PC->IsSelecting())
    {
        const FVector2D A = PC->SelectionStart(), B = PC->SelectionEnd();
        const float X = FMath::Min(A.X, B.X), Y = FMath::Min(A.Y, B.Y), W = FMath::Abs(A.X - B.X), H = FMath::Abs(A.Y - B.Y);
        Panel(X, Y, W, H, FLinearColor(0.05f, 0.9f, 0.7f, 0.09f));
        DrawLine(X, Y, X + W, Y, Mint, 2); DrawLine(X, Y + H, X + W, Y + H, Mint, 2);
        DrawLine(X, Y, X, Y + H, Mint, 2); DrawLine(X + W, Y, X + W, Y + H, Mint, 2);
    }
    if (PC->IsBuildMode())
    {
        auto P = PC->PlacementPoint();
        const auto Guide = CurrentTutorialGuide();
        const bool bSuggestedSite = Battle->Tutorial().IsActive() && !PC->IsPlacementGestureActive()
            && Guide.Target == ECinderTutorialGuideTarget::Ground;
        if (bSuggestedSite) P = Guide.Point;
        const float R = cinder::definition(PC->BuildingKind()).radius;
        const auto Status = PC->BuildPlacementStatus(&P);
        const bool Valid = Status.accepted;
        const FLinearColor Color = Valid ? Mint : FLinearColor(1, 0.24f, 0.18f);
        for (int I = 0; I < 32; ++I)
        {
            const float A = I * UE_TWO_PI / 32, B = (I + 1) * UE_TWO_PI / 32;
            WorldLine(FVector(P.x + FMath::Cos(A) * R, P.y + FMath::Sin(A) * R, 8), FVector(P.x + FMath::Cos(B) * R, P.y + FMath::Sin(B) * R, 8), Color, 3);
        }
        // The tutorial owns the suggested-site instruction. A stale menu tap
        // must not display an unrelated red placement error beneath it.
        if (bSuggestedSite) return;
        FVector2D Screen(Margin, Height * 0.5f);
        FVector2D Projected;
        if (PC->ProjectWorldLocationToScreen(FVector(P.x, P.y, 10), Projected)) Screen = Projected;
#if PLATFORM_IOS || PLATFORM_ANDROID
        const FString PlacementLabel = Valid ? TEXT("TAP TO BUILD") : UTF8_TO_TCHAR(Status.message.c_str());
#else
        const FString PlacementLabel = Valid ? TEXT("CLICK TO BUILD") : UTF8_TO_TCHAR(Status.message.c_str());
#endif
        const float Inset = bCompactLayout ? Margin : 12 * UIScale;
        const float MaxWidth = FMath::Max(1.0f, FMath::Min(560 * UIScale, Width - 2 * Inset));
        const float LabelWidth = FMath::Min(MaxWidth, static_cast<float>(MeasureLabel(PlacementLabel, 0.8f).X));
        const float LabelX = FMath::Clamp(static_cast<float>(Screen.X + 14 * UIScale), Inset, FMath::Max(Inset, Width - Inset - LabelWidth));
        // Leave room for the desktop build panel and its rejection-feedback line.
        const float LowerReserved = bCompactLayout ? 70.0f : PC->bBuildMenu ? 384.0f : 224.0f;
        const float UpperLimit = 70 * UIScale + (bCompactLayout ? SafeTopOffset : 0.0f);
        const float BottomEdge = Height - (bCompactLayout ? SafeBottomOffset : 0.0f);
        const float LabelY = FMath::Clamp(static_cast<float>(Screen.Y - 20 * UIScale), UpperLimit,
            FMath::Max(UpperLimit, BottomEdge - LowerReserved * UIScale));
        Panel(LabelX - 4 * UIScale, LabelY - 3 * UIScale, LabelWidth + 8 * UIScale, 24 * UIScale, Ink);
        SingleLineLabel(PlacementLabel, LabelX, LabelY, MaxWidth, Color, 0.8f);
    }
}

void ACinderHUD::DrawProductionQueue(const cinder::Entity* Producer, float X, float Y, float W, int Columns)
{
    const float S = UIScale;
    const int Count = Producer ? static_cast<int>(Producer->queue.size()) : 0;
    const int PageSize = Columns * 2;
    const int Pages = FMath::Max(1, FMath::DivideAndRoundUp(Count, PageSize));
    QueuePage = FMath::Clamp(QueuePage, 0, Pages - 1);
    const int Start = QueuePage * PageSize, End = FMath::Min(Start + PageSize, Count);
    const int Rows = FMath::Max(1, FMath::DivideAndRoundUp(End - Start, Columns));
    const float PanelH = (bCompactLayout ? 62 + Rows * 51 : 164) * S;
    Panel(X - 5 * S, Y - 6 * S, W + 10 * S, PanelH, Ink);
    UIRegions.Add(FBox2D(FVector2D(X - 5 * S, Y - 6 * S), FVector2D(X + W + 5 * S, Y - 6 * S + PanelH)));
    SingleLineLabel(FString::Printf(TEXT("QUEUE %d / %d"), Count, cinder::Simulation::MaxQueue), X + 5 * S, Y + 3 * S, W - 219 * S, Amber, 0.80f);
#if PLATFORM_IOS || PLATFORM_ANDROID
    const TCHAR* CancelHint = TEXT("TAP TO CANCEL");
#else
    const TCHAR* CancelHint = TEXT("CLICK TO CANCEL");
#endif
    const FString Range = Count ? FString::Printf(TEXT("%d-%d / %s"), Start + 1, End, CancelHint) : TEXT("NO PRODUCTION QUEUED");
    SingleLineLabel(Range, X + 5 * S, Y + 25 * S, W - 219 * S, Muted, 0.70f);
    if (QueuePage > 0) Button(TEXT("<"), TEXT("queuepage"), -1, X + W - 98 * S, Y, 44 * S);
    if (QueuePage + 1 < Pages) Button(TEXT(">"), TEXT("queuepage"), 1, X + W - 44 * S, Y, 44 * S);
    const float CellW = (W - (Columns - 1) * 7 * S) / Columns;
    for (int I = Start; I < End; ++I)
    {
        const auto& Q = Producer->queue[I];
        const int Cell = I - Start;
        const float BX = X + (Cell % Columns) * (CellW + 7 * S), BY = Y + (52 + (Cell / Columns) * 51) * S;
        const FString QName = Q.research ? TEXT("Research") : Name(Q.kind);
        Button(TEXT(""),TEXT("cancelproduction"),0,BX,BY,CellW);
        SingleLineLabel(QName,BX+8*S,BY+7*S,CellW-32*S,White,0.64f);
        SingleLineLabel(FString::Printf(TEXT("%d / %ds"),I+1,FMath::CeilToInt(Q.remaining)),BX+8*S,BY+25*S,CellW-32*S,Muted,0.53f);
        ActionGlyph(TEXT("cancelproduction"),0,BX+CellW-23*S,BY+14*S,16*S,Amber);
        Buttons.Last().EntityId = Producer->id;
        Buttons.Last().JobId = Q.id;
        Panel(BX, BY + 41 * S, CellW * FMath::Clamp(1 - Q.remaining / FMath::Max(Q.total, 0.01f), 0.0f, 1.0f), 2 * S, Mint);
    }

    Button(TEXT("VIEW JOBS"), TEXT("producerjobs"), 0, X + W - 206 * S, Y, 98 * S);
    Buttons.Last().EntityId = Producer ? Producer->id : 0;
}

bool ACinderHUD::GetOnlineNotice(ACinderPlayerController* PC, ACinderBattlefield* Battle, FString& Heading, FString& Detail) const
{
    if (!PC || !Battle || !Battle->IsOnlineMatch()) return false;
    if (Battle->IsPaused() || Battle->Sim().winner() != -1 || PC->IsHelpOpen() || PC->IsOnlineLeavePending()) return false;
    const UCinderOnlineSubsystem* Online = PC->Online();
    if (!Online || Online->State() == ECinderOnlineState::Finished || Online->IsEliminated()) return false;

    FString Status = Online->StatusText().TrimStartAndEnd();
    if (!Online->CanSendOrders())
    {
        if (Online->State() == ECinderOnlineState::Reconnecting) Heading = TEXT("RECONNECTING / ORDERS UNAVAILABLE");
        else if (Online->State() == ECinderOnlineState::Error) Heading = TEXT("CONNECTION LOST / ORDERS UNAVAILABLE");
        else Heading = TEXT("SYNCING / ORDERS UNAVAILABLE");
        if (Status.IsEmpty()) Status = TEXT("Waiting for a current server snapshot.");
        Detail = Status + TEXT("  Open Menu > Connection.");
        return true;
    }

    const int32 LocalSeat = Online->LocalSeat();
    if (Online->State() == ECinderOnlineState::Playing && LocalSeat >= 0)
    {
        TArray<FString> Disconnected;
        for (int32 Seat = 0; Seat < Online->PlayerCount(); ++Seat)
        {
            const int32 Relative = (Seat - LocalSeat + Online->PlayerCount()) % Online->PlayerCount();
            if (Seat == LocalSeat || Battle->Sim().eliminated(Relative) || Online->SeatConnected(Seat)) continue;
            Disconnected.Add(Online->SeatName(Seat).IsEmpty()
                ? FString::Printf(TEXT("Player %d"), Seat + 1) : Online->SeatName(Seat));
        }
        if (!Disconnected.IsEmpty())
        {
            Heading = Disconnected.Num() == 1 ? TEXT("OPPONENT DISCONNECTED") : TEXT("OPPONENTS DISCONNECTED");
            Detail = FString::Join(Disconnected, TEXT(", ")) + TEXT(" · Reconnect window active. Menu > Connection.");
            return true;
        }
    }
    return false;
}

void ACinderHUD::DrawOnlineNotice(const FString& Heading, const FString& Detail, float X, float Y, float W, bool bCompact)
{
    const float S = UIScale;
    const float H = (bCompact ? 50 : 56) * S;
    Panel(X, Y, W, H, FLinearColor(0.025f, 0.035f, 0.04f, 0.97f));
    Panel(X, Y, 4 * S, H, Amber);
    SingleLineLabel(Heading, X + 13 * S, Y + 6 * S, W - 25 * S, Amber, bCompact ? 0.66f : 0.76f);
    SingleLineLabel(Detail, X + 13 * S, Y + (bCompact ? 26 : 29) * S, W - 25 * S, White, bCompact ? 0.60f : 0.68f);
}

void ACinderHUD::DrawUnitRibbon(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    if (!PC || !Battle || !MobileLayout.Portraits.bIsValid) return;
    struct FUnitSummary
    {
        cinder::Kind Kind = cinder::Kind::Worker;
        int32 Count = 0;
        float Health = 0;
        float Maximum = 0;
        int32 SquadMask = 0;
    };
    const cinder::Kind Kinds[] = { cinder::Kind::Worker, cinder::Kind::Striker, cinder::Kind::Lancer,
        cinder::Kind::Scout, cinder::Kind::Bastion, cinder::Kind::Mortar, cinder::Kind::Mender, cinder::Kind::Kite };
    TArray<FUnitSummary> Summaries;
    const auto& Sim = Battle->Sim();
    std::map<cinder::Id, int32> SquadMasks;
    for (int32 Squad = 0; Squad < ACinderPlayerController::SquadCount; ++Squad)
        for (cinder::Id Id : PC->Squad(Squad)) SquadMasks[Id] |= 1 << Squad;
    for (cinder::Kind Kind : Kinds)
    {
        FUnitSummary Summary; Summary.Kind = Kind;
        for (const cinder::Entity& Entity : Sim.entities())
        {
            if (!Entity.alive() || Entity.team != 0 || Entity.kind != Kind) continue;
            ++Summary.Count;
            Summary.Health += Entity.hp;
            Summary.Maximum += cinder::definition(Kind).hp;
            const auto Squad = SquadMasks.find(Entity.id);
            if (Squad != SquadMasks.end()) Summary.SquadMask |= Squad->second;
        }
        if (Summary.Count > 0) Summaries.Add(Summary);
    }

    const FBox2D Bounds = MobileLayout.Portraits;
    const float S = UIScale, W = Bounds.GetSize().X, H = Bounds.GetSize().Y;
    if (Summaries.IsEmpty()) return;

    const float Gap = 3 * S;
    const int32 Capacity = FMath::Max(1, FMath::FloorToInt((W + Gap) / (44 * S + Gap)));
    const int32 Visible = FMath::Min(Capacity, Summaries.Num());
    const float CardW = FMath::Min(58 * S, (W - Gap * (Visible - 1)) / Visible);
    const float UsedW = Visible * CardW + (Visible - 1) * Gap;
    const FBox2D UsedBounds(Bounds.Min, FVector2D(Bounds.Min.X + UsedW, Bounds.Max.Y));
    Panel(UsedBounds.Min.X, UsedBounds.Min.Y, UsedW, H, FLinearColor(0.004f, 0.018f, 0.026f, 0.95f));
    Panel(UsedBounds.Min.X, UsedBounds.Min.Y, UsedW, S, FLinearColor(0.07f, 0.31f, 0.30f));
    UIRegions.Add(UsedBounds);
    for (int32 Index = 0; Index < Visible; ++Index)
    {
        const FUnitSummary& Summary = Summaries[Index];
        const float X = Bounds.Min.X + Index * (CardW + Gap);
        if(Summaries.Num()>Visible && Index==Visible-1)
        {
            ActionButton(FString::Printf(TEXT("+%d types"),Summaries.Num()-Visible+1),TEXT("army"),0,X,Bounds.Min.Y,CardW);
            continue;
        }
        const bool bSelected = !PC->Selection().empty()
            && std::all_of(PC->Selection().begin(), PC->Selection().end(), [&](cinder::Id Id)
            {
                const cinder::Entity* Entity = Sim.find(Id);
                return Entity && Entity->alive() && Entity->team == 0 && Entity->kind == Summary.Kind;
            });
        DrawUnitPortrait(Summary.Kind, X, Bounds.Min.Y, CardW, H, bSelected);
        Panel(X + CardW - 20 * S, Bounds.Min.Y + 3 * S, 17 * S, 15 * S,
            FLinearColor(0.002f, 0.008f, 0.013f, 0.86f));
        SingleLineLabel(FString::Printf(TEXT("%d"), Summary.Count), X + CardW - 18 * S,
            Bounds.Min.Y + 4 * S, 14 * S, White, 0.54f);
        FString SquadText;
        for (int32 Squad = 0; Squad < ACinderPlayerController::SquadCount; ++Squad)
            if ((Summary.SquadMask & (1 << Squad)) != 0) SquadText.AppendChar(TCHAR('A' + Squad));
        if (!SquadText.IsEmpty())
        {
            Panel(X + 3 * S, Bounds.Min.Y + 3 * S, (7 + SquadText.Len() * 7) * S, 15 * S,
                FLinearColor(0.002f, 0.008f, 0.013f, 0.86f));
            SingleLineLabel(SquadText, X + 6 * S, Bounds.Min.Y + 4 * S,
                (SquadText.Len() * 7 + 1) * S, Amber, 0.50f);
        }
        SingleLineLabel(CardName(Summary.Kind), X + 4 * S, Bounds.Max.Y - 17 * S,
            CardW - 8 * S, bSelected ? Mint : White, 0.48f);
        HealthBar(Summary.Health, Summary.Maximum, X + 3 * S, Bounds.Max.Y - 4 * S,
            CardW - 6 * S, Summary.Health / FMath::Max(1.0f, Summary.Maximum) < 0.35f ? Amber : Mint);
        FButton Entry;
        Entry.Bounds = FBox2D(FVector2D(X, Bounds.Min.Y), FVector2D(X + CardW, Bounds.Max.Y));
        Entry.Action = TEXT("armytype");
        Entry.Argument = static_cast<int32>(Summary.Kind);
        Buttons.Add(Entry);
    }
}

void ACinderHUD::DrawSelectionIdentity(ACinderPlayerController* PC, ACinderBattlefield* Battle, bool bCompact)
{
    if (!PC || !Battle || PC->Selection().empty()) return;
    const auto& Sim = Battle->Sim();
    const cinder::Entity* First = nullptr;
    float Health = 0, Maximum = 0;
    int32 Count = 0;
    int32 OtherRoutes = 0;
    FString SharedOrder;
    bool bSharedOrder = true;
    std::map<cinder::Kind, int32> Types;
    for (cinder::Id Id : PC->Selection())
    {
        const cinder::Entity* Entity = Sim.find(Id);
        if (!Entity || !Entity->alive() || Entity->team != 0) continue;
        const FString Order = RosterOrder(*Entity);
        if (!First) { First = Entity; SharedOrder = Order; }
        else
        {
            if (SharedOrder != Order) bSharedOrder = false;
            if (!SameTacticalPlan(*First, *Entity)) ++OtherRoutes;
        }
        Health += Entity->hp;
        Maximum += cinder::definition(Entity->kind).hp;
        ++Types[Entity->kind];
        ++Count;
    }
    if (!First || Count <= 0) return;

    const float S = UIScale;
    const FBox2D Bounds = bCompact
        ? MobileLayout.Identity
        : FBox2D(FVector2D(Margin + 180 * S, Height - 181 * S), FVector2D(Margin + 490 * S, Height - 137 * S));
    const float X = Bounds.Min.X, Y = Bounds.Min.Y, W = Bounds.GetSize().X, H = Bounds.GetSize().Y;
    Surface(X,Y,W,H,FLinearColor(0.007f,0.018f,0.030f,0.92f),8*S);
    DrawUnitPortrait(First->kind,X+4*S,Y+4*S,36*S,H-8*S,true);
    const float InfoW=44*S;
    const FString Title=Count==1 ? Name(First->kind) : Types.size()==1
        ? FString::Printf(TEXT("%s ×%d"),*Name(First->kind),Count) : FString::Printf(TEXT("%d units selected"),Count);
    SingleLineLabel(Title,X+47*S,Y+6*S,W-InfoW-54*S,White,0.73f);
    FString OrderDetail = bSharedOrder ? SharedOrder : TEXT("MIXED");
    if (!First->futureOrders.empty())
    {
        const bool bWaiting = First->order != cinder::Order::Move && First->order != cinder::Order::AttackMove;
        OrderDetail += FString::Printf(TEXT(" +%d %s"),
            static_cast<int32>(First->futureOrders.size()), bWaiting ? TEXT("WAITING") : TEXT("QUEUED"));
    }
    if (PC->IsQueueNextPending()) OrderDetail += TEXT(" / QUEUE PENDING");
    else if (PC->IsDestinationPending() && PC->IsPatrolCommandMode()) OrderDetail += TEXT(" / PATROL PENDING");
    else if (PC->IsDestinationPending() && PC->IsEscortCommandMode()) OrderDetail += TEXT(" / ESCORT PENDING");
    if (OtherRoutes > 0) OrderDetail += FString::Printf(TEXT(" / +%d UNIT ROUTE%s"),
        OtherRoutes, OtherRoutes == 1 ? TEXT("") : TEXT("S"));
    const FString Detail=FString::Printf(TEXT("%s   %.0f/%.0f"),*OrderDetail,Health,Maximum);
    SingleLineLabel(Detail,X+47*S,Y+23*S,W-InfoW-54*S,Muted,0.52f);
    HealthBar(Health,Maximum,X+47*S,Y+H-5*S,W-InfoW-54*S,Mint);
    FButton Focus;
    Focus.Bounds = FBox2D(Bounds.Min, FVector2D(X + W - InfoW, Bounds.Max.Y));
    Focus.Action = TEXT("focus");
    Buttons.Add(Focus);
    ActionButton(TEXT("Info"), TEXT("info"), 0, X + W - InfoW, Y, InfoW, CompactSheet == SheetInfo);

    if (!bCompact) return;
    const float Gap = 5 * S;
    const float SquadW = 52 * S;
    float SquadX = Bounds.Max.X + Gap;
    for (int32 Index = 0; Index < ACinderPlayerController::SquadCount; ++Index)
    {
        const int32 Size = static_cast<int32>(PC->Squad(Index).size());
        if (Size <= 0) continue;
        if (SquadX + SquadW > MobileLayout.Commands.Min.X) break;
        const bool bSelected = PC->Selection().size() == PC->Squad(Index).size()
            && std::all_of(PC->Squad(Index).begin(), PC->Squad(Index).end(), [&](cinder::Id Id)
            {
                return std::find(PC->Selection().begin(), PC->Selection().end(), Id) != PC->Selection().end();
            });
        Button(FString::Printf(TEXT("%c %d"), TCHAR('A' + Index), Size), TEXT("squad"), Index,
            SquadX, Y, SquadW, bSelected);
        SquadX += SquadW + Gap;
    }
}

void ACinderHUD::DrawArmyDrawer(ACinderPlayerController* PC, ACinderBattlefield* Battle, bool bCompact)
{
    if (!PC || !Battle) return;
    const auto& Sim = Battle->Sim();
    std::vector<const cinder::Entity*> Army;
    std::map<cinder::Kind, int32> Counts;
    std::map<cinder::Kind, float> HealthByKind, MaximumByKind;
    for (const cinder::Entity& Entity : Sim.entities())
    {
        const auto& Definition = cinder::definition(Entity.kind);
        if (!Entity.alive() || Entity.team != 0 || Definition.building
            || Entity.kind == cinder::Kind::Worker || Entity.kind == cinder::Kind::Resource) continue;
        Army.push_back(&Entity);
        ++Counts[Entity.kind];
        HealthByKind[Entity.kind] += Entity.hp;
        MaximumByKind[Entity.kind] += Definition.hp;
    }
    std::sort(Army.begin(), Army.end(), [](const cinder::Entity* A, const cinder::Entity* B)
    {
        if (A->kind != B->kind) return static_cast<int32>(A->kind) < static_cast<int32>(B->kind);
        return A->id < B->id;
    });

    const float S = UIScale;
    const FBox2D Bounds = bCompact
        ? MobileLayout.Drawer
        : FBox2D(FVector2D(Margin, Height - 400 * S), FVector2D(Margin + 520 * S, Height - 204 * S));
    const float X = Bounds.Min.X, Y = Bounds.Min.Y, W = Bounds.GetSize().X, H = Bounds.GetSize().Y;
    Surface(X,Y,W,H,FLinearColor(0.009f,0.020f,0.032f,0.96f),10*S);
    Surface(X,Y+12*S,2*S,H-24*S,Mint.CopyWithNewOpacity(0.55f),S);
    UIRegions.Add(Bounds);

    Button(TEXT("ROSTER"), TEXT("armytab"), 0, X + W - 310 * S, Y, 60 * S, ArmyPanelTab == 0);
    Button(TEXT("SQUADS"), TEXT("armytab"), 1, X + W - 246 * S, Y, 60 * S, ArmyPanelTab == 1);
    Button(TEXT("JOBS"), TEXT("armytab"), 2, X + W - 182 * S, Y, 48 * S, ArmyPanelTab == 2);
    Button(TEXT("RALLY"), TEXT("armytab"), 3, X + W - 130 * S, Y, 78 * S, ArmyPanelTab == 3);
    Button(TEXT("X"), TEXT("closesheet"), 0, X + W - 44 * S, Y, 44 * S);
    if (W > 390 * S) SingleLineLabel(TEXT("Army"),X+10*S,Y+15*S,W-322*S,White,0.7f);
    if (ArmyPanelTab == 3)
    {
        const auto& Player = Sim.players()[0];
        Button(Player.armyRallySet ? TEXT("MOVE FLAG") : TEXT("SET FLAG"), TEXT("productionrally"), 0,
            X + 14 * S, Y + 48 * S, 116 * S, true);
        if (Player.armyRallySet)
            Button(TEXT("FIND FLAG"), TEXT("rallyfocus"), 0, X + 138 * S, Y + 48 * S, 116 * S);
        SingleLineLabel(Player.armyRallySet ? TEXT("New troops gather at your army flag.")
                : TEXT("Choose where new troops should gather."),
            X + 14 * S, Y + 108 * S, W - 28 * S, White, 0.66f);
        SingleLineLabel(TEXT("New facilities inherit it; local rallies stay."),
            X + 14 * S, Y + 132 * S, W - 28 * S, Muted, 0.59f);
        SingleLineLabel(TEXT("Drudges mine ore. Existing orders continue."),
            X + 14 * S, Y + 156 * S, W - 28 * S, Muted, 0.59f);
        return;
    }
    if (ArmyPanelTab == 1)
    {

        const float Gap = 6 * S;
        const float ColumnW = (W - 28 * S - Gap * 2) / 3;
        for (int32 Index = 0; Index < ACinderPlayerController::SquadCount; ++Index)
        {
            const float ColumnX = X + 14 * S + Index * (ColumnW + Gap);
            const int32 Size = static_cast<int32>(PC->Squad(Index).size());
            const bool bSelected = Size > 0 && PC->Selection().size() == PC->Squad(Index).size()
                && std::all_of(PC->Squad(Index).begin(), PC->Squad(Index).end(), [&](cinder::Id Id)
                {
                    return std::find(PC->Selection().begin(), PC->Selection().end(), Id) != PC->Selection().end();
                });
            SingleLineLabel(FString::Printf(TEXT("SQUAD %c  /  %d"), TCHAR('A' + Index), Size),
                ColumnX, Y + 53 * S, ColumnW, Size > 0 ? Mint : Muted, 0.66f);
            if (Size > 0)
                Button(TEXT("RECALL"), TEXT("squad"), Index, ColumnX, Y + 72 * S, ColumnW, bSelected);
            else
            {
                Panel(ColumnX, Y + 72 * S, ColumnW, 44 * S, PanelInk);
                SingleLineLabel(TEXT("EMPTY"), ColumnX + 12 * S, Y + 85 * S, ColumnW - 24 * S, Muted, 0.70f);
            }
            Button(TEXT("ASSIGN SELECTED"), TEXT("squadassign"), Index,
                ColumnX, Y + 120 * S, ColumnW);
            if (Size > 0)
                Button(TEXT("TACTICS"), TEXT("squadtactics"), Index,
                    ColumnX, Y + 168 * S, ColumnW);
        }
        return;
    }
    if (ArmyPanelTab == 2)
    {
        struct FProductionJob
        {
            const cinder::Entity* Producer = nullptr;
            const cinder::QueueItem* Item = nullptr;
            int32 QueuePosition = 0;
        };
        TArray<FProductionJob> Jobs;
        for (const cinder::Entity& Producer : Sim.entities())
        {
            if (!Producer.alive() || Producer.team != 0 || !cinder::definition(Producer.kind).building) continue;
            if (PinnedProducerId && Producer.id != PinnedProducerId) continue;
            if (Producer.progress < 1) Jobs.Add({&Producer, nullptr, 0});
            for (int32 QueueIndex = 0; QueueIndex < static_cast<int32>(Producer.queue.size()); ++QueueIndex)
                Jobs.Add({&Producer, &Producer.queue[QueueIndex], QueueIndex + 1});
        }
        Jobs.Sort([](const FProductionJob& A, const FProductionJob& B)
        {
            if (A.Producer->id != B.Producer->id) return A.Producer->id < B.Producer->id;
            return A.QueuePosition < B.QueuePosition;
        });
        const int32 PageSize = 2;
        const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(Jobs.Num(), PageSize));
        ProductionJobsPage = FMath::Clamp(ProductionJobsPage, 0, Pages - 1);
        const cinder::Entity* PinnedProducer = PinnedProducerId ? Sim.find(PinnedProducerId) : nullptr;
        const auto Rally = Sim.autoRallyStatus(0, cinder::Kind::Resource, PinnedProducerId);
        Button(PinnedProducer ? TEXT("RALLY THIS") : TEXT("ARMY FLAG"), TEXT("productionrally"), 0,
            X + 14 * S, Y + 48 * S, 104 * S, Rally.accepted);
        Buttons.Last().EntityId = PinnedProducerId;
        const bool bCanUseDefault = PinnedProducer && PinnedProducer->rallyOverride
            && Sim.autoRallyStatus(0, cinder::Kind::Resource, PinnedProducerId, true).accepted;
        if (bCanUseDefault)
        {
            Button(PinnedProducer->kind == cinder::Kind::Headquarters ? TEXT("AUTO MINE") : TEXT("USE DEFAULT"),
                TEXT("rallydefault"), 0, X + 122 * S, Y + 48 * S, 104 * S);
            Buttons.Last().EntityId = PinnedProducerId;
        }
        else SingleLineLabel(Rally.accepted ? PinnedProducer ? TEXT("This facility")
                        : TEXT("Future troops")
                    : UTF8_TO_TCHAR(Rally.message.c_str()),
                X + 126 * S, Y + 62 * S, W - 240 * S, Rally.accepted ? Muted : Amber, 0.55f);
        if (ProductionJobsPage > 0) Button(TEXT("<"), TEXT("jobpage"), -1, X + W - 96 * S, Y + 48 * S, 44 * S);
        if (ProductionJobsPage + 1 < Pages) Button(TEXT(">"), TEXT("jobpage"), 1, X + W - 48 * S, Y + 48 * S, 44 * S);
        const int32 Start = ProductionJobsPage * PageSize;
        for (int32 Index = Start; Index < FMath::Min(Start + PageSize, Jobs.Num()); ++Index)
        {
            const FProductionJob& Job = Jobs[Index];
            const float RowY = Y + (96 + (Index - Start) * 47) * S;
            const float RowW = W - 28 * S;
            Panel(X + 14 * S, RowY, RowW, 44 * S, PanelInk);
            FString Description;
            FString AssignmentDetail;
            float Progress = Job.Producer->progress;
            if (!Job.Item)
            {
                const cinder::Id Worker = Sim.constructionWorker(Job.Producer->id);
                Description = FString::Printf(TEXT("%s #%u / %s"),
                    *Name(Job.Producer->kind).ToUpper(), Job.Producer->id,
                    Worker ? *FString::Printf(TEXT("DRUDGE #%u"), Worker) : TEXT("NO DRUDGE"));
                AssignmentDetail = Worker ? ConstructionStatus(Sim, *Job.Producer) : TEXT("PAUSED / VIEW TO ASSIGN A DRUDGE");
            }
            else
            {
                FString JobName;
                if (!Job.Item->research) JobName = Name(Job.Item->kind).ToUpper();
                else if (Job.Item->kind == cinder::Kind::Worker) JobName = TEXT("TECH TIER");
                else if (Job.Item->kind == cinder::Kind::Striker) JobName = TEXT("WEAPONS");
                else JobName = TEXT("ARMOR");
                Description = FString::Printf(TEXT("%s #%u / %s / Q%d / %.0fs"),
                    *Name(Job.Producer->kind).ToUpper(), Job.Producer->id,
                    *JobName, Job.QueuePosition, Job.Item->remaining);
                Progress = 1.0f - Job.Item->remaining / FMath::Max(0.01f, Job.Item->total);
            }
            SingleLineLabel(Description, X + 22 * S, RowY + 7 * S, RowW - 126 * S, White, 0.58f);
            if (!AssignmentDetail.IsEmpty())
                SingleLineLabel(AssignmentDetail, X + 22 * S, RowY + 21 * S, RowW - 126 * S, Muted, 0.52f);
            HealthBar(Progress, 1.0f, X + 22 * S, RowY + 34 * S, RowW - 126 * S, Mint);
            Button(TEXT("VIEW"), TEXT("productionfocus"), 0, X + W - 108 * S, RowY, 60 * S);
            Buttons.Last().EntityId = Job.Producer->id;
            Button(TEXT("X"), Job.Item ? TEXT("cancelproduction") : TEXT("cancelconstruction"), 0,
                X + W - 44 * S, RowY, 44 * S);
            Buttons.Last().EntityId = Job.Producer->id;
            Buttons.Last().JobId = Job.Item ? Job.Item->id : 0;
        }
        if (Jobs.IsEmpty())
            SingleLineLabel(TEXT("NO PRODUCTION QUEUED"), X + 14 * S, Y + 113 * S, W - 28 * S, Muted, 0.68f);
        return;
    }

    const bool Narrow=W<400*S;
    const int32 PageSize=Narrow ? 2 : 4;
    const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(static_cast<int32>(Army.size()), PageSize));
    ArmyRosterPage = FMath::Clamp(ArmyRosterPage, 0, Pages - 1);
    const cinder::Kind Kinds[] = { cinder::Kind::Striker, cinder::Kind::Lancer, cinder::Kind::Scout,
        cinder::Kind::Bastion, cinder::Kind::Mortar, cinder::Kind::Mender, cinder::Kind::Kite };
    const float Gap = 4 * S;
    const int32 Columns=Narrow ? 4 : 8;
    const float CellW=(W-28*S-Gap*(Columns-1))/Columns;
    const bool bAllSelected = !Army.empty() && PC->Selection().size() == Army.size()
        && std::all_of(Army.begin(), Army.end(), [&](const cinder::Entity* Entity)
        {
            return std::find(PC->Selection().begin(), PC->Selection().end(), Entity->id) != PC->Selection().end();
        });
    if (!Army.empty())
        Button(TEXT("ALL"), TEXT("armyall"), 0,
            X + 14 * S, Y + 48 * S, CellW, bAllSelected);
    else
    {
        Panel(X + 14 * S, Y + 48 * S, CellW, 44 * S, PanelInk);
        SingleLineLabel(TEXT("ALL 0"), X + 19 * S, Y + 61 * S, CellW - 10 * S, Muted, 0.58f);
    }
    for (int32 Index = 0; Index < 7; ++Index)
    {
        const auto Found = Counts.find(Kinds[Index]);
        const int32 Count = Found == Counts.end() ? 0 : Found->second;
        const bool bKindSelected = !PC->Selection().empty()
            && std::all_of(PC->Selection().begin(), PC->Selection().end(), [&](cinder::Id Id)
            {
                const cinder::Entity* Entity = Sim.find(Id);
                return Entity && Entity->alive() && Entity->team == 0 && Entity->kind == Kinds[Index];
            });
        const float CellX=X+14*S+((Index+1)%Columns)*(CellW+Gap);
        const float CellY=Y+(48+((Index+1)/Columns)*48)*S;
        if (Count > 0)
        {
            DrawUnitPortrait(Kinds[Index], CellX, CellY, CellW, 44 * S, bKindSelected);
            Panel(CellX + CellW - 18 * S, CellY + 3 * S, 15 * S, 14 * S,
                FLinearColor(0.002f, 0.008f, 0.013f, 0.86f));
            SingleLineLabel(FString::Printf(TEXT("%d"), Count), CellX + CellW - 16 * S,
                CellY + 4 * S, 12 * S, White, 0.50f);
            SingleLineLabel(CardName(Kinds[Index]), CellX + 3 * S, CellY + 26 * S,
                CellW - 6 * S, bKindSelected ? Mint : White, 0.44f);
            HealthBar(HealthByKind[Kinds[Index]], MaximumByKind[Kinds[Index]],
                CellX + 3 * S, CellY + 40 * S, CellW - 6 * S, Mint);
            FButton TypeEntry;
            TypeEntry.Bounds = FBox2D(FVector2D(CellX, CellY), FVector2D(CellX + CellW, CellY + 44 * S));
            TypeEntry.Action = TEXT("armytype");
            TypeEntry.Argument = static_cast<int32>(Kinds[Index]);
            Buttons.Add(TypeEntry);
        }
        else
        {
            Panel(CellX, CellY, CellW, 44 * S, PanelInk);
            UnitGlyph(Kinds[Index], CellX + (CellW - 18 * S) * 0.5f, CellY + 6 * S, 18 * S, Muted);
            SingleLineLabel(CardName(Kinds[Index]), CellX + 3 * S, CellY + 27 * S,
                CellW - 6 * S, Muted, 0.42f);
        }
    }
    const int32 Start = ArmyRosterPage * PageSize;
    const float RowY = Y + H - 64 * S;
    const float UnitW=(W-124*S-Gap*(PageSize-1))/PageSize;
    if(ArmyRosterPage>0) Button(TEXT("<"),TEXT("rosterpage"),-1,X+8*S,RowY+10*S,44*S);
    if(ArmyRosterPage+1<Pages) Button(TEXT(">"),TEXT("rosterpage"),1,X+W-52*S,RowY+10*S,44*S);
    for (int32 Index = Start; Index < FMath::Min(Start + PageSize, static_cast<int32>(Army.size())); ++Index)
    {
        const bool bSelected = std::find(PC->Selection().begin(), PC->Selection().end(), Army[Index]->id) != PC->Selection().end();
        int32 SquadMask = 0;
        for (int32 Squad = 0; Squad < ACinderPlayerController::SquadCount; ++Squad)
            if (std::find(PC->Squad(Squad).begin(), PC->Squad(Squad).end(), Army[Index]->id) != PC->Squad(Squad).end())
                SquadMask |= 1 << Squad;
        EntityButton(*Army[Index], X + 62 * S + (Index - Start) * (UnitW + Gap), RowY, UnitW, bSelected, SquadMask);
    }
    if (Army.empty()) SingleLineLabel(TEXT("NO COMBAT UNITS YET"), X + 14 * S, RowY + 14 * S, W - 28 * S, Muted, 0.68f);
}

void ACinderHUD::DrawGlobalCatalog(ACinderPlayerController* PC, ACinderBattlefield* Battle, bool bCompact)
{
    if (!PC || !Battle) return;
    const auto& Sim = Battle->Sim();
    const float S = UIScale;
    const FBox2D Bounds = bCompact
        ? MobileLayout.Drawer
        : FBox2D(FVector2D(Margin, Height - 400 * S), FVector2D(Margin + 520 * S, Height - 204 * S));
    const float X = Bounds.Min.X, Y = Bounds.Min.Y, W = Bounds.GetSize().X, H = Bounds.GetSize().Y;
    Surface(X,Y,W,H,FLinearColor(0.009f,0.020f,0.032f,0.96f),10*S);
    Surface(X,Y+12*S,2*S,H-24*S,Mint.CopyWithNewOpacity(0.55f),S);
    UIRegions.Add(Bounds);
    const cinder::Entity* PinnedProducer = PinnedProducerId ? Sim.find(PinnedProducerId) : nullptr;
    if (!PinnedProducer || !PinnedProducer->alive() || PinnedProducer->team != 0
        || !cinder::definition(PinnedProducer->kind).building)
    {
        PinnedProducer = nullptr;
        PinnedProducerId = 0;
    }
    const float HeaderLabelWidth = W - (PinnedProducer ? 166 : 68) * S;
    if (PinnedProducer)
    {
        Button(TEXT("JOBS"), TEXT("producerjobs"), 0, X + W - 146 * S, Y, 48 * S);
        Buttons.Last().EntityId = PinnedProducer->id;
        const auto Rally = Sim.autoRallyStatus(0, cinder::Kind::Resource, PinnedProducer->id);
        Button(TEXT("RALLY"), TEXT("productionrally"), 0, X + W - 94 * S, Y, 46 * S, Rally.accepted);
        Buttons.Last().EntityId = PinnedProducer->id;
    }
    Button(TEXT("X"), TEXT("closesheet"), 0, X + W - 44 * S, Y, 44 * S);

    if (CompactSheet == SheetGlobalBuild)
    {
        SingleLineLabel(TEXT("BUILD / AUTO DRUDGE"), X + 14 * S, Y + 14 * S, HeaderLabelWidth, Amber, 0.74f);
        const cinder::Kind Buildings[] = { cinder::Kind::Headquarters, cinder::Kind::Processor,
            cinder::Kind::Foundry, cinder::Kind::MotorPool, cinder::Kind::Laboratory, cinder::Kind::Turret };
        const float Gap = 6 * S;
        const float CellW = (W - 28 * S - Gap * 2) / 3;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Buildings); ++Index)
        {
            const cinder::Kind Kind = Buildings[Index];
            const auto Plan = Sim.autoBuildStatus(0, Kind);
            const float CellX = X + 14 * S + (Index % 3) * (CellW + Gap);
            const float CellY = Y + (49 + (Index / 3) * 49) * S;
            Button(Name(Kind).ToUpper(), TEXT("globalbuild"), static_cast<int32>(Kind),
                CellX, CellY, CellW, PC->IsGlobalBuildMode() && PC->BuildingKind() == Kind);
            const FString BuildState = Plan.accepted ? TEXT("") : TEXT(" / Locked");
            SingleLineLabel(FString::Printf(TEXT("%d ore%s"), cinder::definition(Kind).cost, *BuildState),
                CellX + 10 * S, CellY + 27 * S, CellW - 20 * S, Plan.accepted ? Mint : Amber, 0.53f);
        }
        SingleLineLabel(TEXT("Choose a structure, then place it."),
            X + 14 * S, Y + H - 24 * S, W - 28 * S, Muted, 0.55f);
        return;
    }

    if (CompactSheet == SheetGlobalTrain)
    {
        GlobalTrainKind = FMath::Clamp(GlobalTrainKind,
            static_cast<int32>(cinder::Kind::Worker), static_cast<int32>(cinder::Kind::Kite));
        GlobalTrainQuantity = FMath::Clamp(GlobalTrainQuantity, 1, 20);
        const cinder::Kind SelectedKind = static_cast<cinder::Kind>(GlobalTrainKind);
        const bool bAwaitingTutorialChoice = Battle->Tutorial().IsActive() && !bTutorialTrainKindChosen;
        const auto Plan = Sim.autoTrainStatus(0, SelectedKind, GlobalTrainQuantity, PinnedProducerId);
        const FString Allocation = PinnedProducer
            ? FString::Printf(TEXT("PINNED %s"), *Name(PinnedProducer->kind).ToUpper()) : TEXT("AUTO ASSIGN");
        SingleLineLabel(FString::Printf(TEXT("TRAIN / %s / x%d"), *Allocation, GlobalTrainQuantity),
            X + 14 * S, Y + 14 * S, HeaderLabelWidth, Amber, 0.74f);
        const float Gap = 4*S;
        const bool Narrow=W<400*S;
        const int32 Columns=Narrow ? 4 : 8;
        const float CellW=(W-28*S-Gap*(Columns-1))/Columns;
        for (int32 Index = 0; Index < 8; ++Index)
        {
            const cinder::Kind Kind = static_cast<cinder::Kind>(Index);
            const float CellX=X+14*S+(Index%Columns)*(CellW+Gap), CellY=Y+((Narrow ? 44 : 48)+(Index/Columns)*48)*S;
            const bool bSelected = !bAwaitingTutorialChoice && Kind == SelectedKind;
            DrawUnitPortrait(Kind, CellX, CellY, CellW, 44 * S, bSelected);
            SingleLineLabel(CardName(Kind), CellX + 3 * S, CellY + 27 * S,
                CellW - 6 * S, bSelected ? Mint : White, 0.43f);
            FButton Entry;
            Entry.Bounds = FBox2D(FVector2D(CellX, CellY), FVector2D(CellX + CellW, CellY + 44 * S));
            Entry.Action = TEXT("trainkind"); Entry.Argument = Index; Buttons.Add(Entry);
        }
        const float ControlY=Y+(Narrow ? 142 : 99)*S;
        const int32 QuickValues[] = {1, 3, 6};
        float ControlX = X + 14 * S;
        for (int32 Quantity : QuickValues)
        {
            Button(FString::FromInt(Quantity), TEXT("trainqty"), Quantity, ControlX, ControlY, 44 * S,
                GlobalTrainQuantity == Quantity);
            ControlX += 48 * S;
        }
        Button(TEXT("-"), TEXT("trainqtydelta"), -1, ControlX, ControlY, 44 * S); ControlX += 48 * S;
        Button(TEXT("+"), TEXT("trainqtydelta"), 1, ControlX, ControlY, 44 * S); ControlX += 48 * S;
        const float QueueW=X+W-14*S-ControlX;
        Button(FString::Printf(TEXT("QUEUE x%d"), GlobalTrainQuantity), TEXT("globaltrain"), 0,
            ControlX, ControlY, QueueW, !bAwaitingTutorialChoice && Plan.accepted);
        float Completion = 0;
        for (const cinder::ProductionAssignment& Assignment : Plan.assignments)
            Completion = FMath::Max(Completion, Assignment.completionSeconds);
        const FString Status = bAwaitingTutorialChoice ? TEXT("CHOOSE A UNIT TO TRAIN") : Plan.accepted
            ? FString::Printf(TEXT("%d ORE / %d SUPPLY / %d PRODUCERS / FINISH %.0fs"),
                Plan.totalCost, Plan.totalSupply, static_cast<int32>(Plan.assignments.size()), Completion)
            : UTF8_TO_TCHAR(Plan.message.c_str());
        SingleLineLabel(Status, X + 14 * S, Y + (Narrow ? 198*S : H-24*S), W - 28 * S,
            bAwaitingTutorialChoice ? Muted : Plan.accepted ? Mint : Amber, 0.56f);
        return;
    }

    if (CompactSheet == SheetGlobalResearch)
    {
        const FString Allocation = PinnedProducer
            ? FString::Printf(TEXT("PINNED %s"), *Name(PinnedProducer->kind).ToUpper()) : TEXT("AUTO FACILITY");
        SingleLineLabel(FString::Printf(TEXT("RESEARCH / %s"), *Allocation),
            X + 14 * S, Y + 14 * S, HeaderLabelWidth, Amber, 0.74f);
        const TCHAR* Names[] = {TEXT("TECH TIER"), TEXT("WEAPONS"), TEXT("ARMOR")};
        const float Gap = 7 * S;
        const float CellW = (W - 28 * S - Gap * 2) / 3;
        for (int32 Upgrade = 0; Upgrade < 3; ++Upgrade)
        {
            const auto Plan = Sim.autoResearchStatus(0, Upgrade, PinnedProducerId);
            const float CellX = X + 14 * S + Upgrade * (CellW + Gap);
            Button(Names[Upgrade], TEXT("globalresearch"), Upgrade, CellX, Y + 57 * S, CellW);
            FString Status=UTF8_TO_TCHAR(Plan.message.c_str());
            if(Plan.accepted) Status=FString::Printf(TEXT("%d ore / Ready"),Plan.totalCost);
            else if(Status.Contains(TEXT("already queued"))) Status=TEXT("Already queued");
            else if(Status.Contains(TEXT("maximum"))) Status=TEXT("Max level");
            else if(Status.Contains(TEXT("technology tier"))) Status=TEXT("Requires next tier");
            else if(Status.Contains(TEXT("queue space"))) Status=TEXT("Queues full");
            else if(Status.Contains(TEXT("Resonator"))) Status=TEXT("Needs a ready Resonator");
            else if(Status.Contains(TEXT("Insufficient ore"))) Status=FString::Printf(TEXT("Need %d ore"),Plan.totalCost);
            WrappedLabel(Status,CellX+8*S,Y+107*S,CellW-16*S,Plan.accepted ? Mint : Amber,0.54f);
        }
        SingleLineLabel(TEXT("Assigned to the next available Resonator."),
            X + 14 * S, Y + H - 24 * S, W - 28 * S, Muted, 0.55f);
    }
}

void ACinderHUD::DrawInfoDrawer(ACinderPlayerController* PC, ACinderBattlefield* Battle, bool bCompact)
{
    if (!PC || !Battle || PC->Selection().empty()) return;
    const auto& Sim = Battle->Sim();
    const cinder::Entity* Entity = Sim.find(PC->Selection().front());
    if (!Entity || !Entity->alive() || Entity->team != 0) return;
    const FCinderUnitDetails Details = CinderUnitInfo::Describe(Sim, *Entity);
    const float S = UIScale;
    const FBox2D Bounds = bCompact
        ? MobileLayout.Drawer
        : FBox2D(FVector2D(Margin, Height - 400 * S), FVector2D(Margin + 520 * S, Height - 204 * S));
    const float X = Bounds.Min.X, Y = Bounds.Min.Y, W = Bounds.GetSize().X, H = Bounds.GetSize().Y;
    Surface(X,Y,W,H,FLinearColor(0.009f,0.020f,0.032f,0.96f),10*S);
    UIRegions.Add(Bounds);
    UnitGlyph(Entity->kind, X + 14 * S, Y + 12 * S, 24 * S, Mint);
    SingleLineLabel(Details.Name.ToUpper(), X + 48 * S, Y + 8 * S, W - 184 * S, White, 0.88f);
    SingleLineLabel(Details.Role, X + 48 * S, Y + 27 * S, W - 184 * S, Muted, 0.60f);
    const bool bCombatPageAvailable = Details.bArmed || Details.bHealer;
    const int32 NextPage = InfoPage == 0 ? 1 : InfoPage == 1 && bCombatPageAvailable ? 2 : 0;
    const FString NextPageLabel = InfoPage == 0 ? TEXT("GUIDE")
        : InfoPage == 1 && bCombatPageAvailable ? TEXT("COMBAT") : TEXT("STATS");
    Button(NextPageLabel, TEXT("infotab"), NextPage,
        X + W - 122 * S, Y, 72 * S, InfoPage != 0);
    Button(TEXT("X"), TEXT("closesheet"), 0, X + W - 44 * S, Y, 44 * S);
    if (InfoPage == 1)
    {
        float TextY = Y + 56 * S;
        Label(TEXT("PURPOSE"), X + 14 * S, TextY, Amber, 0.62f);
        TextY += 18 * S;
        TextY += WrappedLabel(Details.Purpose, X + 14 * S, TextY, W - 28 * S, White, 0.68f);
        TextY += 7 * S;
        Label(TEXT("CAPABILITIES / UNLOCKS"), X + 14 * S, TextY, Amber, 0.62f);
        TextY += 18 * S;
        WrappedLabel(Details.Capabilities, X + 14 * S, TextY, W - 28 * S, White, 0.64f);
        return;
    }
    if (InfoPage == 2 && bCombatPageAvailable)
    {
        float TextY = Y + 56 * S;
        Label(Details.bHealer ? TEXT("HEALING EFFECT") : TEXT("COMBAT EFFECT"), X + 14 * S, TextY, Amber, 0.62f);
        TextY += 19 * S;
        TextY += WrappedLabel(Details.DamageNote, X + 14 * S, TextY, W - 28 * S, White, 0.68f);
        TextY += 8 * S;
        Label(TEXT("REFERENCE"), X + 14 * S, TextY, Amber, 0.62f);
        TextY += 18 * S;
        const FString Reference = Details.bHealer
            ? FString::Printf(TEXT("HEAL %.0f    INTERVAL %.2fs    CENTER RANGE %.0f"),
                Details.HealAmount, Details.HealInterval, Details.Range)
            : FString::Printf(TEXT("DAMAGE %.0f / HIT    COOLDOWN %.2fs    BASE RANGE %.0f + TARGET RADIUS"),
                Details.Damage, Details.Cooldown, Details.Range);
        WrappedLabel(Reference, X + 14 * S, TextY, W - 28 * S, Muted, 0.64f);
        return;
    }
    HealthBar(Details.Health, Details.MaxHealth, X + 14 * S, Y + 53 * S, W - 28 * S, Mint);
    SingleLineLabel(FString::Printf(TEXT("HEALTH %.0f / %.0f    ORDER %s"), Details.Health, Details.MaxHealth, *Details.OrderText.ToUpper()),
        X + 14 * S, Y + 61 * S, W - 28 * S, White, 0.67f);
    const bool bUnarmedBuilding = Details.bBuilding && !Details.bArmed && !Details.bHealer;
    const FString BuildingStatus = Entity->progress < 1
        ? FString::Printf(TEXT("BUILDING %.0f%%"), Entity->progress * 100)
        : !Entity->queue.empty() ? FString::Printf(TEXT("QUEUE %d"), static_cast<int32>(Entity->queue.size()))
        : FString(TEXT("READY"));
    FString Attack;
    if (Details.bHealer)
        Attack = FString::Printf(TEXT("HEAL %.0f / %.2fs    RANGE %.0f    ARMOR %.0f"), Details.HealAmount, Details.HealInterval, Details.Range, Details.Armor);
    else if (Details.bArmed)
        Attack = FString::Printf(TEXT("DAMAGE %.0f    BASE RANGE %.0f    ARMOR %.0f"), Details.Damage, Details.Range, Details.Armor);
    else if (bUnarmedBuilding)
        Attack = FString::Printf(TEXT("ARMOR %.0f    VISION %.0f    %s"), Details.Armor, Details.Vision, *BuildingStatus);
    else
        Attack = FString::Printf(TEXT("ARMOR %.0f    SPEED %.0f    VISION %.0f"), Details.Armor, Details.Speed, Details.Vision);
    SingleLineLabel(Attack, X + 14 * S, Y + 82 * S, W - 28 * S, Amber, 0.64f);
    if (!bUnarmedBuilding)
        SingleLineLabel(FString::Printf(TEXT("SPEED %.0f    VISION %.0f    COOLDOWN %.2fs"), Details.Speed, Details.Vision, Details.Cooldown),
            X + 14 * S, Y + 102 * S, W - 28 * S, Muted, 0.62f);
    WrappedLabel(Details.Purpose, X + 14 * S, Y + (bUnarmedBuilding ? 107 : 125) * S, W - 28 * S, White, 0.66f);
    if (bCombatPageAvailable)
        SingleLineLabel(TEXT("GUIDE > COMBAT FOR FULL EFFECTS"),
            X + 14 * S, Y + H - 23 * S, W - 28 * S, Muted, 0.56f);
}

void ACinderHUD::DrawCompactMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const auto& Sim = Battle->Sim(); const auto& Player = Sim.players()[0];
    const bool bOnline = Battle->IsOnlineMatch();
    const UCinderOnlineSubsystem* Online = bOnline ? PC->Online() : nullptr;
    FString OnlineHeading, OnlineDetail;
    const bool bOnlineNotice = GetOnlineNotice(PC, Battle, OnlineHeading, OnlineDetail);
    const float S = UIScale;
    const auto& L = MobileLayout;
    const float InnerW = L.Right - L.Left;
    const float Top = L.Top, DockY = L.Commands.Min.Y;
    const cinder::Entity* First = PC->Selection().empty() ? nullptr : Sim.find(PC->Selection().front());
    const bool bBuilding = First && cinder::definition(First->kind).building;
    const bool bSite = bBuilding && First->progress < 1;
    const bool bWorker = First && First->kind == cinder::Kind::Worker;
    const bool bProductionBuilding = First && (First->kind == cinder::Kind::Laboratory
        || std::any_of(cinder::definitions().begin(), cinder::definitions().end(), [&](const auto& Definition)
        {
            return !Definition.building && Definition.kind != cinder::Kind::Resource
                && Definition.producer == First->kind;
        }));
    const bool bCombatSelection = std::any_of(PC->Selection().begin(), PC->Selection().end(), [&](cinder::Id Id)
    {
        const cinder::Entity* Entity = Sim.find(Id);
        return Entity && Entity->alive() && Entity->team == 0
            && !cinder::definition(Entity->kind).building && Entity->kind != cinder::Kind::Worker
            && Entity->kind != cinder::Kind::Resource;
    });
    if (CompactSheet == SheetQueue && (!bBuilding || bSite)) CompactSheet = SheetNone;
    const bool bSheetVisible = !PC->IsBuildMode() && (PC->bBuildMenu || CompactSheet != 0);

    // Separate resource chips leave the middle of the screen transparent.
    const auto Chip = [&](const FString& Glyph, const FString& Value, float X, float W, FLinearColor Color)
    {
        Surface(X, Top+6*S, W, 30*S, FLinearColor(0.006f,0.015f,0.026f,0.88f), 7*S);
        ActionGlyph(Glyph,0,X+9*S,Top+13*S,16*S,Color);
        SingleLineLabel(Value,X+32*S,Top+13*S,W-39*S,Color,0.80f);
        UIRegions.Add(FBox2D(FVector2D(X,Top+6*S),FVector2D(X+W,Top+36*S)));
    };
    Chip(TEXT("ore"),FString::FromInt(Player.ore),L.Left,94*S,Amber);
    Chip(TEXT("crew"),FString::Printf(TEXT("%d/%d"),Sim.supply(0),Sim.capacity(0)),L.Left+100*S,90*S,White);
    Chip(TEXT("tech"),FString::Printf(TEXT("T%d  W%d  A%d"),Player.tier,Player.weapons,Player.armor),L.Left+196*S,118*S,Mint);
    const FString Time = bOnline && Online && Online->PlayerCount() == 4
        ? FString::Printf(TEXT("%d/4 IN"), Online->RemainingPlayers())
        : bOnline ? Online && Online->PingMilliseconds() >= 0
        ? FString::Printf(TEXT("%.0f ms"),Online->PingMilliseconds()) : TEXT("Online") : ClockString(Sim.time());
    Chip(TEXT("clock"),Time,L.Left+320*S,84*S,Muted);
    ActionButton(TEXT("Help"),TEXT("help"),0,L.Right-92*S,Top,44*S);
    ActionButton(TEXT("Menu"),TEXT("pause"),0,L.Right-44*S,Top,44*S);

    const float RailY=L.GlobalActions.Min.Y;
    ActionButton(TEXT("Build"),TEXT("globalcatalog"),SheetGlobalBuild,L.Left,RailY,48*S,
        CompactSheet==SheetGlobalBuild || PC->IsGlobalBuildMode());
    ActionButton(TEXT("Train"),TEXT("globalcatalog"),SheetGlobalTrain,L.Left,RailY+48*S,48*S,CompactSheet==SheetGlobalTrain);
    ActionButton(TEXT("Research"),TEXT("globalcatalog"),SheetGlobalResearch,L.Left,RailY+96*S,48*S,CompactSheet==SheetGlobalResearch);
    ActionButton(TEXT("Army"),TEXT("army"),0,L.Left,RailY+144*S,48*S,CompactSheet==SheetArmy);
    if (!PC->Selection().empty())
        ActionButton(TEXT("Deselect"),TEXT("deselect"),0,L.Navigation.Min.X,L.Navigation.Min.Y,44*S);
    ActionButton(TEXT("Select"),TEXT("box"),0,L.Navigation.Min.X+48*S,L.Navigation.Min.Y,44*S,PC->bBoxSelect);
    ActionButton(TEXT("Home"),TEXT("home"),0,L.Navigation.Min.X+96*S,L.Navigation.Min.Y,44*S);
    if (!bCompactLayout)
    {
        ActionButton(TEXT("Zoom in"),TEXT("zoom+"),0,L.Navigation.Min.X+48*S,L.Navigation.Min.Y+48*S,44*S);
        ActionButton(TEXT("Zoom out"),TEXT("zoom-"),0,L.Navigation.Min.X+96*S,L.Navigation.Min.Y+48*S,44*S);
    }
    if (!bSheetVisible && !PC->IsBuildMode()) DrawMinimap(PC,Battle);

    if (PC->IsBuildMode() || PC->IsProductionRallyMode())
        ActionButton(PC->IsBuildMode() ? TEXT("Cancel build") : TEXT("Cancel rally"),
            PC->IsBuildMode() ? TEXT("cancelplacement") : TEXT("cancelrally"),0,L.Right-96*S,DockY,96*S,true);
    else if (bSheetVisible)
    {
        // The palette already exposes its actions. Only keep its dismissal at the thumb.
        ActionButton(TEXT("Close"),TEXT("closesheet"),0,L.Right-48*S,DockY,48*S,true);
    }
    else
    {
        const float W=49*S, Gap=4*S, X=L.Commands.Min.X;
        if (bCombatSelection)
        {
            ActionButton(TEXT("Move"),TEXT("move"),0,X,DockY,W,PC->IsMoveCommandMode());
            ActionButton(TEXT("Attack"),TEXT("attack"),0,X+W+Gap,DockY,W,PC->IsAttackMoveMode());
            ActionButton(TEXT("Defend"),TEXT("defend"),0,X+2*(W+Gap),DockY,W,PC->IsDefendCommandMode());
            ActionButton(TEXT("Orders"),TEXT("orders"),0,X+3*(W+Gap),DockY,W);
        }
        else if(bWorker)
        {
            ActionButton(TEXT("Move"),TEXT("move"),0,X,DockY,W,PC->IsMoveCommandMode());
            ActionButton(TEXT("Build here"),TEXT("buildmenu"),0,X+W+Gap,DockY,W,PC->bBuildMenu);
            ActionButton(TEXT("Stop"),TEXT("stop"),0,X+2*(W+Gap),DockY,W);
            ActionButton(TEXT("Orders"),TEXT("orders"),0,X+3*(W+Gap),DockY,W);
        }
        else if(bBuilding)
        {
            if(bSite)
                ActionButton(TEXT("Site"),TEXT("sheet"),SheetContext,X,DockY,W);
            else if(First->kind==cinder::Kind::Laboratory)
                ActionButton(TEXT("Tech"),TEXT("sheet"),SheetContext,X,DockY,W);
            else if(bProductionBuilding)
            {
                ActionButton(TEXT("Train here"),TEXT("globalcatalog"),SheetGlobalTrain,X,DockY,W);
                Buttons.Last().EntityId=First->id;
            }
            else ActionButton(TEXT("Info"),TEXT("info"),0,X,DockY,W);
            ActionButton(TEXT("Focus"),TEXT("focus"),0,X+W+Gap,DockY,W);
            if(bProductionBuilding && !bSite)
            {
                ActionButton(bCompactLayout ? TEXT("Queue") : TEXT("Jobs"),bCompactLayout ? TEXT("sheet") : TEXT("producerjobs"),
                    bCompactLayout ? SheetQueue : 0,X+2*(W+Gap),DockY,W);
                if(!bCompactLayout) Buttons.Last().EntityId=First->id;
            }
            else ActionButton(TEXT("Workers"),TEXT("workers"),0,X+2*(W+Gap),DockY,W);
            ActionButton(TEXT("Orders"),TEXT("orders"),0,X+3*(W+Gap),DockY,W);
        }
        else
        {
            // No selected unit means no selection-specific orders to crowd the battlefield.
            ActionButton(TEXT("Workers"),TEXT("workers"),0,L.Right-48*S,DockY,48*S);
        }
    }

    if (!PC->IsBuildMode() && !bSheetVisible)
    {
        DrawUnitRibbon(PC, Battle);
        DrawSelectionIdentity(PC, Battle, true);
    }

    if (!PC->IsBuildMode() && CompactSheet == SheetArmy)
        DrawArmyDrawer(PC, Battle, true);
    else if (!PC->IsBuildMode() && CompactSheet == SheetInfo)
        DrawInfoDrawer(PC, Battle, true);
    else if (!PC->IsBuildMode() && (CompactSheet == SheetGlobalBuild
        || CompactSheet == SheetGlobalTrain || CompactSheet == SheetGlobalResearch))
        DrawGlobalCatalog(PC, Battle, true);
    else if (!PC->IsBuildMode() && !PC->bBuildMenu && CompactSheet == SheetQueue && bBuilding && !bSite)
    {
        const float SheetW = L.Drawer.GetSize().X - 10*S;
        const float X = L.Drawer.Min.X + 5*S;
        const int Count = static_cast<int>(First->queue.size());
        const int Pages = FMath::Max(1, FMath::DivideAndRoundUp(Count, 6));
        QueuePage = FMath::Clamp(QueuePage, 0, Pages - 1);
        const int Rows = FMath::Max(1, FMath::DivideAndRoundUp(FMath::Min(6, Count - QueuePage * 6), 3));
        DrawProductionQueue(First, X, L.Drawer.Min.Y + 6*S, SheetW, 3);
    }
    else if (!PC->IsBuildMode() && (PC->bBuildMenu || CompactSheet != 0))
    {
        struct FOption { FString Text, Action; int Arg; cinder::Id EntityId = 0; };
        TArray<FOption> Options;
        FString Title;
        if (PC->bBuildMenu)
        {
            Title = TEXT("BUILD / ORE COST");
            for (auto K : { cinder::Kind::Headquarters, cinder::Kind::Processor, cinder::Kind::Foundry, cinder::Kind::MotorPool, cinder::Kind::Laboratory, cinder::Kind::Turret })
                Options.Add({ FString::Printf(TEXT("%s / %d"), *Name(K), cinder::definition(K).cost), TEXT("build"), static_cast<int>(K) });
        }
        else if (CompactSheet == SheetTypes)
        {
            Title = TEXT("SELECT AN ARMY SUBGROUP");
            std::map<cinder::Kind, int> Counts;
            for (auto Id : PC->Selection()) if (const auto* E = Sim.find(Id)) ++Counts[E->kind];
            for (const auto& Entry : Counts) Options.Add({ FString::Printf(TEXT("%s x%d"), *Name(Entry.first), Entry.second), TEXT("kind"), static_cast<int>(Entry.first) });
        }
        else if (CompactSheet == SheetOrders)
        {
            Title = TEXT("ORDERS / CURRENT SELECTION");
            if (First)
            {
                const int32 QueuedCount = static_cast<int32>(First->futureOrders.size());
                FString OrderHeading = RosterOrder(*First);
                if (PC->IsFacingPending()) OrderHeading = TEXT("FACING PENDING");
                else if (PC->IsFaceNextArmed()) OrderHeading = TEXT("FACE NEXT ARMED");
                else if (PC->IsQueueNextPending()) OrderHeading = TEXT("QUEUE PENDING");
                else if (PC->IsQueueNextArmed())
                    OrderHeading = PC->IsMoveCommandMode() ? TEXT("QUEUE MOVE") : TEXT("QUEUE ATTACK");
                else if (PC->IsPatrolCommandMode())
                    OrderHeading = PC->IsDestinationPending() ? TEXT("PATROL PENDING") : TEXT("PATROL: CHOOSE POINT");
                else if (PC->IsEscortCommandMode())
                    OrderHeading = PC->IsDestinationPending() ? TEXT("ESCORT PENDING") : TEXT("ESCORT: CHOOSE UNIT");
                Title = FString::Printf(TEXT("ORDERS / %s / %d QUEUED"), *OrderHeading, QueuedCount);
                Options.Add({ TEXT("FOCUS SELECTION"), TEXT("focus"), 0 });
                if (!bBuilding)
                {
                    const TCHAR* SpacingName = PC->FormationSpacingPreset() == cinder::FormationSpacing::Tight
                        ? TEXT("TIGHT") : PC->FormationSpacingPreset() == cinder::FormationSpacing::Wide
                        ? TEXT("WIDE") : TEXT("STD");
                    Options.Add({ FString::Printf(TEXT("SPACE %s"), SpacingName), TEXT("space"), 0 });
                    if (PC->IsMoveCommandMode() || PC->IsAttackMoveMode() || PC->IsDefendCommandMode())
                        Options.Add({ PC->IsFacingPending() ? TEXT("FACE PENDING") : TEXT("FACE NEXT"),
                            TEXT("facenext"), 0 });
                    Options.Add({ PC->IsDestinationPending() && PC->IsPatrolCommandMode()
                        ? TEXT("PATROL PENDING") : TEXT("PATROL"), TEXT("patrol"), 0 });
                    Options.Add({ PC->IsDestinationPending() && PC->IsEscortCommandMode()
                        ? TEXT("ESCORT PENDING") : TEXT("ESCORT"), TEXT("escort"), 0 });
                    FString QueueLabel = TEXT("QUEUE NEXT");
                    if (PC->IsQueueNextPending()) QueueLabel = TEXT("QUEUE PENDING");
                    else if (PC->IsMoveCommandMode()) QueueLabel = TEXT("QUEUE MOVE");
                    else if (PC->IsAttackMoveMode()) QueueLabel = TEXT("QUEUE ATTACK");
                    Options.Add({ QueueLabel, TEXT("queuenext"), 0 });
                    const bool bHasQueuedOrders = std::any_of(PC->Selection().begin(), PC->Selection().end(), [&](cinder::Id Id)
                    {
                        const cinder::Entity* Entity = Sim.find(Id);
                        return Entity && Entity->alive() && Entity->team == 0 && !Entity->futureOrders.empty();
                    });
                    if (bHasQueuedOrders)
                        Options.Add({ TEXT("CLEAR QUEUED"), TEXT("clearorders"), 0 });
                    Options.Add({ TEXT("STOP"), TEXT("stop"), 0 });
                    Options.Add({ TEXT("HOLD POSITION"), TEXT("hold"), 0 });
                }
                if (PC->Selection().size() > 1) Options.Add({ TEXT("SELECT BY TYPE"), TEXT("sheet"), SheetTypes });
            }
            Options.Add({ TEXT("SELECT DRUDGES"), TEXT("workers"), 0 });
            Options.Add({ TEXT("OPEN BUILD"), TEXT("globalcatalog"), SheetGlobalBuild });
        }
        else
        {
            Title = TEXT("CONTEXTUAL ACTIONS");
            if (First && First->progress < 1)
            {
                Title = ConstructionStatus(Sim, *First) + (Sim.constructionWorker(First->id) ? TEXT(" / DRUDGE ASSIGNED") : TEXT(" / NEEDS A DRUDGE"));
                if (!Sim.constructionWorker(First->id)) Options.Add({ TEXT("ASSIGN DRUDGE"), TEXT("resumeconstruction"), 0 });
                Options.Add({ TEXT("CANCEL BUILDING"), TEXT("cancelbuilding"), 0 });
            }
            else if (First && cinder::definition(First->kind).building)
            {
                if (First->kind == cinder::Kind::Laboratory)
                {
                    Options.Add({TEXT("TRAIN MEND"), TEXT("globalcatalog"), SheetGlobalTrain, First->id});
                    Options.Add({TEXT("RESEARCH"), TEXT("globalcatalog"), SheetGlobalResearch, First->id});
                    Options.Add({TEXT("VIEW JOBS"), TEXT("producerjobs"), 0, First->id});
                    Options.Add({TEXT("RALLY THIS"), TEXT("productionrally"), 0, First->id});
                }
                else
                {
                    for (const auto& D : cinder::definitions())
                        if (!D.building && D.kind != cinder::Kind::Resource && D.producer == First->kind)
                            Options.Add({ FString::Printf(TEXT("%s / %d"), *Name(D.kind), D.cost),
                                TEXT("train"), static_cast<int>(D.kind) });
                }
            }
            else
            {
                Options.Add({ TEXT("STOP"), TEXT("stop"), 0 });
                Options.Add({ TEXT("HOLD POSITION"), TEXT("hold"), 0 });
                Options.Add({ TEXT("ATTACK MOVE"), TEXT("attack"), 0 });
                Options.Add({ TEXT("FOCUS"), TEXT("focus"), 0 });
            }
        }
        const int Columns = Options.Num() > 6 ? 4 : 3;
        const int Rows = FMath::Max(1, FMath::DivideAndRoundUp(Options.Num(), Columns));
        const float SheetW = L.Drawer.GetSize().X - 10*S;
        const float SheetH = (52 + Rows * 50) * S;
        const float X = L.Drawer.Min.X + 5*S, Y = L.Drawer.Min.Y;
        Surface(X - 5*S,Y,SheetW+10*S,SheetH,Ink,10*S);
        UIRegions.Add(FBox2D(FVector2D(X - 5 * S, Y), FVector2D(X + SheetW + 5 * S, Y + SheetH)));
        SingleLineLabel(Title, X + 8 * S, Y + 16 * S, SheetW - 70 * S, Amber, 0.74f);
        Button(TEXT("X"), TEXT("closesheet"), 0, X + SheetW - 44 * S, Y + 2 * S, 44 * S);
        const float CellW = (SheetW - (Columns - 1) * 7 * S) / Columns;
        for (int I = 0; I < Options.Num(); ++I)
        {
            const float BX = X + (I % Columns) * (CellW + 7 * S), BY = Y + (52 + (I / Columns) * 50) * S;
            Button(Options[I].Text, Options[I].Action, Options[I].Arg, BX, BY, CellW,
                (Options[I].Action == TEXT("queuenext") && PC->IsQueueNextArmed())
                || (Options[I].Action == TEXT("facenext") && PC->IsFaceNextArmed())
                || Options[I].Action == TEXT("space")
                || (Options[I].Action == TEXT("patrol") && PC->IsPatrolCommandMode())
                || (Options[I].Action == TEXT("escort") && PC->IsEscortCommandMode()));
            Buttons.Last().EntityId = Options[I].EntityId;
            if (Options[I].Action == TEXT("build"))
            {
                const auto Kind = static_cast<cinder::Kind>(Options[I].Arg);
                const auto Status = Sim.buildStatus(0, Kind, PC->Selection());
                SingleLineLabel(FString::Printf(TEXT("T%d / %s"), cinder::definition(Kind).tier, Status.accepted ? TEXT("READY") : TEXT("LOCKED")),
                    BX + 12 * S, BY + 27 * S, CellW - 24 * S, Status.accepted ? Mint : Amber, 0.70f);
            }
        }
        if (Options.IsEmpty()) Label(TEXT("Select a unit or a production structure first."), X + 5 * S, Y + 56 * S, White, 0.78f);
    }
    const bool bTutorialCardVisible = Battle->Tutorial().IsActive()
        && !Battle->IsPaused() && Sim.winner() == -1;
    if (!bOnlineNotice && !bTutorialCardVisible && !PC->Feedback().IsEmpty())
    {
        const float FeedbackX=bSheetVisible ? L.Drawer.Max.X+8*S : L.Drawer.Min.X;
        const float FeedbackY=bSheetVisible ? L.Navigation.Max.Y+8*S : L.Top+48*S;
        const float MaxW=FMath::Min(320*S,L.Right-FeedbackX);
        if(MaxW>=130*S)
        {
            const float FW=FMath::Min(MaxW,static_cast<float>(MeasureLabel(PC->Feedback(),0.64f).X)+20*S);
            const float FH=WrappedHeight(PC->Feedback(),FW-20*S,0.64f)+16*S;
            Surface(FeedbackX,FeedbackY,FW,FH,FLinearColor(0.007f,0.016f,0.027f,0.96f),7*S);
            Surface(FeedbackX,FeedbackY+8*S,2*S,FH-16*S,Amber,S);
            WrappedLabel(PC->Feedback(),FeedbackX+10*S,FeedbackY+8*S,FW-20*S,White,0.64f);
        }
    }
    if (PC->bDebug && !bOnlineNotice && !bSheetVisible && !PC->IsBuildMode())
        Label(FString::Printf(TEXT("%.2f ms sim / %d entities / %.0f fps"), Sim.lastStepMilliseconds(), static_cast<int>(Sim.entities().size()), 1.0f / FMath::Max(GetWorld()->GetDeltaSeconds(), 0.001f)), Margin, 134 * S + SafeTopOffset, Mint, 0.72f);
    if (bOnlineNotice) DrawOnlineNotice(OnlineHeading, OnlineDetail, L.Drawer.Min.X, L.Top+48*S, FMath::Min(420*S,InnerW-170*S), true);
    DrawOverlay(PC, Battle);
}

void ACinderHUD::DrawMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    DrawCompactMatch(PC, Battle);
}

void ACinderHUD::DrawOverlay(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const auto& Sim = Battle->Sim(); const auto& Player = Sim.players()[0];
    const bool bOnline = Battle->IsOnlineMatch();
    const bool bEliminated = bOnline && Sim.eliminated(0);
    const bool bShowResult = Sim.winner() != -1 || bEliminated;
    if (Battle->IsPaused() || bShowResult)
    {
        Panel(0, 0, Width, Height, FLinearColor(0.015f, 0.03f, 0.04f, 0.89f));
        Buttons.Reset(); UIRegions.Reset(); Minimap.Init();
        UIRegions.Add(FBox2D(FVector2D::ZeroVector, FVector2D(Width, Height)));
        const float X = Width * 0.5f - 270 * UIScale, Y = bCompactLayout ? 28 * UIScale + SafeTopOffset : Height * 0.2f;
        const bool bTraining = Battle->Tutorial().IsActive();
        const FString Heading = Sim.winner() == -2 ? TEXT("DRAW")
            : bEliminated && Sim.winner() == -1 ? TEXT("ELIMINATED")
            : Sim.winner() != -1
            ? bOnline ? Sim.winner() == 0 ? TEXT("YOU WIN") : TEXT("DEFEAT")
                : bTraining ? Sim.winner() == 0 ? TEXT("TUTORIAL VICTORY") : TEXT("TUTORIAL DEFEAT")
                : Sim.winner() == 0 ? TEXT("FRONTIER SECURED") : TEXT("ANCHOR LOST")
            : bOnline ? TEXT("ONLINE MATCH CONTINUES") : bTraining ? TEXT("TRAINING PAUSED") : TEXT("SKIRMISH PAUSED");
        Label(Heading, X, Y, Sim.winner() > 0 || bEliminated ? Amber : Mint, 1.65f);
        if (bShowResult)
        {
            const auto& S = Player.stats;
            if (bOnline)
            {
                const auto* Online = PC->Online();
                FString Outcome;
                if (Sim.winner() == -1)
                    Outcome = FString::Printf(TEXT("%d commanders remain. The match continues; you can leave."),
                        Online ? Online->RemainingPlayers() : 0);
                else if (Sim.winner() == -2) Outcome = TEXT("No Anchors remain. The match ended in a draw.");
                else if (Sim.winner() == 0) Outcome = TEXT("Your Anchor is the last one standing.");
                else
                {
                    const int32 Seat = Online ? (Online->LocalSeat() + Sim.winner()) % Online->PlayerCount() : 0;
                    const FString WinnerName = Online ? Online->SeatName(Seat) : FString();
                    Outcome = FString::Printf(TEXT("%s won the match."), WinnerName.IsEmpty() ? TEXT("An opponent") : *WinnerName);
                }
                SingleLineLabel(Outcome, X, Y + 40 * UIScale, 480 * UIScale, Muted, 0.70f);
            }
            if (bTraining && Sim.winner() == 0)
                Label(Battle->Tutorial().IsComplete()
                        ? TEXT("GUIDED TUTORIAL COMPLETE")
                        : TEXT("VICTORY REACHED / REPLAY TO FINISH THE LESSONS"),
                    X, Y + 40 * UIScale, Mint, 0.72f);
            Label(FString::Printf(TEXT("%s    Ore collected %d    Damage %.0f"), *ClockString(Sim.time()), S.gathered, S.damage), X, Y + 62 * UIScale, White, 0.9f);
            Label(FString::Printf(TEXT("Produced %d    Lost %d    Destroyed %d"), S.produced, S.lost, S.killed), X, Y + 96 * UIScale, White, 0.9f);
            Label(FString::Printf(TEXT("Structures %d    Razed %d    Expansions %d"), S.built, S.buildingsDestroyed, S.expansions), X, Y + 130 * UIScale, White, 0.9f);
            Label(FString::Printf(TEXT("Technology upgrades %d"), S.upgrades), X, Y + 164 * UIScale, White, 0.9f);
            if (bOnline)
            {
                Button(TEXT("LEAVE / TO MENU"), TEXT("onlineleave"), 0, X, Y + 224 * UIScale, 480 * UIScale, true);
            }
            else
            {
                Button(bTraining ? TEXT("REPLAY TUTORIAL") : bCompactLayout ? TEXT("REMATCH") : TEXT("REMATCH  [ENTER]"),
                    bTraining ? TEXT("tutorialrestart") : TEXT("start"), Battle->MapIndex(), X, Y + 224 * UIScale, 230 * UIScale, true);
                Button(TEXT("MAIN MENU"), TEXT("menu"), 0, X + 250 * UIScale, Y + 224 * UIScale, 230 * UIScale);
            }
        }
        else if (bOnline)
        {
            Label(TEXT("The server keeps advancing while this menu is open."), X, Y + 48 * UIScale, White, 0.80f);
            Button(TEXT("RESUME"), TEXT("resume"), 0, X, Y + 80 * UIScale, 230 * UIScale, true);
            Button(TEXT("FIELD GUIDE"), TEXT("help"), 0, X + 250 * UIScale, Y + 80 * UIScale, 230 * UIScale);
            Button(TEXT("CONNECTION"), TEXT("connection"), 0, X, Y + 136 * UIScale, 230 * UIScale);
            Button(TEXT("SURRENDER"), TEXT("onlinesurrender"), 0, X + 250 * UIScale, Y + 136 * UIScale, 230 * UIScale);
            Button(TEXT("LEAVE"), TEXT("onlineleave"), 0, X, Y + 192 * UIScale, 480 * UIScale);
            if (!PC->Feedback().IsEmpty()) Label(PC->Feedback(), X, Y + 250 * UIScale, Amber, 0.85f);
        }
        else
        {
            Button(TEXT("RESUME"), TEXT("resume"), 0, X, Y + 80 * UIScale, 230 * UIScale, true);
            Button(bTraining ? bCompactLayout ? TEXT("RESTART TRAINING") : TEXT("RESTART TRAINING  [T]") : TEXT("SAVE MATCH"),
                bTraining ? TEXT("tutorialrestart") : TEXT("save"), 0,
                X + 250 * UIScale, Y + 80 * UIScale, 230 * UIScale);
            Button(bTraining ? TEXT("FIELD GUIDE") : TEXT("LOAD MATCH"), bTraining ? TEXT("help") : TEXT("load"), 0,
                X, Y + 136 * UIScale, 230 * UIScale);
            Button(TEXT("MAIN MENU"), TEXT("menu"), 0, X + 250 * UIScale, Y + 136 * UIScale, 230 * UIScale);
            Button(PC->bDebug ? TEXT("HIDE METRICS") : TEXT("SHOW METRICS"), TEXT("debug"), 0, X, Y + 192 * UIScale, 230 * UIScale, PC->bDebug);
            if (!bTraining) Button(TEXT("FIELD GUIDE"), TEXT("help"), 0, X + 250 * UIScale, Y + 192 * UIScale, 230 * UIScale);
            Label(TEXT("A attack-move / S stop / H hold / B build / F focus / Space home"), X, Y + 264 * UIScale, Muted, 0.78f);
            if (!PC->Feedback().IsEmpty()) Label(PC->Feedback(), X, Y + 310 * UIScale, Amber, 0.85f);
        }
    }

    if (PC->IsOnlineLeavePending())
    {
        Buttons.Reset(); UIRegions.Reset(); Minimap.Init();
        Panel(0, 0, Width, Height, FLinearColor(0.002f, 0.008f, 0.014f, 0.95f));
        UIRegions.Add(FBox2D(FVector2D::ZeroVector, FVector2D(Width, Height)));
        const float S = UIScale;
        const float W = FMath::Min(540 * S, Width - 2 * Margin);
        const float H = 218 * S;
        const float X = (Width - W) * 0.5f;
        const float Y = (Height - H) * 0.5f;
        Panel(X, Y, W, H, PanelInk);
        Panel(X, Y, 4 * S, H, Amber);
        Label(PC->IsOnlineSurrenderPending() ? TEXT("SURRENDER MATCH?") : TEXT("LEAVE MATCH?"), X + 24 * S, Y + 22 * S, White, 1.35f);
        WrappedLabel(PC->IsOnlineSurrenderPending() ? TEXT("Your forces are eliminated. Any remaining opponents keep fighting.") : TEXT("Leaving forfeits this game."), X + 24 * S, Y + 62 * S, W - 48 * S, White, 0.80f);
        const float ButtonW = (W - 55 * S) * 0.5f;
        Button(PC->IsOnlineSurrenderPending() ? TEXT("SURRENDER") : TEXT("LEAVE AND FORFEIT"), TEXT("onlineconfirm"), 0, X + 24 * S, Y + H - 66 * S, ButtonW, true);
        Button(TEXT("CANCEL"), TEXT("onlinecancel"), 0, X + 31 * S + ButtonW, Y + H - 66 * S, ButtonW);
    }

    if (PC->IsTutorialRestartPending())
    {
        Buttons.Reset(); UIRegions.Reset(); Minimap.Init();
        Panel(0, 0, Width, Height, FLinearColor(0.002f, 0.008f, 0.014f, 0.95f));
        UIRegions.Add(FBox2D(FVector2D::ZeroVector, FVector2D(Width, Height)));
        const float S = UIScale;
        const float W = FMath::Min(540 * S, Width - 2 * Margin);
        const float H = 218 * S;
        const float X = (Width - W) * 0.5f;
        const float Y = (Height - H) * 0.5f;
        Panel(X, Y, W, H, PanelInk);
        Panel(X, Y, 4 * S, H, Amber);
        Label(TEXT("RESTART TRAINING?"), X + 24 * S, Y + 22 * S, White, 1.35f);
        WrappedLabel(TEXT("This restarts the guided battle from step one. Your saved skirmish is kept."),
            X + 24 * S, Y + 62 * S, W - 48 * S, White, 0.80f);
        const float ButtonW = (W - 55 * S) * 0.5f;
        Button(bCompactLayout ? TEXT("RESTART") : TEXT("RESTART  [ENTER]"), TEXT("tutorialconfirm"), 0,
            X + 24 * S, Y + H - 66 * S, ButtonW, true);
        Button(bCompactLayout ? TEXT("CANCEL") : TEXT("CANCEL  [ESC]"), TEXT("tutorialcancel"), 0,
            X + 31 * S + ButtonW, Y + H - 66 * S, ButtonW);
    }
}
