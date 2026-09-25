#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"
#include "Presentation/CinderLandscapeTerrain.h"
#include "StaticMeshAttributes.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderAuthoredTerrainFallbackGeometryTest,
    "Cinderline.Presentation.AuthoredTerrainFallback.Geometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderAuthoredTerrainFallbackGeometryTest::RunTest(const FString& Parameters)
{
    using namespace CinderLandscapeTerrain;
    cinder::Simulation Simulation;
    TestTrue(TEXT("Default duel exercises authored terrain"), Simulation.usesAuthoredTerrain());
    FMeshDescription Description;
    UCinderLandscapeTerrain::BuildFallbackMeshDescription(Simulation, Description);
    TestEqual(TEXT("Fallback samples every canonical heightfield vertex"),
        Description.Vertices().Num(), SamplesPerAxis * SamplesPerAxis);
    TestEqual(TEXT("Fallback shares vertices instead of duplicating every triangle"),
        Description.VertexInstances().Num(), SamplesPerAxis * SamplesPerAxis);
    TestEqual(TEXT("Fallback preserves the Landscape's fixed triangle budget"),
        Description.Triangles().Num(), QuadsPerAxis * QuadsPerAxis * 2);
    TestEqual(TEXT("A single ground material owns the entire fallback"), Description.PolygonGroups().Num(), 1);
    const FStaticMeshConstAttributes Attributes(Description);
    const auto Positions = Attributes.GetVertexPositions();
    const auto Normals = Attributes.GetVertexInstanceNormals();
    const auto Tangents = Attributes.GetVertexInstanceTangents();
    const auto UVs = Attributes.GetVertexInstanceUVs();
    const float Spacing = VertexSpacing(Simulation.worldSize());
    bool bHeightsMatch = true, bBasisValid = true, bUVsMatch = true, bWindingMatches = true;
    float Highest = BaselineZ;
    for (const FVertexInstanceID Instance : Description.VertexInstances().GetElementIDs())
    {
        const int32 Index = Instance.GetValue();
        const int32 X = Index % SamplesPerAxis, Y = Index / SamplesPerAxis;
        const FVector3f Position = Positions[Description.GetVertexInstanceVertex(Instance)];
        const float Expected = BaselineZ + HeightAt(Simulation, X * Spacing, Y * Spacing);
        bHeightsMatch &= FMath::IsNearlyEqual(Position.X, X * Spacing)
            && FMath::IsNearlyEqual(Position.Y, Y * Spacing)
            && FMath::IsNearlyEqual(Position.Z, Expected);
        Highest = FMath::Max(Highest, Position.Z);
        bBasisValid &= !Normals[Instance].ContainsNaN() && !Tangents[Instance].ContainsNaN()
            && FMath::IsNearlyEqual(Normals[Instance].SizeSquared(), 1.0f, 0.001f)
            && FMath::IsNearlyEqual(Tangents[Instance].SizeSquared(), 1.0f, 0.001f)
            && FMath::Abs(FVector3f::DotProduct(Normals[Instance], Tangents[Instance])) < 0.001f
            && Normals[Instance].Z > 0.0f;
        const FVector2f UV = UVs.Get(Instance, 0);
        bUVsMatch &= FMath::IsNearlyEqual(UV.X, static_cast<float>(X) / QuadsPerAxis)
            && FMath::IsNearlyEqual(UV.Y, static_cast<float>(Y) / QuadsPerAxis);
    }
    for (const FTriangleID Triangle : Description.Triangles().GetElementIDs())
    {
        const auto Instances = Description.GetTriangleVertexInstances(Triangle);
        const FVector3f A = Positions[Description.GetVertexInstanceVertex(Instances[0])];
        const FVector3f B = Positions[Description.GetVertexInstanceVertex(Instances[1])];
        const FVector3f C = Positions[Description.GetVertexInstanceVertex(Instances[2])];
        // Unreal Landscape uses clockwise winding in XY with outward +Z normals.
        bWindingMatches &= FVector3f::CrossProduct(B - A, C - A).Z < 0.0f;
    }
    TestTrue(TEXT("Fallback vertices render exactly the relief used to seat units and buildings"), bHeightsMatch);
    TestTrue(TEXT("Fallback contains the elevated authored plateau, not a flat ground plane"), Highest > 150.0f);
    TestTrue(TEXT("All fallback vertices have a finite, orthonormal upward shading basis"), bBasisValid);
    TestTrue(TEXT("Fallback UVs span the complete map without seams"), bUVsMatch);
    TestTrue(TEXT("Every fallback triangle faces the same way as Landscape"), bWindingMatches);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderAuthoredTerrainFallbackLifecycleTest,
    "Cinderline.Presentation.AuthoredTerrainFallback.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderAuthoredTerrainFallbackLifecycleTest::RunTest(const FString& Parameters)
{
    FTestWorldWrapper WorldOwner;
    if (!WorldOwner.CreateTestWorld(EWorldType::Game))
    {
        WorldOwner.ForwardErrorMessages(this);
        return false;
    }
    UWorld* World = WorldOwner.GetTestWorld();
    AActor* Owner = World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("Fallback test owns an actor"), Owner)) return false;
    auto* Root = NewObject<USceneComponent>(Owner);
    Owner->AddInstanceComponent(Root);
    Owner->SetRootComponent(Root);
    Root->RegisterComponent();
    auto* Terrain = NewObject<UCinderLandscapeTerrain>(Owner);
    Owner->AddInstanceComponent(Terrain);
    Terrain->RegisterComponent();
    UTexture2D* Fog = NewObject<UTexture2D>(Owner);
    UTexture2D* Layers = NewObject<UTexture2D>(Owner);
    cinder::Simulation Simulation;

    if (!FApp::CanEverRender())
    {
        TestFalse(TEXT("Headless execution never reports rendered terrain"), Terrain->Update(Simulation, Fog, Layers));
        TestNull(TEXT("Headless execution never creates a fallback render component"), Terrain->FallbackComponent.Get());
        TestTrue(TEXT("Headless execution never builds static-mesh RHI resources"), Terrain->FallbackMeshes.IsEmpty());
        AddInfo(TEXT("NullRHI: verified the no-render guard; rendered fallback lifecycle requires a rendering-enabled run."));
        return true;
    }

    if (!TestTrue(TEXT("Missing authored Landscape gets matching runtime terrain"), Terrain->Update(Simulation, Fog, Layers)))
        return false;
    UStaticMeshComponent* Component = Terrain->FallbackComponent;
    if (!TestNotNull(TEXT("Fallback render component exists"), Component)) return false;
    UStaticMesh* Mesh = Component->GetStaticMesh();
    TestNotNull(TEXT("Fallback component owns built terrain geometry"), Mesh);
    TestTrue(TEXT("Fallback geometry is visible"), Component->IsVisible() && !Component->bHiddenInGame);
    TestTrue(TEXT("Fallback geometry does not change collision or navigation"),
        Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Component->CanEverAffectNavigation());
    UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;
    if (!TestNotNull(TEXT("Runtime mesh has an explicitly non-cooking body setup"), BodySetup)) return false;
    TestFalse(TEXT("Visual terrain does not retain a CPU collision copy of render geometry"), Mesh->bAllowCPUAccess);
    // Exercise the engine entry point which SetStaticMesh may visit even for a
    // NoCollision component. It must not attempt to cook GPU-only triangle data.
    BodySetup->CreatePhysicsMeshes();
    TestTrue(TEXT("Physics creation produces no collision shapes for rendered terrain"),
        BodySetup->AggGeom.GetElementCount() == 0 && BodySetup->TriMeshGeometries.IsEmpty());
    TestFalse(TEXT("Visual terrain has no cooked collision payload"), BodySetup->bHasCookedCollisionData);
    TestFalse(TEXT("Visual terrain has no navigation geometry"), Mesh->bHasNavigationData);
    UTexture* BoundFog = nullptr;
    UTexture* BoundLayers = nullptr;
    TestTrue(TEXT("Fallback material receives the current fog texture"),
        Terrain->FallbackMaterial->GetTextureParameterValue(TEXT("FogMask"), BoundFog) && BoundFog == Fog);
    TestTrue(TEXT("Fallback material receives the current terrain paint texture"),
        Terrain->FallbackMaterial->GetTextureParameterValue(TEXT("TerrainLayers"), BoundLayers) && BoundLayers == Layers);
    TestTrue(TEXT("Repeated updates keep matching relief active"), Terrain->Update(Simulation, Fog, Layers));
    TestTrue(TEXT("Repeated updates reuse the built mesh"), Component->GetStaticMesh() == Mesh);
    TestEqual(TEXT("Repeated updates cache only one mesh for the geometry signature"), Terrain->FallbackMeshes.Num(), 1);
    TestTrue(TEXT("Cached terrain retains the same non-cooking body setup"), Mesh->GetBodySetup() == BodySetup
        && BodySetup->bNeverNeedsCookedCollisionData);
    UTexture2D* ReplacementFog = NewObject<UTexture2D>(Owner);
    TestTrue(TEXT("Fog replacement preserves the runtime heightfield"), Terrain->Update(Simulation, ReplacementFog, Layers));
    TestTrue(TEXT("Fog replacement binds the new texture without rebuilding geometry"),
        Terrain->FallbackMaterial->GetTextureParameterValue(TEXT("FogMask"), BoundFog)
        && BoundFog == ReplacementFog && Component->GetStaticMesh() == Mesh);

    TestFalse(TEXT("Missing terrain paint cannot report safely rendered relief"), Terrain->Update(Simulation, Fog, nullptr));
    TestFalse(TEXT("Missing bindings hide the old terrain"), Component->IsVisible());
    TestTrue(TEXT("Restoring bindings reuses the previous mesh"), Terrain->Update(Simulation, Fog, Layers)
        && Component->GetStaticMesh() == Mesh);
    cinder::Config Legacy;
    Legacy.mapRevision = 0;
    Simulation.reset(Legacy);
    TestFalse(TEXT("Legacy maps retain the existing flat fallback policy"), Terrain->Update(Simulation, Fog, Layers));
    TestFalse(TEXT("Leaving authored terrain hides the runtime mesh"), Component->IsVisible());
    WorldOwner.ForwardErrorMessages(this);
    return true;
}

#endif
