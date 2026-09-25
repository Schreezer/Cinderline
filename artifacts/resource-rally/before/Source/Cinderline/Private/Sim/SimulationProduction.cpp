#include "Sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cinder {
namespace {
constexpr float ProductionPi = 3.14159265358979323846f;

float productionDistanceSquared(Vec2 a, Vec2 b) {
    const float x = a.x - b.x, y = a.y - b.y;
    return x * x + y * y;
}
}

void Simulation::updateProduction(Entity& producer) {
    const auto& building = definition(producer.kind);
    if (producer.progress < 1) {
        if (!constructionActive(producer.id)) return;
        const float old = producer.progress;
        const float previousMaximum = building.hp * (0.1f + old * 0.9f);
        const float constructionDamage = std::max(0.0f, previousMaximum - producer.hp);
        producer.progress = std::min(1.0f, old + Step / std::max(1.0f, productionTime(building.buildTime)));
        const float currentMaximum = producer.progress >= 1 ? building.hp
            : building.hp * (0.1f + producer.progress * 0.9f);
        producer.hp = std::max(0.0f, currentMaximum - constructionDamage);
        if (producer.progress >= 1) {
            releaseConstruction(producer);
            ++players_[producer.team].stats.built;
            if (producer.kind == Kind::Processor || producer.kind == Kind::Headquarters)
                ++players_[producer.team].stats.expansions;
            if (producer.team == 0) alert_ = std::string(building.name) + " ready.";
        }
        return;
    }
    if (producer.queue.empty()) return;
    QueueItem& queued = producer.queue.front();
    if (!queued.research && supply(producer.team) > capacity(producer.team)) return;
    queued.remaining = std::max(0.0f, queued.remaining - Step);
    if (queued.remaining > 0) return;
    const QueueItem item = queued;
    const int team = producer.team;
    const Vec2 position = producer.pos, rally = producer.rally;
    const bool producerRallyOverride = producer.rallyOverride;
    if (item.research) {
        Player& player = players_[team];
        if (item.kind == Kind::Worker) player.tier = std::min(3, player.tier + 1);
        else if (item.kind == Kind::Striker) player.weapons = std::min(3, player.weapons + 1);
        else if (item.kind == Kind::Lancer) player.armor = std::min(3, player.armor + 1);
        ++player.stats.upgrades;
        producer.queue.erase(producer.queue.begin());
        producer.repath = 0;
        producer.pathGeometry = 0;
        if (team == 0) alert_ = "Research completed.";
        return;
    }

    const auto& unit = definition(item.kind);
    // Buildings share the serialized navigation retry clock with mobile units.
    // A capped worker assignment retains its paid job and retries with backoff;
    // changed geometry bypasses that delay. step() decrements the clock.
    if (producer.repath > 0) {
        ensureNavigation();
        if (producer.pathGeometry == navigation_.geometryVersion()) return;
        producer.repath = 0;
    }
    // Select the actual formation destination before checking exit reachability.
    // The newborn is not in entities_ yet, so no self-ID needs excluding here.
    Vec2 arrival = rally;
    auto rallyAvailable = [&](Vec2 point) {
        if (point.x < unit.radius || point.y < unit.radius ||
            point.x > worldSize() - unit.radius || point.y > worldSize() - unit.radius) return false;
        if (!unit.air && blocked(point, unit.radius + 4)) return false;
        for (const auto& other : entities_) {
            if (!other.alive() || other.team != team || definition(other.kind).building ||
                other.order == Order::Gather) continue;
            const float spacing = unit.radius + definition(other.kind).radius + 20;
            if (productionDistanceSquared(point, other.goal) < spacing * spacing) return false;
        }
        return true;
    };
    bool rallyFound = rallyAvailable(arrival);
    for (int ring = 1; ring <= 15 && !rallyFound; ++ring) for (int spoke = 0; spoke < 24; ++spoke) {
        const float angle = spoke * (2 * ProductionPi / 24);
        const Vec2 point{rally.x + std::cos(angle) * ring * 64, rally.y + std::sin(angle) * ring * 64};
        if (rallyAvailable(point)) { arrival = point; rallyFound = true; break; }
    }

    if (!unit.air) ensureNavigation();
    std::vector<Vec2> exits;
    exits.reserve(unit.air ? 1 : 96);
    for (int ring = 0; ring < 6 && (!unit.air || exits.empty()); ++ring) for (int spoke = 0; spoke < 16; ++spoke) {
        const float angle = std::atan2(rally.y - position.y, rally.x - position.x) + spoke * ProductionPi / 8;
        const float reach = building.radius + unit.radius + 28 + ring * 35;
        const Vec2 point{position.x + std::cos(angle) * reach, position.y + std::sin(angle) * reach};
        if (point.x < unit.radius || point.y < unit.radius ||
            point.x > worldSize() - unit.radius || point.y > worldSize() - unit.radius) continue;
        if (!unit.air) {
            if (!navigation_.pointClear(point, unit.radius)) continue;
            bool occupied = false;
            for (const auto& other : entities_) {
                const auto& otherUnit = definition(other.kind);
                if (!other.alive() || otherUnit.building || other.kind == Kind::Resource || otherUnit.air) continue;
                const float separation = unit.radius + otherUnit.radius;
                if (productionDistanceSquared(point, other.pos) < separation * separation) { occupied = true; break; }
            }
            if (occupied) continue;
        }
        exits.push_back(point);
        if (unit.air) break;
    }
    if (exits.empty()) {
        if (team == 0) alert_ = std::string(unit.name) + " ready; clear space around " + building.name + ".";
        return; // Keep the paid item and its reserved crew until a local exit opens.
    }
    Vec2 exit = exits.front();
    Entity miningAssignment;
    bool mining = false;
    bool seekMining = item.kind == Kind::Worker && !producerRallyOverride;
    if (item.kind == Kind::Worker && producerRallyOverride && queued.assignmentCandidates.empty()) {
        Id rallyResource = 0;
        for (const auto& entity : entities_) {
            if (entity.alive() && entity.kind == Kind::Resource && entity.resource > 0 && explored(team, entity.pos) &&
                productionDistanceSquared(rally, entity.pos) <= definition(Kind::Resource).radius * definition(Kind::Resource).radius &&
                (!rallyResource || entity.id < rallyResource)) rallyResource = entity.id;
        }
        if (rallyResource) { queued.assignmentCandidates.push_back(rallyResource); seekMining = true; }
    } else if (item.kind == Kind::Worker && producerRallyOverride && !queued.assignmentCandidates.empty()) seekMining = true;
    if (seekMining) {
        miningAssignment.id = nextId_;
        miningAssignment.kind = Kind::Worker;
        miningAssignment.team = team;
        miningAssignment.pos = miningAssignment.goal = exit;
        miningAssignment.rally = rally;
        miningAssignment.hp = unit.hp;
        miningAssignment.progress = 1;
        const auto assignment = assignFreshWorkerToOre(miningAssignment, queued.assignmentCandidates, queued.assignmentCursor, exits);
        if (assignment == WorkerAssignmentResult::Deferred) return;
        if (assignment == WorkerAssignmentResult::SearchLimited) {
            producer.repath = 1.0f;
            producer.pathGeometry = navigation_.geometryVersion();
            if (team == 0) alert_ = "Drudge ready; ore route pending. Change rally to redirect.";
            return;
        }
        mining = assignment == WorkerAssignmentResult::Assigned;
        if (mining) exit = miningAssignment.pos;
    }

    NavigationResult rallyRoute;
    bool routeChecked = false;
    if (!mining && !unit.air) {
        routeChecked = true;
        if (navigation_.segmentClear(exit, arrival, unit.radius)) {
            rallyRoute.reached = true;
            rallyRoute.origin = exit;
            rallyRoute.points.push_back(arrival);
        } else {
            if (!claimNavigationSearch()) return;
            ++navigationStats_.searches;
            rallyRoute = navigation_.routeFromAny(exits, {arrival}, unit.radius);
            navigationStats_.expanded += static_cast<std::uint64_t>(std::max(0, rallyRoute.expanded));
            if (rallyRoute.reached) exit = rallyRoute.origin;
            else ++navigationStats_.failures;
        }
    }

    producer.queue.erase(producer.queue.begin());
    producer.repath = 0;
    producer.pathGeometry = 0;
    const Id id = spawn(item.kind, team, exit); // Appending invalidates producer/queued references.
    Entity* created = get(id);
    if (!created) return;
    ++players_[team].stats.produced;
    created->navigationAnchor = created->pos;
    if (mining) {
        created->order = miningAssignment.order;
        created->target = miningAssignment.target;
        created->resourceTarget = miningAssignment.resourceTarget;
        created->workTarget = miningAssignment.workTarget;
        created->workPoint = miningAssignment.workPoint;
        created->workPointValid = miningAssignment.workPointValid;
        created->path = std::move(miningAssignment.path);
        created->pathIndex = miningAssignment.pathIndex;
        created->pathGeometry = miningAssignment.pathGeometry;
    } else {
        created->order = Order::Move;
        created->goal = arrival;
        if (routeChecked) {
            created->path = std::move(rallyRoute.points);
            created->pathGeometry = navigation_.geometryVersion();
            // An unreachable distant rally must not halt otherwise usable training.
            // Preserve the goal and surface the existing NO ROUTE state at the exit.
            created->navigationExhausted = !rallyRoute.reached && !rallyRoute.exhausted;
            created->navigationFailures = rallyRoute.reached ? 0 : 1;
            created->repath = rallyRoute.exhausted ? Step : 0;
        }
    }
    if (team == 0) {
        if (created->navigationExhausted) alert_ = std::string(unit.name) + " ready; rally route blocked.";
        else if (created->kind == Kind::Worker && !mining && !producerRallyOverride)
            alert_ = "Drudge ready; no reachable ore job.";
        else alert_ = std::string(unit.name) + " ready.";
    }
}
} // namespace cinder
