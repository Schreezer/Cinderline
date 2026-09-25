#include "Sim/Simulation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cinder;
namespace {
void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
float distance(Vec2 a, Vec2 b) { return std::hypot(a.x-b.x,a.y-b.y); }
void advance(Simulation& s, float seconds) {
  const auto count=static_cast<int>(std::ceil(seconds/Simulation::Step));
  for(int i=0;i<count;++i) s.update(Simulation::Step);
}
std::vector<Id> ids(const Simulation& s, int team, Kind kind) {
  std::vector<Id> found;
  for(const auto& e:s.entities()) if(e.alive()&&e.team==team&&e.kind==kind) found.push_back(e.id);
  return found;
}
Id first(const Simulation& s,int team,Kind kind) {
  auto found=ids(s,team,kind); check(!found.empty(),"fixture missing entity"); return found.front();
}
CommandResult send(Simulation& s,CommandType type,int team,std::vector<Id> units={},
                   Vec2 point={},Id target=0,Kind kind=Kind::Worker,int index=0) {
  return s.command({type,team,std::move(units),point,target,kind,index});
}
// These regression scenarios use fixed coordinates from the original maps.
// Authored revision gameplay and migration have their own MapTerrainSimulation suite.
Simulation quiet(int map=0) {
  Simulation s; s.reset({map,42,false,1,MatchLength::Standard,2,0});
  for(int t=0;t<2;++t) send(s,CommandType::Stop,t,ids(s,t,Kind::Worker));
  return s;
}
Vec2 validPlacement(const Simulation& s,int team,Kind kind,Vec2 base) {
  for(float radius=150;radius<600;radius+=50) for(int n=0;n<24;++n) {
    float angle=6.2831853f*n/24;
    Vec2 p{base.x+radius*std::cos(angle),base.y+radius*std::sin(angle)};
    if(s.canPlace(team,kind,p)) return p;
  }
  throw std::runtime_error("fixture cannot find legal placement");
}
Vec2 distantPlacement(const Simulation& s,int team,Kind kind,Id worker) {
  const auto origin=s.find(worker)->pos;
  for(float radius=350;radius<=650;radius+=50) for(int n=0;n<32;++n) {
    const float angle=6.2831853f*n/32;
    const Vec2 point{origin.x+radius*std::cos(angle),origin.y+radius*std::sin(angle)};
    if(s.canPlace(team,kind,point))return point;
  }
  throw std::runtime_error("fixture cannot find a distant legal construction site");
}
void waitUntil(Simulation& s,float seconds,const std::function<bool()>& reached,const std::string& message) {
  for(int i=0;i<static_cast<int>(std::ceil(seconds/Simulation::Step))&&!reached();++i)s.update(Simulation::Step);
  check(reached(),message);
}
void waitForConstruction(Simulation& s,Id building) {
  for(int n=0;n<600&&!s.constructionActive(building);++n)s.update(Simulation::Step);
  if(!s.constructionActive(building)) {
    const auto* site=s.find(building);const auto* worker=s.find(s.constructionWorker(building));
    std::cerr<<"CONSTRUCTION_ARRIVAL_FAILURE time="<<s.time()<<" building="<<building;
    if(site)std::cerr<<" site_x="<<site->pos.x<<" site_y="<<site->pos.y<<" progress="<<site->progress;
    if(worker)std::cerr<<" worker="<<worker->id<<" x="<<worker->pos.x<<" y="<<worker->pos.y
                       <<" distance="<<distance(worker->pos,site->pos)<<" order="<<static_cast<int>(worker->order)
                       <<" path_index="<<worker->pathIndex<<" path_size="<<worker->path.size()
                       <<" goal_x="<<worker->goal.x<<" goal_y="<<worker->goal.y;
    std::cerr<<'\n';
  }
  check(s.constructionActive(building),"builder physically reaches the construction site");
}
Id beginCarryingOre(Simulation& s,Id worker) {
  Id node=0;float best=1e9;
  for(const auto& e:s.entities())if(e.kind==Kind::Resource&&e.resource>0&&s.explored(s.find(worker)->team,e.pos)) {
    const float d=distance(s.find(worker)->pos,e.pos);if(d<best){best=d;node=e.id;}
  }
  check(node!=0&&send(s,CommandType::Gather,s.find(worker)->team,{worker},{},node).accepted,"builder first receives an ordinary mining order");
  waitUntil(s,30,[&]{return s.find(worker)->carried>=6;},"builder harvests cargo before construction interrupts it");
  return node;
}
std::string savePath(const std::string& suffix) {
  return (std::filesystem::temp_directory_path()/("cinderline-tests-"+suffix+".sav")).string();
}


std::vector<std::string> saveFields(const std::string& line) {
  std::istringstream in(line);std::vector<std::string> fields;std::string field;
  while(in>>field)fields.push_back(field);return fields;
}
std::string joinSaveFields(const std::vector<std::string>& fields) {
  std::ostringstream out;for(std::size_t i=0;i<fields.size();++i)out<<(i?" ":"")<<fields[i];return out.str();
}
struct SavedLayout { std::vector<std::size_t> entities,recordings;std::size_t effects=0; };
SavedLayout savedLayout(const std::vector<std::string>& lines) {
  SavedLayout result;std::size_t cursor=5;
  cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
  const auto entities=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
  for(std::size_t n=0;n<entities;++n) {
    result.entities.push_back(cursor++);
    cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
    cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
  }
  result.effects=cursor;
  const auto effects=static_cast<std::size_t>(std::stoul(saveFields(lines.at(cursor))[0]));
  cursor+=1+effects+4;
  const auto recordings=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
  for(std::size_t n=0;n<recordings;++n)result.recordings.push_back(cursor++);
  return result;
}
std::vector<std::string> legacyCombatSave(std::vector<std::string> lines,int version) {
  auto currentConfig=saveFields(lines.at(1));
  check(currentConfig.size()==7&&currentConfig.back()=="0","legacy fixture begins on map revision zero");
  currentConfig.pop_back();lines[1]=joinSaveFields(currentConfig);
  const auto layout=savedLayout(lines);const auto header=saveFields(lines.at(layout.effects));
  check(header.size()==2,"legacy fixture begins with the current effect header");
  const auto count=static_cast<std::size_t>(std::stoul(header.front()));
  lines.front()="CINDERLINE "+std::to_string(version);lines[layout.effects]=header.front();
  for(std::size_t row:layout.recordings) {
    auto fields=saveFields(lines.at(row));
    check(fields.size()>=13&&fields.size()==13+static_cast<std::size_t>(std::stoul(fields[12])),
          "legacy fixture begins with a save-thirteen recording row");
    fields.erase(fields.begin()+9,fields.begin()+12);
    check(fields.size()>=10&&fields.size()==10+static_cast<std::size_t>(std::stoul(fields[9])),
          "legacy fixture begins with a save-eleven recording row");
    fields.erase(fields.begin()+8);lines[row]=joinSaveFields(fields);
  }
  const auto orderQueues=std::find(lines.begin(),lines.end(),"ORDER_QUEUES 1");
  check(orderQueues!=lines.end(),"legacy fixture identifies the order-queue section");
  const auto sustainedOrders=std::find(lines.begin(),lines.end(),"SUSTAINED_ORDERS 1");
  const auto formationOrders=std::find(lines.begin(),lines.end(),"FORMATION_ORDERS 1");
  const auto queuedWork=std::find(lines.begin(),lines.end(),"QUEUED_WORK 1");
  check(sustainedOrders!=lines.end()&&formationOrders!=lines.end()&&queuedWork!=lines.end()&&
        orderQueues<sustainedOrders&&sustainedOrders<formationOrders&&formationOrders<queuedWork,
        "legacy fixture identifies the ordered save-fourteen terminal sections");
  lines.erase(queuedWork,lines.end());
  lines.erase(formationOrders,lines.end());
  lines.erase(orderQueues,lines.end());
  if(version<10) {
    auto config=saveFields(lines.at(1));check(config.size()==6,"legacy fixture starts from a current config row");config.pop_back();lines[1]=joinSaveFields(config);
    auto timeline=saveFields(lines.at(2));check(timeline.size()==6,"legacy fixture starts from a current timeline row");timeline.pop_back();lines[2]=joinSaveFields(timeline);
  }
  if(version<9) {auto config=saveFields(lines.at(1));check(config.size()==5,"version nine fixture retains match length");config.pop_back();lines[1]=joinSaveFields(config);}
  for(std::size_t n=0;n<count;++n) {
    auto fields=saveFields(lines.at(layout.effects+1+n));
    check(fields.size()==13,"legacy fixture begins with a typed v4 event");
    const bool explosion=fields[8]=="3"||fields[9]=="5";
    fields.resize(6);fields.push_back(explosion?"1":"0");lines[layout.effects+1+n]=joinSaveFields(fields);
  }
  if(version==1)for(std::size_t row:layout.entities) {
    auto fields=saveFields(lines[row]);check(fields.size()==24,"v1 fixture strips only known entity assignment fields");
    fields.resize(22);lines[row]=joinSaveFields(fields);
  }
  if(version<3) {
    const auto marker=std::find(lines.begin(),lines.end(),"AI_KNOWLEDGE 1");
    check(marker!=lines.end(),"legacy fixture identifies the knowledge section");lines.erase(marker,lines.end());
  }
  const auto navigation=std::find(lines.begin(),lines.end(),"NAVIGATION 1");
  if(navigation!=lines.end())lines.erase(navigation,lines.end());
  return lines;
}

void resetAndDefinitions() {
  auto s=quiet();
  check(definitions().size()==15,"roster must contain fifteen definitions");
  for(const auto& d:definitions()) {
    check(d.hp>0&&d.radius>0&&d.cost>=0&&d.buildTime>=0,"definition has invalid bounds");
    if(!d.building&&d.kind!=Kind::Resource) check(d.speed>0&&d.supply>0,"unit must move and consume supply");
  }
  check(ids(s,0,Kind::Worker).size()==5&&ids(s,1,Kind::Worker).size()==5,"symmetric initial workers");
  check(s.players()[0].ore==500&&s.players()[1].ore==500,"symmetric starting funds");
  auto worker=first(s,0,Kind::Worker);
  send(s,CommandType::Move,0,{worker},{900,900}); advance(s,2);
  check(s.time()>0&&!s.recording().empty(),"commands recorded while running");
  s.reset({0,42,false,1,MatchLength::Standard,2,0});
  check(s.tick()==0&&s.winner()==-1&&s.recording().empty(),"reset clears time, result and recording");
  Simulation fresh; fresh.reset({0,42,false,1,MatchLength::Standard,2,0});
  check(s.stateHash()==fresh.stateHash(),"reset same seed produces identical state");
}

void gatherAndDepletion() {
  auto s=quiet(); auto w=first(s,0,Kind::Worker); auto p=s.find(w)->pos;
  Id resource=0; float best=1e9;
  for(const auto& e:s.entities()) if(e.kind==Kind::Resource&&e.resource>0&&distance(p,e.pos)<best) {
    resource=e.id;best=distance(p,e.pos);
  }
  check(resource!=0,"resource exists"); const float initial=s.find(resource)->resource;
  check(send(s,CommandType::Gather,0,{w},{},resource).accepted,"gather accepted");
  bool carrying=false,returned=false;
  for(int i=0;i<4000;++i) {
    s.update(Simulation::Step);
    if(s.find(w)->carried>0) {
      carrying=true;
      if(!returned) check(s.players()[0].ore==500,"ore credited before delivery");
    }
    if(s.players()[0].ore>500) {returned=true;break;}
  }
  if(!carrying||!returned) {
    const auto* worker=s.find(w);const auto* node=s.find(resource);
    std::cerr<<"GATHER_DIAGNOSTIC carried="<<worker->carried<<" returning="<<worker->returning
             <<" worker_x="<<worker->pos.x<<" worker_y="<<worker->pos.y
             <<" node_distance="<<distance(worker->pos,node->pos)<<" node_ore="<<node->resource
             <<" player_ore="<<s.players()[0].ore<<'\n';
  }
  check(carrying&&returned,"worker harvests, carries, and returns physical ore");
  check(s.find(resource)->resource<initial,"harvest depletes finite node");
  check(s.players()[0].stats.gathered==s.players()[0].ore-500,"gather stats reflect delivered ore");
  check(send(s,CommandType::Gather,0,ids(s,0,Kind::Worker),{},resource).accepted,"group gather accepted");
  for(int i=0;i<40000&&s.find(resource)&&s.find(resource)->resource>0;++i) s.update(Simulation::Step);
  const auto* node=s.find(resource);
  check(!node||node->resource<=0,"finite node eventually exhausts");
  for(const auto& e:s.entities()) check(e.carried>=0&&e.resource>=0,"no negative ore on entity");
  check(s.players()[0].ore>=500,"economy does not spend without commands");
}

void paidCommandsAndQueues() {
  auto s=quiet(); Id hq=first(s,0,Kind::Headquarters);
  const auto& d=definition(Kind::Worker); const int initial=s.players()[0].ore;
  check(send(s,CommandType::Train,0,{hq},{},0,Kind::Worker).accepted,"worker production accepted");
  check(s.players()[0].ore==initial-d.cost,"production charges actual cost");
  check(s.supply(0)==5+d.supply,"queue reserves supply immediately");
  advance(s,std::max(0.05f,d.buildTime-0.15f));
  check(ids(s,0,Kind::Worker).size()==5,"unit not produced before timer");
  advance(s,0.25f);
  check(ids(s,0,Kind::Worker).size()==6,"unit spawns after timer");
  check(s.players()[0].stats.produced==1,"production counter incremented");
  for(int i=0;i<100;++i) send(s,CommandType::Train,0,{hq},{},0,Kind::Worker);
  check(s.players()[0].ore>=0,"repeated paid commands cannot overdraw");
  check(s.supply(0)<=s.capacity(0),"production cannot overreserve capacity");
  auto cancel=quiet(); hq=first(cancel,0,Kind::Headquarters);
  send(cancel,CommandType::Train,0,{hq},{},0,Kind::Worker);
  send(cancel,CommandType::Train,0,{hq},{},0,Kind::Worker);
  int before=cancel.players()[0].ore;
  check(send(cancel,CommandType::CancelQueue,0,{hq},{},0,Kind::Worker,1).accepted,"cancel waiting item");
  check(cancel.players()[0].ore==before+d.cost,"unstarted queue item fully refunded");
  advance(cancel,d.buildTime/2);
  before=cancel.players()[0].ore;
  check(send(cancel,CommandType::CancelQueue,0,{hq},{},0,Kind::Worker,0).accepted,"cancel active item");
  const int refund=cancel.players()[0].ore-before;
  check(refund>0&&refund<d.cost,"active cancellation refunds only unspent progress");
  check(cancel.supply(0)==5&&cancel.find(hq)->queue.empty(),"cancellation frees reserved supply");
  auto capped=quiet(); hq=first(capped,0,Kind::Headquarters);
  while(capped.supply(0)<capped.capacity(0)) capped.debugSpawn(Kind::Worker,0,{900.f+10*capped.supply(0),500});
  before=capped.players()[0].ore;
  check(!send(capped,CommandType::Train,0,{hq},{},0,Kind::Worker).accepted,"training rejected at supply cap");
  check(capped.players()[0].ore==before,"rejected supply command costs nothing");
}

void boundedProductionQueue() {
  auto s=quiet();
  const Id foundry=s.debugSpawn(Kind::Foundry,0,{1500,1200});
  s.debugSpawn(Kind::Processor,0,{1250,1450});
  s.debugResources(0,100000);
  const std::vector<Kind> pattern{Kind::Striker,Kind::Lancer,Kind::Scout};
  const int initialOre=s.players()[0].ore,initialSupply=s.supply(0);
  int queuedCost=0,queuedSupply=0;
  std::vector<Kind> queued;
  for(int i=0;i<Simulation::MaxQueue;++i) {
    const Kind kind=pattern[static_cast<std::size_t>(i)%pattern.size()];
    check(send(s,CommandType::Train,0,{foundry},{},0,kind).accepted,"production queue accepts item through the public cap");
    queued.push_back(kind);queuedCost+=definition(kind).cost;queuedSupply+=definition(kind).supply;
  }
  check(s.find(foundry)->queue.size()==Simulation::MaxQueue,"production queue reaches twenty items");
  check(s.players()[0].ore==initialOre-queuedCost&&s.supply(0)==initialSupply+queuedSupply,
        "twenty accepted items debit their exact cumulative cost and reserve their exact cumulative supply");
  const int oreAtCap=s.players()[0].ore,supplyAtCap=s.supply(0);
  const auto rejected=send(s,CommandType::Train,0,{foundry},{},0,Kind::Striker);
  check(!rejected.accepted&&rejected.message=="Queue full (20).","twenty-first item reports the explicit queue cap");
  check(s.players()[0].ore==oreAtCap&&s.supply(0)==supplyAtCap&&s.find(foundry)->queue.size()==Simulation::MaxQueue,
        "queue-cap rejection does not charge ore or reserve supply");

  const auto path=savePath("queue-cap");check(s.save(path),"full twenty-item queue saves");
  Simulation loaded;check(loaded.load(path),"full twenty-item queue loads");
  check(loaded.stateHash()==s.stateHash(),"full queue preserves its save-load hash");

  advance(s,definition(queued.front()).buildTime/2);advance(loaded,definition(queued.front()).buildTime/2);
  const int tailCost=definition(queued.back()).cost,tailSupply=definition(queued.back()).supply;
  const int oreBeforeTail=s.players()[0].ore,supplyBeforeTail=s.supply(0);
  check(send(s,CommandType::CancelQueue,0,{foundry},{},0,Kind::Worker,Simulation::MaxQueue-1).accepted,
        "tail item beyond the former cap can be cancelled");
  check(send(loaded,CommandType::CancelQueue,0,{foundry},{},0,Kind::Worker,Simulation::MaxQueue-1).accepted,
        "loaded tail item beyond the former cap can be cancelled");
  check(s.players()[0].ore==oreBeforeTail+tailCost&&s.supply(0)==supplyBeforeTail-tailSupply,
        "unstarted tail cancellation refunds its full cost and supply reservation");
  const int oreBeforeHead=s.players()[0].ore;
  check(send(s,CommandType::CancelQueue,0,{foundry},{},0,Kind::Worker,0).accepted,"partially started head can be cancelled");
  check(send(loaded,CommandType::CancelQueue,0,{foundry},{},0,Kind::Worker,0).accepted,"loaded partially started head can be cancelled");
  const int headRefund=s.players()[0].ore-oreBeforeHead;
  check(headRefund>0&&headRefund<definition(queued.front()).cost,"partially started head refunds only its unused cost");
  check(s.stateHash()==loaded.stateHash(),"loaded queue stays deterministic through tail and head cancellation");
  const auto* producer=s.find(foundry);check(producer->queue.size()==Simulation::MaxQueue-2,"two cancellations leave eighteen items");
  for(std::size_t i=0;i<producer->queue.size();++i)
    check(producer->queue[i].kind==queued[i+1],"cancellation preserves the remaining FIFO order");
  const int producedBefore=s.players()[0].stats.produced;
  advance(s,definition(queued[1]).buildTime+0.1f);advance(loaded,definition(queued[1]).buildTime+0.1f);
  check(s.players()[0].stats.produced==producedBefore+1&&s.find(foundry)->queue.front().kind==queued[2],
        "the next remaining item completes first");
  check(s.stateHash()==loaded.stateHash(),"loaded full queue continues deterministically");
  std::filesystem::remove(path);
}

void placementAndConstruction() {
  auto s=quiet(); auto h=s.find(first(s,0,Kind::Headquarters))->pos;
  auto w=first(s,0,Kind::Worker); int ore=s.players()[0].ore;
  check(!s.canPlace(0,Kind::Foundry,{-100,100}),"reject out-of-map placement");
  check(!s.canPlace(0,Kind::Foundry,h),"reject building overlap");
  check(!s.canPlace(0,Kind::Foundry,{4200,4200}),"reject unexplored placement");
  check(!send(s,CommandType::Build,0,{w},h,0,Kind::Foundry).accepted,"overlap command rejected");
  check(s.players()[0].ore==ore,"invalid placement never charges");
  if(!s.obstacles().empty()) check(!s.canPlace(0,Kind::Foundry,s.obstacles()[0].center),"reject solid terrain placement");
  auto p=validPlacement(s,0,Kind::Foundry,h);
  s.debugSpawn(Kind::Worker,0,p);
  check(!s.canPlace(0,Kind::Foundry,p),"friendly ground unit blocks building footprint");
  p=validPlacement(s,0,Kind::Foundry,h);
  check(send(s,CommandType::Build,0,{w},p,0,Kind::Foundry).accepted,"legal paid build accepted");
  check(s.players()[0].ore==ore-definition(Kind::Foundry).cost,"construction charged once");
  Id building=first(s,0,Kind::Foundry);
  check(s.find(building)->progress<1,"new building under construction");
  check(!send(s,CommandType::Train,0,{building},{},0,Kind::Striker).accepted,"unfinished producer rejects training");
  waitForConstruction(s,building);
  advance(s,definition(Kind::Foundry).buildTime+1);
  check(s.find(building)->progress>=1,"construction completes after build time");
  check(s.find(building)->hp==definition(Kind::Foundry).hp,"undamaged completed building has exact full health");
  auto cancelled=quiet(); w=first(cancelled,0,Kind::Worker);
  p=validPlacement(cancelled,0,Kind::Foundry,h);
  send(cancelled,CommandType::Build,0,{w},p,0,Kind::Foundry);
  building=first(cancelled,0,Kind::Foundry); advance(cancelled,2);
  const int paid=cancelled.players()[0].ore;
  check(send(cancelled,CommandType::CancelBuilding,0,{building}).accepted,"unfinished building can be cancelled");
  check(!cancelled.find(building)||!cancelled.find(building)->alive(),"cancelled construction removed");
  check(cancelled.players()[0].ore>paid&&cancelled.players()[0].ore<=500,"construction refund bounded by cost");
  auto damaged=quiet();w=first(damaged,0,Kind::Worker);
  p=validPlacement(damaged,0,Kind::Foundry,h);
  check(send(damaged,CommandType::Build,0,{w},p,0,Kind::Foundry).accepted,"damage fixture begins legal construction");
  building=first(damaged,0,Kind::Foundry);
  const Id attacker=damaged.debugSpawn(Kind::Striker,1,{p.x+220,p.y});
  check(send(damaged,CommandType::Attack,1,{attacker},{},building).accepted,"unfinished building can be attacked");
  advance(damaged,0.25f);
  const auto* unfinished=damaged.find(building);
  check(unfinished&&unfinished->alive()&&unfinished->progress<1,"attacked construction survives unfinished");
  const auto& foundry=definition(Kind::Foundry);
  const float damageTaken=damaged.players()[1].stats.damage;
  check(damageTaken>1,"actual attack removed construction health");
  check(send(damaged,CommandType::Move,1,{attacker},{4100,100}).accepted,"attacker withdraws before completion");
  waitForConstruction(damaged,building);
  advance(damaged,foundry.buildTime+1);
  const auto* completed=damaged.find(building);
  check(completed&&completed->alive()&&completed->progress==1,"damaged building still completes");
  check(std::abs(completed->hp-(foundry.hp-damageTaken))<0.05f,"completion preserves damage taken during construction");
  std::cout<<"CONSTRUCTION_HEALTH full_hp="<<s.find(first(s,0,Kind::Foundry))->hp
           <<" damage_taken="<<damageTaken<<" completed_damaged_hp="<<completed->hp<<'\n';
}

void workerConstructionEconomy() {
  auto s=quiet();const Id worker=first(s,0,Kind::Worker),node=beginCarryingOre(s,worker);
  const Vec2 start=s.find(worker)->pos,site=distantPlacement(s,0,Kind::Foundry,worker);
  const float cargo=s.find(worker)->carried,remaining=s.find(node)->resource;
  const int ore=s.players()[0].ore,gathered=s.players()[0].stats.gathered;
  check(send(s,CommandType::Build,0,{worker},site,0,Kind::Foundry).accepted,"mining worker accepts a distant paid construction order");
  const Id building=first(s,0,Kind::Foundry);const int paid=ore-definition(Kind::Foundry).cost;
  check(s.players()[0].ore==paid&&s.find(worker)->carried==cargo,"placing a foundation charges its cost and preserves carried ore");
  check(!s.constructionActive(building)&&s.find(building)->progress==0,"foundation does no work before its builder arrives");
  advance(s,0.25f);
  check(distance(start,s.find(worker)->pos)>1,"builder visibly leaves its mining position for the construction site");
  check(s.find(building)->progress==0,"travel time does not count toward construction");
  waitForConstruction(s,building);
  check(distance(s.find(worker)->pos,site)<=definition(Kind::Foundry).radius+definition(Kind::Worker).radius+40,
        "construction requires the worker at the building perimeter");
  advance(s,5);
  check(s.find(building)->progress>0&&s.find(building)->progress<1,"present worker advances unfinished construction");
  check(s.find(node)->resource==remaining&&s.find(worker)->carried==cargo,"builder neither harvests nor discards its cargo while travelling or working");
  check(s.players()[0].ore==paid&&s.players()[0].stats.gathered==gathered,"construction worker generates no mining income");
  waitUntil(s,definition(Kind::Foundry).buildTime+1,[&]{return s.find(building)->progress==1;},"attended construction completes");
  check(s.find(worker)->order==Order::Gather&&s.find(worker)->resourceTarget==node,"completed builder automatically returns to its prior ore deposit");
  check(s.find(worker)->carried>=cargo,"returning builder retains ore harvested before construction");
  check(s.constructionWorker(building)==0&&s.players()[0].stats.built==1,"completion releases its sole worker and counts the building once");
  waitUntil(s,30,[&]{return s.players()[0].ore>paid;},"builder resumes physical mining and delivers ore after finishing");
  check(s.players()[0].stats.gathered>gathered,"resumed mining adds actual delivered income");
  std::cout<<"WORKER_CONSTRUCTION travelled="<<distance(start,site)<<" cargo_preserved="<<cargo
           <<" finished_at="<<s.time()<<" delivered_after="<<s.players()[0].stats.gathered-gathered<<'\n';
}

void workerConstructionInterruptions() {
  auto s=quiet();const Id worker=first(s,0,Kind::Worker);
  const Vec2 site=distantPlacement(s,0,Kind::Foundry,worker);
  check(send(s,CommandType::Build,0,{worker},site,0,Kind::Foundry).accepted,"pause fixture places a foundation");
  const Id building=first(s,0,Kind::Foundry);waitForConstruction(s,building);advance(s,3);
  check(send(s,CommandType::Move,0,{worker},{1300,500}).accepted,"construction worker can be ordered away");
  const float stopped=s.find(building)->progress;const int paid=s.players()[0].ore;
  advance(s,5);
  check(s.find(building)->progress==stopped&&!s.constructionActive(building),"construction pauses immediately when its worker leaves");
  check(s.constructionWorker(building)==0&&distance(s.find(worker)->pos,site)>150,"departing worker actually moves away and leaves the foundation unassigned");
  check(!send(s,CommandType::ResumeConstruction,1,ids(s,1,Kind::Worker),{},building).accepted,"opponent cannot resume another player's foundation");
  check(!send(s,CommandType::ResumeConstruction,0,{first(s,0,Kind::Headquarters)},{},building).accepted,"a structure cannot act as a replacement worker");
  check(send(s,CommandType::ResumeConstruction,0,{worker},{},building).accepted,"returning worker resumes an already paid foundation");
  check(s.players()[0].ore==paid,"resuming construction charges no additional ore");
  waitForConstruction(s,building);advance(s,1);
  check(s.find(building)->progress>stopped,"returning worker advances from retained progress");
  check(send(s,CommandType::Stop,0,{worker}).accepted,"Stop also interrupts construction");
  const float stoppedAgain=s.find(building)->progress;advance(s,2);
  check(s.find(building)->progress==stoppedAgain&&s.find(worker)->order!=Order::Gather,"explicit Stop pauses construction and does not silently restart mining");
  check(send(s,CommandType::ResumeConstruction,0,ids(s,0,Kind::Worker),{},building).accepted,"group selection can resume a foundation");
  const Id assigned=s.constructionWorker(building);check(assigned!=0,"resumed site has one assigned worker");
  int constructing=0;for(const auto& e:s.entities())if(e.alive()&&e.team==0&&e.order==Order::Construct&&e.target==building)++constructing;
  check(constructing==1,"selecting several workers assigns only one constructor");
  waitForConstruction(s,building);const float before=s.find(building)->progress;advance(s,2);
  check(std::abs(s.find(building)->progress-before-2/definition(Kind::Foundry).buildTime)<0.001f,"group selection cannot accelerate the normal construction rate");
  s.debugResources(0,1000);const Vec2 nextSite=distantPlacement(s,0,Kind::Processor,assigned);
  check(send(s,CommandType::Build,0,{assigned},nextSite,0,Kind::Processor).accepted,"worker can be retasked to a different foundation");
  const Id nextBuilding=first(s,0,Kind::Processor);const float oldProgress=s.find(building)->progress;advance(s,2);
  check(s.find(building)->progress==oldProgress&&s.constructionWorker(building)==0,"retasking a builder leaves the earlier foundation paused");
  check(s.constructionWorker(nextBuilding)==assigned,"retasked builder belongs only to its new site");
  check(send(s,CommandType::CancelBuilding,0,{nextBuilding}).accepted,"new site may be cancelled during travel or work");
  check(s.find(assigned)->order!=Order::Construct&&s.constructionWorker(nextBuilding)==0,"cancelled foundation releases its worker");
  check(s.find(building)->progress==oldProgress,"cancelling another site never restarts an older foundation");

  auto cancel=quiet();const Id miner=first(cancel,0,Kind::Worker),node=beginCarryingOre(cancel,miner);
  const float cargo=cancel.find(miner)->carried;
  const Vec2 cancelSite=distantPlacement(cancel,0,Kind::Foundry,miner);
  check(send(cancel,CommandType::Build,0,{miner},cancelSite,0,Kind::Foundry).accepted,"mining cancellation fixture begins construction");
  const Id cancelled=first(cancel,0,Kind::Foundry);
  check(send(cancel,CommandType::CancelBuilding,0,{cancelled}).accepted,"unstarted foundation cancellation accepted");
  check(cancel.players()[0].ore==500&&cancel.find(miner)->carried==cargo,"cancelling unstarted work refunds its cost and retains cargo");
  check(cancel.find(miner)->order==Order::Gather&&cancel.find(miner)->resourceTarget==node,"cancellation returns its uninterrupted mining worker to the same node");

  auto group=quiet();const auto groupWorkers=ids(group,0,Kind::Worker);
  const Vec2 groupSite=distantPlacement(group,0,Kind::Foundry,groupWorkers.front());
  check(send(group,CommandType::Build,0,groupWorkers,groupSite,0,Kind::Foundry).accepted,"a multi-worker selection may place one foundation");
  const Id groupBuilding=first(group,0,Kind::Foundry);int busy=0;
  for(Id id:groupWorkers)if(group.find(id)->order==Order::Construct)++busy;
  check(busy==1&&group.players()[0].ore==250,"group placement spends once and reserves exactly one worker");
  waitForConstruction(group,groupBuilding);const float groupProgress=group.find(groupBuilding)->progress;advance(group,2);
  check(std::abs(group.find(groupBuilding)->progress-groupProgress-2/definition(Kind::Foundry).buildTime)<0.001f,"group placement does not multiply construction speed");
}

void workerConstructionHazards() {
  auto killed=quiet();const Id worker=first(killed,0,Kind::Worker);
  const Vec2 site=distantPlacement(killed,0,Kind::Foundry,worker);
  check(send(killed,CommandType::Build,0,{worker},site,0,Kind::Foundry).accepted,"builder-death fixture starts construction");
  const Id building=first(killed,0,Kind::Foundry);waitForConstruction(killed,building);advance(killed,1);
  const Vec2 builderPosition=killed.find(worker)->pos;
  const Id enemy=killed.debugSpawn(Kind::Bastion,1,{builderPosition.x+200,builderPosition.y});
  check(send(killed,CommandType::Attack,1,{enemy},{},worker).accepted,"enemy attacks the actual constructor");
  waitUntil(killed,15,[&]{return !killed.find(worker)||!killed.find(worker)->alive();},"constructor dies from ordinary enemy attacks");
  check(send(killed,CommandType::Move,1,{enemy},{4000,500}).accepted,"attacker withdraws after killing the worker");
  const float abandoned=killed.find(building)->progress;advance(killed,3);
  check(killed.find(building)->progress==abandoned&&killed.constructionWorker(building)==0,"worker death leaves paid progress paused");
  const Id replacement=first(killed,0,Kind::Worker);const int paid=killed.players()[0].ore;
  check(send(killed,CommandType::ResumeConstruction,0,{replacement},{},building).accepted,"surviving worker can replace the killed builder");
  waitForConstruction(killed,building);advance(killed,1);
  check(killed.players()[0].ore==paid&&killed.find(building)->progress>abandoned,"replacement continues retained progress without another payment");

  auto destroyed=quiet();const Id miner=first(destroyed,0,Kind::Worker),node=beginCarryingOre(destroyed,miner);
  const float cargo=destroyed.find(miner)->carried;
  const Vec2 doomedSite=distantPlacement(destroyed,0,Kind::Foundry,miner);
  check(send(destroyed,CommandType::Build,0,{miner},doomedSite,0,Kind::Foundry).accepted,"site-destruction fixture interrupts a mining worker");
  const Id doomed=first(destroyed,0,Kind::Foundry);std::vector<Id> siege;
  for(int i=0;i<2;++i)siege.push_back(destroyed.debugSpawn(Kind::Mortar,1,{doomedSite.x+350,doomedSite.y+i*70.f}));
  check(send(destroyed,CommandType::Attack,1,siege,{},doomed).accepted,"unfinished foundation can be destroyed in combat");
  waitUntil(destroyed,1,[&]{return !destroyed.find(doomed)||!destroyed.find(doomed)->alive();},"enemy destroys the foundation before its worker arrives");
  check(destroyed.find(miner)&&destroyed.find(miner)->alive(),"foundation destruction fixture preserves the distant worker");
  check(destroyed.find(miner)->order==Order::Gather&&destroyed.find(miner)->resourceTarget==node,"destroyed site releases its worker back to mining");
  check(destroyed.find(miner)->carried==cargo,"destroyed foundation never consumes the worker's carried ore");

  auto blocked=quiet();const Vec2 trappedPosition{1200,700},blockedSite{1620,700};
  const Id trapped=blocked.debugSpawn(Kind::Worker,0,trappedPosition);
  // Real, solid ore deposits form an unbroken ring. The worker is outside the
  // proposed building but has no traversable route to any perimeter work spot.
  for(int n=0;n<12;++n) {
    const float angle=6.2831853f*n/12;
    blocked.debugSpawn(Kind::Resource,-1,{trappedPosition.x+110*std::cos(angle),trappedPosition.y+110*std::sin(angle)});
  }
  check(blocked.canPlace(0,Kind::Foundry,blockedSite),"blocked-builder fixture still has a legal visible building footprint");
  check(!send(blocked,CommandType::Build,0,{trapped},blockedSite,0,Kind::Foundry).accepted,"unreachable construction is rejected before a foundation is charged");
  advance(blocked,definition(Kind::Foundry).buildTime+5);
  check(ids(blocked,0,Kind::Foundry).empty(),"unreachable construction creates no remote foundation");
  check(distance(blocked.find(trapped)->pos,trappedPosition)<70,"blocked builder does not pass through solid deposits");
  check(blocked.players()[0].ore==500,"blocked construction does not charge ore");
}

void researchAndPrerequisites() {
  auto s=quiet(); auto w=first(s,0,Kind::Worker);
  check(!send(s,CommandType::Build,0,{w},{800,850},0,Kind::Laboratory).accepted,"lab requires foundry");
  check(!send(s,CommandType::Build,0,{w},{800,850},0,Kind::MotorPool).accepted,"motor pool requires advanced tier");
  Id lab=s.debugSpawn(Kind::Laboratory,0,{1000,600});
  const int ore=s.players()[0].ore;
  check(!send(s,CommandType::Research,0,{first(s,0,Kind::Headquarters)},{},0,Kind::Worker,0).accepted,"HQ cannot research");
  check(s.players()[0].ore==ore,"rejected research costs nothing");
  check(send(s,CommandType::Research,0,{lab},{},0,Kind::Worker,0).accepted,"tier research accepted at laboratory");
  check(s.players()[0].tier==1,"research has no immediate benefit");
  advance(s,99.9f); check(s.players()[0].tier==1,"tier research waits full duration");
  advance(s,0.2f); check(s.players()[0].tier==2,"tier research completes");
  s.debugResources(0,1000);
  check(send(s,CommandType::Research,0,{lab},{},0,Kind::Worker,1).accepted,"weapon upgrade accepted");
  advance(s,60.1f); check(s.players()[0].weapons==1,"weapon upgrade recorded");
  check(send(s,CommandType::Research,0,{lab},{},0,Kind::Worker,2).accepted,"armor upgrade accepted");
  advance(s,60.1f); check(s.players()[0].armor==1,"armor upgrade recorded");
  check(s.players()[0].stats.upgrades==3,"completed upgrades counted");
  auto gated=quiet();lab=gated.debugSpawn(Kind::Laboratory,0,{1000,600});gated.debugResources(0,1000);
  check(send(gated,CommandType::Research,0,{lab},{},0,Kind::Worker,1).accepted,"first weapon level accepted at tier one");
  advance(gated,60.1f);const int remaining=gated.players()[0].ore;
  check(!send(gated,CommandType::Research,0,{lab},{},0,Kind::Worker,1).accepted,"second weapon level requires higher tier");
  check(gated.players()[0].ore==remaining,"tier-gated research does not charge");

  auto query=[&](const Simulation& sim,const std::vector<Id>& workers,const Vec2* site=nullptr) {
    const auto hash=sim.stateHash();const auto recorded=sim.recording().size();
    const auto result=sim.buildStatus(0,Kind::MotorPool,workers,site);
    check(sim.stateHash()==hash&&sim.recording().size()==recorded,"build query leaves all authoritative state and recorded commands untouched");
    return result;
  };
  auto rejectLikeCommand=[&](Simulation& sim,const std::vector<Id>& workers,Vec2 site,const std::string& expected) {
    const auto hash=sim.stateHash();const auto recorded=sim.recording().size();
    const auto status=query(sim,workers,&site);
    check(!status.accepted&&status.message.find(expected)!=std::string::npos,"Crucible query explains "+expected);
    const auto command=send(sim,CommandType::Build,0,workers,site,0,Kind::MotorPool);
    check(!command.accepted&&command.message==status.message,"rejected Build returns exactly the read-only placement reason");
    check(sim.stateHash()==hash&&sim.recording().size()==recorded,"rejected Build spends nothing and records nothing");
  };
  auto locked=quiet();const Id lockedWorker=first(locked,0,Kind::Worker);
  const Vec2 lockedSite=distantPlacement(locked,0,Kind::MotorPool,lockedWorker);
  const auto tierLock=query(locked,{lockedWorker});
  check(!tierLock.accepted&&tierLock.message.find("T2")!=std::string::npos&&tierLock.message.find("Resonator")!=std::string::npos,
        "Crucible menu status names required T2 and the Resonator upgrade path");
  rejectLikeCommand(locked,{lockedWorker},lockedSite,"T2");
  rejectLikeCommand(locked,{},lockedSite,"Select");
  rejectLikeCommand(locked,{first(locked,1,Kind::Worker)},lockedSite,"foreign");
  rejectLikeCommand(locked,{first(locked,0,Kind::Headquarters)},lockedSite,"Drudge");
  rejectLikeCommand(locked,{lockedWorker},{std::numeric_limits<float>::quiet_NaN(),0},"Invalid command");
  check(!query(locked,{}).accepted&&!query(locked,{first(locked,0,Kind::Headquarters)}).accepted,
        "menu availability still requires a selected live friendly Drudge");

  // Reuse the real completed T2 research above, rather than setting a tier directly.
  w=first(s,0,Kind::Worker);
  Vec2 crucibleSite=distantPlacement(s,0,Kind::MotorPool,w);
  check(!query(s,{w}).accepted,"T2 alone does not bypass the operational Kiln requirement");
  rejectLikeCommand(s,{w},crucibleSite,"operational Kiln");
  const Vec2 kilnSite=distantPlacement(s,0,Kind::Foundry,w);
  check(send(s,CommandType::Build,0,{w},kilnSite,0,Kind::Foundry).accepted,"availability fixture places a normal paid Kiln");
  const Id kiln=first(s,0,Kind::Foundry);
  rejectLikeCommand(s,{w},crucibleSite,"operational Kiln");
  waitForConstruction(s,kiln);
  waitUntil(s,definition(Kind::Foundry).buildTime+1,[&]{return s.find(kiln)->progress>=1;},"availability fixture completes its assigned-worker Kiln");
  crucibleSite=distantPlacement(s,0,Kind::MotorPool,w);
  s.debugResources(0,definition(Kind::MotorPool).cost-1);
  check(!query(s,{w}).accepted,"Crucible menu status rejects insufficient funds without a site");
  rejectLikeCommand(s,{w},crucibleSite,"Insufficient ore");
  s.debugResources(0,definition(Kind::MotorPool).cost);
  check(query(s,{w}).accepted&&query(s,{w},&crucibleSite).accepted,"funded T2 player with an operational Kiln can build at a valid site");
  const Id distantWorker=s.debugSpawn(Kind::Worker,0,{3100,650});
  check(query(s,{distantWorker}).accepted,"menu query omits distance until a site is chosen");
  rejectLikeCommand(s,{distantWorker},crucibleSite,"closer");
  rejectLikeCommand(s,{distantWorker},{3100,1200},"current vision");
  const Vec2 occupied=s.find(w)->pos;
  check(!s.canPlace(0,Kind::MotorPool,occupied),"placement fixture is physically occupied");
  rejectLikeCommand(s,{w},occupied,"");

  const std::vector<Id> selected{distantWorker,w,w};
  const auto hashBefore=s.stateHash();const auto commandsBefore=s.recording().size();
  const auto entitiesBefore=s.entities().size();const int oreBefore=s.players()[0].ore;
  check(query(s,selected,&crucibleSite).accepted&&query(s,selected,&crucibleSite).accepted,"repeated valid queries accept the same legal duplicate selection");
  check(s.stateHash()==hashBefore&&s.entities().size()==entitiesBefore&&s.recording().size()==commandsBefore,
        "accepted queries neither place a foundation nor retask a builder");
  check(send(s,CommandType::Build,0,selected,crucibleSite,0,Kind::MotorPool).accepted,"authoritative Build accepts the queried Crucible site");
  const Id crucible=first(s,0,Kind::MotorPool);
  check(s.players()[0].ore==oreBefore-definition(Kind::MotorPool).cost&&s.entities().size()==entitiesBefore+1
        &&s.recording().size()==commandsBefore+1,"accepted Crucible command pays, spawns and records exactly once");
  check(s.constructionWorker(crucible)==w&&s.find(w)->order==Order::Construct&&s.find(distantWorker)->order!=Order::Construct,
        "shared validation preserves the eligible nearby worker assignment");
}

void ownershipAndFog() {
  auto s=quiet(); auto own=first(s,0,Kind::Worker), enemy=first(s,1,Kind::Worker);
  const auto enemyPos=s.find(enemy)->pos;
  check(s.visible(0,s.find(own)->pos),"friendly units reveal map");
  check(!s.visible(0,enemyPos)&&!s.explored(0,enemyPos),"enemy base starts concealed");
  check(!send(s,CommandType::Move,0,{enemy},{500,500}).accepted,"cannot issue move to opponent");
  check(!send(s,CommandType::Attack,0,{own},{},enemy).accepted,"cannot target hidden opponent");
  check(!send(s,CommandType::Train,0,{first(s,1,Kind::Headquarters)},{},0,Kind::Worker).accepted,"cannot spend through enemy producer");
  check(!send(s,CommandType::Move,7,{own},{500,500}).accepted,"invalid team rejected");
  check(!send(s,CommandType::Move,0,{999999},{500,500}).accepted,"stale entity id rejected");
  check(!send(s,CommandType::Move,0,{own,enemy},{500,500}).accepted,"mixed ownership command rejected atomically");
  check(!send(s,CommandType::Move,0,{own},{std::numeric_limits<float>::quiet_NaN(),500}).accepted,"nonfinite command destination rejected");
  auto scout=s.debugSpawn(Kind::Scout,0,{enemyPos.x-200,enemyPos.y}); advance(s,0.1f);
  check(s.visible(0,enemyPos),"scout reveals opponent");
  check(send(s,CommandType::Attack,0,{scout},{},enemy).accepted,"visible enemy target accepted");
  send(s,CommandType::Move,0,{scout},{3300,3300}); advance(s,20);
  check(s.explored(0,enemyPos),"exploration persists after scout leaves");
  auto rally=quiet();Id hq=first(rally,0,Kind::Headquarters);Id hiddenResource=0;
  for(const auto& e:rally.entities()) if(e.kind==Kind::Resource&&!rally.explored(0,e.pos)) {hiddenResource=e.id;break;}
  check(hiddenResource!=0,"rally fixture has undiscovered ore");
  check(send(rally,CommandType::Rally,0,{hq},rally.find(hiddenResource)->pos).accepted,"rally into fog is permitted");
  check(send(rally,CommandType::Train,0,{hq},{},0,Kind::Worker).accepted,"worker production with fog rally accepted");
  advance(rally,definition(Kind::Worker).buildTime+0.1f);
  const auto trained=ids(rally,0,Kind::Worker).back();
  const auto* newWorker=rally.find(trained);
  if(newWorker->order==Order::Gather) {
    const auto* target=rally.find(newWorker->resourceTarget);
    check(target&&rally.explored(0,target->pos),"new worker cannot auto-target undiscovered resource");
  }
}

void movementAndGroups() {
  for(int map=0;map<3;++map) {
    auto s=quiet(map); check(!s.obstacles().empty(),"map has path obstacles");
    const auto obstacle=s.obstacles().front();
    const float x=obstacle.center.x, y=obstacle.center.y;
    Vec2 start{x-obstacle.half.x-100,y}, goal{x+obstacle.half.x+150,y};
    auto unit=s.debugSpawn(Kind::Striker,0,start);
    check(send(s,CommandType::Move,0,{unit},goal).accepted,"move across obstacle accepted");
    for(int n=0;n<1600;++n) {
      s.update(Simulation::Step);
      const auto* e=s.find(unit); check(e&&e->alive(),"pathing fixture alive");
      check(!(std::abs(e->pos.x-x)<obstacle.half.x-1&&std::abs(e->pos.y-y)<obstacle.half.y-1),"unit never crosses solid obstacle");
    }
    check(distance(s.find(unit)->pos,goal)<100,"ground unit reaches destination around obstacle");
  }
  auto s=quiet(); std::vector<Id> group;
  for(int i=0;i<15;++i) group.push_back(s.debugSpawn(Kind::Striker,0,{700.f+(i%5)*38,1100.f+(i/5)*38}));
  check(send(s,CommandType::Move,0,group,{1300,1250}).accepted,"group move accepted");
  advance(s,25);
  float mean=0,minSeparation=10000;
  for(auto a:group) {
    mean+=distance(s.find(a)->pos,{1300,1250});
    for(auto b:group) if(a!=b) minSeparation=std::min(minSeparation,distance(s.find(a)->pos,s.find(b)->pos));
  }
  check(mean/group.size()<160,"group arrives within formation radius");
  const float diameter=2*definition(Kind::Striker).radius;
  std::cout<<"GROUP_MOVE units=15 mean_destination_distance="<<mean/group.size()
           <<" min_separation="<<minSeparation<<" diameter="<<diameter
           <<" clearance_ratio="<<minSeparation/diameter<<'\n';
  check(minSeparation>=diameter*0.75f,"resting group maintains at least 75 percent diameter clearance");
}

void airAndCombat() {
  auto s=quiet(); Id ground=s.debugSpawn(Kind::Bastion,0,{1050,1700});
  Id air=s.debugSpawn(Kind::Kite,1,{1100,1700}); advance(s,0.1f);
  check(!definition(Kind::Bastion).antiAir&&definition(Kind::Kite).air,"counter fixture roles");
  const float hp=s.find(air)->hp;
  send(s,CommandType::Attack,0,{ground},{},air); advance(s,2);
  check(s.find(air)&&s.find(air)->hp==hp,"ground-only weapon cannot damage air target");
  Id aa=s.debugSpawn(Kind::Lancer,0,{1100,1800}); advance(s,0.1f);
  check(definition(Kind::Lancer).antiAir,"Lancer is anti-air fixture");
  check(send(s,CommandType::Attack,0,{aa},{},air).accepted,"anti-air attack accepted");
  advance(s,3);
  check(!s.find(air)||s.find(air)->hp<hp,"anti-air unit damages airborne opponent");
  check(s.players()[0].stats.damage>0,"combat damage tracked");
  auto armor=quiet(); Id attacker=armor.debugSpawn(Kind::Striker,0,{1000,1700});
  Id victim=armor.debugSpawn(Kind::Bastion,1,{1050,1700}); advance(armor,0.1f);
  const float health=armor.find(victim)->hp;
  send(armor,CommandType::Attack,0,{attacker},{},victim); advance(armor,1);
  check(armor.find(victim)->hp<health,"ground combat damages armored target");
  auto chase=quiet();attacker=chase.debugSpawn(Kind::Striker,0,{800,1400});
  victim=chase.debugSpawn(Kind::Foundry,1,{1450,1400});
  chase.debugSpawn(Kind::Scout,0,{1450,1650});
  check(distance(chase.find(attacker)->pos,chase.find(victim)->pos)>definition(Kind::Striker).vision,"shared vision fixture exceeds attacker vision");
  check(send(chase,CommandType::Attack,0,{attacker},{},victim).accepted,"shared scout vision authorizes explicit attack");
  chase.update(Simulation::Step);
  check(chase.find(attacker)->order==Order::Attack&&chase.find(attacker)->target==victim,"explicit chase retains shared-visible distant target");
  advance(chase,6);
  check(chase.find(victim)->hp<definition(Kind::Foundry).hp,"explicit chase closes distance and fires");
}

void tacticalOrders() {
  // The target is already in range across a thin wall. Attack must find a real
  // firing position instead of stopping because range alone is satisfied.
  for(CommandType order:{CommandType::Attack,CommandType::AttackMove}) {
    auto s=quiet(2);const Id attacker=s.debugSpawn(Kind::Lancer,0,{1600,1950});
    const Id victim=s.debugSpawn(Kind::Worker,1,{1600,2250});
    send(s,CommandType::Hold,1,{victim});
    check(send(s,order,0,{attacker},{1600,2250},victim).accepted,"attack across cover accepted");
    advance(s,0.5f);
    check(s.find(victim)->hp==definition(Kind::Worker).hp,"cover blocks the initial ground shot");
    advance(s,44.5f);
    check(!s.find(victim)||s.find(victim)->hp<definition(Kind::Worker).hp,"attacker routes around cover and fires");
    std::cout<<"COVER_ATTACK order="<<static_cast<int>(order)<<" damage="<<s.players()[0].stats.damage<<'\n';
  }
  auto held=quiet();const Vec2 anchor{1200,1300};
  const Id guard=held.debugSpawn(Kind::Striker,0,anchor);
  check(send(held,CommandType::Hold,0,{guard}).accepted,"hold position accepted");
  std::vector<Id> traffic;
  for(int i=0;i<80;++i)traffic.push_back(held.debugSpawn(Kind::Striker,0,{800.f+(i%10)*42,900.f+(i/10)*42}));
  check(send(held,CommandType::Move,0,traffic,{1400,1600}).accepted,"friendly traffic crosses held position");
  advance(held,30);
  check(held.find(guard)->order==Order::Hold&&distance(held.find(guard)->pos,anchor)<=10,"held unit retains its ordered position after traffic");
  std::cout<<"HOLD_TRAFFIC displacement="<<distance(held.find(guard)->pos,anchor)<<'\n';

  for(CommandType order:{CommandType::Attack,CommandType::AttackMove}) {
    auto s=quiet();const Id soldier=s.debugSpawn(Kind::Bastion,0,{800,1600});
    const Id harasser=s.debugSpawn(Kind::Lancer,1,{1030,1600});
    check(send(s,CommandType::Attack,1,{harasser},{},soldier).accepted,"support fixture suffers a real attack");
    s.update(Simulation::Step);const float wounded=s.find(soldier)->hp;
    check(wounded<definition(Kind::Bastion).hp,"support fixture has real combat damage");
    send(s,CommandType::Move,1,{harasser},{4100,100});
    const Id support=s.debugSpawn(Kind::Mender,0,{800,1700});
    const Id target=s.debugSpawn(Kind::Foundry,1,{1800,1600});
    s.debugSpawn(Kind::Scout,0,{1700,1900});
    check(!send(s,CommandType::Attack,0,{support},{},target).accepted,"support alone cannot attack enemies");
    check(send(s,order,0,{soldier,support},{1800,1600},target).accepted,"mixed army attack order accepted");
    const Vec2 acceptedSupportGoal=s.find(support)->goal;
    // Observe support while its order is still traveling. A correct shorter
    // route can finish AttackMove before the former fixed ten-second sample.
    waitUntil(s,10,[&]{return s.find(support)->pos.x>1100;},"support advances with attacking army");
    const auto* mend=s.find(support);const auto* ally=s.find(soldier);
    check(mend&&ally&&mend->pos.x>1100,"support advances with attacking army");
    check(distance(mend->pos,ally->pos)<=definition(Kind::Mender).range,"support remains within healing range of attack leader");
    check(ally->hp>wounded,"following support heals actual combat damage");
    const bool directFollow=order==CommandType::Attack&&mend->order==Order::Attack&&mend->target==soldier&&mend->supportTarget==0;
    const bool waypointSupport=order==CommandType::AttackMove&&mend->order==Order::AttackMove&&mend->target==0&&mend->supportTarget==soldier;
    check(definition(Kind::Mender).damage==0&&(directFollow||waypointSupport),
          "support uses direct follow for Attack and a retained waypoint relation for Attack-move");
    std::cout<<"SUPPORT_ORDER order="<<static_cast<int>(order)<<" distance="<<distance(mend->pos,ally->pos)<<" healed="<<ally->hp-wounded<<'\n';
    if(order==CommandType::AttackMove) {
      waitUntil(s,20,[&]{
        const auto* traveling=s.find(support);
        check(distance(traveling->goal,acceptedSupportGoal)<0.001f,"support retains its accepted attack-move waypoint");
        if(traveling->order==Order::AttackMove)
          check(traveling->target==0&&traveling->supportTarget==soldier,"traveling support retains its selected leader");
        return traveling->order!=Order::AttackMove;
      },"support completes its accepted attack-move waypoint");
      mend=s.find(support);
      check(mend->order==Order::Idle&&mend->target==0&&mend->supportTarget==0&&
            distance(mend->goal,acceptedSupportGoal)<0.001f&&distance(mend->pos,acceptedSupportGoal)<20,
            "completed attack-move support reaches its own waypoint and clears the finished relation");
    } else {
      advance(s,10);
      mend=s.find(support);ally=s.find(soldier);
      check(mend->order==Order::Attack&&mend->target==soldier&&mend->supportTarget==0&&
            distance(mend->pos,ally->pos)<=definition(Kind::Mender).range,
            "direct attack support keeps following its leader after the initial advance");
    }
  }
}

void victoryAndDefeat() {
  for(int winningTeam=0;winningTeam<2;++winningTeam) {
    auto s=quiet(); int loser=1-winningTeam; Id hq=first(s,loser,Kind::Headquarters);
    Vec2 center=s.find(hq)->pos;
    std::vector<Id> army;
    for(int i=0;i<24;++i) {
      float angle=6.2831853f*i/24;
      army.push_back(s.debugSpawn(Kind::Mortar,winningTeam,{center.x+300*std::cos(angle),center.y+300*std::sin(angle)}));
    }
    advance(s,0.1f); check(send(s,CommandType::Attack,winningTeam,army,{},hq).accepted,"base attack accepted");
    for(int i=0;i<5000&&s.winner()<0;++i) s.update(Simulation::Step);
    check(s.winner()==winningTeam,"destroying opposing headquarters ends match");
    check(!s.find(hq)||!s.find(hq)->alive(),"result requires actual HQ destruction");
    check(s.players()[winningTeam].stats.buildingsDestroyed>0,"destroyed HQ counted");
    const auto tick=s.tick();advance(s,1);
    check(s.tick()==tick,"finished match simulation freezes");
  }
}

void saveLoadAndReplay() {
  auto s=quiet(); auto w=first(s,0,Kind::Worker);
  send(s,CommandType::Move,0,{w},{1000,1100}); advance(s,3.35f);
  send(s,CommandType::Train,0,{first(s,0,Kind::Headquarters)},{},0,Kind::Worker);
  s.update(0.013f);
  const auto path=savePath("continuity"); check(s.save(path),"save succeeds");
  Simulation loaded; check(loaded.load(path),"load succeeds");
  check(s.stateHash()==loaded.stateHash(),"save-load preserves deterministic state hash");
  check(s.recording().size()==loaded.recording().size(),"save-load preserves command recording");
  for(int i=0;i<500;++i) {
    s.update(0.037f);loaded.update(0.037f);
    check(s.stateHash()==loaded.stateHash(),"loaded simulation diverges from uninterrupted run");
  }
  std::filesystem::remove(path);
  const auto corrupt=savePath("corrupt");
  {std::ofstream file(corrupt);file<<"not a Cinderline save\n";}
  auto before=loaded.stateHash();check(!loaded.load(corrupt),"invalid save rejected");
  check(loaded.stateHash()==before,"failed load preserves current match");std::filesystem::remove(corrupt);
  auto original=quiet();w=first(original,0,Kind::Worker);
  send(original,CommandType::Move,0,{w},{950,1000}); advance(original,4);
  send(original,CommandType::Hold,0,{w}); advance(original,2);
  const auto recording=original.recording();Simulation replay;replay.reset({0,42,false,1,MatchLength::Standard,2,0});
  std::size_t index=0;
  while(replay.tick()<original.tick()) {
    while(index<recording.size()&&recording[index].tick==replay.tick()) {
      check(replay.command(recording[index].command).accepted,"recorded command remains valid on replay");++index;
    }
    replay.update(Simulation::Step);
  }
  check(index==recording.size(),"replayed every command");
  check(replay.stateHash()==original.stateHash(),"tick-indexed command replay is deterministic");
  Simulation ai;ai.reset({2,73,true,1,MatchLength::Standard,2,0});advance(ai,75);
  const auto aiPath=savePath("ai-continuity");check(ai.save(aiPath),"AI match save succeeds");
  Simulation resumed;check(resumed.load(aiPath),"AI match load succeeds");
  for(int i=0;i<250;++i) {
    ai.update(Simulation::Step);resumed.update(Simulation::Step);
    check(ai.stateHash()==resumed.stateHash(),"AI timers and decisions continue identically after load");
  }
  std::filesystem::remove(aiPath);
}

void constructionPersistence() {
  auto s=quiet();const Id worker=first(s,0,Kind::Worker),node=beginCarryingOre(s,worker);
  const Vec2 site=distantPlacement(s,0,Kind::Foundry,worker);
  check(send(s,CommandType::Build,0,{worker},site,0,Kind::Foundry).accepted,"save fixture begins a worker construction order");
  const Id building=first(s,0,Kind::Foundry);
  auto roundTrip=[&](const std::string& phase,float duration) {
    const auto path=savePath("construction-"+phase);check(s.save(path),phase+" construction saves");
    Simulation uninterrupted=s,loaded;check(loaded.load(path),phase+" construction loads");
    check(loaded.stateHash()==s.stateHash(),phase+" construction retains all saved state");
    for(int n=0;n<static_cast<int>(duration/Simulation::Step);++n) {
      uninterrupted.update(Simulation::Step);loaded.update(Simulation::Step);
      check(loaded.stateHash()==uninterrupted.stateHash(),phase+" construction diverges after loading");
    }
    std::filesystem::remove(path);return loaded;
  };
  const auto travelling=roundTrip("travelling",definition(Kind::Foundry).buildTime+35);
  check(travelling.find(building)->progress==1&&travelling.find(worker)->order==Order::Gather&&travelling.find(worker)->resourceTarget==node,
        "loading an en-route builder still completes and resumes its mining task");
  waitForConstruction(s,building);advance(s,3);
  check(roundTrip("working",5).find(building)->progress>s.find(building)->progress,"loaded on-site worker continues construction");
  auto rejectBrokenPair=[&](Id entity,std::size_t column,const std::string& replacement,const std::string& failure) {
    const auto path=savePath("construction-invalid-pair");check(s.save(path),"invalid pair fixture starts from a valid working save");
    std::ifstream input(path);std::vector<std::string> lines;std::string line;bool changed=false;
    while(std::getline(input,line)) {
      std::istringstream fields(line);std::vector<std::string> values;std::string value;
      while(fields>>value)values.push_back(value);
      // Version 2 appends the assignment and return-to-mining flag to each
      // 22-field legacy entity record. Alter one relationship in a real save.
      if(!changed&&values.size()==24&&values.front()==std::to_string(entity)) {
        values[column]=replacement;std::ostringstream edited;
        for(std::size_t i=0;i<values.size();++i)edited<<(i?" ":"")<<values[i];line=edited.str();changed=true;
      }
      lines.push_back(line);
    }
    input.close();check(changed,"invalid pair fixture locates the intended entity record");
    {std::ofstream output(path);for(const auto& savedLine:lines)output<<savedLine<<'\n';}
    Simulation current=quiet();const auto before=current.stateHash();
    check(!current.load(path),failure);check(current.stateHash()==before,"rejected construction save leaves the current match intact");
    std::filesystem::remove(path);
  };
  rejectBrokenPair(building,22,"999999","save with a nonexistent constructor is rejected");
  rejectBrokenPair(building,22,std::to_string(first(s,0,Kind::Headquarters)),"save assigning a building as constructor is rejected");
  rejectBrokenPair(worker,17,"999999","save whose constructor targets another site is rejected");
  check(send(s,CommandType::Stop,0,{worker}).accepted,"save fixture pauses its builder");
  const float pausedProgress=s.find(building)->progress;
  const auto paused=roundTrip("paused",definition(Kind::Foundry).buildTime+1);
  check(paused.find(building)->progress==pausedProgress&&paused.constructionWorker(building)==0,"paused foundation remains paused through loading and a full build duration");
  check(send(s,CommandType::ResumeConstruction,0,{worker},{},building).accepted,"resumed construction is recorded as a command");
  advance(s,4);const auto commands=s.recording();Simulation replay;replay.reset({0,42,false,1,MatchLength::Standard,2,0});std::size_t next=0;
  while(replay.tick()<s.tick()) {
    while(next<commands.size()&&commands[next].tick==replay.tick()) {
      check(replay.command(commands[next].command).accepted,"construction replay accepts each historical command");++next;
    }
    replay.update(Simulation::Step);
  }
  check(next==commands.size()&&replay.stateHash()==s.stateHash(),"build, interruption, and free resume replay deterministically");

  // A fixed v1 fixture represents the old autonomous construction behavior:
  // the paid Kiln is 40% finished while its worker is still gathering 9 ore.
  // It deliberately omits every v2 assignment field, rather than relying on
  // the current writer to produce a supposedly legacy snapshot.
  const auto legacyPath=savePath("construction-v1");
  {
    std::ofstream legacy(legacyPath);
    legacy<<"CINDERLINE 1\n0 42 0 1\n20 6 0 0 -1\n"
          <<"250 1 0 0 0 0 0 0 0 0 0 0 0\n500 1 0 0 0 0 0 0 0 0 0 0 0\n0\n5\n"
          <<"1 8 0 600 600 600 600 790 600 3400 0 1 0 0 0 0 0 0 0 0 0 0\n0\n0\n"
          <<"2 0 0 800 700 700 850 800 700 70 0 1 9 0.2 0 0 4 5 5 0 0 0\n0\n0\n"
          <<"3 10 0 1000 700 1000 700 1190 700 713 0 0.4 0 0 0 0 0 0 0 0 0 0\n0\n0\n"
          <<"4 8 1 4200 4200 4200 4200 4010 4200 3400 0 1 0 0 0 0 0 0 0 0 0 0\n0\n0\n"
          <<"5 14 -1 700 850 700 850 700 850 1 0 1 0 0 2500 0 0 0 0 0 0 0\n0\n0\n0\n";
    for(int field=0;field<4;++field) {for(int cell=0;cell<Simulation::FogSize*Simulation::FogSize;++cell)legacy<<"1 ";legacy<<'\n';}
    legacy<<"0\n\"Legacy partially built Kiln\"\n\"Opponent AI disabled\"\n";
  }
  Simulation migrated;check(migrated.load(legacyPath),"legacy v1 construction save remains readable");
  check(migrated.find(3)->progress==0.4f&&migrated.constructionWorker(3)==0,"legacy unfinished structure migrates to a paused paid foundation");
  check(migrated.players()[0].ore==250&&migrated.find(2)->carried==9,"legacy migration preserves currency and carried ore");
  advance(migrated,2);
  check(migrated.find(3)->progress==0.4f,"legacy autonomous timer cannot continue after migration");
  const int legacyOre=migrated.players()[0].ore;
  check(send(migrated,CommandType::ResumeConstruction,0,{2},{},3).accepted,"legacy paid foundation accepts a real worker");
  waitForConstruction(migrated,3);advance(migrated,1);
  check(migrated.find(3)->progress>0.4f&&migrated.players()[0].ore==legacyOre,"legacy foundation resumes remaining work without another construction cost");
  std::filesystem::remove(legacyPath);
}

void boundedUpdate() {
  auto s=quiet(); const auto tick=s.tick();
  s.update(-1);s.update(std::numeric_limits<float>::quiet_NaN());s.update(std::numeric_limits<float>::infinity());
  check(s.tick()==tick,"negative and nonfinite updates ignored");
  s.update(1000000);
  check(s.tick()>tick&&s.tick()-tick<=20,"one update has bounded catch-up work");
  for(const auto& e:s.entities()) check(std::isfinite(e.pos.x)&&std::isfinite(e.pos.y),"bounded update preserves finite positions");
}

void aiExpansionAndBaseDefense() {
  Simulation economy;economy.reset({0,123,true,1,MatchLength::Standard,2,0});
  const Vec2 depletedSite{2900,3800},richSite{3800,2000};
  std::vector<Id> deposits;
  for(const auto& e:economy.entities())if(e.kind==Kind::Resource&&distance(e.pos,depletedSite)<350)deposits.push_back(e.id);
  check(deposits.size()==3,"expansion fixture identifies three real ore deposits");
  economy.debugSpawn(Kind::Processor,0,{2900,3500});
  for(int i=0;i<30;++i) {
    const Id worker=economy.debugSpawn(Kind::Worker,0,{2760.f+(i%10)*32,3610.f+(i/10)*36});
    check(send(economy,CommandType::Gather,0,{worker},{},deposits[i%3]).accepted,"depletion fixture uses ordinary gather commands");
  }
  // Zero the opponent's development-fixture funds while its clock advances;
  // ore removal itself must happen through workers harvesting and delivering.
  for(int n=0;n<8000;++n){economy.debugResources(1,0);economy.update(Simulation::Step);}
  float remaining=0;for(Id id:deposits)remaining+=economy.find(id)->resource;
  check(remaining==0,"first expansion is actually depleted by workers before AI chooses");
  check(economy.players()[0].stats.gathered>=12000,"depletion produced delivered ore rather than edited resource state");
  // Satisfy Normal's production plan so this fixture isolates site choice.
  economy.debugSpawn(Kind::Foundry,1,{3900,4300});
  economy.debugSpawn(Kind::Foundry,1,{3850,4650});
  economy.debugSpawn(Kind::Foundry,1,{4550,4050});
  economy.debugSpawn(Kind::Processor,1,{4300,3900});
  economy.debugSpawn(Kind::Laboratory,1,{4450,4400});
  economy.debugSpawn(Kind::Turret,1,{3900,3950});
  const Id surveyor=economy.debugSpawn(Kind::Scout,1,{2900,3320});
  send(economy,CommandType::Hold,1,{surveyor});
  economy.debugSpawn(Kind::Worker,1,{3600,2150});
  economy.debugResources(1,5000);const std::size_t commandStart=economy.recording().size();
  advance(economy,6);
  bool expanded=false;
  for(std::size_t i=commandStart;i<economy.recording().size();++i) {
    const auto& c=economy.recording()[i].command;
    if(c.team!=1||c.type!=CommandType::Build||c.kind!=Kind::Headquarters)continue;
    expanded=true;
    check(distance(c.point,richSite)<850,"AI expands beside the visible rich cluster");
    check(distance(c.point,depletedSite)>850,"AI does not spend on the depleted first expansion");
  }
  check(expanded,"AI issues a paid expansion command at the viable alternate site");
  std::cout<<"AI_EXPANSION depleted_ore="<<remaining<<" gathered="<<economy.players()[0].stats.gathered<<" alternate_built="<<expanded<<'\n';

  Simulation defense;defense.reset({0,321,true,1,MatchLength::Standard,2,0});
  for(int n=0;n<2020;++n){defense.debugResources(1,0);defense.update(Simulation::Step);}
  defense.debugSpawn(Kind::Foundry,1,{3900,4300});
  defense.debugSpawn(Kind::Processor,1,{4300,3900});
  defense.debugSpawn(Kind::Laboratory,1,{4450,4400});
  const Id oldTurret=defense.debugSpawn(Kind::Turret,1,{3900,4200});
  const Vec2 remoteBase{2200,4200};
  defense.debugSpawn(Kind::Headquarters,1,remoteBase);
  defense.debugSpawn(Kind::Scout,0,{2200,3750}); // A real observed threat makes defense urgent.
  check(distance(defense.find(oldTurret)->pos,remoteBase)>900,"old turret cannot defend the second base");
  defense.debugResources(1,1000);const std::size_t defenseStart=defense.recording().size();
  advance(defense,20);bool defended=false,builderDispatched=false;
  for(std::size_t i=defenseStart;i<defense.recording().size();++i) {
    const auto& c=defense.recording()[i].command;
    if(c.team==1&&c.type==CommandType::Build&&c.kind==Kind::Turret&&distance(c.point,remoteBase)<=900)defended=true;
    if(c.team==1&&c.type==CommandType::Move&&distance(c.point,remoteBase)<150&&!c.units.empty()) {
      const auto* builder=defense.find(c.units.front());
      if(builder&&builder->kind==Kind::Worker)builderDispatched=true;
    }
  }
  check(builderDispatched,"AI dispatches a distant worker to construct remote defense");
  check(defended,"AI buys local defense despite an old surviving turret elsewhere");
  check(defense.find(oldTurret)&&defense.find(oldTurret)->alive(),"local defense is added while original turret survives");
  std::cout<<"AI_BASE_DEFENSE old_turret_alive=1 worker_dispatched="<<builderDispatched<<" remote_turret_built="<<defended<<'\n';
}

void aiArmyAndSupportCoordination() {
  Simulation waiting;waiting.reset({0,123,true,1,MatchLength::Standard,2,0});
  for(int n=0;n<7160;++n){waiting.debugResources(1,0);waiting.update(Simulation::Step);}
  check(waiting.time()>=358&&ids(waiting,1,Kind::Scout).empty(),"army cadence fixture has no scout after350 seconds");
  std::vector<Id> army;
  for(int i=0;i<7;++i)army.push_back(waiting.debugSpawn(Kind::Striker,1,{3800.f+(i%4)*70,4200.f+(i/4)*70}));
  for(bool withScout:{false,true}) {
    Simulation advancing=waiting;Id scout=0;
    if(withScout)scout=advancing.debugSpawn(Kind::Scout,1,{4500,4500});
    const auto firstCommand=advancing.recording().size();advance(advancing,4);
    bool attackRecorded=false;
    for(std::size_t i=firstCommand;i<advancing.recording().size();++i) {
      const auto& c=advancing.recording()[i].command;
      if(c.team!=1||c.type!=CommandType::AttackMove)continue;
      attackRecorded=true;
      for(Id soldier:army)check(std::find(c.units.begin(),c.units.end(),soldier)!=c.units.end(),"strategic attack includes the ready army");
      if(scout)check(std::find(c.units.begin(),c.units.end(),scout)==c.units.end(),"expansion scout remains outside advancing army");
    }
    check(attackRecorded,"expansion scouting does not suppress the strategic attack cadence");
    if(scout)check(advancing.find(scout)->order==Order::Move,"assigned expansion scout continues its scouting route");
    std::cout<<"AI_SCOUT_AND_ARMY scout="<<withScout<<" attack_move="<<attackRecorded<<'\n';
  }

  Simulation joining;joining.reset({0,456,true,1,MatchLength::Standard,2,0});joining.debugResources(1,0);
  const Id target=joining.debugSpawn(Kind::Foundry,0,{3300,4200});
  const Id firstSoldier=joining.debugSpawn(Kind::Striker,1,{3570,4200});
  const Id secondSoldier=joining.debugSpawn(Kind::Lancer,1,{3600,4300});
  check(send(joining,CommandType::Attack,1,{firstSoldier,secondSoldier},{},target).accepted,"armed units already attack the chosen enemy");
  const Id newSupport=joining.debugSpawn(Kind::Mender,1,{3770,4280});
  joining.update(Simulation::Step);
  check(joining.find(newSupport)->order==Order::Escort&&
        (joining.find(newSupport)->sustained.escortTarget==firstSoldier||
         joining.find(newSupport)->sustained.escortTarget==secondSoldier),"new Mender escorts an attack already underway");
  const auto settledCommand=joining.recording().size();advance(joining,4.2f);int redundant=0;
  for(std::size_t i=settledCommand;i<joining.recording().size();++i) {
    const auto& c=joining.recording()[i].command;
    if(c.team==1&&c.type==CommandType::Attack&&c.target==target)++redundant;
  }
  check(redundant==0,"valid support follow does not reset the armed leader every AI tick");

  Simulation replacement;replacement.reset({0,789,true,1,MatchLength::Standard,2,0});replacement.debugResources(1,0);
  const Id enemy=replacement.debugSpawn(Kind::Bastion,0,{3340,4200});
  replacement.debugSpawn(Kind::Mender,0,{3105,4200});
  replacement.debugSpawn(Kind::Mender,0,{3120,4100});
  const Id doomed=replacement.debugSpawn(Kind::Striker,1,{3590,4200});
  const Id survivor=replacement.debugSpawn(Kind::Lancer,1,{3650,4300});
  const Id follower=replacement.debugSpawn(Kind::Mender,1,{3790,4230});
  check(send(replacement,CommandType::Attack,1,{doomed,survivor,follower},{},enemy).accepted,"support initially follows its first armed leader");
  check(replacement.find(follower)->target==doomed,"leader-death fixture begins with the intended follow target");
  std::size_t handoffStart=replacement.recording().size();
  for(int n=0;n<1800&&replacement.find(doomed)&&replacement.find(doomed)->alive();++n) {
    // Keep this unit fighting through the opponent's retreat recommendation;
    // its eventual death must come from ordinary combat, not edited health.
    send(replacement,CommandType::Attack,1,{doomed,follower},{},enemy);
    handoffStart=replacement.recording().size();replacement.debugResources(1,0);replacement.update(Simulation::Step);
  }
  check(!replacement.find(doomed)||!replacement.find(doomed)->alive(),"support leader actually dies in combat");
  check(replacement.find(survivor)&&replacement.find(survivor)->alive(),"another armed leader remains available");
  advance(replacement,3);bool reassigned=false;
  for(std::size_t i=handoffStart;i<replacement.recording().size();++i) {
    const auto& c=replacement.recording()[i].command;
    if(c.team==1&&c.type==CommandType::Escort&&c.target==survivor&&
       std::find(c.units.begin(),c.units.end(),follower)!=c.units.end())reassigned=true;
  }
  check(reassigned&&replacement.find(follower)->sustained.escortTarget==survivor,"AI reassigns orphaned support with an existing eligible armed leader");
  std::cout<<"AI_SUPPORT joined_existing_attack=1 redundant_orders="<<redundant<<" dead_leader_replaced="<<reassigned<<'\n';
}

void aiEconomy() {
  aiArmyAndSupportCoordination();
  Simulation s;s.reset({0,123,true,1,MatchLength::Standard,2,0});
  for(int i=0;i<12000&&s.winner()<0;++i) s.update(Simulation::Step);
  const auto& p=s.players()[1];
  check(p.stats.gathered>0,"opponent gathers finite map resources");
  check(p.stats.produced>0&&p.stats.built>0,"opponent pays to build economy and army");
  check(p.ore>=0,"opponent never overdraws");
  int paid=0;std::size_t paidCommands=0;
  for(const auto& r:s.recording()) if(r.command.team==1) {
    if(r.command.type==CommandType::Build||r.command.type==CommandType::Train) {paid+=definition(r.command.kind).cost;++paidCommands;}
    if(r.command.type==CommandType::Research) ++paidCommands;
  }
  check(paidCommands>0&&paid>0,"AI economy passes through recorded paid commands");
  check(p.ore+paid<=500+p.stats.gathered,"AI has no hidden resource income");
  std::cout<<"AI_ECONOMY seconds="<<s.time()<<" ore="<<p.ore<<" gathered="<<p.stats.gathered
           <<" produced="<<p.stats.produced<<" built="<<p.stats.built<<" paid_command_cost="<<paid<<'\n';
}

void aiConstructionAssignments() {
  Simulation s;s.reset({0,987,true,1,MatchLength::Standard,2,0});
  const Id worker=first(s,1,Kind::Worker);const Vec2 site=distantPlacement(s,1,Kind::Foundry,worker);
  check(send(s,CommandType::Build,1,{worker},site,0,Kind::Foundry).accepted,"AI construction fixture places a paid foundation");
  const Id building=first(s,1,Kind::Foundry);s.debugResources(1,0);
  advance(s,0.1f);
  check(s.constructionWorker(building)==worker&&s.find(worker)->order==Order::Construct,"AI does not replace or harvest an assigned worker still travelling to its site");
  waitForConstruction(s,building);advance(s,3);
  check(s.constructionWorker(building)==worker&&s.find(building)->progress>0,"AI preserves the attending constructor across its economic decisions");
  check(send(s,CommandType::Stop,1,{worker}).accepted,"AI fixture leaves an already paid orphan foundation");
  const auto commandStart=s.recording().size();s.debugResources(1,0);advance(s,2.1f);
  bool resumed=false;
  for(std::size_t i=commandStart;i<s.recording().size();++i) {
    const auto& c=s.recording()[i].command;
    if(c.team==1&&c.type==CommandType::ResumeConstruction&&c.target==building)resumed=true;
  }
  const Id replacement=s.constructionWorker(building);
  check(resumed&&replacement!=0&&s.find(replacement)->order==Order::Construct,"AI recovers an orphan through ResumeConstruction without overwriting it with a mining order in the same update");

  Simulation reserved;reserved.reset({0,654,true,1,MatchLength::Standard,2,0});reserved.debugResources(1,5000);
  const auto workers=ids(reserved,1,Kind::Worker);std::vector<Id> sites;
  for(Id id:workers) {
    const Vec2 point=distantPlacement(reserved,1,Kind::Processor,id);
    check(send(reserved,CommandType::Build,1,{id},point,0,Kind::Processor).accepted,"AI reservation fixture assigns each worker a different legal site");
    sites.push_back(ids(reserved,1,Kind::Processor).back());
  }
  const Vec2 extra=distantPlacement(reserved,1,Kind::Processor,workers.front());
  check(send(reserved,CommandType::Build,1,{workers.front()},extra,0,Kind::Processor).accepted,"retasking one reserved worker creates an orphan without adding a free worker");
  check(reserved.constructionWorker(sites.front())==0,"reservation fixture has exactly an abandoned earlier site");
  const auto start=reserved.recording().size();advance(reserved,0.1f);
  check(reserved.constructionWorker(sites.front())==0,"AI leaves an orphan paused when every surviving worker is already constructing");
  for(std::size_t i=start;i<reserved.recording().size();++i) {
    const auto& c=reserved.recording()[i].command;
    check(c.team!=1||(c.type!=CommandType::Build&&c.type!=CommandType::ResumeConstruction),"AI cannot steal busy constructors or buy duplicate infrastructure while paid work waits");
  }
  for(Id id:workers)check(reserved.find(id)->order==Order::Construct,"reserved worker remains on its current construction task");
  std::cout<<"AI_CONSTRUCTION travelling_preserved=1 orphan_resumed="<<resumed<<" all_busy_orphan_paused=1\n";
}

// The following fixtures place actors to control what can be seen, but all
// scouting movement, destruction, construction and purchases use normal commands.
const AISighting* remembered(const Simulation& s,Id id) {
  const auto& knowledge=s.aiSightings();
  const auto found=std::find_if(knowledge.begin(),knowledge.end(),[&](const AISighting& sighting){return sighting.id==id;});
  return found==knowledge.end()?nullptr:&*found;
}
void advanceWithoutAIFunds(Simulation& s,float seconds) {
  for(int n=0;n<static_cast<int>(std::ceil(seconds/Simulation::Step));++n) {
    s.debugResources(1,0);s.update(Simulation::Step);
  }
}
std::vector<Kind> aiTrainingSince(const Simulation& s,std::size_t start) {
  std::vector<Kind> result;
  for(std::size_t i=start;i<s.recording().size();++i) {
    const auto& c=s.recording()[i].command;
    if(c.team==1&&c.type==CommandType::Train&&c.kind!=Kind::Worker)result.push_back(c.kind);
  }
  return result;
}

void aiObservationLifecycle() {
  Simulation s;s.reset({0,803,true,1,MatchLength::Standard,2,0});s.debugResources(1,0);
  const Id builder=s.debugSpawn(Kind::Worker,0,{1100,300});
  const Vec2 site=validPlacement(s,0,Kind::Processor,{1100,300});
  check(send(s,CommandType::Build,0,{builder},site,0,Kind::Processor).accepted,"observation fixture buys an ordinary foundation");
  const Id foundation=first(s,0,Kind::Processor);
  check(send(s,CommandType::Stop,0,{builder}).accepted,"observation fixture leaves its foundation unfinished");
  const Id mobile=s.debugSpawn(Kind::Mender,0,{site.x,site.y+160});
  check(s.aiSightings().empty()&&s.aiLastObserved(site)==0,"enemy fixtures do not create knowledge before actual opponent vision");
  const Id observer=s.debugSpawn(Kind::Mender,1,{site.x+350,site.y});
  advanceWithoutAIFunds(s,0.1f);
  check(remembered(s,foundation)&&remembered(s,mobile),"visible structures and mobile units become opponent sightings");
  check(remembered(s,foundation)->kind==Kind::Processor&&distance(remembered(s,foundation)->pos,site)<1,"sighting captures the observed kind and location");
  check(s.aiLastObserved(site)>0,"actually observed terrain records its observation time");
  check(send(s,CommandType::Move,1,{observer},{2800,300}).accepted,"observer receives an ordinary withdrawal order");
  advanceWithoutAIFunds(s,15);
  check(!s.visible(1,site)&&!s.visible(1,s.find(mobile)->pos),"observer leaves both enemy fixtures in fog");
  const auto lastSeen=remembered(s,foundation)->lastSeenTick;
  const Vec2 mobileLastPosition=remembered(s,mobile)->pos;
  check(send(s,CommandType::CancelBuilding,0,{foundation}).accepted,"owner cancels the unseen foundation through its ordinary command");
  check(send(s,CommandType::Move,0,{mobile},{700,300}).accepted,"unseen mobile unit receives an ordinary movement order");
  advanceWithoutAIFunds(s,8);
  check(remembered(s,foundation)&&remembered(s,foundation)->lastSeenTick==lastSeen,"hidden destruction cannot erase or refresh a remembered building");
  check(remembered(s,mobile)&&distance(remembered(s,mobile)->pos,mobileLastPosition)<1,"hidden movement cannot update a mobile sighting");
  advanceWithoutAIFunds(s,91);
  check(!remembered(s,mobile),"mobile knowledge expires after ninety seconds without another sighting");
  check(remembered(s,foundation)&&remembered(s,foundation)->lastSeenTick==lastSeen,"building memory survives long fog even after its hidden destruction");
  check(send(s,CommandType::Move,1,{observer},{site.x+350,site.y}).accepted,"observer returns to inspect the last known site");
  advanceWithoutAIFunds(s,15);
  check(s.visible(1,site)&&s.aiLastObserved(site)>lastSeen,"returning observer actually rechecks the remembered terrain");
  check(!remembered(s,foundation),"renewed vision of an empty site invalidates the remembered building");
  Simulation witnessed;witnessed.reset({0,807,true,1,MatchLength::Standard,2,0});
  const Id victim=witnessed.debugSpawn(Kind::Mender,0,{1200,300});
  witnessed.debugSpawn(Kind::Mender,1,{1500,300});advanceWithoutAIFunds(witnessed,1.0f);
  check(remembered(witnessed,victim),"visible-death fixture first establishes a real mobile sighting");
  const Id attacker=witnessed.debugSpawn(Kind::Bastion,1,{1430,300});
  check(send(witnessed,CommandType::Attack,1,{attacker},{},victim).accepted,"visible mobile death comes from ordinary combat");
  while(witnessed.find(victim)&&witnessed.find(victim)->alive()&&witnessed.tick()<120)advanceWithoutAIFunds(witnessed,Simulation::Step);
  std::cout<<"AI_WITNESSED_DEATH tick="<<witnessed.tick()<<" previous_decision_tick=80 cleanup_tick=100\n";
  check(witnessed.find(victim)&&!witnessed.find(victim)->alive()&&witnessed.tick()>80&&witnessed.tick()<100,"four real Bastion hits kill the known mobile between the tick80 decision and tick100 corpse cleanup");
  check(!remembered(witnessed,victim),"witnessed mobile death erases knowledge immediately before the next AI decision");
  while(witnessed.tick()<=100)advanceWithoutAIFunds(witnessed,Simulation::Step);
  check(!witnessed.find(victim)&&!remembered(witnessed,victim),"corpse cleanup cannot leave a dead mobile counter-production report");
  std::cout<<"AI_KNOWLEDGE hidden_death_retained=1 hidden_movement_ignored=1 mobile_expired=1 empty_site_cleared=1 visible_death_immediate=1\n";
}

Simulation aiProductionFixture(Kind enemyKind,bool reveal,int enemyCount=1,int existingStrikers=0) {
  Simulation s;s.reset({0,804,true,1,MatchLength::Standard,2,0});
  s.debugSpawn(Kind::Foundry,1,{3850,4450});
  s.debugSpawn(Kind::Foundry,1,{4150,4550});
  s.debugSpawn(Kind::Foundry,1,{4500,4150});
  s.debugSpawn(Kind::Processor,1,{4500,4500});
  s.debugSpawn(Kind::Scout,1,{4500,3850});
  for(int n=0;n<existingStrikers;++n)s.debugSpawn(Kind::Striker,1,{3900.f+n*55,3800});
  s.debugSpawn(Kind::Mender,1,reveal?Vec2{1530,300}:Vec2{2200,300});
  for(int n=0;n<enemyCount;++n)s.debugSpawn(enemyKind,0,{1100.f+n*60,300});
  s.debugResources(1,2000);
  return s;
}

void aiObservedProduction() {
  auto hiddenAir=aiProductionFixture(Kind::Kite,false);
  auto hiddenArmor=aiProductionFixture(Kind::Bastion,false);
  auto hiddenLight=aiProductionFixture(Kind::Lancer,false);
  hiddenAir.update(Simulation::Step);hiddenArmor.update(Simulation::Step);hiddenLight.update(Simulation::Step);
  check(hiddenAir.aiSightings().empty()&&hiddenArmor.aiSightings().empty()&&hiddenLight.aiSightings().empty(),"unseen army composition remains absent from opponent knowledge");
  const auto baseline=aiTrainingSince(hiddenAir,0);
  check(!baseline.empty()&&baseline==aiTrainingSince(hiddenArmor,0)&&baseline==aiTrainingSince(hiddenLight,0),"hidden air, armor and light infantry produce identical ordinary purchase decisions");
  for(Kind threat:{Kind::Kite,Kind::Bastion}) {
    auto seen=aiProductionFixture(threat,true);const int before=seen.players()[1].ore;
    seen.update(Simulation::Step);const auto trained=aiTrainingSince(seen,0);
    const int counters=static_cast<int>(std::count(trained.begin(),trained.end(),Kind::Lancer));
    check(counters==2&&std::count(trained.begin(),trained.end(),Kind::Striker)==1,"three producers coordinate exactly two Needles for one seen heavy or air threat, then return to baseline production");
    check(trained!=baseline,"actually sighted armor or air changes the paid composition");
    int paid=0;for(const auto& r:seen.recording())if(r.command.team==1&&(r.command.type==CommandType::Train||r.command.type==CommandType::Build))paid+=definition(r.command.kind).cost;
    check(before-seen.players()[1].ore==paid&&seen.players()[1].ore>=0,"counter composition spends ordinary costs without extra income");
  }
  auto light=aiProductionFixture(Kind::Lancer,true,3,3);
  auto blindLight=aiProductionFixture(Kind::Lancer,false,3,3);
  light.update(Simulation::Step);blindLight.update(Simulation::Step);
  const auto lightQueue=aiTrainingSince(light,0),blindQueue=aiTrainingSince(blindLight,0);
  check(!lightQueue.empty()&&lightQueue.front()==Kind::Striker&&!blindQueue.empty()&&blindQueue.front()==Kind::Lancer,"seeing vulnerable light infantry changes the next purchase from a ratio-balancing Needle to Ember infantry");

  auto recent=aiProductionFixture(Kind::Kite,true);recent.debugResources(1,0);
  recent.update(Simulation::Step);const Id observer=first(recent,1,Kind::Mender);
  check(send(recent,CommandType::Move,1,{observer},{2400,300}).accepted,"production observer leaves the sighted aircraft behind");
  advanceWithoutAIFunds(recent,12);
  check(!recent.visible(1,{1100,300})&&!recent.aiSightings().empty(),"counter fixture retains recent aircraft knowledge through fog");
  Simulation stale=recent;
  for(int n=0;n<1300;++n) {
    // Keep the existing reconnaissance unit at home so the aging fixture cannot
    // accidentally receive a fresh aircraft sighting while its clock advances.
    send(stale,CommandType::Hold,1,ids(stale,1,Kind::Scout));advanceWithoutAIFunds(stale,Simulation::Step);
  }
  check(!stale.aiSightings().empty()&&!stale.visible(1,{1100,300}),"sixty-five-second-old mobile report is retained but no longer fresh");
  const auto staleStart=stale.recording().size();stale.debugResources(1,2000);advance(stale,2.1f);
  const auto staleQueue=aiTrainingSince(stale,staleStart);
  check(!staleQueue.empty()&&staleQueue.front()==Kind::Striker,"stale mobile knowledge returns production to the balanced baseline before its ninety-second expiry");
  const auto rememberedStart=recent.recording().size();recent.debugResources(1,2000);advance(recent,2.1f);
  const auto rememberedQueue=aiTrainingSince(recent,rememberedStart);
  check(!rememberedQueue.empty()&&rememberedQueue.front()==Kind::Lancer,"recent remembered aircraft still drive counter production outside vision");

  Simulation scouts;scouts.reset({0,805,true,1,MatchLength::Standard,2,0});
  for(Vec2 p:{Vec2{3850,4450},Vec2{4150,4550},Vec2{4500,4150}})scouts.debugSpawn(Kind::Foundry,1,p);
  scouts.debugSpawn(Kind::Processor,1,{4500,4500});scouts.debugResources(1,2000);scouts.update(Simulation::Step);
  const auto scoutQueue=aiTrainingSince(scouts,0);
  check(std::count(scoutQueue.begin(),scoutQueue.end(),Kind::Scout)==1,"multiple producers share the scout reservation within one decision pass");
  std::cout<<"AI_COMPOSITION hidden_equal=1 observed_air_armor_counters=1 light_infantry_response=1 remembered_air_response=1 duplicate_scouts=0\n";
}

void aiScoutingObjectives() {
  Simulation s;s.reset({0,806,true,1,MatchLength::Standard,2,0});s.debugResources(1,0);
  send(s,CommandType::Stop,0,ids(s,0,Kind::Worker));
  const Vec2 original=s.find(first(s,0,Kind::Headquarters))->pos,remote{600,4200};
  const Id originalHQ=first(s,0,Kind::Headquarters),remoteHQ=s.debugSpawn(Kind::Headquarters,0,remote);
  std::vector<Id> army;
  for(int n=0;n<10;++n)army.push_back(s.debugSpawn(Kind::Kite,1,{800.f+(n%5)*45,450.f+(n/5)*55}));
  check(send(s,CommandType::Attack,1,army,{},originalHQ).accepted,"objective fixture attacks the original headquarters through ordinary combat");
  for(int n=0;n<1300&&s.find(originalHQ)&&s.find(originalHQ)->alive();++n)
    advanceWithoutAIFunds(s,Simulation::Step);
  check(!s.find(originalHQ)||!s.find(originalHQ)->alive(),"original headquarters actually falls in combat");
  check(s.winner()<0&&s.find(remoteHQ)->alive()&&!remembered(s,remoteHQ),"hidden relocated headquarters keeps the match alive without leaking its location");
  // Remove residual local defenders through attack-move before checking the
  // strategic fallback. A visible enemy legitimately takes tactical priority.
  check(send(s,CommandType::AttackMove,1,army,{800,760}).accepted,"army sweeps the original economy through ordinary attack-move");
  // Keep the cleanup sweep local; the improved AI otherwise immediately
  // continues reconnaissance and can finish the match before the memory checks.
  for(int n=0;n<800;++n) {
    send(s,CommandType::AttackMove,1,army,{800,760});
    advanceWithoutAIFunds(s,Simulation::Step);
  }
  check(send(s,CommandType::Move,1,army,{1300,600}).accepted,"army regroups after clearing the first site");
  advanceWithoutAIFunds(s,20);
  check(s.aiLastObserved(original)>0&&!remembered(s,originalHQ),"opponent has confirmed the original headquarters is gone");
  advanceWithoutAIFunds(s,std::max(0.0f,235.0f-s.time()));
  const auto searchStart=s.recording().size();bool searched=false;
  for(int n=0;n<520&&!searched;++n) {
    advanceWithoutAIFunds(s,Simulation::Step);
    for(std::size_t i=searchStart;i<s.recording().size();++i)if(s.recording()[i].command.team==1&&s.recording()[i].command.type==CommandType::AttackMove)searched=true;
  }
  for(std::size_t i=searchStart;i<s.recording().size();++i) {
    const auto& c=s.recording()[i].command;
    if(c.team==1&&c.type==CommandType::AttackMove) {
      searched=true;check(distance(c.point,original)>300,"strategic search does not attack a confirmed empty starting point repeatedly");
    }
  }
  check(searched,"an army searches a different unverified site after the original base is cleared");
  const Id observer=s.debugSpawn(Kind::Mender,1,{remote.x+440,remote.y});
  advanceWithoutAIFunds(s,2.1f);
  check(remembered(s,remoteHQ)&&remembered(s,remoteHQ)->kind==Kind::Headquarters,"renewed scouting discovers the relocated headquarters");
  check(send(s,CommandType::Move,1,{observer},{remote.x+1200,remote.y}).accepted,"observer withdraws after locating the new base");
  for(int n=0;n<280;++n) {
    send(s,CommandType::Move,1,army,{4200,4200});advanceWithoutAIFunds(s,Simulation::Step);
  }
  check(!s.visible(1,remote)&&remembered(s,remoteHQ),"new headquarters remains a known objective after vision ends");
  const auto knownStart=s.recording().size();advanceWithoutAIFunds(s,26);bool targeted=false;
  for(std::size_t i=knownStart;i<s.recording().size();++i) {
    const auto& c=s.recording()[i].command;
    if(c.team==1&&c.type==CommandType::AttackMove&&distance(c.point,remote)<150)targeted=true;
    if(c.team==1&&c.type==CommandType::Attack&&c.target==remoteHQ)targeted=true;
  }
  check(targeted,"the army redirects to the discovered relocated headquarters");
  std::cout<<"AI_OBJECTIVES original_destroyed=1 empty_start_avoided=1 relocated_hq_discovered=1 remembered_hq_targeted=1\n";
}

void aiKnowledgePersistence() {
  auto s=aiProductionFixture(Kind::Kite,true);s.debugResources(1,0);advanceWithoutAIFunds(s,2.1f);
  const auto path=savePath("knowledge-v3");check(s.save(path),"observed AI knowledge saves");
  Simulation loaded;check(loaded.load(path),"observed AI knowledge loads");
  check(s.stateHash()==loaded.stateHash()&&!loaded.aiSightings().empty(),"knowledge and observation ages participate in the saved deterministic state");
  s.debugResources(1,2000);loaded.debugResources(1,2000);
  for(int n=0;n<100;++n){s.update(Simulation::Step);loaded.update(Simulation::Step);check(s.stateHash()==loaded.stateHash(),"loaded knowledge drives identical paid decisions and simulation continuation");}
  std::ifstream in(path);std::vector<std::string> lines;std::string line;
  while(std::getline(in,line))lines.push_back(line);in.close();
  const auto marker=std::find(lines.begin(),lines.end(),"AI_KNOWLEDGE 1");
  check(marker!=lines.end()&&lines.front()=="CINDERLINE 15","current save retains player count, match length, AI knowledge, stable production identities, tactical order queues, sustained orders, formation orders, and queued work");
  const auto start=static_cast<std::size_t>(marker-lines.begin());
  const auto count=static_cast<std::size_t>(std::stoul(lines[start+1]));
  check(count>0&&lines.size()>start+count+3,"knowledge save contains sightings and observation cells");
  auto write=[&](const std::vector<std::string>& content){std::ofstream out(path);for(const auto& value:content)out<<value<<'\n';};
  auto reject=[&](std::vector<std::string> content,const std::string& message){
    write(content);Simulation current=quiet();const auto before=current.stateHash();
    check(!current.load(path),message);check(current.stateHash()==before,"invalid knowledge load preserves the active match atomically");
  };
  auto replaceField=[](std::string row,std::size_t column,const std::string& replacement){
    std::istringstream input(row);std::vector<std::string> fields;std::string value;while(input>>value)fields.push_back(value);
    check(column<fields.size(),"knowledge corruption fixture field exists");fields[column]=replacement;
    std::ostringstream output;for(std::size_t i=0;i<fields.size();++i)output<<(i?" ":"")<<fields[i];return output.str();
  };
  for(const auto& [column,value]:std::vector<std::pair<std::size_t,std::string>>{{0,"0"},{1,"14"},{2,"nan"},{2,"4801"},{4,std::to_string(s.tick()+1000)}}) {
    auto broken=lines;broken[start+2]=replaceField(broken[start+2],column,value);reject(broken,"invalid sighting identity, kind, coordinate or timestamp is rejected");
  }
  auto duplicate=lines;duplicate[start+1]=std::to_string(count+1);duplicate.insert(duplicate.begin()+start+2,lines[start+2]);reject(duplicate,"duplicate remembered identities are rejected");
  auto badCells=lines;badCells[start+2+count]="4095";reject(badCells,"incorrect observation-grid size is rejected");
  auto futureCell=lines;futureCell[start+3+count]=replaceField(futureCell[start+3+count],0,std::to_string(s.tick()+1000));reject(futureCell,"future observation-grid timestamps are rejected");
  auto truncated=lines;truncated.resize(start);reject(truncated,"current save without its required knowledge block is rejected");
  auto futureVersion=lines;futureVersion.front()="CINDERLINE 16";reject(futureVersion,"unsupported save version sixteen is rejected");
  for(int version:{1,2}) {
    const auto legacy=legacyCombatSave(lines,version);
    write(legacy);Simulation migrated;check(migrated.load(path),"pre-knowledge save version remains readable");
    check(migrated.aiSightings().empty()&&migrated.aiLastObserved({1100,300})==0,"legacy migration starts with unknown enemy state rather than reconstructing hidden entities");
    check(migrated.players()[1].ore==0&&migrated.tick()>0,"legacy knowledge migration preserves ordinary match state");
    advanceWithoutAIFunds(migrated,2.1f);check(!migrated.aiSightings().empty(),"legacy match learns only when ordinary vision updates resume");
  }
  std::filesystem::remove(path);
  std::cout<<"AI_KNOWLEDGE_SAVE continuity=1 v1_migrated=1 v2_migrated=1 invalid_v3_atomic=1\n";
}

void combatEventSemantics() {
  for(Kind weapon:{Kind::Striker,Kind::Lancer,Kind::Bastion,Kind::Mortar,Kind::Kite}) {
    auto s=quiet();const Id source=s.debugSpawn(weapon,0,{1400,300});
    const Id target=s.debugSpawn(Kind::Processor,1,{1620,300});
    const float hp=s.find(target)->hp;
    check(s.lastEffectId()==0,"new matches start without a combat event identity");
    check(send(s,CommandType::Attack,0,{source},{},target).accepted,"weapon feedback fixture uses an ordinary attack");
    s.update(Simulation::Step);
    check(s.find(target)->hp<hp&&s.effects().size()==2,"one real shot causes one weapon event and one actual-damage impact");
    const auto shot=s.effects()[0],impact=s.effects()[1];
    check(shot.type==EffectType::Weapon&&impact.type==EffectType::Impact&&shot.id<impact.id,"weapon fire precedes its impact in event identity order");
    check(shot.sourceKind==weapon&&shot.targetKind==Kind::Processor&&shot.team==0,"weapon event identifies its actual weapon profile and target");
    check(impact.sourceKind==weapon&&impact.targetKind==Kind::Processor&&impact.team==1,"impact event records victim ownership and the actual damage source");
    check(distance(shot.from,s.find(source)->pos)<1&&distance(shot.to,s.find(target)->pos)<1&&distance(impact.from,impact.to)==0,"weapon links real firing positions while damage is a point event");
    check(s.lastEffectId()==impact.id,"event cursor includes the final emitted event");
    for(const auto& fx:s.effects())check(fx.id>0&&fx.duration>=0.30f&&fx.life>0&&fx.life<=fx.duration,"every combat event has bounded positive presentation time");
    check(send(s,CommandType::Move,0,{source},{1400,800}).accepted,"moving unit stops firing after the observed shot");
    advance(s,1.1f);check(s.effects().empty()&&s.lastEffectId()==impact.id,"expired feedback is removed without rewinding the sound-event cursor");
    check(send(s,CommandType::Attack,0,{source},{},target).accepted,"weapon can fire again after movement");
    waitUntil(s,12,[&]{return s.lastEffectId()>impact.id;},"later real shot receives a new identity");
    for(const auto& fx:s.effects())check(fx.id>impact.id,"later effects cannot reuse expired event identities");
    s.reset({0,42,false,1,MatchLength::Standard,2,0});check(s.effects().empty()&&s.lastEffectId()==0,"reset starts a fresh effect identity sequence");
  }
  auto siege=quiet();const Id mortar=siege.debugSpawn(Kind::Mortar,0,{1400,300});
  const Id primary=siege.debugSpawn(Kind::Worker,1,{1850,300}),secondary=siege.debugSpawn(Kind::Worker,1,{1890,345});
  send(siege,CommandType::Hold,1,{primary,secondary});
  check(send(siege,CommandType::Attack,0,{mortar},{},primary).accepted,"siege splash fixture uses an ordinary attack");
  siege.update(Simulation::Step);
  check(siege.find(primary)->hp<definition(Kind::Worker).hp&&siege.find(secondary)->hp<definition(Kind::Worker).hp,"primary and nearby splash victims take real damage");
  int impacts=0;for(const auto& fx:siege.effects())if(fx.type==EffectType::Impact)++impacts;
  check(impacts==2&&siege.effects().front().type==EffectType::Weapon,"one siege shot emits a separate point impact for each damaged victim");
  const auto previous=siege.lastEffectId();
  waitUntil(siege,5,[&]{return !siege.find(primary)||!siege.find(primary)->alive();},"siege target dies from repeated real hits");
  const Effect* lethalImpact=nullptr;const Effect* death=nullptr;
  for(const auto& fx:siege.effects())if(fx.id>previous&&distance(fx.to,{1850,300})<20) {
    if(fx.type==EffectType::Impact)lethalImpact=&fx;
    if(fx.type==EffectType::Death)death=&fx;
  }
  check(lethalImpact&&death&&lethalImpact->id<death->id,"lethal damage emits an impact before a distinct death event");
  check(death->sourceKind==Kind::Worker&&death->targetKind==Kind::Worker&&death->team==1&&distance(death->from,death->to)==0,"death profile identifies the destroyed unit and its location");

  auto healing=quiet();const Id ally=healing.debugSpawn(Kind::Striker,0,{1400,300});
  const Id enemy=healing.debugSpawn(Kind::Bastion,1,{1620,300});
  check(send(healing,CommandType::Attack,1,{enemy},{},ally).accepted,"healing fixture first takes real combat damage");
  healing.update(Simulation::Step);const float wounded=healing.find(ally)->hp;const auto beforeHeal=healing.lastEffectId();
  check(wounded<definition(Kind::Striker).hp,"healing requires missing health");
  send(healing,CommandType::Move,1,{enemy},{2400,300});
  const Id healer=healing.debugSpawn(Kind::Mender,0,{1400,450});healing.update(Simulation::Step);
  check(healing.find(ally)->hp>wounded,"Mend applies actual healing");
  int heals=0;for(const auto& fx:healing.effects())if(fx.id>beforeHeal&&fx.type==EffectType::Heal) {
    ++heals;check(fx.sourceKind==Kind::Mender&&fx.targetKind==Kind::Striker&&fx.team==0&&distance(fx.from,healing.find(healer)->pos)<1,"healing has its own typed source-to-patient event");
  }
  check(heals==1,"one actual repair emits one healing event");
  auto healthy=quiet();healthy.debugSpawn(Kind::Mender,0,{1400,300});healthy.debugSpawn(Kind::Striker,0,{1400,450});advance(healthy,0.1f);
  check(healthy.effects().empty()&&healthy.lastEffectId()==0,"full-health allies produce no fake healing feedback");
  std::cout<<"COMBAT_EVENTS weapon_profiles=5 splash_impacts=2 lethal_order=1 actual_heal=1 monotonic_ids=1\n";
}

void combatEventVisibility() {
  auto hidden=quiet();const Id victim=hidden.debugSpawn(Kind::Worker,0,{1500,300});
  const Id mortar=hidden.debugSpawn(Kind::Mortar,1,{2100,300});send(hidden,CommandType::Hold,0,{victim});
  check(!hidden.visible(0,hidden.find(mortar)->pos)&&hidden.visible(1,hidden.find(victim)->pos),"long-range siege fires from beyond the victim's current vision");
  check(send(hidden,CommandType::Attack,1,{mortar},{},victim).accepted,"hidden siege fixture fires an ordinary shared-visible attack");
  hidden.update(Simulation::Step);
  const auto weapon=std::find_if(hidden.effects().begin(),hidden.effects().end(),[](const Effect& fx){return fx.type==EffectType::Weapon;});
  const auto impact=std::find_if(hidden.effects().begin(),hidden.effects().end(),[](const Effect& fx){return fx.type==EffectType::Impact;});
  check(weapon!=hidden.effects().end()&&impact!=hidden.effects().end(),"hidden siege produces its real fire and damage records");
  const Effect oldShot=*weapon,oldImpact=*impact;
  check(!hidden.effectVisible(oldShot,0,true)&&hidden.effectVisible(oldShot,0,false)&&!hidden.effectLinkVisible(oldShot,0),"visible incoming endpoint never reveals an unseen shooter or connecting trajectory");
  check(hidden.effectVisible(oldImpact,0,false)&&hidden.effectVisible(oldImpact,0,true),"damage to a visible victim remains visible without revealing the source");
  hidden.debugSpawn(Kind::Scout,0,{2400,300});
  check(hidden.visible(0,oldShot.from)&&!hidden.effectVisible(oldShot,0,true)&&!hidden.effectLinkVisible(oldShot,0),"vision gained after firing cannot retroactively reveal the old hidden source");
  check(!hidden.effectVisible(oldShot,-1,true)&&!hidden.effectVisible(oldShot,2,false)&&!hidden.effectLinkVisible(oldShot,2),"invalid teams cannot observe event endpoints or links");

  auto fading=quiet();const Id movingVictim=fading.debugSpawn(Kind::Worker,0,{1500,300});
  const Id observer=fading.debugSpawn(Kind::Scout,0,{2400,300});
  const Id shooter=fading.debugSpawn(Kind::Mortar,1,{2100,300});send(fading,CommandType::Hold,0,{movingVictim});
  check(send(fading,CommandType::Attack,1,{shooter},{},movingVictim).accepted,"visible siege fixture fires normally");fading.update(Simulation::Step);
  const auto shot=std::find_if(fading.effects().begin(),fading.effects().end(),[](const Effect& fx){return fx.type==EffectType::Weapon;});
  check(shot!=fading.effects().end(),"visible siege fixture records a shot");const Effect initiallyVisible=*shot;
  check(fading.effectVisible(initiallyVisible,0,true)&&fading.effectVisible(initiallyVisible,0,false),"both endpoints are initially observable");
  send(fading,CommandType::Move,0,{observer},{4200,300});send(fading,CommandType::Move,0,{movingVictim},{4000,1300});
  send(fading,CommandType::Move,1,{shooter},{2100,1200});advance(fading,22);
  check(!fading.visible(0,initiallyVisible.from)&&!fading.visible(0,initiallyVisible.to),"observers actually leave both old firing positions in fog");
  check(!fading.effectVisible(initiallyVisible,0,true)&&!fading.effectVisible(initiallyVisible,0,false)&&!fading.effectLinkVisible(initiallyVisible,0),"recorded visibility cannot keep drawing an endpoint after current vision is lost");

  auto gap=quiet();gap.debugSpawn(Kind::Scout,0,{4200,4200});
  Effect link;link.from={600,600};link.to={4200,4200};link.team=0;link.type=EffectType::Weapon;
  link.sourceKind=Kind::Mortar;link.targetKind=Kind::Worker;link.id=1;link.life=link.duration=0.55f;link.fromVisibleMask=link.toVisibleMask=1;
  check(gap.effectVisible(link,0,true)&&gap.effectVisible(link,0,false)&&!gap.visible(0,{2400,2400}),"link guard fixture has visible endpoints with unexplored terrain between them");
  check(!gap.effectLinkVisible(link,0),"visible endpoints cannot draw a line across hidden terrain");
  for(Vec2 point:{Vec2{1500,1500},Vec2{2400,2400},Vec2{3300,3300}})gap.debugSpawn(Kind::Headquarters,0,point);
  check(gap.effectLinkVisible(link,0),"a connecting line is allowed when the entire segment is currently visible");
  // A line parallel to the grid diagonal clips each adjacent off-diagonal
  // cell for only sqrt(2) world units. Coarse distance samples can skip this
  // hidden interval even though both endpoints and every sample are visible.
  const auto fogPath=savePath("combat-fog-corner");auto fogFixture=quiet();
  check(fogFixture.save(fogPath),"corner visibility fixture saves its valid match shell");
  std::ifstream fogInput(fogPath);std::vector<std::string> fogLines;std::string fogLine;
  while(std::getline(fogInput,fogLine))fogLines.push_back(fogLine);fogInput.close();
  const auto fogLayout=savedLayout(fogLines);
  const auto firstFog=fogLayout.effects+1+static_cast<std::size_t>(std::stoul(saveFields(fogLines[fogLayout.effects])[0]));
  std::vector<std::string> cells(Simulation::FogSize*Simulation::FogSize,"0");
  for(int n=0;n<5;++n)cells[n*Simulation::FogSize+n]="1";
  for(int n=0;n<4;++n)cells[(n+1)*Simulation::FogSize+n]="1";
  cells[2*Simulation::FogSize+1]="0";
  auto loadFog=[&]() {
    fogLines[firstFog]=joinSaveFields(cells);fogLines[firstFog+1]=fogLines[firstFog];
    {std::ofstream out(fogPath);for(const auto& row:fogLines)out<<row<<'\n';}
    Simulation result;check(result.load(fogPath),"controlled fog geometry loads without changing any game actors");return result;
  };
  Effect corner=link;corner.from={37.5f,38.5f};corner.to={337.5f,338.5f};
  auto clipped=loadFog();
  check(clipped.effectVisible(corner,0,true)&&clipped.effectVisible(corner,0,false)&&!clipped.visible(0,{149.5f,150.5f}),"tiny clipped-cell fixture has visible endpoints and a genuinely hidden interval");
  check(!clipped.effectLinkVisible(corner,0),"a link cannot skip the 1.4-unit hidden interval near a fog-cell corner");
  cells[2*Simulation::FogSize+1]="1";auto revealed=loadFog();
  check(revealed.effectLinkVisible(corner,0),"revealing the one clipped cell permits the identical segment");
  std::filesystem::remove(fogPath);
  std::cout<<"COMBAT_FOG hidden_source=1 no_late_revelation=1 current_visibility_required=1 hidden_segment_blocked=1 tiny_corner_clip_blocked=1\n";
}

void combatEventPersistence() {
  auto s=aiProductionFixture(Kind::Kite,true);s.debugResources(1,0);
  const Id attacker=s.debugSpawn(Kind::Striker,0,{1750,300}),victim=first(s,1,Kind::Mender);
  check(send(s,CommandType::Attack,0,{attacker},{},victim).accepted,"event persistence fixture causes ordinary combat");s.update(Simulation::Step);
  check(s.effects().size()>=2&&!s.aiSightings().empty(),"save fixture contains live typed events and observed AI knowledge");
  const auto path=savePath("combat-events-v4");check(s.save(path),"typed combat feedback saves");
  Simulation loaded;check(loaded.load(path)&&loaded.stateHash()==s.stateHash(),"event metadata, lifetime, visibility and next identity survive a round trip");
  const auto savedId=s.lastEffectId();
  for(int n=0;n<100;++n){s.update(Simulation::Step);loaded.update(Simulation::Step);check(s.stateHash()==loaded.stateHash(),"loaded combat continues with identical damage and typed event identities");}
  check(s.lastEffectId()>savedId,"post-load combat emits later identities instead of replaying the saved identity range");
  std::ifstream input(path);std::vector<std::string> lines;std::string line;while(std::getline(input,line))lines.push_back(line);input.close();
  check(lines.front()=="CINDERLINE 15","combat persistence declares revision-aware save version fifteen");
  const auto layout=savedLayout(lines);const auto effectHeader=saveFields(lines[layout.effects]);
  const auto count=static_cast<std::size_t>(std::stoul(effectHeader[0]));check(count>=2,"saved event corruption fixture has distinct ordered events");
  auto write=[&](const std::vector<std::string>& content){std::ofstream out(path);for(const auto& value:content)out<<value<<'\n';};
  auto reject=[&](std::vector<std::string> content,const std::string& message){
    write(content);Simulation current=quiet();const auto before=current.stateHash();check(!current.load(path),message);
    check(current.stateHash()==before,"invalid combat event loads preserve the existing match atomically");
  };
  for(const auto& [column,value]:std::vector<std::pair<std::size_t,std::string>>{{0,"nan"},{2,"4801"},{4,"2"},{5,"0"},{5,"2"},{6,"0.2"},{6,"11"},{7,"0"},{7,"-1"},{7,"18446744073709551616"},{8,"4"},{9,"14"},{9,"15"},{10,"-1"},{11,"4"},{12,"-1"}}) {
    auto broken=lines;auto fields=saveFields(broken[layout.effects+1]);fields[column]=value;broken[layout.effects+1]=joinSaveFields(fields);
    reject(broken,"invalid combat event coordinates, ownership, lifetime, identity, type, profile or visibility are rejected");
  }
  for(const auto& [column,value]:std::vector<std::pair<std::size_t,std::string>>{{0,"100"},{11,"0"},{8,"3"}}) {
    auto broken=lines;auto fields=saveFields(broken[layout.effects+2]);
    check(fields[8]=="1"&&fields[9]!=fields[10],"point-event corruption starts from an actual impact with distinct attacker and victim kinds");
    fields[column]=value;broken[layout.effects+2]=joinSaveFields(fields);
    reject(broken,"point-event geometry, visibility symmetry and death victim profiles are validated");
  }
  auto duplicated=lines;duplicated[layout.effects+2]=duplicated[layout.effects+1];reject(duplicated,"duplicate or non-increasing event identities are rejected");
  for(const std::string& next:{std::string("0"),saveFields(lines[layout.effects+count])[7]}) {
    auto broken=lines;broken[layout.effects]=effectHeader[0]+" "+next;reject(broken,"next event identity must be nonzero and follow every saved event");
  }
  const auto savedKnowledge=static_cast<std::size_t>(std::stoul(*(std::find(lines.begin(),lines.end(),"AI_KNOWLEDGE 1")+1)));
  for(int version:{1,2,3}) {
    write(legacyCombatSave(lines,version));Simulation migrated;check(migrated.load(path),"older saves with ambiguous effects remain readable");
    check(migrated.effects().empty()&&migrated.lastEffectId()==0,"legacy migration discards ambiguous old feedback and starts a fresh event cursor");
    check(migrated.find(victim)&&migrated.find(victim)->hp<definition(Kind::Mender).hp,"legacy feedback migration preserves actual combat damage");
    check(migrated.aiSightings().size()==(version==3?savedKnowledge:0),"version three migration preserves knowledge while versions one and two begin unknown");
    waitUntil(migrated,2,[&]{return migrated.lastEffectId()>0;},"legacy match emits newly typed events through subsequent ordinary combat");
  }
  std::filesystem::remove(path);
  std::cout<<"COMBAT_SAVE v4_continuity=1 legacy_effects_discarded=3 v3_knowledge_retained=1 malformed_events_atomic=1\n";
}

void benchmark() {
  for(int count:{50,100,200}) {
    auto s=quiet();std::vector<Id> groups[2];
    for(int i=0;i<count;++i) {
      int team=i%2;float x=team?3200.f:1600.f;
      groups[team].push_back(s.debugSpawn(i%5==0?Kind::Mortar:Kind::Striker,team,{x+(i%7)*35,1750.f+(i/7)*40}));
    }
    for(int team=0;team<2;++team) send(s,CommandType::AttackMove,team,groups[team],{2400,2400});
    std::vector<double> samples;samples.reserve(500);int minUnits=count;
    for(int i=0;i<500;++i) {
      auto start=std::chrono::steady_clock::now();s.update(Simulation::Step);
      samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
      int alive=0;for(const auto& group:groups) for(auto id:group) if(s.find(id)&&s.find(id)->alive()) ++alive;
      minUnits=std::min(minUnits,alive);
    }
    std::sort(samples.begin(),samples.end());
    std::cout<<"BENCHMARK combat_units="<<count<<" starting_workers=10 min_combat_alive="<<minUnits<<" samples="<<samples.size()
             <<" mean_ms="<<std::accumulate(samples.begin(),samples.end(),0.0)/samples.size()
             <<" p95_ms="<<samples[static_cast<std::size_t>(samples.size()*0.95)]<<" max_ms="<<samples.back()<<'\n';
    check(samples.back()<1000,"simulation update exceeds one second");
  }
  auto s=quiet();std::vector<Id> group;
  for(int i=0;i<200;++i) group.push_back(s.debugSpawn(Kind::Striker,0,{700.f+(i%20)*32,1100.f+(i/20)*32}));
  check(send(s,CommandType::Move,0,group,{1500,1300}).accepted,"large group move accepted");
  std::vector<double> timings;
  for(int i=0;i<1000;++i) {
    auto start=std::chrono::steady_clock::now();s.update(Simulation::Step);
    timings.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
  }
  std::vector<float> remaining;float minSeparation=10000;int unreachable=0;
  for(auto a:group) {
    const auto* e=s.find(a);check(e&&e->alive(),"large moving group remains alive");
    const float d=distance(e->pos,e->goal);remaining.push_back(d);if(d>100) ++unreachable;
    for(auto b:group) if(a!=b) minSeparation=std::min(minSeparation,distance(e->pos,s.find(b)->pos));
  }
  std::sort(remaining.begin(),remaining.end());std::sort(timings.begin(),timings.end());
  std::cout<<"GROUP_MOVE units=200 simulation_seconds="<<s.time()<<" median_distance_to_assigned_goal="<<remaining[100]
           <<" p95_distance_to_assigned_goal="<<remaining[190]<<" unreachable_over_100="<<unreachable
           <<" min_separation="<<minSeparation<<" clearance_ratio="<<minSeparation/(2*definition(Kind::Striker).radius)
           <<" mean_ms="<<std::accumulate(timings.begin(),timings.end(),0.0)/timings.size()
           <<" p95_ms="<<timings[950]<<" max_ms="<<timings.back()<<'\n';
  if(unreachable>10) {
    int printed=0;
    for(auto id:group) {
      const auto* e=s.find(id);if(distance(e->pos,e->goal)<=100) continue;
      if(printed++<8) std::cout<<"GROUP_LAGGARD id="<<id<<" pos="<<e->pos.x<<','<<e->pos.y
        <<" goal="<<e->goal.x<<','<<e->goal.y<<" order="<<static_cast<int>(e->order)
        <<" path="<<e->pathIndex<<'/'<<e->path.size()<<" repath="<<e->repath<<'\n';
    }
    if(std::filesystem::is_directory("artifacts")) s.save("artifacts/group-move-failure.sav");
  }
  check(unreachable<=10,"at least 95 percent of large group reaches assigned goals");
  check(minSeparation>=1.5f*definition(Kind::Striker).radius,"large resting group maintains meaningful clearance");
}

// Team zero uses only public commands and its normal starting resources. This is
// an integration opponent, not a claim that human multiplayer balance is settled.
void commandIntegrationOpponent(Simulation& s) {
  const int team=0;auto hqs=ids(s,team,Kind::Headquarters);if(hqs.empty()) return;
  auto base=s.find(hqs.front())->pos;auto workers=ids(s,team,Kind::Worker);
  for(auto id:workers) {
    const auto* w=s.find(id);if(w->order!=Order::Idle) continue;
    Id nearest=0;float best=1e9;
    for(const auto& e:s.entities()) if(e.kind==Kind::Resource&&e.resource>0&&distance(w->pos,e.pos)<best) {nearest=e.id;best=distance(w->pos,e.pos);}
    if(nearest) send(s,CommandType::Gather,team,{id},{},nearest);
  }
  if(workers.size()<12&&s.find(hqs.front())->queue.empty()) send(s,CommandType::Train,team,{hqs.front()},{},0,Kind::Worker);
  auto build=[&](Kind kind) {
    if(workers.empty()||s.players()[team].ore<definition(kind).cost) return;
    const auto available=std::find_if(workers.begin(),workers.end(),[&](Id id){return s.find(id)->order!=Order::Construct;});
    if(available==workers.end())return;
    try {auto p=validPlacement(s,team,kind,base);send(s,CommandType::Build,team,{*available},p,0,kind);} catch(const std::runtime_error&) {}
  };
  if(ids(s,team,Kind::Foundry).empty()&&workers.size()>=7) build(Kind::Foundry);
  if(s.capacity(team)-s.supply(team)<6) build(Kind::Processor);
  if(s.time()>210&&ids(s,team,Kind::Laboratory).empty()) build(Kind::Laboratory);
  for(auto lab:ids(s,team,Kind::Laboratory)) if(s.find(lab)->queue.empty()&&s.find(lab)->progress>=1) {
    if(s.players()[team].tier<2) send(s,CommandType::Research,team,{lab},{},0,Kind::Worker,0);
    else if(s.players()[team].weapons<1) send(s,CommandType::Research,team,{lab},{},0,Kind::Worker,1);
  }
  if(s.players()[team].tier>=2&&ids(s,team,Kind::MotorPool).empty()) build(Kind::MotorPool);
  for(auto producer:ids(s,team,Kind::Foundry)) if(s.find(producer)->queue.empty()) {
    auto n=s.players()[team].stats.produced;
    send(s,CommandType::Train,team,{producer},{},0,n%4==0?Kind::Lancer:Kind::Striker);
  }
  for(auto producer:ids(s,team,Kind::MotorPool)) if(s.find(producer)->queue.empty())
    send(s,CommandType::Train,team,{producer},{},0,s.players()[team].stats.produced%3==0?Kind::Mortar:Kind::Bastion);
  std::vector<Id> army;
  for(const auto& e:s.entities()) if(e.alive()&&e.team==team&&!definition(e.kind).building&&e.kind!=Kind::Worker&&e.kind!=Kind::Resource) army.push_back(e.id);
  if(s.time()>240&&army.size()>=8) send(s,CommandType::AttackMove,team,army,{4200,4200});
}

void matchDuration() {
  for(int map=0;map<3;++map) {
    Simulation s;s.reset({map,42,true,1,MatchLength::Standard,2,0});
    const auto wall=std::chrono::steady_clock::now();
    while(s.time()<1800&&s.winner()<0) {
      if(s.tick()%40==0) commandIntegrationOpponent(s);
      s.update(Simulation::Step);
      if(s.tick()%6000==0) std::cout<<"MATCH_PROGRESS map="<<map<<" simulation_seconds="<<s.time()<<" entities="<<s.entities().size()<<std::endl;
    }
    std::cout<<"MATCH map="<<map<<" seed=42 duration_seconds="<<s.time()<<" winner="<<s.winner()
             <<" wall_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count();
    for(int t=0;t<2;++t) {
      const auto& p=s.players()[t];
      std::cout<<" team"<<t<<"_gathered="<<p.stats.gathered<<" team"<<t<<"_produced="<<p.stats.produced
               <<" team"<<t<<"_lost="<<p.stats.lost<<" team"<<t<<"_ore="<<p.ore;
    }
    std::cout<<std::endl;
    check(s.players()[0].ore>=0&&s.players()[1].ore>=0,"full match economy stays nonnegative");
  }
}
}

int main(int argc,char** argv) {
  std::cout<<std::fixed<<std::setprecision(3);
  std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"reset and definitions",resetAndDefinitions},{"physical gathering and depletion",gatherAndDepletion},
    {"paid commands, production and supply",paidCommandsAndQueues},{"placement and construction",placementAndConstruction},
    {"bounded production queue",boundedProductionQueue},
    {"worker construction economy",workerConstructionEconomy},{"worker construction interruptions",workerConstructionInterruptions},
    {"worker construction hazards",workerConstructionHazards},{"construction persistence and migration",constructionPersistence},
    {"research and prerequisites",researchAndPrerequisites},{"ownership and fog",ownershipAndFog},
    {"obstacle paths and group movement",movementAndGroups},{"ground and air combat",airAndCombat},{"tactical orders and support",tacticalOrders},
    {"victory and defeat",victoryAndDefeat},{"save-load continuity and replay",saveLoadAndReplay},
    {"bounded fixed-step update",boundedUpdate},{"AI expansion and local defense",aiExpansionAndBaseDefense},{"AI paid economy",aiEconomy},
    {"AI construction assignments",aiConstructionAssignments},
    {"AI observation lifecycle",aiObservationLifecycle},{"AI observed production",aiObservedProduction},
    {"AI scouting objectives",aiScoutingObjectives},{"AI knowledge persistence",aiKnowledgePersistence},
    {"combat event semantics",combatEventSemantics},{"combat event visibility",combatEventVisibility},
    {"combat event persistence",combatEventPersistence}};
  if(argc>1&&std::string(argv[1])=="--benchmark") tests={{"performance",benchmark}};
  else if(argc>1&&std::string(argv[1])=="--match") tests={{"natural AI match durations",matchDuration}};
  else if(argc>1) {
    const std::string filter=argv[1];
    tests.erase(std::remove_if(tests.begin(),tests.end(),[&](const auto& test){return test.first.find(filter)==std::string::npos;}),tests.end());
    if(tests.empty()) {std::cerr<<"No matching test: "<<filter<<'\n';return 2;}
  }
  int failed=0;
  for(const auto& [name,test]:tests) {
    try {test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error) {++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
  }
  std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
