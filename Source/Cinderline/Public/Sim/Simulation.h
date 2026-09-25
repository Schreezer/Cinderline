#pragma once
#include "Sim/FormationRules.h"
#include "Sim/MatchLength.h"
#include "Sim/Navigation.h"
#include <array>
#include <unordered_map>
#include <cstdint>
#include <string>
#include <vector>

namespace cinder {
namespace net { struct Snapshot; }
struct MapDefinition;
enum class Kind : int { Worker, Striker, Lancer, Scout, Bastion, Mortar, Mender, Kite, Headquarters, Processor, Foundry, MotorPool, Laboratory, Turret, Resource };
enum class Order : int { Idle, Move, Attack, AttackMove, Gather, Hold, Construct, Defend, Patrol, Escort };
enum class CommandType : int { Move, Attack, AttackMove, Gather, Stop, Hold, Build, Train, CancelQueue, Rally, Research, CancelBuilding, ResumeConstruction, Defend, AutoBuild, AutoTrain, AutoResearch, AutoRally, ClearOrders, Patrol, Escort };
enum class CommandQueueMode : int { Replace, Append };
// Accepted formation destinations. Production queues remain separate.
struct TacticalOrder {
 Order order=Order::Move; Vec2 point{}; Id supportTarget=0;
 bool hasArrivalFacing=false; float arrivalFacing=0;
 Kind buildingKind=Kind::Worker;
};
enum class SustainedOrderPhase : int { Travel, Pursuit, Return };
struct SustainedOrderState {
 Vec2 patrolOrigin{},patrolDestination{}; bool patrolTowardDestination=false;
 Id escortTarget=0; Vec2 escortOffset{};
 Id pursuitTarget=0; Vec2 pursuitAnchor{};
 SustainedOrderPhase phase=SustainedOrderPhase::Travel;
};
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
 Id supportTarget=0; std::vector<TacticalOrder> futureOrders; SustainedOrderState sustained;
 bool hasArrivalFacing=false; float arrivalFacing=0;
 bool alive() const { return hp>0; }
};
struct NavigationStats { std::uint64_t searches=0, expanded=0, failures=0, budgetDeferrals=0; };
// Local CPU diagnostics, excluded from gameplay state, saves and snapshots.
// Movement and harvesting share a phase to preserve their interleaved order.
struct SimulationStepProfile {
 bool collected=false; std::uint64_t tick=0;
 double setupMs=0, productionMs=0, movementEconomyMs=0, visionMs=0;
 double combatMs=0, aiMs=0, completionMs=0, totalMs=0;
};
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
struct Command {
 CommandType type=CommandType::Move; int team=0; std::vector<Id> units; Vec2 point;
 Id target=0; Kind kind=Kind::Worker; int queueIndex=0;
 CommandQueueMode queueMode=CommandQueueMode::Replace;
 FormationSpacing spacing=FormationSpacing::Standard;
 bool hasArrivalFacing=false; float arrivalFacing=0;
};
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
 // Revision zero retains the original flat maps for saved matches and custom
 // scenarios. New matches use the current authored map where one is available.
 int mapRevision=1;
};
struct RecordedCommand { std::uint64_t tick=0; Command command; };
struct AISighting { Id id=0; Kind kind=Kind::Worker; Vec2 pos; std::uint64_t lastSeenTick=0; };
class Simulation {
public:
 static constexpr float WorldSize=4800;
 static constexpr float Step=0.05f;
 static constexpr int FogSize=64;
 static constexpr int MaxQueue=20;
 static constexpr std::size_t MaxFutureOrders=16;
 static constexpr std::size_t MaxFutureOrdersPerPlayer=4096;
 static constexpr float SustainedPursuitRadius=600.0f;
 static constexpr float SustainedReturnTolerance=24.0f;
 static constexpr float EscortSpacing=96.0f;
 static constexpr float MaxEscortOffset=2048.0f;
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
 // Modified/custom obstacle sets retain flat-terrain semantics. Presentation
 // and gameplay must agree before applying the authored plateau/ramp field.
 bool usesAuthoredTerrain() const;
 float terrainHeight(Vec2 position) const;
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
 const std::string& workerPlanNotice(int team) const;
 std::uint64_t workerPlanNoticeSerial(int team) const;
 std::string aiStatus() const { return aiStatus_; }
 // Opponent knowledge contains observations, never current hidden actor state.
 const std::vector<AISighting>& aiSightings() const { return aiSightings_; }
 std::uint64_t aiLastObserved(Vec2 point) const;
 double lastStepMilliseconds() const { return lastStepMs_; }
 // Opt-in timing adds seven clock reads per step. Reset preserves this local
 // preference; replacing the simulation via load/snapshot disables it.
 void setProfilingEnabled(bool enabled) { profilingEnabled_=enabled; lastStepProfile_={}; }
 bool profilingEnabled() const { return profilingEnabled_; }
 const SimulationStepProfile& lastStepProfile() const { return lastStepProfile_; }
 const NavigationStats& navigationStats() const { return navigationStats_; }
 NavigationVisibilityStats navigationVisibilityStats() const { return navigation_.visibilityStats(); }
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
 // Snapshot application replaces every authoritative field. Avoid creating a
 // throwaway match (including its authored fog-height grid) on each network tick.
 struct EmptyReplicaTag {};
 explicit Simulation(EmptyReplicaTag) : players_{} {}
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
 int movementRouteRequests_=0; bool isStepping_=false,wholeStepActive_=false;
 Config config_; std::vector<Entity> entities_; std::vector<Obstacle> obstacles_; std::vector<Effect> effects_;
 std::array<Player,MaxPlayers> players_; std::array<std::array<unsigned char,FogSize*FogSize>,MaxPlayers> fog_{}, explored_{};
 // Immutable-map derived cache; excluded from persistence, hashes and snapshots.
 const MapDefinition* terrainFogDefinition_=nullptr;
 std::array<float,FogSize*FogSize> terrainFogRequiredHeights_{};
 std::vector<RecordedCommand> recording_; std::uint64_t tick_=0; Id nextId_=1; float accumulator_=0, aiTimer_=0; int winner_=-1;
 std::uint8_t eliminatedMask_=0;
 std::uint64_t nextEffectId_=1;
 bool replica_=false;
 std::string alert_,aiStatus_; double lastStepMs_=0;
 std::array<std::string,MaxPlayers> workerPlanNotices_{};
 std::array<std::uint64_t,MaxPlayers> workerPlanNoticeSerials_{};
 bool profilingEnabled_=false; SimulationStepProfile lastStepProfile_;
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
 bool yieldAtWork(Entity& worker,Vec2 center,Id ignoredStatic=0);
 Id chooseWorkTarget(Entity& worker,const std::vector<Id>& targets,float padding);
 enum class WorkerAssignmentResult { Assigned, Unavailable, Deferred, SearchLimited };
 WorkerAssignmentResult assignFreshWorkerToOre(Entity& worker,std::vector<Id>& candidates,int& cursor,const std::vector<Vec2>& exits);
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
 void resetCurrentOrder(Entity& entity,bool preserveGoal=false);
 void installTacticalOrder(Entity& entity,const TacticalOrder& order);
 bool activateNextOrder(Entity& entity);
 void processQueuedWorkerOrders();
 bool activateQueuedWorkerOrder(Id worker,std::size_t& attemptBudget);
 CommandResult queuedBuildStatus(const Entity& worker,Kind kind,Vec2 site) const;
 bool queuedGatherReachable(const Entity& worker,Id resource) const;
 void resumeOriginalGather(Entity& worker);
 void emitWorkerPlanNotice(int team,const std::string& notice);
 void finishOrder(Entity& entity,bool preserveGoal=false);
	void clearSustainedOrder(Entity& entity);
	bool validateFormationState(const std::vector<Entity>& entities) const;
 bool validateSustainedState(const std::vector<Entity>& entities) const;
 Vec2 escortFollowPoint(const Entity& escort,const Entity& leader) const;
 bool refreshSustainedOrder(Entity& entity);
 Entity* sustainedCombatTarget(Entity& entity);
 void clearSustainedReferences(Id destroyed);
 void abandonConstruction(Entity& worker,bool activateSuccessor=true);
 void releaseConstruction(Entity& foundation,bool activateSuccessor=true);
};
}
