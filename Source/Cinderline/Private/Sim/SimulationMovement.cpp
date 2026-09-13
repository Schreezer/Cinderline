#include "Sim/Simulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace cinder {
namespace {

constexpr float BucketSize = 128.0f;
constexpr float ArrivalDistance = 2.0f;
constexpr float WaypointDistance = 18.0f;
constexpr float ProgressDistance = 4.0f;
constexpr float StallWindow = 0.8f;
constexpr float TrafficYieldDuration = 1.0f;
constexpr int MaxNavigationFailures = 5;
constexpr int MaxRouteRequestsPerStep = 16;

float lengthSquared(Vec2 value) {
    return value.x * value.x + value.y * value.y;
}

float distanceSquared(Vec2 left, Vec2 right) {
    return lengthSquared({left.x - right.x, left.y - right.y});
}

float distanceBetween(Vec2 left, Vec2 right) {
    return std::sqrt(distanceSquared(left, right));
}

Vec2 addPoints(Vec2 left, Vec2 right) {
    return {left.x + right.x, left.y + right.y};
}

Vec2 subtractPoints(Vec2 left, Vec2 right) {
    return {left.x - right.x, left.y - right.y};
}

Vec2 scalePoint(Vec2 point, float amount) {
    return {point.x * amount, point.y * amount};
}

Vec2 normalizePoint(Vec2 point) {
    const float length = std::sqrt(lengthSquared(point));
    return length > 0.0001f ? scalePoint(point, 1.0f / length) : Vec2{1.0f, 0.0f};
}

Vec2 clampToWorld(float worldSize, Vec2 point, float radius) {
    return {
        std::clamp(point.x, radius, worldSize - radius),
        std::clamp(point.y, radius, worldSize - radius),
    };
}

bool finitePoint(Vec2 point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool mobile(const Entity& entity) {
    const Definition& unit = definition(entity.kind);
    return entity.alive() && !unit.building && entity.kind != Kind::Resource;
}

bool anchored(const Entity& entity) {
    return entity.order == Order::Hold ||
        (entity.order == Order::Defend && distanceSquared(entity.pos, entity.goal) <= 5.0f * 5.0f);
}

std::uint64_t bucketKey(int x, int y) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
           static_cast<std::uint32_t>(y);
}

int bucketCoordinate(float value) {
    return static_cast<int>(std::floor(value / BucketSize));
}

float segmentDistanceSquared(Vec2 from, Vec2 to, Vec2 point) {
    const Vec2 segment = subtractPoints(to, from);
    const float segmentLength = lengthSquared(segment);
    if (segmentLength <= 0.0001f) {
        return distanceSquared(from, point);
    }
    const Vec2 offset = subtractPoints(point, from);
    const float t = std::clamp(
        (offset.x * segment.x + offset.y * segment.y) / segmentLength, 0.0f, 1.0f);
    return distanceSquared(addPoints(from, scalePoint(segment, t)), point);
}

float retryDelay(int failures) {
    const int exponent = std::clamp(failures - 1, 0, 4);
    return std::min(1.6f, 0.15f * static_cast<float>(1 << exponent));
}

} // namespace

void Simulation::beginMovementStep() {
    movementBuckets_.clear();
    movementBuckets_.reserve(entities_.size());
    ensureNavigation();

    for (std::size_t index = 0; index < entities_.size(); ++index) {
        Entity& entity = entities_[index];
        if (!mobile(entity)) {
            continue;
        }
        entity.yieldFor = std::max(0.0f, entity.yieldFor - Step);
        movementBuckets_[bucketKey(
            bucketCoordinate(entity.pos.x), bucketCoordinate(entity.pos.y))].push_back(index);
    }
}

bool Simulation::yieldAtWork(Entity& entity, Vec2 center) {
    if (entity.yieldFor <= 0.0f || !mobile(entity) || definition(entity.kind).air) {
        return false;
    }
    ensureNavigation();
    const Definition& unit = definition(entity.kind);
    const Vec2 outward = normalizePoint(subtractPoints(entity.pos, center));
    const Vec2 perpendicular{-outward.y, outward.x};

    auto dynamicallyClear = [&](Vec2 candidate) {
        bool clear = true;
        const int cellX = bucketCoordinate(entity.pos.x);
        const int cellY = bucketCoordinate(entity.pos.y);
        for (int dy = -1; dy <= 1 && clear; ++dy) {
            for (int dx = -1; dx <= 1 && clear; ++dx) {
                const auto bucket = movementBuckets_.find(bucketKey(cellX + dx, cellY + dy));
                if (bucket == movementBuckets_.end()) {
                    continue;
                }
                for (std::size_t index : bucket->second) {
                    if (index >= entities_.size()) {
                        continue;
                    }
                    const Entity& other = entities_[index];
                    if (other.id == entity.id || !mobile(other) || definition(other.kind).air) {
                        continue;
                    }
                    const float separation = unit.radius + definition(other.kind).radius;
                    if (segmentDistanceSquared(entity.pos, candidate, other.pos) <
                        separation * separation) {
                        clear = false;
                        break;
                    }
                }
            }
        }
        return clear;
    };

    const float travel = unit.speed * Step;
    for (float side : std::array<float, 5>{{0.0f, 0.35f, -0.35f, 0.75f, -0.75f}}) {
        const Vec2 direction = normalizePoint(addPoints(outward, scalePoint(perpendicular, side)));
        for (float fraction : std::array<float, 3>{{1.0f, 0.5f, 0.25f}}) {
            const Vec2 candidate = clampToWorld(worldSize(), 
                addPoints(entity.pos, scalePoint(direction, travel * fraction)), unit.radius);
            if (!navigation_.segmentClear(entity.pos, candidate, unit.radius, entity.id) ||
                !dynamicallyClear(candidate)) {
                continue;
            }
            entity.pos = candidate;
            entity.facing = std::atan2(direction.y, direction.x);
            entity.navigationAnchor = candidate;
            entity.stalledFor = 0.0f;
            entity.navigationBestDistance = 0.0f;
            return true;
        }
    }
    return true;
}

bool Simulation::claimNavigationSearch() {
    if (!isStepping_ || movementRouteRequests_ < MaxRouteRequestsPerStep) {
        ++movementRouteRequests_;
        return true;
    }
    ++navigationStats_.budgetDeferrals;
    return false;
}

NavigationResult Simulation::findRoute(Entity& entity, const std::vector<Vec2>& goals) {
    ensureNavigation();
    NavigationResult result;
    if (goals.empty()) {
        entity.navigationExhausted = true;
        entity.navigationFailures = std::min(
            MaxNavigationFailures, entity.navigationFailures + 1);
        ++navigationStats_.failures;
        return result;
    }
    if (!claimNavigationSearch()) {
        result.exhausted = true;
        entity.repath = std::max(entity.repath, Step);
        return result;
    }

    ++navigationStats_.searches;
    result = navigation_.route(entity.pos, goals, definition(entity.kind).radius, entity.id);
    navigationStats_.expanded += static_cast<std::uint64_t>(std::max(0, result.expanded));
    entity.pathGeometry = navigation_.geometryVersion();

    if (result.reached) {
        entity.navigationExhausted = false;
        entity.repath = 0.0f;
        entity.stalledFor = 0.0f;
        entity.navigationAnchor = entity.pos;
        entity.navigationBestDistance = 0.0f;
        return result;
    }

    ++navigationStats_.failures;
    entity.navigationFailures = std::min(
        MaxNavigationFailures, entity.navigationFailures + 1);
    entity.repath = std::max(entity.repath, retryDelay(entity.navigationFailures));
    // Only a complete search can prove these goals unreachable. Hitting the
    // expansion cap keeps retrying with bounded backoff; it must not surface as
    // a permanent NO ROUTE state merely because several attempts were capped.
    entity.navigationExhausted = !result.exhausted;
    return result;
}

bool Simulation::planPath(Entity& entity, Vec2 destination) {
    entity.path.clear();
    entity.pathIndex = 0;
    const float radius = definition(entity.kind).radius;
    destination = clampToWorld(worldSize(), destination, radius);
    const NavigationResult result = findRoute(entity, {destination});
    if (!result.reached) {
        return false;
    }
    entity.path = result.points;
    entity.pathIndex = 0;
    return true;
}

bool Simulation::moveToward(Entity& entity, Vec2 destination) {
    const Definition& unit = definition(entity.kind);
    if (unit.speed <= 0.0f || !finitePoint(destination)) {
        return false;
    }

    destination = clampToWorld(worldSize(), destination, unit.radius);
    Vec2 destinationDelta = subtractPoints(destination, entity.pos);
    if (lengthSquared(destinationDelta) < ArrivalDistance * ArrivalDistance) {
        entity.stalledFor = 0.0f;
        entity.navigationAnchor = entity.pos;
        entity.navigationBestDistance = 0.0f;
        return true;
    }

    if (unit.air) {
        const Vec2 direction = normalizePoint(destinationDelta);
        const float travel = std::min(unit.speed * Step, std::sqrt(lengthSquared(destinationDelta)));
        entity.pos = clampToWorld(worldSize(), addPoints(entity.pos, scalePoint(direction, travel)), unit.radius);
        entity.facing = std::atan2(direction.y, direction.x);
        return true;
    }

    if (entity.order == Order::Idle && entity.target == 0 && entity.yieldFor > 0.0f) {
        return true;
    }

    ensureNavigation();
    const std::uint64_t geometry = navigation_.geometryVersion();
    if (entity.pathGeometry != 0 && entity.pathGeometry != geometry) {
        entity.path.clear();
        entity.pathIndex = 0;
        entity.repath = 0.0f;
        entity.navigationExhausted = false;
        entity.navigationFailures = 0;
        entity.workPointValid = false;
        entity.stalledFor = 0.0f;
        entity.navigationAnchor = entity.pos;
        entity.navigationBestDistance = 0.0f;
    }

    const bool destinationChanged = !entity.path.empty() &&
        distanceSquared(entity.path.back(), destination) > 110.0f * 110.0f;
    if (destinationChanged) {
        entity.path.clear();
        entity.pathIndex = 0;
        entity.repath = 0.0f;
        entity.navigationExhausted = false;
        entity.navigationFailures = 0;
        entity.stalledFor = 0.0f;
        entity.navigationAnchor = entity.pos;
        entity.navigationBestDistance = 0.0f;
    }

    if (entity.path.empty() || entity.pathIndex >= static_cast<int>(entity.path.size())) {
        if (entity.navigationExhausted && entity.pathGeometry == geometry) {
            return false;
        }
        if (entity.repath > 0.0f) {
            return true;
        }
        if (!planPath(entity, destination)) {
            return !entity.navigationExhausted;
        }
        if (entity.path.empty()) {
            return true;
        }
    }

    bool advancedWaypoint = false;
    while (entity.pathIndex < static_cast<int>(entity.path.size())) {
        const bool finalWaypoint = entity.pathIndex + 1 == static_cast<int>(entity.path.size());
        const float arrival = finalWaypoint ? ArrivalDistance : WaypointDistance;
        if (distanceSquared(entity.pos, entity.path[entity.pathIndex]) >= arrival * arrival) {
            break;
        }
        ++entity.pathIndex;
        advancedWaypoint = true;
    }
    if (entity.pathIndex >= static_cast<int>(entity.path.size())) {
        entity.path.clear();
        entity.pathIndex = 0;
        entity.repath = 0.0f;
        entity.stalledFor = 0.0f;
        entity.navigationAnchor = entity.pos;
        entity.navigationBestDistance = 0.0f;
        entity.navigationFailures = std::max(0, entity.navigationFailures - 1);
        return true;
    }

    const Vec2 waypoint = entity.path[entity.pathIndex];
    float remainingDistance = distanceBetween(entity.pos, waypoint);
    for (int index = entity.pathIndex + 1; index < static_cast<int>(entity.path.size()); ++index) {
        remainingDistance += distanceBetween(entity.path[index - 1], entity.path[index]);
    }
    const bool trafficYielding = entity.yieldFor > 0.0f && entity.order != Order::Idle;
    if (advancedWaypoint || entity.navigationBestDistance <= 0.0f || trafficYielding) {
        entity.navigationAnchor = entity.pos;
        entity.stalledFor = 0.0f;
        entity.navigationBestDistance = remainingDistance;
        if (advancedWaypoint) {
            entity.navigationFailures = std::max(0, entity.navigationFailures - 1);
        }
    } else {
        if (entity.navigationBestDistance - remainingDistance >= ProgressDistance) {
            entity.navigationAnchor = entity.pos;
            entity.stalledFor = 0.0f;
            entity.navigationBestDistance = remainingDistance;
        } else {
            entity.stalledFor += Step;
        }
    }

    if (entity.stalledFor >= StallWindow) {
        const int previousFailures = entity.navigationFailures;
        entity.navigationFailures = std::min(
            MaxNavigationFailures, entity.navigationFailures + 1);
        const bool workRoute = entity.workPointValid ||
            entity.order == Order::Gather || entity.order == Order::Construct;
        if (workRoute) {
            entity.workPointValid = false;
        }
        // Traffic is absent from the static route graph. Keep a still-valid
        // route through short traffic stalls and try local passing first. One
        // bounded replan checks for a better static approach after repeated
        // failures; work orders also re-evaluate their perimeter slot.
        const bool replan = workRoute || entity.navigationFailures == 3;
        if (replan) {
            entity.path.clear();
            entity.pathIndex = 0;
            entity.repath = retryDelay(entity.navigationFailures);
        }
        entity.navigationExhausted = false;
        entity.stalledFor = 0.0f;
        entity.navigationAnchor = entity.pos;
        if (replan) {
            entity.navigationBestDistance = 0.0f;
            entity.avoidanceSide = entity.avoidanceSide == 0 ? -1 : -entity.avoidanceSide;
        }
        if (entity.navigationFailures > previousFailures) {
            ++navigationStats_.failures;
        }
        return true;
    }

    Vec2 direction = normalizePoint(subtractPoints(waypoint, entity.pos));
    const float stepDistance = std::min(unit.speed * Step, distanceBetween(entity.pos, waypoint));
    if (stepDistance <= 0.0001f) {
        return true;
    }

    auto visitNeighbors = [&](Vec2 point, const auto& visitor) {
        const int cellX = bucketCoordinate(point.x);
        const int cellY = bucketCoordinate(point.y);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const auto bucket = movementBuckets_.find(bucketKey(cellX + dx, cellY + dy));
                if (bucket == movementBuckets_.end()) {
                    continue;
                }
                for (std::size_t index : bucket->second) {
                    if (index < entities_.size()) {
                        visitor(entities_[index]);
                    }
                }
            }
        }
    };

    Entity* nearestBlocker = nullptr;
    float nearestBlockerDistance = std::numeric_limits<float>::max();
    visitNeighbors(entity.pos, [&](Entity& other) {
        if (other.id == entity.id || !mobile(other) || definition(other.kind).air) {
            return;
        }
        const float combined = unit.radius + definition(other.kind).radius + 3.0f;
        const float along = segmentDistanceSquared(
            entity.pos, addPoints(entity.pos, scalePoint(direction, std::max(stepDistance, 72.0f))), other.pos);
        const Vec2 offset = subtractPoints(other.pos, entity.pos);
        const float forward = offset.x * direction.x + offset.y * direction.y;
        if (forward > -combined * 0.25f && forward < 84.0f && along < combined * combined &&
            forward < nearestBlockerDistance) {
            nearestBlocker = &other;
            nearestBlockerDistance = forward;
        }
    });
    if (nearestBlocker) {
        // Static routing cannot improve a route that is waiting on nearby
        // traffic. Give local avoidance time to resolve it without treating
        // lateral passing or queueing as failed route progress.
        entity.stalledFor = 0.0f;
        entity.navigationBestDistance = remainingDistance;
    }
    const bool yieldableIdle = nearestBlocker && nearestBlocker->order == Order::Idle &&
        nearestBlocker->target == 0;
    const bool yieldableWorker = nearestBlocker && nearestBlocker->order == Order::Gather &&
        !nearestBlocker->returning && nearestBlocker->workPointValid &&
        distanceSquared(nearestBlocker->pos, nearestBlocker->workPoint) < 20.0f * 20.0f;
    if (nearestBlocker && nearestBlocker->team == entity.team &&
        (yieldableIdle || yieldableWorker) &&
        nearestBlocker->yieldFor <= 0.0f) {
        const Vec2 perpendicular{-direction.y, direction.x};
        const Vec2 offset = subtractPoints(nearestBlocker->pos, entity.pos);
        const float lateral = offset.x * perpendicular.x + offset.y * perpendicular.y;
        const int yieldSide = std::fabs(lateral) > 0.1f
            ? (lateral > 0.0f ? 1 : -1)
            : ((nearestBlocker->id & 1U) != 0U ? 1 : -1);
        const Vec2 yieldDirection = scalePoint(perpendicular, static_cast<float>(yieldSide));
        const Definition& otherUnit = definition(nearestBlocker->kind);
        const Vec2 yielded = clampToWorld(worldSize(), 
            addPoints(nearestBlocker->pos, scalePoint(yieldDirection, std::min(6.0f, otherUnit.speed * Step))),
            otherUnit.radius);
        if (navigation_.segmentClear(
                nearestBlocker->pos, yielded, otherUnit.radius, nearestBlocker->id)) {
            nearestBlocker->pos = yielded;
            nearestBlocker->yieldFor = 0.35f;
            nearestBlocker->navigationAnchor = yielded;
            nearestBlocker->stalledFor = 0.0f;
        }
    }

    auto dynamicallyClear = [&](Vec2 candidate) {
        bool clear = true;
        visitNeighbors(entity.pos, [&](const Entity& other) {
            if (!clear || other.id == entity.id || !mobile(other) || definition(other.kind).air) {
                return;
            }
            const float physicalRadius = unit.radius + definition(other.kind).radius;
            const bool compressibleFriendly = other.team == entity.team &&
                !anchored(other) && !anchored(entity);
            // A moving friendly queue may compress slightly during the movement
            // phase; finishMovementStep restores full separation. The swept
            // check still prevents one unit from crossing through another.
            const float combined = compressibleFriendly ? physicalRadius * 0.35f : physicalRadius;
            const float before = distanceSquared(entity.pos, other.pos);
            const float after = distanceSquared(candidate, other.pos);
            if (before < combined * combined && after > before + 0.01f) {
                return;
            }
            if (segmentDistanceSquared(entity.pos, candidate, other.pos) < combined * combined) {
                clear = false;
            }
        });
        return clear;
    };

    auto tryDirection = [&](Vec2 candidateDirection, float travel) {
        candidateDirection = normalizePoint(candidateDirection);
        const Vec2 candidate = clampToWorld(worldSize(), 
            addPoints(entity.pos, scalePoint(candidateDirection, travel)), unit.radius);
        if (!navigation_.segmentClear(entity.pos, candidate, unit.radius, entity.id) ||
            !dynamicallyClear(candidate)) {
            return false;
        }
        entity.pos = candidate;
        direction = candidateDirection;
        return true;
    };

    Vec2 blockerDirection{};
    bool opposingTraffic = false;
    bool narrowPassage = false;
    if (nearestBlocker) {
        blockerDirection = {
            std::cos(nearestBlocker->facing), std::sin(nearestBlocker->facing)};
        if (nearestBlocker->pathIndex >= 0 &&
            nearestBlocker->pathIndex < static_cast<int>(nearestBlocker->path.size())) {
            blockerDirection = normalizePoint(subtractPoints(
                nearestBlocker->path[nearestBlocker->pathIndex], nearestBlocker->pos));
        } else if (nearestBlocker->workPointValid) {
            blockerDirection = normalizePoint(subtractPoints(
                nearestBlocker->workPoint, nearestBlocker->pos));
        }
        opposingTraffic = nearestBlocker->order != Order::Idle &&
            direction.x * blockerDirection.x + direction.y * blockerDirection.y < -0.35f;
        const Vec2 crossingPerpendicular{-direction.y, direction.x};
        const float crossingProbe = unit.radius + definition(nearestBlocker->kind).radius + 8.0f;
        const Vec2 crossingLeft = clampToWorld(worldSize(), 
            addPoints(entity.pos, scalePoint(crossingPerpendicular, crossingProbe)), unit.radius);
        const Vec2 crossingRight = clampToWorld(worldSize(), 
            addPoints(entity.pos, scalePoint(crossingPerpendicular, -crossingProbe)), unit.radius);
        narrowPassage =
            !navigation_.segmentClear(entity.pos, crossingLeft, unit.radius, entity.id) &&
            !navigation_.segmentClear(entity.pos, crossingRight, unit.radius, entity.id);
    }

    if (trafficYielding && narrowPassage && !anchored(entity)) {
        // Pass the yield request back through a queue. Without this, the first
        // unit can have room behind it in principle but remain pinned by the
        // next unit, which never sees the oncoming traffic itself.
        visitNeighbors(entity.pos, [&](Entity& other) {
            if (other.id == entity.id || other.team != entity.team || !mobile(other) ||
                definition(other.kind).air ||
                anchored(other) ||
                (other.order == Order::Gather && other.returning)) {
                return;
            }
            const Vec2 offset = subtractPoints(other.pos, entity.pos);
            const Vec2 perpendicular{-direction.y, direction.x};
            const float behind = offset.x * direction.x + offset.y * direction.y;
            const float lateral = std::fabs(offset.x * perpendicular.x + offset.y * perpendicular.y);
            const float combined = unit.radius + definition(other.kind).radius + 6.0f;
            if (behind < 0.0f && behind > -72.0f && lateral < combined) {
                other.yieldFor = std::max(other.yieldFor, TrafficYieldDuration);
            }
        });
        bool backedOut = false;
        const Vec2 backward = scalePoint(direction, -1.0f);
        const Vec2 perpendicular{-direction.y, direction.x};
        constexpr std::array<float, 4> backSteering{{0.0f, 0.2f, 0.45f, 0.8f}};
        for (float amount : backSteering) {
            for (int side : std::array<int, 2>{{-1, 1}}) {
                const Vec2 escape = addPoints(backward, scalePoint(perpendicular, amount * side));
                for (float fraction : std::array<float, 4>{{1.0f, 0.5f, 0.25f, 0.125f}}) {
                    if (tryDirection(escape, stepDistance * fraction)) {
                        backedOut = true;
                        break;
                    }
                }
                if (backedOut) {
                    break;
                }
            }
            if (backedOut) {
                break;
            }
        }
        if (backedOut) {
            entity.facing = std::atan2(direction.y, direction.x);
        } else {
            // At a work perimeter the lower-priority unit can be pinned against
            // the structure it just serviced. Hand the yield to the approaching
            // unit so the departing unit can leave the perimeter.
            entity.yieldFor = 0.0f;
            if (nearestBlocker && nearestBlocker->team == entity.team &&
                !anchored(*nearestBlocker)) {
                nearestBlocker->yieldFor = std::max(
                    nearestBlocker->yieldFor, TrafficYieldDuration);
            }
        }
        return true;
    }

    bool moved = false;
    if (opposingTraffic && !narrowPassage) {
        const Vec2 perpendicular{-direction.y, direction.x};
        const int side = entity.avoidanceSide == 0 ? -1 : entity.avoidanceSide;
        entity.avoidanceSide = side;
        for (float bias : std::array<float, 2>{{0.3f, 0.55f}}) {
            if (tryDirection(
                    addPoints(direction, scalePoint(perpendicular, side * bias)), stepDistance)) {
                moved = true;
                break;
            }
        }
    }
    for (float fraction : std::array<float, 4>{{1.0f, 0.5f, 0.25f, 0.125f}}) {
        if (moved) {
            break;
        }
        if (tryDirection(direction, stepDistance * fraction)) {
            moved = true;
            break;
        }
    }
    bool wideOpposingTraffic = false;
    if (!moved && nearestBlocker) {
        const bool entityReturning = entity.order == Order::Gather && entity.returning;
        const bool blockerReturning = nearestBlocker->order == Order::Gather && nearestBlocker->returning;
        auto canBackOut = [&](const Entity& candidate, Vec2 intent) {
            const Definition& candidateUnit = definition(candidate.kind);
            const Vec2 backward = scalePoint(intent, -1.0f);
            const Vec2 perpendicular{-intent.y, intent.x};
            for (float amount : std::array<float, 4>{{0.0f, 0.2f, 0.45f, 0.8f}}) {
                for (int side : std::array<int, 2>{{-1, 1}}) {
                    const Vec2 escape = normalizePoint(
                        addPoints(backward, scalePoint(perpendicular, amount * side)));
                    const Vec2 candidatePoint = clampToWorld(worldSize(), 
                        addPoints(candidate.pos, scalePoint(escape, 4.0f)), candidateUnit.radius);
                    if (navigation_.segmentClear(
                            candidate.pos, candidatePoint, candidateUnit.radius, candidate.id)) {
                        return true;
                    }
                }
            }
            return false;
        };
        const bool entityCanBackOut = canBackOut(entity, direction);
        const bool blockerCanBackOut = canBackOut(*nearestBlocker, blockerDirection);
        wideOpposingTraffic = opposingTraffic && !narrowPassage;
        const bool entityConstructing = entity.order == Order::Construct;
        const bool blockerConstructing = nearestBlocker->order == Order::Construct;
        const bool entityHasPriority = entityCanBackOut != blockerCanBackOut
            ? !entityCanBackOut
            : entityConstructing != blockerConstructing
                ? entityConstructing
            : entityReturning != blockerReturning
                ? entityReturning
                : entity.id < nearestBlocker->id;
        if (opposingTraffic && narrowPassage && entityHasPriority && entity.team == nearestBlocker->team &&
            !anchored(*nearestBlocker) && !blockerReturning) {
            nearestBlocker->yieldFor = std::max(
                nearestBlocker->yieldFor, TrafficYieldDuration);
        }
        if (opposingTraffic && narrowPassage && !entityHasPriority && !anchored(*nearestBlocker)) {
            // In a one-unit passage one deterministic unit backs out, allowing
            // the other to clear the passage instead of both changing sides.
            for (float fraction : std::array<float, 4>{{1.0f, 0.5f, 0.25f, 0.125f}}) {
                if (tryDirection(scalePoint(direction, -1.0f), stepDistance * fraction)) {
                    moved = true;
                    break;
                }
            }
            if (moved) {
                entity.yieldFor = TrafficYieldDuration;
            }
        }
    }

    if (!moved) {
        const Vec2 perpendicular{-direction.y, direction.x};
        int side = entity.avoidanceSide;
        if (side == 0) {
            side = -1;
            entity.avoidanceSide = side;
        }
        if (wideOpposingTraffic) {
            for (float fraction : std::array<float, 3>{{1.0f, 0.5f, 0.25f}}) {
                if (tryDirection(
                        scalePoint(perpendicular, static_cast<float>(side)),
                        stepDistance * fraction)) {
                    moved = true;
                    break;
                }
            }
        }
        constexpr std::array<float, 4> steering{{0.55f, 1.0f, 1.65f, 4.0f}};
        for (float amount : steering) {
            if (moved) {
                break;
            }
            if (tryDirection(addPoints(direction, scalePoint(perpendicular, side * amount)), stepDistance)) {
                moved = true;
                break;
            }
        }
        if (!moved) {
            for (float amount : steering) {
                if (tryDirection(addPoints(direction, scalePoint(perpendicular, -side * amount)), stepDistance)) {
                    moved = true;
                    break;
                }
            }
        }
        if (!moved) {
            // At contact, every direction with a forward component can still
            // reduce separation. A short pure lateral move creates enough room
            // for the next step without crossing the blocking unit.
            for (int lateralSide : std::array<int, 2>{{side, -side}}) {
                for (float fraction : std::array<float, 3>{{1.0f, 0.5f, 0.25f}}) {
                    if (tryDirection(
                            scalePoint(perpendicular, static_cast<float>(lateralSide)),
                            stepDistance * fraction)) {
                        entity.avoidanceSide = lateralSide;
                        moved = true;
                        break;
                    }
                }
                if (moved) {
                    break;
                }
            }
        }
    }

    if (moved) {
        entity.facing = std::atan2(direction.y, direction.x);
    }
    return true;
}

void Simulation::finishMovementStep() {
    ensureNavigation();
    // Entity index order makes separation deterministic; the unordered buckets
    // only accelerate lookup and never decide which correction happens first.
    for (std::size_t leftIndex = 0; leftIndex < entities_.size(); ++leftIndex) {
        Entity& left = entities_[leftIndex];
        if (!mobile(left)) {
            continue;
        }
        const int cellX = bucketCoordinate(left.pos.x);
        const int cellY = bucketCoordinate(left.pos.y);
        const Definition& leftUnit = definition(left.kind);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                    const auto neighbors = movementBuckets_.find(bucketKey(cellX + dx, cellY + dy));
                    if (neighbors == movementBuckets_.end()) {
                        continue;
                    }
                    for (std::size_t rightIndex : neighbors->second) {
                        if (rightIndex <= leftIndex || rightIndex >= entities_.size()) {
                            continue;
                        }
                        Entity& right = entities_[rightIndex];
                        const Definition& rightUnit = definition(right.kind);
                        if (!mobile(right) || leftUnit.air != rightUnit.air) {
                            continue;
                        }
                        const float desired = (leftUnit.radius + rightUnit.radius) * 1.04f;
                        Vec2 delta = subtractPoints(left.pos, right.pos);
                        const float separationSquared = lengthSquared(delta);
                        if (separationSquared >= desired * desired) {
                            continue;
                        }
                        const float separation = std::sqrt(separationSquared);
                        if (separation > 0.01f) {
                            delta = scalePoint(delta, 1.0f / separation);
                        } else {
                            delta = normalizePoint({
                                left.id < right.id ? 1.0f : -1.0f,
                                ((left.id + right.id) & 1U) != 0U ? 0.7f : -0.7f,
                            });
                        }
                        const float correction = std::min(8.0f, desired - separation);
                        const bool leftHeld = anchored(left);
                        const bool rightHeld = anchored(right);
                        const bool leftIdle = left.order == Order::Idle && left.target == 0;
                        const bool rightIdle = right.order == Order::Idle && right.target == 0;

                        float leftShare = 0.5f;
                        float rightShare = 0.5f;
                        if (leftHeld || rightHeld) {
                            leftShare = leftHeld ? 0.0f : 1.0f;
                            rightShare = rightHeld ? 0.0f : 1.0f;
                        } else if (leftIdle != rightIdle && left.team == right.team) {
                            leftShare = leftIdle ? 0.8f : 0.2f;
                            rightShare = rightIdle ? 0.8f : 0.2f;
                        }

                        const Vec2 leftCandidate = clampToWorld(worldSize(), 
                            addPoints(left.pos, scalePoint(delta, correction * leftShare)), leftUnit.radius);
                        const Vec2 rightCandidate = clampToWorld(worldSize(), 
                            addPoints(right.pos, scalePoint(delta, -correction * rightShare)), rightUnit.radius);
                        if (leftShare > 0.0f && (leftUnit.air || navigation_.segmentClear(
                                left.pos, leftCandidate, leftUnit.radius, left.id))) {
                            left.pos = leftCandidate;
                            if (leftIdle && left.team == right.team) {
                                left.yieldFor = std::max(left.yieldFor, 0.2f);
                            }
                        }
                        if (rightShare > 0.0f && (rightUnit.air || navigation_.segmentClear(
                                right.pos, rightCandidate, rightUnit.radius, right.id))) {
                            right.pos = rightCandidate;
                            if (rightIdle && left.team == right.team) {
                                right.yieldFor = std::max(right.yieldFor, 0.2f);
                            }
                        }
                    }
            }
        }
    }
    isStepping_ = false;
}

} // namespace cinder
