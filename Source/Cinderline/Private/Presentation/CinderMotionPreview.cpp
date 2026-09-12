#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderPlayerController.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderMotionPreview, Log, All);

namespace
{
constexpr std::uint32_t MotionPreviewSeed = 0xA11CE;
TWeakObjectPtr<ACinderBattlefield> MotionPreviewBattle;
TWeakObjectPtr<UWorld> MotionPreviewWorld;
FTimerHandle MotionPatrolTimer;
float MotionPatrolDeadline = 0;
struct FMotionPatrol
{
    cinder::Id Unit = 0;
    cinder::Vec2 Start, End;
    bool bTowardEnd = true;
};
TArray<FMotionPatrol> MotionPatrols;

cinder::CommandResult Order(cinder::Simulation& Sim, cinder::Id Unit, cinder::CommandType Type,
    cinder::Vec2 Point = {}, cinder::Id Target = 0, cinder::Kind Kind = cinder::Kind::Worker)
{
    const cinder::Entity* Entity = Sim.find(Unit);
    if (!Entity) return {};
    cinder::Command Command;
    Command.team = Entity->team; Command.units = {Unit}; Command.type = Type;
    Command.point = Point; Command.target = Target; Command.kind = Kind;
    return Sim.command(Command);
}

void ClearPatrol()
{
    if (UWorld* World = MotionPreviewWorld.Get()) World->GetTimerManager().ClearTimer(MotionPatrolTimer);
    MotionPatrols.Reset(); MotionPreviewWorld.Reset(); MotionPatrolDeadline = 0;
}

void StartPatrol(UWorld* World, ACinderBattlefield* Battle)
{
    if (!World || !Battle || MotionPatrols.IsEmpty()) return;
    MotionPreviewWorld = World;
    MotionPatrolDeadline = World->GetTimeSeconds() + 30.0f;
    const TWeakObjectPtr<ACinderBattlefield> WeakBattle = Battle;
    World->GetTimerManager().SetTimer(MotionPatrolTimer, FTimerDelegate::CreateLambda([WeakBattle]
    {
        ACinderBattlefield* CurrentBattle = WeakBattle.Get();
        UWorld* CurrentWorld = MotionPreviewWorld.Get();
        if (!CurrentBattle || !CurrentWorld || CurrentBattle->IsMenu()
            || CurrentBattle->Sim().config().seed != MotionPreviewSeed
            || CurrentWorld->GetTimeSeconds() >= MotionPatrolDeadline)
        {
            ClearPatrol();
            return;
        }
        for (FMotionPatrol& Patrol : MotionPatrols)
        {
            if (!CurrentBattle->Sim().find(Patrol.Unit)) continue;
            Patrol.bTowardEnd = !Patrol.bTowardEnd;
            Order(CurrentBattle->Sim(), Patrol.Unit, cinder::CommandType::Move,
                Patrol.bTowardEnd ? Patrol.End : Patrol.Start);
        }
    }), 2.5f, true);
}

void FitCamera(ACinderPlayerController* PC, const TArray<cinder::Vec2>& Points, bool bFullRoster)
{
    if (!PC || !PC->PlayerCameraManager || Points.IsEmpty()) return;
    ACinderCamera* Rig = Cast<ACinderCamera>(PC->GetPawn());
    USpringArmComponent* SpringArm = Rig ? Rig->FindComponentByClass<USpringArmComponent>() : nullptr;
    if (!Rig || !SpringArm) return;
    int32 Width = 0, Height = 0;
    PC->GetViewportSize(Width, Height);
    if (Width <= 0 || Height <= 0) return;

    FBox2D WorldBounds(ForceInit);
    for (const cinder::Vec2 Point : Points) WorldBounds += FVector2D(Point.x, Point.y);
    FVector Focus(WorldBounds.GetCenter(), 0);
    Rig->Zoom(1100.0f - Rig->Distance());
    Rig->Focus(Focus, true);
    const FVector2D Low(Width * 0.12f, Height * 0.18f);
    const FVector2D High(Width * 0.84f, Height * 0.65f);
    const double TanHalfX = FMath::Tan(FMath::DegreesToRadians(26.0));
    const double TanHalfY = TanHalfX / (static_cast<double>(Width) / Height);
    UE_LOG(LogCinderMotionPreview, Display,
        TEXT("CINDERLINE_MOTION_FIT_BEGIN world=(%.1f,%.1f)-(%.1f,%.1f) requested_focus=(%.1f,%.1f) rig=(%.1f,%.1f,%.1f) distance=%.1f viewport=%dx%d tan=(%.5f,%.5f)"),
        WorldBounds.Min.X, WorldBounds.Min.Y, WorldBounds.Max.X, WorldBounds.Max.Y,
        Focus.X, Focus.Y, Rig->GetActorLocation().X, Rig->GetActorLocation().Y, Rig->GetActorLocation().Z,
        Rig->Distance(), Width, Height, TanHalfX, TanHalfY);
    auto CameraFrame = [&]
    {
        const FQuat Rotation = SpringArm->GetTargetRotation().Quaternion();
        const FVector Forward = Rotation.GetForwardVector();
        const FVector Right = Rotation.GetRightVector();
        const FVector Up = Rotation.GetUpVector();
        const FVector Origin = Rig->GetActorLocation() - Forward * Rig->Distance();
        return TTuple<FVector, FVector, FVector, FVector>(Origin, Forward, Right, Up);
    };
    auto Project = [&](const FVector& Point, const TTuple<FVector, FVector, FVector, FVector>& Frame, FVector2D& Out)
    {
        const FVector Relative = Point - Frame.Get<0>();
        const double Depth = FVector::DotProduct(Relative, Frame.Get<1>());
        if (Depth <= KINDA_SMALL_NUMBER) return false;
        const double NormalX = FVector::DotProduct(Relative, Frame.Get<2>()) / (Depth * TanHalfX);
        const double NormalY = FVector::DotProduct(Relative, Frame.Get<3>()) / (Depth * TanHalfY);
        Out = FVector2D((NormalX + 1.0) * Width * 0.5, (1.0 - NormalY) * Height * 0.5);
        return true;
    };
    auto GroundAtScreen = [&](FVector2D Screen, const TTuple<FVector, FVector, FVector, FVector>& Frame, FVector& Out)
    {
        const double NormalX = Screen.X * 2.0 / Width - 1.0;
        const double NormalY = 1.0 - Screen.Y * 2.0 / Height;
        const FVector Direction = Frame.Get<1>() + Frame.Get<2>() * (NormalX * TanHalfX)
            + Frame.Get<3>() * (NormalY * TanHalfY);
        if (Direction.Z >= -KINDA_SMALL_NUMBER) return false;
        Out = Frame.Get<0>() + Direction * (-Frame.Get<0>().Z / Direction.Z);
        return true;
    };

    FBox2D Projected(ForceInit);
    float BestScore = TNumericLimits<float>::Max();
    float BestDistance = Rig->Distance();
    FVector BestFocus = Focus;
    FBox2D BestProjected(ForceInit);
    for (int32 Pass = 0; Pass < 8; ++Pass)
    {
        const auto Frame = CameraFrame();
        Projected = FBox2D(ForceInit);
        const float EnvelopeRadius = 60.0f;
        const float EnvelopeTop = bFullRoster ? 160.0f : 100.0f;
        for (const cinder::Vec2 Point : Points)
            for (const float X : {-EnvelopeRadius, EnvelopeRadius}) for (const float Y : {-EnvelopeRadius, EnvelopeRadius})
                for (const float Z : {0.0f, EnvelopeTop})
                {
                    FVector2D Screen;
                    if (Project(FVector(Point.x + X, Point.y + Y, Z), Frame, Screen))
                        Projected += Screen;
                }
        if (!Projected.bIsValid) break;
        const FVector2D Available = High - Low;
        const float Fit = FMath::Max(Projected.GetSize().X / Available.X, Projected.GetSize().Y / Available.Y);
        const float Overflow = FMath::Max(0.0f, static_cast<float>(Low.X - Projected.Min.X)) / Available.X
            + FMath::Max(0.0f, static_cast<float>(Low.Y - Projected.Min.Y)) / Available.Y
            + FMath::Max(0.0f, static_cast<float>(Projected.Max.X - High.X)) / Available.X
            + FMath::Max(0.0f, static_cast<float>(Projected.Max.Y - High.Y)) / Available.Y;
        const FVector2D CenterDelta = Projected.GetCenter() - (Low + High) * 0.5f;
        const float CenterError = FMath::Square(static_cast<float>(CenterDelta.X / Available.X))
            + FMath::Square(static_cast<float>(CenterDelta.Y / Available.Y));
        const float Score = Overflow * 4.0f + CenterError + FMath::Square(Fit - 0.86f) * 0.1f;
        if (Score < BestScore)
        {
            BestScore = Score; BestDistance = Rig->Distance(); BestFocus = Focus; BestProjected = Projected;
        }
        UE_LOG(LogCinderMotionPreview, Display,
            TEXT("CINDERLINE_MOTION_FIT_PASS pass=%d requested_focus=(%.1f,%.1f) rig=(%.1f,%.1f,%.1f) distance=%.1f origin=(%.1f,%.1f,%.1f) forward=(%.4f,%.4f,%.4f) right=(%.4f,%.4f,%.4f) up=(%.4f,%.4f,%.4f) projected=(%.1f,%.1f)-(%.1f,%.1f) fit=%.4f"),
            Pass, Focus.X, Focus.Y, Rig->GetActorLocation().X, Rig->GetActorLocation().Y, Rig->GetActorLocation().Z,
            Rig->Distance(), Frame.Get<0>().X, Frame.Get<0>().Y, Frame.Get<0>().Z,
            Frame.Get<1>().X, Frame.Get<1>().Y, Frame.Get<1>().Z,
            Frame.Get<2>().X, Frame.Get<2>().Y, Frame.Get<2>().Z,
            Frame.Get<3>().X, Frame.Get<3>().Y, Frame.Get<3>().Z,
            Projected.Min.X, Projected.Min.Y, Projected.Max.X, Projected.Max.Y, Fit);
        if (Fit > 0.94f || Fit < 0.76f)
        {
            const float DesiredDistance = FMath::Clamp(Rig->Distance() * Fit / 0.86f, 650.0f, 3200.0f);
            if (!FMath::IsNearlyEqual(DesiredDistance, Rig->Distance(), 1.0f))
            {
                UE_LOG(LogCinderMotionPreview, Display,
                    TEXT("CINDERLINE_MOTION_FIT_ZOOM pass=%d from=%.1f to=%.1f fit=%.4f"),
                    Pass, Rig->Distance(), DesiredDistance, Fit);
                Rig->Zoom(DesiredDistance - Rig->Distance());
                Rig->Focus(Focus, true);
                continue;
            }
        }
        const FVector2D TargetCenter = (Low + High) * 0.5f;
        FVector CurrentGround, TargetGround;
        if (!GroundAtScreen(Projected.GetCenter(), Frame, CurrentGround)
            || !GroundAtScreen(TargetCenter, Frame, TargetGround)) break;
        const FVector Adjustment = CurrentGround - TargetGround;
        if (Adjustment.SizeSquared2D() < 1.0f) break;
        UE_LOG(LogCinderMotionPreview, Display,
            TEXT("CINDERLINE_MOTION_FIT_CENTER pass=%d projected_center=(%.1f,%.1f) target_center=(%.1f,%.1f) current_ground=(%.1f,%.1f) target_ground=(%.1f,%.1f) adjustment=(%.1f,%.1f)"),
            Pass, Projected.GetCenter().X, Projected.GetCenter().Y, TargetCenter.X, TargetCenter.Y,
            CurrentGround.X, CurrentGround.Y, TargetGround.X, TargetGround.Y, Adjustment.X, Adjustment.Y);
        const FVector PriorRigLocation = Rig->GetActorLocation();
        Focus = PriorRigLocation + FVector(Adjustment.X, Adjustment.Y, 0);
        Rig->Focus(Focus, true);
        if (Rig->GetActorLocation().Equals(PriorRigLocation, 1.0f)) break;
    }
    Rig->Focus(BestFocus, true);
    Rig->Zoom(BestDistance - Rig->Distance());
    Rig->Focus(BestFocus, true);
    Projected = BestProjected;
    SpringArm->TargetArmLength = Rig->Distance();
    SpringArm->TickComponent(0, LEVELTICK_All, nullptr);
    PC->PlayerCameraManager->UpdateCamera(0);
    const FQuat FinalRotation = SpringArm->GetTargetRotation().Quaternion();
    const FVector CalculatedOrigin = Rig->GetActorLocation() - FinalRotation.GetForwardVector() * Rig->Distance();
    const FVector ActualOrigin = PC->PlayerCameraManager->GetCameraLocation();
    const FRotator ActualRotation = PC->PlayerCameraManager->GetCameraRotation();
    UE_LOG(LogCinderMotionPreview, Display,
        TEXT("CINDERLINE_MOTION_FIT_FINAL requested_focus=(%.1f,%.1f) rig=(%.1f,%.1f,%.1f) distance=%.1f calculated_origin=(%.1f,%.1f,%.1f) calculated_rotation=(%.1f,%.1f,%.1f) actual_origin=(%.1f,%.1f,%.1f) actual_rotation=(%.1f,%.1f,%.1f)"),
        Focus.X, Focus.Y, Rig->GetActorLocation().X, Rig->GetActorLocation().Y, Rig->GetActorLocation().Z,
        Rig->Distance(), CalculatedOrigin.X, CalculatedOrigin.Y, CalculatedOrigin.Z,
        SpringArm->GetTargetRotation().Pitch, SpringArm->GetTargetRotation().Yaw, SpringArm->GetTargetRotation().Roll,
        ActualOrigin.X, ActualOrigin.Y, ActualOrigin.Z, ActualRotation.Pitch, ActualRotation.Yaw, ActualRotation.Roll);
    UE_LOG(LogCinderMotionPreview, Display,
        TEXT("CINDERLINE_MOTION_PREVIEW_FRAME viewport=%dx%d distance=%.1f best_score=%.4f target_screen=(%.0f,%.0f)-(%.0f,%.0f) actual_screen=(%.0f,%.0f)-(%.0f,%.0f)"),
        Width, Height, Rig->Distance(), BestScore, Low.X, Low.Y, High.X, High.Y,
        Projected.Min.X, Projected.Min.Y, Projected.Max.X, Projected.Max.Y);
}

FAutoConsoleCommandWithWorldAndArgs MotionPreviewCommand(
    TEXT("cinder.motionpreview"),
    TEXT("DEVELOPMENT: replaces the current unsaved match with a deterministic walking, mining, construction, recoil and aircraft-motion fixture. walk|work|weapons selects a close-up; freeze stops after staging; shot requests one screenshot; reset returns to a fresh menu. Never writes a save."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World) return;
        TActorIterator<ACinderBattlefield> It(World);
        if (!It) return;
        ACinderBattlefield* Battle = *It;
        ACinderPlayerController* PC = Cast<ACinderPlayerController>(World->GetFirstPlayerController());
        ClearPatrol();
        Battle->SetActorTickEnabled(true);
        if (Args.Contains(TEXT("reset")))
        {
            if (PC) { PC->ExecuteAction(TEXT("start"), 0); PC->ExecuteAction(TEXT("menu")); }
            else { Battle->StartMatch(0); Battle->ReturnToMenu(); }
            MotionPreviewBattle.Reset();
            UE_LOG(LogCinderMotionPreview, Display, TEXT("CINDERLINE_MOTION_PREVIEW reset=fresh_menu; no save written"));
            return;
        }

        if (PC) PC->ExecuteAction(TEXT("start"), 0); else Battle->StartMatch(0);
        cinder::Simulation& Sim = Battle->Sim();
        cinder::Config Config; Config.map = 0; Config.ai = false; Config.seed = MotionPreviewSeed;
        Sim.reset(Config);
        Battle->ResetPresentation();
        Sim.debugResources(0, 5000);
        std::vector<cinder::Id> Initial;
        for (const cinder::Entity& Entity : Sim.entities()) Initial.push_back(Entity.id);
        for (cinder::Id Id : Initial) Order(Sim, Id, cinder::CommandType::Hold);

        TArray<cinder::Vec2> FramePoints, WalkPoints, WorkPoints, WeaponPoints;
        auto Spawn = [&](cinder::Kind Kind, int Team, float X, float Y)
        {
            FramePoints.Add({X, Y});
            return Sim.debugSpawn(Kind, Team, {X, Y});
        };
        const cinder::Kind WalkKinds[] = {cinder::Kind::Worker, cinder::Kind::Striker, cinder::Kind::Lancer,
            cinder::Kind::Bastion, cinder::Kind::Mortar};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(WalkKinds); ++Index)
        {
            const float X = 1280.0f + Index * 110.0f;
            const cinder::Id Id = Spawn(WalkKinds[Index], 0, X, 1550);
            const cinder::Vec2 Start{X, 1550}, End{X, 1770};
            Order(Sim, Id, cinder::CommandType::Move, End);
            MotionPatrols.Add({Id, Start, End, true});
            WalkPoints.Add(Start); WalkPoints.Add(End);
            FramePoints.Add({X, 1770});
        }

        cinder::Id Mineral = 0;
        cinder::Vec2 MineralPoint;
        float MineralDistance = TNumericLimits<float>::Max();
        for (const cinder::Entity& Entity : Sim.entities())
            if (Entity.kind == cinder::Kind::Resource && Entity.resource > 0 && Sim.explored(0, Entity.pos))
            {
                // Keep the work fixture away from the map corner, where the normal
                // camera bounds cannot frame the first home deposit and the builder.
                const float Distance = FMath::Square(Entity.pos.x - 1000.0f) + FMath::Square(Entity.pos.y - 1000.0f);
                if (Distance < MineralDistance)
                { Mineral = Entity.id; MineralPoint = Entity.pos; MineralDistance = Distance; }
            }
        if (Mineral)
        {
            const float Reach = cinder::definition(cinder::Kind::Resource).radius
                + cinder::definition(cinder::Kind::Worker).radius + 4;
            const cinder::Id Miner = Spawn(cinder::Kind::Worker, 0, MineralPoint.x - Reach, MineralPoint.y);
            Order(Sim, Miner, cinder::CommandType::Gather, {}, Mineral);
            FramePoints.Add(MineralPoint);
            WorkPoints.Add(MineralPoint); WorkPoints.Add(Sim.find(Miner)->pos);
        }

        cinder::Id Builder = 0;
        cinder::Vec2 BuildSite;
        for (float X = 1000; X <= 2100 && !Builder; X += 100)
            for (float Y = 950; Y <= 1350 && !Builder; Y += 100)
                if (Sim.canPlace(0, cinder::Kind::Foundry, {X, Y}))
                {
                    BuildSite = {X, Y};
                    Builder = Spawn(cinder::Kind::Worker, 0, X + 180, Y);
                    if (!Order(Sim, Builder, cinder::CommandType::Build, BuildSite, 0, cinder::Kind::Foundry).accepted)
                        Builder = 0;
                }
        if (Builder)
        {
            FramePoints.Add(BuildSite); WorkPoints.Add(BuildSite); WorkPoints.Add(Sim.find(Builder)->pos);
        }

        const cinder::Id Vision = Spawn(cinder::Kind::Scout, 0, 2320, 1650);
        Order(Sim, Vision, cinder::CommandType::Move, {2520, 1990});
        const cinder::Id Mender = Spawn(cinder::Kind::Mender, 0, 2220, 1880);
        Order(Sim, Mender, cinder::CommandType::Move, {2460, 2080});
        const cinder::Id Kite = Spawn(cinder::Kind::Kite, 0, 2320, 1980);
        Order(Sim, Kite, cinder::CommandType::Move, {2570, 2180});
        FramePoints.Add({2520, 1990}); FramePoints.Add({2570, 2180});

        const cinder::Id Anvil = Spawn(cinder::Kind::Bastion, 0, 2050, 1450);
        const cinder::Id AnvilTarget = Spawn(cinder::Kind::Worker, 1, 2170, 1450);
        const cinder::Id Mortar = Spawn(cinder::Kind::Mortar, 0, 2050, 1250);
        const cinder::Id MortarTarget = Spawn(cinder::Kind::Turret, 1, 2150, 950);
        WeaponPoints.Add({2050, 1450}); WeaponPoints.Add({2170, 1450});
        WeaponPoints.Add({2050, 1250}); WeaponPoints.Add({2150, 950});
        Sim.update(cinder::Simulation::Step); // Apply normal vision before issuing target-specific attacks.
        Order(Sim, Anvil, cinder::CommandType::Attack, {}, AnvilTarget);
        Order(Sim, Mortar, cinder::CommandType::Attack, {}, MortarTarget);

        Battle->ResetFeedback();
        Battle->RenderState();
        for (int32 Step = 0; Step < 8; ++Step) Battle->Tick(cinder::Simulation::Step);
        const TArray<cinder::Vec2>* CameraPoints = &FramePoints;
        if (Args.Contains(TEXT("walk"))) CameraPoints = &WalkPoints;
        else if (Args.Contains(TEXT("work")) && !WorkPoints.IsEmpty()) CameraPoints = &WorkPoints;
        else if (Args.Contains(TEXT("weapons"))) CameraPoints = &WeaponPoints;
        FitCamera(PC, *CameraPoints, CameraPoints == &FramePoints);
        const bool bFrozen = Args.Contains(TEXT("freeze"));
        Battle->SetActorTickEnabled(!bFrozen);
        MotionPreviewBattle = Battle;
        if (!bFrozen) StartPatrol(World, Battle);
        if (Args.Contains(TEXT("shot")))
        {
            const FString Filename = FPaths::Combine(FPaths::ScreenShotDir(), TEXT("CinderMotionPreview.png"));
            FScreenshotRequest::RequestScreenshot(Filename, false, false);
        }
        if (PC) PC->Notify(bFrozen
            ? TEXT("MOTION PREVIEW FROZEN | deterministic development fixture")
            : TEXT("MOTION PREVIEW LIVE | deterministic development fixture"));
        UE_LOG(LogCinderMotionPreview, Display,
            TEXT("CINDERLINE_MOTION_PREVIEW live=%d seed=%u walking=5 mining=%d construction=%d recoil=2 aircraft=3 tick=%llu; deterministic development actors, ordinary commands, no save written"),
            !bFrozen, MotionPreviewSeed, Mineral != 0, Builder != 0, Sim.tick());
    }));
}

#endif
