#include "Sim/Navigation.h"
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

constexpr float Pi = 3.14159265358979323846f;

void check(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}

float distance(Vec2 left,Vec2 right) {
    return std::hypot(left.x-right.x,left.y-right.y);
}

bool same(Vec2 left,Vec2 right,float tolerance=0.01f) {
    return distance(left,right)<=tolerance;
}

std::vector<Entity>& mutableEntities(Simulation& simulation) {
    return const_cast<std::vector<Entity>&>(simulation.entities());
}

std::vector<Obstacle>& mutableObstacles(Simulation& simulation) {
    return const_cast<std::vector<Obstacle>&>(simulation.obstacles());
}

Entity* edit(Simulation& simulation,Id id) {
    for(auto& entity:mutableEntities(simulation))if(entity.id==id)return &entity;
    return nullptr;
}

std::vector<Id> ids(const Simulation& simulation,int team,Kind kind) {
    std::vector<Id> result;
    for(const auto& entity:simulation.entities())
        if(entity.alive()&&entity.team==team&&entity.kind==kind)result.push_back(entity.id);
    return result;
}

CommandResult send(Simulation& simulation,CommandType type,int team,std::vector<Id> units,
                   Vec2 point={},Id target=0,Kind kind=Kind::Worker,int queueIndex=0) {
    return simulation.command({type,team,std::move(units),point,target,kind,queueIndex});
}

Simulation emptyFixture(Vec2 home={600,4200}) {
    Simulation simulation;
    Config config;config.ai=false;config.seed=0xE117u;
    simulation.reset(config);
    mutableEntities(simulation).clear();
    mutableObstacles(simulation).clear();
    simulation.debugSpawn(Kind::Headquarters,0,home);
    simulation.debugSpawn(Kind::Headquarters,1,{4200,600});
    simulation.debugResources(0,100000);
    return simulation;
}

Navigation navigationFor(const Simulation& simulation) {
    std::vector<NavBox> boxes;
    for(const auto& obstacle:simulation.obstacles())boxes.push_back({obstacle.center,obstacle.half});
    std::vector<NavCircle> circles;
    for(const auto& entity:simulation.entities()) {
        if(!entity.alive())continue;
        if(definition(entity.kind).building||(entity.kind==Kind::Resource&&entity.resource>0))
            circles.push_back({entity.id,entity.pos,definition(entity.kind).radius});
    }
    Navigation navigation;
    navigation.sync(simulation.worldSize(),boxes,circles);
    return navigation;
}

std::vector<Vec2> radialExits(const Simulation& simulation,Id producerId,Kind unitKind,
                              bool requireGroundClear) {
    const Entity* producer=simulation.find(producerId);
    check(producer,"exit fixture producer exists");
    const auto navigation=navigationFor(simulation);
    const float unitRadius=definition(unitKind).radius;
    const float angle=std::atan2(producer->rally.y-producer->pos.y,
                                producer->rally.x-producer->pos.x);
    std::vector<Vec2> exits;
    for(int ring=0;ring<6;++ring)for(int spoke=0;spoke<16;++spoke) {
        const float candidateAngle=angle+spoke*Pi/8;
        const float extent=definition(producer->kind).radius+unitRadius+28+ring*35;
        const Vec2 candidate{producer->pos.x+std::cos(candidateAngle)*extent,
                             producer->pos.y+std::sin(candidateAngle)*extent};
        if(candidate.x<unitRadius||candidate.y<unitRadius||
           candidate.x>simulation.worldSize()-unitRadius||
           candidate.y>simulation.worldSize()-unitRadius)continue;
        if(!requireGroundClear||navigation.pointClear(candidate,unitRadius,producerId))
            exits.push_back(candidate);
    }
    return exits;
}

std::vector<Vec2> clearExits(const Simulation& simulation,Id producerId,Kind unitKind) {
    return radialExits(simulation,producerId,unitKind,true);
}

bool routeReached(const Simulation& simulation,Vec2 from,const std::vector<Vec2>& goals,
                  Kind unitKind,Id ignore=0) {
    return navigationFor(simulation).route(from,goals,definition(unitKind).radius,ignore).reached;
}

std::vector<Vec2> oreWorkPoints(const Simulation& simulation,Id oreId) {
    const Entity* ore=simulation.find(oreId);
    check(ore&&ore->kind==Kind::Resource,"ore work-point fixture has its resource");
    const Navigation navigation=navigationFor(simulation);
    const float workerRadius=definition(Kind::Worker).radius;
    const float reach=definition(Kind::Resource).radius+workerRadius+9;
    std::vector<Vec2> points;
    for(int spoke=0;spoke<32;++spoke) {
        const float angle=spoke*(2*Pi/32);
        const Vec2 point{ore->pos.x+std::cos(angle)*reach,
                         ore->pos.y+std::sin(angle)*reach};
        if(navigation.pointClear(point,workerRadius+0.25f)&&
           navigation.segmentClear(point,ore->pos,workerRadius,oreId))points.push_back(point);
    }
    return points;
}

Id newest(const Simulation& simulation,int team,Kind kind,const std::vector<Id>& before) {
    Id result=0;
    for(Id id:ids(simulation,team,kind))
        if(std::find(before.begin(),before.end(),id)==before.end())result=std::max(result,id);
    return result;
}

template<class Predicate>
void advanceUntil(Simulation& simulation,float maximumSeconds,Predicate complete,
                  const std::string& failure) {
    const int steps=static_cast<int>(std::ceil(maximumSeconds/Simulation::Step));
    for(int step=0;step<steps&&!complete();++step)simulation.update(Simulation::Step);
    check(complete(),failure);
}

struct Enclosure {
    Simulation simulation;
    Id producer=0;
};

Enclosure exactEnclosure(Kind producerKind) {
    Enclosure fixture{emptyFixture(),0};
    auto& simulation=fixture.simulation;
    simulation.debugSpawn(Kind::Processor,0,{387.868f,4412.13f});
    simulation.debugSpawn(Kind::Processor,0,{431.619f,4606.51f});
    simulation.debugSpawn(Kind::Processor,0,{193.493f,4368.38f});
    fixture.producer=simulation.debugSpawn(producerKind,0,{196.949f,4603.05f});
    return fixture;
}

void sealFirstWorkerExit(Simulation& simulation,Id producerId) {
    const auto candidates=radialExits(simulation,producerId,Kind::Worker,false);
    check(!candidates.empty(),"worker pocket has an in-bounds first radial exit");
    const Vec2 first=candidates.front();
    // A worker fits at the center with four units of clearance, but its
    // sixteen-unit radius cannot cross the closed square. Other radial exits
    // remain outside this 52-by-52 obstacle envelope.
    mutableObstacles(simulation)={
        {{first.x-23,first.y},{3,26}},{{first.x+23,first.y},{3,26}},
        {{first.x,first.y-23},{26,3}},{{first.x,first.y+23},{26,3}},
    };
    // Direct obstacle mutation is confined to portable fixtures. Spawning a
    // depleted remote resource invalidates the simulation navigation cache
    // without adding live collision geometry.
    const Id dirty=simulation.debugSpawn(Kind::Resource,-1,{4600,2400});
    edit(simulation,dirty)->resource=0;
}

void exactMortarChoosesReachableAlternateExit() {
    auto fixture=exactEnclosure(Kind::MotorPool);
    auto& simulation=fixture.simulation;
    const_cast<Player&>(simulation.players()[0]).tier=3;
    edit(simulation,fixture.producer)->rally={1120,3680};
    edit(simulation,fixture.producer)->rallyOverride=true;
    const auto exits=clearExits(simulation,fixture.producer,Kind::Mortar);
    check(exits.size()>1,"exact enclosure has multiple geometrically clear Mortar exits");
    check(same(exits.front(),{311.5f,4488.5f},0.2f),
          "exact enclosure retains the reproduced first clear Mortar exit");
    check(!routeReached(simulation,exits.front(),{{1120,3680}},Kind::Mortar),
          "reproduced first clear Mortar exit cannot reach its rally");
    check(std::any_of(exits.begin()+1,exits.end(),[&](Vec2 exit) {
        return routeReached(simulation,exit,{{1120,3680}},Kind::Mortar);
    }),"exact enclosure contains a later reachable Mortar exit");

    const auto before=ids(simulation,0,Kind::Mortar);
    const int oreAfterPayment=simulation.players()[0].ore-definition(Kind::Mortar).cost;
    check(send(simulation,CommandType::Train,0,{fixture.producer},{},0,Kind::Mortar).accepted,
          "exact enclosure accepts the paid Mortar job");
    check(simulation.players()[0].ore==oreAfterPayment,"Mortar job charges its exact cost once");
    advanceUntil(simulation,definition(Kind::Mortar).buildTime+5,[&] {
        return newest(simulation,0,Kind::Mortar,before)!=0;
    },"Mortar did not leave through the reachable alternate exit");
    const Id mortar=newest(simulation,0,Kind::Mortar,before);
    const Entity* created=simulation.find(mortar);
    check(created&&created->order==Order::Move,"alternate-exit Mortar starts its rally order");
    check(!same(created->pos,exits.front(),0.2f),"Mortar does not use the reproduced unreachable first exit");
    check(routeReached(simulation,created->pos,{created->goal},Kind::Mortar),
          "chosen Mortar spawn reaches its actual assigned formation goal");
    check(simulation.find(fixture.producer)->queue.empty()&&simulation.players()[0].stats.produced==1,
          "reachable exit consumes the paid job and records one completion");
    advanceUntil(simulation,45,[&] {return distance(simulation.find(mortar)->pos,simulation.find(mortar)->goal)<35;},
                 "Mortar did not physically reach its rally goal");
}

void explicitGroundWorkerChoosesReachableAlternateExit() {
    auto fixture=exactEnclosure(Kind::Headquarters);
    auto& simulation=fixture.simulation;
    const Vec2 rally{1120,3680};
    check(send(simulation,CommandType::Rally,0,{fixture.producer},rally).accepted,
          "enclosed Anchor accepts an explicit ground worker rally");
    sealFirstWorkerExit(simulation,fixture.producer);
    const auto exits=clearExits(simulation,fixture.producer,Kind::Worker);
    check(exits.size()>1&&!routeReached(simulation,exits.front(),{rally},Kind::Worker),
          "explicit-worker fixture begins with a clear but unreachable exit");
    check(std::any_of(exits.begin()+1,exits.end(),[&](Vec2 exit) {
        return routeReached(simulation,exit,{rally},Kind::Worker);
    }),"explicit-worker fixture has a reachable alternate exit");
    const auto before=ids(simulation,0,Kind::Worker);
    check(send(simulation,CommandType::Train,0,{fixture.producer},{},0,Kind::Worker).accepted,
          "enclosed Anchor accepts a paid worker job");
    advanceUntil(simulation,definition(Kind::Worker).buildTime+5,[&] {
        return newest(simulation,0,Kind::Worker,before)!=0;
    },"explicit-rally worker did not complete");
    const Entity* worker=simulation.find(newest(simulation,0,Kind::Worker,before));
    check(worker&&worker->order==Order::Move&&!worker->resourceTarget,
          "explicit ground worker rally remains a Move instead of automatic mining");
    check(!same(worker->pos,exits.front(),0.2f)&&same(worker->goal,rally),
          "explicit-rally worker uses an alternate exit and retains the requested goal");
    check(routeReached(simulation,worker->pos,{worker->goal},Kind::Worker),
          "explicit-rally worker spawn reaches its actual assigned goal");
}

void oreAndDefaultWorkersChooseReachableAlternateExits() {
    auto run=[&](bool explicitOre) {
        auto fixture=exactEnclosure(Kind::Headquarters);
        auto& simulation=fixture.simulation;
        for(auto& entity:mutableEntities(simulation))if(entity.kind==Kind::Resource)entity.resource=0;
        const Id ore=simulation.debugSpawn(Kind::Resource,-1,{1120,3680});
        if(explicitOre)check(send(simulation,CommandType::Rally,0,{fixture.producer},simulation.find(ore)->pos).accepted,
                             "enclosed Anchor accepts the exact ore rally");
        else {
            Entity* producer=edit(simulation,fixture.producer);
            // Auto Mine is selected by the absence of an override. Retain the
            // enclosure's southeast radial ordering so this case isolates the
            // same alternate-exit choice from the explicit-rally case above.
            producer->rally=simulation.find(ore)->pos;
            producer->rallyOverride=false;
        }
        sealFirstWorkerExit(simulation,fixture.producer);
        const std::vector<Vec2> workPoints=oreWorkPoints(simulation,ore);
        check(!workPoints.empty(),"alternate worker fixture has legal ore work points");
        const auto exits=clearExits(simulation,fixture.producer,Kind::Worker);
        check(exits.size()>1&&!routeReached(simulation,exits.front(),workPoints,Kind::Worker),
              "worker mining fixture begins with an exit that cannot reach ore work");
        check(std::any_of(exits.begin()+1,exits.end(),[&](Vec2 exit) {
            return routeReached(simulation,exit,workPoints,Kind::Worker);
        }),"worker mining fixture has an alternate exit that reaches ore work");
        const auto before=ids(simulation,0,Kind::Worker);
        check(send(simulation,CommandType::Train,0,{fixture.producer},{},0,Kind::Worker).accepted,
              "worker mining fixture accepts its paid job");
        advanceUntil(simulation,definition(Kind::Worker).buildTime+5,[&] {
            return newest(simulation,0,Kind::Worker,before)!=0;
        },"worker mining fixture did not complete");
        const Id workerId=newest(simulation,0,Kind::Worker,before);
        const Entity* worker=simulation.find(workerId);
        check(worker&&worker->order==Order::Gather&&worker->resourceTarget==ore&&worker->target==ore,
              explicitOre?"explicit ore rally keeps its exact deposit":"default Auto Mine assigns the reachable deposit");
        check(!same(worker->pos,exits.front(),0.2f)&&worker->workPointValid,
              "fresh miner uses a reachable alternate exit with a real work point");
        check(routeReached(simulation,worker->pos,{worker->workPoint},Kind::Worker,workerId),
              "fresh miner's chosen spawn reaches its actual assigned work point");
        const int gatheredBefore=simulation.players()[0].stats.gathered;
        advanceUntil(simulation,45,[&] {
            return simulation.players()[0].stats.gathered>gatheredBefore;
        },explicitOre?"explicit ore-rally worker did not harvest and deliver":"default Auto Mine worker did not harvest and deliver");
    };
    run(true);
    run(false);
}

void resourceRallyPinsChosenOreAcrossPaidRetryAndSave() {
    auto simulation=emptyFixture({700,700});
    const Id producer=ids(simulation,0,Kind::Headquarters).front();
    const Id chosen=simulation.debugSpawn(Kind::Resource,-1,{1120,700});
    const Id alternate=simulation.debugSpawn(Kind::Resource,-1,{700,1120});
    std::vector<Id> existingMiners;
    for(int index=0;index<4;++index) {
        const Id worker=simulation.debugSpawn(Kind::Worker,0,{860.0f,580.0f+index*38.0f});
        existingMiners.push_back(worker);
        check(send(simulation,CommandType::Gather,0,{worker},{},chosen).accepted,
              "load-bias fixture assigns an existing miner to the chosen deposit");
    }
    check(std::count_if(existingMiners.begin(),existingMiners.end(),[&](Id worker) {
              return simulation.find(worker)->resourceTarget==chosen;
          })==4&&std::none_of(existingMiners.begin(),existingMiners.end(),[&](Id worker) {
              return simulation.find(worker)->resourceTarget==alternate;
          }),"chosen deposit has four assigned miners while the alternate has none");
    check(send(simulation,CommandType::Rally,0,{producer},simulation.find(chosen)->pos).accepted,
          "Anchor accepts a rally directly on the chosen explored deposit");
    const auto before=ids(simulation,0,Kind::Worker);
    const int oreAfterPayment=simulation.players()[0].ore-definition(Kind::Worker).cost;
    check(send(simulation,CommandType::Train,0,{producer},{},0,Kind::Worker).accepted,
          "resource-rally fixture accepts a paid worker job");
    check(simulation.players()[0].ore==oreAfterPayment,
          "resource-rally worker payment is committed before assignment");

    Entity* anchor=edit(simulation,producer);
    check(anchor&&anchor->queue.size()==1,"resource-rally fixture retains its paid queue item");
    QueueItem& queued=anchor->queue.front();
    // Model the serialized boundary after production resolved the explicit
    // resource coordinate but a capped route-search step deferred completion.
    queued.remaining=0;
    queued.assignmentCandidates={chosen};
    queued.assignmentCursor=0;
    anchor->repath=0.75f;
    anchor->pathGeometry=navigationFor(simulation).geometryVersion();
    check(anchor->pathGeometry!=0,"paid exact-resource retry records current geometry");

    const auto path=(std::filesystem::temp_directory_path()/"cinderline-resource-rally-retry.sav").string();
    check(simulation.save(path),"paid exact-resource retry saves");
    Simulation loaded;
    check(loaded.load(path)&&loaded.stateHash()==simulation.stateHash(),
          "paid exact-resource retry and chosen deposit load exactly");
    std::filesystem::remove(path);

    Id workerId=0;
    for(int step=0;step<80&&!workerId;++step) {
        simulation.update(Simulation::Step);
        loaded.update(Simulation::Step);
        check(simulation.stateHash()==loaded.stateHash(),
              "chosen resource retry continues deterministically after load");
        workerId=newest(simulation,0,Kind::Worker,before);
    }
    check(workerId&&newest(loaded,0,Kind::Worker,before)==workerId,
          "paid exact-resource retry completes once on both authorities");
    const Entity* worker=simulation.find(workerId);
    check(worker&&worker->order==Order::Gather&&worker->resourceTarget==chosen&&
          worker->target==chosen&&worker->resourceTarget!=alternate,
          "resource rally keeps the chosen deposit despite its larger existing mining load");
    for(Id miner:existingMiners) {
        const Entity* existing=simulation.find(miner);
        check(existing&&existing->order==Order::Gather&&existing->resourceTarget==chosen,
              "newborn assignment leaves every existing miner on its prior deposit");
    }
}

void rallyChangesApplyToQueuedAndNewbornWorkersOnly() {
    auto simulation=emptyFixture({700,700});
    const Id producer=ids(simulation,0,Kind::Headquarters).front();
    const Id firstOre=simulation.debugSpawn(Kind::Resource,-1,{1080,700});
    const Id secondOre=simulation.debugSpawn(Kind::Resource,-1,{700,1080});
    const Id existingMiner=simulation.debugSpawn(Kind::Worker,0,{880,700});
    check(send(simulation,CommandType::Gather,0,{existingMiner},{},firstOre).accepted,
          "rally-change fixture assigns its existing miner");
    check(send(simulation,CommandType::Rally,0,{producer},simulation.find(firstOre)->pos).accepted,
          "rally-change fixture starts on the first deposit");

    auto before=ids(simulation,0,Kind::Worker);
    check(send(simulation,CommandType::Train,0,{producer},{},0,Kind::Worker).accepted,
          "rally-change fixture queues a paid worker");
    const Id queuedId=simulation.find(producer)->queue.front().id;
    for(int step=0;step<20;++step)simulation.update(Simulation::Step);
    Entity* pendingProducer=edit(simulation,producer);
    check(pendingProducer&&pendingProducer->queue.size()==1,
          "rally-change fixture retains the queued worker before redirection");
    pendingProducer->queue.front().remaining=0;
    pendingProducer->queue.front().assignmentCandidates={firstOre};
    pendingProducer->queue.front().assignmentCursor=1;
    pendingProducer->repath=0.75f;
    pendingProducer->pathGeometry=navigationFor(simulation).geometryVersion();
    check(send(simulation,CommandType::Rally,0,{producer},simulation.find(secondOre)->pos).accepted,
          "deferred queued worker accepts a changed resource rally");
    const Entity* changedProducer=simulation.find(producer);
    check(changedProducer&&changedProducer->queue.size()==1&&
          changedProducer->queue.front().id==queuedId&&
          changedProducer->queue.front().assignmentCandidates.empty()&&
          changedProducer->queue.front().assignmentCursor==0&&changedProducer->repath==0&&
          changedProducer->pathGeometry==0,
          "rally change preserves the paid job while clearing cached resource choices");
    advanceUntil(simulation,definition(Kind::Worker).buildTime+5,[&] {
        return newest(simulation,0,Kind::Worker,before)!=0;
    },"worker queued across a resource-rally change did not complete");
    const Id secondMiner=newest(simulation,0,Kind::Worker,before);
    check(simulation.find(secondMiner)->order==Order::Gather&&
          simulation.find(secondMiner)->resourceTarget==secondOre,
          "queued worker resolves the latest resource rally when born");
    check(simulation.find(existingMiner)->order==Order::Gather&&
          simulation.find(existingMiner)->resourceTarget==firstOre,
          "resource-rally change does not redirect an existing miner");

    const Vec2 groundRally{1450,1250};
    check(send(simulation,CommandType::Rally,0,{producer},groundRally).accepted,
          "Anchor accepts a later ordinary ground rally");
    before=ids(simulation,0,Kind::Worker);
    check(send(simulation,CommandType::Train,0,{producer},{},0,Kind::Worker).accepted,
          "ground-rally fixture queues another paid worker");
    advanceUntil(simulation,definition(Kind::Worker).buildTime+5,[&] {
        return newest(simulation,0,Kind::Worker,before)!=0;
    },"ground-rallied worker did not complete");
    const Id movingWorker=newest(simulation,0,Kind::Worker,before);
    const Entity* moving=simulation.find(movingWorker);
    check(moving&&moving->order==Order::Move&&!moving->resourceTarget&&same(moving->goal,groundRally),
          "ordinary ground rally remains a Move with its coordinate goal");
    check(simulation.find(existingMiner)->resourceTarget==firstOre&&
          simulation.find(secondMiner)->resourceTarget==secondOre,
          "ordinary rally still leaves established miners untouched");

    check(send(simulation,CommandType::AutoRally,0,{}, {},producer,Kind::Headquarters,1).accepted,
          "Anchor accepts AUTO MINE after explicit resource and ground rallies");
    check(!simulation.find(producer)->rallyOverride,
          "AUTO MINE removes the explicit worker-rally override");
    before=ids(simulation,0,Kind::Worker);
    check(send(simulation,CommandType::Train,0,{producer},{},0,Kind::Worker).accepted,
          "AUTO MINE fixture queues a paid worker");
    advanceUntil(simulation,definition(Kind::Worker).buildTime+5,[&] {
        return newest(simulation,0,Kind::Worker,before)!=0;
    },"AUTO MINE worker did not complete");
    const Id automaticId=newest(simulation,0,Kind::Worker,before);
    const Entity* automatic=simulation.find(automaticId);
    check(automatic&&automatic->order==Order::Gather&&automatic->resourceTarget&&
          automatic->workPointValid,
          "AUTO MINE restores a concrete reachable default mining job");
    check(routeReached(simulation,automatic->pos,{automatic->workPoint},Kind::Worker,automaticId),
          "AUTO MINE newborn has a reachable route to its assigned work point");
}

void depletedResourceRallyFallsBackWithoutSilentRetargeting() {
    auto simulation=emptyFixture({700,700});
    const Id producer=ids(simulation,0,Kind::Headquarters).front();
    const Id depleted=simulation.debugSpawn(Kind::Resource,-1,{1080,700});
    const Id alternate=simulation.debugSpawn(Kind::Resource,-1,{700,1080});
    const Vec2 rally=simulation.find(depleted)->pos;
    check(send(simulation,CommandType::Rally,0,{producer},rally).accepted,
          "depletion fixture accepts a resource rally");
    const auto before=ids(simulation,0,Kind::Worker);
    check(send(simulation,CommandType::Train,0,{producer},{},0,Kind::Worker).accepted,
          "depletion fixture queues a paid worker");
    edit(simulation,depleted)->resource=0;
    advanceUntil(simulation,definition(Kind::Worker).buildTime+5,[&] {
        return newest(simulation,0,Kind::Worker,before)!=0;
    },"worker at a depleted resource rally did not complete");
    const Entity* worker=simulation.find(newest(simulation,0,Kind::Worker,before));
    check(worker&&worker->order==Order::Move&&!worker->resourceTarget&&!worker->target&&
          same(worker->goal,rally),
          "depleted chosen ore falls back to the retained ground rally coordinate");
    check(simulation.find(alternate)->resource>0,
          "depletion fallback retains another valid explored deposit for the no-retarget proof");
}

void airUnitBypassesGroundExitRouting() {
    auto fixture=exactEnclosure(Kind::MotorPool);
    auto& simulation=fixture.simulation;
    const_cast<Player&>(simulation.players()[0]).tier=3;
    const Vec2 rally{1120,3680};
    edit(simulation,fixture.producer)->rally=rally;
    edit(simulation,fixture.producer)->rallyOverride=true;
    const auto exits=radialExits(simulation,fixture.producer,Kind::Kite,false);
    check(!exits.empty(),"air bypass fixture has its deterministic first local exit");
    const auto before=ids(simulation,0,Kind::Kite);
    check(send(simulation,CommandType::Train,0,{fixture.producer},{},0,Kind::Kite).accepted,
          "air bypass fixture accepts a paid Kite");
    const NavigationStats navigationBefore=simulation.navigationStats();
    advanceUntil(simulation,definition(Kind::Kite).buildTime+5,[&] {
        return newest(simulation,0,Kind::Kite,before)!=0;
    },"Kite did not complete through the ground enclosure");
    const Entity* kite=simulation.find(newest(simulation,0,Kind::Kite,before));
    check(kite&&distance(kite->pos,exits.front())<=definition(Kind::Kite).speed*Simulation::Step+0.5f&&
          kite->order==Order::Move&&same(kite->goal,rally),
          "air unit keeps the first geometric exit and its rally movement");
    check(simulation.navigationStats().searches==navigationBefore.searches&&
          simulation.navigationStats().failures==navigationBefore.failures,
          "air production consumes no ground-route search or failure budget");
}

void noLocalExitPreservesPaidReadyJobAcrossSave() {
    auto simulation=emptyFixture({600,600});
    const_cast<Player&>(simulation.players()[0]).tier=3;
    mutableObstacles(simulation)={{{2400,2400},{430,430}}};
    const Id producer=simulation.debugSpawn(Kind::MotorPool,0,{2400,2400});
    edit(simulation,producer)->rally={3400,2400};
    edit(simulation,producer)->rallyOverride=true;
    const auto before=ids(simulation,0,Kind::Mortar);
    const int oreBefore=simulation.players()[0].ore;
    const int supplyBefore=simulation.supply(0);
    check(send(simulation,CommandType::Train,0,{producer},{},0,Kind::Mortar).accepted,
          "blocked producer accepts and charges a Mortar job");
    const QueueItem paid=simulation.find(producer)->queue.front();
    check(send(simulation,CommandType::Train,0,{producer},{},0,Kind::Mortar).accepted,
          "blocked producer accepts a second waiting Mortar job");
    const QueueItem waiting=simulation.find(producer)->queue.back();
    const int paidOre=oreBefore-2*definition(Kind::Mortar).cost;
    const int reservedSupply=supplyBefore+2*definition(Kind::Mortar).supply;
    for(int step=0;step<static_cast<int>((definition(Kind::Mortar).buildTime+5)/Simulation::Step);++step)
        simulation.update(Simulation::Step);
    const Entity* blocked=simulation.find(producer);
    check(blocked&&blocked->queue.size()==2&&blocked->queue.front().id==paid.id&&
          blocked->queue.back().id==waiting.id&&
          blocked->queue.front().remaining==0&&newest(simulation,0,Kind::Mortar,before)==0,
          "no local exit retains the same completed paid front job");
    check(simulation.players()[0].ore==paidOre&&simulation.supply(0)==reservedSupply&&
          simulation.players()[0].stats.produced==0,
          "blocked ready job retains its payment, reservation, and unproduced status");

    const auto path=(std::filesystem::temp_directory_path()/"cinderline-production-exit-blocked.sav").string();
    check(simulation.save(path),"blocked paid-ready production state saves");
    Simulation loaded;
    check(loaded.load(path)&&loaded.stateHash()==simulation.stateHash(),
          "blocked paid-ready production state loads exactly");
    std::filesystem::remove(path);
    mutableObstacles(simulation).clear();
    mutableObstacles(loaded).clear();
    simulation.debugSpawn(Kind::Resource,-1,{4600,2400});
    loaded.debugSpawn(Kind::Resource,-1,{4600,2400});
    advanceUntil(simulation,2,[&] {return newest(simulation,0,Kind::Mortar,before)!=0;},
                 "opening a local exit did not release the paid-ready Mortar");
    advanceUntil(loaded,2,[&] {return newest(loaded,0,Kind::Mortar,before)!=0;},
                 "loaded paid-ready Mortar did not use the opened exit");
    check(simulation.stateHash()==loaded.stateHash()&&simulation.find(producer)->queue.size()==1&&
          simulation.find(producer)->queue.front().id==waiting.id&&
          simulation.players()[0].stats.produced==1,
          "opened exit produces the ready job exactly once and leaves the waiting job intact");
}

void unreachableRallyUsesDeterministicClearFallback() {
    auto simulation=emptyFixture({600,2400});
    mutableObstacles(simulation)={{{2400,2400},{60,2400}}};
    const Id producer=simulation.debugSpawn(Kind::Foundry,0,{1800,2400});
    const Vec2 rally{3200,2400};
    edit(simulation,producer)->rally=rally;
    edit(simulation,producer)->rallyOverride=true;
    const auto exits=clearExits(simulation,producer,Kind::Striker);
    check(!exits.empty(),"sealed-rally fixture has clear local spawn exits");
    check(std::none_of(exits.begin(),exits.end(),[&](Vec2 exit) {
        return routeReached(simulation,exit,{rally},Kind::Striker);
    }),"solid world-height wall makes every local exit unable to reach the rally");
    const auto before=ids(simulation,0,Kind::Striker);
    check(send(simulation,CommandType::Train,0,{producer},{},0,Kind::Striker).accepted,
          "sealed-rally fixture accepts its paid infantry");
    advanceUntil(simulation,definition(Kind::Striker).buildTime+5,[&] {
        return newest(simulation,0,Kind::Striker,before)!=0;
    },"route-unreachable rally incorrectly stranded a locally spawnable paid unit");
    const Id strikerId=newest(simulation,0,Kind::Striker,before);
    const Entity* striker=simulation.find(strikerId);
    check(striker&&same(striker->pos,exits.front(),0.2f)&&same(striker->goal,rally)&&
          striker->order==Order::Move,
          "unreachable rally uses the deterministic first clear fallback and retains its order");
    advanceUntil(simulation,5,[&] {return simulation.find(strikerId)->navigationExhausted;},
                 "fallback unit did not report its ordinary unreachable Move state");
    check(simulation.find(producer)->queue.empty()&&simulation.players()[0].stats.produced==1,
          "unreachable rally fallback consumes the paid job exactly once");
}

struct ReadyWorkerBackoff {
    Simulation simulation;
    Id producer=0;
    Id ore=0;
};

ReadyWorkerBackoff syntheticReadyWorkerBackoff() {
    ReadyWorkerBackoff fixture{emptyFixture(),0,0};
    auto& simulation=fixture.simulation;
    fixture.producer=ids(simulation,0,Kind::Headquarters).front();
    fixture.ore=simulation.debugSpawn(Kind::Resource,-1,{1000,4200});
    check(send(simulation,CommandType::Train,0,{fixture.producer},{},0,Kind::Worker).accepted,
          "synthetic backoff fixture creates a legitimately paid worker job");
    Entity* producer=edit(simulation,fixture.producer);
    check(producer&&producer->queue.size()==1,"synthetic backoff fixture has one worker job");
    producer->queue.front().remaining=0;
    // Represents a valid paid-ready item one quarter-second into its one-second
    // SearchLimited retry delay.
    producer->repath=0.75f;
    producer->pathGeometry=navigationFor(simulation).geometryVersion();
    check(producer->pathGeometry!=0,"synthetic backoff fixture records current navigation geometry");
    return fixture;
}

void paidReadyWorkerBackoffPersistsAndResumesExactly() {
    auto fixture=syntheticReadyWorkerBackoff();
    auto& simulation=fixture.simulation;
    const auto path=(std::filesystem::temp_directory_path()/"cinderline-production-worker-backoff.sav").string();
    check(simulation.save(path),"synthetic paid-ready worker backoff saves");
    Simulation loaded;
    check(loaded.load(path)&&loaded.stateHash()==simulation.stateHash(),
          "paid-ready worker retry clock and geometry load exactly");
    std::filesystem::remove(path);
    const auto originalSearches=simulation.navigationStats().searches;
    const auto loadedSearches=loaded.navigationStats().searches;
    for(int step=0;step<10;++step) {
        simulation.update(Simulation::Step);
        loaded.update(Simulation::Step);
        check(simulation.stateHash()==loaded.stateHash(),
              "paid-ready worker backoff continues exactly after save-load");
    }
    check(simulation.find(fixture.producer)->queue.size()==1&&
          loaded.find(fixture.producer)->queue.size()==1,
          "paid-ready worker remains queued through the first half-second of backoff");
    check(simulation.navigationStats().searches==originalSearches&&
          loaded.navigationStats().searches==loadedSearches,
          "paid-ready worker performs no route search during the first backoff half-second");
    for(int step=0;step<20&&!simulation.find(fixture.producer)->queue.empty();++step) {
        simulation.update(Simulation::Step);
        loaded.update(Simulation::Step);
        check(simulation.stateHash()==loaded.stateHash(),
              "paid-ready worker retry remains deterministic when its backoff expires");
    }
    check(simulation.find(fixture.producer)->queue.empty()&&
          loaded.find(fixture.producer)->queue.empty()&&
          ids(simulation,0,Kind::Worker).size()==1&&ids(loaded,0,Kind::Worker).size()==1,
          "saved paid-ready worker completes exactly once after the retry clock expires");
}

void workerBackoffInvalidatesOnGeometryAndRallyChanges() {
    auto geometry=syntheticReadyWorkerBackoff();
    const auto geometryBefore=geometry.simulation.find(geometry.producer)->pathGeometry;
    geometry.simulation.debugSpawn(Kind::Resource,-1,{4500,2400});
    check(navigationFor(geometry.simulation).geometryVersion()!=geometryBefore,
          "geometry-change fixture has a distinct navigation fingerprint");
    geometry.simulation.update(Simulation::Step);
    check(geometry.simulation.find(geometry.producer)->queue.empty()&&
          ids(geometry.simulation,0,Kind::Worker).size()==1,
          "changed geometry bypasses the paid-ready worker retry delay");

    auto manual=syntheticReadyWorkerBackoff();
    const Vec2 manualRally{1300,4200};
    check(send(manual.simulation,CommandType::Rally,0,{manual.producer},manualRally).accepted,
          "synthetic backed-off Anchor accepts a manual rally");
    check(manual.simulation.find(manual.producer)->repath==0&&
          manual.simulation.find(manual.producer)->pathGeometry==0,
          "manual rally clears the producer retry clock and geometry");
    manual.simulation.update(Simulation::Step);
    const Entity* manualWorker=manual.simulation.find(newest(manual.simulation,0,Kind::Worker,{}));
    check(manual.simulation.find(manual.producer)->queue.empty()&&manualWorker&&
          manualWorker->order==Order::Move&&same(manualWorker->goal,manualRally),
          "manual rally retries immediately and applies the new worker order");

    auto automatic=syntheticReadyWorkerBackoff();
    check(send(automatic.simulation,CommandType::AutoRally,0,{}, {},automatic.producer,
               Kind::Headquarters,1).accepted,
          "synthetic backed-off Anchor accepts USE DEFAULT");
    check(automatic.simulation.find(automatic.producer)->repath==0&&
          automatic.simulation.find(automatic.producer)->pathGeometry==0,
          "default Auto Rally clears the producer retry clock and geometry");
    automatic.simulation.update(Simulation::Step);
    check(automatic.simulation.find(automatic.producer)->queue.empty()&&
          ids(automatic.simulation,0,Kind::Worker).size()==1,
          "default Auto Rally retries the paid-ready worker immediately");
}

void cancelledWorkerBackoffCannotDelayReplacementJobs() {
    auto run=[](bool automatic) {
        auto fixture=syntheticReadyWorkerBackoff();
        auto& simulation=fixture.simulation;
        const Id oldJob=simulation.find(fixture.producer)->queue.front().id;
        check(send(simulation,CommandType::CancelQueue,0,{fixture.producer},{},oldJob).accepted,
              "synthetic backed-off worker front job cancels by stable ID");
        check(simulation.find(fixture.producer)->queue.empty()&&
              simulation.find(fixture.producer)->repath==0&&
              simulation.find(fixture.producer)->pathGeometry==0,
              "front cancellation clears the producer retry state");
        const bool accepted=automatic
            ? send(simulation,CommandType::AutoTrain,0,{}, {},fixture.producer,Kind::Worker,1).accepted
            : send(simulation,CommandType::Train,0,{fixture.producer},{},0,Kind::Worker).accepted;
        check(accepted,automatic?"replacement Auto Train succeeds":"replacement Train succeeds");
        Entity* producer=edit(simulation,fixture.producer);
        check(producer&&producer->queue.size()==1&&producer->repath==0&&producer->pathGeometry==0,
              "an empty queue starts its replacement without inherited retry state");
        producer->queue.front().remaining=0; // Isolate retry lifecycle from ordinary build time.
        simulation.update(Simulation::Step);
        check(simulation.find(fixture.producer)->queue.empty()&&ids(simulation,0,Kind::Worker).size()==1,
              automatic?"replacement Auto Train is not delayed by the cancelled job":
                        "replacement Train is not delayed by the cancelled job");
    };
    run(false);
    run(true);
}

struct FairnessFixture {
    Simulation simulation;
    std::vector<Id> producers;
};

FairnessFixture simultaneousFixture() {
    FairnessFixture fixture{emptyFixture({500,500}),{}};
    auto& simulation=fixture.simulation;
    mutableObstacles(simulation)={{{2400,2400},{60,1900}}};
    const_cast<Player&>(simulation.players()[0]).tier=2;
    simulation.debugSpawn(Kind::Processor,0,{700,4400});
    for(int index=0;index<17;++index) {
        const float y=700.0f+index*212.0f;
        const Id producer=simulation.debugSpawn(Kind::Laboratory,0,{1400,y});
        fixture.producers.push_back(producer);
        check(send(simulation,CommandType::Rally,0,{producer},{3200,y}).accepted,
              "simultaneous producer accepts its reachable rally");
        check(send(simulation,CommandType::Train,0,{producer},{},0,Kind::Mender).accepted,
              "simultaneous producer accepts its paid Mender");
    }
    return fixture;
}

bool everyProducerFinished(const FairnessFixture& fixture) {
    return std::all_of(fixture.producers.begin(),fixture.producers.end(),[&](Id producer) {
        const Entity* entity=fixture.simulation.find(producer);
        return entity&&entity->queue.empty();
    });
}

void simultaneousReadyProducersRespectBudgetAndSaveContinuation() {
    auto fixture=simultaneousFixture();
    auto& simulation=fixture.simulation;
    bool deferred=false;
    for(int step=0;step<static_cast<int>((definition(Kind::Mender).buildTime+5)/Simulation::Step)&&!deferred;++step) {
        const auto before=simulation.navigationStats();
        simulation.update(Simulation::Step);
        const auto after=simulation.navigationStats();
        check(after.searches-before.searches<=16,"production exits respect the shared sixteen-search step budget");
        deferred=after.budgetDeferrals>before.budgetDeferrals;
    }
    check(deferred,"seventeen simultaneous ready ground producers exercise route-budget deferral");
    int ready=0,finished=0;
    for(Id producer:fixture.producers) {
        const Entity* entity=simulation.find(producer);
        if(entity->queue.empty())++finished;
        else if(entity->queue.front().remaining==0)++ready;
    }
    check(finished>0&&ready>0,"budget boundary produces an early subset and preserves later paid-ready jobs");

    const auto path=(std::filesystem::temp_directory_path()/"cinderline-production-exit-budget.sav").string();
    check(simulation.save(path),"route-budget deferred production state saves");
    Simulation loaded;
    check(loaded.load(path)&&loaded.stateHash()==simulation.stateHash(),
          "route-budget deferred production state loads exactly");
    std::filesystem::remove(path);
    for(int step=0;step<200&&!everyProducerFinished(fixture);++step) {
        const auto before=simulation.navigationStats();
        simulation.update(Simulation::Step);
        loaded.update(Simulation::Step);
        const auto after=simulation.navigationStats();
        check(after.searches-before.searches<=16,"continued production stays within the route-search budget");
        check(simulation.stateHash()==loaded.stateHash(),
              "route-budget deferred production continues deterministically after load");
    }
    check(everyProducerFinished(fixture)&&simulation.players()[0].stats.produced==17,
          "all reachable simultaneous producers finish without low-ID starvation");
    check(ids(simulation,0,Kind::Mender).size()==17&&ids(loaded,0,Kind::Mender).size()==17,
          "paired authorities choose and retain all seventeen production exits");
    for(Id mender:ids(simulation,0,Kind::Mender)) {
        const Entity* unit=simulation.find(mender);
        check(unit&&routeReached(simulation,unit->pos,{unit->goal},Kind::Mender,unit->id),
              "each simultaneous chosen spawn can reach its actual assigned goal");
    }
}

} // namespace

int main() {
    const std::vector<std::pair<std::string,std::function<void()>>> tests{
        {"exact Mortar alternate exit",exactMortarChoosesReachableAlternateExit},
        {"explicit ground worker alternate exit",explicitGroundWorkerChoosesReachableAlternateExit},
        {"ore and default worker alternate exits",oreAndDefaultWorkersChooseReachableAlternateExits},
        {"resource rally paid retry save continuation",resourceRallyPinsChosenOreAcrossPaidRetryAndSave},
        {"resource rally queued-worker isolation",rallyChangesApplyToQueuedAndNewbornWorkersOnly},
        {"depleted resource rally fallback",depletedResourceRallyFallsBackWithoutSilentRetargeting},
        {"air unit ground-routing bypass",airUnitBypassesGroundExitRouting},
        {"paid-ready no-local-exit persistence",noLocalExitPreservesPaidReadyJobAcrossSave},
        {"unreachable-rally clear fallback",unreachableRallyUsesDeterministicClearFallback},
        {"paid-ready worker backoff save continuation",paidReadyWorkerBackoffPersistsAndResumesExactly},
        {"worker backoff invalidation",workerBackoffInvalidatesOnGeometryAndRallyChanges},
        {"worker backoff replacement isolation",cancelledWorkerBackoffCannotDelayReplacementJobs},
        {"simultaneous producer budget fairness",simultaneousReadyProducersRespectBudgetAndSaveContinuation},
    };
    int failed=0;
    for(const auto& test:tests)try {
        test.second();std::cout<<"PASS "<<test.first<<'\n';
    } catch(const std::exception& exception) {
        ++failed;std::cerr<<"FAIL "<<test.first<<": "<<exception.what()<<'\n';
    }
    std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
    return failed?1:0;
}
