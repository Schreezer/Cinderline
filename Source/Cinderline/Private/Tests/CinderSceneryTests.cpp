#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Presentation/CinderLandscapeTerrain.h"
#include "Presentation/CinderScenery.h"
#include "Sim/Network.h"
#include "Tests/AutomationCommon.h"

namespace CinderSceneryTests
{
struct FSceneryFixture
{
    FTestWorldWrapper WorldOwner;
    AActor* Owner = nullptr;
    UCinderScenery* Scenery = nullptr;
    cinder::Simulation Simulation;

    bool Initialize(FAutomationTestBase& Test)
    {
        // This suite exercises the original rock formations and their fixed probe
        // coordinates. AuthoredMapScenery covers the revision-one walls and ramps.
        cinder::Config LegacyConfig;
        LegacyConfig.mapRevision = 0;
        Simulation.reset(LegacyConfig);
        if (!WorldOwner.CreateTestWorld(EWorldType::Game))
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        UWorld* World = WorldOwner.GetTestWorld();
        if (!Test.TestNotNull(TEXT("Transient scenery world exists"), World)) return false;
        Owner = World->SpawnActor<AActor>();
        if (!Test.TestNotNull(TEXT("Transient scenery owner spawned"), Owner)) return false;
        auto* Root = NewObject<USceneComponent>(Owner, TEXT("SceneryTestRoot"));
        Owner->SetRootComponent(Root);
        Root->RegisterComponent();
        Scenery = NewObject<UCinderScenery>(Owner, TEXT("SceneryUnderTest"));
        Scenery->RegisterComponent();
        Test.TestFalse(TEXT("A registered component is not initialized before it owns its ISM batches"),
            Scenery->IsInitialized());
        UTexture2D* FogMask = LoadObject<UTexture2D>(nullptr,
            TEXT("/Game/Art/Textures/VisualUpgrade/T_CinderFogDefaultV2.T_CinderFogDefaultV2"), nullptr, LOAD_NoWarn);
        Scenery->Initialize(Root, nullptr, FogMask);
        Test.TestTrue(TEXT("Initialize creates every fixed scenery batch"), Scenery->IsInitialized());
        const FCinderSceneryDiagnostics Diagnostics = Scenery->Diagnostics();
        Test.TestEqual(TEXT("Scenery retains its twelve required roles and only adds an available grass batch"),
            Diagnostics.BatchCount, Diagnostics.bFoliageAvailable ? 13 : 12);
        Test.TestEqual(TEXT("Every canyon role resolves independently to its authored, legacy, or primitive mesh"),
            Diagnostics.CanyonMeshBatches + Diagnostics.LegacyCanyonFallbackBatches
                + Diagnostics.PrimitiveCanyonFallbackBatches,
            8);
        Test.TestEqual(TEXT("Every scenery batch remains presentation-only with collision disabled"),
            Diagnostics.CollisionEnabledBatches, 0);
        WorldOwner.ForwardErrorMessages(&Test);
        return !Test.HasAnyErrors();
    }
};

cinder::Entity KnownResource(cinder::Id Id, cinder::Vec2 Position)
{
    cinder::Entity Resource;
    Resource.id = Id;
    Resource.kind = cinder::Kind::Resource;
    Resource.team = -1;
    Resource.pos = Position;
    Resource.hp = 1;
    Resource.resource = 900;
    return Resource;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderSceneryObservedStateTest,
    "Cinderline.Presentation.SceneryObservedState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderSceneryObservedStateTest::RunTest(const FString& Parameters)
{
    using namespace CinderSceneryTests;
    using namespace cinder;
    FSceneryFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;

    Fixture.Simulation.debugSpawn(Kind::Scout, 0, Fixture.Simulation.obstacles().front().center);
    std::vector<Entity> KnownResources;
    Fixture.Scenery->Update(Fixture.Simulation, KnownResources);
    const FCinderSceneryDiagnostics Initial = Fixture.Scenery->Diagnostics();
    TestTrue(TEXT("An explored cliff receives its authored silhouette"),
        Initial.AuthoredCliffBodyInstances > 0);
    TestTrue(TEXT("Initial friendly completed base receives foundation dressing"),
        Initial.IndustrialInstances > 0);
    TestTrue(TEXT("Unexplored obstacle rectangles do not reveal cliff silhouettes"),
        Initial.TallRockInstances < static_cast<int32>(Fixture.Simulation.obstacles().size()) * 4);
    TestEqual(TEXT("First observed state produces one scenery rebuild"), Initial.Rebuilds, uint64(1));

    Fixture.Scenery->Update(Fixture.Simulation, KnownResources);
    TestEqual(TEXT("An identical observed state skips all instance rebuilding"),
        Fixture.Scenery->Diagnostics().Rebuilds, Initial.Rebuilds);

    const Vec2 FogBoundary{1349, 600};
    TestTrue(TEXT("Boundary resource center occupies a currently visible fog cell"),
        Fixture.Simulation.visible(0, FogBoundary));
    bool bRejectedOffsetFootprint = false;
    for (Id Id = 910000; Id < 910032 && !bRejectedOffsetFootprint; ++Id)
    {
        KnownResources = {KnownResource(Id, FogBoundary)};
        Fixture.Scenery->Update(Fixture.Simulation, KnownResources);
        bRejectedOffsetFootprint = Fixture.Scenery->Diagnostics().FogRejectedResourceInstances > 0;
    }
    TestTrue(TEXT("A visible resource center cannot expose a fragment whose full footprint enters hidden fog"),
        bRejectedOffsetFootprint);
    TestEqual(TEXT("Resource fog-boundary changes never upload static cliff batches"),
        Fixture.Scenery->Diagnostics().TallRockBatchUploads, Initial.TallRockBatchUploads);
    KnownResources.clear();
    Fixture.Scenery->Update(Fixture.Simulation, KnownResources);

    // Both points must be far from every building, not merely from each other, because the
    // industrial assertion below counts dressing globally and cannot say which building
    // produced it. Starting bases sit on the map corners - Simulation::reset places each at
    // (600|4200, 600|4200) - and a scout carries 780 vision against a 125-radius
    // headquarters, so any test point within ~905 of a corner silently drags a whole
    // starting base into the measurement. The old point was (4400, 4400): 283 from team 1's
    // headquarters, which is precisely how a resource assertion ended up billed for two
    // separate bases' worth of pads and crates. These two sit >= 1,800 from every corner and
    // from each other, and map 0's obstacles all end below y=3780, so both are clear ground.
    const Vec2 EnemyPoint{4200, 600};
    const Vec2 HiddenPoint{1200, 4400};
    TestFalse(TEXT("Remote enemy point starts outside local vision"),
        Fixture.Simulation.visible(0, EnemyPoint));
    TestFalse(TEXT("Remote test point starts outside local vision"), Fixture.Simulation.visible(0, HiddenPoint));
    const uint64 BeforeHiddenEnemy = Fixture.Scenery->Diagnostics().Rebuilds;
    Fixture.Simulation.debugSpawn(Kind::Headquarters, 1, EnemyPoint);
    Fixture.Scenery->Update(Fixture.Simulation, KnownResources);
    const FCinderSceneryDiagnostics HiddenEnemy = Fixture.Scenery->Diagnostics();
    TestEqual(TEXT("A hidden enemy building does not change the scenery hash"), HiddenEnemy.Rebuilds, BeforeHiddenEnemy);
    TestEqual(TEXT("A hidden enemy building creates no industrial dressing"),
        HiddenEnemy.IndustrialInstances, Initial.IndustrialInstances);

    KnownResources.push_back(KnownResource(900001, HiddenPoint));
    Fixture.Scenery->Update(Fixture.Simulation, KnownResources);
    const FCinderSceneryDiagnostics HiddenResource = Fixture.Scenery->Diagnostics();
    TestEqual(TEXT("Remembering a currently hidden resource creates no visible resource debris"),
        HiddenResource.DebrisInstances, Initial.DebrisInstances);
    const uint64 HiddenResourceRebuilds = HiddenResource.Rebuilds;
    const uint64 HiddenResourceRockUploads = HiddenResource.TallRockBatchUploads;
    const uint64 HiddenResourceDebrisUploads = HiddenResource.DebrisBatchUploads;
    Fixture.Scenery->Update(Fixture.Simulation, KnownResources);
    TestEqual(TEXT("Stable hidden resource memory is cached"),
        Fixture.Scenery->Diagnostics().Rebuilds, HiddenResourceRebuilds);

    Fixture.Simulation.debugSpawn(Kind::Scout, 0, HiddenPoint);
    TestTrue(TEXT("A real friendly scout reveals the remembered resource point"),
        Fixture.Simulation.visible(0, HiddenPoint));
    Fixture.Scenery->Update(Fixture.Simulation, KnownResources);
    const FCinderSceneryDiagnostics RevealedResource = Fixture.Scenery->Diagnostics();
    TestEqual(TEXT("A newly visible known resource receives its bounded three-piece cluster"),
        RevealedResource.DebrisInstances, HiddenResource.DebrisInstances + 3);
    TestEqual(TEXT("A visible resource uses the soft terrain stain without adding a hard industrial basin"),
        RevealedResource.IndustrialInstances, HiddenResource.IndustrialInstances);
    TestEqual(TEXT("A resource visibility toggle leaves submitted cliff batches untouched"),
        RevealedResource.TallRockBatchUploads, HiddenResourceRockUploads);
    TestEqual(TEXT("A resource visibility toggle uploads only the changed debris batch once"),
        RevealedResource.DebrisBatchUploads, HiddenResourceDebrisUploads + 1);

    // Revealing an enemy building DOES dress it - a base you can see must not read as bare
    // ground - and that is exactly what the resource assertion above used to absorb by
    // accident. Asserting it on its own point turns a side effect nothing was watching into
    // a protected invariant, so the pair above and here now fail for different reasons.
    Fixture.Simulation.debugSpawn(Kind::Scout, 0, EnemyPoint);
    TestTrue(TEXT("A second scout reveals the enemy building site"),
        Fixture.Simulation.visible(0, EnemyPoint));
    Fixture.Scenery->Update(Fixture.Simulation, KnownResources);
    const FCinderSceneryDiagnostics RevealedEnemy = Fixture.Scenery->Diagnostics();
    TestTrue(TEXT("A newly visible enemy building receives its industrial dressing"),
        RevealedEnemy.IndustrialInstances > RevealedResource.IndustrialInstances);
    TestEqual(TEXT("A newly visible enemy building is counted as one more foreign site"),
        RevealedEnemy.ForeignBuildingSites, RevealedResource.ForeignBuildingSites + 1);

    const uint64 BeforeReset = RevealedEnemy.Rebuilds;
    Fixture.Scenery->Reset();
    const FCinderSceneryDiagnostics Reset = Fixture.Scenery->Diagnostics();
    TestTrue(TEXT("Reset retains the reusable initialized batch set"), Reset.bInitialized);
    TestEqual(TEXT("Reset clears every submitted scenery instance"), Reset.TotalInstances, 0);
    TestEqual(TEXT("Reset preserves actor-lifetime rebuild diagnostics"), Reset.Rebuilds, BeforeReset);
    Fixture.Scenery->Update(Fixture.Simulation, KnownResources);
    TestEqual(TEXT("The first update after reset rebuilds the current observed state"),
        Fixture.Scenery->Diagnostics().Rebuilds, BeforeReset + 1);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderSceneryObstacleBoundsTest,
    "Cinderline.Presentation.SceneryObstacleBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderSceneryObstacleBoundsTest::RunTest(const FString& Parameters)
{
    using namespace CinderSceneryTests;
    using namespace cinder;
    FSceneryFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;

    Fixture.Scenery->Update(Fixture.Simulation, {});
    const FCinderSceneryDiagnostics Unknown = Fixture.Scenery->Diagnostics();
    TestEqual(TEXT("A wholly unexplored authored cliff has no submitted body"),
        Unknown.AuthoredCliffBodyInstances, 0);
    Fixture.Scenery->Update(Fixture.Simulation, {});
    TestEqual(TEXT("An unchanged wholly unknown cliff does not rebuild"),
        Fixture.Scenery->Diagnostics().Rebuilds, Unknown.Rebuilds);

    // This scout sees the western cliff's near face, but its 780 cm sight radius
    // cannot reach the center's fog cell. A center-only gate leaves a low mound here.
    const Obstacle WesternCliff = Fixture.Simulation.obstacles()[2];
    Fixture.Simulation.debugSpawn(Kind::Scout, 0, {300.0f, 2400.0f});
    TestTrue(TEXT("The west-edge regression exposes part of the cliff footprint"),
        Fixture.Simulation.explored(0,
            {WesternCliff.center.x - WesternCliff.half.x + 20.0f, WesternCliff.center.y}));
    TestFalse(TEXT("The west-edge regression keeps the cliff center unexplored"),
        Fixture.Simulation.explored(0, WesternCliff.center));
    Fixture.Scenery->Update(Fixture.Simulation, {});
    const FCinderSceneryDiagnostics Partial = Fixture.Scenery->Diagnostics();
    TestEqual(TEXT("A newly known cliff edge invalidates cached scenery"),
        Partial.Rebuilds, Unknown.Rebuilds + 1);
    TestEqual(TEXT("Vertex-fogged authored bodies appear when the near face is explored"),
        Partial.AuthoredCliffBodyInstances, Unknown.bCanyonVertexFogAvailable ? 4 : 0);
    TestEqual(TEXT("The partial reveal includes the body's embedded buttresses"),
        Partial.AuthoredCliffButtressInstances, Unknown.bCanyonVertexFogAvailable ? 2 : 0);
    Fixture.Scenery->Update(Fixture.Simulation, {});
    TestEqual(TEXT("An unchanged partially revealed cliff remains cached"),
        Fixture.Scenery->Diagnostics().Rebuilds, Partial.Rebuilds);
    Fixture.Simulation.debugSpawn(Kind::Scout, 0, WesternCliff.center);
    Fixture.Scenery->Update(Fixture.Simulation, {});
    const FCinderSceneryDiagnostics FullyKnown = Fixture.Scenery->Diagnostics();
    TestEqual(TEXT("A fully explored body also appears in the missing-material fallback"),
        FullyKnown.AuthoredCliffBodyInstances, 4);
    if (Unknown.bCanyonVertexFogAvailable)
        TestEqual(TEXT("Revealing the center does not resubmit the already supplied full body"),
            FullyKnown.TallRockBatchUploads, Partial.TallRockBatchUploads);

    for (int32 Players : {2, 4})
    for (int32 Map = 0; Map < 3; ++Map)
    for (MatchLength Length : {MatchLength::Short, MatchLength::Standard, MatchLength::Long})
    {
        Config MapConfig;
        // Keep the full 18-variant legacy silhouette matrix as compatibility proof.
        // Revision one changes only Standard duel map zero, tested separately.
        MapConfig.mapRevision = 0;
        MapConfig.map = Map;
        MapConfig.playerCount = Players;
        MapConfig.matchLength = Length;
        Fixture.Simulation.reset(MapConfig);
        Fixture.Scenery->Reset();
        const float WorldSize = Fixture.Simulation.worldSize();
        const float WorldScale = WorldSize / Simulation::WorldSize;
        // Observe the complete footprint, including broad Long-map corners. Center
        // scouts alone do not cover those corners in the missing-fog fallback.
        for (int32 Y = 0; Y < 8; ++Y)
        for (int32 X = 0; X < 8; ++X)
            Fixture.Simulation.debugSpawn(Kind::Scout, 0,
                {(X + 0.5f) * WorldSize / 8.0f, (Y + 0.5f) * WorldSize / 8.0f});
        const int32 ObstacleCount = static_cast<int32>(Fixture.Simulation.obstacles().size());
        Fixture.Scenery->Update(Fixture.Simulation, {}, false);
        const FCinderSceneryDiagnostics Flat = Fixture.Scenery->Diagnostics();
        TestEqual(*FString::Printf(TEXT("%dp map %d %s supplies every canonical cliff body"),
            Players, Map, ANSI_TO_TCHAR(matchLengthName(Length))), Flat.AuthoredCliffObstacles, ObstacleCount);
        TestTrue(TEXT("Each formation contains multiple dominant masses and low foundations"),
            Flat.AuthoredCliffBodyInstances >= ObstacleCount * 3 && Flat.CliffMassInstances >= ObstacleCount * 2);
        TestTrue(TEXT("Dominant body heights remain inside the authored silhouette budget"),
            Flat.MinimumAuthoredCliffHeightCm >= 220.0f && Flat.MaximumAuthoredCliffHeightCm <= 340.0f);
        TestTrue(TEXT("Full-map scenery keeps its mobile population limits"),
            Flat.TallRockInstances <= 120 && Flat.DebrisInstances <= 72
                && Flat.IndustrialInstances <= 80 && Flat.TalusInstances <= 24 && Flat.FoliageInstances <= 128);
        TestEqual(TEXT("Construction bounds stay inside authoritative obstacles"), Flat.OutOfBoundsTallInstances, 0);

        Fixture.Scenery->Update(Fixture.Simulation, {}, true);
        const FCinderSceneryDiagnostics Relief = Fixture.Scenery->Diagnostics();
        TestEqual(TEXT("Changing terrain mode invalidates the placement cache once"), Relief.Rebuilds, Flat.Rebuilds + 1);
        TestEqual(TEXT("Authored silhouette population is independent of terrain mode"),
            Relief.TallRockInstances, Flat.TallRockInstances);
        TestEqual(TEXT("Terrain relief does not stack extra height onto cliff bodies"),
            Relief.MaximumAuthoredCliffHeightCm, Flat.MaximumAuthoredCliffHeightCm);
        TestEqual(TEXT("Canonical maps need no legacy landscape cap dressing"), Relief.MinimumMesaHeightCm, 0.0f);
        TestEqual(TEXT("Relief retains fixed roles plus optional foliage"),
            Relief.BatchCount, Relief.bFoliageAvailable ? 13 : 12);
        TestEqual(TEXT("Dressing has no collision"), Relief.CollisionEnabledBatches, 0);
        if (Map == 1)
            TestTrue(TEXT("Broad central formations use staggered rock rows"), Relief.MultiRowMesaInstances > 0);

        TArray<TArray<FBox>> MajorBounds;
        MajorBounds.SetNum(ObstacleCount);
        TArray<int32> FoundationCounts;
        FoundationCounts.Init(0, ObstacleCount);
        TArray<UInstancedStaticMeshComponent*> Batches;
        Fixture.Owner->GetComponents(Batches);
        for (UInstancedStaticMeshComponent* Batch : Batches)
        {
            if (!Batch || !Batch->GetStaticMesh()) continue;
            const FString Name = Batch->GetName();
            const bool bFoundation = Name.StartsWith(TEXT("CinderScenery_SM_CinderScenery_CliffMass"));
            if (!bFoundation && !Name.StartsWith(TEXT("CinderScenery_SM_CinderScenery_Rock_"))) continue;
            const FBox MeshBounds = Batch->GetStaticMesh()->GetBoundingBox();
            for (int32 Index = 0; Index < Batch->GetInstanceCount(); ++Index)
            {
                FTransform Transform;
                Batch->GetInstanceTransform(Index, Transform, true);
                const FBox Bounds = MeshBounds.TransformBy(Transform);
                int32 ContainingObstacle = INDEX_NONE;
                for (int32 ObstacleIndex = 0; ObstacleIndex < ObstacleCount; ++ObstacleIndex)
                {
                    const Obstacle& Cliff = Fixture.Simulation.obstacles()[ObstacleIndex];
                    if (Bounds.Min.X >= Cliff.center.x - Cliff.half.x - 0.1f
                        && Bounds.Max.X <= Cliff.center.x + Cliff.half.x + 0.1f
                        && Bounds.Min.Y >= Cliff.center.y - Cliff.half.y - 0.1f
                        && Bounds.Max.Y <= Cliff.center.y + Cliff.half.y + 0.1f)
                    {
                        ContainingObstacle = ObstacleIndex;
                        break;
                    }
                }
                TestTrue(TEXT("Every submitted cliff AABB fits a blocked rectangle"), ContainingObstacle != INDEX_NONE);
                TestTrue(TEXT("Bodies, foundation tiles and buttresses share one baseline"),
                    FMath::IsNearlyEqual(Bounds.Min.Z, -12.0, 0.1));
                TestTrue(TEXT("Submitted silhouettes stay within aircraft clearance"), Bounds.Max.Z <= 340.1);
                if (ContainingObstacle == INDEX_NONE) continue;
                const Obstacle& Cliff = Fixture.Simulation.obstacles()[ContainingObstacle];
                if (bFoundation)
                {
                    ++FoundationCounts[ContainingObstacle];
                    const float AlongSpan = Cliff.half.x >= Cliff.half.y ? Bounds.GetSize().X : Bounds.GetSize().Y;
                    TestTrue(TEXT("No foundation tile recreates a rectangle-wide continuous plinth"),
                        AlongSpan < 1.6f * FMath::Max(Cliff.half.x, Cliff.half.y));
                    TestTrue(TEXT("Foundation tiles remain low beneath the rock bodies"), Bounds.GetSize().Z <= 80.1);
                }
                else
                {
                    const FVector Dimensions = MeshBounds.GetSize() * Transform.GetScale3D().GetAbs();
                    TestTrue(TEXT("Authored rocks keep compact horizontal proportions"),
                        FMath::Max(Dimensions.X, Dimensions.Y) / FMath::Min(Dimensions.X, Dimensions.Y) <= 1.801);
                    if (Bounds.GetSize().Z >= 200.0)
                    {
                        TestTrue(TEXT("Main rock width stays within its normalized size budget"),
                            Dimensions.X / WorldScale <= 340.1);
                        MajorBounds[ContainingObstacle].Add(Bounds);
                    }
                }
            }
        }
        for (int32 Index = 0; Index < ObstacleCount; ++Index)
        {
            const Obstacle& Cliff = Fixture.Simulation.obstacles()[Index];
            TestTrue(TEXT("Every blocked rectangle has several full-height rock masses"), MajorBounds[Index].Num() >= 3);
            TestTrue(TEXT("Every formation has overlapping low foundation tiles"), FoundationCounts[Index] >= 2);
            // Check the occupied silhouette, not exact transforms or generation formulas.
            // A sparse row of disconnected posts would leave these cross-section samples open.
            for (float Along : {-0.70f, -0.40f, 0.0f, 0.40f, 0.70f})
            {
                const bool bLongX = Cliff.half.x >= Cliff.half.y;
                const FVector Point(Cliff.center.x + (bLongX ? Along * Cliff.half.x : 0.0f),
                    Cliff.center.y + (bLongX ? 0.0f : Along * Cliff.half.y), 0.0);
                bool bOccupied = false;
                for (const FBox& Bounds : MajorBounds[Index])
                    if (Point.X >= Bounds.Min.X && Point.X <= Bounds.Max.X
                        && Point.Y >= Bounds.Min.Y && Point.Y <= Bounds.Max.Y) bOccupied = true;
                TestTrue(TEXT("Overlapping rock bodies keep the blocked center section visually occupied"), bOccupied);
            }
        }
        Fixture.Scenery->Update(Fixture.Simulation, {}, true);
        TestEqual(TEXT("Identical observed geometry remains cached"), Fixture.Scenery->Diagnostics().Rebuilds, Relief.Rebuilds);
        Fixture.Scenery->Update(Fixture.Simulation, {}, false);
        TestEqual(TEXT("Returning to flat terrain restores the same body population"),
            Fixture.Scenery->Diagnostics().TallRockInstances, Flat.TallRockInstances);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderSceneryGroundRoadFallbackTest,
    "Cinderline.Presentation.SceneryGroundRoadFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderSceneryGroundRoadFallbackTest::RunTest(const FString& Parameters)
{
    using namespace CinderSceneryTests;
    using namespace cinder;
    FSceneryFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;

    Fixture.Simulation.debugSpawn(Kind::Foundry, 0, {900.0f, 600.0f});
    Fixture.Scenery->Update(Fixture.Simulation, {});
    const FCinderSceneryDiagnostics Diagnostics = Fixture.Scenery->Diagnostics();
    if (Diagnostics.bCanyonGroundMaterialLoaded)
        TestEqual(TEXT("Terrain-layer canyon ground suppresses raised rectangular road meshes"),
            Diagnostics.RoadInstances, 0);
    else
        TestEqual(TEXT("Missing canyon ground retains the single legacy service-road mesh"),
            Diagnostics.RoadInstances, 1);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderSceneryAmbientScatterTest,
    "Cinderline.Presentation.SceneryAmbientScatter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderSceneryAmbientScatterTest::RunTest(const FString& Parameters)
{
    using namespace CinderSceneryTests;
    using namespace cinder;
    FSceneryFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;

    Fixture.Scenery->Update(Fixture.Simulation, {});
    const FCinderSceneryDiagnostics Start = Fixture.Scenery->Diagnostics();

    // A lone scout on ground nothing else in the observed state touches: no obstacle
    // centre enters vision, no building changes team or completion, and no resource is
    // passed at all. If the explored bit under a scatter candidate were missing from the
    // rebuild gate, this update would be skipped and the new ground would stay bare.
    const Vec2 EmptyGround{600, 3900};
    TestFalse(TEXT("Scatter probe point starts unexplored"),
        Fixture.Simulation.explored(0, EmptyGround));
    Fixture.Simulation.debugSpawn(Kind::Scout, 0, EmptyGround);
    Fixture.Scenery->Update(Fixture.Simulation, {});
    const FCinderSceneryDiagnostics Probed = Fixture.Scenery->Diagnostics();
    TestEqual(TEXT("Newly explored open ground alone forces exactly one scenery rebuild"),
        Probed.Rebuilds, Start.Rebuilds + 1);
    TestEqual(TEXT("Revealing open ground never invents authored cliff silhouettes"),
        Probed.TallRockInstances, Start.TallRockInstances);
    TestTrue(TEXT("Ambient scatter only ever grows as ground becomes explored"),
        Probed.AmbientScatterInstances >= Start.AmbientScatterInstances);

    // Sweep the playable surface so effectively every candidate passes the fog gate.
    for (int32 GridY = 0; GridY < 8; ++GridY)
    for (int32 GridX = 0; GridX < 8; ++GridX)
        Fixture.Simulation.debugSpawn(Kind::Scout, 0,
            {300.0f + GridX * 600.0f, 300.0f + GridY * 600.0f});
    Fixture.Scenery->Update(Fixture.Simulation, {});
    const FCinderSceneryDiagnostics Swept = Fixture.Scenery->Diagnostics();
    TestTrue(TEXT("A fully explored surface carries ambient scatter"),
        Swept.AmbientScatterInstances > Probed.AmbientScatterInstances);
    TestTrue(TEXT("Ambient scatter respects its share of the debris component"),
        Swept.AmbientScatterInstances <= 220);
    TestTrue(TEXT("Authored debris dressing is reported apart from ambient scatter"),
        Swept.DebrisInstances + Swept.AmbientScatterInstances + Swept.TalusInstances <= 400);
    TestEqual(TEXT("Scatter uses fixed roles alongside the optional grass batch"),
        Swept.BatchCount, Swept.bFoliageAvailable ? 13 : 12);
    TestEqual(TEXT("Scatter density never enables collision on a scenery batch"),
        Swept.CollisionEnabledBatches, 0);
    TestTrue(TEXT("The whole scenery population stays inside its mobile instance ceiling"),
        Swept.TotalInstances - Swept.FoliageInstances - Swept.TalusInstances <= 1300
            && Swept.FoliageInstances <= 128 && Swept.TalusInstances <= 24);

    const uint64 SweptRebuilds = Swept.Rebuilds;
    Fixture.Scenery->Update(Fixture.Simulation, {});
    TestEqual(TEXT("A fully explored surface caches its scatter instead of resubmitting"),
        Fixture.Scenery->Diagnostics().Rebuilds, SweptRebuilds);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderSceneryFoliageTest,
    "Cinderline.Presentation.SceneryFoliage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderSceneryFoliageTest::RunTest(const FString& Parameters)
{
    using namespace CinderSceneryTests;
    using namespace cinder;
    FSceneryFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    if (!Fixture.Scenery->Diagnostics().bFoliageAvailable)
    {
        Fixture.Scenery->Update(Fixture.Simulation, {});
        TestTrue(TEXT("Missing optional foliage leaves the original scenery initialized"),
            Fixture.Scenery->IsInitialized());
        TestEqual(TEXT("Missing grass assets never create a substitute plant batch"),
            Fixture.Scenery->Diagnostics().FoliageInstances, 0);
        AddWarning(TEXT("Optional grass assets are absent; placement and wind checks require the foliage import."));
        return !HasAnyErrors();
    }

    Simulation Source;
    Config Config;
    Config.mapRevision = 0;
    Config.ai = false;
    Source.reset(Config);
    for (int32 Y = 0; Y < 8; ++Y)
    for (int32 X = 0; X < 8; ++X)
        Source.debugSpawn(Kind::Scout, 0, {300.0f + X * 600.0f, 300.0f + Y * 600.0f});
    auto Snapshot = net::snapshotFor(Source, 0);
    if (!TestTrue(TEXT("The foliage fixture accepts its static observed snapshot"),
        Fixture.Simulation.applySnapshot(Snapshot))) return false;
    std::vector<Entity> Resources;
    for (const Entity& Entity : Fixture.Simulation.entities())
        if (Entity.kind == Kind::Resource) Resources.push_back(Entity);
    Fixture.Scenery->Update(Fixture.Simulation, Resources);
    const FCinderSceneryDiagnostics Full = Fixture.Scenery->Diagnostics();
    TestTrue(TEXT("An explored map supplies sparse grouped foliage within the mobile budget"),
        Full.FoliageInstances > 0 && Full.FoliageInstances <= 128);
    TestEqual(TEXT("Foliage adds only one component"), Full.BatchCount, 13);
    TestTrue(TEXT("Talus remains a small population separate from existing debris diagnostics"),
        Full.TalusInstances <= 24);

    TArray<UInstancedStaticMeshComponent*> Components;
    Fixture.Owner->GetComponents(Components);
    UInstancedStaticMeshComponent* Grass = nullptr;
    for (auto* Component : Components)
        if (Component->GetName().StartsWith(TEXT("CinderScenery_SM_CinderFrontier_Grass_A"))) Grass = Component;
    if (!TestNotNull(TEXT("The imported grass owns its optional instanced component"), Grass)) return false;
    TestEqual(TEXT("Grass never blocks gameplay"), Grass->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    TestFalse(TEXT("Grass never changes navigation"), Grass->CanEverAffectNavigation());
    TestFalse(TEXT("Grass does not add shadow casters"), Grass->CastShadow);
    TestTrue(TEXT("Grass culls before four thousand centimeters"), Grass->InstanceEndCullDistance < 4000);

    TArray<FTransform> Transforms;
    TArray<FBox> Footprints;
    TArray<TPair<Vec2, Vec2>> Routes;
    CinderLandscapeTerrain::GatherPathways(Fixture.Simulation, Routes);
    for (int32 Index = 0; Index < Grass->GetInstanceCount(); ++Index)
    {
        FTransform Transform;
        Grass->GetInstanceTransform(Index, Transform, true);
        Transforms.Add(Transform);
        const FBox Bounds = Grass->GetStaticMesh()->GetBoundingBox().TransformBy(Transform);
        const FBox Footprint = Bounds.ExpandBy(FVector(3.0, 3.0, 0.0));
        Footprints.Add(Footprint);
        const FVector Center = Footprint.GetCenter();
        for (const Obstacle& Cliff : Fixture.Simulation.obstacles())
            TestTrue(TEXT("A complete wind-expanded grass footprint clears blocked terrain"),
                Footprint.Max.X < Cliff.center.x - Cliff.half.x || Footprint.Min.X > Cliff.center.x + Cliff.half.x
                    || Footprint.Max.Y < Cliff.center.y - Cliff.half.y || Footprint.Min.Y > Cliff.center.y + Cliff.half.y);
        const float Radius = Footprint.GetExtent().Size2D();
        for (const Entity& Entity : Fixture.Simulation.entities())
        {
            if (!Entity.alive() || Entity.progress < 1.0f || !definition(Entity.kind).building) continue;
            const float Distance = FVector2D(Center.X - Entity.pos.x, Center.Y - Entity.pos.y).Size();
            TestTrue(TEXT("Grass leaves complete building pads readable"),
                Distance - Radius > definition(Entity.kind).radius * 1.25f);
        }
        for (const Entity& Ore : Resources)
            TestTrue(TEXT("Grass clears the center of every known mineral cluster"),
                FVector2D(Center.X - Ore.pos.x, Center.Y - Ore.pos.y).Size() - Radius > 120.0f);
        for (const auto& Route : Routes)
        {
            const FVector2D A(Route.Key.x, Route.Key.y), B(Route.Value.x, Route.Value.y), P(Center.X, Center.Y);
            const FVector2D Delta = B - A;
            const double Along = Delta.SizeSquared() > 1.0
                ? FMath::Clamp(FVector2D::DotProduct(P - A, Delta) / Delta.SizeSquared(), 0.0, 1.0) : 0.0;
            TestTrue(TEXT("Grass keeps open route centers clear through the complete wind footprint"),
                (P - A - Delta * Along).Size() - Radius > 80.0f);
        }
    }
    for (int32 Index = 0; Index < Transforms.Num(); ++Index)
    {
        bool bNeighbor = false;
        for (int32 Other = 0; Other < Transforms.Num(); ++Other)
            if (Other != Index && FVector::Dist2D(Transforms[Index].GetTranslation(),
                Transforms[Other].GetTranslation()) < 110.0f) bNeighbor = true;
        TestTrue(TEXT("Accepted grass remains in groups instead of isolated surviving dots"), bNeighbor);
    }

    // Advance only the observed clock: entities, fog, routes and remembered ore stay
    // identical. This makes a wind-only upload regression independent of unit AI.
    Snapshot.tick += 200;
    TestTrue(TEXT("The fixture advances the snapshot clock"), Fixture.Simulation.applySnapshot(Snapshot));
    Fixture.Scenery->Update(Fixture.Simulation, Resources);
    const FCinderSceneryDiagnostics ClockOnly = Fixture.Scenery->Diagnostics();
    TestEqual(TEXT("Wind-only time does not rebuild placement"), ClockOnly.Rebuilds, Full.Rebuilds);
    TestEqual(TEXT("Wind-only time never uploads any instance buffer"), ClockOnly.BatchUploads, Full.BatchUploads);
    auto* Wind = Cast<UMaterialInstanceDynamic>(Grass->GetMaterial(0));
    if (TestNotNull(TEXT("Grass wind uses a dynamic material"), Wind))
        TestEqual(TEXT("The material receives simulation seconds despite the cache early return"),
            Wind->K2_GetScalarParameterValue(TEXT("WindTime")), Fixture.Simulation.time());
    for (int32 Index = 0; Index < Transforms.Num(); ++Index)
    {
        FTransform Current;
        Grass->GetInstanceTransform(Index, Current, true);
        TestTrue(TEXT("Wind changes no submitted instance transform"), Current.Equals(Transforms[Index], 0.0));
    }

    // Hide a cell touched only by the edge of a real tuft; its center stays known.
    // The whole wind-expanded footprint must disappear, and restoring the same fog
    // state must reproduce the same deterministic transform.
    const float Cell = Fixture.Simulation.worldSize() / Simulation::FogSize;
    int32 HiddenCell = INDEX_NONE, Probe = INDEX_NONE;
    for (int32 Index = 0; Index < Footprints.Num() && HiddenCell == INDEX_NONE; ++Index)
    {
        const FBox& Bounds = Footprints[Index];
        const FVector Center = Bounds.GetCenter();
        const int32 CenterCell = FMath::FloorToInt(Center.Y / Cell) * Simulation::FogSize
            + FMath::FloorToInt(Center.X / Cell);
        for (int32 Y = FMath::FloorToInt(Bounds.Min.Y / Cell); Y <= FMath::FloorToInt(Bounds.Max.Y / Cell); ++Y)
        for (int32 X = FMath::FloorToInt(Bounds.Min.X / Cell); X <= FMath::FloorToInt(Bounds.Max.X / Cell); ++X)
            if (Y * Simulation::FogSize + X != CenterCell)
            {
                HiddenCell = Y * Simulation::FogSize + X;
                Probe = Index;
            }
    }
    if (!TestTrue(TEXT("A real tuft crosses a fog-cell boundary for the footprint regression"), Probe != INDEX_NONE))
        return false;
    auto HoledSnapshot = Snapshot;
    HoledSnapshot.fog[HiddenCell] = 0;
    TestTrue(TEXT("The fixture accepts a single unknown edge cell"), Fixture.Simulation.applySnapshot(HoledSnapshot));
    const FVector ProbeCenter = Footprints[Probe].GetCenter();
    TestTrue(TEXT("The rejected tuft's center remains explored"), Fixture.Simulation.explored(0,
        {static_cast<float>(ProbeCenter.X), static_cast<float>(ProbeCenter.Y)}));
    Fixture.Scenery->Update(Fixture.Simulation, Resources);
    auto ContainsTransform = [&](const FTransform& Wanted)
    {
        for (int32 Index = 0; Index < Grass->GetInstanceCount(); ++Index)
        {
            FTransform Current;
            Grass->GetInstanceTransform(Index, Current, true);
            if (Current.Equals(Wanted, 0.0)) return true;
        }
        return false;
    };
    TestFalse(TEXT("An unknown footprint cell removes the tuft even while its center is explored"),
        ContainsTransform(Transforms[Probe]));
    TestTrue(TEXT("Restoring explored cells succeeds"), Fixture.Simulation.applySnapshot(Snapshot));
    Fixture.Scenery->Update(Fixture.Simulation, Resources);
    TestTrue(TEXT("Restored fog recreates the exact original tuft placement"), ContainsTransform(Transforms[Probe]));
    return !HasAnyErrors();
}

#endif
