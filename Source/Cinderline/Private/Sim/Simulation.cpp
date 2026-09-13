#include "Sim/Simulation.h"
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
Vec2 scaledFromStandard(Vec2 p,float worldSize) {
 const float factor=worldSize/Simulation::WorldSize;return scale(p,factor);
}
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
 replica_=false; buildAccessCached_=false; navigationDirty_=true; navigation_=Navigation{}; navigationStats_={}; movementBuckets_.clear(); movementRouteRequests_=0; isStepping_=false;
 config_=config; config_.map=std::clamp(config_.map,0,2);
 config_.matchLength=matchLengthAt(static_cast<int>(config_.matchLength));
 config_.playerCount=config_.playerCount==4?4:2;
 if(config_.playerCount==4)config_.ai=false;
 if(!std::isfinite(config_.aiAggression)) config_.aiAggression=1;
 config_.aiAggression=std::clamp(config_.aiAggression,0.5f,2.0f);
 entities_.clear(); obstacles_.clear(); effects_.clear(); recording_.clear(); aiSightings_.clear(); aiObserved_={};
 players_={}; fog_={}; explored_={}; tick_=0; nextId_=1; nextEffectId_=1; accumulator_=0; aiTimer_=0; winner_=-1; eliminatedMask_=0; lastStepMs_=0;
 alert_="Build a Kiln, scout, and protect your Anchor."; aiStatus_=config_.ai?"Establishing economy":"Opponent AI disabled";
 const auto profile=matchLengthProfile(config_.matchLength);const float size=profile.worldSize;
 auto mapPoint=[&](Vec2 point){return scaledFromStandard(point,size);};
 auto mapObstacle=[&](Vec2 center,Vec2 half){return Obstacle{mapPoint(center),mapPoint(half)};};
 // Two-player layouts retain their original geometry. Four-player layouts use
 // fourfold-symmetric lanes so no starting corner inherits the privileged side
 // of an obstacle pattern authored for a diagonal duel.
 if(config_.playerCount==2) {
  if(config_.map==0) {
   obstacles_={mapObstacle({2400,1570},{220,550}),mapObstacle({2400,3230},{220,550}),mapObstacle({1320,2400},{380,140}),mapObstacle({3480,2400},{380,140})};
  } else if(config_.map==1) {
   obstacles_={mapObstacle({2400,2400},{630,500}),mapObstacle({1450,1550},{180,330}),mapObstacle({3350,3250},{180,330}),mapObstacle({1400,3520},{420,120}),mapObstacle({3400,1280},{420,120})};
  } else {
   obstacles_={mapObstacle({2400,1000},{150,600}),mapObstacle({2400,3800},{150,600}),mapObstacle({1600,2100},{600,120}),mapObstacle({3200,2700},{600,120}),mapObstacle({850,3300},{180,350}),mapObstacle({3950,1500},{180,350})};
  }
 } else if(config_.map==0) {
  obstacles_={mapObstacle({2400,1500},{180,420}),mapObstacle({2400,3300},{180,420}),
              mapObstacle({1500,2400},{420,180}),mapObstacle({3300,2400},{420,180})};
 } else if(config_.map==1) {
  obstacles_={mapObstacle({2400,2400},{520,520}),
              mapObstacle({1500,1500},{150,300}),mapObstacle({3300,1500},{300,150}),
              mapObstacle({3300,3300},{150,300}),mapObstacle({1500,3300},{300,150})};
 } else {
  obstacles_={mapObstacle({2400,1100},{130,500}),mapObstacle({3700,2400},{500,130}),
              mapObstacle({2400,3700},{130,500}),mapObstacle({1100,2400},{500,130}),
              mapObstacle({1550,1550},{120,280}),mapObstacle({3250,1550},{280,120}),
              mapObstacle({3250,3250},{120,280}),mapObstacle({1550,3250},{280,120})};
 }
 for(int team=0;team<config_.playerCount;++team) {
  const bool right=team==1||team==2;
  const bool top=team==1||team==3;
  const Vec2 base=mapPoint({right?4200.0f:600.0f,top?4200.0f:600.0f});
  spawn(Kind::Headquarters,team,base);
  const std::array<Vec2,4> offsets{{{-300,170},{-230,290},{-100,370},{65,400}}};
  std::vector<Id> nodes;
  for(const auto offset:offsets) {
   const Vec2 scaledOffset=mapPoint(offset);
   Id id=spawn(Kind::Resource,-1,add(base,startOffset(team,scaledOffset)));
   get(id)->resource=profile.homeNodeOre; nodes.push_back(id);
  }
  for(int i=0;i<5;++i) {
   // Unit radii do not scale with the battlefield. Keep the original local
   // formation spacing so Short starts cannot overlap the Anchor or each other.
   Id id=spawn(Kind::Worker,team,add(base,startOffset(team,{static_cast<float>(155+i*34),55})));
   Entity* worker=get(id);worker->order=Order::Gather;worker->target=nodes[i%nodes.size()];worker->resourceTarget=worker->target;
  }
 }
 if(config_.playerCount==2) {
  for(Vec2 standardCluster:std::array<Vec2,4>{{{1900,1000},{2900,3800},{1000,2800},{3800,2000}}}) {
   const Vec2 cluster=mapPoint(standardCluster);
   for(int i=0;i<3;++i) {
    Vec2 p=add(cluster,mapPoint({static_cast<float>(i-1)*120,static_cast<float>((i%2)*90)}));
    if(blocked(p,definition(Kind::Resource).radius)) p=add(p,mapPoint({0,250}));
    Id id=spawn(Kind::Resource,-1,p);get(id)->resource=profile.expansionNodeOre;
   }
  }
 } else {
  struct Cluster { Vec2 center,tangent,inward; };
  const std::array<Cluster,4> clusters{{
   {{1500,1000},{1,0},{1,1}},{{3800,1500},{0,1},{-1,1}},
   {{3300,3800},{-1,0},{-1,-1}},{{1000,3300},{0,-1},{1,-1}}
  }};
  for(const auto& authored:clusters)for(int i=0;i<3;++i) {
   const float side=static_cast<float>(i-1)*180;
   const float inset=i==1?60.0f:0.0f;
   const Vec2 standard{authored.center.x+authored.tangent.x*side+authored.inward.x*inset,
                       authored.center.y+authored.tangent.y*side+authored.inward.y*inset};
   Id id=spawn(Kind::Resource,-1,mapPoint(standard));get(id)->resource=profile.expansionNodeOre;
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
bool Simulation::visible(int team,Vec2 position) const { return activeTeam(team)&&finite(position)&&position.x>=0&&position.y>=0&&position.x<=worldSize()&&position.y<=worldSize()&&fog_[team][fogIndex(position,worldSize())]!=0; }
bool Simulation::explored(int team,Vec2 position) const { return activeTeam(team)&&finite(position)&&position.x>=0&&position.y>=0&&position.x<=worldSize()&&position.y<=worldSize()&&explored_[team][fogIndex(position,worldSize())]!=0; }
bool Simulation::canPlace(int team,Kind kind,Vec2 point,std::string* reason) const {
 auto fail=[&](const char* message){if(reason)*reason=message;return false;};
 if(!activeTeam(team)||eliminated(team)||!rules::validKind(kind)||!definition(kind).building)return fail("Choose a structure.");
 if(!finite(point))return fail("Invalid position.");
 if(!visible(team,point))return fail("Keep the construction site in current vision.");
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
     (entity.order==Order::Idle||entity.order==Order::Gather))selected.push_back(entity.id);
  std::sort(selected.begin(),selected.end());
 }
 if(selected.empty())return fail(automatic?"No idle or mining Drudge is available.":site?"Select your units or a structure first.":"Select a Drudge to build this structure.");
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
void Simulation::abandonConstruction(Entity& worker) {
 if(worker.kind!=Kind::Worker)return;
 if(worker.order==Order::Construct) {
  Entity* foundation=get(worker.target);
  if(foundation&&foundation->builderId==worker.id)foundation->builderId=0;
  worker.order=Order::Idle;worker.target=0;worker.goal=worker.pos;
  resetNavigation(worker);
 }
 worker.resumeGather=false;
}
void Simulation::releaseConstruction(Entity& foundation) {
 const Id assigned=foundation.builderId;foundation.builderId=0;Entity* worker=get(assigned);
 if(!worker||!worker->alive()||worker->kind!=Kind::Worker||worker->team!=foundation.team||worker->order!=Order::Construct||worker->target!=foundation.id)return;
 const bool resume=worker->resumeGather;abandonConstruction(*worker);
 if(!resume)return;
 const Entity* deposit=find(worker->resourceTarget);
 if(!deposit||!deposit->alive()||deposit->kind!=Kind::Resource||deposit->resource<=0) {
  Id replacement=0;float best=std::numeric_limits<float>::max();
  for(const auto& candidate:entities_)if(candidate.alive()&&candidate.kind==Kind::Resource&&candidate.resource>0&&explored(worker->team,candidate.pos)) {
   float d=distanceSq(worker->pos,candidate.pos);if(d<best){best=d;replacement=candidate.id;}
  }
  worker->resourceTarget=replacement;deposit=find(replacement);
 }
 if(worker->carried>=CarryCapacity||(!deposit&&worker->carried>0))worker->returning=true;
 if(deposit||worker->carried>0) {worker->order=Order::Gather;worker->target=worker->resourceTarget;}
}
void Simulation::assignConstruction(Entity& foundation,Entity& worker) {
 const bool resume=worker.order==Order::Gather||(worker.order==Order::Construct&&worker.resumeGather);
 abandonConstruction(worker);releaseConstruction(foundation);
 worker.resumeGather=resume;worker.order=Order::Construct;worker.target=foundation.id;worker.goal=foundation.pos;
 resetNavigation(worker);foundation.builderId=worker.id;
}

CommandResult Simulation::command(const Command& input) {
 auto fail=[](const std::string& why){return CommandResult{false,why};};
 if(replica_)return fail("Online replicas accept server snapshots only.");
 if(winner_!=-1)return fail("The match has ended.");
 if(!activeTeam(input.team)||!rules::validKind(input.kind)||!finite(input.point)||input.point.x<0||input.point.y<0||input.point.x>worldSize()||input.point.y>worldSize())return fail("Invalid command.");
 if(eliminated(input.team))return fail("That player has been eliminated.");
 const int rawType=static_cast<int>(input.type);
 if(rawType<0||rawType>static_cast<int>(CommandType::AutoRally)||input.units.size()>500)return fail("Invalid command.");
 const bool automatic=rawType>=static_cast<int>(CommandType::AutoBuild);
 if(automatic&&(!input.units.empty()||input.queueIndex<0||input.queueIndex>MaxQueue))return fail("Invalid automatic job request.");
 if((input.type==CommandType::AutoBuild&&(input.target||input.queueIndex))||
    (input.type==CommandType::AutoRally&&(input.queueIndex>1||(input.queueIndex==1&&!input.target))))return fail("Invalid automatic job request.");
 if(input.type==CommandType::CancelQueue&&input.units.size()!=1)return fail("Choose one producer and a valid queue item.");
 Command cmd=input;std::sort(cmd.units.begin(),cmd.units.end());cmd.units.erase(std::unique(cmd.units.begin(),cmd.units.end()),cmd.units.end());
 std::vector<Id> ids;
 for(Id id:cmd.units) { const Entity* e=find(id);if(!e||!e->alive()||e->team!=cmd.team||e->kind==Kind::Resource)return fail("The selection contains unavailable or foreign units.");ids.push_back(id); }
 if(ids.empty()&&!automatic)return fail("Select your units or a structure first.");
 Player& player=players_[cmd.team];std::string feedback="Order acknowledged.";
 if(cmd.type==CommandType::Build||cmd.type==CommandType::AutoBuild) {
  Id workerId=0;const auto status=checkBuild(cmd.team,cmd.kind,ids,&cmd.point,&workerId,cmd.type==CommandType::AutoBuild);
  if(!status.accepted)return status;
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
  assignConstruction(*foundation,*get(workerId));feedback="Drudge assigned to resume construction.";
 } else if(cmd.type==CommandType::AutoTrain) {
  const auto plan=autoTrainStatus(cmd.team,cmd.kind,cmd.queueIndex,cmd.target);
  if(!plan.accepted)return {false,plan.message};
  const auto& d=definition(cmd.kind);
  for(const auto& assignment:plan.assignments) {
   Entity* producer=get(assignment.producer);
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
   Entity* producer=get(cmd.target);producer->rallyOverride=false;
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
    entity.rally=bounded(cmd.point,worldSize());entity.rallyOverride=true;
    if(entity.kind==Kind::Headquarters)for(auto& item:entity.queue)if(item.kind==Kind::Worker&&!item.research) {
     item.assignmentCursor=0;item.assignmentCandidates.clear();
    }
   }
   feedback="Production rally point updated.";
  }
 } else if(cmd.type==CommandType::Train) {
  const auto& d=definition(cmd.kind);if(d.building||cmd.kind==Kind::Resource)return fail("Choose a unit to train.");
  Entity* producer=nullptr;bool operational=false;for(Id id:ids){auto* e=get(id);if(e->kind==d.producer&&e->progress>=1){operational=true;if(e->queue.size()<MaxQueue&&e->nextQueueId>0&&e->nextQueueId<std::numeric_limits<Id>::max()){producer=e;break;}}}
  if(!producer)return fail(operational?"Queue full ("+std::to_string(MaxQueue)+").":"Select an operational producer with queue space.");
  if(player.tier<d.tier)return fail("Research the required technology tier first.");
  if(player.ore<d.cost)return fail("Insufficient ore.");
  if(supply(cmd.team)+d.supply>capacity(cmd.team))return fail("Supply full. Build a Siphon or Anchor.");
  const float duration=productionTime(d.buildTime);
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
  player.ore+=static_cast<int>(std::floor(item.cost*fraction+0.001f));e->queue.erase(itemPosition);feedback="Queue item cancelled; unused ore refunded.";
 } else if(cmd.type==CommandType::CancelBuilding) {
  bool cancelled=false;for(Id id:ids){auto* e=get(id);if(definition(e->kind).building&&e->progress<1){player.ore+=static_cast<int>(std::floor(definition(e->kind).cost*(1-e->progress)));releaseConstruction(*e);e->hp=0;navigationDirty_=true;e->queue.clear();cancelled=true;}}
  if(!cancelled)return fail("Select an unfinished structure.");feedback="Construction cancelled; unused ore refunded.";
 } else if(cmd.type==CommandType::Rally) {
  bool accepted=false;for(Id id:ids){auto* e=get(id);if(definition(e->kind).building){e->rally=bounded(cmd.point,worldSize());if(rules::productionKind(e->kind))e->rallyOverride=true;
   if(e->kind==Kind::Headquarters)for(auto& item:e->queue)if(item.kind==Kind::Worker&&!item.research){item.assignmentCursor=0;item.assignmentCandidates.clear();}
   accepted=true;}}
  if(!accepted)return fail("Select a production structure.");feedback="Rally point updated.";
 } else {
  const Entity* target=find(cmd.target);
  if(cmd.type==CommandType::Attack) {
   if(!target||!target->alive()||target->team==cmd.team||target->team<0||!visible(cmd.team,target->pos))return fail("Choose a visible enemy.");
  }
  if(cmd.type==CommandType::Gather&&(!target||!target->alive()||target->kind!=Kind::Resource||target->resource<=0||!explored(cmd.team,target->pos)))return fail("Choose an explored ore deposit.");
  std::vector<Id> movable;for(Id id:ids){const auto* e=find(id);if(!definition(e->kind).building&&(cmd.type!=CommandType::Gather||e->kind==Kind::Worker)&&(cmd.type!=CommandType::Attack||definition(e->kind).damage>0)&&!(cmd.type==CommandType::Attack&&definition(target->kind).air&&!definition(e->kind).antiAir))movable.push_back(id);}
  if(movable.empty())return fail("Selected units cannot execute this order.");
  Id attackLeader=0;
  if(cmd.type==CommandType::Attack||cmd.type==CommandType::AttackMove)for(Id id:movable)if(definition(find(id)->kind).damage>0){attackLeader=id;break;}
  if(cmd.type==CommandType::Attack&&attackLeader)for(Id id:ids)if(find(id)->kind==Kind::Mender)movable.push_back(id);
  const int columns=static_cast<int>(std::ceil(std::sqrt(static_cast<float>(movable.size()))));
  std::vector<std::pair<Vec2,float>> assignedGoals;
  for(std::size_t n=0;n<movable.size();++n) {
   Entity* e=get(movable[n]);if(e->kind==Kind::Worker)abandonConstruction(*e);resetNavigation(*e);e->target=0;
   switch(cmd.type) {
    case CommandType::Stop:e->order=Order::Idle;e->goal=e->pos;break;
    case CommandType::Hold:e->order=Order::Hold;e->goal=e->pos;break;
    case CommandType::Gather:e->order=Order::Gather;e->target=cmd.target;e->resourceTarget=cmd.target;e->returning=e->carried>=CarryCapacity;break;
    case CommandType::Attack:e->order=Order::Attack;e->target=e->kind==Kind::Mender?attackLeader:cmd.target;e->goal=find(e->target)->pos;break;
    case CommandType::Move:case CommandType::AttackMove:case CommandType::Defend: {
     if(cmd.type==CommandType::AttackMove&&e->kind==Kind::Mender&&attackLeader) {
      e->order=Order::Attack;e->target=attackLeader;e->goal=find(attackLeader)->pos;break;
     }
     const float spacing=64;Vec2 offset{(static_cast<int>(n)%columns-(columns-1)*0.5f)*spacing,(static_cast<int>(n)/columns-(columns-1)*0.5f)*spacing};
     e->order=cmd.type==CommandType::Move?Order::Move:
              cmd.type==CommandType::AttackMove?Order::AttackMove:Order::Defend;
     const auto& unit=definition(e->kind);const Vec2 desired=bounded(add(cmd.point,offset),worldSize(),unit.radius);Vec2 destination=desired;
     auto available=[&](Vec2 point) {
      if(point.x<unit.radius||point.y<unit.radius||point.x>worldSize()-unit.radius||point.y>worldSize()-unit.radius)return false;
      if(!unit.air&&blocked(point,unit.radius+4,e->id))return false;
      for(const auto& assigned:assignedGoals) {const float clearance=unit.radius+assigned.second+10;if(distanceSq(point,assigned.first)<clearance*clearance)return false;}
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
     e->goal=destination;assignedGoals.push_back({destination,unit.radius});break;
    }
    default:break;
   }
  }
 }
 recording_.push_back({tick_,cmd});if(cmd.team==0)alert_=feedback;return {true,feedback};
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
 isStepping_=true;movementRouteRequests_=0;
 for(auto& e:entities_)if(e.alive()){e.cooldown=std::max(0.0f,e.cooldown-Step);e.repath=std::max(0.0f,e.repath-Step);}
 for(auto& fx:effects_)fx.life-=Step;
 effects_.erase(std::remove_if(effects_.begin(),effects_.end(),[](const Effect& fx){return fx.life<=0;}),effects_.end());
 // Production can append to entities_; retain IDs and reacquire each object after it does.
 std::vector<Id> production;
 for(const auto& e:entities_)if(e.alive()&&definition(e.kind).building)production.push_back(e.id);
 for(Id id:production){Entity* e=get(id);if(e&&e->alive())updateProduction(*e);}
 beginMovementStep();
 for(auto& e:entities_)if(e.alive()&&e.progress>=1){if(e.kind==Kind::Worker&&e.order==Order::Gather)updateEconomy(e);else updateMovement(e);}
 finishMovementStep();
 updateVision();
 for(auto& e:entities_)if(e.alive()&&e.progress>=1)updateCombat(e);
 if(config_.ai) {aiTimer_-=Step;if(aiTimer_<=0){aiTimer_=2;updateAI();}}
 updateEliminations();
 if(tick_%100==0)entities_.erase(std::remove_if(entities_.begin(),entities_.end(),[](const Entity& e){return !e.alive();}),entities_.end());
 lastStepMs_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
}

void Simulation::eliminateTeam(int team) {
 if(!activeTeam(team)||eliminated(team))return;
 // Release paired construction state before disabling the defeated army. No
 // defeated actor may retain a live queue, target or navigation reservation.
 for(auto& entity:entities_)if(entity.alive()&&entity.team==team&&entity.kind==Kind::Worker)abandonConstruction(entity);
 for(auto& entity:entities_)if(entity.alive()&&entity.team==team&&definition(entity.kind).building)releaseConstruction(entity);
 for(auto& entity:entities_)if(entity.alive()&&entity.team==team) {
  entity.hp=0;entity.queue.clear();entity.target=0;entity.resourceTarget=0;entity.builderId=0;
  entity.order=Order::Idle;entity.goal=entity.pos;entity.returning=false;entity.resumeGather=false;
  resetNavigation(entity);
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

void Simulation::updateProduction(Entity& producer) {
 const auto& d=definition(producer.kind);
 if(producer.progress<1) {
  if(!constructionActive(producer.id))return;
  const float old=producer.progress;
  const float previousMaximum=d.hp*(0.1f+old*0.9f);
  const float constructionDamage=std::max(0.0f,previousMaximum-producer.hp);
  producer.progress=std::min(1.0f,old+Step/std::max(1.0f,productionTime(d.buildTime)));
  const float currentMaximum=producer.progress>=1?d.hp:d.hp*(0.1f+producer.progress*0.9f);
  producer.hp=std::max(0.0f,currentMaximum-constructionDamage);
  if(producer.progress>=1) {
   releaseConstruction(producer);
   ++players_[producer.team].stats.built;
   if(producer.kind==Kind::Processor||producer.kind==Kind::Headquarters)++players_[producer.team].stats.expansions;
   if(producer.team==0)alert_=std::string(d.name)+" ready.";
  }
  return;
 }
 if(producer.queue.empty())return;
 QueueItem& q=producer.queue.front();
 if(!q.research&&supply(producer.team)>capacity(producer.team))return;
 q.remaining=std::max(0.0f,q.remaining-Step);if(q.remaining>0)return;
 const QueueItem item=q;const Id producerId=producer.id;const int team=producer.team;const Vec2 position=producer.pos,rally=producer.rally;
 const bool producerRallyOverride=producer.rallyOverride;
 if(item.research) {
  Player& player=players_[team];
  if(item.kind==Kind::Worker)player.tier=std::min(3,player.tier+1);
  else if(item.kind==Kind::Striker)player.weapons=std::min(3,player.weapons+1);
  else if(item.kind==Kind::Lancer)player.armor=std::min(3,player.armor+1);
  ++player.stats.upgrades;producer.queue.erase(producer.queue.begin());if(team==0)alert_="Research completed.";return;
 }
 const auto& unit=definition(item.kind);Vec2 exit{};bool found=false;
 for(int ring=0;ring<6&&!found;++ring)for(int n=0;n<16;++n) {
  float angle=std::atan2(rally.y-position.y,rally.x-position.x)+n*Pi/8;
  Vec2 candidate=add(position,{std::cos(angle)*(d.radius+unit.radius+28+ring*35),std::sin(angle)*(d.radius+unit.radius+28+ring*35)});
  if(candidate.x<unit.radius||candidate.y<unit.radius||candidate.x>worldSize()-unit.radius||candidate.y>worldSize()-unit.radius)continue;
  if(unit.air||!blocked(candidate,unit.radius)){exit=candidate;found=true;break;}
 }
 if(!found)return; // Keep the paid item ready until an exit becomes available.
 Entity miningAssignment;bool mining=false;
 bool seekMining=item.kind==Kind::Worker&&!producerRallyOverride;
 if(item.kind==Kind::Worker&&producerRallyOverride&&q.assignmentCandidates.empty()) {
  Id rallyResource=0;
  for(const auto& entity:entities_)if(entity.alive()&&entity.kind==Kind::Resource&&entity.resource>0&&explored(team,entity.pos)&&
     distanceSq(rally,entity.pos)<=definition(Kind::Resource).radius*definition(Kind::Resource).radius&&(!rallyResource||entity.id<rallyResource))rallyResource=entity.id;
  if(rallyResource){q.assignmentCandidates.push_back(rallyResource);seekMining=true;}
 } else if(item.kind==Kind::Worker&&producerRallyOverride&&!q.assignmentCandidates.empty())seekMining=true;
 if(seekMining) {
  miningAssignment.id=nextId_;miningAssignment.kind=Kind::Worker;miningAssignment.team=team;
  miningAssignment.pos=miningAssignment.goal=exit;miningAssignment.rally=rally;
  miningAssignment.hp=unit.hp;miningAssignment.progress=1;
  bool deferred=false;
  mining=assignFreshWorkerToOre(miningAssignment,q.assignmentCandidates,q.assignmentCursor,deferred);
  if(deferred)return; // Keep the paid, completed item until its bounded search resumes.
 }
 producer.queue.erase(producer.queue.begin());
 Id id=spawn(item.kind,team,exit); // Never access producer or q after this append.
 Entity* created=get(id);if(!created)return;
 ++players_[team].stats.produced;
 // Give repeated production a real rally formation so settled units can retain
 // their destination without all competing for the producer's exact rally point.
 Vec2 arrival=rally;
 auto rallyAvailable=[&](Vec2 point) {
  if(point.x<unit.radius||point.y<unit.radius||point.x>worldSize()-unit.radius||point.y>worldSize()-unit.radius)return false;
  if(!unit.air&&blocked(point,unit.radius+4,id))return false;
  for(const auto& other:entities_) {
   if(!other.alive()||other.id==id||other.team!=team||definition(other.kind).building||other.order==Order::Gather)continue;
   const float spacing=unit.radius+definition(other.kind).radius+20;
   if(distanceSq(point,other.goal)<spacing*spacing)return false;
  }
  return true;
 };
 bool rallyFound=rallyAvailable(arrival);
 for(int ring=1;ring<=15&&!rallyFound;++ring)for(int spoke=0;spoke<24;++spoke) {
  float angle=spoke*(2*Pi/24);Vec2 candidate=add(rally,{std::cos(angle)*ring*64,std::sin(angle)*ring*64});
  if(rallyAvailable(candidate)){arrival=candidate;rallyFound=true;break;}
 }
 if(created->kind==Kind::Worker) {
  if(mining) {
   created->order=miningAssignment.order;created->target=miningAssignment.target;created->resourceTarget=miningAssignment.resourceTarget;
   created->workTarget=miningAssignment.workTarget;created->workPoint=miningAssignment.workPoint;created->workPointValid=miningAssignment.workPointValid;
   created->path=std::move(miningAssignment.path);created->pathIndex=miningAssignment.pathIndex;created->pathGeometry=miningAssignment.pathGeometry;
  } else {created->order=Order::Move;created->goal=arrival;}
  if(team==0&&!mining&&!producerRallyOverride)alert_="Drudge ready; no reachable ore job.";
 } else {created->order=Order::Move;created->goal=arrival;}
 if(team==0&&(created->kind!=Kind::Worker||producerRallyOverride||created->order==Order::Gather))alert_=std::string(unit.name)+" ready.";
 (void)producerId;
}

void Simulation::updateEconomy(Entity& worker) {
 if(worker.kind!=Kind::Worker||worker.order!=Order::Gather)return;
 ensureNavigation();
 if(worker.carried>=CarryCapacity-0.001f)worker.returning=true;
 if(worker.returning) {
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
    worker.carried=0;worker.returning=false;resetNavigation(worker);return;
   }
   depots.push_back(entity.id);
  }
  if(depots.empty()){worker.order=Order::Idle;resetNavigation(worker);return;}
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
  std::vector<Id> candidates;
  for(const auto& entity:entities_)if(entity.alive()&&entity.kind==Kind::Resource&&
      entity.resource>0&&explored(worker.team,entity.pos))candidates.push_back(entity.id);
  if(candidates.empty()) {
   if(worker.carried>0){worker.returning=true;resetNavigation(worker);}
   else {worker.order=Order::Idle;resetNavigation(worker);}
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
 if(e.kind==Kind::Mender&&e.order==Order::Attack) {
  const Entity* ally=find(e.target);
  if(!ally||!ally->alive()||ally->team!=e.team){e.target=0;e.order=Order::Idle;e.goal=e.pos;return;}
  e.goal=ally->pos;
  if(distance(e.pos,ally->pos)>d.range*0.7f)moveToward(e,add(ally->pos,scale(normalized(subtract(e.pos,ally->pos)),d.range*0.55f)));
  return;
 }
 if(e.order==Order::Move||e.order==Order::AttackMove) {
  if(e.order==Order::AttackMove&&e.target) {
   const Entity* target=find(e.target);
   if(target&&target->alive()&&visible(e.team,target->pos)&&distance(e.pos,target->pos)<=d.vision) {
    pursue(*target);return;
   }
   e.target=0;
  }
  if(distanceSq(e.pos,e.goal)<20*20){e.order=Order::Idle;e.path.clear();e.pathIndex=0;return;}
  moveToward(e,e.goal);
 } else if(e.order==Order::Attack) {
  const Entity* target=find(e.target);
  if(!target||!target->alive()){e.target=0;e.order=Order::Idle;return;}
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
  if(victim.kind==Kind::Worker)abandonConstruction(victim);
  else if(definition(victim.kind).building)releaseConstruction(victim);
  victim.hp=0;navigationDirty_=true;victim.queue.clear();victim.path.clear();victim.pathIndex=0;
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
  if(e.cooldown>0||e.order==Order::Move)return;
  Entity* patient=nullptr;float need=0;
  for(auto& ally:entities_)if(ally.alive()&&ally.team==e.team&&ally.id!=e.id&&!definition(ally.kind).building&&ally.hp<definition(ally.kind).hp&&distanceSq(ally.pos,e.pos)<=d.range*d.range) {
   float missing=1-ally.hp/definition(ally.kind).hp;if(missing>need){need=missing;patient=&ally;}
  }
  if(patient) {
   patient->hp=std::min(definition(patient->kind).hp,patient->hp+16);e.cooldown=d.cooldown;
   emitEffect(EffectType::Heal,e.pos,patient->pos,e.team,e.kind,patient->kind,0.45f);
  }
  return;
 }
 if(d.damage<=0||e.order==Order::Move||e.order==Order::Gather||e.order==Order::Construct)return;
 auto validTarget=[&](const Entity* target) {return target&&target->alive()&&target->team>=0&&target->team!=e.team&&target->kind!=Kind::Resource&&(!definition(target->kind).air||d.antiAir)&&visible(e.team,target->pos);};
 Entity* target=get(e.target);
 if(!validTarget(target))target=nullptr;
 if(target&&e.order!=Order::Attack) {
  const float acquisition=(e.order==Order::Hold||e.order==Order::Defend||d.building)?d.range:d.vision;
  if(distance(e.pos,target->pos)>acquisition+definition(target->kind).radius)target=nullptr;
 }
 if(!target) {
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
 if(!target||e.cooldown>0)return;
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
 for(const auto& e:entities_) {
  if(!e.alive()||!activeTeam(e.team)||e.kind==Kind::Resource)continue;
  const float radius=definition(e.kind).vision*(e.progress>=1?1.0f:0.5f);
  int minX=std::clamp(static_cast<int>((e.pos.x-radius)/cell),0,FogSize-1),maxX=std::clamp(static_cast<int>((e.pos.x+radius)/cell),0,FogSize-1);
  int minY=std::clamp(static_cast<int>((e.pos.y-radius)/cell),0,FogSize-1),maxY=std::clamp(static_cast<int>((e.pos.y+radius)/cell),0,FogSize-1);
  for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x) {
   Vec2 center{(x+0.5f)*cell,(y+0.5f)*cell};if(distanceSq(e.pos,center)>(radius+cell*0.5f)*(radius+cell*0.5f))continue;
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
 return hash.value;
}

bool Simulation::save(const std::string& path) const {
 if(replica_)return false;
 std::ofstream out(path,std::ios::trunc);if(!out)return false;out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<float>::max_digits10);
 out<<"CINDERLINE 10\n"<<config_.map<<' '<<config_.seed<<' '<<config_.ai<<' '<<config_.aiAggression<<' '<<static_cast<int>(config_.matchLength)<<' '<<config_.playerCount<<'\n';
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
  const auto& c=r.command;out<<r.tick<<' '<<static_cast<int>(c.type)<<' '<<c.team<<' '<<c.point.x<<' '<<c.point.y<<' '<<c.target<<' '<<static_cast<int>(c.kind)<<' '<<c.queueIndex<<' '<<c.units.size();for(Id id:c.units)out<<' '<<id;out<<'\n';
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
 out.flush();return out.good();
}
bool Simulation::load(const std::string& path) {
 if(replica_)return false;
 std::ifstream in(path);if(!in)return false;in.imbue(std::locale::classic());std::string magic;int version=0;in>>magic>>version;if(magic!="CINDERLINE"||(version<1||version>10))return false;
 Simulation loaded;loaded.entities_.clear();loaded.obstacles_.clear();loaded.effects_.clear();loaded.recording_.clear();
 in>>loaded.config_.map>>loaded.config_.seed>>loaded.config_.ai>>loaded.config_.aiAggression;
 int length=static_cast<int>(MatchLength::Standard);if(version>=9)in>>length;
 if(length<0||length>=static_cast<int>(MatchLength::Count))return false;
 loaded.config_.matchLength=static_cast<MatchLength>(length);
 int playerCount=2;if(version>=10)in>>playerCount;
 if(!rules::validPlayerCount(playerCount))return false;
 loaded.config_.playerCount=playerCount;if(playerCount==4)loaded.config_.ai=false;
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
  const Order maximumOrder=version>=6?Order::Defend:(version==1?Order::Hold:Order::Construct);
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
  if(e.resumeGather&&(e.kind!=Kind::Worker||e.order!=Order::Construct||!e.alive()))return false;
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
  RecordedCommand r;Command& c=r.command;int type,kind;std::size_t unitCount;in>>r.tick>>type>>c.team>>c.point.x>>c.point.y>>c.target>>kind>>c.queueIndex>>unitCount;c.type=static_cast<CommandType>(type);c.kind=static_cast<Kind>(kind);
  const CommandType maximumCommand=version>=7?CommandType::AutoRally:version>=6?CommandType::Defend:(version==1?CommandType::CancelBuilding:CommandType::ResumeConstruction);
  if(!in||r.tick>loaded.tick_||type<0||type>static_cast<int>(maximumCommand)||!loaded.activeTeam(c.team)||!rules::validKind(c.kind)||!finite(c.point)||unitCount>500)return false;
  for(std::size_t n=0;n<unitCount;++n){Id id;in>>id;c.units.push_back(id);}if(!in)return false;
  if(version<7&&c.type==CommandType::CancelQueue) {
   // Older cancellation used the first producer and ignored target entirely.
   // Preserve that operation when an older recording is saved in version 7.
   if(c.units.size()>1)c.units.resize(1);
   c.target=0;
  }
  if(version>=7) {
   const bool automatic=type>=static_cast<int>(CommandType::AutoBuild);
   if(automatic!=c.units.empty()||c.point.x<0||c.point.y<0||c.point.x>loaded.worldSize()||c.point.y>loaded.worldSize())return false;
   if(automatic&&(c.queueIndex<0||c.queueIndex>MaxQueue))return false;
   if(c.type==CommandType::AutoBuild&&(!definition(c.kind).building||c.target||c.queueIndex))return false;
   if(c.type==CommandType::AutoTrain&&(definition(c.kind).building||c.kind==Kind::Resource||c.queueIndex<1))return false;
   if(c.type==CommandType::AutoResearch&&c.queueIndex>2)return false;
   if(c.type==CommandType::AutoRally&&((c.queueIndex<0||c.queueIndex>1)||(c.queueIndex==1&&!c.target)||
      (c.kind!=Kind::Resource&&!rules::productionKind(c.kind))))return false;
   if(c.type==CommandType::CancelQueue&&(c.units.size()!=1||c.queueIndex<0||c.queueIndex>=MaxQueue))return false;
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
 const auto bytes=net::encodeSnapshot(snapshot);if(bytes.empty())return fail("Invalid server snapshot.");
 net::Snapshot checked;std::string codecError;
 if(!net::decodeSnapshot(bytes.data(),bytes.size(),checked,codecError))return fail(codecError);
 Simulation replica;
 replica.config_=checked.config;replica.config_.ai=false;replica.entities_=std::move(checked.entities);replica.obstacles_=std::move(checked.obstacles);replica.effects_=std::move(checked.effects);
 replica.players_={};replica.players_[0]=checked.player;
 for(int team=1;team<MaxPlayers;++team){replica.players_[team]=Player{};replica.players_[team].ore=0;replica.players_[team].tier=0;replica.players_[team].weapons=0;replica.players_[team].armor=0;replica.players_[team].armyRally={};replica.players_[team].armyRallySet=false;replica.players_[team].stats={};}
 replica.fog_={};replica.explored_={};
 for(std::size_t cell=0;cell<checked.fog.size();++cell){replica.fog_[0][cell]=checked.fog[cell]==2;replica.explored_[0][cell]=checked.fog[cell]!=0;}
 replica.recording_.clear();replica.tick_=checked.tick;replica.accumulator_=0;replica.aiTimer_=0;replica.winner_=checked.winner;replica.eliminatedMask_=checked.eliminatedMask;replica.nextId_=1;
 for(const auto& entity:replica.entities_)if(entity.alive()&&entity.team>=0&&replica.eliminated(entity.team))return fail("Eliminated players cannot retain active entities.");
 for(const auto& entity:replica.entities_)if(entity.id>=replica.nextId_)replica.nextId_=entity.id==std::numeric_limits<Id>::max()?entity.id:entity.id+1;
 replica.nextEffectId_=checked.lastEffectId+1;replica.alert_="Online match synchronized.";replica.aiStatus_="Server authoritative";replica.lastStepMs_=0;replica.aiSightings_.clear();replica.aiObserved_={};replica.replica_=true;
 *this=std::move(replica);if(error)error->clear();return true;
}

void Simulation::forfeit(int team) {
 if(replica_||winner_!=-1||!activeTeam(team)||eliminated(team))return;
 eliminateTeam(team);updateVision();updateWinner();
}
} // namespace cinder
