#include "Sim/Simulation.h"

#include <cmath>
#include <filesystem>
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

Simulation fixture() {
  Simulation simulation;
  simulation.reset({0,0x0D3Fu,false,1});
  const_cast<std::vector<Entity>&>(simulation.entities()).clear();
  const_cast<std::vector<Obstacle>&>(simulation.obstacles()).clear();
  simulation.debugSpawn(Kind::Headquarters,0,{700,700});
  simulation.debugSpawn(Kind::Headquarters,1,{4400,4400});
  return simulation;
}

CommandResult send(Simulation& simulation,CommandType type,int team,std::vector<Id> units,
                   Vec2 point={},Id target=0) {
  return simulation.command({type,team,std::move(units),point,target,Kind::Worker,0});
}

void step(Simulation& simulation,int steps) {
  for(int index=0;index<steps;++index) {
    const auto before=simulation.tick();
    simulation.update(Simulation::Step);
    check(simulation.tick()==before+1,"active order-completion fixture stopped advancing ticks");
    check(simulation.winner()==-1,"order-completion fixture ended while both headquarters survived");
  }
}

template<class Predicate>
void stepUntil(Simulation& simulation,int maximumSteps,Predicate complete,const std::string& failure) {
  for(int index=0;index<maximumSteps&&!complete();++index)step(simulation,1);
  check(complete(),failure);
}

Simulation checkpoint(const Simulation& simulation,const std::string& name) {
  const auto path=std::filesystem::temp_directory_path()/
    ("cinderline-order-completion-"+name+".sav");
  check(simulation.save(path.string()),"order-completion checkpoint saves");
  Simulation loaded;
  check(loaded.load(path.string()),"order-completion checkpoint loads");
  std::filesystem::remove(path);
  check(loaded.stateHash()==simulation.stateHash(),"checkpoint preserves pending order completion");
  check(loaded.tick()==simulation.tick()&&loaded.winner()==simulation.winner(),
        "checkpoint preserves tick and winner state");
  return loaded;
}

void stepPair(Simulation& simulation,Simulation& loaded,int steps) {
  for(int index=0;index<steps;++index) {
    step(simulation,1);
    step(loaded,1);
    check(loaded.stateHash()==simulation.stateHash(),
          "order completion diverged after save and load continuation");
  }
}

void explicitAttackStopsWhereItsTargetDied() {
  auto simulation=fixture();
  const Id attacker=simulation.debugSpawn(Kind::Striker,0,{1400,1500});
  const Id target=simulation.debugSpawn(Kind::Worker,1,{1650,1500});
  check(send(simulation,CommandType::Hold,1,{target}).accepted,"attack target accepts Hold");
  check(send(simulation,CommandType::Attack,0,{attacker},{},target).accepted,
        "explicit Attack is accepted");
  stepUntil(simulation,400,[&]{return !simulation.find(target)||!simulation.find(target)->alive();},
            "explicit Attack did not kill its target through ordinary combat");
  const Vec2 completionPosition=simulation.find(attacker)->pos;
  auto loaded=checkpoint(simulation,"explicit-attack");
  stepPair(simulation,loaded,20);
  for(const Simulation* result:{&simulation,&loaded}) {
    const Entity* unit=result->find(attacker);
    check(unit&&unit->order==Order::Idle&&unit->target==0,
          "completed explicit Attack does not become Idle");
    check(distance(unit->pos,completionPosition)<1.0f,
          "completed explicit Attack walked toward its target's corpse");
    check(distance(unit->goal,completionPosition)<1.0f,
          "completed explicit Attack retained a stale target goal");
  }
}

void explicitAttackHandlesCorpseCleanup() {
  auto simulation=fixture();
  step(simulation,99);
  const Id firstKiller=simulation.debugSpawn(Kind::Bastion,0,{1550,1500});
  const Id secondKiller=simulation.debugSpawn(Kind::Bastion,0,{1600,1500});
  const Id attacker=simulation.debugSpawn(Kind::Striker,0,{1200,1500});
  const Id target=simulation.debugSpawn(Kind::Worker,1,{1650,1500});
  check(send(simulation,CommandType::Hold,1,{target}).accepted,"cleanup target accepts Hold");
  check(send(simulation,CommandType::Attack,0,{firstKiller},{},target).accepted&&
        send(simulation,CommandType::Attack,0,{secondKiller},{},target).accepted&&
        send(simulation,CommandType::Attack,0,{attacker},{},target).accepted,
        "cleanup fixture accepts its ordinary Attack commands");
  step(simulation,1);
  check(simulation.tick()==100&&simulation.find(target)==nullptr,
        "real tick-100 combat removes the dead target before the attacker's next update");
  check(simulation.players()[0].stats.killed>=1,"cleanup fixture records the combat kill");
  const Vec2 completionPosition=simulation.find(attacker)->pos;
  auto loaded=checkpoint(simulation,"missing-attack-target");
  stepPair(simulation,loaded,20);
  for(const Simulation* result:{&simulation,&loaded}) {
    const Entity* unit=result->find(attacker);
    check(unit&&unit->order==Order::Idle&&unit->target==0,
          "Attack with a cleaned-up target does not complete to Idle");
    check(distance(unit->pos,completionPosition)<1.0f,
          "Attack with a cleaned-up target walked toward its stale position");
    check(distance(unit->goal,completionPosition)<1.0f,
          "Attack with a cleaned-up target retained a stale goal");
  }
}

void explicitAttackDoesNotInheritAnotherEnemy() {
  auto simulation=fixture();
  const Id finisher=simulation.debugSpawn(Kind::Bastion,0,{1600,1800});
  const Id explicitTarget=simulation.debugSpawn(Kind::Worker,1,{1650,1800});
  check(send(simulation,CommandType::Hold,1,{explicitTarget}).accepted,
        "shared explicit target accepts Hold");
  check(send(simulation,CommandType::Attack,0,{finisher},{},explicitTarget).accepted,
        "finisher accepts its first explicit Attack");
  step(simulation,1);
  check(simulation.find(explicitTarget)->alive()&&
        simulation.find(explicitTarget)->hp<definition(Kind::Worker).hp,
        "finisher naturally wounds the shared explicit target");
  check(send(simulation,CommandType::Move,0,{finisher},{1000,1800}).accepted,
        "finisher can wait for its weapon through ordinary movement");
  step(simulation,25);

  const Id laterAttacker=simulation.debugSpawn(Kind::Striker,0,{1400,1800});
  const Id incidentalEnemy=simulation.debugSpawn(Kind::Worker,1,{1450,1800});
  check(send(simulation,CommandType::Hold,1,{incidentalEnemy}).accepted,
        "nearby incidental enemy accepts Hold");
  check(send(simulation,CommandType::Attack,0,{finisher},{},explicitTarget).accepted&&
        send(simulation,CommandType::Attack,0,{laterAttacker},{},explicitTarget).accepted,
        "both attackers accept the same explicit target");
  step(simulation,1);
  check(!simulation.find(explicitTarget)->alive(),
        "earlier attacker finishes the explicit target before the later combat update");
  const Vec2 completionPosition=simulation.find(laterAttacker)->pos;
  check(simulation.find(incidentalEnemy)->hp==definition(Kind::Worker).hp,
        "later explicit attacker damaged an enemy it was never ordered to attack");
  check(simulation.find(laterAttacker)->order==Order::Idle&&
        simulation.find(laterAttacker)->target==0,
        "later explicit attacker inherited a nearby enemy after its target died");
  auto loaded=checkpoint(simulation,"shared-explicit-target");
  stepPair(simulation,loaded,10);
  for(const Simulation* result:{&simulation,&loaded}) {
    const Entity* unit=result->find(laterAttacker);
    check(unit&&unit->order==Order::Idle,
          "completed shared explicit Attack inherited explicit pursuit of an incidental enemy");
    check(distance(unit->pos,completionPosition)<1.0f&&
          distance(unit->goal,completionPosition)<1.0f,
          "completed shared explicit Attack did not stay at its completion anchor");
  }
}

void explicitAttackPreservesLiveTargetUntilVisibilityTransition() {
  auto simulation=fixture();
  const Id attacker=simulation.debugSpawn(Kind::Striker,0,{1000,2100});
  const Id explicitTarget=simulation.debugSpawn(Kind::Worker,1,{1569,2100});
  const Id incidentalEnemy=simulation.debugSpawn(Kind::Worker,1,{1100,2100});
  check(simulation.visible(0,simulation.find(explicitTarget)->pos),
        "moving explicit target starts in the final visible fog cell");
  check(send(simulation,CommandType::Move,1,{explicitTarget},{2000,2100}).accepted,
        "explicit target accepts an ordinary move into fog");
  check(send(simulation,CommandType::Hold,1,{incidentalEnemy}).accepted,
        "visibility fixture's incidental enemy accepts Hold");
  check(send(simulation,CommandType::Attack,0,{attacker},{},explicitTarget).accepted,
        "visible primary target accepts an explicit Attack");
  const Vec2 lastSeenGoal=simulation.find(attacker)->goal;
  step(simulation,1);
  check(simulation.find(explicitTarget)->alive()&&
        !simulation.visible(0,simulation.find(explicitTarget)->pos),
        "ordinary target movement crosses into fog between movement and combat");
  check(simulation.find(attacker)->order==Order::Attack&&
        simulation.find(attacker)->target==explicitTarget,
        "combat replaced a live hidden explicit target before movement handled vision loss");
  check(simulation.find(incidentalEnemy)->hp==definition(Kind::Worker).hp,
        "combat attacked a nearby enemy while the explicit target was only newly hidden");
  check(distance(simulation.find(attacker)->goal,lastSeenGoal)<0.01f,
        "newly hidden explicit target changed its last-seen goal");
  auto loaded=checkpoint(simulation,"visibility-transition");
  stepPair(simulation,loaded,1);
  for(const Simulation* result:{&simulation,&loaded}) {
    const Entity* unit=result->find(attacker);
    check(unit&&unit->order==Order::AttackMove,
          "live hidden explicit target did not transition through last-seen AttackMove");
    check(distance(unit->goal,lastSeenGoal)<0.01f,
          "last-seen AttackMove lost the explicit target's final visible position");
  }
}

void attackMoveContinuesAfterIncidentalKill() {
  auto simulation=fixture();
  const Vec2 destination{2500,2100};
  const Id attacker=simulation.debugSpawn(Kind::Striker,0,{1000,2100});
  const Id incidental=simulation.debugSpawn(Kind::Worker,1,{1300,2100});
  check(send(simulation,CommandType::Hold,1,{incidental}).accepted,
        "incidental target accepts Hold");
  check(send(simulation,CommandType::AttackMove,0,{attacker},destination).accepted,
        "AttackMove is accepted without an explicit target");
  const Vec2 acceptedGoal=simulation.find(attacker)->goal;
  stepUntil(simulation,400,[&]{return !simulation.find(incidental)||!simulation.find(incidental)->alive();},
            "AttackMove did not kill the incidental target through ordinary combat");
  check(distance(simulation.find(attacker)->goal,acceptedGoal)<0.01f,
        "incidental combat rewrote the accepted AttackMove goal");
  auto loaded=checkpoint(simulation,"attack-move");
  for(int index=0;index<500&&distance(simulation.find(attacker)->pos,acceptedGoal)>30;++index)
    stepPair(simulation,loaded,1);
  for(const Simulation* result:{&simulation,&loaded}) {
    const Entity* unit=result->find(attacker);
    check(unit&&distance(unit->pos,acceptedGoal)<=30,
          "AttackMove did not continue to its accepted goal after the incidental kill");
    check(distance(unit->goal,acceptedGoal)<0.01f,
          "AttackMove completion changed the accepted goal");
  }
}

void idleAutoAcquisitionReturnsToAnchor() {
  auto simulation=fixture();
  const Vec2 anchor{1400,2700};
  const Id guard=simulation.debugSpawn(Kind::Striker,0,anchor);
  const Id intruder=simulation.debugSpawn(Kind::Worker,1,{1600,2700});
  check(send(simulation,CommandType::Move,1,{intruder},{1850,2700}).accepted,
        "intruder accepts a movement order past the idle guard");
  stepUntil(simulation,400,[&]{return !simulation.find(intruder)||!simulation.find(intruder)->alive();},
            "idle guard did not kill its automatically acquired target");
  check(distance(simulation.find(guard)->pos,anchor)>30,
        "automatic pursuit did not displace the guard from its anchor");
  check(distance(simulation.find(guard)->goal,anchor)<0.01f,
        "automatic acquisition rewrote the idle guard's anchor");
  auto loaded=checkpoint(simulation,"idle-anchor");
  for(int index=0;index<200&&distance(simulation.find(guard)->pos,anchor)>30;++index)
    stepPair(simulation,loaded,1);
  for(const Simulation* result:{&simulation,&loaded}) {
    const Entity* unit=result->find(guard);
    check(unit&&unit->order==Order::Idle&&unit->target==0,
          "idle guard retained its dead automatic target");
    check(distance(unit->pos,anchor)<=30,"idle guard did not return to its original anchor");
    check(distance(unit->goal,anchor)<0.01f,"idle guard lost its original anchor");
  }
}

void menderStopsFollowingDeadLeader() {
  auto simulation=fixture();
  const Id leader=simulation.debugSpawn(Kind::Striker,0,{1500,3300});
  const Id mender=simulation.debugSpawn(Kind::Mender,0,{1300,3300});
  const Id enemy=simulation.debugSpawn(Kind::Bastion,1,{1750,3300});
  check(send(simulation,CommandType::Attack,0,{leader,mender},{},enemy).accepted,
        "mixed Attack assigns the Mender to its combat leader");
  check(simulation.find(mender)->order==Order::Attack&&simulation.find(mender)->target==leader,
        "Mender starts following the selected combat leader");
  check(send(simulation,CommandType::Attack,1,{enemy},{},leader).accepted,
        "enemy accepts an ordinary Attack against the leader");
  stepUntil(simulation,300,[&]{return !simulation.find(leader)||!simulation.find(leader)->alive();},
            "enemy did not kill the Mender's leader through ordinary combat");
  const Vec2 completionPosition=simulation.find(mender)->pos;
  auto loaded=checkpoint(simulation,"mender-leader");
  stepPair(simulation,loaded,5);
  for(const Simulation* result:{&simulation,&loaded}) {
    const Entity* unit=result->find(mender);
    check(unit&&unit->order==Order::Idle&&unit->target==0,
          "Mender retained its dead combat leader");
    check(distance(unit->pos,completionPosition)<1.0f,
          "Mender walked toward its dead leader after follow completion");
    check(distance(unit->goal,completionPosition)<1.0f,
          "Mender retained a stale leader goal after follow completion");
  }
}

} // namespace

int main() {
  const std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"explicit Attack target death",explicitAttackStopsWhereItsTargetDied},
    {"explicit Attack missing corpse",explicitAttackHandlesCorpseCleanup},
    {"explicit Attack shared target death",explicitAttackDoesNotInheritAnotherEnemy},
    {"explicit Attack visibility transition",explicitAttackPreservesLiveTargetUntilVisibilityTransition},
    {"AttackMove incidental target death",attackMoveContinuesAfterIncidentalKill},
    {"Idle automatic acquisition anchor",idleAutoAcquisitionReturnsToAnchor},
    {"Mender leader death",menderStopsFollowingDeadLeader},
  };
  int failed=0;
  for(const auto& [name,test]:tests) {
    try {test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
  }
  std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
