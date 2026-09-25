#include "Sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
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

bool samePoint(Vec2 left,Vec2 right) {
  return left.x==right.x&&left.y==right.y;
}

bool sameCommand(const Command& left,const Command& right) {
  return left.type==right.type&&left.team==right.team&&left.units==right.units&&
         samePoint(left.point,right.point)&&left.target==right.target&&left.kind==right.kind&&
         left.queueIndex==right.queueIndex&&left.queueMode==right.queueMode&&
         left.spacing==right.spacing&&left.hasArrivalFacing==right.hasArrivalFacing&&
         left.arrivalFacing==right.arrivalFacing;
}

bool sameRecording(const std::vector<RecordedCommand>& left,
                   const std::vector<RecordedCommand>& right) {
  if(left.size()!=right.size())return false;
  for(std::size_t index=0;index<left.size();++index)
    if(left[index].tick!=right[index].tick||
       !sameCommand(left[index].command,right[index].command))return false;
  return true;
}

Entity* edit(Simulation& simulation,Id id) {
  for(auto& entity:const_cast<std::vector<Entity>&>(simulation.entities()))
    if(entity.id==id)return &entity;
  return nullptr;
}

Simulation fixture() {
  Simulation simulation;
  // This synthetic arena has no authored plateau geometry.
  simulation.reset({0,0x71ACu,false,1,MatchLength::Standard,2,0});
  const_cast<std::vector<Entity>&>(simulation.entities()).clear();
  const_cast<std::vector<Obstacle>&>(simulation.obstacles()).clear();
  simulation.debugSpawn(Kind::Headquarters,0,{700,700});
  simulation.debugSpawn(Kind::Headquarters,1,{4300,4300});
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
          "tactical-order fixture remains an active match");
  }
}

template<class Predicate>
void stepUntil(Simulation& simulation,int maximum,Predicate complete,const std::string& failure) {
  for(int index=0;index<maximum&&!complete();++index)step(simulation);
  check(complete(),failure);
}

Id first(const Simulation& simulation,int team,Kind kind,Id excluded=0) {
  for(const auto& entity:simulation.entities())
    if(entity.alive()&&entity.team==team&&entity.kind==kind&&entity.id!=excluded)return entity.id;
  return 0;
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::vector<std::string> lines;
  std::string line;
  while(std::getline(input,line))lines.push_back(line);
  return lines;
}

void writeLines(const std::filesystem::path& path,const std::vector<std::string>& lines) {
  std::ofstream output(path,std::ios::trunc);
  for(const auto& line:lines)output<<line<<'\n';
}

std::vector<std::string> fields(const std::string& row) {
  std::istringstream input(row);
  std::vector<std::string> result;
  std::string field;
  while(input>>field)result.push_back(field);
  return result;
}

std::string joined(const std::vector<std::string>& values) {
  std::ostringstream output;
  for(std::size_t index=0;index<values.size();++index)
    output<<(index?" ":"")<<values[index];
  return output.str();
}

std::vector<std::size_t> recordingRows(const std::vector<std::string>& lines) {
  const auto config=fields(lines.at(1));
  check(config.size()==6||config.size()==7,"save mutation fixture reads a legacy or revision-aware config row");
  const auto players=static_cast<std::size_t>(std::stoul(config.at(5)));
  std::size_t cursor=3+players;
  cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
  const auto entities=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
  for(std::size_t entity=0;entity<entities;++entity) {
    ++cursor;
    cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
    cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
  }
  const auto effectHeader=fields(lines.at(cursor));
  check(effectHeader.size()==2,"save mutation fixture reads the effect header");
  cursor+=1+static_cast<std::size_t>(std::stoul(effectHeader.front()))+players*2;
  const auto count=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
  std::vector<std::size_t> result;
  for(std::size_t index=0;index<count;++index)result.push_back(cursor++);
  return result;
}

void stripQueuedWorkForSaveThirteen(std::vector<std::string>& lines) {
  if(!lines.empty()&&lines.front()=="CINDERLINE 15") {
    auto config=fields(lines.at(1));check(config.size()==7&&config.back()=="0","legacy tactical migration uses flat-map revision zero");
    config.pop_back();lines[1]=joined(config);lines.front()="CINDERLINE 14";
  }
  check(!lines.empty()&&lines.front()=="CINDERLINE 14",
        "save-thirteen migration begins from current save-fourteen data");
  const auto formation=std::find(lines.begin(),lines.end(),"FORMATION_ORDERS 1");
  const auto queuedWork=std::find(lines.begin(),lines.end(),"QUEUED_WORK 1");
  check(formation!=lines.end()&&queuedWork!=lines.end()&&formation<queuedWork,
        "save-thirteen migration locates the final queued-work section");
  const auto count=static_cast<std::size_t>(std::stoul(*(queuedWork+1)));
  check(static_cast<std::size_t>(lines.end()-(queuedWork+2))==count,
        "save-fourteen queued-work section has exactly one row per saved entity");
  lines.erase(queuedWork,lines.end());
  lines.front()="CINDERLINE 13";
}

void stripFormationForSaveTwelve(std::vector<std::string>& lines) {
  if(!lines.empty()&&(lines.front()=="CINDERLINE 14"||lines.front()=="CINDERLINE 15"))stripQueuedWorkForSaveThirteen(lines);
  check(!lines.empty()&&lines.front()=="CINDERLINE 13",
        "save-twelve migration continues from save-thirteen data");
  for(std::size_t row:recordingRows(lines)) {
    auto values=fields(lines.at(row));
    check(values.size()>=13&&values.size()==13+static_cast<std::size_t>(std::stoul(values[12])),
          "save-thirteen recording row contains spacing and arrival-facing fields");
    values.erase(values.begin()+9,values.begin()+12);
    lines[row]=joined(values);
  }
  const auto sustained=std::find(lines.begin(),lines.end(),"SUSTAINED_ORDERS 1");
  const auto formation=std::find(lines.begin(),lines.end(),"FORMATION_ORDERS 1");
  check(sustained!=lines.end()&&formation!=lines.end()&&sustained<formation,
        "save-twelve migration locates the final formation section");
  const auto count=static_cast<std::size_t>(std::stoul(*(formation+1)));
  check(static_cast<std::size_t>(lines.end()-(formation+2))==count,
        "save-thirteen formation section has exactly one row per saved entity");
  lines.erase(formation,lines.end());
  lines.front()="CINDERLINE 12";
}

void downgradeCurrentToEleven(std::vector<std::string>& lines) {
  stripFormationForSaveTwelve(lines);
  const auto orders=std::find(lines.begin(),lines.end(),"ORDER_QUEUES 1");
  const auto sustained=std::find(lines.begin(),lines.end(),"SUSTAINED_ORDERS 1");
  check(orders!=lines.end()&&sustained!=lines.end()&&orders<sustained,
        "save-eleven migration locates the ordered terminal sections");
  const auto count=static_cast<std::size_t>(std::stoul(*(sustained+1)));
  check(static_cast<std::size_t>(lines.end()-(sustained+2))==count,
        "save-twelve sustained section has exactly one row per saved entity");
  lines.erase(sustained,lines.end());
  lines.front()="CINDERLINE 11";
}

std::size_t orderRow(const std::vector<std::string>& lines,std::size_t marker,Id id) {
  const auto count=static_cast<std::size_t>(std::stoul(lines.at(marker+1)));
  for(std::size_t index=0;index<count;++index) {
    const auto row=fields(lines.at(marker+2+index));
    if(!row.empty()&&row.front()==std::to_string(id))return marker+2+index;
  }
  throw std::runtime_error("save mutation fixture cannot find the requested order row");
}

std::string tailRow(Id id,Vec2 point,Id support=0) {
  std::ostringstream output;
  output<<id<<" 0 1 "<<static_cast<int>(Order::Move)<<' '
        <<point.x<<' '<<point.y<<' '<<support;
  return output.str();
}

void waypointSequencePreservesAcceptedFormation() {
  auto simulation=fixture();
  const Id striker=simulation.debugSpawn(Kind::Striker,0,{1000,1000});
  const Id lancer=simulation.debugSpawn(Kind::Lancer,0,{1000,1080});
  const std::vector<Id> group{striker,lancer};

  check(send(simulation,CommandType::Move,0,group,{1350,1050},{},Kind::Worker,
             CommandQueueMode::Append).accepted,
        "an append starts immediately for every idle recipient");
  const Vec2 firstStriker=simulation.find(striker)->goal;
  const Vec2 firstLancer=simulation.find(lancer)->goal;
  check(simulation.find(striker)->order==Order::Move&&simulation.find(lancer)->order==Order::Move&&
        simulation.find(striker)->futureOrders.empty()&&simulation.find(lancer)->futureOrders.empty(),
        "idle append installs a current order instead of leaving an Idle unit with a tail");
  check(distance(firstStriker,firstLancer)>30,
        "mixed-size group receives distinct accepted formation destinations");

  check(send(simulation,CommandType::AttackMove,0,group,{1750,1150},{},Kind::Worker,
             CommandQueueMode::Append).accepted&&
        send(simulation,CommandType::Move,0,group,{2100,1050},{},Kind::Worker,
             CommandQueueMode::Append).accepted,
        "two further formation waypoints append to the live move");
  const Vec2 secondStriker=simulation.find(striker)->futureOrders[0].point;
  const Vec2 secondLancer=simulation.find(lancer)->futureOrders[0].point;
  const Vec2 thirdStriker=simulation.find(striker)->futureOrders[1].point;
  const Vec2 thirdLancer=simulation.find(lancer)->futureOrders[1].point;
  check(distance(secondStriker,secondLancer)>30,
        "queued mixed-selection formation points are assigned per recipient");

  bool strikerSecond=false,lancerSecond=false,strikerThird=false,lancerThird=false;
  bool strikerFinished=false,lancerFinished=false;
  for(int index=0;index<1000&&!(strikerFinished&&lancerFinished);++index) {
    step(simulation);
    const Entity* strikerState=simulation.find(striker);
    const Entity* lancerState=simulation.find(lancer);
    if(!strikerSecond&&strikerState->order==Order::AttackMove&&strikerState->futureOrders.size()==1) {
      check(distance(strikerState->goal,secondStriker)<0.01f,
            "Striker activates its immutable second formation destination");
      strikerSecond=true;
    }
    if(!lancerSecond&&lancerState->order==Order::AttackMove&&lancerState->futureOrders.size()==1) {
      check(distance(lancerState->goal,secondLancer)<0.01f,
            "Lancer activates its immutable second formation destination");
      lancerSecond=true;
    }
    if(!strikerThird&&strikerState->order==Order::Move&&strikerState->futureOrders.empty()) {
      check(strikerSecond&&distance(strikerState->goal,thirdStriker)<0.01f,
            "Striker activates its immutable final destination after its second waypoint");
      strikerThird=true;
    }
    if(!lancerThird&&lancerState->order==Order::Move&&lancerState->futureOrders.empty()) {
      check(lancerSecond&&distance(lancerState->goal,thirdLancer)<0.01f,
            "Lancer activates its immutable final destination after its second waypoint");
      lancerThird=true;
    }
    if(strikerThird&&strikerState->order==Order::Idle&&distance(strikerState->pos,thirdStriker)<30)
      strikerFinished=true;
    if(lancerThird&&lancerState->order==Order::Idle&&distance(lancerState->pos,thirdLancer)<30)
      lancerFinished=true;
  }
  check(strikerSecond&&lancerSecond&&strikerThird&&lancerThird&&strikerFinished&&lancerFinished,
        "each recipient independently completes the accepted three-waypoint FIFO sequence");
}

void attackMoveEngagementPreservesWaypointAndTail() {
  auto simulation=fixture();
  const Id attacker=simulation.debugSpawn(Kind::Striker,0,{1000,1600});
  const Id incidental=simulation.debugSpawn(Kind::Worker,1,{1250,1600});
  check(send(simulation,CommandType::Hold,1,{incidental}).accepted,
        "incidental enemy can hold position");
  check(send(simulation,CommandType::AttackMove,0,{attacker},{1800,1600}).accepted&&
        send(simulation,CommandType::Move,0,{attacker},{2200,1750},{},Kind::Worker,
             CommandQueueMode::Append).accepted,
        "AttackMove with a queued successor is accepted");
  const Vec2 waypoint=simulation.find(attacker)->goal;
  const TacticalOrder tail=simulation.find(attacker)->futureOrders.front();

  stepUntil(simulation,500,[&] {
    const Entity* enemy=simulation.find(incidental);
    return !enemy||!enemy->alive();
  },"ordinary AttackMove combat did not defeat the incidental enemy");
  const Entity* afterCombat=simulation.find(attacker);
  check(afterCombat->order==Order::AttackMove&&distance(afterCombat->goal,waypoint)<0.01f&&
        afterCombat->futureOrders.size()==1&&
        distance(afterCombat->futureOrders.front().point,tail.point)<0.01f,
        "incidental engagement neither completes the waypoint nor consumes its tail");

  stepUntil(simulation,500,[&] {
    return simulation.find(attacker)->order==Order::Move&&
           simulation.find(attacker)->futureOrders.empty();
  },"AttackMove arrival did not resume and activate its saved successor");
  check(distance(simulation.find(attacker)->goal,tail.point)<0.01f,
        "post-combat successor uses its accepted destination");
}

void mixedMenderWaypointsSurviveLeaderLoss() {
  auto simulation=fixture();
  const Id leader=simulation.debugSpawn(Kind::Striker,0,{1100,2200});
  const Id mender=simulation.debugSpawn(Kind::Mender,0,{1100,2290});
  const std::vector<Id> group{leader,mender};
  check(send(simulation,CommandType::AttackMove,0,group,{2050,2250}).accepted&&
        send(simulation,CommandType::AttackMove,0,group,{2450,2400},{},Kind::Worker,
             CommandQueueMode::Append).accepted,
        "mixed combat group accepts current and queued AttackMove waypoints");
  const Vec2 currentWaypoint=simulation.find(mender)->goal;
  const Vec2 queuedWaypoint=simulation.find(mender)->futureOrders.front().point;
  check(simulation.find(mender)->order==Order::AttackMove&&
        simulation.find(mender)->supportTarget==leader&&
        simulation.find(mender)->futureOrders.front().supportTarget==leader,
        "Mender keeps waypoints while current and queued steps remember the selected leader");

  edit(simulation,leader)->hp=1;
  const Id enemy=simulation.debugSpawn(Kind::Bastion,1,{1200,2200});
  check(send(simulation,CommandType::Attack,1,{enemy},{},leader).accepted,
        "enemy receives an ordinary command against the support leader");
  stepUntil(simulation,20,[&] {
    const Entity* unit=simulation.find(leader);return !unit||!unit->alive();
  },"ordinary combat did not remove the Mender's leader");
  step(simulation);
  const Entity* survivor=simulation.find(mender);
  check(survivor&&survivor->order==Order::AttackMove&&survivor->supportTarget==0&&
        distance(survivor->goal,currentWaypoint)<0.01f,
        "leader loss clears the current relation and resumes the saved waypoint");
  check(survivor->futureOrders.size()==1&&survivor->futureOrders.front().supportTarget==0&&
        distance(survivor->futureOrders.front().point,queuedWaypoint)<0.01f,
        "leader loss clears queued relations without discarding queued waypoints");

  auto direct=fixture();
  const Id directLeader=direct.debugSpawn(Kind::Striker,0,{1000,2700});
  const Id directMender=direct.debugSpawn(Kind::Mender,0,{1000,2780});
  const Id directEnemy=direct.debugSpawn(Kind::Worker,1,{1250,2700});
  check(send(direct,CommandType::Attack,0,{directLeader,directMender},{},directEnemy).accepted,
        "mixed direct Attack is accepted");
  check(direct.find(directMender)->order==Order::Attack&&
        direct.find(directMender)->target==directLeader&&
        direct.find(directMender)->futureOrders.empty(),
        "direct Attack retains separate follow semantics rather than inventing a waypoint");
}

void wideFormationMenderCompletesOwnWaypointBeforeTail() {
  auto simulation=fixture();
  std::vector<Id> group;
  for(int index=0;index<15;++index) {
    const Vec2 position{850.0f+64.0f*static_cast<float>(index%4),
                        3250.0f+64.0f*static_cast<float>(index/4)};
    group.push_back(simulation.debugSpawn(Kind::Striker,0,position));
  }
  const Id leader=group.front();
  const Id mender=simulation.debugSpawn(Kind::Mender,0,{850.0f+3*64.0f,3250.0f+3*64.0f});
  group.push_back(mender);
  check(send(simulation,CommandType::AttackMove,0,group,{1700,3300}).accepted,
        "sixteen-unit mixed formation accepts a wide AttackMove");
  const Vec2 leaderDestination=simulation.find(leader)->goal;
  const Vec2 menderDestination=simulation.find(mender)->goal;
  check(simulation.find(mender)->supportTarget==leader&&
        distance(leaderDestination,menderDestination)>250,
        "last-ID Mender keeps its own wide formation slot and the first combat leader relation");

  check(send(simulation,CommandType::Move,0,{mender},{2250,3650},0,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "wide-formation Mender accepts an explicit successor of its own");
  const Vec2 tailDestination=simulation.find(mender)->futureOrders.front().point;
  bool activatedTail=false;
  for(int index=0;index<900&&!activatedTail;++index) {
    step(simulation);
    const Entity* unit=simulation.find(mender);
    if(unit->order==Order::Move&&unit->futureOrders.empty()) {
      check(distance(unit->pos,menderDestination)<30,
            "Mender reaches its own accepted AttackMove slot before activating the tail");
      check(distance(unit->goal,tailDestination)<0.01f,
            "wide-formation completion activates the unchanged explicit tail");
      activatedTail=true;
    }
  }
  check(activatedTail,
        "leader support distance does not stall the Mender before its own formation waypoint");
  check(simulation.find(leader)->order==Order::Idle&&
        distance(simulation.find(leader)->goal,leaderDestination)<0.01f,
        "first combat leader remains at its separately accepted formation slot");
  stepUntil(simulation,500,[&] {
    const Entity* unit=simulation.find(mender);
    return unit->order==Order::Idle&&distance(unit->pos,tailDestination)<30;
  },"wide-formation Mender did not complete its explicit tail");
}

void holdClearStopAndReplacementControlTheTail() {
  auto simulation=fixture();
  const Id unit=simulation.debugSpawn(Kind::Striker,0,{900,3100});
  check(send(simulation,CommandType::Hold,0,{unit}).accepted&&
        send(simulation,CommandType::Move,0,{unit},{1400,3100},{},Kind::Worker,
             CommandQueueMode::Append).accepted,
        "indefinite Hold retains a queued destination");
  const Vec2 anchor=simulation.find(unit)->pos;
  step(simulation,20);
  check(simulation.find(unit)->order==Order::Hold&&
        simulation.find(unit)->futureOrders.size()==1&&
        distance(simulation.find(unit)->pos,anchor)<0.01f,
        "Hold does not prematurely activate its tail");

  check(send(simulation,CommandType::ClearOrders,0,{unit}).accepted,
        "ClearOrders accepts a held unit");
  check(simulation.find(unit)->order==Order::Hold&&simulation.find(unit)->futureOrders.empty(),
        "ClearOrders removes only future orders");
  check(send(simulation,CommandType::AttackMove,0,{unit},{1500,3200},{},Kind::Worker,
             CommandQueueMode::Append).accepted&&
        send(simulation,CommandType::Stop,0,{unit}).accepted,
        "Stop accepts a held unit with a fresh tail");
  check(simulation.find(unit)->order==Order::Idle&&simulation.find(unit)->futureOrders.empty(),
        "Stop clears current and future orders");

  check(send(simulation,CommandType::Hold,0,{unit}).accepted&&
        send(simulation,CommandType::Move,0,{unit},{1600,3200},{},Kind::Worker,
             CommandQueueMode::Append).accepted&&
        send(simulation,CommandType::AttackMove,0,{unit},{1700,3300}).accepted,
        "ordinary replacement follows a queued Hold");
  check(simulation.find(unit)->order==Order::AttackMove&&simulation.find(unit)->futureOrders.empty(),
        "replacing tactical command discards the old tail");
}

void invalidModesAndGroupLimitRejectAtomically() {
  auto simulation=fixture();
  const Id full=simulation.debugSpawn(Kind::Striker,0,{900,3550});
  const Id room=simulation.debugSpawn(Kind::Striker,0,{900,3650});
  check(send(simulation,CommandType::Hold,0,{full,room}).accepted,
        "queue-limit recipients begin on an indefinite order");

  auto rejectUnchanged=[&](const Command& command,const std::string& reason) {
    const auto hash=simulation.stateHash();
    const auto recorded=simulation.recording().size();
    check(!simulation.command(command).accepted,reason+" is rejected");
    check(simulation.stateHash()==hash&&simulation.recording().size()==recorded,
          reason+" leaves authoritative state and recording unchanged");
  };
  rejectUnchanged({CommandType::Hold,0,{full},{},0,Kind::Worker,0,CommandQueueMode::Append},
                  "Append on a non-tactical command");
  rejectUnchanged({CommandType::Move,0,{full},{1200,3550},0,Kind::Worker,0,
                   static_cast<CommandQueueMode>(99)},"unknown queue mode");

  for(std::size_t index=0;index<Simulation::MaxFutureOrders;++index) {
    const Vec2 point{1200.0f+20.0f*static_cast<float>(index),3550};
    check(send(simulation,CommandType::Move,0,{full},point,0,Kind::Worker,
               CommandQueueMode::Append).accepted,"full recipient accepts waypoint within its limit");
    if(index+1<Simulation::MaxFutureOrders)
      check(send(simulation,CommandType::Move,0,{room},{point.x,3650},0,Kind::Worker,
                 CommandQueueMode::Append).accepted,"second recipient retains one available slot");
  }
  check(simulation.find(full)->futureOrders.size()==Simulation::MaxFutureOrders&&
        simulation.find(room)->futureOrders.size()==Simulation::MaxFutureOrders-1,
        "fixture reaches the exact per-unit boundary");
  const auto hash=simulation.stateHash();
  const auto recorded=simulation.recording().size();
  const Vec2 lastRoom=simulation.find(room)->futureOrders.back().point;
  const CommandResult overflow=send(simulation,CommandType::AttackMove,0,{full,room},{1900,3600},0,
                                    Kind::Worker,CommandQueueMode::Append);
  check(!overflow.accepted,"group append rejects when one recipient is already full");
  check(simulation.stateHash()==hash&&simulation.recording().size()==recorded&&
        simulation.find(full)->futureOrders.size()==Simulation::MaxFutureOrders&&
        simulation.find(room)->futureOrders.size()==Simulation::MaxFutureOrders-1&&
        distance(simulation.find(room)->futureOrders.back().point,lastRoom)<0.01f,
        "group preflight prevents a partial append to recipients with room");
}

void constructionCompletionCancellationAndReplacementRespectTail() {
  auto completion=fixture();
  const Id worker=completion.debugSpawn(Kind::Worker,0,{1050,950});
  const Vec2 site{1400,950};
  const Vec2 successor{1800,1100};
  check(send(completion,CommandType::Build,0,{worker},site,0,Kind::Foundry).accepted,
        "worker begins ordinary paid construction");
  const Id foundation=first(completion,0,Kind::Foundry);
  check(send(completion,CommandType::Move,0,{worker},successor,0,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "constructing worker accepts an explicit successor");
  check(completion.find(worker)->order==Order::Construct&&
        completion.constructionWorker(foundation)==worker&&
        completion.find(worker)->futureOrders.size()==1,
        "append leaves construction ownership and work intact");
  stepUntil(completion,4000,[&] {
    return completion.find(foundation)->progress>=1&&
           completion.find(worker)->order==Order::Move;
  },"construction completion did not activate its explicit successor");
  check(completion.find(worker)->futureOrders.empty()&&
        distance(completion.find(worker)->goal,successor)<0.01f,
        "completed construction chooses the explicit successor over mining recovery");

  auto cancelled=fixture();
  const Id cancelledWorker=cancelled.debugSpawn(Kind::Worker,0,{1050,1450});
  check(send(cancelled,CommandType::Build,0,{cancelledWorker},{1400,1450},0,Kind::Foundry).accepted,
        "cancellation fixture begins construction");
  const Id cancelledFoundation=first(cancelled,0,Kind::Foundry);
  const Vec2 cancellationSuccessor{1800,1450};
  check(send(cancelled,CommandType::Move,0,{cancelledWorker},cancellationSuccessor,0,
             Kind::Worker,CommandQueueMode::Append).accepted&&
        send(cancelled,CommandType::CancelBuilding,0,{cancelledFoundation}).accepted,
        "owner cancels a foundation with an explicit worker successor");
  check(cancelled.find(cancelledWorker)->order==Order::Move&&
        cancelled.find(cancelledWorker)->futureOrders.empty()&&
        distance(cancelled.find(cancelledWorker)->goal,cancellationSuccessor)<0.01f,
        "foundation cancellation activates exactly one surviving worker successor");

  auto replaced=fixture();
  const Id replacedWorker=replaced.debugSpawn(Kind::Worker,0,{1050,1900});
  check(send(replaced,CommandType::Build,0,{replacedWorker},{1400,1900},0,Kind::Foundry).accepted,
        "replacement fixture begins construction");
  const Id replacedFoundation=first(replaced,0,Kind::Foundry);
  check(send(replaced,CommandType::Move,0,{replacedWorker},{1800,1900},0,Kind::Worker,
             CommandQueueMode::Append).accepted&&
        send(replaced,CommandType::Move,0,{replacedWorker},{1050,2250}).accepted,
        "constructing worker receives a replacing command after a queued successor");
  check(replaced.find(replacedWorker)->order==Order::Move&&
        replaced.find(replacedWorker)->futureOrders.empty()&&
        replaced.constructionWorker(replacedFoundation)==0&&
        distance(replaced.find(replacedWorker)->goal,{1050,2250})<0.01f,
        "replacement clears the old successor before construction cleanup");
}

void automaticBuildSkipsExplicitPlans() {
  auto simulation=fixture();
  const Id planned=simulation.debugSpawn(Kind::Worker,0,{1050,2700});
  const Id available=simulation.debugSpawn(Kind::Worker,0,{1150,2700});
  check(send(simulation,CommandType::Hold,0,{planned}).accepted&&
        send(simulation,CommandType::Move,0,{planned},{1800,2700},0,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "worker receives an explicit future plan");
  const Vec2 site{1450,2700};
  check(send(simulation,CommandType::AutoBuild,0,{},site,0,Kind::Foundry).accepted,
        "automatic construction finds an unplanned worker");
  const Id foundation=first(simulation,0,Kind::Foundry);
  check(simulation.constructionWorker(foundation)==available&&
        simulation.find(planned)->order==Order::Hold&&
        simulation.find(planned)->futureOrders.size()==1,
        "automatic allocation skips and preserves the explicitly planned worker");

  auto none=fixture();
  const Id only=none.debugSpawn(Kind::Worker,0,{1100,3200});
  check(send(none,CommandType::Hold,0,{only}).accepted&&
        send(none,CommandType::Move,0,{only},{1700,3200},0,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "only automatic-build candidate receives a plan");
  const auto hash=none.stateHash();
  const auto recorded=none.recording().size();
  check(!send(none,CommandType::AutoBuild,0,{},{1450,3200},0,Kind::Foundry).accepted&&
        none.stateHash()==hash&&none.recording().size()==recorded,
        "automatic construction rejects atomically when every candidate has an explicit plan");
}

void depletedGatherWaitsForDeliveryAndDepotRecovery() {
  auto simulation=fixture();
  const Id depot=first(simulation,0,Kind::Headquarters);
  const Id worker=simulation.debugSpawn(Kind::Worker,0,{1050,700});
  const Id ore=simulation.debugSpawn(Kind::Resource,-1,{1160,700});
  edit(simulation,ore)->resource=3;
  const Vec2 successor{1550,850};
  check(send(simulation,CommandType::Gather,0,{worker},{},ore).accepted&&
        send(simulation,CommandType::Move,0,{worker},successor,0,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "depleting Gather accepts an explicit successor");
  stepUntil(simulation,200,[&] {
    const Entity* entity=simulation.find(worker);
    return entity->returning&&entity->carried==3;
  },"worker did not harvest the deposit's final cargo");

  edit(simulation,depot)->progress=0.5f;
  const Vec2 waitingPosition=simulation.find(worker)->pos;
  step(simulation,20);
  const Entity* waiting=simulation.find(worker);
  check(waiting->order==Order::Gather&&waiting->returning&&waiting->carried==3&&
        waiting->futureOrders.size()==1&&waiting->navigationExhausted&&
        distance(waiting->pos,waitingPosition)<0.01f,
        "missing operational depot preserves final cargo, Gather, and explicit tail");

  edit(simulation,depot)->progress=1;
  stepUntil(simulation,400,[&] {
    const Entity* entity=simulation.find(worker);
    return entity->order==Order::Move&&entity->carried==0;
  },"restored depot did not receive final cargo before activating the successor");
  check(simulation.players()[0].stats.gathered==3&&
        simulation.find(worker)->futureOrders.empty()&&
        distance(simulation.find(worker)->goal,successor)<0.01f,
        "final delivery is credited once and wins over mining retargeting");
}

void saveLoadContinuationAndReplayPreserveQueueMode() {
  Config config{0,0xA117u,false,1};
  Simulation original;
  original.reset(config);
  Id worker=0;
  for(const auto& entity:original.entities())
    if(entity.alive()&&entity.team==0&&entity.kind==Kind::Worker){worker=entity.id;break;}
  check(worker!=0,"replay fixture finds its deterministic starting worker");
  check(send(original,CommandType::Move,0,{worker},{1050,900}).accepted&&
        send(original,CommandType::AttackMove,0,{worker},{1400,1050},0,Kind::Worker,
             CommandQueueMode::Append).accepted&&
        send(original,CommandType::Move,0,{worker},{1700,900},0,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "recording fixture accepts a three-step tactical sequence");
  check(original.recording().size()==3&&
        original.recording()[1].command.queueMode==CommandQueueMode::Append&&
        original.recording()[2].command.queueMode==CommandQueueMode::Append,
        "recording retains the queue mode of appended commands");
  step(original,35);

  const auto path=std::filesystem::temp_directory_path()/"cinderline-tactical-orders-v14.sav";
  check(original.save(path.string()),"live tactical sequence saves");
  Simulation loaded;
  check(loaded.load(path.string()),"live tactical sequence loads");
  std::filesystem::remove(path);
  check(loaded.stateHash()==original.stateHash()&&
        loaded.recording().size()==original.recording().size()&&
        loaded.find(worker)->futureOrders.size()==original.find(worker)->futureOrders.size(),
        "save-load preserves current order, full tail, hash, and recording");
  for(int index=0;index<300;++index) {
    step(original);step(loaded);
    check(loaded.stateHash()==original.stateHash(),
          "loaded tactical sequence continues deterministically");
  }

  Simulation replay;
  replay.reset(config);
  const auto recording=original.recording();
  std::size_t next=0;
  while(replay.tick()<original.tick()) {
    while(next<recording.size()&&recording[next].tick==replay.tick()) {
      check(replay.command(recording[next].command).accepted,
            "recorded tactical command replays with its original queue mode");
      ++next;
    }
    step(replay);
  }
  check(next==recording.size()&&replay.stateHash()==original.stateHash(),
        "queue-mode recording reproduces the full authoritative continuation");
}

void savesElevenThroughThirteenMigratePreservingTacticalState() {
  auto source=fixture();
  const Id leader=source.debugSpawn(Kind::Striker,0,{1000,3600});
  const Id mender=source.debugSpawn(Kind::Mender,0,{1000,3690});
  check(send(source,CommandType::AttackMove,0,{leader,mender},{1450,3650}).accepted&&
        send(source,CommandType::AttackMove,0,{leader,mender},{1800,3800},0,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "save-eleven migration fixture creates current and future P1.1 support state");
  const Entity* sourceMender=source.find(mender);
  check(sourceMender&&sourceMender->supportTarget==leader&&
        sourceMender->futureOrders.size()==1&&sourceMender->futureOrders.front().supportTarget==leader,
        "save-eleven migration source contains nonempty support and tactical tail data");

  const auto path=std::filesystem::temp_directory_path()/
    "cinderline-tactical-orders-migration-v11.sav";
  check(source.save(path.string()),"save-eleven migration fixture first writes save-fourteen data");
  const auto current=readLines(path);

  auto thirteen=current;
  stripQueuedWorkForSaveThirteen(thirteen);
  writeLines(path,thirteen);
  Simulation migratedThirteen;
  check(migratedThirteen.load(path.string())&&
        migratedThirteen.stateHash()==source.stateHash()&&
        sameRecording(migratedThirteen.recording(),source.recording()),
        "save-thirteen migration defaults queued building kinds without changing tactical state");

  auto twelve=current;
  stripFormationForSaveTwelve(twelve);
  writeLines(path,twelve);
  Simulation migratedTwelve;
  check(migratedTwelve.load(path.string())&&
        migratedTwelve.stateHash()==source.stateHash()&&
        sameRecording(migratedTwelve.recording(),source.recording()),
        "save-twelve migration defaults formation and queued-work fields without changing state");

  auto legacy=current;
  downgradeCurrentToEleven(legacy);
  check(std::find(legacy.begin(),legacy.end(),"ORDER_QUEUES 1")!=legacy.end()&&
        std::find(legacy.begin(),legacy.end(),"SUSTAINED_ORDERS 1")==legacy.end(),
        "save-eleven fixture retains tactical queues and removes only sustained orders");
  writeLines(path,legacy);

  Simulation migrated;
  check(migrated.load(path.string()),"representative save-eleven data loads through its migration path");
  std::filesystem::remove(path);
  const Entity* migratedMender=migrated.find(mender);
  check(migratedMender&&migratedMender->supportTarget==leader&&
        migratedMender->futureOrders.size()==1&&
        migratedMender->futureOrders.front().supportTarget==leader,
        "save-eleven migration preserves current and future P1.1 support relations");
  check(migrated.stateHash()==source.stateHash()&&
        sameRecording(migrated.recording(),source.recording()),
        "save-eleven migration preserves P1.1 state and queue-mode recording exactly");
}

void saveTenMigratesWithoutInventingTacticalState() {
  Config config{2,0x10A7u,false,1,MatchLength::Long};
  config.playerCount=2;
  config.mapRevision=0; // Source represents a pre-authored save-ten match.
  Simulation source;
  source.reset(config);
  const Id worker=first(source,0,Kind::Worker);
  const Id headquarters=first(source,0,Kind::Headquarters);
  const Vec2 moveGoal{1500,1250};
  const Vec2 rallyGoal{1200,900};
  check(send(source,CommandType::Move,0,{worker},moveGoal).accepted&&
        send(source,CommandType::Rally,0,{headquarters},rallyGoal).accepted&&
        send(source,CommandType::Train,0,{headquarters},{},0,Kind::Worker).accepted,
        "save-ten migration fixture records only legacy-compatible replacing commands");
  const Entity* sourceProducer=source.find(headquarters);
  check(source.find(worker)->order==Order::Move&&samePoint(source.find(worker)->goal,moveGoal)&&
        sourceProducer->rallyOverride&&samePoint(sourceProducer->rally,rallyGoal)&&
        sourceProducer->queue.size()==1,
        "legacy fixture has live movement, explicit rally, and a paid production job");
  const int savedOre=source.players()[0].ore;
  const Id savedJob=sourceProducer->queue.front().id;
  const Id savedNextJob=sourceProducer->nextQueueId;

  const auto versionTenPath=std::filesystem::temp_directory_path()/
    "cinderline-tactical-orders-migration-v10.sav";
  check(source.save(versionTenPath.string()),"migration fixture first writes current save-fourteen data");
  auto legacy=readLines(versionTenPath);
  downgradeCurrentToEleven(legacy);
  const auto rows=recordingRows(legacy);
  check(rows.size()==source.recording().size(),
        "migration fixture locates every save-eleven recording row");
  for(std::size_t row:rows) {
    auto values=fields(legacy.at(row));
    check(values.size()>=10&&
          values.size()==10+static_cast<std::size_t>(std::stoul(values[9]))&&values[8]=="0",
          "legacy-compatible recording begins with an explicit Replace mode");
    values.erase(values.begin()+8);
    legacy[row]=joined(values);
  }
  const auto orders=std::find(legacy.begin(),legacy.end(),"ORDER_QUEUES 1");
  check(orders!=legacy.end(),"save-eleven intermediate has the tactical tail removed for save ten");
  legacy.erase(orders,legacy.end());
  legacy.front()="CINDERLINE 10";
  writeLines(versionTenPath,legacy);

  Simulation migrated;
  check(migrated.load(versionTenPath.string()),
        "representative save-ten data loads through the explicit migration path");
  std::filesystem::remove(versionTenPath);
  const Entity* migratedWorker=migrated.find(worker);
  const Entity* migratedProducer=migrated.find(headquarters);
  check(migrated.config().matchLength==MatchLength::Long&&migrated.playerCount()==2,
        "save-ten migration preserves match length and player count");
  check(migratedWorker&&migratedWorker->order==Order::Move&&
        samePoint(migratedWorker->goal,moveGoal),
        "save-ten migration preserves the current movement order and goal");
  check(migrated.players()[0].ore==savedOre&&migratedProducer&&
        migratedProducer->rallyOverride&&samePoint(migratedProducer->rally,rallyGoal)&&
        migratedProducer->queue.size()==1&&migratedProducer->queue.front().id==savedJob&&
        migratedProducer->nextQueueId==savedNextJob,
        "save-ten migration preserves paid economy, production job identity, and rally state");
  check(migrated.recording().size()==source.recording().size()&&
        std::all_of(migrated.recording().begin(),migrated.recording().end(),
                    [](const RecordedCommand& item) {
                      return item.command.queueMode==CommandQueueMode::Replace;
                    }),
        "save-ten recordings migrate with Replace mode and retain their command count");
  check(std::all_of(migrated.entities().begin(),migrated.entities().end(),[](const Entity& entity) {
          return entity.supportTarget==0&&entity.futureOrders.empty();
        }),"save-ten migration starts every entity without invented support or tactical tails");
  check(migrated.stateHash()==source.stateHash(),
        "save-ten migration reproduces the equivalent current authoritative state");

  const auto currentPath=std::filesystem::temp_directory_path()/
    "cinderline-tactical-orders-migrated-v14.sav";
  check(migrated.save(currentPath.string()),"migrated save-ten state writes as save fourteen");
  Simulation reloaded;
  check(reloaded.load(currentPath.string()),"migrated save-fourteen state reloads");
  std::filesystem::remove(currentPath);
  check(reloaded.stateHash()==migrated.stateHash()&&
        sameRecording(reloaded.recording(),migrated.recording()),
        "migrated state and Replace-mode recording remain stable through save-fourteen reload");
}

void saveFourteenRejectsCorruptOrderQueuesAtomically() {
  auto source=fixture();
  const Id leader=source.debugSpawn(Kind::Striker,0,{1000,3800});
  const Id mender=source.debugSpawn(Kind::Mender,0,{1000,3890});
  const Id idle=source.debugSpawn(Kind::Striker,0,{800,4100});
  const Id dead=source.debugSpawn(Kind::Worker,0,{900,4200});
  const Id ownBuilding=first(source,0,Kind::Headquarters);
  const Id enemyBuilding=first(source,1,Kind::Headquarters);
  check(send(source,CommandType::AttackMove,0,{leader,mender},{1450,3850}).accepted&&
        send(source,CommandType::AttackMove,0,{leader,mender},{1800,4000},0,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "corruption fixture saves valid current support and future order rows");
  edit(source,dead)->hp=0;

  const auto path=std::filesystem::temp_directory_path()/"cinderline-tactical-orders-corrupt-v14.sav";
  check(source.save(path.string()),"save-fourteen corruption fixture writes a valid baseline");
  const auto valid=readLines(path);
  const auto marker=std::find(valid.begin(),valid.end(),"ORDER_QUEUES 1");
  const auto sustained=std::find(valid.begin(),valid.end(),"SUSTAINED_ORDERS 1");
  const auto queuedWork=std::find(valid.begin(),valid.end(),"QUEUED_WORK 1");
  check(valid.front()=="CINDERLINE 15"&&marker!=valid.end()&&sustained!=valid.end()&&
        queuedWork!=valid.end()&&marker+2<sustained&&marker<sustained&&sustained<queuedWork,
        "save-fourteen baseline has ordered tactical, sustained, and queued-work sections");
  const std::size_t markerIndex=static_cast<std::size_t>(marker-valid.begin());
  const auto records=recordingRows(valid);
  check(records.size()==2,"save-fourteen baseline has queue-mode recording rows to mutate");
  Simulation baseline;
  check(baseline.load(path.string())&&baseline.stateHash()==source.stateHash(),
        "unmodified save-fourteen tactical baseline loads before corruption tests");

  auto rejects=[&](std::vector<std::string> lines,const std::string& message) {
    writeLines(path,lines);
    Simulation untouched;
    untouched.reset({1,0xBAD5u,false,1});
    Id worker=0;
    for(const auto& entity:untouched.entities())
      if(entity.alive()&&entity.team==0&&entity.kind==Kind::Worker){worker=entity.id;break;}
    check(worker&&send(untouched,CommandType::Hold,0,{worker}).accepted,
          "atomic rejection fixture begins with recorded authoritative state");
    const auto before=untouched.stateHash();
    const auto beforeRecording=untouched.recording().size();
    const auto beforeEntities=untouched.entities().size();
    check(!untouched.load(path.string()),message+" is rejected");
    check(untouched.stateHash()==before&&untouched.recording().size()==beforeRecording&&
          untouched.entities().size()==beforeEntities,
          message+" leaves the destination simulation unchanged");
  };

  auto unknownRow=valid;
  auto unknownFields=fields(unknownRow.at(markerIndex+2));
  unknownFields[0]="999999";
  unknownRow[markerIndex+2]=joined(unknownFields);
  rejects(unknownRow,"unknown ORDER_QUEUES entity row");

  auto duplicateRow=valid;
  duplicateRow[markerIndex+3]=duplicateRow[markerIndex+2];
  rejects(duplicateRow,"duplicate ORDER_QUEUES entity row");

  auto missingRow=valid;
  const auto orderCount=static_cast<std::size_t>(std::stoul(*(marker+1)));
  missingRow.erase(missingRow.begin()+static_cast<std::ptrdiff_t>(markerIndex+1+orderCount));
  rejects(missingRow,"missing ORDER_QUEUES entity row");

  auto invalidCurrentSupport=valid;
  const std::size_t menderRow=orderRow(invalidCurrentSupport,markerIndex,mender);
  auto currentFields=fields(invalidCurrentSupport[menderRow]);
  check(currentFields.size()==7&&currentFields[2]=="1",
        "support mutation fixture finds one current and one queued Mender relation");
  currentFields[1]=std::to_string(enemyBuilding);
  invalidCurrentSupport[menderRow]=joined(currentFields);
  rejects(invalidCurrentSupport,"hostile building as current Mender support");

  auto invalidFutureSupport=valid;
  auto futureFields=fields(invalidFutureSupport[menderRow]);
  futureFields[6]=std::to_string(enemyBuilding);
  invalidFutureSupport[menderRow]=joined(futureFields);
  rejects(invalidFutureSupport,"hostile building as future Mender support");

  auto idleTail=valid;
  idleTail[orderRow(idleTail,markerIndex,idle)]=tailRow(idle,{1200,4100});
  rejects(idleTail,"Idle unit with a future tail");

  auto deadTail=valid;
  deadTail[orderRow(deadTail,markerIndex,dead)]=tailRow(dead,{1250,4200});
  rejects(deadTail,"dead unit with a future tail");

  auto buildingTail=valid;
  buildingTail[orderRow(buildingTail,markerIndex,ownBuilding)]=tailRow(ownBuilding,{900,700});
  rejects(buildingTail,"building with a future tail");

  auto invalidMode=valid;
  auto invalidModeFields=fields(invalidMode[records.front()]);
  check(invalidModeFields.size()>=13&&
        invalidModeFields.size()==13+static_cast<std::size_t>(std::stoul(invalidModeFields[12])),
        "recording mutation fixture finds queue mode before its unit identities");
  invalidModeFields[8]="2";
  invalidMode[records.front()]=joined(invalidModeFields);
  rejects(invalidMode,"recording with an invalid queue mode");

  auto truncatedMode=valid;
  auto truncatedFields=fields(truncatedMode[records.front()]);
  truncatedFields.resize(8);
  truncatedMode[records.front()]=joined(truncatedFields);
  truncatedMode.resize(records.front()+1);
  rejects(truncatedMode,"recording truncated before its queue mode");

  std::filesystem::remove(path);
}

} // namespace

int main() {
  const std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"waypoint sequence and accepted formation",waypointSequencePreservesAcceptedFormation},
    {"AttackMove incidental engagement",attackMoveEngagementPreservesWaypointAndTail},
    {"mixed Mender waypoint and leader loss",mixedMenderWaypointsSurviveLeaderLoss},
    {"wide formation Mender waypoint completion",wideFormationMenderCompletesOwnWaypointBeforeTail},
    {"Hold ClearOrders Stop and replacement",holdClearStopAndReplacementControlTheTail},
    {"invalid modes and atomic group limit",invalidModesAndGroupLimitRejectAtomically},
    {"construction completion cancellation and replacement",constructionCompletionCancellationAndReplacementRespectTail},
    {"automatic build excludes explicit plans",automaticBuildSkipsExplicitPlans},
    {"depleted Gather and missing depot recovery",depletedGatherWaitsForDeliveryAndDepotRecovery},
    {"save-load continuation and replay",saveLoadContinuationAndReplayPreserveQueueMode},
    {"save-eleven through thirteen tactical migration",savesElevenThroughThirteenMigratePreservingTacticalState},
    {"save-ten tactical migration",saveTenMigratesWithoutInventingTacticalState},
    {"save-fourteen tactical corruption rejection",saveFourteenRejectsCorruptOrderQueuesAtomically},
  };
  int failed=0;
  for(const auto& [name,test]:tests) {
    try {test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
  }
  std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
