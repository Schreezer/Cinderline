#include "Presentation/CinderHUD.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include <algorithm>
#include <map>

DEFINE_LOG_CATEGORY_STATIC(LogCinderHUD, Log, All);

namespace
{
const FLinearColor Ink(0.025f, 0.06f, 0.08f, 0.96f);
const FLinearColor PanelInk(0.04f, 0.10f, 0.12f, 0.97f);
const FLinearColor White(0.87f, 0.96f, 0.95f);
const FLinearColor Muted(0.46f, 0.67f, 0.68f);
const FLinearColor Mint(0.10f, 0.90f, 0.74f);
const FLinearColor Amber(1.0f, 0.64f, 0.24f);
FString Name(cinder::Kind Kind) { return UTF8_TO_TCHAR(cinder::definition(Kind).name); }
FString ClockString(float Seconds) { return FString::Printf(TEXT("%02d:%02d"), static_cast<int>(Seconds) / 60, static_cast<int>(Seconds) % 60); }
}

void ACinderHUD::BeginPlay()
{
    Super::BeginPlay();
    // Load once per HUD lifetime; retain the optional texture through garbage collection.
    MenuBackdrop = MenuBackdropAsset.LoadSynchronous();
}

void ACinderHUD::Panel(float X, float Y, float W, float H, FLinearColor Color) { DrawRect(Color, X, Y, W, H); }
void ACinderHUD::Label(const FString& Text, float X, float Y, FLinearColor Color, float Scale)
{
    if (!GEngine || !Canvas) return;
    FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), GEngine->GetMediumFont(), Color);
    Item.Scale = FVector2D(Scale * UIScale);
    Item.EnableShadow(FLinearColor(0, 0, 0, 0.55f));
    Canvas->DrawItem(Item);
}
void ACinderHUD::Button(const FString& Text, const FString& Action, int Arg, float X, float Y, float W, bool Active)
{
    const float H = 44 * UIScale;
    Panel(X, Y, W, H, Active ? FLinearColor(0.07f, 0.32f, 0.30f) : PanelInk);
    Panel(X, Y, W, 2 * UIScale, Active ? Mint : FLinearColor(0.14f, 0.28f, 0.29f));
    float TextWidth = 0, TextHeight = 0;
    GetTextSize(Text, TextWidth, TextHeight, GEngine ? GEngine->GetMediumFont() : nullptr, 0.80f * UIScale);
    const float Fit = TextWidth > 0 ? FMath::Min(1.0f, (W - 20 * UIScale) / TextWidth) : 1;
    Label(Text, X + 10 * UIScale, Y + 12 * UIScale, Active ? Mint : White, 0.80f * Fit);
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
        if (B.Action == TEXT("map")) SelectedMap = B.Argument;
        else if (B.Action == TEXT("groupsnext")) ++SubgroupPage;
        else if (B.Action == TEXT("sheet")) { CompactSheet = CompactSheet == B.Argument ? 0 : B.Argument; PC->bBuildMenu = false; }
        else PC->ExecuteAction(B.Action, B.Argument);
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

void ACinderHUD::DrawHUD()
{
    Super::DrawHUD();
    Buttons.Reset(); UIRegions.Reset(); Minimap.Init();
    if (!Canvas) return;
    Width = Canvas->SizeX; Height = Canvas->SizeY;
#if PLATFORM_IOS || PLATFORM_ANDROID
    bCompactLayout = true;
#else
    bCompactLayout = Width / Height > 2.0f || Height < 500;
#endif
    UIScale = bCompactLayout ? FMath::Min(Width / 667.0f, Height / 375.0f) : FMath::Max(0.45f, FMath::Min(Width / 1280.0f, Height / 720.0f));
    // Conservative landscape insets reserve room for phone cutouts and the home indicator.
    Margin = bCompactLayout ? 60 * UIScale : FMath::Max(24 * UIScale, Width * 0.035f);
    auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
    auto* Battle = PC ? PC->Battlefield() : nullptr;
    if (!Battle) { Label(TEXT("Preparing battlefield..."), Margin, Margin, White); return; }
    if (Battle->IsMenu()) { DrawMenu(Battle); return; }
    DrawWorldIndicators(PC, Battle);
    if (bCompactLayout) DrawCompactMatch(PC, Battle); else DrawMatch(PC, Battle);
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
    Panel(0, 0, Width, Height, FLinearColor(0.015f, 0.035f, 0.045f, bHasBackdrop ? 0.40f : 0.85f));
    UIRegions.Add(FBox2D(FVector2D::ZeroVector, FVector2D(Width, Height)));
    if (bCompactLayout)
    {
        const float X = Margin, Y = 25 * UIScale;
        Label(TEXT("C I N D E R L I N E"), X, Y, White, 1.6f);
        Label(TEXT("CAIRN ASSEMBLY / FRONTIER SKIRMISH"), X, Y + 39 * UIScale, Mint, 0.75f);
        Label(TEXT("Grow your economy. Scout the dark. Destroy the opposing Anchor."), X, Y + 78 * UIScale, White, 0.85f);
        const float MapW = (Width - 2 * Margin - 16 * UIScale) / 3;
        const TCHAR* Names[] = { TEXT("Shattered Rift"), TEXT("Glass Basin"), TEXT("Iron Reach") };
        for (int I = 0; I < 3; ++I) Button(Names[I], TEXT("map"), I, X + I * (MapW + 8 * UIScale), Y + 117 * UIScale, MapW, SelectedMap == I);
        Button(TEXT("START SKIRMISH"), TEXT("start"), SelectedMap, X, Y + 177 * UIScale, MapW, true);
        Button(TEXT("CONTINUE SAVE"), TEXT("load"), 0, X + MapW + 8 * UIScale, Y + 177 * UIScale, MapW);
        Label(TEXT("Drag to pan / Pinch to zoom / Tap a unit, then tap its destination"), X, Y + 247 * UIScale, Muted, 0.78f);
        Label(TEXT("Hold, then drag to select / Double-tap a unit to select its type"), X, Y + 273 * UIScale, Muted, 0.78f);
        Label(TEXT("Development build / Offline opponent / One faction"), X, Height - 32 * UIScale, Amber, 0.72f);
        return;
    }
    const float X = Width * 0.12f, Y = Height * 0.14f;
    Label(TEXT("C I N D E R L I N E"), X, Y, White, 2.35f);
    Label(TEXT("THE CAIRN ASSEMBLY  /  FRONTIER SKIRMISH"), X + 3 * UIScale, Y + 61 * UIScale, Mint, 0.90f);
    Label(TEXT("Grow a frontier settlement. Scout the dark. Command a combined army."), X, Y + 112 * UIScale, White, 0.94f);
    Label(TEXT("Destroy the opposing Anchor to win."), X, Y + 141 * UIScale, Muted, 0.9f);
    const TCHAR* MapNames[] = { TEXT("01  Shattered Rift"), TEXT("02  Glass Basin"), TEXT("03  Iron Reach") };
    for (int I = 0; I < 3; ++I) Button(MapNames[I], TEXT("map"), I, X + I * 251 * UIScale, Y + 204 * UIScale, 238 * UIScale, SelectedMap == I);
    Button(TEXT("START SKIRMISH  [ENTER]"), TEXT("start"), SelectedMap, X, Y + 272 * UIScale, 238 * UIScale, true);
    Button(TEXT("CONTINUE SAVE"), TEXT("load"), 0, X + 251 * UIScale, Y + 272 * UIScale, 238 * UIScale);
    Label(TEXT("TOUCH   Drag to pan  |  Pinch to zoom  |  Tap to select and command"), X, Y + 351 * UIScale, Muted, 0.84f);
    Label(TEXT("ARMY    Hold, then drag to select  |  Double-tap a unit to select its type"), X, Y + 379 * UIScale, Muted, 0.84f);
    Label(TEXT("MOUSE   Left drag selects  |  Middle drag pans  |  Right click commands"), X, Y + 407 * UIScale, Muted, 0.84f);
    Label(TEXT("Development build  /  Offline opponent  /  One faction"), X, Height - 58 * UIScale, Amber, 0.78f);
}

void ACinderHUD::DrawMinimap(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const float Size = (bCompactLayout ? 92 : 155) * UIScale;
    const FVector2D Origin(Margin, bCompactLayout ? 76 * UIScale : Height - Size - 30 * UIScale);
    Minimap = FBox2D(Origin, Origin + FVector2D(Size));
    UIRegions.Add(Minimap);
    Panel(Origin.X - 3, Origin.Y - 3, Size + 6, Size + 6, Muted);
    const float Cell = Size / cinder::Simulation::FogSize;
    for (int Y = 0; Y < cinder::Simulation::FogSize; ++Y)
        for (int X = 0; X < cinder::Simulation::FogSize; ++X)
        {
            const cinder::Vec2 P{ (X + 0.5f) * 75, (Y + 0.5f) * 75 };
            const FLinearColor C = Battle->Sim().visible(0, P) ? FLinearColor(0.11f, 0.25f, 0.25f) : Battle->Sim().explored(0, P) ? FLinearColor(0.065f, 0.12f, 0.14f) : FLinearColor(0.014f, 0.025f, 0.04f);
            Panel(Origin.X + X * Cell, Origin.Y + Y * Cell, Cell + 0.3f, Cell + 0.3f, C);
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
    Label(TEXT("SECTOR MAP"), Origin.X, Origin.Y - 23 * UIScale, Muted, 0.7f);
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
        const auto& D = cinder::definition(E.kind);
        const bool Selected = std::find(PC->Selection().begin(), PC->Selection().end(), E.id) != PC->Selection().end();
        if (Selected)
        {
            const float R = D.radius + 9;
            for (int I = 0; I < 24; ++I)
            {
                const float A = I * UE_TWO_PI / 24, B = (I + 1) * UE_TWO_PI / 24;
                WorldLine(FVector(E.pos.x + FMath::Cos(A) * R, E.pos.y + FMath::Sin(A) * R, 7), FVector(E.pos.x + FMath::Cos(B) * R, E.pos.y + FMath::Sin(B) * R, 7), Mint, 2);
            }
            if (!D.building && E.order != cinder::Order::Idle && PC->bDebug) WorldLine(FVector(E.pos.x, E.pos.y, 10), FVector(E.goal.x, E.goal.y, 10), Muted, 1);
        }
        if (Selected || E.hp < D.hp || E.progress < 1)
        {
            FVector2D P;
            const float Z = D.air ? 180 : D.building ? 200 : 83;
            if (PC->ProjectWorldLocationToScreen(FVector(E.pos.x, E.pos.y, Z), P))
            {
                const float BarW = (D.building ? 58 : 34) * UIScale;
                Panel(P.X - BarW / 2, P.Y, BarW, 4 * UIScale, Ink);
                Panel(P.X - BarW / 2, P.Y, BarW * FMath::Clamp(E.hp / D.hp, 0.0f, 1.0f), 4 * UIScale, E.team == 0 ? Mint : Amber);
                if (E.progress < 1) Panel(P.X - BarW / 2, P.Y + 6 * UIScale, BarW * E.progress, 3 * UIScale, Amber);
            }
        }
    }
    for (const auto& FX : Sim.effects())
    {
        if (!Sim.visible(0, FX.from) && !Sim.visible(0, FX.to)) continue;
        const FLinearColor C = FX.team == 0 ? Mint : Amber;
        WorldLine(FVector(FX.from.x, FX.from.y, 35), FVector(FX.to.x, FX.to.y, 35), C, FX.explosion ? 4 : 2);
        FVector2D P;
        if (PC->ProjectWorldLocationToScreen(FVector(FX.to.x, FX.to.y, 35), P))
        {
            const float Radius = (FX.explosion ? 13 : 4) * UIScale;
            DrawLine(P.X - Radius, P.Y, P.X + Radius, P.Y, C, 2 * UIScale);
            DrawLine(P.X, P.Y - Radius, P.X, P.Y + Radius, C, 2 * UIScale);
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
        std::string Reason; const bool Valid = Sim.canPlace(0, PC->BuildingKind(), P, &Reason);
        const FLinearColor Color = Valid ? Mint : FLinearColor(1, 0.24f, 0.18f);
        for (int I = 0; I < 32; ++I)
        {
            const float A = I * UE_TWO_PI / 32, B = (I + 1) * UE_TWO_PI / 32;
            WorldLine(FVector(P.x + FMath::Cos(A) * R, P.y + FMath::Sin(A) * R, 8), FVector(P.x + FMath::Cos(B) * R, P.y + FMath::Sin(B) * R, 8), Color, 3);
        }
        FVector2D Screen;
        if (PC->ProjectWorldLocationToScreen(FVector(P.x, P.y, 10), Screen)) Label(Valid ? TEXT("TAP TO BUILD") : UTF8_TO_TCHAR(Reason.c_str()), Screen.X + 14 * UIScale, Screen.Y - 20 * UIScale, Color, 0.8f);
    }
}

void ACinderHUD::DrawCompactMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const auto& Sim = Battle->Sim(); const auto& Player = Sim.players()[0];
    const float S = UIScale;
    const float InnerW = Width - 2 * Margin;
    const float Top = 12 * S, Bottom = Height - 70 * S;
    Panel(Margin - 6 * S, Top - 4 * S, InnerW + 12 * S, 46 * S, Ink);
    UIRegions.Add(FBox2D(FVector2D(Margin - 6 * S, Top - 4 * S), FVector2D(Width - Margin + 6 * S, Top + 42 * S)));
    Label(FString::Printf(TEXT("ORE %d"), Player.ore), Margin + 6 * S, Top + 11 * S, Amber, 0.9f);
    Label(FString::Printf(TEXT("CREW %d/%d"), Sim.supply(0), Sim.capacity(0)), Margin + 112 * S, Top + 12 * S, White, 0.77f);
    Label(FString::Printf(TEXT("T%d W%d A%d"), Player.tier, Player.weapons, Player.armor), Margin + 248 * S, Top + 12 * S, Mint, 0.77f);
    Label(ClockString(Sim.time()), Width - Margin - 166 * S, Top + 12 * S, Muted, 0.77f);
    Button(TEXT("PAUSE"), TEXT("pause"), 0, Width - Margin - 88 * S, Top - 3 * S, 88 * S);
    if (CompactSheet == 0 && !PC->bBuildMenu) DrawMinimap(PC, Battle);
    const cinder::Entity* First = PC->Selection().empty() ? nullptr : Sim.find(PC->Selection().front());
    Panel(0, Height - 129 * S, Width, 129 * S, Ink);
    UIRegions.Add(FBox2D(FVector2D(0, Height - 129 * S), FVector2D(Width, Height)));
    const float W = (InnerW - 5 * 7 * S) / 6;
    const TCHAR* Titles[] = { TEXT("ARMY"), TEXT("SELECT"), TEXT("ATTACK"), TEXT("ACTIONS"), TEXT("BUILD"), TEXT("HOME") };
    const TCHAR* Actions[] = { TEXT("army"), TEXT("box"), TEXT("attack"), TEXT("sheet"), TEXT("buildmenu"), TEXT("home") };
    for (int I = 0; I < 6; ++I)
        Button(Titles[I], Actions[I], I == 3 ? 1 : 0, Margin + I * (W + 7 * S), Bottom, W, I == 1 ? PC->bBoxSelect : I == 2 ? PC->bAttackMove : I == 3 ? CompactSheet == 1 : I == 4 ? PC->bBuildMenu : false);

    const float SummaryY = Height - 122 * S;
    const float SummaryW = InnerW - 238 * S;
    const FString Summary = First ? PC->Selection().size() > 1 ? FString::Printf(TEXT("%d UNITS / FOCUS"), static_cast<int>(PC->Selection().size())) : FString::Printf(TEXT("%s / HP %d"), *Name(First->kind), static_cast<int>(First->hp)) : TEXT("SELECT A UNIT OR STRUCTURE");
    Button(Summary, TEXT("focus"), 0, Margin, SummaryY, SummaryW);
    Button(TEXT("TYPES"), TEXT("sheet"), 2, Margin + SummaryW + 8 * S, SummaryY, 105 * S, CompactSheet == 2);
    if (First && cinder::definition(First->kind).building)
        Button(FString::Printf(TEXT("QUEUE %d"), static_cast<int>(First->queue.size())), TEXT("sheet"), 3, Width - Margin - 117 * S, SummaryY, 117 * S, CompactSheet == 3);
    else Button(TEXT("STOP"), TEXT("stop"), 0, Width - Margin - 117 * S, SummaryY, 117 * S);

    if (PC->IsBuildMode())
    {
        Button(TEXT("CANCEL PLACEMENT"), TEXT("cancelplacement"), 0, Width - Margin - 195 * S, 65 * S, 195 * S, true);
    }
    else if (PC->bBuildMenu || CompactSheet != 0)
    {
        struct FOption { FString Text, Action; int Arg; };
        TArray<FOption> Options;
        FString Title;
        if (PC->bBuildMenu)
        {
            Title = TEXT("BUILD / SELECT A DRUDGE FIRST");
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
        else if (CompactSheet == 3)
        {
            Title = TEXT("PRODUCTION QUEUE / TAP TO CANCEL");
            if (First) for (int I = 0; I < static_cast<int>(First->queue.size()); ++I)
            {
                const auto& Q = First->queue[I];
                Options.Add({ FString::Printf(TEXT("%s %ds"), Q.research ? TEXT("Research") : *Name(Q.kind), FMath::CeilToInt(Q.remaining)), TEXT("cancelqueue"), I });
            }
        }
        else
        {
            Title = TEXT("CONTEXTUAL ACTIONS");
            if (First && First->progress < 1) Options.Add({ TEXT("CANCEL BUILDING"), TEXT("cancelbuilding"), 0 });
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
        const float SheetW = FMath::Min(InnerW, 620 * S), X = Width - Margin - SheetW, Y = Height - 284 * S;
        Panel(X - 5 * S, Y - 6 * S, SheetW + 10 * S, 152 * S, Ink);
        UIRegions.Add(FBox2D(FVector2D(X - 5 * S, Y - 6 * S), FVector2D(X + SheetW + 5 * S, Y + 146 * S)));
        Label(Title, X + 5 * S, Y + 7 * S, Amber, 0.74f);
        const float CellW = (SheetW - 3 * 7 * S) / 4;
        for (int I = 0; I < Options.Num() && I < 8; ++I)
            Button(Options[I].Text, Options[I].Action, Options[I].Arg, X + (I % 4) * (CellW + 7 * S), Y + (35 + (I / 4) * 51) * S, CellW);
        if (Options.IsEmpty()) Label(TEXT("Select a unit or a production structure first."), X + 5 * S, Y + 56 * S, White, 0.78f);
    }
    if (!PC->Feedback().IsEmpty()) Label(PC->Feedback(), Margin + 107 * S, 72 * S, Amber, 0.70f);
    else if (Sim.time() < 120 && CompactSheet == 0 && !PC->bBuildMenu)
        Label(TEXT("Select a Drudge. Build a Kiln. Train an army."), Margin + 107 * S, 72 * S, White, 0.76f);
    if (PC->bDebug)
        Label(FString::Printf(TEXT("%.2f ms sim / %d entities / %.0f fps"), Sim.lastStepMilliseconds(), static_cast<int>(Sim.entities().size()), 1.0f / FMath::Max(GetWorld()->GetDeltaSeconds(), 0.001f)), Margin + 107 * S, 98 * S, Mint, 0.72f);
    DrawOverlay(PC, Battle);
}

void ACinderHUD::DrawMatch(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const auto& Sim = Battle->Sim(); const auto& Player = Sim.players()[0];
    const float Top = 18 * UIScale, Bottom = Height - 126 * UIScale;
    Panel(Margin - 8 * UIScale, Top - 6 * UIScale, 520 * UIScale, 47 * UIScale, Ink);
    UIRegions.Add(FBox2D(FVector2D(Margin - 8 * UIScale, Top - 6 * UIScale), FVector2D(Margin + 512 * UIScale, Top + 41 * UIScale)));
    Label(FString::Printf(TEXT("ORE  %d"), Player.ore), Margin + 5 * UIScale, Top + 7 * UIScale, Amber, 1.05f);
    Label(FString::Printf(TEXT("CREW  %d / %d"), Sim.supply(0), Sim.capacity(0)), Margin + 155 * UIScale, Top + 9 * UIScale, White, 0.85f);
    Label(FString::Printf(TEXT("T%d  W%d  A%d"), Player.tier, Player.weapons, Player.armor), Margin + 300 * UIScale, Top + 9 * UIScale, Mint, 0.85f);
    Label(ClockString(Sim.time()), Margin + 436 * UIScale, Top + 9 * UIScale, Muted, 0.85f);
    Button(TEXT("PAUSE"), TEXT("pause"), 0, Width - Margin - 90 * UIScale, Top - 5 * UIScale, 90 * UIScale);
    Button(TEXT("+"), TEXT("zoom+"), 0, Width - Margin - 50 * UIScale, Top + 68 * UIScale, 50 * UIScale);
    Button(TEXT("-"), TEXT("zoom-"), 0, Width - Margin - 50 * UIScale, Top + 119 * UIScale, 50 * UIScale);
    if (Sim.time() < 180)
    {
        const FString Hint = Player.stats.built == 0 ? TEXT("OPENING  Build a Kiln, train Ember infantry, and scout beyond your perimeter.") : Player.stats.produced < 4 ? TEXT("REINFORCE  Select your production structure to queue units. Tap terrain to set its rally.") : TEXT("SCOUT  The red Anchor is your objective. Keep harvesting and expand before your ore runs out.");
        Label(Hint, Margin, Top + 54 * UIScale, White, 0.76f);
    }
    Panel(0, Height - 196 * UIScale, Width, 196 * UIScale, Ink);
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
        Label(TEXT("Tap a unit or structure."), SX, Height - 140 * UIScale, White, 0.78f);
        Label(TEXT("Hold, then drag to select."), SX, Height - 115 * UIScale, Muted, 0.78f);
        Label(TEXT("Two fingers pan and zoom."), SX, Height - 90 * UIScale, Muted, 0.78f);
    }
    else
    {
        const auto& Def = cinder::definition(First->kind);
        Label(PC->Selection().size() == 1 ? Name(First->kind).ToUpper() : FString::Printf(TEXT("%d UNITS SELECTED"), static_cast<int>(PC->Selection().size())), SX, Height - 174 * UIScale, Mint, 0.92f);
        Label(FString::Printf(TEXT("HP %d / %d"), static_cast<int>(First->hp), static_cast<int>(Def.hp)), SX, Height - 144 * UIScale, White, 0.78f);
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
            Button(FString::Printf(TEXT("BUILDING %d%% / CANCEL"), static_cast<int>(First->progress * 100)), TEXT("cancelbuilding"), 0, CommandsX, ContextY, 238 * UIScale);
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
                const float QY = Height - 244 * UIScale;
                const float QW = 155 * UIScale;
                for (int I = 0; I < static_cast<int>(First->queue.size()) && I < 3; ++I)
                {
                    const auto& Q = First->queue[I];
                    const FString QName = Q.research ? TEXT("Research") : Name(Q.kind);
                    Button(FString::Printf(TEXT("%s %ds  x"), *QName, FMath::CeilToInt(Q.remaining)), TEXT("cancelqueue"), I, CommandsX + I * (QW + 10 * UIScale), QY, QW);
                    Panel(CommandsX + I * (QW + 10 * UIScale), QY + 41 * UIScale, QW * FMath::Clamp(1 - Q.remaining / FMath::Max(Q.total, 0.01f), 0.0f, 1.0f), 2 * UIScale, Mint);
                }
            }
        }
    }
    if (PC->bBuildMenu)
    {
        const cinder::Kind Buildings[] = { cinder::Kind::Headquarters, cinder::Kind::Processor, cinder::Kind::Foundry, cinder::Kind::MotorPool, cinder::Kind::Laboratory, cinder::Kind::Turret };
        const float X = CommandsX, Y = Height - 297 * UIScale, W = 156 * UIScale;
        Panel(X - 8 * UIScale, Y - 34 * UIScale, 504 * UIScale, 145 * UIScale, Ink);
        UIRegions.Add(FBox2D(FVector2D(X - 8 * UIScale, Y - 34 * UIScale), FVector2D(X + 496 * UIScale, Y + 111 * UIScale)));
        Label(TEXT("CONSTRUCTION / SELECT A DRUDGE FIRST"), X, Y - 26 * UIScale, Amber, 0.70f);
        for (int I = 0; I < 6; ++I)
            Button(FString::Printf(TEXT("%s  %d"), *Name(Buildings[I]), cinder::definition(Buildings[I]).cost), TEXT("build"), static_cast<int>(Buildings[I]), X + (I % 3) * (W + 10 * UIScale), Y + (I / 3) * 52 * UIScale, W, PC->IsBuildMode() && PC->BuildingKind() == Buildings[I]);
    }
    if (!PC->Feedback().IsEmpty()) Label(PC->Feedback(), Margin + 180 * UIScale, Height - 226 * UIScale, Amber, 0.85f);
    if (PC->bDebug)
    {
        Label(FString::Printf(TEXT("SIM %.3f ms  |  %d entities  |  tick %llu  |  %.1f fps"), Sim.lastStepMilliseconds(), static_cast<int>(Sim.entities().size()), static_cast<unsigned long long>(Sim.tick()), 1.0f / FMath::Max(GetWorld()->GetDeltaSeconds(), 0.001f)), Margin, Top + 92 * UIScale, Mint, 0.8f);
        Label(UTF8_TO_TCHAR(Sim.aiStatus().c_str()), Margin, Top + 120 * UIScale, Muted, 0.8f);
    }
    DrawOverlay(PC, Battle);
}

void ACinderHUD::DrawOverlay(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    const auto& Sim = Battle->Sim(); const auto& Player = Sim.players()[0];
    if (Battle->IsPaused() || Sim.winner() >= 0)
    {
        Panel(0, 0, Width, Height, FLinearColor(0.015f, 0.03f, 0.04f, 0.89f));
        Buttons.Reset(); UIRegions.Reset(); Minimap.Init();
        UIRegions.Add(FBox2D(FVector2D::ZeroVector, FVector2D(Width, Height)));
        const float X = Width * 0.5f - 270 * UIScale, Y = bCompactLayout ? 28 * UIScale : Height * 0.2f;
        Label(Sim.winner() >= 0 ? Sim.winner() == 0 ? TEXT("FRONTIER SECURED") : TEXT("ANCHOR LOST") : TEXT("SKIRMISH PAUSED"), X, Y, Sim.winner() == 1 ? Amber : Mint, 1.65f);
        if (Sim.winner() >= 0)
        {
            const auto& S = Player.stats;
            Label(FString::Printf(TEXT("%s    Ore collected %d    Damage %.0f"), *ClockString(Sim.time()), S.gathered, S.damage), X, Y + 62 * UIScale, White, 0.9f);
            Label(FString::Printf(TEXT("Produced %d    Lost %d    Destroyed %d"), S.produced, S.lost, S.killed), X, Y + 96 * UIScale, White, 0.9f);
            Label(FString::Printf(TEXT("Structures %d    Razed %d    Expansions %d"), S.built, S.buildingsDestroyed, S.expansions), X, Y + 130 * UIScale, White, 0.9f);
            Label(FString::Printf(TEXT("Technology upgrades %d"), S.upgrades), X, Y + 164 * UIScale, White, 0.9f);
            Button(bCompactLayout ? TEXT("REMATCH") : TEXT("REMATCH  [ENTER]"), TEXT("start"), Battle->MapIndex(), X, Y + 224 * UIScale, 230 * UIScale, true);
            Button(TEXT("MAIN MENU"), TEXT("menu"), 0, X + 250 * UIScale, Y + 224 * UIScale, 230 * UIScale);
        }
        else
        {
            Button(TEXT("RESUME"), TEXT("resume"), 0, X, Y + 80 * UIScale, 230 * UIScale, true);
            Button(TEXT("SAVE MATCH"), TEXT("save"), 0, X + 250 * UIScale, Y + 80 * UIScale, 230 * UIScale);
            Button(TEXT("LOAD MATCH"), TEXT("load"), 0, X, Y + 136 * UIScale, 230 * UIScale);
            Button(TEXT("MAIN MENU"), TEXT("menu"), 0, X + 250 * UIScale, Y + 136 * UIScale, 230 * UIScale);
            Button(PC->bDebug ? TEXT("HIDE METRICS") : TEXT("SHOW METRICS"), TEXT("debug"), 0, X, Y + 192 * UIScale, 230 * UIScale, PC->bDebug);
            Label(TEXT("A attack-move / S stop / H hold / B build / F focus / Space home"), X, Y + 264 * UIScale, Muted, 0.78f);
            if (!PC->Feedback().IsEmpty()) Label(PC->Feedback(), X, Y + 310 * UIScale, Amber, 0.85f);
        }
    }
}
