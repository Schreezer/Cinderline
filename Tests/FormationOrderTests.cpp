#include "Sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace cinder;

namespace {

constexpr float Pi=3.14159265358979323846f;

void check(bool condition,const std::string& message) {
  if(!condition)throw std::runtime_error(message);
}

float distance(Vec2 left,Vec2 right) {
  return std::hypot(left.x-right.x,left.y-right.y);
}

float segmentDistance(Vec2 from,Vec2 to,Vec2 point) {
  const float dx=to.x-from.x,dy=to.y-from.y;
  const float lengthSquared=dx*dx+dy*dy;
  const float projection=lengthSquared>0?
    std::clamp(((point.x-from.x)*dx+(point.y-from.y)*dy)/lengthSquared,0.0f,1.0f):0.0f;
  return distance({from.x+projection*dx,from.y+projection*dy},point);
}

bool samePoint(Vec2 left,Vec2 right,float tolerance=0.001f) {
  return distance(left,right)<=tolerance;
}

float angleDistance(float left,float right) {
  return std::fabs(std::remainder(left-right,2.0f*Pi));
}

bool sameCommand(const Command& left,const Command& right) {
  return left.type==right.type&&left.team==right.team&&left.units==right.units&&
         samePoint(left.point,right.point)&&left.target==right.target&&left.kind==right.kind&&
         left.queueIndex==right.queueIndex&&left.queueMode==right.queueMode&&
         left.spacing==right.spacing&&left.hasArrivalFacing==right.hasArrivalFacing&&
         left.arrivalFacing==right.arrivalFacing&&
         std::signbit(left.arrivalFacing)==std::signbit(right.arrivalFacing);
}

bool sameRecording(const std::vector<RecordedCommand>& left,
                   const std::vector<RecordedCommand>& right) {
  if(left.size()!=right.size())return false;
  for(std::size_t index=0;index<left.size();++index)
    if(left[index].tick!=right[index].tick||
       !sameCommand(left[index].command,right[index].command))return false;
  return true;
}

std::vector<Obstacle>& editObstacles(Simulation& simulation) {
  return const_cast<std::vector<Obstacle>&>(simulation.obstacles());
}

Simulation fixture() {
  Simulation simulation;
  // Formation fixtures supply synthetic flat obstacles, independently of maps.
  simulation.reset({0,0xF013u,false,1,MatchLength::Standard,2,0});
  const_cast<std::vector<Entity>&>(simulation.entities()).clear();
  editObstacles(simulation).clear();
  simulation.debugSpawn(Kind::Headquarters,0,{700,700});
  simulation.debugSpawn(Kind::Headquarters,1,{4300,4300});
  simulation.debugResources(0,10000);
  return simulation;
}

Command makeCommand(CommandType type,int team,std::vector<Id> units,Vec2 point={},Id target=0,
                    Kind kind=Kind::Worker,CommandQueueMode mode=CommandQueueMode::Replace,
                    FormationSpacing spacing=FormationSpacing::Standard,
                    bool hasFacing=false,float facing=0) {
  Command command;
  command.type=type;command.team=team;command.units=std::move(units);command.point=point;
  command.target=target;command.kind=kind;command.queueMode=mode;command.spacing=spacing;
  command.hasArrivalFacing=hasFacing;command.arrivalFacing=facing;
  return command;
}

CommandResult send(Simulation& simulation,CommandType type,int team,std::vector<Id> units,
                   Vec2 point={},Id target=0,Kind kind=Kind::Worker,
                   CommandQueueMode mode=CommandQueueMode::Replace,
                   FormationSpacing spacing=FormationSpacing::Standard,
                   bool hasFacing=false,float facing=0) {
  return simulation.command(makeCommand(type,team,std::move(units),point,target,kind,mode,
                                        spacing,hasFacing,facing));
}

void step(Simulation& simulation,int count=1) {
  for(int index=0;index<count;++index) {
    const auto tick=simulation.tick();
    simulation.update(Simulation::Step);
    check(simulation.tick()==tick+1&&simulation.winner()==-1,
          "formation fixture remains an active match");
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

std::filesystem::path fixturePath(const std::string& name) {
  const auto besideSource=std::filesystem::path(__FILE__).parent_path()/"Fixtures"/name;
  if(std::filesystem::exists(besideSource))return besideSource;
  for(std::filesystem::path directory=std::filesystem::current_path();!directory.empty();) {
    const auto candidate=directory/"Tests"/"Fixtures"/name;
    if(std::filesystem::exists(candidate))return candidate;
    const auto parent=directory.parent_path();
    if(parent==directory)break;
    directory=parent;
  }
  throw std::runtime_error("cannot locate formation fixture "+name);
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
  std::ifstream input(path);std::vector<std::string> lines;std::string line;
  while(std::getline(input,line))lines.push_back(line);
  return lines;
}

void writeLines(const std::filesystem::path& path,const std::vector<std::string>& lines) {
  std::ofstream output(path,std::ios::trunc);
  for(const auto& line:lines)output<<line<<'\n';
}

std::vector<std::string> fields(const std::string& row) {
  std::istringstream input(row);std::vector<std::string> result;std::string value;
  while(input>>value)result.push_back(value);
  return result;
}

std::string joined(const std::vector<std::string>& values) {
  std::ostringstream output;
  for(std::size_t index=0;index<values.size();++index)output<<(index?" ":"")<<values[index];
  return output.str();
}

std::vector<std::size_t> recordingRows(const std::vector<std::string>& lines) {
  const auto config=fields(lines.at(1));
  check(config.size()==6||config.size()==7,"formation save helper reads a legacy or revision-aware config row");
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
  check(effectHeader.size()==2,"formation save helper reads the effect header");
  cursor+=1+static_cast<std::size_t>(std::stoul(effectHeader.front()))+players*2;
  const auto count=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
  std::vector<std::size_t> result;
  for(std::size_t index=0;index<count;++index)result.push_back(cursor++);
  return result;
}

std::size_t sectionRow(const std::vector<std::string>& lines,std::size_t marker,Id id) {
  const auto count=static_cast<std::size_t>(std::stoul(lines.at(marker+1)));
  for(std::size_t index=0;index<count;++index) {
    const auto values=fields(lines.at(marker+2+index));
    if(!values.empty()&&values.front()==std::to_string(id))return marker+2+index;
  }
  throw std::runtime_error("formation save helper cannot find entity row");
}

Vec2 centroid(const Simulation& simulation,const std::vector<Id>& units) {
  Vec2 result{};
  for(Id id:units) {const Entity* entity=simulation.find(id);result.x+=entity->goal.x;result.y+=entity->goal.y;}
  result.x/=static_cast<float>(units.size());result.y/=static_cast<float>(units.size());
  return result;
}

float projectedSpan(const Simulation& simulation,const std::vector<Id>& units,Vec2 axis) {
  float low=std::numeric_limits<float>::infinity();
  float high=-std::numeric_limits<float>::infinity();
  for(Id id:units) {
    const Entity* entity=simulation.find(id);
    const float value=entity->goal.x*axis.x+entity->goal.y*axis.y;
    low=std::min(low,value);high=std::max(high,value);
  }
  return high-low;
}

void checkPairwiseGoals(const Simulation& simulation,const std::vector<Id>& units,
                        const std::string& context,float margin=0) {
  for(std::size_t left=0;left<units.size();++left)for(std::size_t right=left+1;right<units.size();++right) {
    const Entity* a=simulation.find(units[left]);const Entity* b=simulation.find(units[right]);
    if(definition(a->kind).air!=definition(b->kind).air)continue;
    check(distance(a->goal,b->goal)+0.001f>=
            definition(a->kind).radius+definition(b->kind).radius+margin,
          context+": accepted same-layer unit footprints overlap");
  }
}

void checkGoalStaticClearance(const Simulation& simulation,Id id,const std::string& context) {
  const Entity* moving=simulation.find(id);const auto& unit=definition(moving->kind);
  check(std::isfinite(moving->goal.x)&&std::isfinite(moving->goal.y)&&
        moving->goal.x>=unit.radius&&moving->goal.y>=unit.radius&&
        moving->goal.x<=simulation.worldSize()-unit.radius&&
        moving->goal.y<=simulation.worldSize()-unit.radius,
        context+": goal leaves finite world bounds");
  if(unit.air)return;
  for(const auto& entity:simulation.entities()) {
    if(!entity.alive()||entity.id==id||
       (!definition(entity.kind).building&&entity.kind!=Kind::Resource)||
       (entity.kind==Kind::Resource&&entity.resource<=0))continue;
    check(distance(moving->goal,entity.pos)+0.001f>=
            unit.radius+definition(entity.kind).radius,
          context+": goal overlaps static entity geometry");
  }
  for(const auto& obstacle:simulation.obstacles()) {
    const float dx=std::max(std::fabs(moving->goal.x-obstacle.center.x)-obstacle.half.x,0.0f);
    const float dy=std::max(std::fabs(moving->goal.y-obstacle.center.y)-obstacle.half.y,0.0f);
    check(dx*dx+dy*dy+0.001f>=unit.radius*unit.radius,
          context+": goal overlaps authored terrain");
  }
}

std::vector<Id> spawnGrid(Simulation& simulation,Kind kind,int count,Vec2 origin,float pitch=70) {
  std::vector<Id> result;
  for(int index=0;index<count;++index)
    result.push_back(simulation.debugSpawn(kind,0,
      {origin.x+(index%4)*pitch,origin.y+(index/4)*pitch}));
  return result;
}

void spacingPresetsChangeOpenFormationAndPreserveLegacyStandard() {
  struct Layout {Simulation simulation;std::vector<Id> units;};
  auto make=[&](FormationSpacing spacing,bool explicitStandard) {
    Layout layout{fixture(),{}};layout.units=spawnGrid(layout.simulation,Kind::Scout,9,{900,1000});
    Command command=makeCommand(CommandType::Move,0,layout.units,{2300,1300});
    if(explicitStandard||spacing!=FormationSpacing::Standard)command.spacing=spacing;
    check(layout.simulation.command(command).accepted,"open formation spacing command is accepted");
    checkPairwiseGoals(layout.simulation,layout.units,"open spacing");
    check(samePoint(centroid(layout.simulation,layout.units),{2300,1300}),
          "open square formation remains centered on the requested point");
    return layout;
  };
  auto tight=make(FormationSpacing::Tight,true);
  auto standard=make(FormationSpacing::Standard,true);
  auto wide=make(FormationSpacing::Wide,true);
  auto legacy=make(FormationSpacing::Standard,false);
  const Vec2 horizontal{1,0};
  check(projectedSpan(tight.simulation,tight.units,horizontal)<
          projectedSpan(standard.simulation,standard.units,horizontal)&&
        projectedSpan(standard.simulation,standard.units,horizontal)<
          projectedSpan(wide.simulation,wide.units,horizontal),
        "unobstructed homogeneous formations visibly distinguish all spacing presets");
  for(std::size_t index=0;index<standard.units.size();++index) {
    const Entity* explicitUnit=standard.simulation.find(standard.units[index]);
    const Entity* legacyUnit=legacy.simulation.find(legacy.units[index]);
    check(samePoint(explicitUnit->goal,legacyUnit->goal)&&!explicitUnit->hasArrivalFacing&&
          !legacyUnit->hasArrivalFacing,
          "explicit Standard without facing preserves legacy per-ID assignment and intent");
  }
}

void rotatedFormationsCenterSortAndApplyArrivalFacing() {
  struct Layout {Simulation simulation;std::vector<Id> units;};
  auto make=[&](float angle,bool reverse) {
    Layout layout{fixture(),{}};layout.units=spawnGrid(layout.simulation,Kind::Striker,8,{900,1700});
    auto commandUnits=layout.units;if(reverse)std::reverse(commandUnits.begin(),commandUnits.end());
    check(send(layout.simulation,CommandType::Move,0,commandUnits,{2350,1850},0,Kind::Worker,
               CommandQueueMode::Replace,FormationSpacing::Standard,true,angle).accepted,
          "explicit-facing formation is accepted");
    check(samePoint(centroid(layout.simulation,layout.units),{2350,1850}),
          "explicit-facing formation has the requested world-space centroid");
    const Vec2 forward{std::cos(angle),std::sin(angle)};
    const Vec2 right{-forward.y,forward.x};
    check(projectedSpan(layout.simulation,layout.units,right)>
          1.8f*projectedSpan(layout.simulation,layout.units,forward),
          "explicit-facing formation exposes substantially more frontage than depth");
    checkPairwiseGoals(layout.simulation,layout.units,"rotated formation");
    for(Id id:layout.units) {
      const Entity* entity=layout.simulation.find(id);
      check(entity->hasArrivalFacing&&angleDistance(entity->arrivalFacing,angle)<0.0001f,
            "each current order stores the canonical requested arrival facing");
    }
    return layout;
  };
  for(float angle:std::vector<float>{0,Pi/4,Pi/2,-Pi}) {
    auto ordered=make(angle,false);auto reversed=make(angle,true);
    for(std::size_t index=0;index<ordered.units.size();++index)
      check(samePoint(ordered.simulation.find(ordered.units[index])->goal,
                      reversed.simulation.find(reversed.units[index])->goal),
            "explicit assignment depends on spatial sort and ID rather than command-list order");
  }

  auto arrival=make(Pi/4,false);std::vector<bool> observed(arrival.units.size(),false);
  for(int tick=0;tick<1000&&!std::all_of(observed.begin(),observed.end(),[](bool value){return value;});++tick) {
    std::vector<Order> before;for(Id id:arrival.units)before.push_back(arrival.simulation.find(id)->order);
    step(arrival.simulation);
    for(std::size_t index=0;index<arrival.units.size();++index) {
      const Entity* entity=arrival.simulation.find(arrival.units[index]);
      if(before[index]==Order::Move&&entity->order==Order::Idle) {
        check(!entity->hasArrivalFacing&&entity->arrivalFacing==0&&
              !std::signbit(entity->arrivalFacing)&&angleDistance(entity->facing,Pi/4)<0.02f,
              "Move completion applies rendered facing and clears one-shot current intent");
        observed[index]=true;
      }
    }
  }
  check(std::all_of(observed.begin(),observed.end(),[](bool value){return value;}),
        "every explicit-facing recipient reaches its own accepted point");
}

void mixedFootprintsAdaptAtEdgeAndActuallyArrive() {
  auto simulation=fixture();
  const std::vector<Kind> kinds{Kind::Worker,Kind::Striker,Kind::Lancer,
                                Kind::Bastion,Kind::Mender,Kind::Kite};
  std::vector<Id> units;
  for(std::size_t index=0;index<kinds.size();++index)
    units.push_back(simulation.debugSpawn(kinds[index],0,{1050.0f+index*80.0f,2250.0f}));
  check(send(simulation,CommandType::Move,0,units,{45,80},0,Kind::Worker,
             CommandQueueMode::Replace,FormationSpacing::Wide,true,Pi/4).accepted,
        "mixed-radius formation adapts at a legal map corner");
  std::vector<Vec2> accepted;
  for(Id id:units) {accepted.push_back(simulation.find(id)->goal);checkGoalStaticClearance(simulation,id,"corner formation");}
  checkPairwiseGoals(simulation,units,"corner formation");
  std::vector<bool> arrived(units.size(),false);
  for(int tick=0;tick<1600&&!std::all_of(arrived.begin(),arrived.end(),[](bool value){return value;});++tick) {
    step(simulation);
    for(std::size_t index=0;index<units.size();++index) {
      const Entity* entity=simulation.find(units[index]);
      check(samePoint(entity->goal,accepted[index]),"edge recovery rewrote an accepted formation point");
      if(entity->order==Order::Idle&&distance(entity->pos,accepted[index])<30)arrived[index]=true;
    }
  }
  check(std::all_of(arrived.begin(),arrived.end(),[](bool value){return value;}),
        "mixed Worker, combat, Mender, heavy and air units reach adapted edge slots");
}

void holdsReserveFootprintsAcrossMixedAppendAndLayers() {
  auto simulation=fixture();
  const Id unselected=simulation.debugSpawn(Kind::Bastion,0,{2200,2300});
  const Id selected=simulation.debugSpawn(Kind::Bastion,0,{2020,2300});
  const Id idle=simulation.debugSpawn(Kind::Striker,0,{1700,2300});
  check(send(simulation,CommandType::Hold,0,{unselected,selected}).accepted,
        "Hold reservation fixture establishes stationary heavy units");
  const Vec2 unselectedPosition=simulation.find(unselected)->pos;
  const Vec2 selectedPosition=simulation.find(selected)->pos;
  check(send(simulation,CommandType::Move,0,{idle,selected},{2200,2300},0,Kind::Worker,
             CommandQueueMode::Append,FormationSpacing::Tight,true,0).accepted,
        "mixed Idle-plus-Hold Append normalizes before installing or deferring");
  const Entity* moving=simulation.find(idle);const Entity* deferred=simulation.find(selected);
  check(moving->order==Order::Move&&moving->futureOrders.empty()&&
        deferred->order==Order::Hold&&deferred->futureOrders.size()==1,
        "Idle starts immediately while selected Hold defers its appended step");
  for(Id hold:{unselected,selected})
    check(distance(moving->goal,simulation.find(hold)->pos)+0.001f>=
            definition(Kind::Striker).radius+definition(Kind::Bastion).radius+10,
          "immediate ground destination reserves projected Hold footprint and safety margin");
  check(distance(deferred->futureOrders.front().point,unselectedPosition)+0.001f>=
          2*definition(Kind::Bastion).radius+10,
        "deferred selected Hold destination still reserves the other Hold footprint");
  step(simulation,40);
  check(samePoint(simulation.find(unselected)->pos,unselectedPosition)&&
        samePoint(simulation.find(selected)->pos,selectedPosition),
        "normalization and traffic do not push selected or unselected Holds");

  const Id air=simulation.debugSpawn(Kind::Kite,0,{1700,2500});
  check(send(simulation,CommandType::Move,0,{air},unselectedPosition).accepted&&
        samePoint(simulation.find(air)->goal,unselectedPosition),
        "ground Hold footprint does not reserve an air-layer destination");
  check(send(simulation,CommandType::Move,0,{selected},{2700,2450},0,Kind::Worker,
             CommandQueueMode::Replace,FormationSpacing::Wide).accepted&&
        simulation.find(selected)->order==Order::Move&&simulation.find(selected)->futureOrders.empty(),
        "replacing command releases the deliberately selected Hold and clears its deferred step");
  check(simulation.find(unselected)->order==Order::Hold&&
        samePoint(simulation.find(unselected)->pos,unselectedPosition),
        "replacement never retasks the unselected Hold");
}

void airMoverBypassesHeldAirFootprintWithoutTunneling() {
  auto simulation=fixture();
  const Id held=simulation.debugSpawn(Kind::Kite,0,{2000,2000});
  const Id mover=simulation.debugSpawn(Kind::Kite,0,{1800,2000});
  check(send(simulation,CommandType::Hold,0,{held}).accepted&&
        send(simulation,CommandType::Move,0,{mover},{2400,2000}).accepted,
        "air Hold crossing fixture accepts stationary and movement orders");
  const Vec2 heldPosition=simulation.find(held)->pos;
  const Vec2 acceptedGoal=simulation.find(mover)->goal;
  check(samePoint(acceptedGoal,{2400,2000}),
        "air mover retains the requested point beyond the held same-layer unit");
  const float clearance=2*definition(Kind::Kite).radius;
  Vec2 previous=simulation.find(mover)->pos;
  bool arrived=false;
  for(int tick=0;tick<800&&!arrived;++tick) {
    step(simulation);
    const Entity* stationary=simulation.find(held);const Entity* moving=simulation.find(mover);
    check(stationary&&stationary->alive()&&stationary->order==Order::Hold&&
          samePoint(stationary->pos,heldPosition),
          "air traffic moved or retasked the held Veil");
    check(moving&&moving->alive()&&samePoint(moving->goal,acceptedGoal),
          "air bypass rewrote or lost the accepted movement destination");
    check(distance(moving->pos,heldPosition)+0.001f>=clearance&&
          segmentDistance(previous,moving->pos,heldPosition)+0.001f>=clearance,
          "moving Veil penetrated or tunneled through the held air footprint");
    previous=moving->pos;
    arrived=moving->order==Order::Idle&&distance(moving->pos,acceptedGoal)<30;
  }
  check(arrived,
        "moving Veil did not bypass the held air footprint and reach its retained destination");
}

void heldHeavyClosesThenReopensSingleUnitCorridor() {
  auto simulation=fixture();
  editObstacles(simulation)={
    {{750,2000},{750,90}},
    {{3200,2000},{1600,90}}
  };
  const Id held=simulation.debugSpawn(Kind::Bastion,0,{1550,2000});
  const Id mover=simulation.debugSpawn(Kind::Striker,0,{1550,1600});
  check(send(simulation,CommandType::Hold,0,{held}).accepted,
        "single-unit corridor begins with a held heavy occupying its only opening");
  const Vec2 heldPosition=simulation.find(held)->pos;
  check(send(simulation,CommandType::Move,0,{mover},{2200,2450}).accepted,
        "blocked corridor accepts a Move with a legal endpoint beyond the Hold");
  const Vec2 acceptedGoal=simulation.find(mover)->goal;
  checkGoalStaticClearance(simulation,mover,"Hold-blocked corridor endpoint");

  bool exhausted=false;
  for(int tick=0;tick<240&&!exhausted;++tick) {
    step(simulation);
    const Entity* stationary=simulation.find(held);const Entity* moving=simulation.find(mover);
    check(stationary->order==Order::Hold&&samePoint(stationary->pos,heldPosition),
          "blocked-route feedback moved or retasked the held heavy");
    check(moving->order==Order::Move&&samePoint(moving->goal,acceptedGoal)&&
          moving->pos.y<heldPosition.y,
          "blocked mover passed the heavy-filled opening or lost its exact Move intent");
    const float heldDistance=distance(moving->pos,heldPosition);
    check(heldDistance+0.001f>=
            definition(Kind::Striker).radius+definition(Kind::Bastion).radius,
          "blocked mover penetrated held footprint tick="+std::to_string(tick)+
            " mover="+std::to_string(mover)+" pos="+std::to_string(moving->pos.x)+","+
            std::to_string(moving->pos.y)+" held="+std::to_string(held)+" pos="+
            std::to_string(heldPosition.x)+","+std::to_string(heldPosition.y)+
            " distance="+std::to_string(heldDistance));
    exhausted=moving->navigationExhausted;
  }
  const Entity* stalled=simulation.find(mover);
  check(exhausted&&stalled->navigationFailures>=5&&stalled->order==Order::Move&&
        samePoint(stalled->goal,acceptedGoal),
        "repeated anchored stalls did not report bounded exhaustion while retaining Move and goal");

  check(send(simulation,CommandType::Move,0,{held},{1950,2350}).accepted,
        "ordinary replacing Move releases the heavy from the corridor opening");
  bool exhaustionCleared=false,progressedBeyondWall=false,arrived=false;
  for(int tick=0;tick<900&&!arrived;++tick) {
    step(simulation);
    const Entity* moving=simulation.find(mover);
    check(moving&&moving->alive()&&samePoint(moving->goal,acceptedGoal)&&
          (moving->order==Order::Move||moving->order==Order::Idle),
          "reopened corridor changed the waiting mover's accepted intent");
    for(const auto& obstacle:simulation.obstacles()) {
      const float dx=std::max(std::fabs(moving->pos.x-obstacle.center.x)-obstacle.half.x,0.0f);
      const float dy=std::max(std::fabs(moving->pos.y-obstacle.center.y)-obstacle.half.y,0.0f);
      check(dx*dx+dy*dy+0.001f>=
              definition(Kind::Striker).radius*definition(Kind::Striker).radius,
            "reopened mover penetrated corridor terrain");
    }
    exhaustionCleared=exhaustionCleared||!moving->navigationExhausted;
    progressedBeyondWall=progressedBeyondWall||moving->pos.y>2090+definition(Kind::Striker).radius;
    arrived=moving->order==Order::Idle&&distance(moving->pos,acceptedGoal)<30;
  }
  check(exhaustionCleared&&progressedBeyondWall&&arrived,
        "mover did not clear exhaustion, resume through the released corridor, and arrive");
}

void mixedFormationTurnsThroughPassageAroundUnselectedHold() {
  auto simulation=fixture();
  // A world-spanning wall leaves one 180-unit ground opening. The destination
  // lies above and east of it, and an unselected Anvil occupies the natural
  // exit line, requiring both the authored turn and local mobile avoidance.
  editObstacles(simulation)={
    {{725,2000},{725,90}},
    {{3215,2000},{1585,90}}
  };
  const Id held=simulation.debugSpawn(Kind::Bastion,0,{1750,2200});
  check(send(simulation,CommandType::Hold,0,{held}).accepted,
        "constrained-turn fixture establishes its unselected Hold blocker");
  const Vec2 heldPosition=simulation.find(held)->pos;
  const std::vector<Kind> kinds{Kind::Worker,Kind::Striker,Kind::Mender,Kind::Kite};
  const std::vector<Vec2> starts{{1180,1650},{1280,1730},{1380,1650},{1220,1540}};
  std::vector<Id> units;
  for(std::size_t index=0;index<kinds.size();++index)
    units.push_back(simulation.debugSpawn(kinds[index],0,starts[index]));
  check(send(simulation,CommandType::Move,0,units,{2300,2500},0,Kind::Worker,
             CommandQueueMode::Replace,FormationSpacing::Standard,true,0).accepted,
        "mixed Worker, combat, Mender, and air formation accepts a constrained turn");
  std::vector<Vec2> goals;for(Id id:units)goals.push_back(simulation.find(id)->goal);
  checkPairwiseGoals(simulation,units,"constrained mixed formation");

  std::vector<bool> crossedOpening(units.size(),false),arrived(units.size(),false);
  std::vector<Vec2> previous;for(Id id:units)previous.push_back(simulation.find(id)->pos);
  bool encounteredHold=false;
  for(int tick=0;tick<1800&&!std::all_of(arrived.begin(),arrived.end(),[](bool value){return value;});++tick) {
    step(simulation);
    check(simulation.find(held)->order==Order::Hold&&
          samePoint(simulation.find(held)->pos,heldPosition),
          "formation traffic moved or retasked the unselected Hold");
    for(std::size_t index=0;index<units.size();++index) {
      const Entity* entity=simulation.find(units[index]);
      check(entity&&entity->alive()&&samePoint(entity->goal,goals[index]),
            "constrained recovery lost a recipient or rewrote its accepted point");
      if(!definition(entity->kind).air) {
        const float clearance=definition(entity->kind).radius+definition(Kind::Bastion).radius;
        const float holdDistance=distance(entity->pos,heldPosition);
        check(holdDistance+0.001f>=clearance,
              "ground recipient penetrated held footprint tick="+std::to_string(tick)+
                " mover="+std::to_string(entity->id)+" pos="+std::to_string(entity->pos.x)+","+
                std::to_string(entity->pos.y)+" held="+std::to_string(held)+" pos="+
                std::to_string(heldPosition.x)+","+std::to_string(heldPosition.y)+
                " distance="+std::to_string(holdDistance));
        encounteredHold=encounteredHold||holdDistance<150;
        if(!crossedOpening[index]&&previous[index].y<=2000&&entity->pos.y>2000) {
          const float alpha=(2000-previous[index].y)/(entity->pos.y-previous[index].y);
          const float crossingX=previous[index].x+alpha*(entity->pos.x-previous[index].x);
          check(crossingX>=1450+definition(entity->kind).radius-1&&
                crossingX<=1630-definition(entity->kind).radius+1,
                "ground recipient crossed the wall outside its radius-valid opening");
          crossedOpening[index]=true;
        }
        for(const auto& obstacle:simulation.obstacles()) {
          const float dx=std::max(std::fabs(entity->pos.x-obstacle.center.x)-obstacle.half.x,0.0f);
          const float dy=std::max(std::fabs(entity->pos.y-obstacle.center.y)-obstacle.half.y,0.0f);
          check(dx*dx+dy*dy+0.001f>=
                  definition(entity->kind).radius*definition(entity->kind).radius,
                "ground recipient penetrated constrained-passage terrain");
        }
      }
      if(entity->order==Order::Idle&&distance(entity->pos,goals[index])<30)arrived[index]=true;
      previous[index]=entity->pos;
    }
  }
  check(crossedOpening[0]&&crossedOpening[1]&&crossedOpening[2],
        "every ground role traverses the only radius-valid passage");
  check(encounteredHold,
        "ground formation never exercised local routing around the authored Hold blocker");
  check(std::all_of(arrived.begin(),arrived.end(),[](bool value){return value;}),
        "mixed formation did not independently complete its constrained turn and accepted slots");
}

void impossibleLayoutAndInvalidModifiersRejectAtomically() {
  auto blocked=fixture();
  const auto units=spawnGrid(blocked,Kind::Bastion,4,{700,3300},90);
  editObstacles(blocked).push_back({{2400,2400},{1100,1100}});
  const auto blockedHash=blocked.stateHash();const auto blockedRecording=blocked.recording().size();
  check(!send(blocked,CommandType::Move,0,units,{2400,2400},0,Kind::Worker,
              CommandQueueMode::Replace,FormationSpacing::Wide,true,0).accepted,
        "formation rejects when every bounded fallback ring remains in static terrain");
  check(blocked.stateHash()==blockedHash&&blocked.recording().size()==blockedRecording,
        "impossible complete layout rejects before any recipient or recording mutation");

  auto simulation=fixture();const Id unit=simulation.debugSpawn(Kind::Striker,0,{1000,3500});
  check(send(simulation,CommandType::Hold,0,{unit}).accepted,"invalid-modifier fixture has live state");
  auto reject=[&](Command command,const std::string& reason) {
    const auto hash=simulation.stateHash();const auto recorded=simulation.recording().size();
    const Entity before=*simulation.find(unit);
    check(!simulation.command(command).accepted,reason+" is rejected");
    const Entity* after=simulation.find(unit);
    check(simulation.stateHash()==hash&&simulation.recording().size()==recorded&&
          after->order==before.order&&samePoint(after->goal,before.goal)&&
          after->futureOrders.size()==before.futureOrders.size(),
          reason+" leaves authoritative order state and recording unchanged");
  };
  Command invalid=makeCommand(CommandType::Move,0,{unit},{1500,3500});
  invalid.spacing=static_cast<FormationSpacing>(99);reject(invalid,"unknown spacing preset");
  invalid=makeCommand(CommandType::Move,0,{unit},{1500,3500},0,Kind::Worker,
                      CommandQueueMode::Replace,FormationSpacing::Standard,true,
                      std::numeric_limits<float>::quiet_NaN());
  reject(invalid,"nonfinite arrival angle");
  invalid.arrivalFacing=Pi;reject(invalid,"noncanonical positive pi angle");
  invalid.hasArrivalFacing=false;invalid.arrivalFacing=0.5f;
  reject(invalid,"unflagged nonzero arrival angle");
  reject(makeCommand(CommandType::Hold,0,{unit},{},0,Kind::Worker,
                     CommandQueueMode::Replace,FormationSpacing::Wide),
         "spacing on an ineligible Hold");
  reject(makeCommand(CommandType::Patrol,0,{unit},{1600,3500},0,Kind::Worker,
                     CommandQueueMode::Replace,FormationSpacing::Standard,true,0),
         "arrival facing on Patrol");
  reject(makeCommand(CommandType::Defend,0,{unit},{1600,3500},0,Kind::Worker,
                     CommandQueueMode::Append,FormationSpacing::Standard,true,0),
         "appended Defend");

  Command negativeZero=makeCommand(CommandType::Move,0,{unit},{1550,3500},0,Kind::Worker,
                                   CommandQueueMode::Replace,FormationSpacing::Standard,true,-0.0f);
  check(simulation.command(negativeZero).accepted&&simulation.find(unit)->hasArrivalFacing&&
        simulation.find(unit)->arrivalFacing==0&&!std::signbit(simulation.find(unit)->arrivalFacing)&&
        simulation.recording().back().command.arrivalFacing==0&&
        !std::signbit(simulation.recording().back().command.arrivalFacing),
        "true negative-zero command is accepted and canonicalized before mutation and recording");
}

void moveWithdrawalClearsCombatAndCompletesRearFormation() {
  auto simulation=fixture();
  const Id firstAttacker=simulation.debugSpawn(Kind::Striker,0,{1450,2900});
  const Id secondAttacker=simulation.debugSpawn(Kind::Striker,0,{1450,2970});
  const Id enemy=simulation.debugSpawn(Kind::Bastion,1,{1690,2935});
  check(send(simulation,CommandType::Hold,1,{enemy}).accepted&&
        send(simulation,CommandType::Attack,0,{firstAttacker,secondAttacker},{},enemy).accepted,
        "withdrawal fixture enters real combat");
  const float fullHp=simulation.find(enemy)->hp;
  stepUntil(simulation,80,[&]{return simulation.find(enemy)->hp<fullHp;},
            "attackers did not establish combat before withdrawal");
  check(send(simulation,CommandType::AttackMove,0,{firstAttacker,secondAttacker},{2100,3000},0,
             Kind::Worker,CommandQueueMode::Append).accepted,
        "engaged group can hold a future combat waypoint before withdrawal");
  check(send(simulation,CommandType::Move,0,{firstAttacker,secondAttacker},{850,2940},0,
             Kind::Worker,CommandQueueMode::Replace,FormationSpacing::Wide,true,-Pi).accepted,
        "ordinary replacing Move issues a faced rear withdrawal");
  std::vector<Id> units{firstAttacker,secondAttacker};std::vector<Vec2> goals;
  for(Id id:units) {
    const Entity* entity=simulation.find(id);goals.push_back(entity->goal);
    check(entity->order==Order::Move&&entity->target==0&&entity->futureOrders.empty(),
          "withdrawal immediately clears combat target and prior route");
  }
  const float hpAfterCommand=simulation.find(enemy)->hp;
  step(simulation,20);
  check(simulation.find(enemy)->hp==hpAfterCommand,
        "Move withdrawal neither acquires nor fires while travelling");
  for(std::size_t index=0;index<units.size();++index)
    stepUntil(simulation,500,[&,index]{const Entity* entity=simulation.find(units[index]);
      return entity->order==Order::Idle&&distance(entity->pos,goals[index])<30;},
      "withdrawing unit did not reach its immutable rear slot");
  check(simulation.find(enemy)->hp<fullHp,"withdrawal assertion followed an actual prior engagement");
}

void attackMoveAndQueuedFacingSurviveCombatInFifoOrder() {
  auto simulation=fixture();
  const Id attacker=simulation.debugSpawn(Kind::Striker,0,{1000,3650});
  const Id enemy=simulation.debugSpawn(Kind::Worker,1,{1350,3650});
  check(send(simulation,CommandType::Hold,1,{enemy}).accepted&&
        send(simulation,CommandType::AttackMove,0,{attacker},{1950,3650},0,Kind::Worker,
             CommandQueueMode::Replace,FormationSpacing::Tight,true,Pi/2).accepted&&
        send(simulation,CommandType::Move,0,{attacker},{2250,3900},0,Kind::Worker,
             CommandQueueMode::Append,FormationSpacing::Wide,true,-Pi/2).accepted,
        "AttackMove and independently faced successor are accepted");
  const Vec2 waypoint=simulation.find(attacker)->goal;
  const TacticalOrder successor=simulation.find(attacker)->futureOrders.front();
  stepUntil(simulation,500,[&]{const Entity* target=simulation.find(enemy);return !target||!target->alive();},
            "AttackMove did not resolve its incidental engagement");
  const Entity* afterCombat=simulation.find(attacker);
  check(afterCombat->order==Order::AttackMove&&samePoint(afterCombat->goal,waypoint)&&
        afterCombat->hasArrivalFacing&&angleDistance(afterCombat->arrivalFacing,Pi/2)<0.001f&&
        afterCombat->futureOrders.size()==1&&samePoint(afterCombat->futureOrders.front().point,successor.point),
        "combat and target loss preserve the waypoint, its facing, and explicit successor");
  stepUntil(simulation,700,[&]{return simulation.find(attacker)->order==Order::Move&&
                                      simulation.find(attacker)->futureOrders.empty();},
            "AttackMove did not activate its successor after actual waypoint completion");
  const Entity* activated=simulation.find(attacker);
  check(samePoint(activated->goal,successor.point)&&activated->hasArrivalFacing&&
        angleDistance(activated->arrivalFacing,-Pi/2)<0.001f,
        "successor activation installs its own accepted point and facing intent");
  stepUntil(simulation,500,[&]{return simulation.find(attacker)->order==Order::Idle;},
            "final faced successor did not complete");
  check(angleDistance(simulation.find(attacker)->facing,-Pi/2)<0.02f&&
        !simulation.find(attacker)->hasArrivalFacing,
        "only final FIFO completion leaves its requested rendered facing");
}

void defendFightsThenReturnsAndRestoresFacing() {
  auto simulation=fixture();
  const Id defender=simulation.debugSpawn(Kind::Striker,0,{2500,1050});
  const Id enemy=simulation.debugSpawn(Kind::Worker,1,{2740,1050});
  const Id traffic=simulation.debugSpawn(Kind::Bastion,0,{2400,1050});
  check(send(simulation,CommandType::Hold,1,{enemy}).accepted&&
        send(simulation,CommandType::Defend,0,{defender},{2500,1050},0,Kind::Worker,
             CommandQueueMode::Replace,FormationSpacing::Standard,true,Pi/2).accepted,
        "faced Defend and hostile Hold are accepted");
  const Vec2 anchor=simulation.find(defender)->goal;
  const float enemyHp=simulation.find(enemy)->hp;
  stepUntil(simulation,80,[&]{return simulation.find(enemy)->hp<enemyHp&&
                                     simulation.find(defender)->target==enemy;},
            "Defend did not acquire and fire on a legal target");
  const float combatFacing=simulation.find(defender)->facing;
  check(angleDistance(combatFacing,Pi/2)>0.4f,
        "legal combat target controls facing instead of premature anchor restoration");
  check(send(simulation,CommandType::Move,0,{traffic},{2600,1050}).accepted,
        "friendly heavy traffic moves through the cooling defender's anchor");
  bool displacedCoolingFacing=false;
  int displacedCoolingSamples=0;
  for(int tick=0;tick<40&&displacedCoolingSamples<4;++tick) {
    step(simulation);
    const Entity* state=simulation.find(defender);const Entity* target=simulation.find(enemy);
    if(state->target==enemy&&target&&target->alive()&&state->cooldown>0&&
       distance(state->pos,anchor)>5) {
      const float targetFacing=std::atan2(target->pos.y-state->pos.y,target->pos.x-state->pos.x);
      check(angleDistance(state->facing,targetFacing)<0.02f,
            "anchor recovery overwrote legal target-facing during Defend cooldown");
      displacedCoolingFacing=true;
      ++displacedCoolingSamples;
    }
  }
  check(displacedCoolingFacing&&displacedCoolingSamples>=4,
        "friendly traffic did not sustain a cooling defender displacement for facing validation");
  stepUntil(simulation,600,[&]{const Entity* target=simulation.find(enemy);return !target||!target->alive();},
            "Defender did not finish its real engagement");
  stepUntil(simulation,500,[&]{const Entity* entity=simulation.find(defender);
    return entity->order==Order::Defend&&entity->target==0&&distance(entity->pos,anchor)<24&&
           angleDistance(entity->facing,Pi/2)<0.02f;},
    "Defend did not return to its immutable anchor and restore requested facing");
  check(simulation.find(defender)->hasArrivalFacing&&
        angleDistance(simulation.find(defender)->arrivalFacing,Pi/2)<0.001f,
        "persistent Defend retains its arrival-facing intent after restoration");
}

void loadedWideDefendJamRecoversEverySavedAnchor() {
  // Captured from the deterministic synthetic 16-unit formation workload with
  // seed 984231 after Mortar 47 remained 158 units from its Wide Defend anchor.
  Simulation simulation;
  check(simulation.load(fixturePath("formation-defend-jam-v13.cinder").string()),
        "exact saved Wide Defend jam loads successfully");
  check(simulation.config().seed==984231&&simulation.tick()==10506,
        "loaded Defend jam retains its captured seed and tick provenance");

  struct SavedIntent {
    Id id=0;
    Order order=Order::Idle;
    Vec2 goal{};
    bool hasFacing=false;
    float arrivalFacing=0;
  };
  std::vector<SavedIntent> saved;
  std::vector<Id> defenders;
  int initiallyAnchored=0;
  for(const auto& entity:simulation.entities()) {
    saved.push_back({entity.id,entity.order,entity.goal,
                     entity.hasArrivalFacing,entity.arrivalFacing});
    if(entity.alive()&&entity.team==0&&!definition(entity.kind).building) {
      check(entity.order==Order::Defend&&entity.hasArrivalFacing&&
            angleDistance(entity.arrivalFacing,-Pi)<0.0001f,
            "saved formation mobile retains Wide Defend and its explicit facing intent");
      defenders.push_back(entity.id);
      if(distance(entity.pos,entity.goal)<=5)++initiallyAnchored;
    }
  }
  check(defenders.size()==16&&initiallyAnchored==15,
        "saved regression begins with exactly fifteen of sixteen defenders anchored");
  const Entity* initialLaggard=simulation.find(47);
  check(initialLaggard&&initialLaggard->kind==Kind::Mortar&&
        distance(initialLaggard->pos,initialLaggard->goal)>150,
        "Mortar 47 begins at the recorded unresolved distance from its anchor");

  int stableTicks=0;
  for(int tick=0;tick<1200&&stableTicks<20;++tick) {
    step(simulation);
    for(const auto& intent:saved) {
      const Entity* entity=simulation.find(intent.id);
      check(entity&&entity->order==intent.order&&
            entity->goal.x==intent.goal.x&&entity->goal.y==intent.goal.y&&
            entity->hasArrivalFacing==intent.hasFacing&&
            entity->arrivalFacing==intent.arrivalFacing&&
            std::signbit(entity->arrivalFacing)==std::signbit(intent.arrivalFacing),
            "Defend jam recovery changed saved order, goal, or facing intent for id "+
              std::to_string(intent.id));
    }
    bool settled=true;
    for(Id id:defenders) {
      const Entity* entity=simulation.find(id);
      settled=settled&&distance(entity->pos,entity->goal)<=5&&
        angleDistance(entity->facing,entity->arrivalFacing)<0.02f;
    }
    stableTicks=settled?stableTicks+1:0;
  }
  check(stableTicks>=20,
        "all sixteen loaded defenders, including Mortar 47, did not reclaim exact anchors and facing");
}

void clearStopReplacementAndSustainedOrdersCleanFacingState() {
  auto simulation=fixture();
  const Id unit=simulation.debugSpawn(Kind::Striker,0,{2850,1550});
  check(send(simulation,CommandType::Move,0,{unit},{3300,1550},0,Kind::Worker,
             CommandQueueMode::Replace,FormationSpacing::Wide,true,Pi/4).accepted&&
        send(simulation,CommandType::AttackMove,0,{unit},{3600,1750},0,Kind::Worker,
             CommandQueueMode::Append,FormationSpacing::Tight,true,-Pi/4).accepted,
        "current and future faced steps are accepted");
  check(send(simulation,CommandType::ClearOrders,0,{unit}).accepted&&
        simulation.find(unit)->order==Order::Move&&simulation.find(unit)->hasArrivalFacing&&
        simulation.find(unit)->futureOrders.empty(),
        "ClearOrders removes future facing with the tail and preserves current facing intent");
  check(send(simulation,CommandType::Stop,0,{unit}).accepted&&
        simulation.find(unit)->order==Order::Idle&&!simulation.find(unit)->hasArrivalFacing&&
        simulation.find(unit)->arrivalFacing==0,
        "Stop clears current and future formation-facing state");
  check(send(simulation,CommandType::Move,0,{unit},{3400,1550},0,Kind::Worker,
             CommandQueueMode::Replace,FormationSpacing::Standard,true,0).accepted&&
        send(simulation,CommandType::Patrol,0,{unit},{3650,1550}).accepted,
        "ordinary Patrol replaces a faced Move");
  check(simulation.find(unit)->order==Order::Patrol&&!simulation.find(unit)->hasArrivalFacing&&
        simulation.find(unit)->arrivalFacing==0,
        "Patrol replacement clears incompatible formation-facing state");
}

struct PersistenceFixture {Simulation simulation;Id first=0,second=0,defender=0;};

PersistenceFixture persistenceFixture() {
  PersistenceFixture value{fixture()};
  value.first=value.simulation.debugSpawn(Kind::Striker,0,{1000,2100});
  value.second=value.simulation.debugSpawn(Kind::Lancer,0,{1000,2180});
  value.defender=value.simulation.debugSpawn(Kind::Striker,0,{2800,2200});
  return value;
}

void saveThirteenLoadReplayAndContinuationPreserveFacing() {
  auto original=persistenceFixture();
  const std::vector<Id> movers{original.first,original.second};
  check(send(original.simulation,CommandType::Move,0,movers,{1800,2140},0,Kind::Worker,
             CommandQueueMode::Replace,FormationSpacing::Wide,true,Pi/4).accepted&&
        send(original.simulation,CommandType::AttackMove,0,movers,{2300,2350},0,Kind::Worker,
             CommandQueueMode::Append,FormationSpacing::Tight,true,-Pi/2).accepted&&
        send(original.simulation,CommandType::Defend,0,{original.defender},{3000,2200},0,
             Kind::Worker,CommandQueueMode::Replace,FormationSpacing::Standard,true,0).accepted,
        "save fixture establishes current, future, and persistent formation facing");
  step(original.simulation,30);
  const auto path=std::filesystem::temp_directory_path()/"cinderline-formation-v14.sav";
  check(original.simulation.save(path.string()),"save fourteen writes active formation intent");
  const auto lines=readLines(path);
  check(!lines.empty()&&lines.front()=="CINDERLINE 15"&&
        std::find(lines.begin(),lines.end(),"FORMATION_ORDERS 1")!=lines.end()&&
        std::find(lines.begin(),lines.end(),"QUEUED_WORK 1")!=lines.end(),
        "current save declares version fifteen with map revision, formation and queued-work sections");
  Simulation loaded;
  check(loaded.load(path.string())&&loaded.stateHash()==original.simulation.stateHash()&&
        sameRecording(loaded.recording(),original.simulation.recording()),
        "save-load preserves exact formation state, commands, and hash");
  std::filesystem::remove(path);
  for(int tick=0;tick<500;++tick) {
    step(original.simulation);step(loaded);
    check(loaded.stateHash()==original.simulation.stateHash(),
          "loaded formation state continues deterministically per tick");
  }

  auto replay=persistenceFixture();const auto recording=original.simulation.recording();std::size_t next=0;
  while(replay.simulation.tick()<original.simulation.tick()) {
    while(next<recording.size()&&recording[next].tick==replay.simulation.tick()) {
      check(replay.simulation.command(recording[next].command).accepted,
            "recorded formation command replays at its authoritative tick");
      ++next;
    }
    step(replay.simulation);
  }
  check(next==recording.size()&&replay.simulation.stateHash()==original.simulation.stateHash(),
        "spacing and facing recordings reproduce the full authoritative continuation");
}

void genuineSaveTwelveMigratesWithoutInventingFacing() {
  Simulation migrated;
  check(migrated.load(fixturePath("escort-row-jam-v12.cinder").string()),
        "genuine saved version-twelve dense Escort fixture loads");
  for(const auto& entity:migrated.entities()) {
    check(!entity.hasArrivalFacing&&entity.arrivalFacing==0&&!std::signbit(entity.arrivalFacing),
          "version twelve entity migrates with canonical empty current facing intent");
    for(const auto& order:entity.futureOrders)
      check(!order.hasArrivalFacing&&order.arrivalFacing==0&&!std::signbit(order.arrivalFacing),
            "version twelve tactical tail migrates with canonical empty facing intent");
  }
  for(const auto& item:migrated.recording())
    check(item.command.spacing==FormationSpacing::Standard&&!item.command.hasArrivalFacing&&
          item.command.arrivalFacing==0&&!std::signbit(item.command.arrivalFacing),
          "version twelve recording migrates to Standard with canonical empty facing");
  const auto path=std::filesystem::temp_directory_path()/"cinderline-migrated-formation-v14.sav";
  check(migrated.save(path.string()),"migrated version-twelve state saves as version fourteen");
  Simulation reloaded;
  check(reloaded.load(path.string())&&reloaded.stateHash()==migrated.stateHash()&&
        sameRecording(reloaded.recording(),migrated.recording()),
        "genuine migration remains stable through version-fourteen save and reload");
  std::filesystem::remove(path);
}

void saveThirteenRejectsCorruptFormationRowsAtomically() {
  auto source=fixture();
  const Id mover=source.debugSpawn(Kind::Striker,0,{1100,4100});
  const Id second=source.debugSpawn(Kind::Lancer,0,{1100,4180});
  check(send(source,CommandType::Move,0,{mover,second},{1800,4140},0,Kind::Worker,
             CommandQueueMode::Replace,FormationSpacing::Wide,true,Pi/4).accepted&&
        send(source,CommandType::AttackMove,0,{mover,second},{2300,4250},0,Kind::Worker,
             CommandQueueMode::Append,FormationSpacing::Tight,true,-Pi/2).accepted,
        "corruption fixture contains current and future formation intent");
  const Id headquarters=first(source,0,Kind::Headquarters);
  const auto path=std::filesystem::temp_directory_path()/"cinderline-corrupt-formation-v14.sav";
  check(source.save(path.string()),"valid formation corruption baseline saves");
  const auto valid=readLines(path);
  const auto marker=std::find(valid.begin(),valid.end(),"FORMATION_ORDERS 1");
  const auto queuedWork=std::find(valid.begin(),valid.end(),"QUEUED_WORK 1");
  check(marker!=valid.end()&&queuedWork!=valid.end()&&marker<queuedWork&&valid.front()=="CINDERLINE 15","corruption fixture locates formation before queued-work section");
  const std::size_t markerIndex=static_cast<std::size_t>(marker-valid.begin());
  const auto entityCount=static_cast<std::size_t>(std::stoul(valid.at(markerIndex+1)));
  check(queuedWork==marker+2+static_cast<std::ptrdiff_t>(entityCount),"formation section has one row per entity before queued work");
  const auto records=recordingRows(valid);
  check(records.size()==2,"formation corruption fixture locates recorded command rows");
  auto reject=[&](std::vector<std::string> broken,const std::string& reason) {
    writeLines(path,broken);auto untouched=fixture();const Id unit=untouched.debugSpawn(Kind::Striker,0,{900,900});
    check(send(untouched,CommandType::Hold,0,{unit}).accepted,"atomic load fixture has live recorded state");
    const auto hash=untouched.stateHash();const auto recording=untouched.recording().size();
    const auto entities=untouched.entities().size();
    check(!untouched.load(path.string()),reason+" is rejected");
    check(untouched.stateHash()==hash&&untouched.recording().size()==recording&&
          untouched.entities().size()==entities,
          reason+" rejects atomically without changing the destination simulation");
  };
  auto mutate=[&](std::vector<std::string> lines,Id id,std::size_t column,const std::string& value) {
    const auto row=sectionRow(lines,markerIndex,id);auto values=fields(lines[row]);
    check(column<values.size(),"formation corruption mutation addresses an existing field");
    values[column]=value;lines[row]=joined(values);return lines;
  };

  reject(mutate(valid,mover,0,"999999"),"unknown formation entity identity");
  auto duplicate=valid;duplicate[sectionRow(duplicate,markerIndex,second)]=
    duplicate[sectionRow(duplicate,markerIndex,mover)];
  reject(duplicate,"duplicate formation entity identity");
  auto reordered=valid;std::swap(reordered[markerIndex+2],reordered[markerIndex+3]);
  reject(reordered,"reordered formation entity rows");
  auto missing=valid;missing.erase(missing.begin()+static_cast<std::ptrdiff_t>(markerIndex+2));reject(missing,"missing formation entity row");
  auto missingSection=valid;missingSection.erase(missingSection.begin()+
    static_cast<std::ptrdiff_t>(markerIndex),missingSection.end());
  reject(missingSection,"missing formation section");
  reject(mutate(valid,mover,1,"2"),"invalid current-facing boolean");
  reject(mutate(valid,mover,2,"nan"),"nonfinite current-facing angle");
  reject(mutate(valid,mover,2,std::to_string(Pi)),"out-of-range current-facing angle");
  reject(mutate(valid,mover,2,"-0"),"persisted negative-zero current-facing angle");
  reject(mutate(valid,mover,1,"0"),"unflagged nonzero persisted angle");
  reject(mutate(valid,mover,3,"0"),"future-facing count disagreement");
  reject(mutate(valid,headquarters,1,"1"),"building carrying current formation-facing intent");
  auto trailing=valid;trailing.push_back("TRAILING");reject(trailing,"trailing save data");

  auto badSpacing=valid;auto record=fields(badSpacing[records.front()]);
  check(record.size()>=14,"save-thirteen recording exposes appended modifier fields");
  record[9]="99";badSpacing[records.front()]=joined(record);
  reject(badSpacing,"recording with invalid spacing");
  auto badFlag=valid;record=fields(badFlag[records.front()]);record[10]="2";
  badFlag[records.front()]=joined(record);reject(badFlag,"recording with invalid facing flag");
  auto badAngle=valid;record=fields(badAngle[records.front()]);record[11]="inf";
  badAngle[records.front()]=joined(record);reject(badAngle,"recording with nonfinite facing angle");
  std::filesystem::remove(path);
}

} // namespace

int main() {
  const std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"spacing presets and legacy Standard",spacingPresetsChangeOpenFormationAndPreserveLegacyStandard},
    {"rotated centroid sorting and arrival facing",rotatedFormationsCenterSortAndApplyArrivalFacing},
    {"mixed footprints adapt and arrive",mixedFootprintsAdaptAtEdgeAndActuallyArrive},
    {"Hold footprint reservations and layers",holdsReserveFootprintsAcrossMixedAppendAndLayers},
    {"air movement around held air footprint",airMoverBypassesHeldAirFootprintWithoutTunneling},
    {"held heavy closes and reopens corridor",heldHeavyClosesThenReopensSingleUnitCorridor},
    {"mixed constrained turn around Hold",mixedFormationTurnsThroughPassageAroundUnselectedHold},
    {"impossible layout and invalid modifiers",impossibleLayoutAndInvalidModifiersRejectAtomically},
    {"Move withdrawal from real combat",moveWithdrawalClearsCombatAndCompletesRearFormation},
    {"AttackMove combat and queued facing",attackMoveAndQueuedFacingSurviveCombatInFifoOrder},
    {"Defend combat return facing",defendFightsThenReturnsAndRestoresFacing},
    {"loaded Wide Defend jam recovery",loadedWideDefendJamRecoversEverySavedAnchor},
    {"formation clear stop and replacement",clearStopReplacementAndSustainedOrdersCleanFacingState},
    {"save-load replay and continuation",saveThirteenLoadReplayAndContinuationPreserveFacing},
    {"genuine save-twelve migration",genuineSaveTwelveMigratesWithoutInventingFacing},
    {"save-thirteen formation corruption",saveThirteenRejectsCorruptFormationRowsAtomically},
  };
  int failed=0;
  for(const auto& [name,test]:tests) {
    try {test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
  }
  std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
