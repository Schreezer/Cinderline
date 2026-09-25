#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogCinderArtPreview, Log, All);

namespace
{
constexpr std::uint32_t ArtPreviewSeed = 0x415254;
constexpr float BaseCaptureDelay = 3.0f;
constexpr float BattleCaptureDelay = 4.0f;
constexpr cinder::Vec2 WesternCliffCenter{1320, 2400};
constexpr cinder::Vec2 WesternCliffHalf{380, 140};

TWeakObjectPtr<UWorld> ArtPreviewWorld;
TWeakObjectPtr<ACinderBattlefield> ArtPreviewBattle;
FTimerHandle ArtPreviewCaptureTimer;
FTimerHandle ArtPreviewStageTimer;
FTimerHandle ArtPreviewBattleCaptureTimer;
FTimerHandle ArtPreviewResetTimer;
float BaseStagedAt = 0;
float BattleStagedAt = 0;

void FinishArtPreviewCompilation(const FString& Mode)
{
#if WITH_EDITOR
    const int32 ShaderJobsBefore = GShaderCompilingManager
        ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
    const int32 MeshesBefore = FStaticMeshCompilingManager::Get().GetNumRemainingMeshes();
    FStaticMeshCompilingManager::Get().FinishAllCompilation();
    // Mesh finalization can enqueue material-usage permutations, so shaders
    // finish last and the requested frame cannot observe their gray fallback.
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    const int32 ShaderJobsAfter = GShaderCompilingManager
        ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
    const int32 MeshesAfter = FStaticMeshCompilingManager::Get().GetNumRemainingMeshes();
    UE_LOG(LogCinderArtPreview, Display,
        TEXT("CINDERLINE_ART_PREVIEW_READY mode=%s shader_jobs=%d->%d static_meshes=%d->%d"),
        *Mode, ShaderJobsBefore, ShaderJobsAfter, MeshesBefore, MeshesAfter);
#else
    (void)Mode;
#endif
}

void LogArtPreviewViewport(UWorld* World, const FString& Mode)
{
    UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
    const FEngineShowFlags* Flags = Viewport ? Viewport->GetEngineShowFlags() : nullptr;
    UE_LOG(LogCinderArtPreview, Display,
        TEXT("CINDERLINE_ART_PREVIEW_VIEW mode=%s viewport=%s view_mode=%d materials=%d lighting=%d post_processing=%d"),
        *Mode, Viewport ? TEXT("ready") : TEXT("missing"), Viewport ? Viewport->ViewModeIndex : -1,
        Flags ? static_cast<int32>(Flags->Materials) : -1,
        Flags ? static_cast<int32>(Flags->Lighting) : -1,
        Flags ? static_cast<int32>(Flags->PostProcessing) : -1);
}

ACinderBattlefield* FindBattlefield(UWorld* World)
{
    if (!World) return nullptr;
    TActorIterator<ACinderBattlefield> It(World);
    return It ? *It : nullptr;
}

ACinderPlayerController* FindPlayer(UWorld* World)
{
    return World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
}

void ClearArtPreviewTimers()
{
    if (UWorld* World = ArtPreviewWorld.Get())
    {
        FTimerManager& Timers = World->GetTimerManager();
        Timers.ClearTimer(ArtPreviewCaptureTimer);
        Timers.ClearTimer(ArtPreviewStageTimer);
        Timers.ClearTimer(ArtPreviewBattleCaptureTimer);
        Timers.ClearTimer(ArtPreviewResetTimer);
    }
    ArtPreviewWorld.Reset();
    ArtPreviewBattle.Reset();
    BaseStagedAt = 0;
    BattleStagedAt = 0;
}

bool IsArtFixture(const ACinderBattlefield* Battle)
{
    return Battle && !Battle->IsMenu() && !Battle->IsOnlineMatch()
        && !Battle->Sim().config().ai && Battle->Sim().config().map == 0
        && Battle->Sim().config().seed == ArtPreviewSeed;
}

bool RefuseOnline(ACinderBattlefield* Battle, ACinderPlayerController* PC)
{
    if (!Battle || !Battle->IsOnlineMatch()) return false;
    UE_LOG(LogCinderArtPreview, Warning,
        TEXT("CINDERLINE_ART_PREVIEW refused=online_match; leave the online match before replacing local state"));
    if (PC) PC->Notify(TEXT("Art preview refused during an online match."));
    return true;
}

void ReturnToFreshMenu(UWorld* World, bool bOnlyIfFixture)
{
    ACinderBattlefield* Battle = FindBattlefield(World);
    ACinderPlayerController* PC = FindPlayer(World);
    if (!Battle || RefuseOnline(Battle, PC)) return;
    if (bOnlyIfFixture && !IsArtFixture(Battle))
    {
        UE_LOG(LogCinderArtPreview, Warning,
            TEXT("CINDERLINE_ART_PREVIEW sweep_stop=state_changed; current local state left untouched"));
        ClearArtPreviewTimers();
        return;
    }
    ClearArtPreviewTimers();
    Battle->SetActorTickEnabled(true);
    if (PC)
    {
        PC->ExecuteAction(TEXT("start"), 0);
        PC->ExecuteAction(TEXT("menu"));
        PC->Notify(TEXT("ART PREVIEW CLEARED | fresh skirmish menu"));
    }
    else
    {
        Battle->StartMatch(0);
        Battle->ReturnToMenu();
    }
    UE_LOG(LogCinderArtPreview, Display,
        TEXT("CINDERLINE_ART_PREVIEW reset=fresh_menu fixture_cleared=1; no save written"));
}

bool OutsideObstacles(const cinder::Simulation& Sim, cinder::Kind Kind, cinder::Vec2 Point)
{
    const float Radius = cinder::definition(Kind).radius;
    for (const cinder::Obstacle& Obstacle : Sim.obstacles())
    {
        const float DX = FMath::Max(FMath::Abs(Point.x - Obstacle.center.x) - Obstacle.half.x, 0.0f);
        const float DY = FMath::Max(FMath::Abs(Point.y - Obstacle.center.y) - Obstacle.half.y, 0.0f);
        if (DX * DX + DY * DY < Radius * Radius) return false;
    }
    return true;
}

bool HasWesternCliff(const cinder::Simulation& Sim)
{
    for (const cinder::Obstacle& Obstacle : Sim.obstacles())
        if (FMath::IsNearlyEqual(Obstacle.center.x, WesternCliffCenter.x)
            && FMath::IsNearlyEqual(Obstacle.center.y, WesternCliffCenter.y)
            && FMath::IsNearlyEqual(Obstacle.half.x, WesternCliffHalf.x)
            && FMath::IsNearlyEqual(Obstacle.half.y, WesternCliffHalf.y)) return true;
    return false;
}

void FrameArtPreview(ACinderPlayerController* PC, const cinder::Simulation& Sim, bool bBattle)
{
    ACinderCamera* Rig = PC ? Cast<ACinderCamera>(PC->GetPawn()) : nullptr;
    if (!Rig || !PC->PlayerCameraManager) return;
    int32 Width = 0, Height = 0;
    PC->GetViewportSize(Width, Height);
    if (Width <= 0 || Height <= 0) return;

    TArray<FVector> Points;
    FBox2D GroundBounds(ForceInit);
    for (const cinder::Entity& Entity : Sim.entities())
    {
        const float MinimumX = bBattle ? 580.0f : 800.0f;
        if (!Entity.alive() || Entity.pos.x < MinimumX || Entity.pos.x > 2160
            || Entity.pos.y < (bBattle ? 2740.0f : 2200.0f) || Entity.pos.y > 3380) continue;
        const cinder::Definition& Definition = cinder::definition(Entity.kind);
        const float Top = Definition.building ? 220.0f : Definition.air ? 170.0f : 85.0f;
        Points.Add(FVector(Entity.pos.x - Definition.radius, Entity.pos.y - Definition.radius, 0));
        Points.Add(FVector(Entity.pos.x + Definition.radius, Entity.pos.y + Definition.radius, 0));
        Points.Add(FVector(Entity.pos.x, Entity.pos.y, Top));
        GroundBounds += FVector2D(Entity.pos.x - Definition.radius, Entity.pos.y - Definition.radius);
        GroundBounds += FVector2D(Entity.pos.x + Definition.radius, Entity.pos.y + Definition.radius);
    }
    // The battle frame follows the active approach; distant cliff corners must not shrink the army.
    if (!bBattle)
    for (const float X : {WesternCliffCenter.x - WesternCliffHalf.x,
                          WesternCliffCenter.x + WesternCliffHalf.x})
        for (const float Y : {WesternCliffCenter.y - WesternCliffHalf.y,
                              WesternCliffCenter.y + WesternCliffHalf.y})
        {
            Points.Add(FVector(X, Y, 0));
            Points.Add(FVector(X, Y, 220));
            GroundBounds += FVector2D(X, Y);
        }
    if (Points.IsEmpty() || !GroundBounds.bIsValid) return;

    FVector Focus(GroundBounds.GetCenter(), 0);
    const float InitialDistance = bBattle ? 2050.0f : 1850.0f;
    Rig->Zoom(InitialDistance - Rig->Distance());
    Rig->Focus(Focus, true);
    const FVector2D Low(Width * 0.08f, Height * 0.09f);
    const FVector2D High(Width * 0.89f, Height * 0.80f);
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
        const FVector2D Available = High - Low;
        const float Fit = FMath::Max(
            static_cast<float>(Projected.GetSize().X / Available.X),
            static_cast<float>(Projected.GetSize().Y / Available.Y));
        const float DesiredDistance = FMath::Clamp(Rig->Distance() * Fit * 1.035f, 1300.0f, 2400.0f);
        if (!FMath::IsNearlyEqual(DesiredDistance, Rig->Distance(), 2.0f))
        {
            Rig->Zoom(DesiredDistance - Rig->Distance());
            Rig->Focus(Focus, true);
            continue;
        }
        const FVector2D Offset = Projected.GetCenter() - (Low + High) * 0.5f;
        if (Offset.SizeSquared() < 4) break;
        const double Scale = 2 * Rig->Distance() * FMath::Tan(FMath::DegreesToRadians(26.0)) / Width;
        const FVector Right(0.70710678, 0.70710678, 0);
        const FVector GroundUp(0.70710678, -0.70710678, 0);
        Focus += Right * (Offset.X * Scale) - GroundUp * (Offset.Y * Scale / 0.8660254);
        Rig->Focus(Focus, true);
    }
    PC->PlayerCameraManager->UpdateCamera(0);
    UE_LOG(LogCinderArtPreview, Display,
        TEXT("CINDERLINE_ART_PREVIEW_FRAME mode=%s viewport=%dx%d distance=%.1f focus=(%.1f,%.1f) target_screen=(%.0f,%.0f)-(%.0f,%.0f) projected=(%.0f,%.0f)-(%.0f,%.0f)"),
        bBattle ? TEXT("battle") : TEXT("base"), Width, Height, Rig->Distance(),
        Rig->GetActorLocation().X, Rig->GetActorLocation().Y,
        Low.X, Low.Y, High.X, High.Y, Projected.Min.X, Projected.Min.Y,
        Projected.Max.X, Projected.Max.Y);
}

cinder::CommandResult Issue(cinder::Simulation& Sim, const std::vector<cinder::Id>& Units,
    cinder::CommandType Type, cinder::Vec2 Point = {}, cinder::Id Target = 0)
{
    cinder::Command Command;
    Command.team = Units.empty() || !Sim.find(Units.front()) ? 0 : Sim.find(Units.front())->team;
    Command.units = Units;
    Command.type = Type;
    Command.point = Point;
    Command.target = Target;
    return Sim.command(Command);
}

bool StageBase(UWorld* World)
{
    ACinderBattlefield* Battle = FindBattlefield(World);
    ACinderPlayerController* PC = FindPlayer(World);
    if (!World || !Battle || RefuseOnline(Battle, PC)) return false;

    // A profile that has never finished the tutorial has a pending onboarding
    // offer, and that gate swallows every action except its own three answers —
    // including "start". Staging would then dress the simulation while the
    // battlefield quietly stayed in the menu, and the capture would abort later
    // with "fixture changed" pointing nowhere near the cause. Decline it first:
    // this is a development capture path, and a first-run prompt is not what it
    // is here to photograph.
    if (PC && PC->IsTutorialOfferPending()) PC->ExecuteAction(TEXT("onboardskip"));
    if (PC) PC->ExecuteAction(TEXT("start"), 0);
    else Battle->StartMatch(0);
    if (Battle->IsMenu())
    {
        UE_LOG(LogCinderArtPreview, Error,
            TEXT("CINDERLINE_ART_PREVIEW refused=start_rejected; the battlefield stayed in the menu, so an action gate swallowed \"start\""));
        if (PC) PC->Notify(TEXT("Art preview failed: the match would not start."));
        return false;
    }
    cinder::Simulation& Sim = Battle->Sim();
    cinder::Config Config;
    Config.map = 0;
    Config.ai = false;
    Config.seed = ArtPreviewSeed;
    Sim.reset(Config);
    Sim.debugResources(0, 12000);
    Battle->ResetPresentation();
    if (!HasWesternCliff(Sim))
    {
        UE_LOG(LogCinderArtPreview, Error,
            TEXT("CINDERLINE_ART_PREVIEW refused=map_contract_changed expected_cliff=(1320,2400,380,140)"));
        if (PC) PC->Notify(TEXT("Art preview failed: map-zero western cliff changed."));
        return false;
    }

    int32 BuildingCount = 0;
    int32 MiningCount = 0;
    int32 ArmyCount = 0;
    auto SpawnChecked = [&](cinder::Kind Kind, int32 Team, cinder::Vec2 Point)
    {
        if (!OutsideObstacles(Sim, Kind, Point))
        {
            UE_LOG(LogCinderArtPreview, Error,
                TEXT("CINDERLINE_ART_PREVIEW invalid_spawn kind=%s point=(%.0f,%.0f) reason=obstacle_overlap"),
                UTF8_TO_TCHAR(cinder::definition(Kind).name), Point.x, Point.y);
            return static_cast<cinder::Id>(0);
        }
        return Sim.debugSpawn(Kind, Team, Point);
    };

    // The scout reveals the actual western cliff, its north clearing, and the
    // nearby map-authored ore. No fog or terrain state is edited directly.
    const cinder::Id VisionScout = SpawnChecked(cinder::Kind::Scout, 0, {1320, 2690});
    if (!VisionScout || !Sim.explored(0, WesternCliffCenter)) return false;
    Issue(Sim, {VisionScout}, cinder::CommandType::Hold);

    auto SpawnBuilding = [&](cinder::Kind Kind, cinder::Vec2 Point)
    {
        std::string Reason;
        if (!Sim.canPlace(0, Kind, Point, &Reason))
        {
            UE_LOG(LogCinderArtPreview, Error,
                TEXT("CINDERLINE_ART_PREVIEW invalid_building kind=%s point=(%.0f,%.0f) reason=%s"),
                UTF8_TO_TCHAR(cinder::definition(Kind).name), Point.x, Point.y,
                UTF8_TO_TCHAR(Reason.c_str()));
            return static_cast<cinder::Id>(0);
        }
        const cinder::Id Id = SpawnChecked(Kind, 0, Point);
        if (Id) ++BuildingCount;
        return Id;
    };

    const cinder::Id Anchor = SpawnBuilding(cinder::Kind::Headquarters, {1500, 2920});
    const cinder::Id Kiln = SpawnBuilding(cinder::Kind::Foundry, {1770, 2860});
    const cinder::Id Crucible = SpawnBuilding(cinder::Kind::MotorPool, {1860, 3130});
    const cinder::Id Siphon = SpawnBuilding(cinder::Kind::Processor, {1240, 3180});
    const cinder::Id Resonator = SpawnBuilding(cinder::Kind::Laboratory, {1540, 3210});
    const cinder::Id Ward = SpawnBuilding(cinder::Kind::Turret, {2050, 2920});
    if (!Anchor || !Kiln || !Crucible || !Siphon || !Resonator || !Ward) return false;

    // Use three deposits from the real map-zero northern ore cluster. Gather
    // commands must pass ordinary visibility and target validation.
    TArray<cinder::Id> Deposits;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.kind == cinder::Kind::Resource && Entity.resource > 0
            && Entity.pos.x >= 850 && Entity.pos.x <= 1150
            && Entity.pos.y >= 2750 && Entity.pos.y <= 2940) Deposits.Add(Entity.id);
    Deposits.Sort([&Sim](cinder::Id Left, cinder::Id Right)
    {
        const cinder::Entity* A = Sim.find(Left);
        const cinder::Entity* B = Sim.find(Right);
        return A && B ? A->pos.x < B->pos.x : Left < Right;
    });
    for (int32 Index = 0; Index < FMath::Min(3, Deposits.Num()); ++Index)
    {
        const cinder::Id DepositId = Deposits[Index];
        const cinder::Entity* Deposit = Sim.find(DepositId);
        if (!Deposit) continue;
        const cinder::Vec2 DepositPoint = Deposit->pos;
        const float Side = Index == 0 ? -1.0f : 1.0f;
        const cinder::Vec2 WorkerPoint{DepositPoint.x + Side * 72.0f, DepositPoint.y + 66.0f};
        const cinder::Id Worker = SpawnChecked(cinder::Kind::Worker, 0, WorkerPoint);
        const cinder::CommandResult Gather = Issue(Sim, {Worker}, cinder::CommandType::Gather, {}, DepositId);
        if (!Worker || !Gather.accepted)
        {
            UE_LOG(LogCinderArtPreview, Error,
                TEXT("CINDERLINE_ART_PREVIEW gather_failed deposit=%u reason=%s"),
                DepositId, UTF8_TO_TCHAR(Gather.message.c_str()));
            return false;
        }
        ++MiningCount;
    }
    if (MiningCount != 3) return false;

    const TPair<cinder::Kind, cinder::Vec2> Army[] = {
        {cinder::Kind::Striker, cinder::Vec2{1040, 3030}},
        {cinder::Kind::Lancer, cinder::Vec2{1100, 3070}},
        {cinder::Kind::Scout, cinder::Vec2{980, 3100}},
        {cinder::Kind::Bastion, cinder::Vec2{1120, 3140}},
        {cinder::Kind::Mortar, cinder::Vec2{1180, 3000}},
        {cinder::Kind::Mender, cinder::Vec2{1040, 3190}},
        {cinder::Kind::Kite, cinder::Vec2{970, 2970}}
    };
    std::vector<cinder::Id> Defenders;
    for (const auto& Entry : Army)
    {
        const cinder::Id Id = SpawnChecked(Entry.Key, 0, Entry.Value);
        if (!Id) return false;
        Defenders.push_back(Id);
        ++ArmyCount;
    }
    if (!Issue(Sim, Defenders, cinder::CommandType::Hold).accepted) return false;

    FrameArtPreview(PC, Sim, false);
    Battle->ResetFeedback();
    Battle->RenderState();
    Battle->SetPaused(false);
    Battle->SetActorTickEnabled(true);
    ArtPreviewWorld = World;
    ArtPreviewBattle = Battle;
    BaseStagedAt = World->GetTimeSeconds();
    if (PC) PC->Notify(TEXT("ART PREVIEW: INDUSTRIAL BASE | development fixture, never saved"));
    UE_LOG(LogCinderArtPreview, Display,
        TEXT("CINDERLINE_ART_PREVIEW mode=base fixture=development_unsaved seed=%u buildings=%d mining_workers=%d army=%d cliff=(1320,2400,380,140) camera=projected_fit_1300_2400; ordinary simulation and fog, no save written"),
        ArtPreviewSeed, BuildingCount, MiningCount, ArmyCount);
    return true;
}

bool StageBattleRaid(UWorld* World)
{
    ACinderBattlefield* Battle = FindBattlefield(World);
    ACinderPlayerController* PC = FindPlayer(World);
    auto Abort = [&](const FString& Reason)
    {
        UE_LOG(LogCinderArtPreview, Warning,
            TEXT("CINDERLINE_ART_PREVIEW battle_aborted reason=%s; base fixture remains active and unsaved"),
            *Reason);
        if (PC) PC->Notify(FString::Printf(
            TEXT("ART PREVIEW BATTLE ABORTED: %s | base fixture remains unsaved"), *Reason));
        return false;
    };
    if (RefuseOnline(Battle, PC)) return false;
    if (!IsArtFixture(Battle)) return Abort(TEXT("fixture changed before raid staging"));
    cinder::Simulation& Sim = Battle->Sim();
    const TPair<cinder::Kind, cinder::Vec2> Raid[] = {
        {cinder::Kind::Striker, cinder::Vec2{650, 3070}},
        {cinder::Kind::Striker, cinder::Vec2{720, 3130}},
        {cinder::Kind::Lancer, cinder::Vec2{790, 3070}},
        {cinder::Kind::Scout, cinder::Vec2{670, 3230}},
        {cinder::Kind::Bastion, cinder::Vec2{820, 3200}},
        {cinder::Kind::Mortar, cinder::Vec2{740, 3280}},
        {cinder::Kind::Kite, cinder::Vec2{850, 3110}}
    };
    std::vector<cinder::Id> Raiders;
    for (const auto& Entry : Raid)
    {
        if (!OutsideObstacles(Sim, Entry.Key, Entry.Value))
            return Abort(FString::Printf(TEXT("%s spawn overlaps map obstacle at %.0f,%.0f"),
                UTF8_TO_TCHAR(cinder::definition(Entry.Key).name), Entry.Value.x, Entry.Value.y));
        const cinder::Id Id = Sim.debugSpawn(Entry.Key, 1, Entry.Value);
        if (!Id) return Abort(FString::Printf(TEXT("%s debug spawn failed at %.0f,%.0f"),
            UTF8_TO_TCHAR(cinder::definition(Entry.Key).name), Entry.Value.x, Entry.Value.y));
        Raiders.push_back(Id);
    }
    Sim.update(cinder::Simulation::Step);
    const cinder::CommandResult Attack = Issue(Sim, Raiders, cinder::CommandType::AttackMove, {1200, 3050});
    std::vector<cinder::Id> Defenders;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.team == 0 && Entity.alive() && !cinder::definition(Entity.kind).building
            && Entity.kind != cinder::Kind::Resource && Entity.kind != cinder::Kind::Worker
            && Entity.pos.x >= 900 && Entity.pos.x <= 1220
            && Entity.pos.y >= 2920 && Entity.pos.y <= 3240) Defenders.push_back(Entity.id);
    const cinder::CommandResult Counter = Issue(Sim, Defenders, cinder::CommandType::AttackMove, {850, 3120});
    if (!Attack.accepted || !Counter.accepted)
    {
        UE_LOG(LogCinderArtPreview, Error,
            TEXT("CINDERLINE_ART_PREVIEW battle_order_failed attack=%s counter=%s"),
            UTF8_TO_TCHAR(Attack.message.c_str()), UTF8_TO_TCHAR(Counter.message.c_str()));
        return Abort(FString::Printf(TEXT("attack orders rejected: raid=%s counter=%s"),
            UTF8_TO_TCHAR(Attack.message.c_str()), UTF8_TO_TCHAR(Counter.message.c_str())));
    }
    FrameArtPreview(PC, Sim, true);
    Battle->RenderState();
    Battle->SetActorTickEnabled(true);
    BattleStagedAt = World->GetTimeSeconds();
    if (PC) PC->Notify(TEXT("ART PREVIEW: BASE UNDER RAID | development fixture, never saved"));
    UE_LOG(LogCinderArtPreview, Display,
        TEXT("CINDERLINE_ART_PREVIEW mode=battle fixture=development_unsaved raiders=%d defenders=%d cliff_clear=1; ordinary attack-move orders and combat, no save written"),
        static_cast<int32>(Raiders.size()), static_cast<int32>(Defenders.size()));
    return true;
}

void CaptureArtPreview(UWorld* World, const FString& Mode)
{
    ACinderBattlefield* Battle = FindBattlefield(World);
    if (!World || !IsArtFixture(Battle))
    {
        // Name the condition that actually moved. A bare "fixture_changed" forces
        // whoever hits this to guess between five unrelated causes, and this path
        // only runs in the development capture fixture.
        UE_LOG(LogCinderArtPreview, Warning,
            TEXT("CINDERLINE_ART_PREVIEW capture=%s skipped=fixture_changed world=%d battle=%d menu=%d online=%d ai=%d map=%d seed=%u expected_seed=%u"),
            *Mode, World != nullptr, Battle != nullptr,
            Battle ? Battle->IsMenu() : -1, Battle ? Battle->IsOnlineMatch() : -1,
            Battle ? Battle->Sim().config().ai : -1, Battle ? Battle->Sim().config().map : -1,
            Battle ? Battle->Sim().config().seed : 0u, ArtPreviewSeed);
        return;
    }
    // Asset generation runs under NullRHI, so the first rendered editor launch
    // can still be building Metal shader maps and static-mesh render data. Only
    // this development capture path blocks; packaged gameplay pays no cost.
    FinishArtPreviewCompilation(Mode);
    LogArtPreviewViewport(World, Mode);
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("VisualTarget");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = Directory / (Mode + TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(Filename, false, false);
    const float StagedAt = Mode == TEXT("base") ? BaseStagedAt : BattleStagedAt;
    UE_LOG(LogCinderArtPreview, Display,
        TEXT("CINDERLINE_ART_PREVIEW_CAPTURE mode=%s elapsed=%.2f file=%s fixture=development_unsaved; no save written"),
        *Mode, World->GetTimeSeconds() - StagedAt, *Filename);
}

void ScheduleCapture(UWorld* World, const FString& Mode, float Delay, FTimerHandle& Handle)
{
    const TWeakObjectPtr<UWorld> WeakWorld = World;
    World->GetTimerManager().SetTimer(Handle,
        FTimerDelegate::CreateLambda([WeakWorld, Mode]
        {
            if (UWorld* CurrentWorld = WeakWorld.Get()) CaptureArtPreview(CurrentWorld, Mode);
        }), Delay, false);
}

void BeginSweep(UWorld* World)
{
    ClearArtPreviewTimers();
    if (!StageBase(World)) return;
    ScheduleCapture(World, TEXT("base"), BaseCaptureDelay, ArtPreviewCaptureTimer);
    const TWeakObjectPtr<UWorld> WeakWorld = World;
    World->GetTimerManager().SetTimer(ArtPreviewStageTimer,
        FTimerDelegate::CreateLambda([WeakWorld]
        {
            UWorld* CurrentWorld = WeakWorld.Get();
            if (!CurrentWorld) return;
            if (!StageBattleRaid(CurrentWorld))
            {
                UE_LOG(LogCinderArtPreview, Warning,
                    TEXT("CINDERLINE_ART_PREVIEW sweep_aborted=battle_stage_failed; returning fixture to fresh menu"));
                ReturnToFreshMenu(CurrentWorld, true);
                return;
            }
            ScheduleCapture(CurrentWorld, TEXT("battle"), BattleCaptureDelay,
                ArtPreviewBattleCaptureTimer);
            CurrentWorld->GetTimerManager().SetTimer(ArtPreviewResetTimer,
                FTimerDelegate::CreateLambda([WeakWorld]
                {
                    if (UWorld* ResetWorld = WeakWorld.Get()) ReturnToFreshMenu(ResetWorld, true);
                }), BattleCaptureDelay + 0.75f, false);
        }), BaseCaptureDelay + 0.35f, false);
    UE_LOG(LogCinderArtPreview, Display,
        TEXT("CINDERLINE_ART_PREVIEW sweep=started base_capture=3.0 battle_stage=3.35 battle_capture_after_stage=4.0 reset_after_capture=0.75 output=%s; development fixture, no save written"),
        *(FPaths::ProjectSavedDir() / TEXT("VisualTarget")));
}

FAutoConsoleCommandWithWorldAndArgs ArtPreviewCommand(
    TEXT("cinder.artpreview"),
    TEXT("DEVELOPMENT: replaces the current unsaved local match with a developed industrial base fixture. base captures at 3s; battle captures at 4s; battlelive runs without screenshot stalls for profiling/video; sweep captures both then returns to a fresh menu; reset clears it. Never writes a save."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World) return;
        ACinderBattlefield* Battle = FindBattlefield(World);
        ACinderPlayerController* PC = FindPlayer(World);
        if (!Battle || RefuseOnline(Battle, PC)) return;
        const FString Mode = Args.IsEmpty() ? TEXT("base") : Args[0].ToLower();
        if (Mode == TEXT("reset"))
        {
            ReturnToFreshMenu(World, false);
            return;
        }
        if (Mode == TEXT("sweep"))
        {
            BeginSweep(World);
            return;
        }
        if (Mode != TEXT("base") && Mode != TEXT("battle") && Mode != TEXT("battlelive"))
        {
            UE_LOG(LogCinderArtPreview, Warning,
                TEXT("CINDERLINE_ART_PREVIEW refused=unknown_mode value=%s expected=base,battle,battlelive,sweep,reset"),
                *Mode);
            if (PC) PC->Notify(TEXT("Art preview mode must be base, battle, battlelive, sweep, or reset."));
            return;
        }
        ClearArtPreviewTimers();
        if (!StageBase(World)) return;
        if ((Mode == TEXT("battle") || Mode == TEXT("battlelive")) && !StageBattleRaid(World)) return;
        if (Mode == TEXT("battlelive"))
        {
            // battlelive never captures, so nothing else on this path would flush
            // shader compilation. A profile taken while shader maps are still
            // building measures the compiler over gray fallback materials rather
            // than the frame, and the readiness marker these emit is what the
            // capture tooling refuses to proceed without.
            FinishArtPreviewCompilation(Mode);
            LogArtPreviewViewport(World, Mode);
            return;
        }
        ScheduleCapture(World, Mode,
            Mode == TEXT("base") ? BaseCaptureDelay : BattleCaptureDelay,
            ArtPreviewCaptureTimer);
    }));
}

#endif
