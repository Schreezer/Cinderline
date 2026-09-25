#pragma once
#include "Sim/Simulation.h"
#include <cstddef>
#include <unordered_map>
#include <utility>

namespace cinder::net {
// Map revisions select gameplay terrain as well as geometry. Older peers must
// negotiate a matching protocol before they can exchange commands or snapshots.
constexpr std::uint32_t ProtocolVersion = 12;
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
    std::string workerPlanNotice;
    std::uint64_t workerPlanNoticeSerial = 0;
};
Snapshot snapshotFor(const Simulation& simulation, int viewer, ViewMemory* memory = nullptr);
std::vector<std::uint8_t> encodeSnapshot(const Snapshot& snapshot);
bool decodeSnapshot(const void* data, std::size_t size, Snapshot& out, std::string& error);
std::vector<std::uint8_t> encodeCommand(const Command& command, std::uint32_t sequence);
bool decodeCommand(const void* data, std::size_t size, Command& out, std::uint32_t& sequence, std::string& error);
bool translateCommand(const Simulation& simulation, int team, const ViewMemory& memory, Command& command, std::string& error);
// Read-only admission for normalized appended tactical destinations. Includes
// the current viewer snapshot, without mutating opaque-handle memory.
bool orderPlanFitsSnapshot(const Simulation& simulation, int team,
                          const std::vector<std::pair<Id,TacticalOrder>>& orders);
// Read-only admission for complete, prevalidated Patrol/Escort recipient states.
// Authoritative state and opaque-handle memory are never mutated.
bool sustainedPlanFitsSnapshot(const Simulation& simulation, int team,
                               const std::vector<Entity>& candidates);
}
