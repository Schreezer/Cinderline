#include "Sim/Simulation.h"
#include "Sim/SimulationRules.h"

#include <string>

namespace cinder {
namespace {
constexpr float WorkerCarryCapacity=18.0f;
constexpr std::size_t MaxWorkerPlanNoticeBytes=240;
constexpr std::size_t MaxWorkerPlanAttemptsPerBoundary=4;
}

const std::string& Simulation::workerPlanNotice(int team) const {
 static const std::string Empty;
 return activeTeam(team)?workerPlanNotices_[team]:Empty;
}

std::uint64_t Simulation::workerPlanNoticeSerial(int team) const {
 return activeTeam(team)?workerPlanNoticeSerials_[team]:0;
}

void Simulation::emitWorkerPlanNotice(int team,const std::string& notice) {
 if(!activeTeam(team))return;
 workerPlanNotices_[team]=notice.substr(0,MaxWorkerPlanNoticeBytes);
 ++workerPlanNoticeSerials_[team];
}

CommandResult Simulation::queuedBuildStatus(const Entity& worker,Kind kind,Vec2 site) const {
 auto fail=[](const std::string& why){return CommandResult{false,why};};
 if(!worker.alive()||worker.kind!=Kind::Worker||!activeTeam(worker.team)||eliminated(worker.team))
  return fail("The assigned Drudge is no longer available.");
 if(!rules::validKind(kind)||!definition(kind).building)return fail("Only structures can be deployed.");
 if(!hasBuilding(worker.team,Kind::Headquarters))return fail("An operational Anchor is required.");
 if(players_[worker.team].tier<definition(kind).tier)
  return fail("Requires T"+std::to_string(definition(kind).tier)+". Choose TECH TIER at an operational Resonator.");
 if((kind==Kind::MotorPool||kind==Kind::Laboratory||kind==Kind::Turret)&&!hasBuilding(worker.team,Kind::Foundry))
  return fail("Build an operational Kiln first.");
 std::string reason;if(!canPlace(worker.team,kind,site,&reason))return fail(reason);
 if(players_[worker.team].ore<definition(kind).cost)return fail("Insufficient ore.");
 if(!reachableConstructionWorker({worker.id},site,kind))return fail("No accessible route to this construction site.");
 return {true,"Ready to build."};
}

bool Simulation::queuedGatherReachable(const Entity& worker,Id resourceId) const {
 const Entity* resource=find(resourceId);
 if(!worker.alive()||worker.kind!=Kind::Worker||!resource||!resource->alive()||
    resource->kind!=Kind::Resource||resource->resource<=0)return false;
 ensureNavigation();
 const float radius=definition(worker.kind).radius;
 const float reach=definition(resource->kind).radius+radius+9.0f;
 Navigation proposed=navigation_;
 const auto goals=workPoints(worker,resource->pos,reach,resource->id,proposed,false);
 if(goals.empty())return false;
 return proposed.route(worker.pos,goals,radius,worker.id).reached;
}

void Simulation::resumeOriginalGather(Entity& worker) {
 if(worker.kind!=Kind::Worker||!worker.alive()||!worker.resumeGather)return;
 worker.resumeGather=false;
 const Entity* deposit=find(worker.resourceTarget);
 if(!deposit||!deposit->alive()||deposit->kind!=Kind::Resource||deposit->resource<=0) {
  worker.resourceTarget=0;deposit=nullptr;
 }
 if(worker.carried>=WorkerCarryCapacity||(!deposit&&worker.carried>0))worker.returning=true;
 if(!deposit&&worker.carried<=0)worker.returning=false;
 worker.order=Order::Gather;worker.target=worker.resourceTarget;
}

bool Simulation::activateQueuedWorkerOrder(Id workerId,std::size_t& attemptBudget) {
 std::string skipped;
 std::size_t skippedCount=0;
 auto publishSkipped=[&](int team) {
  if(skipped.empty())return;
  const std::string summary="Skipped "+std::to_string(skippedCount)+" queued worker job"+
    (skippedCount==1?". ":"s. ")+skipped;
  emitWorkerPlanNotice(team,summary);
  if(team==0)alert_=workerPlanNotices_[team];
 };
 for(;;) {
  Entity* worker=get(workerId);
  if(!worker||!worker->alive()||worker->kind!=Kind::Worker||worker->order!=Order::Idle)return false;
  if(worker->futureOrders.empty()) {
   publishSkipped(worker->team);
   if(worker->resumeGather)resumeOriginalGather(*worker);
   return false;
  }
  const TacticalOrder plan=worker->futureOrders.front();
  if(plan.order!=Order::Construct&&plan.order!=Order::Gather) {
   publishSkipped(worker->team);return activateNextOrder(*worker);
  }
  if(attemptBudget==0) {publishSkipped(worker->team);return false;}
  --attemptBudget;

  std::string rejected;
  if(plan.hasArrivalFacing||plan.arrivalFacing!=0.0f)rejected="worker jobs do not accept formation modifiers.";
  else if(plan.order==Order::Gather) {
   const Entity* deposit=find(plan.supportTarget);
   if(plan.buildingKind!=Kind::Worker||!plan.supportTarget||!deposit||!deposit->alive()||
      deposit->kind!=Kind::Resource||deposit->resource<=0||!explored(worker->team,deposit->pos))
    rejected="the ore deposit is no longer available.";
   else if(!queuedGatherReachable(*worker,deposit->id))
    rejected="the ore deposit has no accessible route.";
   else {
    worker->futureOrders.erase(worker->futureOrders.begin());
    resetNavigation(*worker);clearSustainedOrder(*worker);
    worker->order=Order::Gather;worker->target=plan.supportTarget;
    worker->resourceTarget=plan.supportTarget;worker->returning=worker->carried>=WorkerCarryCapacity;
    worker->supportTarget=0;worker->resumeGather=false;
    worker->hasArrivalFacing=false;worker->arrivalFacing=0;
    publishSkipped(worker->team);
    return true;
   }
  } else if(plan.supportTarget) {
   const Entity* foundation=find(plan.supportTarget);
   if(plan.buildingKind!=Kind::Worker||!foundation||!foundation->alive()||
      foundation->team!=worker->team||!definition(foundation->kind).building||foundation->progress>=1)
    rejected="the foundation is no longer available.";
   else if(!reachableConstructionWorker({workerId},foundation->pos,foundation->kind,foundation->id))
    rejected="the foundation has no accessible route.";
   else {
    const Id foundationId=foundation->id;
    worker->futureOrders.erase(worker->futureOrders.begin());
    foundation=get(foundationId);worker=get(workerId);
    if(!foundation||!worker)continue;
    assignConstruction(*get(foundationId),*get(workerId));
    publishSkipped(worker->team);
    if(worker->team==0&&skipped.empty())alert_="Queued construction resumed.";
    return true;
   }
  } else {
   const auto status=queuedBuildStatus(*worker,plan.buildingKind,plan.point);
   if(!status.accepted)rejected=status.message;
   else {
    const int team=worker->team;const Kind kind=plan.buildingKind;const Vec2 site=plan.point;
    worker->futureOrders.erase(worker->futureOrders.begin());
    players_[team].ore-=definition(kind).cost;
    const Id foundationId=spawn(kind,team,site,false);
    // spawn() may reallocate entities_; reacquire both participants before use.
    Entity* foundation=get(foundationId);worker=get(workerId);
    if(!foundation||!worker)return false;
    assignConstruction(*foundation,*worker);
    publishSkipped(team);
    if(team==0&&skipped.empty())alert_="Queued "+std::string(definition(kind).name)+" foundation placed; Drudge assigned.";
    return true;
   }
  }

  worker=get(workerId);if(!worker)return false;
  worker->futureOrders.erase(worker->futureOrders.begin());
  const std::string notice="Queued worker job skipped: "+rejected;
  ++skippedCount;
  skipped+=notice+" ";
  // A stale plan is consumed exactly once; immediately try the remaining tail.
 }
}

void Simulation::processQueuedWorkerOrders() {
 std::size_t attemptBudget=MaxWorkerPlanAttemptsPerBoundary;
 std::vector<Id> pending;
 pending.reserve(entities_.size());
 for(const auto& entity:entities_)if(entity.alive()&&entity.kind==Kind::Worker&&
    entity.order==Order::Idle&&!entity.futureOrders.empty())pending.push_back(entity.id);
 for(Id id:pending)activateQueuedWorkerOrder(id,attemptBudget);
}
} // namespace cinder
