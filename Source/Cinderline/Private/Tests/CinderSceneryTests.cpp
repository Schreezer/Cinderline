#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Presentation/CinderScenery.h"
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
        Scenery->Initialize(Root);
        Test.TestTrue(TEXT("Initialize creates every fixed scenery batch"), Scenery->IsInitialized());
        Test.TestEqual(TEXT("Scenery uses one fixed component per authored mesh role"),
            Scenery->Diagnostics().BatchCount, 12);
        const FCinderSceneryDiagnostics Diagnostics = Scenery->Diagnostics();
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

    const Vec2 HiddenPoint{4400, 4400};
    TestFalse(TEXT("Remote test point starts outside local vision"), Fixture.Simulation.visible(0, HiddenPoint));
    const uint64 BeforeHiddenEnemy = Fixture.Scenery->Diagnostics().Rebuilds;
    Fixture.Simulation.debugSpawn(Kind::Headquarters, 1, HiddenPoint);
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
    TestEqual(TEXT("A resource visibility toggle leaves submitted cliff batches untouched"),
        RevealedResource.TallRockBatchUploads, HiddenResourceRockUploads);
    TestEqual(TEXT("A resource visibility toggle uploads only the changed debris batch once"),
        RevealedResource.DebrisBatchUploads, HiddenResourceDebrisUploads + 1);

    const uint64 BeforeReset = RevealedResource.Rebuilds;
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

        Fixture.Scenery->Update(Fixture.Simulation, {});
        const FCinderSceneryDiagnostics Diagnostics = Fixture.Scenery->Diagnostics();
        TestTrue(*FString::Printf(TEXT("Map %d %s obstacles produce a layered cliff composition"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Diagnostics.TallRockInstances >= static_cast<int32>(Fixture.Simulation.obstacles().size()) * 4);
        TestTrue(*FString::Printf(TEXT("Map %d %s obstacles receive contiguous low cliff-mass segments"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Diagnostics.CliffMassInstances >= static_cast<int32>(Fixture.Simulation.obstacles().size()) * 2);
        TestTrue(*FString::Printf(TEXT("Map %d %s authored rocks remain layered over the cliff skirt"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Diagnostics.RockVariantInstances >= Diagnostics.CliffMassInstances);
        TestTrue(*FString::Printf(TEXT("Map %d %s mesa relief stays in the 180-240 cm authored range"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Diagnostics.MinimumMesaHeightCm >= 180.0f && Diagnostics.MaximumMesaHeightCm <= 240.0f
                && Diagnostics.MaximumMesaHeightCm >= 210.0f);
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

        const uint64 BuiltAt = Diagnostics.Rebuilds;
        Fixture.Scenery->Update(Fixture.Simulation, {});
        TestEqual(*FString::Printf(TEXT("Map %d %s obstacle geometry is cached after submission"),
            Map, ANSI_TO_TCHAR(matchLengthName(Length))),
            Fixture.Scenery->Diagnostics().Rebuilds, BuiltAt);
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

#endif
