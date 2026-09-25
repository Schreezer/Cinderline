#include "Sim/AIDifficulty.h"
#include "Sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace cinder;

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
float distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
void advance(Simulation& simulation, float seconds) {
    for (int i = 0; i < static_cast<int>(std::ceil(seconds / Simulation::Step)); ++i) {
        simulation.update(Simulation::Step);
    }
}
Simulation fixture(AIDifficulty difficulty = AIDifficulty::Hard) {
    Simulation simulation;
    // This fixture replaces the entire world with an open, flat tactics arena.
    simulation.reset({0, 93617, true, aiDifficultyAggression(difficulty), MatchLength::Standard, 2, 0});
    const_cast<std::vector<Entity>&>(simulation.entities()).clear();
    const_cast<std::vector<Obstacle>&>(simulation.obstacles()).clear();
    simulation.debugSpawn(Kind::Headquarters, 0, {400, 400});
    simulation.debugSpawn(Kind::Headquarters, 1, {4200, 4200});
    simulation.debugResources(1, 0);
    return simulation;
}
CommandResult send(Simulation& simulation, CommandType type, std::vector<Id> units,
                   Vec2 point = {}, Id target = 0, int team = 1) {
    return simulation.command({type, team, std::move(units), point, target, Kind::Worker, 0});
}
std::vector<Id> army(Simulation& simulation, int count, Vec2 origin, Kind kind = Kind::Striker) {
    std::vector<Id> result;
    for (int i = 0; i < count; ++i) {
        result.push_back(simulation.debugSpawn(kind, 1,
            {origin.x + (i % 4) * 55.0f, origin.y + (i / 4) * 55.0f}));
    }
    return result;
}
bool includes(const Command& command, Id id) {
    return std::find(command.units.begin(), command.units.end(), id) != command.units.end();
}

void economyBuildingsDoNotCauseFalseRetreat() {
    for (AIDifficulty difficulty : {AIDifficulty::Hard, AIDifficulty::Expert}) {
        auto simulation = fixture(difficulty);
        const auto attackers = army(simulation, 3, {1700, 1500});
        for (int i = 0; i < 8; ++i) {
            simulation.debugSpawn(Kind::Processor, 0, {1260.0f + (i % 2) * 180.0f,
                                                       1170.0f + (i / 2) * 180.0f});
        }
        check(send(simulation, CommandType::AttackMove, attackers, {1250, 1450}).accepted,
              "fixture assault command was rejected");
        const auto start = simulation.recording().size();
        advance(simulation, 2.1f);
        bool fighting = false;
        for (std::size_t i = start; i < simulation.recording().size(); ++i) {
            const Command& order = simulation.recording()[i].command;
            if (order.team != 1 || !std::any_of(attackers.begin(), attackers.end(), [&](Id id) { return includes(order, id); })) continue;
            check(order.type != CommandType::Move || distance(order.point, {4200, 4200}) > 500,
                  "unarmed economic structures caused a healthy assault to retreat");
            fighting |= order.type == CommandType::Attack;
        }
        check(fighting, "small committed assault stopped attacking a defenseless economy");
    }
}

void heavyLocalArmyForcesARealWithdrawal() {
    auto simulation = fixture();
    const auto attackers = army(simulation, 4, {1700, 1700});
    for (int i = 0; i < 4; ++i) {
        simulation.debugSpawn(Kind::Bastion, 0, {1250.0f, 1600.0f + i * 80.0f});
    }
    check(send(simulation, CommandType::AttackMove, attackers, {1200, 1700}).accepted,
          "fixture losing assault command was rejected");
    advance(simulation, 0.05f);
    for (Id id : attackers) {
        const Entity* unit = simulation.find(id);
        check(unit && unit->order == Order::Move && distance(unit->goal, {4200, 4200}) < 450,
              "infantry failed to withdraw from an equally sized but much stronger armored army");
    }
}

void minorRaidDoesNotRecallTheWholeAssault() {
    auto simulation = fixture();
    const auto assault = army(simulation, 6, {2000, 1800});
    const auto guards = army(simulation, 3, {4150, 4010});
    simulation.debugSpawn(Kind::Scout, 0, {4200, 3690});
    check(send(simulation, CommandType::AttackMove, assault, {700, 700}).accepted,
          "fixture main assault command was rejected");
    const auto start = simulation.recording().size();
    advance(simulation, 2.1f);
    for (std::size_t i = start; i < simulation.recording().size(); ++i) {
        const auto& order = simulation.recording()[i].command;
        if (order.team != 1) continue;
        for (Id id : assault) {
            if (includes(order, id)) {
                check((order.type != CommandType::AttackMove && order.type != CommandType::Move) ||
                      distance(order.point, {4200, 3690}) > 1500,
                      "one raider redirected the committed army back across the map");
            }
        }
    }
    check(std::any_of(guards.begin(), guards.end(), [&](Id id) {
        const Entity* guard = simulation.find(id);
        return guard && (guard->order == Order::Attack || guard->order == Order::AttackMove);
    }), "nearby guards ignored the base raid");
}

void mixedRaidUsesGroundAndAirDefenders() {
    auto simulation = fixture();
    const auto armor = army(simulation, 8, {4070, 4190}, Kind::Bastion);
    const auto antiAir = army(simulation, 2, {4120, 3980}, Kind::Lancer);
    simulation.debugSpawn(Kind::Kite, 0, {3750, 4100});
    for (int i = 0; i < 8; ++i) {
        simulation.debugSpawn(Kind::Striker, 0, {3700.0f + (i % 3) * 55.0f, 4000.0f + (i / 3) * 55.0f});
    }
    advance(simulation, 0.05f);
    check(std::any_of(armor.begin(), armor.end(), [&](Id id) {
        const Entity* unit = simulation.find(id);
        return unit && (unit->order == Order::Attack || unit->order == Order::AttackMove);
    }), "aircraft at the head of a mixed raid prevented ground defenders from responding");
    check(std::any_of(antiAir.begin(), antiAir.end(), [&](Id id) {
        const Entity* unit = simulation.find(id);
        return unit && unit->order == Order::Attack && definition(simulation.find(unit->target)->kind).air;
    }), "mixed raid did not receive compatible anti-air defenders");
}

void countersSelectCompatibleHighValueTargets() {
    auto simulation = fixture();
    const Id lancer = simulation.debugSpawn(Kind::Lancer, 1, {3800, 4000});
    const Id mortar = simulation.debugSpawn(Kind::Mortar, 1, {3880, 4200});
    const Id aircraft = simulation.debugSpawn(Kind::Kite, 0, {3520, 4000});
    const Id structure = simulation.debugSpawn(Kind::Foundry, 0, {3440, 4230});
    simulation.debugSpawn(Kind::Worker, 0, {3650, 4030});
    advance(simulation, 0.05f);
    check(simulation.find(lancer)->order == Order::Attack && simulation.find(lancer)->target == aircraft,
          "anti-air infantry chased a worker instead of the nearby aircraft");
    check(simulation.find(mortar)->order == Order::Attack && simulation.find(mortar)->target == structure,
          "siege failed to select a reachable structure while an incompatible aircraft was visible");
}

void damagedVeteransStillFightWithoutAHealer() {
    auto simulation = fixture();
    const auto attackers = army(simulation, 3, {1650, 1400});
    for (Id id : attackers) const_cast<Entity*>(simulation.find(id))->hp = 40.0f;
    const Id target = simulation.debugSpawn(Kind::Processor, 0, {1370, 1400});
    check(send(simulation, CommandType::AttackMove, attackers, {1300, 1400}).accepted,
          "fixture damaged assault command was rejected");
    advance(simulation, 2.1f);
    check(simulation.find(target)->hp < definition(Kind::Processor).hp,
          "damaged veterans were permanently benched instead of finishing an undefended target");
    for (Id id : attackers) {
        check(simulation.find(id)->order != Order::Move,
              "damaged infantry retreated despite having no recovery path or opposing weapons");
    }
}

void scatteredArmyAssemblesBeforeLaunching() {
    auto simulation = fixture();
    advance(simulation, 190.1f);
    std::vector<Id> soldiers;
    for (Vec2 point : {Vec2{4100, 4100}, Vec2{2500, 3600}, Vec2{3600, 2500}}) {
        const auto group = army(simulation, 3, point);
        soldiers.insert(soldiers.end(), group.begin(), group.end());
    }
    const auto start = simulation.recording().size();
    advance(simulation, 2.1f);
    bool assembling = false;
    for (std::size_t i = start; i < simulation.recording().size(); ++i) {
        const auto& order = simulation.recording()[i].command;
        if (order.team != 1 || !std::any_of(soldiers.begin(), soldiers.end(), [&](Id id) { return includes(order, id); })) continue;
        check(order.type != CommandType::AttackMove,
              "scattered fragments launched as if they were a concentrated army");
        assembling |= order.type == CommandType::Move;
    }
    check(assembling, "scattered army did not assemble at a common position");
}

void unseenForcesDoNotAffectTacticalCommands() {
    auto ordinary = fixture();
    const auto attackers = army(ordinary, 3, {1650, 1400});
    check(send(ordinary, CommandType::AttackMove, attackers, {1100, 1400}).accepted,
          "fog fixture attack command was rejected");
    auto hidden = ordinary;
    for (int i = 0; i < 12; ++i) hidden.debugSpawn(Kind::Bastion, 0, {4000.0f, 400.0f + i * 65.0f});
    advance(ordinary, 2.1f);
    advance(hidden, 2.1f);
    check(ordinary.recording().size() == hidden.recording().size(), "hidden army changed the number of AI decisions");
    for (std::size_t i = 0; i < ordinary.recording().size(); ++i) {
        const auto& a = ordinary.recording()[i].command;
        const auto& b = hidden.recording()[i].command;
        check(a.type == b.type && a.units == b.units && a.target == b.target && distance(a.point, b.point) < 0.01f,
              "tactics used unseen enemy positions or composition");
    }
}

void supportAndAssaultSurviveSaveLoad() {
    auto original = fixture(AIDifficulty::Expert);
    const auto attackers = army(original, 4, {2300, 1900});
    const Id medic = original.debugSpawn(Kind::Mender, 1, {2550, 2000});
    check(send(original, CommandType::AttackMove, attackers, {1300, 1200}).accepted,
          "continuity fixture assault command was rejected");
    advance(original, 2.1f);
    check(original.find(medic)->order == Order::Escort, "repair support did not follow the assault");
    const auto path = std::filesystem::temp_directory_path() / "cinderline-ai-tactics-continuity.save";
    check(original.save(path.string()), "tactical fixture failed to save");
    Simulation resumed;
    check(resumed.load(path.string()), "tactical fixture failed to load");
    std::filesystem::remove(path);
    for (int i = 0; i < 200; ++i) {
        original.update(Simulation::Step);
        resumed.update(Simulation::Step);
        check(original.stateHash() == resumed.stateHash(), "AI assault or support orders diverged after save/load");
    }
}
}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"undefended economy assault", economyBuildingsDoNotCauseFalseRetreat},
        {"local weighted threat withdrawal", heavyLocalArmyForcesARealWithdrawal},
        {"limited raid response", minorRaidDoesNotRecallTheWholeAssault},
        {"mixed raid defender allocation", mixedRaidUsesGroundAndAirDefenders},
        {"counter-aware local targets", countersSelectCompatibleHighValueTargets},
        {"damaged veteran reuse", damagedVeteransStillFightWithoutAHealer},
        {"concentrated army launch", scatteredArmyAssemblesBeforeLaunching},
        {"fog-fair tactics", unseenForcesDoNotAffectTacticalCommands},
        {"tactical save continuation", supportAndAssaultSurviveSaveLoad},
    };
    int failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    return failed == 0 ? 0 : 1;
}
