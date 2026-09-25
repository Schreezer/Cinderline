#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Presentation/CinderTeamColors.h"
#include "Presentation/CinderWorldEffects.h"
#include "Sim/Network.h"
#include "Tests/AutomationCommon.h"

namespace CinderWorldEffectsTests
{
struct FFixture
{
    FTestWorldWrapper WorldOwner;
    AActor* Owner = nullptr;
    UCinderWorldEffects* Effects = nullptr;
    UStaticMesh* Cylinder = nullptr;

    bool Initialize(FAutomationTestBase& Test)
    {
        if (!WorldOwner.CreateTestWorld(EWorldType::Game))
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        UWorld* World = WorldOwner.GetTestWorld();
        if (!Test.TestNotNull(TEXT("Transient effects world exists"), World)) return false;
        Owner = World->SpawnActor<AActor>();
        if (!Test.TestNotNull(TEXT("Effects owner spawned"), Owner)) return false;

        USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("WorldEffectsRoot"));
        Owner->AddInstanceComponent(Root);
        Owner->SetRootComponent(Root);
        Root->RegisterComponent();
        Effects = NewObject<UCinderWorldEffects>(Owner, TEXT("WorldEffects"));
        Owner->AddInstanceComponent(Effects);
        Effects->RegisterComponent();

        UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
        UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
        UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
        UMaterialInterface* Fallback = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        if (!Test.TestTrue(TEXT("Engine effect primitives and fallback material load"),
            Sphere && Cylinder && Cone && Plane && Fallback)) return false;
        Effects->Initialize(Root, Sphere, Cylinder, Cone, Plane, Fallback);
        if (!Test.TestTrue(TEXT("World effects initialize"), Effects->IsInitialized())) return false;
        if (!WorldOwner.BeginPlayInTestWorld())
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        WorldOwner.ForwardErrorMessages(&Test);
        return !Test.HasAnyErrors();
    }

    int32 EffectComponentCount() const
    {
        TArray<UInstancedStaticMeshComponent*> Components;
        Owner->GetComponents<UInstancedStaticMeshComponent>(Components);
        return Components.Num();
    }

    int32 CylinderInstanceCount() const
    {
        TArray<UInstancedStaticMeshComponent*> Components;
        Owner->GetComponents<UInstancedStaticMeshComponent>(Components);
        int32 Count = 0;
        for (const UInstancedStaticMeshComponent* Component : Components)
            if (Component && Component->GetStaticMesh() == Cylinder) Count += Component->GetInstanceCount();
        return Count;
    }

    int32 TotalInstanceCount() const
    {
        TArray<UInstancedStaticMeshComponent*> Components;
        Owner->GetComponents<UInstancedStaticMeshComponent>(Components);
        int32 Count = 0;
        for (const UInstancedStaticMeshComponent* Component : Components)
            if (Component) Count += Component->GetInstanceCount();
        return Count;
    }

    int32 ComponentsWithTint(FLinearColor Expected) const
    {
        TArray<UInstancedStaticMeshComponent*> EffectComponents;
        Owner->GetComponents<UInstancedStaticMeshComponent>(EffectComponents);
        int32 Count = 0;
        for (const UInstancedStaticMeshComponent* Component : EffectComponents)
        {
            FLinearColor Tint;
            UMaterialInterface* Material = Component ? Component->GetMaterial(0) : nullptr;
            if (Material && Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Tint")), Tint)
                && Tint.Equals(Expected, 0.001f)) ++Count;
        }
        return Count;
    }

    int32 InstancesWithTint(FLinearColor Expected) const
    {
        TArray<UInstancedStaticMeshComponent*> EffectComponents;
        Owner->GetComponents<UInstancedStaticMeshComponent>(EffectComponents);
        int32 Count = 0;
        for (const UInstancedStaticMeshComponent* Component : EffectComponents)
        {
            FLinearColor Tint;
            UMaterialInterface* Material = Component ? Component->GetMaterial(0) : nullptr;
            if (Material && Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Tint")), Tint)
                && Tint.Equals(Expected, 0.001f)) Count += Component->GetInstanceCount();
        }
        return Count;
    }
};

void StopStartingWorkers(cinder::Simulation& Simulation)
{
    for (int32 Team = 0; Team < Simulation.playerCount(); ++Team)
    {
        cinder::Command Stop;
        Stop.type = cinder::CommandType::Stop;
        Stop.team = Team;
        for (const cinder::Entity& Entity : Simulation.entities())
            if (Entity.alive() && Entity.team == Team && Entity.kind == cinder::Kind::Worker)
                Stop.units.push_back(Entity.id);
        Simulation.command(Stop);
    }
}

bool Hold(cinder::Simulation& Simulation, int32 Team, const std::vector<cinder::Id>& Units)
{
    cinder::Command Command;
    Command.type = cinder::CommandType::Hold;
    Command.team = Team;
    Command.units = Units;
    return Simulation.command(Command).accepted;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderWorldEffectsInitialization,
    "Cinderline.Presentation.WorldEffects.Initialization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderWorldEffectsInitialization::RunTest(const FString& Parameters)
{
    using namespace CinderWorldEffectsTests;
    FFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    TestEqual(TEXT("Initialization creates only four team glow/beam pairs, shared effects and one scorch batch"),
        Fixture.EffectComponentCount(), 14);
    for (int32 Team = 0; Team < CinderTeamColors::Count; ++Team)
        TestEqual(*FString::Printf(TEXT("Team %d owns one distinct glow and one distinct beam material"), Team),
            Fixture.ComponentsWithTint(CinderTeamColors::Accent(Team))
                + Fixture.ComponentsWithTint(CinderTeamColors::Color(Team)), 2);
    const int32 BeforeSecondInitialize = Fixture.EffectComponentCount();
    Fixture.Effects->Initialize(Fixture.Owner->GetRootComponent(), nullptr, nullptr, nullptr, nullptr, nullptr);
    TestEqual(TEXT("Repeated initialization creates no components"),
        Fixture.EffectComponentCount(), BeforeSecondInitialize);
    TestEqual(TEXT("Initialized component starts without transient instances"), Fixture.TotalInstanceCount(), 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderWorldEffectsFourTeamRouting,
    "Cinderline.Presentation.WorldEffects.FourTeamRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderWorldEffectsFourTeamRouting::RunTest(const FString& Parameters)
{
    using namespace CinderWorldEffectsTests;
    using namespace cinder;
    FFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    Config FourPlayers;
    FourPlayers.ai = false;
    FourPlayers.playerCount = Simulation::MaxPlayers;
    Simulation Sim;
    Sim.reset(FourPlayers);
    StopStartingWorkers(Sim);
    for (int32 Team = 0; Team < Simulation::MaxPlayers; ++Team)
    {
        const Id Attacker = Sim.debugSpawn(Kind::Lancer, Team, {820.0f + Team * 32.0f, 820.0f});
        const int32 TargetTeam = (Team + 1) % Simulation::MaxPlayers;
        const Id Target = Sim.debugSpawn(Kind::Striker, TargetTeam, {820.0f + Team * 32.0f, 900.0f});
        Command Attack;
        Attack.type = CommandType::Attack;
        Attack.team = Team;
        Attack.units = {Attacker};
        Attack.target = Target;
        TestTrue(TEXT("Each faction accepts an ordinary visible attack command"), Sim.command(Attack).accepted);
    }
    Sim.update(Simulation::Step);
    Fixture.Effects->Update(Sim, 0);
    for (int32 Team = 0; Team < CinderTeamColors::Count; ++Team)
    {
        TestTrue(*FString::Printf(TEXT("Team %d weapon uses its own beam batch"), Team),
            Fixture.InstancesWithTint(CinderTeamColors::Color(Team)) > 0);
        TestTrue(*FString::Printf(TEXT("Team %d muzzle uses its own glow batch"), Team),
            Fixture.InstancesWithTint(CinderTeamColors::Accent(Team)) > 0);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderWorldEffectsHiddenTargetDirection,
    "Cinderline.Presentation.WorldEffects.HiddenTargetDirection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderWorldEffectsHiddenTargetDirection::RunTest(const FString& Parameters)
{
    using namespace CinderWorldEffectsTests;
    using namespace cinder;
    FFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;

    // Build a replica frame with an event whose source is still visible while
    // the recorded target has fallen back into fog. This isolates the renderer
    // contract without relying on combat timing or movement speed.
    Simulation Source;
    Source.reset({0, 53, false, 1});
    StopStartingWorkers(Source);
    const Vec2 From{850, 650};
    const Vec2 HiddenTarget{1500, 650};
    if (!TestTrue(TEXT("Directional fog fixture has a visible source and hidden target"),
        Source.debugSpawn(Kind::Mortar, 0, From) && Source.visible(0, From)
            && !Source.visible(0, HiddenTarget))) return false;

    net::Snapshot Snapshot = net::snapshotFor(Source, 0);
    Effect Weapon;
    Weapon.from = From;
    Weapon.to = HiddenTarget;
    Weapon.team = 0;
    Weapon.life = Weapon.duration = 0.55f;
    Weapon.id = Snapshot.lastEffectId + 1;
    Weapon.type = EffectType::Weapon;
    Weapon.sourceKind = Kind::Mortar;
    Weapon.targetKind = Kind::Headquarters;
    Weapon.fromVisibleMask = 1;
    // The target was visible when the event was recorded, then became hidden
    // in the current replica fog before this presentation update.
    Weapon.toVisibleMask = 1;
    Snapshot.effects.push_back(Weapon);
    Snapshot.lastEffectId = Weapon.id;

    Simulation Replica;
    std::string SnapshotError;
    if (!TestTrue(TEXT("Replica accepts the visibility-boundary effect frame"),
        Replica.applySnapshot(Snapshot, &SnapshotError)))
    {
        AddError(FString::Printf(TEXT("Snapshot error: %s"), UTF8_TO_TCHAR(SnapshotError.c_str())));
        return false;
    }
    TestTrue(TEXT("The source-local muzzle remains visible"), Replica.effectVisible(Replica.effects()[0], 0, true));
    TestFalse(TEXT("The target endpoint remains hidden"), Replica.effectVisible(Replica.effects()[0], 0, false));
    TestFalse(TEXT("The directional link is hidden"), Replica.effectLinkVisible(Replica.effects()[0], 0));

    Fixture.Effects->Update(Replica, 0);
    TestTrue(TEXT("Visible source retains nondirectional muzzle blooms"), Fixture.TotalInstanceCount() > 0);
    TestEqual(TEXT("Hidden target bearing emits no shard, beam, or projectile cylinder"),
        Fixture.CylinderInstanceCount(), 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderWorldEffectsFogResetAndStableFrame,
    "Cinderline.Presentation.WorldEffects.FogResetAndStableFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderWorldEffectsFogResetAndStableFrame::RunTest(const FString& Parameters)
{
    using namespace CinderWorldEffectsTests;
    using namespace cinder;
    FFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    Simulation Sim;
    Sim.reset({0, 42, false, 1});
    StopStartingWorkers(Sim);
    const Id Mortar = Sim.debugSpawn(Kind::Mortar, 1, {3400, 650});
    const Id Victim = Sim.debugSpawn(Kind::Worker, 0, {3990, 650});
    if (!TestTrue(TEXT("Hidden mortar fixture accepts real hold orders"),
        Mortar && Victim && Hold(Sim, 1, {Mortar}) && Hold(Sim, 0, {Victim}))) return false;
    TestFalse(TEXT("Mortar starts outside the viewer's current fog visibility"),
        Sim.visible(0, Sim.find(Mortar)->pos));
    Sim.update(Simulation::Step);

    const Effect* Weapon = nullptr;
    const Effect* Impact = nullptr;
    for (const Effect& Effect : Sim.effects())
    {
        if (Effect.type == EffectType::Weapon) Weapon = &Effect;
        if (Effect.type == EffectType::Impact) Impact = &Effect;
    }
    if (!TestTrue(TEXT("Ordinary hidden mortar combat emits weapon and impact events"), Weapon && Impact)) return false;
    TestFalse(TEXT("Recorded and current visibility hide the mortar muzzle"), Sim.effectVisible(*Weapon, 0, true));
    TestFalse(TEXT("Hidden source prevents projectile link visibility"), Sim.effectLinkVisible(*Weapon, 0));
    TestTrue(TEXT("The victim-local impact remains visible"), Sim.effectVisible(*Impact, 0, false));

    Fixture.Effects->Update(Sim, 0);
    TestTrue(TEXT("Visible impact produces world geometry"), Fixture.Effects->Stats().RenderedInstances > 0);
    TestEqual(TEXT("Hidden mortar produces no beam or projectile cylinder"), Fixture.CylinderInstanceCount(), 0);
    TestEqual(TEXT("Reported instance count matches submitted ISM instances"),
        Fixture.Effects->Stats().RenderedInstances, Fixture.TotalInstanceCount());
    const uint64 LiveUploads = Fixture.Effects->Stats().Uploads;
    const int32 LiveInstances = Fixture.TotalInstanceCount();
    const float StableTime = Sim.time();
    Fixture.Effects->Update(Sim, 0);
    TestEqual(TEXT("Repeated live impact frame submits no duplicate transforms"),
        Fixture.Effects->Stats().Uploads, LiveUploads);
    TestEqual(TEXT("Frozen live impact frame keeps its visible instances"),
        Fixture.TotalInstanceCount(), LiveInstances);
    TestEqual(TEXT("Presentation updates do not advance paused simulation time"), Sim.time(), StableTime);

    Fixture.Effects->Reset(Sim.lastEffectId());
    TestEqual(TEXT("Reset clears all transient geometry"), Fixture.TotalInstanceCount(), 0);
    Fixture.Effects->Update(Sim, 0);
    TestEqual(TEXT("Reset cursor suppresses retained serialized-style events"),
        Fixture.Effects->Stats().RenderedInstances, 0);
    const uint64 StableUploads = Fixture.Effects->Stats().Uploads;
    Fixture.Effects->Update(Sim, 0);
    TestEqual(TEXT("A frozen same-time frame submits no additional uploads"),
        Fixture.Effects->Stats().Uploads, StableUploads);
    TestEqual(TEXT("Repeated suppressed frame remains empty"), Fixture.TotalInstanceCount(), 0);

    const Id HiddenKite = Sim.debugSpawn(Kind::Kite, 1, {3000, 3000});
    if (!TestTrue(TEXT("Fog-change fixture spawns a hidden aircraft"),
        HiddenKite && !Sim.visible(0, Sim.find(HiddenKite)->pos))) return false;
    Fixture.Effects->Update(Sim, 0);
    TestEqual(TEXT("Hidden aircraft adds no engine exhaust"), Fixture.TotalInstanceCount(), 0);
    const uint64 HiddenUploads = Fixture.Effects->Stats().Uploads;
    const Id RevealScout = Sim.debugSpawn(Kind::Scout, 0, {2940, 3000});
    if (!TestTrue(TEXT("Same-time scout changes current fog visibility"),
        RevealScout && Sim.visible(0, Sim.find(HiddenKite)->pos) && Sim.time() == StableTime)) return false;
    Fixture.Effects->Update(Sim, 0);
    TestTrue(TEXT("Same-time fog reveal submits newly visible engine exhaust"),
        Fixture.TotalInstanceCount() > 0 && Fixture.Effects->Stats().Uploads > HiddenUploads);
    const uint64 RevealedUploads = Fixture.Effects->Stats().Uploads;
    Fixture.Effects->Update(Sim, 0);
    TestEqual(TEXT("Repeated revealed frame is upload-free"),
        Fixture.Effects->Stats().Uploads, RevealedUploads);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderWorldEffectsInstanceBudget,
    "Cinderline.Presentation.WorldEffects.InstanceBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderWorldEffectsInstanceBudget::RunTest(const FString& Parameters)
{
    using namespace CinderWorldEffectsTests;
    using namespace cinder;
    FFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    Simulation Sim;
    Sim.reset({0, 77, false, 1});
    StopStartingWorkers(Sim);
    std::vector<Id> Friendly;
    std::vector<Id> Enemy;
    for (int32 Index = 0; Index < 24; ++Index)
    {
        const float OffsetX = static_cast<float>(Index % 6) * 7.0f;
        const float OffsetY = static_cast<float>(Index / 6) * 7.0f;
        // Keep both formations in the clear southwest lane. The central map-0
        // obstacle blocks direct fire across x=2180..2620 at this latitude.
        Friendly.push_back(Sim.debugSpawn(Kind::Striker, 0, {1500 + OffsetX, 1600 + OffsetY}));
        Enemy.push_back(Sim.debugSpawn(Kind::Bastion, 1, {1690 + OffsetX, 1600 + OffsetY}));
    }
    if (!TestTrue(TEXT("Mass-combat fixture accepts real hold orders"),
        Hold(Sim, 0, Friendly) && Hold(Sim, 1, Enemy))) return false;
    Sim.update(Simulation::Step);
    TestTrue(TEXT("Mass-combat fixture emits enough real events to pressure the visual budget"),
        Sim.effects().size() >= 80);

    Fixture.Effects->Update(Sim, 0);
    TestTrue(TEXT("Mass combat reaches the fixed effect budget"), Fixture.Effects->Stats().DroppedForBudget > 0);
    TestTrue(TEXT("Rendered instance count never exceeds the mobile budget"),
        Fixture.Effects->Stats().RenderedInstances <= 160);
    TestEqual(TEXT("Budget accounting matches submitted ISM instances"),
        Fixture.Effects->Stats().RenderedInstances, Fixture.TotalInstanceCount());
    TestEqual(TEXT("Peak accounting records the bounded frame"),
        Fixture.Effects->Stats().PeakInstances, Fixture.Effects->Stats().RenderedInstances);
    return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
