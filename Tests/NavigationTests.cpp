#include "Sim/Simulation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
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

float distance(Vec2 a,Vec2 b) {return std::hypot(a.x-b.x,a.y-b.y);}

std::vector<Entity>& mutableEntities(Simulation& simulation) {
  return const_cast<std::vector<Entity>&>(simulation.entities());
}

std::vector<Obstacle>& mutableObstacles(Simulation& simulation) {
  return const_cast<std::vector<Obstacle>&>(simulation.obstacles());
}

Simulation emptyFixture(Vec2 home={700,700}) {
  Simulation simulation;
  // Tests install synthetic obstacles and compare legacy save migrations.
  simulation.reset({0,0xC1D3u,false,1,MatchLength::Standard,2,0});
  mutableEntities(simulation).clear();
  mutableObstacles(simulation).clear();
  simulation.debugSpawn(Kind::Headquarters,0,home);
  simulation.debugSpawn(Kind::Headquarters,1,{4400,4400});
  simulation.debugResources(0,100000);
  return simulation;
}

CommandResult send(Simulation& simulation,CommandType type,int team,std::vector<Id> units,
                   Vec2 point={},Id target=0,Kind kind=Kind::Worker) {
  return simulation.command({type,team,std::move(units),point,target,kind,0});
}

void setObstacles(Simulation& simulation,std::initializer_list<Obstacle> obstacles) {
  mutableObstacles(simulation).assign(obstacles);
}

bool circleIntersectsBox(Vec2 point,float radius,const Obstacle& obstacle) {
  const float dx=std::max(std::fabs(point.x-obstacle.center.x)-obstacle.half.x,0.0f);
  const float dy=std::max(std::fabs(point.y-obstacle.center.y)-obstacle.half.y,0.0f);
  return dx*dx+dy*dy<radius*radius-0.001f;
}

bool sweptSegmentClear(const Simulation& simulation,Vec2 from,Vec2 to,float radius) {
  const int samples=std::max(1,static_cast<int>(std::ceil(distance(from,to)/4)));
  for(int sample=0;sample<=samples;++sample) {
    const float alpha=static_cast<float>(sample)/samples;
    const Vec2 point{from.x+(to.x-from.x)*alpha,from.y+(to.y-from.y)*alpha};
    for(const auto& obstacle:simulation.obstacles())if(circleIntersectsBox(point,radius,obstacle))return false;
  }
  return true;
}

void checkStaticClearance(const Simulation& simulation,Id unit,const std::string& context) {
  const Entity* moving=simulation.find(unit);
  check(moving&&moving->alive(),context+": moving unit disappeared");
  const float radius=definition(moving->kind).radius;
  for(const auto& obstacle:simulation.obstacles())
    check(!circleIntersectsBox(moving->pos,radius,obstacle),context+": unit radius penetrated terrain");
  for(const auto& entity:simulation.entities()) {
    if(entity.id==unit||!entity.alive())continue;
    if(!definition(entity.kind).building&&entity.kind!=Kind::Resource)continue;
    if(entity.kind==Kind::Resource&&entity.resource<=0)continue;
    check(distance(moving->pos,entity.pos)+0.001f>=radius+definition(entity.kind).radius,
          context+": unit radius penetrated a building or ore deposit");
  }
}

template<class Predicate>
void runUntil(Simulation& simulation,float maximumSeconds,Predicate complete,
              const std::vector<Id>& checkedUnits,const std::string& failure) {
  const int steps=static_cast<int>(std::ceil(maximumSeconds/Simulation::Step));
  for(int step=0;step<steps&&!complete();++step) {
    simulation.update(Simulation::Step);
    for(Id unit:checkedUnits)checkStaticClearance(simulation,unit,failure);
  }
  if(!complete())for(Id unit:checkedUnits) {
    const auto* entity=simulation.find(unit);
    if(entity)std::cerr<<"NAV_FAILURE unit="<<unit<<" pos="<<entity->pos.x<<','<<entity->pos.y
      <<" goal="<<entity->goal.x<<','<<entity->goal.y<<" order="<<static_cast<int>(entity->order)
      <<" carried="<<entity->carried<<" returning="<<entity->returning
      <<" path="<<entity->pathIndex<<'/'<<entity->path.size()
      <<" work_valid="<<entity->workPointValid<<" work="<<entity->workPoint.x<<','<<entity->workPoint.y
      <<" failures="<<entity->navigationFailures<<" exhausted="<<entity->navigationExhausted<<'\n';
  }
  check(complete(),failure);
}

std::string temporarySave(const std::string& name) {
  return (std::filesystem::temp_directory_path()/("cinderline-navigation-"+name+".sav")).string();
}

std::vector<std::string> readLines(const std::string& path) {
  std::ifstream input(path);std::vector<std::string> lines;std::string line;
  while(std::getline(input,line))lines.push_back(line);
  return lines;
}

void writeLines(const std::string& path,const std::vector<std::string>& lines) {
  std::ofstream output(path);
  for(const auto& line:lines)output<<line<<'\n';
  check(output.good(),"navigation save fixture writes successfully");
}

std::vector<std::string> fields(const std::string& line) {
  std::istringstream input(line);std::vector<std::string> result;std::string field;
  while(input>>field)result.push_back(field);
  return result;
}

std::string joinFields(const std::vector<std::string>& values) {
  std::ostringstream output;
  for(std::size_t index=0;index<values.size();++index)output<<(index?" ":"")<<values[index];
  return output.str();
}

void stripQueueModesForSaveTen(std::vector<std::string>& lines) {
  const auto config=fields(lines.at(1));const auto players=static_cast<std::size_t>(std::stoul(config.at(5)));std::size_t cursor=3+players;
  cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
  const auto entityCount=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
  for(std::size_t entity=0;entity<entityCount;++entity) {
    ++cursor;cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
    cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
  }
  const auto effectHeader=fields(lines.at(cursor));cursor+=1+static_cast<std::size_t>(std::stoul(effectHeader.front()))+players*2;
  const auto recordingCount=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
  for(std::size_t recording=0;recording<recordingCount;++recording) {
    auto row=fields(lines.at(cursor));
    check(row.size()>=10&&row.size()==10+static_cast<std::size_t>(std::stoul(row[9])),
          "version-eleven recording layout contains queue mode");
    row.erase(row.begin()+8);lines[cursor++]=joinFields(row);
  }
}

void stripFormationForSaveTwelve(std::vector<std::string>& lines) {
  if(lines.front()=="CINDERLINE 15") {
    auto config=fields(lines.at(1));check(config.size()==7&&config.back()=="0","legacy migration uses explicit flat-map revision zero");
    config.pop_back();lines[1]=joinFields(config);
  }
  const auto config=fields(lines.at(1));const auto players=static_cast<std::size_t>(std::stoul(config.at(5)));std::size_t cursor=3+players;
  cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
  const auto entityCount=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
  for(std::size_t entity=0;entity<entityCount;++entity) {
    ++cursor;cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
    cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
  }
  const auto effectHeader=fields(lines.at(cursor));cursor+=1+static_cast<std::size_t>(std::stoul(effectHeader.front()))+players*2;
  const auto recordingCount=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
  for(std::size_t recording=0;recording<recordingCount;++recording) {
    auto row=fields(lines.at(cursor));
    check(row.size()>=13&&row.size()==13+static_cast<std::size_t>(std::stoul(row[12])),
          "version-thirteen recording layout contains formation fields");
    row.erase(row.begin()+9,row.begin()+12);lines[cursor++]=joinFields(row);
  }
  const auto sustained=std::find(lines.begin(),lines.end(),"SUSTAINED_ORDERS 1");
  const auto formation=std::find(lines.begin(),lines.end(),"FORMATION_ORDERS 1");
  const auto queuedWork=std::find(lines.begin(),lines.end(),"QUEUED_WORK 1");
  check(sustained!=lines.end()&&formation!=lines.end()&&queuedWork!=lines.end()&&sustained<formation&&formation<queuedWork,
        "save-fourteen navigation fixture contains ordered sustained, formation, and queued-work sections");
  lines.erase(queuedWork,lines.end());
  lines.erase(formation,lines.end());
}

void narrowTurningGap() {
  auto simulation=emptyFixture({900,900});
  const float openingLeft=1310,openingRight=1354;
  setObstacles(simulation,{
    {{openingLeft/2,1400},{openingLeft/2,90}},
    {{(openingRight+Simulation::WorldSize)/2,1400},{(Simulation::WorldSize-openingRight)/2,90}},
    {{1190,1585},{75,95}}
  });
  const Id worker=simulation.debugSpawn(Kind::Worker,0,{1332,1180});
  const Vec2 goal{1550,1660};
  check(send(simulation,CommandType::Move,0,{worker},goal).accepted,"narrow-turn move is accepted");
  runUntil(simulation,45,[&]{return distance(simulation.find(worker)->pos,goal)<35;},{worker},
           "Drudge did not clear the radius-valid turning gap");
}

void blockedWorkSiteSides() {
  {
    auto simulation=emptyFixture({900,1200});
    setObstacles(simulation,{{{1480,1200},{20,120}}});
    const Id worker=simulation.debugSpawn(Kind::Worker,0,{1200,1200});
    const Id ore=simulation.debugSpawn(Kind::Resource,-1,{1550,1200});
    check(send(simulation,CommandType::Gather,0,{worker},{},ore).accepted,"blocked-side gather is accepted");
    bool harvestedFromClearSide=false;float previousCargo=0;
    runUntil(simulation,75,[&]{
      const float cargo=simulation.find(worker)->carried;
      if(cargo>previousCargo+0.001f) {
        const bool clear=sweptSegmentClear(simulation,simulation.find(worker)->pos,simulation.find(ore)->pos,
                                           definition(Kind::Worker).radius);
        if(!clear)std::cerr<<"WORK_SEGMENT_FAILURE worker="<<simulation.find(worker)->pos.x<<','
          <<simulation.find(worker)->pos.y<<" ore="<<simulation.find(ore)->pos.x<<','<<simulation.find(ore)->pos.y
          <<" slot="<<simulation.find(worker)->workPoint.x<<','<<simulation.find(worker)->workPoint.y<<'\n';
        check(clear,
              "worker cannot harvest through the thin wall from a disconnected work point");
        harvestedFromClearSide=true;
      }
      previousCargo=cargo;
      return simulation.players()[0].stats.gathered>=18;
    },{worker},
             "worker did not harvest and deliver from the reachable side of ore");
    check(harvestedFromClearSide,"blocked-side worker performs real harvesting from a clear target segment");
  }
  {
    auto simulation=emptyFixture({900,2200});
    setObstacles(simulation,{{{1470,2200},{10,80}}});
    const Id worker=simulation.debugSpawn(Kind::Worker,0,{1200,2200});
    const Vec2 site{1600,2200};
    check(simulation.canPlace(0,Kind::Foundry,site),"blocked-side foundation footprint is legal");
    check(send(simulation,CommandType::Build,0,{worker},site,0,Kind::Foundry).accepted,
          "blocked-side foundation is accepted");
    Id foundation=0;
    for(const auto& entity:simulation.entities())if(entity.kind==Kind::Foundry)foundation=entity.id;
    runUntil(simulation,75,[&]{return simulation.find(foundation)->progress>0.08f;},{worker},
             "builder did not reach an accessible side of the foundation");
  }
}

void reachableAlternativeWins() {
  {
    auto simulation=emptyFixture({900,2100});
    const Id nearWorker=simulation.debugSpawn(Kind::Worker,0,{1200,2400});
    const Id farWorker=simulation.debugSpawn(Kind::Worker,0,{1600,1900});
    for(int spoke=0;spoke<8;++spoke) {
      const float angle=spoke*0.78539816339f;
      simulation.debugSpawn(Kind::Resource,-1,{1200+110*std::cos(angle),2400+110*std::sin(angle)});
    }
    const Vec2 site{1600,2400};
    check(simulation.canPlace(0,Kind::Foundry,site),"alternative-worker foundation footprint is legal");
    check(send(simulation,CommandType::Build,0,{nearWorker,farWorker},site,0,Kind::Foundry).accepted,
          "construction with two candidate workers is accepted");
    Id foundation=0;
    for(const auto& entity:simulation.entities())if(entity.kind==Kind::Foundry)foundation=entity.id;
    runUntil(simulation,80,[&]{return simulation.find(foundation)->progress>0.08f;},{nearWorker,farWorker},
             "reachable worker did not take over from the trapped nearer worker");
    check(simulation.constructionWorker(foundation)==farWorker,
          "route-aware construction keeps the reachable worker assigned");
  }
  {
    auto simulation=emptyFixture({1600,1200});
    const Id processor=simulation.debugSpawn(Kind::Processor,0,{2300,1200});
    (void)processor;
    setObstacles(simulation,{
      {{1425,1200},{20,175}},{{1775,1200},{20,175}},
      {{1600,1025},{195,20}},{{1600,1375},{195,20}}
    });
    const Id worker=simulation.debugSpawn(Kind::Worker,0,{1950,1200});
    const Id ore=simulation.debugSpawn(Kind::Resource,-1,{2050,1500});
    Entity* carrying=nullptr;
    for(auto& entity:mutableEntities(simulation))if(entity.id==worker)carrying=&entity;
    carrying->order=Order::Gather;carrying->target=ore;carrying->resourceTarget=ore;
    carrying->carried=18;carrying->returning=true;
    runUntil(simulation,60,[&]{return simulation.players()[0].stats.gathered>=18;},{worker},
             "worker did not bypass the trapped nearer depot and deliver to the farther depot");
  }
}

void inaccessibleBuildStatusMatchesCommand() {
  auto simulation=emptyFixture({900,2100});
  const Id trapped=simulation.debugSpawn(Kind::Worker,0,{1200,2400});
  for(int spoke=0;spoke<8;++spoke) {
    const float angle=spoke*0.78539816339f;
    simulation.debugSpawn(Kind::Resource,-1,{1200+110*std::cos(angle),2400+110*std::sin(angle)});
  }
  const Vec2 site{1600,2400};
  check(simulation.canPlace(0,Kind::Foundry,site),"inaccessible build fixture keeps a legal foundation footprint");
  const int ore=simulation.players()[0].ore;const auto entityCount=simulation.entities().size();
  const auto status=simulation.buildStatus(0,Kind::Foundry,{trapped},&site);
  const auto command=send(simulation,CommandType::Build,0,{trapped},site,0,Kind::Foundry);
  check(!status.accepted&&!command.accepted,"read-only status and actual command both reject an inaccessible builder");
  check(status.message==command.message,"read-only status reports the same access failure as the actual command");
  check(simulation.players()[0].ore==ore&&simulation.entities().size()==entityCount,
        "inaccessible build rejection spends no ore and places no foundation");
}

void busyMiningCorridor() {
  auto simulation=emptyFixture({1050,1800});
  setObstacles(simulation,{
    {{1225,1200},{1225,100}},{{1225,2400},{1225,100}},
    {{3675,1200},{1225,100}},{{3675,2400},{1225,100}},
    {{2400,889},{100,889}},{{2400,3311},{100,1489}}
  });
  const Id ore=simulation.debugSpawn(Kind::Resource,-1,{3150,1800});
  const Id spotter=simulation.debugSpawn(Kind::Scout,0,{3050,1800});
  std::vector<Id> workers;
  for(int row=0;row<2;++row)for(int column=0;column<3;++column)
    workers.push_back(simulation.debugSpawn(Kind::Worker,0,{1450.0f+column*42,1765.0f+row*70}));
  check(send(simulation,CommandType::Gather,0,workers,{},ore).accepted,"corridor mining order is accepted");
  for(auto& entity:mutableEntities(simulation))if(entity.id==spotter)entity.hp=0;
  std::vector<bool> carried(workers.size()),delivered(workers.size());
  const int initialOre=simulation.players()[0].ore;
  runUntil(simulation,180,[&]{
    for(std::size_t index=0;index<workers.size();++index) {
      const float cargo=simulation.find(workers[index])->carried;
      carried[index]=carried[index]||cargo>0;
      delivered[index]=delivered[index]||(carried[index]&&cargo==0&&simulation.find(workers[index])->returning==false);
    }
    return std::all_of(delivered.begin(),delivered.end(),[](bool value){return value;});
  },workers,"one or more workers made no full mining delivery through the single-file corridor");
  check(simulation.players()[0].ore>=initialOre+18*static_cast<int>(workers.size()),
        "every corridor worker contributes a full delivered load");
}

void foundationRouteInvalidation() {
  auto simulation=emptyFixture({900,3100});
  const Id mover=simulation.debugSpawn(Kind::Worker,0,{1250,3100});
  const Id builder=simulation.debugSpawn(Kind::Worker,0,{1700,2850});
  const Vec2 goal{2200,3100};
  check(send(simulation,CommandType::Move,0,{mover},goal).accepted,"route-invalidation move is accepted");
  for(int step=0;step<20;++step)simulation.update(Simulation::Step);
  const Vec2 site{1700,3100};
  check(simulation.canPlace(0,Kind::Foundry,site),"new blocking foundation footprint is legal");
  check(send(simulation,CommandType::Build,0,{builder},site,0,Kind::Foundry).accepted,
        "new blocking foundation is placed");
  check(send(simulation,CommandType::Stop,0,{builder}).accepted,"fixture leaves the blocking foundation in place");
  runUntil(simulation,45,[&]{return distance(simulation.find(mover)->pos,goal)<35;},{mover},
           "worker did not recover after a foundation invalidated its route");
}

void holdAndEnemyCollision() {
  auto simulation=emptyFixture({900,3500});
  const Id mover=simulation.debugSpawn(Kind::Worker,0,{1350,3500});
  const Id friendlyHold=simulation.debugSpawn(Kind::Worker,0,{1700,3470});
  const Id enemyHold=simulation.debugSpawn(Kind::Worker,1,{1880,3530});
  const Vec2 friendlyStart=simulation.find(friendlyHold)->pos;
  const Vec2 enemyStart=simulation.find(enemyHold)->pos;
  check(send(simulation,CommandType::Hold,0,{friendlyHold}).accepted,"friendly Hold order is accepted");
  check(send(simulation,CommandType::Hold,1,{enemyHold}).accepted,"enemy Hold order is accepted");
  const Vec2 goal{2250,3500};
  check(send(simulation,CommandType::Move,0,{mover},goal).accepted,"collision fixture move is accepted");
  const float required=2*definition(Kind::Worker).radius;
  runUntil(simulation,35,[&]{
    check(distance(simulation.find(friendlyHold)->pos,friendlyStart)<0.01f,"local avoidance displaced a friendly Hold unit");
    check(distance(simulation.find(enemyHold)->pos,enemyStart)<0.01f,"local avoidance displaced an enemy Hold unit");
    check(distance(simulation.find(mover)->pos,simulation.find(friendlyHold)->pos)+0.01f>=required,
          "moving worker overlapped a friendly Hold unit");
    check(distance(simulation.find(mover)->pos,simulation.find(enemyHold)->pos)+0.01f>=required,
          "moving worker overlapped an enemy unit");
    return distance(simulation.find(mover)->pos,goal)<35;
  },{mover},"worker did not pass held friendly and enemy units without collision");
}

void workSlotExhaustionSemantics() {
  {
    auto simulation=emptyFixture({900,900});
    const Id worker=simulation.debugSpawn(Kind::Worker,0,{1200,900});
    const Id ore=simulation.debugSpawn(Kind::Resource,-1,{1550,900});
    std::vector<Id> reservations;
    for(int spoke=0;spoke<32;++spoke) {
      const Id reservation=simulation.debugSpawn(Kind::Worker,0,{700.0f+(spoke%8)*42,1300.0f+(spoke/8)*42});
      reservations.push_back(reservation);
      const float angle=spoke*0.19634954085f;
      for(auto& entity:mutableEntities(simulation))if(entity.id==reservation) {
        entity.progress=0.5f;entity.order=Order::Gather;entity.workTarget=ore;
        entity.workPoint={1550+70*std::cos(angle),900+70*std::sin(angle)};
        entity.workPointValid=true;
      }
    }
    check(send(simulation,CommandType::Gather,0,{worker},{},ore).accepted,"reserved-perimeter gather is accepted");
    for(auto& entity:mutableEntities(simulation))if(entity.id==worker) {
      entity.navigationExhausted=true;entity.repath=0;
    }
    simulation.update(Simulation::Step);
    check(!simulation.find(worker)->navigationExhausted&&simulation.find(worker)->repath>0,
          "temporary work-slot reservations clear stale exhaustion and schedule a short retry");
    for(auto& entity:mutableEntities(simulation))
      if(std::find(reservations.begin(),reservations.end(),entity.id)!=reservations.end())entity.hp=0;
    runUntil(simulation,60,[&]{return simulation.find(worker)->carried>0;},{worker},
             "worker does not recover after temporary work-slot reservations clear");
  }
  {
    auto simulation=emptyFixture({900,1500});
    const Id worker=simulation.debugSpawn(Kind::Worker,0,{1200,1500});
    const Id ore=simulation.debugSpawn(Kind::Resource,-1,{1550,1500});
    setObstacles(simulation,{{{1550,1500},{100,100}}});
    check(send(simulation,CommandType::Gather,0,{worker},{},ore).accepted,"no-surface gather order is accepted for bounded diagnosis");
    simulation.update(Simulation::Step);
    check(simulation.find(worker)->navigationExhausted&&simulation.find(worker)->repath>1.5f,
          "work target with no legal perimeter reports exhaustion and uses the long retry interval");
  }
}

void impossibleOrderIsBoundedAndPersistent() {
  auto simulation=emptyFixture({900,3900});
  const Vec2 start{1800,3900},goal{2800,3900};
  setObstacles(simulation,{
    {{1600,3900},{25,250}},{{2000,3900},{25,250}},
    {{1800,3700},{225,25}},{{1800,4100},{225,25}}
  });
  const Id worker=simulation.debugSpawn(Kind::Worker,0,start);
  check(send(simulation,CommandType::Move,0,{worker},goal).accepted,"impossible move is accepted as an order");
  double maximumStep=0;
  for(int step=0;step<2400;++step) {
    simulation.update(Simulation::Step);maximumStep=std::max(maximumStep,simulation.lastStepMilliseconds());
    checkStaticClearance(simulation,worker,"impossible route");
  }
  check(distance(simulation.find(worker)->pos,start)<40,"impossible order neither teleports nor escapes sealed geometry");
  const auto attempts=simulation.navigationStats();
  check(attempts.searches>0,"impossible order performs an initial route search");
  check(attempts.searches<=128,"impossible order bounds route retries over 120 simulated seconds");
  check(attempts.failures<=attempts.searches,"failed routes cannot exceed route searches");
  const auto savePath=temporarySave("impossible");
  check(simulation.save(savePath),"impossible navigation state saves");
  Simulation loaded;
  check(loaded.load(savePath),"impossible navigation state loads");
  check(loaded.stateHash()==simulation.stateHash(),"impossible navigation state has the same save-load hash");
  for(int step=0;step<400;++step) {simulation.update(Simulation::Step);loaded.update(Simulation::Step);}
  check(loaded.stateHash()==simulation.stateHash(),"impossible order remains deterministic after save-load continuation");
  std::filesystem::remove(savePath);
  std::cout<<"IMPOSSIBLE_ROUTE simulation_seconds="<<simulation.time()<<" searches="<<attempts.searches
           <<" expanded="<<attempts.expanded<<" failures="<<attempts.failures
           <<" budget_deferrals="<<attempts.budgetDeferrals<<" max_step_ms="<<maximumStep<<'\n';
}

void navigationSaveValidationAndMigration() {
  auto simulation=emptyFixture({900,4300});
  setObstacles(simulation,{{{1900,4300},{90,230}}});
  const Id worker=simulation.debugSpawn(Kind::Worker,0,{1500,4300});
  const Vec2 goal{2350,4300};
  check(send(simulation,CommandType::Move,0,{worker},goal).accepted,"navigation persistence move is accepted");
  for(int step=0;step<30;++step)simulation.update(Simulation::Step);
  check(distance(simulation.find(worker)->pos,goal)>100,"navigation persistence fixture saves during route travel");
  const auto path=temporarySave("validation-v13");
  check(simulation.save(path),"version-thirteen navigation state saves");
  const auto original=readLines(path);
  const auto marker=std::find(original.begin(),original.end(),"NAVIGATION 1");
  const auto production=std::find(original.begin(),original.end(),"PRODUCTION_JOBS 1");
  const auto orders=std::find(original.begin(),original.end(),"ORDER_QUEUES 1");
  const auto sustained=std::find(original.begin(),original.end(),"SUSTAINED_ORDERS 1");
  const auto formation=std::find(original.begin(),original.end(),"FORMATION_ORDERS 1");
  const auto queuedWork=std::find(original.begin(),original.end(),"QUEUED_WORK 1");
  check(marker!=original.end()&&production!=original.end()&&marker<production&&
        orders!=original.end()&&sustained!=original.end()&&formation!=original.end()&&queuedWork!=original.end()&&
        orders<sustained&&sustained<formation&&formation<queuedWork&&original.front()=="CINDERLINE 15",
        "fresh save contains player count, match length, navigation, production, tactical-order, sustained-order, formation-order, and queued-work sections");
  const std::size_t navigationLine=static_cast<std::size_t>(marker-original.begin());
  const std::size_t productionLine=static_cast<std::size_t>(production-original.begin());
  check(navigationLine+2<original.size(),"navigation section contains entity records");
  auto versionFive=original;
  stripFormationForSaveTwelve(versionFive);
  stripQueueModesForSaveTen(versionFive);
  versionFive.erase(versionFive.begin()+static_cast<std::ptrdiff_t>(productionLine),versionFive.end());
  versionFive.front()="CINDERLINE 5";
  {auto config=fields(versionFive.at(1));check(config.size()==6,"current navigation fixture includes player count and match length");config.pop_back();config.pop_back();versionFive[1]=joinFields(config);}
  {auto timeline=fields(versionFive.at(2));check(timeline.size()==6,"current navigation fixture includes elimination state");timeline.pop_back();versionFive[2]=joinFields(timeline);}
  writeLines(path,versionFive);
  Simulation previousVersion;
  check(previousVersion.load(path)&&previousVersion.stateHash()==simulation.stateHash(),
        "version-five navigation save remains readable with its original enum bounds");
  auto versionSix=versionFive;versionSix.front()="CINDERLINE 6";writeLines(path,versionSix);
  Simulation previousDefendVersion;
  check(previousDefendVersion.load(path)&&previousDefendVersion.stateHash()==simulation.stateHash(),
        "version-six navigation save migrates default production job identities");
  auto reject=[&](std::size_t column,const std::string& value,const std::string& message) {
    auto malformed=original;auto values=fields(malformed[navigationLine+2]);
    check(values.size()==14,"navigation record has fourteen fields");
    values[column]=value;malformed[navigationLine+2]=joinFields(values);writeLines(path,malformed);
    auto current=emptyFixture({1100,900});const auto before=current.stateHash();
    check(!current.load(path),message);
    check(current.stateHash()==before,"malformed navigation load preserves the current match atomically");
  };
  reject(7,"-1","negative navigation timers are rejected");
  reject(2,"nan","nonfinite navigation work points are rejected");
  const auto obstacleCount=static_cast<std::size_t>(std::stoul(original.at(5)));
  const std::size_t firstEntityRow=7+obstacleCount;
  auto rejectRepath=[&](const std::string& value,const std::string& message) {
    auto malformed=original;auto values=fields(malformed.at(firstEntityRow));
    check(values.size()==24,"entity record has twenty-four fields");
    values[21]=value;malformed[firstEntityRow]=joinFields(values);writeLines(path,malformed);
    auto current=emptyFixture({1100,900});const auto before=current.stateHash();
    check(!current.load(path),message);
    check(current.stateHash()==before,"invalid repath load preserves the current match atomically");
  };
  rejectRepath("-0.01","negative navigation retry timers are rejected");
  rejectRepath("10.01","navigation retry timers above the compatibility ceiling are rejected");

  auto future=original;future.front()="CINDERLINE 16";writeLines(path,future);
  auto current=emptyFixture({1100,900});const auto currentHash=current.stateHash();
  check(!current.load(path)&&current.stateHash()==currentHash,"unsupported version sixteen is rejected atomically");

  auto legacy=original;
  stripFormationForSaveTwelve(legacy);
  stripQueueModesForSaveTen(legacy);
  legacy.front()="CINDERLINE 4";
  {auto config=fields(legacy.at(1));config.pop_back();config.pop_back();legacy[1]=joinFields(config);}
  {auto timeline=fields(legacy.at(2));timeline.pop_back();legacy[2]=joinFields(timeline);}
  legacy.erase(legacy.begin()+static_cast<std::ptrdiff_t>(navigationLine),legacy.end());
  writeLines(path,legacy);
  Simulation migrated;
  check(migrated.load(path),"version-four save without navigation state migrates");
  check(migrated.find(worker)&&distance(migrated.find(worker)->pos,goal)>100,
        "version-four migration preserves the travelling unit and destination");
  runUntil(migrated,45,[&]{return distance(migrated.find(worker)->pos,goal)<35;},{worker},
           "version-four migrated unit did not rebuild navigation and finish its route");
  std::filesystem::remove(path);
}

void movingTargetRecoversFromFailedApproach() {
  auto simulation=emptyFixture();
  setObstacles(simulation,{{{2050,1500},{250,450}}});
  const Id soldier=simulation.debugSpawn(Kind::Striker,0,{1400,1500});
  simulation.debugSpawn(Kind::Scout,0,{1600,1500}); // Observes air but cannot damage it.
  const Id flyer=simulation.debugSpawn(Kind::Kite,1,{2050,1500});
  check(send(simulation,CommandType::Hold,1,{flyer}).accepted,"air target holds over terrain");
  simulation.update(Simulation::Step);
  check(send(simulation,CommandType::Attack,0,{soldier},{},flyer).accepted,
        "visible aircraft accepts an attack order");
  for(int step=0;step<20;++step)simulation.update(Simulation::Step);
  check(simulation.find(soldier)->navigationExhausted,
        "initial firing approach is obstructed by terrain");
  const Vec2 before=simulation.find(soldier)->pos;
  const auto searches=simulation.navigationStats().searches;
  for(int step=0;step<100;++step)simulation.update(Simulation::Step);
  check(simulation.navigationStats().searches-searches<=10,
        "stationary inaccessible targets retain bounded pursuit retry work");
  check(distance(simulation.find(soldier)->pos,before)<1,
        "failed pursuit cannot cross blocked terrain");
  check(send(simulation,CommandType::Move,1,{flyer},{1650,1100}).accepted,
        "air target can move into a reachable approach without changing static geometry");
  runUntil(simulation,15,[&]{return simulation.players()[0].stats.damage>0;},{soldier},
           "ground attacker did not resume pursuit after the aircraft moved into reach");
  check(distance(simulation.find(soldier)->pos,before)>30,
        "attacker reached a firing position using the original attack order");
}

void displacedIdleDrainsYieldAndReclaimsGoal() {
  auto simulation=emptyFixture();
  const Vec2 goal{2900,1500};
  const Id reclaimer=simulation.debugSpawn(Kind::Lancer,0,goal);
  check(send(simulation,CommandType::Move,0,{reclaimer},goal).accepted,
        "formation unit accepts its original movement goal");
  simulation.update(Simulation::Step);
  check(simulation.find(reclaimer)->order==Order::Idle,
        "formation unit settles before traffic displaces it");

  auto& displaced=*std::find_if(mutableEntities(simulation).begin(),mutableEntities(simulation).end(),
    [&](const Entity& entity){return entity.id==reclaimer;});
  displaced.pos={3000,1500};
  displaced.yieldFor=0.2f;

  const float contact=(definition(Kind::Lancer).radius*2.0f)*1.04f;
  const Id held=simulation.debugSpawn(Kind::Lancer,0,
    {displaced.pos.x+std::nextafter(contact,0.0f),displaced.pos.y});
  const Vec2 heldPosition=simulation.find(held)->pos;
  check(send(simulation,CommandType::Hold,0,{held}).accepted,
        "near-contact traffic fixture anchors its held unit");

  runUntil(simulation,2.0f,[&]{return distance(simulation.find(reclaimer)->pos,goal)<=30;},
           {reclaimer},"displaced Idle unit kept yielding instead of reclaiming its goal");
  check(distance(simulation.find(reclaimer)->goal,goal)<0.01f,
        "Idle recovery preserves the player's original formation goal");
  check(distance(simulation.find(held)->pos,heldPosition)<0.01f,
        "Idle recovery does not displace a held friendly unit");
}

void repeatedMixedArmyRetreat() {
  auto simulation=emptyFixture({700,700});
  setObstacles(simulation,{{{2400,2300},{120,500}}});
  const std::array<Kind,8> kinds{{Kind::Worker,Kind::Striker,Kind::Lancer,Kind::Scout,
    Kind::Bastion,Kind::Mortar,Kind::Mender,Kind::Kite}};
  std::vector<Id> units;
  std::vector<Id> groundUnits;
  for(int row=0;row<3;++row)for(int column=0;column<8;++column) {
    const Kind kind=kinds[static_cast<std::size_t>(column)];
    const Id unit=simulation.debugSpawn(kind,0,{1150.0f+column*100,1850.0f+row*100});
    units.push_back(unit);
    if(!definition(kind).air)groundUnits.push_back(unit);
  }
  // Retarget while the mixed-speed army is still negotiating the obstruction.
  // Arrival checks use the last acknowledged destination, not an Idle flag.
  for(int reversal=0;reversal<12;++reversal) {
    const bool retreat=(reversal%2)!=0;
    const Vec2 goal=retreat?Vec2{900,2800}:Vec2{3300,1800};
    const auto tickBefore=simulation.tick();
    check(send(simulation,CommandType::Move,0,units,goal).accepted,
          "every repeated mixed-army movement order is accepted");
    check(simulation.tick()==tickBefore,"accepting a new order does not advance game time");
    for(Id unit:units) {
      const auto* entity=simulation.find(unit);
      check(entity&&entity->order==Order::Move&&entity->target==0,
            "latest direct move applies to every selected unit, including support");
      check(retreat?entity->goal.x<1400:entity->goal.x>2800,
            "every mixed-army unit receives the latest side of the retreat");
    }
    for(int step=0;step<20;++step) {
      simulation.update(Simulation::Step);
      for(Id unit:groundUnits)checkStaticClearance(simulation,unit,"repeated mixed-army retreat");
    }
  }
  std::vector<Vec2> finalGoals;
  for(Id unit:units)finalGoals.push_back(simulation.find(unit)->goal);
  const auto savePath=temporarySave("mixed-retreat");
  check(simulation.save(savePath),"pending retreat saves");
  Simulation loaded;
  check(loaded.load(savePath)&&loaded.stateHash()==simulation.stateHash(),
        "pending retreat loads without changing orders");
  std::filesystem::remove(savePath);
  const auto retreatStartTick=simulation.tick();
  for(int step=0;step<1600;++step) {
    simulation.update(Simulation::Step);loaded.update(Simulation::Step);
    check(simulation.stateHash()==loaded.stateHash(),"retreat continuation is deterministic after reload");
    for(Id unit:groundUnits)checkStaticClearance(simulation,unit,"final retreat");
  }
  check(simulation.winner()==-1&&simulation.tick()==retreatStartTick+1600,
        "retreat fixture stays live and executes every requested simulation step");
  for(std::size_t index=0;index<units.size();++index) {
    const auto* entity=simulation.find(units[index]);
    if(entity&&distance(entity->pos,finalGoals[index])>=35)
      std::cerr<<"RETREAT_LAGGARD id="<<entity->id<<" kind="<<static_cast<int>(entity->kind)
        <<" pos="<<entity->pos.x<<','<<entity->pos.y<<" goal="<<finalGoals[index].x<<','<<finalGoals[index].y
        <<" remaining="<<distance(entity->pos,finalGoals[index])<<" order="<<static_cast<int>(entity->order)
        <<" failures="<<entity->navigationFailures<<" exhausted="<<entity->navigationExhausted<<'\n';
    check(entity&&entity->alive()&&distance(entity->pos,finalGoals[index])<35,
          "every surviving mixed-army unit reaches its final retreat slot");
    check(distance(entity->goal,finalGoals[index])<0.01f,
          "movement recovery preserves the last acknowledged retreat destination");
    check(!entity->navigationExhausted,"reachable final retreat does not retain route failure");
  }
}

void benchmark() {
  auto simulation=emptyFixture({700,700});
  setObstacles(simulation,{{{2400,1700},{120,700}},{{2400,3100},{120,700}}});
  std::vector<Id> units;
  for(int side=0;side<2;++side)for(int row=0;row<8;++row)for(int column=0;column<10;++column) {
    const Vec2 start=side==0?Vec2{900.0f+column*38,1900.0f+row*38}:Vec2{3900.0f-column*38,2900.0f-row*38};
    units.push_back(simulation.debugSpawn(Kind::Worker,0,start));
  }
  std::vector<Id> left(units.begin(),units.begin()+80),right(units.begin()+80,units.end());
  check(send(simulation,CommandType::Move,0,left,{3800,2800}).accepted,"left benchmark group accepts movement");
  check(send(simulation,CommandType::Move,0,right,{1000,2000}).accepted,"right benchmark group accepts movement");
  std::vector<double> timings;timings.reserve(1600);
  const auto wallStart=std::chrono::steady_clock::now();
  for(int step=0;step<1600;++step) {
    simulation.update(Simulation::Step);timings.push_back(simulation.lastStepMilliseconds());
  }
  int arrivals=0;
  int surviving=0;
  std::vector<std::pair<float,Id>> remaining;
  Vec2 leftMean{},rightMean{};
  for(std::size_t unitIndex=0;unitIndex<units.size();++unitIndex) {
    const Id unit=units[unitIndex];const auto* entity=simulation.find(unit);
    if(!entity||!entity->alive())continue;
    ++surviving;
    const float distanceToGoal=distance(entity->pos,entity->goal);
    remaining.push_back({distanceToGoal,unit});
    Vec2& mean=unitIndex<80?leftMean:rightMean;
    mean.x+=entity->pos.x;mean.y+=entity->pos.y;
    if(entity->order==Order::Idle||distanceToGoal<70)++arrivals;
  }
  leftMean.x/=80;leftMean.y/=80;rightMean.x/=80;rightMean.y/=80;
  std::sort(remaining.begin(),remaining.end());
  std::sort(timings.begin(),timings.end());
  const double mean=std::accumulate(timings.begin(),timings.end(),0.0)/timings.size();
  const auto navigation=simulation.navigationStats();
  std::cout<<"NAV_BENCHMARK workers="<<units.size()<<" simulation_seconds="<<simulation.time()
           <<" surviving="<<surviving<<" arrivals="<<arrivals<<" mean_step_ms="<<mean
           <<" p95_step_ms="<<timings[static_cast<std::size_t>(timings.size()*0.95)]
           <<" max_step_ms="<<timings.back()
           <<" searches="<<navigation.searches<<" expanded="<<navigation.expanded
           <<" failures="<<navigation.failures<<" budget_deferrals="<<navigation.budgetDeferrals
           <<" wall_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-wallStart).count()<<'\n';
  std::cout<<"NAV_DISTRIBUTION median_remaining="<<remaining[80].first<<" p95_remaining="<<remaining[152].first
           <<" max_remaining="<<remaining.back().first<<" left_mean="<<leftMean.x<<','<<leftMean.y
           <<" right_mean="<<rightMean.x<<','<<rightMean.y<<'\n';
  for(int index=0;index<8;++index) {
    const auto [distanceToGoal,unit]=remaining[remaining.size()-1-static_cast<std::size_t>(index)];
    const auto* entity=simulation.find(unit);
    std::cout<<"NAV_LAGGARD id="<<unit<<" remaining="<<distanceToGoal<<" pos="<<entity->pos.x<<','<<entity->pos.y
             <<" goal="<<entity->goal.x<<','<<entity->goal.y<<" order="<<static_cast<int>(entity->order)
             <<" path="<<entity->pathIndex<<'/'<<entity->path.size()<<" yield="<<entity->yieldFor
             <<" stalled="<<entity->stalledFor<<'\n';
  }
}

} // namespace

int main(int argc,char** argv) {
  std::cout<<std::fixed<<std::setprecision(3);
  std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"narrow turning gap",narrowTurningGap},
    {"blocked work-site sides",blockedWorkSiteSides},
    {"reachable alternatives",reachableAlternativeWins},
    {"inaccessible build status",inaccessibleBuildStatusMatchesCommand},
    {"busy mining corridor",busyMiningCorridor},
    {"foundation route invalidation",foundationRouteInvalidation},
    {"Hold and enemy collision",holdAndEnemyCollision},
    {"work-slot exhaustion semantics",workSlotExhaustionSemantics},
    {"impossible order persistence",impossibleOrderIsBoundedAndPersistent},
    {"navigation save validation and v4 migration",navigationSaveValidationAndMigration},
    {"displaced Idle yield recovery",displacedIdleDrainsYieldAndReclaimsGoal},
    {"repeated mixed-army retreat",repeatedMixedArmyRetreat},
    {"moving-target failed approach recovery",movingTargetRecoversFromFailedApproach}
  };
  if(argc>1&&std::string(argv[1])=="--benchmark")tests={{"navigation benchmark",benchmark}};
  else if(argc>1) {
    const std::string filter=argv[1];
    tests.erase(std::remove_if(tests.begin(),tests.end(),[&](const auto& test){return test.first.find(filter)==std::string::npos;}),tests.end());
    if(tests.empty()){std::cerr<<"No matching test: "<<filter<<'\n';return 2;}
  }
  int failures=0;
  for(const auto& [name,test]:tests) {
    try {test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error) {++failures;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
  }
  std::cout<<"RESULT passed="<<tests.size()-failures<<" failed="<<failures<<'\n';
  return failures?1:0;
}
