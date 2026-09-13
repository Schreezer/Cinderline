#pragma once

#include <cstddef>
#include <cstdint>

namespace cinder {

enum class AIDifficulty : std::uint8_t {
    VeryEasy,
    Easy,
    Normal,
    Hard,
    Expert,
    Count
};

constexpr std::size_t kAIDifficultyCount = static_cast<std::size_t>(AIDifficulty::Count);

constexpr AIDifficulty aiDifficultyAt(std::size_t index) {
    return index < kAIDifficultyCount ? static_cast<AIDifficulty>(index) : AIDifficulty::Normal;
}

constexpr float aiDifficultyAggression(AIDifficulty difficulty) {
    switch (difficulty) {
        case AIDifficulty::VeryEasy: return 0.5f;
        case AIDifficulty::Easy: return 0.75f;
        case AIDifficulty::Normal: return 1.0f;
        case AIDifficulty::Hard: return 1.5f;
        case AIDifficulty::Expert: return 2.0f;
        case AIDifficulty::Count: return 1.0f;
    }
    return 1.0f;
}

constexpr AIDifficulty aiDifficultyFromAggression(float aggression) {
    if (!(aggression >= 0.0f)) {
        return AIDifficulty::Normal;
    }
    if (aggression < 0.625f) {
        return AIDifficulty::VeryEasy;
    }
    if (aggression < 0.875f) {
        return AIDifficulty::Easy;
    }
    if (aggression < 1.25f) {
        return AIDifficulty::Normal;
    }
    if (aggression < 1.75f) {
        return AIDifficulty::Hard;
    }
    return AIDifficulty::Expert;
}

constexpr const char* aiDifficultyName(AIDifficulty difficulty) {
    switch (difficulty) {
        case AIDifficulty::VeryEasy: return "Very Easy";
        case AIDifficulty::Easy: return "Easy";
        case AIDifficulty::Normal: return "Normal";
        case AIDifficulty::Hard: return "Hard";
        case AIDifficulty::Expert: return "Expert";
        case AIDifficulty::Count: return "Normal";
    }
    return "Normal";
}

constexpr const char* aiDifficultyDescription(AIDifficulty difficulty) {
    switch (difficulty) {
        case AIDifficulty::VeryEasy: return "Long buildup with small, infrequent attacks.";
        case AIDifficulty::Easy: return "Slower development with lighter attacks.";
        case AIDifficulty::Normal: return "Balanced development and pressure.";
        case AIDifficulty::Hard: return "Faster development with larger, frequent attacks.";
        case AIDifficulty::Expert: return "Fast development with relentless coordinated attacks.";
        case AIDifficulty::Count: return "Balanced development and pressure.";
    }
    return "Balanced development and pressure.";
}

} // namespace cinder
