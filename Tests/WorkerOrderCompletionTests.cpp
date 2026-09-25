#include "Sim/Simulation.h"

#include <cstdint>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cinder;

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

float distance(Vec2 left, Vec2 right) {
    return std::hypot(left.x - right.x, left.y - right.y);
}

bool intersects(Vec2 point, float radius, const Obstacle& obstacle) {
    const float dx = std::max(std::fabs(point.x - obstacle.center.x) - obstacle.half.x, 0.0f);
    const float dy = std::max(std::fabs(point.y - obstacle.center.y) - obstacle.half.y, 0.0f);
    return dx * dx + dy * dy < radius * radius - 0.001f;
}

Entity* edit(Simulation& simulation, Id id) {
    for (auto& entity : const_cast<std::vector<Entity>&>(simulation.entities()))
        if (entity.id == id) return &entity;
    return nullptr;
}

Simulation fixture() {
    Simulation simulation;
    // Lone-ore scenarios use the original ground around their synthetic depot.
    Config config; config.ai = false; config.mapRevision = 0;
    simulation.reset(config);
    auto& entities = const_cast<std::vector<Entity>&>(simulation.entities());
    entities.clear();
    simulation.debugSpawn(Kind::Headquarters, 0, {800, 900});
    simulation.debugSpawn(Kind::Headquarters, 1, {4400, 4400});
    simulation.debugResources(0, 10000);
    return simulation;
}

CommandResult send(Simulation& simulation, CommandType type, int team, std::vector<Id> units,
                   Vec2 point = {}, Id target = 0, Kind kind = Kind::Worker) {
    return simulation.command({type, team, std::move(units), point, target, kind, 0});
}

void advance(Simulation& simulation, float seconds) {
    const int steps = static_cast<int>(std::ceil(seconds / Simulation::Step));
    for (int step = 0; step < steps; ++step) {
        const std::uint64_t tick = simulation.tick();
        simulation.update(Simulation::Step);
        check(simulation.winner() == -1 && simulation.tick() == tick + 1,
              "fixture remains live while advancing ordinary simulation time");
    }
}

template <typename Predicate>
void advanceUntil(Simulation& simulation, float seconds, Predicate predicate, const std::string& message) {
    const int steps = static_cast<int>(std::ceil(seconds / Simulation::Step));
    for (int step = 0; step < steps && !predicate(); ++step) {
        const std::uint64_t tick = simulation.tick();
        simulation.update(Simulation::Step);
        check(simulation.winner() == -1 && simulation.tick() == tick + 1,
              "fixture remains live while waiting for ordinary simulation progress");
    }
    check(predicate(), message);
}

Vec2 placementFor(const Simulation& simulation, Kind kind, Vec2 near) {
    for (float radius = 180; radius <= 700; radius += 35) for (int spoke = 0; spoke < 48; ++spoke) {
        const float angle = 6.283185307f * spoke / 48;
        const Vec2 site{near.x + std::cos(angle) * radius, near.y + std::sin(angle) * radius};
        if (simulation.canPlace(0, kind, site)) return site;
    }
    throw std::runtime_error("fixture cannot find a legal headquarters foundation site");
}

Id first(const Simulation& simulation, int team, Kind kind) {
    for (const auto& entity : simulation.entities())
        if (entity.alive() && entity.team == team && entity.kind == kind) return entity.id;
    return 0;
}

void checkClearGroundSpawn(const Simulation& simulation, Id id) {
    const Entity* entity = simulation.find(id);
    check(entity && entity->alive() && !definition(entity->kind).air,
          "combat fixture spawns a living ground attacker");
    const float radius = definition(entity->kind).radius;
    check(entity->pos.x >= radius && entity->pos.y >= radius
          && entity->pos.x <= simulation.worldSize() - radius && entity->pos.y <= simulation.worldSize() - radius,
          "combat fixture attacker starts inside the battlefield");
    for (const Obstacle& obstacle : simulation.obstacles())
        check(!intersects(entity->pos, radius, obstacle), "combat fixture attacker starts outside terrain");
    for (const Entity& other : simulation.entities()) {
        if (!other.alive() || other.id == id || (!definition(other.kind).building && other.kind != Kind::Resource)) continue;
        check(distance(entity->pos, other.pos) + 0.001f >= radius + definition(other.kind).radius,
              "combat fixture attacker starts clear of every building and resource");
    }
}

void exhaustedOreFinishesAtDeliveryPoint() {
    Simulation simulation = fixture();
    const Id headquarters = first(simulation, 0, Kind::Headquarters);
    const Id worker = simulation.debugSpawn(Kind::Worker, 0, {1200, 1050});
    const Id ore = simulation.debugSpawn(Kind::Resource, -1, {1500, 1050});
    edit(simulation, ore)->resource = 3;
    simulation.update(Simulation::Step); // Establish explored ore through ordinary vision.

    const Vec2 oldMoveGoal{3000, 1050};
    check(send(simulation, CommandType::Move, 0, {worker}, oldMoveGoal).accepted,
          "worker accepts a prior direct movement order");
    check(send(simulation, CommandType::Gather, 0, {worker}, {}, ore).accepted,
          "worker accepts the lone explored ore order");

    advanceUntil(simulation, 35, [&] {
        const Entity* entity = simulation.find(worker);
        return entity && simulation.players()[0].stats.gathered == 3 && entity->carried == 0;
    }, "worker normally harvests and delivers the final three ore");
    simulation.update(Simulation::Step); // The following economy tick resolves the exhausted Gather order.

    const Entity* delivered = simulation.find(worker);
    const Vec2 deliveryPosition = delivered->pos;
    check(delivered->order == Order::Idle && delivered->target == 0 && delivered->resourceTarget == 0,
          "exhausted lone ore finishes the Gather order instead of retaining a stale target");
    check(distance(delivered->goal, deliveryPosition) < 0.01f,
          "finished Gather replaces a prior Move goal with the actual delivery position");
    check(distance(delivered->goal, oldMoveGoal) > 200,
          "finished Gather does not retain the old Move destination");
    const int credited = simulation.players()[0].stats.gathered;
    advance(simulation, 1.0f);
    const Entity* stable = simulation.find(worker);
    check(stable && stable->order == Order::Idle && distance(stable->pos, deliveryPosition) < 0.01f,
          "finished worker remains at the delivery point instead of drifting toward a stale goal");
    check(stable->carried == 0 && credited == 3 && simulation.players()[0].stats.gathered == credited,
          "final ore is credited exactly once with no remaining cargo");
    check(simulation.find(headquarters) && simulation.find(headquarters)->alive(),
          "lone-ore fixture remains an active ordinary match");
}

void pendingCargoWaitsForReplacementDepotAcrossSaveLoad() {
    Simulation simulation = fixture();
    const Id originalHeadquarters = first(simulation, 0, Kind::Headquarters);
    const Id builder = simulation.debugSpawn(Kind::Worker, 0, {1050, 900});
    const Id gatherer = simulation.debugSpawn(Kind::Worker, 0, {1200, 1250});
    const Id ore = simulation.debugSpawn(Kind::Resource, -1, {1270, 1250});
    edit(simulation, ore)->resource = 3;
    edit(simulation, originalHeadquarters)->hp = 1;
    simulation.update(Simulation::Step); // Establish ordinary vision before orders.

    const Vec2 backupSite = placementFor(simulation, Kind::Headquarters, simulation.find(builder)->pos);
    const CommandResult build = send(simulation, CommandType::Build, 0, {builder}, backupSite, 0, Kind::Headquarters);
    check(build.accepted, "builder pays for an unfinished replacement Anchor: " + build.message);
    const Id backupHeadquarters = [&] {
        for (const auto& entity : simulation.entities())
            if (entity.alive() && entity.team == 0 && entity.kind == Kind::Headquarters && entity.id != originalHeadquarters)
                return entity.id;
        return Id(0);
    }();
    check(backupHeadquarters && simulation.find(backupHeadquarters)->progress < 1,
          "replacement Anchor exists as an unfinished paid foundation");
    check(send(simulation, CommandType::Stop, 0, {builder}).accepted,
          "builder pauses the replacement Anchor before it becomes a depot");

    check(send(simulation, CommandType::Gather, 0, {gatherer}, {}, ore).accepted,
          "gatherer accepts the small real ore job");
    advanceUntil(simulation, 6, [&] {
        const Entity* entity = simulation.find(gatherer);
        return entity && entity->carried > 0 && entity->returning;
    }, "gatherer physically harvests cargo before the original Anchor falls");
    const float carried = simulation.find(gatherer)->carried;
    check(carried == 3 && simulation.find(ore)->resource == 0,
          "only the real final ore load is pending return");

    const Id mortar = simulation.debugSpawn(Kind::Mortar, 1, {900, 1500});
    checkClearGroundSpawn(simulation, mortar);
    check(send(simulation, CommandType::Attack, 1, {mortar}, {}, originalHeadquarters).accepted,
          "enemy Mortar receives an ordinary attack command against the original Anchor");
    advanceUntil(simulation, 5, [&] {
        const Entity* anchor = simulation.find(originalHeadquarters);
        return !anchor || !anchor->alive();
    }, "Mortar destroys the original Anchor through normal combat");
    check(simulation.winner() == -1 && simulation.find(backupHeadquarters)->alive()
          && simulation.find(backupHeadquarters)->progress < 1,
          "unfinished paid Anchor keeps the match active while no completed depot exists");
    check(send(simulation, CommandType::Move, 1, {mortar}, {4200, 3800}).accepted,
          "enemy attacker leaves the recovery fixture after the original Anchor is destroyed");

    simulation.update(Simulation::Step);
    const Entity* pending = simulation.find(gatherer);
    const Vec2 pendingPosition = pending->pos;
    check(pending && pending->order == Order::Gather && pending->returning && pending->carried == carried
          && pending->navigationExhausted,
          "cargo stays in a pending Gather return while no completed depot exists: order=" +
          std::to_string(pending ? static_cast<int>(pending->order) : -1) + " returning=" +
          std::to_string(pending && pending->returning) + " carried=" +
          std::to_string(pending ? pending->carried : -1));
    advance(simulation, 1.0f);
    const Entity* stillPending = simulation.find(gatherer);
    check(stillPending && distance(stillPending->pos, pendingPosition) < 0.01f,
          "pending cargo does not drift while the only replacement Anchor is unfinished");

    const std::filesystem::path savePath = std::filesystem::temp_directory_path() / "cinderline-worker-pending-return.sav";
    check(simulation.save(savePath.string()), "pending cargo state saves");
    Simulation loaded;
    check(loaded.load(savePath.string()) && loaded.stateHash() == simulation.stateHash(),
          "pending cargo return loads without changing authoritative state");
    std::filesystem::remove(savePath);

    for (int step = 0; step < 20; ++step) {
        const std::uint64_t simulationTick = simulation.tick();
        const std::uint64_t loadedTick = loaded.tick();
        simulation.update(Simulation::Step);
        loaded.update(Simulation::Step);
        check(simulation.winner() == -1 && loaded.winner() == -1
              && simulation.tick() == simulationTick + 1 && loaded.tick() == loadedTick + 1
              && simulation.stateHash() == loaded.stateHash(),
              "saved pending return stays live and deterministic before a depot is restored");
    }

    check(send(loaded, CommandType::ResumeConstruction, 0, {builder}, {}, backupHeadquarters).accepted,
          "builder resumes the already paid replacement Anchor after reload");
    advanceUntil(loaded, definition(Kind::Headquarters).buildTime + 20, [&] {
        const Entity* depot = loaded.find(backupHeadquarters);
        return depot && depot->progress >= 1;
    }, "replacement Anchor completes through ordinary construction time");
    advanceUntil(loaded, 30, [&] {
        const Entity* entity = loaded.find(gatherer);
        return entity && entity->carried == 0 && loaded.players()[0].stats.gathered == 3;
    }, "pending worker delivers preserved cargo once the replacement depot completes");
    advance(loaded, Simulation::Step); // Resolve the exhausted Gather order after the delivery tick.
    const Entity* delivered = loaded.find(gatherer);
    check(delivered->order == Order::Idle && delivered->resourceTarget == 0,
          "delivery completes the old Gather order without retaining an ore target");
    check(loaded.recording().size() == simulation.recording().size() + 1,
          "only the explicit ResumeConstruction command follows the saved pending return");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, void(*)()>> tests{
        {"exhausted ore finishes at delivery point", exhaustedOreFinishesAtDeliveryPoint},
        {"pending cargo waits for replacement depot across save-load", pendingCargoWaitsForReplacementDepotAcrossSaveLoad},
    };
    int failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    std::cout << "RESULT passed=" << tests.size() - failed << " failed=" << failed << '\n';
    return failed ? 1 : 0;
}
