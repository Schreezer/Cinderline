#include "Presentation/CinderHUD.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderPlayerController.h"
#include "Presentation/CinderAudioSubsystem.h"
#include "Presentation/CinderHelpContent.h"
#include "Presentation/CinderOnlineSubsystem.h"
#include "Presentation/CinderTutorial.h"
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
#if PLATFORM_MAC
const TCHAR* DesktopControlHint = TEXT("Option+drag pan / Drag select / Scroll zoom");
#else
const TCHAR* DesktopControlHint = TEXT("Alt+drag pan / Drag select / Scroll zoom");
#endif
FString Name(cinder::Kind Kind) { return UTF8_TO_TCHAR(cinder::definition(Kind).name); }
FString ClockString(float Seconds) { return FString::Printf(TEXT("%02d:%02d"), static_cast<int>(Seconds) / 60, static_cast<int>(Seconds) % 60); }
FString ConstructionStatus(const cinder::Simulation& Sim, const cinder::Entity& Site)
{
    const TCHAR* State = Sim.constructionActive(Site.id) ? TEXT("BUILDING") : Sim.constructionWorker(Site.id) ? TEXT("EN ROUTE") : TEXT("PAUSED");
    return FString::Printf(TEXT("%s %d%%"), State, static_cast<int>(Site.progress * 100));
}
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
    const float H = 44 * UIScale;
    float MouseX = -1, MouseY = -1;
    if (PlayerOwner) PlayerOwner->GetMousePosition(MouseX, MouseY);
    const bool Hover = MouseX >= X && MouseY >= Y && MouseX < X + W && MouseY < Y + H;
    const bool Primary = Action == TEXT("start");
    const bool Warning = Action == TEXT("pause") || Action.StartsWith(TEXT("cancel"));
    const FLinearColor Accent = Warning ? Amber : Mint;
    Panel(X, Y, W, H, Primary ? Mint : Active ? FLinearColor(0.018f, 0.11f, 0.105f) : Hover ? FLinearColor(0.026f, 0.055f, 0.072f) : PanelInk);
    if (!Primary)
    {
        // Broad value bands read at phone scale and give the flat tile an inset face.
        Panel(X + UIScale, Y + UIScale, W - 2 * UIScale, 8 * UIScale, FLinearColor(0.035f, 0.070f, 0.082f, Hover ? 0.82f : 0.54f));
        Panel(X + UIScale, Y + H - 7 * UIScale, W - 2 * UIScale, 6 * UIScale, FLinearColor(0.002f, 0.008f, 0.013f, 0.64f));
        Panel(X + W - UIScale, Y + UIScale, UIScale, H - 2 * UIScale, FLinearColor(0.002f, 0.008f, 0.013f, 0.88f));
    }
    const FLinearColor Edge = Active || Primary ? Accent : FLinearColor(0.075f, 0.16f, 0.18f);
    Panel(X, Y, W, UIScale, Edge);
    Panel(X, Y + H - UIScale, W, UIScale, FLinearColor(0.018f, 0.055f, 0.065f));
    const float Bracket = FMath::Min(9 * UIScale, W * 0.22f);
    Panel(X, Y, Bracket, 2 * UIScale, Edge);
    Panel(X, Y, 2 * UIScale, 9 * UIScale, Edge);
    Panel(X + W - Bracket, Y + H - 2 * UIScale, Bracket, 2 * UIScale, Warning ? Amber : FLinearColor(0.07f, 0.25f, 0.25f));
    Panel(X + W - 2 * UIScale, Y + H - 9 * UIScale, 2 * UIScale, 9 * UIScale, Warning ? Amber : FLinearColor(0.07f, 0.25f, 0.25f));
    if (Active && !Primary) Panel(X, Y, 3 * UIScale, H, Accent);
    const float TextWidth = MeasureLabel(Text, 0.80f).X;
    const float Fit = TextWidth > 0 ? FMath::Min(1.0f, (W - 20 * UIScale) / TextWidth) : 1;
    Label(Text, X + 12 * UIScale, Y + (Action == TEXT("build") ? 6 : 12) * UIScale, Primary ? Ink : Active ? Accent : White, 0.80f * Fit);
    FButton B; B.Bounds = FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)); B.Action = Action; B.Argument = Arg; Buttons.Add(B);
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
        else if (B.Action == TEXT("helpsection")) CompactHelpSection = FMath::Max(0, B.Argument);
        else if (B.Action == TEXT("tutorialdetails")) { bTutorialDetails = !bTutorialDetails; CompactSheet = 0; PC->bBuildMenu = false; }
        else if (B.Action == TEXT("sheet")) { CompactSheet = CompactSheet == B.Argument ? 0 : B.Argument; PC->bBuildMenu = false; bTutorialDetails = false; }
        else if (B.Action == TEXT("closesheet")) { CompactSheet = 0; PC->bBuildMenu = false; }
        else
        {
            if (B.Action == TEXT("tutorialfocus") || B.Action == TEXT("buildmenu")) bTutorialDetails = false;
            if (B.Action == TEXT("buildmenu") || B.Action == TEXT("kind") || B.Action == TEXT("box")
                || B.Action == TEXT("attack") || B.Action == TEXT("stop") || B.Action == TEXT("hold")
                || B.Action == TEXT("focus") || B.Action == TEXT("workers") || B.Action == TEXT("cancelbuilding")) CompactSheet = 0;
            PC->ExecuteAction(B.Action, B.Argument);
        }
        return true;
    }
    if (Minimap.bIsValid && Minimap.IsInside(Point) && PC->Battlefield() && !PC->Battlefield()->IsMenu() && !PC->Battlefield()->IsPaused())
    {
        const int X = FMath::Clamp(static_cast<int>((Point.X - Minimap.Min.X) / Minimap.GetSize().X * cinder::Simulation::WorldSize), 0, 4800);
        const int Y = FMath::Clamp(static_cast<int>((Point.Y - Minimap.Min.Y) / Minimap.GetSize().Y * cinder::Simulation::WorldSize), 0, 4800);
        PC->ExecuteAction(TEXT("minimap"), Y * 10000 + X);
        return true;
    }
    return ContainsUI(Point);
}

bool ACinderHUD::CloseCompactSheet()
{
    const bool bWasOpen = CompactSheet != 0;
    CompactSheet = 0;
    return bWasOpen;
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
        const FVector4 CachedInsets = Metrics.TitleSafePaddingSize;
        const bool bCachedSafeAreaEmpty = CachedInsets.X <= 0.5f && CachedInsets.Y <= 0.5f
            && CachedInsets.Z <= 0.5f && CachedInsets.W <= 0.5f;
        // The initial Slate snapshot can precede UIWindow safe-area setup on iOS.
        // Refresh briefly after launch; orientation changes update this same cache.
        const double Now = FPlatformTime::Seconds();
        if (bCachedSafeAreaEmpty && SafeInsetRefreshAttempts < 8 && Now >= NextSafeInsetRefreshTime)
        {
            FSlateApplication::Get().GetDisplayMetrics(Metrics);
            ++SafeInsetRefreshAttempts;
            NextSafeInsetRefreshTime = Now + 0.25;
        }
        const float ScaleX = Width / FMath::Max(1, Metrics.PrimaryDisplayWidth);
        const float ScaleY = Height / FMath::Max(1, Metrics.PrimaryDisplayHeight);
        const auto& Insets = Metrics.TitleSafePaddingSize;
        SafeInsets = FVector4(Insets.X * ScaleX, Insets.Y * ScaleY, Insets.Z * ScaleX, Insets.W * ScaleY);
    }
    MobileLayout = FCinderMobileHUDLayout::Make(FVector2D(Width, Height), SafeInsets);
    UIScale = bCompactLayout ? MobileLayout.Scale : FMath::Max(0.45f, FMath::Min(Width / 1280.0f, Height / 720.0f));
    Margin = bCompactLayout ? FMath::Max(MobileLayout.Left, Width - MobileLayout.Right) : FMath::Max(24 * UIScale, Width * 0.035f);
    if (bCompactLayout)
    {
        SafeTopOffset = MobileLayout.Top - 10 * UIScale;
        SafeBottomOffset = Height - MobileLayout.Bottom - 12 * UIScale;
    }
    auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
    auto* Battle = PC ? PC->Battlefield() : nullptr;
    if (!Battle) { Label(TEXT("Preparing battlefield..."), Margin, Margin, White); return; }
    if (Battle->IsMenu())
    {
        QueuePage = 0; QueueProducerId = 0; CompactSheet = 0; CompactSelectionId = 0;
        TutorialCardStep = -1; bTutorialDetails = false;
        DrawMenu(Battle);
        if (PC->IsHelpOpen()) DrawHelp(PC, Battle);
        return;
    }
    const auto* Selected = PC->Selection().empty() ? nullptr : Battle->Sim().find(PC->Selection().front());
    const uint32 SelectionId = Selected ? Selected->id : 0;
    if (CompactSelectionId != SelectionId) { CompactSelectionId = SelectionId; CompactSheet = 0; }
    const uint32 ProducerId = Selected && cinder::definition(Selected->kind).building ? Selected->id : 0;
    if (QueueProducerId != ProducerId) { QueueProducerId = ProducerId; QueuePage = 0; }
    DrawWorldIndicators(PC, Battle);
    DrawTutorialWaypoint(PC, Battle);
    if (bCompactLayout) DrawCompactMatch(PC, Battle); else DrawMatch(PC, Battle);
    if (Battle->Tutorial().IsActive() && !Battle->IsPaused() && Battle->Sim().winner() < 0) DrawTutorialCard(Battle);
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
        Label(TEXT("THE CAIRN ASSEMBLY"), X, 19 * UIScale + SafeTopOffset, Mint, 0.65f);
        Label(TEXT("CINDERLINE"), X - UIScale, 45 * UIScale + SafeTopOffset, White, 2.35f);
        Label(TEXT("Build your foothold. Command the frontier."), X, 88 * UIScale + SafeTopOffset, White, 0.78f);
        const float MapW = (W - 16 * UIScale) / 3;
        for (int I = 0; I < 3; ++I)
            Button(MapNames[I], TEXT("map"), I, X + I * (MapW + 8 * UIScale), 112 * UIScale + SafeTopOffset, MapW, SelectedMap == I);
        SingleLineLabel(MapDescriptions[SelectedMap], X, 162 * UIScale + SafeTopOffset, W, Muted, 0.70f);
        const float OnlineY = FMath::Min(297 * UIScale + SafeTopOffset, Height - SafeBottomOffset - 78 * UIScale);
        const float TrainingY = OnlineY - 51 * UIScale;
        const float SkirmishY = TrainingY - 52 * UIScale;
        Button(TEXT("START SKIRMISH"), TEXT("start"), SelectedMap, X, SkirmishY, W * 0.55f - 5 * UIScale, true);
        Button(TEXT("CONTINUE SAVE"), TEXT("load"), 0, X + W * 0.55f + 5 * UIScale, SkirmishY, W * 0.45f - 5 * UIScale);
        Button(PC && PC->HasCompletedTutorial() ? TEXT("REPLAY TRAINING") : TEXT("GUIDED TRAINING"), TEXT("tutorial"), 0,
            X, TrainingY, W * 0.55f - 5 * UIScale, true);
        Button(TEXT("FIELD GUIDE"), TEXT("help"), 0, X + W * 0.55f + 5 * UIScale, TrainingY, W * 0.45f - 5 * UIScale);
        Button(TEXT("ONLINE 1V1"), TEXT("online"), 0, X, OnlineY, W, true);
#if PLATFORM_IOS || PLATFORM_ANDROID
        SingleLineLabel(TEXT("Drag to pan / Hold + drag to select / Pinch to zoom"), X, Height - SafeBottomOffset - 20 * UIScale, W, Muted, 0.70f);
#else
        SingleLineLabel(DesktopControlHint, X, Height - 20 * UIScale, W, Muted, 0.70f);
#endif
        return;
    }
    const auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
    const float X = 72 * UIScale, W = 540 * UIScale;
    Label(TEXT("THE CAIRN ASSEMBLY"), X, 83 * UIScale, Mint, 0.78f);
    Label(TEXT("CINDERLINE"), X - 4 * UIScale, 122 * UIScale, White, 3.65f);
    Label(TEXT("Build your foothold."), X, 219 * UIScale, White, 1.17f);
    Label(TEXT("Command the frontier."), X, 250 * UIScale, White, 1.17f);
    Label(TEXT("A tactical war for the world's last resources."), X, 296 * UIScale, Muted, 0.84f);
    Panel(X, 350 * UIScale, W, UIScale, FLinearColor(0.10f, 0.17f, 0.19f, 0.8f));
    Label(TEXT("DEPLOYMENT SECTOR"), X, 371 * UIScale, Muted, 0.68f);
    const float MapW = (W - 16 * UIScale) / 3;
    for (int I = 0; I < 3; ++I)
        Button(MapNames[I], TEXT("map"), I, X + I * (MapW + 8 * UIScale), 401 * UIScale, MapW, SelectedMap == I);
    Label(MapDescriptions[SelectedMap], X, 461 * UIScale, Muted, 0.78f);
    Button(TEXT("START SKIRMISH   [ENTER]"), TEXT("start"), SelectedMap, X, 510 * UIScale, 302 * UIScale, true);
    Button(TEXT("CONTINUE SAVE"), TEXT("load"), 0, X + 314 * UIScale, 510 * UIScale, 226 * UIScale);
    Button(PC && PC->HasCompletedTutorial() ? TEXT("REPLAY TRAINING  [T]") : TEXT("GUIDED TRAINING  [T]"), TEXT("tutorial"), 0, X, 566 * UIScale, 302 * UIScale, true);
    Button(TEXT("FIELD GUIDE  [F1]"), TEXT("help"), 0, X + 314 * UIScale, 566 * UIScale, 226 * UIScale);
    Button(TEXT("ONLINE 1V1"), TEXT("online"), 0, X, 622 * UIScale, W, true);
    Label(TEXT("SOLO SKIRMISH"), X, Height - 35 * UIScale, Mint, 0.70f);
    SingleLineLabel(FString(DesktopControlHint) + TEXT(" / Right-click command"), 317 * UIScale, Height - 35 * UIScale,
        Width - 317 * UIScale - X, Muted, 0.70f);

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

void ACinderHUD::DrawTutorialWaypoint(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const FCinderTutorial& Tutorial = Battle->Tutorial();
    if (!Tutorial.IsActive() || Tutorial.IsComplete() || Battle->IsPaused()) return;
    cinder::Vec2 Point;
    FString Caption;
    if (Tutorial.Step() == ECinderTutorialStep::Scout)
    {
        Point = FCinderTutorial::ScoutPoint();
        Caption = TEXT("SCOUT POINT");
    }
    else if (Tutorial.Step() == ECinderTutorialStep::AttackMove)
    {
        Point = FCinderTutorial::CombatPoint();
        Caption = TEXT("ATTACK HERE");
    }
    else return;

    FVector2D Screen;
    if (!PC->ProjectWorldLocationToScreen(FVector(Point.x, Point.y, 26), Screen)) return;
    const float R = 12 * UIScale;
    DrawLine(Screen.X, Screen.Y - R, Screen.X + R, Screen.Y, Amber, 3 * UIScale);
    DrawLine(Screen.X + R, Screen.Y, Screen.X, Screen.Y + R, Amber, 3 * UIScale);
    DrawLine(Screen.X, Screen.Y + R, Screen.X - R, Screen.Y, Amber, 3 * UIScale);
    DrawLine(Screen.X - R, Screen.Y, Screen.X, Screen.Y - R, Amber, 3 * UIScale);
    const float LabelW = FMath::Min(126 * UIScale, static_cast<float>(MeasureLabel(Caption, 0.70f).X) + 16 * UIScale);
    const float LabelX = FMath::Clamp(static_cast<float>(Screen.X) - LabelW * 0.5f, Margin, Width - Margin - LabelW);
    const float LabelY = FMath::Clamp(static_cast<float>(Screen.Y) + 18 * UIScale, 62 * UIScale + SafeTopOffset, Height - 210 * UIScale);
    Panel(LabelX, LabelY, LabelW, 24 * UIScale, Ink);
    SingleLineLabel(Caption, LabelX + 8 * UIScale, LabelY + 4 * UIScale, LabelW - 16 * UIScale, Amber, 0.70f);
}

void ACinderHUD::DrawTutorialCard(ACinderBattlefield* Battle)
{
    const FCinderTutorial& Tutorial = Battle->Tutorial();
    if (!Tutorial.IsActive()) return;
    const FCinderTutorialText Text = Tutorial.Text(Battle->Sim(), PLATFORM_IOS || PLATFORM_ANDROID);
    const float S = UIScale;
    const bool bComplete = Tutorial.IsComplete();
    const int32 Step = static_cast<int32>(Tutorial.Step());
    if (TutorialCardStep != Step)
    {
        TutorialCardStep = Step;
        bTutorialDetails = false;
    }
    const float W = bCompactLayout ? FMath::Min(480 * S, FMath::Max(280 * S, Width - 2 * Margin - 108 * S)) : 408 * S;
    const float X = bCompactLayout ? Margin : Width - Margin - W - 62 * S;
    const float Pad = 14 * S;
    const float BodyScale = bCompactLayout ? 0.70f : 0.80f;
    const float BodyWidth = W - 2 * Pad;
    const bool bShowBody = !bCompactLayout || bComplete || bTutorialDetails;
    const bool bShowHint = !Text.Hint.IsEmpty();
    const float BodyHeight = bShowBody ? WrappedHeight(Text.Body, BodyWidth, BodyScale) : 0;
    const float HintHeight = bShowHint ? WrappedHeight(Text.Hint, BodyWidth, bCompactLayout ? 0.70f : 0.72f) : 0;
    const float ProgressHeight = WrappedHeight(Text.Progress, BodyWidth, bCompactLayout ? 0.66f : 0.68f);
    if (bCompactLayout && !bComplete)
    {
        const float HeaderHeight = 48 * S;
        const float TextGaps = (bShowBody && bShowHint ? 2 * S : 0) + 4 * S;
        const float H = bTutorialDetails ? HeaderHeight + BodyHeight + HintHeight + TextGaps + ProgressHeight + 6 * S : HeaderHeight;
        const float Y = 65 * S + SafeTopOffset;
        Panel(X, Y, W, H, FLinearColor(0.004f, 0.018f, 0.026f, 0.97f));
        Panel(X, Y, 4 * S, H, Amber);
        UIRegions.Add(FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)));

        const float ActionW = 52 * S;
        const float ActionGap = 4 * S;
        const float ActionsW = bTutorialDetails ? ActionW * 3 + ActionGap * 2 : ActionW * 2 + ActionGap;
        const float ActionsX = X + W - Pad - ActionsW;
        SingleLineLabel(FString::Printf(TEXT("%d/%d  %s"), Step + 1, FCinderTutorial::StepCount, *Text.Title),
            X + Pad, Y + 6 * S, ActionsX - X - Pad - 7 * S, Amber, 0.78f);
        SingleLineLabel(Text.Hint, X + Pad, Y + 26 * S, ActionsX - X - Pad - 7 * S, White, 0.70f);
        Button(TEXT("FIND"), TEXT("tutorialfocus"), 0, ActionsX, Y, ActionW);
        if (bTutorialDetails) Button(TEXT("HELP"), TEXT("tutorialhelp"), 0, ActionsX + ActionW + ActionGap, Y, ActionW);
        Button(bTutorialDetails ? TEXT("LESS") : TEXT("MORE"), TEXT("tutorialdetails"), 0,
            ActionsX + (ActionW + ActionGap) * (bTutorialDetails ? 2 : 1), Y, ActionW, bTutorialDetails);
        if (!bTutorialDetails) return;

        float TextY = Y + HeaderHeight;
        if (bShowBody)
        {
            TextY += WrappedLabel(Text.Body, X + Pad, TextY, BodyWidth, White, BodyScale);
            if (bShowHint) TextY += 2 * S;
        }
        if (bShowHint) TextY += WrappedLabel(Text.Hint, X + Pad, TextY, BodyWidth, White, 0.70f);
        TextY += 4 * S;
        WrappedLabel(Text.Progress, X + Pad, TextY, BodyWidth, Muted, 0.66f);
        return;
    }

    const float HeaderHeight = bCompactLayout ? 32 * S : 57 * S;
    const float TextGaps = (bShowBody && bShowHint ? 2 * S : 0) + 4 * S;
    const float H = HeaderHeight + BodyHeight + HintHeight + TextGaps + ProgressHeight + 6 * S + 44 * S;
    const float CompactBottom = MobileLayout.Commands.Min.Y - 58 * S;
    const float Y = bCompactLayout ? CompactBottom - H : 78 * S;
    Panel(X, Y, W, H, FLinearColor(0.004f, 0.018f, 0.026f, 0.97f));
    Panel(X, Y, 4 * S, H, bComplete ? Mint : Amber);
    UIRegions.Add(FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)));

    if (bCompactLayout)
    {
        if (bComplete) Label(TEXT("TRAINING COMPLETE"), X + Pad, Y + 10 * S, Mint, 0.68f);
        else
        {
            Label(FString::Printf(TEXT("STEP %d / %d"), Step + 1, FCinderTutorial::StepCount), X + Pad, Y + 10 * S, Amber, 0.68f);
            SingleLineLabel(Text.Title, X + Pad + 92 * S, Y + 9 * S, BodyWidth - 92 * S, White, 0.90f);
        }
    }
    else if (bComplete)
        Label(TEXT("TRAINING COMPLETE"), X + Pad, Y + 12 * S, Mint, 0.72f);
    else
        Label(FString::Printf(TEXT("STEP %d / %d"), Step + 1, FCinderTutorial::StepCount), X + Pad, Y + 12 * S, Amber, 0.72f);
    if (!bCompactLayout) SingleLineLabel(Text.Title, X + Pad, Y + 32 * S, BodyWidth, White, 1.15f);

    float TextY = Y + HeaderHeight;
    if (bShowBody)
    {
        TextY += WrappedLabel(Text.Body, X + Pad, TextY, BodyWidth, White, BodyScale);
        if (bShowHint) TextY += 2 * S;
    }
    if (bShowHint) TextY += WrappedLabel(Text.Hint, X + Pad, TextY, BodyWidth, bCompactLayout ? White : Muted, bCompactLayout ? 0.70f : 0.72f);
    TextY += 4 * S;
    WrappedLabel(Text.Progress, X + Pad, TextY, BodyWidth, bComplete ? Mint : Muted, bCompactLayout ? 0.66f : 0.68f);

    const float ButtonY = Y + H - 44 * S;
    if (bComplete)
    {
        const float ActionW = (W - 2 * Pad - 7 * S) * 0.5f;
        Panel(X + Pad, ButtonY, ActionW, 44 * S, FLinearColor(0.018f, 0.11f, 0.105f));
        Panel(X + Pad, ButtonY, 3 * S, 44 * S, Mint);
        Label(TEXT("PRACTICE ON"), X + Pad + 12 * S, ButtonY + 12 * S, Mint, 0.80f);
        Button(TEXT("END TRAINING"), TEXT("tutorialend"), 0, X + Pad + ActionW + 7 * S, ButtonY, ActionW);
    }
    else
    {
        const int32 Columns = bCompactLayout ? 3 : 2;
        const float ActionW = (W - 2 * Pad - (Columns - 1) * 7 * S) / Columns;
        Button(TEXT("FIND"), TEXT("tutorialfocus"), 0, X + Pad, ButtonY, ActionW);
        Button(TEXT("HELP"), TEXT("tutorialhelp"), 0, X + Pad + ActionW + 7 * S, ButtonY, ActionW);
        if (bCompactLayout)
            Button(bTutorialDetails ? TEXT("LESS") : TEXT("MORE"), TEXT("tutorialdetails"), 0,
                X + Pad + (ActionW + 7 * S) * 2, ButtonY, ActionW, bTutorialDetails);
    }
}

void ACinderHUD::DrawMinimap(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const float Size = (bCompactLayout ? 92 : 155) * UIScale;
    const FVector2D Origin(bCompactLayout ? Width - Margin - Size : Margin, bCompactLayout ? 76 * UIScale + SafeTopOffset : Height - Size - 30 * UIScale);
    Minimap = FBox2D(Origin, Origin + FVector2D(Size));
    UIRegions.Add(Minimap);
    Panel(Origin.X - 5 * UIScale, Origin.Y - 5 * UIScale, Size + 10 * UIScale, Size + 10 * UIScale, Ink);
    Panel(Origin.X - 3, Origin.Y - 3, Size + 6, Size + 6, Muted);
    const float Cell = Size / cinder::Simulation::FogSize;
    const float CellExtent = Cell + 0.3f;
    const FLinearColor FogColors[] = {
        FLinearColor(0.014f, 0.025f, 0.04f),
        FLinearColor(0.065f, 0.12f, 0.14f),
        FLinearColor(0.11f, 0.25f, 0.25f)
    };
    auto FogState = [&](int X, int Y)
    {
        const cinder::Vec2 P{ (X + 0.5f) * 75, (Y + 0.5f) * 75 };
        return Battle->Sim().visible(0, P) ? 2 : Battle->Sim().explored(0, P) ? 1 : 0;
    };
    for (int Y = 0; Y < cinder::Simulation::FogSize; ++Y)
    {
        int RunStart = 0, RunState = FogState(0, Y);
        for (int X = 1; X <= cinder::Simulation::FogSize; ++X)
        {
            const int State = X < cinder::Simulation::FogSize ? FogState(X, Y) : -1;
            if (State == RunState) continue;
            // Opaque equal-color cells have the same union as one row run.
            // Keep the original last-cell edge and row order at color boundaries.
            const float Left = Origin.X + RunStart * Cell;
            const float LastCellLeft = Origin.X + (X - 1) * Cell;
            const float Right = LastCellLeft + CellExtent;
            Panel(Left, Origin.Y + Y * Cell, Right - Left, CellExtent, FogColors[RunState]);
            RunStart = X;
            RunState = State;
        }
    }
    const float K = Size / cinder::Simulation::WorldSize;
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
        Panel(Origin.X + E.pos.x * K - Dot * 0.5f, Origin.Y + E.pos.y * K - Dot * 0.5f, Dot, Dot, Resource ? Amber : E.team == 0 ? Mint : FLinearColor(1.0f, 0.29f, 0.21f));
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
    const float MapBracket = 11 * UIScale;
    Panel(Origin.X - 3 * UIScale, Origin.Y - 3 * UIScale, MapBracket, 2 * UIScale, Mint);
    Panel(Origin.X - 3 * UIScale, Origin.Y - 3 * UIScale, 2 * UIScale, MapBracket, Mint);
    Panel(Origin.X + Size + 3 * UIScale - MapBracket, Origin.Y - 3 * UIScale, MapBracket, 2 * UIScale, Amber);
    Panel(Origin.X + Size + UIScale, Origin.Y - 3 * UIScale, 2 * UIScale, MapBracket, Amber);
    Panel(Origin.X - 3 * UIScale, Origin.Y + Size + UIScale, MapBracket, 2 * UIScale, FLinearColor(0.06f, 0.34f, 0.31f));
    Panel(Origin.X - 3 * UIScale, Origin.Y + Size + 3 * UIScale - MapBracket, 2 * UIScale, MapBracket, FLinearColor(0.06f, 0.34f, 0.31f));
    if (!bCompactLayout) Label(TEXT("SECTOR MAP"), Origin.X, Origin.Y - 23 * UIScale, Muted, 0.7f);
}

void ACinderHUD::DrawWorldIndicators(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const auto& Sim = Battle->Sim();
    auto WorldLine = [&](FVector A, FVector B, FLinearColor C, float Thickness)
    {
        FVector2D SA, SB;
        if (PC->ProjectWorldLocationToScreen(A, SA) && PC->ProjectWorldLocationToScreen(B, SB)) DrawLine(SA.X, SA.Y, SB.X, SB.Y, C, Thickness * UIScale);
    };
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
                Panel(P.X - BarW / 2, P.Y, BarW * FMath::Clamp(E.hp / D.hp, 0.0f, 1.0f), 4 * UIScale, E.team == 0 ? Mint : Amber);
                if (E.progress < 1) Panel(P.X - BarW / 2, P.Y + 6 * UIScale, BarW * E.progress, 3 * UIScale, Amber);
                if (E.team == 0 && D.building && E.progress < 1)
                    Label(ConstructionStatus(Sim, E), P.X - BarW / 2, P.Y - 19 * UIScale, Amber, 0.62f);
            }
        }
    }
    if (!Battle->HasWorldEffects())
    {
        // Local effect geometry must not spill into a hidden fog cell. Link geometry
        // uses the simulation's recorded-endpoint and complete-segment visibility gate.
        const float EffectScale = bCompactLayout ? 0.72f : 1.0f;
        const float FogCell = cinder::Simulation::WorldSize / cinder::Simulation::FogSize;
        auto EffectAreaVisible = [&](FVector Center, float Radius)
        {
            if (Center.X - Radius < 0 || Center.Y - Radius < 0 ||
                Center.X + Radius >= cinder::Simulation::WorldSize || Center.Y + Radius >= cinder::Simulation::WorldSize) return false;
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
            const FLinearColor Shot = FX.team == 0 ? Mint : Amber;
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
        const auto P = PC->PlacementPoint(); const float R = cinder::definition(PC->BuildingKind()).radius;
        const auto Status = Sim.buildStatus(0, PC->BuildingKind(), PC->Selection(), &P);
        const bool Valid = Status.accepted;
        const FLinearColor Color = Valid ? Mint : FLinearColor(1, 0.24f, 0.18f);
        for (int I = 0; I < 32; ++I)
        {
            const float A = I * UE_TWO_PI / 32, B = (I + 1) * UE_TWO_PI / 32;
            WorldLine(FVector(P.x + FMath::Cos(A) * R, P.y + FMath::Sin(A) * R, 8), FVector(P.x + FMath::Cos(B) * R, P.y + FMath::Sin(B) * R, 8), Color, 3);
        }
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
    SingleLineLabel(FString::Printf(TEXT("QUEUE %d / %d"), Count, cinder::Simulation::MaxQueue), X + 5 * S, Y + 3 * S, W - 111 * S, Amber, 0.80f);
#if PLATFORM_IOS || PLATFORM_ANDROID
    const TCHAR* CancelHint = TEXT("TAP TO CANCEL");
#else
    const TCHAR* CancelHint = TEXT("CLICK TO CANCEL");
#endif
    const FString Range = Count ? FString::Printf(TEXT("%d-%d / %s"), Start + 1, End, CancelHint) : TEXT("NO PRODUCTION QUEUED");
    SingleLineLabel(Range, X + 5 * S, Y + 25 * S, W - 111 * S, Muted, 0.70f);
    if (QueuePage > 0) Button(TEXT("<"), TEXT("queuepage"), -1, X + W - 98 * S, Y, 44 * S);
    if (QueuePage + 1 < Pages) Button(TEXT(">"), TEXT("queuepage"), 1, X + W - 44 * S, Y, 44 * S);
    const float CellW = (W - (Columns - 1) * 7 * S) / Columns;
    for (int I = Start; I < End; ++I)
    {
        const auto& Q = Producer->queue[I];
        const int Cell = I - Start;
        const float BX = X + (Cell % Columns) * (CellW + 7 * S), BY = Y + (52 + (Cell / Columns) * 51) * S;
        const FString QName = Q.research ? TEXT("Research") : Name(Q.kind);
        Button(FString::Printf(TEXT("%d %s %ds x"), I + 1, *QName, FMath::CeilToInt(Q.remaining)), TEXT("cancelqueue"), I, BX, BY, CellW);
        Panel(BX, BY + 41 * S, CellW * FMath::Clamp(1 - Q.remaining / FMath::Max(Q.total, 0.01f), 0.0f, 1.0f), 2 * S, Mint);
    }
}

bool ACinderHUD::GetOnlineNotice(ACinderPlayerController* PC, ACinderBattlefield* Battle, FString& Heading, FString& Detail) const
{
    if (!PC || !Battle || !Battle->IsOnlineMatch()) return false;
    if (Battle->IsPaused() || Battle->Sim().winner() >= 0 || PC->IsHelpOpen() || PC->IsOnlineLeavePending()) return false;
    const UCinderOnlineSubsystem* Online = PC->Online();
    if (!Online || Online->State() == ECinderOnlineState::Finished) return false;

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
    const int32 OtherSeat = LocalSeat == 0 ? 1 : LocalSeat == 1 ? 0 : INDEX_NONE;
    if (Online->State() == ECinderOnlineState::Playing && OtherSeat != INDEX_NONE && !Online->SeatConnected(OtherSeat))
    {
        Heading = TEXT("OPPONENT DISCONNECTED");
        Detail = Status.IsEmpty() ? TEXT("The match continues while the other player reconnects.") : Status;
        Detail += TEXT("  Open Menu > Connection.");
        return true;
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
    if (CompactSheet == 3 && (!bBuilding || bSite)) CompactSheet = 0;
    const bool bSheetVisible = !PC->IsBuildMode() && (PC->bBuildMenu || CompactSheet != 0);

    // Resources share a slim top rail; tap targets retain their full 44-unit height.
    Panel(L.Left, Top, InnerW, 44 * S, Ink);
    Panel(L.Left, Top, InnerW, S, FLinearColor(0.07f, 0.31f, 0.30f));
    Panel(L.Left, Top + 32 * S, InnerW, 12 * S, FLinearColor(0.002f, 0.009f, 0.014f, 0.72f));
    Panel(L.Left, Top, 76 * S, 2 * S, Amber);
    Panel(L.Left + 99 * S, Top + 11 * S, S, 22 * S, FLinearColor(0.07f, 0.25f, 0.25f));
    Panel(L.Left + 214 * S, Top + 11 * S, S, 22 * S, FLinearColor(0.07f, 0.25f, 0.25f));
    Panel(L.Right - 184 * S, Top + 11 * S, S, 22 * S, FLinearColor(0.07f, 0.25f, 0.25f));
    UIRegions.Add(FBox2D(FVector2D(L.Left, Top), FVector2D(L.Right, Top + 44 * S)));
    Label(FString::Printf(TEXT("ORE %d"), Player.ore), L.Left + 10 * S, Top + 13 * S, Amber, 0.86f);
    Label(FString::Printf(TEXT("CREW %d/%d"), Sim.supply(0), Sim.capacity(0)), L.Left + 109 * S, Top + 13 * S, White, 0.77f);
    Label(FString::Printf(TEXT("T%d W%d A%d"), Player.tier, Player.weapons, Player.armor), L.Left + 224 * S, Top + 13 * S, Mint, 0.72f);
    const FString Time = bOnline ? Online && Online->PingMilliseconds() >= 0
        ? FString::Printf(TEXT("%.0f MS"), Online->PingMilliseconds()) : TEXT("ONLINE") : ClockString(Sim.time());
    SingleLineLabel(Time, L.Right - 171 * S, Top + 14 * S, 58 * S, Muted, 0.72f);
    Button(TEXT("?"), TEXT("help"), 0, L.Right - 106 * S, Top, 44 * S);
    Button(TEXT("MENU"), TEXT("pause"), 0, L.Right - 56 * S, Top, 56 * S);
    if (!bSheetVisible && !PC->IsBuildMode()) DrawMinimap(PC, Battle);

    // Only the corner islands consume input. The gap and the space above them
    // are real battlefield: no transparent full-width blocker remains.
    UIRegions.Add(L.Navigation);
    Button(TEXT("ARMY"), TEXT("army"), 0, L.Left, DockY, 60 * S);
    Button(TEXT("SELECT"), TEXT("box"), 0, L.Left + 66 * S, DockY, 64 * S, PC->bBoxSelect);
    Button(TEXT("HOME"), TEXT("home"), 0, L.Left + 136 * S, DockY, 56 * S);

    if (First && !bSheetVisible && !PC->IsBuildMode())
    {
        FString Summary = PC->Selection().size() > 1
            ? FString::Printf(TEXT("%d UNITS / FOCUS"), static_cast<int>(PC->Selection().size()))
            : FString::Printf(TEXT("%s / HP %d"), *Name(First->kind), static_cast<int>(First->hp));
        if (bSite) Summary = Name(First->kind) + TEXT(" / ") + ConstructionStatus(Sim, *First);
        else if (First->order == cinder::Order::Construct)
            Summary = Sim.constructionActive(First->target) ? TEXT("DRUDGE / BUILDING") : TEXT("DRUDGE / TO SITE");
        Button(Summary, TEXT("focus"), 0, L.Left, DockY - 50 * S, 192 * S);
    }

    if (PC->IsBuildMode())
    {
        // Cancel sits under the command thumb, leaving the placement target clear.
        Button(TEXT("CANCEL PLACEMENT"), TEXT("cancelplacement"), 0, L.Commands.Min.X, DockY, 224 * S, true);
    }
    else
    {
        UIRegions.Add(L.Commands);
        const float X = L.Commands.Min.X;
        Button(bBuilding ? bSite ? TEXT("SITE") : First->kind == cinder::Kind::Laboratory ? TEXT("TECH") : TEXT("TRAIN")
                         : !First || bWorker ? TEXT("BUILD") : TEXT("ATTACK"),
            bBuilding ? TEXT("sheet") : !First || bWorker ? TEXT("buildmenu") : TEXT("attack"),
            bBuilding ? 1 : 0, X, DockY, 80 * S,
            bBuilding ? CompactSheet == 1 && !PC->bBuildMenu : !First || bWorker ? PC->bBuildMenu : PC->bAttackMove);
        if (bBuilding && !bSite)
            Button(FString::Printf(TEXT("QUEUE %d"), static_cast<int>(First->queue.size())), TEXT("sheet"), 3, X + 86 * S, DockY, 76 * S, CompactSheet == 3);
        else if (First) Button(TEXT("STOP"), TEXT("stop"), 0, X + 86 * S, DockY, 76 * S);
        else Button(TEXT("DRUDGES"), TEXT("workers"), 0, X + 86 * S, DockY, 76 * S);
        Button(bSheetVisible ? TEXT("CLOSE") : TEXT("MORE"), bSheetVisible ? TEXT("closesheet") : TEXT("sheet"), 4,
            X + 168 * S, DockY, 56 * S, bSheetVisible);
    }

    if (!PC->IsBuildMode() && !PC->bBuildMenu && CompactSheet == 3 && bBuilding && !bSite)
    {
        const float SheetW = FMath::Min(InnerW - 10 * S, 430 * S);
        const float X = L.Right - SheetW - 5 * S;
        const int Count = static_cast<int>(First->queue.size());
        const int Pages = FMath::Max(1, FMath::DivideAndRoundUp(Count, 6));
        QueuePage = FMath::Clamp(QueuePage, 0, Pages - 1);
        const int Rows = FMath::Max(1, FMath::DivideAndRoundUp(FMath::Min(6, Count - QueuePage * 6), 3));
        DrawProductionQueue(First, X, DockY - (64 + Rows * 51) * S, SheetW, 3);
    }
    else if (!PC->IsBuildMode() && (PC->bBuildMenu || CompactSheet != 0))
    {
        struct FOption { FString Text, Action; int Arg; };
        TArray<FOption> Options;
        FString Title;
        if (PC->bBuildMenu)
        {
            Title = TEXT("BUILD / ORE COST");
            for (auto K : { cinder::Kind::Headquarters, cinder::Kind::Processor, cinder::Kind::Foundry, cinder::Kind::MotorPool, cinder::Kind::Laboratory, cinder::Kind::Turret })
                Options.Add({ FString::Printf(TEXT("%s / %d"), *Name(K), cinder::definition(K).cost), TEXT("build"), static_cast<int>(K) });
        }
        else if (CompactSheet == 2)
        {
            Title = TEXT("SELECT AN ARMY SUBGROUP");
            std::map<cinder::Kind, int> Counts;
            for (auto Id : PC->Selection()) if (const auto* E = Sim.find(Id)) ++Counts[E->kind];
            for (const auto& Entry : Counts) Options.Add({ FString::Printf(TEXT("%s x%d"), *Name(Entry.first), Entry.second), TEXT("kind"), static_cast<int>(Entry.first) });
        }
        else if (CompactSheet == 4)
        {
            Title = TEXT("MORE COMMANDS");
            if (First)
            {
                Options.Add({ TEXT("FOCUS SELECTION"), TEXT("focus"), 0 });
                if (!bBuilding) Options.Add({ TEXT("HOLD POSITION"), TEXT("hold"), 0 });
                if (PC->Selection().size() > 1) Options.Add({ TEXT("UNIT TYPES"), TEXT("sheet"), 2 });
            }
            Options.Add({ TEXT("SELECT DRUDGES"), TEXT("workers"), 0 });
            Options.Add({ TEXT("BUILD STRUCTURE"), TEXT("buildmenu"), 0 });
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
                for (const auto& D : cinder::definitions())
                    if (!D.building && D.kind != cinder::Kind::Resource && D.producer == First->kind)
                        Options.Add({ FString::Printf(TEXT("%s / %d"), *Name(D.kind), D.cost), TEXT("train"), static_cast<int>(D.kind) });
                if (First->kind == cinder::Kind::Laboratory)
                {
                    Options.Add({ TEXT("TECH TIER"), TEXT("research"), 0 });
                    Options.Add({ TEXT("WEAPONS"), TEXT("research"), 1 });
                    Options.Add({ TEXT("ARMOR"), TEXT("research"), 2 });
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
        const float SheetW = FMath::Min(InnerW - 10 * S, 430 * S);
        const float SheetH = (52 + Rows * 50) * S;
        const float X = L.Right - SheetW - 5 * S, Y = DockY - SheetH - 8 * S;
        Panel(X - 5 * S, Y, SheetW + 10 * S, SheetH, Ink);
        UIRegions.Add(FBox2D(FVector2D(X - 5 * S, Y), FVector2D(X + SheetW + 5 * S, Y + SheetH)));
        SingleLineLabel(Title, X + 8 * S, Y + 16 * S, SheetW - 70 * S, Amber, 0.74f);
        Button(TEXT("X"), TEXT("closesheet"), 0, X + SheetW - 44 * S, Y + 2 * S, 44 * S);
        const float CellW = (SheetW - (Columns - 1) * 7 * S) / Columns;
        for (int I = 0; I < Options.Num(); ++I)
        {
            const float BX = X + (I % Columns) * (CellW + 7 * S), BY = Y + (52 + (I / Columns) * 50) * S;
            Button(Options[I].Text, Options[I].Action, Options[I].Arg, BX, BY, CellW);
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
    if (!bOnlineNotice && !PC->Feedback().IsEmpty())
    {
        if (bSheetVisible) SingleLineLabel(PC->Feedback(), Margin, 62 * S + SafeTopOffset, InnerW, Amber, 0.70f);
        else WrappedLabel(PC->Feedback(), Margin, 72 * S + SafeTopOffset, InnerW - 108 * S, Amber, 0.70f);
    }
    else if (!bOnlineNotice && !Battle->Tutorial().IsActive() && Sim.time() < 15 && CompactSheet == 0 && !PC->bBuildMenu && !PC->IsBuildMode())
    {
#if PLATFORM_IOS || PLATFORM_ANDROID
        WrappedLabel(TEXT("Tap to select or command. Drag to pan. Hold + drag to select an army."), Margin, 72 * S + SafeTopOffset, InnerW - 108 * S, White, 0.76f);
#else
        if (!First) SingleLineLabel(DesktopControlHint, Margin, 72 * S, InnerW - 108 * S, White, 0.70f);
        else WrappedLabel(TEXT("Select a Drudge. Build a Kiln. Train an army."), Margin, 72 * S, InnerW - 108 * S, White, 0.76f);
#endif
    }
    if (PC->bDebug && !bOnlineNotice && !bSheetVisible && !PC->IsBuildMode())
        Label(FString::Printf(TEXT("%.2f ms sim / %d entities / %.0f fps"), Sim.lastStepMilliseconds(), static_cast<int>(Sim.entities().size()), 1.0f / FMath::Max(GetWorld()->GetDeltaSeconds(), 0.001f)), Margin, 134 * S + SafeTopOffset, Mint, 0.72f);
    if (bOnlineNotice) DrawOnlineNotice(OnlineHeading, OnlineDetail, Margin, 62 * S + SafeTopOffset, InnerW, true);
    DrawOverlay(PC, Battle);
}

void ACinderHUD::DrawMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const auto& Sim = Battle->Sim(); const auto& Player = Sim.players()[0];
    const bool bOnline = Battle->IsOnlineMatch();
    const UCinderOnlineSubsystem* Online = bOnline ? PC->Online() : nullptr;
    FString OnlineHeading, OnlineDetail;
    const bool bOnlineNotice = GetOnlineNotice(PC, Battle, OnlineHeading, OnlineDetail);
    const float Top = 18 * UIScale, Bottom = Height - 126 * UIScale;
    Panel(Margin - 8 * UIScale, Top - 6 * UIScale, 520 * UIScale, 47 * UIScale, Ink);
    Panel(Margin - 8 * UIScale, Top - 6 * UIScale, 520 * UIScale, UIScale, FLinearColor(0.07f, 0.31f, 0.30f));
    Panel(Margin - 8 * UIScale, Top + 29 * UIScale, 520 * UIScale, 12 * UIScale, FLinearColor(0.002f, 0.009f, 0.014f, 0.72f));
    Panel(Margin - 8 * UIScale, Top - 6 * UIScale, 96 * UIScale, 2 * UIScale, Amber);
    Panel(Margin + 140 * UIScale, Top + 3 * UIScale, UIScale, 25 * UIScale, FLinearColor(0.07f, 0.25f, 0.25f));
    Panel(Margin + 285 * UIScale, Top + 3 * UIScale, UIScale, 25 * UIScale, FLinearColor(0.07f, 0.25f, 0.25f));
    Panel(Margin + 425 * UIScale, Top + 3 * UIScale, UIScale, 25 * UIScale, FLinearColor(0.07f, 0.25f, 0.25f));
    UIRegions.Add(FBox2D(FVector2D(Margin - 8 * UIScale, Top - 6 * UIScale), FVector2D(Margin + 512 * UIScale, Top + 41 * UIScale)));
    Label(FString::Printf(TEXT("ORE  %d"), Player.ore), Margin + 5 * UIScale, Top + 7 * UIScale, Amber, 1.05f);
    Label(FString::Printf(TEXT("CREW  %d / %d"), Sim.supply(0), Sim.capacity(0)), Margin + 155 * UIScale, Top + 9 * UIScale, White, 0.85f);
    Label(FString::Printf(TEXT("T%d  W%d  A%d"), Player.tier, Player.weapons, Player.armor), Margin + 300 * UIScale, Top + 9 * UIScale, Mint, 0.85f);
    Label(ClockString(Sim.time()), Margin + 436 * UIScale, Top + 9 * UIScale, Muted, 0.85f);
    if (bOnline)
    {
        const FString Network = Online && Online->PingMilliseconds() >= 0
            ? FString::Printf(TEXT("ONLINE  |  %.0f MS"), Online->PingMilliseconds()) : TEXT("ONLINE  |  -- MS");
        SingleLineLabel(Network, Width - Margin - 390 * UIScale, Top + 9 * UIScale, 168 * UIScale, Mint, 0.78f);
    }
    Button(TEXT("HELP  [F1]"), TEXT("help"), 0, Width - Margin - 210 * UIScale, Top - 5 * UIScale, 112 * UIScale);
    Button(bOnline ? TEXT("MENU") : TEXT("PAUSE"), TEXT("pause"), 0, Width - Margin - 90 * UIScale, Top - 5 * UIScale, 90 * UIScale);
    Button(TEXT("+"), TEXT("zoom+"), 0, Width - Margin - 50 * UIScale, Top + 68 * UIScale, 50 * UIScale);
    Button(TEXT("-"), TEXT("zoom-"), 0, Width - Margin - 50 * UIScale, Top + 119 * UIScale, 50 * UIScale);
    if (!bOnlineNotice && !Battle->Tutorial().IsActive() && Sim.time() < 180)
    {
        const FString Hint = Player.stats.built == 0 ? TEXT("OPENING  Build a Kiln, train Ember infantry, and scout beyond your perimeter.") : Player.stats.produced < 4 ? TEXT("REINFORCE  Select your production structure to queue units. Tap terrain to set its rally.") : TEXT("SCOUT  The red Anchor is your objective. Keep harvesting and expand before your ore runs out.");
        Label(Hint, Margin, Top + 54 * UIScale, White, 0.76f);
    }
    Panel(0, Height - 196 * UIScale, Width, 196 * UIScale, Ink);
    Panel(0, Height - 196 * UIScale, Width, UIScale, FLinearColor(0.07f, 0.31f, 0.30f));
    Panel(0, Height - 196 * UIScale, 116 * UIScale, 2 * UIScale, Amber);
    UIRegions.Add(FBox2D(FVector2D(0, Height - 196 * UIScale), FVector2D(Width, Height)));
    DrawMinimap(PC, Battle);
    const float SX = Margin + 180 * UIScale;
    const float CommandsX = Width - Margin - 488 * UIScale;
    const float ButtonW = 114 * UIScale, Gap = 124 * UIScale;
    Button(TEXT("ARMY"), TEXT("army"), 0, CommandsX, Bottom, ButtonW);
    Button(TEXT("SELECT BOX"), TEXT("box"), 0, CommandsX + Gap, Bottom, ButtonW, PC->bBoxSelect);
    Button(TEXT("HOME"), TEXT("home"), 0, CommandsX + Gap * 2, Bottom, ButtonW);
    Button(TEXT("FOCUS"), TEXT("focus"), 0, CommandsX + Gap * 3, Bottom, ButtonW);
    Button(TEXT("ATTACK MOVE"), TEXT("attack"), 0, CommandsX, Bottom + 53 * UIScale, ButtonW, PC->bAttackMove);
    Button(TEXT("STOP"), TEXT("stop"), 0, CommandsX + Gap, Bottom + 53 * UIScale, ButtonW);
    Button(TEXT("HOLD"), TEXT("hold"), 0, CommandsX + Gap * 2, Bottom + 53 * UIScale, ButtonW);
    Button(PC->IsBuildMode() ? TEXT("CANCEL") : TEXT("BUILD"), PC->IsBuildMode() ? TEXT("cancelplacement") : TEXT("buildmenu"), 0, CommandsX + Gap * 3, Bottom + 53 * UIScale, ButtonW, PC->bBuildMenu);

    const cinder::Entity* First = PC->Selection().empty() ? nullptr : Sim.find(PC->Selection().front());
    if (!First)
    {
        Label(TEXT("YOUR FRONTIER"), SX, Height - 174 * UIScale, Mint, 0.88f);
#if PLATFORM_IOS || PLATFORM_ANDROID
        Label(TEXT("Tap a unit or structure."), SX, Height - 140 * UIScale, White, 0.78f);
        Label(TEXT("Hold, then drag to select."), SX, Height - 115 * UIScale, Muted, 0.78f);
        Label(TEXT("Two fingers pan and zoom."), SX, Height - 90 * UIScale, Muted, 0.78f);
#else
        SingleLineLabel(TEXT("Click a unit. Drag to select."), SX, Height - 140 * UIScale, 240 * UIScale, White, 0.78f);
#if PLATFORM_MAC
        SingleLineLabel(TEXT("Option+drag to pan."), SX, Height - 115 * UIScale, 240 * UIScale, Muted, 0.78f);
#else
        SingleLineLabel(TEXT("Alt+drag to pan."), SX, Height - 115 * UIScale, 240 * UIScale, Muted, 0.78f);
#endif
        SingleLineLabel(TEXT("Scroll to zoom."), SX, Height - 90 * UIScale, 240 * UIScale, Muted, 0.78f);
#endif
    }
    else
    {
        const auto& Def = cinder::definition(First->kind);
        Label(PC->Selection().size() == 1 ? Name(First->kind).ToUpper() : FString::Printf(TEXT("%d UNITS SELECTED"), static_cast<int>(PC->Selection().size())), SX, Height - 174 * UIScale, Mint, 0.92f);
        FString Detail = FString::Printf(TEXT("HP %d / %d"), static_cast<int>(First->hp), static_cast<int>(Def.hp));
        if (Def.building && First->progress < 1) Detail = ConstructionStatus(Sim, *First);
        else if (First->order == cinder::Order::Construct) Detail = Sim.constructionActive(First->target) ? TEXT("BUILDING / MINING PAUSED") : TEXT("TO SITE / MINING PAUSED");
        SingleLineLabel(Detail, SX, Height - 144 * UIScale, 240 * UIScale, White, 0.78f);
        std::map<cinder::Kind, int> Counts;
        for (auto Id : PC->Selection()) if (const auto* E = Sim.find(Id)) ++Counts[E->kind];
        const int Pages = FMath::Max(1, (static_cast<int>(Counts.size()) + 1) / 2);
        SubgroupPage %= Pages;
        if (Pages > 1) Button(TEXT("NEXT TYPES >"), TEXT("groupsnext"), 0, SX + 132 * UIScale, Height - 157 * UIScale, 108 * UIScale);
        int Row = 0;
        int EntryIndex = 0;
        for (const auto& Entry : Counts)
        {
            if (EntryIndex++ < SubgroupPage * 2) continue;
            if (Row >= 2) break;
            Button(FString::Printf(TEXT("%s x%d"), *Name(Entry.first), Entry.second), TEXT("kind"), static_cast<int>(Entry.first), SX, Height - (112 - Row * 47) * UIScale, 240 * UIScale);
            ++Row;
        }
        int ActionIndex = 0;
        const float ContextY = Height - 185 * UIScale;
        if (First->progress < 1)
        {
            if (!Sim.constructionWorker(First->id)) Button(TEXT("ASSIGN DRUDGE"), TEXT("resumeconstruction"), 0, CommandsX, ContextY, 238 * UIScale);
            else Label(Sim.constructionActive(First->id) ? TEXT("DRUDGE CONSTRUCTING") : TEXT("DRUDGE EN ROUTE"), CommandsX, ContextY + 12 * UIScale, Amber, 0.80f);
            Button(TEXT("CANCEL BUILDING"), TEXT("cancelbuilding"), 0, CommandsX + 2 * Gap, ContextY, 238 * UIScale);
        }
        else if (Def.building && !PC->bBuildMenu)
        {
            for (const auto& Unit : cinder::definitions())
            {
                if (Unit.building || Unit.kind == cinder::Kind::Resource || Unit.producer != First->kind) continue;
                Button(FString::Printf(TEXT("%s  %d"), *Name(Unit.kind), Unit.cost), TEXT("train"), static_cast<int>(Unit.kind), CommandsX + ActionIndex * Gap, ContextY, ButtonW);
                if (++ActionIndex >= 4) break;
            }
            if (First->kind == cinder::Kind::Laboratory)
            {
                Button(TEXT("TECH TIER"), TEXT("research"), 0, CommandsX + Gap, ContextY, ButtonW);
                Button(TEXT("WEAPONS"), TEXT("research"), 1, CommandsX + Gap * 2, ContextY, ButtonW);
                Button(TEXT("ARMOR"), TEXT("research"), 2, CommandsX + Gap * 3, ContextY, ButtonW);
            }
            if (!First->queue.empty())
            {
                DrawProductionQueue(First, CommandsX, Height - 355 * UIScale, 488 * UIScale, 3);
            }
        }
    }
    if (PC->bBuildMenu)
    {
        const cinder::Kind Buildings[] = { cinder::Kind::Headquarters, cinder::Kind::Processor, cinder::Kind::Foundry, cinder::Kind::MotorPool, cinder::Kind::Laboratory, cinder::Kind::Turret };
        const float X = CommandsX, Y = Height - 297 * UIScale, W = 156 * UIScale;
        Panel(X - 8 * UIScale, Y - 34 * UIScale, 504 * UIScale, 145 * UIScale, Ink);
        UIRegions.Add(FBox2D(FVector2D(X - 8 * UIScale, Y - 34 * UIScale), FVector2D(X + 496 * UIScale, Y + 111 * UIScale)));
        SingleLineLabel(TEXT("CONSTRUCTION / SELECT LOCKED ITEM FOR REQUIREMENTS"), X, Y - 26 * UIScale, 496 * UIScale, Amber, 0.70f);
        for (int I = 0; I < 6; ++I)
        {
            const auto& Definition = cinder::definition(Buildings[I]);
            const auto Status = Sim.buildStatus(0, Buildings[I], PC->Selection());
            const float BX = X + (I % 3) * (W + 10 * UIScale), BY = Y + (I / 3) * 52 * UIScale;
            Button(FString::Printf(TEXT("%s  %d"), *Name(Buildings[I]), Definition.cost), TEXT("build"), static_cast<int>(Buildings[I]), BX, BY, W, PC->IsBuildMode() && PC->BuildingKind() == Buildings[I]);
            SingleLineLabel(FString::Printf(TEXT("T%d / %s"), Definition.tier, Status.accepted ? TEXT("READY") : TEXT("LOCKED")),
                BX + 12 * UIScale, BY + 27 * UIScale, W - 24 * UIScale, Status.accepted ? Mint : Amber, 0.70f);
        }
    }
    if (!bOnlineNotice && !PC->Feedback().IsEmpty())
    {
        if (PC->bBuildMenu) SingleLineLabel(PC->Feedback(), Margin, Height - 357 * UIScale, Width - 2 * Margin, Amber, 0.80f);
        else
        {
            const bool bQueueVisible = First && cinder::definition(First->kind).building && First->progress >= 1 && !First->queue.empty();
            SingleLineLabel(PC->Feedback(), Margin + 180 * UIScale, Height - (bQueueVisible ? 386 : 226) * UIScale, Width - 2 * Margin - 180 * UIScale, Amber, 0.85f);
        }
    }
    if (PC->bDebug && !bOnlineNotice)
    {
        Label(FString::Printf(TEXT("SIM %.3f ms  |  %d entities  |  tick %llu  |  %.1f fps"), Sim.lastStepMilliseconds(), static_cast<int>(Sim.entities().size()), static_cast<unsigned long long>(Sim.tick()), 1.0f / FMath::Max(GetWorld()->GetDeltaSeconds(), 0.001f)), Margin, Top + 92 * UIScale, Mint, 0.8f);
        Label(UTF8_TO_TCHAR(Sim.aiStatus().c_str()), Margin, Top + 120 * UIScale, Muted, 0.8f);
    }
    if (bOnlineNotice)
        DrawOnlineNotice(OnlineHeading, OnlineDetail, Margin, Top + 54 * UIScale,
            FMath::Min(660 * UIScale, Width - 2 * Margin), false);
    DrawOverlay(PC, Battle);
}

void ACinderHUD::DrawOverlay(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const auto& Sim = Battle->Sim(); const auto& Player = Sim.players()[0];
    const bool bOnline = Battle->IsOnlineMatch();
    if (Battle->IsPaused() || Sim.winner() >= 0)
    {
        Panel(0, 0, Width, Height, FLinearColor(0.015f, 0.03f, 0.04f, 0.89f));
        Buttons.Reset(); UIRegions.Reset(); Minimap.Init();
        UIRegions.Add(FBox2D(FVector2D::ZeroVector, FVector2D(Width, Height)));
        const float X = Width * 0.5f - 270 * UIScale, Y = bCompactLayout ? 28 * UIScale + SafeTopOffset : Height * 0.2f;
        const bool bTraining = Battle->Tutorial().IsActive();
        const FString Heading = Sim.winner() >= 0
            ? bOnline ? Sim.winner() == 0 ? TEXT("YOU WIN") : TEXT("DEFEAT") : Sim.winner() == 0 ? TEXT("FRONTIER SECURED") : TEXT("ANCHOR LOST")
            : bOnline ? TEXT("ONLINE MATCH CONTINUES") : bTraining ? TEXT("TRAINING PAUSED") : TEXT("SKIRMISH PAUSED");
        Label(Heading, X, Y, Sim.winner() == 1 ? Amber : Mint, 1.65f);
        if (Sim.winner() >= 0)
        {
            const auto& S = Player.stats;
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
                Button(bTraining ? TEXT("RESTART TRAINING") : bCompactLayout ? TEXT("REMATCH") : TEXT("REMATCH  [ENTER]"),
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
        WrappedLabel(PC->IsOnlineSurrenderPending() ? TEXT("Concede this match and see the final result.") : TEXT("Leaving forfeits this game."), X + 24 * S, Y + 62 * S, W - 48 * S, White, 0.80f);
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
        WrappedLabel(TEXT("This restarts the practice mission from step one. Your saved skirmish is kept."),
            X + 24 * S, Y + 62 * S, W - 48 * S, White, 0.80f);
        const float ButtonW = (W - 55 * S) * 0.5f;
        Button(bCompactLayout ? TEXT("RESTART") : TEXT("RESTART  [ENTER]"), TEXT("tutorialconfirm"), 0,
            X + 24 * S, Y + H - 66 * S, ButtonW, true);
        Button(bCompactLayout ? TEXT("CANCEL") : TEXT("CANCEL  [ESC]"), TEXT("tutorialcancel"), 0,
            X + 31 * S + ButtonW, Y + H - 66 * S, ButtonW);
    }
}
