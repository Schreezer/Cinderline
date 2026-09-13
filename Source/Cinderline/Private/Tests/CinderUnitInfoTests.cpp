#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Presentation/CinderUnitInfo.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderUnitInfoTest,
    "Cinderline.Presentation.UnitInfo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderUnitInfoTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace cinder;

    for (const Definition& Definition : definitions())
    {
        TestFalse(*FString::Printf(TEXT("%s has selection purpose copy"), UTF8_TO_TCHAR(Definition.name)),
            CinderUnitInfo::Purpose(Definition.kind).IsEmpty());
        TestEqual(*FString::Printf(TEXT("%s exposes its public center range"), UTF8_TO_TCHAR(Definition.name)),
            CinderUnitInfo::Range(Definition.kind), Definition.range);
    }

    Simulation Sim;
    Sim.reset({0, 42, false, 1});
    Sim.debugResources(0, 5000);
    const Id Laboratory = Sim.debugSpawn(Kind::Laboratory, 0, {1200, 1200});
    Command Research;
    Research.type = CommandType::Research;
    Research.team = 0;
    Research.units = {Laboratory};
    Research.queueIndex = 1;
    TestTrue(TEXT("Weapons research queues through an operational Resonator"), Sim.command(Research).accepted);
    Research.queueIndex = 2;
    TestTrue(TEXT("Armor research queues through an operational Resonator"), Sim.command(Research).accepted);
    for (int Second = 0; Second < 121; ++Second) Sim.update(1.0f);
    TestEqual(TEXT("Test fixture completed weapons level one"), Sim.players()[0].weapons, 1);
    TestEqual(TEXT("Test fixture completed armor level one"), Sim.players()[0].armor, 1);

    const Id Striker = Sim.debugSpawn(Kind::Striker, 0, {1500, 1500});
    const FCinderUnitDetails StrikerInfo = CinderUnitInfo::Describe(Sim, *Sim.find(Striker));
    TestEqual(TEXT("Friendly damage includes the current weapons upgrade"), StrikerInfo.Damage, definition(Kind::Striker).damage * 1.12f);
    TestEqual(TEXT("Friendly armor includes the current armor upgrade"), StrikerInfo.Armor, static_cast<float>(definition(Kind::Striker).armor + 1));
    TestTrue(TEXT("Damage is identified as pre-armor"), StrikerInfo.DamageNote.Contains(TEXT("before armor")));
    TestTrue(TEXT("Ember counter behavior is described"), StrikerInfo.DamageNote.Contains(TEXT("1.35x")));

    const Id Enemy = Sim.debugSpawn(Kind::Striker, 1, {3300, 3300});
    const FCinderUnitDetails EnemyInfo = CinderUnitInfo::Describe(Sim, *Sim.find(Enemy));
    TestEqual(TEXT("Enemy selection exposes public base damage only"), EnemyInfo.Damage, definition(Kind::Striker).damage);
    TestEqual(TEXT("Enemy selection exposes public base armor only"), EnemyInfo.Armor, static_cast<float>(definition(Kind::Striker).armor));
    TestTrue(TEXT("Enemy upgrades are identified as unknown"), EnemyInfo.DamageNote.Contains(TEXT("upgrades are unknown")));

    const Id Mender = Sim.debugSpawn(Kind::Mender, 0, {1600, 1500});
    const FCinderUnitDetails MenderInfo = CinderUnitInfo::Describe(Sim, *Sim.find(Mender));
    TestTrue(TEXT("Mend is identified as a healer"), MenderInfo.bHealer);
    TestFalse(TEXT("Mend is not identified as armed"), MenderInfo.bArmed);
    TestEqual(TEXT("Mend healing amount matches simulation combat"), MenderInfo.HealAmount, 16.0f);
    TestEqual(TEXT("Mend healing interval matches its definition"), MenderInfo.HealInterval, 0.75f);
    TestTrue(TEXT("Mend limitations are described"), MenderInfo.DamageNote.Contains(TEXT("buildings cannot")));

    const Id Turret = Sim.debugSpawn(Kind::Turret, 0, {1800, 1500});
    const FCinderUnitDetails TurretInfo = CinderUnitInfo::Describe(Sim, *Sim.find(Turret));
    TestEqual(TEXT("Ward exposes its static center range"), TurretInfo.Range, definition(Kind::Turret).range);
    TestTrue(TEXT("Ward explains target-radius reach"), TurretInfo.DamageNote.Contains(TEXT("target's footprint radius")));

    const FCinderUnitDetails ResearchInfo = CinderUnitInfo::Describe(Sim, *Sim.find(Laboratory));
    TestTrue(TEXT("Resonator purpose lists research"), ResearchInfo.Purpose.Contains(TEXT("Research")));
    TestTrue(TEXT("Resonator purpose lists Mend training"), ResearchInfo.Purpose.Contains(TEXT("Mend")));
    TestTrue(TEXT("Unlock copy comes from its current definition"), ResearchInfo.Capabilities.Contains(TEXT("350 ore")));

    Entity Defending = *Sim.find(Striker);
    Defending.order = Order::Defend;
    Defending.goal = {Defending.pos.x + 6.0f, Defending.pos.y};
    TestEqual(TEXT("Defend travel status uses the assigned goal anchor"),
        CinderUnitInfo::Describe(Sim, Defending).OrderText, FString(TEXT("Moving to defend")));
    Defending.goal = {Defending.pos.x + 5.0f, Defending.pos.y};
    TestEqual(TEXT("Defend arrival status uses the simulation's five-unit boundary"),
        CinderUnitInfo::Describe(Sim, Defending).OrderText, FString(TEXT("Defending")));

    Config ShortConfig;
    ShortConfig.ai = false;
    ShortConfig.matchLength = MatchLength::Short;
    Simulation ShortSim;
    ShortSim.reset(ShortConfig);
    const Id ShortKiln = ShortSim.debugSpawn(Kind::Foundry, 0, {900, 900});
    TestTrue(TEXT("Short match building information shows its active 52-second duration"),
        CinderUnitInfo::Describe(ShortSim, *ShortSim.find(ShortKiln)).Capabilities.Contains(TEXT("52 seconds")));
    return true;
}

#endif
