#include "Sim/AIDifficulty.h"
#include "Sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cinder;
namespace {
void check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
void advance(Simulation& sim, float seconds) {
    for (int i = 0; i < static_cast<int>(std::ceil(seconds / Simulation::Step)); ++i) sim.update(Simulation::Step);
}
float distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
// Isolated development fixture, like AITacticsTests. Production still uses public commands.
Simulation fixture(AIDifficulty level, float seconds = 0, std::uint32_t seed = 7300) {
    Simulation sim; sim.reset({0, seed, true, aiDifficultyAggression(level)});
    const_cast<std::vector<Entity>&>(sim.entities()).clear();
    const_cast<std::vector<Obstacle>&>(sim.obstacles()).clear();
    sim.debugSpawn(Kind::Headquarters, 0, {600, 600});
    sim.debugSpawn(Kind::Headquarters, 1, {4200, 4200});
    sim.debugResources(1, 0); advance(sim, seconds); return sim;
}
std::vector<Id> troops(Simulation& sim, int count, Vec2 origin, Kind kind = Kind::Striker) {
    std::vector<Id> ids;
    for (int i = 0; i < count; ++i)
        ids.push_back(sim.debugSpawn(kind, 1, {origin.x + i % 4 * 50.0f, origin.y + i / 4 * 50.0f}));
    return ids;
}
void everyLevelRecoversItsEconomy() {
    for (std::size_t index = 0; index < kAIDifficultyCount; ++index) {
        auto sim = fixture(aiDifficultyAt(index), 100);
        sim.debugSpawn(Kind::Foundry, 1, {3850, 4350});
        sim.debugSpawn(Kind::Processor, 1, {4500, 4500});
        sim.debugSpawn(Kind::Turret, 1, {4400, 4000});
        sim.debugResources(1, 100); advance(sim, 2.1f);
        int workers = 0;
        for (const Entity& unit : sim.entities()) if (unit.team == 1 && unit.alive()) {
            workers += unit.kind == Kind::Worker;
            for (const QueueItem& item : unit.queue) workers += !item.research && item.kind == Kind::Worker;
        }
        check(workers == 1 && sim.players()[1].ore == 40,
              std::string(aiDifficultyName(aiDifficultyAt(index))) + " did not fund a replacement miner");
    }
}
void beginnerRaidsAreBoundedAndRecoverySurvivesLoading() {
    for (const auto level : {AIDifficulty::VeryEasy, AIDifficulty::Easy}) {
        const float start = level == AIDifficulty::VeryEasy ? 420.0f : 320.0f;
        const float duration = level == AIDifficulty::VeryEasy ? 90.0f : 105.0f;
        const int cap = level == AIDifficulty::VeryEasy ? 4 : 6;
        auto sim = fixture(level, start - 3);
        // A second distant Anchor keeps the fixture running if the first is destroyed.
        sim.debugSpawn(Kind::Headquarters, 0, {600, 4200});
        const auto units = troops(sim, 12, {3870, 3850});
        const auto medics = troops(sim, 2, {3850, 4100}, Kind::Mender);
        const std::size_t firstCommand = sim.recording().size();
        advance(sim, 2.1f);
        for (std::size_t i = firstCommand; i < sim.recording().size(); ++i)
            check(sim.recording()[i].command.type != CommandType::AttackMove, "beginner launched before its grace period");
        advance(sim, 5);
        std::set<Id> wave;
        for (std::size_t i = firstCommand; i < sim.recording().size(); ++i) {
            const Command& order = sim.recording()[i].command;
            if (order.type == CommandType::AttackMove && order.team == 1)
                wave.insert(order.units.begin(), order.units.end());
        }
        check(wave.size() == static_cast<std::size_t>(cap), "beginner sent an oversized or missing opening raid");
        for (Id medic : medics) check(sim.find(medic)->order != Order::Escort,
                                      "repair escorts bypassed the beginner raid budget");
        advance(sim, start + duration - 2 - sim.time());
        check(sim.winner() == -1, "raid fixture ended before recovery could be checked");
        const auto path = std::filesystem::temp_directory_path() /
            (std::string("cinder-raid-recovery-") + std::to_string(cap) + ".save");
        check(sim.save(path.string()), "could not save a raid before its recovery window");
        Simulation loaded; check(loaded.load(path.string()), "could not load raid recovery state");
        std::filesystem::remove(path);
        for (int i = 0; i < 160; ++i) {
            sim.update(Simulation::Step); loaded.update(Simulation::Step);
            check(sim.stateHash() == loaded.stateHash(), "raid schedule changed after loading");
        }
        for (Id id : units) {
            const Entity* unit = sim.find(id);
            if (!unit || !unit->alive()) continue;
            check(unit->order != Order::Attack && unit->order != Order::AttackMove,
                  "beginner kept attacking during the promised recovery window");
        }
        const Vec2 raidPoint{4200, 3800};
        const Id invader = sim.debugSpawn(Kind::Striker, 0, raidPoint);
        const float invaderHp = sim.find(invader)->hp;
        const auto defenseStart = sim.recording().size(); advance(sim, 2.1f);
        bool defended = false;
        for (std::size_t i = defenseStart; i < sim.recording().size(); ++i) {
            const Command& order = sim.recording()[i].command;
            defended |= order.team == 1 && order.type == CommandType::AttackMove &&
                        distance(order.point, raidPoint) < 250;
        }
        // Home reserves may kill the invader through ordinary auto-combat
        // before the next AI decision has to issue an interception command.
        const Entity* survivingInvader = sim.find(invader);
        defended |= !survivingInvader || survivingInvader->hp < invaderHp;
        check(defended, "beginner recovery disabled defense against a real base raid");
        std::cout << "EVIDENCE " << aiDifficultyName(level) << " raid_cap=" << cap << " recovery_save_continuity=1\n";
    }
}
void normalSpendsOnParallelProduction() {
    Simulation sim; sim.reset({0, 7300, true, 1.0f}); advance(sim, 300);
    int factories = 0, army = 0;
    for (const Entity& unit : sim.entities()) if (unit.alive() && unit.team == 1) {
        factories += unit.kind == Kind::Foundry && unit.progress >= 1;
        army += unit.kind >= Kind::Striker && unit.kind <= Kind::Kite;
    }
    check(factories >= 2, "Normal still depends on a single infantry factory");
    check(army >= 9 || sim.winner() == 1, "Normal failed to convert income into an army");
    std::cout << "EVIDENCE Normal_300s factories=" << factories << " army=" << army << " ore=" << sim.players()[1].ore << '\n';
}
Kind firstExpertInfantry(std::uint32_t seed) {
    auto sim = fixture(AIDifficulty::Expert, 0, seed);
    sim.debugSpawn(Kind::Foundry, 1, {3850, 4350});
    sim.debugSpawn(Kind::Processor, 1, {4500, 4500});
    troops(sim, 6, {3900, 4500}, Kind::Worker);
    troops(sim, 4, {3850, 3800}); troops(sim, 2, {4150, 3800}, Kind::Lancer);
    sim.debugSpawn(Kind::Scout, 1, {4400, 3800});
    sim.debugResources(1, 200); const auto start = sim.recording().size(); advance(sim, 2.1f);
    for (std::size_t i = start; i < sim.recording().size(); ++i) {
        const Command& order = sim.recording()[i].command;
        if (order.type == CommandType::Train && (order.kind == Kind::Striker || order.kind == Kind::Lancer)) return order.kind;
    }
    throw std::runtime_error("Expert opening made no paid infantry purchase");
}
void expertOpeningsVaryDeterministically() {
    check(firstExpertInfantry(7302) == Kind::Striker, "infantry-heavy Expert opening ignored its composition");
    check(firstExpertInfantry(7304) == Kind::Lancer, "piercing-heavy Expert opening ignored its composition");
    check(firstExpertInfantry(7304) == firstExpertInfantry(7304), "seeded opening changed across repeats");
}
bool secondaryRaid(bool observed, bool guarded) {
    auto sim = fixture(AIDifficulty::Expert, 200);
    troops(sim, 20, {2450, 1700});
    sim.debugSpawn(Kind::Headquarters, 0, {1100, 1100});
    sim.debugSpawn(Kind::Headquarters, 0, {1100, 3500});
    sim.debugSpawn(Kind::Scout, 1, {1200, 1500});
    if (observed) sim.debugSpawn(Kind::Scout, 1, {1200, 3100});
    if (guarded) sim.debugSpawn(Kind::Turret, 0, {1500, 3400});
    check(sim.visible(1, {1100, 3500}) == observed, "secondary base visibility fixture incorrect");
    const auto start = sim.recording().size(); advance(sim, 2.1f);
    bool raid = false, mainArmy = false;
    for (std::size_t i = start; i < sim.recording().size(); ++i) {
        const Command& order = sim.recording()[i].command;
        if (order.team != 1 || order.type != CommandType::AttackMove) continue;
        if (distance(order.point, {1100, 3500}) < 100 && order.units.size() == 3) raid = true;
        if (distance(order.point, {1100, 1100}) < 100 && order.units.size() >= 11) mainArmy = true;
    }
    if (raid) {
        check(mainArmy, "Expert split off raiders without retaining a main assault");
        const auto path = std::filesystem::temp_directory_path() / "cinder-expert-split-assault.save";
        check(sim.save(path.string()), "could not save split assault");
        Simulation resumed; check(resumed.load(path.string()), "could not load split assault");
        std::filesystem::remove(path);
        for (int i = 0; i < 200; ++i) {
            sim.update(Simulation::Step); resumed.update(Simulation::Step);
            check(sim.stateHash() == resumed.stateHash(), "split assault diverged after loading");
        }
    }
    return raid;
}
void expertRaidsOnlyObservedWeakExpansions() {
    check(secondaryRaid(true, false), "Expert ignored a scouted unguarded expansion");
    check(!secondaryRaid(false, false), "Expert raided an unseen expansion");
    check(!secondaryRaid(true, true), "Expert sent a small raid into known static defenses");
}
}
int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"every level recovers workers", everyLevelRecoversItsEconomy},
        {"bounded beginner raids and saved recovery", beginnerRaidsAreBoundedAndRecoverySurvivesLoading},
        {"Normal parallel production", normalSpendsOnParallelProduction},
        {"Expert seeded compositions", expertOpeningsVaryDeterministically},
        {"Expert observed expansion raids", expertRaidsOnlyObservedWeakExpansions},
    };
    int failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    return failed ? 1 : 0;
}
