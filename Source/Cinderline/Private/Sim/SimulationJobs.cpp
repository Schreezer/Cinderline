#include "Sim/Simulation.h"
#include "Sim/SimulationRules.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cinder {
namespace {
constexpr float Pi=3.14159265358979323846f;
float distanceSquared(Vec2 a,Vec2 b) {const float x=a.x-b.x,y=a.y-b.y;return x*x+y*y;}
float remainingWork(const Entity& entity) {
 float seconds=0;for(const auto& item:entity.queue)seconds+=item.remaining;return seconds;
}
bool queueIdentityAvailable(const Entity& entity) {
 return entity.nextQueueId>0&&entity.nextQueueId<std::numeric_limits<Id>::max();
}
}

JobPlan Simulation::autoBuildStatus(int team,Kind kind,const Vec2* site) const {
 JobPlan plan;
 if(static_cast<int>(kind)>=0&&static_cast<int>(kind)<static_cast<int>(definitions().size()))
  plan.totalCost=definition(kind).cost;
 const auto status=checkBuild(team,kind,{},site,&plan.worker,true);
 plan.accepted=status.accepted;plan.message=status.message;return plan;
}

JobPlan Simulation::autoTrainStatus(int team,Kind kind,int quantity,Id pinned) const {
 JobPlan plan;plan.quantity=quantity;
 auto fail=[&](const std::string& message){plan.message=message;return plan;};
 if(winner_!=-1)return fail("The match has ended.");
 if(!activeTeam(team)||eliminated(team)||static_cast<int>(kind)<0||static_cast<int>(kind)>=static_cast<int>(definitions().size()))return fail("Invalid training request.");
 if(quantity<1||quantity>MaxQueue)return fail("Choose between 1 and 20 units.");
 const auto& type=definition(kind);
 if(type.building||kind==Kind::Resource)return fail("Choose a unit to train.");
 plan.totalCost=type.cost*quantity;plan.totalSupply=type.supply*quantity;
 if(players_[team].tier<type.tier)return fail("Research the required technology tier first.");
 const Entity* pin=pinned?find(pinned):nullptr;
 if(pinned&&(!pin||!pin->alive()||pin->team!=team||pin->kind!=type.producer||pin->progress<1))
  return fail("Choose an operational owned producer for this unit.");
 std::vector<const Entity*> producers;
 for(const auto& entity:entities_)
  if(entity.alive()&&entity.team==team&&entity.kind==type.producer&&entity.progress>=1&&(!pinned||entity.id==pinned))
   producers.push_back(&entity);
 std::sort(producers.begin(),producers.end(),[](const Entity* a,const Entity* b){return a->id<b->id;});
 if(producers.empty())return fail(std::string("Build an operational ")+definition(type.producer).name+" first.");
 if(players_[team].ore<plan.totalCost)return fail("Insufficient ore for the complete batch.");
 if(supply(team)+plan.totalSupply>capacity(team))return fail("Supply full. Build a Siphon or Anchor.");
 std::vector<int> allocated(producers.size(),0);
 std::vector<float> work;work.reserve(producers.size());
 for(const auto* producer:producers)work.push_back(remainingWork(*producer));
 for(int item=0;item<quantity;++item) {
  std::size_t best=producers.size();
  for(std::size_t i=0;i<producers.size();++i) {
   const auto& producer=*producers[i];
   if(producer.queue.size()+static_cast<std::size_t>(allocated[i])>=MaxQueue||!queueIdentityAvailable(producer)||
      static_cast<std::uint64_t>(producer.nextQueueId)+static_cast<unsigned>(allocated[i])>=std::numeric_limits<Id>::max())continue;
   if(best==producers.size()||work[i]<work[best]||(work[i]==work[best]&&producer.id<producers[best]->id))best=i;
  }
  if(best==producers.size())return fail("Not enough production queue space for the complete batch.");
  ++allocated[best];work[best]+=productionTime(type.buildTime);
 }
 for(std::size_t i=0;i<producers.size();++i)if(allocated[i])
  plan.assignments.push_back({producers[i]->id,allocated[i],remainingWork(*producers[i]),work[i]});
 plan.accepted=true;plan.message=std::to_string(quantity)+" "+type.name+" ready to queue.";return plan;
}

JobPlan Simulation::autoResearchStatus(int team,int index,Id pinned) const {
 JobPlan plan;
 auto fail=[&](const std::string& message){plan.message=message;return plan;};
 if(winner_!=-1)return fail("The match has ended.");
 if(!activeTeam(team)||eliminated(team)||index<0||index>2)return fail("Unknown research.");
 const auto& player=players_[team];
 const int level=index==0?player.tier:index==1?player.weapons:player.armor;
 if(level>=3)return fail("Research is already at its maximum level.");
 if(index>0&&level>=player.tier)return fail("Advance your technology tier first.");
 plan.totalCost=index==0?500*level:200*(level+1);
 const float duration=productionTime(index==0?100.0f*level:60.0f);
 const Kind itemKind=rules::researchKind(index);
 for(const auto& entity:entities_)if(entity.alive()&&entity.team==team)
  for(const auto& item:entity.queue)if(item.research&&item.kind==itemKind)return fail("This research is already queued.");
 const Entity* pin=pinned?find(pinned):nullptr;
 if(pinned&&(!pin||!pin->alive()||pin->team!=team||pin->kind!=Kind::Laboratory||pin->progress<1))
  return fail("Choose an operational owned Resonator.");
 const Entity* chosen=nullptr;float best=0;bool operational=false;
 for(const auto& entity:entities_) {
  if(!entity.alive()||entity.team!=team||entity.kind!=Kind::Laboratory||entity.progress<1||(pinned&&entity.id!=pinned))continue;
  operational=true;
  if(entity.queue.size()>=MaxQueue||!queueIdentityAvailable(entity))continue;
  const float seconds=remainingWork(entity);
  if(!chosen||seconds<best||(seconds==best&&entity.id<chosen->id)){chosen=&entity;best=seconds;}
 }
 if(!chosen)return fail(operational?"No research queue space is available.":"Build an operational Resonator first.");
 if(player.ore<plan.totalCost)return fail("Insufficient ore.");
 plan.assignments.push_back({chosen->id,1,best,best+duration});
 plan.accepted=true;plan.message="Research ready to queue.";return plan;
}

CommandResult Simulation::autoRallyStatus(int team,Kind kind,Id pinned,bool useDefault) const {
 if(winner_!=-1)return {false,"The match has ended."};
 if(!activeTeam(team)||eliminated(team)||(kind!=Kind::Resource&&!rules::productionKind(kind)))return {false,"Choose a production structure."};
 const Entity* pin=pinned?find(pinned):nullptr;
 if(useDefault) {
  if(!pinned||!pin||!pin->alive()||pin->team!=team||pin->progress<1||!rules::productionKind(pin->kind)||
     (kind!=Kind::Resource&&pin->kind!=kind))return {false,"Choose an operational owned combat producer."};
  if(pin->kind==Kind::Headquarters)return {true,"Ready to restore automatic mining."};
  if(!players_[team].armyRallySet)return {false,"Set the army rally point first."};
  return {true,"Ready to use the team army rally point."};
 }
 if(!pinned&&kind==Kind::Resource)return {true,"Ready to choose the team army rally point."};
 if(pinned&&(!pin||!pin->alive()||pin->team!=team||pin->progress<1||!rules::productionKind(pin->kind)||(kind!=Kind::Resource&&pin->kind!=kind)))
  return {false,"Choose an operational owned production structure."};
 for(const auto& entity:entities_)
  if(entity.alive()&&entity.team==team&&entity.progress>=1&&rules::productionKind(entity.kind)&&(!pinned||entity.id==pinned)&&(kind==Kind::Resource||entity.kind==kind))
   return {true,"Ready to choose a rally point."};
 return {false,"Build an operational production structure first."};
}

void Simulation::ensureNavigation() const {
 if(isStepping_&&!navigationDirty_)return;
 std::vector<NavBox> boxes; boxes.reserve(obstacles_.size());
 for(const auto& obstacle:obstacles_)boxes.push_back({obstacle.center,obstacle.half});
 std::vector<NavCircle> circles; circles.reserve(entities_.size());
 for(const auto& entity:entities_) {
  if(!entity.alive())continue;
  const auto& type=definition(entity.kind);
  if(type.building||(entity.kind==Kind::Resource&&entity.resource>0))
   circles.push_back({entity.id,entity.pos,type.radius});
 }
 navigation_.sync(worldSize(),boxes,circles);navigationDirty_=false;
}

void Simulation::resetNavigation(Entity& entity) {
 entity.path.clear();entity.pathIndex=0;entity.repath=0;
 entity.workTarget=0;entity.workPoint={};entity.workPointValid=false;
 entity.navigationAnchor=entity.pos;entity.stalledFor=0;entity.yieldFor=0;
 entity.pathGeometry=0;entity.navigationFailures=0;entity.avoidanceSide=0;
 entity.navigationExhausted=false;entity.navigationBestDistance=0;
}

std::vector<Vec2> Simulation::workPoints(const Entity& worker,Vec2 center,float reach,
 Id target,const Navigation& navigation,bool reserve) const {
 const float radius=definition(worker.kind).radius;
 std::vector<Vec2> points;points.reserve(33);
 auto append=[&](Vec2 point) {
  if(!navigation.pointClear(point,radius+0.25f,worker.id)||
     !navigation.segmentClear(point,center,radius,target))return;
  if(reserve)for(const auto& other:entities_) {
   if(other.id==worker.id||!other.alive()||!other.workPointValid||other.workTarget!=target)continue;
   if(other.order!=Order::Gather&&other.order!=Order::Construct)continue;
   const float separation=radius+definition(other.kind).radius+4;
   if(distanceSquared(point,other.workPoint)<separation*separation)return;
  }
  points.push_back(point);
 };
 // The radial point keeps an unobstructed approach short; the fixed perimeter
 // supplies reachable alternatives without making the goal move every frame.
 const float angle=std::atan2(worker.pos.y-center.y,worker.pos.x-center.x);
 append({center.x+std::cos(angle)*reach,center.y+std::sin(angle)*reach});
 for(int spoke=0;spoke<32;++spoke) {
  const float a=spoke*(2*Pi/32);
  append({center.x+std::cos(a)*reach,center.y+std::sin(a)*reach});
 }
 return points;
}

bool Simulation::approachWork(Entity& worker,const Entity& target,float padding) {
 ensureNavigation();
 const float radius=definition(worker.kind).radius;
 if(worker.workPointValid&&worker.workTarget==target.id&&
    worker.pathGeometry==navigation_.geometryVersion()&&
    navigation_.pointClear(worker.workPoint,radius,worker.id)&&
    navigation_.segmentClear(worker.workPoint,target.pos,radius,target.id)) {
  if(moveToward(worker,worker.workPoint))return true;
  worker.workPointValid=false;
 }
 worker.workPointValid=false;
 if(worker.repath>0&&worker.pathGeometry==navigation_.geometryVersion())return !worker.navigationExhausted;
 const float reach=definition(target.kind).radius+radius+padding;
 auto points=workPoints(worker,target.pos,reach,target.id,navigation_,true);
 if(points.empty()) {
  if(workPoints(worker,target.pos,reach,target.id,navigation_,false).empty()) {
   // No legal work surface exists. Keep the task intact, but report the
   // obstruction and wait before checking again.
   worker.navigationExhausted=true;worker.repath=2.0f;
  } else {worker.navigationExhausted=false;worker.repath=0.5f;}
  worker.pathGeometry=navigation_.geometryVersion();return !worker.navigationExhausted;
 }
 auto route=findRoute(worker,points);
 if(!route.reached)return !worker.navigationExhausted;
 worker.workTarget=target.id;
 worker.workPoint=route.points.empty()?worker.pos:route.points.back();
 worker.workPointValid=true;worker.path=std::move(route.points);worker.pathIndex=0;
 worker.navigationBestDistance=0;
 return moveToward(worker,worker.workPoint);
}

Id Simulation::chooseWorkTarget(Entity& worker,const std::vector<Id>& targets,float padding) {
 ensureNavigation();
 if(worker.repath>0&&worker.pathGeometry==navigation_.geometryVersion())return 0;
 std::vector<Vec2> goals;std::vector<Id> owners;
 for(Id id:targets) {
  const Entity* target=find(id);if(!target)continue;
  const float reach=definition(target->kind).radius+definition(worker.kind).radius+padding;
  auto points=workPoints(worker,target->pos,reach,id,navigation_,true);
  for(Vec2 point:points){goals.push_back(point);owners.push_back(id);}
 }
 if(goals.empty()) {
  bool legalSurface=false;
  for(Id id:targets) {
   const Entity* target=find(id);if(!target)continue;
   const float reach=definition(target->kind).radius+definition(worker.kind).radius+padding;
   if(!workPoints(worker,target->pos,reach,id,navigation_,false).empty()) {legalSurface=true;break;}
  }
  worker.navigationExhausted=!legalSurface;
  worker.repath=legalSurface?0.5f:2.0f;
  worker.pathGeometry=navigation_.geometryVersion();return 0;
 }
 auto route=findRoute(worker,goals);
 if(!route.reached)return 0;
 const Vec2 arrival=route.points.empty()?worker.pos:route.points.back();
 std::size_t best=0;
 for(std::size_t i=1;i<goals.size();++i)
  if(distanceSquared(arrival,goals[i])<distanceSquared(arrival,goals[best]))best=i;
 worker.workTarget=owners[best];worker.workPoint=arrival;worker.workPointValid=true;
 worker.path=std::move(route.points);worker.pathIndex=0;worker.navigationBestDistance=0;
 return worker.workTarget;
}

bool Simulation::assignFreshWorkerToOre(Entity& worker,std::vector<Id>& assignmentCandidates,int& cursor,bool& deferred) {
 deferred=false;
 if(worker.kind!=Kind::Worker||!worker.alive()||!activeTeam(worker.team)||eliminated(worker.team))return false;
 ensureNavigation();
 std::vector<Id> depots;
 for(const auto& entity:entities_)if(entity.alive()&&entity.team==worker.team&&entity.progress>=1&&
    (entity.kind==Kind::Headquarters||entity.kind==Kind::Processor))depots.push_back(entity.id);
 if(depots.empty())return false;

 const float workerRadius=definition(worker.kind).radius;
 if(assignmentCandidates.empty()) {
  struct Candidate { Id resource=0; float score=0; };
  std::vector<Candidate> ranked;
  for(const auto& resource:entities_) {
   if(!resource.alive()||resource.kind!=Kind::Resource||resource.resource<=0||!explored(worker.team,resource.pos))continue;
   int assigned=0;
   for(const auto& other:entities_)if(other.id!=worker.id&&other.alive()&&other.team==worker.team&&
      other.kind==Kind::Worker&&other.order==Order::Gather&&other.resourceTarget==resource.id)++assigned;
   float nearestDepot=std::numeric_limits<float>::max();
   for(Id depotId:depots)if(const Entity* depot=find(depotId))
    nearestDepot=std::min(nearestDepot,std::max(0.0f,std::sqrt(distanceSquared(resource.pos,depot->pos))-
       definition(resource.kind).radius-definition(depot->kind).radius-2*workerRadius-24));
   const float outward=std::max(0.0f,std::sqrt(distanceSquared(worker.pos,resource.pos))-
      definition(resource.kind).radius-workerRadius-9);
   ranked.push_back({resource.id,outward+nearestDepot+assigned*320.0f});
  }
  std::sort(ranked.begin(),ranked.end(),[](const Candidate& left,const Candidate& right) {
   return left.score<right.score||(left.score==right.score&&left.resource<right.resource);
  });
  assignmentCandidates.reserve(ranked.size());for(const auto& candidate:ranked)assignmentCandidates.push_back(candidate.resource);
 }
 cursor=std::clamp(cursor,0,static_cast<int>(assignmentCandidates.size()));
 for(;cursor<static_cast<int>(assignmentCandidates.size());++cursor) {
  const Entity* resource=find(assignmentCandidates[static_cast<std::size_t>(cursor)]);
  if(!resource||!resource->alive()||resource->kind!=Kind::Resource||resource->resource<=0||!explored(worker.team,resource->pos))continue;
  const float resourceReach=definition(resource->kind).radius+workerRadius+9;
  const auto resourceGoals=workPoints(worker,resource->pos,resourceReach,resource->id,navigation_,true);
  if(resourceGoals.empty())continue;
  if(!claimNavigationSearch()){deferred=true;return false;}
  ++navigationStats_.searches;
  auto outward=navigation_.route(worker.pos,resourceGoals,workerRadius,worker.id);
  navigationStats_.expanded+=static_cast<std::uint64_t>(std::max(0,outward.expanded));
  if(!outward.reached)continue;
  const Vec2 resourcePoint=outward.points.empty()?worker.pos:outward.points.back();

  Entity returning=worker;returning.pos=resourcePoint;
  std::vector<Vec2> depotGoals;
  for(Id depotId:depots)if(const Entity* depot=find(depotId)) {
   const float depotReach=definition(depot->kind).radius+workerRadius+15;
   const auto points=workPoints(returning,depot->pos,depotReach,depot->id,navigation_,false);
   depotGoals.insert(depotGoals.end(),points.begin(),points.end());
  }
  if(depotGoals.empty())continue;
  if(!claimNavigationSearch()){deferred=true;return false;}
  ++navigationStats_.searches;
  const auto home=navigation_.route(resourcePoint,depotGoals,workerRadius,worker.id);
  navigationStats_.expanded+=static_cast<std::uint64_t>(std::max(0,home.expanded));
  if(!home.reached)continue;
  resetNavigation(worker);
  worker.order=Order::Gather;worker.target=resource->id;worker.resourceTarget=resource->id;
  worker.workTarget=resource->id;worker.workPoint=resourcePoint;worker.workPointValid=true;
  worker.path=std::move(outward.points);worker.pathGeometry=navigation_.geometryVersion();
  return true;
 }
 return false;
}

Id Simulation::reachableConstructionWorker(const std::vector<Id>& workers,Vec2 site,Kind kind,Id existing,bool automatic) const {
 ensureNavigation();
 auto priority=[&](const Entity& worker){return automatic?(worker.order==Order::Idle?0:1):(worker.order==Order::Construct?1:0);};
 std::vector<NavCircle> inputs;inputs.reserve(workers.size());
 for(Id id:workers)if(const auto* worker=find(id);worker&&worker->alive()&&worker->kind==Kind::Worker)
  inputs.push_back({id,worker->pos,static_cast<float>(priority(*worker))});
 if(buildAccessCached_&&buildAccessGeometry_==navigation_.geometryVersion()&&
    buildAccessSite_.x==site.x&&buildAccessSite_.y==site.y&&buildAccessKind_==kind&&
    buildAccessExisting_==existing&&buildAccessAutomatic_==automatic&&inputs.size()==buildAccessInputs_.size()&&
    std::equal(inputs.begin(),inputs.end(),buildAccessInputs_.begin(),[](const NavCircle& a,const NavCircle& b) {
     return a.id==b.id&&a.center.x==b.center.x&&a.center.y==b.center.y&&a.radius==b.radius;
    }))return buildAccessWorker_;
 Navigation proposed=navigation_;
 if(!existing)proposed.addCircle({std::numeric_limits<Id>::max(),site,definition(kind).radius});
 Id chosen=0;int chosenPriority=2;float best=std::numeric_limits<float>::max();
 std::vector<Id> ordered=workers;
 if(automatic)std::stable_sort(ordered.begin(),ordered.end(),[&](Id a,Id b) {
  const Entity* left=find(a);const Entity* right=find(b);
  const int pa=left?priority(*left):2,pb=right?priority(*right):2;
  return pa!=pb?pa<pb:a<b;
 });
 for(Id id:ordered) {
  const Entity* worker=find(id);
  if(!worker||!worker->alive()||worker->kind!=Kind::Worker)continue;
  const int workerPriority=priority(*worker);
  if(chosen&&workerPriority>chosenPriority)continue;
  const float reach=definition(kind).radius+definition(worker->kind).radius+20;
  const auto goals=workPoints(*worker,site,reach,existing?existing:std::numeric_limits<Id>::max(),proposed,false);
  const auto route=proposed.route(worker->pos,goals,definition(worker->kind).radius,worker->id);
  if(!route.reached)continue;
  if(!chosen||workerPriority<chosenPriority||(chosenPriority==workerPriority&&
    (route.cost<best||(route.cost==best&&id<chosen)))) {
   chosen=id;chosenPriority=workerPriority;best=route.cost;
  }
 }
 buildAccessCached_=true;buildAccessGeometry_=navigation_.geometryVersion();
 buildAccessSite_=site;buildAccessKind_=kind;buildAccessExisting_=existing;
 buildAccessAutomatic_=automatic;
 buildAccessInputs_=std::move(inputs);buildAccessWorker_=chosen;
 return chosen;
}
}
