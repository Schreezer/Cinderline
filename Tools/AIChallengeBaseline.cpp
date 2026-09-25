#include "Sim/Simulation.h"
#include "Sim/AIDifficulty.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// Paid, deterministic opponents. Both sides use the normal economy, fog,
// production, command validation and combat rules. No debug resources/spawns.
// This is a balance benchmark, deliberately not a win-rate assertion in CTest.
using namespace cinder;

namespace {
float distanceSquared(Vec2 a, Vec2 b) {
    const float x = a.x - b.x, y = a.y - b.y;
    return x * x + y * y;
}

bool combatUnit(Kind kind) {
    return !definition(kind).building && kind != Kind::Worker && kind != Kind::Resource;
}

int count(const Simulation& sim, int team, Kind kind, bool queued = false) {
    int result = 0;
    for (const Entity& entity : sim.entities()) {
        if (!entity.alive() || entity.team != team) continue;
        if (entity.kind == kind) ++result;
        if (queued) for (const QueueItem& item : entity.queue)
            if (!item.research && item.kind == kind) ++result;
    }
    return result;
}

struct Opponent {
    std::string style;
    Vec2 home{}, enemyHome{}, rally{};
    int accepted = 0, rejected = 0;
    bool launched = false;
    std::uint64_t nextAttack = 0;

    bool issue(Simulation& sim, Command command) {
        command.team = 0;
        const auto result = sim.command(command);
        if (result.accepted) ++accepted; else ++rejected;
        return result.accepted;
    }

    void initialize(Simulation& sim) {
        for (const Entity& entity : sim.entities()) if (entity.team == 0 && entity.kind == Kind::Headquarters)
            home = entity.pos;
        // Public map spawn locations, never reads the opponent's hidden actors.
        enemyHome = {sim.worldSize() - home.x, sim.worldSize() - home.y};
        rally = {home.x + 480, home.y + 180};
        Command command; command.type = CommandType::AutoRally;
        command.kind = Kind::Resource; command.point = rally; issue(sim, command);
    }

    bool build(Simulation& sim, Kind kind) {
        if (!sim.autoBuildStatus(0, kind).accepted) return false;
        // Spread structures far enough apart for the largest combat units.
        for (float radius : {300.0f, 460.0f, 620.0f, 800.0f, 980.0f}) {
            for (int spoke = 0; spoke < 24; ++spoke) {
                const float angle = static_cast<float>(spoke) * 6.28318530718f / 24.0f;
                const Vec2 point{home.x + std::cos(angle) * radius, home.y + std::sin(angle) * radius};
                bool passage = true;
                for (const Entity& entity : sim.entities()) {
                    if (!entity.alive() || (!definition(entity.kind).building && entity.kind != Kind::Resource)) continue;
                    // Own placement may consult only known map geometry.
                    if (entity.kind == Kind::Resource ? !sim.explored(0, entity.pos) :
                        entity.team != 0 && !sim.visible(0, entity.pos)) continue;
                    const float gap = definition(kind).radius + definition(entity.kind).radius + 78.0f;
                    if (distanceSquared(point, entity.pos) < gap * gap) { passage = false; break; }
                }
                if (!passage || !sim.autoBuildStatus(0, kind, &point).accepted) continue;
                Command command; command.type = CommandType::AutoBuild;
                command.kind = kind; command.point = point;
                return issue(sim, command);
            }
        }
        return false;
    }

    bool train(Simulation& sim, Kind kind, int target) {
        if (count(sim, 0, kind, true) >= target) return false;
        // One queued unit per producer; don't lock the whole bank in a queue.
        for (const Entity& entity : sim.entities()) {
            if (!entity.alive() || entity.team != 0 || entity.progress < 1 ||
                entity.kind != definition(kind).producer || !entity.queue.empty()) continue;
            const Id producer = entity.id;
            if (!sim.autoTrainStatus(0, kind, 1, producer).accepted) continue;
            Command command; command.type = CommandType::AutoTrain;
            command.kind = kind; command.queueIndex = 1; command.target = producer;
            return issue(sim, command);
        }
        return false;
    }

    void maintainMining(Simulation& sim) {
        std::vector<Id> idle;
        for (const Entity& entity : sim.entities())
            if (entity.alive() && entity.team == 0 && entity.kind == Kind::Worker && entity.order == Order::Idle)
                idle.push_back(entity.id);
        for (Id id : idle) {
            const Entity* worker = sim.find(id);
            Id best = 0; float bestScore = std::numeric_limits<float>::max();
            for (const Entity& resource : sim.entities()) {
                if (resource.kind != Kind::Resource || resource.resource <= 0 || !sim.explored(0, resource.pos)) continue;
                int miners = 0;
                for (const Entity& other : sim.entities()) if (other.alive() && other.team == 0 &&
                    other.kind == Kind::Worker && other.resourceTarget == resource.id) ++miners;
                const float score = distanceSquared(worker->pos, resource.pos) + miners * 60000.0f;
                if (score < bestScore) { bestScore = score; best = resource.id; }
            }
            if (best) { Command command; command.type = CommandType::Gather; command.units = {id};
                command.target = best; issue(sim, command); }
        }
        std::vector<Id> orphaned;
        for (const Entity& entity : sim.entities()) if (entity.alive() && entity.team == 0 &&
            definition(entity.kind).building && entity.progress < 1 && !sim.constructionWorker(entity.id))
            orphaned.push_back(entity.id);
        for (Id id : orphaned) {
            std::vector<Id> workers;
            for (const Entity& entity : sim.entities()) if (entity.alive() && entity.team == 0 &&
                entity.kind == Kind::Worker && entity.order != Order::Construct) workers.push_back(entity.id);
            if (workers.empty()) break;
            Command command; command.type = CommandType::ResumeConstruction;
            command.units = workers; command.target = id; issue(sim, command);
        }
    }

    void tick(Simulation& sim) {
        if (style == "passive") return;
        maintainMining(sim);
        const bool rush = style == "rush", turtle = style == "turtle";
        const int workerTarget = rush ? 10 : 18;
        if (count(sim, 0, Kind::Foundry) == 0) build(sim, Kind::Foundry);
        if (sim.capacity(0) - sim.supply(0) <= 6 && count(sim, 0, Kind::Processor) < 12) {
            bool pending = false;
            for (const Entity& entity : sim.entities()) if (entity.alive() && entity.team == 0 &&
                entity.kind == Kind::Processor && entity.progress < 1) pending = true;
            if (!pending) build(sim, Kind::Processor);
        }
        train(sim, Kind::Worker, workerTarget);
        const int foundryTarget = rush ? 3 : 2;
        if (sim.time() >= (rush ? 70.0f : 120.0f) && count(sim, 0, Kind::Foundry) < foundryTarget)
            build(sim, Kind::Foundry);
        if (turtle && sim.time() > 120 && count(sim, 0, Kind::Turret) < 5)
            build(sim, Kind::Turret);
        if (!rush && sim.time() > 180 && count(sim, 0, Kind::Laboratory) == 0)
            build(sim, Kind::Laboratory);
        if (!rush && sim.time() > 230 && sim.players()[0].tier < 2 && sim.autoResearchStatus(0, 0).accepted) {
            Command command; command.type = CommandType::AutoResearch; command.queueIndex = 0; issue(sim, command);
        }
        bool fundingTechnology = !rush && sim.time() > 230 && sim.players()[0].tier < 2 &&
            count(sim, 0, Kind::Laboratory) > 0;
        for (const Entity& entity : sim.entities()) if (entity.alive() && entity.team == 0)
            for (const QueueItem& item : entity.queue) if (item.research) fundingTechnology = false;
        if (!rush && sim.players()[0].tier >= 2 && count(sim, 0, Kind::MotorPool) < 2)
            build(sim, Kind::MotorPool);
        if (!fundingTechnology) {
            train(sim, Kind::Mortar, turtle ? 5 : 3);
            train(sim, Kind::Bastion, 8);
            if (!rush) train(sim, Kind::Mender, 2);
            // Mix piercing infantry with the main mass; all recruitment is paid.
            if (count(sim, 0, Kind::Striker, true) >= 4 && count(sim, 0, Kind::Lancer, true) * 3 < count(sim, 0, Kind::Striker, true))
                train(sim, Kind::Lancer, 16);
            for (int producer = 0; producer < foundryTarget; ++producer) train(sim, Kind::Striker, 60);
        }

        std::vector<Id> army;
        for (const Entity& entity : sim.entities()) if (entity.alive() && entity.team == 0 && combatUnit(entity.kind))
            army.push_back(entity.id);
        if (!launched && sim.time() >= (rush ? 155.0f : turtle ? 650.0f : 380.0f) &&
            army.size() >= static_cast<std::size_t>(rush ? 6 : 14)) launched = true;
        if (!launched || sim.tick() < nextAttack || army.empty()) return;
        nextAttack = sim.tick() + static_cast<std::uint64_t>(20.0f / Simulation::Step);
        Command attack; attack.type = CommandType::AttackMove; attack.units = army; attack.point = enemyHome;
        // Finish observed bases, otherwise move toward the public enemy spawn.
        for (const Entity& entity : sim.entities()) if (entity.alive() && entity.team == 1 &&
            entity.kind == Kind::Headquarters && sim.visible(0, entity.pos)) { attack.point = entity.pos; break; }
        issue(sim, attack);
    }
};

struct Snapshot {
    int workers = 0, army = 0, armyValue = 0, buildings = 0, ore = 0, gathered = 0;
    int foundries = 0, supply = 0, capacity = 0;
    int produced = 0, killed = 0, lost = 0, buildingsDestroyed = 0, tier = 0;
    float damage = 0;
};

Snapshot snapshot(const Simulation& sim, int team) {
    Snapshot result;
    for (const Entity& entity : sim.entities()) if (entity.alive() && entity.team == team) {
        if (entity.kind == Kind::Worker) ++result.workers;
        if (combatUnit(entity.kind)) { ++result.army; result.armyValue += definition(entity.kind).cost; }
        if (definition(entity.kind).building && entity.progress >= 1) ++result.buildings;
        if (entity.kind == Kind::Foundry && entity.progress >= 1) ++result.foundries;
    }
    const Player& player = sim.players()[team];
    result.ore = player.ore; result.gathered = player.stats.gathered; result.produced = player.stats.produced;
    result.killed = player.stats.killed; result.lost = player.stats.lost; result.damage = player.stats.damage;
    result.buildingsDestroyed = player.stats.buildingsDestroyed; result.tier = player.tier;
    result.supply = sim.supply(team); result.capacity = sim.capacity(team);
    return result;
}

void printSnapshot(const Snapshot& value) {
    std::cout << "{\"workers\":" << value.workers << ",\"army\":" << value.army
        << ",\"army_value\":" << value.armyValue << ",\"buildings\":" << value.buildings
        << ",\"foundries\":" << value.foundries << ",\"supply\":" << value.supply << ",\"capacity\":" << value.capacity
        << ",\"ore\":" << value.ore << ",\"gathered\":" << value.gathered
        << ",\"produced\":" << value.produced << ",\"killed\":" << value.killed
        << ",\"lost\":" << value.lost << ",\"buildings_destroyed\":" << value.buildingsDestroyed
        << ",\"tier\":" << value.tier << ",\"damage\":" << value.damage << '}';
}

void run(const std::string& style, int map, std::uint32_t seed, int seconds, AIDifficulty difficulty) {
    Simulation sim; sim.reset({map, seed, true, aiDifficultyAggression(difficulty)});
    Opponent opponent; opponent.style = style; opponent.initialize(sim);
    float firstDamage = -1, firstPressure = -1;
    std::array<Snapshot, 3> checkpoints{};
    constexpr std::array<int, 3> checkpointSeconds{180, 300, 600};
    const int maxTicks = static_cast<int>(seconds / Simulation::Step);
    for (int tick = 0; tick < maxTicks && sim.winner() == -1; ++tick) {
        if (tick % 40 == 0) opponent.tick(sim);
        sim.update(Simulation::Step);
        if (firstDamage < 0 && sim.players()[1].stats.damage > 0) firstDamage = sim.time();
        if (firstPressure < 0) {
            int attackers = 0;
            for (const Entity& entity : sim.entities())
                if (entity.alive() && entity.team == 1 && combatUnit(entity.kind) &&
                    distanceSquared(entity.pos, opponent.home) < 1000.0f * 1000.0f) ++attackers;
            if (attackers >= 4) firstPressure = sim.time();
        }
        for (std::size_t index = 0; index < checkpoints.size(); ++index)
            if (sim.tick() == static_cast<std::uint64_t>(checkpointSeconds[index] / Simulation::Step))
                checkpoints[index] = snapshot(sim, 1);
    }
    std::cout << std::fixed << std::setprecision(2)
        << "{\"scenario\":\"" << style << "\",\"map\":" << map << ",\"seed\":" << seed
        << ",\"difficulty\":\"" << aiDifficultyName(difficulty) << "\",\"seconds\":" << sim.time() << ",\"winner\":" << sim.winner()
        << ",\"ai_first_damage_seconds\":" << firstDamage << ",\"ai_first_pressure_seconds\":" << firstPressure
        << ",\"script_accepted\":" << opponent.accepted << ",\"script_rejected\":" << opponent.rejected
        << ",\"state_hash\":" << sim.stateHash() << ",\"ai\":";
    printSnapshot(snapshot(sim, 1)); std::cout << ",\"opponent\":"; printSnapshot(snapshot(sim, 0));
    std::cout << ",\"ai_checkpoints\": [";
    for (std::size_t index = 0; index < checkpoints.size(); ++index) {
        if (index) std::cout << ',';
        std::cout << "{\"seconds\":" << checkpointSeconds[index] << ",\"state\":";
        if (sim.time() < checkpointSeconds[index]) std::cout << "null"; else printSnapshot(checkpoints[index]);
        std::cout << '}';
    }
    std::cout << "]}" << std::endl;
}
}

int main(int argc, char** argv) {
    try {
        int seconds = 900, map = -1, seeds = 1;
        std::uint32_t firstSeed = 7300;
        std::string scenario = "all", level = "hard";
        const std::array<std::string, 5> levels{"very-easy", "easy", "normal", "hard", "expert"};
        for (int index = 1; index < argc; ++index) {
            const std::string flag = argv[index];
            if (flag == "--help") {
                std::cout << "AIChallengeBaseline [--scenario all|rush|macro|turtle|passive] [--map 0|1|2] "
                             "[--seed N] [--seeds N] [--seconds N] [--difficulty all|very-easy|easy|normal|hard|expert]\n"
                             "Outputs one JSON object per paid match. winner=1 is the selected AI, 0 is scripted opponent, -1 is unresolved.\n";
                return 0;
            }
            if (index + 1 >= argc) throw std::runtime_error("missing argument for " + flag);
            const std::string value = argv[++index];
            if (flag == "--seconds") seconds = std::stoi(value);
            else if (flag == "--map") map = std::stoi(value);
            else if (flag == "--seed") firstSeed = static_cast<std::uint32_t>(std::stoul(value));
            else if (flag == "--seeds") seeds = std::stoi(value);
            else if (flag == "--scenario") scenario = value;
            else if (flag == "--difficulty") level = value;
            else throw std::runtime_error("unknown argument " + flag);
        }
        if (seconds <= 0 || seconds > 7200 || map < -1 || map > 2 || seeds < 1 || seeds > 100)
            throw std::runtime_error("invalid duration/map/seed count");
        if (scenario != "all" && scenario != "rush" && scenario != "macro" && scenario != "turtle" && scenario != "passive")
            throw std::runtime_error("invalid scenario");
        if (level != "all" && std::find(levels.begin(), levels.end(), level) == levels.end())
            throw std::runtime_error("invalid difficulty");
        for (std::size_t difficulty = 0; difficulty < levels.size(); ++difficulty) if (level == "all" || level == levels[difficulty])
            for (const std::string style : {"rush", "macro", "turtle", "passive"})
                if ((scenario == "all" && style != "passive") || scenario == style)
                    for (int layout = 0; layout < 3; ++layout) if (map < 0 || map == layout)
                        for (int seed = 0; seed < seeds; ++seed)
                            run(style, layout, firstSeed + seed, seconds, aiDifficultyAt(difficulty));
    } catch (const std::exception& error) {
        std::cerr << "AI challenge: " << error.what() << '\n'; return 1;
    }
}
