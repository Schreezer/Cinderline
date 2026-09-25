#include "Sim/Network.h"
#include "MatchSnapshotMetrics.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace cinder;

namespace {
using Clock = std::chrono::steady_clock;
constexpr float Pi = 3.14159265358979323846f;
constexpr int MaximumTicksPerLeg = 9600;
constexpr std::array<Kind, 8> Mix{{Kind::Striker, Kind::Lancer, Kind::Scout, Kind::Bastion,
    Kind::Mortar, Kind::Mender, Kind::Kite, Kind::Worker}};

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
float distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
void word(std::uint64_t& hash, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) { hash ^= (value >> (8 * byte)) & 255; hash *= 1099511628211ull; }
}
void real(std::uint64_t& hash, float value) {
    std::uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits)); word(hash, bits);
}
std::uint64_t recordingHash(const Simulation& simulation) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto& entry : simulation.recording()) {
        const auto& command = entry.command;
        word(hash, entry.tick); word(hash, static_cast<int>(command.type)); word(hash, command.team);
        word(hash, command.units.size()); for (Id id : command.units) word(hash, id);
        real(hash, command.point.x); real(hash, command.point.y); word(hash, command.target);
        word(hash, static_cast<int>(command.kind)); word(hash, command.queueIndex);
        word(hash, static_cast<int>(command.queueMode)); word(hash, static_cast<int>(command.spacing));
        word(hash, command.hasArrivalFacing); real(hash, command.arrivalFacing);
    }
    return hash;
}
struct Leg {
    int spacing = 0, ticks = 0, arrived = 0;
    float facing = 0;
    double commandMs = 0;
};
struct Result {
    int count = 0;
    std::uint64_t state = 0, trajectory = 1469598103934665603ull, recording = 0;
    std::vector<Leg> legs;
    std::vector<double> stepMs;
    baseline::SnapshotMetrics snapshots;
};

Result run(int count) {
    Simulation simulation; simulation.reset({0, 0xF04A7u, false, 1});
    // A synthetic open-map workload. Paid economy and constrained navigation
    // are separate suites; this measures full-sized formation commands/arrivals.
    const_cast<std::vector<Entity>&>(simulation.entities()).clear();
    const_cast<std::vector<Obstacle>&>(simulation.obstacles()).clear();
    simulation.debugSpawn(Kind::Headquarters, 0, {250, 250});
    simulation.debugSpawn(Kind::Headquarters, 1, {4550, 4550});
    std::vector<Id> units;
    const int columns = static_cast<int>(std::ceil(std::sqrt(count)));
    for (int index = 0; index < count; ++index)
        units.push_back(simulation.debugSpawn(Mix[index % Mix.size()], 0,
            {600.0f + 80.0f * (index % columns), 1400.0f + 80.0f * (index / columns)}));
    Result result; result.count = count;
    std::array<net::ViewMemory, Simulation::MaxPlayers> views;
    const std::array<Vec2, 3> centers{{{3100, 1800}, {3100, 3200}, {1700, 3200}}};
    const std::array<float, 3> angles{{0, Pi / 4, -Pi}};
    for (int stage = 0; stage < 3; ++stage) {
        Command command; command.type = stage == 2 ? CommandType::Defend : CommandType::Move;
        command.team = 0; command.units = units; command.point = centers[stage];
        command.spacing = static_cast<FormationSpacing>(stage);
        command.hasArrivalFacing = true; command.arrivalFacing = angles[stage];
        Leg leg; leg.spacing = stage; leg.facing = angles[stage];
        const auto commandStart = Clock::now();
        const auto accepted = simulation.command(command);
        leg.commandMs = std::chrono::duration<double, std::milli>(Clock::now() - commandStart).count();
        if (!accepted.accepted) throw std::runtime_error("Formation command rejected: " + accepted.message);
        std::vector<Vec2> points;
        std::vector<bool> completed(units.size(), false);
        for (Id id : units) points.push_back(simulation.find(id)->goal);
        for (std::size_t i = 0; i < units.size(); ++i) {
            const auto& first = definition(simulation.find(units[i])->kind);
            require(points[i].x >= first.radius && points[i].y >= first.radius &&
                points[i].x <= simulation.worldSize() - first.radius &&
                points[i].y <= simulation.worldSize() - first.radius, "Accepted slot outside world");
            for (std::size_t j = 0; j < i; ++j) {
                const auto& second = definition(simulation.find(units[j])->kind);
                if (first.air == second.air)
                    require(distance(points[i], points[j]) + 0.001f >= first.radius + second.radius + 10,
                            "Accepted formation slots violate same-layer clearance");
            }
        }
        int stable = 0;
        while (leg.ticks < MaximumTicksPerLeg && stable < 20) {
            const auto started = Clock::now();
            const auto tick = simulation.tick(); simulation.update(Simulation::Step);
            result.stepMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - started).count());
            require(simulation.tick() == tick + 1 && simulation.winner() == -1, "Synthetic match stopped");
            ++leg.ticks; word(result.trajectory, simulation.stateHash());
            if (leg.ticks % 2 == 0) result.snapshots.sample(simulation, views);
            int atDestination = 0;
            for (std::size_t index = 0; index < units.size(); ++index) {
                const auto* entity = simulation.find(units[index]);
                require(entity && entity->alive(), "Formation recipient disappeared");
                require(entity->goal.x == points[index].x && entity->goal.y == points[index].y,
                        "Formation changed an accepted destination");
                // Defend restores facing only inside its existing five-unit
                // anchor tolerance; being near the anchor is still travel.
                const bool finished = distance(entity->pos, points[index]) <= (stage == 2 ? 5.001f : 35.0f) &&
                    entity->order == (stage == 2 ? Order::Defend : Order::Idle);
                if (finished && !completed[index]) {
                    if (std::abs(std::remainder(entity->facing - angles[stage], 2 * Pi)) >= 0.001f)
                        throw std::runtime_error("Formation completion did not apply arrival facing: stage=" +
                            std::to_string(stage) + " id=" + std::to_string(entity->id) + " kind=" +
                            std::to_string(static_cast<int>(entity->kind)) + " tick=" + std::to_string(leg.ticks) +
                            " actual=" + std::to_string(entity->facing) + " requested=" +
                            std::to_string(angles[stage]) + " distance=" +
                            std::to_string(distance(entity->pos, points[index])) + " order=" +
                            std::to_string(static_cast<int>(entity->order)));
                    completed[index] = true;
                }
                require(entity->order == Order::Move || entity->order == Order::Idle ||
                        (stage == 2 && entity->order == Order::Defend), "Formation lost tactical intent");
                atDestination += finished;
            }
            stable = atDestination == count ? stable + 1 : 0;
        }
        leg.arrived = static_cast<int>(std::count(completed.begin(), completed.end(), true));
        if (leg.arrived != count || stable < 20) {
            for (std::size_t index = 0; index < units.size(); ++index) {
                const auto& e = *simulation.find(units[index]);
                if (distance(e.pos, points[index]) <= 5.001f && completed[index]) continue;
                std::cerr << "laggard stage=" << stage << " id=" << e.id << " kind=" << static_cast<int>(e.kind)
                    << " pos=" << e.pos.x << ',' << e.pos.y << " goal=" << e.goal.x << ',' << e.goal.y
                    << " distance=" << distance(e.pos,e.goal) << " order=" << static_cast<int>(e.order)
                    << " exhausted=" << e.navigationExhausted << " failures=" << e.navigationFailures << '\n';
            }
            simulation.save("artifacts/formation-orders/formation-jam-" + std::to_string(count) +
                            "-" + std::to_string(stage) + ".cinder");
            throw std::runtime_error("Formation failed stable arrivals: units=" + std::to_string(count) +
                " spacing=" + std::to_string(stage) + " arrived=" + std::to_string(leg.arrived));
        }
        result.legs.push_back(leg);
    }
    result.state = simulation.stateHash(); result.recording = recordingHash(simulation);
    return result;
}

double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values.empty() ? 0 : values[std::min(values.size() - 1,
        static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1)];
}
} // namespace

int main() {
    try {
        std::vector<Result> results;
        for (int count : {16, 160, 200}) {
            std::cerr << "formation baseline units=" << count << '\n';
            auto first = run(count); const auto repeated = run(count);
            require(first.state == repeated.state && first.trajectory == repeated.trajectory &&
                    first.recording == repeated.recording, "Formation deterministic repeat diverged");
            results.push_back(std::move(first));
        }
        std::cout << std::fixed << std::setprecision(6)
            << "{\n  \"schema\": \"cinderline.formation_baseline.v1\",\n  \"success\": true,\n"
            << "  \"scope\": \"synthetic mixed-unit simulation and fog-filtered snapshots\",\n"
            << "  \"device_performance_claim\": false,\n  \"maximum_ticks_per_leg\": "
            << MaximumTicksPerLeg << ",\n  \"stable_arrival_ticks\": 20,\n  \"workloads\": [\n";
        for (std::size_t index = 0; index < results.size(); ++index) {
            const auto& r = results[index];
            if (index) std::cout << ",\n";
            std::cout << "    {\"units\": " << r.count << ", \"state_hash\": \"" << r.state
                << "\", \"trajectory_hash\": \"" << r.trajectory << "\", \"recording_hash\": \""
                << r.recording << "\", \"deterministic_repeat\": true, \"accepted_points_unchanged\": true,\n"
                << "     \"step_ms\": {\"p50\": " << percentile(r.stepMs, .5) << ", \"p95\": "
                << percentile(r.stepMs, .95) << ", \"p99\": " << percentile(r.stepMs, .99)
                << ", \"max\": " << percentile(r.stepMs, 1) << "}, \"legs\": [";
            for (std::size_t i = 0; i < r.legs.size(); ++i) {
                const auto& leg = r.legs[i]; if (i) std::cout << ',';
                std::cout << "{\"spacing\": " << leg.spacing << ", \"facing\": " << leg.facing
                    << ", \"ticks\": " << leg.ticks << ", \"arrived\": " << leg.arrived
                    << ", \"command_ms\": " << leg.commandMs << '}';
            }
            std::cout << "], \"snapshots\": "; r.snapshots.writeJson(std::cout); std::cout << '}';
        }
        std::cout << "\n  ]\n}\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "formation baseline failed: " << error.what() << '\n';
        std::cout << "{\"schema\":\"cinderline.formation_baseline.v1\",\"success\":false}\n";
        return 1;
    }
}
