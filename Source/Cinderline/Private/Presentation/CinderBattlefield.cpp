#include "Presentation/CinderBattlefield.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/ConstructorHelpers.h"
#include <algorithm>

ACinderBattlefield::ACinderBattlefield()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("BattlefieldRoot"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeAsset(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Cube = CubeAsset.Object; Cylinder = CylinderAsset.Object; Cone = ConeAsset.Object; Sphere = SphereAsset.Object;
}

ACinderBattlefield::FBatch& ACinderBattlefield::AddBatch(UStaticMesh* Mesh, FLinearColor Color)
{
    auto* Component = NewObject<UInstancedStaticMeshComponent>(this);
    Component->SetupAttachment(RootComponent);
    Component->SetStaticMesh(Mesh);
    Component->SetMobility(EComponentMobility::Movable);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCastShadow(false);
    Component->RegisterComponent();
    if (BaseMaterial)
    {
        auto* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
        Material->SetVectorParameterValue(TEXT("Tint"), Color);
        Component->SetMaterial(0, Material);
    }
    MeshComponents.Add(Component);
    FBatch Batch; Batch.Mesh = Component;
    Batches.Add(MoveTemp(Batch));
    return Batches.Last();
}

void ACinderBattlefield::BeginPlay()
{
    Super::BeginPlay();
    BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Generated/M_CinderTint.M_CinderTint"));
    if (!BaseMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cinderline generated material missing. Run scripts/unreal.sh bootstrap before playing."));
        BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    }
    AddBatch(Cube, FLinearColor(0.065f, 0.12f, 0.13f));
    AddBatch(Cube, FLinearColor(0.14f, 0.21f, 0.22f));
    AddBatch(Cone, FLinearColor(1.0f, 0.52f, 0.12f));
    AddBatch(Cube, FLinearColor(0.012f, 0.022f, 0.035f));
    AddBatch(Cube, FLinearColor(0.034f, 0.067f, 0.080f));
    for (int Team = 0; Team < 2; ++Team)
    {
        const FLinearColor Color = Team == 0 ? FLinearColor(0.04f, 0.82f, 0.72f) : FLinearColor(0.96f, 0.24f, 0.17f);
        for (UStaticMesh* Shape : { Cube.Get(), Cylinder.Get(), Cone.Get(), Sphere.Get() }) AddBatch(Shape, Color);
    }
    for (int Team = 0; Team < 2; ++Team)
        for (UStaticMesh* Shape : { Cube.Get(), Cylinder.Get(), Cone.Get(), Sphere.Get() })
            AddBatch(Shape, Team == 0 ? FLinearColor(0.68f, 1.0f, 0.92f) : FLinearColor(1.0f, 0.68f, 0.28f));

    auto* Sun = GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-58, -32, 0));
    Sun->GetLightComponent()->SetIntensity(3.0f);
    Sun->GetLightComponent()->SetCastShadows(false);
    auto* Sky = GetWorld()->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetIntensity(0.8f);
    Simulation.reset();
    RenderState();
}

void ACinderBattlefield::StartMatch(int MapIndex)
{
    CurrentMap = FMath::Clamp(MapIndex, 0, 2);
    cinder::Config Config; Config.map = CurrentMap;
    Simulation.reset(Config);
    ResourceMemory.clear();
    bMenu = false; bPaused = false;
    RenderState();
}

void ACinderBattlefield::ReturnToMenu() { bMenu = true; bPaused = false; }

void ACinderBattlefield::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bMenu && !bPaused && Simulation.winner() < 0) Simulation.update(FMath::Min(DeltaSeconds, 0.2f));
    RenderTimer += DeltaSeconds;
    if (RenderTimer >= cinder::Simulation::Step) { RenderTimer = 0; RenderState(); }
}

void ACinderBattlefield::AddEntity(const cinder::Entity& Entity)
{
    using namespace cinder;
    const Definition& Def = definition(Entity.kind);
    if (Entity.kind == Kind::Resource)
    {
        if (!Simulation.explored(0, Entity.pos) || Entity.resource <= 0) return;
        for (int I = 0; I < 3; ++I)
        {
            const float A = I * 2.0944f;
            Batches[2].Transforms.Add(FTransform(FRotator(0, I * 120, I * 8), FVector(Entity.pos.x + FMath::Cos(A) * 15, Entity.pos.y + FMath::Sin(A) * 15, 34), FVector(0.25f, 0.3f, 0.65f + I * 0.1f)));
        }
        return;
    }
    if (Entity.team != 0 && !Simulation.visible(0, Entity.pos)) return;
    const float R = Def.radius / 50.0f;
    const float Elevation = Def.air ? 125.0f : 0.0f;
    const float BuildScale = Def.building ? FMath::Max(0.08f, Entity.progress) : 1;
    const FQuat Facing = FRotator(0, FMath::RadiansToDegrees(Entity.facing), 0).Quaternion();
    auto Part = [&](int Shape, FVector Offset, FVector Scale, bool Accent = false, FRotator Rotation = FRotator::ZeroRotator)
    {
        if (Def.building) { Offset.X *= 0.5; Offset.Y *= 0.5; Scale.X *= 0.5; Scale.Y *= 0.5; }
        Offset.Z *= BuildScale; Scale.Z *= BuildScale;
        const FVector Position(Entity.pos.x, Entity.pos.y, Elevation);
        Batches[5 + Entity.team * 4 + Shape + (Accent ? 8 : 0)].Transforms.Add(FTransform(Facing * Rotation.Quaternion(), Position + Facing.RotateVector(Offset), Scale));
    };
    switch (Entity.kind)
    {
    case Kind::Worker:
        Part(3, FVector(0, 0, 20), FVector(R, R, 0.35f));
        Part(0, FVector(12, 0, 34), FVector(0.2f, 0.28f, 0.15f), true);
        Part(1, FVector(-10, -15, 9), FVector(0.14f, 0.14f, 0.18f));
        Part(1, FVector(-10, 15, 9), FVector(0.14f, 0.14f, 0.18f)); break;
    case Kind::Striker:
        Part(1, FVector(0, 0, 24), FVector(R * 0.7f, R * 0.7f, 0.44f));
        Part(3, FVector(0, 0, 48), FVector(0.24f), true);
        Part(0, FVector(20, 0, 34), FVector(0.4f, 0.10f, 0.12f), true); break;
    case Kind::Lancer:
        Part(0, FVector(0, 0, 25), FVector(R, R * 0.85f, 0.48f));
        Part(2, FVector(0, 0, 59), FVector(0.34f, 0.34f, 0.38f), true);
        Part(0, FVector(30, 0, 39), FVector(0.62f, 0.12f, 0.13f), true); break;
    case Kind::Scout:
        Part(2, FVector(0, 0, 22), FVector(0.36f, 0.44f, 0.8f), false, FRotator(90, 0, 0));
        Part(3, FVector(-12, 0, 34), FVector(0.23f), true); break;
    case Kind::Bastion:
        Part(0, FVector(0, 0, 25), FVector(R * 1.6f, R, 0.45f));
        Part(1, FVector(0, 0, 55), FVector(R * 0.8f, R * 0.8f, 0.32f), true);
        Part(0, FVector(40, 0, 64), FVector(0.9f, 0.14f, 0.16f), true); break;
    case Kind::Mortar:
        Part(0, FVector(0, 0, 20), FVector(R * 1.4f, R * 1.25f, 0.36f));
        Part(1, FVector(7, 0, 53), FVector(0.24f, 0.24f, 0.9f), true, FRotator(35, 0, 0)); break;
    case Kind::Mender:
        Part(1, FVector(0, 0, 25), FVector(R, R, 0.28f));
        Part(3, FVector(0, 0, 54), FVector(0.3f), true);
        Part(0, FVector(0, 0, 54), FVector(0.65f, 0.12f, 0.1f), true);
        Part(0, FVector(0, 0, 54), FVector(0.12f, 0.65f, 0.1f), true); break;
    case Kind::Kite:
        Part(2, FVector::ZeroVector, FVector(R * 1.4f, R * 2.2f, 0.25f));
        Part(0, FVector(12, 0, 8), FVector(0.8f, 0.14f, 0.16f), true);
        Part(3, FVector(-18, -32, -6), FVector(0.22f), true);
        Part(3, FVector(-18, 32, -6), FVector(0.22f), true); break;
    case Kind::Headquarters:
        Part(1, FVector(0, 0, 22), FVector(R * 2, R * 2, 0.4f));
        Part(0, FVector(0, 0, 69), FVector(R * 1.25f, R * 1.25f, 0.9f));
        Part(2, FVector(0, 0, 146), FVector(R * 0.8f, R * 0.8f, 0.9f), true);
        Part(0, FVector(0, 0, 108), FVector(R * 1.65f, 0.2f, 0.2f), true); break;
    case Kind::Processor:
        Part(1, FVector(0, 0, 24), FVector(R * 1.8f, R * 1.8f, 0.45f));
        Part(2, FVector(0, 0, 87), FVector(R, R, 1.0f), true);
        Part(0, FVector(0, 0, 42), FVector(R * 2, 0.3f, 0.2f), true); break;
    case Kind::Foundry:
        Part(0, FVector(0, 0, 40), FVector(R * 1.7f, R * 1.6f, 0.8f));
        Part(0, FVector(0, -30, 92), FVector(R * 1.5f, 0.18f, 0.25f), true);
        Part(0, FVector(0, 30, 92), FVector(R * 1.5f, 0.18f, 0.25f), true);
        Part(1, FVector(-35, 0, 100), FVector(0.3f, 0.3f, 0.7f), true); break;
    case Kind::MotorPool:
        Part(0, FVector(0, 0, 32), FVector(R * 1.9f, R * 1.6f, 0.6f));
        Part(1, FVector(0, -38, 75), FVector(0.4f, 0.4f, 1.35f), true, FRotator(90, 0, 0));
        Part(1, FVector(0, 38, 75), FVector(0.4f, 0.4f, 1.35f), true, FRotator(90, 0, 0)); break;
    case Kind::Laboratory:
        Part(1, FVector(0, 0, 28), FVector(R * 1.8f, R * 1.8f, 0.5f));
        Part(0, FVector(0, 0, 95), FVector(0.28f, 0.28f, 1.4f));
        Part(3, FVector(0, 0, 165), FVector(0.85f), true);
        Part(0, FVector(0, 0, 125), FVector(R * 1.55f, 0.13f, 0.14f), true); break;
    case Kind::Turret:
        Part(1, FVector(0, 0, 23), FVector(R * 1.7f, R * 1.7f, 0.45f));
        Part(0, FVector(0, 0, 65), FVector(0.3f, 0.3f, 0.8f));
        Part(0, FVector(15, -13, 110), FVector(0.75f, 0.13f, 0.16f), true);
        Part(0, FVector(15, 13, 110), FVector(0.75f, 0.13f, 0.16f), true); break;
    default: break;
    }
}

void ACinderBattlefield::RenderState()
{
    for (FBatch& Batch : Batches) Batch.Transforms.Reset();
    if (Batches.IsEmpty()) return;
    Batches[0].Transforms.Add(FTransform(FQuat::Identity, FVector(2400, 2400, -16), FVector(49, 49, 0.3f)));
    for (const auto& Obstacle : Simulation.obstacles())
        if (Simulation.explored(0, Obstacle.center))
            Batches[1].Transforms.Add(FTransform(FQuat::Identity, FVector(Obstacle.center.x, Obstacle.center.y, 38), FVector(Obstacle.half.x / 50, Obstacle.half.y / 50, 0.8f)));
    constexpr float Cell = cinder::Simulation::WorldSize / cinder::Simulation::FogSize;
    for (int Y = 0; Y < cinder::Simulation::FogSize; ++Y)
        for (int X = 0; X < cinder::Simulation::FogSize; ++X)
        {
            cinder::Vec2 P{(X + 0.5f) * Cell, (Y + 0.5f) * Cell};
            if (!Simulation.visible(0, P))
                Batches[Simulation.explored(0, P) ? 4 : 3].Transforms.Add(FTransform(FQuat::Identity, FVector(P.x, P.y, 1), FVector(Cell / 100 + 0.001f, Cell / 100 + 0.001f, 0.02f)));
        }
    for (const auto& Entity : Simulation.entities())
    {
        if (Entity.kind == cinder::Kind::Resource)
        {
            // Cache only observed resource state: unseen harvesting must not leak through visuals.
            if (Simulation.visible(0, Entity.pos))
            {
                const auto It = std::find_if(ResourceMemory.begin(), ResourceMemory.end(), [&](const cinder::Entity& E) { return E.id == Entity.id; });
                if (It == ResourceMemory.end()) ResourceMemory.push_back(Entity); else *It = Entity;
            }
        }
        else if (Entity.alive()) AddEntity(Entity);
    }
    for (const auto& Resource : ResourceMemory) AddEntity(Resource);
    FlushBatches();
}

void ACinderBattlefield::FlushBatches()
{
    for (FBatch& Batch : Batches)
    {
        if (Batch.Mesh->GetInstanceCount() == Batch.Transforms.Num())
        {
            if (!Batch.Transforms.IsEmpty()) Batch.Mesh->BatchUpdateInstancesTransforms(0, Batch.Transforms, false, true, true);
        }
        else
        {
            Batch.Mesh->ClearInstances();
            Batch.Mesh->AddInstances(Batch.Transforms, false, false);
        }
    }
}

bool ACinderBattlefield::SaveMatch() const
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Matches");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*(Directory / TEXT("skirmish.cinder")));
    return Simulation.save(TCHAR_TO_UTF8(*Filename));
}

bool ACinderBattlefield::LoadMatch()
{
    const FString Filename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*(FPaths::ProjectSavedDir() / TEXT("Matches/skirmish.cinder")));
    if (!Simulation.load(TCHAR_TO_UTF8(*Filename))) return false;
    CurrentMap = Simulation.config().map;
    ResourceMemory.clear();
    bMenu = false; bPaused = false; RenderState(); return true;
}
