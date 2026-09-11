#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderPlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderCombatPreview, Log, All);

namespace
{
void Order(cinder::Simulation& Sim, cinder::Id Unit, cinder::CommandType Type, cinder::Id Target = 0)
{
    const auto* Entity = Sim.find(Unit);
    if (!Entity) return;
    cinder::Command Command;
    Command.team = Entity->team; Command.units = {Unit}; Command.type = Type; Command.target = Target;
    Sim.command(Command);
}

// This is an explicit development fixture, never part of starting a skirmish or
// the opponent's rules. Damage and healing still run through ordinary combat.
FAutoConsoleCommandWithWorldAndArgs CombatPreviewCommand(
    TEXT("cinder.combatpreview"),
    TEXT("DEVELOPMENT: replaces the current unsaved match with a combat fixture. weapons|support [live]; reset returns to a fresh menu. Never writes a save."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World) return;
        TActorIterator<ACinderBattlefield> It(World);
        if (!It) return;
        ACinderBattlefield* Battle = *It;
        ACinderPlayerController* PC = Cast<ACinderPlayerController>(World->GetFirstPlayerController());
        Battle->SetActorTickEnabled(true);
        if (Args.Contains(TEXT("reset")))
        {
            if (PC) PC->ExecuteAction(TEXT("menu"));
            else Battle->ReturnToMenu();
            return;
        }
        if (PC) PC->ExecuteAction(TEXT("start"), 0);
        else Battle->StartMatch(0);
        auto& Sim = Battle->Sim();
        cinder::Config Config; Config.ai = false;
        Sim.reset(Config);
        const bool Support = Args.Contains(TEXT("support"));
        auto Spawn = [&](cinder::Kind Kind, int Team, float X, float Y)
        {
            const auto Id = Sim.debugSpawn(Kind, Team, {X, Y});
            Order(Sim, Id, cinder::CommandType::Hold);
            return Id;
        };
        if (Support)
        {
            const auto Patient = Spawn(cinder::Kind::Worker, 0, 1200, 1030);
            const auto Attacker = Spawn(cinder::Kind::Striker, 1, 1390, 1030);
            Order(Sim, Attacker, cinder::CommandType::Attack, Patient);
            Sim.update(cinder::Simulation::Step); // Wound the patient through a real shot.
            Spawn(cinder::Kind::Mender, 0, 1150, 1110);
            const auto Victim = Spawn(cinder::Kind::Worker, 1, 1480, 1350);
            for (const auto P : {cinder::Vec2{1210, 1320}, cinder::Vec2{1210, 1380}, cinder::Vec2{1255, 1350}})
            {
                const auto Lancer = Spawn(cinder::Kind::Lancer, 0, P.x, P.y);
                Order(Sim, Lancer, cinder::CommandType::Attack, Victim);
            }
        }
        else
        {
            const auto Armor = Spawn(cinder::Kind::Bastion, 1, 1560, 990);
            const auto Lancer = Spawn(cinder::Kind::Lancer, 0, 1280, 990);
            Order(Sim, Lancer, cinder::CommandType::Attack, Armor);
            const auto Ward = Spawn(cinder::Kind::Turret, 1, 1710, 1240);
            const auto Mortar = Spawn(cinder::Kind::Mortar, 0, 1120, 1240);
            Order(Sim, Mortar, cinder::CommandType::Attack, Ward);
            const auto Infantry = Spawn(cinder::Kind::Striker, 1, 1550, 1510);
            const auto Ember = Spawn(cinder::Kind::Striker, 0, 1340, 1510);
            Order(Sim, Ember, cinder::CommandType::Attack, Infantry);
            const auto Veil = Spawn(cinder::Kind::Kite, 0, 1270, 1400);
            Order(Sim, Veil, cinder::CommandType::Attack, Infantry);
        }
        if (PC)
        {
            if (auto* Rig = Cast<ACinderCamera>(PC->GetPawn()))
            {
                // Frame the fixture inside the battlefield above the command panel.
                Rig->Zoom(1800 - Rig->Distance());
                Rig->Focus(FVector(1310, 1320, 0), true);
            }
            PC->Notify(TEXT("COMBAT PREVIEW · development fixture"));
        }
        // Update the camera's cached projection before the first audible events.
        if (PC && PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0);
        Battle->ResetFeedback();
        Battle->Tick(0.15f);
        Battle->RenderState();
        const bool Live = Args.Contains(TEXT("live"));
        Battle->SetActorTickEnabled(Live);
        UE_LOG(LogCinderCombatPreview, Display,
            TEXT("CINDERLINE_COMBAT_PREVIEW mode=%s live=%d tick=%llu effects=%llu; development actors, ordinary damage/healing, no save written"),
            Support ? TEXT("support") : TEXT("weapons"), Live, Sim.tick(), static_cast<uint64>(Sim.effects().size()));
    }));
}

#endif
