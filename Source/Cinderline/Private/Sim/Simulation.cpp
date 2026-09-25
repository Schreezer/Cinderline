#include "Sim/Simulation.h"
#include "Sim/MapDefinition.h"
#include "Sim/Network.h"
#include "Sim/SimulationRules.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <queue>
#include <sstream>
#include <utility>

namespace cinder {
namespace {
constexpr float Pi = 3.14159265358979323846f;
constexpr float CarryCapacity = 18.0f;
constexpr float HarvestPeriod = 0.65f;
constexpr float ConstructionPadding = 20.0f;
const std::array<Definition,15> Data{{
 {Kind::Worker,"Drudge","Harvests ore and deploys structures",70,140,65,4,1.4f,16,460,12,60,0,1,1,Kind::Headquarters,false,false,false},
 {Kind::Striker,"Ember","Ranged infantry; efficient against scouts and lancers",130,150,240,14,0.8f,20,510,24,100,1,2,1,Kind::Foundry,false,false,true},
 {Kind::Lancer,"Needle","Heavy piercing infantry; counters armor and aircraft",110,125,315,25,1.4f,21,550,32,140,1,2,1,Kind::Foundry,false,false,true},
 {Kind::Scout,"Skim","Fast reconnaissance and worker harassment",90,245,180,8,0.65f,18,780,20,90,0,1,1,Kind::Foundry,false,false,false},
 {Kind::Bastion,"Anvil","Armored frontline vehicle; counters infantry",480,105,280,28,1.2f,34,530,50,250,6,4,2,Kind::MotorPool,false,false,false},
 {Kind::Mortar,"Cinderthrow","Long-range siege; vulnerable to close flanks",220,86,610,64,3.5f,30,620,65,320,2,4,3,Kind::MotorPool,false,false,false},
 {Kind::Mender,"Mend","Repairs nearby allied units; cannot attack",110,140,250,0,0.75f,19,520,38,180,0,2,2,Kind::Laboratory,false,false,false},
 {Kind::Kite,"Veil","Flying interceptor and harassment craft",210,205,285,22,1.1f,26,680,55,300,2,3,3,Kind::MotorPool,false,true,true},
 {Kind::Headquarters,"Anchor","Main base, worker production and ore delivery",3400,0,0,0,0,125,780,110,650,5,0,1,Kind::Worker,true,false,false},
 {Kind::Processor,"Siphon","Ore delivery, expansion economy and +14 supply",1250,0,0,0,0,76,620,55,220,3,0,1,Kind::Worker,true,false,false},
 {Kind::Foundry,"Kiln","Trains Ember, Needle and Skim infantry",1550,0,0,0,0,90,590,65,250,3,0,1,Kind::Worker,true,false,false},
 {Kind::MotorPool,"Crucible","Produces armored vehicles and Veil aircraft",1850,0,0,0,0,104,620,85,400,4,0,2,Kind::Worker,true,false,false},
 {Kind::Laboratory,"Resonator","Researches tiers and upgrades; trains Mend",1350,0,0,0,0,85,620,75,350,2,0,1,Kind::Worker,true,false,false},
 {Kind::Turret,"Ward","Static ground and air defense",850,0,430,24,1.2f,48,640,40,180,4,0,1,Kind::Worker,true,false,true},
 {Kind::Resource,"Ember ore","Finite ore deposit",1,0,0,0,0,45,0,0,0,0,0,1,Kind::Worker,false,false,false}
}};
float lengthSq(Vec2 a) { return a.x*a.x+a.y*a.y; }
float distanceSq(Vec2 a,Vec2 b) { return lengthSq({a.x-b.x,a.y-b.y}); }
float distance(Vec2 a,Vec2 b) { return std::sqrt(distanceSq(a,b)); }
Vec2 add(Vec2 a,Vec2 b) { return {a.x+b.x,a.y+b.y}; }
Vec2 subtract(Vec2 a,Vec2 b) { return {a.x-b.x,a.y-b.y}; }
Vec2 scale(Vec2 a,float value) { return {a.x*value,a.y*value}; }
Vec2 normalized(Vec2 a) { const float len=std::sqrt(lengthSq(a)); return len>0.0001f?scale(a,1/len):Vec2{1,0}; }
bool finite(Vec2 p) { return std::isfinite(p.x)&&std::isfinite(p.y); }
bool readEffectId(std::istream& in,std::uint64_t& value) {
 std::string token;if(!(in>>token)||token.empty())return false;
 std::uint64_t parsed=0;
 for(char digit:token) {
  if(digit<'0'||digit>'9')return false;
  const auto number=static_cast<std::uint64_t>(digit-'0');
  if(parsed>(std::numeric_limits<std::uint64_t>::max()-number)/10)return false;
  parsed=parsed*10+number;
 }
 value=parsed;return true;
}
Vec2 bounded(Vec2 p,float worldSize,float radius=1) { return {std::clamp(p.x,radius,worldSize-radius),std::clamp(p.y,radius,worldSize-radius)}; }
Vec2 startOffset(int team,Vec2 local) {
 switch(team) {
  case 1:return {-local.x,-local.y};
  case 2:return {-local.y,local.x};
  case 3:return {local.y,-local.x};
  default:return local;
 }
}
bool circleBox(Vec2 p,float radius,const Obstacle& o) {
 const float dx=std::max(std::fabs(p.x-o.center.x)-o.half.x,0.0f);
 const float dy=std::max(std::fabs(p.y-o.center.y)-o.half.y,0.0f);
 return dx*dx+dy*dy<radius*radius;
}
bool clearFireLine(Vec2 from,Vec2 to,const std::vector<Obstacle>& obstacles) {
 const Vec2 delta=subtract(to,from);const int samples=std::max(1,static_cast<int>(std::ceil(distance(from,to)/24)));
 for(int sample=1;sample<samples;++sample) {
  const Vec2 p=add(from,scale(delta,static_cast<float>(sample)/samples));
  for(const auto& obstacle:obstacles)if(circleBox(p,2,obstacle))return false;
 }
 return true;
}
int fogIndex(Vec2 p,float worldSize) {
 const float cell=worldSize/Simulation::FogSize;
 int x=std::clamp(static_cast<int>(p.x/cell),0,Simulation::FogSize-1);
 int y=std::clamp(static_cast<int>(p.y/cell),0,Simulation::FogSize-1);
 return y*Simulation::FogSize+x;
}
struct Hasher {
 std::uint64_t value=1469598103934665603ULL;
 void byte(unsigned char b) { value^=b; value*=1099511628211ULL; }
 void integer(std::uint64_t n) { for(int i=0;i<8;++i) { byte(static_cast<unsigned char>(n&255)); n>>=8; } }
 void real(float v) { std::uint32_t bits; std::memcpy(&bits,&v,sizeof(bits)); integer(bits); }
 void point(Vec2 p) { real(p.x);real(p.y); }
};
}

const std::array<Definition,15>& definitions() { return Data; }
const Definition& definition(Kind kind) { return Data[rules::validKind(kind)?static_cast<int>(kind):0]; }
Simulation::Simulation() { reset(); }
void Simulation::reset(Config config) {
 lastStepProfile_={};
 workerPlanNotices_={};workerPlanNoticeSerials_={};
 replica_=false; buildAccessCached_=false; navigationDirty_=true; navigation_=Navigation{}; navigationStats_={}; movementBuckets_.clear(); movementRouteRequests_=0; isStepping_=false;wholeStepActive_=false;
 config_=config; config_.map=std::clamp(config_.map,0,2);
 config_.matchLength=matchLengthAt(static_cast<int>(config_.matchLength));
 config_.playerCount=config_.playerCount==4?4:2;
 config_.mapRevision=validMapRevision(config_.mapRevision)?config_.mapRevision:0;
 if(config_.playerCount==4)config_.ai=false;
 if(!std::isfinite(config_.aiAggression)) config_.aiAggression=1;
 config_.aiAggression=std::clamp(config_.aiAggression,0.5f,2.0f);
 entities_.clear(); obstacles_.clear(); effects_.clear(); recording_.clear(); aiSightings_.clear(); aiObserved_={};
 players_={}; fog_={}; explored_={}; tick_=0; nextId_=1; nextEffectId_=1; accumulator_=0; aiTimer_=0; winner_=-1; eliminatedMask_=0; lastStepMs_=0;
 alert_="Build a Kiln, scout, and protect your Anchor."; aiStatus_=config_.ai?"Establishing economy":"Opponent AI disabled";
 const auto& map=mapDefinition(config_.map,config_.playerCount,config_.matchLength,config_.mapRevision);
 obstacles_.reserve(map.obstacles.size());
 for(const auto& obstacle:map.obstacles)obstacles_.push_back({obstacle.center,obstacle.half});
 for(int team=0;team<config_.playerCount;++team) {
  const Vec2 base=map.starts[team];
  spawn(Kind::Headquarters,team,base);
  const auto& home=map.sites[team];
  std::vector<Id> nodes;
  for(const Vec2 position:home.nodes) {
   const Id id=spawn(Kind::Resource,-1,position);
   get(id)->resource=home.nodeOre;nodes.push_back(id);
  }
  for(int i=0;i<5;++i) {
   // Actor radii and local worker spacing stay fixed across match sizes.
   // Retain the legacy HQ, ore, worker ordering for stable entity identifiers.
   const Id id=spawn(Kind::Worker,team,add(base,startOffset(team,{static_cast<float>(155+i*34),55})));
   Entity* worker=get(id);worker->order=Order::Gather;worker->target=nodes[i%nodes.size()];worker->resourceTarget=worker->target;
  }
 }
 for(std::size_t index=static_cast<std::size_t>(config_.playerCount);index<map.sites.size();++index) {
  const auto& site=map.sites[index];
  for(const Vec2 position:site.nodes) {
   const Id id=spawn(Kind::Resource,-1,position);get(id)->resource=site.nodeOre;
  }
 }
 updateVision();
}

Entity* Simulation::get(Id id) { for(auto& e:entities_) if(e.id==id)return &e;return nullptr; }
const Entity* Simulation::find(Id id) const { for(const auto& e:entities_)if(e.id==id)return &e;return nullptr; }
Id Simulation::spawn(Kind kind,int team,Vec2 position,bool complete) {
 if(!rules::validKind(kind)||!finite(position)||(kind!=Kind::Resource&&(!activeTeam(team)||eliminated(team))))return 0;
 const auto& d=definition(kind); Entity e;
 e.id=nextId_++; e.kind=kind;e.team=kind==Kind::Resource?-1:team;e.pos=bounded(position,worldSize(),d.radius);e.goal=e.pos;
 e.rally=bounded(add(e.pos,startOffset(team,{190,0})),worldSize()); e.progress=complete?1.0f:0.0f;e.hp=complete?d.hp:d.hp*0.1f;
 if(rules::combatProductionKind(kind)&&players_[team].armyRallySet)e.rally=players_[team].armyRally;
 if(kind==Kind::Resource)e.resource=4000;
 const Id id=e.id;entities_.push_back(std::move(e));navigationDirty_=true;return id;
}
Id Simulation::debugSpawn(Kind kind,int team,Vec2 position) { if(replica_)return 0;Id id=spawn(kind,team,position);updateVision();return id; }
void Simulation::debugResources(int team,int ore) { if(!replica_&&activeTeam(team)&&!eliminated(team))players_[team].ore=std::clamp(ore,0,100000000); }
int Simulation::supply(int team) const {
 if(!activeTeam(team)||eliminated(team))return 0;int n=0;
 for(const auto& e:entities_)if(e.alive()&&e.team==team) {
  n+=definition(e.kind).supply;
  for(const auto& item:e.queue)if(!item.research)n+=definition(item.kind).supply;
 }
 return n;
}
int Simulation::capacity(int team) const {
 if(!activeTeam(team)||eliminated(team))return 0;int n=0;
 for(const auto& e:entities_)if(e.alive()&&e.team==team&&e.progress>=1) {
  if(e.kind==Kind::Headquarters)n+=30;if(e.kind==Kind::Processor)n+=14;
 }
 return std::min(200,n);
}
bool Simulation::hasBuilding(int team,Kind kind) const {
 for(const auto& e:entities_)if(e.alive()&&e.team==team&&e.kind==kind&&e.progress>=1)return true;return false;
}
Id Simulation::nearest(int team,Vec2 point,Kind kind) const {
 Id result=0;float best=std::numeric_limits<float>::max();
 for(const auto& e:entities_) {
  if(!e.alive()||e.kind!=kind||(team>=0&&e.team!=team)||e.progress<1||(kind==Kind::Resource&&e.resource<=0))continue;
  float d=distanceSq(point,e.pos);if(d<best){best=d;result=e.id;}
 }
 return result;
}
bool Simulation::blocked(Vec2 point,float radius,Id ignore) const {
 if(!finite(point)||point.x<radius||point.y<radius||point.x>worldSize()-radius||point.y>worldSize()-radius)return true;
 for(const auto& o:obstacles_)if(circleBox(point,radius,o))return true;
 for(const auto& e:entities_) {
  if(e.id==ignore||!e.alive())continue;
  if(!definition(e.kind).building&&e.kind!=Kind::Resource)continue;
  if(e.kind==Kind::Resource&&e.resource<=0)continue;
  float combined=radius+definition(e.kind).radius;
  if(distanceSq(point,e.pos)<combined*combined)return true;
 }
 return false;
}
bool Simulation::usesAuthoredTerrain() const {
 const auto& map=mapDefinition(config_.map,config_.playerCount,config_.matchLength,config_.mapRevision);
 if(!map.authored()||map.obstacles.size()!=obstacles_.size())return false;
 for(std::size_t i=0;i<obstacles_.size();++i) {
  const auto& actual=obstacles_[i];const auto& expected=map.obstacles[i];
  if(actual.center.x!=expected.center.x||actual.center.y!=expected.center.y||
     actual.half.x!=expected.half.x||actual.half.y!=expected.half.y)return false;
 }
 return true;
}
float Simulation::terrainHeight(Vec2 position) const {
 if(!finite(position)||!usesAuthoredTerrain())return 0;
 return mapDefinition(config_.map,config_.playerCount,config_.matchLength,config_.mapRevision).terrainHeight(position);
}
bool Simulation::visible(int team,Vec2 position) const { return activeTeam(team)&&finite(position)&&position.x>=0&&position.y>=0&&position.x<=worldSize()&&position.y<=worldSize()&&fog_[team][fogIndex(position,worldSize())]!=0; }
bool Simulation::explored(int team,Vec2 position) const { return activeTeam(team)&&finite(position)&&position.x>=0&&position.y>=0&&position.x<=worldSize()&&position.y<=worldSize()&&explored_[team][fogIndex(position,worldSize())]!=0; }
bool Simulation::canPlace(int team,Kind kind,Vec2 point,std::string* reason) const {
 auto fail=[&](const char* message){if(reason)*reason=message;return false;};
 if(!activeTeam(team)||eliminated(team)||!rules::validKind(kind)||!definition(kind).building)return fail("Choose a structure.");
 if(!finite(point))return fail("Invalid position.");
 if(!visible(team,point))return fail("Keep the construction site in current vision.");
 if(usesAuthoredTerrain()&&!mapDefinition(config_.map,config_.playerCount,config_.matchLength,config_.mapRevision).buildable(point,definition(kind).radius+15))
  return fail("Structures require flat ground clear of ramps and cliff edges.");
 if(blocked(point,definition(kind).radius+15))return fail("Blocked by terrain, a structure, or an ore deposit.");
 for(const auto& e:entities_)if(e.alive()&&e.kind!=Kind::Resource&&!definition(e.kind).building&&!definition(e.kind).air&&distance(e.pos,point)<definition(kind).radius+definition(e.kind).radius+8)return fail("A ground unit occupies this site.");
 if(reason)reason->clear();return true;
}

CommandResult Simulation::buildStatus(int team,Kind kind,const std::vector<Id>& units,const Vec2* site) const {
 return checkBuild(team,kind,units,site,nullptr);
}
CommandResult Simulation::checkBuild(int team,Kind kind,const std::vector<Id>& units,const Vec2* site,Id* worker,bool automatic) const {
 auto fail=[](const std::string& why){return CommandResult{false,why};};
 if(worker)*worker=0;
 if(winner_!=-1)return fail("The match has ended.");
 if(!activeTeam(team)||eliminated(team)||!rules::validKind(kind)||units.size()>500
    ||(site&&(!finite(*site)||site->x<0||site->y<0||site->x>worldSize()||site->y>worldSize())))return fail("Invalid command.");
 std::vector<Id> selected=units;std::sort(selected.begin(),selected.end());selected.erase(std::unique(selected.begin(),selected.end()),selected.end());
 for(Id id:selected) {
  const Entity* e=find(id);
  if(!e||!e->alive()||e->team!=team||e->kind==Kind::Resource)return fail("The selection contains unavailable or foreign units.");
 }
 if(automatic) {
  if(!selected.empty())return fail("Automatic construction does not take a selection.");
  for(const auto& entity:entities_)if(entity.alive()&&entity.team==team&&entity.kind==Kind::Worker&&
     entity.futureOrders.empty()&&(entity.order==Order::Idle||entity.order==Order::Gather))selected.push_back(entity.id);
  std::sort(selected.begin(),selected.end());
 }
 if(selected.empty())return fail(automatic?"No idle or mining Drudge without queued orders is available.":site?"Select your units or a structure first.":"Select a Drudge to build this structure.");
 if(!definition(kind).building)return fail("Only structures can be deployed.");
 std::vector<Id> nearbyWorkers;
 for(Id id:selected) {const Entity* e=find(id);if(e->kind==Kind::Worker&&(automatic||!site||distance(e->pos,*site)<=700))nearbyWorkers.push_back(id);}
 Id chosen=selectConstructionWorker(nearbyWorkers,site?*site:Vec2{});
 if(automatic&&!site) {
  std::vector<Id> idle;
  for(Id id:nearbyWorkers)if(find(id)->order==Order::Idle)idle.push_back(id);
  if(!idle.empty())chosen=selectConstructionWorker(idle,{});
 }
 if(!chosen)return fail(site?"Select a Drudge closer to the site.":"Select a Drudge to build this structure.");
 if(!hasBuilding(team,Kind::Headquarters))return fail("An operational Anchor is required.");
 if(players_[team].tier<definition(kind).tier)
  return fail("Requires T"+std::to_string(definition(kind).tier)+". Choose TECH TIER at an operational Resonator.");
 if((kind==Kind::MotorPool||kind==Kind::Laboratory||kind==Kind::Turret)&&!hasBuilding(team,Kind::Foundry))return fail("Build an operational Kiln first.");
 if(site) {std::string reason;if(!canPlace(team,kind,*site,&reason))return fail(reason);}
 if(players_[team].ore<definition(kind).cost)return fail("Insufficient ore.");
 if(site) {
  chosen=reachableConstructionWorker(nearbyWorkers,*site,kind,0,automatic);
  if(!chosen)return fail("No accessible route to this construction site.");
 }
 if(worker)*worker=chosen;
 return {true,site?"Ready to build.":"Ready to choose a construction site."};
}

Id Simulation::constructionWorker(Id foundationId) const {
 const Entity* foundation=find(foundationId);
 if(!foundation||!foundation->alive()||!definition(foundation->kind).building||foundation->progress>=1)return 0;
 const Entity* worker=find(foundation->builderId);
 if(!worker||!worker->alive()||worker->kind!=Kind::Worker||worker->team!=foundation->team||worker->order!=Order::Construct||worker->target!=foundationId)return 0;
 return worker->id;
}
bool Simulation::constructionActive(Id foundationId) const {
 const Id assigned=constructionWorker(foundationId);if(!assigned)return false;
 const Entity* foundation=find(foundationId);const Entity* worker=find(assigned);
 const float reach=definition(foundation->kind).radius+definition(Kind::Worker).radius+ConstructionPadding;
 return distance(worker->pos,foundation->pos)<=reach+3&&!blocked(worker->pos,definition(Kind::Worker).radius,worker->id)&&clearFireLine(worker->pos,foundation->pos,obstacles_);
}
Id Simulation::selectConstructionWorker(const std::vector<Id>& workers,Vec2 site) const {
 Id selected=0;bool selectedBusy=true;float best=std::numeric_limits<float>::max();
 for(Id id:workers) {
  const Entity* worker=find(id);if(!worker||!worker->alive()||worker->kind!=Kind::Worker)continue;
  const bool busy=worker->order==Order::Construct;const float d=distanceSq(worker->pos,site);
  if(!selected||(selectedBusy&&!busy)||(selectedBusy==busy&&(d<best||(d==best&&id<selected)))) {selected=id;selectedBusy=busy;best=d;}
 }
 return selected;
}
void Simulation::resetCurrentOrder(Entity& entity,bool preserveGoal) {
 entity.order=Order::Idle;entity.target=0;entity.supportTarget=0;
 entity.hasArrivalFacing=false;entity.arrivalFacing=0;
 clearSustainedOrder(entity);
 if(!preserveGoal)entity.goal=entity.pos;
 resetNavigation(entity);
}
void Simulation::installTacticalOrder(Entity& entity,const TacticalOrder& order) {
 resetNavigation(entity);entity.order=order.order;entity.goal=order.point;
 entity.target=0;entity.supportTarget=order.supportTarget;
 entity.hasArrivalFacing=order.hasArrivalFacing;entity.arrivalFacing=order.arrivalFacing;
 clearSustainedOrder(entity);
}
bool Simulation::activateNextOrder(Entity& entity) {
 if(!entity.alive()||entity.futureOrders.empty())return false;
 if(entity.kind==Kind::Worker&&(entity.futureOrders.front().order==Order::Construct||
    entity.futureOrders.front().order==Order::Gather))return true;
 const TacticalOrder next=entity.futureOrders.front();
 entity.futureOrders.erase(entity.futureOrders.begin());
 installTacticalOrder(entity,next);return true;
}
bool Simulation::validateFormationState(const std::vector<Entity>& entities) const {
 auto validPair=[](bool hasFacing,float facing,bool eligible) {
  if(!rules::validCanonicalArrivalFacing(facing))return false;
  if(!hasFacing)return facing==0.0f;
  return eligible;
 };
 for(const auto& entity:entities) {
  const bool mobile=entity.alive()&&activeTeam(entity.team)&&!definition(entity.kind).building&&entity.kind!=Kind::Resource;
  const bool currentEligible=mobile&&(entity.order==Order::Move||entity.order==Order::AttackMove||entity.order==Order::Defend);
  if(!validPair(entity.hasArrivalFacing,entity.arrivalFacing,currentEligible))return false;
  for(const auto& order:entity.futureOrders) {
   const bool eligible=mobile&&(order.order==Order::Move||order.order==Order::AttackMove);
   if(!validPair(order.hasArrivalFacing,order.arrivalFacing,eligible))return false;
  }
 }
 return true;
}
void Simulation::finishOrder(Entity& entity,bool preserveGoal) {
 const bool hasArrivalFacing=entity.hasArrivalFacing;
 const float arrivalFacing=entity.arrivalFacing;
 resetCurrentOrder(entity,preserveGoal);
 if(!activateNextOrder(entity)) {
  if(entity.kind==Kind::Worker&&entity.resumeGather)resumeOriginalGather(entity);
  if(entity.order==Order::Idle&&hasArrivalFacing)entity.facing=arrivalFacing;
 }
}
void Simulation::abandonConstruction(Entity& worker,bool activateSuccessor) {
 if(worker.kind!=Kind::Worker)return;
 if(worker.order==Order::Construct) {
   Entity* foundation=get(worker.target);
   if(foundation&&foundation->builderId==worker.id)foundation->builderId=0;
  resetCurrentOrder(worker);
  }
 worker.resumeGather=false;
 if(activateSuccessor)activateNextOrder(worker);
}
void Simulation::releaseConstruction(Entity& foundation,bool activateSuccessor) {
 const Id assigned=foundation.builderId;foundation.builderId=0;Entity* worker=get(assigned);
 if(!worker||!worker->alive()||worker->kind!=Kind::Worker||worker->team!=foundation.team||worker->order!=Order::Construct||worker->target!=foundation.id)return;
 const bool resume=worker->resumeGather;abandonConstruction(*worker,false);
 worker->resumeGather=resume;
 if(activateSuccessor&&activateNextOrder(*worker))return;
 if(resume)resumeOriginalGather(*worker);
}
void Simulation::assignConstruction(Entity& foundation,Entity& worker) {
 const bool resume=worker.resumeGather||worker.order==Order::Gather;
 abandonConstruction(worker,false);releaseConstruction(foundation);
 clearSustainedOrder(worker);worker.hasArrivalFacing=false;worker.arrivalFacing=0;
 worker.resumeGather=resume;worker.order=Order::Construct;worker.target=foundation.id;worker.goal=foundation.pos;
 resetNavigation(worker);foundation.builderId=worker.id;
}

CommandResult Simulation::command(const Command& input) {
 auto fail=[](const std::string& why){return CommandResult{false,why};};
 if(replica_)return fail("Online replicas accept server snapshots only.");
 if(winner_!=-1)return fail("The match has ended.");
 Command cmd=input;
 // Submitted negative zero is equivalent to positive zero. Persisted and wire
 // state remain stricter so equivalent intents cannot acquire distinct hashes.
 if(cmd.arrivalFacing==0.0f)cmd.arrivalFacing=0.0f;
 if(!activeTeam(cmd.team)||!rules::validKind(cmd.kind)||!finite(cmd.point)||cmd.point.x<0||cmd.point.y<0||cmd.point.x>worldSize()||cmd.point.y>worldSize())return fail("Invalid command.");
 if(eliminated(cmd.team))return fail("That player has been eliminated.");
 const int rawType=static_cast<int>(cmd.type);
 const int rawMode=static_cast<int>(cmd.queueMode);
 if(rawType<0||rawType>static_cast<int>(CommandType::Escort)||rawMode<static_cast<int>(CommandQueueMode::Replace)||rawMode>static_cast<int>(CommandQueueMode::Append)||cmd.units.size()>500)return fail("Invalid command.");
 if(!rules::validFormationSpacing(cmd.spacing)||!rules::validCanonicalArrivalFacing(cmd.arrivalFacing)||
    (!cmd.hasArrivalFacing&&cmd.arrivalFacing!=0.0f))return fail("Invalid formation modifiers.");
 const bool formationCommand=cmd.type==CommandType::Move||cmd.type==CommandType::AttackMove||cmd.type==CommandType::Defend;
 if(!formationCommand&&(cmd.spacing!=FormationSpacing::Standard||cmd.hasArrivalFacing||cmd.arrivalFacing!=0.0f))return fail("Formation modifiers apply only to Move, Attack-move, and Defend.");
 const bool automatic=rawType>=static_cast<int>(CommandType::AutoBuild)&&rawType<=static_cast<int>(CommandType::AutoRally);
 const bool append=cmd.queueMode==CommandQueueMode::Append;
 if(append&&cmd.type!=CommandType::Move&&cmd.type!=CommandType::AttackMove&&
    cmd.type!=CommandType::Build&&cmd.type!=CommandType::ResumeConstruction&&cmd.type!=CommandType::Gather)
  return fail("Only movement, construction, and mining orders can be queued.");
 if(append&&cmd.type==CommandType::Build&&(cmd.target||cmd.queueIndex))return fail("Invalid queued construction plan.");
 if(append&&cmd.type==CommandType::ResumeConstruction&&(!cmd.target||cmd.queueIndex||cmd.kind!=Kind::Worker))
  return fail("Invalid queued construction resume.");
 if(append&&cmd.type==CommandType::Gather&&(!cmd.target||cmd.queueIndex||cmd.kind!=Kind::Worker))
  return fail("Invalid queued mining plan.");
 if(cmd.type==CommandType::ClearOrders&&(append||cmd.target||cmd.queueIndex))return fail("Clear queued orders must be a standalone replacement command.");
 const bool sustainedCommand=cmd.type==CommandType::Patrol||cmd.type==CommandType::Escort;
 if(sustainedCommand&&(append||cmd.queueIndex))return fail("Patrol and Escort are replacement orders.");
 if(cmd.type==CommandType::Patrol&&cmd.target)return fail("Patrol requires a ground destination.");
 if(cmd.type==CommandType::Escort&&(!cmd.target||cmd.point.x!=0||cmd.point.y!=0))return fail("Escort requires one friendly mobile target.");
 if(automatic&&(!cmd.units.empty()||cmd.queueIndex<0||cmd.queueIndex>MaxQueue))return fail("Invalid automatic job request.");
 if((cmd.type==CommandType::AutoBuild&&(cmd.target||cmd.queueIndex))||
    (cmd.type==CommandType::AutoRally&&(cmd.queueIndex>1||(cmd.queueIndex==1&&!cmd.target))))return fail("Invalid automatic job request.");
 if(cmd.type==CommandType::CancelQueue&&cmd.units.size()!=1)return fail("Choose one producer and a valid queue item.");
 std::sort(cmd.units.begin(),cmd.units.end());cmd.units.erase(std::unique(cmd.units.begin(),cmd.units.end()),cmd.units.end());
 std::vector<Id> ids;
 for(Id id:cmd.units) { const Entity* e=find(id);if(!e||!e->alive()||e->team!=cmd.team||e->kind==Kind::Resource)return fail("The selection contains unavailable or foreign units.");ids.push_back(id); }
 if(ids.empty()&&!automatic)return fail("Select your units or a structure first.");
 Player& player=players_[cmd.team];std::string feedback="Order acknowledged.";
 auto commitAppended=[&](const std::vector<std::pair<Id,TacticalOrder>>& planned,const std::string& message)->CommandResult {
  std::size_t aggregate=0;for(const auto& entity:entities_)if(entity.alive()&&entity.team==cmd.team)aggregate+=entity.futureOrders.size();
  for(const auto& item:planned) {
   const Entity* entity=find(item.first);if(!entity)return fail("The selection changed before the order was accepted.");
   const bool workerJob=entity->kind==Kind::Worker&&
      (item.second.order==Order::Construct||item.second.order==Order::Gather);
   const bool willStoreInFuture=entity->order!=Order::Idle||!entity->futureOrders.empty()||workerJob;
   if(willStoreInFuture) {
    if(entity->futureOrders.size()>=MaxFutureOrders)return fail("A unit already has the maximum 16 queued orders.");
    if(++aggregate>MaxFutureOrdersPerPlayer)return fail("This player already has the maximum 4096 queued orders.");
   }
  }
  if(!net::orderPlanFitsSnapshot(*this,cmd.team,planned))return fail("The queued plan is too large for an online snapshot.");
  for(const auto& item:planned) {
   Entity* entity=get(item.first);
   const bool workerJob=entity->kind==Kind::Worker&&
      (item.second.order==Order::Construct||item.second.order==Order::Gather);
   if(entity->order==Order::Idle&&entity->futureOrders.empty()&&workerJob)entity->futureOrders.push_back(item.second);
   else if(entity->order==Order::Idle&&entity->futureOrders.empty())installTacticalOrder(*entity,item.second);
   else entity->futureOrders.push_back(item.second);
  }
  recording_.push_back({tick_,cmd});if(cmd.team==0)alert_=message;
  if(!wholeStepActive_)processQueuedWorkerOrders();
  return {true,message};
 };
 if(append&&(cmd.type==CommandType::Build||cmd.type==CommandType::ResumeConstruction)) {
  if(ids.size()!=1||find(ids.front())->kind!=Kind::Worker)return fail("Select exactly one Drudge for queued construction.");
  TacticalOrder plan;plan.order=Order::Construct;
  if(cmd.type==CommandType::Build) {
   if(!definition(cmd.kind).building)return fail("Only structures can be deployed.");
   std::string reason;if(!canPlace(cmd.team,cmd.kind,cmd.point,&reason))return fail(reason);
   if(!reachableConstructionWorker(ids,cmd.point,cmd.kind))return fail("No accessible route to this construction site.");
   plan.point=cmd.point;plan.buildingKind=cmd.kind;
  } else {
   const Entity* foundation=find(cmd.target);
   if(!foundation||!foundation->alive()||foundation->team!=cmd.team||
      !definition(foundation->kind).building||foundation->progress>=1)return fail("Choose your unfinished structure.");
   if(!reachableConstructionWorker(ids,foundation->pos,foundation->kind,foundation->id))
    return fail("The selected Drudge has no accessible route to this foundation.");
   plan.point=foundation->pos;plan.supportTarget=foundation->id;
  }
  return commitAppended({{ids.front(),plan}},cmd.type==CommandType::Build?"Construction queued.":"Construction resume queued.");
 } else if(cmd.type==CommandType::Build||cmd.type==CommandType::AutoBuild) {
  Id workerId=0;const auto status=checkBuild(cmd.team,cmd.kind,ids,&cmd.point,&workerId,cmd.type==CommandType::AutoBuild);
  if(!status.accepted)return status;
  if(cmd.type==CommandType::Build)get(workerId)->futureOrders.clear();
  player.ore-=definition(cmd.kind).cost;const Id foundationId=spawn(cmd.kind,cmd.team,cmd.point,false);
  assignConstruction(*get(foundationId),*get(workerId));
  if(cmd.type==CommandType::AutoBuild) {
   get(workerId)->resumeGather=true;
   feedback="Drudge #"+std::to_string(workerId)+" assigned to "+definition(cmd.kind).name+" #"+std::to_string(foundationId)+"; returns to mining afterward.";
  } else feedback=std::string(definition(cmd.kind).name)+" foundation placed; Drudge assigned.";
 } else if(cmd.type==CommandType::ResumeConstruction) {
  Entity* foundation=get(cmd.target);
  if(!foundation||!foundation->alive()||foundation->team!=cmd.team||!definition(foundation->kind).building||foundation->progress>=1)return fail("Choose your unfinished structure.");
  const Id workerId=reachableConstructionWorker(ids,foundation->pos,foundation->kind,foundation->id);
  if(!workerId)return fail("No selected Drudge has an accessible route to this foundation.");
  get(workerId)->futureOrders.clear();
  assignConstruction(*foundation,*get(workerId));feedback="Drudge assigned to resume construction.";
 } else if(cmd.type==CommandType::AutoTrain) {
  const auto plan=autoTrainStatus(cmd.team,cmd.kind,cmd.queueIndex,cmd.target);
  if(!plan.accepted)return {false,plan.message};
  const auto& d=definition(cmd.kind);
  for(const auto& assignment:plan.assignments) {
   Entity* producer=get(assignment.producer);
   if(producer->queue.empty()){producer->repath=0;producer->pathGeometry=0;}
   for(int item=0;item<assignment.quantity;++item) {
    const float duration=productionTime(d.buildTime);
    producer->queue.push_back({cmd.kind,duration,duration,d.cost,false,producer->nextQueueId++,0,{}});
   }
  }
  player.ore-=plan.totalCost;
  feedback=std::to_string(plan.quantity)+" "+d.name+" queued across "+std::to_string(plan.assignments.size())+" producer(s).";
 } else if(cmd.type==CommandType::AutoResearch) {
  const auto plan=autoResearchStatus(cmd.team,cmd.queueIndex,cmd.target);
  if(!plan.accepted)return {false,plan.message};
  Entity* lab=get(plan.assignments.front().producer);
  const int level=cmd.queueIndex==0?player.tier:cmd.queueIndex==1?player.weapons:player.armor;
  const float duration=productionTime(cmd.queueIndex==0?100.0f*level:60.0f);
  lab->queue.push_back({rules::researchKind(cmd.queueIndex),duration,duration,plan.totalCost,true,lab->nextQueueId++,0,{}});
  player.ore-=plan.totalCost;
  feedback="Research assigned to "+std::string(definition(lab->kind).name)+" #"+std::to_string(lab->id)+".";
 } else if(cmd.type==CommandType::AutoRally) {
  const bool useDefault=cmd.queueIndex==1;
  const auto status=autoRallyStatus(cmd.team,cmd.kind,cmd.target,useDefault);
  if(!status.accepted)return status;
  if(useDefault) {
   Entity* producer=get(cmd.target);producer->rallyOverride=false;producer->repath=0;producer->pathGeometry=0;
   if(producer->kind==Kind::Headquarters) {
    producer->rally=bounded(add(producer->pos,startOffset(producer->team,{190,0})),worldSize());
    for(auto& item:producer->queue)if(item.kind==Kind::Worker&&!item.research){item.assignmentCursor=0;item.assignmentCandidates.clear();}
    feedback="Anchor restored automatic mining.";
   } else {producer->rally=player.armyRally;feedback="Producer now uses the team army rally point.";}
   cmd.point={};
  } else if(!cmd.target&&cmd.kind==Kind::Resource) {
   player.armyRally=bounded(cmd.point,worldSize());player.armyRallySet=true;
   for(auto& entity:entities_)if(entity.alive()&&entity.team==cmd.team&&rules::combatProductionKind(entity.kind)&&!entity.rallyOverride)
    entity.rally=player.armyRally;
   feedback="Team army rally point updated.";
  } else {
   for(auto& entity:entities_)if(entity.alive()&&entity.team==cmd.team&&entity.progress>=1&&rules::productionKind(entity.kind)&&
      (!cmd.target||entity.id==cmd.target)&&(cmd.kind==Kind::Resource||entity.kind==cmd.kind)) {
    entity.rally=bounded(cmd.point,worldSize());entity.rallyOverride=true;entity.repath=0;entity.pathGeometry=0;
    if(entity.kind==Kind::Headquarters)for(auto& item:entity.queue)if(item.kind==Kind::Worker&&!item.research) {
     item.assignmentCursor=0;item.assignmentCandidates.clear();
    }
   }
   feedback="Production rally point updated.";
  }
 } else if(cmd.type==CommandType::ClearOrders) {
  bool mobile=false;for(Id id:ids)if(!definition(get(id)->kind).building) {
   Entity* entity=get(id);entity->futureOrders.clear();
   if(entity->kind==Kind::Worker&&entity->order==Order::Idle&&entity->resumeGather) {
    resetNavigation(*entity);resumeOriginalGather(*entity);
   }
   mobile=true;
  }
  if(!mobile)return fail("Select mobile units to clear queued orders.");
  feedback="Queued orders cleared.";
 } else if(cmd.type==CommandType::Train) {
  const auto& d=definition(cmd.kind);if(d.building||cmd.kind==Kind::Resource)return fail("Choose a unit to train.");
  Entity* producer=nullptr;bool operational=false;for(Id id:ids){auto* e=get(id);if(e->kind==d.producer&&e->progress>=1){operational=true;if(e->queue.size()<MaxQueue&&e->nextQueueId>0&&e->nextQueueId<std::numeric_limits<Id>::max()){producer=e;break;}}}
  if(!producer)return fail(operational?"Queue full ("+std::to_string(MaxQueue)+").":"Select an operational producer with queue space.");
  if(player.tier<d.tier)return fail("Research the required technology tier first.");
  if(player.ore<d.cost)return fail("Insufficient ore.");
  if(supply(cmd.team)+d.supply>capacity(cmd.team))return fail("Supply full. Build a Siphon or Anchor.");
  const float duration=productionTime(d.buildTime);
  if(producer->queue.empty()){producer->repath=0;producer->pathGeometry=0;}
  producer->queue.push_back({cmd.kind,duration,duration,d.cost,false,producer->nextQueueId++,0,{}});player.ore-=d.cost;
  feedback=std::string(d.name)+" queued.";
 } else if(cmd.type==CommandType::Research) {
  if(cmd.queueIndex<0||cmd.queueIndex>2)return fail("Unknown research.");
  Entity* lab=nullptr;bool operational=false;for(Id id:ids){auto* e=get(id);if(e->kind==Kind::Laboratory&&e->progress>=1){operational=true;if(e->queue.size()<MaxQueue&&e->nextQueueId>0&&e->nextQueueId<std::numeric_limits<Id>::max()){lab=e;break;}}}
  if(!lab)return fail(operational?"Queue full ("+std::to_string(MaxQueue)+").":"Select an operational Resonator with queue space.");
  const Kind itemKind=rules::researchKind(cmd.queueIndex);
  for(const auto& e:entities_)if(e.alive()&&e.team==cmd.team)for(const auto& q:e.queue)if(q.research&&q.kind==itemKind)return fail("This research is already queued.");
  int level=cmd.queueIndex==0?player.tier:cmd.queueIndex==1?player.weapons:player.armor;
  if(level>=3)return fail("Research is already at its maximum level.");
  if(cmd.queueIndex>0&&level>=player.tier)return fail("Advance your technology tier first.");
  int cost=cmd.queueIndex==0?500*level:200*(level+1);float duration=productionTime(cmd.queueIndex==0?100.0f*level:60.0f);
  if(player.ore<cost)return fail("Insufficient ore.");
  lab->queue.push_back({itemKind,duration,duration,cost,true,lab->nextQueueId++,0,{}});player.ore-=cost;feedback="Research queued.";
 } else if(cmd.type==CommandType::CancelQueue) {
  if(ids.size()!=1||cmd.queueIndex<0||cmd.queueIndex>=MaxQueue)return fail("Choose one producer and a valid queue item.");
  Entity* e=get(ids.front());
  auto itemPosition=cmd.target?std::find_if(e->queue.begin(),e->queue.end(),[&](const QueueItem& item){return item.id==cmd.target;}):
   (cmd.queueIndex<static_cast<int>(e->queue.size())?e->queue.begin()+cmd.queueIndex:e->queue.end());
  if(itemPosition==e->queue.end())return fail("That production job is no longer queued.");
  const QueueItem item=*itemPosition;float fraction=item.total>0?std::clamp(item.remaining/item.total,0.0f,1.0f):1;
  if(itemPosition==e->queue.begin()){e->repath=0;e->pathGeometry=0;}
  player.ore+=static_cast<int>(std::floor(item.cost*fraction+0.001f));e->queue.erase(itemPosition);feedback="Queue item cancelled; unused ore refunded.";
 } else if(cmd.type==CommandType::CancelBuilding) {
  bool cancelled=false;for(Id id:ids){auto* e=get(id);if(definition(e->kind).building&&e->progress<1){player.ore+=static_cast<int>(std::floor(definition(e->kind).cost*(1-e->progress)));releaseConstruction(*e);e->hp=0;navigationDirty_=true;e->queue.clear();cancelled=true;}}
  if(!cancelled)return fail("Select an unfinished structure.");feedback="Construction cancelled; unused ore refunded.";
 } else if(cmd.type==CommandType::Rally) {
  bool accepted=false;for(Id id:ids){auto* e=get(id);if(definition(e->kind).building){e->rally=bounded(cmd.point,worldSize());e->repath=0;e->pathGeometry=0;if(rules::productionKind(e->kind))e->rallyOverride=true;
   if(e->kind==Kind::Headquarters)for(auto& item:e->queue)if(item.kind==Kind::Worker&&!item.research){item.assignmentCursor=0;item.assignmentCandidates.clear();}
   accepted=true;}}
  if(!accepted)return fail("Select a production structure.");feedback="Rally point updated.";
 } else {
  const Entity* target=find(cmd.target);
  if(cmd.type==CommandType::Attack) {
   if(!target||!target->alive()||target->team==cmd.team||target->team<0||!visible(cmd.team,target->pos))return fail("Choose a visible enemy.");
  }
  if(cmd.type==CommandType::Gather&&(!target||!target->alive()||target->kind!=Kind::Resource||target->resource<=0||!explored(cmd.team,target->pos)))return fail("Choose an explored ore deposit.");
  if(cmd.type==CommandType::Escort&&(!target||!target->alive()||target->team!=cmd.team||target->id==0||definition(target->kind).building||target->kind==Kind::Resource))return fail("Choose one of your mobile units to escort.");
  std::vector<Id> movable;for(Id id:ids){const auto* e=find(id);if(!definition(e->kind).building&&
    (cmd.type!=CommandType::Gather||e->kind==Kind::Worker)&&
    (cmd.type!=CommandType::Attack||definition(e->kind).damage>0)&&
    !(cmd.type==CommandType::Attack&&definition(target->kind).air&&!definition(e->kind).antiAir)&&
    !(cmd.type==CommandType::Escort&&id==cmd.target))movable.push_back(id);}
  if(movable.empty())return fail("Selected units cannot execute this order.");
  if(append&&cmd.type==CommandType::Gather)for(Id id:movable)
   if(!queuedGatherReachable(*find(id),target->id))
    return fail("A selected Drudge has no accessible route to that ore deposit.");
  Id attackLeader=0;
  if(cmd.type==CommandType::Attack||cmd.type==CommandType::AttackMove)for(Id id:movable)if(definition(find(id)->kind).damage>0){attackLeader=id;break;}
  if(cmd.type==CommandType::Attack&&attackLeader)for(Id id:ids)if(find(id)->kind==Kind::Mender)movable.push_back(id);
  std::vector<std::pair<Id,TacticalOrder>> normalized;
  if(formationCommand||cmd.type==CommandType::Patrol) {
   struct ReservedGoal { Id id=0;Vec2 point{};float radius=0;bool air=false; };
   std::vector<rules::FormationRecipient> recipients;
   recipients.reserve(movable.size());
   for(Id id:movable)recipients.push_back({id,find(id)->pos});
   std::vector<rules::NominalFormationSlot> nominal;
   const FormationSpacing spacing=formationCommand?cmd.spacing:FormationSpacing::Standard;
   const bool hasFacing=formationCommand&&cmd.hasArrivalFacing;
   const float facing=hasFacing?cmd.arrivalFacing:0.0f;
   if(!rules::nominalFormationSlots(cmd.point,spacing,hasFacing,facing,recipients,nominal))return fail("Invalid formation layout.");

   std::vector<ReservedGoal> held;
   if(formationCommand)for(const auto& candidate:entities_) {
    if(!candidate.alive()||candidate.order!=Order::Hold||definition(candidate.kind).building||candidate.kind==Kind::Resource)continue;
    const bool selected=std::binary_search(movable.begin(),movable.end(),candidate.id);
    if(selected&&!append)continue;
    held.push_back({candidate.id,candidate.pos,definition(candidate.kind).radius,definition(candidate.kind).air});
   }
   std::vector<ReservedGoal> assignedGoals;
   normalized.reserve(nominal.size());
   for(const auto& slot:nominal) {
    const Entity* e=find(slot.id);if(!e)return fail("The selection changed before the order was accepted.");
    const auto& unit=definition(e->kind);const Vec2 desired=bounded(slot.point,worldSize(),unit.radius);Vec2 destination=desired;
    auto available=[&](Vec2 point) {
     if(point.x<unit.radius||point.y<unit.radius||point.x>worldSize()-unit.radius||point.y>worldSize()-unit.radius)return false;
     if(!unit.air&&blocked(point,unit.radius+4,e->id))return false;
     for(const auto& assigned:assignedGoals) {const float clearance=unit.radius+assigned.radius+10;if(distanceSq(point,assigned.point)<clearance*clearance)return false;}
     for(const auto& hold:held) {
      if(hold.air!=unit.air)continue;
      if(e->order==Order::Hold&&append&&hold.id==e->id)continue;
      const float clearance=unit.radius+hold.radius+10;if(distanceSq(point,hold.point)<clearance*clearance)return false;
     }
     return true;
    };
    bool found=available(destination);
    for(int ring=1;ring<=18&&!found;++ring) {
     float best=std::numeric_limits<float>::max();
     for(int spoke=0;spoke<24;++spoke) {
      const float angle=spoke*(2*Pi/24);Vec2 candidate=add(desired,{std::cos(angle)*ring*40,std::sin(angle)*ring*40});
      if(!available(candidate))continue;const float score=distanceSq(candidate,desired)+distanceSq(candidate,e->pos)*0.001f;
      if(score<best){best=score;destination=candidate;found=true;}
     }
    }
    if(!found)return fail("The complete formation does not fit at that destination.");
    TacticalOrder order;
    order.order=cmd.type==CommandType::Move?Order::Move:cmd.type==CommandType::AttackMove?Order::AttackMove:
      cmd.type==CommandType::Patrol?Order::Patrol:Order::Defend;
    order.point=destination;order.hasArrivalFacing=formationCommand&&cmd.hasArrivalFacing;
    order.arrivalFacing=order.hasArrivalFacing?cmd.arrivalFacing:0.0f;
    if(order.order==Order::AttackMove&&e->kind==Kind::Mender)order.supportTarget=attackLeader;
    normalized.push_back({e->id,order});assignedGoals.push_back({e->id,destination,unit.radius,unit.air});
   }
  } else if(cmd.type==CommandType::Gather) {
   normalized.reserve(movable.size());
   for(Id id:movable) {
    TacticalOrder order;order.order=Order::Gather;order.supportTarget=cmd.target;
    normalized.push_back({id,order});
   }
  }
  std::vector<Entity> sustainedCandidates;
  if(sustainedCommand) {
   sustainedCandidates.reserve(movable.size());
   if(cmd.type==CommandType::Patrol) {
    for(std::size_t n=0;n<movable.size();++n) {
     Entity candidate=*find(movable[n]);
     candidate.order=Order::Patrol;candidate.goal=normalized[n].second.point;
     candidate.target=0;candidate.supportTarget=0;candidate.returning=false;candidate.resumeGather=false;
     candidate.hasArrivalFacing=false;candidate.arrivalFacing=0;
     candidate.workTarget=0;candidate.workPoint={};candidate.workPointValid=false;candidate.futureOrders.clear();
     candidate.sustained={};candidate.sustained.patrolOrigin=candidate.pos;
     candidate.sustained.patrolDestination=candidate.goal;candidate.sustained.patrolTowardDestination=true;
     sustainedCandidates.push_back(std::move(candidate));
    }
   } else {
    struct ReservedEscortGoal { Vec2 point{}; float radius=0; };
    std::vector<Vec2> usedOffsets;
    std::vector<ReservedEscortGoal> usedGoals;
    for(const auto& existing:entities_) {
     if(existing.order!=Order::Escort||existing.sustained.escortTarget!=target->id||
        std::find(movable.begin(),movable.end(),existing.id)!=movable.end())continue;
     usedOffsets.push_back(existing.sustained.escortOffset);
     usedGoals.push_back({escortFollowPoint(existing,*target),definition(existing.kind).radius});
    }
    for(Id id:movable) {
     const Entity* source=find(id);Vec2 chosen{},chosenGoal{};bool found=false;
     const float clearance=definition(source->kind).radius+definition(target->kind).radius+10.0f;
     Entity candidate=*source;
     candidate.order=Order::Escort;candidate.target=0;candidate.supportTarget=0;
     candidate.hasArrivalFacing=false;candidate.arrivalFacing=0;
     candidate.returning=false;candidate.resumeGather=false;candidate.workTarget=0;candidate.workPoint={};candidate.workPointValid=false;
     candidate.futureOrders.clear();candidate.sustained={};candidate.sustained.escortTarget=target->id;
     for(int ring=1;ring<=static_cast<int>(MaxEscortOffset/EscortSpacing)&&!found;++ring) {
      for(int y=-ring;y<=ring&&!found;++y)for(int x=-ring;x<=ring&&!found;++x) {
       if(std::max(std::abs(x),std::abs(y))!=ring)continue;
       const Vec2 offset{x*EscortSpacing,y*EscortSpacing};
       if(lengthSq(offset)>MaxEscortOffset*MaxEscortOffset||lengthSq(offset)<clearance*clearance)continue;
       if(std::find_if(usedOffsets.begin(),usedOffsets.end(),[&](Vec2 used){return used.x==offset.x&&used.y==offset.y;})!=usedOffsets.end())continue;
       candidate.sustained.escortOffset=offset;
       const Vec2 goal=escortFollowPoint(candidate,*target);
       const float radius=definition(source->kind).radius;
       if(std::any_of(usedGoals.begin(),usedGoals.end(),[&](const ReservedEscortGoal& used) {
        const float required=radius+used.radius;
        return distanceSq(goal,used.point)<required*required;
       }))continue;
       chosen=offset;chosenGoal=goal;found=true;
      }
     }
     if(!found)return fail("The escort formation is too large.");
     usedOffsets.push_back(chosen);usedGoals.push_back({chosenGoal,definition(source->kind).radius});
     candidate.sustained.escortOffset=chosen;candidate.goal=chosenGoal;
     sustainedCandidates.push_back(std::move(candidate));
    }
   }
   std::vector<Entity> projected=entities_;
   for(const auto& candidate:sustainedCandidates) {
    auto found=std::find_if(projected.begin(),projected.end(),[&](const Entity& entity){return entity.id==candidate.id;});
    if(found==projected.end())return fail("The selection changed before the order was accepted.");
    *found=candidate;
   }
   if(!validateSustainedState(projected))return fail(cmd.type==CommandType::Escort?"That escort order would create an invalid follow chain.":"Invalid patrol route.");
   if(!net::sustainedPlanFitsSnapshot(*this,cmd.team,sustainedCandidates))return fail("The sustained order is too large for an online snapshot.");
   for(const auto& candidate:sustainedCandidates) {
    Entity* entity=get(candidate.id);entity->futureOrders.clear();
    if(entity->kind==Kind::Worker)abandonConstruction(*entity,false);
    resetNavigation(*entity);entity->order=candidate.order;entity->goal=candidate.goal;
    entity->target=0;entity->supportTarget=0;entity->returning=false;entity->resumeGather=false;
    entity->hasArrivalFacing=false;entity->arrivalFacing=0;
    entity->workTarget=0;entity->workPoint={};entity->workPointValid=false;entity->sustained=candidate.sustained;
   }
   feedback=cmd.type==CommandType::Patrol?"Patrol route accepted.":"Escort formation assigned.";
  } else if(append) {
   return commitAppended(normalized,cmd.type==CommandType::Gather?"Mining queued.":"Destination queued.");
  } else {
   for(Id id:movable)get(id)->futureOrders.clear();
   for(std::size_t n=0;n<movable.size();++n) {
    Entity* e=get(movable[n]);if(e->kind==Kind::Worker)abandonConstruction(*e,false);resetNavigation(*e);e->target=0;e->supportTarget=0;e->hasArrivalFacing=false;e->arrivalFacing=0;clearSustainedOrder(*e);
    switch(cmd.type) {
     case CommandType::Stop:e->order=Order::Idle;e->goal=e->pos;break;
     case CommandType::Hold:e->order=Order::Hold;e->goal=e->pos;break;
     case CommandType::Gather:e->order=Order::Gather;e->target=cmd.target;e->resourceTarget=cmd.target;e->returning=e->carried>=CarryCapacity;break;
     case CommandType::Attack:e->order=Order::Attack;e->target=e->kind==Kind::Mender?attackLeader:cmd.target;e->goal=find(e->target)->pos;break;
     case CommandType::Move:case CommandType::AttackMove:case CommandType::Defend: {
      const auto planned=std::find_if(normalized.begin(),normalized.end(),[&](const auto& item){return item.first==e->id;});
      if(planned!=normalized.end())installTacticalOrder(*e,planned->second);break;
     }
     default:break;
    }
   }
  }
 }
 recording_.push_back({tick_,cmd});if(cmd.team==0)alert_=feedback;
 if(!wholeStepActive_)processQueuedWorkerOrders();return {true,feedback};
}

void Simulation::update(float seconds) {
 if(replica_)return;
 if(!std::isfinite(seconds)||seconds<=0||winner_!=-1)return;
 accumulator_=std::min(accumulator_+std::min(seconds,1.0f),1.0f);
 while(accumulator_+0.000001f>=Step&&winner_==-1) {
  accumulator_=std::max(0.0f,accumulator_-Step);step();
 }
 if(accumulator_<0.000001f||winner_!=-1)accumulator_=0;
}
void Simulation::step() {
 const auto started=std::chrono::steady_clock::now();++tick_;navigationDirty_=true;
 auto phaseStarted=started;
 SimulationStepProfile profile;
 auto finishPhase=[&](double SimulationStepProfile::*field) {
  if(!profilingEnabled_)return;
  const auto now=std::chrono::steady_clock::now();
  profile.*field=std::chrono::duration<double,std::milli>(now-phaseStarted).count();
  phaseStarted=now;
 };
 wholeStepActive_=true;isStepping_=true;movementRouteRequests_=0;
 for(auto& e:entities_)if(e.alive()){e.cooldown=std::max(0.0f,e.cooldown-Step);e.repath=std::max(0.0f,e.repath-Step);}
 for(auto& fx:effects_)fx.life-=Step;
 effects_.erase(std::remove_if(effects_.begin(),effects_.end(),[](const Effect& fx){return fx.life<=0;}),effects_.end());
 finishPhase(&SimulationStepProfile::setupMs);
 // Production can append to entities_; retain IDs and reacquire each object after it does.
 std::vector<Id> production;
 for(const auto& e:entities_)if(e.alive()&&definition(e.kind).building)production.push_back(e.id);
 for(Id id:production){Entity* e=get(id);if(e&&e->alive())updateProduction(*e);}
 finishPhase(&SimulationStepProfile::productionMs);
 beginMovementStep();
 for(auto& e:entities_)if(e.alive()&&e.progress>=1){if(e.kind==Kind::Worker&&e.order==Order::Gather)updateEconomy(e);else updateMovement(e);}
 finishMovementStep();
 finishPhase(&SimulationStepProfile::movementEconomyMs);
 updateVision();
 finishPhase(&SimulationStepProfile::visionMs);
 for(auto& e:entities_)if(e.alive()&&e.progress>=1)updateCombat(e);
 finishPhase(&SimulationStepProfile::combatMs);
 if(config_.ai) {aiTimer_-=Step;if(aiTimer_<=0){aiTimer_=2;updateAI();}}
 finishPhase(&SimulationStepProfile::aiMs);
 updateEliminations();
 processQueuedWorkerOrders();
 if(tick_%100==0)entities_.erase(std::remove_if(entities_.begin(),entities_.end(),[](const Entity& e){return !e.alive();}),entities_.end());
 finishPhase(&SimulationStepProfile::completionMs);
 lastStepMs_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
 if(profilingEnabled_) {
  profile.collected=true;profile.tick=tick_;profile.totalMs=lastStepMs_;lastStepProfile_=profile;
 }
 wholeStepActive_=false;
}

void Simulation::eliminateTeam(int team) {
 if(!activeTeam(team)||eliminated(team))return;
 // Release paired construction state before disabling the defeated army. No
 // defeated actor may retain a live queue, target or navigation reservation.
 for(auto& entity:entities_)if(entity.alive()&&entity.team==team)entity.futureOrders.clear();
 for(auto& entity:entities_)if(entity.alive()&&entity.team==team&&entity.kind==Kind::Worker)abandonConstruction(entity,false);
 for(auto& entity:entities_)if(entity.alive()&&entity.team==team&&definition(entity.kind).building)releaseConstruction(entity,false);
 std::vector<Id> destroyed;
 for(auto& entity:entities_)if(entity.alive()&&entity.team==team) {
  destroyed.push_back(entity.id);
  entity.hp=0;entity.queue.clear();entity.target=0;entity.supportTarget=0;entity.resourceTarget=0;entity.builderId=0;
  entity.order=Order::Idle;entity.goal=entity.pos;entity.returning=false;entity.resumeGather=false;
  entity.hasArrivalFacing=false;entity.arrivalFacing=0;
  clearSustainedOrder(entity);
  resetNavigation(entity);
 }
 for(Id id:destroyed)clearSustainedReferences(id);
 for(auto& entity:entities_) {
  const Entity* support=find(entity.supportTarget);
  if(entity.supportTarget&&(!support||!support->alive()))entity.supportTarget=0;
  for(auto& order:entity.futureOrders) {
   support=find(order.supportTarget);
   if(order.order==Order::AttackMove&&order.supportTarget&&(!support||!support->alive()))order.supportTarget=0;
  }
 }
 eliminatedMask_|=static_cast<std::uint8_t>(1u<<team);navigationDirty_=true;
}

void Simulation::updateWinner() {
 const int survivors=rules::survivorCount(config_.playerCount,eliminatedMask_);
 if(survivors>1){winner_=-1;if(eliminated(0))alert_=std::to_string(survivors)+" players remain after your elimination.";return;}
 accumulator_=0;
 if(survivors==0){winner_=-2;alert_="Draw — no Anchors survived.";return;}
 for(int team=0;team<config_.playerCount;++team)if(!eliminated(team)) {
  winner_=team;alert_=team==0?"Victory — every opposing Anchor was destroyed.":"Defeat — your Anchor was destroyed.";return;
 }
}

void Simulation::updateEliminations() {
 std::uint8_t defeated=0;
 for(int team=0;team<config_.playerCount;++team)if(!eliminated(team)) {
  const bool hasAnchor=std::any_of(entities_.begin(),entities_.end(),[&](const Entity& entity) {
   return entity.alive()&&entity.team==team&&entity.kind==Kind::Headquarters;
  });
  if(!hasAnchor)defeated|=static_cast<std::uint8_t>(1u<<team);
 }
 for(int team=0;team<config_.playerCount;++team)if(defeated&(1u<<team))eliminateTeam(team);
 if(defeated){updateVision();updateWinner();}
}

void Simulation::updateEconomy(Entity& worker) {
 if(worker.kind!=Kind::Worker||worker.order!=Order::Gather)return;
 const bool constructionNext=!worker.futureOrders.empty()&&worker.futureOrders.front().order==Order::Construct;
 if(constructionNext&&!worker.returning) {
  worker.resumeGather=true;
  if(worker.carried>0) {worker.returning=true;resetNavigation(worker);}
  else {resetCurrentOrder(worker);activateNextOrder(worker);return;}
 }
 ensureNavigation();
 if(worker.carried>=CarryCapacity-0.001f)worker.returning=true;
 if(worker.returning) {
  const Entity* serviced=find(worker.resourceTarget);
  if(serviced&&serviced->alive()&&serviced->kind==Kind::Resource) {
   const float exitClearance=definition(serviced->kind).radius+definition(worker.kind).radius+0.25f;
   if(distanceSq(worker.pos,serviced->pos)<exitClearance*exitClearance) {
    // A worker may finish harvesting while slightly inside the resource's
    // routing clearance. Step out while ignoring only that serviced deposit;
    // otherwise every depot route rejects the blocked start and cargo strands.
    worker.yieldFor=std::max(worker.yieldFor,Step);
    yieldAtWork(worker,serviced->pos,serviced->id);return;
   }
  }
  std::vector<Id> depots;
  for(const auto& entity:entities_) {
   if(!entity.alive()||entity.team!=worker.team||entity.progress<1||
      (entity.kind!=Kind::Headquarters&&entity.kind!=Kind::Processor))continue;
   const float reach=definition(entity.kind).radius+definition(worker.kind).radius+15;
   if(distance(worker.pos,entity.pos)<=reach+7&&
      navigation_.segmentClear(worker.pos,entity.pos,definition(worker.kind).radius,entity.id)) {
    if(yieldAtWork(worker,entity.pos))return;
    const int ore=static_cast<int>(std::floor(worker.carried+0.001f));
    players_[worker.team].ore+=ore;players_[worker.team].stats.gathered+=ore;
    worker.carried=0;worker.returning=false;resetNavigation(worker);
    if(constructionNext) {worker.resumeGather=true;resetCurrentOrder(worker);activateNextOrder(worker);return;}
    const Entity* assigned=find(worker.resourceTarget);
    if(!worker.futureOrders.empty()&&(!assigned||!assigned->alive()||assigned->kind!=Kind::Resource||assigned->resource<=0)) {
     worker.resourceTarget=0;finishOrder(worker);
    }
    return;
   }
   depots.push_back(entity.id);
  }
  if(depots.empty()) {
   // An unfinished replacement Anchor can keep this player in the match.
   // Preserve its worker's cargo and delivery job until a depot is operational;
   // no route search is useful while there is nowhere to unload.
   if(!worker.navigationExhausted||worker.workTarget) {
    resetNavigation(worker);worker.navigationExhausted=true;
    if(worker.team==0)alert_="Drudge waiting for an operational Anchor or Siphon to deliver ore.";
   }
   return;
  }
  const Entity* depot=find(worker.workTarget);
  if(!depot||!worker.workPointValid||worker.navigationExhausted||
     std::find(depots.begin(),depots.end(),worker.workTarget)==depots.end()) {
   const Id chosen=chooseWorkTarget(worker,depots,15);
   if(!chosen)return;
   depot=find(chosen);
  }
  approachWork(worker,*depot,15);return;
 }
 const Entity* deposit=find(worker.resourceTarget);
 if(!deposit||!deposit->alive()||deposit->kind!=Kind::Resource||deposit->resource<=0) {
  if(!worker.futureOrders.empty()) {
   if(worker.carried>0){worker.returning=true;resetNavigation(worker);}
   else {worker.resourceTarget=0;worker.returning=false;finishOrder(worker);}
   return;
  }
  std::vector<Id> candidates;
  for(const auto& entity:entities_)if(entity.alive()&&entity.kind==Kind::Resource&&
      entity.resource>0&&explored(worker.team,entity.pos))candidates.push_back(entity.id);
  if(candidates.empty()) {
   if(worker.carried>0){worker.returning=true;resetNavigation(worker);}
   else {worker.resourceTarget=0;worker.returning=false;finishOrder(worker);}
   return;
  }
  const Id chosen=chooseWorkTarget(worker,candidates,9);
  if(!chosen)return;
  worker.resourceTarget=chosen;worker.target=chosen;deposit=find(chosen);
 }
 const float reach=definition(Kind::Resource).radius+definition(worker.kind).radius+9;
 if(distance(worker.pos,deposit->pos)<=reach+8&&
    navigation_.segmentClear(worker.pos,deposit->pos,definition(worker.kind).radius,deposit->id)) {
  if(yieldAtWork(worker,deposit->pos))return;
  worker.harvestTimer+=Step;
  if(worker.harvestTimer+0.00001f<HarvestPeriod)return;
  worker.harvestTimer-=HarvestPeriod;Entity* mutableDeposit=get(deposit->id);
  const float amount=std::min({3.0f,CarryCapacity-worker.carried,mutableDeposit->resource});
  worker.carried+=amount;mutableDeposit->resource-=amount;
  if(mutableDeposit->resource<=0)navigationDirty_=true;
  if(worker.carried>=CarryCapacity-0.001f||mutableDeposit->resource<=0) {
   worker.returning=true;resetNavigation(worker);
  }
 } else approachWork(worker,*deposit,9);
}

void Simulation::updateMovement(Entity& e) {
 const auto& d=definition(e.kind);if(d.building||e.kind==Kind::Resource||e.order==Order::Gather)return;
 if(e.order==Order::Construct) {
  Entity* foundation=get(e.target);
  if(!foundation||constructionWorker(foundation->id)!=e.id) {
   if(foundation&&foundation->builderId==e.id)releaseConstruction(*foundation);else abandonConstruction(e);
   return;
  }
  if(constructionActive(foundation->id)){e.path.clear();e.pathIndex=0;e.repath=0;return;}
  approachWork(e,*foundation,ConstructionPadding);return;
 }
 if(e.order==Order::Hold||e.order==Order::Defend) {
  if(distanceSq(e.pos,e.goal)>5*5)moveToward(e,e.goal);
  else if(e.order==Order::Defend&&(!e.path.empty()||e.pathIndex!=0||e.repath>0))resetNavigation(e);
  return;
 }
 auto pursue=[&](const Entity& target) {
  const auto& targetDefinition=definition(target.kind);const float range=d.range+targetDefinition.radius;
  const bool requiresSight=e.kind!=Kind::Mortar&&!d.air&&!targetDefinition.air;
  const bool shotClear=!requiresSight||clearFireLine(e.pos,target.pos,obstacles_);
  if(distance(e.pos,target.pos)<=range*0.95f&&shotClear)return;
  Vec2 destination=add(target.pos,scale(normalized(subtract(e.pos,target.pos)),range*0.82f));
  if(!shotClear) {
   // Range alone is not a firing position. Find a terrain-clear point on the
   // target's side of cover and let A* take the unit around the obstruction.
   bool cached=!e.path.empty()&&distance(e.path.back(),target.pos)<=range&&clearFireLine(e.path.back(),target.pos,obstacles_);
   if(cached)destination=e.path.back();
   else {
    float best=std::numeric_limits<float>::max();
    for(int spoke=0;spoke<24;++spoke) {
     const float angle=spoke*(2*Pi/24);Vec2 candidate=add(target.pos,{std::cos(angle)*range*0.82f,std::sin(angle)*range*0.82f});
     if(blocked(candidate,d.radius+3,e.id)||!clearFireLine(candidate,target.pos,obstacles_))continue;
     const float score=distanceSq(e.pos,candidate);if(score<best){best=score;destination=candidate;}
    }
   }
  }
  moveToward(e,destination);
 };
 if(e.order==Order::Patrol||e.order==Order::Escort) {
  if(!refreshSustainedOrder(e))return;
  if(e.order==Order::Escort&&e.yieldFor>0) {
   const Entity* leader=find(e.sustained.escortTarget);
   const float nearby=EscortSpacing+definition(e.kind).radius+
      (leader?definition(leader->kind).radius:0.0f);
   const bool leaderAtWork=leader&&
      (leader->order==Order::Gather||leader->order==Order::Construct);
   if(leaderAtWork&&distanceSq(e.pos,leader->pos)<nearby*nearby) {
    // Do not immediately steer back into a work or entry lane after the leader
    // asks this follower to yield. The existing work-area escape keeps the
    // response deterministic and the short yield timer resumes Escort normally.
    yieldAtWork(e,leader->pos);return;
   }
  }
  if(e.order==Order::Escort&&e.yieldFor>0&&
     e.sustained.phase==SustainedOrderPhase::Travel&&
     distanceSq(e.pos,e.goal)<(EscortSpacing*0.5f)*(EscortSpacing*0.5f))return;
  const auto& sustained=e.sustained;
  if(sustained.phase==SustainedOrderPhase::Pursuit) {
   const Entity* target=find(sustained.pursuitTarget);
   if(target){pursue(*target);return;}
  }
  if(e.order==Order::Patrol) {
   if(sustained.phase==SustainedOrderPhase::Return) {
    moveToward(e,e.goal);return;
   }
   // Endpoints closer than the phase tolerance are an accepted stationary
   // patrol. Do not flip its direction or rebuild an equivalent route forever.
   if(distanceSq(sustained.patrolOrigin,sustained.patrolDestination)<=
      SustainedReturnTolerance*SustainedReturnTolerance) {
    if(!e.path.empty()||e.pathIndex||e.repath>0)resetNavigation(e);
    return;
   }
   if(distanceSq(e.pos,e.goal)<20*20) {
    e.sustained.patrolTowardDestination=!e.sustained.patrolTowardDestination;
    e.goal=e.sustained.patrolTowardDestination?e.sustained.patrolDestination:e.sustained.patrolOrigin;
    resetNavigation(e);
   }
   moveToward(e,e.goal);return;
  }
  moveToward(e,e.goal);return;
 }
 if(e.kind==Kind::Mender&&e.order==Order::Attack) {
  const Entity* ally=find(e.target);
  if(!ally||!ally->alive()||ally->team!=e.team){finishOrder(e);return;}
  e.goal=ally->pos;
  if(distance(e.pos,ally->pos)>d.range*0.7f)moveToward(e,add(ally->pos,scale(normalized(subtract(e.pos,ally->pos)),d.range*0.55f)));
  return;
 }
 if(e.kind==Kind::Mender&&e.order==Order::AttackMove&&distanceSq(e.pos,e.goal)<20*20) {
  finishOrder(e,true);return;
 }
 if(e.kind==Kind::Mender&&e.order==Order::AttackMove&&e.supportTarget) {
  const Entity* leader=find(e.supportTarget);
  if(!leader||!leader->alive()||leader->team!=e.team||leader->id==e.id||definition(leader->kind).building||definition(leader->kind).damage<=0) {
   const Id lost=e.supportTarget;
   for(auto& entity:entities_) {
    if(entity.supportTarget==lost)entity.supportTarget=0;
    for(auto& order:entity.futureOrders)if(order.order==Order::AttackMove&&order.supportTarget==lost)order.supportTarget=0;
   }
  } else if(distance(e.pos,leader->pos)>d.range*0.7f) {
   const Vec2 supportPoint=bounded(add(leader->pos,scale(normalized(subtract(e.pos,leader->pos)),d.range*0.55f)),worldSize(),d.radius);
   // Support steering may help the formation advance, but cannot pull a Mender
   // away from or indefinitely short of its accepted AttackMove waypoint.
   constexpr float WaypointProgress=20.0f;
   if(distance(supportPoint,e.goal)+WaypointProgress<distance(e.pos,e.goal)) {
    moveToward(e,supportPoint);return;
   }
  }
 }
 if(e.order==Order::Move||e.order==Order::AttackMove) {
  if(e.order==Order::AttackMove&&e.target) {
   const Entity* target=find(e.target);
   if(target&&target->alive()&&visible(e.team,target->pos)&&distance(e.pos,target->pos)<=d.vision) {
    pursue(*target);return;
   }
   e.target=0;
  }
  if(distanceSq(e.pos,e.goal)<20*20){finishOrder(e,true);return;}
  moveToward(e,e.goal);
 } else if(e.order==Order::Attack) {
  const Entity* target=find(e.target);
  if(!target||!target->alive()){finishOrder(e);return;}
  if(!visible(e.team,target->pos)) {e.target=0;e.order=Order::AttackMove;moveToward(e,e.goal);return;}
  e.goal=target->pos;pursue(*target);
 } else if(e.order==Order::Idle&&!e.target&&distanceSq(e.pos,e.goal)>30*30) {
  // A unit that reached its formation slot can be displaced by traffic arriving
  // behind it. Reclaim the slot after that traffic passes instead of staying
  // stranded several hundred meters away from the player's destination.
  moveToward(e,e.goal);
 } else if(e.order==Order::Idle&&e.target) {
  const Entity* target=find(e.target);
  if(target&&target->alive()&&visible(e.team,target->pos)&&distance(e.pos,target->pos)<=d.vision&&distance(target->pos,e.goal)<d.vision) {
   pursue(*target);
  } else e.target=0;
 }
}
void Simulation::emitEffect(EffectType type,Vec2 from,Vec2 to,int team,Kind sourceKind,Kind targetKind,float duration) {
 // The recorded masks prevent a later reveal from exposing a hidden event.
 // Current visibility is checked separately by each presentation consumer.
 if(nextEffectId_==std::numeric_limits<std::uint64_t>::max())return;
 Effect effect;effect.from=from;effect.to=to;effect.team=team;
 effect.life=effect.duration=duration;effect.id=nextEffectId_++;
 effect.type=type;effect.sourceKind=sourceKind;effect.targetKind=targetKind;
 for(int observer=0;observer<config_.playerCount;++observer) {
  if(visible(observer,from))effect.fromVisibleMask|=static_cast<std::uint8_t>(1u<<observer);
  if(visible(observer,to))effect.toVisibleMask|=static_cast<std::uint8_t>(1u<<observer);
 }
 effects_.push_back(effect);
}

bool Simulation::effectVisible(const Effect& effect,int team,bool source) const {
 if(!activeTeam(team))return false;
 const auto mask=source?effect.fromVisibleMask:effect.toVisibleMask;
 return (mask&(1u<<team))!=0&&visible(team,source?effect.from:effect.to);
}

bool Simulation::effectLinkVisible(const Effect& effect,int team) const {
 if(!effectVisible(effect,team,true)||!effectVisible(effect,team,false))return false;
 const float cell=worldSize()/FogSize;
 // Test every closed grid cell crossed by the segment. Spaced samples can miss
 // a short corner crossing; closed boxes also hide links touching a fog edge.
 const int minX=std::clamp(static_cast<int>(std::floor(std::min(effect.from.x,effect.to.x)/cell))-1,0,FogSize-1);
 const int maxX=std::clamp(static_cast<int>(std::floor(std::max(effect.from.x,effect.to.x)/cell)),0,FogSize-1);
 const int minY=std::clamp(static_cast<int>(std::floor(std::min(effect.from.y,effect.to.y)/cell))-1,0,FogSize-1);
 const int maxY=std::clamp(static_cast<int>(std::floor(std::max(effect.from.y,effect.to.y)/cell)),0,FogSize-1);
 const double dx=static_cast<double>(effect.to.x)-effect.from.x;
 const double dy=static_cast<double>(effect.to.y)-effect.from.y;
 for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x) {
  if(fog_[team][y*FogSize+x])continue;
  double entry=0,exit=1;
  auto crossesAxis=[&](double origin,double delta,double low,double high) {
   if(delta==0)return origin>=low&&origin<=high;
   double first=(low-origin)/delta,last=(high-origin)/delta;
   if(first>last)std::swap(first,last);
   entry=std::max(entry,first);exit=std::min(exit,last);
   return entry<=exit+1e-10;
  };
  if(crossesAxis(effect.from.x,dx,x*cell,(x+1)*cell)&&
     crossesAxis(effect.from.y,dy,y*cell,(y+1)*cell))return false;
 }
 return true;
}

void Simulation::damage(Entity& victim,float amount,int attackerTeam,Kind sourceKind) {
 if(!victim.alive()||victim.kind==Kind::Resource||amount<=0)return;
 const float dealt=std::min(victim.hp,amount);victim.hp-=dealt;
 emitEffect(EffectType::Impact,victim.pos,victim.pos,victim.team,sourceKind,victim.kind,0.4f);
 if(activeTeam(attackerTeam))players_[attackerTeam].stats.damage+=dealt;
 if(victim.hp<=0) {
  // Record a witnessed death at the event, before periodic corpse cleanup can
  // remove it between the opponent's strategic updates. Hidden deaths stay unknown.
  if(victim.team==0&&visible(1,victim.pos))
   aiSightings_.erase(std::remove_if(aiSightings_.begin(),aiSightings_.end(),[&](const AISighting& sighting){return sighting.id==victim.id;}),aiSightings_.end());
  victim.futureOrders.clear();victim.supportTarget=0;victim.hasArrivalFacing=false;victim.arrivalFacing=0;
  if(victim.order==Order::Patrol||victim.order==Order::Escort) {
   victim.order=Order::Idle;victim.target=0;victim.goal=victim.pos;
  }
  clearSustainedOrder(victim);
  if(victim.kind==Kind::Worker)abandonConstruction(victim,false);
  else if(definition(victim.kind).building)releaseConstruction(victim);
  victim.hp=0;navigationDirty_=true;victim.queue.clear();victim.path.clear();victim.pathIndex=0;
  for(auto& entity:entities_) {
   if(entity.supportTarget==victim.id)entity.supportTarget=0;
   for(auto& order:entity.futureOrders)if(order.order==Order::AttackMove&&order.supportTarget==victim.id)order.supportTarget=0;
  }
  clearSustainedReferences(victim.id);
  if(activeTeam(victim.team)) {
   if(definition(victim.kind).building){if(activeTeam(attackerTeam))++players_[attackerTeam].stats.buildingsDestroyed;}
   else {++players_[victim.team].stats.lost;if(activeTeam(attackerTeam))++players_[attackerTeam].stats.killed;}
  }
  emitEffect(EffectType::Death,victim.pos,victim.pos,victim.team,victim.kind,victim.kind,definition(victim.kind).building?1.0f:0.7f);
 }
}
void Simulation::updateCombat(Entity& e) {
 const auto& d=definition(e.kind);if(e.kind==Kind::Resource||e.progress<1)return;
 if(e.kind==Kind::Mender) {
  if(e.order==Order::Patrol||e.order==Order::Escort)refreshSustainedOrder(e);
  if(e.cooldown>0||e.order==Order::Move)return;
  Entity* patient=nullptr;float need=0;
  for(auto& ally:entities_)if(ally.alive()&&ally.team==e.team&&ally.id!=e.id&&!definition(ally.kind).building&&ally.hp<definition(ally.kind).hp&&distanceSq(ally.pos,e.pos)<=d.range*d.range) {
   float missing=1-ally.hp/definition(ally.kind).hp;if(missing>need){need=missing;patient=&ally;}
  }
  if(patient) {
   patient->hp=std::min(definition(patient->kind).hp,patient->hp+16);e.cooldown=d.cooldown;
   emitEffect(EffectType::Heal,e.pos,patient->pos,e.team,e.kind,patient->kind,0.45f);
  } else if(e.order==Order::Defend&&e.hasArrivalFacing&&distanceSq(e.pos,e.goal)<=5*5) {
   e.facing=e.arrivalFacing;
  }
  return;
 }
 if(d.damage<=0||e.order==Order::Move||e.order==Order::Gather||e.order==Order::Construct)return;
 const bool sustainedOrder=e.order==Order::Patrol||e.order==Order::Escort;
 auto validTarget=[&](const Entity* target) {return target&&target->alive()&&target->team>=0&&target->team!=e.team&&target->kind!=Kind::Resource&&(!definition(target->kind).air||d.antiAir)&&visible(e.team,target->pos);};
 Entity* target=sustainedOrder?sustainedCombatTarget(e):get(e.target);
 if(sustainedOrder) {
  if(!target||e.sustained.phase!=SustainedOrderPhase::Pursuit)return;
  if(!validTarget(target))return;
 }
 if(e.order==Order::Attack) {
  // Another attacker may have killed this explicit target earlier in this
  // combat pass. Do not turn an incidental acquisition into explicit pursuit.
  if(!target||!target->alive()){finishOrder(e);return;}
  // Vision refreshes after movement. Preserve a newly hidden live target until
  // movement can resume toward its last known location on the next step.
  if(!validTarget(target))return;
 }
 if(!validTarget(target))target=nullptr;
 if(target&&e.order!=Order::Attack) {
  const float acquisition=(e.order==Order::Hold||e.order==Order::Defend||d.building)?d.range:d.vision;
  if(distance(e.pos,target->pos)>acquisition+definition(target->kind).radius)target=nullptr;
 }
 if(!target&&!sustainedOrder) {
  float best=-std::numeric_limits<float>::max();
  for(auto& enemy:entities_) {
   if(!validTarget(&enemy))continue;
   float dist=distance(e.pos,enemy.pos),range=(e.order==Order::Hold||e.order==Order::Defend||d.building)?d.range+definition(enemy.kind).radius:d.vision;
   if(dist>range)continue;
   float score=1000-dist;
   if(definition(enemy.kind).damage>0)score+=200;
   if(e.kind==Kind::Lancer&&(definition(enemy.kind).armor>=4||definition(enemy.kind).air))score+=180;
   if(e.kind==Kind::Mortar&&definition(enemy.kind).building)score+=180;
   if(score>best){best=score;target=&enemy;}
  }
  e.target=target?target->id:0;
 }
 if(!target) {
  if(e.order==Order::Defend&&e.hasArrivalFacing&&distanceSq(e.pos,e.goal)<=5*5)e.facing=e.arrivalFacing;
  return;
 }
 if(e.order==Order::Defend) {
  const Vec2 combatDirection=subtract(target->pos,e.pos);
  if(lengthSq(combatDirection)>0.0001f)
   e.facing=std::atan2(combatDirection.y,combatDirection.x);
 }
 if(e.cooldown>0)return;
 const auto& td=definition(target->kind);
 if(distance(e.pos,target->pos)>d.range+td.radius)return;
 // Ground direct fire cannot shoot through terrain. Siege and aircraft arc over it.
 if(e.kind!=Kind::Mortar&&!d.air&&!td.air&&!clearFireLine(e.pos,target->pos,obstacles_))return;
 float modifier=1;
 if(e.kind==Kind::Striker&&(target->kind==Kind::Lancer||target->kind==Kind::Scout))modifier=1.35f;
 if(e.kind==Kind::Lancer&&(td.armor>=4||td.air))modifier=1.8f;
 if(e.kind==Kind::Bastion&&(target->kind==Kind::Worker||target->kind==Kind::Striker||target->kind==Kind::Lancer))modifier=1.4f;
 if(e.kind==Kind::Scout&&target->kind==Kind::Worker)modifier=1.5f;
 if(e.kind==Kind::Mortar&&td.building)modifier=1.65f;
 if(e.kind==Kind::Mortar&&distance(e.pos,target->pos)<150)modifier=0.35f;
 const Vec2 impact=target->pos;const Id primary=target->id;
 const float weapon=d.damage*(1+0.12f*players_[e.team].weapons)*modifier;
 const float effectiveArmor=static_cast<float>(td.armor+players_[target->team].armor);
 emitEffect(EffectType::Weapon,e.pos,impact,e.team,e.kind,target->kind,e.kind==Kind::Mortar?0.55f:0.35f);
 damage(*target,std::max(1.0f,weapon-effectiveArmor),e.team,e.kind);
 e.cooldown=d.cooldown;e.facing=std::atan2(impact.y-e.pos.y,impact.x-e.pos.x);
 if(e.kind==Kind::Mortar)for(auto& enemy:entities_)if(enemy.id!=primary&&enemy.alive()&&enemy.team>=0&&enemy.team!=e.team&&!definition(enemy.kind).air&&distanceSq(enemy.pos,impact)<100*100)damage(enemy,std::max(1.0f,weapon*0.42f-definition(enemy.kind).armor-players_[enemy.team].armor),e.team,e.kind);
 // Units under direct attack can retaliate without changing deliberate move or gather orders.
 target=get(primary);if(target&&target->alive()&&target->order==Order::Idle&&!target->target&&(!d.air||td.antiAir))target->target=e.id;
}
void Simulation::updateVision() {
 for(auto& f:fog_)f.fill(0);
 const float cell=worldSize()/FogSize;
 const auto& map=mapDefinition(config_.map,config_.playerCount,config_.matchLength,config_.mapRevision);
 const bool authored=usesAuthoredTerrain();
 if(authored&&terrainFogDefinition_!=&map) {
  // Ramps are visible approaches from their lower level, not a series of tiny
  // high-ground steps. A cell containing any actual upper terrace still needs
  // an upper observer; classify its complete footprint to protect mixed cells.
  for(int y=0;y<FogSize;++y)for(int x=0;x<FogSize;++x) {
   terrainFogRequiredHeights_[y*FogSize+x]=map.terrainVisibilityHeight(
    {{(x+0.5f)*cell,(y+0.5f)*cell},{cell*0.5f,cell*0.5f}});
  }
  terrainFogDefinition_=&map;
 }
 for(const auto& e:entities_) {
  if(!e.alive()||!activeTeam(e.team)||e.kind==Kind::Resource)continue;
  const float radius=definition(e.kind).vision*(e.progress>=1?1.0f:0.5f);
  const bool ignoresHeight=!authored||definition(e.kind).air;
  const float sourceHeight=authored?map.terrainHeight(e.pos):0;
  int minX=std::clamp(static_cast<int>((e.pos.x-radius)/cell),0,FogSize-1),maxX=std::clamp(static_cast<int>((e.pos.x+radius)/cell),0,FogSize-1);
  int minY=std::clamp(static_cast<int>((e.pos.y-radius)/cell),0,FogSize-1),maxY=std::clamp(static_cast<int>((e.pos.y+radius)/cell),0,FogSize-1);
  for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x) {
   Vec2 center{(x+0.5f)*cell,(y+0.5f)*cell};if(distanceSq(e.pos,center)>(radius+cell*0.5f)*(radius+cell*0.5f))continue;
   if(!ignoresHeight&&terrainFogRequiredHeights_[y*FogSize+x]>sourceHeight+1.0f)continue;
   fog_[e.team][y*FogSize+x]=1;explored_[e.team][y*FogSize+x]=1;
  }
 }
}

std::uint64_t Simulation::aiLastObserved(Vec2 point) const {
 if(!finite(point)||point.x<0||point.y<0||point.x>worldSize()||point.y>worldSize())return 0;
 return aiObserved_[fogIndex(point,worldSize())];
}

void Simulation::updateAIKnowledge() {
 if(config_.playerCount!=2)return;
 constexpr int observer=1;
 for(int cell=0;cell<FogSize*FogSize;++cell)if(fog_[observer][cell])aiObserved_[cell]=tick_;
 std::vector<AISighting> observed;
 std::vector<Id> observedDeaths;
 for(const auto& entity:entities_) {
  if(entity.team!=0||!visible(observer,entity.pos))continue;
  if(entity.alive())observed.push_back({entity.id,entity.kind,entity.pos,tick_});
  else observedDeaths.push_back(entity.id);
 }
 const auto lifetime=static_cast<std::uint64_t>(AIMobileMemorySeconds/Step);
 aiSightings_.erase(std::remove_if(aiSightings_.begin(),aiSightings_.end(),[&](AISighting& sighting) {
  const auto current=std::find_if(observed.begin(),observed.end(),[&](const AISighting& item){return item.id==sighting.id;});
  if(current!=observed.end()){sighting=*current;return false;}
  if(std::find(observedDeaths.begin(),observedDeaths.end(),sighting.id)!=observedDeaths.end())return true;
  // A fixed structure is disproved by vision of its empty site. A mobile unit
  // may have left that site, so retain its observed type until memory expires.
  if(definition(sighting.kind).building)return visible(observer,sighting.pos);
  return tick_-sighting.lastSeenTick>lifetime;
 }),aiSightings_.end());
 for(const auto& sighting:observed) {
  if(std::none_of(aiSightings_.begin(),aiSightings_.end(),[&](const AISighting& item){return item.id==sighting.id;}))aiSightings_.push_back(sighting);
 }
 std::sort(aiSightings_.begin(),aiSightings_.end(),[](const AISighting& left,const AISighting& right){return left.id<right.id;});
}

std::uint64_t Simulation::stateHash() const {
 Hasher hash;hash.integer(config_.map);hash.integer(config_.seed);hash.integer(config_.ai);hash.real(config_.aiAggression);hash.integer(static_cast<int>(config_.matchLength));hash.integer(config_.playerCount);
 // Keep legacy replay hashes stable while separating authored map revisions.
 if(config_.mapRevision)hash.integer(config_.mapRevision);
 hash.integer(tick_);hash.integer(nextId_);hash.real(accumulator_);hash.real(aiTimer_);hash.integer(winner_);hash.byte(eliminatedMask_);
 for(int team=0;team<config_.playerCount;++team) {const auto& p=players_[team];
  hash.integer(p.ore);hash.integer(p.tier);hash.integer(p.weapons);hash.integer(p.armor);
  hash.point(p.armyRally);hash.integer(p.armyRallySet);
  const auto& s=p.stats;hash.integer(s.gathered);hash.integer(s.produced);hash.integer(s.lost);hash.integer(s.killed);hash.integer(s.built);hash.integer(s.buildingsDestroyed);hash.integer(s.expansions);hash.integer(s.upgrades);hash.real(s.damage);
 }
 hash.integer(entities_.size());for(const auto& e:entities_) {
  hash.integer(e.id);hash.integer(static_cast<int>(e.kind));hash.integer(e.team);hash.point(e.pos);hash.point(e.goal);hash.point(e.rally);
  hash.real(e.hp);hash.real(e.cooldown);hash.real(e.progress);hash.real(e.carried);hash.real(e.harvestTimer);hash.real(e.resource);hash.real(e.facing);
  hash.integer(static_cast<int>(e.order));hash.integer(e.target);hash.integer(e.resourceTarget);hash.integer(e.returning);hash.integer(e.pathIndex);hash.real(e.repath);hash.integer(e.builderId);hash.integer(e.resumeGather);hash.integer(e.rallyOverride);
  hash.integer(e.nextQueueId);hash.integer(e.queue.size());for(const auto& q:e.queue){hash.integer(static_cast<int>(q.kind));hash.real(q.remaining);hash.real(q.total);hash.integer(q.cost);hash.integer(q.research);hash.integer(q.id);hash.integer(q.assignmentCursor);hash.integer(q.assignmentCandidates.size());for(Id id:q.assignmentCandidates)hash.integer(id);}
  hash.integer(e.path.size());for(Vec2 p:e.path)hash.point(p);
  hash.integer(e.workTarget);hash.point(e.workPoint);hash.integer(e.workPointValid);
  hash.point(e.navigationAnchor);hash.real(e.stalledFor);hash.real(e.yieldFor);hash.real(e.navigationBestDistance);
  hash.integer(e.pathGeometry);hash.integer(e.navigationFailures);hash.integer(e.avoidanceSide);hash.integer(e.navigationExhausted);
  hash.integer(e.supportTarget);hash.integer(e.futureOrders.size());for(const auto& order:e.futureOrders) {
   hash.integer(static_cast<int>(order.order));hash.point(order.point);hash.integer(order.supportTarget);
   hash.integer(order.hasArrivalFacing);hash.real(order.arrivalFacing);hash.integer(static_cast<int>(order.buildingKind));
  }
  hash.point(e.sustained.patrolOrigin);hash.point(e.sustained.patrolDestination);hash.integer(e.sustained.patrolTowardDestination);
  hash.integer(e.sustained.escortTarget);hash.point(e.sustained.escortOffset);hash.integer(e.sustained.pursuitTarget);
  hash.point(e.sustained.pursuitAnchor);hash.integer(static_cast<int>(e.sustained.phase));
  hash.integer(e.hasArrivalFacing);hash.real(e.arrivalFacing);
 }
 hash.integer(obstacles_.size());for(const auto& o:obstacles_){hash.point(o.center);hash.point(o.half);}
 hash.integer(nextEffectId_);hash.integer(effects_.size());for(const auto& fx:effects_) {
  hash.point(fx.from);hash.point(fx.to);hash.integer(fx.team);hash.real(fx.life);hash.real(fx.duration);
  hash.integer(fx.id);hash.integer(static_cast<int>(fx.type));hash.integer(static_cast<int>(fx.sourceKind));hash.integer(static_cast<int>(fx.targetKind));
  hash.byte(fx.fromVisibleMask);hash.byte(fx.toVisibleMask);
 }
 for(int t=0;t<config_.playerCount;++t)for(int i=0;i<FogSize*FogSize;++i){hash.byte(fog_[t][i]);hash.byte(explored_[t][i]);}
 hash.integer(aiSightings_.size());for(const auto& sighting:aiSightings_){hash.integer(sighting.id);hash.integer(static_cast<int>(sighting.kind));hash.point(sighting.pos);hash.integer(sighting.lastSeenTick);}
 for(auto stamp:aiObserved_)hash.integer(stamp);
 hash.integer(recording_.size());for(const auto& recorded:recording_) {
  const auto& command=recorded.command;hash.integer(recorded.tick);hash.integer(static_cast<int>(command.type));hash.integer(command.team);
  hash.integer(command.units.size());for(Id id:command.units)hash.integer(id);
  hash.point(command.point);hash.integer(command.target);hash.integer(static_cast<int>(command.kind));hash.integer(command.queueIndex);
  hash.integer(static_cast<int>(command.queueMode));
  hash.integer(static_cast<int>(command.spacing));hash.integer(command.hasArrivalFacing);hash.real(command.arrivalFacing);
 }
 return hash.value;
}

bool Simulation::save(const std::string& path) const {
 auto validQueuedOrders=[&]() {
  std::array<std::size_t,MaxPlayers> aggregate{};
  for(const auto& entity:entities_) {
   const bool pendingWorkerJob=entity.alive()&&activeTeam(entity.team)&&entity.kind==Kind::Worker&&
      entity.order==Order::Idle&&!entity.futureOrders.empty()&&
      (entity.futureOrders.front().order==Order::Construct||entity.futureOrders.front().order==Order::Gather);
   if(entity.futureOrders.size()>MaxFutureOrders)return false;
   if(!entity.futureOrders.empty()&&(!entity.alive()||!activeTeam(entity.team)||
      definition(entity.kind).building||entity.kind==Kind::Resource||(entity.order==Order::Idle&&!pendingWorkerJob)))return false;
   if(entity.resumeGather&&(entity.kind!=Kind::Worker||!entity.alive()||
      (entity.order==Order::Idle&&!pendingWorkerJob)))return false;
   if(activeTeam(entity.team)&&(aggregate[entity.team]+=entity.futureOrders.size())>MaxFutureOrdersPerPlayer)return false;
   for(const auto& order:entity.futureOrders) {
    if(!finite(order.point)||order.point.x<0||order.point.y<0||order.point.x>worldSize()||order.point.y>worldSize()||
       !rules::validKind(order.buildingKind)||!rules::validCanonicalArrivalFacing(order.arrivalFacing)||
       (!order.hasArrivalFacing&&order.arrivalFacing!=0.0f))return false;
    if(order.order==Order::Move) {
     if(order.supportTarget||order.buildingKind!=Kind::Worker)return false;
    } else if(order.order==Order::AttackMove) {
     if(order.buildingKind!=Kind::Worker)return false;
     if(order.supportTarget) {
      const Entity* leader=find(order.supportTarget);
      if(entity.kind!=Kind::Mender||order.supportTarget==entity.id||!leader||!leader->alive()||
         leader->team!=entity.team||definition(leader->kind).building||leader->kind==Kind::Resource||
         definition(leader->kind).damage<=0)return false;
     }
    } else if(order.order==Order::Gather) {
     if(entity.kind!=Kind::Worker||!order.supportTarget||order.supportTarget==entity.id||order.buildingKind!=Kind::Worker||
        order.hasArrivalFacing||order.arrivalFacing!=0.0f)return false;
    } else if(order.order==Order::Construct) {
     if(entity.kind!=Kind::Worker||order.hasArrivalFacing||order.arrivalFacing!=0.0f)return false;
     if(order.supportTarget) {
      if(order.supportTarget==entity.id||order.buildingKind!=Kind::Worker)return false;
     } else if(order.buildingKind==Kind::Resource||!definition(order.buildingKind).building)return false;
    } else return false;
   }
  }
  return true;
 };
 if(replica_||!validateSustainedState(entities_)||!validateFormationState(entities_)||!validQueuedOrders())return false;
 std::ofstream out(path,std::ios::trunc);if(!out)return false;out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<float>::max_digits10);
 out<<"CINDERLINE 15\n"<<config_.map<<' '<<config_.seed<<' '<<config_.ai<<' '<<config_.aiAggression<<' '<<static_cast<int>(config_.matchLength)<<' '<<config_.playerCount<<' '<<config_.mapRevision<<'\n';
 out<<tick_<<' '<<nextId_<<' '<<accumulator_<<' '<<aiTimer_<<' '<<winner_<<' '<<static_cast<int>(eliminatedMask_)<<'\n';
 for(int team=0;team<config_.playerCount;++team) {const auto& p=players_[team];
  const auto& s=p.stats;out<<p.ore<<' '<<p.tier<<' '<<p.weapons<<' '<<p.armor<<' '<<s.gathered<<' '<<s.produced<<' '<<s.lost<<' '<<s.killed<<' '<<s.built<<' '<<s.buildingsDestroyed<<' '<<s.expansions<<' '<<s.upgrades<<' '<<s.damage<<'\n';
 }
 out<<obstacles_.size()<<'\n';for(const auto& o:obstacles_)out<<o.center.x<<' '<<o.center.y<<' '<<o.half.x<<' '<<o.half.y<<'\n';
 out<<entities_.size()<<'\n';for(const auto& e:entities_) {
  out<<e.id<<' '<<static_cast<int>(e.kind)<<' '<<e.team<<' '<<e.pos.x<<' '<<e.pos.y<<' '<<e.goal.x<<' '<<e.goal.y<<' '<<e.rally.x<<' '<<e.rally.y<<' '<<e.hp<<' '<<e.cooldown<<' '<<e.progress<<' '<<e.carried<<' '<<e.harvestTimer<<' '<<e.resource<<' '<<e.facing<<' '<<static_cast<int>(e.order)<<' '<<e.target<<' '<<e.resourceTarget<<' '<<e.returning<<' '<<e.pathIndex<<' '<<e.repath<<' '<<e.builderId<<' '<<e.resumeGather<<'\n';
  out<<e.queue.size()<<'\n';for(const auto& q:e.queue)out<<static_cast<int>(q.kind)<<' '<<q.remaining<<' '<<q.total<<' '<<q.cost<<' '<<q.research<<'\n';
  out<<e.path.size()<<'\n';for(Vec2 p:e.path)out<<p.x<<' '<<p.y<<'\n';
 }
 out<<effects_.size()<<' '<<nextEffectId_<<'\n';for(const auto& fx:effects_) {
  out<<fx.from.x<<' '<<fx.from.y<<' '<<fx.to.x<<' '<<fx.to.y<<' '<<fx.team<<' '<<fx.life<<' '<<fx.duration<<' '<<fx.id<<' '<<static_cast<int>(fx.type)<<' '
     <<static_cast<int>(fx.sourceKind)<<' '<<static_cast<int>(fx.targetKind)<<' '<<static_cast<int>(fx.fromVisibleMask)<<' '<<static_cast<int>(fx.toVisibleMask)<<'\n';
 }
 for(int t=0;t<config_.playerCount;++t){for(auto v:fog_[t])out<<static_cast<int>(v)<<' ';out<<'\n';for(auto v:explored_[t])out<<static_cast<int>(v)<<' ';out<<'\n';}
 out<<recording_.size()<<'\n';for(const auto& r:recording_) {
  const auto& c=r.command;out<<r.tick<<' '<<static_cast<int>(c.type)<<' '<<c.team<<' '<<c.point.x<<' '<<c.point.y<<' '<<c.target<<' '<<static_cast<int>(c.kind)<<' '<<c.queueIndex<<' '<<static_cast<int>(c.queueMode)<<' '<<static_cast<int>(c.spacing)<<' '<<c.hasArrivalFacing<<' '<<c.arrivalFacing<<' '<<c.units.size();for(Id id:c.units)out<<' '<<id;out<<'\n';
 }
 out<<std::quoted(alert_)<<'\n'<<std::quoted(aiStatus_)<<'\n';
 out<<"AI_KNOWLEDGE 1\n"<<aiSightings_.size()<<'\n';
 for(const auto& sighting:aiSightings_)out<<sighting.id<<' '<<static_cast<int>(sighting.kind)<<' '<<sighting.pos.x<<' '<<sighting.pos.y<<' '<<sighting.lastSeenTick<<'\n';
 out<<aiObserved_.size()<<'\n';for(auto stamp:aiObserved_)out<<stamp<<' ';out<<'\n';
 out<<"NAVIGATION 1\n"<<entities_.size()<<'\n';
 for(const auto& entity:entities_) {
  out<<entity.id<<' '<<entity.workTarget<<' '<<entity.workPoint.x<<' '<<entity.workPoint.y<<' '<<entity.workPointValid<<' '
     <<entity.navigationAnchor.x<<' '<<entity.navigationAnchor.y<<' '<<entity.stalledFor<<' '<<entity.yieldFor<<' '
     <<entity.navigationBestDistance<<' '<<entity.pathGeometry<<' '<<entity.navigationFailures<<' '
     <<entity.avoidanceSide<<' '<<entity.navigationExhausted<<'\n';
 }
 out<<"PRODUCTION_JOBS 1\n"<<entities_.size()<<'\n';
 for(const auto& entity:entities_) {
  out<<entity.id<<' '<<entity.nextQueueId<<' '<<entity.queue.size();
  for(const auto& item:entity.queue)out<<' '<<item.id;
  out<<'\n';
 }
 out<<"RALLY_STATE 1\n";
 for(int team=0;team<config_.playerCount;++team) {const auto& player=players_[team];out<<player.armyRally.x<<' '<<player.armyRally.y<<' '<<player.armyRallySet<<'\n';}
 out<<entities_.size()<<'\n';for(const auto& entity:entities_) {
  out<<entity.id<<' '<<entity.rallyOverride<<' '<<entity.queue.size();
  for(const auto& item:entity.queue) {
   out<<' '<<item.assignmentCursor<<' '<<item.assignmentCandidates.size();
   for(Id candidate:item.assignmentCandidates)out<<' '<<candidate;
  }
  out<<'\n';
 }
 out<<"ORDER_QUEUES 1\n"<<entities_.size()<<'\n';
 for(const auto& entity:entities_) {
  out<<entity.id<<' '<<entity.supportTarget<<' '<<entity.futureOrders.size();
  for(const auto& order:entity.futureOrders)
   out<<' '<<static_cast<int>(order.order)<<' '<<order.point.x<<' '<<order.point.y<<' '<<order.supportTarget;
  out<<'\n';
 }
 out<<"SUSTAINED_ORDERS 1\n"<<entities_.size()<<'\n';
 for(const auto& entity:entities_) {
  const auto& state=entity.sustained;
  out<<entity.id<<' '<<state.patrolOrigin.x<<' '<<state.patrolOrigin.y<<' '
     <<state.patrolDestination.x<<' '<<state.patrolDestination.y<<' '<<state.patrolTowardDestination<<' '
     <<state.escortTarget<<' '<<state.escortOffset.x<<' '<<state.escortOffset.y<<' '
     <<state.pursuitTarget<<' '<<state.pursuitAnchor.x<<' '<<state.pursuitAnchor.y<<' '
     <<static_cast<int>(state.phase)<<'\n';
 }
 out<<"FORMATION_ORDERS 1\n"<<entities_.size()<<'\n';
 for(const auto& entity:entities_) {
  out<<entity.id<<' '<<entity.hasArrivalFacing<<' '<<entity.arrivalFacing<<' '<<entity.futureOrders.size();
  for(const auto& order:entity.futureOrders)out<<' '<<order.hasArrivalFacing<<' '<<order.arrivalFacing;
  out<<'\n';
 }
 out<<"QUEUED_WORK 1\n"<<entities_.size()<<'\n';
 for(const auto& entity:entities_) {
  out<<entity.id<<' '<<entity.futureOrders.size();
  for(const auto& order:entity.futureOrders)out<<' '<<static_cast<int>(order.buildingKind);
  out<<'\n';
 }
 out.flush();return out.good();
}
bool Simulation::load(const std::string& path) {
 if(replica_)return false;
 std::ifstream in(path);if(!in)return false;in.imbue(std::locale::classic());std::string magic;int version=0;in>>magic>>version;if(magic!="CINDERLINE"||(version<1||version>15))return false;
 Simulation loaded;loaded.entities_.clear();loaded.obstacles_.clear();loaded.effects_.clear();loaded.recording_.clear();
 in>>loaded.config_.map>>loaded.config_.seed>>loaded.config_.ai>>loaded.config_.aiAggression;
 int length=static_cast<int>(MatchLength::Standard);if(version>=9)in>>length;
 if(length<0||length>=static_cast<int>(MatchLength::Count))return false;
 loaded.config_.matchLength=static_cast<MatchLength>(length);
 int playerCount=2;if(version>=10)in>>playerCount;
 if(!rules::validPlayerCount(playerCount))return false;
 loaded.config_.playerCount=playerCount;if(playerCount==4)loaded.config_.ai=false;
 loaded.config_.mapRevision=0;
 if(version>=15)in>>loaded.config_.mapRevision;
 if(!in||!validMapRevision(loaded.config_.mapRevision))return false;
 auto loadedInWorld=[&](Vec2 point){return finite(point)&&point.x>=0&&point.y>=0&&point.x<=loaded.worldSize()&&point.y<=loaded.worldSize();};
 in>>loaded.tick_>>loaded.nextId_>>loaded.accumulator_>>loaded.aiTimer_>>loaded.winner_;
 int eliminatedMask=0;if(version>=10)in>>eliminatedMask;
 if(!in||loaded.config_.map<0||loaded.config_.map>2||!std::isfinite(loaded.config_.aiAggression)||loaded.config_.aiAggression<0.5f||loaded.config_.aiAggression>2||!std::isfinite(loaded.accumulator_)||loaded.accumulator_<0||loaded.accumulator_>1||!std::isfinite(loaded.aiTimer_)||loaded.winner_<(version>=10?-2:-1)||loaded.winner_>=(version>=10?playerCount:2))return false;
 const std::uint8_t activeMask=rules::activePlayerMask(playerCount);
 if(eliminatedMask<0||eliminatedMask>activeMask)return false;
 loaded.eliminatedMask_=static_cast<std::uint8_t>(eliminatedMask);
 if(version>=10) {
  if(!rules::validMatchOutcome(playerCount,loaded.winner_,loaded.eliminatedMask_))return false;
 }
 for(int team=0;team<playerCount;++team) {auto& p=loaded.players_[team];
  auto& s=p.stats;in>>p.ore>>p.tier>>p.weapons>>p.armor>>s.gathered>>s.produced>>s.lost>>s.killed>>s.built>>s.buildingsDestroyed>>s.expansions>>s.upgrades>>s.damage;
  if(!in||p.ore<0||p.tier<1||p.tier>3||p.weapons<0||p.weapons>3||p.armor<0||p.armor>3||!std::isfinite(s.damage))return false;
 }
 std::size_t count=0;in>>count;if(!in||count>1024)return false;
 for(std::size_t i=0;i<count;++i){Obstacle o;in>>o.center.x>>o.center.y>>o.half.x>>o.half.y;if(!in||!loadedInWorld(o.center)||!finite(o.half)||o.half.x<0||o.half.y<0||o.center.x-o.half.x<0||o.center.y-o.half.y<0||o.center.x+o.half.x>loaded.worldSize()||o.center.y+o.half.y>loaded.worldSize())return false;loaded.obstacles_.push_back(o);}
 in>>count;if(!in||count>10000)return false;Id maxId=0;
 for(std::size_t i=0;i<count;++i) {
  Entity e;int kind,order;in>>e.id>>kind>>e.team>>e.pos.x>>e.pos.y>>e.goal.x>>e.goal.y>>e.rally.x>>e.rally.y>>e.hp>>e.cooldown>>e.progress>>e.carried>>e.harvestTimer>>e.resource>>e.facing>>order>>e.target>>e.resourceTarget>>e.returning>>e.pathIndex>>e.repath;
  if(version>=2)in>>e.builderId>>e.resumeGather;
  e.kind=static_cast<Kind>(kind);e.order=static_cast<Order>(order);
  if(version>=5&&(e.repath<0||e.repath>10))return false;
  const Order maximumOrder=version>=12?Order::Escort:version>=6?Order::Defend:(version==1?Order::Hold:Order::Construct);
  if(!in||!e.id||!rules::validKind(e.kind)||(!loaded.activeTeam(e.team)&&!(e.kind==Kind::Resource&&e.team==-1))||!loadedInWorld(e.pos)||!loadedInWorld(e.goal)||!loadedInWorld(e.rally)||!std::isfinite(e.hp)||e.hp<0||!std::isfinite(e.cooldown)||e.cooldown<0||!std::isfinite(e.progress)||e.progress<0||e.progress>1||!std::isfinite(e.carried)||e.carried<0||e.carried>CarryCapacity||!std::isfinite(e.harvestTimer)||!std::isfinite(e.resource)||e.resource<0||!std::isfinite(e.facing)||!std::isfinite(e.repath)||order<0||order>static_cast<int>(maximumOrder)||loaded.find(e.id))return false;
  if(version<8&&rules::productionKind(e.kind)) {
   const Vec2 legacyDefault=bounded(add(e.pos,startOffset(e.team,{190,0})),loaded.worldSize());
   e.rallyOverride=distanceSq(e.rally,legacyDefault)>0.01f;
  }
  maxId=std::max(maxId,e.id);std::size_t queueCount;in>>queueCount;if(!in||queueCount>MaxQueue)return false;
  for(std::size_t q=0;q<queueCount;++q){QueueItem item;int qkind;in>>qkind>>item.remaining>>item.total>>item.cost>>item.research;item.kind=static_cast<Kind>(qkind);if(!in||!rules::validKind(item.kind)||!std::isfinite(item.remaining)||!std::isfinite(item.total)||item.total<=0||item.remaining<0||item.remaining>item.total+0.001f||item.cost<0)return false;item.id=e.nextQueueId++;e.queue.push_back(item);}
  std::size_t pathCount;in>>pathCount;if(!in||pathCount>FogSize*FogSize+1)return false;
  for(std::size_t p=0;p<pathCount;++p){Vec2 point;in>>point.x>>point.y;if(!in||!loadedInWorld(point))return false;e.path.push_back(point);}
  if(e.pathIndex<0||e.pathIndex>static_cast<int>(e.path.size()))return false;loaded.entities_.push_back(std::move(e));
 }
 if(loaded.nextId_<=maxId)return false;
 for(const auto& e:loaded.entities_) {
  if(e.builderId&&loaded.constructionWorker(e.id)!=e.builderId)return false;
  // A construction-triggered return to mining survives each active order in
  // the remaining tactical tail, not just the Construct step itself.
  if(e.resumeGather&&(e.kind!=Kind::Worker||!e.alive()||(e.order==Order::Idle&&version<14)))return false;
  if(e.order==Order::Construct) {
   const Entity* foundation=loaded.find(e.target);
   if(e.kind!=Kind::Worker||!e.alive()||!foundation||loaded.constructionWorker(foundation->id)!=e.id)return false;
  }
 }
 in>>count;if(!in||count>10000)return false;
 if(version>=4) {
  if(!readEffectId(in,loaded.nextEffectId_)||loaded.nextEffectId_==0)return false;
  std::uint64_t previousEffect=0;
  for(std::size_t i=0;i<count;++i) {
   Effect fx;int type=0,sourceKind=0,targetKind=0,fromMask=0,toMask=0;
   in>>fx.from.x>>fx.from.y>>fx.to.x>>fx.to.y>>fx.team>>fx.life>>fx.duration;
   if(!readEffectId(in,fx.id))return false;
   in>>type>>sourceKind>>targetKind>>fromMask>>toMask;
   fx.type=static_cast<EffectType>(type);fx.sourceKind=static_cast<Kind>(sourceKind);fx.targetKind=static_cast<Kind>(targetKind);
   if(!in||!loadedInWorld(fx.from)||!loadedInWorld(fx.to)||!loaded.activeTeam(fx.team)||!std::isfinite(fx.life)||!std::isfinite(fx.duration)||fx.duration<0.3f||fx.duration>10.0f||fx.life<=0||fx.life>fx.duration||fx.id<=previousEffect||fx.id>=loaded.nextEffectId_||type<static_cast<int>(EffectType::Weapon)||type>static_cast<int>(EffectType::Death)||!rules::validKind(fx.sourceKind)||!rules::validKind(fx.targetKind)||fx.sourceKind==Kind::Resource||fx.targetKind==Kind::Resource||fromMask<0||fromMask>activeMask||toMask<0||toMask>activeMask)return false;
   if((fx.type==EffectType::Impact||fx.type==EffectType::Death)&&(fx.from.x!=fx.to.x||fx.from.y!=fx.to.y||fromMask!=toMask))return false;
   if(fx.type==EffectType::Death&&fx.sourceKind!=fx.targetKind)return false;
   fx.fromVisibleMask=static_cast<std::uint8_t>(fromMask);fx.toVisibleMask=static_cast<std::uint8_t>(toMask);
   previousEffect=fx.id;loaded.effects_.push_back(fx);
  }
 } else {
  // Older events conflate healing, fire and destruction. Consume their syntax
  // but drop the cosmetic records rather than guessing a new event identity.
  for(std::size_t i=0;i<count;++i) {
   Vec2 from,to;int team=0;float life=0;bool explosion=false;
   in>>from.x>>from.y>>to.x>>to.y>>team>>life>>explosion;
   if(!in||!finite(from)||!finite(to)||!std::isfinite(life))return false;
  }
  loaded.nextEffectId_=1;
 }
 for(int t=0;t<playerCount;++t)for(auto* field:{&loaded.fog_[t],&loaded.explored_[t]})for(auto& value:*field){int v;in>>v;if(!in||v<0||v>1)return false;value=static_cast<unsigned char>(v);}
 in>>count;if(!in||count>1000000)return false;
 for(std::size_t i=0;i<count;++i) {
  RecordedCommand r;Command& c=r.command;int type,kind,mode=static_cast<int>(CommandQueueMode::Replace);
  int spacing=static_cast<int>(FormationSpacing::Standard),hasFacing=0;float facing=0;std::size_t unitCount;
  in>>r.tick>>type>>c.team>>c.point.x>>c.point.y>>c.target>>kind>>c.queueIndex;
  if(version>=11)in>>mode;
  if(version>=13)in>>spacing>>hasFacing>>facing;
  in>>unitCount;c.type=static_cast<CommandType>(type);c.kind=static_cast<Kind>(kind);c.queueMode=static_cast<CommandQueueMode>(mode);
  c.spacing=static_cast<FormationSpacing>(spacing);c.hasArrivalFacing=hasFacing!=0;c.arrivalFacing=facing;
  const CommandType maximumCommand=version>=12?CommandType::Escort:version>=11?CommandType::ClearOrders:version>=7?CommandType::AutoRally:version>=6?CommandType::Defend:(version==1?CommandType::CancelBuilding:CommandType::ResumeConstruction);
  if(!in||r.tick>loaded.tick_||type<0||type>static_cast<int>(maximumCommand)||mode<static_cast<int>(CommandQueueMode::Replace)||mode>static_cast<int>(CommandQueueMode::Append)||
     spacing<static_cast<int>(FormationSpacing::Tight)||spacing>static_cast<int>(FormationSpacing::Wide)||hasFacing<0||hasFacing>1||
     !rules::validCanonicalArrivalFacing(c.arrivalFacing)||(!c.hasArrivalFacing&&c.arrivalFacing!=0.0f)||
     !loaded.activeTeam(c.team)||!rules::validKind(c.kind)||!finite(c.point)||unitCount>500)return false;
  for(std::size_t n=0;n<unitCount;++n){Id id;in>>id;c.units.push_back(id);}if(!in)return false;
  if(version<7&&c.type==CommandType::CancelQueue) {
   // Older cancellation used the first producer and ignored target entirely.
   // Preserve that operation when an older recording is saved in version 7.
   if(c.units.size()>1)c.units.resize(1);
   c.target=0;
  }
  if(version>=7) {
   const bool automatic=type>=static_cast<int>(CommandType::AutoBuild)&&type<=static_cast<int>(CommandType::AutoRally);
   if(automatic!=c.units.empty()||c.point.x<0||c.point.y<0||c.point.x>loaded.worldSize()||c.point.y>loaded.worldSize())return false;
   if(automatic&&(c.queueIndex<0||c.queueIndex>MaxQueue))return false;
   if(c.type==CommandType::AutoBuild&&(!definition(c.kind).building||c.target||c.queueIndex))return false;
   if(c.type==CommandType::AutoTrain&&(definition(c.kind).building||c.kind==Kind::Resource||c.queueIndex<1))return false;
   if(c.type==CommandType::AutoResearch&&c.queueIndex>2)return false;
   if(c.type==CommandType::AutoRally&&((c.queueIndex<0||c.queueIndex>1)||(c.queueIndex==1&&!c.target)||
      (c.kind!=Kind::Resource&&!rules::productionKind(c.kind))))return false;
   if(c.type==CommandType::CancelQueue&&(c.units.size()!=1||c.queueIndex<0||c.queueIndex>=MaxQueue))return false;
   if(c.queueMode==CommandQueueMode::Append) {
    const bool movement=c.type==CommandType::Move||c.type==CommandType::AttackMove;
    const bool queuedWork=version>=14&&(c.type==CommandType::Build||
        c.type==CommandType::ResumeConstruction||c.type==CommandType::Gather);
    if(!movement&&!queuedWork)return false;
    if(c.type==CommandType::Build&&
       (c.units.size()!=1||c.target||c.queueIndex||c.kind==Kind::Resource||!definition(c.kind).building))return false;
    if(c.type==CommandType::ResumeConstruction&&
       (c.units.size()!=1||!c.target||c.queueIndex||c.kind!=Kind::Worker))return false;
    if(c.type==CommandType::Gather&&(!c.target||c.queueIndex||c.kind!=Kind::Worker))return false;
   }
   if(c.type==CommandType::ClearOrders&&(c.queueMode!=CommandQueueMode::Replace||c.target||c.queueIndex))return false;
   if((c.type==CommandType::Patrol||c.type==CommandType::Escort)&&
      (c.queueMode!=CommandQueueMode::Replace||c.queueIndex))return false;
   if(c.type==CommandType::Patrol&&c.target)return false;
   if(c.type==CommandType::Escort&&(!c.target||c.point.x!=0||c.point.y!=0))return false;
   const bool formationCommand=c.type==CommandType::Move||c.type==CommandType::AttackMove||c.type==CommandType::Defend;
   if(!formationCommand&&(c.spacing!=FormationSpacing::Standard||c.hasArrivalFacing||c.arrivalFacing!=0.0f))return false;
  }
  loaded.recording_.push_back(std::move(r));
 }
 in>>std::quoted(loaded.alert_)>>std::quoted(loaded.aiStatus_);if(!in||loaded.alert_.size()>4096||loaded.aiStatus_.size()>4096)return false;
 if(version>=3) {
  std::string section;int knowledgeVersion=0;in>>section>>knowledgeVersion>>count;
  if(!in||section!="AI_KNOWLEDGE"||knowledgeVersion!=1||count>10000)return false;
  Id previous=0;
  for(std::size_t index=0;index<count;++index) {
   AISighting sighting;int kind=0;in>>sighting.id>>kind>>sighting.pos.x>>sighting.pos.y>>sighting.lastSeenTick;
   sighting.kind=static_cast<Kind>(kind);
   if(!in||sighting.id<=previous||sighting.id>=loaded.nextId_||!rules::validKind(sighting.kind)||sighting.kind==Kind::Resource||!finite(sighting.pos)||sighting.pos.x<0||sighting.pos.y<0||sighting.pos.x>loaded.worldSize()||sighting.pos.y>loaded.worldSize()||sighting.lastSeenTick==0||sighting.lastSeenTick>loaded.tick_)return false;
   previous=sighting.id;loaded.aiSightings_.push_back(sighting);
  }
  in>>count;if(!in||count!=loaded.aiObserved_.size())return false;
  for(auto& stamp:loaded.aiObserved_){in>>stamp;if(!in||stamp>loaded.tick_)return false;}
  for(const auto& sighting:loaded.aiSightings_)if(loaded.aiLastObserved(sighting.pos)<sighting.lastSeenTick)return false;
 }
 if(version>=5) {
  std::string section;int navVersion=0;in>>section>>navVersion>>count;
  if(!in||section!="NAVIGATION"||navVersion!=1||count!=loaded.entities_.size())return false;
  for(auto& entity:loaded.entities_) {
   Id id=0;int valid=0,exhausted=0;
   in>>id>>entity.workTarget>>entity.workPoint.x>>entity.workPoint.y>>valid
     >>entity.navigationAnchor.x>>entity.navigationAnchor.y>>entity.stalledFor>>entity.yieldFor
     >>entity.navigationBestDistance>>entity.pathGeometry>>entity.navigationFailures>>entity.avoidanceSide>>exhausted;
   if(!in||id!=entity.id||valid<0||valid>1||exhausted<0||exhausted>1||!finite(entity.workPoint)||
      !finite(entity.navigationAnchor)||!std::isfinite(entity.stalledFor)||entity.stalledFor<0||
      !std::isfinite(entity.yieldFor)||entity.yieldFor<0||!std::isfinite(entity.navigationBestDistance)||
      entity.navigationBestDistance<0||entity.navigationFailures<0||entity.navigationFailures>1000||
      entity.avoidanceSide< -1||entity.avoidanceSide>1)return false;
   entity.workPointValid=valid!=0;entity.navigationExhausted=exhausted!=0;
   if(entity.workPointValid&&(!entity.workTarget||!loaded.find(entity.workTarget)||
      entity.workPoint.x<0||entity.workPoint.y<0||entity.workPoint.x>loaded.worldSize()||entity.workPoint.y>loaded.worldSize()))return false;
  }
 }
 if(version>=7) {
  std::string section;int jobsVersion=0;in>>section>>jobsVersion>>count;
  if(!in||section!="PRODUCTION_JOBS"||jobsVersion!=1||count!=loaded.entities_.size())return false;
  for(auto& entity:loaded.entities_) {
   std::uint64_t id=0,next=0;std::size_t queueCount=0;
   if(!readEffectId(in,id)||id!=entity.id||!readEffectId(in,next)||!next||next>std::numeric_limits<Id>::max())return false;
   in>>queueCount;if(!in||queueCount!=entity.queue.size())return false;
   entity.nextQueueId=static_cast<Id>(next);Id previous=0;
   for(auto& item:entity.queue) {
    std::uint64_t job=0;
    if(!readEffectId(in,job)||job<=previous||job>=next)return false;
    item.id=static_cast<Id>(job);previous=item.id;
   }
  }
 }
 if(version>=8) {
  std::string section;int rallyVersion=0;in>>section>>rallyVersion;
  if(!in||section!="RALLY_STATE"||rallyVersion!=1)return false;
  for(int team=0;team<playerCount;++team) {auto& player=loaded.players_[team];
   int set=0;in>>player.armyRally.x>>player.armyRally.y>>set;
   if(!in||set<0||set>1||!finite(player.armyRally)||player.armyRally.x<0||player.armyRally.y<0||
      player.armyRally.x>loaded.worldSize()||player.armyRally.y>loaded.worldSize())return false;
   player.armyRallySet=set!=0;
  }
  in>>count;if(!in||count!=loaded.entities_.size())return false;
  for(auto& entity:loaded.entities_) {
   Id id=0;int overridden=0;std::size_t queueCount=0;in>>id>>overridden>>queueCount;
   if(!in||id!=entity.id||overridden<0||overridden>1||queueCount!=entity.queue.size())return false;
   entity.rallyOverride=overridden!=0;
   if(entity.rallyOverride&&!rules::productionKind(entity.kind))return false;
   for(auto& item:entity.queue) {
    std::size_t candidateCount=0;in>>item.assignmentCursor>>candidateCount;
    if(!in||item.assignmentCursor<0||candidateCount>10000||static_cast<std::size_t>(item.assignmentCursor)>candidateCount||
       (candidateCount&&(item.kind!=Kind::Worker||item.research||item.remaining>0.001f)))return false;
    Id previous=0;item.assignmentCandidates.reserve(candidateCount);
    for(std::size_t candidate=0;candidate<candidateCount;++candidate) {
     Id resource=0;in>>resource;const Entity* target=loaded.find(resource);
     if(!in||!resource||resource==previous||!target||target->kind!=Kind::Resource||
        std::find(item.assignmentCandidates.begin(),item.assignmentCandidates.end(),resource)!=item.assignmentCandidates.end())return false;
     item.assignmentCandidates.push_back(resource);previous=resource;
    }
   }
  }
  for(const auto& entity:loaded.entities_)if(entity.alive()&&loaded.activeTeam(entity.team)&&rules::combatProductionKind(entity.kind)&&
     loaded.players_[entity.team].armyRallySet&&!entity.rallyOverride&&distanceSq(entity.rally,loaded.players_[entity.team].armyRally)>0.01f)return false;
 }
 if(version>=11) {
  std::string section;int ordersVersion=0;in>>section>>ordersVersion>>count;
  if(!in||section!="ORDER_QUEUES"||ordersVersion!=1||count!=loaded.entities_.size())return false;
  for(auto& entity:loaded.entities_) {
   Id id=0;std::size_t futureCount=0;in>>id>>entity.supportTarget>>futureCount;
   if(!in||id!=entity.id||futureCount>MaxFutureOrders)return false;
   entity.futureOrders.reserve(futureCount);
   for(std::size_t index=0;index<futureCount;++index) {
    TacticalOrder order;int type=0;in>>type>>order.point.x>>order.point.y>>order.supportTarget;
    order.order=static_cast<Order>(type);
    const bool knownOrder=order.order==Order::Move||order.order==Order::AttackMove||
                          (version>=14&&(order.order==Order::Construct||order.order==Order::Gather));
    if(!in||!knownOrder||!loadedInWorld(order.point))return false;
    entity.futureOrders.push_back(order);
   }
  }
  auto validSupport=[&](const Entity& owner,Order order,Id support) {
   if(!support)return true;
   if(!owner.alive()||!loaded.activeTeam(owner.team)||definition(owner.kind).building||owner.kind!=Kind::Mender||order!=Order::AttackMove||support==owner.id)return false;
   const Entity* leader=loaded.find(support);
   return leader&&leader->alive()&&leader->team==owner.team&&!definition(leader->kind).building&&leader->kind!=Kind::Resource&&definition(leader->kind).damage>0;
  };
  std::array<std::size_t,MaxPlayers> aggregate{};
  for(const auto& entity:loaded.entities_) {
   const bool pendingWorkerJob=version>=14&&entity.alive()&&loaded.activeTeam(entity.team)&&
      entity.kind==Kind::Worker&&entity.order==Order::Idle&&!entity.futureOrders.empty()&&
      (entity.futureOrders.front().order==Order::Construct||entity.futureOrders.front().order==Order::Gather);
   if(!validSupport(entity,entity.order,entity.supportTarget))return false;
   if(!entity.futureOrders.empty()&&(!entity.alive()||!loaded.activeTeam(entity.team)||definition(entity.kind).building||
      entity.kind==Kind::Resource||(entity.order==Order::Idle&&!pendingWorkerJob)))return false;
   if(loaded.activeTeam(entity.team)) {
    aggregate[entity.team]+=entity.futureOrders.size();
    if(aggregate[entity.team]>MaxFutureOrdersPerPlayer)return false;
   }
   for(const auto& order:entity.futureOrders)
    if(order.order!=Order::Construct&&order.order!=Order::Gather&&
       !validSupport(entity,order.order,order.supportTarget))return false;
  }
 }
 if(version>=12) {
  std::string section;int sustainedVersion=0;in>>section>>sustainedVersion>>count;
  if(!in||section!="SUSTAINED_ORDERS"||sustainedVersion!=1||count!=loaded.entities_.size())return false;
  for(auto& entity:loaded.entities_) {
   Id id=0;int toward=0,phase=0;auto& state=entity.sustained;
   in>>id>>state.patrolOrigin.x>>state.patrolOrigin.y>>state.patrolDestination.x>>state.patrolDestination.y>>toward
     >>state.escortTarget>>state.escortOffset.x>>state.escortOffset.y>>state.pursuitTarget
     >>state.pursuitAnchor.x>>state.pursuitAnchor.y>>phase;
   if(!in||id!=entity.id||toward<0||toward>1||phase<static_cast<int>(SustainedOrderPhase::Travel)||
      phase>static_cast<int>(SustainedOrderPhase::Return))return false;
   state.patrolTowardDestination=toward!=0;state.phase=static_cast<SustainedOrderPhase>(phase);
  }
  if(!loaded.validateSustainedState(loaded.entities_))return false;
 }
 if(version>=13) {
  std::string section;int formationVersion=0;in>>section>>formationVersion>>count;
  if(!in||section!="FORMATION_ORDERS"||formationVersion!=1||count!=loaded.entities_.size())return false;
  for(auto& entity:loaded.entities_) {
   Id id=0;int hasFacing=0;std::size_t futureCount=0;
   in>>id>>hasFacing>>entity.arrivalFacing>>futureCount;
   if(!in||id!=entity.id||hasFacing<0||hasFacing>1||futureCount!=entity.futureOrders.size())return false;
   entity.hasArrivalFacing=hasFacing!=0;
   for(auto& order:entity.futureOrders) {
    int futureHasFacing=0;in>>futureHasFacing>>order.arrivalFacing;
    if(!in||futureHasFacing<0||futureHasFacing>1)return false;
    order.hasArrivalFacing=futureHasFacing!=0;
   }
  }
  if(!loaded.validateFormationState(loaded.entities_))return false;
 }
 if(version>=14) {
  std::string section;int queuedWorkVersion=0;in>>section>>queuedWorkVersion>>count;
  if(!in||section!="QUEUED_WORK"||queuedWorkVersion!=1||count!=loaded.entities_.size())return false;
  for(auto& entity:loaded.entities_) {
   Id id=0;std::size_t futureCount=0;in>>id>>futureCount;
   if(!in||id!=entity.id||futureCount!=entity.futureOrders.size())return false;
   for(auto& order:entity.futureOrders) {
    int buildingKind=0;in>>buildingKind;order.buildingKind=static_cast<Kind>(buildingKind);
    if(!in||!rules::validKind(order.buildingKind))return false;
   }
  }
 }
 {
  std::array<std::size_t,MaxPlayers> aggregate{};
  for(const auto& entity:loaded.entities_) {
   const bool pendingWorkerJob=version>=14&&entity.alive()&&loaded.activeTeam(entity.team)&&
      entity.kind==Kind::Worker&&entity.order==Order::Idle&&!entity.futureOrders.empty()&&
      (entity.futureOrders.front().order==Order::Construct||entity.futureOrders.front().order==Order::Gather);
   if(!entity.futureOrders.empty()&&(!entity.alive()||!loaded.activeTeam(entity.team)||
      definition(entity.kind).building||entity.kind==Kind::Resource||(entity.order==Order::Idle&&!pendingWorkerJob)))return false;
   if(entity.resumeGather&&(entity.kind!=Kind::Worker||!entity.alive()||
      (entity.order==Order::Idle&&!pendingWorkerJob)))return false;
   if(loaded.activeTeam(entity.team)&&(aggregate[entity.team]+=entity.futureOrders.size())>MaxFutureOrdersPerPlayer)return false;
   for(const auto& order:entity.futureOrders) {
    if(!rules::validKind(order.buildingKind))return false;
    if(order.order==Order::Move) {
     if(order.supportTarget||order.buildingKind!=Kind::Worker)return false;
    } else if(order.order==Order::AttackMove) {
     if(order.buildingKind!=Kind::Worker)return false;
    } else if(order.order==Order::Gather) {
     if(version<14||entity.kind!=Kind::Worker||!order.supportTarget||order.supportTarget==entity.id||order.buildingKind!=Kind::Worker||
        order.hasArrivalFacing||order.arrivalFacing!=0.0f)return false;
    } else if(order.order==Order::Construct) {
     if(version<14||entity.kind!=Kind::Worker||order.hasArrivalFacing||order.arrivalFacing!=0.0f)return false;
     if(order.supportTarget) {
      if(order.supportTarget==entity.id||order.buildingKind!=Kind::Worker)return false;
     } else if(order.buildingKind==Kind::Resource||!definition(order.buildingKind).building)return false;
    } else return false;
   }
  }
 }
 if(version>=10) {
  for(int team=0;team<playerCount;++team) {
   const bool anchor=std::any_of(loaded.entities_.begin(),loaded.entities_.end(),[&](const Entity& entity) {
    return entity.alive()&&entity.team==team&&entity.kind==Kind::Headquarters;
   });
   if(anchor==loaded.eliminated(team))return false;
  }
  for(const auto& entity:loaded.entities_)if(entity.alive()&&loaded.activeTeam(entity.team)&&loaded.eliminated(entity.team))return false;
 } else if(loaded.winner_>=0) {
  // Versions 1-9 ended immediately and could retain live losing actors. Their
  // authoritative winner migrates to the equivalent disabled loser state.
  const int legacyWinner=loaded.winner_;
  for(int team=0;team<playerCount;++team)if(team!=legacyWinner)loaded.eliminateTeam(team);
  loaded.winner_=legacyWinner;
 }
 if(version>=3){in>>std::ws;if(!in.eof())return false;}
 loaded.lastStepMs_=0;*this=std::move(loaded);return true;
}

bool Simulation::applySnapshot(const net::Snapshot& snapshot,std::string* error) {
 auto fail=[&](const std::string& message){if(error)*error=message;return false;};
 const std::uint64_t previousNoticeSerial=replica_?workerPlanNoticeSerials_[0]:0;
 const std::string previousAlert=replica_?alert_:"Online match synchronized.";
 const auto bytes=net::encodeSnapshot(snapshot);if(bytes.empty())return fail("Invalid server snapshot.");
 net::Snapshot checked;std::string codecError;
 if(!net::decodeSnapshot(bytes.data(),bytes.size(),checked,codecError))return fail(codecError);
 Simulation replica(EmptyReplicaTag{});
 replica.config_=checked.config;replica.config_.ai=false;replica.entities_=std::move(checked.entities);replica.obstacles_=std::move(checked.obstacles);replica.effects_=std::move(checked.effects);
 replica.players_={};replica.players_[0]=checked.player;
 for(int team=1;team<MaxPlayers;++team){replica.players_[team]=Player{};replica.players_[team].ore=0;replica.players_[team].tier=0;replica.players_[team].weapons=0;replica.players_[team].armor=0;replica.players_[team].armyRally={};replica.players_[team].armyRallySet=false;replica.players_[team].stats={};}
 replica.fog_={};replica.explored_={};
 for(std::size_t cell=0;cell<checked.fog.size();++cell){replica.fog_[0][cell]=checked.fog[cell]==2;replica.explored_[0][cell]=checked.fog[cell]!=0;}
 replica.recording_.clear();replica.tick_=checked.tick;replica.accumulator_=0;replica.aiTimer_=0;replica.winner_=checked.winner;replica.eliminatedMask_=checked.eliminatedMask;replica.nextId_=1;
 for(const auto& entity:replica.entities_)if(entity.alive()&&entity.team>=0&&replica.eliminated(entity.team))return fail("Eliminated players cannot retain active entities.");
 for(const auto& entity:replica.entities_)if(entity.id>=replica.nextId_)replica.nextId_=entity.id==std::numeric_limits<Id>::max()?entity.id:entity.id+1;
 replica.nextEffectId_=checked.lastEffectId+1;
 replica.workerPlanNotices_={};replica.workerPlanNoticeSerials_={};
 replica.workerPlanNotices_[0]=checked.workerPlanNotice;
 replica.workerPlanNoticeSerials_[0]=checked.workerPlanNoticeSerial;
 replica.alert_=checked.workerPlanNoticeSerial!=previousNoticeSerial&&!checked.workerPlanNotice.empty()
     ?checked.workerPlanNotice:previousAlert;
 replica.aiStatus_="Server authoritative";replica.lastStepMs_=0;replica.aiSightings_.clear();replica.aiObserved_={};replica.replica_=true;
 *this=std::move(replica);if(error)error->clear();return true;
}

void Simulation::forfeit(int team) {
 if(replica_||winner_!=-1||!activeTeam(team)||eliminated(team))return;
 eliminateTeam(team);updateVision();updateWinner();
}
} // namespace cinder
