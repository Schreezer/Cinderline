#include "Sim/Simulation.h"
#include "Sim/Network.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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

float distance(Vec2 a,Vec2 b) { return std::hypot(a.x-b.x,a.y-b.y); }

float segmentDistanceSquared(Vec2 from,Vec2 to,Vec2 point) {
  const Vec2 delta{to.x-from.x,to.y-from.y};
  const float lengthSquared=delta.x*delta.x+delta.y*delta.y;
  const float projection=lengthSquared>0
      ? std::clamp(((point.x-from.x)*delta.x+(point.y-from.y)*delta.y)/lengthSquared,0.0f,1.0f)
      : 0.0f;
  const Vec2 closest{from.x+delta.x*projection,from.y+delta.y*projection};
  const float dx=point.x-closest.x,dy=point.y-closest.y;
  return dx*dx+dy*dy;
}

bool exact(Vec2 a,Vec2 b) {
  std::uint32_t ax=0,ay=0,bx=0,by=0;
  std::memcpy(&ax,&a.x,sizeof(ax));std::memcpy(&ay,&a.y,sizeof(ay));
  std::memcpy(&bx,&b.x,sizeof(bx));std::memcpy(&by,&b.y,sizeof(by));
  return ax==bx&&ay==by;
}

std::vector<Entity>& mutableEntities(Simulation& simulation) {
  return const_cast<std::vector<Entity>&>(simulation.entities());
}

std::vector<Obstacle>& mutableObstacles(Simulation& simulation) {
  return const_cast<std::vector<Obstacle>&>(simulation.obstacles());
}

Simulation emptyFixture() {
  Simulation simulation;
  // This traffic arena installs custom flat corridors and crowd positions.
  simulation.reset({0,0x7A11u,false,1,MatchLength::Standard,2,0});
  mutableEntities(simulation).clear();
  mutableObstacles(simulation).clear();
  simulation.debugSpawn(Kind::Headquarters,0,{450,450});
  simulation.debugSpawn(Kind::Headquarters,1,{4300,4300});
  return simulation;
}

std::filesystem::path fixturePath(const std::string& name) {
  const std::filesystem::path besideSource=std::filesystem::path(__FILE__).parent_path()/"Fixtures"/name;
  if(std::filesystem::exists(besideSource))return besideSource;
  for(std::filesystem::path directory=std::filesystem::current_path();!directory.empty();) {
    const auto candidate=directory/"Tests"/"Fixtures"/name;
    if(std::filesystem::exists(candidate))return candidate;
    const auto parent=directory.parent_path();if(parent==directory)break;directory=parent;
  }
  throw std::runtime_error("cannot locate traffic fixture "+name);
}

CommandResult send(Simulation& simulation,CommandType type,std::vector<Id> units,
                   Vec2 point={}) {
  Command command;command.type=type;command.team=0;command.units=std::move(units);command.point=point;
  return simulation.command(command);
}

void checkStaticClearance(const Simulation& simulation,Id id,const std::string& context) {
  const Entity* moving=simulation.find(id);
  check(moving&&moving->alive(),context+": unit disappeared");
  const float radius=definition(moving->kind).radius;
  check(std::isfinite(moving->pos.x)&&std::isfinite(moving->pos.y)&&
        moving->pos.x>=radius&&moving->pos.y>=radius&&
        moving->pos.x<=simulation.worldSize()-radius&&moving->pos.y<=simulation.worldSize()-radius,
        context+": unit left finite world bounds");
  for(const auto& entity:simulation.entities()) {
    if(entity.id==id||!entity.alive()||
       (!definition(entity.kind).building&&entity.kind!=Kind::Resource)||
       (entity.kind==Kind::Resource&&entity.resource<=0))continue;
    check(distance(moving->pos,entity.pos)+0.001f>=radius+definition(entity.kind).radius,
          context+": unit penetrated static geometry");
  }
  for(const auto& obstacle:simulation.obstacles()) {
    const float dx=std::max(std::fabs(moving->pos.x-obstacle.center.x)-obstacle.half.x,0.0f);
    const float dy=std::max(std::fabs(moving->pos.y-obstacle.center.y)-obstacle.half.y,0.0f);
    check(dx*dx+dy*dy+0.001f>=radius*radius,
          context+": unit penetrated an obstacle box");
  }
}

void run(Simulation& simulation,int ticks,const std::vector<Id>& checked,
         const std::string& context) {
  for(int tick=0;tick<ticks;++tick) {
    simulation.update(Simulation::Step);
    for(Id id:checked)checkStaticClearance(simulation,id,context);
  }
}

void denseIdleWorkersDoNotPermanentlyBlockProducedUnits() {
  Simulation simulation=emptyFixture();
  mutableObstacles(simulation)={
    {{1150,1050},{650,70}},
    {{1150,1350},{650,70}}
  };
  const Vec2 crowdGoal{1100,1200};
  std::vector<Id> workers;
  for(Vec2 point:std::vector<Vec2>{{1020,1150},{1060,1150},{1100,1150},{1140,1150},{1180,1150},
                                  {1020,1250},{1060,1250},{1100,1250},{1140,1250},{1180,1250}}) {
    const Id worker=simulation.debugSpawn(Kind::Worker,0,point);
    workers.push_back(worker);
    check(send(simulation,CommandType::Move,{worker},crowdGoal).accepted,"worker crowd move is accepted");
  }
  run(simulation,800,workers,"worker crowd setup");
  int idle=0;
  for(Id worker:workers)if(simulation.find(worker)->order==Order::Idle)++idle;
  check(idle>=8,"worker crowd did not settle into idle reclaimers");

  const Id anvil=simulation.debugSpawn(Kind::Bastion,0,{760,1200});
  const Id mortar=simulation.debugSpawn(Kind::Mortar,0,{680,1200});
  const Vec2 anvilGoal{1600,1200},mortarGoal{1600,1200};
  check(send(simulation,CommandType::Move,{anvil},anvilGoal).accepted,"Anvil traffic move is accepted");
  check(send(simulation,CommandType::Move,{mortar},mortarGoal).accepted,"Mortar traffic move is accepted");
  bool anvilCompleted=false,mortarCompleted=false;
  for(int tick=0;tick<1600;++tick) {
    simulation.update(Simulation::Step);
    checkStaticClearance(simulation,anvil,"dense friendly traffic");
    checkStaticClearance(simulation,mortar,"dense friendly traffic");
    anvilCompleted=anvilCompleted||
        (simulation.find(anvil)->order==Order::Idle&&distance(simulation.find(anvil)->pos,anvilGoal)<=35);
    mortarCompleted=mortarCompleted||
        (simulation.find(mortar)->order==Order::Idle&&distance(simulation.find(mortar)->pos,mortarGoal)<=35);
  }

  check(exact(simulation.find(anvil)->goal,anvilGoal)&&exact(simulation.find(mortar)->goal,mortarGoal),
        "traffic recovery rewrote an accepted goal");
  if(!anvilCompleted||!mortarCompleted) {
    for(Id id:std::vector<Id>{anvil,mortar}) {
      const Entity* mover=simulation.find(id);
      std::cerr<<"DENSE_TRAFFIC_FAILURE id="<<id<<" kind="<<static_cast<int>(mover->kind)
               <<" pos="<<mover->pos.x<<','<<mover->pos.y
               <<" distance="<<distance(mover->pos,mover->goal)
               <<" path="<<mover->pathIndex<<'/'<<mover->path.size()
               <<" stalled="<<mover->stalledFor<<" failures="<<mover->navigationFailures
               <<" side="<<mover->avoidanceSide<<" yield="<<mover->yieldFor<<'\n';
      for(const Entity& other:simulation.entities()) {
        if(other.id==id||!other.alive()||definition(other.kind).building||
           other.kind==Kind::Resource||distance(mover->pos,other.pos)>100)continue;
        std::cerr<<"  NEIGHBOR id="<<other.id<<" kind="<<static_cast<int>(other.kind)
                 <<" pos="<<other.pos.x<<','<<other.pos.y
                 <<" distance="<<distance(mover->pos,other.pos)
                 <<" order="<<static_cast<int>(other.order)
                 <<" goal_distance="<<distance(other.pos,other.goal)
                 <<" path="<<other.pathIndex<<'/'<<other.path.size()
                 <<" failures="<<other.navigationFailures<<" yield="<<other.yieldFor<<'\n';
      }
    }
  }
  check(anvilCompleted&&mortarCompleted,
        "dense idle-worker traffic permanently blocked a produced unit");
  for(Id worker:workers)check(simulation.find(worker)->order==Order::Idle,
                              "traffic recovery changed an idle worker's intent");
}

void holdRemainsFixedWhileDefendYieldsAndReclaims() {
  Simulation simulation=emptyFixture();
  const Id held=simulation.debugSpawn(Kind::Worker,0,{1100,1160});
  const Id defended=simulation.debugSpawn(Kind::Worker,0,{1100,1240});
  check(send(simulation,CommandType::Hold,{held}).accepted,"Hold anchor command is accepted");
  check(send(simulation,CommandType::Defend,{defended},{1100,1240}).accepted,
        "Defend anchor command is accepted");
  const Vec2 heldAt=simulation.find(held)->pos,defendedAt=simulation.find(defended)->pos;
  const Vec2 defendedGoal=simulation.find(defended)->goal;
  const bool defendedHasFacing=simulation.find(defended)->hasArrivalFacing;
  const float defendedArrivalFacing=simulation.find(defended)->arrivalFacing;

  const Id anvil=simulation.debugSpawn(Kind::Bastion,0,{760,1200});
  const Vec2 goal{1500,1200};
  check(send(simulation,CommandType::Move,{anvil},goal).accepted,"anchored-traffic move is accepted");
  std::size_t maximumPathSize=0;
  const auto savePath=std::filesystem::temp_directory_path()/
      "cinderline-persistent-friendly-traffic.sav";
  Vec2 previousMover=simulation.find(anvil)->pos;
  int defendedStableTicks=0;
  for(int tick=0;tick<1200;++tick) {
    simulation.update(Simulation::Step);
    for(Id id:std::vector<Id>{anvil,held,defended})
      checkStaticClearance(simulation,id,"anchored friendly traffic");
    maximumPathSize=std::max(maximumPathSize,simulation.find(anvil)->path.size());
    const Vec2 currentMover=simulation.find(anvil)->pos;
    const float moverRadius=definition(Kind::Bastion).radius;
    const float anchorRadius=definition(Kind::Worker).radius;
    check(segmentDistanceSquared(previousMover,currentMover,heldAt)+0.001f>=
              (moverRadius+anchorRadius)*(moverRadius+anchorRadius),
          "traffic recovery swept through the fixed Hold footprint");
    check(distance(previousMover,currentMover)<=
              definition(Kind::Bastion).speed*Simulation::Step+8.001f,
          "traffic recovery moved a unit more than once in one simulation step");
    previousMover=currentMover;
    const Entity* holdState=simulation.find(held);const Entity* defendState=simulation.find(defended);
    check(holdState->order==Order::Hold&&exact(holdState->pos,heldAt),
          "traffic recovery moved or retasked the explicit Hold");
    check(defendState->order==Order::Defend&&exact(defendState->goal,defendedGoal)&&
          defendState->hasArrivalFacing==defendedHasFacing&&
          defendState->arrivalFacing==defendedArrivalFacing&&
          std::signbit(defendState->arrivalFacing)==std::signbit(defendedArrivalFacing),
          "traffic recovery changed Defend order, anchor, or facing intent");
    defendedStableTicks=distance(defendState->pos,defendedAt)<=5?
      defendedStableTicks+1:0;
    if(tick==399) {
      check(simulation.save(savePath.string()),"persistent traffic state saves");
      Simulation loaded;
      check(loaded.load(savePath.string())&&loaded.stateHash()==simulation.stateHash(),
            "persistent traffic state survives save/load");
      std::filesystem::remove(savePath);

      const auto bytes=net::encodeSnapshot(net::snapshotFor(simulation,0));
      net::Snapshot decoded;
      std::string error;
      check(!bytes.empty()&&net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error),
            "persistent traffic snapshot round trips: "+error);
      Simulation replica;
      check(replica.applySnapshot(decoded,&error),"persistent traffic snapshot applies: "+error);
      const Entity* copied=replica.find(anvil);
      check(copied&&exact(copied->goal,goal)&&
            copied->path.size()<=Simulation::FogSize*Simulation::FogSize+1,
            "persistent traffic snapshot lost or oversized the accepted route");
    }
  }
  std::filesystem::remove(savePath);

  check(exact(simulation.find(held)->pos,heldAt),
        "traffic recovery displaced the explicit Hold anchor");
  check(defendedStableTicks>=20&&distance(simulation.find(defended)->pos,defendedAt)<=5,
        "yielding Defend did not reclaim its original anchor for twenty stable ticks");
  check(simulation.find(held)->order==Order::Hold&&simulation.find(defended)->order==Order::Defend,
        "traffic recovery changed an anchored order");
  check(maximumPathSize<=8,
        "persistent traffic recovery grew the simple route without bound");
  if(distance(simulation.find(anvil)->pos,goal)>35) {
    const Entity* mover=simulation.find(anvil);
    std::cerr<<"TRAFFIC_FAILURE pos="<<mover->pos.x<<','<<mover->pos.y
             <<" goal="<<mover->goal.x<<','<<mover->goal.y
             <<" path="<<mover->pathIndex<<'/'<<mover->path.size()
             <<" stalled="<<mover->stalledFor<<" failures="<<mover->navigationFailures
             <<" side="<<mover->avoidanceSide<<" yield="<<mover->yieldFor<<'\n';
  }
  check(exact(simulation.find(anvil)->goal,goal)&&distance(simulation.find(anvil)->pos,goal)<=35,
        "moving unit did not pass fixed friendly anchors");
}

void opposingFriendlyTrafficMovesAtMostOncePerTick() {
  Simulation simulation=emptyFixture();
  const Id rightward=simulation.debugSpawn(Kind::Striker,0,{850,2200});
  const Id leftward=simulation.debugSpawn(Kind::Striker,0,{1150,2200});
  const Vec2 rightGoal{1350,2200},leftGoal{650,2200};
  check(send(simulation,CommandType::Move,{rightward},rightGoal).accepted&&
        send(simulation,CommandType::Move,{leftward},leftGoal).accepted,
        "opposing friendly moves are accepted");

  Vec2 previousRight=simulation.find(rightward)->pos;
  Vec2 previousLeft=simulation.find(leftward)->pos;
  const float maximumDisplacement=definition(Kind::Striker).speed*Simulation::Step+4.001f;
  for(int tick=0;tick<400;++tick) {
    simulation.update(Simulation::Step);
    const Vec2 currentRight=simulation.find(rightward)->pos;
    const Vec2 currentLeft=simulation.find(leftward)->pos;
    check(distance(previousRight,currentRight)<=maximumDisplacement&&
          distance(previousLeft,currentLeft)<=maximumDisplacement,
          "opposing friendly traffic moved a unit twice in one simulation step");
    previousRight=currentRight;previousLeft=currentLeft;
    checkStaticClearance(simulation,rightward,"opposing friendly traffic");
    checkStaticClearance(simulation,leftward,"opposing friendly traffic");
  }
  check(exact(simulation.find(rightward)->goal,rightGoal)&&
        exact(simulation.find(leftward)->goal,leftGoal),
        "opposing friendly avoidance rewrote an accepted goal");
  check(distance(simulation.find(rightward)->pos,rightGoal)<=35&&
        distance(simulation.find(leftward)->pos,leftGoal)<=35,
        "opposing friendly units did not pass one another");
}

void congestedGatherRetainsItsWorkIntent() {
  Simulation simulation=emptyFixture();
  const Id ore=simulation.debugSpawn(Kind::Resource,-1,{1500,1800});
  mutableEntities(simulation).back().resource=1000;
  simulation.update(Simulation::Step);

  std::vector<Id> anchors;
  for(Vec2 point:std::vector<Vec2>{{1100,1740},{1100,1780},{1100,1820},{1100,1860}}) {
    const Id anchor=simulation.debugSpawn(Kind::Worker,0,point);
    anchors.push_back(anchor);
    check(send(simulation,CommandType::Hold,{anchor}).accepted,
          "gather-traffic anchor accepts Hold");
  }
  const Id gatherer=simulation.debugSpawn(Kind::Worker,0,{760,1800});
  Command gather;gather.type=CommandType::Gather;gather.team=0;gather.units={gatherer};gather.target=ore;
  check(simulation.command(gather).accepted,"congested gather order is accepted");

  for(int tick=0;tick<1200;++tick) {
    simulation.update(Simulation::Step);
    checkStaticClearance(simulation,gatherer,"congested gather traffic");
    const Entity* worker=simulation.find(gatherer);
    check(worker&&worker->order==Order::Gather&&worker->resourceTarget==ore,
          "traffic recovery abandoned the gatherer's ore target");
  }
  check(simulation.players()[0].stats.gathered>0,
        "congested gatherer never reached and delivered from its work target");
  for(Id anchor:anchors)
    check(simulation.find(anchor)->order==Order::Hold,
          "gather traffic recovery changed a Hold anchor");
}

void recordedPaidCrowdCompletesOriginalMoves() {
  Simulation simulation;
  check(simulation.load(fixturePath("production-crowd-stall.save").string()),
        "recorded paid crowd fixture loads");
  constexpr Id Anvil=186,Mortar=262;
  const Vec2 anvilGoal{1550,1294},mortarGoal{1614,1422};
  check(simulation.winner()==-1&&simulation.find(Anvil)&&simulation.find(Mortar),
        "recorded paid crowd fixture is a live match with both laggards");
  check(simulation.find(Anvil)->order==Order::Move&&simulation.find(Mortar)->order==Order::Move&&
        exact(simulation.find(Anvil)->goal,anvilGoal)&&exact(simulation.find(Mortar)->goal,mortarGoal),
        "recorded paid laggards retain their original accepted Move goals");

  const std::uint64_t initialTick=simulation.tick();
  bool anvilCompleted=false,mortarCompleted=false;
  std::size_t maximumPathSize=0;
  for(int tick=0;tick<400;++tick) {
    simulation.update(Simulation::Step);
    check(simulation.tick()==initialTick+static_cast<std::uint64_t>(tick+1)&&simulation.winner()==-1,
          "recorded paid crowd continuation stopped advancing a live match");
    for(Id id:std::vector<Id>{Anvil,Mortar})checkStaticClearance(simulation,id,"recorded paid crowd");
    const Entity* anvil=simulation.find(Anvil);const Entity* mortar=simulation.find(Mortar);
    check(exact(anvil->goal,anvilGoal)&&exact(mortar->goal,mortarGoal),
          "paid crowd recovery rewrote an accepted formation goal");
    maximumPathSize=std::max({maximumPathSize,anvil->path.size(),mortar->path.size()});
    anvilCompleted=anvilCompleted||(anvil->order==Order::Idle&&distance(anvil->pos,anvilGoal)<=35);
    mortarCompleted=mortarCompleted||(mortar->order==Order::Idle&&distance(mortar->pos,mortarGoal)<=35);
  }

  const auto savePath=std::filesystem::temp_directory_path()/"cinderline-paid-crowd-midpoint.sav";
  check(simulation.save(savePath.string()),"paid crowd midpoint saves");
  Simulation resumed;
  check(resumed.load(savePath.string())&&resumed.stateHash()==simulation.stateHash(),
        "paid crowd midpoint survives save/load exactly");
  std::filesystem::remove(savePath);
  for(int tick=0;tick<800;++tick) {
    simulation.update(Simulation::Step);resumed.update(Simulation::Step);
    for(Id id:std::vector<Id>{Anvil,Mortar})checkStaticClearance(simulation,id,"recorded paid crowd");
    const Entity* anvil=simulation.find(Anvil);const Entity* mortar=simulation.find(Mortar);
    check(exact(anvil->goal,anvilGoal)&&exact(mortar->goal,mortarGoal),
          "loaded paid crowd recovery rewrote an accepted formation goal");
    maximumPathSize=std::max({maximumPathSize,anvil->path.size(),mortar->path.size()});
    anvilCompleted=anvilCompleted||(anvil->order==Order::Idle&&distance(anvil->pos,anvilGoal)<=35);
    mortarCompleted=mortarCompleted||(mortar->order==Order::Idle&&distance(mortar->pos,mortarGoal)<=35);
  }
  check(simulation.tick()==initialTick+1200&&simulation.winner()==-1,
        "recorded paid crowd continuation did not finish all requested live ticks");
  check(simulation.stateHash()==resumed.stateHash(),
        "paid crowd save/load continuation diverged");
  check(anvilCompleted&&mortarCompleted&&
        simulation.find(Anvil)->order==Order::Idle&&simulation.find(Mortar)->order==Order::Idle&&
        distance(simulation.find(Anvil)->pos,anvilGoal)<=35&&
        distance(simulation.find(Mortar)->pos,mortarGoal)<=35,
        "recorded paid crowd laggards did not complete and hold their original goals");
  check(maximumPathSize<=16,
        "paid crowd recovery grew a route without a bounded navigation need");
}

} // namespace

int main() {
  const std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"dense idle workers do not permanently block produced units",denseIdleWorkersDoNotPermanentlyBlockProducedUnits},
    {"Hold fixed while Defend yields and reclaims",holdRemainsFixedWhileDefendYieldsAndReclaims},
    {"congested Gather retains its work intent",congestedGatherRetainsItsWorkIntent},
    {"opposing friendly traffic moves at most once per tick",opposingFriendlyTrafficMovesAtMostOncePerTick},
    {"recorded paid crowd completes original moves",recordedPaidCrowdCompletesOriginalMoves},
  };
  int failed=0;
  for(const auto& [name,test]:tests) {
    try {test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
  }
  std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
