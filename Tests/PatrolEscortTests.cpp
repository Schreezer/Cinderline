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

bool emptySustained(const SustainedOrderState& state) {
  return samePoint(state.patrolOrigin,{})&&samePoint(state.patrolDestination,{})&&
         !state.patrolTowardDestination&&state.escortTarget==0&&
         samePoint(state.escortOffset,{})&&state.pursuitTarget==0&&
         samePoint(state.pursuitAnchor,{})&&state.phase==SustainedOrderPhase::Travel;
}

std::string entityState(const Entity* entity) {
  if(!entity)return "missing";
  std::ostringstream output;
  output<<"id="<<entity->id<<" pos="<<entity->pos.x<<','<<entity->pos.y
        <<" goal="<<entity->goal.x<<','<<entity->goal.y
        <<" order="<<static_cast<int>(entity->order)
        <<" phase="<<static_cast<int>(entity->sustained.phase)
        <<" direction="<<entity->sustained.patrolTowardDestination
        <<" carried="<<entity->carried<<" returning="<<entity->returning
        <<" workTarget="<<entity->workTarget<<" pathIndex="<<entity->pathIndex
        <<" pathSize="<<entity->path.size()<<" repath="<<entity->repath
        <<" failures="<<entity->navigationFailures
        <<" exhausted="<<entity->navigationExhausted;
  return output.str();
}

Simulation fixture() {
  Simulation simulation;
  // Tests replace terrain with their own flat corridors and traffic fixtures.
  simulation.reset({0,0xE5C012u,false,1,MatchLength::Standard,2,0});
  const_cast<std::vector<Entity>&>(simulation.entities()).clear();
  const_cast<std::vector<Obstacle>&>(simulation.obstacles()).clear();
  simulation.debugSpawn(Kind::Headquarters,0,{650,650});
  simulation.debugSpawn(Kind::Headquarters,1,{4250,4250});
  simulation.debugResources(0,10000);
  return simulation;
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
  throw std::runtime_error("cannot locate Patrol/Escort fixture "+name);
}

CommandResult send(Simulation& simulation,CommandType type,int team,std::vector<Id> units,
                   Vec2 point={},Id target=0,Kind kind=Kind::Worker,
                   CommandQueueMode mode=CommandQueueMode::Replace) {
  return simulation.command({type,team,std::move(units),point,target,kind,0,mode});
}

void step(Simulation& simulation,int count=1) {
  for(int index=0;index<count;++index) {
    const auto before=simulation.tick();
    simulation.update(Simulation::Step);
    check(simulation.tick()==before+1&&simulation.winner()==-1,
          "patrol and escort fixture remains an active match");
  }
}

template<class Predicate>
void stepUntil(Simulation& simulation,int maximum,Predicate complete,const std::string& failure) {
  for(int index=0;index<maximum&&!complete();++index)step(simulation);
  check(complete(),failure);
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

std::size_t sustainedRow(const std::vector<std::string>& lines,std::size_t marker,Id id) {
  const auto count=static_cast<std::size_t>(std::stoul(lines.at(marker+1)));
  for(std::size_t index=0;index<count;++index) {
    const auto row=fields(lines.at(marker+2+index));
    if(!row.empty()&&row.front()==std::to_string(id))return marker+2+index;
  }
  throw std::runtime_error("sustained save fixture cannot find an entity row");
}

void mixedPatrolCompletesImmutableLaps() {
  auto simulation=fixture();
  const std::vector<Kind> kinds{Kind::Striker,Kind::Lancer,Kind::Scout,Kind::Mender};
  std::vector<Id> units;
  for(std::size_t index=0;index<kinds.size();++index)
    units.push_back(simulation.debugSpawn(kinds[index],0,{950.0f,1250.0f+index*90.0f}));
  check(send(simulation,CommandType::Patrol,0,units,{1650,1385}).accepted,
        "mixed-speed group accepts Patrol");

  struct Route {Vec2 origin,destination;bool lastDirection=true;int completedLegs=0;};
  std::vector<Route> routes;
  for(Id id:units) {
    const Entity* unit=simulation.find(id);
    check(unit&&unit->order==Order::Patrol&&unit->target==0&&unit->supportTarget==0&&
          unit->futureOrders.empty()&&unit->sustained.patrolTowardDestination,
          "Patrol begins on the accepted outward leg without tactical residue");
    routes.push_back({unit->sustained.patrolOrigin,unit->sustained.patrolDestination,
                      unit->sustained.patrolTowardDestination,0});
  }
  for(std::size_t left=0;left<routes.size();++left)for(std::size_t right=left+1;right<routes.size();++right)
    check(distance(routes[left].destination,routes[right].destination)>30,
          "group Patrol stores distinct formation destinations");

  for(int tick=0;tick<1400;++tick) {
    step(simulation);
    for(std::size_t index=0;index<units.size();++index) {
      const Entity* unit=simulation.find(units[index]);const Route& accepted=routes[index];
      check(unit&&unit->order==Order::Patrol&&samePoint(unit->sustained.patrolOrigin,accepted.origin)&&
            samePoint(unit->sustained.patrolDestination,accepted.destination),
            "combat speed and formation traffic never rewrite accepted Patrol endpoints");
      const Vec2 active=unit->sustained.patrolTowardDestination?accepted.destination:accepted.origin;
      check(samePoint(unit->goal,active),"Patrol goal always names its active accepted endpoint");
      if(unit->sustained.patrolTowardDestination!=routes[index].lastDirection) {
        ++routes[index].completedLegs;
        routes[index].lastDirection=unit->sustained.patrolTowardDestination;
      }
    }
  }
  check(std::all_of(routes.begin(),routes.end(),[](const Route& route){return route.completedLegs>=4;}),
        "every mixed-speed unit, including the Mender, independently completes at least two full Patrol laps");
}

void patrolPursuitReturnsWithoutRetargeting() {
  auto simulation=fixture();
  const Id patrol=simulation.debugSpawn(Kind::Striker,0,{1000,1900});
  const Id enemy=simulation.debugSpawn(Kind::Mender,1,{1360,1900});
  check(send(simulation,CommandType::Hold,1,{enemy}).accepted&&
        send(simulation,CommandType::Patrol,0,{patrol},{2200,1900}).accepted,
        "Patrol pursuit fixture accepts ordinary orders");
  const Vec2 origin=simulation.find(patrol)->sustained.patrolOrigin;
  const Vec2 destination=simulation.find(patrol)->sustained.patrolDestination;
  stepUntil(simulation,80,[&]{return simulation.find(patrol)->sustained.phase==SustainedOrderPhase::Pursuit;},
            "visible enemy did not trigger bounded Patrol pursuit");
  const Vec2 firstAnchor=simulation.find(patrol)->sustained.pursuitAnchor;
  check(simulation.find(patrol)->sustained.pursuitTarget==enemy&&
        distance(firstAnchor,simulation.find(patrol)->pos)<40,
        "Patrol records its acquisition target and fixed route anchor");
  check(send(simulation,CommandType::Move,1,{enemy},{1360,2800}).accepted,
        "pursued enemy can leave the fixed Patrol leash");
  stepUntil(simulation,260,[&]{return simulation.find(patrol)->sustained.phase==SustainedOrderPhase::Return&&
                                      !simulation.visible(0,simulation.find(enemy)->pos);},
            "fog loss did not send Patrol into endpoint Return");
  const Vec2 anchor=simulation.find(patrol)->sustained.pursuitAnchor;
  check(simulation.find(patrol)->sustained.phase==SustainedOrderPhase::Return&&
        simulation.find(patrol)->sustained.pursuitTarget==0,
        "fog loss clears the private incidental target and keeps the unit in Return");
  const Id decoy=simulation.debugSpawn(Kind::Mender,1,{anchor.x+90,anchor.y+30});
  const Id endpointEnemy=simulation.debugSpawn(Kind::Mender,1,{2350,1900});
  check(send(simulation,CommandType::Hold,1,{decoy,endpointEnemy}).accepted,
        "non-damaging enemies hold at the old anchor and completed endpoint");
  const float decoyHp=simulation.find(decoy)->hp;
  const float retreatingHp=simulation.find(enemy)->hp;
  const float endpointHp=simulation.find(endpointEnemy)->hp;
  bool resumed=false;
  for(int tick=0;tick<320&&!resumed;++tick) {
    const Entity* unit=simulation.find(patrol);
    check(unit->target==0&&unit->supportTarget==0&&unit->sustained.pursuitTarget==0&&
          samePoint(unit->sustained.pursuitAnchor,anchor)&&samePoint(unit->sustained.patrolOrigin,origin)&&
          samePoint(unit->sustained.patrolDestination,destination)&&unit->sustained.patrolTowardDestination&&
          samePoint(unit->goal,destination),
          "Return preserves its anchor and completes the unchanged active endpoint without hidden tracking");
    check(simulation.find(decoy)->hp==decoyHp&&simulation.find(enemy)->hp==retreatingHp&&
          simulation.find(endpointEnemy)->hp==endpointHp,
          "Return suppresses reacquisition along the entire interrupted leg");
    step(simulation);resumed=simulation.find(patrol)->sustained.phase!=SustainedOrderPhase::Return;
  }
  check(resumed&&!simulation.find(patrol)->sustained.patrolTowardDestination&&
        distance(simulation.find(patrol)->pos,destination)<22,
        "Patrol reaches the interrupted endpoint before switching legs: "+
          entityState(simulation.find(patrol)));
  stepUntil(simulation,6,[&]{return simulation.find(patrol)->sustained.phase==SustainedOrderPhase::Pursuit;},
            "Patrol did not reacquire the enemy waiting at the completed endpoint");
  check(simulation.find(patrol)->sustained.pursuitTarget==endpointEnemy&&
        distance(simulation.find(patrol)->sustained.pursuitAnchor,destination)<30&&
        distance(simulation.find(patrol)->sustained.pursuitAnchor,anchor)>600,
        "post-endpoint reacquisition starts a fresh anchor instead of ratcheting from the earlier chase");

  auto killed=fixture();
  const Id killer=killed.debugSpawn(Kind::Striker,0,{1000,2350});
  const Id victim=killed.debugSpawn(Kind::Worker,1,{1300,2350});
  check(send(killed,CommandType::Hold,1,{victim}).accepted&&
        send(killed,CommandType::Patrol,0,{killer},{1800,2350}).accepted,
        "kill-resume fixture accepts Patrol and Hold");
  const bool leg=killed.find(killer)->sustained.patrolTowardDestination;
  stepUntil(killed,500,[&]{const Entity* value=killed.find(victim);return !value||!value->alive();},
            "ordinary Patrol combat did not defeat its incidental target");
  stepUntil(killed,300,[&]{return killed.find(killer)->sustained.phase==SustainedOrderPhase::Travel;},
            "Patrol did not finish its interrupted endpoint after an incidental kill");
  check(killed.find(killer)->order==Order::Patrol&&
        killed.find(killer)->sustained.patrolTowardDestination!=leg&&
        distance(killed.find(killer)->pos,killed.find(killer)->sustained.patrolDestination)<22&&
        killed.players()[0].stats.killed==1,
        "incidental kill completes the interrupted endpoint exactly once before switching legs");
}

void blockedAndStationaryPatrolsRemainStable() {
  auto simulation=fixture();
  auto& obstacles=const_cast<std::vector<Obstacle>&>(simulation.obstacles());
  obstacles.push_back({{1500,1065},{110,415}});
  obstacles.push_back({{1500,2135},{110,415}});
  const Id builder=simulation.debugSpawn(Kind::Worker,0,{1270,1600});
  const Id patrol=simulation.debugSpawn(Kind::Striker,0,{1040,1600});
  check(send(simulation,CommandType::Patrol,0,{patrol},{2000,1600}).accepted,
        "corridor Patrol is accepted before the route is blocked");
  const Vec2 origin=simulation.find(patrol)->sustained.patrolOrigin;
  const Vec2 destination=simulation.find(patrol)->sustained.patrolDestination;
  const CommandResult construction=send(simulation,CommandType::Build,0,{builder},{1500,1600},0,Kind::Foundry);
  check(construction.accepted,"ordinary construction closes the authored corridor: "+construction.message);
  Id foundation=0;
  for(const auto& entity:simulation.entities())if(entity.kind==Kind::Foundry&&entity.team==0)foundation=entity.id;
  check(foundation!=0,"corridor fixture creates its blocking foundation");
  step(simulation,140);
  check(simulation.find(patrol)->order==Order::Patrol&&
        samePoint(simulation.find(patrol)->sustained.patrolOrigin,origin)&&
        samePoint(simulation.find(patrol)->sustained.patrolDestination,destination)&&
        simulation.find(patrol)->pos.x<1450,
        "blocked navigation retains Patrol intent without crossing the closed corridor");
  check(send(simulation,CommandType::CancelBuilding,0,{foundation}).accepted,
        "ordinary cancellation reopens the Patrol corridor");
  stepUntil(simulation,350,[&]{return simulation.find(patrol)->pos.x>1660;},
            "Patrol did not recover after its construction blockage was removed");

  auto stationary=fixture();
  const Id unit=stationary.debugSpawn(Kind::Scout,0,{2600,900});
  const auto searches=stationary.navigationStats().searches;
  check(send(stationary,CommandType::Patrol,0,{unit},{2600,900}).accepted,
        "zero-length Patrol is a valid stationary sustained order");
  const bool direction=stationary.find(unit)->sustained.patrolTowardDestination;
  step(stationary,120);
  check(stationary.find(unit)->order==Order::Patrol&&
        stationary.find(unit)->sustained.patrolTowardDestination==direction&&
        distance(stationary.find(unit)->pos,{2600,900})<1&&
        stationary.navigationStats().searches<=searches+1,
        "zero-length Patrol stays stationary without direction or navigation churn");
}

void escortPreservesLeaderAndStableGroundAirSlots() {
  auto simulation=fixture();
  const Id leader=simulation.debugSpawn(Kind::Scout,0,{1200,2900});
  const std::vector<Id> followers{
    simulation.debugSpawn(Kind::Striker,0,{980,2750}),
    simulation.debugSpawn(Kind::Lancer,0,{980,2850}),
    simulation.debugSpawn(Kind::Mender,0,{980,2950}),
    simulation.debugSpawn(Kind::Worker,0,{980,3050})};
  check(send(simulation,CommandType::Move,0,{leader},{2050,3050}).accepted&&
        send(simulation,CommandType::Move,0,{leader},{2500,2900},0,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "escort leader begins a real two-step route");
  const Order leaderOrder=simulation.find(leader)->order;const Vec2 leaderGoal=simulation.find(leader)->goal;
  const auto leaderTail=simulation.find(leader)->futureOrders;
  std::vector<Id> selection{leader};selection.insert(selection.end(),followers.begin(),followers.end());
  check(send(simulation,CommandType::Escort,0,selection,{},leader).accepted,
        "mixed group accepts Escort while including its selected leader");
  check(simulation.find(leader)->order==leaderOrder&&samePoint(simulation.find(leader)->goal,leaderGoal)&&
        simulation.find(leader)->futureOrders.size()==leaderTail.size()&&emptySustained(simulation.find(leader)->sustained),
        "Escort excludes the leader without changing its current or queued orders");
  std::vector<Vec2> offsets;
  for(Id id:followers) {
    const Entity* follower=simulation.find(id);offsets.push_back(follower->sustained.escortOffset);
    check(follower->order==Order::Escort&&follower->sustained.escortTarget==leader&&
          distance(follower->sustained.escortOffset,{})<=Simulation::MaxEscortOffset&&
          distance(follower->sustained.escortOffset,{})>
            definition(follower->kind).radius+definition(Kind::Scout).radius,
          "each follower receives a bounded slot outside the leader footprint");
  }
  for(std::size_t left=0;left<offsets.size();++left)for(std::size_t right=left+1;right<offsets.size();++right)
    check(!samePoint(offsets[left],offsets[right]),"Escort assigns distinct deterministic slots");
  for(int tick=0;tick<180;++tick) {
    step(simulation);
    for(std::size_t index=0;index<followers.size();++index)
      check(samePoint(simulation.find(followers[index])->sustained.escortOffset,offsets[index]),
            "ground Escort slots remain fixed in world axes while the leader moves");
  }
  check(distance(simulation.find(leader)->pos,{1200,2900})>500,"leader advances independently of its escorts");

  auto air=fixture();
  const Id kite=air.debugSpawn(Kind::Kite,0,{1150,3600});
  const Id guard=air.debugSpawn(Kind::Scout,0,{950,3500});
  check(send(air,CommandType::Move,0,{kite},{2150,3700}).accepted&&
        send(air,CommandType::Escort,0,{guard},{},kite).accepted,
        "ground escort accepts a moving air leader");
  const Vec2 offset=air.find(guard)->sustained.escortOffset;
  step(air,140);
  const Vec2 expected{air.find(kite)->pos.x+offset.x,air.find(kite)->pos.y+offset.y};
  check(air.find(guard)->order==Order::Escort&&samePoint(air.find(guard)->sustained.escortOffset,offset)&&
        distance(air.find(guard)->goal,expected)<1,
        "ground follower tracks the air leader at its stable bounded slot");
}

void escortEdgesStayDistinctAndFailedRoutesRetry() {
  auto edge=fixture();
  const Id leader=edge.debugSpawn(Kind::Scout,0,{30,30});
  // Eight recipients consume the complete first square ring. Heavy units on
  // both opposing axes exercise boundary folding that must retain footprints.
  const std::vector<Kind> kinds{Kind::Worker,Kind::Bastion,Kind::Lancer,Kind::Bastion,
                                Kind::Bastion,Kind::Mender,Kind::Bastion,Kind::Kite};
  std::vector<Id> followers;
  for(std::size_t index=0;index<kinds.size();++index)
    followers.push_back(edge.debugSpawn(kinds[index],0,{300.0f+index*75.0f,260.0f}));
  check(send(edge,CommandType::Escort,0,followers,{},leader).accepted,
        "mixed escorts accept slots around a leader in the map corner");
  std::vector<Vec2> offsets;
  for(Id id:followers)offsets.push_back(edge.find(id)->sustained.escortOffset);
  auto legalDistinctGoals=[&](const std::string& context) {
    std::vector<Vec2> goals;
    for(std::size_t index=0;index<followers.size();++index) {
      const Entity* follower=edge.find(followers[index]);const float radius=definition(follower->kind).radius;
      check(follower->order==Order::Escort&&samePoint(follower->sustained.escortOffset,offsets[index])&&
            follower->goal.x>=radius&&follower->goal.y>=radius&&
            follower->goal.x<=edge.worldSize()-radius&&follower->goal.y<=edge.worldSize()-radius&&
            distance(follower->goal,edge.find(leader)->pos)>=
              radius+definition(Kind::Scout).radius,
            context+" keeps immutable slots at legal goals outside the leader footprint");
      goals.push_back(follower->goal);
    }
    for(std::size_t left=0;left<goals.size();++left)for(std::size_t right=left+1;right<goals.size();++right) {
      const float clearance=definition(kinds[left]).radius+definition(kinds[right]).radius;
      check(distance(goals[left],goals[right])>=clearance,
            context+" keeps every grouped Escort footprint distinct at the boundary");
    }
  };
  legalDistinctGoals("corner reprojection");
  check(send(edge,CommandType::Move,0,{leader},{30,900}).accepted,
        "corner leader begins an ordinary move along the world edge");
  for(int tick=0;tick<100;++tick) {
    step(edge);
    if(tick%10==0)legalDistinctGoals("moving-edge reprojection");
  }
  check(edge.find(leader)->pos.y>700,"edge leader actually moves while goals are reprojected");

  auto retry=fixture();
  const_cast<std::vector<Obstacle>&>(retry.obstacles()).push_back({{2400,2400},{80,2400}});
  const Id airLeader=retry.debugSpawn(Kind::Kite,0,{2850,2400});
  const Id groundGuard=retry.debugSpawn(Kind::Scout,0,{1800,2400});
  check(send(retry,CommandType::Escort,0,{groundGuard},{},airLeader).accepted,
        "ground escort accepts an air leader across an impassable divider");
  const Vec2 retryOffset=retry.find(groundGuard)->sustained.escortOffset;
  stepUntil(retry,220,[&]{return retry.find(groundGuard)->navigationExhausted;},
            "unreachable air-leader slot did not report navigation exhaustion");
  const Vec2 failedGoal=retry.find(groundGuard)->goal;
  check(failedGoal.x>2480,"initial Escort slot lies on the unreachable side of the divider");
  check(send(retry,CommandType::Move,0,{airLeader},{1650,2850}).accepted,
        "air leader can move its slot across the divider through ordinary flight");
  stepUntil(retry,360,[&] {
    const Entity* guard=retry.find(groundGuard);
    return guard->goal.x<2250&&!guard->navigationExhausted&&distance(guard->pos,guard->goal)<80;
  },"ground Escort did not retry after the moving air leader made its slot reachable");
  check(retry.find(groundGuard)->order==Order::Escort&&
        retry.find(groundGuard)->sustained.escortTarget==airLeader&&
        samePoint(retry.find(groundGuard)->sustained.escortOffset,retryOffset),
        "failed-route recovery preserves Escort intent and its assigned slot");
}

void sequentialEscortAssignmentAndReassignmentReserveSlots() {
  auto simulation=fixture();
  const Id firstLeader=simulation.debugSpawn(Kind::Scout,0,{1700,2150});
  const Id secondLeader=simulation.debugSpawn(Kind::Kite,0,{3100,2150});
  const Id first=simulation.debugSpawn(Kind::Bastion,0,{1400,2050});
  const Id reassigned=simulation.debugSpawn(Kind::Bastion,0,{1400,2200});
  const Id secondExisting=simulation.debugSpawn(Kind::Lancer,0,{2800,2200});
  check(send(simulation,CommandType::Move,0,{firstLeader},{2050,2300}).accepted&&
        send(simulation,CommandType::Move,0,{secondLeader},{3450,2300}).accepted,
        "independent Escort leaders begin ordinary movement");
  const Vec2 firstLeaderGoal=simulation.find(firstLeader)->goal;
  const Vec2 secondLeaderGoal=simulation.find(secondLeader)->goal;

  check(send(simulation,CommandType::Escort,0,{first},{},firstLeader).accepted,
        "first independently assigned follower accepts Escort");
  const Vec2 firstOffset=simulation.find(first)->sustained.escortOffset;
  check(send(simulation,CommandType::Escort,0,{reassigned},{},firstLeader).accepted,
        "second independently assigned follower reserves a different live slot");
  const Vec2 reassignedOffset=simulation.find(reassigned)->sustained.escortOffset;
  check(!samePoint(firstOffset,reassignedOffset)&&
        distance(simulation.find(first)->goal,simulation.find(reassigned)->goal)>=
          definition(Kind::Bastion).radius*2,
        "sequential same-leader commands keep both heavy follower footprints clear");
  check(simulation.find(first)->sustained.escortTarget==firstLeader&&
        samePoint(simulation.find(first)->sustained.escortOffset,firstOffset),
        "later independent assignment does not rewrite an existing follower slot");

  check(send(simulation,CommandType::Escort,0,{secondExisting},{},secondLeader).accepted,
        "second leader receives an independent existing follower");
  const Vec2 secondExistingOffset=simulation.find(secondExisting)->sustained.escortOffset;
  check(send(simulation,CommandType::Escort,0,{reassigned},{},secondLeader).accepted,
        "follower can be reassigned to a leader with a reserved slot");
  check(simulation.find(reassigned)->sustained.escortTarget==secondLeader&&
        distance(simulation.find(reassigned)->goal,simulation.find(secondExisting)->goal)>=
          definition(Kind::Bastion).radius+definition(Kind::Lancer).radius,
        "reassignment selects a nonoverlapping slot at the new leader");
  check(simulation.find(first)->sustained.escortTarget==firstLeader&&
        samePoint(simulation.find(first)->sustained.escortOffset,firstOffset)&&
        simulation.find(secondExisting)->sustained.escortTarget==secondLeader&&
        samePoint(simulation.find(secondExisting)->sustained.escortOffset,secondExistingOffset)&&
        samePoint(simulation.find(firstLeader)->goal,firstLeaderGoal)&&
        samePoint(simulation.find(secondLeader)->goal,secondLeaderGoal),
        "reassignment preserves both leaders and every other follower's accepted offset");

  // Synthetic capacity setup: occupy every public 96-unit lattice point inside
  // the 2048-unit cap, then prove one ordinary command rejects as a whole.
  auto full=fixture();
  const Id leader=full.debugSpawn(Kind::Scout,0,{2400,2400});
  const Id waiting=full.debugSpawn(Kind::Worker,0,{400,2400});
  auto& entities=const_cast<std::vector<Entity>&>(full.entities());
  Id syntheticId=100000;
  const int rings=static_cast<int>(Simulation::MaxEscortOffset/Simulation::EscortSpacing);
  for(int y=-rings;y<=rings;++y)for(int x=-rings;x<=rings;++x) {
    const Vec2 offset{x*Simulation::EscortSpacing,y*Simulation::EscortSpacing};
    if((x==0&&y==0)||distance(offset,{})>Simulation::MaxEscortOffset)continue;
    Entity follower;
    follower.id=syntheticId++;follower.kind=Kind::Worker;follower.team=0;
    follower.pos={2400+offset.x,2400+offset.y};follower.goal=follower.pos;follower.rally=follower.pos;
    follower.hp=definition(Kind::Worker).hp;follower.progress=1;follower.order=Order::Escort;
    follower.sustained.escortTarget=leader;follower.sustained.escortOffset=offset;
    entities.push_back(std::move(follower));
  }
  const auto hash=full.stateHash();const auto recorded=full.recording().size();
  const CommandResult noSlot=send(full,CommandType::Escort,0,{waiting},{},leader);
  check(!noSlot.accepted&&noSlot.message.find("formation")!=std::string::npos,
        "Escort rejects specifically because every bounded formation slot is reserved");
  check(full.stateHash()==hash&&full.recording().size()==recorded&&
        full.find(waiting)->order==Order::Idle&&emptySustained(full.find(waiting)->sustained),
        "no-slot rejection leaves the recipient, existing formation, leaders, and recording unchanged");
}

void farEscortApproachDelaysLeaderBypassUntilLocal() {
  auto simulation=fixture();
  const Vec2 leaderPosition{2100,2500};
  const Id leader=simulation.debugSpawn(Kind::Bastion,0,leaderPosition);
  const std::vector<Kind> arrivedKinds{Kind::Striker,Kind::Lancer,Kind::Scout};
  const std::vector<Vec2> arrivedOffsets{{-96,-96},{0,-96},{96,-96}};
  std::vector<Id> arrived;
  for(std::size_t index=0;index<arrivedKinds.size();++index)
    arrived.push_back(simulation.debugSpawn(arrivedKinds[index],0,
      {leaderPosition.x+arrivedOffsets[index].x,leaderPosition.y+arrivedOffsets[index].y}));
  check(send(simulation,CommandType::Escort,0,arrived,{},leader).accepted,
        "ordinary Escort reserves the first three slots around an Idle leader");
  std::vector<Vec2> acceptedOffsets,acceptedGoals;
  for(Id id:arrived) {
    acceptedOffsets.push_back(simulation.find(id)->sustained.escortOffset);
    acceptedGoals.push_back(simulation.find(id)->goal);
  }

  const Id approaching=simulation.debugSpawn(Kind::Bastion,0,{3400,2500});
  const Entity leaderBefore=*simulation.find(leader);
  check(send(simulation,CommandType::Escort,0,{approaching},{},leader).accepted,
        "far-east Bastion accepts a separately reserved Escort slot");
  const Entity* accepted=simulation.find(approaching);
  check(accepted->sustained.escortOffset.x==-96&&accepted->sustained.escortOffset.y==0,
        "fourth follower receives the west slot behind the stationary leader");
  const Vec2 offset=accepted->sustained.escortOffset;const Vec2 goal=accepted->goal;
  const float initialDistance=distance(accepted->pos,goal);
  for(std::size_t index=0;index<arrived.size();++index)
    check(distance(goal,acceptedGoals[index])>=
            definition(Kind::Bastion).radius+definition(arrivedKinds[index]).radius,
          "far follower's west destination clears every reserved follower footprint");
  for(int tick=0;tick<40;++tick) {
    step(simulation);
    const Entity* follower=simulation.find(approaching);const Entity* stationary=simulation.find(leader);
    check(follower&&follower->order==Order::Escort&&follower->sustained.escortTarget==leader&&
          samePoint(follower->sustained.escortOffset,offset)&&samePoint(follower->goal,goal),
          "far approach retains the separately accepted west Escort slot");
    check(stationary&&stationary->order==leaderBefore.order&&samePoint(stationary->pos,leaderBefore.pos)&&
          samePoint(stationary->goal,leaderBefore.goal),
          "far approach neither moves nor retasks the stationary leader");
    check(distance(follower->pos,stationary->pos)>=definition(Kind::Bastion).radius*2-0.01f,
          "far approach does not cross the leader footprint");
  }
  check(initialDistance-distance(simulation.find(approaching)->pos,goal)>150,
        "far Escort closes substantial normal distance before local leader bypass activates; "+
          entityState(simulation.find(approaching)));

  int stableTicks=0;
  for(int tick=0;tick<600&&stableTicks<20;++tick) {
    step(simulation);
    const Entity* follower=simulation.find(approaching);const Entity* stationary=simulation.find(leader);
    check(follower&&follower->order==Order::Escort&&follower->sustained.escortTarget==leader&&
          samePoint(follower->sustained.escortOffset,offset)&&samePoint(follower->goal,goal),
          "local bypass retains the far follower's accepted Escort state");
    check(stationary&&samePoint(stationary->pos,leaderBefore.pos)&&stationary->order==leaderBefore.order,
          "local bypass preserves the Idle leader position and order");
    check(distance(follower->pos,stationary->pos)>=definition(Kind::Bastion).radius*2-0.01f,
          "local bypass routes around the leader footprint");
    for(std::size_t index=0;index<arrived.size();++index) {
      const Entity* existing=simulation.find(arrived[index]);
      check(existing&&existing->order==Order::Escort&&existing->sustained.escortTarget==leader&&
            samePoint(existing->sustained.escortOffset,acceptedOffsets[index])&&
            samePoint(existing->goal,acceptedGoals[index]),
            "far follower never rewrites an already reserved Escort slot");
    }
    stableTicks=distance(follower->pos,goal)<=Simulation::SustainedReturnTolerance?
      stableTicks+1:0;
  }
  check(stableTicks==20,
        "far-east Bastion completes local leader bypass and remains at the west slot; "+
          entityState(simulation.find(approaching)));
}

void escortTraversesAroundIdleLeaderAndArrivedFollowers() {
  auto simulation=fixture();
  const Vec2 leaderPosition{1818.09f,3299.67f};
  const Id leader=simulation.debugSpawn(Kind::Bastion,0,leaderPosition);
  const std::vector<Kind> kinds{Kind::Striker,Kind::Lancer,Kind::Scout,Kind::Bastion,
                                Kind::Mortar,Kind::Mender,Kind::Kite,Kind::Worker};
  const std::vector<Vec2> positions{
    {1722.09f,3203.67f},{1818.09f,3203.67f},{1914.09f,3203.67f},
    {1882.54f,3270.59f},{1931.57f,3315.62f},{1722.09f,3395.67f},
    {1818.09f,3395.67f},{1914.09f,3395.67f}};
  std::vector<Id> followers;
  for(std::size_t index=0;index<kinds.size();++index)
    followers.push_back(simulation.debugSpawn(kinds[index],0,positions[index]));
  constexpr std::size_t crossingIndex=3;
  const Id crossing=followers[crossingIndex];
  check(leader==35&&followers.front()==36&&crossing==39&&followers.back()==43,
        "congestion fixture retains the benchmark's leader and first-ring identities");
  const Entity leaderBefore=*simulation.find(leader);
  check(send(simulation,CommandType::Escort,0,followers,{},leader).accepted,
        "recorded congestion fixture accepts its complete first-ring Escort formation");
  check(simulation.find(crossing)->sustained.escortOffset.x==-96&&
        simulation.find(crossing)->sustained.escortOffset.y==0,
        "east-side Bastion receives the recorded west-side Escort slot");

  std::vector<Vec2> offsets,goals;
  for(Id id:followers) {
    offsets.push_back(simulation.find(id)->sustained.escortOffset);
    goals.push_back(simulation.find(id)->goal);
  }
  for(std::size_t left=0;left<followers.size();++left)for(std::size_t right=left+1;right<followers.size();++right)
    check(distance(goals[left],goals[right])>=
            definition(simulation.find(followers[left])->kind).radius+
            definition(simulation.find(followers[right])->kind).radius,
          "congested Escort destinations preserve complete follower footprints");

  bool passedLeader=false;int stableTicks=0;
  std::vector<std::string> finalTrace;
  for(int tick=0;tick<1200&&stableTicks<20;++tick) {
    step(simulation);
    const Entity* mover=simulation.find(crossing);const Entity* stationary=simulation.find(leader);
    check(stationary&&stationary->order==leaderBefore.order&&
          samePoint(stationary->pos,leaderBefore.pos)&&samePoint(stationary->goal,leaderBefore.goal),
          "Escort traversal never moves or retasks its Idle leader");
    check(mover&&mover->order==Order::Escort&&mover->sustained.escortTarget==leader&&
          samePoint(mover->sustained.escortOffset,offsets[crossingIndex])&&
          samePoint(mover->goal,goals[crossingIndex]),
          "congestion recovery retains the Bastion's accepted Escort destination");
    const float leaderDistance=distance(mover->pos,stationary->pos);
    const float leaderClearance=definition(Kind::Bastion).radius*2;
    if(leaderDistance<leaderClearance-0.01f) {
      std::ostringstream failure;
      failure<<"crossing follower never traverses its leader's physical footprint"
             <<" tick="<<simulation.tick()<<" distance="<<leaderDistance
             <<" clearance="<<leaderClearance
             <<" mover={"<<entityState(mover)<<"} leader={"<<entityState(stationary)<<"} nearby=";
      bool nearby=false;
      for(const auto& other:simulation.entities()) {
        if(other.id==crossing||!other.alive()||definition(other.kind).air!=definition(mover->kind).air)continue;
        const float separation=distance(mover->pos,other.pos);
        if(separation>85)continue;
        nearby=true;
        const float clearance=definition(mover->kind).radius+definition(other.kind).radius;
        failure<<'['<<other.id<<" kind="<<static_cast<int>(other.kind)
               <<" pos="<<other.pos.x<<','<<other.pos.y
               <<" distance="<<separation<<" clearance="<<clearance
               <<" overlap="<<(separation<clearance)
               <<" order="<<static_cast<int>(other.order)
               <<" stalled="<<other.stalledFor<<" failures="<<other.navigationFailures
               <<" yield="<<other.yieldFor<<']';
      }
      if(!nearby)failure<<"none";
      throw std::runtime_error(failure.str());
    }
    for(std::size_t index=0;index<followers.size();++index) {
      if(index==crossingIndex)continue;
      const Entity* arrived=simulation.find(followers[index]);
      check(arrived&&arrived->order==Order::Escort&&arrived->sustained.escortTarget==leader&&
            samePoint(arrived->sustained.escortOffset,offsets[index])&&samePoint(arrived->goal,goals[index]),
            "nearby arrived followers preserve their orders, offsets, and destinations");
    }
    if(mover->pos.x<stationary->pos.x)passedLeader=true;
    stableTicks=distance(mover->pos,mover->goal)<=Simulation::SustainedReturnTolerance?
      stableTicks+1:0;
    std::ostringstream sample;
    sample<<"tick="<<simulation.tick()<<" crossing="<<entityState(mover)
          <<" goalDistance="<<distance(mover->pos,mover->goal)
          <<" stalled="<<mover->stalledFor<<" yield="<<mover->yieldFor;
    if(mover->pathIndex>=0&&mover->pathIndex<static_cast<int>(mover->path.size())) {
      const Vec2 waypoint=mover->path[static_cast<std::size_t>(mover->pathIndex)];
      sample<<" waypoint="<<waypoint.x<<','<<waypoint.y
            <<" waypointDistance="<<distance(mover->pos,waypoint);
    } else sample<<" waypoint=none";
    sample<<" nearby=";
    bool nearby=false;
    for(const auto& other:simulation.entities()) {
      if(other.id==crossing||!other.alive()||definition(other.kind).air!=definition(mover->kind).air)continue;
      const float separation=distance(mover->pos,other.pos);
      if(separation>85)continue;
      nearby=true;
      const float clearance=definition(mover->kind).radius+definition(other.kind).radius;
      sample<<'['<<other.id<<" kind="<<static_cast<int>(other.kind)
            <<" distance="<<separation<<" clearance="<<clearance
            <<" overlap="<<(separation<clearance)
            <<" order="<<static_cast<int>(other.order)
            <<" stalled="<<other.stalledFor<<" failures="<<other.navigationFailures
            <<" yield="<<other.yieldFor<<']';
    }
    if(!nearby)sample<<"none";
    finalTrace.push_back(sample.str());
    if(finalTrace.size()>100)finalTrace.erase(finalTrace.begin());
  }
  if(!passedLeader||stableTicks!=20) {
    std::ostringstream failure;
    failure<<"east-side Bastion traverses around the Idle leader and remains at its west slot; "
           <<entityState(simulation.find(crossing));
    for(const auto& sample:finalTrace)failure<<'\n'<<sample;
    throw std::runtime_error(failure.str());
  }
}

void escortCyclesAndInvalidTargetsRejectAtomically() {
  auto simulation=fixture();
  const Id leader=simulation.debugSpawn(Kind::Scout,0,{1100,3800});
  const Id middle=simulation.debugSpawn(Kind::Striker,0,{900,3750});
  const Id tail=simulation.debugSpawn(Kind::Lancer,0,{700,3700});
  const Id innocent=simulation.debugSpawn(Kind::Mender,0,{750,3900});
  check(send(simulation,CommandType::Escort,0,{middle},{},leader).accepted&&
        send(simulation,CommandType::Escort,0,{tail},{},middle).accepted&&
        send(simulation,CommandType::Hold,0,{innocent}).accepted,
        "acyclic Escort chain and independent Hold are accepted");
  auto reject=[&](Command command,const std::string& reason) {
    const auto hash=simulation.stateHash();const auto recorded=simulation.recording().size();
    check(!simulation.command(command).accepted,reason+" is rejected");
    check(simulation.stateHash()==hash&&simulation.recording().size()==recorded,
          reason+" leaves every projected recipient and recording unchanged");
  };
  reject({CommandType::Escort,0,{leader,innocent},{},tail,Kind::Worker,0,CommandQueueMode::Replace},
         "mixed selection containing an indirect Escort cycle");
  reject({CommandType::Escort,0,{leader},{},leader},"selection containing only the leader itself");
  const Id foreign=simulation.debugSpawn(Kind::Scout,1,{1200,3800});
  reject({CommandType::Escort,0,{innocent},{},foreign},"foreign Escort target");
  Id ownHQ=0;for(const auto& entity:simulation.entities())if(entity.team==0&&entity.kind==Kind::Headquarters)ownHQ=entity.id;
  reject({CommandType::Escort,0,{innocent},{},ownHQ},"building Escort target");
  reject({CommandType::Patrol,0,{innocent},{1400,3900},0,Kind::Worker,0,CommandQueueMode::Append},
         "appended Patrol mode");
  reject({CommandType::Escort,0,{innocent},{},leader,Kind::Worker,0,CommandQueueMode::Append},
         "appended Escort mode");
}

void leaderDeathUsesTailBeforeDefendFallback() {
  auto simulation=fixture();
  const Id leader=simulation.debugSpawn(Kind::Scout,0,{1800,1100});
  const Id withTail=simulation.debugSpawn(Kind::Striker,0,{1550,1040});
  const Id fallback=simulation.debugSpawn(Kind::Lancer,0,{1550,1160});
  check(send(simulation,CommandType::Escort,0,{withTail,fallback},{},leader).accepted&&
        send(simulation,CommandType::Move,0,{withTail},{900,900},0,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "one Escort stores an explicit finite successor");
  step(simulation,30);
  const Vec2 acceptedTail=simulation.find(withTail)->futureOrders.front().point;
  std::vector<Id> attackers;
  for(int index=0;index<3;++index)attackers.push_back(simulation.debugSpawn(Kind::Bastion,1,{2070.0f,1040.0f+index*65.0f}));
  check(send(simulation,CommandType::Attack,1,attackers,{},leader).accepted,
        "ordinary enemy combat targets the Escort leader");
  Vec2 lastFollow=simulation.find(fallback)->goal;
  stepUntil(simulation,300,[&]{
    const Entity* alive=simulation.find(leader);
    if(alive&&alive->alive()){lastFollow=simulation.find(fallback)->goal;return false;}
    return true;
  },"ordinary combat did not destroy the Escort leader");
  const Entity* successor=simulation.find(withTail);const Entity* defender=simulation.find(fallback);
  check(successor&&successor->order==Order::Move&&successor->futureOrders.empty()&&
        samePoint(successor->goal,acceptedTail)&&emptySustained(successor->sustained),
        "leader loss activates an explicit queued successor before fallback");
  check(defender&&defender->order==Order::Defend&&
        distance(defender->goal,lastFollow)<=definition(Kind::Scout).speed*Simulation::Step+1&&
        defender->futureOrders.empty()&&emptySustained(defender->sustained),
        "leader loss without a tail Defends the last assigned follow position");
}

void workerCargoAndConstructionSurviveLeaderExclusion() {
  auto mining=fixture();
  const Id depot=mining.debugSpawn(Kind::Processor,0,{1600,1600});
  const Id ore=mining.debugSpawn(Kind::Resource,-1,{1100,1600});
  const Id worker=mining.debugSpawn(Kind::Worker,0,{1170,1600});
  const Id guard=mining.debugSpawn(Kind::Striker,0,{980,1800});
  check(send(mining,CommandType::Gather,0,{worker},{},ore).accepted,
        "worker begins ordinary mining");
  stepUntil(mining,300,[&]{return mining.find(worker)->returning&&mining.find(worker)->carried>0;},
            "worker did not begin a loaded return trip");
  const float cargo=mining.find(worker)->carried;const int gathered=mining.players()[0].stats.gathered;
  check(send(mining,CommandType::Escort,0,{worker,guard},{},worker).accepted,
        "worker leader can be included in its guard selection");
  check(mining.find(worker)->order==Order::Gather&&mining.find(worker)->resourceTarget==ore&&
        mining.find(worker)->carried==cargo&&mining.find(worker)->returning&&
        mining.find(guard)->order==Order::Escort,
        "Escort exclusion preserves the worker's loaded return state and cargo");
  for(int tick=0;tick<350&&mining.players()[0].stats.gathered==gathered;++tick)step(mining);
  check(mining.players()[0].stats.gathered>gathered,
        "escorted worker did not complete its real cargo delivery; worker "+
          entityState(mining.find(worker))+" guard "+entityState(mining.find(guard))+
          " depot "+entityState(mining.find(depot)));
  check(mining.find(depot)&&mining.find(guard)->order==Order::Escort,
        "cargo delivery leaves the guard following the worker leader");

  auto building=fixture();
  const Id builder=building.debugSpawn(Kind::Worker,0,{1000,900});
  const Id escort=building.debugSpawn(Kind::Mender,0,{850,1000});
  check(send(building,CommandType::Build,0,{builder},{1200,900},0,Kind::Foundry).accepted,
        "worker starts an explicit construction job");
  Id foundation=0;for(const auto& entity:building.entities())if(entity.kind==Kind::Foundry&&entity.team==0)foundation=entity.id;
  check(foundation&&send(building,CommandType::Escort,0,{builder,escort},{},builder).accepted,
        "construction worker can remain the selected Escort leader");
  check(building.find(builder)->order==Order::Construct&&building.find(builder)->target==foundation&&
        building.find(foundation)->builderId==builder&&building.find(escort)->order==Order::Escort,
        "leader exclusion preserves both sides of the construction assignment");
  check(send(building,CommandType::CancelBuilding,0,{foundation}).accepted,
        "ordinary cancellation ends the preserved construction job");
  step(building);
  check(building.find(builder)->order!=Order::Construct&&building.find(escort)->order==Order::Escort,
        "construction cancellation recovers the worker while its escort remains valid");
}

void escortDoesNotBlockAnchorCargoDelivery() {
  auto simulation=fixture();
  Id anchor=0;
  for(const auto& entity:simulation.entities())
    if(entity.team==0&&entity.kind==Kind::Headquarters)anchor=entity.id;
  const Id ore=simulation.debugSpawn(Kind::Resource,-1,{900,650});
  const Id worker=simulation.debugSpawn(Kind::Worker,0,{850,650});
  const Id guard=simulation.debugSpawn(Kind::Striker,0,{770,820});
  check(anchor&&send(simulation,CommandType::Gather,0,{worker},{},ore).accepted,
        "starting worker begins a normal Anchor-area mining trip");
  stepUntil(simulation,300,[&]{return simulation.find(worker)->returning&&
                                      simulation.find(worker)->carried>0;},
            "Anchor-area worker did not begin a loaded return trip");
  const float cargo=simulation.find(worker)->carried;
  const int gathered=simulation.players()[0].stats.gathered;
  check(send(simulation,CommandType::Escort,0,{worker,guard},{},worker).accepted,
        "loaded Anchor-area worker remains the selected Escort leader");
  check(simulation.find(worker)->order==Order::Gather&&simulation.find(worker)->returning&&
        simulation.find(worker)->carried==cargo&&simulation.find(guard)->order==Order::Escort,
        "Anchor-area Escort preserves the worker's loaded delivery state");
  for(int tick=0;tick<400&&simulation.players()[0].stats.gathered==gathered;++tick)step(simulation);
  check(simulation.players()[0].stats.gathered>gathered,
        "Escort traffic blocked normal Anchor cargo delivery; worker "+
          entityState(simulation.find(worker))+" guard "+entityState(simulation.find(guard))+
          " anchor "+entityState(simulation.find(anchor)));
  check(simulation.find(guard)->order==Order::Escort&&
        simulation.find(guard)->sustained.escortTarget==worker,
        "guard retains Escort after the worker unloads at the starting Anchor");
}

void clearStopAndReplacementCleanSustainedState() {
  auto simulation=fixture();
  const Id leader=simulation.debugSpawn(Kind::Scout,0,{2800,1500});
  const Id unit=simulation.debugSpawn(Kind::Striker,0,{2600,1500});
  check(send(simulation,CommandType::Patrol,0,{unit},{3300,1500}).accepted&&
        send(simulation,CommandType::Move,0,{unit},{3500,1700},0,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "Patrol stores a finite successor");
  const auto patrolState=simulation.find(unit)->sustained;
  check(send(simulation,CommandType::ClearOrders,0,{unit}).accepted,
        "ClearOrders applies to a sustained order");
  check(simulation.find(unit)->order==Order::Patrol&&simulation.find(unit)->futureOrders.empty()&&
        samePoint(simulation.find(unit)->sustained.patrolOrigin,patrolState.patrolOrigin)&&
        samePoint(simulation.find(unit)->sustained.patrolDestination,patrolState.patrolDestination),
        "ClearOrders removes only the Patrol tail");
  check(send(simulation,CommandType::Stop,0,{unit}).accepted&&
        simulation.find(unit)->order==Order::Idle&&emptySustained(simulation.find(unit)->sustained),
        "Stop clears Patrol state");

  check(send(simulation,CommandType::Escort,0,{unit},{},leader).accepted&&
        send(simulation,CommandType::Move,0,{unit},{2400,1900},0,Kind::Worker,
             CommandQueueMode::Append).accepted&&
        send(simulation,CommandType::ClearOrders,0,{unit}).accepted,
        "Escort tail can be added and explicitly cleared");
  check(simulation.find(unit)->order==Order::Escort&&simulation.find(unit)->futureOrders.empty()&&
        simulation.find(unit)->sustained.escortTarget==leader,
        "ClearOrders preserves current Escort state");
  check(send(simulation,CommandType::Hold,0,{unit}).accepted&&
        simulation.find(unit)->order==Order::Hold&&emptySustained(simulation.find(unit)->sustained),
        "ordinary replacing Hold clears Escort state");
}

void loadedDenseEscortRowRecoversWithoutIntentChanges() {
  // Deterministic synthetic 160-mobile jam captured at tick 9600 with seed
  // 332517; the filename suffix records source state hash 7106836022754952452.
  Simulation simulation;
  check(simulation.load(fixturePath("escort-row-jam-v12.cinder").string()),
        "exact saved dense Escort row loads successfully");
  check(simulation.config().seed==332517&&simulation.tick()==9600,
        "loaded jam retains its captured seed and tick provenance");

  constexpr Id leaderId=35;
  const Entity* initialLeader=simulation.find(leaderId);
  check(initialLeader&&initialLeader->alive()&&initialLeader->team==0&&
        initialLeader->order==Order::Idle,
        "saved Escort leader remains the original owned idle mobile");
  const Vec2 leaderPosition=initialLeader->pos;
  const Vec2 leaderGoal=initialLeader->goal;
  const Id leaderTarget=initialLeader->target;
  const Id leaderSupport=initialLeader->supportTarget;

  struct AcceptedEscort {
    Id id=0;
    Vec2 goal{};
    Vec2 offset{};
    Id target=0;
    Id supportTarget=0;
  };
  std::vector<AcceptedEscort> accepted;
  for(const auto& entity:simulation.entities()) {
    if(entity.alive()&&entity.team==0&&entity.order==Order::Escort&&
       entity.sustained.escortTarget==leaderId) {
      accepted.push_back({entity.id,entity.goal,entity.sustained.escortOffset,
                          entity.target,entity.supportTarget});
    }
  }
  check(accepted.size()==159,
        "exact synthetic row loads all 159 followers assigned to leader 35");

  const Entity* firstLaggard=simulation.find(84);
  const Entity* secondLaggard=simulation.find(127);
  check(firstLaggard&&secondLaggard&&firstLaggard->order==Order::Escort&&
        secondLaggard->order==Order::Escort&&
        firstLaggard->sustained.escortTarget==leaderId&&
        secondLaggard->sustained.escortTarget==leaderId,
        "recorded laggards retain their original Escort assignments");
  const Vec2 firstGoal=firstLaggard->goal;
  const Vec2 secondGoal=secondLaggard->goal;
  check(distance(firstLaggard->pos,firstGoal)>Simulation::SustainedReturnTolerance&&
        distance(secondLaggard->pos,secondGoal)>Simulation::SustainedReturnTolerance,
        "loaded regression begins with both recorded laggards genuinely unsettled");

  int firstStable=0;
  int secondStable=0;
  for(int tick=0;tick<1200&&(firstStable<20||secondStable<20);++tick) {
    // Continue only through the public fixed update; the loaded positions,
    // destinations, targets, and sustained state are never rewritten by the test.
    step(simulation);
    const Entity* leader=simulation.find(leaderId);
    check(leader&&leader->alive()&&leader->team==0&&leader->order==Order::Idle&&
          samePoint(leader->pos,leaderPosition)&&samePoint(leader->goal,leaderGoal)&&
          leader->target==leaderTarget&&leader->supportTarget==leaderSupport,
          "dense recovery mutated or displaced its original owned leader");
    for(const auto& original:accepted) {
      const Entity* follower=simulation.find(original.id);
      check(follower&&follower->alive()&&follower->team==0&&
            follower->order==Order::Escort&&
            follower->sustained.escortTarget==leaderId&&
            samePoint(follower->goal,original.goal)&&
            samePoint(follower->sustained.escortOffset,original.offset)&&
            follower->target==original.target&&
            follower->supportTarget==original.supportTarget,
            "dense recovery changed an accepted Escort order, target, goal, or offset for id "+
              std::to_string(original.id));
    }
    firstLaggard=simulation.find(84);
    secondLaggard=simulation.find(127);
    firstStable=distance(firstLaggard->pos,firstGoal)<=Simulation::SustainedReturnTolerance?
      firstStable+1:0;
    secondStable=distance(secondLaggard->pos,secondGoal)<=Simulation::SustainedReturnTolerance?
      secondStable+1:0;
  }
  check(firstStable>=20&&secondStable>=20,
        "loaded dense-row laggards failed to settle at their original goals for 20 ticks: 84 "+
          entityState(simulation.find(84))+" 127 "+entityState(simulation.find(127)));
}

struct ReplayFixture {Simulation simulation;Id patrol=0,leader=0,escort=0,enemy=0;};

ReplayFixture replayFixture() {
  ReplayFixture value{fixture()};
  value.patrol=value.simulation.debugSpawn(Kind::Striker,0,{1000,2600});
  value.leader=value.simulation.debugSpawn(Kind::Scout,0,{2700,2600});
  value.escort=value.simulation.debugSpawn(Kind::Lancer,0,{2450,2600});
  value.enemy=value.simulation.debugSpawn(Kind::Scout,1,{1350,2600});
  send(value.simulation,CommandType::Hold,1,{value.enemy});
  return value;
}

void saveLoadAndReplayPreserveSustainedContinuation() {
  auto source=replayFixture();
  check(send(source.simulation,CommandType::Patrol,0,{source.patrol},{2100,2600}).accepted&&
        send(source.simulation,CommandType::Move,0,{source.leader},{3400,2800}).accepted&&
        send(source.simulation,CommandType::Escort,0,{source.escort},{},source.leader).accepted,
        "persistence fixture accepts Patrol and moving-leader Escort");
  stepUntil(source.simulation,100,[&]{return source.simulation.find(source.patrol)->sustained.phase==SustainedOrderPhase::Pursuit;},
            "persistence fixture reaches an active pursuit phase");
  const auto path=std::filesystem::temp_directory_path()/"cinderline-patrol-escort-v14.sav";
  check(source.simulation.save(path.string()),"save fourteen writes active sustained state");
  const auto lines=readLines(path);
  check(!lines.empty()&&lines.front()=="CINDERLINE 15"&&
        std::find(lines.begin(),lines.end(),"SUSTAINED_ORDERS 1")!=lines.end()&&
        std::find(lines.begin(),lines.end(),"FORMATION_ORDERS 1")!=lines.end()&&
        std::find(lines.begin(),lines.end(),"QUEUED_WORK 1")!=lines.end(),
        "current save declares version fifteen with map revision, sustained, formation, and queued-work sections");
  Simulation loaded;check(loaded.load(path.string())&&loaded.stateHash()==source.simulation.stateHash(),
                          "save-load preserves active pursuit and Escort exactly");
  for(int tick=0;tick<100;++tick) {
    step(source.simulation);step(loaded);
    check(loaded.stateHash()==source.simulation.stateHash(),
          "loaded sustained orders continue deterministically through combat and following");
  }

  auto original=replayFixture();
  check(send(original.simulation,CommandType::Patrol,0,{original.patrol},{2100,2600}).accepted&&
        send(original.simulation,CommandType::Move,0,{original.leader},{3400,2800}).accepted&&
        send(original.simulation,CommandType::Escort,0,{original.escort},{},original.leader).accepted,
        "recording fixture accepts its sustained command sequence");
  step(original.simulation,220);const auto recording=original.simulation.recording();
  auto replay=replayFixture();std::size_t next=replay.simulation.recording().size();
  while(replay.simulation.tick()<original.simulation.tick()) {
    while(next<recording.size()&&recording[next].tick==replay.simulation.tick())
      check(replay.simulation.command(recording[next++].command).accepted,
            "recorded sustained command replays at its authoritative tick");
    step(replay.simulation);
  }
  check(next==recording.size()&&replay.simulation.stateHash()==original.simulation.stateHash(),
        "Patrol and Escort recording reproduces the authoritative result");
  std::filesystem::remove(path);
}

void saveThirteenRejectsCorruptSustainedRowsAtomically() {
  auto source=fixture();
  Id hq=0;for(const auto& entity:source.entities())if(entity.team==0&&entity.kind==Kind::Headquarters)hq=entity.id;
  const Id patrol=source.debugSpawn(Kind::Mender,0,{1000,3350});
  const Id leader=source.debugSpawn(Kind::Scout,0,{2500,3350});
  const Id first=source.debugSpawn(Kind::Striker,0,{2250,3300});
  const Id second=source.debugSpawn(Kind::Lancer,0,{2050,3250});
  const Id enemy=source.debugSpawn(Kind::Worker,1,{1400,3350});
  check(send(source,CommandType::Hold,1,{enemy}).accepted&&
        send(source,CommandType::Patrol,0,{patrol},{1800,3350}).accepted&&
        send(source,CommandType::Escort,0,{first},{},leader).accepted&&
        send(source,CommandType::Escort,0,{second},{},first).accepted,
        "corruption baseline contains Patrol and an acyclic Escort chain");
  const auto path=std::filesystem::temp_directory_path()/"cinderline-corrupt-sustained-v14.sav";
  check(source.save(path.string()),"valid sustained corruption baseline saves");
  const auto valid=readLines(path);
  const auto marker=std::find(valid.begin(),valid.end(),"SUSTAINED_ORDERS 1");
  const auto formation=std::find(valid.begin(),valid.end(),"FORMATION_ORDERS 1");
  const auto queuedWork=std::find(valid.begin(),valid.end(),"QUEUED_WORK 1");
  check(marker!=valid.end()&&formation!=valid.end()&&queuedWork!=valid.end()&&marker<formation&&formation<queuedWork&&valid.front()=="CINDERLINE 15",
        "corruption fixture locates ordered save-fourteen sustained, formation, and queued-work rows");
  const std::size_t markerIndex=static_cast<std::size_t>(marker-valid.begin());
  const auto entityCount=static_cast<std::size_t>(std::stoul(valid.at(markerIndex+1)));
  check(formation==marker+2+static_cast<std::ptrdiff_t>(entityCount),
        "sustained rows end exactly where the formation section begins");
  auto reject=[&](std::vector<std::string> broken,const std::string& reason) {
    writeLines(path,broken);auto untouched=fixture();const auto hash=untouched.stateHash();
    const auto recorded=untouched.recording().size();
    check(!untouched.load(path.string()),reason+" is rejected");
    check(untouched.stateHash()==hash&&untouched.recording().size()==recorded,
          reason+" rejects atomically without changing the active match");
  };
  auto mutate=[&](std::vector<std::string> lines,Id id,std::size_t column,const std::string& value) {
    const auto row=sustainedRow(lines,markerIndex,id);auto data=fields(lines[row]);
    check(data.size()==13&&column<data.size(),"sustained mutation addresses an exact row field");
    data[column]=value;lines[row]=joined(data);return lines;
  };

  reject(mutate(valid,patrol,0,"999999"),"unknown sustained entity identity");
  auto duplicate=valid;duplicate[sustainedRow(duplicate,markerIndex,first)]=
    duplicate[sustainedRow(duplicate,markerIndex,patrol)];
  reject(duplicate,"duplicate sustained entity identity");
  auto missing=valid;missing.erase(missing.begin()+static_cast<std::ptrdiff_t>(sustainedRow(missing,markerIndex,second)));
  reject(missing,"missing sustained entity row");
  reject(mutate(valid,patrol,5,"2"),"invalid Patrol direction flag");
  reject(mutate(valid,patrol,6,std::to_string(leader)),"Patrol carrying Escort state");
  reject(mutate(valid,patrol,9,std::to_string(enemy)),"Travel phase carrying a pursuit target");
  reject(mutate(valid,patrol,12,"1"),"Pursuit phase without a hostile reference");
  auto badReturn=mutate(valid,patrol,12,"2");
  badReturn=mutate(std::move(badReturn),patrol,9,std::to_string(enemy));
  reject(badReturn,"Return phase retaining a hostile reference");
  reject(mutate(valid,first,12,"1"),"Escort Pursuit phase without a hostile reference");
  reject(mutate(valid,patrol,12,"3"),"unknown sustained phase");
  reject(mutate(valid,first,7,"4096"),"Escort offset beyond its Euclidean cap");
  reject(mutate(valid,hq,1,"1"),"inactive building carrying sustained state");
  reject(mutate(valid,first,6,std::to_string(second)),"cyclic Escort graph");
  auto truncated=valid;
  truncated.erase(truncated.begin()+static_cast<std::ptrdiff_t>(markerIndex+1+entityCount));
  reject(truncated,"truncated sustained section");
  std::filesystem::remove(path);
}

} // namespace

int main() {
  const std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"mixed Patrol immutable laps",mixedPatrolCompletesImmutableLaps},
    {"Patrol pursuit return suppression and kill",patrolPursuitReturnsWithoutRetargeting},
    {"blocked and stationary Patrol recovery",blockedAndStationaryPatrolsRemainStable},
    {"Escort leader immutability and stable slots",escortPreservesLeaderAndStableGroundAirSlots},
    {"Escort boundary reprojection and failed-route retry",escortEdgesStayDistinctAndFailedRoutesRetry},
    {"sequential Escort assignment and reassignment",sequentialEscortAssignmentAndReassignmentReserveSlots},
    {"far Escort approach before local leader bypass",farEscortApproachDelaysLeaderBypassUntilLocal},
    {"Escort traversal around an Idle formation",escortTraversesAroundIdleLeaderAndArrivedFollowers},
    {"Escort projected cycles and atomic rejection",escortCyclesAndInvalidTargetsRejectAtomically},
    {"Escort leader death successors",leaderDeathUsesTailBeforeDefendFallback},
    {"worker cargo and construction Escort",workerCargoAndConstructionSurviveLeaderExclusion},
    {"Anchor cargo delivery with Escort traffic",escortDoesNotBlockAnchorCargoDelivery},
    {"sustained clear stop and replacement",clearStopAndReplacementCleanSustainedState},
    {"loaded dense Escort row recovery",loadedDenseEscortRowRecoversWithoutIntentChanges},
    {"save-load continuation and replay",saveLoadAndReplayPreserveSustainedContinuation},
    {"save-thirteen sustained corruption rejection",saveThirteenRejectsCorruptSustainedRowsAtomically},
  };
  int failed=0;
  for(const auto& [name,test]:tests) {
    try {test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
  }
  std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
