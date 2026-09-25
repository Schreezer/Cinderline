#include "Sim/Simulation.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace cinder;

namespace {

void check(bool condition,const std::string& message) {
  if(!condition)throw std::runtime_error(message);
}

float distance(Vec2 left,Vec2 right) {
  return std::hypot(left.x-right.x,left.y-right.y);
}

std::vector<Entity>& entities(Simulation& simulation) {
  return const_cast<std::vector<Entity>&>(simulation.entities());
}

std::vector<Obstacle>& obstacles(Simulation& simulation) {
  return const_cast<std::vector<Obstacle>&>(simulation.obstacles());
}

Entity* edit(Simulation& simulation,Id id) {
  for(auto& entity:entities(simulation))if(entity.id==id)return &entity;
  return nullptr;
}

Simulation fixture() {
  Simulation simulation;
  // The interruption fixture installs its own sealed, flat routes.
  simulation.reset({0,0x1A7E22u,false,1,MatchLength::Standard,2,0});
  entities(simulation).clear();
  obstacles(simulation).clear();
  simulation.debugSpawn(Kind::Headquarters,0,{700,700});
  simulation.debugSpawn(Kind::Headquarters,1,{4400,4400});
  simulation.debugResources(0,10000);
  return simulation;
}

CommandResult send(Simulation& simulation,CommandType type,int team,std::vector<Id> units,
                   Vec2 point={},Id target=0,Kind kind=Kind::Worker,
                   CommandQueueMode mode=CommandQueueMode::Replace) {
  return simulation.command({type,team,std::move(units),point,target,kind,0,mode});
}

void step(Simulation& simulation,int count=1) {
  for(int index=0;index<count;++index) {
    const auto tick=simulation.tick();
    simulation.update(Simulation::Step);
    check(simulation.tick()==tick+1&&simulation.winner()==-1,
          "interruption fixture remains an active match");
  }
}

template<class Predicate>
void stepUntil(Simulation& simulation,int maximum,Predicate complete,const std::string& failure) {
  for(int index=0;index<maximum&&!complete();++index)step(simulation);
  check(complete(),failure);
}

Id first(const Simulation& simulation,int team,Kind kind) {
  for(const auto& entity:simulation.entities())
    if(entity.alive()&&entity.team==team&&entity.kind==kind)return entity.id;
  return 0;
}

void sealedTerrainPreservesAndResumes(CommandType currentType,CommandType tailType,
                                      const std::string& label) {
  auto simulation=fixture();
  obstacles(simulation)={{{2400,2400},{100,2400}}};
  const Id unit=simulation.debugSpawn(Kind::Striker,0,{1200,1800});
  check(send(simulation,currentType,0,{unit},{3200,1800}).accepted&&
        send(simulation,tailType,0,{unit},{3800,2100},0,Kind::Worker,
             CommandQueueMode::Append).accepted,
        label+" accepts a current waypoint and one future waypoint");
  const Vec2 currentGoal=simulation.find(unit)->goal;
  const TacticalOrder tail=simulation.find(unit)->futureOrders.front();
  const Order expectedCurrent=currentType==CommandType::Move?Order::Move:Order::AttackMove;
  const Order expectedTail=tailType==CommandType::Move?Order::Move:Order::AttackMove;

  stepUntil(simulation,300,[&]{return simulation.find(unit)->navigationExhausted;},
            label+" did not prove the sealed destination unreachable");
  const Entity* blocked=simulation.find(unit);
  check(blocked->order==expectedCurrent&&distance(blocked->goal,currentGoal)<0.01f&&
        blocked->futureOrders.size()==1&&blocked->futureOrders.front().order==expectedTail&&
        distance(blocked->futureOrders.front().point,tail.point)<0.01f,
        label+" preserves the exact current goal and future tail while terrain is sealed");

  obstacles(simulation).clear();
  bool activatedTail=false;
  for(int index=0;index<1000&&!activatedTail;++index) {
    step(simulation);
    const Entity* moving=simulation.find(unit);
    if(moving->order==expectedTail&&moving->futureOrders.empty()) {
      check(distance(moving->pos,currentGoal)<30&&distance(moving->goal,tail.point)<0.01f,
            label+" reaches the first waypoint before activating the unchanged tail");
      activatedTail=true;
    }
  }
  check(activatedTail,label+" did not resume after the sealed geometry opened");
  stepUntil(simulation,700,[&] {
    const Entity* moving=simulation.find(unit);
    return moving->order==Order::Idle&&moving->futureOrders.empty()&&distance(moving->pos,tail.point)<30;
  },label+" did not finish its second waypoint after geometry opened");
}

void blockedMoveAndAttackMoveResumeInOrder() {
  sealedTerrainPreservesAndResumes(CommandType::Move,CommandType::AttackMove,"Move interruption");
  sealedTerrainPreservesAndResumes(CommandType::AttackMove,CommandType::Move,"AttackMove interruption");
}

struct ConstructionPlan {
  Simulation simulation;
  Id worker=0;
  Id foundation=0;
  Vec2 firstSuccessor{};
  Vec2 secondSuccessor{};
};

ConstructionPlan constructionPlan(float y) {
  ConstructionPlan plan{fixture()};
  plan.worker=plan.simulation.debugSpawn(Kind::Worker,0,{1100,y});
  const Vec2 site{1450,y};
  plan.firstSuccessor={1900,y};
  plan.secondSuccessor={2200,y+180};
  check(send(plan.simulation,CommandType::Build,0,{plan.worker},site,0,Kind::Foundry).accepted,
        "construction interruption fixture places an ordinary paid foundation");
  plan.foundation=first(plan.simulation,0,Kind::Foundry);
  check(plan.foundation!=0&&
        send(plan.simulation,CommandType::Move,0,{plan.worker},plan.firstSuccessor,0,
             Kind::Worker,CommandQueueMode::Append).accepted&&
        send(plan.simulation,CommandType::AttackMove,0,{plan.worker},plan.secondSuccessor,0,
             Kind::Worker,CommandQueueMode::Append).accepted,
        "constructing worker accepts two explicit successors");
  const Entity* worker=plan.simulation.find(plan.worker);
  check(worker->order==Order::Construct&&worker->target==plan.foundation&&
        worker->futureOrders.size()==2&&plan.simulation.constructionWorker(plan.foundation)==plan.worker,
        "construction interruption fixture starts with an intact current link and two-step tail");
  return plan;
}

void destroyedFoundationActivatesExactlyOneSuccessor() {
  auto plan=constructionPlan(1400);
  Entity* foundation=edit(plan.simulation,plan.foundation);
  foundation->hp=1;
  const Id attacker=plan.simulation.debugSpawn(Kind::Striker,1,{foundation->pos.x+260,foundation->pos.y});
  check(send(plan.simulation,CommandType::Attack,1,{attacker},{},plan.foundation).accepted,
        "enemy receives a real attack order against the unfinished foundation");
  stepUntil(plan.simulation,20,[&] {
    const Entity* target=plan.simulation.find(plan.foundation);
    return !target||!target->alive();
  },"ordinary combat did not destroy the unfinished foundation");

  const Entity* worker=plan.simulation.find(plan.worker);
  const Entity* destroyed=plan.simulation.find(plan.foundation);
  check(worker&&worker->alive()&&worker->order==Order::Move&&worker->target==0&&
        distance(worker->goal,plan.firstSuccessor)<0.01f&&worker->futureOrders.size()==1&&
        worker->futureOrders.front().order==Order::AttackMove&&
        distance(worker->futureOrders.front().point,plan.secondSuccessor)<0.01f,
        "destroying a foundation activates exactly one successor on its surviving builder");
  check(destroyed&&destroyed->builderId==0&&plan.simulation.constructionWorker(plan.foundation)==0,
        "destroyed foundation and surviving builder release both sides of the construction link");
}

void lostBuilderLinkActivatesExactlyOneSuccessor() {
  auto plan=constructionPlan(2200);
  edit(plan.simulation,plan.foundation)->builderId=0;
  step(plan.simulation);
  const Entity* worker=plan.simulation.find(plan.worker);
  check(worker&&worker->alive()&&worker->order==Order::Move&&worker->target==0&&
        distance(worker->goal,plan.firstSuccessor)<0.01f&&worker->futureOrders.size()==1&&
        worker->futureOrders.front().order==Order::AttackMove&&
        distance(worker->futureOrders.front().point,plan.secondSuccessor)<0.01f,
        "a surviving worker activates exactly one successor after its builder link is lost");
  check(plan.simulation.find(plan.foundation)->builderId==0&&
        plan.simulation.constructionWorker(plan.foundation)==0,
        "lost-link cleanup leaves the paid foundation unassigned");
}

void dyingBuilderClearsTailAndNeverAdvances() {
  auto plan=constructionPlan(3000);
  Entity* worker=edit(plan.simulation,plan.worker);
  const Id attacker=plan.simulation.debugSpawn(Kind::Striker,1,{worker->pos.x+200,worker->pos.y});
  check(send(plan.simulation,CommandType::Attack,1,{attacker},{},plan.worker).accepted,
        "enemy receives a real attack order against the assigned builder");
  worker=edit(plan.simulation,plan.worker);
  worker->hp=1;
  stepUntil(plan.simulation,20,[&] {
    const Entity* target=plan.simulation.find(plan.worker);
    return !target||!target->alive();
  },"ordinary combat did not kill the assigned builder");

  const Entity* dead=plan.simulation.find(plan.worker);
  const Entity* foundation=plan.simulation.find(plan.foundation);
  check(dead&&!dead->alive()&&dead->order==Order::Idle&&dead->target==0&&
        dead->supportTarget==0&&dead->futureOrders.empty(),
        "builder death clears its current construction state and complete tactical tail");
  check(foundation&&foundation->alive()&&foundation->builderId==0&&
        plan.simulation.constructionWorker(plan.foundation)==0,
        "builder death releases the surviving foundation link");
  const float progress=foundation->progress;
  step(plan.simulation,20);
  dead=plan.simulation.find(plan.worker);
  check(dead&&!dead->alive()&&dead->futureOrders.empty()&&
        plan.simulation.find(plan.foundation)->progress==progress,
        "dead builder never advances a successor or the unassigned foundation");
}

} // namespace

int main() {
  const std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"sealed Move and AttackMove interruption",blockedMoveAndAttackMoveResumeInOrder},
    {"destroyed foundation successor",destroyedFoundationActivatesExactlyOneSuccessor},
    {"lost builder link successor",lostBuilderLinkActivatesExactlyOneSuccessor},
    {"dying builder cleanup",dyingBuilderClearsTailAndNeverAdvances},
  };
  int failed=0;
  for(const auto& [name,test]:tests) {
    try {test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
  }
  std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
