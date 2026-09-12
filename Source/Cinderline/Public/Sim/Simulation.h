#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cinder {
namespace net { struct Snapshot; }
using Id = std::uint32_t;
struct Vec2 { float x=0, y=0; };
enum class Kind : int { Worker, Striker, Lancer, Scout, Bastion, Mortar, Mender, Kite, Headquarters, Processor, Foundry, MotorPool, Laboratory, Turret, Resource };
enum class Order : int { Idle, Move, Attack, AttackMove, Gather, Hold, Construct };
enum class CommandType : int { Move, Attack, AttackMove, Gather, Stop, Hold, Build, Train, CancelQueue, Rally, Research, CancelBuilding, ResumeConstruction };
struct Definition {
 Kind kind; const char* name; const char* role;
 float hp, speed, range, damage, cooldown, radius, vision, buildTime;
 int cost, armor, supply, tier; Kind producer; bool building, air, antiAir;
};
const Definition& definition(Kind kind);
const std::array<Definition,15>& definitions();
struct QueueItem { Kind kind=Kind::Worker; float remaining=0, total=0; int cost=0; bool research=false; };
struct Entity {
 Id id=0; Kind kind=Kind::Worker; int team=0; Vec2 pos, goal, rally;
 float hp=0, cooldown=0, progress=1, carried=0, harvestTimer=0, resource=0, facing=0;
 Order order=Order::Idle; Id target=0, resourceTarget=0; bool returning=false;
 Id builderId=0; bool resumeGather=false;
 std::vector<QueueItem> queue; std::vector<Vec2> path; int pathIndex=0; float repath=0;
 bool alive() const { return hp>0; }
};
struct Stats { int gathered=0, produced=0, lost=0, killed=0, built=0, buildingsDestroyed=0, expansions=0, upgrades=0; float damage=0; };
struct Player { int ore=500, tier=1, weapons=0, armor=0; Stats stats; };
struct Obstacle { Vec2 center; Vec2 half; };
enum class EffectType : int { Weapon, Impact, Heal, Death };
struct Effect {
 Vec2 from,to; int team=0; float life=0.3f,duration=0.3f;
 std::uint64_t id=0; EffectType type=EffectType::Weapon;
 Kind sourceKind=Kind::Worker,targetKind=Kind::Worker;
 std::uint8_t fromVisibleMask=0,toVisibleMask=0;
};
struct Command { CommandType type=CommandType::Move; int team=0; std::vector<Id> units; Vec2 point; Id target=0; Kind kind=Kind::Worker; int queueIndex=0; };
struct CommandResult { bool accepted=false; std::string message; };
struct Config { int map=0; std::uint32_t seed=42; bool ai=true; float aiAggression=1; };
struct RecordedCommand { std::uint64_t tick=0; Command command; };
struct AISighting { Id id=0; Kind kind=Kind::Worker; Vec2 pos; std::uint64_t lastSeenTick=0; };
class Simulation {
public:
 static constexpr float WorldSize=4800;
 static constexpr float Step=0.05f;
 static constexpr int FogSize=64;
 static constexpr int MaxQueue=20;
 static constexpr float AIMobileMemorySeconds=90.0f;
 Simulation();
 void reset(Config config={});
 void update(float seconds);
 CommandResult command(const Command& command);
 const std::vector<Entity>& entities() const { return entities_; }
 const std::vector<Obstacle>& obstacles() const { return obstacles_; }
 const std::vector<Effect>& effects() const { return effects_; }
 std::uint64_t lastEffectId() const { return nextEffectId_-1; }
 bool effectVisible(const Effect& effect,int team,bool source) const;
 bool effectLinkVisible(const Effect& effect,int team) const;
 const std::array<Player,2>& players() const { return players_; }
 const Config& config() const { return config_; }
 const Entity* find(Id id) const;
 bool visible(int team,Vec2 position) const;
 bool explored(int team,Vec2 position) const;
 bool canPlace(int team,Kind kind,Vec2 point,std::string* reason=nullptr) const;
 // Read-only build validation. A null site omits only distance and placement checks.
 CommandResult buildStatus(int team,Kind kind,const std::vector<Id>& units,const Vec2* site=nullptr) const;
 // Assigned worker includes travel; active means the worker is physically building.
 Id constructionWorker(Id foundation) const;
 bool constructionActive(Id foundation) const;
 int supply(int team) const;
 int capacity(int team) const;
 float time() const { return tick_*Step; }
 std::uint64_t tick() const { return tick_; }
 int winner() const { return winner_; }
 const std::vector<RecordedCommand>& recording() const { return recording_; }
 const std::string& alert() const { return alert_; }
 std::string aiStatus() const { return aiStatus_; }
 // Opponent knowledge contains observations, never current hidden actor state.
 const std::vector<AISighting>& aiSightings() const { return aiSightings_; }
 std::uint64_t aiLastObserved(Vec2 point) const;
 double lastStepMilliseconds() const { return lastStepMs_; }
 std::uint64_t stateHash() const;
 bool save(const std::string& path) const;
 bool load(const std::string& path);
 bool applySnapshot(const net::Snapshot& snapshot,std::string* error=nullptr);
 bool isReplica() const { return replica_; }
 void forfeit(int team);
 // Explicit development tools, never called by the opponent.
 Id debugSpawn(Kind kind,int team,Vec2 position);
 void debugResources(int team,int ore);
private:
 Config config_; std::vector<Entity> entities_; std::vector<Obstacle> obstacles_; std::vector<Effect> effects_;
 std::array<Player,2> players_; std::array<std::array<unsigned char,FogSize*FogSize>,2> fog_{}, explored_{};
 std::vector<RecordedCommand> recording_; std::uint64_t tick_=0; Id nextId_=1; float accumulator_=0, aiTimer_=0; int winner_=-1;
 std::uint64_t nextEffectId_=1;
 bool replica_=false;
 std::string alert_,aiStatus_; double lastStepMs_=0;
 std::vector<AISighting> aiSightings_;
 std::array<std::uint64_t,FogSize*FogSize> aiObserved_{};
 Entity* get(Id id); Id spawn(Kind kind,int team,Vec2 position,bool complete=true);
 void step(); void updateVision(); void updateAI(); void updateMovement(Entity& e); void updateEconomy(Entity& e); void updateCombat(Entity& e);
 void updateAIKnowledge();
 void updateProduction(Entity& e); void moveToward(Entity& e,Vec2 destination); void planPath(Entity& e,Vec2 destination);
 bool blocked(Vec2 point,float radius,Id ignore=0) const; bool hasBuilding(int team,Kind kind) const;
 Id nearest(int team,Vec2 point,Kind kind) const; void damage(Entity& victim,float amount,int attackerTeam,Kind sourceKind);
 void emitEffect(EffectType type,Vec2 from,Vec2 to,int team,Kind sourceKind,Kind targetKind,float duration);
 Id selectConstructionWorker(const std::vector<Id>& workers,Vec2 site) const;
 CommandResult checkBuild(int team,Kind kind,const std::vector<Id>& units,const Vec2* site,Id* worker) const;
 void assignConstruction(Entity& foundation,Entity& worker);
 void abandonConstruction(Entity& worker); void releaseConstruction(Entity& foundation);
};
}
