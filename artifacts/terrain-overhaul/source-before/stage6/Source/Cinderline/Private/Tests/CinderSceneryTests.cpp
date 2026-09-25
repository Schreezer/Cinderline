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
    TestEqual(TEXT("The unexplored western cliff submits no authored body"),
        Initial.AuthoredCliffBodyInstances, 0);
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
    const Vec2 EnemyPoint{600, 4400};
    const Vec2 HiddenPoint{2400, 4400};
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

    for (int32 Map = 0; Map < 3; ++Map)
    for (MatchLength Length : {MatchLength::Short, MatchLength::Standard, MatchLength::Long})
    {
        Config MapConfig;
        MapConfig.map = Map;
        MapConfig.matchLength = Length;
        Fixture.Simulation.reset(MapConfig);
        Fixture.Scenery->Reset();
        for (const Obstacle& Obstacle : Fixture.Simulation.obstacles())
            Fixture.Simulation.debugSpawn(Kind::Scout, 0, Obstacle.center);
        for (const Obstacle& Obstacle : Fixture.Simulation.obstacles())
            TestTrue(*FString::Printf(TEXT("Map %d %s scout observation marks each obstacle center explored"),
                Map, ANSI_TO_TCHAR(matchLengthName(Length))),
                Fixture.Simulation.explored(0, Obstacle.center));

        Fixture.Scenery->Update(Fixture.Simulation, {}, false);
        const FCinderSceneryDiagnostics Diagnostics = Fixture.Scenery->Diagnostics();
        const int32 AuthoredObstacles = Map == 0 ? 1 : 0;
        TestEqual(TEXT("Only the selected western map-zero cliff uses the authored body"),
            Diagnostics.AuthoredCliffObstacles, AuthoredObstacles);
        TestEqual(TEXT("Each authored cliff has four overlapping dominant masses"),
            Diagnostics.AuthoredCliffBodyInstances, AuthoredObstacles * 4);
        TestEqual(TEXT("Each authored cliff has two embedded buttresses"),
            Diagnostics.AuthoredCliffButtressInstances, AuthoredObstacles * 2);
        if (AuthoredObstacles > 0)
            TestTrue(TEXT("Authored full bodies retain 220-340 cm heights on flat terrain"),
                Diagnostics.MinimumAuthoredCliffHeightCm >= 220.0f
                    && Diagnostics.MaximumAuthoredCliffHeightCm <= 340.0f
                    && Diagnostics.MaximumAuthoredCliffHeightCm >= 320.0f);
        TestTrue(*FString::Printf(TEXT("Map %d %s flat fallback obstacles produce a layered cliff composition"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Diagnostics.TallRockInstances >= static_cast<int32>(Fixture.Simulation.obstacles().size()) * 4);
        TestTrue(*FString::Printf(TEXT("Map %d %s obstacles receive contiguous low cliff-mass segments"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Diagnostics.CliffMassInstances >= static_cast<int32>(Fixture.Simulation.obstacles().size()) * 2);
        TestTrue(*FString::Printf(TEXT("Map %d %s authored rocks remain layered over the cliff skirt"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Diagnostics.RockVariantInstances >= Diagnostics.CliffMassInstances);
        // Flat terrain still needs a tall mesh to communicate its blocked footprint.
        TestTrue(*FString::Printf(TEXT("Map %d %s flat fallback keeps the 240-340 cm blocked silhouette"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Diagnostics.MinimumMesaHeightCm >= 240.0f && Diagnostics.MaximumMesaHeightCm <= 340.0f
                && Diagnostics.MaximumMesaHeightCm >= 300.0f);
        TestEqual(*FString::Printf(TEXT("Map %d %s tall bounds stay inside authoritative obstacles"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Diagnostics.OutOfBoundsTallInstances, 0);
        TestTrue(*FString::Printf(TEXT("Map %d %s keeps the fixed mobile instance caps"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Diagnostics.TallRockInstances <= 120 && Diagnostics.DebrisInstances <= 72
                && Diagnostics.IndustrialInstances <= 80);
        if (Map == 1)
            TestTrue(TEXT("The broad central map-one obstacle composes multiple rock rows without widening gameplay bounds"),
                Diagnostics.MultiRowMesaInstances > 0);

        Fixture.Scenery->Update(Fixture.Simulation, {}, true);
        const FCinderSceneryDiagnostics Relief = Fixture.Scenery->Diagnostics();
        TestEqual(TEXT("Changing the rendered terrain mode alone invalidates the scenery cache"),
            Relief.Rebuilds, Diagnostics.Rebuilds + 1);
        TestEqual(TEXT("Relief foundations exist only under authored cliff bodies"),
            Relief.CliffMassInstances, AuthoredObstacles);
        TestEqual(TEXT("Relief retains the same authored body population as flat fallback"),
            Relief.AuthoredCliffBodyInstances, Diagnostics.AuthoredCliffBodyInstances);
        TestEqual(TEXT("Relief retains embedded buttresses without stacking them"),
            Relief.AuthoredCliffButtressInstances, Diagnostics.AuthoredCliffButtressInstances);
        TestEqual(TEXT("Relief does not add landscape height to the authored body's dimensions"),
            Relief.MaximumAuthoredCliffHeightCm, Diagnostics.MaximumAuthoredCliffHeightCm);
        TestTrue(*FString::Printf(TEXT("Map %d %s plateau outcrops stay within 50-120 cm"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Relief.MinimumMesaHeightCm >= 50.0f && Relief.MaximumMesaHeightCm <= 120.0f);
        TestTrue(TEXT("Every explored obstacle receives crest dressing"),
            Relief.RockVariantInstances >= static_cast<int32>(Fixture.Simulation.obstacles().size()));
        TestTrue(TEXT("Sparse relief caps leave more plateau visible than the flat fallback wall"),
            Relief.RockVariantInstances < Diagnostics.RockVariantInstances);
        TestEqual(TEXT("Relief caps remain wholly inside authoritative obstacles"),
            Relief.OutOfBoundsTallInstances, 0);
        TestEqual(TEXT("Relief dressing retains fixed roles plus optional foliage"),
            Relief.BatchCount, Relief.bFoliageAvailable ? 13 : 12);
        TestEqual(TEXT("Relief dressing has no collision"), Relief.CollisionEnabledBatches, 0);
        if (AuthoredObstacles > 0)
        {
            // Inspect submitted mesh bounds rather than the construction counters: this
            // catches accidental stacking and nonzero legacy/primitive mesh pivots.
            const Obstacle& Authored = Fixture.Simulation.obstacles()[2];
            TArray<UInstancedStaticMeshComponent*> Batches;
            Fixture.Owner->GetComponents(Batches);
            int32 BodyPieces = 0;
            for (UInstancedStaticMeshComponent* Batch : Batches)
            {
                if (!Batch || !Batch->GetStaticMesh()) continue;
                const FString Name = Batch->GetName();
                if (!Name.StartsWith(TEXT("CinderScenery_SM_CinderScenery_Rock_"))
                    && !Name.StartsWith(TEXT("CinderScenery_SM_CinderScenery_CliffMass"))) continue;
                for (int32 Index = 0; Index < Batch->GetInstanceCount(); ++Index)
                {
                    FTransform Transform;
                    if (!Batch->GetInstanceTransform(Index, Transform, true)) continue;
                    const FBox Bounds = Batch->GetStaticMesh()->GetBoundingBox().TransformBy(Transform);
                    const FVector Center = Bounds.GetCenter();
                    if (FMath::Abs(Center.X - Authored.center.x) > Authored.half.x
                        || FMath::Abs(Center.Y - Authored.center.y) > Authored.half.y) continue;
                    ++BodyPieces;
                    TestTrue(TEXT("Submitted authored body bounds remain inside the blocked footprint"),
                        Bounds.Min.X >= Authored.center.x - Authored.half.x - 0.1f
                            && Bounds.Max.X <= Authored.center.x + Authored.half.x + 0.1f
                            && Bounds.Min.Y >= Authored.center.y - Authored.half.y - 0.1f
                            && Bounds.Max.Y <= Authored.center.y + Authored.half.y + 0.1f);
                    TestTrue(TEXT("Every body, footing and buttress shares the ground baseline"),
                        FMath::IsNearlyEqual(Bounds.Min.Z, -12.0, 0.1));
                    TestTrue(TEXT("Authored body tops stay below 340 cm without landscape stacking"),
                        Bounds.Max.Z <= 340.0f);
                }
            }
            TestEqual(TEXT("One foundation, four bodies and two buttresses form the western cliff"),
                BodyPieces, 7);
        }

        Fixture.Scenery->Update(Fixture.Simulation, {}, true);
        TestEqual(*FString::Printf(TEXT("Map %d %s obstacle geometry is cached after submission"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Fixture.Scenery->Diagnostics().Rebuilds, Relief.Rebuilds);
        Fixture.Scenery->Update(Fixture.Simulation, {}, false);
        const FCinderSceneryDiagnostics Restored = Fixture.Scenery->Diagnostics();
        TestEqual(TEXT("Returning to flat terrain restores the same deterministic wall count"),
            Restored.TallRockInstances, Diagnostics.TallRockInstances);
        TestEqual(TEXT("Returning to flat terrain restores its original rock height"),
            Restored.MaximumMesaHeightCm, Diagnostics.MaximumMesaHeightCm);
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
            && Swept.FoliageInstances <= 128 && Swept.TalusInstances <= 3);

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
        Full.TalusInstances <= 3);

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
