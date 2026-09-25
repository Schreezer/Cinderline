#include "Sim/Simulation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace cinder;

namespace {

constexpr int WarmupTicks = 20;
constexpr float ArrivalTolerance = 35.0f;

struct Route {
    std::string name;
    int team = 0;
    std::vector<Id> units;
    Vec2 commandGoal{};
};

struct Workload {
    std::string name;
    std::string description;
    int measuredTicks = 0;
    Simulation simulation;
    std::vector<Route> routes;
};

struct CommandMetric {
    std::string route;
    int team = 0;
    std::size_t unitCount = 0;
    Vec2 goal{};
    bool accepted = false;
    std::uint64_t issuedTick = 0;
    std::uint64_t acceptedTick = 0;
    std::uint64_t acceptanceTickDelta = 0;
    double wallMilliseconds = 0;
    std::string message;
};

struct AssignedGoal {
    Id id = 0;
    Kind kind = Kind::Worker;
    Vec2 goal{};
};

struct Laggard {
    Id id = 0;
    Kind kind = Kind::Worker;
    Vec2 position{};
    Vec2 assignedGoal{};
    Vec2 currentGoal{};
    Order order = Order::Idle;
    float remaining = 0;
    float yieldFor = 0;
    float stalledFor = 0;
    int pathIndex = 0;
    std::size_t pathSize = 0;
    bool goalChanged = false;
};

struct StaticCircle {
    Id id = 0;
    Vec2 position{};
    float radius = 0;
};

struct GeometryFailure {
    std::uint64_t tick = 0;
    Id id = 0;
    std::string check;
};

struct RunResult {
    std::string name;
    std::string description;
    int warmupTicks = WarmupTicks;
    int measuredTicks = 0;
    float stepSeconds = Simulation::Step;
    float arrivalTolerance = ArrivalTolerance;
    int expected = 0;
    int arrived = 0;
    int alive = 0;
    int failed = 0;
    int stillMoving = 0;
    int exhausted = 0;
    int missing = 0;
    int dead = 0;
    int unexplainedOrderLoss = 0;
    int winner = -1;
    std::uint64_t finalTick = 0;
    std::uint64_t stateHash = 0;
    double p50StepMs = 0;
    double p95StepMs = 0;
    double p99StepMs = 0;
    double maxStepMs = 0;
    double commandWallTotalMs = 0;
    NavigationStats navigation{};
    std::array<int, Simulation::MaxPlayers> teamSupply{};
    std::vector<CommandMetric> commands;
    std::vector<Laggard> laggards;
    std::vector<GeometryFailure> geometryFailures;
    int droppedGeometryFailures = 0;
    std::vector<std::string> errors;
};

std::vector<Entity>& entities(Simulation& simulation) {
    return const_cast<std::vector<Entity>&>(simulation.entities());
}

std::vector<Obstacle>& obstacles(Simulation& simulation) {
    return const_cast<std::vector<Obstacle>&>(simulation.obstacles());
}

float distance(Vec2 left, Vec2 right) {
    return std::hypot(left.x - right.x, left.y - right.y);
}

bool intersects(Vec2 point, float radius, const Obstacle& obstacle) {
    const float dx = std::max(std::fabs(point.x - obstacle.center.x) - obstacle.half.x, 0.0f);
    const float dy = std::max(std::fabs(point.y - obstacle.center.y) - obstacle.half.y, 0.0f);
    return dx * dx + dy * dy < radius * radius - 0.001f;
}

void addHeadquarters(Simulation& simulation, int players) {
    const std::array<Vec2, 4> homes{{{250, 250}, {4550, 4550}, {4550, 250}, {250, 4550}}};
    for (int team = 0; team < players; ++team) {
        simulation.debugSpawn(Kind::Headquarters, team, homes[static_cast<std::size_t>(team)]);
    }
}

Simulation emptySimulation(int players, std::uint32_t seed) {
    Simulation simulation;
    Config config{0, seed, false, 1.0f};
    config.playerCount = players;
    simulation.reset(config);
    entities(simulation).clear();
    obstacles(simulation).clear();
    addHeadquarters(simulation, players);
    return simulation;
}

void spawnGrid(Simulation& simulation, Route& route, const std::vector<Kind>& kinds,
               Vec2 origin, int rows, int columns, Vec2 columnStep, Vec2 rowStep) {
    const int count = rows * columns;
    for (int index = 0; index < count; ++index) {
        const int row = index / columns;
        const int column = index % columns;
        const Vec2 position{origin.x + columnStep.x * column + rowStep.x * row,
                            origin.y + columnStep.y * column + rowStep.y * row};
        route.units.push_back(simulation.debugSpawn(kinds[static_cast<std::size_t>(index) % kinds.size()],
                                                    route.team, position));
    }
}

Workload crossing160() {
    Workload workload{"navigation_160_crossing",
                      "Existing NavigationTests 160-worker two-way central-gap workload.", 1600,
                      emptySimulation(2, 0xC1D3u), {}};
    obstacles(workload.simulation) = {{{2400, 1700}, {120, 700}}, {{2400, 3100}, {120, 700}}};
    Route left{"left_to_right", 0, {}, {3800, 2800}};
    Route right{"right_to_left", 0, {}, {1000, 2000}};
    spawnGrid(workload.simulation, left, {Kind::Worker}, {900, 1900}, 8, 10, {38, 0}, {0, 38});
    spawnGrid(workload.simulation, right, {Kind::Worker}, {3900, 2900}, 8, 10, {-38, 0}, {0, -38});
    workload.routes = {std::move(left), std::move(right)};
    return workload;
}

Workload mixedArmy200() {
    Workload workload{"mixed_army_200",
                      "Two friendly mixed-speed 100-unit armies cross through the central gap; Move orders suppress combat.",
                      2400, emptySimulation(2, 0xA2200u), {}};
    obstacles(workload.simulation) = {{{2400, 1650}, {140, 650}}, {{2400, 3150}, {140, 650}}};
    const std::vector<Kind> mix{Kind::Striker, Kind::Lancer, Kind::Scout, Kind::Bastion,
                                Kind::Mortar, Kind::Mender, Kind::Kite, Kind::Worker};
    Route left{"mixed_left_to_right", 0, {}, {3800, 3000}};
    Route right{"mixed_right_to_left", 0, {}, {1000, 1800}};
    spawnGrid(workload.simulation, left, mix, {620, 1650}, 10, 10, {76, 0}, {0, 76});
    spawnGrid(workload.simulation, right, mix, {4180, 3150}, 10, 10, {-76, 0}, {0, -76});
    workload.routes = {std::move(left), std::move(right)};
    return workload;
}

Workload fourSeat400() {
    Workload workload{"four_seat_400",
                      "Four isolated seats each run two friendly 50-unit crossing routes; center walls prevent inter-seat combat.",
                      2000, emptySimulation(4, 0xA4400u), {}};
    obstacles(workload.simulation) = {{{2400, 2400}, {80, 2400}}, {{2400, 2400}, {2400, 80}}};
    const std::vector<Kind> mix{Kind::Striker, Kind::Lancer, Kind::Scout, Kind::Bastion,
                                Kind::Mortar, Kind::Mender, Kind::Kite, Kind::Worker};
    const std::array<Vec2, 4> centers{{{1200, 1200}, {3600, 3600}, {3600, 1200}, {1200, 3600}}};
    for (int team = 0; team < 4; ++team) {
        const Vec2 center = centers[static_cast<std::size_t>(team)];
        Route first{"seat_" + std::to_string(team) + "_a", team, {}, {center.x + 650, center.y + 300}};
        Route second{"seat_" + std::to_string(team) + "_b", team, {}, {center.x - 650, center.y - 300}};
        spawnGrid(workload.simulation, first, mix, {center.x - 850, center.y - 650}, 5, 10,
                  {70, 0}, {0, 70});
        spawnGrid(workload.simulation, second, mix, {center.x + 850, center.y + 650}, 5, 10,
                  {-70, 0}, {0, -70});
        workload.routes.push_back(std::move(first));
        workload.routes.push_back(std::move(second));
    }
    return workload;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(std::ceil(fraction * values.size()));
    return values[std::min(values.size() - 1, std::max<std::size_t>(1, rank) - 1)];
}

void validateRuntimeGeometry(const Workload& workload, const std::vector<StaticCircle>& buildings,
                             RunResult& result) {
    constexpr std::size_t MaxReportedFailures = 16;
    auto record = [&](Id id, const char* check) {
        const auto duplicate = std::find_if(result.geometryFailures.begin(), result.geometryFailures.end(),
                                            [&](const GeometryFailure& failure) {
                                                return failure.id == id && failure.check == check;
                                            });
        if (duplicate != result.geometryFailures.end()) return;
        if (result.geometryFailures.size() < MaxReportedFailures)
            result.geometryFailures.push_back({workload.simulation.tick(), id, check});
        else
            ++result.droppedGeometryFailures;
    };
    for (const auto& entity : workload.simulation.entities()) {
        const auto& unit = definition(entity.kind);
        if (!entity.alive() || unit.building || entity.kind == Kind::Resource) continue;
        if (!std::isfinite(entity.pos.x) || !std::isfinite(entity.pos.y)) {
            record(entity.id, "non_finite_position");
            continue;
        }
        if (entity.pos.x < unit.radius || entity.pos.y < unit.radius ||
            entity.pos.x > workload.simulation.worldSize() - unit.radius ||
            entity.pos.y > workload.simulation.worldSize() - unit.radius)
            record(entity.id, "outside_battlefield");
        if (unit.air) continue;
        for (const auto& obstacle : workload.simulation.obstacles())
            if (intersects(entity.pos, unit.radius, obstacle)) record(entity.id, "terrain_overlap");
        for (const auto& building : buildings)
            if (distance(entity.pos, building.position) + 0.001f < unit.radius + building.radius)
                record(entity.id, "building_overlap");
    }
}

void validateGeometry(const Workload& workload, RunResult& result) {
    std::vector<const Entity*> tracked;
    for (const auto& route : workload.routes) {
        result.expected += static_cast<int>(route.units.size());
        for (Id id : route.units) {
            const Entity* entity = workload.simulation.find(id);
            if (!entity) {
                result.errors.push_back("spawned unit " + std::to_string(id) + " is missing");
                continue;
            }
            tracked.push_back(entity);
            const auto& unit = definition(entity->kind);
            if (entity->pos.x < unit.radius || entity->pos.y < unit.radius ||
                entity->pos.x > workload.simulation.worldSize() - unit.radius ||
                entity->pos.y > workload.simulation.worldSize() - unit.radius) {
                result.errors.push_back("unit " + std::to_string(id) + " spawned outside the battlefield");
            }
            if (!unit.air) for (const auto& obstacle : workload.simulation.obstacles()) {
                if (intersects(entity->pos, unit.radius, obstacle))
                    result.errors.push_back("unit " + std::to_string(id) + " spawned in terrain");
            }
        }
    }
    for (std::size_t left = 0; left < tracked.size(); ++left) {
        if (definition(tracked[left]->kind).air) continue;
        for (std::size_t right = left + 1; right < tracked.size(); ++right) {
            if (definition(tracked[right]->kind).air) continue;
            const float required = definition(tracked[left]->kind).radius + definition(tracked[right]->kind).radius;
            if (distance(tracked[left]->pos, tracked[right]->pos) + 0.001f < required) {
                result.errors.push_back("ground units " + std::to_string(tracked[left]->id) + " and " +
                                        std::to_string(tracked[right]->id) + " overlap at spawn");
            }
        }
    }
}

RunResult run(Workload workload, bool collectTimings) {
    RunResult result;
    result.name = workload.name;
    result.description = workload.description;
    result.measuredTicks = workload.measuredTicks;
    validateGeometry(workload, result);

    std::vector<StaticCircle> buildings;
    for (const auto& entity : workload.simulation.entities()) if (entity.alive() && definition(entity.kind).building)
        buildings.push_back({entity.id, entity.pos, definition(entity.kind).radius});

    for (int tick = 0; tick < WarmupTicks; ++tick) {
        workload.simulation.update(Simulation::Step);
        validateRuntimeGeometry(workload, buildings, result);
    }
    if (workload.simulation.tick() != WarmupTicks || workload.simulation.winner() != -1) {
        result.errors.push_back("simulation did not remain active through warmup");
    }

    for (const auto& route : workload.routes) {
        CommandMetric metric;
        metric.route = route.name;
        metric.team = route.team;
        metric.unitCount = route.units.size();
        metric.goal = route.commandGoal;
        metric.issuedTick = workload.simulation.tick();
        const auto wallStart = std::chrono::steady_clock::now();
        const CommandResult command = workload.simulation.command(
            {CommandType::Move, route.team, route.units, route.commandGoal, 0, Kind::Worker, 0});
        metric.wallMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - wallStart).count();
        metric.accepted = command.accepted;
        metric.message = command.message;
        metric.acceptedTick = workload.simulation.recording().empty()
                                  ? workload.simulation.tick()
                                  : workload.simulation.recording().back().tick;
        metric.acceptanceTickDelta = metric.acceptedTick >= metric.issuedTick
                                       ? metric.acceptedTick - metric.issuedTick : 0;
        result.commandWallTotalMs += metric.wallMilliseconds;
        if (!metric.accepted) result.errors.push_back("route " + route.name + " command was rejected: " + command.message);
        if (metric.acceptedTick != metric.issuedTick)
            result.errors.push_back("route " + route.name + " was not recorded on its issue tick");
        for (Id id : route.units) {
            const Entity* entity = workload.simulation.find(id);
            if (!entity || entity->order != Order::Move)
                result.errors.push_back("route " + route.name + " did not enter Move state on the acceptance tick");
        }
        result.commands.push_back(std::move(metric));
    }

    std::vector<AssignedGoal> assignedGoals;
    for (const auto& route : workload.routes) for (Id id : route.units) {
        const Entity* entity = workload.simulation.find(id);
        if (!entity) continue;
        assignedGoals.push_back({id, entity->kind, entity->goal});
        const auto& unit = definition(entity->kind);
        if (entity->goal.x < unit.radius || entity->goal.y < unit.radius ||
            entity->goal.x > workload.simulation.worldSize() - unit.radius ||
            entity->goal.y > workload.simulation.worldSize() - unit.radius)
            result.errors.push_back("unit " + std::to_string(id) + " received an out-of-bounds goal");
        if (!unit.air) for (const auto& obstacle : workload.simulation.obstacles()) {
            if (intersects(entity->goal, unit.radius, obstacle))
                result.errors.push_back("unit " + std::to_string(id) + " received a goal in terrain");
        }
    }

    std::vector<double> timings;
    if (collectTimings) timings.reserve(static_cast<std::size_t>(workload.measuredTicks));
    for (int tick = 0; tick < workload.measuredTicks; ++tick) {
        workload.simulation.update(Simulation::Step);
        if (collectTimings) timings.push_back(workload.simulation.lastStepMilliseconds());
        validateRuntimeGeometry(workload, buildings, result);
    }

    result.winner = workload.simulation.winner();
    result.finalTick = workload.simulation.tick();
    result.stateHash = workload.simulation.stateHash();
    result.navigation = workload.simulation.navigationStats();
    for (int team = 0; team < workload.simulation.playerCount(); ++team)
        result.teamSupply[static_cast<std::size_t>(team)] = workload.simulation.supply(team);
    result.p50StepMs = percentile(timings, 0.50);
    result.p95StepMs = percentile(timings, 0.95);
    result.p99StepMs = percentile(timings, 0.99);
    result.maxStepMs = percentile(timings, 1.0);

    for (const auto& tracked : assignedGoals) {
        const Entity* entity = workload.simulation.find(tracked.id);
        if (!entity) {
            ++result.missing;
            continue;
        }
        if (!entity->alive()) {
            ++result.dead;
            continue;
        }
        ++result.alive;
        const float remaining = distance(entity->pos, tracked.goal);
        const bool goalChanged = distance(entity->goal, tracked.goal) > 0.001f;
        if (goalChanged) result.errors.push_back("unit " + std::to_string(tracked.id) + " changed its assigned goal");
        const auto& unit = definition(entity->kind);
        if (!std::isfinite(entity->pos.x) || !std::isfinite(entity->pos.y))
            result.errors.push_back("unit " + std::to_string(tracked.id) + " ended at a non-finite position");
        if (!unit.air) {
            for (const auto& obstacle : workload.simulation.obstacles()) if (intersects(entity->pos, unit.radius, obstacle))
                result.errors.push_back("unit " + std::to_string(tracked.id) + " ended inside terrain");
            for (const auto& fixed : workload.simulation.entities()) {
                if (!fixed.alive() || fixed.id == entity->id || !definition(fixed.kind).building) continue;
                if (distance(entity->pos, fixed.pos) + 0.001f < unit.radius + definition(fixed.kind).radius)
                    result.errors.push_back("unit " + std::to_string(tracked.id) + " ended inside a building");
            }
        }
        if (remaining <= ArrivalTolerance) {
            ++result.arrived;
        } else if (entity->navigationExhausted) {
            ++result.exhausted;
        } else if (entity->order == Order::Move) {
            ++result.stillMoving;
        } else {
            ++result.unexplainedOrderLoss;
        }
        if (remaining > ArrivalTolerance) {
            result.laggards.push_back({tracked.id, tracked.kind, entity->pos, tracked.goal, entity->goal,
                                      entity->order, remaining, entity->yieldFor, entity->stalledFor,
                                      entity->pathIndex, entity->path.size(), goalChanged});
        }
    }
    result.failed = result.missing + result.dead + result.exhausted + result.unexplainedOrderLoss;
    if (result.winner != -1 || result.finalTick != static_cast<std::uint64_t>(WarmupTicks + workload.measuredTicks))
        result.errors.push_back("simulation ended or froze before all measured ticks ran");
    if (result.arrived != result.expected)
        result.errors.push_back(std::to_string(result.expected - result.arrived) + " units did not arrive within tolerance");
    if (result.failed != 0) result.errors.push_back("workload has missing, dead, exhausted, or unexplained units");
    if (result.stillMoving != 0) result.errors.push_back("workload ended with units still moving");
    if (!result.geometryFailures.empty()) result.errors.push_back("runtime geometry validation failed");
    return result;
}

std::string escaped(const std::string& text) {
    std::ostringstream output;
    for (unsigned char character : text) {
        switch (character) {
            case '\"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20) output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                                             << static_cast<int>(character) << std::dec;
                else output << character;
        }
    }
    return output.str();
}

std::string hashText(std::uint64_t hash) {
    std::ostringstream output;
    output << "0x" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}

void printResult(const RunResult& result, bool deterministic, std::uint64_t repeatHash, bool last) {
    std::cout << "    {\n"
              << "      \"name\": \"" << escaped(result.name) << "\",\n"
              << "      \"description\": \"" << escaped(result.description) << "\",\n"
              << "      \"stress_target_only\": true,\n"
              << "      \"synthetic_debug_spawn\": true,\n"
              << "      \"economy_supply_limits_bypassed\": true,\n"
              << "      \"warmup_ticks\": " << result.warmupTicks << ",\n"
              << "      \"measured_ticks\": " << result.measuredTicks << ",\n"
              << "      \"fixed_step_seconds\": " << result.stepSeconds << ",\n"
              << "      \"simulated_seconds\": " << result.measuredTicks * result.stepSeconds << ",\n"
              << "      \"arrival_tolerance\": " << result.arrivalTolerance << ",\n"
              << "      \"step_ms\": {\"p50\": " << result.p50StepMs << ", \"p95\": " << result.p95StepMs
              << ", \"p99\": " << result.p99StepMs << ", \"max\": " << result.maxStepMs << "},\n"
              << "      \"command_wall_total_ms\": " << result.commandWallTotalMs << ",\n"
              << "      \"routes\": [\n";
    for (std::size_t index = 0; index < result.commands.size(); ++index) {
        const auto& command = result.commands[index];
        std::cout << "        {\"name\": \"" << escaped(command.route) << "\", \"team\": " << command.team
                  << ", \"expected_units\": " << command.unitCount
                  << ", \"command_goal\": {\"x\": " << command.goal.x << ", \"y\": " << command.goal.y << "}"
                  << ", \"accepted\": " << (command.accepted ? "true" : "false")
                  << ", \"issued_tick\": " << command.issuedTick << ", \"accepted_tick\": " << command.acceptedTick
                  << ", \"acceptance_tick_delta\": " << command.acceptanceTickDelta
                  << ", \"command_wall_ms\": " << command.wallMilliseconds
                  << ", \"message\": \"" << escaped(command.message) << "\"}"
                  << (index + 1 == result.commands.size() ? "\n" : ",\n");
    }
    std::cout << "      ],\n"
              << "      \"navigation\": {\"searches\": " << result.navigation.searches
              << ", \"expanded\": " << result.navigation.expanded << ", \"failures\": " << result.navigation.failures
              << ", \"budget_deferrals\": " << result.navigation.budgetDeferrals << "},\n"
              << "      \"team_supply\": [";
    for (std::size_t index = 0; index < result.teamSupply.size(); ++index) {
        if (index) std::cout << ", ";
        std::cout << result.teamSupply[index];
    }
    std::cout << "],\n"
              << "      \"units\": {\"expected\": " << result.expected << ", \"arrived\": " << result.arrived
              << ", \"alive\": " << result.alive << ", \"failed\": " << result.failed
              << ", \"still_moving\": " << result.stillMoving << ", \"missing\": " << result.missing
              << ", \"dead\": " << result.dead << ", \"navigation_exhausted\": " << result.exhausted
              << ", \"unexplained_order_loss\": " << result.unexplainedOrderLoss << "},\n"
              << "      \"winner\": " << result.winner << ",\n"
              << "      \"final_tick\": " << result.finalTick << ",\n"
              << "      \"state_hash\": \"" << hashText(result.stateHash) << "\",\n"
              << "      \"repeat_state_hash\": \"" << hashText(repeatHash) << "\",\n"
              << "      \"deterministic_state\": " << (deterministic ? "true" : "false") << ",\n"
              << "      \"valid\": " << (result.errors.empty() && deterministic ? "true" : "false") << ",\n"
              << "      \"laggards\": [";
    for (std::size_t index = 0; index < result.laggards.size(); ++index) {
        const auto& laggard = result.laggards[index];
        if (index) std::cout << ", ";
        std::cout << "{\"id\": " << laggard.id << ", \"kind\": \"" << definition(laggard.kind).name
                  << "\", \"position\": {\"x\": " << laggard.position.x << ", \"y\": " << laggard.position.y
                  << "}, \"assigned_goal\": {\"x\": " << laggard.assignedGoal.x << ", \"y\": " << laggard.assignedGoal.y
                  << "}, \"current_goal\": {\"x\": " << laggard.currentGoal.x << ", \"y\": " << laggard.currentGoal.y
                  << "}, \"goal_changed\": " << (laggard.goalChanged ? "true" : "false")
                  << ", \"remaining\": " << laggard.remaining << ", \"order\": " << static_cast<int>(laggard.order)
                  << ", \"yield_for\": " << laggard.yieldFor << ", \"stalled_for\": " << laggard.stalledFor
                  << ", \"path_index\": " << laggard.pathIndex << ", \"path_size\": " << laggard.pathSize << "}";
    }
    std::cout << "],\n"
              << "      \"runtime_geometry\": {\"checked_each_tick\": true, \"failures\": [";
    for (std::size_t index = 0; index < result.geometryFailures.size(); ++index) {
        const auto& failure = result.geometryFailures[index];
        if (index) std::cout << ", ";
        std::cout << "{\"tick\": " << failure.tick << ", \"id\": " << failure.id
                  << ", \"check\": \"" << failure.check << "\"}";
    }
    std::cout << "], \"dropped_unique_failures\": " << result.droppedGeometryFailures << "},\n"
              << "      \"errors\": [";
    for (std::size_t index = 0; index < result.errors.size(); ++index) {
        if (index) std::cout << ", ";
        std::cout << "\"" << escaped(result.errors[index]) << "\"";
    }
    std::cout << "]\n    }" << (last ? "\n" : ",\n");
}

} // namespace

int main() {
    std::cout << std::fixed << std::setprecision(6);
    std::vector<RunResult> results;
    std::vector<std::uint64_t> repeatHashes;
    std::vector<bool> deterministic;

    auto execute = [&](auto makeWorkload) {
        RunResult measured = run(makeWorkload(), true);
        RunResult repeat = run(makeWorkload(), false);
        const bool matches = measured.stateHash == repeat.stateHash;
        if (!matches) measured.errors.push_back("repeat run produced a different gameplay state hash");
        results.push_back(std::move(measured));
        repeatHashes.push_back(repeat.stateHash);
        deterministic.push_back(matches);
    };
    execute(crossing160);
    execute(mixedArmy200);
    execute(fourSeat400);

    bool success = true;
    for (std::size_t index = 0; index < results.size(); ++index)
        success = success && results[index].errors.empty() && deterministic[index];
    std::cout << "{\n"
              << "  \"schema\": \"cinderline.gameplay_baseline.v1\",\n"
              << "  \"success\": " << (success ? "true" : "false") << ",\n"
              << "  \"timings_excluded_from_determinism_hash\": true,\n"
              << "  \"workloads\": [\n";
    for (std::size_t index = 0; index < results.size(); ++index)
        printResult(results[index], deterministic[index], repeatHashes[index], index + 1 == results.size());
    std::cout << "  ]\n}\n";
    return success ? 0 : 1;
}
