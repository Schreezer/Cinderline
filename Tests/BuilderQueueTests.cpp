#include "Sim/Network.h"
#include "Sim/Simulation.h"

#include <algorithm>
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

std::vector<Entity>& entities(Simulation& simulation) {
  return const_cast<std::vector<Entity>&>(simulation.entities());
}

Entity* edit(Simulation& simulation,Id id) {
  for(auto& entity:entities(simulation))if(entity.id==id)return &entity;
  return nullptr;
}

Simulation fixture() {
  Simulation simulation;
  // Queued-work fixtures replace the map with a custom flat building arena.
  simulation.reset({0,0xB017D3u,false,1,MatchLength::Standard,2,0});
  entities(simulation).clear();
  const_cast<std::vector<Obstacle>&>(simulation.obstacles()).clear();
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
          "builder-queue fixture remains an active match");
  }
}

template<class Predicate>
void stepUntil(Simulation& simulation,int maximum,Predicate complete,const std::string& failure) {
  for(int index=0;index<maximum&&!complete();++index)step(simulation);
  check(complete(),failure);
}

std::vector<Id> living(const Simulation& simulation,int team,Kind kind,Id excluded=0) {
  std::vector<Id> result;
  for(const auto& entity:simulation.entities())
    if(entity.alive()&&entity.team==team&&entity.kind==kind&&entity.id!=excluded)
      result.push_back(entity.id);
  return result;
}

Id first(const Simulation& simulation,int team,Kind kind,Id excluded=0) {
  const auto result=living(simulation,team,kind,excluded);
  return result.empty()?0:result.front();
}

Vec2 placement(const Simulation& simulation,Kind kind,Vec2 origin,
               const std::vector<Vec2>& avoid={},int team=0) {
  for(float radius=260;radius<=680;radius+=35)for(int spoke=0;spoke<48;++spoke) {
    const float angle=6.283185307f*static_cast<float>(spoke)/48.0f;
    const Vec2 point{origin.x+radius*std::cos(angle),origin.y+radius*std::sin(angle)};
    bool separated=true;
    for(Vec2 other:avoid)
      if(distance(point,other)<definition(kind).radius+110.0f)separated=false;
    if(separated&&simulation.canPlace(team,kind,point))return point;
  }
  throw std::runtime_error("fixture cannot find a legal queued construction site");
}

void checkBuildStep(const TacticalOrder& order,Vec2 point,Kind kind,const std::string& label) {
  check(order.order==Order::Construct&&order.supportTarget==0&&
        order.buildingKind==kind&&distance(order.point,point)<0.01f,
        label+" stores an unpaid site and its actual building kind");
}

void checkResumeStep(const TacticalOrder& order,Id foundation,const std::string& label) {
  check(order.order==Order::Construct&&order.supportTarget==foundation&&
        order.buildingKind==Kind::Worker,
        label+" stores the unfinished foundation with the Worker sentinel");
}

void checkGatherStep(const TacticalOrder& order,Id ore,const std::string& label) {
  check(order.order==Order::Gather&&order.supportTarget==ore&&
        order.buildingKind==Kind::Worker,
        label+" stores the ore target with the Worker sentinel");
}

void serialBuildsStartLazilyAndResumeOriginalMining() {
  auto simulation=fixture();
  const Id worker=simulation.debugSpawn(Kind::Worker,0,{1050,1050});
  const Id ore=simulation.debugSpawn(Kind::Resource,-1,{1130,1050});
  step(simulation); // Establish ordinary vision and exploration for Gather.
  const Vec2 firstSite=placement(simulation,Kind::Foundry,simulation.find(worker)->pos);
  const Vec2 secondSite=placement(simulation,Kind::Processor,simulation.find(worker)->pos,{firstSite});
  check(send(simulation,CommandType::Gather,0,{worker},{},ore).accepted,
        "Drudge begins ordinary mining before receiving a construction plan");
  stepUntil(simulation,300,[&]{return simulation.find(worker)->carried>0;},
            "Drudge did not harvest cargo before construction was queued");
  const int initialOre=simulation.players()[0].ore;

  check(send(simulation,CommandType::Build,0,{worker},firstSite,0,Kind::Foundry,
             CommandQueueMode::Append).accepted&&
        send(simulation,CommandType::Build,0,{worker},secondSite,0,Kind::Processor,
             CommandQueueMode::Append).accepted,
        "a mining Drudge accepts two serial construction jobs");
  const Entity* planned=simulation.find(worker);
  check(planned->order==Order::Gather&&planned->futureOrders.size()==2&&
        simulation.players()[0].ore==initialOre&&living(simulation,0,Kind::Foundry).empty()&&
        living(simulation,0,Kind::Processor).empty(),
        "queued builds reserve neither ore nor foundation entities");
  checkBuildStep(planned->futureOrders[0],firstSite,Kind::Foundry,"first queued build");
  checkBuildStep(planned->futureOrders[1],secondSite,Kind::Processor,"second queued build");

  stepUntil(simulation,1200,[&]{return first(simulation,0,Kind::Foundry)!=0;},
            "mining delivery did not activate the first queued build");
  const Id foundry=first(simulation,0,Kind::Foundry);
  check(simulation.find(worker)->order==Order::Construct&&
        simulation.constructionWorker(foundry)==worker&&
        simulation.find(worker)->futureOrders.size()==1&&
        simulation.find(worker)->carried==0&&simulation.players()[0].stats.gathered>0&&
        simulation.players()[0].ore==initialOre+simulation.players()[0].stats.gathered-
                                      definition(Kind::Foundry).cost,
        "first activation delivers existing cargo, charges exactly once, and preserves the remaining plan");

  stepUntil(simulation,4000,[&] {
    return simulation.find(foundry)->progress>=1&&first(simulation,0,Kind::Processor)!=0;
  },"first completion did not safely create the next foundation while iterating construction");
  const Id processor=first(simulation,0,Kind::Processor);
  check(simulation.find(foundry)->progress==1&&simulation.find(worker)->order==Order::Construct&&
        simulation.constructionWorker(processor)==worker&&simulation.find(worker)->futureOrders.empty()&&
        simulation.players()[0].ore==initialOre+simulation.players()[0].stats.gathered-
                                      definition(Kind::Foundry).cost-definition(Kind::Processor).cost,
        "completion-time vector growth leaves both foundations and the builder link valid");

  stepUntil(simulation,4000,[&] {
    const Entity* state=simulation.find(worker);
    return simulation.find(processor)->progress>=1&&state->order==Order::Gather&&
           state->resourceTarget==ore;
  },"the full construction chain did not resume the original mining job");
  check(simulation.find(worker)->futureOrders.empty()&&simulation.players()[0].stats.built==2,
        "two real construction successes finish before original mining resumes");
}

void explicitQueuedGatherOverridesMiningRecovery() {
  auto simulation=fixture();
  const Id worker=simulation.debugSpawn(Kind::Worker,0,{1050,1500});
  const Id originalOre=simulation.debugSpawn(Kind::Resource,-1,{1130,1500});
  const Id chosenOre=simulation.debugSpawn(Kind::Resource,-1,{1200,1660});
  step(simulation);
  const Vec2 site=placement(simulation,Kind::Foundry,simulation.find(worker)->pos);

  check(send(simulation,CommandType::Gather,0,{worker},{},originalOre).accepted&&
        send(simulation,CommandType::Build,0,{worker},site,0,Kind::Foundry,
             CommandQueueMode::Append).accepted&&
        send(simulation,CommandType::Gather,0,{worker},{},chosenOre,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "a miner accepts construction followed by an explicit ore target");
  checkGatherStep(simulation.find(worker)->futureOrders[1],chosenOre,"queued Gather");

  stepUntil(simulation,5000,[&] {
    const Entity* state=simulation.find(worker);
    const Id foundry=first(simulation,0,Kind::Foundry);
    return foundry&&simulation.find(foundry)->progress>=1&&state->order==Order::Gather&&
           state->resourceTarget==chosenOre;
  },"explicit post-build Gather did not override automatic mining recovery");
  check(simulation.find(worker)->target==chosenOre&&simulation.find(worker)->futureOrders.empty(),
        "queued Gather activates once with its selected deposit");
}

void intermediateMovePreservesMiningAcrossSaveLoad() {
  auto simulation=fixture();
  const Id worker=simulation.debugSpawn(Kind::Worker,0,{1050,1850});
  const Id ore=simulation.debugSpawn(Kind::Resource,-1,{1130,1850});
  step(simulation);
  const Vec2 firstSite=placement(simulation,Kind::Foundry,simulation.find(worker)->pos);
  const Vec2 secondSite=placement(simulation,Kind::Processor,simulation.find(worker)->pos,{firstSite});
  const Vec2 waypoint{1000,2250};
  check(send(simulation,CommandType::Gather,0,{worker},{},ore).accepted&&
        send(simulation,CommandType::Build,0,{worker},firstSite,0,Kind::Foundry).accepted&&
        simulation.find(worker)->resumeGather&&
        send(simulation,CommandType::Move,0,{worker},waypoint,0,Kind::Worker,
             CommandQueueMode::Append).accepted&&
        send(simulation,CommandType::Build,0,{worker},secondSite,0,Kind::Processor,
             CommandQueueMode::Append).accepted,
        "Construct accepts an intermediate Move and second Construct while remembering mining");
  const Id firstFoundation=first(simulation,0,Kind::Foundry);
  stepUntil(simulation,4000,[&] {
    const Entity* state=simulation.find(worker);
    return simulation.find(firstFoundation)->progress>=1&&state->order==Order::Move&&
           state->futureOrders.size()==1;
  },"first Construct did not advance to the intermediate Move");
  const Entity* moving=simulation.find(worker);
  check(moving->resumeGather&&moving->resourceTarget==ore&&
        distance(moving->goal,waypoint)<0.01f,
        "intermediate Move retains the original mining recovery state");
  checkBuildStep(moving->futureOrders.front(),secondSite,Kind::Processor,
                 "Construct after intermediate Move");

  const auto path=std::filesystem::temp_directory_path()/
    "cinderline-builder-queue-intermediate-move-v14.sav";
  check(simulation.save(path.string()),"intermediate Move worker plan saves");
  Simulation loaded;
  check(loaded.load(path.string()),"intermediate Move worker plan loads");
  std::filesystem::remove(path);
  check(loaded.stateHash()==simulation.stateHash()&&loaded.find(worker)->resumeGather&&
        loaded.find(worker)->resourceTarget==ore,
        "save-load preserves the exact intermediate Move hash and mining recovery state");

  stepUntil(simulation,6000,[&] {
    const Id second=first(simulation,0,Kind::Processor);
    const Entity* state=simulation.find(worker);
    return second&&simulation.find(second)->progress>=1&&state->order==Order::Gather&&
           state->resourceTarget==ore;
  },"Construct-Move-Construct chain did not return the source simulation to mining");
  stepUntil(loaded,6000,[&] {
    const Id second=first(loaded,0,Kind::Processor);
    const Entity* state=loaded.find(worker);
    return second&&loaded.find(second)->progress>=1&&state->order==Order::Gather&&
           state->resourceTarget==ore;
  },"loaded Construct-Move-Construct chain did not return to mining");
  check(loaded.stateHash()==simulation.stateHash(),
        "source and loaded intermediate-Move plans complete deterministically");
}

void queuedResumeThenBuildCompletesSerially() {
  auto simulation=fixture();
  const Id donor=simulation.debugSpawn(Kind::Worker,0,{1050,2050});
  const Id worker=simulation.debugSpawn(Kind::Worker,0,{1000,2300});
  step(simulation);
  const Vec2 pausedSite=placement(simulation,Kind::Foundry,simulation.find(donor)->pos);
  check(send(simulation,CommandType::Build,0,{donor},pausedSite,0,Kind::Foundry).accepted,
        "resume fixture creates a paid foundation");
  const Id paused=first(simulation,0,Kind::Foundry);
  stepUntil(simulation,900,[&]{return simulation.find(paused)->progress>0.02f;},
            "donor did not begin real construction before the pause");
  check(send(simulation,CommandType::Stop,0,{donor}).accepted&&
        simulation.constructionWorker(paused)==0,"Stop leaves a resumable paid foundation");
  const float retainedProgress=simulation.find(paused)->progress;
  const Vec2 newSite=placement(simulation,Kind::Processor,simulation.find(worker)->pos,{pausedSite});
  const Vec2 waypoint{simulation.find(worker)->pos.x+120,simulation.find(worker)->pos.y};

  const auto unchangedHash=simulation.stateHash();
  const auto unchangedRecording=simulation.recording().size();
  check(!send(simulation,CommandType::Build,0,{worker,donor},newSite,0,Kind::Processor,
              CommandQueueMode::Append).accepted&&
        !send(simulation,CommandType::ResumeConstruction,0,{worker,donor},{},paused,
              Kind::Worker,CommandQueueMode::Append).accepted&&
        simulation.stateHash()==unchangedHash&&simulation.recording().size()==unchangedRecording,
        "queued Build and Resume require exactly one Drudge and reject groups atomically");

  check(send(simulation,CommandType::Move,0,{worker},waypoint).accepted&&
        send(simulation,CommandType::ResumeConstruction,0,{worker},{},paused,Kind::Worker,
             CommandQueueMode::Append).accepted&&
        send(simulation,CommandType::Build,0,{worker},newSite,0,Kind::Processor,
             CommandQueueMode::Append).accepted,
        "a moving Drudge accepts Resume followed by Build");
  const Entity* planned=simulation.find(worker);
  check(planned->futureOrders.size()==2,
        "resume chain retains both deferred jobs behind the active waypoint");
  checkResumeStep(planned->futureOrders[0],paused,"queued Resume");
  checkBuildStep(planned->futureOrders[1],newSite,Kind::Processor,"build after Resume");

  stepUntil(simulation,1800,[&] {
    return simulation.find(worker)->order==Order::Construct&&
           simulation.find(worker)->target==paused&&simulation.constructionWorker(paused)==worker;
  },"waypoint completion did not activate the queued Resume");
  check(simulation.find(paused)->progress>=retainedProgress,
        "queued Resume continues the existing foundation without resetting progress");
  stepUntil(simulation,5000,[&] {
    return simulation.find(paused)->progress>=1&&first(simulation,0,Kind::Processor)!=0;
  },"resumed construction did not activate the following queued Build");
  const Id processor=first(simulation,0,Kind::Processor);
  stepUntil(simulation,4000,[&]{return simulation.find(processor)->progress>=1;},
            "Build after Resume did not complete through ordinary construction");
  check(simulation.find(worker)->futureOrders.empty()&&simulation.players()[0].stats.built==2,
        "Resume and Build each complete once in FIFO order");
}

void activationFailuresSkipOnlyInvalidWork() {
  auto noOre=fixture();
  const Id worker=noOre.debugSpawn(Kind::Worker,0,{1050,2850});
  const Id ore=noOre.debugSpawn(Kind::Resource,-1,{1170,2850});
  step(noOre);
  const Vec2 site=placement(noOre,Kind::Foundry,noOre.find(worker)->pos);
  check(send(noOre,CommandType::Move,0,{worker},{1150,2850}).accepted&&
        send(noOre,CommandType::Build,0,{worker},site,0,Kind::Foundry,
             CommandQueueMode::Append).accepted&&
        send(noOre,CommandType::Gather,0,{worker},{},ore,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "insufficient-ore fixture accepts a valid future plan");
  const auto teamZeroSerial=noOre.workerPlanNoticeSerial(0);
  const auto teamOneSerial=noOre.workerPlanNoticeSerial(1);
  check(teamZeroSerial==0&&teamOneSerial==0&&noOre.workerPlanNotice(0).empty()&&
        noOre.workerPlanNotice(1).empty(),
        "accepted deferred work starts without a failure notice for either owner");
  noOre.debugResources(0,0);
  stepUntil(noOre,1000,[&] {
    return noOre.find(worker)->order==Order::Gather&&noOre.find(worker)->resourceTarget==ore;
  },"insufficient future Build did not skip to Gather");
  check(living(noOre,0,Kind::Foundry).empty()&&noOre.players()[0].ore==0&&
        noOre.find(worker)->futureOrders.empty()&&
        noOre.alert().find("Insufficient ore")!=std::string::npos,
        "failed activation creates no foundation, charges nothing, alerts, and continues the tail");
  check(noOre.workerPlanNoticeSerial(0)==teamZeroSerial+1&&
        noOre.workerPlanNotice(0).find("Insufficient ore")!=std::string::npos&&
        noOre.workerPlanNoticeSerial(1)==teamOneSerial&&noOre.workerPlanNotice(1).empty(),
        "deferred failure increments only the owning player's durable notice serial");

  const std::string retainedTeamZeroNotice=noOre.workerPlanNotice(0);
  const Id teamOneWorker=noOre.debugSpawn(Kind::Worker,1,{4050,4050});
  noOre.debugResources(1,10000);
  step(noOre);
  const Vec2 teamOneSite=placement(noOre,Kind::Foundry,noOre.find(teamOneWorker)->pos,{},1);
  check(send(noOre,CommandType::Move,1,{teamOneWorker},{3950,4050}).accepted&&
        send(noOre,CommandType::Build,1,{teamOneWorker},teamOneSite,0,Kind::Foundry,
             CommandQueueMode::Append).accepted,
        "second owner accepts an independent deferred Build");
  noOre.debugSpawn(Kind::Resource,-1,teamOneSite);
  stepUntil(noOre,1000,[&] {
    return noOre.find(teamOneWorker)->order==Order::Idle&&
           noOre.find(teamOneWorker)->futureOrders.empty();
  },"second owner's blocked Build did not resolve");
  check(noOre.workerPlanNoticeSerial(1)==teamOneSerial+1&&
        noOre.workerPlanNotice(1).find("Blocked")!=std::string::npos&&
        noOre.workerPlanNoticeSerial(0)==teamZeroSerial+1&&
        noOre.workerPlanNotice(0)==retainedTeamZeroNotice,
        "each owner retains a separate notice and monotonic serial");
  const auto teamZeroView=net::snapshotFor(noOre,0);
  const auto teamOneView=net::snapshotFor(noOre,1);
  check(teamZeroView.workerPlanNotice==noOre.workerPlanNotice(0)&&
        teamZeroView.workerPlanNoticeSerial==noOre.workerPlanNoticeSerial(0)&&
        teamOneView.workerPlanNotice==noOre.workerPlanNotice(1)&&
        teamOneView.workerPlanNoticeSerial==noOre.workerPlanNoticeSerial(1)&&
        teamZeroView.workerPlanNotice!=teamOneView.workerPlanNotice,
        "private snapshots expose only the receiving owner's worker-plan notice");

  auto blocked=fixture();
  const Id blockedWorker=blocked.debugSpawn(Kind::Worker,0,{1050,3350});
  const Id fallbackOre=blocked.debugSpawn(Kind::Resource,-1,{1170,3350});
  step(blocked);
  const Vec2 blockedSite=placement(blocked,Kind::Foundry,blocked.find(blockedWorker)->pos);
  check(send(blocked,CommandType::Move,0,{blockedWorker},{1150,3350}).accepted&&
        send(blocked,CommandType::Build,0,{blockedWorker},blockedSite,0,Kind::Foundry,
             CommandQueueMode::Append).accepted&&
        send(blocked,CommandType::Gather,0,{blockedWorker},{},fallbackOre,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "blocked-site fixture accepts a currently legal future plan");
  const int retainedOre=blocked.players()[0].ore;
  const auto blockedNoticeSerial=blocked.workerPlanNoticeSerial(0);
  blocked.debugSpawn(Kind::Resource,-1,blockedSite);
  stepUntil(blocked,1000,[&] {
    return blocked.find(blockedWorker)->order==Order::Gather&&
           blocked.find(blockedWorker)->resourceTarget==fallbackOre;
  },"blocked future Build did not skip to Gather");
  check(living(blocked,0,Kind::Foundry).empty()&&blocked.players()[0].ore==retainedOre&&
        blocked.find(blockedWorker)->futureOrders.empty()&&
        blocked.alert().find("Blocked")!=std::string::npos&&
        blocked.workerPlanNoticeSerial(0)==blockedNoticeSerial+1&&
        blocked.workerPlanNotice(0).find("Blocked")!=std::string::npos,
        "activation rechecks placement, preserves ore, alerts, and continues the tail");
}

void clearStopAndReplacementControlConstructionPlans() {
  auto cleared=fixture();
  const Id worker=cleared.debugSpawn(Kind::Worker,0,{1050,3850});
  step(cleared);
  const Vec2 activeSite=placement(cleared,Kind::Foundry,cleared.find(worker)->pos);
  const Vec2 futureSite=placement(cleared,Kind::Processor,cleared.find(worker)->pos,{activeSite});
  const int initialOre=cleared.players()[0].ore;
  check(send(cleared,CommandType::Build,0,{worker},activeSite,0,Kind::Foundry).accepted,
        "Replace Build immediately creates and charges a foundation");
  const Id foundation=first(cleared,0,Kind::Foundry);
  check(foundation&&cleared.players()[0].ore==initialOre-definition(Kind::Foundry).cost&&
        send(cleared,CommandType::Build,0,{worker},futureSite,0,Kind::Processor,
             CommandQueueMode::Append).accepted,
        "active builder accepts one unpaid future Build");
  check(living(cleared,0,Kind::Processor).empty()&&
        cleared.players()[0].ore==initialOre-definition(Kind::Foundry).cost,
        "append preserves the active build without charging the successor");
  check(send(cleared,CommandType::ClearOrders,0,{worker}).accepted&&
        cleared.find(worker)->order==Order::Construct&&cleared.find(worker)->target==foundation&&
        cleared.find(worker)->futureOrders.empty()&&cleared.constructionWorker(foundation)==worker,
        "ClearOrders removes only future construction work");
  check(send(cleared,CommandType::Stop,0,{worker}).accepted&&
        cleared.find(worker)->order==Order::Idle&&cleared.constructionWorker(foundation)==0,
        "Stop clears the plan and pauses the paid foundation");
  const float paused=cleared.find(foundation)->progress;
  step(cleared,20);
  check(cleared.find(foundation)->progress==paused&&living(cleared,0,Kind::Processor).empty(),
        "stopped construction and its discarded successor remain paused");

  auto replaced=fixture();
  const Id replacedWorker=replaced.debugSpawn(Kind::Worker,0,{1050,4200});
  step(replaced);
  const Vec2 replacedSite=placement(replaced,Kind::Foundry,replaced.find(replacedWorker)->pos);
  const Vec2 discardedSite=placement(replaced,Kind::Processor,replaced.find(replacedWorker)->pos,
                                     {replacedSite});
  check(send(replaced,CommandType::Build,0,{replacedWorker},replacedSite,0,Kind::Foundry).accepted&&
        send(replaced,CommandType::Build,0,{replacedWorker},discardedSite,0,Kind::Processor,
             CommandQueueMode::Append).accepted&&
        send(replaced,CommandType::Move,0,{replacedWorker},{900,4050}).accepted,
        "ordinary replacement interrupts a builder with a future Build");
  const Id replacedFoundation=first(replaced,0,Kind::Foundry);
  check(replaced.find(replacedWorker)->order==Order::Move&&
        replaced.find(replacedWorker)->futureOrders.empty()&&
        replaced.constructionWorker(replacedFoundation)==0&&living(replaced,0,Kind::Processor).empty(),
        "replacement clears the tail and pauses only the already-paid foundation");

  const auto autoHash=replaced.stateHash();
  const auto autoRecording=replaced.recording().size();
  check(!send(replaced,CommandType::AutoBuild,0,{},discardedSite,0,Kind::Processor,
              CommandQueueMode::Append).accepted&&replaced.stateHash()==autoHash&&
        replaced.recording().size()==autoRecording,
        "AutoBuild remains Replace-only and rejects Append atomically");
}

void cancelledDestroyedAndDeadBuildersResolvePlansOnce() {
  auto cancelled=fixture();
  const Id cancelWorker=cancelled.debugSpawn(Kind::Worker,0,{1100,1200});
  step(cancelled);
  const Vec2 cancelSite=placement(cancelled,Kind::Foundry,cancelled.find(cancelWorker)->pos);
  const Vec2 cancelTail=placement(cancelled,Kind::Processor,cancelled.find(cancelWorker)->pos,
                                  {cancelSite});
  check(send(cancelled,CommandType::Build,0,{cancelWorker},cancelSite,0,Kind::Foundry).accepted&&
        send(cancelled,CommandType::Build,0,{cancelWorker},cancelTail,0,Kind::Processor,
             CommandQueueMode::Append).accepted,"cancellation fixture has one paid and one future build");
  const Id cancelledFoundation=first(cancelled,0,Kind::Foundry);
  check(send(cancelled,CommandType::CancelBuilding,0,{cancelledFoundation}).accepted,
        "owner cancels the active paid foundation");
  const Id afterCancel=first(cancelled,0,Kind::Processor);
  check(afterCancel&&cancelled.find(cancelWorker)->order==Order::Construct&&
        cancelled.find(cancelWorker)->target==afterCancel&&
        cancelled.constructionWorker(afterCancel)==cancelWorker&&
        cancelled.find(cancelWorker)->futureOrders.empty()&&
        living(cancelled,0,Kind::Processor).size()==1,
        "cancellation advances exactly one queued Build");

  auto destroyed=fixture();
  const Id destroyWorker=destroyed.debugSpawn(Kind::Worker,0,{1100,2000});
  step(destroyed);
  const Vec2 destroySite=placement(destroyed,Kind::Foundry,destroyed.find(destroyWorker)->pos);
  const Vec2 destroyTail=placement(destroyed,Kind::Processor,destroyed.find(destroyWorker)->pos,
                                   {destroySite});
  check(send(destroyed,CommandType::Build,0,{destroyWorker},destroySite,0,Kind::Foundry).accepted&&
        send(destroyed,CommandType::Build,0,{destroyWorker},destroyTail,0,Kind::Processor,
             CommandQueueMode::Append).accepted,"destruction fixture has one paid and one future build");
  const Id doomed=first(destroyed,0,Kind::Foundry);
  edit(destroyed,doomed)->hp=1;
  const Id attacker=destroyed.debugSpawn(Kind::Bastion,1,
                                         {destroyed.find(doomed)->pos.x+220,destroyed.find(doomed)->pos.y});
  check(send(destroyed,CommandType::Attack,1,{attacker},{},doomed).accepted,
        "enemy receives an ordinary attack against the active foundation");
  stepUntil(destroyed,80,[&]{return !destroyed.find(doomed)->alive();},
            "ordinary combat did not destroy the active foundation");
  const Id afterDestroy=first(destroyed,0,Kind::Processor);
  check(afterDestroy&&destroyed.find(destroyWorker)->order==Order::Construct&&
        destroyed.find(destroyWorker)->target==afterDestroy&&
        destroyed.constructionWorker(afterDestroy)==destroyWorker&&
        destroyed.find(destroyWorker)->futureOrders.empty()&&
        living(destroyed,0,Kind::Processor).size()==1,
        "foundation destruction advances exactly one queued Build");

  auto killed=fixture();
  const Id deadWorker=killed.debugSpawn(Kind::Worker,0,{1100,2800});
  step(killed);
  const Vec2 deadSite=placement(killed,Kind::Foundry,killed.find(deadWorker)->pos);
  const Vec2 deadTail=placement(killed,Kind::Processor,killed.find(deadWorker)->pos,{deadSite});
  check(send(killed,CommandType::Build,0,{deadWorker},deadSite,0,Kind::Foundry).accepted&&
        send(killed,CommandType::Build,0,{deadWorker},deadTail,0,Kind::Processor,
             CommandQueueMode::Append).accepted,"death fixture has one paid and one future build");
  const Id orphaned=first(killed,0,Kind::Foundry);
  edit(killed,deadWorker)->hp=1;
  const Vec2 workerPosition=killed.find(deadWorker)->pos;
  const Id killer=killed.debugSpawn(Kind::Bastion,1,{workerPosition.x+180,workerPosition.y});
  check(send(killed,CommandType::Attack,1,{killer},{},deadWorker).accepted,
        "enemy receives an ordinary attack against the planned builder");
  stepUntil(killed,80,[&]{return !killed.find(deadWorker)->alive();},
            "ordinary combat did not kill the planned builder");
  const Entity* dead=killed.find(deadWorker);
  check(dead->order==Order::Idle&&dead->futureOrders.empty()&&
        killed.constructionWorker(orphaned)==0&&living(killed,0,Kind::Processor).empty(),
        "dead Drudge discards its plan instead of transferring or activating it");
  const float orphanedProgress=killed.find(orphaned)->progress;
  step(killed,20);
  check(killed.find(orphaned)->progress==orphanedProgress&&
        living(killed,0,Kind::Processor).empty(),
        "dead-worker cleanup leaves the paid foundation paused and never advances later");
}

struct PersistedPlan {
  Simulation simulation;
  Id donor=0,worker=0,ore=0,foundation=0;
  Vec2 buildSite{};
};

PersistedPlan persistenceBase() {
  PersistedPlan result{fixture()};
  result.donor=result.simulation.debugSpawn(Kind::Worker,0,{1050,3550});
  result.worker=result.simulation.debugSpawn(Kind::Worker,0,{1050,3800});
  result.ore=result.simulation.debugSpawn(Kind::Resource,-1,{1180,3800});
  step(result.simulation);
  const Vec2 foundationSite=placement(result.simulation,Kind::Foundry,
                                      result.simulation.find(result.donor)->pos);
  result.buildSite=placement(result.simulation,Kind::Processor,
                             result.simulation.find(result.worker)->pos,{foundationSite});
  check(send(result.simulation,CommandType::Build,0,{result.donor},foundationSite,0,
             Kind::Foundry).accepted,"persistence base creates a paid foundation");
  result.foundation=first(result.simulation,0,Kind::Foundry);
  check(result.foundation&&send(result.simulation,CommandType::Stop,0,{result.donor}).accepted,
        "persistence base pauses the foundation");
  return result;
}

void saveLoadHashAndReplayPreserveWorkerPlans() {
  auto source=persistenceBase();
  check(send(source.simulation,CommandType::Hold,0,{source.worker}).accepted&&
        send(source.simulation,CommandType::Build,0,{source.worker},source.buildSite,0,
             Kind::Processor,CommandQueueMode::Append).accepted&&
        send(source.simulation,CommandType::ResumeConstruction,0,{source.worker},{},
             source.foundation,Kind::Worker,CommandQueueMode::Append).accepted&&
        send(source.simulation,CommandType::Gather,0,{source.worker},{},source.ore,
             Kind::Worker,CommandQueueMode::Append).accepted,
        "persistence fixture records Build, Resume, and Gather work");
  const Entity* planned=source.simulation.find(source.worker);
  check(planned->order==Order::Hold&&planned->futureOrders.size()==3,
        "persistence fixture holds a three-step worker plan");
  checkBuildStep(planned->futureOrders[0],source.buildSite,Kind::Processor,"saved Build");
  checkResumeStep(planned->futureOrders[1],source.foundation,"saved Resume");
  checkGatherStep(planned->futureOrders[2],source.ore,"saved Gather");

  const auto path=std::filesystem::temp_directory_path()/"cinderline-builder-queue-v14.sav";
  check(source.simulation.save(path.string()),"worker plan saves in version fourteen");
  Simulation loaded;
  check(loaded.load(path.string()),"version-fourteen worker plan loads");
  std::filesystem::remove(path);
  const Entity* loadedWorker=loaded.find(source.worker);
  check(loaded.stateHash()==source.simulation.stateHash()&&loadedWorker&&
        loadedWorker->futureOrders.size()==3,
        "save-load preserves the exact authoritative hash and plan length");
  checkBuildStep(loadedWorker->futureOrders[0],source.buildSite,Kind::Processor,"loaded Build");
  checkResumeStep(loadedWorker->futureOrders[1],source.foundation,"loaded Resume");
  checkGatherStep(loadedWorker->futureOrders[2],source.ore,"loaded Gather");

  auto replay=persistenceBase();
  // persistenceBase already issued the first Build and Stop at this same tick.
  const auto& recording=source.simulation.recording();
  const std::size_t prefix=replay.simulation.recording().size();
  check(prefix==2&&recording.size()==6,
        "replay fixture isolates Hold plus the three queued worker commands");
  for(std::size_t index=prefix;index<recording.size();++index) {
    check(recording[index].tick==replay.simulation.tick()&&
          replay.simulation.command(recording[index].command).accepted,
          "recorded worker-plan command replays at its original tick");
  }
  check(replay.simulation.stateHash()==source.simulation.stateHash(),
        "recording replay reproduces the exact queued-work hash");
}

void queueLimitsAreAtomicAndFitSnapshots() {
  auto perWorker=fixture();
  const Id worker=perWorker.debugSpawn(Kind::Worker,0,{1000,1100});
  step(perWorker);
  const Vec2 site=placement(perWorker,Kind::Foundry,perWorker.find(worker)->pos);
  check(send(perWorker,CommandType::Hold,0,{worker}).accepted,
        "per-Drudge limit fixture starts on an indefinite order");
  for(std::size_t index=0;index<Simulation::MaxFutureOrders;++index)
    check(send(perWorker,CommandType::Build,0,{worker},site,0,Kind::Foundry,
               CommandQueueMode::Append).accepted,
          "per-Drudge queue accepts every entry through its exact limit");
  check(perWorker.find(worker)->futureOrders.size()==Simulation::MaxFutureOrders,
        "per-Drudge builder queue reaches sixteen entries");
  const auto fullHash=perWorker.stateHash();
  const auto fullRecording=perWorker.recording().size();
  check(!send(perWorker,CommandType::Build,0,{worker},site,0,Kind::Foundry,
              CommandQueueMode::Append).accepted&&perWorker.stateHash()==fullHash&&
        perWorker.recording().size()==fullRecording,
        "seventeenth queued Build rejects without partial state or recording changes");

  auto aggregate=fixture();
  const Id ore=aggregate.debugSpawn(Kind::Resource,-1,{900,900});
  std::vector<Id> filled;
  filled.reserve(Simulation::MaxFutureOrdersPerPlayer/Simulation::MaxFutureOrders);
  for(std::size_t index=0;
      index<Simulation::MaxFutureOrdersPerPlayer/Simulation::MaxFutureOrders;++index) {
    const float x=1000.0f+static_cast<float>(index%16)*20.0f;
    const float y=1200.0f+static_cast<float>(index/16)*20.0f;
    filled.push_back(aggregate.debugSpawn(Kind::Worker,0,{x,y}));
  }
  const Id overflow=aggregate.debugSpawn(Kind::Worker,0,{1400,1700});
  step(aggregate);
  std::vector<Id> all=filled;all.push_back(overflow);
  check(send(aggregate,CommandType::Hold,0,all).accepted,
        "aggregate fixture holds every selected Drudge");
  for(std::size_t index=0;index<Simulation::MaxFutureOrders;++index)
    check(send(aggregate,CommandType::Gather,0,filled,{},ore,Kind::Worker,
               CommandQueueMode::Append).accepted,
          "multi-selection Gather fills the player-wide queue through its exact limit");
  std::size_t total=0;
  for(Id id:filled) {
    const Entity* state=aggregate.find(id);
    total+=state->futureOrders.size();
    check(state->futureOrders.size()==Simulation::MaxFutureOrders,
          "each selected Drudge receives one Gather entry per append");
  }
  check(total==Simulation::MaxFutureOrdersPerPlayer,
        "public commands reach the exact 4096-entry aggregate limit");

  const auto bytes=net::encodeSnapshot(net::snapshotFor(aggregate,0));
  net::Snapshot decoded;std::string error;
  check(!bytes.empty()&&bytes.size()<=net::MaxMessageBytes&&
        net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error),
        "a maximum valid queued-work plan fits and round trips through one snapshot");
  std::size_t decodedFuture=0;
  for(const auto& entity:decoded.entities)if(entity.team==0) {
    decodedFuture+=entity.futureOrders.size();
    for(const auto& order:entity.futureOrders)
      check(order.order==Order::Gather&&order.supportTarget!=0&&
            order.buildingKind==Kind::Worker,
            "snapshot retains the canonical queued-Gather payload");
  }
  check(decodedFuture==Simulation::MaxFutureOrdersPerPlayer,
        "snapshot round trip retains all 4096 owned worker entries");

  const auto aggregateHash=aggregate.stateHash();
  const auto aggregateRecording=aggregate.recording().size();
  check(!send(aggregate,CommandType::Gather,0,{overflow},{},ore,Kind::Worker,
              CommandQueueMode::Append).accepted&&aggregate.stateHash()==aggregateHash&&
        aggregate.recording().size()==aggregateRecording&&
        aggregate.find(overflow)->futureOrders.empty(),
        "player-wide overflow rejects atomically without touching a recipient that has room");
}

bool pendingWorkerJob(const Entity& entity) {
  return entity.order==Order::Idle&&!entity.futureOrders.empty()&&
         (entity.futureOrders.front().order==Order::Construct||
          entity.futureOrders.front().order==Order::Gather);
}

void sameBoundaryWorkerJobsRespectTheFourAttemptBudget() {
  auto simulation=fixture();
  std::vector<Id> workers;
  for(int index=0;index<12;++index) {
    const float x=980.0f+static_cast<float>(index%4)*70.0f;
    const float y=1180.0f+static_cast<float>(index/4)*70.0f;
    workers.push_back(simulation.debugSpawn(Kind::Worker,0,{x,y}));
  }
  const Id ore=simulation.debugSpawn(Kind::Resource,-1,{1450,1450});
  std::vector<Id> resumeFoundations;
  for(int index=0;index<4;++index) {
    const Id foundation=simulation.debugSpawn(
      Kind::Foundry,0,{1050.0f+static_cast<float>(index)*280.0f,3150});
    edit(simulation,foundation)->progress=0.5f;
    edit(simulation,foundation)->builderId=0;
    resumeFoundations.push_back(foundation);
  }
  step(simulation);

  std::vector<Vec2> buildSites;
  for(int index=0;index<4;++index)
    buildSites.push_back(placement(simulation,Kind::Processor,{1000,1000},buildSites));
  const Vec2 staging{3000,2400};
  check(send(simulation,CommandType::Move,0,workers,staging).accepted,
        "budget fixture gives every Drudge a shared movement boundary");
  int buildIndex=0,resumeIndex=0;
  for(std::size_t index=0;index<workers.size();++index) {
    CommandResult queued;
    if(index%3==0) {
      queued=send(simulation,CommandType::Build,0,{workers[index]},buildSites[buildIndex++],0,
                  Kind::Processor,CommandQueueMode::Append);
    } else if(index%3==1) {
      queued=send(simulation,CommandType::ResumeConstruction,0,{workers[index]},{},
                  resumeFoundations[resumeIndex++],Kind::Worker,CommandQueueMode::Append);
    } else {
      queued=send(simulation,CommandType::Gather,0,{workers[index]},{},ore,
                  Kind::Worker,CommandQueueMode::Append);
    }
    check(queued.accepted,"each mixed worker job queues behind the shared movement boundary");
  }
  check(buildIndex==4&&resumeIndex==4,
        "budget fixture contains four Builds, four Resumes, and four Gathers");

  for(Id id:workers) {
    Entity* worker=edit(simulation,id);
    worker->pos=worker->goal;
    worker->path.clear();worker->pathIndex=0;worker->repath=0;
  }
  step(simulation);
  std::size_t pending=0,started=0;
  for(Id id:workers) {
    if(pendingWorkerJob(*simulation.find(id)))++pending;
    else if(simulation.find(id)->order==Order::Construct||
            simulation.find(id)->order==Order::Gather)++started;
  }
  check(started==4&&pending==8,
        "one fixed step starts exactly four mixed worker jobs and persists eight idle fronts");

  const auto path=std::filesystem::temp_directory_path()/
    "cinderline-builder-queue-budget-pending-v14.sav";
  check(simulation.save(path.string()),"budget-deferred idle worker tails save");
  Simulation loaded;
  check(loaded.load(path.string()),"budget-deferred idle worker tails load");
  std::filesystem::remove(path);
  check(loaded.stateHash()==simulation.stateHash(),
        "save-load preserves the exact pending-budget hash");
  std::size_t loadedPending=0;
  for(Id id:workers)if(pendingWorkerJob(*loaded.find(id)))++loadedPending;
  check(loadedPending==pending,
        "save-load retains every idle worker whose front job awaits budget");

  const auto bytes=net::encodeSnapshot(net::snapshotFor(simulation,0));
  net::Snapshot decoded;std::string error;
  check(!bytes.empty()&&net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error),
        "pending-budget snapshot round trips");
  std::size_t snapshotPending=0;
  for(const auto& entity:decoded.entities)if(entity.team==0&&pendingWorkerJob(entity))
    ++snapshotPending;
  check(snapshotPending==pending,
        "snapshot retains all budget-deferred idle worker tails");

  std::size_t previousStarted=started;
  for(int boundary=0;boundary<2;++boundary) {
    step(simulation);step(loaded);
    std::size_t nowStarted=0,nowPending=0;
    for(Id id:workers) {
      const Entity* worker=simulation.find(id);
      if(pendingWorkerJob(*worker))++nowPending;
      else if(worker->order==Order::Construct||worker->order==Order::Gather)++nowStarted;
    }
    check(nowStarted-previousStarted<=4&&nowStarted==previousStarted+4&&
          nowPending==workers.size()-nowStarted,
          "each later fixed step starts at most four additional worker jobs");
    check(loaded.stateHash()==simulation.stateHash(),
          "loaded pending scheduler advances deterministically at each boundary");
    previousStarted=nowStarted;
  }
  check(previousStarted==workers.size(),
        "three fixed-step boundaries eventually activate every mixed worker job");

  std::vector<Id> buildFoundations(workers.size());
  for(std::size_t index=0;index<workers.size();++index)if(index%3==0) {
    buildFoundations[index]=simulation.find(workers[index])->target;
    check(buildFoundations[index]!=0&&
          loaded.find(workers[index])->target==buildFoundations[index],
          "each activated queued Build has a deterministic foundation identity");
  }
  std::vector<bool> sourceGatherProgress(workers.size());
  std::vector<bool> loadedGatherProgress(workers.size());
  auto observeGather=[&](const Simulation& value,std::vector<bool>& observed) {
    for(std::size_t index=0;index<workers.size();++index)if(index%3==2) {
      const Entity* worker=value.find(workers[index]);
      if(worker&&worker->carried>0)observed[index]=true;
    }
  };
  auto progressed=[&](const Simulation& value,const std::vector<bool>& gatherProgress) {
    int build=0,resume=0,gather=0;
    for(std::size_t index=0;index<workers.size();++index) {
      if(index%3==0) {
        const Entity* foundation=value.find(buildFoundations[index]);
        if(foundation&&foundation->kind==Kind::Processor&&foundation->progress>0)++build;
      } else if(index%3==1) {
        const Entity* foundation=value.find(resumeFoundations[index/3]);
        if(foundation&&foundation->progress>0.5f)++resume;
      } else if(gatherProgress[index])++gather;
    }
    return build==4&&resume==4&&gather==4;
  };
  for(int index=0;index<2400&&
      (!progressed(simulation,sourceGatherProgress)||
       !progressed(loaded,loadedGatherProgress));++index) {
    step(simulation);step(loaded);
    observeGather(simulation,sourceGatherProgress);
    observeGather(loaded,loadedGatherProgress);
    check(loaded.stateHash()==simulation.stateHash(),
          "source and loaded mixed jobs remain deterministic while doing real work");
  }
  check(progressed(simulation,sourceGatherProgress)&&
        progressed(loaded,loadedGatherProgress),
        "every budgeted Build, Resume, and Gather makes ordinary simulation progress");
}

void invalidWorkerTailDoesNotDrainPastOneBoundaryBudget() {
  auto simulation=fixture();
  const Id worker=simulation.debugSpawn(Kind::Worker,0,{1050,1900});
  const Id ore=simulation.debugSpawn(Kind::Resource,-1,{1180,1900});
  step(simulation);
  const Vec2 site=placement(simulation,Kind::Foundry,simulation.find(worker)->pos);
  check(send(simulation,CommandType::Move,0,{worker},{1450,2100}).accepted,
        "invalid-tail fixture begins on a finite movement order");
  for(int index=0;index<8;++index)
    check(send(simulation,CommandType::Build,0,{worker},site,0,Kind::Foundry,
               CommandQueueMode::Append).accepted,
          "currently valid repeated Build enters the invalidation tail");
  check(send(simulation,CommandType::Gather,0,{worker},{},ore,Kind::Worker,
             CommandQueueMode::Append).accepted,
        "valid Gather follows eight deferred Builds");
  simulation.debugSpawn(Kind::Resource,-1,site);
  Entity* moving=edit(simulation,worker);
  moving->pos=moving->goal;moving->path.clear();moving->pathIndex=0;moving->repath=0;

  step(simulation);
  const Entity* afterFirst=simulation.find(worker);
  check(pendingWorkerJob(*afterFirst)&&afterFirst->futureOrders.size()==5&&
        living(simulation,0,Kind::Foundry).empty()&&
        simulation.workerPlanNoticeSerial(0)==1&&
        simulation.workerPlanNotice(0).find("Skipped 4 queued worker jobs")!=std::string::npos,
        "one boundary consumes four invalid Builds and leaves the remaining invalid tail pending");
  step(simulation);
  const Entity* afterSecond=simulation.find(worker);
  check(pendingWorkerJob(*afterSecond)&&afterSecond->futureOrders.size()==1&&
        afterSecond->futureOrders.front().order==Order::Gather&&
        simulation.workerPlanNoticeSerial(0)==2,
        "second boundary consumes only the next four invalid Builds");
  step(simulation);
  const Entity* gathered=simulation.find(worker);
  check(gathered->order==Order::Gather&&gathered->resourceTarget==ore&&
        gathered->futureOrders.empty()&&simulation.workerPlanNoticeSerial(0)==2,
        "third boundary activates the valid tail without another failure notice");
}

void appendBehindBudgetPendingWorkerPreservesItsHead() {
  enum class Tail { Move,Build,Gather };
  for(Tail tail:{Tail::Move,Tail::Build,Tail::Gather}) {
    auto simulation=fixture();
    std::vector<Id> workers;
    for(int index=0;index<9;++index)
      workers.push_back(simulation.debugSpawn(
        Kind::Worker,0,{980.0f+static_cast<float>(index%5)*55.0f,
                        1200.0f+static_cast<float>(index/5)*70.0f}));
    const Id headOre=simulation.debugSpawn(Kind::Resource,-1,{1420,1450});
    const Id tailOre=simulation.debugSpawn(Kind::Resource,-1,{1540,1450});
    step(simulation);
    check(send(simulation,CommandType::Move,0,workers,{3000,2350}).accepted,
          "pending-append fixture gives every worker a finite current Move");
    for(Id id:workers)
      check(send(simulation,CommandType::Gather,0,{id},{},headOre,Kind::Worker,
                 CommandQueueMode::Append).accepted,
            "pending-append fixture queues the original Gather head");
    for(Id id:workers) {
      Entity* worker=edit(simulation,id);
      worker->pos=worker->goal;worker->path.clear();worker->pathIndex=0;worker->repath=0;
    }
    step(simulation);
    const Id subject=workers.back();
    const Entity* pending=simulation.find(subject);
    check(pendingWorkerJob(*pending)&&pending->futureOrders.size()==1,
          "four unrelated candidates precede the first remaining pending subject");
    const TacticalOrder originalHead=pending->futureOrders.front();
    checkGatherStep(originalHead,headOre,"budget-pending head");

    if(tail==Tail::Move) {
      Entity* resumable=edit(simulation,subject);
      resumable->resumeGather=true;resumable->resourceTarget=headOre;
      const auto path=std::filesystem::temp_directory_path()/
        "cinderline-builder-queue-idle-pending-resume-v14.sav";
      check(simulation.save(path.string()),
            "idle pending front with mining recovery saves in version fourteen");
      Simulation loaded;
      check(loaded.load(path.string()),
            "idle pending front with mining recovery loads in version fourteen");
      std::filesystem::remove(path);
      const Entity* loadedSubject=loaded.find(subject);
      check(loaded.stateHash()==simulation.stateHash()&&loadedSubject->order==Order::Idle&&
            loadedSubject->resumeGather&&loadedSubject->resourceTarget==headOre&&
            loadedSubject->futureOrders.size()==1,
            "load preserves Idle, resumeGather, and the exact pending front without activating it");
      checkGatherStep(loadedSubject->futureOrders.front(),headOre,
                      "loaded budget-pending head");
    }

    const std::size_t beforeRecording=simulation.recording().size();
    const int beforeOre=simulation.players()[0].ore;
    CommandResult appended;
    Vec2 buildSite{};
    if(tail==Tail::Move) {
      appended=send(simulation,CommandType::Move,0,{subject},{2700,2100},0,
                    Kind::Worker,CommandQueueMode::Append);
    } else if(tail==Tail::Build) {
      buildSite=placement(simulation,Kind::Foundry,{1000,1000});
      appended=send(simulation,CommandType::Build,0,{subject},buildSite,0,
                    Kind::Foundry,CommandQueueMode::Append);
    } else {
      appended=send(simulation,CommandType::Gather,0,{subject},{},tailOre,
                    Kind::Worker,CommandQueueMode::Append);
    }
    check(appended.accepted&&simulation.recording().size()==beforeRecording+1,
          "Append accepts a new tail behind an Idle budget-pending worker");
    const Entity* after=simulation.find(subject);
    check(after->order==Order::Idle&&after->futureOrders.size()==2&&
          after->futureOrders.front().order==originalHead.order&&
          after->futureOrders.front().supportTarget==originalHead.supportTarget&&
          after->futureOrders.front().buildingKind==originalHead.buildingKind&&
          distance(after->futureOrders.front().point,originalHead.point)<0.01f&&
          after->futureOrders.front().hasArrivalFacing==originalHead.hasArrivalFacing&&
          after->futureOrders.front().arrivalFacing==originalHead.arrivalFacing,
          "Append preserves the pending head and adds exactly one tail entry");
    if(tail==Tail::Move)
      check(after->futureOrders.back().order==Order::Move,
            "Move remains behind the earlier pending Gather");
    else if(tail==Tail::Build) {
      checkBuildStep(after->futureOrders.back(),buildSite,Kind::Foundry,
                     "Build behind pending Gather");
      check(simulation.players()[0].ore==beforeOre&&
            living(simulation,0,Kind::Foundry).empty(),
            "Build appended behind a pending head remains unpaid and unspawned");
    } else checkGatherStep(after->futureOrders.back(),tailOre,
                           "Gather behind pending Gather");
  }
}

void fullIdlePendingWorkerRejectsAppendAtomically() {
  auto simulation=fixture();
  std::vector<Id> blockers;
  for(int index=0;index<5;++index)
    blockers.push_back(simulation.debugSpawn(
      Kind::Worker,0,{980.0f+static_cast<float>(index)*55.0f,1700}));
  const Id subject=simulation.debugSpawn(Kind::Worker,0,{1250,1800});
  const Id ore=simulation.debugSpawn(Kind::Resource,-1,{1450,1850});
  step(simulation);
  std::vector<Id> all=blockers;all.push_back(subject);
  check(send(simulation,CommandType::Move,0,all,{3000,2700}).accepted,
        "full pending fixture gives every worker a finite current Move");
  for(Id blocker:blockers)
    check(send(simulation,CommandType::Gather,0,{blocker},{},ore,Kind::Worker,
               CommandQueueMode::Append).accepted,
          "scheduler blocker receives one queued Gather");
  for(std::size_t index=0;index<Simulation::MaxFutureOrders;++index)
    check(send(simulation,CommandType::Gather,0,{subject},{},ore,Kind::Worker,
               CommandQueueMode::Append).accepted,
          "subject fills all sixteen future slots while still moving");
  for(Id id:all) {
    Entity* worker=edit(simulation,id);
    worker->pos=worker->goal;worker->path.clear();worker->pathIndex=0;worker->repath=0;
  }
  step(simulation);
  const Entity* pending=simulation.find(subject);
  check(pendingWorkerJob(*pending)&&pending->futureOrders.size()==Simulation::MaxFutureOrders&&
        pendingWorkerJob(*simulation.find(blockers.back())),
        "four attempts leave both a blocker and the full subject Idle and pending");
  const TacticalOrder head=pending->futureOrders.front();
  const auto hash=simulation.stateHash();
  const auto recording=simulation.recording().size();
  const auto noticeSerial=simulation.workerPlanNoticeSerial(0);
  const std::size_t otherPending=simulation.find(blockers.back())->futureOrders.size();
  check(!send(simulation,CommandType::Move,0,{subject},{2600,2500},0,Kind::Worker,
              CommandQueueMode::Append).accepted,
        "seventeenth Append rejects while the full worker is Idle pending budget");
  pending=simulation.find(subject);
  check(simulation.stateHash()==hash&&simulation.recording().size()==recording&&
        simulation.workerPlanNoticeSerial(0)==noticeSerial&&
        pending->order==Order::Idle&&pending->futureOrders.size()==Simulation::MaxFutureOrders&&
        pending->futureOrders.front().order==head.order&&
        pending->futureOrders.front().supportTarget==head.supportTarget&&
        simulation.find(blockers.back())->futureOrders.size()==otherPending,
        "full pending rejection preserves queue count, head, scheduler counter, recording, and hash");
}

void clearNaturallyPendingWorkerRestoresMiningAndStopCancelsIt() {
  for(Order pendingHead:{Order::Construct,Order::Gather}) {
    auto simulation=fixture();
    std::vector<Id> blockers;
    for(int index=0;index<4;++index)
      blockers.push_back(simulation.debugSpawn(
        Kind::Worker,0,{980.0f+static_cast<float>(index)*55.0f,1300}));
    const Id subject=simulation.debugSpawn(Kind::Worker,0,{1150,1750});
    const Id originalOre=simulation.debugSpawn(Kind::Resource,-1,{1260,1750});
    const Id queuedOre=simulation.debugSpawn(Kind::Resource,-1,{1420,1810});
    step(simulation);

    check(send(simulation,CommandType::Move,0,blockers,{3000,2450}).accepted,
          "clear-pending fixture gives four earlier workers a finite boundary");
    for(Id blocker:blockers)
      check(send(simulation,CommandType::Gather,0,{blocker},{},queuedOre,Kind::Worker,
                 CommandQueueMode::Append).accepted,
            "each earlier worker queues one scheduler attempt");

    const Vec2 activeSite=placement(simulation,Kind::Foundry,simulation.find(subject)->pos);
    check(send(simulation,CommandType::Gather,0,{subject},{},originalOre).accepted&&
          send(simulation,CommandType::Build,0,{subject},activeSite,0,Kind::Foundry).accepted,
          "subject starts construction while retaining its original ore target");
    const Id activeFoundation=first(simulation,0,Kind::Foundry);
    Vec2 queuedSite{};
    if(pendingHead==Order::Construct) {
      queuedSite=placement(simulation,Kind::Processor,simulation.find(subject)->pos,{activeSite});
      check(send(simulation,CommandType::Build,0,{subject},queuedSite,0,Kind::Processor,
                 CommandQueueMode::Append).accepted,
            "subject queues a Build after its active foundation");
    } else {
      check(send(simulation,CommandType::Gather,0,{subject},{},queuedOre,Kind::Worker,
                 CommandQueueMode::Append).accepted,
            "subject queues a Gather after its active foundation");
    }
    check(simulation.find(subject)->resumeGather&&
          simulation.find(subject)->resourceTarget==originalOre,
          "active construction naturally remembers the original mining job");

    Entity* foundation=edit(simulation,activeFoundation);
    Entity* worker=edit(simulation,subject);
    foundation->progress=0.9999f;
    const float workingDistance=definition(Kind::Foundry).radius+
      definition(Kind::Worker).radius+15.0f;
    worker->pos={foundation->pos.x+workingDistance,foundation->pos.y};
    worker->goal=foundation->pos;worker->path.clear();worker->pathIndex=0;worker->repath=0;
    check(simulation.constructionActive(activeFoundation),
          "subject is positioned to complete its active foundation this step");
    for(Id blocker:blockers) {
      Entity* state=edit(simulation,blocker);
      state->pos=state->goal;state->path.clear();state->pathIndex=0;state->repath=0;
    }

    step(simulation);
    const Entity* pending=simulation.find(subject);
    check(simulation.find(activeFoundation)->progress>=1&&pendingWorkerJob(*pending)&&
          pending->futureOrders.size()==1&&pending->futureOrders.front().order==pendingHead&&
          pending->resumeGather&&pending->resourceTarget==originalOre,
          "four earlier attempts naturally leave the completed builder Idle with mining recovery pending");
    if(pendingHead==Order::Construct)
      checkBuildStep(pending->futureOrders.front(),queuedSite,Kind::Processor,
                     "naturally pending Build head");
    else checkGatherStep(pending->futureOrders.front(),queuedOre,
                         "naturally pending Gather head");

    const auto path=std::filesystem::temp_directory_path()/
      (pendingHead==Order::Construct?
       "cinderline-builder-queue-clear-pending-build-v14.sav":
       "cinderline-builder-queue-clear-pending-gather-v14.sav");
    check(simulation.save(path.string()),
          "natural Idle pending state with mining recovery saves");
    Simulation loaded;
    check(loaded.load(path.string()),
          "natural Idle pending state with mining recovery loads");
    std::filesystem::remove(path);
    const Entity* loadedPending=loaded.find(subject);
    check(loaded.stateHash()==simulation.stateHash()&&pendingWorkerJob(*loadedPending)&&
          loadedPending->futureOrders.front().order==pendingHead&&loadedPending->resumeGather&&
          loadedPending->resourceTarget==originalOre,
          "save round trip preserves the exact natural pending state without activating it");

    const auto bytes=net::encodeSnapshot(net::snapshotFor(simulation,0));
    net::Snapshot decoded;std::string error;
    check(!bytes.empty()&&net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error),
          "natural pending state round trips through a snapshot");
    const auto projected=std::find_if(decoded.entities.begin(),decoded.entities.end(),
      [&](const Entity& entity){return entity.id==subject;});
    check(projected!=decoded.entities.end()&&pendingWorkerJob(*projected)&&
          projected->futureOrders.front().order==pendingHead&&projected->resumeGather&&
          projected->resourceTarget==originalOre,
          "snapshot retains Idle, queued head, and original mining recovery together");

    check(send(simulation,CommandType::ClearOrders,0,{subject}).accepted,
          "ClearOrders accepts a naturally budget-pending builder");
    const Entity* cleared=simulation.find(subject);
    check(cleared->futureOrders.empty()&&cleared->order==Order::Gather&&
          cleared->target==originalOre&&cleared->resourceTarget==originalOre&&
          !cleared->resumeGather,
          "ClearOrders removes the pending head and immediately restores original mining");
    check(send(simulation,CommandType::Stop,0,{subject}).accepted,
          "Stop accepts the restored miner");
    const Entity* stopped=simulation.find(subject);
    check(stopped->order==Order::Idle&&stopped->futureOrders.empty()&&!stopped->resumeGather,
          "Stop leaves the cleared worker Idle with mining recovery disabled");
  }
}

} // namespace

int main() {
  const std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"serial builds and original mining recovery",serialBuildsStartLazilyAndResumeOriginalMining},
    {"explicit Gather overrides mining recovery",explicitQueuedGatherOverridesMiningRecovery},
    {"intermediate Move preserves mining and save hash",intermediateMovePreservesMiningAcrossSaveLoad},
    {"queued Resume then Build",queuedResumeThenBuildCompletesSerially},
    {"activation failures skip invalid work",activationFailuresSkipOnlyInvalidWork},
    {"clear stop and replacement",clearStopAndReplacementControlConstructionPlans},
    {"cancel destroy and worker death",cancelledDestroyedAndDeadBuildersResolvePlansOnce},
    {"save load hash and replay",saveLoadHashAndReplayPreserveWorkerPlans},
    {"queue limits and snapshot fit",queueLimitsAreAtomicAndFitSnapshots},
    {"same-boundary mixed worker budget",sameBoundaryWorkerJobsRespectTheFourAttemptBudget},
    {"invalid tail boundary budget",invalidWorkerTailDoesNotDrainPastOneBoundaryBudget},
    {"append behind budget-pending worker",appendBehindBudgetPendingWorkerPreservesItsHead},
    {"full idle-pending append rejection",fullIdlePendingWorkerRejectsAppendAtomically},
    {"clear naturally pending worker",clearNaturallyPendingWorkerRestoresMiningAndStopCancelsIt},
  };
  int failed=0;
  for(const auto& [name,test]:tests) {
    try {test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error) {
      ++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';
    }
  }
  std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
