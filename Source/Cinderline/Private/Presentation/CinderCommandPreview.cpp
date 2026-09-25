#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderHUD.h"
#include "Presentation/CinderPlayerController.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderCommandPreview, Log, All);

namespace
{
TWeakObjectPtr<UWorld> CommandCaptureWorld;
FTimerHandle CommandPanelTimer, CommandSecondPanelTimer, CommandThirdPanelTimer;
FTimerHandle CommandFourthPanelTimer, CommandFifthPanelTimer, CommandSixthPanelTimer;
FTimerHandle CommandSeventhPanelTimer, CommandCaptureTimer;

FAutoConsoleCommandWithWorldAndArgs CommandPreviewCommand(
    TEXT("cinder.commandpreview"),
    TEXT("DEVELOPMENT: unattended fresh local menu only. Army HUD fixture; never writes preferences or saves. States include idle|ribbon|army|roster-page|squads|unit|orders|queue|rally-unset|rally-set|rally-marker|rally-override|worker-rally|build-plan|deselected."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        const FString State = Args.Num() == 1 ? Args[0].ToLower() : FString();
        const TArray<FString> States{TEXT("idle"), TEXT("ribbon"), TEXT("army"), TEXT("roster-page"), TEXT("squads"),
            TEXT("unit"), TEXT("unit-info"), TEXT("tower"), TEXT("tower-info"), TEXT("healer"),
            TEXT("building-info"), TEXT("orders"), TEXT("defend"), TEXT("queue"), TEXT("worker"),
            TEXT("building-guide"), TEXT("unit-guide"), TEXT("unit-combat"), TEXT("healer-combat"),
            TEXT("global-build"), TEXT("global-train"), TEXT("global-research"), TEXT("global-jobs"),
            TEXT("global-pinned"), TEXT("global-blocked"), TEXT("global-rally"), TEXT("global-construction"),
            TEXT("rally-unset"), TEXT("rally-set"), TEXT("rally-marker"), TEXT("rally-override"),
            TEXT("worker-rally"), TEXT("build-plan"), TEXT("deselected")};
        if (!States.Contains(State))
        {
            UE_LOG(LogCinderCommandPreview, Warning, TEXT("CINDERLINE_COMMAND_PREVIEW refused=invalid_state"));
            return;
        }
        auto* PC = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        if (!FApp::IsUnattended() || !World || !World->IsGameWorld() || !PC || !Battle
            || !Battle->IsMenu() || Battle->IsOnlineMatch() || PC->IsHelpOpen())
        {
            UE_LOG(LogCinderCommandPreview, Warning,
                TEXT("CINDERLINE_COMMAND_PREVIEW refused=requires_unattended_fresh_local_menu"));
            return;
        }
        if (UWorld* Previous = CommandCaptureWorld.Get())
        {
            Previous->GetTimerManager().ClearTimer(CommandPanelTimer);
            Previous->GetTimerManager().ClearTimer(CommandSecondPanelTimer);
            Previous->GetTimerManager().ClearTimer(CommandThirdPanelTimer);
            Previous->GetTimerManager().ClearTimer(CommandFourthPanelTimer);
            Previous->GetTimerManager().ClearTimer(CommandFifthPanelTimer);
            Previous->GetTimerManager().ClearTimer(CommandSixthPanelTimer);
            Previous->GetTimerManager().ClearTimer(CommandSeventhPanelTimer);
            Previous->GetTimerManager().ClearTimer(CommandCaptureTimer);
        }

        // Preview fixtures deliberately use development spawn/resources. Gameplay
        // correctness is covered separately by authoritative command regressions.
        if (PC->IsTutorialOfferPending()) PC->ExecuteAction(TEXT("onboardskip"));
        PC->ExecuteAction(TEXT("start"), 0);
        if (Battle->IsMenu())
        {
            UE_LOG(LogCinderCommandPreview, Error, TEXT("CINDERLINE_COMMAND_PREVIEW failed=start"));
            return;
        }
        cinder::Simulation& Sim = Battle->Sim();
        cinder::Config Config; Config.ai = false; Config.seed = 713;
        Sim.reset(Config);
        Sim.debugResources(0, 9000);
        cinder::Id HQ = 0;
        for (const cinder::Entity& E : Sim.entities())
            if (E.team == 0 && E.kind == cinder::Kind::Headquarters) { HQ = E.id; break; }
        cinder::Id Selected = HQ;
        cinder::Id Worker = 0;
        for (const cinder::Entity& E : Sim.entities())
            if (E.team == 0 && E.kind == cinder::Kind::Worker) { Worker = E.id; break; }
        auto Issue = [&](cinder::Command Command)
        {
            const auto Result = Battle->SubmitCommand(Command);
            if (!Result.accepted)
                UE_LOG(LogCinderCommandPreview, Error, TEXT("CINDERLINE_COMMAND_PREVIEW failed=command_%d reason=%s"),
                    static_cast<int>(Command.type), UTF8_TO_TCHAR(Result.message.c_str()));
            return Result.accepted;
        };
        cinder::Id PinnedProducer = 0;
        if (State.StartsWith(TEXT("global-")))
        {
            const auto First = Sim.debugSpawn(cinder::Kind::Foundry, 0, {1050, 650});
            Sim.debugSpawn(cinder::Kind::Foundry, 0, {1450, 650});
            Sim.debugSpawn(cinder::Kind::Laboratory, 0, {700, 1100});
            Sim.debugSpawn(cinder::Kind::Processor, 0, {1650, 1050});
            Selected = Sim.debugSpawn(cinder::Kind::Striker, 0, {1050, 1000});
            PinnedProducer = First;
            cinder::Command Train; Train.type = cinder::CommandType::AutoTrain;
            Train.kind = cinder::Kind::Striker; Train.queueIndex = 6;
            if (!Issue(Train)) return;
            cinder::Command Research; Research.type = cinder::CommandType::AutoResearch; Research.queueIndex = 1;
            if (!Issue(Research)) return;
            bool bPlaced = false;
            for (int X = 350; X <= 1400 && !bPlaced; X += 150)
                for (int Y = 350; Y <= 1400 && !bPlaced; Y += 150)
                {
                    const cinder::Vec2 Site{static_cast<float>(X), static_cast<float>(Y)};
                    if (!Sim.autoBuildStatus(0, cinder::Kind::Processor, &Site).accepted) continue;
                    cinder::Command Build; Build.type = cinder::CommandType::AutoBuild;
                    Build.kind = cinder::Kind::Processor; Build.point = Site;
                    bPlaced = Issue(Build);
                }
            if (!bPlaced)
            {
                UE_LOG(LogCinderCommandPreview, Error, TEXT("CINDERLINE_COMMAND_PREVIEW failed=global_foundation"));
                return;
            }
            for (int Step = 0; Step < 60; ++Step) Sim.update(cinder::Simulation::Step);
            if (State == TEXT("global-blocked")) Sim.debugResources(0, 0);
        }
        else if (State == TEXT("build-plan"))
        {
            // Blueprints only earn their place if they survive deselection, so
            // the fixture carries three at once: a queued chain on the drudge
            // that is selected, one queued site on a drudge that is not, and a
            // foundation already raising, where the ghost has to read against
            // the stub growing inside it.
            Sim.debugSpawn(cinder::Kind::Foundry, 0, {1050, 650});
            cinder::Id Partner = 0;
            for (const cinder::Entity& E : Sim.entities())
                if (E.team == 0 && E.kind == cinder::Kind::Worker && E.id != Worker) { Partner = E.id; break; }
            // Authored coordinates rot the moment the map generator changes, so
            // the fixture asks the simulation where each kind will actually fit.
            const auto FindSite = [&](cinder::Kind Kind, cinder::Vec2 Near, cinder::Vec2& Out)
            {
                for (int Ring = 0; Ring <= 8; ++Ring)
                    for (int Spoke = 0; Spoke < 16; ++Spoke)
                    {
                        const float Angle = Spoke * 2.0f * PI / 16.0f;
                        const cinder::Vec2 Site{Near.x + FMath::Cos(Angle) * Ring * 90.0f,
                            Near.y + FMath::Sin(Angle) * Ring * 90.0f};
                        if (!Sim.visible(0, Site) || !Sim.canPlace(0, Kind, Site)) continue;
                        Out = Site; return true;
                    }
                return false;
            };
            const cinder::Kind Plans[]{cinder::Kind::Processor, cinder::Kind::Laboratory, cinder::Kind::Turret};
            const cinder::Vec2 Anchors[]{{1420, 1080}, {1720, 1200}, {1340, 1430}};
            for (int32 Index = 0; Index < 3; ++Index)
            {
                cinder::Command Build; Build.type = cinder::CommandType::Build;
                Build.units = {Worker}; Build.kind = Plans[Index];
                Build.queueMode = cinder::CommandQueueMode::Append;
                if (!FindSite(Plans[Index], Anchors[Index], Build.point))
                {
                    UE_LOG(LogCinderCommandPreview, Error,
                        TEXT("CINDERLINE_COMMAND_PREVIEW failed=build_plan_site_%d"), Index);
                    return;
                }
                if (!Issue(Build)) return;
            }
            // Two for the partner as well: it starts the first, which leaves
            // the second queued, which is what proves a plan stays drawn on a
            // drudge nobody has selected.
            const cinder::Kind PartnerPlans[]{cinder::Kind::Foundry, cinder::Kind::Turret};
            const cinder::Vec2 PartnerAnchors[]{{880, 1330}, {640, 1180}};
            if (Partner) for (int32 Index = 0; Index < 2; ++Index)
            {
                cinder::Command Build; Build.type = cinder::CommandType::Build;
                Build.units = {Partner}; Build.kind = PartnerPlans[Index];
                Build.queueMode = cinder::CommandQueueMode::Append;
                if (!FindSite(PartnerPlans[Index], PartnerAnchors[Index], Build.point))
                {
                    UE_LOG(LogCinderCommandPreview, Error,
                        TEXT("CINDERLINE_COMMAND_PREVIEW failed=build_plan_partner_site_%d"), Index);
                    return;
                }
                if (!Issue(Build)) return;
            }
            // Long enough for the lead drudge to walk to the first site and get
            // a foundation out of the ground, short of finishing it.
            for (int Step = 0; Step < 900; ++Step) Sim.update(cinder::Simulation::Step);
            Selected = Worker;
        }
        else if (State == TEXT("queue"))
        {
            cinder::Command Train; Train.type = cinder::CommandType::Train;
            Train.units = {HQ}; Train.kind = cinder::Kind::Worker;
            for (int i = 0; i < cinder::Simulation::MaxQueue; ++i) if (!Issue(Train)) return;
        }
        else
        {
            const cinder::Id Foundry = Sim.debugSpawn(cinder::Kind::Foundry, 0, {1050, 650});
            PinnedProducer = State == TEXT("worker-rally") ? HQ : Foundry;
            Sim.debugSpawn(cinder::Kind::Laboratory, 0, {700, 1000});
            const cinder::Id Tower = Sim.debugSpawn(cinder::Kind::Turret, 0, {1050, 1100});
            Sim.debugSpawn(cinder::Kind::Processor, 0, {1500, 630});
            Sim.debugSpawn(cinder::Kind::Processor, 0, {1820, 630});
            const cinder::Kind Kinds[]{cinder::Kind::Striker, cinder::Kind::Striker, cinder::Kind::Striker,
                cinder::Kind::Lancer, cinder::Kind::Lancer, cinder::Kind::Scout,
                cinder::Kind::Bastion, cinder::Kind::Bastion, cinder::Kind::Mortar,
                cinder::Kind::Mender, cinder::Kind::Kite, cinder::Kind::Striker,
                cinder::Kind::Lancer, cinder::Kind::Scout};
            std::vector<cinder::Id> Army;
            cinder::Id Healer = 0, Mortar = 0;
            for (int i = 0; i < UE_ARRAY_COUNT(Kinds); ++i)
            {
                const cinder::Id Id = Sim.debugSpawn(Kinds[i], 0,
                    {1280.f + (i % 4) * 95.f, 870.f + (i / 4) * 100.f});
                Army.push_back(Id);
                if (Kinds[i] == cinder::Kind::Mender) Healer = Id;
                if (Kinds[i] == cinder::Kind::Mortar) Mortar = Id;
            }
            cinder::Command Hold; Hold.type = cinder::CommandType::Hold; Hold.units = Army;
            if (!Issue(Hold)) return;
            const cinder::Kind GroupKinds[]{cinder::Kind::Striker, cinder::Kind::Lancer, cinder::Kind::Bastion};
            for (int32 i = 0; i < ACinderPlayerController::SquadCount; ++i)
            {
                PC->SelectOwnedKind(GroupKinds[i]);
                if (!PC->AssignSquad(i))
                {
                    UE_LOG(LogCinderCommandPreview, Error, TEXT("CINDERLINE_COMMAND_PREVIEW failed=assign_squad"));
                    return;
                }
            }
            Selected = State == TEXT("worker-rally") ? HQ : State == TEXT("worker") ? Worker
                : State.StartsWith(TEXT("tower")) ? Tower
                : State.StartsWith(TEXT("healer")) ? Healer
                : State == TEXT("unit-combat") ? Mortar
                : State.StartsWith(TEXT("building-")) ? Foundry : Army.front();
            if (State == TEXT("defend"))
            {
                for (int32 i = 0; i < ACinderPlayerController::SquadCount; ++i)
                {
                    cinder::Command Defend; Defend.type = cinder::CommandType::Defend;
                    Defend.units = PC->Squad(i); Defend.point = {1340.f, 1360.f + i * 170.f};
                    if (!Issue(Defend)) return;
                }
                for (int i = 0; i < 800; ++i) Sim.update(cinder::Simulation::Step);
            }
        }
        Sim.update(cinder::Simulation::Step);
        Battle->ResetPresentation();
        if (State == TEXT("defend")) PC->RecallSquad(0);
        else if (State == TEXT("ribbon") || State == TEXT("deselected")) PC->SelectArmy();
        else if (State == TEXT("global-pinned")) PC->SelectOwnedEntity(PinnedProducer);
        else if (State != TEXT("idle")) PC->SelectOwnedEntity(Selected);
        else PC->SelectOwnedKind(cinder::Kind::Resource); // No owned resources: clear selection through the public selector.
        PC->Notify(FString());
        Battle->SetActorTickEnabled(false);
        Battle->RenderState();
        if (auto* Rig = Cast<ACinderCamera>(PC->GetPawn()))
        {
            const cinder::Entity* Focus = Sim.find(Selected);
            Rig->Focus(State == TEXT("queue") && Focus ? FVector(Focus->pos.x, Focus->pos.y, 0)
                : State == TEXT("defend") ? FVector(1250, 1430, 0)
                // Centred on the plan rather than the base, so the frame shows
                // the queued ghosts and not the structures already standing.
                : State == TEXT("build-plan") ? FVector(1320, 1220, 0)
                : FVector(1160, 1010, 0), true);
        }

        CommandCaptureWorld = World;
        const TWeakObjectPtr<ACinderPlayerController> WeakPC(PC);
        auto Tap = [WeakPC](const FString& Action, TOptional<int32> Argument = {},
            TOptional<cinder::Id> Entity = {})
        {
            auto* CurrentPC = WeakPC.Get();
            auto* HUD = CurrentPC ? Cast<ACinderHUD>(CurrentPC->GetHUD()) : nullptr;
            if (!HUD || !HUD->TapPreviewAction(Action, Argument, Entity))
            {
                UE_LOG(LogCinderCommandPreview, Error, TEXT("CINDERLINE_COMMAND_PREVIEW failed=button_%s"), *Action);
                return false;
            }
            return true;
        };
        World->GetTimerManager().SetTimer(CommandPanelTimer,
            FTimerDelegate::CreateLambda([Tap, State, PinnedProducer]
        {
            if (State.StartsWith(TEXT("global-")))
            {
                if (State == TEXT("global-jobs") || State == TEXT("global-rally") || State == TEXT("global-construction")) Tap(TEXT("army"));
                else Tap(TEXT("globalcatalog"), TOptional<int32>(State == TEXT("global-build") ? 7
                    : State == TEXT("global-research") ? 9 : 8));
            }
            else if (State == TEXT("army") || State == TEXT("squads") || State == TEXT("roster-page")
                || State.StartsWith(TEXT("rally-"))) Tap(TEXT("army"));
            else if (State == TEXT("worker-rally"))
                Tap(TEXT("globalcatalog"), TOptional<int32>(8), TOptional<cinder::Id>(PinnedProducer));
            else if (State == TEXT("deselected")) Tap(TEXT("deselect"));
            else if (State.EndsWith(TEXT("-info")) || State.EndsWith(TEXT("-guide"))
                || State.EndsWith(TEXT("-combat"))) Tap(TEXT("info"));
            else if (State == TEXT("orders")) Tap(TEXT("orders"));
            else if (State == TEXT("queue")) Tap(TEXT("sheet"), TOptional<int32>(3));
        }), 0.7f, false);
        World->GetTimerManager().SetTimer(CommandSecondPanelTimer,
            FTimerDelegate::CreateLambda([Tap, WeakPC, State, PinnedProducer]
        {
            if (State == TEXT("global-jobs") || State == TEXT("global-rally") || State == TEXT("global-construction")) Tap(TEXT("armytab"), TOptional<int32>(2));
            else if (State == TEXT("global-train")) Tap(TEXT("trainqty"), TOptional<int32>(6));
            else if (State.StartsWith(TEXT("rally-"))) Tap(TEXT("armytab"), TOptional<int32>(3));
            else if (State == TEXT("worker-rally"))
            {
                auto* CurrentPC = WeakPC.Get();
                auto* CurrentBattle = CurrentPC ? CurrentPC->Battlefield() : nullptr;
                const cinder::Vec2 Point{1100, 1400};
                std::string Reason;
                if (!CurrentBattle || !CurrentBattle->Sim().visible(0, Point)
                    || !CurrentBattle->Sim().canPlace(0, cinder::Kind::Turret, Point, &Reason))
                {
                    UE_LOG(LogCinderCommandPreview, Error,
                        TEXT("CINDERLINE_COMMAND_PREVIEW failed=worker_rally_point reason=%s"),
                        UTF8_TO_TCHAR(Reason.c_str()));
                    return;
                }
                if (!Tap(TEXT("productionrally"), TOptional<int32>{},
                    TOptional<cinder::Id>(PinnedProducer))) return;
                CurrentPC->ExecuteAction(TEXT("minimap"), 1400 * 10000 + 1100);
            }
            else if (State == TEXT("squads")) Tap(TEXT("armytab"), TOptional<int32>(1));
            else if (State == TEXT("roster-page")) Tap(TEXT("rosterpage"), TOptional<int32>(1));
            else if (State.EndsWith(TEXT("-guide")) || State.EndsWith(TEXT("-combat"))) Tap(TEXT("infotab"), TOptional<int32>(1));
        }), 1.2f, false);
        World->GetTimerManager().SetTimer(CommandThirdPanelTimer, FTimerDelegate::CreateLambda([Tap, State]
        {
            if (State == TEXT("global-rally")) Tap(TEXT("productionrally"));
            else if (State == TEXT("global-construction"))
                for (int Page = 0; Page < 3; ++Page) Tap(TEXT("jobpage"), TOptional<int32>(1));
            else if (State == TEXT("global-train")) Tap(TEXT("globaltrain"));
            else if (State.EndsWith(TEXT("-combat"))) Tap(TEXT("infotab"), TOptional<int32>(2));
        }), 1.6f, false);
        World->GetTimerManager().SetTimer(CommandFourthPanelTimer,
            FTimerDelegate::CreateLambda([Tap, WeakPC, State, PinnedProducer]
        {
            if (State == TEXT("rally-set") || State == TEXT("rally-marker") || State == TEXT("rally-override"))
            {
                auto* CurrentPC = WeakPC.Get();
                auto* CurrentBattle = CurrentPC ? CurrentPC->Battlefield() : nullptr;
                const cinder::Vec2 Point{1450, 1400};
                std::string Reason;
                if (!CurrentBattle || !CurrentBattle->Sim().visible(0, Point)
                    || !CurrentBattle->Sim().canPlace(0, cinder::Kind::Turret, Point, &Reason))
                {
                    UE_LOG(LogCinderCommandPreview, Error,
                        TEXT("CINDERLINE_COMMAND_PREVIEW failed=default_rally_point reason=%s"),
                        UTF8_TO_TCHAR(Reason.c_str()));
                    return;
                }
                if (!Tap(TEXT("productionrally"))) return;
                CurrentPC->ExecuteAction(TEXT("minimap"), 1400 * 10000 + 1450);
            }
            else if (State == TEXT("worker-rally"))
                Tap(TEXT("globalcatalog"), TOptional<int32>(8), TOptional<cinder::Id>(PinnedProducer));
        }), 2.0f, false);
        World->GetTimerManager().SetTimer(CommandFifthPanelTimer,
            FTimerDelegate::CreateLambda([Tap, WeakPC, State, PinnedProducer]
        {
            if (State == TEXT("rally-set") || State == TEXT("rally-marker")) Tap(TEXT("army"));
            else if (State == TEXT("worker-rally"))
                Tap(TEXT("producerjobs"), TOptional<int32>{}, TOptional<cinder::Id>(PinnedProducer));
            else if (State == TEXT("rally-override"))
            {
                auto* CurrentPC = WeakPC.Get();
                if (!CurrentPC || !CurrentPC->SelectOwnedEntity(PinnedProducer))
                    UE_LOG(LogCinderCommandPreview, Error,
                        TEXT("CINDERLINE_COMMAND_PREVIEW failed=select_rally_producer"));
            }
        }), 2.4f, false);
        World->GetTimerManager().SetTimer(CommandSixthPanelTimer,
            FTimerDelegate::CreateLambda([Tap, State, PinnedProducer]
        {
            if (State == TEXT("rally-set") || State == TEXT("rally-marker"))
                Tap(TEXT("armytab"), TOptional<int32>(3));
            else if (State == TEXT("rally-override"))
                Tap(TEXT("globalcatalog"), TOptional<int32>(8), TOptional<cinder::Id>(PinnedProducer));
        }), 2.8f, false);
        World->GetTimerManager().SetTimer(CommandSeventhPanelTimer,
            FTimerDelegate::CreateLambda([WeakPC, State, PinnedProducer]
        {
            auto* CurrentPC = WeakPC.Get();
            auto* HUD = CurrentPC ? Cast<ACinderHUD>(CurrentPC->GetHUD()) : nullptr;
            if (!CurrentPC || !HUD) return;
            if (State == TEXT("rally-marker"))
            {
                if (!HUD->TapPreviewAction(TEXT("rallyfocus")))
                    UE_LOG(LogCinderCommandPreview, Error,
                        TEXT("CINDERLINE_COMMAND_PREVIEW failed=button_rallyfocus"));
            }
            else if (State == TEXT("rally-override"))
            {
                if (HUD->TapPreviewAction(TEXT("productionrally"), TOptional<int32>{},
                    TOptional<cinder::Id>(PinnedProducer)))
                {
                    const cinder::Vec2 Point{1780, 1420};
                    auto* CurrentBattle = CurrentPC->Battlefield();
                    std::string Reason;
                    if (!CurrentBattle || !CurrentBattle->Sim().visible(0, Point)
                        || !CurrentBattle->Sim().canPlace(0, cinder::Kind::Turret, Point, &Reason))
                    {
                        UE_LOG(LogCinderCommandPreview, Error,
                            TEXT("CINDERLINE_COMMAND_PREVIEW failed=local_rally_point reason=%s"),
                            UTF8_TO_TCHAR(Reason.c_str()));
                        return;
                    }
                    CurrentPC->ExecuteAction(TEXT("minimap"), 1420 * 10000 + 1780);
                    if (!HUD->TapPreviewAction(TEXT("producerjobs"), TOptional<int32>{},
                        TOptional<cinder::Id>(PinnedProducer)))
                        UE_LOG(LogCinderCommandPreview, Error,
                            TEXT("CINDERLINE_COMMAND_PREVIEW failed=button_producerjobs"));
                }
                else UE_LOG(LogCinderCommandPreview, Error,
                    TEXT("CINDERLINE_COMMAND_PREVIEW failed=button_productionrally"));
            }
        }), 3.2f, false);
        World->GetTimerManager().SetTimer(CommandCaptureTimer, FTimerDelegate::CreateLambda([WeakPC, State, PinnedProducer]
        {
            auto* CurrentPC = WeakPC.Get();
            auto* CurrentBattle = CurrentPC ? CurrentPC->Battlefield() : nullptr;
            auto* HUD = CurrentPC ? Cast<ACinderHUD>(CurrentPC->GetHUD()) : nullptr;
            if (!CurrentBattle || CurrentBattle->IsOnlineMatch() || !HUD) return;
            HUD->LogMobileLayout();
            const FString Directory = FPaths::ProjectSavedDir() / TEXT("ArmyCommand");
            IFileManager::Get().MakeDirectory(*Directory, true);
            FScreenshotRequest::RequestScreenshot(Directory / (State + TEXT(".png")), false, false);
            UE_LOG(LogCinderCommandPreview, Display,
                TEXT("CINDERLINE_COMMAND_PREVIEW state=%s selection=%d squad_a=%d squad_b=%d squad_c=%d tick=%llu"),
                *State, static_cast<int>(CurrentPC->Selection().size()),
                static_cast<int>(CurrentPC->Squad(0).size()), static_cast<int>(CurrentPC->Squad(1).size()),
                static_cast<int>(CurrentPC->Squad(2).size()), CurrentBattle->Sim().tick());
            if (State.StartsWith(TEXT("rally-")) || State == TEXT("worker-rally"))
            {
                const auto& Player = CurrentBattle->Sim().players()[0];
                const cinder::Entity* Producer = PinnedProducer
                    ? CurrentBattle->Sim().find(PinnedProducer) : nullptr;
                FVector2D RallyScreen;
                int32 ViewWidth = 0, ViewHeight = 0;
                CurrentPC->GetViewportSize(ViewWidth, ViewHeight);
                const bool bMarkerVisible = Player.armyRallySet
                    && CurrentPC->ProjectWorldLocationToScreen(
                        FVector(Player.armyRally.x, Player.armyRally.y, 10), RallyScreen)
                    && RallyScreen.X >= 0 && RallyScreen.Y >= 0
                    && RallyScreen.X <= ViewWidth && RallyScreen.Y <= ViewHeight;
                UE_LOG(LogCinderCommandPreview, Display,
                    TEXT("CINDERLINE_ARMY_RALLY_PREVIEW state=%s army_set=%d army_x=%.0f army_y=%.0f producer=%u override=%d local_x=%.0f local_y=%.0f rally_armed=%d marker_visible=%d"),
                    *State, Player.armyRallySet, Player.armyRally.x, Player.armyRally.y,
                    PinnedProducer, Producer && Producer->rallyOverride,
                    Producer ? Producer->rally.x : 0.0f, Producer ? Producer->rally.y : 0.0f,
                    CurrentPC->IsProductionRallyMode(), bMarkerVisible);
            }
            if (State == TEXT("build-plan"))
            {
                int32 PlannedSites = 0, Foundations = 0, PlanningWorkers = 0;
                for (const auto& Entity : CurrentBattle->Sim().entities())
                {
                    if (!Entity.alive() || Entity.team != 0) continue;
                    if (cinder::definition(Entity.kind).building && Entity.progress < 1) ++Foundations;
                    if (Entity.kind != cinder::Kind::Worker) continue;
                    int32 Sites = 0;
                    for (const auto& Planned : Entity.futureOrders)
                        if (Planned.order == cinder::Order::Construct
                            && Planned.buildingKind != cinder::Kind::Worker && !Planned.supportTarget) ++Sites;
                    PlannedSites += Sites;
                    if (Sites > 0) ++PlanningWorkers;
                }
                UE_LOG(LogCinderCommandPreview, Display,
                    TEXT("CINDERLINE_BUILD_PLAN_PREVIEW state=%s planned_sites=%d planning_workers=%d foundations=%d selection=%d"),
                    *State, PlannedSites, PlanningWorkers, Foundations,
                    static_cast<int>(CurrentPC->Selection().size()));
                if (PlannedSites < 2 || PlanningWorkers < 2 || Foundations < 1)
                    UE_LOG(LogCinderCommandPreview, Error,
                        TEXT("CINDERLINE_COMMAND_PREVIEW failed=build_plan_fixture"));
            }
            if (State.StartsWith(TEXT("global-")))
            {
                int32 Queued = 0, Producers = 0;
                for (const auto& Entity : CurrentBattle->Sim().entities())
                    if (Entity.alive() && Entity.team == 0 && !Entity.queue.empty())
                    { Queued += static_cast<int32>(Entity.queue.size()); ++Producers; }
                const auto& Commands = CurrentBattle->Sim().recording();
                const bool bBatch = !Commands.empty() && Commands.back().command.type == cinder::CommandType::AutoTrain
                    && Commands.back().command.queueIndex == 6 && Commands.back().command.units.empty();
                UE_LOG(LogCinderCommandPreview, Display,
                    TEXT("CINDERLINE_GLOBAL_COMMAND_UI state=%s jobs=%d producers=%d batch_six=%d rally_armed=%d"),
                    *State, Queued, Producers, bBatch, CurrentPC->IsProductionRallyMode());
                if (State == TEXT("global-train") && (!bBatch || Queued != 13 || Producers != 3))
                    UE_LOG(LogCinderCommandPreview, Error, TEXT("CINDERLINE_COMMAND_PREVIEW failed=ui_batch_allocation"));
            }
        }), 3.7f, false);
    }));
}

#endif
