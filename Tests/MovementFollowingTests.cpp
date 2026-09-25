#include "Sim/Simulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cinder;

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

float distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

Entity& movingUnit(Simulation& simulation, Id id) {
    return const_cast<Entity&>(*simulation.find(id));
}

Simulation fixture() {
    Simulation simulation;
    simulation.reset({0, 0xC1D3u, false, 1});
    const_cast<std::vector<Entity>&>(simulation.entities()).clear();
    const_cast<std::vector<Obstacle>&>(simulation.obstacles()).clear();
    simulation.debugSpawn(Kind::Headquarters, 0, {700, 700});
    simulation.debugSpawn(Kind::Headquarters, 1, {4400, 4400});
    return simulation;
}

void move(Simulation& simulation, Id unit, Vec2 goal) {
    check(simulation.command({CommandType::Move, 0, {unit}, goal}).accepted,
          "fixture move command is accepted");
}

// Independent swept-disk oracle: analytic segment distances against original
// terrain boxes and entity circles, without invoking navigation predicates.
float segmentDistanceSquared(Vec2 a, Vec2 b, Vec2 point) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length = dx * dx + dy * dy;
    const float t = length > 0 ? std::clamp(
        ((point.x - a.x) * dx + (point.y - a.y) * dy) / length, 0.0f, 1.0f) : 0;
    const float x = a.x + t * dx - point.x, y = a.y + t * dy - point.y;
    return x * x + y * y;
}

float pointBoxDistanceSquared(Vec2 point, const Obstacle& box) {
    const float dx = std::max(std::fabs(point.x - box.center.x) - box.half.x, 0.0f);
    const float dy = std::max(std::fabs(point.y - box.center.y) - box.half.y, 0.0f);
    return dx * dx + dy * dy;
}

float segmentBoxDistanceSquared(Vec2 a, Vec2 b, const Obstacle& box) {
    float enter = 0, leave = 1;
    const std::array<float, 2> start{{a.x, a.y}}, delta{{b.x - a.x, b.y - a.y}};
    const std::array<float, 2> low{{box.center.x - box.half.x, box.center.y - box.half.y}};
    const std::array<float, 2> high{{box.center.x + box.half.x, box.center.y + box.half.y}};
    bool intersects = true;
    for (int axis = 0; axis < 2; ++axis) {
        if (std::fabs(delta[axis]) < 0.000001f) {
            if (start[axis] < low[axis] || start[axis] > high[axis]) intersects = false;
        } else {
            const float first = (low[axis] - start[axis]) / delta[axis];
            const float second = (high[axis] - start[axis]) / delta[axis];
            enter = std::max(enter, std::min(first, second));
            leave = std::min(leave, std::max(first, second));
        }
    }
    if (intersects && enter <= leave) return 0;
    float best = std::min(pointBoxDistanceSquared(a, box), pointBoxDistanceSquared(b, box));
    for (float x : {low[0], high[0]}) for (float y : {low[1], high[1]})
        best = std::min(best, segmentDistanceSquared(a, b, {x, y}));
    return best;
}

bool clearSegment(const Simulation& simulation, Id unit, Vec2 from, Vec2 to) {
    const float radius = definition(simulation.find(unit)->kind).radius;
    constexpr float tolerance = 0.02f;
    for (const auto& box : simulation.obstacles())
        if (segmentBoxDistanceSquared(from, to, box) < (radius - tolerance) * (radius - tolerance))
            return false;
    for (const auto& obstacle : simulation.entities()) {
        if (obstacle.id == unit || !obstacle.alive()) continue;
        if (!definition(obstacle.kind).building && obstacle.kind != Kind::Resource) continue;
        if (obstacle.kind == Kind::Resource && obstacle.resource <= 0) continue;
        const float clearance = radius + definition(obstacle.kind).radius - tolerance;
        if (segmentDistanceSquared(from, to, obstacle.pos) < clearance * clearance) return false;
    }
    return true;
}

std::string state(const Simulation& simulation, Id id, int step) {
    const auto& unit = *simulation.find(id);
    std::ostringstream result;
    result << "tick=" << step << " pos=" << unit.pos.x << ',' << unit.pos.y
           << " waypoint=" << unit.pathIndex << '/' << unit.path.size()
           << " stalled=" << unit.stalledFor << " failures=" << unit.navigationFailures;
    if (unit.pathIndex >= 0 && unit.pathIndex < static_cast<int>(unit.path.size()))
        result << " next=" << unit.path[unit.pathIndex].x << ',' << unit.path[unit.pathIndex].y;
    return result.str();
}

void checkActiveLeg(const Simulation& simulation, Id id, int step) {
    const auto& unit = *simulation.find(id);
    if (unit.pathIndex < static_cast<int>(unit.path.size()))
        check(clearSegment(simulation, id, unit.pos, unit.path[unit.pathIndex]),
              "follower aimed through static geometry: " + state(simulation, id, step));
}

void finishMove(Simulation& simulation, Id id, Vec2 goal, float maximumSeconds) {
    const auto initialFailures = simulation.navigationStats().failures;
    for (int step = 0; step < static_cast<int>(maximumSeconds / Simulation::Step); ++step) {
        const Vec2 before = simulation.find(id)->pos;
        simulation.update(Simulation::Step);
        const auto& unit = *simulation.find(id);
        check(clearSegment(simulation, id, before, unit.pos),
              "movement swept through static geometry: " + state(simulation, id, step));
        checkActiveLeg(simulation, id, step);
        check(!unit.navigationExhausted && unit.navigationFailures == 0 &&
              simulation.navigationStats().failures == initialFailures,
              "unobstructed route accumulated a navigation failure: " + state(simulation, id, step));
        check(unit.stalledFor < 0.35f,
              "unobstructed route stalled before changing direction: " + state(simulation, id, step));
        if (distance(unit.pos, goal) < 24) return;
    }
    throw std::runtime_error("move did not finish: " + state(simulation, id, -1));
}

void boxRoute(Kind kind) {
    auto simulation = fixture();
    const_cast<std::vector<Obstacle>&>(simulation.obstacles()) = {{{1600, 1200}, {100, 150}}};
    const Id unit = simulation.debugSpawn(kind, 0, {1200, 1200});
    move(simulation, unit, {2000, 1200});
    finishMove(simulation, unit, {2000, 1200}, 18);
}

void circleRoute(Kind kind) {
    auto simulation = fixture();
    simulation.debugSpawn(Kind::Headquarters, 0, {1600, 1200});
    const Id unit = simulation.debugSpawn(kind, 0, {1200, 1200});
    move(simulation, unit, {2000, 1200});
    finishMove(simulation, unit, {2000, 1200}, 18);
}

void narrowTurningPassage() {
    auto simulation = fixture();
    const float left = 1310, right = 1354;
    const_cast<std::vector<Obstacle>&>(simulation.obstacles()) = {
        {{left / 2, 1400}, {left / 2, 90}},
        {{(right + Simulation::WorldSize) / 2, 1400}, {(Simulation::WorldSize - right) / 2, 90}},
        {{1190, 1585}, {75, 95}}
    };
    const Id unit = simulation.debugSpawn(Kind::Worker, 0, {1332, 1180});
    move(simulation, unit, {1550, 1660});
    finishMove(simulation, unit, {1550, 1660}, 15);
}

void displacedCachedRouteRepairsBeforeContact() {
    auto simulation = fixture();
    const_cast<std::vector<Obstacle>&>(simulation.obstacles()) = {{{1600, 1200}, {100, 150}}};
    const Id unit = simulation.debugSpawn(Kind::Worker, 0, {1200, 1200});
    move(simulation, unit, {2000, 1200});
    simulation.update(Simulation::Step);
    auto& displaced = movingUnit(simulation, unit);
    check(!displaced.path.empty(), "displacement fixture obtains a cached route");
    // Model crowd separation pushing the unit to the other side of the box.
    // Static geometry and accepted destination remain unchanged.
    displaced.pos = {1550, 1400};
    displaced.stalledFor = 0;
    check(clearSegment(simulation, unit, displaced.pos, displaced.pos),
          "displaced starting position has full physical clearance");
    check(!clearSegment(simulation, unit, displaced.pos, displaced.path[displaced.pathIndex]),
          "displacement hides the cached active waypoint");
    const auto beforeSearches = simulation.navigationStats().searches;
    const auto beforeFailures = simulation.navigationStats().failures;
    simulation.update(Simulation::Step);
    check(simulation.navigationStats().searches > beforeSearches,
          "hidden cached waypoint must be repaired on the next tick before wall contact: " + state(simulation, unit, 1));
    check(simulation.find(unit)->pos.x >= 1550 - 0.02f,
          "repair must head toward the visible right side instead of the hidden old waypoint");
    check(simulation.navigationStats().failures == beforeFailures,
          "off-route repair is not a failed route or stall");
    checkActiveLeg(simulation, unit, 1);
    finishMove(simulation, unit, {2000, 1200}, 15);
}

void redirectedCommandDiscardsOldRoute() {
    auto simulation = fixture();
    const_cast<std::vector<Obstacle>&>(simulation.obstacles()) = {{{1600, 1200}, {100, 150}}};
    const Id unit = simulation.debugSpawn(Kind::Worker, 0, {1200, 1200});
    move(simulation, unit, {2000, 1200});
    for (int step = 0; step < 20; ++step) simulation.update(Simulation::Step);
    check(!simulation.find(unit)->path.empty(), "redirect fixture has an old route");
    const Vec2 before = simulation.find(unit)->pos;
    const Vec2 goal{1000, 1700};
    move(simulation, unit, goal);
    check(simulation.find(unit)->path.empty(), "new move discards the old route immediately");
    simulation.update(Simulation::Step);
    check(distance(simulation.find(unit)->pos, goal) < distance(before, goal),
          "first movement tick follows the redirected destination");
    finishMove(simulation, unit, goal, 10);
}

void saveLoadContinuesCornerRouteDeterministically() {
    auto original = fixture();
    original.debugSpawn(Kind::Headquarters, 0, {1600, 1200});
    const Id unit = original.debugSpawn(Kind::Worker, 0, {1200, 1200});
    move(original, unit, {2000, 1200});
    for (int step = 0; step < 42; ++step) original.update(Simulation::Step);
    const auto path = std::filesystem::temp_directory_path() / "cinderline-movement-following.sav";
    check(original.save(path.string()), "corner-route midpoint saves");
    Simulation restored;
    const bool loaded = restored.load(path.string());
    std::filesystem::remove(path);
    check(loaded && restored.stateHash() == original.stateHash(), "corner-route midpoint restores exactly");
    for (int step = 0; step < 360; ++step) {
        original.update(Simulation::Step);
        restored.update(Simulation::Step);
        check(original.stateHash() == restored.stateHash(), "restored route diverged from uninterrupted movement");
        checkActiveLeg(original, unit, step);
    }
    check(distance(original.find(unit)->pos, {2000, 1200}) < 24,
          "saved corner route reaches the accepted destination");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"worker box corners", [] { boxRoute(Kind::Worker); }},
        {"combat unit box corners", [] { boxRoute(Kind::Striker); }},
        {"worker circular building", [] { circleRoute(Kind::Worker); }},
        {"heavy unit circular building", [] { circleRoute(Kind::Bastion); }},
        {"narrow turning passage", narrowTurningPassage},
        {"displaced cached route repairs before contact", displacedCachedRouteRepairsBeforeContact},
        {"redirected move discards old route", redirectedCommandDiscardsOldRoute},
        {"save-load deterministic corner route", saveLoadContinuesCornerRouteDeterministically},
    };
    int failures = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
