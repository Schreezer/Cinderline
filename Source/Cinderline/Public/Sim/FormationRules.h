#pragma once

#include "Sim/Navigation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace cinder {

enum class FormationSpacing : int { Tight = 0, Standard = 1, Wide = 2 };

namespace rules {

struct FormationRecipient {
    Id id = 0;
    Vec2 position{};
};

struct NominalFormationSlot {
    Id id = 0;
    Vec2 point{};
};

inline bool validFormationSpacing(FormationSpacing spacing) {
    const int value = static_cast<int>(spacing);
    return value >= static_cast<int>(FormationSpacing::Tight) &&
        value <= static_cast<int>(FormationSpacing::Wide);
}

inline float formationPitch(FormationSpacing spacing) {
    switch (spacing) {
        case FormationSpacing::Tight: return 48.0f;
        case FormationSpacing::Wide: return 96.0f;
        case FormationSpacing::Standard: return 64.0f;
    }
    return 0.0f;
}

inline bool validCanonicalArrivalFacing(float angle) {
    constexpr float Pi = 3.14159265358979323846f;
    return std::isfinite(angle) && angle >= -Pi && angle < Pi &&
        !(angle == 0.0f && std::signbit(angle));
}

inline bool arrivalFacingFromDirection(Vec2 direction, float& outCanonicalAngle) {
    constexpr float Pi = 3.14159265358979323846f;
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y) ||
        (direction.x == 0.0f && direction.y == 0.0f)) {
        return false;
    }
    float angle = std::atan2(direction.y, direction.x);
    if (!std::isfinite(angle)) return false;
    if (angle >= Pi) angle = -Pi;
    if (angle == 0.0f) angle = 0.0f;
    outCanonicalAngle = angle;
    return true;
}

inline bool nominalFormationSlots(Vec2 center, FormationSpacing spacing,
    bool hasArrivalFacing, float arrivalFacing,
    const std::vector<FormationRecipient>& recipients,
    std::vector<NominalFormationSlot>& out) {
    out.clear();
    if (!validFormationSpacing(spacing) || recipients.empty() ||
        !std::isfinite(center.x) || !std::isfinite(center.y) ||
        !validCanonicalArrivalFacing(arrivalFacing) ||
        (!hasArrivalFacing && arrivalFacing != 0.0f)) {
        return false;
    }

    std::vector<FormationRecipient> ordered = recipients;
    for (const auto& recipient : ordered) {
        if (!recipient.id || !std::isfinite(recipient.position.x) ||
            !std::isfinite(recipient.position.y)) return false;
    }
    std::sort(ordered.begin(), ordered.end(), [](const FormationRecipient& left,
        const FormationRecipient& right) { return left.id < right.id; });
    for (std::size_t index = 1; index < ordered.size(); ++index)
        if (ordered[index - 1].id == ordered[index].id) return false;

    const float pitch = formationPitch(spacing);
    if (!hasArrivalFacing) {
        const int columns = static_cast<int>(std::ceil(
            std::sqrt(static_cast<float>(ordered.size()))));
        out.reserve(ordered.size());
        for (std::size_t index = 0; index < ordered.size(); ++index) {
            const float x = (static_cast<int>(index) % columns -
                (columns - 1) * 0.5f) * pitch;
            const float y = (static_cast<int>(index) / columns -
                (columns - 1) * 0.5f) * pitch;
            const Vec2 point{center.x + x, center.y + y};
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                out.clear();
                return false;
            }
            out.push_back({ordered[index].id, point});
        }
        return true;
    }

    const Vec2 forward{std::cos(arrivalFacing), std::sin(arrivalFacing)};
    const Vec2 right{-forward.y, forward.x};
    auto projection = [](Vec2 point, Vec2 axis) {
        return static_cast<double>(point.x) * axis.x +
            static_cast<double>(point.y) * axis.y;
    };
    std::sort(ordered.begin(), ordered.end(), [&](const FormationRecipient& left,
        const FormationRecipient& rightRecipient) {
        const double leftLateral = projection(left.position, right);
        const double rightLateral = projection(rightRecipient.position, right);
        if (leftLateral != rightLateral) return leftLateral < rightLateral;
        const double leftDepth = projection(left.position, forward);
        const double rightDepth = projection(rightRecipient.position, forward);
        if (leftDepth != rightDepth) return leftDepth > rightDepth;
        return left.id < rightRecipient.id;
    });

    const int count = static_cast<int>(ordered.size());
    const int columns = std::min(count, static_cast<int>(std::ceil(
        std::sqrt(2.0f * static_cast<float>(count)))));
    const int rows = (count + columns - 1) / columns;
    float weightedRows = 0.0f;
    for (int row = 0; row < rows; ++row) {
        const int rowCount = std::min(columns, count - row * columns);
        weightedRows += static_cast<float>(row * rowCount);
    }
    const float centerRow = weightedRows / static_cast<float>(count);

    out.reserve(ordered.size());
    int recipient = 0;
    for (int row = 0; row < rows; ++row) {
        const int rowCount = std::min(columns, count - row * columns);
        for (int column = 0; column < rowCount; ++column, ++recipient) {
            const float lateral = (column - (rowCount - 1) * 0.5f) * pitch;
            const float depth = (centerRow - row) * pitch;
            const Vec2 point{
                center.x + right.x * lateral + forward.x * depth,
                center.y + right.y * lateral + forward.y * depth,
            };
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                out.clear();
                return false;
            }
            out.push_back({ordered[static_cast<std::size_t>(recipient)].id, point});
        }
    }
    return true;
}

} // namespace rules
} // namespace cinder
