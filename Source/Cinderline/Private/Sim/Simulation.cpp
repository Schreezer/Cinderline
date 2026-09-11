#include "Sim/Simulation.h"
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
constexpr float Cell = Simulation::WorldSize / Simulation::FogSize;
constexpr float CarryCapacity = 18.0f;
constexpr float HarvestPeriod = 0.65f;
constexpr int MaxQueue = 8;
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
bool validKind(Kind kind) { const int n=static_cast<int>(kind); return n>=0&&n<15; }
bool validTeam(int team) { return team==0||team==1; }
Vec2 bounded(Vec2 p,float radius=1) { return {std::clamp(p.x,radius,Simulation::WorldSize-radius),std::clamp(p.y,radius,Simulation::WorldSize-radius)}; }
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
Kind researchKind(int index) { return index==1?Kind::Striker:index==2?Kind::Lancer:Kind::Worker; }
int fogIndex(Vec2 p) {
 int x=std::clamp(static_cast<int>(p.x/Cell),0,Simulation::FogSize-1);
 int y=std::clamp(static_cast<int>(p.y/Cell),0,Simulation::FogSize-1);
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
const Definition& definition(Kind kind) { return Data[validKind(kind)?static_cast<int>(kind):0]; }
Simulation::Simulation() { reset(); }
void Simulation::reset(Config config) {
 config_=config; config_.map=std::clamp(config_.map,0,2);
 if(!std::isfinite(config_.aiAggression)) config_.aiAggression=1;
 config_.aiAggression=std::clamp(config_.aiAggression,0.5f,2.0f);
 entities_.clear(); obstacles_.clear(); effects_.clear(); recording_.clear(); aiSightings_.clear(); aiObserved_={};
 players_={}; fog_={}; explored_={}; tick_=0; nextId_=1; accumulator_=0; aiTimer_=0; winner_=-1; lastStepMs_=0;
 alert_="Build a Kiln, scout, and protect your Anchor."; aiStatus_=config_.ai?"Establishing economy":"Opponent AI disabled";
 // Each map is rotationally symmetric. Every obstacle leaves multiple routes.
 if(config_.map==0) {
  obstacles_={{{2400,1570},{220,550}},{{2400,3230},{220,550}},{{1320,2400},{380,140}},{{3480,2400},{380,140}}};
 } else if(config_.map==1) {
  obstacles_={{{2400,2400},{630,500}},{{1450,1550},{180,330}},{{3350,3250},{180,330}},{{1400,3520},{420,120}},{{3400,1280},{420,120}}};
 } else {
  obstacles_={{{2400,1000},{150,600}},{{2400,3800},{150,600}},{{1600,2100},{600,120}},{{3200,2700},{600,120}},{{850,3300},{180,350}},{{3950,1500},{180,350}}};
 }
 for(int team=0;team<2;++team) {
  const Vec2 base=team==0?Vec2{600,600}:Vec2{4200,4200};
  const float sign=team==0?1.0f:-1.0f;
  spawn(Kind::Headquarters,team,base);
  const std::array<Vec2,4> offsets{{{-300,170},{-230,290},{-100,370},{65,400}}};
  std::vector<Id> nodes;
  for(const auto offset:offsets) {
   Id id=spawn(Kind::Resource,-1,add(base,scale(offset,sign)));
   get(id)->resource=2500; nodes.push_back(id);
  }
  for(int i=0;i<5;++i) {
   Id id=spawn(Kind::Worker,team,add(base,{sign*(155+i*34),sign*55}));
   Entity* worker=get(id);worker->order=Order::Gather;worker->target=nodes[i%nodes.size()];worker->resourceTarget=worker->target;
  }
 }
 for(Vec2 cluster:std::array<Vec2,4>{{{1900,1000},{2900,3800},{1000,2800},{3800,2000}}}) {
  for(int i=0;i<3;++i) {
   Vec2 p=add(cluster,{static_cast<float>(i-1)*120,static_cast<float>((i%2)*90)});
   if(blocked(p,definition(Kind::Resource).radius)) p=add(p,{0,250});
   Id id=spawn(Kind::Resource,-1,p);get(id)->resource=4200;
  }
 }
 updateVision();
}

Entity* Simulation::get(Id id) { for(auto& e:entities_) if(e.id==id)return &e;return nullptr; }
const Entity* Simulation::find(Id id) const { for(const auto& e:entities_)if(e.id==id)return &e;return nullptr; }
Id Simulation::spawn(Kind kind,int team,Vec2 position,bool complete) {
 if(!validKind(kind)||!finite(position)||(kind!=Kind::Resource&&!validTeam(team)))return 0;
 const auto& d=definition(kind); Entity e;
 e.id=nextId_++; e.kind=kind;e.team=kind==Kind::Resource?-1:team;e.pos=bounded(position,d.radius);e.goal=e.pos;
 e.rally=bounded(add(e.pos,{team==1?-190.0f:190.0f,0})); e.progress=complete?1.0f:0.0f;e.hp=complete?d.hp:d.hp*0.1f;
 if(kind==Kind::Resource)e.resource=4000;
 const Id id=e.id;entities_.push_back(std::move(e));return id;
}
Id Simulation::debugSpawn(Kind kind,int team,Vec2 position) { Id id=spawn(kind,team,position);updateVision();return id; }
void Simulation::debugResources(int team,int ore) { if(validTeam(team))players_[team].ore=std::clamp(ore,0,100000000); }
int Simulation::supply(int team) const {
 if(!validTeam(team))return 0;int n=0;
 for(const auto& e:entities_)if(e.alive()&&e.team==team) {
  n+=definition(e.kind).supply;
  for(const auto& item:e.queue)if(!item.research)n+=definition(item.kind).supply;
 }
 return n;
}
int Simulation::capacity(int team) const {
 if(!validTeam(team))return 0;int n=0;
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
 if(!finite(point)||point.x<radius||point.y<radius||point.x>WorldSize-radius||point.y>WorldSize-radius)return true;
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
bool Simulation::visible(int team,Vec2 position) const { return validTeam(team)&&finite(position)&&position.x>=0&&position.y>=0&&position.x<=WorldSize&&position.y<=WorldSize&&fog_[team][fogIndex(position)]!=0; }
bool Simulation::explored(int team,Vec2 position) const { return validTeam(team)&&finite(position)&&position.x>=0&&position.y>=0&&position.x<=WorldSize&&position.y<=WorldSize&&explored_[team][fogIndex(position)]!=0; }
bool Simulation::canPlace(int team,Kind kind,Vec2 point,std::string* reason) const {
 auto fail=[&](const char* message){if(reason)*reason=message;return false;};
 if(!validTeam(team)||!validKind(kind)||!definition(kind).building)return fail("Choose a structure.");
 if(!finite(point))return fail("Invalid position.");
 if(!visible(team,point))return fail("Keep the construction site in current vision.");
 if(blocked(point,definition(kind).radius+15))return fail("Blocked by terrain, a structure, or an ore deposit.");
 for(const auto& e:entities_)if(e.alive()&&e.kind!=Kind::Resource&&!definition(e.kind).building&&!definition(e.kind).air&&distance(e.pos,point)<definition(kind).radius+definition(e.kind).radius+8)return fail("A ground unit occupies this site.");
 if(reason)reason->clear();return true;
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
  worker.path.clear();worker.pathIndex=0;worker.repath=0;
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
 worker.path.clear();worker.pathIndex=0;worker.repath=0;foundation.builderId=worker.id;
}

CommandResult Simulation::command(const Command& input) {
 auto fail=[](const std::string& why){return CommandResult{false,why};};
 if(winner_!=-1)return fail("The match has ended.");
 if(!validTeam(input.team)||!validKind(input.kind)||!finite(input.point)||input.point.x<0||input.point.y<0||input.point.x>WorldSize||input.point.y>WorldSize)return fail("Invalid command.");
 const int rawType=static_cast<int>(input.type);
 if(rawType<0||rawType>static_cast<int>(CommandType::ResumeConstruction)||input.units.size()>500)return fail("Invalid command.");
 Command cmd=input;std::sort(cmd.units.begin(),cmd.units.end());cmd.units.erase(std::unique(cmd.units.begin(),cmd.units.end()),cmd.units.end());
 std::vector<Id> ids;
 for(Id id:cmd.units) { const Entity* e=find(id);if(!e||!e->alive()||e->team!=cmd.team||e->kind==Kind::Resource)return fail("The selection contains unavailable or foreign units.");ids.push_back(id); }
 if(ids.empty())return fail("Select your units or a structure first.");
 Player& player=players_[cmd.team];std::string feedback="Order acknowledged.";
 if(cmd.type==CommandType::Build) {
  if(!definition(cmd.kind).building)return fail("Only structures can be deployed.");
  std::vector<Id> nearbyWorkers;for(Id id:ids){const auto* e=find(id);if(e->kind==Kind::Worker&&distance(e->pos,cmd.point)<=700)nearbyWorkers.push_back(id);}
  const Id workerId=selectConstructionWorker(nearbyWorkers,cmd.point);
  if(!workerId)return fail("Select a Drudge closer to the site.");
  if(!hasBuilding(cmd.team,Kind::Headquarters))return fail("An operational Anchor is required.");
  if(player.tier<definition(cmd.kind).tier)return fail("Research the required technology tier first.");
  if((cmd.kind==Kind::MotorPool||cmd.kind==Kind::Laboratory||cmd.kind==Kind::Turret)&&!hasBuilding(cmd.team,Kind::Foundry))return fail("Build an operational Kiln first.");
  std::string reason;if(!canPlace(cmd.team,cmd.kind,cmd.point,&reason))return fail(reason);
  if(player.ore<definition(cmd.kind).cost)return fail("Insufficient ore.");
  player.ore-=definition(cmd.kind).cost;const Id foundationId=spawn(cmd.kind,cmd.team,cmd.point,false);
  assignConstruction(*get(foundationId),*get(workerId));
  feedback=std::string(definition(cmd.kind).name)+" foundation placed; Drudge assigned.";
 } else if(cmd.type==CommandType::ResumeConstruction) {
  Entity* foundation=get(cmd.target);
  if(!foundation||!foundation->alive()||foundation->team!=cmd.team||!definition(foundation->kind).building||foundation->progress>=1)return fail("Choose your unfinished structure.");
  const Id workerId=selectConstructionWorker(ids,foundation->pos);
  if(!workerId)return fail("Select a Drudge to resume construction.");
  assignConstruction(*foundation,*get(workerId));feedback="Drudge assigned to resume construction.";
 } else if(cmd.type==CommandType::Train) {
  const auto& d=definition(cmd.kind);if(d.building||cmd.kind==Kind::Resource)return fail("Choose a unit to train.");
  Entity* producer=nullptr;for(Id id:ids){auto* e=get(id);if(e->kind==d.producer&&e->progress>=1&&e->queue.size()<MaxQueue){producer=e;break;}}
  if(!producer)return fail("Select an operational producer with queue space.");
  if(player.tier<d.tier)return fail("Research the required technology tier first.");
  if(player.ore<d.cost)return fail("Insufficient ore.");
  if(supply(cmd.team)+d.supply>capacity(cmd.team))return fail("Supply full. Build a Siphon or Anchor.");
  producer->queue.push_back({cmd.kind,d.buildTime,d.buildTime,d.cost,false});player.ore-=d.cost;
  feedback=std::string(d.name)+" queued.";
 } else if(cmd.type==CommandType::Research) {
  if(cmd.queueIndex<0||cmd.queueIndex>2)return fail("Unknown research.");
  Entity* lab=nullptr;for(Id id:ids){auto* e=get(id);if(e->kind==Kind::Laboratory&&e->progress>=1&&e->queue.size()<MaxQueue){lab=e;break;}}
  if(!lab)return fail("Select an operational Resonator with queue space.");
  const Kind itemKind=researchKind(cmd.queueIndex);
  for(const auto& e:entities_)if(e.alive()&&e.team==cmd.team)for(const auto& q:e.queue)if(q.research&&q.kind==itemKind)return fail("This research is already queued.");
  int level=cmd.queueIndex==0?player.tier:cmd.queueIndex==1?player.weapons:player.armor;
  if(level>=3)return fail("Research is already at its maximum level.");
  if(cmd.queueIndex>0&&level>=player.tier)return fail("Advance your technology tier first.");
  int cost=cmd.queueIndex==0?500*level:200*(level+1);float duration=cmd.queueIndex==0?100.0f*level:60.0f;
  if(player.ore<cost)return fail("Insufficient ore.");
  lab->queue.push_back({itemKind,duration,duration,cost,true});player.ore-=cost;feedback="Research queued.";
 } else if(cmd.type==CommandType::CancelQueue) {
  Entity* e=get(ids.front());if(cmd.queueIndex<0||cmd.queueIndex>=static_cast<int>(e->queue.size()))return fail("No queue item at that position.");
  const QueueItem item=e->queue[cmd.queueIndex];float fraction=item.total>0?std::clamp(item.remaining/item.total,0.0f,1.0f):1;
  player.ore+=static_cast<int>(std::floor(item.cost*fraction+0.001f));e->queue.erase(e->queue.begin()+cmd.queueIndex);feedback="Queue item cancelled; unused ore refunded.";
 } else if(cmd.type==CommandType::CancelBuilding) {
  bool cancelled=false;for(Id id:ids){auto* e=get(id);if(definition(e->kind).building&&e->progress<1){player.ore+=static_cast<int>(std::floor(definition(e->kind).cost*(1-e->progress)));releaseConstruction(*e);e->hp=0;e->queue.clear();cancelled=true;}}
  if(!cancelled)return fail("Select an unfinished structure.");feedback="Construction cancelled; unused ore refunded.";
 } else if(cmd.type==CommandType::Rally) {
  bool accepted=false;for(Id id:ids){auto* e=get(id);if(definition(e->kind).building){e->rally=bounded(cmd.point);accepted=true;}}
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
   Entity* e=get(movable[n]);if(e->kind==Kind::Worker)abandonConstruction(*e);e->path.clear();e->pathIndex=0;e->repath=0;e->target=0;
   switch(cmd.type) {
    case CommandType::Stop:e->order=Order::Idle;e->goal=e->pos;break;
    case CommandType::Hold:e->order=Order::Hold;e->goal=e->pos;break;
    case CommandType::Gather:e->order=Order::Gather;e->target=cmd.target;e->resourceTarget=cmd.target;e->returning=e->carried>=CarryCapacity;break;
    case CommandType::Attack:e->order=Order::Attack;e->target=e->kind==Kind::Mender?attackLeader:cmd.target;e->goal=find(e->target)->pos;break;
    case CommandType::Move:case CommandType::AttackMove: {
     if(cmd.type==CommandType::AttackMove&&e->kind==Kind::Mender&&attackLeader) {
      e->order=Order::Attack;e->target=attackLeader;e->goal=find(attackLeader)->pos;break;
     }
     const float spacing=64;Vec2 offset{(static_cast<int>(n)%columns-(columns-1)*0.5f)*spacing,(static_cast<int>(n)/columns-(columns-1)*0.5f)*spacing};
     e->order=cmd.type==CommandType::Move?Order::Move:Order::AttackMove;
     const auto& unit=definition(e->kind);const Vec2 desired=bounded(add(cmd.point,offset),unit.radius);Vec2 destination=desired;
     auto available=[&](Vec2 point) {
      if(point.x<unit.radius||point.y<unit.radius||point.x>WorldSize-unit.radius||point.y>WorldSize-unit.radius)return false;
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
 if(!std::isfinite(seconds)||seconds<=0||winner_!=-1)return;
 accumulator_=std::min(accumulator_+std::min(seconds,1.0f),1.0f);
 while(accumulator_+0.000001f>=Step&&winner_==-1) {
  accumulator_=std::max(0.0f,accumulator_-Step);step();
 }
 if(accumulator_<0.000001f||winner_!=-1)accumulator_=0;
}
void Simulation::step() {
 const auto started=std::chrono::steady_clock::now();++tick_;
 for(auto& e:entities_)if(e.alive()){e.cooldown=std::max(0.0f,e.cooldown-Step);e.repath=std::max(0.0f,e.repath-Step);}
 for(auto& fx:effects_)fx.life-=Step;
 effects_.erase(std::remove_if(effects_.begin(),effects_.end(),[](const Effect& fx){return fx.life<=0;}),effects_.end());
 // Production can append to entities_; retain IDs and reacquire each object after it does.
 std::vector<Id> production;
 for(const auto& e:entities_)if(e.alive()&&definition(e.kind).building)production.push_back(e.id);
 for(Id id:production){Entity* e=get(id);if(e&&e->alive())updateProduction(*e);}
 for(auto& e:entities_)if(e.alive()&&e.progress>=1){if(e.kind==Kind::Worker&&e.order==Order::Gather)updateEconomy(e);else updateMovement(e);}
 // Small deterministic symmetric separation prevents stacks without making units static obstacles.
 for(std::size_t a=0;a<entities_.size();++a) {
  Entity& left=entities_[a];const auto& ld=definition(left.kind);
  if(!left.alive()||ld.building||left.kind==Kind::Resource)continue;
  for(std::size_t b=a+1;b<entities_.size();++b) {
   Entity& right=entities_[b];const auto& rd=definition(right.kind);
   if(!right.alive()||rd.building||right.kind==Kind::Resource||ld.air!=rd.air)continue;
   float desired=(ld.radius+rd.radius)*1.04f;Vec2 delta=subtract(left.pos,right.pos);float dist2=lengthSq(delta);if(dist2>=desired*desired)continue;
   float dist=std::sqrt(dist2);Vec2 direction=dist>0.01f?scale(delta,1/dist):normalized(Vec2{(left.id%2)?1.0f:-1.0f,(right.id%3)?0.7f:-0.7f});
   float displacement=std::min(8.0f,(desired-dist)*0.5f);
   const bool leftAnchored=left.order==Order::Hold,rightAnchored=right.order==Order::Hold;
   const float leftShift=leftAnchored&&!rightAnchored?0:displacement*(rightAnchored&&!leftAnchored?2.0f:1.0f);
   const float rightShift=rightAnchored&&!leftAnchored?0:displacement*(leftAnchored&&!rightAnchored?2.0f:1.0f);
   Vec2 lp=bounded(add(left.pos,scale(direction,leftShift)),ld.radius),rp=bounded(subtract(right.pos,scale(direction,rightShift)),rd.radius);
   if(ld.air||!blocked(lp,ld.radius,left.id))left.pos=lp;
   if(rd.air||!blocked(rp,rd.radius,right.id))right.pos=rp;
  }
 }
 updateVision();
 for(auto& e:entities_)if(e.alive()&&e.progress>=1)updateCombat(e);
 if(config_.ai) {aiTimer_-=Step;if(aiTimer_<=0){aiTimer_=2;updateAI();}}
 for(int team=0;team<2;++team) {
  bool hq=false;for(const auto& e:entities_)if(e.alive()&&e.team==team&&e.kind==Kind::Headquarters){hq=true;break;}
  if(!hq){winner_=1-team;alert_=winner_==0?"Victory — opposing Anchor destroyed.":"Defeat — your Anchor was destroyed.";break;}
 }
 if(tick_%100==0)entities_.erase(std::remove_if(entities_.begin(),entities_.end(),[](const Entity& e){return !e.alive();}),entities_.end());
 lastStepMs_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
}

void Simulation::updateProduction(Entity& producer) {
 const auto& d=definition(producer.kind);
 if(producer.progress<1) {
  if(!constructionActive(producer.id))return;
  const float old=producer.progress;
  const float previousMaximum=d.hp*(0.1f+old*0.9f);
  const float constructionDamage=std::max(0.0f,previousMaximum-producer.hp);
  producer.progress=std::min(1.0f,old+Step/std::max(1.0f,d.buildTime));
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
  if(candidate.x<unit.radius||candidate.y<unit.radius||candidate.x>WorldSize-unit.radius||candidate.y>WorldSize-unit.radius)continue;
  if(unit.air||!blocked(candidate,unit.radius)){exit=candidate;found=true;break;}
 }
 if(!found)return; // Keep the paid item ready until an exit becomes available.
 producer.queue.erase(producer.queue.begin());
 Id id=spawn(item.kind,team,exit); // Never access producer or q after this append.
 Entity* created=get(id);if(!created)return;
 ++players_[team].stats.produced;
 // Give repeated production a real rally formation so settled units can retain
 // their destination without all competing for the producer's exact rally point.
 Vec2 arrival=rally;
 auto rallyAvailable=[&](Vec2 point) {
  if(point.x<unit.radius||point.y<unit.radius||point.x>WorldSize-unit.radius||point.y>WorldSize-unit.radius)return false;
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
  Id node=0;float closest=std::numeric_limits<float>::max();
  for(const auto& candidate:entities_)if(candidate.alive()&&candidate.kind==Kind::Resource&&candidate.resource>0&&explored(team,candidate.pos)){float dist=distanceSq(rally,candidate.pos);if(dist<closest){closest=dist;node=candidate.id;}}
  const Entity* resource=find(node);
  if(resource&&distance(resource->pos,rally)<600){created->order=Order::Gather;created->target=node;created->resourceTarget=node;}
  else {created->order=Order::Move;created->goal=arrival;}
 } else {created->order=Order::Move;created->goal=arrival;}
 if(team==0)alert_=std::string(unit.name)+" ready.";
 (void)producerId;
}

void Simulation::updateEconomy(Entity& worker) {
 if(worker.kind!=Kind::Worker||worker.order!=Order::Gather)return;
 if(worker.carried>=CarryCapacity-0.001f)worker.returning=true;
 if(worker.returning) {
  Id drop=0;float best=std::numeric_limits<float>::max();
  for(const auto& e:entities_)if(e.alive()&&e.team==worker.team&&e.progress>=1&&(e.kind==Kind::Headquarters||e.kind==Kind::Processor)) {
   float d=distanceSq(worker.pos,e.pos);if(d<best){drop=e.id;best=d;}
  }
  const Entity* depot=find(drop);if(!depot){worker.order=Order::Idle;return;}
  const float reach=definition(depot->kind).radius+definition(worker.kind).radius+15;
  if(distance(worker.pos,depot->pos)<=reach+7) {
   int ore=static_cast<int>(std::floor(worker.carried+0.001f));players_[worker.team].ore+=ore;players_[worker.team].stats.gathered+=ore;
   worker.carried=0;worker.returning=false;worker.path.clear();worker.pathIndex=0;worker.repath=0;
  } else {
   Vec2 destination=add(depot->pos,scale(normalized(subtract(worker.pos,depot->pos)),reach));moveToward(worker,destination);
  }
  return;
 }
 const Entity* deposit=find(worker.resourceTarget);
 if(!deposit||!deposit->alive()||deposit->kind!=Kind::Resource||deposit->resource<=0) {
  // Retarget only discovered ore, avoiding hidden-map economic information.
  Id replacement=0;float best=std::numeric_limits<float>::max();
  for(const auto& e:entities_)if(e.alive()&&e.kind==Kind::Resource&&e.resource>0&&explored(worker.team,e.pos)) {
   float d=distanceSq(worker.pos,e.pos);if(d<best){best=d;replacement=e.id;}
  }
  worker.resourceTarget=replacement;worker.target=replacement;deposit=find(replacement);worker.path.clear();worker.pathIndex=0;worker.repath=0;
  if(!deposit){if(worker.carried>0)worker.returning=true;else worker.order=Order::Idle;return;}
 }
 const float reach=definition(Kind::Resource).radius+definition(worker.kind).radius+9;
 if(distance(worker.pos,deposit->pos)<=reach+8) {
  worker.harvestTimer+=Step;if(worker.harvestTimer+0.00001f<HarvestPeriod)return;
  worker.harvestTimer-=HarvestPeriod;Entity* mutableDeposit=get(deposit->id);
  const float amount=std::min({3.0f,CarryCapacity-worker.carried,mutableDeposit->resource});worker.carried+=amount;mutableDeposit->resource-=amount;
  if(worker.carried>=CarryCapacity-0.001f||mutableDeposit->resource<=0){worker.returning=true;worker.path.clear();worker.pathIndex=0;worker.repath=0;}
 } else {
  const Vec2 destination=add(deposit->pos,scale(normalized(subtract(worker.pos,deposit->pos)),reach));moveToward(worker,destination);
 }
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
  const float reach=definition(foundation->kind).radius+d.radius+ConstructionPadding;
  Vec2 destination=add(foundation->pos,scale(normalized(subtract(e.pos,foundation->pos)),reach));
  if(blocked(destination,d.radius+2,e.id)||!clearFireLine(destination,foundation->pos,obstacles_)) {
   float best=std::numeric_limits<float>::max();bool found=false;
   for(int spoke=0;spoke<32;++spoke) {
    float angle=spoke*(2*Pi/32);Vec2 candidate=add(foundation->pos,{std::cos(angle)*reach,std::sin(angle)*reach});
    if(blocked(candidate,d.radius+2,e.id)||!clearFireLine(candidate,foundation->pos,obstacles_))continue;
    float score=distanceSq(e.pos,candidate);if(score<best){best=score;destination=candidate;found=true;}
   }
   if(!found){e.path.clear();e.pathIndex=0;return;}
  }
  moveToward(e,destination);return;
 }
 if(e.order==Order::Hold) {if(distanceSq(e.pos,e.goal)>5*5)moveToward(e,e.goal);return;}
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
void Simulation::moveToward(Entity& e,Vec2 destination) {
 const auto& d=definition(e.kind);if(d.speed<=0||!finite(destination))return;
 destination=bounded(destination,d.radius);Vec2 delta=subtract(destination,e.pos);if(lengthSq(delta)<4)return;
 if(d.air) {
  const Vec2 dir=normalized(delta);e.pos=bounded(add(e.pos,scale(dir,std::min(d.speed*Step,std::sqrt(lengthSq(delta))))),d.radius);e.facing=std::atan2(dir.y,dir.x);return;
 }
 // Reuse paths while gathering and chasing; only refresh when the destination shifts materially.
 bool different=!e.path.empty()&&distanceSq(e.path.back(),destination)>110*110;
 if(e.path.empty()||e.pathIndex>=static_cast<int>(e.path.size())||different) {
  if(e.repath<=0)planPath(e,destination);
  if(e.path.empty())return;
 }
 while(e.pathIndex<static_cast<int>(e.path.size())) {
  const float arrival=e.pathIndex+1==static_cast<int>(e.path.size())?2.0f:18.0f;
  if(distanceSq(e.pos,e.path[e.pathIndex])>=arrival*arrival)break;
  ++e.pathIndex;
 }
 if(e.pathIndex>=static_cast<int>(e.path.size())){e.path.clear();e.pathIndex=0;e.repath=0;return;}
 Vec2 waypoint=e.path[e.pathIndex];Vec2 dir=normalized(subtract(waypoint,e.pos));float stepDistance=std::min(d.speed*Step,distance(e.pos,waypoint));
 if(e.order==Order::Construct) {
  // Static paths do not include units. Give builders a lateral route through
  // traffic so a collinear row of idle or held workers cannot pin them in place.
  const Vec2 perpendicular{-dir.y,dir.x};const Entity* obstacle=nullptr;float nearest=80.0f;float side=0;
  for(const auto& other:entities_) {
   const auto& otherDefinition=definition(other.kind);
   if(other.id==e.id||!other.alive()||otherDefinition.building||otherDefinition.air||other.kind==Kind::Resource)continue;
   const Vec2 offset=subtract(other.pos,e.pos);const float forward=offset.x*dir.x+offset.y*dir.y;
   const float lateral=offset.x*perpendicular.x+offset.y*perpendicular.y;
   if(forward<=0||forward>=nearest||std::fabs(lateral)>=d.radius+otherDefinition.radius+12)continue;
   obstacle=&other;nearest=forward;side=lateral;
  }
  if(obstacle) {
   const float sign=std::fabs(side)>0.1f?(side>0?-1.0f:1.0f):(e.id%2?1.0f:-1.0f);
   const Vec2 around=normalized(add(dir,scale(perpendicular,sign*1.5f)));
   const Vec2 alternate=normalized(add(dir,scale(perpendicular,-sign*1.5f)));
   if(!blocked(add(e.pos,scale(around,stepDistance)),d.radius,e.id))dir=around;
   else if(!blocked(add(e.pos,scale(alternate,stepDistance)),d.radius,e.id))dir=alternate;
  }
 }
 Vec2 candidate=add(e.pos,scale(dir,stepDistance));
 if(!blocked(candidate,d.radius,e.id))e.pos=candidate;
 else {
  Vec2 xOnly{candidate.x,e.pos.y},yOnly{e.pos.x,candidate.y};
  if(!blocked(xOnly,d.radius,e.id)&&std::fabs(dir.x)>0.01f)e.pos=xOnly;
  else if(!blocked(yOnly,d.radius,e.id)&&std::fabs(dir.y)>0.01f)e.pos=yOnly;
  else {e.path.clear();e.pathIndex=0;e.repath=0.25f;}
 }
 e.facing=std::atan2(dir.y,dir.x);
}
void Simulation::planPath(Entity& e,Vec2 destination) {
 e.path.clear();e.pathIndex=0;e.repath=0.35f;
 const float radius=definition(e.kind).radius;
 auto clearLine=[&](Vec2 a,Vec2 b) {
  const float len=distance(a,b);const int samples=std::max(1,static_cast<int>(std::ceil(len/24)));
  for(int i=1;i<=samples;++i)if(blocked(add(a,scale(subtract(b,a),static_cast<float>(i)/samples)),radius,e.id))return false;
  return true;
 };
 if(clearLine(e.pos,destination)){e.path.push_back(destination);return;}
 constexpr int N=FogSize,Count=N*N;
 auto center=[](int cell){return Vec2{(cell%N+0.5f)*Cell,(cell/N+0.5f)*Cell};};
 std::array<signed char,Count> walk;walk.fill(-1);
 auto walkable=[&](int cell) {
  if(cell<0||cell>=Count)return false;
  if(walk[cell]<0)walk[cell]=blocked(center(cell),radius,e.id)?0:1;
  return walk[cell]!=0;
 };
 int start=fogIndex(e.pos),goal=fogIndex(destination);walk[start]=1;
 if(!walkable(goal)) {
  int best=-1;float bestDistance=std::numeric_limits<float>::max();const int gx=goal%N,gy=goal/N;
  for(int ring=1;ring<=7&&best<0;++ring)for(int dy=-ring;dy<=ring;++dy)for(int dx=-ring;dx<=ring;++dx) {
   if(std::max(std::abs(dx),std::abs(dy))!=ring||gx+dx<0||gx+dx>=N||gy+dy<0||gy+dy>=N)continue;
   int candidate=(gy+dy)*N+gx+dx;float dist=distanceSq(center(candidate),destination);
   if(walkable(candidate)&&dist<bestDistance){best=candidate;bestDistance=dist;}
  }
  if(best<0)return;goal=best;destination=center(goal);
 }
 std::array<float,Count> costs;costs.fill(std::numeric_limits<float>::max());std::array<int,Count> parent;parent.fill(-1);std::array<bool,Count> closed{};
 struct Node {float score;int cell;};struct Greater {bool operator()(const Node& a,const Node& b)const{return a.score==b.score?a.cell>b.cell:a.score>b.score;}};
 auto heuristic=[&](int cell){float dx=static_cast<float>(std::abs(cell%N-goal%N)),dy=static_cast<float>(std::abs(cell/N-goal/N));return std::max(dx,dy)+0.41421356237f*std::min(dx,dy);};
 std::priority_queue<Node,std::vector<Node>,Greater> open;costs[start]=0;open.push({heuristic(start),start});
 bool reached=false;
 while(!open.empty()) {
  int current=open.top().cell;open.pop();if(closed[current])continue;closed[current]=true;if(current==goal){reached=true;break;}
  int x=current%N,y=current/N;
  for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
   if((dx==0&&dy==0)||x+dx<0||x+dx>=N||y+dy<0||y+dy>=N)continue;
   int next=(y+dy)*N+x+dx;if(closed[next]||!walkable(next))continue;
   if(dx&&dy&&(!walkable(y*N+x+dx)||!walkable((y+dy)*N+x)))continue;
   float cost=costs[current]+(dx&&dy?1.41421356237f:1);
   if(cost<costs[next]){costs[next]=cost;parent[next]=current;open.push({cost+heuristic(next),next});}
  }
 }
 if(!reached)return;
 std::vector<Vec2> reversed;for(int at=goal;at!=start&&at>=0;at=parent[at])reversed.push_back(center(at));
 std::reverse(reversed.begin(),reversed.end());
 Vec2 from=e.pos;
 for(std::size_t index=0;index<reversed.size();) {
  std::size_t far=index;
  for(std::size_t candidate=index+1;candidate<reversed.size();++candidate){if(clearLine(from,reversed[candidate]))far=candidate;else break;}
  e.path.push_back(reversed[far]);from=reversed[far];index=far+1;
 }
 if(clearLine(from,destination))e.path.push_back(destination);
 if(e.path.empty()&&clearLine(e.pos,destination))e.path.push_back(destination);
}

void Simulation::damage(Entity& victim,float amount,int attackerTeam) {
 if(!victim.alive()||victim.kind==Kind::Resource||amount<=0)return;
 const float dealt=std::min(victim.hp,amount);victim.hp-=dealt;
 if(validTeam(attackerTeam))players_[attackerTeam].stats.damage+=dealt;
 if(victim.hp<=0) {
  // Record a witnessed death at the event, before periodic corpse cleanup can
  // remove it between the opponent's strategic updates. Hidden deaths stay unknown.
  if(victim.team==0&&visible(1,victim.pos))
   aiSightings_.erase(std::remove_if(aiSightings_.begin(),aiSightings_.end(),[&](const AISighting& sighting){return sighting.id==victim.id;}),aiSightings_.end());
  if(victim.kind==Kind::Worker)abandonConstruction(victim);
  else if(definition(victim.kind).building)releaseConstruction(victim);
  victim.hp=0;victim.queue.clear();victim.path.clear();victim.pathIndex=0;
  if(validTeam(victim.team)) {
   if(definition(victim.kind).building){if(validTeam(attackerTeam))++players_[attackerTeam].stats.buildingsDestroyed;}
   else {++players_[victim.team].stats.lost;if(validTeam(attackerTeam))++players_[attackerTeam].stats.killed;}
  }
  effects_.push_back({victim.pos,victim.pos,victim.team,0.7f,true});
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
  if(patient){patient->hp=std::min(definition(patient->kind).hp,patient->hp+16);e.cooldown=d.cooldown;effects_.push_back({e.pos,patient->pos,e.team,0.24f,false});}
  return;
 }
 if(d.damage<=0||e.order==Order::Move||e.order==Order::Gather||e.order==Order::Construct)return;
 auto validTarget=[&](const Entity* target) {return target&&target->alive()&&target->team>=0&&target->team!=e.team&&target->kind!=Kind::Resource&&(!definition(target->kind).air||d.antiAir)&&visible(e.team,target->pos);};
 Entity* target=get(e.target);
 if(!validTarget(target))target=nullptr;
 if(target&&e.order!=Order::Attack) {
  const float acquisition=(e.order==Order::Hold||d.building)?d.range:d.vision;
  if(distance(e.pos,target->pos)>acquisition+definition(target->kind).radius)target=nullptr;
 }
 if(!target) {
  float best=-std::numeric_limits<float>::max();
  for(auto& enemy:entities_) {
   if(!validTarget(&enemy))continue;
   float dist=distance(e.pos,enemy.pos),range=(e.order==Order::Hold||d.building)?d.range+definition(enemy.kind).radius:d.vision;
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
 damage(*target,std::max(1.0f,weapon-effectiveArmor),e.team);
 e.cooldown=d.cooldown;e.facing=std::atan2(impact.y-e.pos.y,impact.x-e.pos.x);
 effects_.push_back({e.pos,impact,e.team,e.kind==Kind::Mortar?0.5f:0.18f,e.kind==Kind::Mortar});
 if(e.kind==Kind::Mortar)for(auto& enemy:entities_)if(enemy.id!=primary&&enemy.alive()&&enemy.team>=0&&enemy.team!=e.team&&!definition(enemy.kind).air&&distanceSq(enemy.pos,impact)<100*100)damage(enemy,std::max(1.0f,weapon*0.42f-definition(enemy.kind).armor-players_[enemy.team].armor),e.team);
 // Units under direct attack can retaliate without changing deliberate move or gather orders.
 target=get(primary);if(target&&target->alive()&&target->order==Order::Idle&&!target->target&&(!d.air||td.antiAir))target->target=e.id;
}
void Simulation::updateVision() {
 for(auto& f:fog_)f.fill(0);
 for(const auto& e:entities_) {
  if(!e.alive()||!validTeam(e.team)||e.kind==Kind::Resource)continue;
  const float radius=definition(e.kind).vision*(e.progress>=1?1.0f:0.5f);
  int minX=std::clamp(static_cast<int>((e.pos.x-radius)/Cell),0,FogSize-1),maxX=std::clamp(static_cast<int>((e.pos.x+radius)/Cell),0,FogSize-1);
  int minY=std::clamp(static_cast<int>((e.pos.y-radius)/Cell),0,FogSize-1),maxY=std::clamp(static_cast<int>((e.pos.y+radius)/Cell),0,FogSize-1);
  for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x) {
   Vec2 center{(x+0.5f)*Cell,(y+0.5f)*Cell};if(distanceSq(e.pos,center)>(radius+Cell*0.5f)*(radius+Cell*0.5f))continue;
   fog_[e.team][y*FogSize+x]=1;explored_[e.team][y*FogSize+x]=1;
  }
 }
}

std::uint64_t Simulation::aiLastObserved(Vec2 point) const {
 if(!finite(point)||point.x<0||point.y<0||point.x>WorldSize||point.y>WorldSize)return 0;
 return aiObserved_[fogIndex(point)];
}

void Simulation::updateAIKnowledge() {
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
 Hasher hash;hash.integer(config_.map);hash.integer(config_.seed);hash.integer(config_.ai);hash.real(config_.aiAggression);
 hash.integer(tick_);hash.integer(nextId_);hash.real(accumulator_);hash.real(aiTimer_);hash.integer(winner_);
 for(const auto& p:players_) {
  hash.integer(p.ore);hash.integer(p.tier);hash.integer(p.weapons);hash.integer(p.armor);
  const auto& s=p.stats;hash.integer(s.gathered);hash.integer(s.produced);hash.integer(s.lost);hash.integer(s.killed);hash.integer(s.built);hash.integer(s.buildingsDestroyed);hash.integer(s.expansions);hash.integer(s.upgrades);hash.real(s.damage);
 }
 hash.integer(entities_.size());for(const auto& e:entities_) {
  hash.integer(e.id);hash.integer(static_cast<int>(e.kind));hash.integer(e.team);hash.point(e.pos);hash.point(e.goal);hash.point(e.rally);
  hash.real(e.hp);hash.real(e.cooldown);hash.real(e.progress);hash.real(e.carried);hash.real(e.harvestTimer);hash.real(e.resource);hash.real(e.facing);
  hash.integer(static_cast<int>(e.order));hash.integer(e.target);hash.integer(e.resourceTarget);hash.integer(e.returning);hash.integer(e.pathIndex);hash.real(e.repath);hash.integer(e.builderId);hash.integer(e.resumeGather);
  hash.integer(e.queue.size());for(const auto& q:e.queue){hash.integer(static_cast<int>(q.kind));hash.real(q.remaining);hash.real(q.total);hash.integer(q.cost);hash.integer(q.research);}
  hash.integer(e.path.size());for(Vec2 p:e.path)hash.point(p);
 }
 hash.integer(obstacles_.size());for(const auto& o:obstacles_){hash.point(o.center);hash.point(o.half);}
 hash.integer(effects_.size());for(const auto& fx:effects_){hash.point(fx.from);hash.point(fx.to);hash.integer(fx.team);hash.real(fx.life);hash.integer(fx.explosion);}
 for(int t=0;t<2;++t)for(int i=0;i<FogSize*FogSize;++i){hash.byte(fog_[t][i]);hash.byte(explored_[t][i]);}
 hash.integer(aiSightings_.size());for(const auto& sighting:aiSightings_){hash.integer(sighting.id);hash.integer(static_cast<int>(sighting.kind));hash.point(sighting.pos);hash.integer(sighting.lastSeenTick);}
 for(auto stamp:aiObserved_)hash.integer(stamp);
 return hash.value;
}

bool Simulation::save(const std::string& path) const {
 std::ofstream out(path,std::ios::trunc);if(!out)return false;out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<float>::max_digits10);
 out<<"CINDERLINE 3\n"<<config_.map<<' '<<config_.seed<<' '<<config_.ai<<' '<<config_.aiAggression<<'\n';
 out<<tick_<<' '<<nextId_<<' '<<accumulator_<<' '<<aiTimer_<<' '<<winner_<<'\n';
 for(const auto& p:players_) {
  const auto& s=p.stats;out<<p.ore<<' '<<p.tier<<' '<<p.weapons<<' '<<p.armor<<' '<<s.gathered<<' '<<s.produced<<' '<<s.lost<<' '<<s.killed<<' '<<s.built<<' '<<s.buildingsDestroyed<<' '<<s.expansions<<' '<<s.upgrades<<' '<<s.damage<<'\n';
 }
 out<<obstacles_.size()<<'\n';for(const auto& o:obstacles_)out<<o.center.x<<' '<<o.center.y<<' '<<o.half.x<<' '<<o.half.y<<'\n';
 out<<entities_.size()<<'\n';for(const auto& e:entities_) {
  out<<e.id<<' '<<static_cast<int>(e.kind)<<' '<<e.team<<' '<<e.pos.x<<' '<<e.pos.y<<' '<<e.goal.x<<' '<<e.goal.y<<' '<<e.rally.x<<' '<<e.rally.y<<' '<<e.hp<<' '<<e.cooldown<<' '<<e.progress<<' '<<e.carried<<' '<<e.harvestTimer<<' '<<e.resource<<' '<<e.facing<<' '<<static_cast<int>(e.order)<<' '<<e.target<<' '<<e.resourceTarget<<' '<<e.returning<<' '<<e.pathIndex<<' '<<e.repath<<' '<<e.builderId<<' '<<e.resumeGather<<'\n';
  out<<e.queue.size()<<'\n';for(const auto& q:e.queue)out<<static_cast<int>(q.kind)<<' '<<q.remaining<<' '<<q.total<<' '<<q.cost<<' '<<q.research<<'\n';
  out<<e.path.size()<<'\n';for(Vec2 p:e.path)out<<p.x<<' '<<p.y<<'\n';
 }
 out<<effects_.size()<<'\n';for(const auto& fx:effects_)out<<fx.from.x<<' '<<fx.from.y<<' '<<fx.to.x<<' '<<fx.to.y<<' '<<fx.team<<' '<<fx.life<<' '<<fx.explosion<<'\n';
 for(int t=0;t<2;++t){for(auto v:fog_[t])out<<static_cast<int>(v)<<' ';out<<'\n';for(auto v:explored_[t])out<<static_cast<int>(v)<<' ';out<<'\n';}
 out<<recording_.size()<<'\n';for(const auto& r:recording_) {
  const auto& c=r.command;out<<r.tick<<' '<<static_cast<int>(c.type)<<' '<<c.team<<' '<<c.point.x<<' '<<c.point.y<<' '<<c.target<<' '<<static_cast<int>(c.kind)<<' '<<c.queueIndex<<' '<<c.units.size();for(Id id:c.units)out<<' '<<id;out<<'\n';
 }
 out<<std::quoted(alert_)<<'\n'<<std::quoted(aiStatus_)<<'\n';
 out<<"AI_KNOWLEDGE 1\n"<<aiSightings_.size()<<'\n';
 for(const auto& sighting:aiSightings_)out<<sighting.id<<' '<<static_cast<int>(sighting.kind)<<' '<<sighting.pos.x<<' '<<sighting.pos.y<<' '<<sighting.lastSeenTick<<'\n';
 out<<aiObserved_.size()<<'\n';for(auto stamp:aiObserved_)out<<stamp<<' ';out<<'\n';
 out.flush();return out.good();
}
bool Simulation::load(const std::string& path) {
 std::ifstream in(path);if(!in)return false;in.imbue(std::locale::classic());std::string magic;int version=0;in>>magic>>version;if(magic!="CINDERLINE"||(version<1||version>3))return false;
 Simulation loaded;loaded.entities_.clear();loaded.obstacles_.clear();loaded.effects_.clear();loaded.recording_.clear();
 in>>loaded.config_.map>>loaded.config_.seed>>loaded.config_.ai>>loaded.config_.aiAggression;
 in>>loaded.tick_>>loaded.nextId_>>loaded.accumulator_>>loaded.aiTimer_>>loaded.winner_;
 if(!in||loaded.config_.map<0||loaded.config_.map>2||!std::isfinite(loaded.config_.aiAggression)||loaded.config_.aiAggression<0.5f||loaded.config_.aiAggression>2||!std::isfinite(loaded.accumulator_)||loaded.accumulator_<0||loaded.accumulator_>1||!std::isfinite(loaded.aiTimer_)||loaded.winner_<-1||loaded.winner_>1)return false;
 for(auto& p:loaded.players_) {
  auto& s=p.stats;in>>p.ore>>p.tier>>p.weapons>>p.armor>>s.gathered>>s.produced>>s.lost>>s.killed>>s.built>>s.buildingsDestroyed>>s.expansions>>s.upgrades>>s.damage;
  if(!in||p.ore<0||p.tier<1||p.tier>3||p.weapons<0||p.weapons>3||p.armor<0||p.armor>3||!std::isfinite(s.damage))return false;
 }
 std::size_t count=0;in>>count;if(!in||count>1024)return false;
 for(std::size_t i=0;i<count;++i){Obstacle o;in>>o.center.x>>o.center.y>>o.half.x>>o.half.y;if(!in||!finite(o.center)||!finite(o.half)||o.half.x<0||o.half.y<0)return false;loaded.obstacles_.push_back(o);}
 in>>count;if(!in||count>10000)return false;Id maxId=0;
 for(std::size_t i=0;i<count;++i) {
  Entity e;int kind,order;in>>e.id>>kind>>e.team>>e.pos.x>>e.pos.y>>e.goal.x>>e.goal.y>>e.rally.x>>e.rally.y>>e.hp>>e.cooldown>>e.progress>>e.carried>>e.harvestTimer>>e.resource>>e.facing>>order>>e.target>>e.resourceTarget>>e.returning>>e.pathIndex>>e.repath;
  if(version>=2)in>>e.builderId>>e.resumeGather;
  e.kind=static_cast<Kind>(kind);e.order=static_cast<Order>(order);
  if(!in||!e.id||!validKind(e.kind)||(!validTeam(e.team)&&!(e.kind==Kind::Resource&&e.team==-1))||!finite(e.pos)||!finite(e.goal)||!finite(e.rally)||!std::isfinite(e.hp)||e.hp<0||!std::isfinite(e.cooldown)||e.cooldown<0||!std::isfinite(e.progress)||e.progress<0||e.progress>1||!std::isfinite(e.carried)||e.carried<0||e.carried>CarryCapacity||!std::isfinite(e.harvestTimer)||!std::isfinite(e.resource)||e.resource<0||!std::isfinite(e.facing)||!std::isfinite(e.repath)||order<0||order>static_cast<int>(version==1?Order::Hold:Order::Construct)||loaded.find(e.id))return false;
  maxId=std::max(maxId,e.id);std::size_t queueCount;in>>queueCount;if(!in||queueCount>MaxQueue)return false;
  for(std::size_t q=0;q<queueCount;++q){QueueItem item;int qkind;in>>qkind>>item.remaining>>item.total>>item.cost>>item.research;item.kind=static_cast<Kind>(qkind);if(!in||!validKind(item.kind)||!std::isfinite(item.remaining)||!std::isfinite(item.total)||item.total<=0||item.remaining<0||item.remaining>item.total+0.001f||item.cost<0)return false;e.queue.push_back(item);}
  std::size_t pathCount;in>>pathCount;if(!in||pathCount>FogSize*FogSize+1)return false;
  for(std::size_t p=0;p<pathCount;++p){Vec2 point;in>>point.x>>point.y;if(!in||!finite(point))return false;e.path.push_back(point);}
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
 for(std::size_t i=0;i<count;++i){Effect fx;in>>fx.from.x>>fx.from.y>>fx.to.x>>fx.to.y>>fx.team>>fx.life>>fx.explosion;if(!in||!finite(fx.from)||!finite(fx.to)||!std::isfinite(fx.life))return false;loaded.effects_.push_back(fx);}
 for(int t=0;t<2;++t)for(auto* field:{&loaded.fog_[t],&loaded.explored_[t]})for(auto& value:*field){int v;in>>v;if(!in||v<0||v>1)return false;value=static_cast<unsigned char>(v);}
 in>>count;if(!in||count>1000000)return false;
 for(std::size_t i=0;i<count;++i) {
  RecordedCommand r;Command& c=r.command;int type,kind;std::size_t unitCount;in>>r.tick>>type>>c.team>>c.point.x>>c.point.y>>c.target>>kind>>c.queueIndex>>unitCount;c.type=static_cast<CommandType>(type);c.kind=static_cast<Kind>(kind);
  if(!in||r.tick>loaded.tick_||type<0||type>static_cast<int>(version==1?CommandType::CancelBuilding:CommandType::ResumeConstruction)||!validTeam(c.team)||!validKind(c.kind)||!finite(c.point)||unitCount>500)return false;
  for(std::size_t n=0;n<unitCount;++n){Id id;in>>id;c.units.push_back(id);}if(!in)return false;loaded.recording_.push_back(std::move(r));
 }
 in>>std::quoted(loaded.alert_)>>std::quoted(loaded.aiStatus_);if(!in||loaded.alert_.size()>4096||loaded.aiStatus_.size()>4096)return false;
 if(version>=3) {
  std::string section;int knowledgeVersion=0;in>>section>>knowledgeVersion>>count;
  if(!in||section!="AI_KNOWLEDGE"||knowledgeVersion!=1||count>10000)return false;
  Id previous=0;
  for(std::size_t index=0;index<count;++index) {
   AISighting sighting;int kind=0;in>>sighting.id>>kind>>sighting.pos.x>>sighting.pos.y>>sighting.lastSeenTick;
   sighting.kind=static_cast<Kind>(kind);
   if(!in||sighting.id<=previous||sighting.id>=loaded.nextId_||!validKind(sighting.kind)||sighting.kind==Kind::Resource||!finite(sighting.pos)||sighting.pos.x<0||sighting.pos.y<0||sighting.pos.x>WorldSize||sighting.pos.y>WorldSize||sighting.lastSeenTick==0||sighting.lastSeenTick>loaded.tick_)return false;
   previous=sighting.id;loaded.aiSightings_.push_back(sighting);
  }
  in>>count;if(!in||count!=loaded.aiObserved_.size())return false;
  for(auto& stamp:loaded.aiObserved_){in>>stamp;if(!in||stamp>loaded.tick_)return false;}
  for(const auto& sighting:loaded.aiSightings_)if(loaded.aiLastObserved(sighting.pos)<sighting.lastSeenTick)return false;
  in>>std::ws;if(!in.eof())return false;
 }
 loaded.lastStepMs_=0;*this=std::move(loaded);return true;
}
} // namespace cinder
