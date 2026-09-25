#include "Presentation/CinderWorldEffects.h"
#include "Presentation/CinderLandscapeTerrain.h"
#include "Presentation/CinderTeamColors.h"

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
// A unit still has somewhere to go when its goal is more than a few centimetres
// away. Squared to avoid a square root per entity per frame.
constexpr float TravelDustGoalEpsilonSquared = 36.0f;
constexpr float BasicSphereRadius = 50.0f;
constexpr float BasicCylinderHeight = 100.0f;

// Centimetres of relief under one XY. Every Z literal in this file was authored against
// the old flat plane, where the ground was Z 0 at every XY on the map. Once the generator
// puts real hills and drainage channels outside the obstacle rectangles, a puff authored
// at Z 6 sits six centimetres above sea level, which is half a metre underground on a rise
// and half a metre in the air over a trough. Adding the relief at the effect's own XY
// restores the authored clearance above whatever the ground is actually doing there.
//
// HeightAt, deliberately not BaselineZ + HeightAt: CinderScenery seats every ground prop
// the same way, so effects and the props they sit among keep the exact vertical
// relationship they have today. The one centimetre of baseline is left unspent as extra
// clearance, which is what keeps these thin camera-facing sheets off the surface.
//
// IDEMPOTENCE, which the FogResetAndStableFrame regression enforces: HeightAt is a pure
// function of position with no clock, no frame counter and no simulation mutation, so two
// Update calls on an unchanged simulation still produce byte-identical transforms and
// Submit still uploads nothing. Nothing seated here may ever acquire a time-varying term.
float TerrainZ(const cinder::Simulation& Simulation, float X, float Y)
{
    // Only a compatible world size renders the authored Landscape; every other match
    // length draws a genuinely flat plane, and seating an effect against relief that
    // nothing renders would hang it in the air over level ground. Asked here rather
    // than threaded in from the battlefield because these seating helpers sit in an
    // anonymous namespace shared by thirteen call sites, and the check is a hash of
    // the map index, the world size and four floats per obstacle — a rounding error
    // against a pass that only runs on a simulation step, not on a rendered frame.
    return CinderLandscapeTerrain::IsCanonicalGeometry(Simulation)
        ? CinderLandscapeTerrain::HeightAt(Simulation, X, Y) : 0.0f;
}

// Named apart from CinderScenery's own GroundPoint on purpose: Unreal unity builds fold
// these anonymous namespaces together, and two seating helpers that differ only in
// argument shape are exactly the pair a future edit would call by accident.
FVector EffectGroundPoint(const cinder::Simulation& Simulation, cinder::Vec2 Point, float Offset)
{
    return FVector(Point.x, Point.y, TerrainZ(Simulation, Point.x, Point.y) + Offset);
}

FVector EffectPoint(const cinder::Simulation& Simulation, cinder::Vec2 Point, cinder::Kind Kind)
{
    const cinder::Definition& Definition = cinder::definition(Kind);
    const float Height = Definition.air ? 125.0f + Definition.radius
        : Definition.building ? 35.0f + Definition.radius * 0.45f
        : 20.0f + Definition.radius * 0.8f;
    // Aircraft clearance is measured from the ground for the same reason the battlefield
    // seats their hulls that way: a muzzle bloom pinned to an absolute 125 cm would be
    // swallowed whole by the first 480 cm obstacle mesa the gunship crossed.
    return EffectGroundPoint(Simulation, Point, Height);
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
    const float WorldSize = Simulation.worldSize();
    const float Cell = WorldSize / cinder::Simulation::FogSize;
    if (Point.x - Radius < 0 || Point.y - Radius < 0
        || Point.x + Radius >= WorldSize
        || Point.y + Radius >= WorldSize) return false;
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
    for (int32 Team = 0; Team < CinderTeamColors::Count; ++Team)
        CreateBatch(AttachRoot, Sphere, Emissive, CinderTeamColors::Accent(Team), 5.0f, 1.0f);
    CreateBatch(AttachRoot, Sphere, Emissive, FLinearColor(1.0f, 0.48f, 0.08f), 7.0f, 1.0f);
    CreateBatch(AttachRoot, Cone, Emissive, FLinearColor(1.0f, 0.55f, 0.10f), 6.0f, 1.0f);
    CreateBatch(AttachRoot, Sphere, Emissive, FLinearColor(0.25f, 1.0f, 0.50f), 4.5f, 1.0f);
    for (int32 Team = 0; Team < CinderTeamColors::Count; ++Team)
        CreateBatch(AttachRoot, Cylinder, Emissive, CinderTeamColors::Color(Team), 5.5f, 1.0f);
    CreateBatch(AttachRoot, Cylinder, Emissive, FLinearColor(0.20f, 1.0f, 0.45f), 4.0f, 1.0f);
    CreateBatch(AttachRoot, Plane, Dust, FLinearColor(0.38f, 0.25f, 0.16f), 0.0f, 0.42f);
    CreateBatch(AttachRoot, Plane, Dust, FLinearColor(0.055f, 0.042f, 0.038f), 0.0f, 0.72f);
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
    if (!bInitialized || ViewerTeam < 0 || ViewerTeam >= Simulation.playerCount()
        || ViewerTeam >= cinder::Simulation::MaxPlayers) return;
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
    const float Time = Simulation.time();

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
        if (Effect.team < 0 || Effect.team >= Simulation.playerCount()) continue;
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
        const FVector From = EffectPoint(Simulation, Effect.from, Effect.sourceKind);
        const FVector To = EffectPoint(Simulation, Effect.to, Effect.targetKind);
        const EBatch TeamGlow = static_cast<EBatch>(static_cast<uint8>(EBatch::TeamGlow0) + Effect.team);
        const EBatch TeamBeam = static_cast<EBatch>(static_cast<uint8>(EBatch::TeamBeam0) + Effect.team);

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
                // To.Z is the victim's body height; the kicked-up dust belongs on the
                // surface underneath it, not at the height the round struck.
                if (bSoftDust) Add(EBatch::Dust,
                    DustTransform(EffectGroundPoint(Simulation, Effect.to, 7.0f), 20.0f + Age * 16.0f));
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
                // One relief sample serves the hull burst and the dust sheet under it. An
                // aircraft keeps the body height EffectPoint already seated on the terrain;
                // a ground victim bursts 12 cm above whatever the ground does beneath it.
                const float VictimGroundZ = TerrainZ(Simulation, Effect.to.x, Effect.to.y);
                const FVector Center(Effect.to.x, Effect.to.y,
                    Victim.air ? To.Z : VictimGroundZ + 12.0f);
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
                // The plume stays on the ground even when the victim died in the air.
                if (bSoftDust) Add(EBatch::Dust, DustTransform(FVector(Center.X, Center.Y, VictimGroundZ + 9.0f),
                    Footprint * (0.55f + Age * 0.65f)));
                if (!Victim.air && !Wrecks.ContainsByPredicate(
                    [&](const FWreck& Known) { return Known.EffectId == Effect.id; }))
                {
                    // A fixed ring keeps the cost bounded during a long battle:
                    // the oldest mark is overwritten rather than the array growing.
                    const FWreck Wreck{Effect.to, Footprint * 0.72f, Time, Effect.id};
                    if (Wrecks.Num() < MaxWrecks) Wrecks.Add(Wreck);
                    else { Wrecks[NextWreck] = Wreck; NextWreck = (NextWreck + 1) % MaxWrecks; }
                }
            }
            break;
        }
    }

    // Low-cost continuous cues come only from visible actors performing the
    // corresponding real order. They do not synthesize combat or economy events.
    for (const cinder::Entity& Entity : Simulation.entities())
    {
        if (!Entity.alive() || Entity.kind == cinder::Kind::Resource) continue;
        if (Entity.team < 0 || Entity.team >= Simulation.playerCount()) continue;
        if (Entity.team != ViewerTeam && !Simulation.visible(ViewerTeam, Entity.pos)) continue;
        const cinder::Definition& Definition = cinder::definition(Entity.kind);
        const EBatch TeamGlow = static_cast<EBatch>(static_cast<uint8>(EBatch::TeamGlow0) + Entity.team);
        // One relief sample per entity, shared by every ambience branch below. The generator
        // is closed form rather than a trace, but it still walks the obstacle list on each
        // call, so it is evaluated once here and reused rather than once per plume.
        const float EntityGroundZ = TerrainZ(Simulation, Entity.pos.x, Entity.pos.y);
        if (Definition.air && AreaVisible(Simulation, ViewerTeam, Entity.pos, 18.0f))
        {
            const FVector Forward(FMath::Cos(Entity.facing), FMath::Sin(Entity.facing), 0);
            const float Pulse = 0.82f + FMath::Sin(Time * 15.0f + Entity.id) * 0.18f;
            // Aircraft hold their clearance above the terrain, so the exhaust that trails
            // them has to climb the same hills or it detaches over rising ground.
            const FVector Exhaust(Entity.pos.x, Entity.pos.y,
                EntityGroundZ + 120.0f + Definition.radius);
            Add(TeamGlow, SphereTransform(Exhaust - Forward * (Definition.radius + 8.0f), 6.0f * Pulse));
        }
        // A structure that is actually building something vents. This is ambience
        // and information at once: a player can see at a glance which factories
        // are working, so an idle production building reads as idle from the map
        // rather than only from the panel. Two offset plumes avoid a single
        // metronomic puff. Completed structures only; a site under construction
        // already has its own builder effects.
        if (bSoftDust && Definition.building && Entity.progress >= 1 && !Entity.queue.empty())
        {
            for (int32 Plume = 0; Plume < 2; ++Plume)
            {
                const float Rise = FMath::Frac(Time * 0.42f
                    + static_cast<float>(Entity.id) * 0.37f + Plume * 0.5f);
                const float Spread = Definition.radius * (0.16f + Rise * 0.34f);
                const float Angle = HashAngle(Entity.id, Plume);
                Add(EBatch::Dust, DustTransform(
                    FVector(Entity.pos.x + FMath::Cos(Angle) * Definition.radius * 0.34f,
                            Entity.pos.y + FMath::Sin(Angle) * Definition.radius * 0.34f,
                            EntityGroundZ + 26.0f + Definition.radius * 0.45f + Rise * 78.0f),
                    Spread));
            }
        }
        // Travelling ground units kick dust off the surface. Without it an army
        // slides over the ground weightlessly. The puff duty-cycles per unit, so
        // roughly half a column is emitting at any moment: it reads as
        // intermittent kick-up rather than a solid carpet, and it halves the
        // pressure on the shared dust budget. Ambience is added after combat
        // feedback on purpose, so Add drops these first when the budget is tight.
        if (bSoftDust && !Definition.building && !Definition.air)
        {
            const float GoalDX = Entity.goal.x - Entity.pos.x, GoalDY = Entity.goal.y - Entity.pos.y;
            const float Puff = FMath::Sin(Time * 6.0f + static_cast<float>(Entity.id) * 0.83f);
            if (GoalDX * GoalDX + GoalDY * GoalDY > TravelDustGoalEpsilonSquared && Puff > 0)
            {
                const FVector Behind(-FMath::Cos(Entity.facing), -FMath::Sin(Entity.facing), 0);
                Add(EBatch::Dust, DustTransform(
                    FVector(Entity.pos.x + Behind.X * Definition.radius,
                            Entity.pos.y + Behind.Y * Definition.radius, EntityGroundZ + 5.0f),
                    Definition.radius * (0.55f + Puff * 0.45f)));
            }
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
        // The work point sits up to one combined reach radius from the body, far enough
        // that the body's own sample would bury a 22 cm spark on a real gradient. This is
        // the single branch that pays for a second relief sample, and only for a worker
        // that is actually mining or building.
        const float WorkGroundZ = TerrainZ(Simulation, WorkPoint.x, WorkPoint.y);
        const FVector Spark(WorkPoint.x + FMath::Cos(Phase) * 8.0f,
            WorkPoint.y + FMath::Sin(Phase) * 8.0f,
            WorkGroundZ + 22.0f + FMath::Abs(FMath::Sin(Phase * 0.7f)) * 16.0f);
        Add(EBatch::WarmGlow, SphereTransform(Spark, Mining ? 4.0f : 5.5f));
        if (bSoftDust) Add(EBatch::Dust,
            DustTransform(FVector(WorkPoint.x, WorkPoint.y, WorkGroundZ + 6.0f), Mining ? 14.0f : 18.0f));
    }

    // Wrecks persist well past their death effect so a fought-over position still
    // shows what happened there. They are drawn after combat feedback on purpose,
    // so the instance budget sheds scenery before it sheds live information.
    for (int32 Index = Wrecks.Num() - 1; Index >= 0; --Index)
    {
        const FWreck& Wreck = Wrecks[Index];
        const float Age = Time - Wreck.RecordedAt;
        if (Age < 0 || Age > WreckLifetimeSeconds) continue;
        if (!Simulation.explored(ViewerTeam, Wreck.Position)) continue;
        const float Fade = 1.0f - Age / WreckLifetimeSeconds;
        // A scorch is the one mark that outlives its own effect, so it is also the one most
        // likely to be looked at on sloping ground. One sample per retained wreck, bounded
        // by MaxWrecks at 24, keeps the mark pinned to the hillside it was burned into.
        Add(EBatch::Scorch, DustTransform(EffectGroundPoint(Simulation, Wreck.Position, 3.0f),
            Wreck.Radius * (0.62f + Fade * 0.38f)));
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
    Wrecks.Reset();
    NextWreck = 0;
    for (TArray<FTransform>& Batch : Pending) Batch.Reset();
    for (TArray<FTransform>& Batch : Submitted) Batch.Reset();
    for (UInstancedStaticMeshComponent* Component : Components)
        if (Component) Component->ClearInstances();
    Diagnostics = FCinderWorldEffectsStats{};
}
