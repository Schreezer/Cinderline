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
Simulation quiet(int map=0) {
  Simulation s; s.reset({map,42,false,1});
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
std::string savePath(const std::string& suffix) {
  return (std::filesystem::temp_directory_path()/("cinderline-tests-"+suffix+".sav")).string();
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
  s.reset({0,42,false,1});
  check(s.tick()==0&&s.winner()==-1&&s.recording().empty(),"reset clears time, result and recording");
  Simulation fresh; fresh.reset({0,42,false,1});
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
  advance(damaged,foundry.buildTime+1);
  const auto* completed=damaged.find(building);
  check(completed&&completed->alive()&&completed->progress==1,"damaged building still completes");
  check(std::abs(completed->hp-(foundry.hp-damageTaken))<0.05f,"completion preserves damage taken during construction");
  std::cout<<"CONSTRUCTION_HEALTH full_hp="<<s.find(first(s,0,Kind::Foundry))->hp
           <<" damage_taken="<<damageTaken<<" completed_damaged_hp="<<completed->hp<<'\n';
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
  const auto recording=original.recording();Simulation replay;replay.reset({0,42,false,1});
  std::size_t index=0;
  while(replay.tick()<original.tick()) {
    while(index<recording.size()&&recording[index].tick==replay.tick()) {
      check(replay.command(recording[index].command).accepted,"recorded command remains valid on replay");++index;
    }
    replay.update(Simulation::Step);
  }
  check(index==recording.size(),"replayed every command");
  check(replay.stateHash()==original.stateHash(),"tick-indexed command replay is deterministic");
  Simulation ai;ai.reset({2,73,true,1});advance(ai,75);
  const auto aiPath=savePath("ai-continuity");check(ai.save(aiPath),"AI match save succeeds");
  Simulation resumed;check(resumed.load(aiPath),"AI match load succeeds");
  for(int i=0;i<250;++i) {
    ai.update(Simulation::Step);resumed.update(Simulation::Step);
    check(ai.stateHash()==resumed.stateHash(),"AI timers and decisions continue identically after load");
  }
  std::filesystem::remove(aiPath);
}

void boundedUpdate() {
  auto s=quiet(); const auto tick=s.tick();
  s.update(-1);s.update(std::numeric_limits<float>::quiet_NaN());s.update(std::numeric_limits<float>::infinity());
  check(s.tick()==tick,"negative and nonfinite updates ignored");
  s.update(1000000);
  check(s.tick()>tick&&s.tick()-tick<=20,"one update has bounded catch-up work");
  for(const auto& e:s.entities()) check(std::isfinite(e.pos.x)&&std::isfinite(e.pos.y),"bounded update preserves finite positions");
}

void aiEconomy() {
  Simulation s;s.reset({0,123,true,1});
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
    try {auto p=validPlacement(s,team,kind,base);send(s,CommandType::Build,team,{workers.front()},p,0,kind);} catch(const std::runtime_error&) {}
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
    Simulation s;s.reset({map,42,true,1});
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
    {"research and prerequisites",researchAndPrerequisites},{"ownership and fog",ownershipAndFog},
    {"obstacle paths and group movement",movementAndGroups},{"ground and air combat",airAndCombat},
    {"victory and defeat",victoryAndDefeat},{"save-load continuity and replay",saveLoadAndReplay},
    {"bounded fixed-step update",boundedUpdate},{"AI paid economy",aiEconomy}};
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
