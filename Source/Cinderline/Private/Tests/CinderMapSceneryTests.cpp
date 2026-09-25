#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Presentation/CinderScenery.h"
#include "Sim/MapDefinition.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderAuthoredMapSceneryTest,
    "Cinderline.Presentation.AuthoredMapScenery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderAuthoredMapSceneryTest::RunTest(const FString& Parameters)
{
    FTestWorldWrapper WorldOwner;
    if (!WorldOwner.CreateTestWorld(EWorldType::Game))
    {
        WorldOwner.ForwardErrorMessages(this);
        return false;
    }
    AActor* Owner = WorldOwner.GetTestWorld()->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("Authored scenery owner exists"), Owner)) return false;
    auto* Root = NewObject<USceneComponent>(Owner);
    Owner->SetRootComponent(Root);
    Root->RegisterComponent();
    auto* Scenery = NewObject<UCinderScenery>(Owner);
    Scenery->RegisterComponent();
    UTexture2D* Fog = LoadObject<UTexture2D>(nullptr,
        TEXT("/Game/Art/Textures/VisualUpgrade/T_CinderFogDefaultV2.T_CinderFogDefaultV2"), nullptr, LOAD_NoWarn);
    Scenery->Initialize(Root, nullptr, Fog);

    cinder::Config Config;
    Config.map = 0;
    Config.mapRevision = cinder::CurrentMapRevision;
    Config.ai = false;
    cinder::Simulation Simulation;
    Simulation.reset(Config);
    if (!TestTrue(TEXT("Shattered Rift uses authoritative plateau geometry"), Simulation.usesAuthoredTerrain())) return false;
    Scenery->Update(Simulation, {});
    const auto Partial = Scenery->Diagnostics();
    TestTrue(TEXT("Starting exploration reveals the actual home foundation"), Partial.AuthoredPavingInstances > 0);
    TestTrue(TEXT("Unknown map architecture is rejected by full-footprint fog checks"), Partial.FogRejectedArchitectureInstances > 0);
    Scenery->Update(Simulation, {});
    TestEqual(TEXT("Unchanged architecture does not rebuild"), Scenery->Diagnostics().Rebuilds, Partial.Rebuilds);

    for (int32 Y = 0; Y < 8; ++Y)
    for (int32 X = 0; X < 8; ++X)
        Simulation.debugSpawn(cinder::Kind::Scout, 0, {300.0f + X * 600.0f, 300.0f + Y * 600.0f});
    Scenery->Update(Simulation, {});
    const auto Full = Scenery->Diagnostics();
    TestTrue(TEXT("Exploration reveals additional foundations and ramp thresholds"), Full.AuthoredPavingInstances > Partial.AuthoredPavingInstances);
    TestTrue(TEXT("Terraces have retaining facades"), Full.RetainingWallInstances > 0);
    TestTrue(TEXT("Both entrances receive pitched ramp panels"), Full.RampInstances >= 6);
    TestEqual(TEXT("Ground roads are painted once without overlapping road boxes"), Full.RoadInstances, Full.RampInstances);
    TestTrue(TEXT("The blocked perimeter carries a continuous fractured embankment"), Full.BoundarySceneryInstances > 0);
    TestEqual(TEXT("Map architecture reuses existing components"), Full.BatchCount, Full.bFoliageAvailable ? 13 : 12);
    TestEqual(TEXT("Architecture remains presentation-only"), Full.CollisionEnabledBatches, 0);

    TArray<UInstancedStaticMeshComponent*> Components;
    Owner->GetComponents(Components);
    UInstancedStaticMeshComponent* Pad = nullptr;
    UInstancedStaticMeshComponent* Road = nullptr;
    for (auto* Component : Components)
    {
        if (Component->GetName().Contains(TEXT("CinderScenery_Pad"))) Pad = Component;
        if (Component->GetName().Contains(TEXT("CinderScenery_Road"))) Road = Component;
    }
    if (!TestNotNull(TEXT("Architecture owns a paving batch"), Pad)
        || !TestNotNull(TEXT("Architecture owns a road batch"), Road)) return false;
    TestEqual(TEXT("Authored facades use a contained rectangular module"), Pad->GetStaticMesh()->GetName(), FString(TEXT("Cube")));
    const auto& Definition = cinder::mapDefinition(0, 2, cinder::MatchLength::Standard, Config.mapRevision);
    for (int32 Index = 0; Index < Pad->GetInstanceCount(); ++Index)
    {
        FTransform Transform;
        Pad->GetInstanceTransform(Index, Transform);
        const FBox Bounds = Pad->GetStaticMesh()->GetBoundingBox().TransformBy(Transform);
        if (Bounds.GetSize().Z < 35.0f) continue;
        bool bContained = false;
        for (size_t ObstacleIndex = 0; ObstacleIndex < Definition.obstacles.size(); ++ObstacleIndex)
        {
            if (Definition.obstacleKinds[ObstacleIndex] != cinder::MapObstacleKind::RetainingWall) continue;
            const auto& Wall = Definition.obstacles[ObstacleIndex];
            if (Bounds.Min.X >= Wall.center.x - Wall.half.x - 0.05f
                && Bounds.Max.X <= Wall.center.x + Wall.half.x + 0.05f
                && Bounds.Min.Y >= Wall.center.y - Wall.half.y - 0.05f
                && Bounds.Max.Y <= Wall.center.y + Wall.half.y + 0.05f) bContained = true;
        }
        TestTrue(TEXT("Every retaining facade stays within its authoritative obstacle"), bContained);
        TestTrue(TEXT("Retaining facades remain below 210 cm instead of becoming tall rock masses"), Bounds.Max.Z <= 210.0f);
    }
    int32 PitchedPanels = 0;
    for (int32 Index = 0; Index < Road->GetInstanceCount(); ++Index)
    {
        FTransform Transform;
        Road->GetInstanceTransform(Index, Transform);
        if (FMath::Abs(Transform.Rotator().Pitch) > 5.0f) ++PitchedPanels;
    }
    TestTrue(TEXT("Ramp road panels follow the incline instead of floating horizontally"), PitchedPanels >= 6);

    Config.mapRevision = 0;
    Simulation.reset(Config);
    Scenery->Update(Simulation, {});
    const auto Legacy = Scenery->Diagnostics();
    TestEqual(TEXT("Legacy layouts receive no authored map walls"), Legacy.RetainingWallInstances, 0);
    TestEqual(TEXT("Legacy layouts receive no new fixed paving"), Legacy.AuthoredPavingInstances, 0);
    TestEqual(TEXT("Legacy layouts receive no new ramps"), Legacy.RampInstances, 0);
    TestEqual(TEXT("Returning to legacy restores the original pad mesh"), Pad->GetStaticMesh()->GetName(), FString(TEXT("SM_CinderScenery_Pad")));
    WorldOwner.ForwardErrorMessages(this);
    return !HasAnyErrors();
}

#endif
