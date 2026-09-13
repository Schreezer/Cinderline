#pragma once
#include "Sim/Simulation.h"
#include <cstddef>
#include <unordered_map>

namespace cinder::net {
constexpr std::uint32_t ProtocolVersion = 7;
constexpr std::size_t MaxMessageBytes = 1024 * 1024;
constexpr std::size_t MaxCommandUnits = 256;
struct Snapshot;
class ViewMemory {
public:
    ViewMemory();
private:
    std::uint64_t secret_ = 0;
    std::uint64_t nextHandle_ = 1;
    std::uint64_t nextEffectId_ = 1;
    std::unordered_map<Id, Id> handles_;
    std::unordered_map<Id, Id> entities_;
    std::unordered_map<Id, float> resources_;
    std::unordered_map<std::uint64_t, std::uint64_t> effects_;
    friend Snapshot snapshotFor(const Simulation&, int, ViewMemory*);
    friend bool translateCommand(const Simulation&, int, const ViewMemory&, Command&, std::string&);
};
struct Snapshot {
    Config config;
    std::uint64_t tick = 0, lastEffectId = 0;
    int winner = -1;
    std::uint8_t eliminatedMask = 0;
    Player player;
    std::vector<Entity> entities;
    std::vector<Obstacle> obstacles;
    std::vector<Effect> effects;
    // 0 unexplored, 1 previously explored, 2 currently visible. Recipient only.
    std::array<std::uint8_t, Simulation::FogSize * Simulation::FogSize> fog{};
};
Snapshot snapshotFor(const Simulation& simulation, int viewer, ViewMemory* memory = nullptr);
std::vector<std::uint8_t> encodeSnapshot(const Snapshot& snapshot);
bool decodeSnapshot(const void* data, std::size_t size, Snapshot& out, std::string& error);
std::vector<std::uint8_t> encodeCommand(const Command& command, std::uint32_t sequence);
bool decodeCommand(const void* data, std::size_t size, Command& out, std::uint32_t& sequence, std::string& error);
bool translateCommand(const Simulation& simulation, int team, const ViewMemory& memory, Command& command, std::string& error);
}
