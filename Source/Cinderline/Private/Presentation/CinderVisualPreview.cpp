#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderPlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderVisualPreview, Log, All);

namespace
{
TWeakObjectPtr<ACinderBattlefield> VisualPreviewBattle;
constexpr std::uint32_t VisualPreviewSeed = 0xC1D3;

void HoldPreviewUnit(cinder::Simulation& Sim, cinder::Id Id)
{
    const auto* Entity = Sim.find(Id);
    if (!Entity || cinder::definition(Entity->kind).building || Entity->kind == cinder::Kind::Resource) return;
    cinder::Command Command;
    Command.type = cinder::CommandType::Hold;
    Command.team = Entity->team;
    Command.units = {Id};
    Sim.command(Command);
}

void FrameVisualPreview(ACinderPlayerController* PC, const TArray<FVector>& Points)
{
    auto* Rig = PC ? Cast<ACinderCamera>(PC->GetPawn()) : nullptr;
    if (!Rig || !PC->PlayerCameraManager) return;
    int32 Width = 0, Height = 0;
    PC->GetViewportSize(Width, Height);
    FVector Focus(1490, 2380, 0);
    Rig->Zoom(2450 - Rig->Distance());
    Rig->Focus(Focus, true);
    if (Width <= 0 || Height <= 0) return;

    // Reserve the upper resource strip, bottom commands, and space beside the minimap.
    // Fit projected mesh tops as well as their ground footprints on desktop and compact views.
    const FVector2D Low(Width * 0.12, Height * 0.13);
    const FVector2D High(Width * 0.84, Height * 0.72);
    FBox2D Projected(ForceInit);
    for (int32 Pass = 0; Pass < 8; ++Pass)
    {
        PC->PlayerCameraManager->UpdateCamera(0);
        Projected = FBox2D(ForceInit);
        for (const FVector& Point : Points)
        {
            FVector2D Screen;
            if (PC->ProjectWorldLocationToScreen(Point, Screen)) Projected += Screen;
        }
        if (!Projected.bIsValid) break;
        const FVector2D Span = Projected.GetSize(), Available = High - Low;
        const double Fit = FMath::Max(Span.X / Available.X, Span.Y / Available.Y);
        if (Fit > 1.015 && Rig->Distance() < 3199)
        {
            Rig->Zoom(static_cast<float>(Rig->Distance() * (Fit * 1.025 - 1)));
            Rig->Focus(Focus, true);
            continue;
        }
        const FVector2D Offset = Projected.GetCenter() - (Low + High) * 0.5;
        if (Offset.SizeSquared() < 4) break;
        const double Scale = 2 * Rig->Distance() * FMath::Tan(FMath::DegreesToRadians(26.0)) / Width;
        const FVector Right(0.70710678, 0.70710678, 0);
        const FVector GroundUp(0.70710678, -0.70710678, 0);
        Focus += Right * (Offset.X * Scale) - GroundUp * (Offset.Y * Scale / 0.8660254);
        Rig->Focus(Focus, true);
    }
    PC->PlayerCameraManager->UpdateCamera(0);
    UE_LOG(LogCinderVisualPreview, Display,
        TEXT("CINDERLINE_VISUAL_PREVIEW_FRAME viewport=%dx%d distance=%.1f focus=(%.1f,%.1f) target_screen=(%.0f,%.0f)-(%.0f,%.0f); framing is a development camera setup"),
        Width, Height, Rig->Distance(), Rig->GetActorLocation().X, Rig->GetActorLocation().Y, Low.X, Low.Y, High.X, High.Y);
}

FAutoConsoleCommandWithWorldAndArgs VisualPreviewCommand(
    TEXT("cinder.visualpreview"),
    TEXT("DEVELOPMENT: stages and freezes an unsaved materials/cliffs/roster fixture. live advances it; reset creates a fresh menu. Never writes a save."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World) return;
        TActorIterator<ACinderBattlefield> It(World);
        if (!It) return;
        ACinderBattlefield* Battle = *It;
        auto* PC = Cast<ACinderPlayerController>(World->GetFirstPlayerController());
        if (Args.Contains(TEXT("reset")))
        {
            // Reset the fixture too, rather than leaving development units behind a menu.
            if (PC)
            {
                PC->ExecuteAction(TEXT("start"), 0);
                PC->ExecuteAction(TEXT("menu"));
                PC->Notify(TEXT("Visual preview cleared. Fresh skirmish menu."));
            }
            else { Battle->StartMatch(0); Battle->ReturnToMenu(); }
            VisualPreviewBattle.Reset();
            UE_LOG(LogCinderVisualPreview, Display, TEXT("CINDERLINE_VISUAL_PREVIEW reset=fresh_menu; no save written"));
            return;
        }
        const bool bLive = Args.Contains(TEXT("live"));
        if (bLive && VisualPreviewBattle.Get() == Battle && !Battle->IsMenu()
            && !Battle->Sim().config().ai && Battle->Sim().config().seed == VisualPreviewSeed)
        {
            Battle->SetPaused(false);
            Battle->SetActorTickEnabled(true);
            if (PC) PC->Notify(TEXT("VISUAL PREVIEW LIVE · development fixture"));
            UE_LOG(LogCinderVisualPreview, Display, TEXT("CINDERLINE_VISUAL_PREVIEW mode=live_resume tick=%llu; ordinary simulation, no save written"), Battle->Sim().tick());
            return;
        }

        if (PC) PC->ExecuteAction(TEXT("start"), 0);
        else Battle->StartMatch(0);
        auto& Sim = Battle->Sim();
        cinder::Config Config;
        Config.map = 0; Config.ai = false; Config.seed = VisualPreviewSeed;
        Sim.reset(Config);
        for (const auto& Entity : Sim.entities()) HoldPreviewUnit(Sim, Entity.id);
        TArray<FVector> FramePoints;
        int32 Staged = 0;
        auto Spawn = [&](cinder::Kind Kind, int32 Team, float X, float Y, bool bFrame = true)
        {
            const cinder::Id Id = Sim.debugSpawn(Kind, Team, {X, Y});
            HoldPreviewUnit(Sim, Id);
            const auto& Def = cinder::definition(Kind);
            if (bFrame)
            {
                const float Top = Def.building ? 200.0f : Def.air ? 160.0f : 75.0f;
                FramePoints.Add(FVector(X - Def.radius, Y - Def.radius, 0));
                FramePoints.Add(FVector(X + Def.radius, Y + Def.radius, 0));
                FramePoints.Add(FVector(X, Y, Top));
            }
            ++Staged;
        };
        // Frontier's ordinary western cliff remains exactly at (1320,2400), half-size(380,140).
        // Structures occupy its north clearing; the opposing palette sits across the rock.
        Spawn(cinder::Kind::Foundry, 0, 1040, 2040);
        Spawn(cinder::Kind::Laboratory, 0, 1370, 1990);
        Spawn(cinder::Kind::Processor, 0, 1720, 1990);
        const cinder::Kind Roster[] = {cinder::Kind::Worker, cinder::Kind::Striker, cinder::Kind::Lancer,
            cinder::Kind::Scout, cinder::Kind::Bastion, cinder::Kind::Mortar, cinder::Kind::Mender, cinder::Kind::Kite};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Roster); ++Index)
            Spawn(Roster[Index], 0, 1030 + (Index % 4) * 185.0f, 1710 + (Index / 4) * 150.0f);
        Spawn(cinder::Kind::Foundry, 1, 1930, 2660);
        Spawn(cinder::Kind::Turret, 1, 2090, 2440);
        Spawn(cinder::Kind::Striker, 1, 1850, 2850);
        Spawn(cinder::Kind::Lancer, 1, 1990, 2850);
        Spawn(cinder::Kind::Kite, 1, 2130, 2800);
        // Vision comes from actual friendly scouts, not a reveal flag or modified fog rule.
        Spawn(cinder::Kind::Scout, 0, 1470, 2820);
        Spawn(cinder::Kind::Scout, 0, 1630, 2170);
        for (float X : {940.0f, 1700.0f}) for (float Y : {2260.0f, 2540.0f})
        {
            FramePoints.Add(FVector(X, Y, 0));
            FramePoints.Add(FVector(X, Y, 200));
        }

        FrameVisualPreview(PC, FramePoints);
        Battle->ResetPresentation();
        // Three normal fixed steps settle vision and movement before the rendered inspection.
        // Disabling the adapter tick freezes only this explicit preview; the camera remains usable.
        Battle->Tick(0.15f);
        Battle->RenderState();
        Battle->SetActorTickEnabled(bLive);
        VisualPreviewBattle = Battle;
        if (PC) PC->Notify(bLive ? TEXT("VISUAL PREVIEW LIVE · development fixture") : TEXT("VISUAL PREVIEW FROZEN · development fixture"));
        UE_LOG(LogCinderVisualPreview, Display,
            TEXT("CINDERLINE_VISUAL_PREVIEW scene=frontier_western_cliff live=%d staged=%d tick=%llu obstacle=(1320,2400) enemy_palette_visible=%d; ordinary terrain and rules, no save written"),
            bLive, Staged, Sim.tick(), Sim.visible(0, {1930, 2660}));
        Battle->LogModelStatus();
    }));
}

#endif
