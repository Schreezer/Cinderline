#pragma once

#include "Sim/Simulation.h"

#include <cstdint>

namespace cinder::rules {

constexpr bool validKind(Kind kind) {
    const int value = static_cast<int>(kind);
    return value >= static_cast<int>(Kind::Worker) &&
           value <= static_cast<int>(Kind::Resource);
}

constexpr bool productionKind(Kind kind) {
    return kind == Kind::Headquarters || kind == Kind::Foundry ||
           kind == Kind::MotorPool || kind == Kind::Laboratory;
}

constexpr bool combatProductionKind(Kind kind) {
    return kind == Kind::Foundry || kind == Kind::MotorPool ||
           kind == Kind::Laboratory;
}

// Research queue entries retain their established sentinel Kind values.
constexpr Kind researchKind(int index) {
    return index == 1 ? Kind::Striker :
           index == 2 ? Kind::Lancer : Kind::Worker;
}

constexpr bool validPlayerCount(int count) {
    return count == 2 || count == 4;
}

constexpr std::uint8_t activePlayerMask(int playerCount) {
    return validPlayerCount(playerCount)
        ? static_cast<std::uint8_t>((1u << playerCount) - 1u)
        : std::uint8_t{0};
}

constexpr int eliminatedPlayerCount(std::uint8_t mask) {
    int count = 0;
    for (; mask; mask = static_cast<std::uint8_t>(mask & (mask - 1))) {
        ++count;
    }
    return count;
}

constexpr int survivorCount(int playerCount, std::uint8_t eliminatedMask) {
    return validPlayerCount(playerCount)
        ? playerCount - eliminatedPlayerCount(
              static_cast<std::uint8_t>(eliminatedMask & activePlayerMask(playerCount)))
        : 0;
}

constexpr bool validMatchOutcome(int playerCount, int winner,
                                 std::uint8_t eliminatedMask) {
    if (!validPlayerCount(playerCount)) {
        return false;
    }
    const std::uint8_t activeMask = activePlayerMask(playerCount);
    if ((eliminatedMask & static_cast<std::uint8_t>(~activeMask)) != 0 ||
        winner < -2 || winner >= playerCount) {
        return false;
    }
    const int survivors = survivorCount(playerCount, eliminatedMask);
    if (winner == -1) {
        return survivors >= 2;
    }
    if (winner == -2) {
        return survivors == 0;
    }
    return survivors == 1 && (eliminatedMask & (1u << winner)) == 0;
}

} // namespace cinder::rules
