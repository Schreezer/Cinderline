#include "Sim/Simulation.h"
#include "MatchSnapshotMetrics.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <os/log.h>
#include <os/signpost.h>
#endif

using namespace cinder;

namespace {

constexpr int CadenceTicks = 20;
constexpr int PreparationLimitTicks = 72000;
constexpr int MarchTicks = 2400;
constexpr int CombatTicks = 2400;
constexpr float ArmyPassageGap = 72.0f;
constexpr std::uint64_t PreparationPatrolCommandLimit =
    static_cast<std::uint64_t>(Simulation::MaxPlayers) * 2 * PreparationLimitTicks;
const auto ProbeMonotonicOrigin = std::chrono::steady_clock::now();

#if defined(__APPLE__)
os_log_t latencySignpostLog() {
    static os_log_t log = os_log_create("com.cinderline.simulation", "MatchLatency");
    return log;
}
#endif

struct Target { Kind kind; int count; };
constexpr std::array<Target, 8> Roster{{
    {Kind::Worker, 20}, {Kind::Striker, 50}, {Kind::Lancer, 10}, {Kind::Scout, 10},
    {Kind::Bastion, 4}, {Kind::Mortar, 2}, {Kind::Mender, 2}, {Kind::Kite, 2}
}};

struct Distribution {
    double p50 = 0, p95 = 0, p99 = 0, maximum = 0;
};

struct ProfileSamples {
    std::vector<double> setup, production, movementEconomy, vision, combat, ai, completion, total;
};

struct StageMetrics {
    std::string name;
    int ticks = 0;
    std::vector<double> steps;
    ProfileSamples profile;
    baseline::SnapshotMetrics snapshots;
};

struct CommandMetrics {
    std::uint64_t accepted = 0;
    std::uint64_t rejected = 0;
    double wallTotalMs = 0;
    double wallMaxMs = 0;
    std::map<std::string, std::uint64_t> acceptedByType;
    std::map<std::string, std::vector<double>> wallByType;
};

struct TeamState {
    Vec2 home{};
    Vec2 rally{};
    Vec2 patrolA{};
    Vec2 patrolB{};
    Id expansionResource = 0;
    Vec2 expansionPoint{};
    std::vector<Vec2> placementCandidates;
    std::size_t placementCursor = 0;
    bool rallySet = false;
    bool initialGatherIssued = false;
    bool scoutSent = false;
    bool expansionGatherIssued = false;
    bool economyFunded = false;
};

struct ProgressSample {
    std::uint64_t tick = 0;
    int team = 0, ore = 0, gathered = 0, produced = 0, built = 0, tier = 0, supply = 0, capacity = 0;
    int mobile = 0;
};

struct AssignedMarchGoal { Id id = 0; Vec2 goal{}; std::uint64_t firstExhaustionTick = 0; };

struct MarchEvidence {
    int expected = 0, alive = 0, arrived = 0, pending = 0, exhausted = 0, missing = 0, goalChanged = 0;
};

struct RunResult {
    int players = 0;
    bool profileEnabled = false;
    bool snapshotsEnabled = false;
    bool prepared = false;
    bool valid = true;
    bool earlyEnd = false;
    int winner = -1;
    std::uint64_t preparationTicks = 0;
    std::uint64_t finalTick = 0;
    std::uint64_t stateHash = 0;
    std::uint64_t trajectoryHash = 1469598103934665603ULL;
    std::uint64_t recordingHash = 0;
    std::size_t recordedCommands = 0;
    std::uint64_t preparationPatrolCommands = 0;
NavigationStats navigation{};
    CommandMetrics commands;
    std::array<StageMetrics, 3> phases{{
        {"preparation", 0, {}, {}, {}}, {"march", 0, {}, {}, {}}, {"combat", 0, {}, {}, {}}
    }};
    std::array<std::array<int, 8>, Simulation::MaxPlayers> preparedUnitCounts{}, unitCounts{};
    std::array<int, Simulation::MaxPlayers> preparedSupply{}, preparedCapacity{};
    std::array<int, Simulation::MaxPlayers> supply{}, capacity{}, ore{}, gathered{}, produced{}, built{}, tier{};
    std::array<int, Simulation::MaxPlayers> combatLostDelta{}, combatKilledDelta{};
    std::array<double, Simulation::MaxPlayers> combatDamageDelta{};
    std::array<std::vector<double>, Simulation::MaxPlayers> schedulerMs;
    std::vector<double> movementSchedulerMs;
    std::array<MarchEvidence, Simulation::MaxPlayers> march;
    std::map<std::string, std::uint64_t> blockedStatuses;
    std::uint64_t droppedBlockedStatuses = 0;
    std::vector<ProgressSample> progress;
    std::vector<std::string> errors;
};

Navigation navigationFor(const Simulation& simulation) {
    std::vector<NavBox> boxes;
    for (const auto& obstacle : simulation.obstacles()) boxes.push_back({obstacle.center, obstacle.half});
    std::vector<NavCircle> circles;
    for (const auto& entity : simulation.entities()) if (entity.alive() &&
        (definition(entity.kind).building || (entity.kind == Kind::Resource && entity.resource > 0)))
        circles.push_back({entity.id, entity.pos, definition(entity.kind).radius});
    Navigation navigation; navigation.sync(simulation.worldSize(), boxes, circles);
    return navigation;
}

float distanceSquared(Vec2 a, Vec2 b) {
    const float x = a.x - b.x, y = a.y - b.y;
    return x * x + y * y;
}

void appendHash(std::uint64_t& hash, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= static_cast<unsigned char>(value & 255);
        hash *= 1099511628211ULL;
        value >>= 8;
    }
}

const char* kindName(Kind kind) { return definition(kind).name; }

const char* commandName(CommandType type) {
    switch (type) {
        case CommandType::Move: return "Move";
        case CommandType::AttackMove: return "AttackMove";
        case CommandType::Gather: return "Gather";
        case CommandType::AutoBuild: return "AutoBuild";
        case CommandType::AutoTrain: return "AutoTrain";
        case CommandType::AutoResearch: return "AutoResearch";
        case CommandType::AutoRally: return "AutoRally";
        default: return "Other";
    }
}

int countKind(const Simulation& simulation, int team, Kind kind, bool operationalOnly = false) {
    return static_cast<int>(std::count_if(simulation.entities().begin(), simulation.entities().end(),
        [&](const Entity& entity) {
            return entity.alive() && entity.team == team && entity.kind == kind &&
                   (!operationalOnly || entity.progress >= 1.0f);
        }));
}

int queuedKind(const Simulation& simulation, int team, Kind kind) {
    int count = 0;
    for (const auto& entity : simulation.entities()) if (entity.alive() && entity.team == team)
        for (const auto& item : entity.queue) if (!item.research && item.kind == kind) ++count;
    return count;
}

bool researchQueued(const Simulation& simulation, int team) {
    for (const auto& entity : simulation.entities()) if (entity.alive() && entity.team == team)
        for (const auto& item : entity.queue) if (item.research) return true;
    return false;
}

bool queuesEmpty(const Simulation& simulation, int team) {
    return std::none_of(simulation.entities().begin(), simulation.entities().end(), [&](const Entity& entity) {
        return entity.alive() && entity.team == team && !entity.queue.empty();
    });
}

std::vector<Id> ids(const Simulation& simulation, int team, Kind kind) {
    std::vector<Id> result;
    for (const auto& entity : simulation.entities())
        if (entity.alive() && entity.team == team && entity.kind == kind) result.push_back(entity.id);
    return result;
}

std::vector<Id> combatIds(const Simulation& simulation, int team) {
    std::vector<Id> result;
    for (const auto& entity : simulation.entities()) if (entity.alive() && entity.team == team &&
        !definition(entity.kind).building && entity.kind != Kind::Resource && entity.kind != Kind::Worker)
        result.push_back(entity.id);
    return result;
}

Id headquarters(const Simulation& simulation, int team) {
    for (const auto& entity : simulation.entities())
        if (entity.alive() && entity.team == team && entity.kind == Kind::Headquarters) return entity.id;
    return 0;
}

void recordBlocked(RunResult& result, const std::string& action, const std::string& message) {
    const std::string key = action + ": " + message;
    auto found = result.blockedStatuses.find(key);
    if (found != result.blockedStatuses.end()) { ++found->second; return; }
    if (result.blockedStatuses.size() < 64) result.blockedStatuses.emplace(key, 1);
    else ++result.droppedBlockedStatuses;
}

bool issue(Simulation& simulation, RunResult& result, Command command) {
    const auto started = std::chrono::steady_clock::now();
    const CommandResult response = simulation.command(command);
    const double elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    result.commands.wallTotalMs += elapsed;
    result.commands.wallMaxMs = std::max(result.commands.wallMaxMs, elapsed);
    result.commands.wallByType[commandName(command.type)].push_back(elapsed);
    if (response.accepted) {
        ++result.commands.accepted;
        ++result.commands.acceptedByType[commandName(command.type)];
        return true;
    }
    ++result.commands.rejected;
    result.valid = false;
    result.errors.push_back(std::string("issued ") + commandName(command.type) + " rejected: " + response.message);
    return false;
}

std::vector<Vec2> placementCandidates(Vec2 home, float worldSize) {
    std::vector<Vec2> result;
    constexpr std::array<float, 7> radii{{300, 440, 570, 680, 820, 960, 1100}};
    for (float radius : radii) for (int spoke = 0; spoke < 32; ++spoke) {
        const float angle = static_cast<float>(spoke) * 6.28318530718f / 32.0f;
        const Vec2 point{home.x + std::cos(angle) * radius, home.y + std::sin(angle) * radius};
        if (point.x >= 120 && point.y >= 120 && point.x <= worldSize - 120 && point.y <= worldSize - 120)
            result.push_back(point);
    }
    return result;
}

void initializeTeam(Simulation& simulation, int team, TeamState& state) {
    const Entity* anchor = simulation.find(headquarters(simulation, team));
    state.home = anchor ? anchor->pos : Vec2{};
    const float sx = state.home.x < simulation.worldSize() * 0.5f ? 1.0f : -1.0f;
    const float sy = state.home.y < simulation.worldSize() * 0.5f ? 1.0f : -1.0f;
    state.rally = {state.home.x + sx * 520, state.home.y + sy * 520};
    state.patrolA = {state.home.x + sx * 620, state.home.y + sy * 260};
    state.patrolB = {state.home.x + sx * 260, state.home.y + sy * 620};
    state.placementCandidates = placementCandidates(state.home, simulation.worldSize());
    float best = 1.0e30f;
    for (const auto& entity : simulation.entities()) if (entity.alive() && entity.kind == Kind::Resource) {
        const float distance = distanceSquared(entity.pos, state.home);
        if (distance > 800.0f * 800.0f && distance < best) {
            best = distance;
            state.expansionResource = entity.id;
            state.expansionPoint = entity.pos;
        }
    }
}

bool tryBuild(Simulation& simulation, RunResult& result, TeamState& state, int team, Kind kind) {
    const JobPlan general = simulation.autoBuildStatus(team, kind, nullptr);
    if (!general.accepted) {
        recordBlocked(result, std::string("build ") + kindName(kind), general.message);
        return false;
    }
    // AutoBuild deliberately validates worker access. This reference workload
    // needs passages for the entire army: the original dense radial layout
    // enclosed two radius-30 Mortars behind gaps of only 47-55 world units.
    // Reserve a 72-unit gap to every existing static footprint and map edge,
    // then still pay for and validate the building through the public command.
    const Navigation placementNavigation = navigationFor(simulation);
    while (state.placementCursor < state.placementCandidates.size()) {
        const Vec2 site = state.placementCandidates[state.placementCursor];
        if (!placementNavigation.pointClear(site, definition(kind).radius + ArmyPassageGap)) {
            ++state.placementCursor;
            recordBlocked(result, std::string("reference layout ") + kindName(kind),
                "reserve full-army passage between static footprints");
            continue;
        }
        const JobPlan status = simulation.autoBuildStatus(team, kind, &site);
        if (status.accepted) {
            ++state.placementCursor;
            Command command; command.type = CommandType::AutoBuild; command.team = team;
            command.kind = kind; command.point = site;
            return issue(simulation, result, command);
        }
        ++state.placementCursor;
        recordBlocked(result, std::string("place ") + kindName(kind), status.message);
    }
    result.valid = false;
    result.errors.push_back("team " + std::to_string(team) + " exhausted deterministic placement candidates");
    return false;
}

bool tryTrain(Simulation& simulation, RunResult& result, int team, Kind kind, int target) {
    const int remaining = target - countKind(simulation, team, kind) - queuedKind(simulation, team, kind);
    if (remaining <= 0) return false;
    std::string blocked;
    for (int quantity = std::min(remaining, Simulation::MaxQueue); quantity >= 1; --quantity) {
        const JobPlan status = simulation.autoTrainStatus(team, kind, quantity);
        if (!status.accepted) { if (quantity == 1) blocked = status.message; continue; }
        Command command; command.type = CommandType::AutoTrain; command.team = team;
        command.kind = kind; command.queueIndex = quantity;
        return issue(simulation, result, command);
    }
    recordBlocked(result, std::string("train ") + kindName(kind), blocked);
    return false;
}

void assignInitialGather(Simulation& simulation, RunResult& result, TeamState& state, int team) {
    if (state.initialGatherIssued) return;
    auto workers = ids(simulation, team, Kind::Worker);
    workers.erase(std::remove_if(workers.begin(), workers.end(), [&](Id id) {
        const Entity* worker = simulation.find(id);
        return !worker || worker->order == Order::Construct || worker->builderId != 0;
    }), workers.end());
    for (Id worker : workers) {
        const Entity* actor = simulation.find(worker);
        const Entity* closest = nullptr;
        float best = 1.0e30f;
        for (const auto& entity : simulation.entities()) if (entity.alive() && entity.kind == Kind::Resource &&
            entity.resource > 0 && simulation.explored(team, entity.pos)) {
            const float d = distanceSquared(actor->pos, entity.pos);
            if (d < best) { best = d; closest = &entity; }
        }
        if (!closest) { recordBlocked(result, "initial gather", "no explored ore"); return; }
        Command gather; gather.type = CommandType::Gather; gather.team = team;
        gather.units = {worker}; gather.target = closest->id;
        if (!issue(simulation, result, gather)) return;
    }
    state.initialGatherIssued = true;
}

void manageExpansion(Simulation& simulation, RunResult& result, TeamState& state, int team) {
    if (!state.scoutSent) {
        const auto scouts = ids(simulation, team, Kind::Scout);
        if (!scouts.empty()) {
            Command move; move.type = CommandType::Move; move.team = team;
            move.units = {scouts.front()}; move.point = state.expansionPoint;
            if (issue(simulation, result, move)) state.scoutSent = true;
        }
    }
    if (state.expansionGatherIssued || !state.expansionResource ||
        !simulation.explored(team, state.expansionPoint)) return;
    std::vector<const Entity*> deposits;
    for (const auto& entity : simulation.entities()) if (entity.alive() && entity.kind == Kind::Resource &&
        entity.resource > 0 && simulation.explored(team, entity.pos) &&
        distanceSquared(entity.pos, state.expansionPoint) < 500.0f * 500.0f) deposits.push_back(&entity);
    if (deposits.empty()) return;
    auto workers = ids(simulation, team, Kind::Worker);
    workers.erase(std::remove_if(workers.begin(), workers.end(), [&](Id id) {
        const Entity* worker = simulation.find(id);
        return !worker || worker->order == Order::Construct;
    }), workers.end());
    if (workers.size() < 10) return;
    std::sort(workers.begin(), workers.end(), std::greater<Id>());
    workers.resize(10);
    for (std::size_t deposit = 0; deposit < deposits.size(); ++deposit) {
        std::vector<Id> assigned;
        for (std::size_t index = deposit; index < workers.size(); index += deposits.size())
            assigned.push_back(workers[index]);
        if (assigned.empty()) continue;
        Command gather; gather.type = CommandType::Gather; gather.team = team;
        gather.units = std::move(assigned); gather.target = deposits[deposit]->id;
        if (!issue(simulation, result, gather)) return;
    }
    state.expansionGatherIssued = true;
}

int requiredGatheredPerTeam() {
    int spend = 0;
    for (const auto& target : Roster) spend += target.count * definition(target.kind).cost;
    spend -= 5 * definition(Kind::Worker).cost;
    spend += 11 * definition(Kind::Processor).cost + definition(Kind::Foundry).cost +
             definition(Kind::Laboratory).cost + definition(Kind::MotorPool).cost;
    spend += 500 + 1000;
    return spend - 500;
}

void keepUnitsMoving(Simulation& simulation, RunResult& result, const TeamState& state, int team,
                     bool includeWorkers) {
    std::array<std::vector<Id>, 2> groups;
    for (const auto& candidate : simulation.entities()) {
        if (!candidate.alive() || candidate.team != team || definition(candidate.kind).building ||
            candidate.kind == Kind::Resource || candidate.order == Order::Construct ||
            (candidate.kind == Kind::Worker && !includeWorkers)) continue;
        const Entity* entity = &candidate;
        const Id id = entity->id;
        const float switchDistance = definition(entity->kind).speed * Simulation::Step + 25.0f;
        if (entity->order == Order::Move &&
            distanceSquared(entity->pos, entity->goal) > switchDistance * switchDistance) continue;
        const std::size_t destination = distanceSquared(entity->pos, state.patrolA) >=
                                        distanceSquared(entity->pos, state.patrolB) ? 0 : 1;
        groups[destination].push_back(id);
    }
    for (std::size_t group = 0; group < groups.size(); ++group) if (!groups[group].empty()) {
        if (result.preparationPatrolCommands >= PreparationPatrolCommandLimit) {
            result.valid = false;
            result.errors.push_back("preparation patrol command limit exceeded");
            return;
        }
        Command move; move.type = CommandType::Move; move.team = team;
        move.units = std::move(groups[group]); move.point = group == 0 ? state.patrolA : state.patrolB;
        if (issue(simulation, result, std::move(move))) ++result.preparationPatrolCommands;
    }
}

bool teamPrepared(const Simulation& simulation, int team) {
    for (const auto& target : Roster) if (countKind(simulation, team, target.kind) != target.count) return false;
    return countKind(simulation, team, Kind::Processor, true) >= 11 &&
           countKind(simulation, team, Kind::Foundry, true) >= 1 &&
           countKind(simulation, team, Kind::Laboratory, true) >= 1 &&
           countKind(simulation, team, Kind::MotorPool, true) >= 1 &&
           simulation.players()[team].tier == 3 && queuesEmpty(simulation, team) &&
           simulation.supply(team) == 184 && simulation.capacity(team) >= 184 && simulation.capacity(team) <= 200;
}

void scheduleTeam(Simulation& simulation, RunResult& result, TeamState& state, int team) {
    if (!state.rallySet) {
        const CommandResult status = simulation.autoRallyStatus(team, Kind::Resource, 0, false);
        if (status.accepted) {
            Command rally; rally.type = CommandType::AutoRally; rally.team = team;
            rally.kind = Kind::Resource; rally.point = state.rally;
            state.rallySet = issue(simulation, result, rally);
        } else recordBlocked(result, "set army rally", status.message);
    }
    assignInitialGather(simulation, result, state, team);
    manageExpansion(simulation, result, state, team);

    Kind wanted = Kind::Resource;
    if (countKind(simulation, team, Kind::Foundry) < 1) wanted = Kind::Foundry;
    else if (countKind(simulation, team, Kind::Foundry, true) && countKind(simulation, team, Kind::Laboratory) < 1)
        wanted = Kind::Laboratory;
    else if (simulation.players()[team].tier >= 2 && countKind(simulation, team, Kind::MotorPool) < 1)
        wanted = Kind::MotorPool;
    else if (countKind(simulation, team, Kind::Processor) < 11) wanted = Kind::Processor;
    if (wanted != Kind::Resource) tryBuild(simulation, result, state, team, wanted);

    if (countKind(simulation, team, Kind::Laboratory, true) && simulation.players()[team].tier < 3 &&
        !researchQueued(simulation, team)) {
        const JobPlan status = simulation.autoResearchStatus(team, 0);
        if (status.accepted) {
            Command research; research.type = CommandType::AutoResearch;
            research.team = team; research.queueIndex = 0;
            issue(simulation, result, research);
        } else recordBlocked(result, "research tier", status.message);
    }

    tryTrain(simulation, result, team, Kind::Worker, 20);
    tryTrain(simulation, result, team, Kind::Scout, simulation.players()[team].tier >= 3 ? 10 : 1);
    if (simulation.players()[team].tier >= 3 && countKind(simulation, team, Kind::MotorPool, true)) {
        tryTrain(simulation, result, team, Kind::Striker, 50);
        tryTrain(simulation, result, team, Kind::Lancer, 10);
        tryTrain(simulation, result, team, Kind::Scout, 10);
        tryTrain(simulation, result, team, Kind::Bastion, 4);
        tryTrain(simulation, result, team, Kind::Mortar, 2);
        tryTrain(simulation, result, team, Kind::Mender, 2);
        tryTrain(simulation, result, team, Kind::Kite, 2);
    }
}

void sampleProgress(const Simulation& simulation, RunResult& result) {
    for (int team = 0; team < result.players; ++team) {
        const auto& player = simulation.players()[team];
        int mobile = 0;
        for (const auto& entity : simulation.entities()) if (entity.alive() && entity.team == team &&
            !definition(entity.kind).building && entity.kind != Kind::Resource) ++mobile;
        result.progress.push_back({simulation.tick(), team, player.ore, player.stats.gathered,
                                   player.stats.produced, player.stats.built, player.tier,
                                   simulation.supply(team), simulation.capacity(team), mobile});
    }
}

bool noCombatBeforeCombatStage(const Simulation& simulation, RunResult& result, const char* stage) {
    for (int team = 0; team < result.players; ++team) {
        const auto& stats = simulation.players()[team].stats;
        if (stats.damage <= 0 && stats.lost == 0 && stats.killed == 0) continue;
        std::ostringstream error;
        error << "combat occurred during " << stage << " at tick " << simulation.tick()
              << " for team " << team << " (damage=" << stats.damage
              << ", lost=" << stats.lost << ", killed=" << stats.killed << ')';
        for (const auto& effect : simulation.effects()) if (effect.team == team &&
            (effect.type == EffectType::Impact || effect.type == EffectType::Death)) {
            error << " effect=" << (effect.type == EffectType::Death ? "death" : "impact")
                  << " source=" << kindName(effect.sourceKind) << " target=" << kindName(effect.targetKind)
                  << " at=" << effect.to.x << ',' << effect.to.y;
            break;
        }
        int detailed = 0;
        for (const auto& entity : simulation.entities()) if (entity.alive() && entity.team == team &&
            entity.hp < definition(entity.kind).hp && detailed++ < 4) {
            error << " damaged=" << entity.id << ':' << kindName(entity.kind)
                  << '@' << entity.pos.x << ',' << entity.pos.y
                  << " hp=" << entity.hp << '/' << definition(entity.kind).hp;
            int nearby = 0;
            for (const auto& other : simulation.entities()) if (other.alive() && other.team != team &&
                other.team >= 0 && !definition(other.kind).building &&
                distanceSquared(other.pos, entity.pos) < 900.0f * 900.0f && nearby++ < 4)
                error << " nearby_enemy=" << other.id << ':' << kindName(other.kind)
                      << '@' << other.pos.x << ',' << other.pos.y;
        }
        result.valid = false;
        result.errors.push_back(error.str());
        return false;
    }
    return true;
}

void stepStage(Simulation& simulation, RunResult& result, StageMetrics& stage,
               std::array<net::ViewMemory, Simulation::MaxPlayers>& views) {
    const auto before = simulation.tick();
#if defined(__APPLE__)
    const os_log_t signpostLog = latencySignpostLog();
    os_signpost_id_t signpostId = OS_SIGNPOST_ID_INVALID;
    if (os_signpost_enabled(signpostLog)) {
        signpostId = os_signpost_id_generate(signpostLog);
        if (signpostId != OS_SIGNPOST_ID_INVALID) {
            os_signpost_interval_begin(signpostLog, signpostId, "simulation.update",
                "tick=%{public}llu phase=%{public}s",
                static_cast<unsigned long long>(before + 1), stage.name.c_str());
        }
    }
#endif
    const auto updateStarted = std::chrono::steady_clock::now();
    simulation.update(Simulation::Step);
    const auto updateFinished = std::chrono::steady_clock::now();
#if defined(__APPLE__)
    if (signpostId != OS_SIGNPOST_ID_INVALID) {
        os_signpost_interval_end(signpostLog, signpostId, "simulation.update",
            "tick=%{public}llu phase=%{public}s",
            static_cast<unsigned long long>(before + 1), stage.name.c_str());
    }
#endif
    const double updateWallMs = std::chrono::duration<double, std::milli>(
        updateFinished - updateStarted).count();
    if (updateWallMs >= 20.0) {
        const auto& profile = simulation.lastStepProfile();
        const double monotonicMs = std::chrono::duration<double, std::milli>(
            updateStarted - ProbeMonotonicOrigin).count();
        const double wallEpochMs = std::chrono::duration<double, std::milli>(
            std::chrono::system_clock::now().time_since_epoch()).count() - updateWallMs;
        std::cerr << std::fixed << std::setprecision(3)
                  << "match-latency slow_step tick=" << simulation.tick()
                  << " phase=" << stage.name
                  << " update_wall_ms=" << updateWallMs
                  << " monotonic_ms=" << monotonicMs
                  << " wall_epoch_ms=" << wallEpochMs
                  << " profile_collected=" << profile.collected
                  << " profile_tick=" << profile.tick
                  << " setup_ms=" << profile.setupMs
                  << " production_ms=" << profile.productionMs
                  << " movement_economy_ms=" << profile.movementEconomyMs
                  << " vision_ms=" << profile.visionMs
                  << " combat_ms=" << profile.combatMs
                  << " ai_ms=" << profile.aiMs
                  << " completion_ms=" << profile.completionMs
                  << " profile_total_ms=" << profile.totalMs << '\n';
    }
    if (simulation.tick() != before + 1) {
        result.valid = false;
        result.errors.push_back("fixed-step update did not advance exactly one tick");
        return;
    }
    stage.steps.push_back(simulation.lastStepMilliseconds());
    if (result.profileEnabled) {
        if (!simulation.profilingEnabled()) {
            result.valid = false;
            result.errors.push_back("profiling became disabled during a requested profiled run");
        }
        const auto& profile = simulation.lastStepProfile();
        const std::array<double, 8> samples{{profile.setupMs, profile.productionMs,
            profile.movementEconomyMs, profile.visionMs, profile.combatMs, profile.aiMs,
            profile.completionMs, profile.totalMs}};
        const bool validSamples = std::all_of(samples.begin(), samples.end(), [](double value) {
            return std::isfinite(value) && value >= 0;
        });
        if (!profile.collected || profile.tick != simulation.tick() || !validSamples) {
            result.valid = false;
            result.errors.push_back("missing, mismatched, or invalid step profile sample");
        } else {
            stage.profile.setup.push_back(profile.setupMs);
            stage.profile.production.push_back(profile.productionMs);
            stage.profile.movementEconomy.push_back(profile.movementEconomyMs);
            stage.profile.vision.push_back(profile.visionMs);
            stage.profile.combat.push_back(profile.combatMs);
            stage.profile.ai.push_back(profile.aiMs);
            stage.profile.completion.push_back(profile.completionMs);
            stage.profile.total.push_back(profile.totalMs);
        }
    }
    if (result.snapshotsEnabled && simulation.tick() % 2 == 0) {
        try { stage.snapshots.sample(simulation, views); }
        catch (const std::exception& error) {
            result.valid = false;
            result.errors.push_back(std::string("snapshot sampling failed: ") + error.what());
        }
    }
    appendHash(result.trajectoryHash, simulation.stateHash());
    ++stage.ticks;
}

std::uint64_t hashRecording(const std::vector<RecordedCommand>& recording) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto add = [&](std::uint64_t value) { appendHash(hash, value); };
    auto addFloat = [&](float value) { std::uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits)); add(bits); };
    for (const auto& entry : recording) {
        add(entry.tick); add(static_cast<int>(entry.command.type)); add(entry.command.team);
        addFloat(entry.command.point.x); addFloat(entry.command.point.y);
        add(entry.command.target); add(static_cast<int>(entry.command.kind)); add(entry.command.queueIndex);
        add(entry.command.units.size());
        for (Id id : entry.command.units) add(id);
    }
    return hash;
}

RunResult run(int players, bool profile, bool snapshots, const std::string& diagnosticSavePrefix = {}) {
    RunResult result; result.players = players; result.profileEnabled = profile; result.snapshotsEnabled = snapshots;
    Simulation simulation;
    Config config{0, static_cast<std::uint32_t>(0xB450u + players), false, 1.0f, MatchLength::Standard};
    config.playerCount = players;
    simulation.reset(config);
    simulation.setProfilingEnabled(profile);
    std::array<net::ViewMemory, Simulation::MaxPlayers> snapshotViews;
    std::array<TeamState, Simulation::MaxPlayers> teams;
    for (int team = 0; team < players; ++team) initializeTeam(simulation, team, teams[team]);

    for (int tick = 0; tick < PreparationLimitTicks && simulation.winner() == -1; ++tick) {
        if (tick % CadenceTicks == 0) {
            for (int team = 0; team < players; ++team) {
                const auto started = std::chrono::steady_clock::now();
                scheduleTeam(simulation, result, teams[team], team);
                result.schedulerMs[team].push_back(std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count());
            }
            if (tick % 6000 == 0) {
                sampleProgress(simulation, result);
                std::cerr << "match-baseline preparation tick " << simulation.tick();
                for (int team = 0; team < players; ++team)
                    std::cerr << " team" << team << "="
                              << countKind(simulation, team, Kind::Worker) +
                                 static_cast<int>(combatIds(simulation, team).size())
                              << "/100 supply=" << simulation.supply(team)
                              << "/" << simulation.capacity(team);
                std::cerr << '\n';
            }
            bool complete = true;
            for (int team = 0; team < players; ++team) complete = complete && teamPrepared(simulation, team);
            if (complete) {
                result.prepared = true;
                for (int team = 0; team < players; ++team) {
                    for (std::size_t index = 0; index < Roster.size(); ++index)
                        result.preparedUnitCounts[team][index] = countKind(simulation, team, Roster[index].kind);
                    result.preparedSupply[team] = simulation.supply(team);
                    result.preparedCapacity[team] = simulation.capacity(team);
                }
                break;
            }
        }
        const auto movementSchedulerStarted = std::chrono::steady_clock::now();
        for (int team = 0; team < players && result.valid; ++team) {
            teams[team].economyFunded = teams[team].economyFunded ||
                                        simulation.players()[team].stats.gathered >= requiredGatheredPerTeam();
            keepUnitsMoving(simulation, result, teams[team], team, teams[team].economyFunded);
        }
        result.movementSchedulerMs.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - movementSchedulerStarted).count());
        if (!result.valid) break;
        stepStage(simulation, result, result.phases[0], snapshotViews);
        if (result.valid) noCombatBeforeCombatStage(simulation, result, "preparation");
        if (!result.valid) break;
    }
    result.preparationTicks = simulation.tick();
    sampleProgress(simulation, result);
    if (!result.prepared) {
        if (result.valid) {
            result.valid = false;
            result.errors.push_back(simulation.winner() == -1 ? "preparation exceeded 3600 simulated seconds" :
                                                            "match ended during preparation");
        }
    }

    if (result.prepared) {
        const std::array<Vec2, 4> marchGoals{{{1550, 1550}, {3250, 3250}, {3250, 1550}, {1550, 3250}}};
        std::array<std::vector<AssignedMarchGoal>, Simulation::MaxPlayers> assigned;
        if (!diagnosticSavePrefix.empty() && !simulation.save(diagnosticSavePrefix + "-march-start.save")) {
            result.valid = false;
            result.errors.push_back("failed to save diagnostic march-start state");
        }
        for (int team = 0; team < players; ++team) {
            Command move; move.type = CommandType::Move; move.team = team;
            move.units = combatIds(simulation, team); move.point = marchGoals[static_cast<std::size_t>(team)];
            if (issue(simulation, result, move)) for (Id id : move.units) {
                const Entity* entity = simulation.find(id);
                if (entity) assigned[team].push_back({id, entity->goal});
            }
        }
        for (int tick = 0; tick < MarchTicks && simulation.winner() == -1 && result.valid; ++tick)
        {
            stepStage(simulation, result, result.phases[1], snapshotViews);
            if (result.valid) noCombatBeforeCombatStage(simulation, result, "march");
            for (int team = 0; team < players; ++team) for (auto& target : assigned[team]) {
                const Entity* entity = simulation.find(target.id);
                if (entity && entity->navigationExhausted && !target.firstExhaustionTick)
                    target.firstExhaustionTick = simulation.tick();
            }
        }
        if (simulation.winner() != -1) {
            result.valid = false;
            result.earlyEnd = true;
            result.errors.push_back("match ended during non-combat march");
        }
        Simulation reloaded;
        bool reloadValid = false;
        if (!diagnosticSavePrefix.empty()) {
            const std::string endPath = diagnosticSavePrefix + "-march-end.save";
            if (!simulation.save(endPath)) {
                result.valid = false;
                result.errors.push_back("failed to save diagnostic march-end state");
            } else reloadValid = reloaded.load(endPath) && reloaded.stateHash() == simulation.stateHash();
        }
        Navigation finalNavigation = navigationFor(simulation);
        Navigation reloadedNavigation;
        if (reloadValid) reloadedNavigation = navigationFor(reloaded);
        for (int team = 0; team < players; ++team) {
            int mobile = 0;
            for (const auto& entity : simulation.entities()) if (entity.alive() && entity.team == team &&
                !definition(entity.kind).building && entity.kind != Kind::Resource) ++mobile;
            if (mobile != 100 || simulation.players()[team].stats.lost != 0 || simulation.players()[team].stats.killed != 0) {
                result.valid = false;
                result.errors.push_back("team " + std::to_string(team) + " lost units before combat");
            }
            auto& evidence = result.march[team]; evidence.expected = static_cast<int>(assigned[team].size());
            int laggardDetails = 0;
            for (const auto& target : assigned[team]) {
                const Entity* entity = simulation.find(target.id);
                if (!entity || !entity->alive()) { ++evidence.missing; continue; }
                ++evidence.alive;
                if (distanceSquared(entity->goal, target.goal) > 0.001f) ++evidence.goalChanged;
                if (distanceSquared(entity->pos, target.goal) <= 35.0f * 35.0f) ++evidence.arrived;
                else if (entity->navigationExhausted) ++evidence.exhausted;
                else if (entity->order == Order::Move || entity->order == Order::Idle) ++evidence.pending;
                else ++evidence.goalChanged;
                if (distanceSquared(entity->pos, target.goal) > 35.0f * 35.0f && laggardDetails++ < 8) {
                    std::ostringstream detail;
                    detail << "team " << team << " march laggard id=" << entity->id
                           << " kind=" << kindName(entity->kind) << " pos=" << entity->pos.x << ',' << entity->pos.y
                           << " radius=" << definition(entity->kind).radius
                           << " assigned_goal=" << target.goal.x << ',' << target.goal.y
                           << " current_goal=" << entity->goal.x << ',' << entity->goal.y
                           << " order=" << static_cast<int>(entity->order)
                           << " navigation_exhausted=" << entity->navigationExhausted
                           << " first_exhaustion_tick=" << target.firstExhaustionTick
                           << " path_remaining=" << (entity->path.size() > static_cast<std::size_t>(entity->pathIndex)
                                ? entity->path.size() - static_cast<std::size_t>(entity->pathIndex) : 0)
                           << " path_geometry=" << entity->pathGeometry
                           << " repath=" << entity->repath
                           << " navigation_failures=" << entity->navigationFailures
                           << " yield_for=" << entity->yieldFor
                           << " work_target=" << entity->workTarget
                           << " work_point_valid=" << entity->workPointValid
                           << " static_geometry_version=" << finalNavigation.geometryVersion();
                    if (!definition(entity->kind).air) {
                        const auto fresh = finalNavigation.route(entity->pos, {target.goal},
                                                                 definition(entity->kind).radius, entity->id);
                        const bool direct = finalNavigation.segmentClear(entity->pos, target.goal,
                                                                         definition(entity->kind).radius, entity->id);
                        detail << " fresh_static_route_reached=" << fresh.reached
                               << " fresh_static_route_exhausted=" << fresh.exhausted
                               << " fresh_static_route_points=" << fresh.points.size()
                               << " fresh_static_route_cost=" << fresh.cost
                               << " fresh_static_route_expanded=" << fresh.expanded
                               << " direct_segment_clear=" << direct;
                        if (!fresh.points.empty())
                            detail << " fresh_static_route_endpoint=" << fresh.points.back().x << ','
                                   << fresh.points.back().y;
                        detail << " reload_valid=" << reloadValid;
                        if (reloadValid) {
                            const Entity* loadedEntity = reloaded.find(entity->id);
                            const auto loadedRoute = loadedEntity
                                ? reloadedNavigation.route(loadedEntity->pos, {target.goal},
                                    definition(loadedEntity->kind).radius, loadedEntity->id)
                                : NavigationResult{};
                            detail << " reload_route_reached=" << loadedRoute.reached
                                   << " reload_route_exhausted=" << loadedRoute.exhausted
                                   << " reload_route_cost=" << loadedRoute.cost
                                   << " reload_route_expanded=" << loadedRoute.expanded;
                        }
                    }
                    result.errors.push_back(detail.str());
                }
            }
            if (evidence.alive != evidence.expected || evidence.arrived != evidence.expected || evidence.pending ||
                evidence.exhausted || evidence.goalChanged || evidence.missing) {
                result.valid = false;
                result.errors.push_back("team " + std::to_string(team) + " did not complete every march order");
            }
        }
    }

    if (result.prepared && result.valid) {
        std::array<int, Simulation::MaxPlayers> lostBefore{}, killedBefore{};
        std::array<float, Simulation::MaxPlayers> damageBefore{};
        for (int team = 0; team < players; ++team) {
            lostBefore[team] = simulation.players()[team].stats.lost;
            killedBefore[team] = simulation.players()[team].stats.killed;
            damageBefore[team] = simulation.players()[team].stats.damage;
        }
        for (int team = 0; team < players; ++team) {
            Command attack; attack.type = CommandType::AttackMove; attack.team = team;
            attack.units = combatIds(simulation, team); attack.point = {2400, 2400};
            issue(simulation, result, attack);
        }
        for (int tick = 0; tick < CombatTicks && simulation.winner() == -1 && result.valid; ++tick)
            stepStage(simulation, result, result.phases[2], snapshotViews);
        result.earlyEnd = simulation.winner() != -1;
        int totalLost = 0;
        double totalDamage = 0;
        for (int team = 0; team < players; ++team) {
            const auto& stats = simulation.players()[team].stats;
            result.combatLostDelta[team] = stats.lost - lostBefore[team];
            result.combatKilledDelta[team] = stats.killed - killedBefore[team];
            result.combatDamageDelta[team] = static_cast<double>(stats.damage - damageBefore[team]);
            totalLost += result.combatLostDelta[team];
            totalDamage += result.combatDamageDelta[team];
        }
        if (totalDamage <= 0 || totalLost <= 0) {
            result.valid = false;
            result.errors.push_back("combat stage produced no persistent damage or unit loss");
        }
    }

    result.winner = simulation.winner(); result.finalTick = simulation.tick();
    result.stateHash = simulation.stateHash(); result.recordedCommands = simulation.recording().size();
    result.recordingHash = hashRecording(simulation.recording()); result.navigation = simulation.navigationStats();
    for (int team = 0; team < players; ++team) {
        for (std::size_t index = 0; index < Roster.size(); ++index)
            result.unitCounts[team][index] = countKind(simulation, team, Roster[index].kind);
        const auto& player = simulation.players()[team];
        result.supply[team] = simulation.supply(team); result.capacity[team] = simulation.capacity(team);
        result.ore[team] = player.ore; result.gathered[team] = player.stats.gathered;
        result.produced[team] = player.stats.produced; result.built[team] = player.stats.built;
        result.tier[team] = player.tier;
    }
    if (result.commands.rejected) { result.valid = false; result.errors.push_back("one or more issued commands were rejected"); }
    return result;
}

Distribution distribution(std::vector<double> values) {
    Distribution result;
    if (values.empty()) return result;
    std::sort(values.begin(), values.end());
    auto at = [&](double quantile) {
        const std::size_t rank = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(quantile * values.size())));
        return values[std::min(values.size() - 1, rank - 1)];
    };
    result.p50 = at(0.50); result.p95 = at(0.95); result.p99 = at(0.99); result.maximum = values.back();
    return result;
}

std::string hex(std::uint64_t value) {
    std::ostringstream output; output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value; return output.str();
}

std::string escaped(const std::string& text) {
    std::string result;
    for (char character : text) {
        if (character == '\"' || character == '\\') result.push_back('\\');
        if (character == '\n') result += "\\n"; else result.push_back(character);
    }
    return result;
}

void writeDistribution(const std::vector<double>& samples) {
    if (samples.empty()) { std::cout << "null"; return; }
    const auto stats = distribution(samples);
    std::cout << "{\"p50\": " << stats.p50 << ", \"p95\": " << stats.p95
              << ", \"p99\": " << stats.p99 << ", \"max\": " << stats.maximum << "}";
}

void print(const RunResult& result) {
    const bool measuredValid = result.valid && result.prepared;
    std::cout << "{\n  \"schema\": \"cinderline.match_latency_probe.v1\",\n"
              << "  \"success\": false,\n"
              << "  \"diagnostic_only\": true,\n"
              << "  \"full_acceptance\": false,\n"
              << "  \"repeat_run_performed\": false,\n"
              << "  \"completed\": true,\n"
              << "  \"measured_run_valid\": " << (measuredValid ? "true" : "false") << ",\n"
              << "  \"players\": " << result.players << ",\n"
              << "  \"reference_mobile_units_per_team\": 100,\n"
              << "  \"reference_crew_per_team\": 184,\n"
              << "  \"supported_device_capacity_claim\": false,\n"
              << "  \"economy_bypass\": false,\n"
              << "  \"debug_spawn_used\": false,\n"
              << "  \"script_uses_authored_map_knowledge\": true,\n"
              << "  \"gather_requires_explored_resource\": true,\n"
              << "  \"preparation_combat_suppression\": \"accepted Move orders between two own-corner waypoints\",\n"
              << "  \"required_gathered_ore_per_team\": " << requiredGatheredPerTeam() << ",\n"
              << "  \"reference_layout_minimum_static_gap_world_units\": " << ArmyPassageGap << ",\n"
              << "  \"workers_join_holding_pattern_after_required_ore_gathered\": true,\n"
              << "  \"preparation_patrol_commands\": " << result.preparationPatrolCommands << ",\n"
              << "  \"preparation_patrol_command_limit\": " << PreparationPatrolCommandLimit << ",\n"
              << "  \"preparation_movement_scheduler_ms\": ";
    writeDistribution(result.movementSchedulerMs);
    std::cout << ",\n"
              << "  \"preparation_limit_seconds\": 3600,\n"
              << "  \"prepared\": " << (result.prepared ? "true" : "false") << ",\n"
              << "  \"preparation_ticks\": " << result.preparationTicks << ",\n"
              << "  \"final_tick\": " << result.finalTick << ",\n"
              << "  \"winner\": " << result.winner << ",\n"
              << "  \"early_end\": " << (result.earlyEnd ? "true" : "false") << ",\n"
              << "  \"profile_enabled\": " << (result.profileEnabled ? "true" : "false") << ",\n"
              << "  \"snapshots_enabled\": " << (result.snapshotsEnabled ? "true" : "false") << ",\n"
              << "  \"state_hash\": \"" << hex(result.stateHash) << "\",\n"
              << "  \"repeat_state_hash\": null,\n"
              << "  \"trajectory_hash\": \"" << hex(result.trajectoryHash) << "\",\n"
              << "  \"repeat_trajectory_hash\": null,\n"
              << "  \"recording_hash\": \"" << hex(result.recordingHash) << "\",\n"
              << "  \"repeat_recording_hash\": null,\n"
              << "  \"recorded_commands\": " << result.recordedCommands << ",\n"
              << "  \"deterministic_repeat\": null,\n"
              << "  \"commands\": {\"accepted\": " << result.commands.accepted
              << ", \"rejected\": " << result.commands.rejected
              << ", \"wall_total_ms\": " << result.commands.wallTotalMs
              << ", \"wall_max_ms\": " << result.commands.wallMaxMs << ", \"accepted_by_type\": {";
    std::size_t entry = 0;
    for (const auto& item : result.commands.acceptedByType) {
        if (entry++) std::cout << ", ";
        std::cout << "\"" << item.first << "\": " << item.second;
    }
    std::cout << "}, \"wall_ms_by_type\": {";
    entry = 0;
    for (const auto& item : result.commands.wallByType) {
        if (entry++) std::cout << ", ";
        std::cout << "\"" << item.first << "\": ";
        writeDistribution(item.second);
    }
    std::cout << "}},\n  \"phases\": [\n";
    for (std::size_t index = 0; index < result.phases.size(); ++index) {
        const auto& phase = result.phases[index];
        std::cout << "    {\"name\": \"" << phase.name << "\", \"ticks\": " << phase.ticks
                  << ", \"step_ms\": ";
        writeDistribution(phase.steps);
        std::cout << ", \"profile_ms\": ";
        if (!result.profileEnabled || phase.profile.total.empty()) std::cout << "null";
        else {
            std::cout << "{\"setup\": "; writeDistribution(phase.profile.setup);
            std::cout << ", \"production\": "; writeDistribution(phase.profile.production);
            std::cout << ", \"movement_economy\": "; writeDistribution(phase.profile.movementEconomy);
            std::cout << ", \"vision\": "; writeDistribution(phase.profile.vision);
            std::cout << ", \"combat\": "; writeDistribution(phase.profile.combat);
            std::cout << ", \"ai\": "; writeDistribution(phase.profile.ai);
            std::cout << ", \"completion\": "; writeDistribution(phase.profile.completion);
            std::cout << ", \"total\": "; writeDistribution(phase.profile.total);
            std::cout << "}";
        }
        std::cout << ", \"snapshots\": ";
        if (result.snapshotsEnabled) phase.snapshots.writeJson(std::cout); else std::cout << "null";
        std::cout << "}"
                  << (index + 1 == result.phases.size() ? "\n" : ",\n");
    }
    std::cout << "  ],\n  \"navigation\": {\"searches\": " << result.navigation.searches
              << ", \"expanded\": " << result.navigation.expanded << ", \"failures\": " << result.navigation.failures
              << ", \"budget_deferrals\": " << result.navigation.budgetDeferrals << "},\n  \"teams\": [\n";
    for (int team = 0; team < result.players; ++team) {
        std::cout << "    {\"team\": " << team << ", \"supply\": " << result.supply[team]
                  << ", \"capacity\": " << result.capacity[team] << ", \"ore\": " << result.ore[team]
                  << ", \"gathered\": " << result.gathered[team] << ", \"produced\": " << result.produced[team]
                  << ", \"built\": " << result.built[team] << ", \"tier\": " << result.tier[team]
                  << ", \"scheduler_ms\": ";
        writeDistribution(result.schedulerMs[team]);
        const auto& march = result.march[team];
        std::cout << ", \"march\": {\"expected\": " << march.expected << ", \"alive\": " << march.alive
                  << ", \"arrived\": " << march.arrived << ", \"pending_valid_order\": " << march.pending
                  << ", \"navigation_exhausted\": " << march.exhausted << ", \"missing\": " << march.missing
                  << ", \"goal_changed\": " << march.goalChanged << "}"
                  << ", \"prepared_supply\": " << result.preparedSupply[team]
                  << ", \"prepared_capacity\": " << result.preparedCapacity[team]
                  << ", \"combat_delta\": {\"damage\": " << result.combatDamageDelta[team]
                  << ", \"lost\": " << result.combatLostDelta[team]
                  << ", \"killed\": " << result.combatKilledDelta[team] << "}"
                  << ", \"prepared_units\": {";
        for (std::size_t index = 0; index < Roster.size(); ++index) {
            if (index) std::cout << ", ";
            std::cout << "\"" << kindName(Roster[index].kind) << "\": "
                      << result.preparedUnitCounts[team][index];
        }
        std::cout << "}, \"final_units\": {";
        for (std::size_t index = 0; index < Roster.size(); ++index) {
            if (index) std::cout << ", ";
            std::cout << "\"" << kindName(Roster[index].kind) << "\": " << result.unitCounts[team][index];
        }
        std::cout << "}}" << (team + 1 == result.players ? "\n" : ",\n");
    }
    std::cout << "  ],\n  \"blocked_statuses\": {";
    entry = 0;
    for (const auto& item : result.blockedStatuses) {
        if (entry++) std::cout << ", ";
        std::cout << "\"" << escaped(item.first) << "\": " << item.second;
    }
    std::cout << "},\n  \"dropped_blocked_statuses\": " << result.droppedBlockedStatuses
              << ",\n  \"progress\": [\n";
    for (std::size_t index = 0; index < result.progress.size(); ++index) {
        const auto& sample = result.progress[index];
        std::cout << "    {\"tick\": " << sample.tick << ", \"team\": " << sample.team
                  << ", \"ore\": " << sample.ore << ", \"gathered\": " << sample.gathered
                  << ", \"produced\": " << sample.produced << ", \"built\": " << sample.built
                  << ", \"tier\": " << sample.tier << ", \"supply\": " << sample.supply
                  << ", \"capacity\": " << sample.capacity << ", \"mobile\": " << sample.mobile << "}"
                  << (index + 1 == result.progress.size() ? "\n" : ",\n");
    }
    std::cout << "  ],\n  \"errors\": [";
    for (std::size_t index = 0; index < result.errors.size(); ++index) {
        if (index) std::cout << ", ";
        std::cout << "\"" << escaped(result.errors[index]) << "\"";
    }
    if (!result.errors.empty()) std::cout << ", ";
    std::cout << "\"diagnostic single measured run; deterministic repeat and full acceptance not performed\"";
    std::cout << "]\n}\n";
}

} // namespace

int main(int argc, char** argv) {
    int players = 0; bool profile = true, snapshots = false; std::string diagnosticSavePrefix;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--players2" || argument == "--players4") {
            if (players) { std::cerr << "Choose exactly one roster argument.\n"; return 2; }
            players = argument == "--players2" ? 2 : 4;
        }
        else if (argument == "--profile") profile = true;
        else if (argument == "--snapshots") snapshots = true;
        else if (argument == "--diagnostic-save-prefix" && index + 1 < argc)
            diagnosticSavePrefix = argv[++index];
        else { std::cerr << "Unknown argument: " << argument << '\n'; return 2; }
    }
    if (!players) { std::cerr << "Choose exactly one of --players2 or --players4.\n"; return 2; }
    std::cout << std::fixed << std::setprecision(6);
    RunResult measured = run(players, profile, snapshots, diagnosticSavePrefix);
    print(measured);
    return measured.valid && measured.prepared ? 0 : 1;
}
