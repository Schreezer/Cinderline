#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderHelpContent.h"
#include "Presentation/CinderHUD.h"
#include "Presentation/CinderGameEngine.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderPlayerController.h"
#include "Presentation/CinderTerrainSurface.h"
#include "Presentation/CinderTeamColors.h"
#include "Tests/AutomationCommon.h"
#include <algorithm>
#include <array>
#include <functional>

namespace CinderWorldIntegration
{
// Engine's own automation tests use this wrapper to own a transient game world.
// No editor map, local player viewport, or persistent player save is modified.
struct FGameFixture
{
    FTestWorldWrapper WorldOwner;
    ACinderBattlefield* Battle = nullptr;
    ACinderPlayerController* Controller = nullptr;

    bool Initialize(FAutomationTestBase& Test)
    {
        if (!WorldOwner.CreateTestWorld(EWorldType::Game))
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        UWorld* World = WorldOwner.GetTestWorld();
        if (!Test.TestNotNull(TEXT("Transient game world exists"), World)) return false;
        World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
        Battle = World->SpawnActor<ACinderBattlefield>();
        if (!Test.TestNotNull(TEXT("Real battlefield actor spawned"), Battle)) return false;
        if (!WorldOwner.BeginPlayInTestWorld())
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        // PlayerController initialization expects an existing game mode/state.
        Controller = World->SpawnActor<ACinderPlayerController>();
        if (!Test.TestNotNull(TEXT("Real controller actor spawned"), Controller)) return false;
        Test.TestTrue(TEXT("Cinderline game mode starts the world"),
            World->GetAuthGameMode<ACinderGameMode>() != nullptr && World->HasBegunPlay());
        Test.TestTrue(TEXT("Both actors received BeginPlay"),
            Battle->HasActorBegunPlay() && Controller->HasActorBegunPlay());
        Test.TestTrue(TEXT("Controller resolves this world's battlefield"), Controller->Battlefield() == Battle);
        TArray<UInstancedStaticMeshComponent*> Components;
        Battle->GetComponents<UInstancedStaticMeshComponent>(Components);
        bool HasRenderedInstances = false;
        for (const UInstancedStaticMeshComponent* Component : Components)
            if (Component && Component->GetInstanceCount() > 0) HasRenderedInstances = true;
        Test.TestTrue(TEXT("Battlefield BeginPlay populated presentation components"), HasRenderedInstances);
        Test.TestTrue(TEXT("Initial rendering remembers discovered resources"), !Battle->KnownResources().empty());
        WorldOwner.ForwardErrorMessages(&Test);
        return !Test.HasAnyErrors();
    }

    // Invoke the actual adapter tick, including its pause/menu gate and rendering.
    // This deliberately does not stand in for an engine frame-rate or UI test.
    bool TickUntil(const std::function<bool()>& Predicate, float MaximumSeconds)
    {
        constexpr float Delta = 0.1f;
        for (int32 I = 0; I < FMath::CeilToInt(MaximumSeconds / Delta); ++I)
        {
            if (Predicate()) return true;
            Battle->Tick(Delta);
        }
        return Predicate();
    }
};

std::vector<cinder::Id> EntitiesOfKind(const cinder::Simulation& Sim, cinder::Kind Kind)
{
    std::vector<cinder::Id> Result;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.team == 0 && Entity.kind == Kind && Entity.alive()) Result.push_back(Entity.id);
    return Result;
}

cinder::CommandResult IssueAtBattlefield(ACinderBattlefield& Battle, cinder::CommandType Type,
    const std::vector<cinder::Id>& Units, cinder::Kind Kind = cinder::Kind::Worker,
    cinder::Vec2 Point = {}, cinder::Id Target = 0)
{
    cinder::Command Command;
    Command.type = Type; Command.team = 0; Command.units = Units;
    Command.kind = Kind; Command.point = Point; Command.target = Target;
    return Battle.Sim().command(Command);
}

bool FindBuildSite(const cinder::Simulation& Sim, cinder::Id Builder,
    cinder::Vec2 Base, cinder::Vec2& OutSite)
{
    const cinder::Entity* Worker = Sim.find(Builder);
    if (!Worker) return false;
    for (float Radius = 240; Radius <= 560; Radius += 40)
        for (int32 Spoke = 0; Spoke < 24; ++Spoke)
        {
            const float Angle = 2 * PI * Spoke / 24;
            const cinder::Vec2 Point{ Base.x + Radius * FMath::Cos(Angle), Base.y + Radius * FMath::Sin(Angle) };
            const float DX = Worker->pos.x - Point.x, DY = Worker->pos.y - Point.y;
            // Keep enough approach distance to observe travel before construction can start.
            if (DX * DX + DY * DY >= 300 * 300 && DX * DX + DY * DY <= 700 * 700
                && Sim.canPlace(0, cinder::Kind::Foundry, Point))
            {
                OutSite = Point;
                return true;
            }
        }
    return false;
}

struct FTemporarySnapshot
{
    FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("Automation"),
        TEXT("Cinderline"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("fixture.cinder")));
    ~FTemporarySnapshot()
    {
        IFileManager::Get().Delete(*Path, false, true);
        IFileManager::Get().DeleteDirectory(*Directory, false, false);
    }
};

bool HasPendingModes(const ACinderPlayerController& Controller)
{
    return Controller.IsBuildMode() || Controller.bBuildMenu || Controller.IsAttackMoveMode()
        || Controller.IsMoveCommandMode() || Controller.IsDefendCommandMode()
        || Controller.IsProductionRallyMode() || Controller.bBoxSelect;
}

void TryArmModes(ACinderPlayerController& Controller)
{
    Controller.ExecuteAction(TEXT("attack"));
    Controller.ExecuteAction(TEXT("buildmenu"));
    Controller.ExecuteAction(TEXT("box"));
    Controller.ExecuteAction(TEXT("build"), static_cast<int>(cinder::Kind::Foundry));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderWorldLifecycleIntegration,
    "Cinderline.Integration.WorldLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderWorldLifecycleIntegration::RunTest(const FString& Parameters)
{
    using namespace CinderWorldIntegration;
    using namespace cinder;

    TestFalse(TEXT("F1 help cannot execute Unreal's inherited wireframe command"),
        GetDefault<UPlayerInput>()->DebugExecBindings.ContainsByPredicate([](const FKeyBind& Binding)
        { return Binding.Key == EKeys::F1 && Binding.Command.Contains(TEXT("viewmode")); }));

    FCinderMotionObservation Prior;
    Prior.Id = 17; Prior.Kind = Kind::Worker; Prior.Position = {100, 100};
    Prior.Order = Order::Move; Prior.Tick = 10; Prior.Time = 0.5f;
    FCinderMotionObservation Current = Prior;
    Current.Tick = 11; Current.Time = 0.55f;
    const FCinderEntityPose StationaryPose = FCinderEntityMotion::CalculatePose(Current, &Prior);
    TestFalse(TEXT("A movement order without an actual position delta has no walk pose"), StationaryPose.bMoving);
    TestTrue(TEXT("A stationary ground unit retains an exact neutral body pose"),
        StationaryPose.BodyZ == 0 && StationaryPose.BodyRotation.IsNearlyZero());
    Current.Position.x += 4;
    const FCinderEntityPose WalkingPose = FCinderEntityMotion::CalculatePose(Current, &Prior);
    TestTrue(TEXT("An observed authoritative position delta starts locomotion"), WalkingPose.bMoving);
    TestTrue(TEXT("Drudge diagonal legs alternate around manifest pivots"),
        WalkingPose.LegFrontLeftRotation.Pitch * WalkingPose.LegFrontRightRotation.Pitch < 0
        && FMath::IsNearlyEqual(WalkingPose.LegFrontLeftRotation.Pitch, WalkingPose.LegRearRightRotation.Pitch));

    Prior.Order = Current.Order = Order::Gather; Prior.HarvestTimer = Current.HarvestTimer = 0.2f;
    const FCinderEntityPose TravellingGather = FCinderEntityMotion::CalculatePose(Current, &Prior);
    TestFalse(TEXT("A Drudge travelling on a gather order keeps its tool still"), TravellingGather.bToolActive);
    Current.Position = Prior.Position; Current.HarvestTimer += Simulation::Step;
    const FCinderEntityPose ActiveGather = FCinderEntityMotion::CalculatePose(Current, &Prior);
    TestTrue(TEXT("An observed harvest timer advance drives the mining tool"), ActiveGather.bToolActive);
    Current.bReturning = true;
    TestFalse(TEXT("Returning with ore stops mining tool motion"),
        FCinderEntityMotion::CalculatePose(Current, &Prior).bToolActive);
    Current.bReturning = false; Current.Order = Order::Construct; Current.bConstructionActive = false;
    TestFalse(TEXT("A travelling or interrupted builder keeps its tool still"),
        FCinderEntityMotion::CalculatePose(Current, &Prior).bToolActive);
    Current.bConstructionActive = true;
    TestTrue(TEXT("Only an active construction worker drives its tool"),
        FCinderEntityMotion::CalculatePose(Current, &Prior).bToolActive);

    Current.Kind = Kind::Mortar; Current.Order = Order::Hold; Current.Time = 1.11f;
    const FCinderEntityPose RecoilPose = FCinderEntityMotion::CalculatePose(Current, &Prior, 1.0f);
    TestTrue(TEXT("A recent observed cooldown increase produces sloped Cinderthrow recoil"),
        RecoilPose.bRecoil && RecoilPose.WeaponOffset.X < 0 && RecoilPose.WeaponOffset.Z < 0);
    Current.Time = 1.3f;
    TestFalse(TEXT("Recoil ends from simulation time after its bounded impulse"),
        FCinderEntityMotion::CalculatePose(Current, &Prior, 1.0f).bRecoil);
    Current.Kind = Kind::Scout; Current.Position = {104, 106}; Current.Facing = 0;
    Current.Time = 0.55f;
    const FCinderEntityPose AircraftPose = FCinderEntityMotion::CalculatePose(Current, &Prior);
    TestTrue(TEXT("A moving aircraft banks while retaining a root-local hover"),
        AircraftPose.bMoving && !FMath::IsNearlyZero(AircraftPose.BodyRotation.Roll));
    const FVector Pivot(6.71087, 0, 17.01571), Root(1200, 1400, 0);
    const FTransform Pivoted = CinderPartWorldTransform(Root, FQuat::Identity, Pivot, FRotator(25, 0, 0));
    TestTrue(TEXT("Manifest pivot rotation leaves the tool hinge fixed in world space"),
        Pivoted.TransformPosition(Pivot).Equals(Root + Pivot, KINDA_SMALL_NUMBER));

    FCinderEntityMotion MotionCache;
    FCinderMotionObservation OtherEntity = Current; OtherEntity.Id = Current.Id + 1;
    MotionCache.BeginFrame(); MotionCache.Observe(Prior); MotionCache.Observe(OtherEntity); MotionCache.EndFrame();
    TestEqual(TEXT("Pose cache contains only entities observed in its frame"), MotionCache.CachedEntities(), 2);
    MotionCache.BeginFrame(); MotionCache.Observe(OtherEntity); MotionCache.EndFrame();
    TestTrue(TEXT("Pose cache removes hidden or dead entities at the frame boundary"),
        MotionCache.CachedEntities() == 1 && !MotionCache.HasSample(Prior.Id));
    MotionCache.Reset();
    TestEqual(TEXT("Pose cache reset clears prior match samples"), MotionCache.CachedEntities(), 0);

    FCinderMotionObservation ShooterOne;
    ShooterOne.Id = 101; ShooterOne.Kind = Kind::Bastion; ShooterOne.Tick = 20; ShooterOne.Time = 1.0f;
    FCinderMotionObservation ShooterTwo = ShooterOne; ShooterTwo.Id = 102;
    MotionCache.BeginFrame(); MotionCache.Observe(ShooterOne); MotionCache.Observe(ShooterTwo); MotionCache.EndFrame();
    ++ShooterOne.Tick; ++ShooterTwo.Tick; ShooterOne.Time += Simulation::Step; ShooterTwo.Time += Simulation::Step;
    ShooterOne.Cooldown = definition(Kind::Bastion).cooldown;
    MotionCache.BeginFrame();
    const FCinderEntityPose FiredPose = MotionCache.Observe(ShooterOne);
    const FCinderEntityPose NeighborPose = MotionCache.Observe(ShooterTwo);
    MotionCache.EndFrame();
    TestTrue(TEXT("Observe starts recoil only for the exact entity whose cooldown rose"),
        FiredPose.bRecoil && !NeighborPose.bRecoil);

    CinderTerrainSurface::FFeatures Terrain;
    const FColor EmptyTerrain = CinderTerrainSurface::Sample(Terrain, 1000, 1000);
    TestTrue(TEXT("Terrain without observed inputs has no exposed stone, mineral stain or service ground"),
        EmptyTerrain.R == 0 && EmptyTerrain.G == 0 && EmptyTerrain.A == 0);
    Terrain.Minerals.Add({1000, 1000});
    const FColor MineralCenter = CinderTerrainSurface::Sample(Terrain, 1000, 1000);
    const FColor MineralFar = CinderTerrainSurface::Sample(Terrain, 1800, 1800);
    TestTrue(TEXT("Observed ore stains only its local terrain"), MineralCenter.G > 0 && MineralFar.G == 0);
    Terrain.Cliffs.Add({{600, 700}, {120, 80}});
    const FColor CliffCenter = CinderTerrainSurface::Sample(Terrain, 600, 700);
    const FColor CliffFar = CinderTerrainSurface::Sample(Terrain, 1800, 1800);
    TestTrue(TEXT("A cliff mask exposes stone inside its rectangle and not at a distant point"),
        CliffCenter.R > 0 && CliffFar.R == 0);
    TArray<FColor> MapZeroSamples;
    for (const Vec2 Point : {Vec2{375, 425}, Vec2{1375, 1925}, Vec2{3175, 925}})
        MapZeroSamples.Add(CinderTerrainSurface::Sample(Terrain, Point.x, Point.y));
    Terrain.Map = 2;
    bool bMapChangesAsh = false, bSameInputDeterministic = true;
    int32 SampleIndex = 0;
    for (const Vec2 Point : {Vec2{375, 425}, Vec2{1375, 1925}, Vec2{3175, 925}})
    {
        const FColor MapTwo = CinderTerrainSurface::Sample(Terrain, Point.x, Point.y);
        bMapChangesAsh |= MapZeroSamples[SampleIndex++].B != MapTwo.B;
        bSameInputDeterministic &= MapTwo == CinderTerrainSurface::Sample(Terrain, Point.x, Point.y);
    }
    TestTrue(TEXT("Map seed changes the ash mask while identical inputs stay deterministic"),
        bMapChangesAsh && bSameInputDeterministic);

    CinderTerrainSurface::FFeatures ServiceTerrain;
    ServiceTerrain.ServicePads.Add({{1000, 1000}, {120, 80}});
    const FColor PadCenter = CinderTerrainSurface::Sample(ServiceTerrain, 1000, 1000);
    const FColor PadFeather = CinderTerrainSurface::Sample(ServiceTerrain, 1140, 1000);
    const FColor PadOutside = CinderTerrainSurface::Sample(ServiceTerrain, 1180, 1000);
    TestTrue(TEXT("Service pads occupy their footprint, feather at the edge and leave nearby terrain clear"),
        PadCenter.A > 0 && PadFeather.A > 0 && PadFeather.A < PadCenter.A && PadOutside.A == 0);
    TestTrue(TEXT("Adding a service pad changes only the service-ground channel"),
        PadCenter.R == EmptyTerrain.R && PadCenter.G == EmptyTerrain.G && PadCenter.B == EmptyTerrain.B);
    ServiceTerrain.Roads.Emplace(cinder::Vec2{1400, 1600}, cinder::Vec2{1900, 1600});
    const FColor RoadCenter = CinderTerrainSurface::Sample(ServiceTerrain, 1650, 1600);
    const FColor RoadFeather = CinderTerrainSurface::Sample(ServiceTerrain, 1650, 1640);
    const FColor RoadOutside = CinderTerrainSurface::Sample(ServiceTerrain, 1650, 1670);
    TestTrue(TEXT("Roads create a narrow service strip with a feathered shoulder"),
        RoadCenter.A > 0 && RoadFeather.A > 0 && RoadFeather.A < RoadCenter.A && RoadOutside.A == 0);
    TestEqual(TEXT("A service road ends locally instead of painting an infinite line"),
        CinderTerrainSurface::Sample(ServiceTerrain, 1200, 1600).A, uint8(0));
    TestEqual(TEXT("Pads and roads leave distant terrain without service ground"),
        CinderTerrainSurface::Sample(ServiceTerrain, 3000, 3000).A, uint8(0));
    TArray<uint8> ServicePixels, RepeatedServicePixels;
    CinderTerrainSurface::BuildPixels(ServiceTerrain, ServicePixels);
    CinderTerrainSurface::BuildPixels(ServiceTerrain, RepeatedServicePixels);
    if (!TestEqual(TEXT("Service mask preserves the complete BGRA texture dimensions"), ServicePixels.Num(),
        CinderTerrainSurface::TextureSize * CinderTerrainSurface::TextureSize * 4)) return false;
    TestTrue(TEXT("Identical known pad and road inputs produce identical packed masks"),
        ServicePixels == RepeatedServicePixels);
    bool bContainsService = false, bContainsClearGround = false, bPacksServiceAlpha = true;
    for (int32 Y = 0; Y < CinderTerrainSurface::TextureSize; ++Y)
        for (int32 X = 0; X < CinderTerrainSurface::TextureSize; ++X)
        {
            const uint8 Alpha = ServicePixels[(Y * CinderTerrainSurface::TextureSize + X) * 4 + 3];
            const FColor Expected = CinderTerrainSurface::Sample(ServiceTerrain,
                (X + 0.5f) * ServiceTerrain.WorldSize / CinderTerrainSurface::TextureSize,
                (Y + 0.5f) * ServiceTerrain.WorldSize / CinderTerrainSurface::TextureSize);
            bContainsService |= Alpha > 0;
            bContainsClearGround |= Alpha == 0;
            bPacksServiceAlpha &= Alpha == Expected.A;
        }
    TestTrue(TEXT("Packed texture alpha preserves local service coverage rather than becoming opaque everywhere"),
        bContainsService && bContainsClearGround && bPacksServiceAlpha);
    for (float WorldSize : {3600.0f, 6000.0f})
    {
        CinderTerrainSurface::FFeatures EdgeTerrain;
        EdgeTerrain.WorldSize = WorldSize;
        EdgeTerrain.ServicePads.Add({{WorldSize - 100.0f, WorldSize - 100.0f}, {70.0f, 70.0f}});
        TArray<uint8> EdgePixels;
        CinderTerrainSurface::BuildPixels(EdgeTerrain, EdgePixels);
        bool bActiveEdgePainted = false;
        for (int32 Y = CinderTerrainSurface::TextureSize - 12; Y < CinderTerrainSurface::TextureSize; ++Y)
            for (int32 X = CinderTerrainSurface::TextureSize - 12; X < CinderTerrainSurface::TextureSize; ++X)
                bActiveEdgePainted |= EdgePixels[(Y * CinderTerrainSurface::TextureSize + X) * 4 + 3] > 0;
        TestTrue(*FString::Printf(TEXT("The %.0f-unit terrain mask reaches its active far edge"), WorldSize),
            bActiveEdgePainted);
    }

    FGameFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderBattlefield& Battle = *Fixture.Battle;
    ACinderPlayerController& Controller = *Fixture.Controller;
    TestTrue(TEXT("BeginPlay fixture has observed resource presentation state"), !Battle.ResourceMemory.empty());
    auto BatchTint = [&](int32 BatchIndex, int32 MaterialSlot, FLinearColor& OutTint)
    {
        return Battle.Batches.IsValidIndex(BatchIndex) && Battle.Batches[BatchIndex].Mesh
            && Battle.Batches[BatchIndex].Mesh->GetMaterial(MaterialSlot)
            && Battle.Batches[BatchIndex].Mesh->GetMaterial(MaterialSlot)->GetVectorParameterValue(
                FMaterialParameterInfo(TEXT("Tint")), OutTint);
    };
    for (int32 Team = 0; Team < CinderTeamColors::Count; ++Team)
    {
        FLinearColor BodyTint, AccentTint;
        const int32 BodyBatch = 5 + Team * 4;
        const int32 AccentBatch = 5 + CinderTeamColors::Count * 4 + Team * 4;
        TestTrue(*FString::Printf(TEXT("Team %d fallback body exposes its material tint"), Team),
            BatchTint(BodyBatch, 0, BodyTint));
        TestTrue(*FString::Printf(TEXT("Team %d fallback body uses its stable faction color"), Team),
            BodyTint.Equals(CinderTeamColors::Color(Team), 0.001f));
        TestTrue(*FString::Printf(TEXT("Team %d fallback accent exposes its material tint"), Team),
            BatchTint(AccentBatch, 0, AccentTint));
        TestTrue(*FString::Printf(TEXT("Team %d fallback accent uses its stable faction color"), Team),
            AccentTint.Equals(CinderTeamColors::Accent(Team), 0.001f));
        for (int32 Other = 0; Other < Team; ++Other)
            TestFalse(TEXT("Each faction palette remains visually distinct"),
                CinderTeamColors::Color(Team).Equals(CinderTeamColors::Color(Other), 0.001f));
    }
    TestEqual(TEXT("BeginPlay exposes one complete fog snapshot to presentation consumers"),
        Battle.FogCells().Num(), Battle.FogDimension() * Battle.FogDimension());
    const uint64 InitialFogRevision = Battle.FogRevision();
    Battle.RenderState();
    TestEqual(TEXT("Rendering unchanged fog retains its revision"), Battle.FogRevision(), InitialFogRevision);

    TArray<uint8> RunFixture = {
        0, 0, 1, 1,
        2, 1, 1, 0,
        2, 2, 2, 2,
        9, 9, 9, 9
    };
    uint64 CachedRunRevision = MAX_uint64;
    int32 CachedRunDimension = 0;
    TArray<ACinderHUD::FMinimapFogRun> CachedRuns;
    TestTrue(TEXT("First fog snapshot builds retained minimap runs"),
        ACinderHUD::RefreshFogRuns(7, 4, RunFixture, CachedRunRevision, CachedRunDimension, CachedRuns));
    TestEqual(TEXT("Retained minimap runs preserve row boundaries and state changes"), CachedRuns.Num(), 7);
    TestTrue(TEXT("Retained minimap runs preserve exact first-row coverage"),
        CachedRuns.Num() >= 2 && CachedRuns[0].Y == 0 && CachedRuns[0].StartX == 0
        && CachedRuns[0].EndX == 2 && CachedRuns[0].State == 0
        && CachedRuns[1].Y == 0 && CachedRuns[1].StartX == 2
        && CachedRuns[1].EndX == 4 && CachedRuns[1].State == 1);
    TestFalse(TEXT("Repeated HUD frames reuse runs for the same fog revision"),
        ACinderHUD::RefreshFogRuns(7, 4, RunFixture, CachedRunRevision, CachedRunDimension, CachedRuns));
    RunFixture[0] = 2;
    TestTrue(TEXT("A new fog revision rebuilds retained minimap runs"),
        ACinderHUD::RefreshFogRuns(8, 4, RunFixture, CachedRunRevision, CachedRunDimension, CachedRuns));
    TArray<uint8> SmallerSnapshot = {0, 1, 1, 2};
    TestTrue(TEXT("A fog dimension change rebuilds retained minimap runs even at the same revision"),
        ACinderHUD::RefreshFogRuns(8, 2, SmallerSnapshot, CachedRunRevision, CachedRunDimension, CachedRuns));
    TestEqual(TEXT("Dimension changes retain complete row coverage"), CachedRuns.Num(), 4);
    TArray<uint8> InvalidSnapshot;
    TestTrue(TEXT("An invalidated fog snapshot refreshes the retained cache"),
        ACinderHUD::RefreshFogRuns(9, 4, InvalidSnapshot, CachedRunRevision, CachedRunDimension, CachedRuns));
    bool bInvalidSnapshotIsPrivate = CachedRuns.Num() == 4;
    for (const ACinderHUD::FMinimapFogRun& Run : CachedRuns)
        bInvalidSnapshotIsPrivate &= Run.StartX == 0 && Run.EndX == 4 && Run.State == 0;
    TestTrue(TEXT("Missing fog bytes render as fully unknown instead of leaking simulation state"),
        bInvalidSnapshotIsPrivate);

    Battle.Sim().reset({0, 42, false, 1});
    Battle.ResetPresentation();
    TestTrue(TEXT("Direct simulation replacement clears prior resource memory and pose samples"),
        Battle.ResourceMemory.empty() && Battle.EntityMotion.CachedEntities() == 0);
    TestTrue(TEXT("Presentation reset invalidates the shared fog snapshot exactly once"),
        Battle.FogCells().IsEmpty() && Battle.FogRevision() == InitialFogRevision + 1);
    Battle.ResetPresentation();
    TestEqual(TEXT("Repeated invalidation of an empty fog snapshot retains its revision"),
        Battle.FogRevision(), InitialFogRevision + 1);
    Battle.RenderState();
    TestTrue(TEXT("Rendering after reset publishes a fresh complete fog snapshot"),
        Battle.FogCells().Num() == Battle.FogDimension() * Battle.FogDimension()
        && Battle.FogRevision() == InitialFogRevision + 2);
    for (const Kind KindWithParts : {Kind::Worker, Kind::Striker, Kind::Lancer, Kind::Bastion, Kind::Mortar})
    {
        const int32 KindIndex = static_cast<int32>(KindWithParts);
        const bool bAvailable = Battle.MotionKindAvailable.IsValidIndex(KindIndex)
            && Battle.MotionKindAvailable[KindIndex] != 0;
        bool bAllParts = true, bAnyParts = false;
        for (int32 Team = 0; Team < CinderTeamColors::Count; ++Team)
            for (const FCinderMotionAssetPart& Part : CinderMotionAssetParts(KindWithParts))
            {
                const int32 Key = (KindIndex * CinderTeamColors::Count + Team) * static_cast<int32>(ECinderMotionPart::Count)
                    + static_cast<int32>(Part.Part);
                const bool bLoaded = Battle.MotionPartBatchIndices.IsValidIndex(Key)
                    && Battle.MotionPartBatchIndices[Key] != INDEX_NONE;
                bAllParts &= bLoaded; bAnyParts |= bLoaded;
            }
        TestTrue(TEXT("A motion kind loads every required part or retains its complete static model"),
            bAvailable ? bAllParts : !bAnyParts);
        TestTrue(TEXT("Motion completeness fallback always retains a body batch"),
            Battle.ModelBatchIndices[KindIndex * CinderTeamColors::Count] != INDEX_NONE);
    }
    const auto UploadStamp = [&]
    {
        const auto& Uploads = Battle.InstanceUploads;
        return std::array<uint64, 9>{ Uploads.Passes, Uploads.DeltaCalls, Uploads.Transforms,
            Uploads.Added, Uploads.Removed, Uploads.FullRebuilds, Uploads.UnchangedSkips,
            Uploads.StaticSkips, Battle.FogTextureUploads };
    };
    const auto FrameCap = [&](float EngineLimit = 0.0f, bool bForeground = true)
    {
        return UCinderGameEngine::ApplyStateCap(EngineLimit,
            UCinderGameEngine::ClassifyMatchState(&Battle, bForeground));
    };
    using FrameState = ECinderFramePacingState;
    const FrameState CappedStates[] = { FrameState::Gameplay, FrameState::Idle, FrameState::Background };
    const float ExpectedCaps[] = { 120.0f, 30.0f, 10.0f };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(CappedStates); ++Index)
    {
        TestEqual(TEXT("Unlimited engine policy receives the applicable state cap"),
            UCinderGameEngine::ApplyStateCap(0.0f, CappedStates[Index]), ExpectedCaps[Index]);
        TestEqual(TEXT("Negative unlimited engine policy receives the applicable state cap"),
            UCinderGameEngine::ApplyStateCap(-1.0f, CappedStates[Index]), ExpectedCaps[Index]);
        TestEqual(TEXT("A stricter engine cap is retained in every state"),
            UCinderGameEngine::ApplyStateCap(5.0f, CappedStates[Index]), 5.0f);
    }
    TestEqual(TEXT("Scope bypass retains an unlimited engine policy"), UCinderGameEngine::ApplyStateCap(0.0f, FrameState::Bypass), 0.0f);
    TestEqual(TEXT("Scope bypass retains a higher configured engine cap"), UCinderGameEngine::ApplyStateCap(144.0f, FrameState::Bypass), 144.0f);
    TestTrue(TEXT("Engine context detection bypasses a viewport-free automation engine"),
        GetDefault<UCinderGameEngine>()->GetFramePacingState() == FrameState::Bypass);
    TestTrue(TEXT("A foreground without a battlefield uses the idle policy"),
        UCinderGameEngine::ClassifyMatchState(nullptr, true) == FrameState::Idle);
    TestTrue(TEXT("Background policy takes precedence even without a battlefield"),
        UCinderGameEngine::ClassifyMatchState(nullptr, false) == FrameState::Background);
    TestTrue(TEXT("World begins at the menu"), Battle.IsMenu());
    TestEqual(TEXT("Actual menu state limits foreground presentation to 30 FPS"), FrameCap(), 30.0f);
    TestEqual(TEXT("Actual menu state limits background presentation to 10 FPS"), FrameCap(0.0f, false), 10.0f);
    TryArmModes(Controller);
    TestFalse(TEXT("Menu rejects gameplay targeting and placement modes"), HasPendingModes(Controller));
    const uint64 MenuTick = Battle.Sim().tick();
    const uint64 MenuHash = Battle.Sim().stateHash();
    const size_t MenuCommands = Battle.Sim().recording().size();
    const auto MenuUploads = UploadStamp();
    Battle.Tick(0.2f);
    TestEqual(TEXT("Menu prevents simulation advancement"), static_cast<uint64>(Battle.Sim().tick()), MenuTick);
    TestEqual(TEXT("Menu Tick leaves all authoritative state unchanged"), static_cast<uint64>(Battle.Sim().stateHash()), MenuHash);
    TestTrue(TEXT("Menu Tick records no commands"), Battle.Sim().recording().size() == MenuCommands);
    TestTrue(TEXT("Menu Tick submits no instance or fog uploads"), UploadStamp() == MenuUploads);

    Controller.ExecuteAction(TEXT("start"), 2);
    TestTrue(TEXT("Controller start exits menu and pause"), !Battle.IsMenu() && !Battle.IsPaused());
    TestEqual(TEXT("Controller start chooses map two"), Battle.MapIndex(), 2);
    TestEqual(TEXT("Start resets the simulation clock"), static_cast<uint64>(Battle.Sim().tick()), uint64(0));
    TestEqual(TEXT("Actual running match permits the 120 FPS gameplay cap"), FrameCap(), 120.0f);
    TestEqual(TEXT("Gameplay retains an existing 60 FPS engine limit"), FrameCap(60.0f), 60.0f);
    TestEqual(TEXT("Background gameplay is capped at 10 FPS"), FrameCap(0.0f, false), 10.0f);
    TestTrue(TEXT("Starting a match immediately repopulates rendering after idle"), Battle.InstanceUploads.Passes > MenuUploads[0]);
    const uint64 StartedRenderPasses = Battle.InstanceUploads.Passes;
    Battle.Tick(Simulation::Step);
    TestTrue(TEXT("Battlefield Tick advances active match"), Battle.Sim().tick() > 0);
    TestTrue(TEXT("Active Tick resumes rendering passes"), Battle.InstanceUploads.Passes > StartedRenderPasses);

    const int Ore = Battle.Sim().players()[0].ore;
    const size_t Recorded = Battle.Sim().recording().size();
    Controller.ExecuteAction(TEXT("train"), static_cast<int>(Kind::Worker));
    TestEqual(TEXT("Empty selection cannot pay for training"), Battle.Sim().players()[0].ore, Ore);
    TestTrue(TEXT("Rejected controller training is not recorded"), Battle.Sim().recording().size() == Recorded);
    TestTrue(TEXT("Rejected controller training produces feedback"), !Controller.Feedback().IsEmpty());

    Controller.Selected = EntitiesOfKind(Battle.Sim(), Kind::Worker);
    if (!TestTrue(TEXT("Pause-flush fixture has a real selected worker"), !Controller.Selected.empty())) return false;
    const Id PauseWorker = Controller.Selected.front();
    const Vec2 LastRenderedWorker = Battle.Sim().find(PauseWorker)->pos;
    UInstancedStaticMeshComponent* PauseWorkerComponent = nullptr;
    int32 PauseWorkerInstance = INDEX_NONE;
    TArray<UInstancedStaticMeshComponent*> PauseComponents;
    Battle.GetComponents<UInstancedStaticMeshComponent>(PauseComponents);
    for (UInstancedStaticMeshComponent* Component : PauseComponents)
    {
        if (!Component) continue;
        for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
        {
            FTransform Pose;
            if (Component->GetInstanceTransform(Index, Pose, true)
                && FMath::Abs(Pose.GetTranslation().X - LastRenderedWorker.x) < 0.01
                && FMath::Abs(Pose.GetTranslation().Y - LastRenderedWorker.y) < 0.01)
            { PauseWorkerComponent = Component; PauseWorkerInstance = Index; break; }
        }
        if (PauseWorkerComponent) break;
    }
    if (!TestNotNull(TEXT("Pause-flush fixture locates an actual centered worker instance"), PauseWorkerComponent)) return false;
    if (!TestTrue(TEXT("Pause-flush fixture issues a normal worker move"),
        IssueAtBattlefield(Battle, CommandType::Move, {PauseWorker}, Kind::Worker,
            {LastRenderedWorker.x + 160, LastRenderedWorker.y + 140}).accepted)) return false;
    const uint64 BeforePendingMovementRender = Battle.InstanceUploads.Passes;
    Battle.Sim().update(Simulation::Step);
    const Vec2 PendingWorkerPosition = Battle.Sim().find(PauseWorker)->pos;
    if (!TestTrue(TEXT("Actual simulation movement creates a pending worker pose"),
        FMath::Abs(PendingWorkerPosition.x - LastRenderedWorker.x) > 0.01
        || FMath::Abs(PendingWorkerPosition.y - LastRenderedWorker.y) > 0.01)) return false;
    FTransform StaleWorkerPose;
    TestTrue(TEXT("Worker instance still has the previous pose before the pause flush"),
        PauseWorkerComponent->GetInstanceTransform(PauseWorkerInstance, StaleWorkerPose, true)
        && FMath::Abs(StaleWorkerPose.GetTranslation().X - LastRenderedWorker.x) < 0.01
        && FMath::Abs(StaleWorkerPose.GetTranslation().Y - LastRenderedWorker.y) < 0.01);
    TestEqual(TEXT("Simulation-only movement has not submitted a presentation pass"), Battle.InstanceUploads.Passes, BeforePendingMovementRender);
    TryArmModes(Controller);
    TestTrue(TEXT("Active match permits a placement mode"), Controller.IsBuildMode());
    Controller.ExecuteAction(TEXT("pause"));
    TestEqual(TEXT("Entering pause flushes exactly one presentation pass"), Battle.InstanceUploads.Passes, BeforePendingMovementRender + uint64(1));
    FTransform PausedWorkerPose;
    TestTrue(TEXT("Pause flush submits the latest actual worker position"),
        PauseWorkerComponent->GetInstanceTransform(PauseWorkerInstance, PausedWorkerPose, true)
        && FMath::Abs(PausedWorkerPose.GetTranslation().X - PendingWorkerPosition.x) < 0.01
        && FMath::Abs(PausedWorkerPose.GetTranslation().Y - PendingWorkerPosition.y) < 0.01);
    TestEqual(TEXT("Pause flush resets the presentation interval"), Battle.RenderTimer, 0.0f);
    const auto EnteredPauseUploads = UploadStamp();
    const uint64 PausedTick = Battle.Sim().tick();
    TestTrue(TEXT("One pause action cancels modes and pauses the battlefield"), Battle.IsPaused() && !HasPendingModes(Controller));
    TestEqual(TEXT("Actual pause transition lowers foreground cap to 30 FPS"), FrameCap(), 30.0f);
    TestEqual(TEXT("A paused match in the background uses 10 FPS"), FrameCap(0.0f, false), 10.0f);
    TryArmModes(Controller);
    TestFalse(TEXT("Paused match rejects gameplay modes"), HasPendingModes(Controller));
    Controller.ExecuteAction(TEXT("pause"));
    TestTrue(TEXT("Explicit pause remains paused when repeated"), Battle.IsPaused());
    TestTrue(TEXT("Repeated pause submits no additional instance or fog work"), UploadStamp() == EnteredPauseUploads);
    const uint64 PausedHash = Battle.Sim().stateHash();
    const size_t PausedCommands = Battle.Sim().recording().size();
    const auto PausedUploads = UploadStamp();
    Battle.Tick(0.2f);
    TestEqual(TEXT("Paused battlefield does not advance"), static_cast<uint64>(Battle.Sim().tick()), PausedTick);
    TestEqual(TEXT("Paused Tick leaves all authoritative state unchanged"), static_cast<uint64>(Battle.Sim().stateHash()), PausedHash);
    TestTrue(TEXT("Paused Tick records no commands"), Battle.Sim().recording().size() == PausedCommands);
    TestTrue(TEXT("Paused Tick submits no instance or fog uploads"), UploadStamp() == PausedUploads);
    Controller.ExecuteAction(TEXT("resume"));
    TestEqual(TEXT("Actual resume transition restores the gameplay cap"), FrameCap(), 120.0f);
    Battle.Tick(Simulation::Step);
    TestTrue(TEXT("Controller resume permits advancement"), !Battle.IsPaused() && Battle.Sim().tick() > PausedTick);
    TestTrue(TEXT("Resume restores render submission on the next simulation step"), Battle.InstanceUploads.Passes > PausedUploads[0]);
    Controller.ExecuteAction(TEXT("attack"));
    TestTrue(TEXT("Active match can arm attack targeting"), Controller.IsAttackMoveMode());
    Controller.ExecuteAction(TEXT("pause"));
    TestTrue(TEXT("Pause also cancels attack targeting in one action"), Battle.IsPaused() && !HasPendingModes(Controller));
    Controller.ExecuteAction(TEXT("resume"));

    const auto Headquarters = EntitiesOfKind(Battle.Sim(), Kind::Headquarters);
    if (!TestTrue(TEXT("Starting headquarters exists"), Headquarters.size() == 1)) return false;
    if (!TestTrue(TEXT("Normal paid queue command accepted at battlefield boundary"),
        IssueAtBattlefield(Battle, CommandType::Train, Headquarters, Kind::Worker).accepted)) return false;
    TestTrue(TEXT("Queue command changed match economy before reset"), Battle.Sim().players()[0].ore < Ore);
    TryArmModes(Controller);
    Controller.ExecuteAction(TEXT("menu"));
    TestFalse(TEXT("Menu transition clears all pending gameplay modes"), HasPendingModes(Controller));
    const uint64 ReturnedMenuTick = Battle.Sim().tick();
    const uint64 ReturnedMenuHash = Battle.Sim().stateHash();
    const size_t ReturnedMenuCommands = Battle.Sim().recording().size();
    const auto ReturnedMenuUploads = UploadStamp();
    Battle.Tick(0.2f);
    TestTrue(TEXT("Returning to menu freezes the current match"), Battle.IsMenu() && Battle.Sim().tick() == ReturnedMenuTick);
    TestEqual(TEXT("Returning to menu reinstates the idle frame cap"), FrameCap(), 30.0f);
    TestEqual(TEXT("Returned-menu Tick preserves authoritative state"), static_cast<uint64>(Battle.Sim().stateHash()), ReturnedMenuHash);
    TestTrue(TEXT("Returned-menu Tick records no commands"), Battle.Sim().recording().size() == ReturnedMenuCommands);
    TestTrue(TEXT("Returned-menu Tick submits no instance or fog uploads"), UploadStamp() == ReturnedMenuUploads);
    Controller.ExecuteAction(TEXT("start"), 0);
    TestTrue(TEXT("A new match refreshes presentation after returning to the menu"), Battle.InstanceUploads.Passes > ReturnedMenuUploads[0]);
    Controller.Selected = EntitiesOfKind(Battle.Sim(), Kind::Worker);
    TryArmModes(Controller);
    TestTrue(TEXT("Reset fixture arms gameplay modes in an active match"), HasPendingModes(Controller));
    Controller.ExecuteAction(TEXT("start"), 0);
    TestFalse(TEXT("Start transition clears all pending gameplay modes"), HasPendingModes(Controller));
    TestEqual(TEXT("New match switches back to map zero"), Battle.MapIndex(), 0);
    TestEqual(TEXT("New match restores normal starting funds"), Battle.Sim().players()[0].ore, 500);
    TestEqual(TEXT("New match resets time again"), static_cast<uint64>(Battle.Sim().tick()), uint64(0));
    TestTrue(TEXT("New match clears old command recording"), Battle.Sim().recording().empty());
    for (const Entity& Entity : Battle.Sim().entities())
        TestTrue(TEXT("New match clears old production queues"), Entity.queue.empty());

    // The lifecycle test is a friend of the controller so it can exercise the
    // actual gesture reducer without inventing a local-player viewport. Seed
    // selection with real starting IDs; screen positions here test thresholds,
    // not world targeting, camera motion, or physical Alt/trackpad delivery.
    const auto GestureWorkers = EntitiesOfKind(Battle.Sim(), Kind::Worker);
    if (!TestTrue(TEXT("Gesture fixture has its normal five starting workers"), GestureWorkers.size() == 5)) return false;
    const FVector2D GestureStart(600, 400);
    const FVector2D LargeDrag(128, 64);
    Controller.Selected = GestureWorkers;
    Controller.PointerPressed(GestureStart, false, false);
    Controller.PointerMoved(GestureStart + LargeDrag, 0.1f);
    TestTrue(TEXT("Ordinary left drag enters rectangle selection"), Controller.IsSelecting() && !Controller.bPointerCameraPan);
    Controller.PointerReleased(GestureStart + LargeDrag);
    TestFalse(TEXT("Ordinary release ends rectangle selection"), Controller.IsSelecting());
    TestTrue(TEXT("Ordinary rectangle release replaces selection in this viewport-free fixture"), Controller.Selection().empty());

    const FVector2D PanOffsets[] = { FVector2D::ZeroVector, FVector2D(2, 1), LargeDrag };
    for (int32 Mode = 0; Mode < 3; ++Mode)
        for (const FVector2D& Offset : PanOffsets)
        {
            Controller.ResetInteraction(false);
            Controller.Selected = GestureWorkers;
            Controller.ExecuteAction(TEXT("box"));
            if (Mode == 1)
            {
                Controller.ExecuteAction(TEXT("buildmenu"));
                Controller.ExecuteAction(TEXT("build"), static_cast<int>(Kind::Foundry));
            }
            else if (Mode == 2) Controller.ExecuteAction(TEXT("attack"));
            const bool BuildBefore = Controller.IsBuildMode(), MenuBefore = Controller.bBuildMenu;
            const bool AttackBefore = Controller.IsAttackMoveMode(), BoxBefore = Controller.bBoxSelect;
            const FString FeedbackBefore = Controller.Feedback();
            const float LastTapBefore = Controller.LastTapTime;
            const uint64 HashBefore = Battle.Sim().stateHash();
            const size_t CommandsBefore = Battle.Sim().recording().size();
            Controller.PointerPressed(GestureStart, false, true);
            TestTrue(TEXT("Explicit desktop pan latches and overrides armed box selection"),
                Controller.bPointerCameraPan && !Controller.bGestureSelect && !Controller.bPointerTouch && !Controller.bPointerUI);
            // No modifier state is supplied after the press, as when Alt is
            // released before the mouse button. The latched gesture must persist.
            Controller.PointerMoved(GestureStart + Offset, 0.1f);
            TestFalse(TEXT("Desktop pan never enters rectangle selection"), Controller.IsSelecting());
            TestTrue(TEXT("Desktop pan remains latched until release"), Controller.bPointerCameraPan);
            TestEqual(TEXT("Only a pan beyond the movement threshold becomes a drag"), Controller.bDragging, Offset == LargeDrag);
            Controller.PointerReleased(GestureStart + Offset);
            TestTrue(TEXT("Pan release consumes and clears the pointer even below the drag threshold"),
                !Controller.bPointerDown && !Controller.bDragging && !Controller.bPointerCameraPan);
            TestTrue(TEXT("Every pan release preserves the selected workers"), Controller.Selection() == GestureWorkers);
            TestTrue(TEXT("Pan preserves build, attack, menu and box modes"), Controller.IsBuildMode() == BuildBefore
                && Controller.bBuildMenu == MenuBefore && Controller.IsAttackMoveMode() == AttackBefore
                && Controller.bBoxSelect == BoxBefore);
            TestEqual(TEXT("Pan release does not change click feedback"), Controller.Feedback(), FeedbackBefore);
            TestEqual(TEXT("Pan release does not prime a later double tap"), Controller.LastTapTime, LastTapBefore);
            TestEqual(TEXT("Pan neither orders units nor creates a foundation"), static_cast<uint64>(Battle.Sim().stateHash()), HashBefore);
            TestTrue(TEXT("Pan adds no authoritative command recording"), Battle.Sim().recording().size() == CommandsBefore);
        }

    Controller.ResetInteraction(false);
    Controller.Selected = GestureWorkers;
    const uint64 BeforeTierRejection = Battle.Sim().stateHash();
    const size_t CommandsBeforeTierRejection = Battle.Sim().recording().size();
    TestEqual(TEXT("Crucible prerequisite fixture starts at Tier 1"), Battle.Sim().players()[0].tier, 1);
    Controller.ExecuteAction(TEXT("build"), static_cast<int>(Kind::MotorPool));
    TestFalse(TEXT("Tier 1 cannot arm Crucible placement"), Controller.IsBuildMode());
    TestTrue(TEXT("Rejected Crucible placement explains the Tier 2 research requirement"),
        Controller.Feedback().Contains(TEXT("T2")) && Controller.Feedback().Contains(TEXT("TECH TIER"))
        && Controller.Feedback().Contains(TEXT("Resonator")));
    TestTrue(TEXT("Rejected Crucible placement preserves its selected builders"), Controller.Selection() == GestureWorkers);
    TestEqual(TEXT("Rejected Crucible placement leaves the simulation unchanged"), static_cast<uint64>(Battle.Sim().stateHash()), BeforeTierRejection);
    TestTrue(TEXT("Rejected Crucible placement records no command"), Battle.Sim().recording().size() == CommandsBeforeTierRejection);

    Controller.ResetInteraction(false);
    Controller.Selected = GestureWorkers;
    Controller.ExecuteAction(TEXT("build"), static_cast<int>(Kind::Foundry));
    if (!TestTrue(TEXT("Selected starting workers can arm plain mouse placement"),
        Controller.IsBuildMode() && Controller.BuildingKind() == Kind::Foundry)) return false;
    const uint64 BeforePlacementDrag = Battle.Sim().stateHash();
    const size_t CommandsBeforePlacementDrag = Battle.Sim().recording().size();
    const FString PlacementFeedback = Controller.Feedback();
    Controller.PointerPressed(GestureStart, false, false);
    TestTrue(TEXT("Ordinary placement press latches placement without desktop pan"), Controller.bPointerPlacement && !Controller.bPointerCameraPan);
    Controller.PointerMoved(GestureStart + LargeDrag, 0.1f);
    TestTrue(TEXT("Plain placement drag crosses the drag threshold without a selection box"), Controller.bDragging && !Controller.IsSelecting());
    TestTrue(TEXT("Plain placement drag keeps the selected builders while held"), Controller.Selection() == GestureWorkers);
    Controller.PointerReleased(GestureStart + LargeDrag);
    TestTrue(TEXT("Plain placement release ends the gesture without a selection box"),
        !Controller.bPointerDown && !Controller.bDragging && !Controller.bPointerPlacement && !Controller.IsSelecting());
    TestTrue(TEXT("Plain placement drag preserves the workers, placement mode and pending building"),
        Controller.Selection() == GestureWorkers && Controller.IsBuildMode() && Controller.BuildingKind() == Kind::Foundry);
    TestEqual(TEXT("Plain placement drag preserves placement guidance"), Controller.Feedback(), PlacementFeedback);
    TestEqual(TEXT("Plain placement drag neither builds nor issues another order"), static_cast<uint64>(Battle.Sim().stateHash()), BeforePlacementDrag);
    TestTrue(TEXT("Plain placement drag records no command"), Battle.Sim().recording().size() == CommandsBeforePlacementDrag);

    // Cancelling while the primary button is held must consume its later release,
    // even with no movement, rather than reinterpret it as a world-selection click.
    Controller.PointerPressed(GestureStart, false, false);
    Controller.MouseContext();
    TestFalse(TEXT("Secondary click cancels active placement"), Controller.IsBuildMode());
    TestTrue(TEXT("Secondary-click placement cancellation gives explicit feedback"), Controller.Feedback().Contains(TEXT("cancelled")));
    const FString CancelledFeedback = Controller.Feedback();
    Controller.PointerReleased(GestureStart);
    TestTrue(TEXT("Release after placement cancellation preserves builders and clears the gesture"),
        Controller.Selection() == GestureWorkers && !Controller.bPointerDown && !Controller.bPointerPlacement && !Controller.IsSelecting());
    TestTrue(TEXT("Cancelled placement release preserves the pending building without rearming"),
        !Controller.IsBuildMode() && Controller.BuildingKind() == Kind::Foundry);
    TestEqual(TEXT("Cancelled placement release preserves cancellation feedback"), Controller.Feedback(), CancelledFeedback);
    TestEqual(TEXT("Cancelled placement release cannot mutate the simulation"), static_cast<uint64>(Battle.Sim().stateHash()), BeforePlacementDrag);
    TestTrue(TEXT("Cancelled placement release records no command"), Battle.Sim().recording().size() == CommandsBeforePlacementDrag);

    Controller.ExecuteAction(TEXT("build"), static_cast<int>(Kind::Foundry));
    Controller.PointerPressed(GestureStart, false, false);
    TestTrue(TEXT("Placement can be rearmed before a pause"), Controller.IsBuildMode() && Controller.bPointerPlacement);
    Controller.ExecuteAction(TEXT("pause"));
    TestTrue(TEXT("Pause clears a held placement gesture immediately"), Battle.IsPaused()
        && !Controller.IsBuildMode() && !Controller.bPointerPlacement && !Controller.bPointerDown && !Controller.IsSelecting());
    Controller.PointerReleased(GestureStart);
    TestTrue(TEXT("Release after placement pause preserves the workers"), Controller.Selection() == GestureWorkers);
    TestEqual(TEXT("Paused placement release leaves the simulation unchanged"), static_cast<uint64>(Battle.Sim().stateHash()), BeforePlacementDrag);
    TestTrue(TEXT("Paused placement release records no command"), Battle.Sim().recording().size() == CommandsBeforePlacementDrag);
    Controller.ExecuteAction(TEXT("resume"));

    Controller.ResetInteraction(false);
    Controller.Selected = GestureWorkers;
    TestTrue(TEXT("Normal touch cases begin outside placement mode"), !Controller.IsBuildMode() && !Controller.bPointerPlacement);
    Controller.PointerPressed(GestureStart, true, true);
    TestTrue(TEXT("Touch ignores the desktop camera-pan modifier"), Controller.bPointerTouch && !Controller.bPointerCameraPan && !Controller.bGestureSelect);
    Controller.PointerMoved(GestureStart + LargeDrag, 0.1f);
    TestTrue(TEXT("A short one-finger drag keeps the ordinary touch pan path"), Controller.bDragging && !Controller.IsSelecting());
    Controller.PointerReleased(GestureStart + LargeDrag);
    TestTrue(TEXT("Touch pan preserves existing selection"), Controller.Selection() == GestureWorkers);

    Controller.ResetInteraction(false);
    Controller.Selected = GestureWorkers;
    const uint64 BeforeStationaryHold = Battle.Sim().stateHash();
    const size_t CommandsBeforeStationaryHold = Battle.Sim().recording().size();
    Controller.PointerPressed(GestureStart, true, false);
    Controller.PointerMoved(GestureStart, 0.5f);
    TestTrue(TEXT("A stationary touch hold arms selection once"), Controller.bLongPress && Controller.bGestureSelect && !Controller.bDragging);
    const float HoldFeedbackLife = Controller.FeedbackLife;
    Controller.PointerMoved(GestureStart, 0.1f);
    TestEqual(TEXT("An armed stationary hold does not refresh its feedback every frame"), Controller.FeedbackLife, HoldFeedbackLife);
    Controller.PointerReleased(GestureStart);
    TestTrue(TEXT("Stationary hold release retains selection and clears its gesture"),
        Controller.Selection() == GestureWorkers && !Controller.bPointerDown && !Controller.bLongPress && !Controller.IsSelecting());
    TestEqual(TEXT("Stationary hold release issues no terrain action"), static_cast<uint64>(Battle.Sim().stateHash()), BeforeStationaryHold);
    TestTrue(TEXT("Stationary hold release records no command"), Battle.Sim().recording().size() == CommandsBeforeStationaryHold);

    Controller.ResetInteraction(false);
    Controller.PointerPressed(GestureStart, true, false);
    Controller.PointerMoved(GestureStart, 0.5f);
    Controller.PointerMoved(GestureStart + LargeDrag, 0.01f);
    TestTrue(TEXT("Touch hold then drag still enters rectangle selection"), Controller.IsSelecting() && !Controller.bPointerCameraPan);
    Controller.PointerReleased(GestureStart + LargeDrag);
    TestTrue(TEXT("Touch selection release finishes the normal rectangle path"), !Controller.IsSelecting() && Controller.Selection().empty());

    Controller.ResetInteraction(false);
    Controller.Selected = GestureWorkers;
    Controller.ExecuteAction(TEXT("build"), static_cast<int>(Kind::Foundry));
    const uint64 BeforeTouchPlacement = Battle.Sim().stateHash();
    const size_t CommandsBeforeTouchPlacement = Battle.Sim().recording().size();
    const FString TouchPlacementFeedback = Controller.Feedback();
    Controller.PointerPressed(GestureStart, true, false);
    Controller.PointerMoved(GestureStart, 0.5f);
    TestTrue(TEXT("A held placement touch stays in placement instead of arming selection"),
        Controller.bPointerPlacement && !Controller.bLongPress && !Controller.bGestureSelect && Controller.Feedback() == TouchPlacementFeedback);
    Controller.PointerMoved(GestureStart + LargeDrag, 0.01f);
    Controller.PointerReleased(GestureStart + LargeDrag);
    TestTrue(TEXT("Touch placement drag retains its worker and pending building"),
        Controller.Selection() == GestureWorkers && Controller.IsBuildMode() && Controller.BuildingKind() == Kind::Foundry);
    TestEqual(TEXT("Touch placement drag issues no order"), static_cast<uint64>(Battle.Sim().stateHash()), BeforeTouchPlacement);
    TestTrue(TEXT("Touch placement drag records no command"), Battle.Sim().recording().size() == CommandsBeforeTouchPlacement);

    Controller.ResetInteraction(false);
    Controller.bMultiTouch = true; Controller.bTwoDown = true;
    Controller.TouchPressed(ETouchIndex::Touch1, FVector(GestureStart.X, GestureStart.Y, 0));
    TestTrue(TEXT("Fresh primary touch clears a completed multi-touch gesture"),
        Controller.bPointerDown && Controller.bPointerTouch && !Controller.bMultiTouch && !Controller.bTwoDown);
    Controller.PointerReleased(GestureStart);

    Controller.ResetInteraction(false);
    Controller.Selected = GestureWorkers;
    const uint64 BeforeSecondaryTouch = Battle.Sim().stateHash();
    const size_t CommandsBeforeSecondaryTouch = Battle.Sim().recording().size();
    Controller.PointerPressed(GestureStart, true, false);
    Controller.PointerMoved(GestureStart, 0.5f);
    TestTrue(TEXT("Secondary-contact fixture begins with armed touch selection"), Controller.bLongPress && Controller.bGestureSelect);
    Controller.TouchPressed(ETouchIndex::Touch2, FVector(GestureStart.X + 20, GestureStart.Y + 20, 0));
    TestTrue(TEXT("Secondary contact cancels pending one-finger selection intent"),
        Controller.bMultiTouch && !Controller.bLongPress && !Controller.bGestureSelect);
    Controller.PointerReleased(GestureStart);
    TestTrue(TEXT("Primary release after secondary contact retains selection"), Controller.Selection() == GestureWorkers && !Controller.IsSelecting());
    TestEqual(TEXT("Multi-touch release issues no terrain action"), static_cast<uint64>(Battle.Sim().stateHash()), BeforeSecondaryTouch);
    TestTrue(TEXT("Multi-touch release records no command"), Battle.Sim().recording().size() == CommandsBeforeSecondaryTouch);

    Controller.ResetInteraction(false);
    Controller.Selected = GestureWorkers;
    Controller.PointerPressed(GestureStart, true, false);
    Controller.PointerMoved(GestureStart, 0.5f);
    const uint64 BeforeBackground = Battle.Sim().stateHash();
    const size_t CommandsBeforeBackground = Battle.Sim().recording().size();
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Broadcast();
    TestTrue(TEXT("Application background pauses an active match and clears held touch state"), Battle.IsPaused()
        && !Controller.bPointerDown && !Controller.bLongPress && Controller.Selection() == GestureWorkers);
    Battle.Tick(0.2f);
    TestEqual(TEXT("Background pause freezes authoritative state"), static_cast<uint64>(Battle.Sim().stateHash()), BeforeBackground);
    FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Broadcast();
    TestTrue(TEXT("Foreground leaves the background pause for the player to resume"), Battle.IsPaused());
    TestTrue(TEXT("Background and foreground record no command"), Battle.Sim().recording().size() == CommandsBeforeBackground);
    Controller.ExecuteAction(TEXT("resume"));
    TestFalse(TEXT("Explicit Resume continues a background-paused match"), Battle.IsPaused());
    Controller.ExecuteAction(TEXT("pause"));
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Broadcast();
    FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Broadcast();
    TestTrue(TEXT("Foreground also preserves a pause chosen by the player"), Battle.IsPaused());
    const FString PausedFeedback = Controller.Feedback();
    Controller.PointerPressed(GestureStart, true, false);
    Controller.PointerMoved(GestureStart, 0.5f);
    TestTrue(TEXT("Paused gameplay cannot arm long-press selection or feedback"),
        !Controller.bLongPress && !Controller.bGestureSelect && Controller.Feedback() == PausedFeedback);
    Controller.PointerReleased(GestureStart);
    Controller.ExecuteAction(TEXT("resume"));

    Controller.ResetInteraction(false);
    Controller.Selected = GestureWorkers;
    Controller.PointerPressed(GestureStart, false, true);
    Controller.PointerMoved(GestureStart + LargeDrag, 0.1f);
    const uint64 BeforePanPause = Battle.Sim().stateHash();
    Controller.ExecuteAction(TEXT("pause"));
    TestTrue(TEXT("Pause immediately clears every active desktop-pan gesture flag"), Battle.IsPaused()
        && !Controller.bPointerDown && !Controller.bDragging && !Controller.bPointerCameraPan && !Controller.bGestureSelect);
    Controller.PointerReleased(GestureStart + LargeDrag);
    TestTrue(TEXT("Release after a paused pan preserves selection"), Controller.Selection() == GestureWorkers);
    TestEqual(TEXT("Release after pause cannot dispatch a stale order"), static_cast<uint64>(Battle.Sim().stateHash()), BeforePanPause);
    Controller.PointerPressed(GestureStart, false, true);
    TestFalse(TEXT("Paused match cannot latch desktop camera pan"), Controller.bPointerCameraPan);
    Controller.PointerReleased(GestureStart);
    Controller.ExecuteAction(TEXT("resume"));
    Controller.PointerPressed(GestureStart, false, true);
    Controller.PointerMoved(GestureStart + LargeDrag, 0.1f);
    Controller.ExecuteAction(TEXT("start"), 0);
    TestTrue(TEXT("New match clears pending desktop pan and selection"), !Controller.bPointerDown
        && !Controller.bDragging && !Controller.bPointerCameraPan && !Controller.bGestureSelect && Controller.Selection().empty());
    const uint64 AfterPanReset = Battle.Sim().stateHash();
    Controller.PointerReleased(GestureStart + LargeDrag);
    TestEqual(TEXT("Release from the prior match cannot alter the reset simulation"), static_cast<uint64>(Battle.Sim().stateHash()), AfterPanReset);
    TestTrue(TEXT("Reset leaves no command from the old pan gesture"), Battle.Sim().recording().empty());
    if (!HasAnyErrors()) AddInfo(TEXT("CINDERLINE_UE_INTEGRATION_GESTURES_PASS: ordinary mouse rectangle selection, nine desktop-pan mode/distance cases, plain placement drag/cancel/pause, Tier 2 Crucible rejection, touch drag/hold/placement/multi-touch, app background pause, player-pause preservation and reset; real gesture methods with explicit pan intent and starting worker selection. NullRHI does not prove viewport targeting, camera movement, physical modifier routing, or internal TapWorld non-invocation when deprojection is unavailable."));

    Battle.Sim().reset({0, 42, false, 1});
    Battle.ResetPresentation();
    const Id HiddenEnemy = Battle.Sim().debugSpawn(Kind::Striker, 1, {4400, 4400});
    Battle.RenderState();
    TestTrue(TEXT("Fog filtering runs before animation state can observe a hidden enemy"),
        !Battle.Sim().visible(0, {4400, 4400}) && !Battle.EntityMotion.HasSample(HiddenEnemy));
    Battle.Sim().debugSpawn(Kind::Scout, 0, {4280, 4400});
    Battle.Sim().update(Simulation::Step);
    Battle.RenderState();
    TestTrue(TEXT("A newly visible live enemy enters the bounded pose cache"),
        Battle.Sim().visible(0, {4400, 4400}) && Battle.EntityMotion.HasSample(HiddenEnemy));

    // Check actual renderer output through public actors/components. Skim has one
    // centered body instance in both the imported-model and primitive-fallback adapters.
    Battle.Sim().reset({0, 42, false, 1});
    Battle.ResetPresentation();
    auto& Sim = Battle.Sim();
    const Id FirstSkim = Sim.debugSpawn(Kind::Scout, 0, {1103, 1309});
    const Id SecondSkim = Sim.debugSpawn(Kind::Scout, 0, {1337, 1309});
    IssueAtBattlefield(Battle, CommandType::Hold, {FirstSkim, SecondSkim});
    Battle.RenderState();
    const Vec2 FirstStart = Sim.find(FirstSkim)->pos;
    auto AtPoint = [](const FTransform& Pose, Vec2 Point)
    {
        const FVector Position = Pose.GetTranslation();
        return FMath::Abs(Position.X - Point.x) < 0.01 && FMath::Abs(Position.Y - Point.y) < 0.01;
    };
    UInstancedStaticMeshComponent* SkimComponent = nullptr;
    TArray<UInstancedStaticMeshComponent*> Components;
    Battle.GetComponents<UInstancedStaticMeshComponent>(Components);
    for (UInstancedStaticMeshComponent* Component : Components)
    {
        if (!Component) continue;
        for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
        {
            FTransform Pose;
            if (Component->GetInstanceTransform(Index, Pose, true) && AtPoint(Pose, FirstStart))
            { SkimComponent = Component; break; }
        }
        if (SkimComponent) break;
    }
    if (!TestNotNull(TEXT("Spawned Skim has an actual centered mesh instance"), SkimComponent)) return false;
    auto HasSkimAt = [&](Vec2 Point)
    {
        int32 Matches = 0;
        for (int32 Index = 0; Index < SkimComponent->GetInstanceCount(); ++Index)
        {
            FTransform Pose;
            if (SkimComponent->GetInstanceTransform(Index, Pose, true) && AtPoint(Pose, Point)) ++Matches;
        }
        return Matches == 1;
    };
    TestEqual(TEXT("Two staged Skims render two body instances"), SkimComponent->GetInstanceCount(), 2);
    TestTrue(TEXT("Both starting Skim positions reach the component"), HasSkimAt(FirstStart) && HasSkimAt(Sim.find(SecondSkim)->pos));
    if (!TestTrue(TEXT("Renderer movement fixture accepts a normal move"),
        IssueAtBattlefield(Battle, CommandType::Move, {FirstSkim}, Kind::Worker, {1103, 1509}).accepted)) return false;
    if (!TestTrue(TEXT("Normal actor ticks move the staged Skim"), Fixture.TickUntil([&]
        { const Entity* E = Sim.find(FirstSkim); return E && E->pos.y > FirstStart.y + 80; }, 2))) return false;
    IssueAtBattlefield(Battle, CommandType::Hold, {FirstSkim});
    Battle.RenderState();
    const Vec2 MovedPosition = Sim.find(FirstSkim)->pos;
    TestEqual(TEXT("Moving a Skim preserves the rendered unit count"), SkimComponent->GetInstanceCount(), 2);
    TestTrue(TEXT("Rendered Skim follows its actual moved position"), HasSkimAt(MovedPosition));
    TestFalse(TEXT("Movement leaves no instance at the abandoned position"), HasSkimAt(FirstStart));
    const Id ThirdSkim = Sim.debugSpawn(Kind::Scout, 0, {1499, 1409});
    IssueAtBattlefield(Battle, CommandType::Hold, {ThirdSkim});
    Battle.RenderState();
    TestEqual(TEXT("A new Skim adds one rendered instance"), SkimComponent->GetInstanceCount(), 3);
    TestTrue(TEXT("Growth preserves old units and displays the new unit"),
        HasSkimAt(MovedPosition) && HasSkimAt(Sim.find(SecondSkim)->pos) && HasSkimAt(Sim.find(ThirdSkim)->pos));
    auto DefeatSkim = [&](Id Victim)
    {
        const Entity* E = Sim.find(Victim);
        if (!E || !E->alive()) return false;
        const Vec2 Position = E->pos;
        Command Attack;
        Attack.type = CommandType::Attack; Attack.team = 1; Attack.target = Victim;
        for (int32 Index = 0; Index < 4; ++Index)
            Attack.units.push_back(Sim.debugSpawn(Kind::Lancer, 1, {Position.x + 220, Position.y + Index * 30 - 45}));
        if (!Sim.command(Attack).accepted) return false;
        // Advance ordinary combat without submitting an intermediate render, so a
        // replacement can retain the same visible count while changing its membership.
        Sim.update(Simulation::Step);
        E = Sim.find(Victim);
        return !E || !E->alive();
    };
    if (!TestTrue(TEXT("Ordinary combat defeats the first rendered Skim"), DefeatSkim(FirstSkim))) return false;
    const Id Replacement = Sim.debugSpawn(Kind::Scout, 0, {1603, 1679});
    IssueAtBattlefield(Battle, CommandType::Hold, {Replacement});
    Battle.RenderState();
    TestEqual(TEXT("Death plus replacement retains three visible instances"), SkimComponent->GetInstanceCount(), 3);
    TestFalse(TEXT("Same-count replacement removes the defeated unit's old pose"), HasSkimAt(MovedPosition));
    TestTrue(TEXT("Same-count replacement renders every surviving and new unit"),
        HasSkimAt(Sim.find(SecondSkim)->pos) && HasSkimAt(Sim.find(ThirdSkim)->pos) && HasSkimAt(Sim.find(Replacement)->pos));
    const Vec2 ReplacementPosition = Sim.find(Replacement)->pos;
    if (!TestTrue(TEXT("A second ordinary defeat exercises count reduction"), DefeatSkim(Replacement))) return false;
    Battle.RenderState();
    TestEqual(TEXT("Death without replacement removes one rendered instance"), SkimComponent->GetInstanceCount(), 2);
    TestFalse(TEXT("Count reduction leaves no defeated-unit ghost"), HasSkimAt(ReplacementPosition));
    TestTrue(TEXT("Count reduction preserves both surviving unit poses"), HasSkimAt(Sim.find(SecondSkim)->pos) && HasSkimAt(Sim.find(ThirdSkim)->pos));
    TArray<FTransform> BeforeRepeat;
    for (int32 Index = 0; Index < SkimComponent->GetInstanceCount(); ++Index)
    {
        FTransform Pose;
        if (!TestTrue(TEXT("Renderer snapshot reads a real surviving instance"), SkimComponent->GetInstanceTransform(Index, Pose, true))) return false;
        BeforeRepeat.Add(Pose);
    }
    const uint64 BeforeRepeatHash = Sim.stateHash();
    Battle.RenderState(); Battle.RenderState();
    TestEqual(TEXT("Unchanged renders preserve the instance count"), SkimComponent->GetInstanceCount(), BeforeRepeat.Num());
    for (int32 Index = 0; Index < BeforeRepeat.Num(); ++Index)
    {
        FTransform Pose;
        TestTrue(TEXT("Unchanged renders preserve complete instance transforms"),
            SkimComponent->GetInstanceTransform(Index, Pose, true) && Pose.Equals(BeforeRepeat[Index], 0.0));
    }
    TestEqual(TEXT("Repeated rendering does not change authoritative state"), static_cast<uint64>(Sim.stateHash()), BeforeRepeatHash);
    if (!HasAnyErrors()) AddInfo(TEXT("CINDERLINE_UE_INSTANCE_UPDATES_PASS: actual component transforms/counts through movement, growth, same-count death/replacement, removal, and unchanged repeat render."));

    // A development-only siege fixture ends a real match through ordinary damage.
    // No health or winner is injected. Stage the simulation accumulator so the
    // winning adapter tick is shorter than its normal presentation interval.
    Controller.ExecuteAction(TEXT("start"), 0);
    Sim.reset({0, 42, false, 1});
    Battle.ResetPresentation();
    Battle.Tick(Simulation::Step);
    const auto FinalHeadquarters = EntitiesOfKind(Sim, Kind::Headquarters);
    if (!TestTrue(TEXT("Terminal-render fixture has one normal player headquarters"), FinalHeadquarters.size() == 1)) return false;
    const Id FinalHQ = FinalHeadquarters.front();
    const Vec2 FinalHQPosition = Sim.find(FinalHQ)->pos;
    Command FinalAttack;
    FinalAttack.type = CommandType::Attack; FinalAttack.team = 1; FinalAttack.target = FinalHQ;
    for (int32 Index = 0; Index < 40; ++Index)
    {
        const float Angle = 2 * PI * Index / 40;
        FinalAttack.units.push_back(Sim.debugSpawn(Kind::Mortar, 1,
            { FinalHQPosition.x + 420 * FMath::Cos(Angle), FinalHQPosition.y + 420 * FMath::Sin(Angle) }));
    }
    Battle.RenderState();
    const int32 FinalHQBatch = Battle.ModelBatchIndices[
        static_cast<int32>(Kind::Headquarters) * CinderTeamColors::Count];
    if (!TestTrue(TEXT("Terminal-render fixture has the validated headquarters model batch"),
        FinalHQBatch != INDEX_NONE)) return false;
    UInstancedStaticMeshComponent* FinalHQComponent = Battle.Batches[FinalHQBatch].Mesh;
    const auto InstancesAtFinalHQ = [&]
    {
        // World-space destruction effects can legitimately remain at this position.
        // This regression checks removal of the headquarters mesh itself.
        int32 Count = 0;
        for (int32 Index = 0; Index < FinalHQComponent->GetInstanceCount(); ++Index)
        {
            FTransform Pose;
            if (FinalHQComponent->GetInstanceTransform(Index, Pose, true) && AtPoint(Pose, FinalHQPosition)) ++Count;
        }
        return Count;
    };
    if (!TestTrue(TEXT("Headquarters is actually rendered before the final volley"), InstancesAtFinalHQ() > 0)) return false;
    if (!TestTrue(TEXT("Terminal-render fixture accepts an ordinary enemy attack"), Sim.command(FinalAttack).accepted)) return false;
    const uint64 BeforeFinalTick = Sim.tick();
    Sim.update(Simulation::Step * 0.8f);
    TestEqual(TEXT("Partial simulation step has not fired the staged volley"), static_cast<uint64>(Sim.tick()), BeforeFinalTick);
    TestEqual(TEXT("Terminal-render fixture starts with an empty presentation timer"), Battle.RenderTimer, 0.0f);
    const uint64 BeforeFinalRender = Battle.InstanceUploads.Passes;
    Battle.Tick(Simulation::Step * 0.2f);
    if (!TestEqual(TEXT("Normal siege damage ends the match on the short adapter tick"), Sim.winner(), 1)) return false;
    TestTrue(TEXT("The terminal tick removes the defeated headquarters through simulation combat"), !Sim.find(FinalHQ) || !Sim.find(FinalHQ)->alive());
    TestTrue(TEXT("A winning tick submits its final render below the normal interval"), Battle.InstanceUploads.Passes > BeforeFinalRender);
    TestEqual(TEXT("Terminal render removes the defeated headquarters instances"), InstancesAtFinalHQ(), 0);
    TestEqual(TEXT("Actual results state uses the 30 FPS foreground cap"), FrameCap(), 30.0f);
    TestEqual(TEXT("Actual results state uses the 10 FPS background cap"), FrameCap(0.0f, false), 10.0f);
    const auto ResultUploads = UploadStamp();
    const uint64 ResultHash = Sim.stateHash();
    const size_t ResultCommands = Sim.recording().size();
    Battle.Tick(0.2f); Battle.Tick(0.2f);
    TestTrue(TEXT("Subsequent results ticks submit no instance or fog uploads"), UploadStamp() == ResultUploads);
    TestEqual(TEXT("Results ticks leave authoritative state unchanged"), static_cast<uint64>(Sim.stateHash()), ResultHash);
    TestTrue(TEXT("Results ticks record no commands"), Sim.recording().size() == ResultCommands);
    Controller.ExecuteAction(TEXT("start"), 0);
    TestEqual(TEXT("Starting after results restores the gameplay frame cap"), FrameCap(), 120.0f);
    TestTrue(TEXT("Starting after results immediately refreshes the scene"), Battle.InstanceUploads.Passes > ResultUploads[0]);
    const uint64 AfterResultsStartRender = Battle.InstanceUploads.Passes;
    Battle.Tick(Simulation::Step);
    TestTrue(TEXT("New match ticks resume both simulation and rendering after results"),
        Sim.tick() > 0 && Battle.InstanceUploads.Passes > AfterResultsStartRender);
    if (!HasAnyErrors()) AddInfo(TEXT("CINDERLINE_UE_IDLE_WORK_POLICY_PASS: production state classifier/cap helpers, 120/30/10 transitions, stricter/unlimited engine limits, automation scope bypass, one pause-transition flush of actual pending movement, frozen menu/pause/results upload counters, final combat death rendered on a short tick, and new-match render recovery. This does not measure native focus delivery, presented FPS, GPU load or thermal behavior."));

    Controller.Rig = Fixture.WorldOwner.GetTestWorld()->SpawnActor<ACinderCamera>();
    if (!TestNotNull(TEXT("Help checks have a real camera rig"), Controller.Rig.Get())) return false;
    Controller.Selected = EntitiesOfKind(Sim, Kind::Worker);
    Controller.bBuildMenu = true;
    Controller.bPointerDown = true;
    Controller.ExecuteAction(TEXT("help"), 5);
    TestTrue(TEXT("Help pauses the active match and clears in-flight input"),
        Controller.IsHelpOpen() && Battle.IsPaused() && !Controller.bPointerDown && !HasPendingModes(Controller));
    TestEqual(TEXT("Contextual help opens the requested technology topic"), Controller.HelpPage(), 5);
    const uint64 HelpHash = Sim.stateHash();
    const uint64 HelpTick = Sim.tick();
    const float HelpZoom = Controller.Rig->Distance();
    Controller.ExecuteAction(TEXT("resume"));
    Controller.ExecuteAction(TEXT("start"), 1);
    Controller.ExecuteAction(TEXT("army"));
    Controller.ExecuteAction(TEXT("train"), static_cast<int32>(Kind::Worker));
    Controller.ZoomIn(); Controller.Home(); Controller.NudgeArrow(0);
    Battle.Tick(1);
    TestTrue(TEXT("Help blocks underlying actions, camera changes and simulation advancement"),
        Controller.IsHelpOpen() && Battle.IsPaused() && Sim.stateHash() == HelpHash
        && Sim.tick() == HelpTick && Controller.Rig->Distance() == HelpZoom);
    Controller.ExecuteAction(TEXT("helppage"), -1);
    TestEqual(TEXT("Help previous-page navigation wraps safely"), Controller.HelpPage(), CinderHelp::TopicCount - 1);
    Controller.ExecuteAction(TEXT("helpreference"), 1000);
    TestTrue(TEXT("Reference navigation remains in range"), Controller.HelpReference() >= 0 && Controller.HelpReference() < CinderHelp::ReferenceCount);
    Controller.ExecuteAction(TEXT("helpinput"), 1);
    TestTrue(TEXT("Touch instructions can be selected on desktop"), Controller.HelpUsesTouch());
    Controller.ExecuteAction(TEXT("helpinput"), 0);
    Controller.ApplicationWillEnterBackground(); Controller.ApplicationHasEnteredForeground();
    Controller.Confirm();
    TestTrue(TEXT("Closing help after foreground return keeps explicit pause"), !Controller.IsHelpOpen() && Battle.IsPaused());
    Controller.ExecuteAction(TEXT("resume"));
    TestTrue(TEXT("Resume works after help closes"), Controller.IsGameplayActive());
    Controller.ExecuteAction(TEXT("menu"));
    Controller.ExecuteAction(TEXT("help"));
    Controller.Escape();
    TestTrue(TEXT("Menu help closes back to the menu without starting or pausing a match"), Battle.IsMenu() && !Battle.IsPaused() && !Controller.IsHelpOpen());

    Controller.ExecuteAction(TEXT("tutorial"));
    TestTrue(TEXT("Training starts from the menu with no AI and a separate scenario seed"),
        Battle.Tutorial().IsActive() && !Sim.config().ai && Sim.config().seed == FCinderTutorial::Seed && !Battle.IsMenu());
    TestEqual(TEXT("Training preserves the normal starting ore"), Sim.players()[0].ore, 500);
    TestEqual(TEXT("Training preserves the normal five starting Drudges"), static_cast<int32>(EntitiesOfKind(Sim, Kind::Worker).size()), 5);
    int32 EnemyFoundries = 0, HeldEnemyEmbers = 0;
    for (const Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != 1) continue;
        EnemyFoundries += Entity.kind == Kind::Foundry && Entity.progress >= 1.0f ? 1 : 0;
        HeldEnemyEmbers += Entity.kind == Kind::Striker && Entity.order == Order::Hold ? 1 : 0;
    }
    TestTrue(TEXT("Training authors its finite opponent without enabling strategic AI"),
        EnemyFoundries == 1 && HeldEnemyEmbers == 1 && Battle.Tutorial().OpponentOrdersIssued() == 0
        && Battle.Tutorial().PracticeTarget() != 0 && !Sim.config().ai);
    TestTrue(TEXT("Automatic initial camera setup does not complete the first lesson"), Battle.Tutorial().Step() == ECinderTutorialStep::Camera);
    Controller.ZoomIn(); Controller.UpdateTutorial();
    TestTrue(TEXT("A real controller camera action advances the camera lesson"), Battle.Tutorial().Step() != ECinderTutorialStep::Camera);
    Controller.Selected = EntitiesOfKind(Sim, Kind::Worker);
    Controller.UpdateTutorial();
    const auto BeforeBadTraining = Battle.Tutorial().Step();
    Controller.Selected.clear();
    Controller.ExecuteAction(TEXT("train"), static_cast<int32>(Kind::Worker));
    Controller.UpdateTutorial();
    TestTrue(TEXT("Rejected training does not advance the walkthrough"), Battle.Tutorial().Step() == BeforeBadTraining);
    Controller.Selected = {EntitiesOfKind(Sim, Kind::Worker).front()};
    Command LearnGather;
    LearnGather.type = CommandType::Gather;
    for (const auto& Entity : Sim.entities())
        if (Entity.kind == Kind::Resource && Sim.explored(0, Entity.pos)) { LearnGather.target = Entity.id; break; }
    Controller.Issue(LearnGather);
    TestTrue(TEXT("Accepted controller gathering feeds the tutorial and waits for actual mining"),
        Fixture.TickUntil([&]
        {
            Controller.UpdateTutorial();
            return static_cast<int32>(Battle.Tutorial().Step()) > static_cast<int32>(ECinderTutorialStep::GatherOre);
        }, 20));
    Controller.ExecuteAction(TEXT("save"));
    TestTrue(TEXT("Training save action explains protection without entering the skirmish save branch"), Controller.Feedback().Contains(TEXT("skirmish save is kept")));
    Controller.ExecuteAction(TEXT("load"));
    TestTrue(TEXT("Training cannot silently replace itself with the saved skirmish"), Battle.Tutorial().IsActive() && Controller.Feedback().Contains(TEXT("End training")));
    Controller.ExecuteAction(TEXT("pause"));
    Controller.TutorialShortcut();
    TestTrue(TEXT("Restart first opens a paused confirmation"), Controller.IsTutorialRestartPending() && Battle.IsPaused());
    const uint64 RestartHash = Sim.stateHash();
    Controller.ExecuteAction(TEXT("start"), 2);
    Controller.ExecuteAction(TEXT("resume"));
    Controller.Escape();
    TestTrue(TEXT("Escape cancels restart without destroying training progress"),
        !Controller.IsTutorialRestartPending() && Battle.IsPaused() && Sim.stateHash() == RestartHash);
    Controller.ExecuteAction(TEXT("tutorialrestart"));
    Controller.Confirm();
    TestTrue(TEXT("Explicit restart resets objectives and interaction state"),
        Battle.Tutorial().Step() == ECinderTutorialStep::Camera && Sim.tick() == 0
        && !Battle.IsPaused() && Controller.Selection().empty() && !Controller.IsTutorialRestartPending());
    Controller.ExecuteAction(TEXT("tutorialend"));
    TestTrue(TEXT("End training returns to menu and clears objective state"), Battle.IsMenu() && !Battle.Tutorial().IsActive());
    Controller.ExecuteAction(TEXT("start"), 1);
    TestTrue(TEXT("A later normal skirmish restores AI and does not inherit training"), Sim.config().ai && !Battle.Tutorial().IsActive() && Battle.MapIndex() == 1);
    if (!HasAnyErrors()) AddInfo(TEXT("CINDERLINE_UE_HELP_TUTORIAL_LIFECYCLE_PASS: modal input isolation, explicit help resume, tutorial start/action tracking, rejected command, save/load routing, restart confirmation and normal-match restoration. No player save or completion preference was written."));
    if (!HasAnyErrors()) AddInfo(TEXT("CINDERLINE_UE_INTEGRATION_LIFECYCLE_PASS: transient world, real BeginPlay, controller transitions, paused adapter tick, paid queue reset."));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderEconomyAndProductionIntegration,
    "Cinderline.Integration.EconomyAndProduction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderEconomyAndProductionIntegration::RunTest(const FString& Parameters)
{
    using namespace CinderWorldIntegration;
    using namespace cinder;
    FGameFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderBattlefield& Battle = *Fixture.Battle;
    ACinderPlayerController& Controller = *Fixture.Controller;
    Controller.ExecuteAction(TEXT("start"), 0);
    const auto Workers = EntitiesOfKind(Battle.Sim(), Kind::Worker);
    const auto Headquarters = EntitiesOfKind(Battle.Sim(), Kind::Headquarters);
    if (!TestTrue(TEXT("Normal initial economy exists"), Workers.size() == 5 && Headquarters.size() == 1)) return false;
    const Id Worker = Workers.front(), HQ = Headquarters.front();
    const Vec2 Base = Battle.Sim().find(HQ)->pos;
    if (!TestTrue(TEXT("Workers can be stopped through normal command boundary"),
        IssueAtBattlefield(Battle, CommandType::Stop, Workers).accepted)) return false;
    Id Deposit = 0;
    for (const Entity& Entity : Battle.Sim().entities())
        if (Entity.kind == Kind::Resource && Entity.resource > 0 && Battle.Sim().visible(0, Entity.pos))
        { Deposit = Entity.id; break; }
    if (!TestTrue(TEXT("Starting vision reveals an actual deposit"), Deposit != 0)) return false;
    const float InitialDeposit = Battle.Sim().find(Deposit)->resource;
    const int InitialOre = Battle.Sim().players()[0].ore;
    if (!TestTrue(TEXT("Normal gather command accepted"),
        IssueAtBattlefield(Battle, CommandType::Gather, {Worker}, Kind::Worker, {}, Deposit).accepted)) return false;
    if (!TestTrue(TEXT("Battlefield ticks advance travel and harvesting"), Fixture.TickUntil([&]
        { const Entity* E = Battle.Sim().find(Worker); return E && E->carried > 0; }, 30))) return false;
    TestEqual(TEXT("Carried ore has not been credited early"), Battle.Sim().players()[0].ore, InitialOre);
    if (!TestTrue(TEXT("Worker returns a real deposit through adapter ticks"), Fixture.TickUntil([&]
        { return Battle.Sim().players()[0].stats.gathered > 0; }, 30))) return false;
    TestEqual(TEXT("Delivered ore reaches the player economy"), Battle.Sim().players()[0].ore,
        InitialOre + Battle.Sim().players()[0].stats.gathered);
    TestTrue(TEXT("Harvesting depleted the finite resource"), Battle.Sim().find(Deposit)->resource < InitialDeposit);
    bool ResourcePresentationUpdated = false;
    for (const Entity& Known : Battle.KnownResources())
        if (Known.id == Deposit && Known.resource == Battle.Sim().find(Deposit)->resource) ResourcePresentationUpdated = true;
    TestTrue(TEXT("Adapter resource memory reflects harvested ore"), ResourcePresentationUpdated);
    IssueAtBattlefield(Battle, CommandType::Stop, Workers);

    int Before = Battle.Sim().players()[0].ore;
    if (!TestTrue(TEXT("Paid worker training accepted"),
        IssueAtBattlefield(Battle, CommandType::Train, {HQ}, Kind::Worker).accepted)) return false;
    TestEqual(TEXT("Training charges the defined worker cost"), Battle.Sim().players()[0].ore, Before - definition(Kind::Worker).cost);
    TestEqual(TEXT("Queued worker reserves supply"), Battle.Sim().supply(0), 6);
    const float QueueRemaining = Battle.Sim().find(HQ)->queue.front().remaining;
    Controller.ExecuteAction(TEXT("pause"));
    Battle.Tick(0.2f);
    TestEqual(TEXT("Pause also stops production progress"), Battle.Sim().find(HQ)->queue.front().remaining, QueueRemaining);
    Controller.ExecuteAction(TEXT("resume"));
    if (!TestTrue(TEXT("Adapter ticks finish worker production"), Fixture.TickUntil([&]
        { return EntitiesOfKind(Battle.Sim(), Kind::Worker).size() == 6; }, definition(Kind::Worker).buildTime + 1))) return false;

    const uint64 BeforeEmptyBuild = Battle.Sim().stateHash();
    const size_t CommandsBeforeEmptyBuild = Battle.Sim().recording().size();
    TestTrue(TEXT("Build rejection fixture has an empty controller selection"), Controller.Selection().empty());
    Controller.ExecuteAction(TEXT("build"), static_cast<int>(Kind::Foundry));
    TestFalse(TEXT("Public build action rejects placement without a selected worker"), Controller.IsBuildMode());
    TestTrue(TEXT("Empty-selection build rejection asks for a Drudge"), Controller.Feedback().Contains(TEXT("Drudge")));
    TestEqual(TEXT("Empty-selection build rejection leaves the economy and units unchanged"),
        static_cast<uint64>(Battle.Sim().stateHash()), BeforeEmptyBuild);
    TestTrue(TEXT("Empty-selection build rejection records no command"), Battle.Sim().recording().size() == CommandsBeforeEmptyBuild);
    Vec2 Site;
    if (!TestTrue(TEXT("Existing worker has a visible legal build site"), FindBuildSite(Battle.Sim(), Worker, Base, Site))) return false;
    Before = Battle.Sim().players()[0].ore;
    if (!TestTrue(TEXT("Normal paid construction command accepted"),
        IssueAtBattlefield(Battle, CommandType::Build, {Worker}, Kind::Foundry, Site).accepted)) return false;
    TestEqual(TEXT("Construction charges the defined cost"), Battle.Sim().players()[0].ore, Before - definition(Kind::Foundry).cost);
    Controller.ExecuteAction(TEXT("cancelplacement"));
    TestFalse(TEXT("Controller leaves placement mode"), Controller.IsBuildMode());
    const auto Foundries = EntitiesOfKind(Battle.Sim(), Kind::Foundry);
    if (!TestTrue(TEXT("Construction creates one real producer"), Foundries.size() == 1)) return false;
    const Id Foundry = Foundries.front();
    const float FoundationProgress = Battle.Sim().find(Foundry)->progress;
    const Vec2 BuilderStart = Battle.Sim().find(Worker)->pos;
    TestEqual(TEXT("Paid foundation retains its assigned builder"), Battle.Sim().constructionWorker(Foundry), Worker);
    TestTrue(TEXT("Construction command sends the Drudge to the foundation"), Battle.Sim().find(Worker)->order == Order::Construct);
    TestFalse(TEXT("Distant builder is not constructing before arrival"), Battle.Sim().constructionActive(Foundry));
    Battle.Tick(0.2f);
    TestEqual(TEXT("Foundation makes no progress while builder approaches"), Battle.Sim().find(Foundry)->progress, FoundationProgress);
    TestFalse(TEXT("First approach ticks have not reached the construction site"), Battle.Sim().constructionActive(Foundry));
    const Vec2 BuilderAfterTravel = Battle.Sim().find(Worker)->pos;
    TestTrue(TEXT("Builder physically travels toward its construction site"),
        BuilderAfterTravel.x != BuilderStart.x || BuilderAfterTravel.y != BuilderStart.y);
    Before = Battle.Sim().players()[0].ore;
    TestFalse(TEXT("Unfinished structure cannot train"), IssueAtBattlefield(Battle, CommandType::Train, {Foundry}, Kind::Striker).accepted);
    TestEqual(TEXT("Rejected unfinished production spends nothing"), Battle.Sim().players()[0].ore, Before);
    bool AdvancedBeforeArrival = false;
    if (!TestTrue(TEXT("Builder reaches the site through battlefield ticks"), Fixture.TickUntil([&]
        {
            const bool Active = Battle.Sim().constructionActive(Foundry);
            if (!Active && Battle.Sim().find(Foundry)->progress > FoundationProgress) AdvancedBeforeArrival = true;
            return Active;
        }, 30))) return false;
    TestFalse(TEXT("Construction remains frozen throughout the builder approach"), AdvancedBeforeArrival);
    if (!TestTrue(TEXT("Nearby assigned builder advances construction"), Fixture.TickUntil([&]
        { return Battle.Sim().find(Foundry)->progress > FoundationProgress; }, 1))) return false;
    const float WorkingProgress = Battle.Sim().find(Foundry)->progress;
    Controller.ExecuteAction(TEXT("pause"));
    Battle.Tick(0.2f);
    TestEqual(TEXT("Match pause freezes active construction"), Battle.Sim().find(Foundry)->progress, WorkingProgress);
    TestEqual(TEXT("Match pause preserves the builder assignment"), Battle.Sim().constructionWorker(Foundry), Worker);
    Controller.ExecuteAction(TEXT("resume"));
    if (!TestTrue(TEXT("Normal stop order releases the builder"),
        IssueAtBattlefield(Battle, CommandType::Stop, {Worker}).accepted)) return false;
    TestEqual(TEXT("Stopped foundation has no assigned builder"), Battle.Sim().constructionWorker(Foundry), Id(0));
    TestFalse(TEXT("Stopped builder leaves construction inactive"), Battle.Sim().constructionActive(Foundry));
    for (int32 Tick = 0; Tick < 20; ++Tick) Battle.Tick(0.1f);
    TestEqual(TEXT("Unassigned foundation remains paused during an active match"), Battle.Sim().find(Foundry)->progress, WorkingProgress);
    Before = Battle.Sim().players()[0].ore;
    if (!TestTrue(TEXT("Normal resume construction order reassigns the Drudge"),
        IssueAtBattlefield(Battle, CommandType::ResumeConstruction, {Worker}, Kind::Worker, {}, Foundry).accepted)) return false;
    TestEqual(TEXT("Resuming an already-paid foundation costs no ore"), Battle.Sim().players()[0].ore, Before);
    TestEqual(TEXT("Resumed foundation restores its builder assignment"), Battle.Sim().constructionWorker(Foundry), Worker);
    TestTrue(TEXT("Resumed Drudge receives a construction order"), Battle.Sim().find(Worker)->order == Order::Construct);
    if (!TestTrue(TEXT("Resumed builder continues real construction"), Fixture.TickUntil([&]
        { return Battle.Sim().constructionActive(Foundry) && Battle.Sim().find(Foundry)->progress > WorkingProgress; }, 5))) return false;
    if (!TestTrue(TEXT("Construction finishes through battlefield tick"), Fixture.TickUntil([&]
        { const Entity* E = Battle.Sim().find(Foundry); return E && E->alive() && E->progress >= 1; }, definition(Kind::Foundry).buildTime + 2))) return false;
    TestEqual(TEXT("Completed construction releases the builder"), Battle.Sim().constructionWorker(Foundry), Id(0));
    TestFalse(TEXT("Completed foundation no longer reports active construction"), Battle.Sim().constructionActive(Foundry));
    TestTrue(TEXT("Builder without an earlier gather job becomes idle after completion"), Battle.Sim().find(Worker)->order == Order::Idle);
    Before = Battle.Sim().players()[0].ore;
    if (!TestTrue(TEXT("Completed producer accepts paid infantry training"),
        IssueAtBattlefield(Battle, CommandType::Train, {Foundry}, Kind::Striker).accepted)) return false;
    TestEqual(TEXT("Infantry cost charged once"), Battle.Sim().players()[0].ore, Before - definition(Kind::Striker).cost);
    if (!TestTrue(TEXT("Adapter ticks produce real infantry"), Fixture.TickUntil([&]
        { return !EntitiesOfKind(Battle.Sim(), Kind::Striker).empty(); }, definition(Kind::Striker).buildTime + 1))) return false;
    const Id Infantry = EntitiesOfKind(Battle.Sim(), Kind::Striker).front();
    Controller.SelectArmy();
    TestTrue(TEXT("Controller selects normally produced infantry without a viewport"),
        std::find(Controller.Selection().begin(), Controller.Selection().end(), Infantry) != Controller.Selection().end());
    Controller.ExecuteAction(TEXT("hold"));
    TestTrue(TEXT("Public controller dispatch reaches selected infantry"), Battle.Sim().find(Infantry)->order == Order::Hold);
    Controller.ExecuteAction(TEXT("stop"));
    const auto SelectedBeforePause = Controller.Selection();
    Controller.ExecuteAction(TEXT("pause"));
    const size_t PausedRecording = Battle.Sim().recording().size();
    Controller.ExecuteAction(TEXT("hold"));
    TestTrue(TEXT("Pause blocks controller command dispatch even with valid selection"),
        Battle.Sim().recording().size() == PausedRecording && Battle.Sim().find(Infantry)->order == Order::Idle);
    TestTrue(TEXT("Pause preserves valid selection"), Controller.Selection() == SelectedBeforePause);
    Controller.ExecuteAction(TEXT("resume"));
    TestTrue(TEXT("Resume preserves valid selection"), Controller.Selection() == SelectedBeforePause);

    // The production SaveMatch/LoadMatch wrappers target the player's real save.
    // Exercise only simulation persistence here, using an owned temporary file.
    FTemporarySnapshot Snapshot;
    if (!TestTrue(TEXT("Temporary snapshot directory created"), IFileManager::Get().MakeDirectory(*Snapshot.Directory, true))) return false;
    const uint64 SavedHash = Battle.Sim().stateHash();
    if (!TestTrue(TEXT("Transient integration snapshot saves"), Battle.Sim().save(TCHAR_TO_UTF8(*Snapshot.Path)))) return false;
    Battle.Tick(0.1f);
    TestTrue(TEXT("Live adapter state advances after snapshot"), Battle.Sim().stateHash() != SavedHash);
    if (!TestTrue(TEXT("Transient snapshot reloads"), Battle.Sim().load(TCHAR_TO_UTF8(*Snapshot.Path)))) return false;
    TestEqual(TEXT("Snapshot restores authoritative state"), static_cast<uint64>(Battle.Sim().stateHash()), SavedHash);
    Battle.RenderState();
    TestTrue(TEXT("Loaded state still renders discovered resources"), !Battle.KnownResources().empty());
    if (!HasAnyErrors()) AddInfo(FString::Printf(TEXT("CINDERLINE_UE_INTEGRATION_ECONOMY_PASS: gathered=%d, produced=%d, built=%d, ore=%d; paid commands, builder travel and pause/resume, actual actor ticks, selected-controller dispatch, temporary snapshot only."),
        Battle.Sim().players()[0].stats.gathered, Battle.Sim().players()[0].stats.produced,
        Battle.Sim().players()[0].stats.built, Battle.Sim().players()[0].ore));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderAIKnowledgeAndPersistenceIntegration,
    "Cinderline.Integration.AIKnowledgeAndPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderAIKnowledgeAndPersistenceIntegration::RunTest(const FString& Parameters)
{
    using namespace CinderWorldIntegration;
    using namespace cinder;
    FGameFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderBattlefield& Battle = *Fixture.Battle;
    Fixture.Controller->ExecuteAction(TEXT("start"), 0);
    if (!TestTrue(TEXT("Knowledge fixture runs the ordinary active opponent"), Battle.Sim().config().ai)) return false;
    const auto Workers = EntitiesOfKind(Battle.Sim(), Kind::Worker);
    const auto Headquarters = EntitiesOfKind(Battle.Sim(), Kind::Headquarters);
    if (!TestTrue(TEXT("Knowledge fixture uses the starting player economy"), Workers.size() == 5 && Headquarters.size() == 1)) return false;
    const Id Worker = Workers.front();
    const Vec2 Home = Battle.Sim().find(Headquarters.front())->pos;
    Vec2 OpponentHome;
    bool HasOpponentHome = false;
    for (const Entity& Entity : Battle.Sim().entities())
        if (Entity.alive() && Entity.team == 1 && Entity.kind == Kind::Headquarters)
        {
            OpponentHome = Entity.pos;
            HasOpponentHome = true;
            break;
        }
    if (!TestTrue(TEXT("Knowledge fixture has the normal opponent Anchor"), HasOpponentHome)) return false;
    auto WorkerSighting = [&]() -> const AISighting*
    {
        const auto& Sightings = Battle.Sim().aiSightings();
        const auto Found = std::find_if(Sightings.begin(), Sightings.end(), [&](const AISighting& Sighting)
            { return Sighting.id == Worker; });
        return Found == Sightings.end() ? nullptr : &*Found;
    };
    TestFalse(TEXT("Opponent cannot initially see the player scout worker"), Battle.Sim().visible(1, Battle.Sim().find(Worker)->pos));
    TestTrue(TEXT("Unseen starting worker has no AI sighting"), WorkerSighting() == nullptr);
    TestEqual(TEXT("Opponent has never observed the player's starting Anchor cell"),
        static_cast<uint64>(Battle.Sim().aiLastObserved(Home)), uint64(0));

    const float DX = Home.x - OpponentHome.x, DY = Home.y - OpponentHome.y;
    const float Distance = FMath::Sqrt(DX * DX + DY * DY);
    const float ApproachRadius = definition(Kind::Headquarters).vision * 0.75f;
    const Vec2 Approach{ OpponentHome.x + DX / Distance * ApproachRadius,
                         OpponentHome.y + DY / Distance * ApproachRadius };
    if (!TestTrue(TEXT("Starting Drudge accepts an ordinary scouting move"),
        IssueAtBattlefield(Battle, CommandType::Move, {Worker}, Kind::Worker, Approach).accepted)) return false;
    if (!TestTrue(TEXT("Opponent observes the Drudge after real travel into its vision"), Fixture.TickUntil([&]
        { return WorkerSighting() != nullptr; }, 70))) return false;
    const Entity* ObservedWorker = Battle.Sim().find(Worker);
    if (!TestTrue(TEXT("Observed scout is still a living starting worker"), ObservedWorker && ObservedWorker->alive())) return false;
    const AISighting Observed = *WorkerSighting();
    TestTrue(TEXT("Opponent sighting records the observed unit kind"), Observed.kind == Kind::Worker);
    TestTrue(TEXT("AI observation follows active match ticks"), Observed.lastSeenTick > 0 && Observed.lastSeenTick <= Battle.Sim().tick());
    TestTrue(TEXT("AI observes the worker through its current team vision"), Battle.Sim().visible(1, ObservedWorker->pos));
    TestTrue(TEXT("AI observation also timestamps the visible map cell"),
        Battle.Sim().aiLastObserved(Observed.pos) >= Observed.lastSeenTick);
    TestTrue(TEXT("Opponent continues its paid economy while observing"), Battle.Sim().players()[1].stats.gathered > 0);

    if (!TestTrue(TEXT("Observed Drudge accepts an ordinary retreat move"),
        IssueAtBattlefield(Battle, CommandType::Move, {Worker}, Kind::Worker, Home).accepted)) return false;
    if (!TestTrue(TEXT("Drudge returns outside opponent vision through actor ticks"), Fixture.TickUntil([&]
        {
            const Entity* Entity = Battle.Sim().find(Worker);
            return Entity && Entity->alive() && !Battle.Sim().visible(1, Entity->pos);
        }, 25))) return false;
    // Cross at least one AI update after vision is lost, then copy the remembered values.
    const float HiddenAt = Battle.Sim().time();
    if (!TestTrue(TEXT("Active opponent gets time to process the lost contact"), Fixture.TickUntil([&]
        { return Battle.Sim().time() >= HiddenAt + 3; }, 4))) return false;
    if (!TestTrue(TEXT("Opponent retains its recent lost-contact sighting"), WorkerSighting() != nullptr)) return false;
    const AISighting Remembered = *WorkerSighting();
    const float MemoryCheckAt = Battle.Sim().time();
    if (!TestTrue(TEXT("Hidden scout keeps moving during later AI updates"), Fixture.TickUntil([&]
        { return Battle.Sim().time() >= MemoryCheckAt + 3; }, 4))) return false;
    const Entity* HiddenWorker = Battle.Sim().find(Worker);
    if (!TestTrue(TEXT("Retreating worker remains alive and hidden"),
        HiddenWorker && HiddenWorker->alive() && !Battle.Sim().visible(1, HiddenWorker->pos))) return false;
    if (!TestTrue(TEXT("Recent mobile sighting survives additional AI decisions"), WorkerSighting() != nullptr)) return false;
    TestEqual(TEXT("Hidden movement does not refresh the AI's last-seen tick"),
        static_cast<uint64>(WorkerSighting()->lastSeenTick), static_cast<uint64>(Remembered.lastSeenTick));
    TestEqual(TEXT("Hidden movement does not reveal a new X position"), WorkerSighting()->pos.x, Remembered.pos.x);
    TestEqual(TEXT("Hidden movement does not reveal a new Y position"), WorkerSighting()->pos.y, Remembered.pos.y);
    const float HiddenDX = HiddenWorker->pos.x - Remembered.pos.x, HiddenDY = HiddenWorker->pos.y - Remembered.pos.y;
    TestTrue(TEXT("Remembered location differs from the worker's actual hidden location"), HiddenDX * HiddenDX + HiddenDY * HiddenDY > 200 * 200);
    TestEqual(TEXT("Scouting contact does not reveal the player's distant Anchor cell"),
        static_cast<uint64>(Battle.Sim().aiLastObserved(Home)), uint64(0));

    FTemporarySnapshot Snapshot;
    if (!TestTrue(TEXT("AI snapshot gets an owned temporary directory"), IFileManager::Get().MakeDirectory(*Snapshot.Directory, true))) return false;
    const uint64 SavedHash = Battle.Sim().stateHash();
    const uint64 SavedObservedCell = Battle.Sim().aiLastObserved(Remembered.pos);
    if (!TestTrue(TEXT("Active AI memory saves to the temporary snapshot"), Battle.Sim().save(TCHAR_TO_UTF8(*Snapshot.Path)))) return false;
    std::vector<uint64> ContinuedHashes;
    for (int32 Tick = 0; Tick < 60; ++Tick)
    {
        Battle.Tick(0.1f);
        ContinuedHashes.push_back(Battle.Sim().stateHash());
    }
    if (!TestTrue(TEXT("AI memory reloads from the temporary snapshot"), Battle.Sim().load(TCHAR_TO_UTF8(*Snapshot.Path)))) return false;
    TestEqual(TEXT("Reload restores authoritative AI state exactly"), static_cast<uint64>(Battle.Sim().stateHash()), SavedHash);
    if (!TestTrue(TEXT("Reload preserves the hidden-worker sighting"), WorkerSighting() != nullptr)) return false;
    TestEqual(TEXT("Reload preserves the remembered sighting time"),
        static_cast<uint64>(WorkerSighting()->lastSeenTick), static_cast<uint64>(Remembered.lastSeenTick));
    TestEqual(TEXT("Reload preserves the remembered X position"), WorkerSighting()->pos.x, Remembered.pos.x);
    TestEqual(TEXT("Reload preserves the remembered Y position"), WorkerSighting()->pos.y, Remembered.pos.y);
    TestEqual(TEXT("Reload preserves AI map observation time"),
        static_cast<uint64>(Battle.Sim().aiLastObserved(Remembered.pos)), SavedObservedCell);
    Battle.RenderState();
    bool IdenticalContinuation = true;
    for (const uint64 ExpectedHash : ContinuedHashes)
    {
        Battle.Tick(0.1f);
        if (Battle.Sim().stateHash() != ExpectedHash) IdenticalContinuation = false;
    }
    TestTrue(TEXT("Loaded active opponent repeats every authoritative hash across six seconds of actor ticks"), IdenticalContinuation);
    TestEqual(TEXT("AI persistence fixture finishes in an active match"), Battle.Sim().winner(), -1);
    if (!HasAnyErrors()) AddInfo(FString::Printf(TEXT("CINDERLINE_UE_INTEGRATION_AI_KNOWLEDGE_PASS: observed_worker=%u, first_seen_tick=%llu, retained_seen_tick=%llu, continuation_ticks=%d; ordinary scouting/retreat commands, active opponent, real actor ticks, temporary snapshot only."),
        Worker, static_cast<unsigned long long>(Observed.lastSeenTick),
        static_cast<unsigned long long>(Remembered.lastSeenTick), static_cast<int32>(ContinuedHashes.size())));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCombatFeedbackIntegration,
    "Cinderline.Integration.CombatFeedback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCombatFeedbackIntegration::RunTest(const FString& Parameters)
{
    using namespace CinderWorldIntegration;
    using namespace cinder;
    FGameFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderBattlefield& Battle = *Fixture.Battle;
    ACinderPlayerController& Controller = *Fixture.Controller;
    Controller.ExecuteAction(TEXT("start"), 0);

    // Explicit development fixtures isolate presentation event flow. Units are
    // spawned at full health; damage, healing and death come from real combat.
    // These fixtures do not establish paid-economy balance or physical UI proof.
    auto ResetCombatFixture = [&]()
    {
        Battle.Sim().reset({0, 42, false, 1});
        Battle.ResetPresentation();
        for (int Team = 0; Team < 2; ++Team)
        {
            Command Stop;
            Stop.type = CommandType::Stop; Stop.team = Team;
            for (const Entity& Entity : Battle.Sim().entities())
                if (Entity.alive() && Entity.team == Team && Entity.kind == Kind::Worker)
                    Stop.units.push_back(Entity.id);
            TestTrue(TEXT("Fixture stops starting workers through commands"), Battle.Sim().command(Stop).accepted);
        }
    };
    auto Hold = [&](int Team, const std::vector<Id>& Units)
    {
        Command Command;
        Command.type = CommandType::Hold; Command.team = Team; Command.units = Units;
        return Battle.Sim().command(Command).accepted;
    };
    auto SameCounters = [](const FCinderCombatFeedbackStats& A, const FCinderCombatFeedbackStats& B)
    {
        return A.ProcessedHighWater == B.ProcessedHighWater && A.ConsumedEvents == B.ConsumedEvents
            && A.WeaponRequests == B.WeaponRequests && A.ImpactRequests == B.ImpactRequests
            && A.DeathRequests == B.DeathRequests && A.HiddenEvents == B.HiddenEvents
            && A.OffscreenEvents == B.OffscreenEvents && A.CoalescedEvents == B.CoalescedEvents;
    };
    ResetCombatFixture();
    const Id Defender = Battle.Sim().debugSpawn(Kind::Bastion, 0, {1300, 700});
    const Id Attacker = Battle.Sim().debugSpawn(Kind::Lancer, 1, {1510, 700});
    const Id Healer = Battle.Sim().debugSpawn(Kind::Mender, 0, {1250, 820});
    if (!TestTrue(TEXT("Visible combat fixture accepts stationary combat orders"),
        Defender && Attacker && Healer && Hold(0, {Defender, Healer}) && Hold(1, {Attacker}))) return false;
    uint64 ObservedTypes[4] = {};
    auto CheckedVisibleTick = [&]()
    {
        const FCinderCombatFeedbackStats Before = Battle.CombatFeedbackStats();
        Battle.Tick(Simulation::Step);
        uint64 NewTypes[4] = {};
        uint64 NewEvents = 0;
        for (const Effect& Effect : Battle.Sim().effects())
        {
            if (Effect.id <= Before.ProcessedHighWater) continue;
            ++NewEvents;
            const int Type = static_cast<int>(Effect.type);
            if (!TestTrue(TEXT("Combat emits a recognized typed event"), Type >= 0 && Type < 4)) continue;
            ++NewTypes[Type]; ++ObservedTypes[Type];
            TestTrue(TEXT("Nearby combat event retains a visible endpoint"),
                Battle.Sim().effectVisible(Effect, 0, Effect.type == EffectType::Weapon));
            if (Effect.type == EffectType::Heal)
                TestTrue(TEXT("Actual healing produces a visible support link"), Battle.Sim().effectLinkVisible(Effect, 0));
        }
        const FCinderCombatFeedbackStats After = Battle.CombatFeedbackStats();
        TestEqual(TEXT("Actor Tick consumes each newly emitted retained event once"), After.ConsumedEvents - Before.ConsumedEvents, NewEvents);
        TestEqual(TEXT("Actor Tick advances its cursor to the simulation event ID"), After.ProcessedHighWater,
            static_cast<uint64>(Battle.Sim().lastEffectId()));
        const uint64 WeaponCue = NewTypes[static_cast<int>(EffectType::Weapon)] > 0 ? 1 : 0;
        const uint64 ImpactCue = NewTypes[static_cast<int>(EffectType::Impact)] > 0 ? 1 : 0;
        const uint64 DeathCue = NewTypes[static_cast<int>(EffectType::Death)] > 0 ? 1 : 0;
        TestEqual(TEXT("Visible weapon events request one shared cue per actor tick"), After.WeaponRequests - Before.WeaponRequests, WeaponCue);
        TestEqual(TEXT("Visible impacts request one shared cue per actor tick"), After.ImpactRequests - Before.ImpactRequests, ImpactCue);
        TestEqual(TEXT("Visible deaths request one shared cue per actor tick"), After.DeathRequests - Before.DeathRequests, DeathCue);
        const uint64 AudibleEvents = NewEvents - NewTypes[static_cast<int>(EffectType::Heal)];
        TestEqual(TEXT("Simultaneous cues coalesce while every event remains consumed"),
            After.CoalescedEvents - Before.CoalescedEvents, AudibleEvents - WeaponCue - ImpactCue - DeathCue);
        TestTrue(TEXT("Visible fixture drops no events for fog or viewport"), After.HiddenEvents == 0 && After.OffscreenEvents == 0);
        const uint64 HashBeforeConsume = Battle.Sim().stateHash();
        Battle.UpdateCombatFeedback();
        Battle.RenderState();
        Battle.UpdateCombatFeedback();
        TestTrue(TEXT("Repeated consumption and rendering do not replay retained effects"), SameCounters(After, Battle.CombatFeedbackStats()));
        TestEqual(TEXT("Feedback consumption and rendering leave authoritative state unchanged"),
            static_cast<uint64>(Battle.Sim().stateHash()), HashBeforeConsume);
    };

    // The engine wrapper initializes a GameInstance. Detach it only for this
    // synchronous call in the owned world, then restore it for normal teardown.
    // Its world context retains ownership while the optional audio path is absent.
    UWorld* World = Battle.GetWorld();
    UGameInstance* SavedGameInstance = World->GetGameInstance();
    World->SetGameInstance(nullptr);
    TestTrue(TEXT("Missing-audio fixture has no game-instance subsystem"), World->GetGameInstance() == nullptr);
    CheckedVisibleTick();
    World->SetGameInstance(SavedGameInstance);
    TestTrue(TEXT("Feedback requests survive an absent audio subsystem"),
        Battle.CombatFeedbackStats().WeaponRequests > 0 && Battle.CombatFeedbackStats().ImpactRequests > 0);
    TestTrue(TEXT("Actual opening exchange damages the defender and emits healing"),
        Battle.Sim().find(Defender)->hp < definition(Kind::Bastion).hp && ObservedTypes[static_cast<int>(EffectType::Heal)] > 0);

    Controller.ExecuteAction(TEXT("pause"));
    const uint64 PausedHash = Battle.Sim().stateHash();
    const FCinderCombatFeedbackStats PausedCounters = Battle.CombatFeedbackStats();
    Battle.Tick(0.2f);
    TestEqual(TEXT("Paused actor Tick freezes combat and retained effect lifetimes"), static_cast<uint64>(Battle.Sim().stateHash()), PausedHash);
    TestTrue(TEXT("Paused actor Tick emits no repeated combat feedback"), SameCounters(PausedCounters, Battle.CombatFeedbackStats()));
    Controller.ExecuteAction(TEXT("resume"));

    FTemporarySnapshot Snapshot;
    if (!TestTrue(TEXT("Combat snapshot gets an owned temporary directory"), IFileManager::Get().MakeDirectory(*Snapshot.Directory, true))) return false;
    const uint64 SavedHash = Battle.Sim().stateHash();
    const uint64 SavedEffectId = Battle.Sim().lastEffectId();
    if (!TestTrue(TEXT("Snapshot contains retained live combat effects"), !Battle.Sim().effects().empty())) return false;
    if (!TestTrue(TEXT("Combat snapshot saves outside the player's match path"), Battle.Sim().save(TCHAR_TO_UTF8(*Snapshot.Path)))) return false;
    for (int32 Tick = 0; Tick < 240; ++Tick)
    {
        const Entity* Enemy = Battle.Sim().find(Attacker);
        if (!Enemy || !Enemy->alive()) break;
        CheckedVisibleTick();
    }
    const Entity* Defeated = Battle.Sim().find(Attacker);
    TestTrue(TEXT("Normal combat kills the opposing fixture unit"), !Defeated || !Defeated->alive());
    for (int Type = 0; Type < 4; ++Type)
        TestTrue(TEXT("Actor ticks carried weapon, impact, heal and death events"), ObservedTypes[Type] > 0);
    const FCinderCombatFeedbackStats CompletedCombat = Battle.CombatFeedbackStats();
    const uint64 CompletedHealCount = ObservedTypes[static_cast<int>(EffectType::Heal)];
    TestEqual(TEXT("One defeated enemy produces one death cue request"), CompletedCombat.DeathRequests, uint64(1));
    TestTrue(TEXT("Healing is consumed silently without adding an attack or impact cue"),
        CompletedCombat.ConsumedEvents == CompletedCombat.WeaponRequests + CompletedCombat.ImpactRequests
            + CompletedCombat.DeathRequests + CompletedCombat.CoalescedEvents + CompletedHealCount);
    if (!TestTrue(TEXT("Temporary combat snapshot reloads"), Battle.Sim().load(TCHAR_TO_UTF8(*Snapshot.Path)))) return false;
    TestEqual(TEXT("Loading restores combat and effects exactly"), static_cast<uint64>(Battle.Sim().stateHash()), SavedHash);
    // Exercise the same public feedback reset used by LoadMatch, without calling
    // the production wrapper that reads the player's persistent match file.
    Battle.ResetFeedback();
    TestEqual(TEXT("Load reset snapshots the restored effect cursor"), Battle.CombatFeedbackStats().ProcessedHighWater, SavedEffectId);
    TestEqual(TEXT("Load reset clears diagnostic counters"), Battle.CombatFeedbackStats().ConsumedEvents, uint64(0));
    Battle.UpdateCombatFeedback();
    Battle.UpdateCombatFeedback();
    TestEqual(TEXT("Retained snapshot effects do not replay after load"), Battle.CombatFeedbackStats().ConsumedEvents, uint64(0));
    for (int32 Tick = 0; Tick < 40 && Battle.CombatFeedbackStats().ConsumedEvents == 0; ++Tick) CheckedVisibleTick();
    TestTrue(TEXT("New post-load combat events are consumed after the restored cursor"), Battle.CombatFeedbackStats().ConsumedEvents > 0);
    TestTrue(TEXT("Restored cursor accepts event IDs below the abandoned future"),
        Battle.CombatFeedbackStats().ProcessedHighWater < CompletedCombat.ProcessedHighWater);

    ResetCombatFixture();
    // A long-range enemy weapon is beyond the victim's vision. Its impact is
    // visible at the friendly victim, but its hidden muzzle must remain silent.
    const Id Siege = Battle.Sim().debugSpawn(Kind::Mortar, 1, {3400, 650});
    const Id Victim = Battle.Sim().debugSpawn(Kind::Worker, 0, {3990, 650});
    if (!TestTrue(TEXT("Fog fixture accepts ordinary hold orders"), Siege && Victim && Hold(1, {Siege}) && Hold(0, {Victim}))) return false;
    TestFalse(TEXT("Long-range attacker starts outside player vision"), Battle.Sim().visible(0, Battle.Sim().find(Siege)->pos));
    Battle.Tick(Simulation::Step);
    const FCinderCombatFeedbackStats FogCounters = Battle.CombatFeedbackStats();
    TestEqual(TEXT("Fog fixture consumes both authoritative events"), FogCounters.ConsumedEvents, uint64(2));
    TestEqual(TEXT("Hidden weapon endpoint requests no weapon cue"), FogCounters.WeaponRequests, uint64(0));
    TestEqual(TEXT("Visible victim requests one impact cue"), FogCounters.ImpactRequests, uint64(1));
    TestEqual(TEXT("Hidden weapon advances the cursor without a cue"), FogCounters.HiddenEvents, uint64(1));
    TestEqual(TEXT("Fog cursor still reaches the newest event"), FogCounters.ProcessedHighWater, static_cast<uint64>(Battle.Sim().lastEffectId()));
    TestEqual(TEXT("First real siege hit does not kill this victim"), FogCounters.DeathRequests, uint64(0));
    Effect HiddenWeapon;
    bool FoundHiddenWeapon = false;
    for (const Effect& Effect : Battle.Sim().effects())
        if (Effect.type == EffectType::Weapon) { HiddenWeapon = Effect; FoundHiddenWeapon = true; }
    if (!TestTrue(TEXT("Fog fixture retains its actual weapon event"), FoundHiddenWeapon)) return false;
    Battle.Sim().debugSpawn(Kind::Scout, 0, {3300, 700});
    TestTrue(TEXT("A later scout now reveals the old weapon position"), Battle.Sim().visible(0, HiddenWeapon.from));
    TestFalse(TEXT("Later scouting cannot reveal an event hidden when emitted"), Battle.Sim().effectVisible(HiddenWeapon, 0, true));
    Battle.UpdateCombatFeedback();
    Battle.RenderState();
    TestTrue(TEXT("Revealing terrain does not replay consumed hidden feedback"), SameCounters(FogCounters, Battle.CombatFeedbackStats()));
    Controller.ExecuteAction(TEXT("menu"));
    const uint64 MenuHash = Battle.Sim().stateHash();
    Battle.Tick(0.2f);
    TestEqual(TEXT("Menu transition freezes combat"), static_cast<uint64>(Battle.Sim().stateHash()), MenuHash);
    TestTrue(TEXT("Menu transition preserves match diagnostics without replaying effects"), SameCounters(FogCounters, Battle.CombatFeedbackStats()));
    Controller.ExecuteAction(TEXT("start"), 0);
    TestEqual(TEXT("New match clears the authoritative event sequence"), static_cast<uint64>(Battle.Sim().lastEffectId()), uint64(0));
    TestEqual(TEXT("New match clears the feedback cursor"), Battle.CombatFeedbackStats().ProcessedHighWater, uint64(0));
    TestEqual(TEXT("New match clears feedback counts"), Battle.CombatFeedbackStats().ConsumedEvents, uint64(0));
    if (!HasAnyErrors()) AddInfo(FString::Printf(TEXT("CINDERLINE_UE_INTEGRATION_COMBAT_FEEDBACK_PASS: weapon=%llu, impact=%llu, heal=%llu, death=%llu, hidden_weapon=%llu; development spawns, real combat actor ticks, exactly-once requests, pause/reset, temporary snapshot cursor, absent subsystem; no audible-output, viewport or physical-input proof."),
        static_cast<unsigned long long>(CompletedCombat.WeaponRequests), static_cast<unsigned long long>(CompletedCombat.ImpactRequests),
        static_cast<unsigned long long>(CompletedHealCount),
        static_cast<unsigned long long>(CompletedCombat.DeathRequests), static_cast<unsigned long long>(FogCounters.HiddenEvents)));
    return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
