#pragma once

#include "Sim/Navigation.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cinder::rules {

// Projects an immutable world-axis Escort offset into the legal map area.
// When a whole nominal lane would cross an edge, opposite slots are interleaved
// inward at full spacing. This preserves formation footprints instead of
// collapsing or half-spacing the two sides of the formation at an edge.
inline Vec2 projectEscortFollowPoint(Vec2 leaderPosition, float leaderRadius,
    float followerRadius, Vec2 nominalOffset, float worldSize, float spacing) {
    const float minimum = followerRadius;
    const float maximum = worldSize - followerRadius;
    auto projectAxis = [&](float leader, float offset) {
        const float magnitude = std::fabs(offset);
        const bool crossesLower = leader - magnitude < minimum;
        const bool crossesUpper = leader + magnitude > maximum;
        if (!crossesLower && !crossesUpper) {
            return std::clamp(leader + offset, minimum, maximum);
        }

        // A positive nominal lane occupies the first inward lane and its
        // negative counterpart occupies the second. Larger magnitudes retain
        // that ordering, so distinct lattice slots remain a full slot apart.
        const float lane = offset >= 0.0f
            ? std::max(0.0f, magnitude * 2.0f - spacing)
            : magnitude * 2.0f;
        const bool useLower = crossesLower &&
            (!crossesUpper || leader - minimum <= maximum - leader);
        const float projected = useLower ? leader + lane : leader - lane;
        return std::clamp(projected, minimum, maximum);
    };

    Vec2 point{
        projectAxis(leaderPosition.x, nominalOffset.x),
        projectAxis(leaderPosition.y, nominalOffset.y),
    };

    // Accepted formation offsets already clear the leader. This fallback also
    // keeps imported finite offsets legal when edge projection shortens them.
    const float clearance = leaderRadius + followerRadius + 10.0f;
    const auto distanceSquared = [&](Vec2 candidate) {
        const float x = candidate.x - leaderPosition.x;
        const float y = candidate.y - leaderPosition.y;
        return x * x + y * y;
    };
    if (distanceSquared(point) < clearance * clearance) {
        const std::array<Vec2, 4> candidates{{
            {leaderPosition.x + clearance, leaderPosition.y},
            {leaderPosition.x - clearance, leaderPosition.y},
            {leaderPosition.x, leaderPosition.y + clearance},
            {leaderPosition.x, leaderPosition.y - clearance},
        }};
        float bestDistance = distanceSquared(point);
        for (Vec2 candidate : candidates) {
            candidate.x = std::clamp(candidate.x, minimum, maximum);
            candidate.y = std::clamp(candidate.y, minimum, maximum);
            const float candidateDistance = distanceSquared(candidate);
            if (candidateDistance > bestDistance) {
                bestDistance = candidateDistance;
                point = candidate;
            }
        }
    }
    return point;
}

} // namespace cinder::rules
