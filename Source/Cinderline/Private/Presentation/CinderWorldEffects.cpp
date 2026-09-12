#include "Presentation/CinderWorldEffects.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
constexpr float BasicSphereRadius = 50.0f;
constexpr float BasicCylinderHeight = 100.0f;

FVector EffectPoint(cinder::Vec2 Point, cinder::Kind Kind)
{
    const cinder::Definition& Definition = cinder::definition(Kind);
    const float Height = Definition.air ? 125.0f + Definition.radius
        : Definition.building ? 35.0f + Definition.radius * 0.45f
        : 20.0f + Definition.radius * 0.8f;
    return FVector(Point.x, Point.y, Height);
}

FTransform SphereTransform(FVector Position, float Radius)
{
    return FTransform(FQuat::Identity, Position, FVector(Radius / BasicSphereRadius));
}

FTransform BeamTransform(FVector Start, FVector End, float Width)
{
    const FVector Delta = End - Start;
    const float Length = Delta.Size();
    if (Length <= UE_SMALL_NUMBER) return FTransform(FQuat::Identity, Start, FVector::ZeroVector);
    const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Delta / Length);
    return FTransform(Rotation, (Start + End) * 0.5f,
        FVector(Width / BasicSphereRadius, Width / BasicSphereRadius, Length / BasicCylinderHeight));
}

bool AreaVisible(const cinder::Simulation& Simulation, int32 Team, cinder::Vec2 Point, float Radius)
{
    constexpr float Cell = cinder::Simulation::WorldSize / cinder::Simulation::FogSize;
    if (Point.x - Radius < 0 || Point.y - Radius < 0
        || Point.x + Radius >= cinder::Simulation::WorldSize
        || Point.y + Radius >= cinder::Simulation::WorldSize) return false;
    const int32 MinX = FMath::FloorToInt((Point.x - Radius) / Cell);
    const int32 MaxX = FMath::FloorToInt((Point.x + Radius) / Cell);
    const int32 MinY = FMath::FloorToInt((Point.y - Radius) / Cell);
    const int32 MaxY = FMath::FloorToInt((Point.y + Radius) / Cell);
    for (int32 Y = MinY; Y <= MaxY; ++Y)
        for (int32 X = MinX; X <= MaxX; ++X)
            if (!Simulation.visible(Team, {(X + 0.5f) * Cell, (Y + 0.5f) * Cell})) return false;
    return true;
}

float HashAngle(uint64 Id, int32 Index)
{
    const uint32 Mixed = static_cast<uint32>(Id) * 747796405u + static_cast<uint32>(Index) * 2891336453u;
    return static_cast<float>(Mixed & 1023u) * (UE_TWO_PI / 1024.0f);
}
}

UCinderWorldEffects::UCinderWorldEffects()
{
    PrimaryComponentTick.bCanEverTick = false;
}

UInstancedStaticMeshComponent* UCinderWorldEffects::CreateBatch(USceneComponent* AttachRoot,
    UStaticMesh* Mesh, UMaterialInterface* Material, FLinearColor Tint, float GlowIntensity, float Opacity)
{
    AActor* Owner = GetOwner();
    if (!Owner || !AttachRoot || !Mesh) return nullptr;
    UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Owner);
    Component->SetupAttachment(AttachRoot);
    Component->SetStaticMesh(Mesh);
    Component->SetMobility(EComponentMobility::Movable);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCanEverAffectNavigation(false);
    Component->SetCastShadow(false);
    Component->SetCastContactShadow(false);
    Component->SetReceivesDecals(false);
    Component->SetTranslucentSortPriority(4);
    Component->RegisterComponent();
    if (Material)
    {
        UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Material, this);
        Dynamic->SetVectorParameterValue(TEXT("Tint"), Tint);
        Dynamic->SetScalarParameterValue(TEXT("GlowIntensity"), GlowIntensity);
        Dynamic->SetScalarParameterValue(TEXT("Opacity"), Opacity);
        Dynamic->SetScalarParameterValue(TEXT("FadeDistance"), 42.0f);
        Component->SetMaterial(0, Dynamic);
        Materials.Add(Dynamic);
    }
    Components.Add(Component);
    return Component;
}

void UCinderWorldEffects::Initialize(USceneComponent* AttachRoot, UStaticMesh* Sphere, UStaticMesh* Cylinder,
    UStaticMesh* Cone, UStaticMesh* Plane, UMaterialInterface* FallbackMaterial)
{
    if (bInitialized) return;
    UMaterialInterface* Emissive = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Art/VisualTarget/Materials/M_VT_Emissive.M_VT_Emissive"), nullptr, LOAD_NoWarn);
    UMaterialInterface* Dust = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Art/VisualTarget/Materials/M_VT_Dust.M_VT_Dust"), nullptr, LOAD_NoWarn);
    bSoftDust = Dust != nullptr;
    if (!Emissive) Emissive = FallbackMaterial;
    if (!Dust) Dust = FallbackMaterial;
    if (!AttachRoot || !Sphere || !Cylinder || !Cone || !Plane || !Emissive || !Dust) return;

    Components.Reserve(static_cast<int32>(EBatch::Count));
    Materials.Reserve(static_cast<int32>(EBatch::Count));
    Pending.SetNum(static_cast<int32>(EBatch::Count));
    Submitted.SetNum(static_cast<int32>(EBatch::Count));
    CreateBatch(AttachRoot, Sphere, Emissive, FLinearColor(0.18f, 1.0f, 0.76f), 5.0f, 1.0f);
    CreateBatch(AttachRoot, Sphere, Emissive, FLinearColor(1.0f, 0.16f, 0.055f), 5.0f, 1.0f);
    CreateBatch(AttachRoot, Sphere, Emissive, FLinearColor(1.0f, 0.48f, 0.08f), 7.0f, 1.0f);
    CreateBatch(AttachRoot, Cone, Emissive, FLinearColor(1.0f, 0.55f, 0.10f), 6.0f, 1.0f);
    CreateBatch(AttachRoot, Sphere, Emissive, FLinearColor(0.25f, 1.0f, 0.50f), 4.5f, 1.0f);
    CreateBatch(AttachRoot, Cylinder, Emissive, FLinearColor(0.10f, 1.0f, 0.76f), 5.5f, 1.0f);
    CreateBatch(AttachRoot, Cylinder, Emissive, FLinearColor(1.0f, 0.12f, 0.035f), 5.5f, 1.0f);
    CreateBatch(AttachRoot, Cylinder, Emissive, FLinearColor(0.20f, 1.0f, 0.45f), 4.0f, 1.0f);
    CreateBatch(AttachRoot, Plane, Dust, FLinearColor(0.38f, 0.25f, 0.16f), 0.0f, 0.42f);
    bInitialized = Components.Num() == static_cast<int32>(EBatch::Count);
    if (!bInitialized)
    {
        Reset();
        for (UInstancedStaticMeshComponent* Component : Components)
            if (Component) Component->DestroyComponent();
        Components.Reset(); Materials.Reset(); Pending.Reset(); Submitted.Reset();
    }
}

bool UCinderWorldEffects::Add(EBatch Batch, const FTransform& Transform)
{
    const int32 Index = static_cast<int32>(Batch);
    if (!Pending.IsValidIndex(Index) || Pending[Index].Num() >= MaxBatchInstances
        || PendingInstanceCount >= MaxInstances)
    {
        ++Diagnostics.DroppedForBudget;
        return false;
    }
    Pending[Index].Add(Transform);
    ++PendingInstanceCount;
    return true;
}

void UCinderWorldEffects::Update(const cinder::Simulation& Simulation, int32 ViewerTeam)
{
    if (!bInitialized || ViewerTeam < 0 || ViewerTeam > 1) return;
    const uint64 LatestEffectId = Simulation.lastEffectId();
    if (LatestEffectId < LastSimulationEffectId)
    {
        // A direct simulation reset starts a new ID sequence. Explicit load paths
        // should call Reset(lastEffectId) before their first visual submission.
        SuppressedThroughId = 0;
    }
    LastSimulationEffectId = LatestEffectId;
    PendingViewerTeam = ViewerTeam;
    for (TArray<FTransform>& Batch : Pending) Batch.Reset();
    PendingInstanceCount = 0;
    ++Diagnostics.Updates;

    APlayerCameraManager* Camera = nullptr;
    if (UWorld* World = GetWorld())
        if (APlayerController* Controller = World->GetFirstPlayerController()) Camera = Controller->PlayerCameraManager;
    auto DustTransform = [&](FVector Position, float Radius)
    {
        FQuat Rotation = FQuat::Identity;
        if (Camera)
        {
            const FVector TowardCamera = (Camera->GetCameraLocation() - Position).GetSafeNormal();
            if (!TowardCamera.IsNearlyZero()) Rotation = FQuat::FindBetweenNormals(FVector::UpVector, TowardCamera);
        }
        return FTransform(Rotation, Position, FVector(Radius / BasicSphereRadius));
    };

    // New hits win the fixed budget during mass combat. Older retained effects
    // still render when room remains and expire on their simulation lifetime.
    for (auto It = Simulation.effects().rbegin(); It != Simulation.effects().rend(); ++It)
    {
        const cinder::Effect& Effect = *It;
        if (Effect.id <= SuppressedThroughId || Effect.life <= 0 || Effect.duration <= 0) continue;
        const bool SourceVisible = Simulation.effectVisible(Effect, ViewerTeam, true);
        const bool TargetVisible = Simulation.effectVisible(Effect, ViewerTeam, false);
        const bool LinkVisible = Simulation.effectLinkVisible(Effect, ViewerTeam);
        if (!SourceVisible && !TargetVisible)
        {
            ++Diagnostics.HiddenEvents;
            continue;
        }
        const float Age = FMath::Clamp(1.0f - Effect.life / Effect.duration, 0.0f, 1.0f);
        const float Fade = FMath::Square(1.0f - Age);
        const FVector From = EffectPoint(Effect.from, Effect.sourceKind);
        const FVector To = EffectPoint(Effect.to, Effect.targetKind);
        const EBatch TeamGlow = Effect.team == 0 ? EBatch::FriendlyGlow : EBatch::EnemyGlow;
        const EBatch TeamBeam = Effect.team == 0 ? EBatch::FriendlyBeam : EBatch::EnemyBeam;

        switch (Effect.type)
        {
        case cinder::EffectType::Weapon:
        {
            const bool Heavy = Effect.sourceKind == cinder::Kind::Bastion
                || Effect.sourceKind == cinder::Kind::Mortar || Effect.sourceKind == cinder::Kind::Turret;
            if (SourceVisible && Age < 0.42f && AreaVisible(Simulation, ViewerTeam, Effect.from, Heavy ? 24.0f : 16.0f))
            {
                const float Burst = FMath::Max(0.12f, 1.0f - Age / 0.42f);
                Add(EBatch::WarmGlow, SphereTransform(From, (Heavy ? 17.0f : 11.0f) * Burst));
                Add(TeamGlow, SphereTransform(From, (Heavy ? 10.0f : 7.0f) * Burst));
                // The round muzzle blooms reveal only the source position. The
                // shard also encodes target bearing, so require the complete
                // segment to remain visible before deriving its direction.
                if (LinkVisible)
                {
                    const FVector Direction = (To - From).GetSafeNormal();
                    if (!Direction.IsNearlyZero()) Add(EBatch::WarmShard,
                        BeamTransform(From, From + Direction * (Heavy ? 38.0f : 24.0f) * Burst, Heavy ? 7.0f : 4.0f));
                }
            }
            if (!LinkVisible) break;
            if (Effect.sourceKind == cinder::Kind::Lancer)
            {
                Add(TeamBeam, BeamTransform(From, To, 2.0f + Fade * 1.5f));
                const float Head = FMath::Clamp(Age / 0.82f, 0.0f, 1.0f);
                Add(EBatch::WarmGlow, SphereTransform(FMath::Lerp(From, To, Head), 5.0f + Fade * 3.0f));
                break;
            }
            const float Head = FMath::Clamp(Age / 0.78f, 0.0f, 1.0f);
            const float Tail = FMath::Max(0.0f, Head - (Heavy ? 0.16f : 0.10f));
            auto ArcPoint = [&](float T)
            {
                FVector Point = FMath::Lerp(From, To, T);
                if (Effect.sourceKind == cinder::Kind::Mortar) Point.Z += 190.0f * 4.0f * T * (1.0f - T);
                return Point;
            };
            const FVector HeadPoint = ArcPoint(Head);
            Add(TeamBeam, BeamTransform(ArcPoint(Tail), HeadPoint, Heavy ? 4.0f : 2.7f));
            Add(EBatch::WarmGlow, SphereTransform(HeadPoint, Heavy ? 8.0f : 5.0f));
            break;
        }
        case cinder::EffectType::Impact:
            if (TargetVisible && AreaVisible(Simulation, ViewerTeam, Effect.to, 38.0f))
            {
                const float Radius = 7.0f + Age * 19.0f;
                Add(EBatch::WarmGlow, SphereTransform(To, Radius * FMath::Max(0.18f, Fade)));
                for (int32 Index = 0; Index < 4; ++Index)
                {
                    const float Angle = HashAngle(Effect.id, Index);
                    const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.45f);
                    Add(EBatch::WarmShard, BeamTransform(To + Direction * Radius * 0.35f,
                        To + Direction * Radius * 1.25f, 1.2f));
                }
                if (bSoftDust) Add(EBatch::Dust, DustTransform(FVector(To.X, To.Y, 7.0f), 20.0f + Age * 16.0f));
            }
            break;
        case cinder::EffectType::Heal:
            if (LinkVisible)
            {
                Add(EBatch::HealBeam, BeamTransform(From, To, 1.8f + Fade));
                const float Head = FMath::Clamp(Age / 0.86f, 0.0f, 1.0f);
                Add(EBatch::HealGlow, SphereTransform(FMath::Lerp(From, To, Head), 6.0f));
            }
            if (TargetVisible && AreaVisible(Simulation, ViewerTeam, Effect.to, 22.0f))
                Add(EBatch::HealGlow, SphereTransform(To + FVector(0, 0, 8), 7.0f + Fade * 7.0f));
            break;
        case cinder::EffectType::Death:
            if (TargetVisible)
            {
                const cinder::Definition& Victim = cinder::definition(Effect.targetKind);
                const float Footprint = FMath::Clamp(Victim.radius * (Victim.building ? 1.05f : 1.28f), 28.0f, 125.0f);
                if (!AreaVisible(Simulation, ViewerTeam, Effect.to, Footprint * 1.25f)) break;
                const FVector Center(Effect.to.x, Effect.to.y, Victim.air ? To.Z : 12.0f);
                Add(EBatch::WarmGlow, SphereTransform(Center, Footprint * (0.18f + Age * 0.45f) * FMath::Max(0.20f, Fade)));
                const int32 FragmentCount = Victim.building ? 6 : 4;
                for (int32 Index = 0; Index < FragmentCount; ++Index)
                {
                    const float Angle = HashAngle(Effect.id, Index);
                    const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0);
                    const FVector Fragment = Center + Direction * Footprint * Age
                        + FVector(0, 0, FMath::Sin(Age * PI) * (Victim.building ? 75.0f : 42.0f));
                    Add(TeamGlow, SphereTransform(Fragment, Victim.building ? 7.0f : 4.5f));
                    const FVector Flight(Direction.X, Direction.Y, 0.35f);
                    Add(EBatch::WarmShard, BeamTransform(Fragment - Flight * 5.0f,
                        Fragment + Flight * (Victim.building ? 12.0f : 8.0f), Victim.building ? 3.0f : 2.0f));
                }
                if (bSoftDust) Add(EBatch::Dust, DustTransform(FVector(Center.X, Center.Y, 9.0f), Footprint * (0.55f + Age * 0.65f)));
            }
            break;
        }
    }

    // Low-cost continuous cues come only from visible actors performing the
    // corresponding real order. They do not synthesize combat or economy events.
    const float Time = Simulation.time();
    for (const cinder::Entity& Entity : Simulation.entities())
    {
        if (!Entity.alive() || Entity.kind == cinder::Kind::Resource) continue;
        if (Entity.team != ViewerTeam && !Simulation.visible(ViewerTeam, Entity.pos)) continue;
        const cinder::Definition& Definition = cinder::definition(Entity.kind);
        const EBatch TeamGlow = Entity.team == 0 ? EBatch::FriendlyGlow : EBatch::EnemyGlow;
        if (Definition.air && AreaVisible(Simulation, ViewerTeam, Entity.pos, 18.0f))
        {
            const FVector Forward(FMath::Cos(Entity.facing), FMath::Sin(Entity.facing), 0);
            const float Pulse = 0.82f + FMath::Sin(Time * 15.0f + Entity.id) * 0.18f;
            const FVector Exhaust(Entity.pos.x, Entity.pos.y, 120.0f + Definition.radius);
            Add(TeamGlow, SphereTransform(Exhaust - Forward * (Definition.radius + 8.0f), 6.0f * Pulse));
        }
        if (Entity.kind != cinder::Kind::Worker) continue;
        const cinder::Entity* Target = Simulation.find(Entity.target ? Entity.target : Entity.resourceTarget);
        if (!Target || !Simulation.visible(ViewerTeam, Target->pos)) continue;
        const float DX = Target->pos.x - Entity.pos.x, DY = Target->pos.y - Entity.pos.y;
        const float Reach = cinder::definition(Target->kind).radius + Definition.radius + 28.0f;
        const bool AtTarget = DX * DX + DY * DY <= Reach * Reach;
        const bool Mining = Entity.order == cinder::Order::Gather && !Entity.returning && Entity.harvestTimer > 0 && AtTarget;
        const bool Constructing = Entity.order == cinder::Order::Construct
            && Simulation.constructionActive(Target->id) && AtTarget;
        if (!Mining && !Constructing) continue;
        const cinder::Vec2 WorkPoint{Entity.pos.x + DX * 0.58f, Entity.pos.y + DY * 0.58f};
        if (!AreaVisible(Simulation, ViewerTeam, WorkPoint, 18.0f)) continue;
        const float Phase = Time * (Mining ? 12.0f : 16.0f) + Entity.id;
        const FVector Spark(WorkPoint.x + FMath::Cos(Phase) * 8.0f,
            WorkPoint.y + FMath::Sin(Phase) * 8.0f, 22.0f + FMath::Abs(FMath::Sin(Phase * 0.7f)) * 16.0f);
        Add(EBatch::WarmGlow, SphereTransform(Spark, Mining ? 4.0f : 5.5f));
        if (bSoftDust) Add(EBatch::Dust, DustTransform(FVector(WorkPoint.x, WorkPoint.y, 6.0f), Mining ? 14.0f : 18.0f));
    }

    Submit();
    Diagnostics.RenderedInstances = PendingInstanceCount;
    Diagnostics.PeakInstances = FMath::Max(Diagnostics.PeakInstances, PendingInstanceCount);
}

void UCinderWorldEffects::Submit()
{
    const bool bViewerChanged = PendingViewerTeam != SubmittedViewerTeam;
    for (int32 BatchIndex = 0; BatchIndex < Components.Num() && Pending.IsValidIndex(BatchIndex); ++BatchIndex)
    {
        UInstancedStaticMeshComponent* Component = Components[BatchIndex];
        if (!Component) continue;
        const TArray<FTransform>& Transforms = Pending[BatchIndex];
        TArray<FTransform>& Previous = Submitted[BatchIndex];
        const int32 OldCount = Component->GetInstanceCount();
        const int32 NewCount = Transforms.Num();
        bool bUnchanged = OldCount == 0 && NewCount == 0;
        if (!bUnchanged) bUnchanged = !bViewerChanged && Previous.Num() == OldCount && OldCount == NewCount;
        for (int32 Index = 0; bUnchanged && Index < NewCount; ++Index)
            bUnchanged = Transforms[Index].Equals(Previous[Index], 0.0);
        if (bUnchanged) continue;
        bool Accepted = true;
        if (OldCount > NewCount)
        {
            TArray<int32> Tail;
            Tail.Reserve(OldCount - NewCount);
            for (int32 Index = OldCount - 1; Index >= NewCount; --Index) Tail.Add(Index);
            Accepted = Component->RemoveInstances(Tail, true);
        }
        if (Accepted && OldCount < NewCount)
        {
            TArray<FTransform> Tail;
            Tail.Append(Transforms.GetData() + OldCount, NewCount - OldCount);
            Component->AddInstances(Tail, false, false, false);
            Accepted = Component->GetInstanceCount() == NewCount;
        }
        const int32 Common = FMath::Min(OldCount, NewCount);
        if (Accepted && Common > 0)
            Accepted = Component->BatchUpdateInstancesTransforms(0,
                TArrayView<const FTransform>(Transforms.GetData(), Common), false, false, true);
        if (!Accepted || Component->GetInstanceCount() != NewCount)
        {
            Component->ClearInstances();
            if (NewCount > 0) Component->AddInstances(Transforms, false, false, false);
        }
        if (Component->GetInstanceCount() == NewCount) Previous = Transforms;
        else Previous.Reset();
        ++Diagnostics.Uploads;
    }
    SubmittedViewerTeam = PendingViewerTeam;
}

void UCinderWorldEffects::Reset(uint64 SuppressThroughId)
{
    SuppressedThroughId = SuppressThroughId;
    LastSimulationEffectId = SuppressThroughId;
    PendingInstanceCount = 0;
    PendingViewerTeam = SubmittedViewerTeam = INDEX_NONE;
    for (TArray<FTransform>& Batch : Pending) Batch.Reset();
    for (TArray<FTransform>& Batch : Submitted) Batch.Reset();
    for (UInstancedStaticMeshComponent* Component : Components)
        if (Component) Component->ClearInstances();
    Diagnostics = FCinderWorldEffectsStats{};
}
