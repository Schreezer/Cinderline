#include "CoreMinimal.h"
#if UE_BUILD_DEVELOPMENT
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderPlayerController.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#include "StaticMeshCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCinderFourPlayerPreview, Log, All);
namespace
{
FTimerHandle CaptureTimer;
TWeakObjectPtr<UWorld> CaptureWorld;

bool Free(const cinder::Simulation& Sim, cinder::Kind Kind, cinder::Vec2 Point)
{
    const float Radius = cinder::definition(Kind).radius + 10;
    if (Point.x < Radius || Point.y < Radius || Point.x > Sim.worldSize() - Radius || Point.y > Sim.worldSize() - Radius) return false;
    for (const auto& Obstacle : Sim.obstacles())
    {
        const float X = FMath::Max(FMath::Abs(Point.x - Obstacle.center.x) - Obstacle.half.x, 0.0f);
        const float Y = FMath::Max(FMath::Abs(Point.y - Obstacle.center.y) - Obstacle.half.y, 0.0f);
        if (X * X + Y * Y < Radius * Radius) return false;
    }
    for (const auto& Entity : Sim.entities()) if (Entity.alive())
    {
        const float X = Entity.pos.x - Point.x, Y = Entity.pos.y - Point.y;
        const float Clearance = Radius + cinder::definition(Entity.kind).radius + 10;
        if (X * X + Y * Y < Clearance * Clearance) return false;
    }
    return true;
}

FAutoConsoleCommandWithWorld FourPlayerPreview(
    TEXT("cinder.fourbattlepreview"),
    TEXT("DEVELOPMENT: unattended fresh menu only. Disposable four-faction material and terrain capture; never saves."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        auto* PC = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        auto* Battle = PC ? PC->Battlefield() : nullptr;
        auto* Camera = PC ? Cast<ACinderCamera>(PC->GetPawn()) : nullptr;
        if (!FApp::IsUnattended() || !World || !World->IsGameWorld() || !Battle || !Camera
            || !Battle->IsMenu() || Battle->IsOnlineMatch() || PC->IsHelpOpen()) return;
        if (auto* Previous = CaptureWorld.Get()) Previous->GetTimerManager().ClearTimer(CaptureTimer);
        if (PC->IsTutorialOfferPending()) PC->ExecuteAction(TEXT("onboardskip"));
        PC->ExecuteAction(TEXT("start"), 0);
        cinder::Config Config; Config.ai = false; Config.playerCount = 4; Config.seed = 0x4FFA;
        Battle->Sim().reset(Config);
        auto& Sim = Battle->Sim();
        for (int32 Team = 0; Team < 4; ++Team)
        {
            const cinder::Kind Kinds[] = {cinder::Kind::Foundry, cinder::Kind::Striker, cinder::Kind::Scout};
            for (int32 Unit = 0; Unit < 3; ++Unit)
            {
                const cinder::Vec2 Preferred{1100.0f + Team * 270.0f, 800.0f + Unit * 190.0f};
                cinder::Id Id = 0;
                for (int32 Ring = 0; Ring < 12 && !Id; ++Ring)
                    for (int32 Y = -Ring; Y <= Ring && !Id; ++Y)
                        for (int32 X = -Ring; X <= Ring && !Id; ++X)
                        {
                            if (Ring && FMath::Abs(X) != Ring && FMath::Abs(Y) != Ring) continue;
                            const cinder::Vec2 Point{Preferred.x + X * 45.0f, Preferred.y + Y * 45.0f};
                            if (Free(Sim, Kinds[Unit], Point)) Id = Sim.debugSpawn(Kinds[Unit], Team, Point);
                        }
                if (!Id) { UE_LOG(LogCinderFourPlayerPreview, Error, TEXT("FOUR_PLAYER_PREVIEW spawn_failed")); return; }
                cinder::Command Hold; Hold.type = cinder::CommandType::Hold; Hold.team = Team; Hold.units = {Id}; Sim.command(Hold);
            }
        }
        // Ordinary local scout sight reveals the fixture, without disabling fog.
        for (cinder::Vec2 Point : {cinder::Vec2{1400, 500}, cinder::Vec2{1750, 1450}})
            if (Free(Sim, cinder::Kind::Scout, Point)) Sim.debugSpawn(cinder::Kind::Scout, 0, Point);
        Battle->ResetPresentation(); Battle->RenderState(); Battle->SetActorTickEnabled(false);
        PC->Notify(TEXT("Four-faction visual fixture"));
        Camera->Zoom(2150 - Camera->Distance()); Camera->Focus(FVector(1500, 1000, 0), true);
        CaptureWorld = World;
        World->GetTimerManager().SetTimer(CaptureTimer, FTimerDelegate::CreateLambda([]()
        {
#if WITH_EDITOR
            FStaticMeshCompilingManager::Get().FinishAllCompilation();
            if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
            UE_LOG(LogCinderFourPlayerPreview, Display, TEXT("FOUR_PLAYER_PREVIEW shader_jobs=%d"),
                GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0);
#endif
            const FString Directory = FPaths::ProjectSavedDir() / TEXT("FourPlayer");
            IFileManager::Get().MakeDirectory(*Directory, true);
            FScreenshotRequest::RequestScreenshot(Directory / TEXT("battle.png"), true, false);
            UE_LOG(LogCinderFourPlayerPreview, Display, TEXT("FOUR_PLAYER_PREVIEW capture_ready teams=4 no_save=1"));
        }), 2.0f, false);
    }));
}
#endif
