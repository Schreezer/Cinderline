#pragma once
#include "Sim/MatchLength.h"
#include "Sim/Navigation.h"
#include <array>
#include <unordered_map>
#include <cstdint>
#include <string>
#include <vector>

namespace cinder {
namespace net { struct Snapshot; }
enum class Kind : int { Worker, Striker, Lancer, Scout, Bastion, Mortar, Mender, Kite, Headquarters, Processor, Foundry, MotorPool, Laboratory, Turret, Resource };
enum class Order : int { Idle, Move, Attack, AttackMove, Gather, Hold, Construct, Defend };
enum class CommandType : int { Move, Attack, AttackMove, Gather, Stop, Hold, Build, Train, CancelQueue, Rally, Research, CancelBuilding, ResumeConstruction, Defend, AutoBuild, AutoTrain, AutoResearch, AutoRally };
struct Definition {
 Kind kind; const char* name; const char* role;
 float hp, speed, range, damage, cooldown, radius, vision, buildTime;
 int cost, armor, supply, tier; Kind producer; bool building, air, antiAir;
};
const Definition& definition(Kind kind);
const std::array<Definition,15>& definitions();
struct QueueItem {
 Kind kind=Kind::Worker; float remaining=0, total=0; int cost=0; bool research=false; Id id=0;
 int assignmentCursor=0; std::vector<Id> assignmentCandidates;
};
struct Entity {
 Id id=0; Kind kind=Kind::Worker; int team=0; Vec2 pos, goal, rally;
 float hp=0, cooldown=0, progress=1, carried=0, harvestTimer=0, resource=0, facing=0;
 Order order=Order::Idle; Id target=0, resourceTarget=0; bool returning=false;
 Id builderId=0; bool resumeGather=false, rallyOverride=false;
 std::vector<QueueItem> queue; Id nextQueueId=1; std::vector<Vec2> path; int pathIndex=0; float repath=0;
 Id workTarget=0; Vec2 workPoint{}; bool workPointValid=false;
 Vec2 navigationAnchor{}; float stalledFor=0, yieldFor=0, navigationBestDistance=0; std::uint64_t pathGeometry=0;
 int navigationFailures=0, avoidanceSide=0; bool navigationExhausted=false;
 bool alive() const { return hp>0; }
};
struct NavigationStats { std::uint64_t searches=0, expanded=0, failures=0, budgetDeferrals=0; };
struct Stats { int gathered=0, produced=0, lost=0, killed=0, built=0, buildingsDestroyed=0, expansions=0, upgrades=0; float damage=0; };
struct Player {
 int ore=500, tier=1, weapons=0, armor=0;
 Vec2 armyRally{}; bool armyRallySet=false;
 Stats stats;
};
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
struct ProductionAssignment { Id producer=0; int quantity=0; float queuedSeconds=0,completionSeconds=0; };
struct JobPlan {
 bool accepted=false; std::string message; Id worker=0;
 int quantity=1,totalCost=0,totalSupply=0;
 std::vector<ProductionAssignment> assignments;
};
struct Config {
 int map=0; std::uint32_t seed=42; bool ai=true; float aiAggression=1;
 MatchLength matchLength=MatchLength::Standard;
 int playerCount=2;
};
struct RecordedCommand { std::uint64_t tick=0; Command command; };
struct AISighting { Id id=0; Kind kind=Kind::Worker; Vec2 pos; std::uint64_t lastSeenTick=0; };
class Simulation {
public:
 static constexpr float WorldSize=4800;
 static constexpr float Step=0.05f;
 static constexpr int FogSize=64;
 static constexpr int MaxQueue=20;
 static constexpr int MaxPlayers=4;
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
 const std::array<Player,MaxPlayers>& players() const { return players_; }
 const Config& config() const { return config_; }
 int playerCount() const { return config_.playerCount; }
 bool eliminated(int team) const { return team>=0&&team<config_.playerCount&&(eliminatedMask_&(1u<<team))!=0; }
 std::uint8_t eliminatedMask() const { return eliminatedMask_; }
 float worldSize() const { return matchLengthProfile(config_.matchLength).worldSize; }
 const Entity* find(Id id) const;
 bool visible(int team,Vec2 position) const;
 bool explored(int team,Vec2 position) const;
 bool canPlace(int team,Kind kind,Vec2 point,std::string* reason=nullptr) const;
 // Read-only build requirements and access validation. A null site omits
 // distance, footprint and worker access checks.
 CommandResult buildStatus(int team,Kind kind,const std::vector<Id>& units,const Vec2* site=nullptr) const;
 // Global catalogs and previews use the same authoritative validation as commands.
 // A nonzero producer pins training, research or rally to that owned structure.
 // Rally reset restores combat inheritance or automatic mining for an Anchor.
 JobPlan autoBuildStatus(int team,Kind kind,const Vec2* site=nullptr) const;
 JobPlan autoTrainStatus(int team,Kind kind,int quantity=1,Id producer=0) const;
 JobPlan autoResearchStatus(int team,int researchIndex,Id producer=0) const;
 CommandResult autoRallyStatus(int team,Kind producerKind=Kind::Resource,Id producer=0,bool useDefault=false) const;
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
 const NavigationStats& navigationStats() const { return navigationStats_; }
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
 mutable Navigation navigation_;
 mutable bool navigationDirty_=true;
 // Derived placement-query cache; never affects simulation persistence.
 mutable bool buildAccessCached_=false;
 mutable bool buildAccessAutomatic_=false;
 mutable std::uint64_t buildAccessGeometry_=0;
 mutable Vec2 buildAccessSite_{};
 mutable Kind buildAccessKind_=Kind::Foundry;
 mutable Id buildAccessExisting_=0, buildAccessWorker_=0;
 mutable std::vector<NavCircle> buildAccessInputs_;
 NavigationStats navigationStats_;
 std::unordered_map<std::uint64_t,std::vector<std::size_t>> movementBuckets_;
 int movementRouteRequests_=0; bool isStepping_=false;
 Config config_; std::vector<Entity> entities_; std::vector<Obstacle> obstacles_; std::vector<Effect> effects_;
 std::array<Player,MaxPlayers> players_; std::array<std::array<unsigned char,FogSize*FogSize>,MaxPlayers> fog_{}, explored_{};
 std::vector<RecordedCommand> recording_; std::uint64_t tick_=0; Id nextId_=1; float accumulator_=0, aiTimer_=0; int winner_=-1;
 std::uint8_t eliminatedMask_=0;
 std::uint64_t nextEffectId_=1;
 bool replica_=false;
 std::string alert_,aiStatus_; double lastStepMs_=0;
 std::vector<AISighting> aiSightings_;
 std::array<std::uint64_t,FogSize*FogSize> aiObserved_{};
 Entity* get(Id id); Id spawn(Kind kind,int team,Vec2 position,bool complete=true);
 void step(); void updateVision(); void updateAI(); void updateMovement(Entity& e); void updateEconomy(Entity& e); void updateCombat(Entity& e);
 void updateAIKnowledge();
 void updateProduction(Entity& e); bool moveToward(Entity& e,Vec2 destination); bool planPath(Entity& e,Vec2 destination);
 void ensureNavigation() const; void resetNavigation(Entity& e);
 void beginMovementStep(); void finishMovementStep(); bool claimNavigationSearch();
 NavigationResult findRoute(Entity& e,const std::vector<Vec2>& goals);
 std::vector<Vec2> workPoints(const Entity& worker,Vec2 center,float reach,Id target,const Navigation& navigation,bool reserve) const;
 bool approachWork(Entity& worker,const Entity& target,float padding);
 bool yieldAtWork(Entity& worker,Vec2 center);
 Id chooseWorkTarget(Entity& worker,const std::vector<Id>& targets,float padding);
 bool assignFreshWorkerToOre(Entity& worker,std::vector<Id>& candidates,int& cursor,bool& deferred);
 Id reachableConstructionWorker(const std::vector<Id>& workers,Vec2 site,Kind kind,Id existing=0,bool automatic=false) const;
 bool blocked(Vec2 point,float radius,Id ignore=0) const; bool hasBuilding(int team,Kind kind) const;
 Id nearest(int team,Vec2 point,Kind kind) const; void damage(Entity& victim,float amount,int attackerTeam,Kind sourceKind);
 void emitEffect(EffectType type,Vec2 from,Vec2 to,int team,Kind sourceKind,Kind targetKind,float duration);
 bool activeTeam(int team) const { return team>=0&&team<config_.playerCount; }
 void eliminateTeam(int team); void updateEliminations(); void updateWinner();
 Id selectConstructionWorker(const std::vector<Id>& workers,Vec2 site) const;
 CommandResult checkBuild(int team,Kind kind,const std::vector<Id>& units,const Vec2* site,Id* worker,bool automatic=false) const;
 void assignConstruction(Entity& foundation,Entity& worker);
 float productionTime(float standardSeconds) const { return standardSeconds*matchLengthProfile(config_.matchLength).productionTimeScale; }
 void abandonConstruction(Entity& worker); void releaseConstruction(Entity& foundation);
};
}
