#include "Sim/Simulation.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

Simulation fixture() {
    Simulation simulation;
    simulation.reset({0,4242,false,1});
    return simulation;
}

std::vector<Id> ids(const Simulation& simulation,int team,Kind kind) {
    std::vector<Id> result;
    for(const auto& entity:simulation.entities())
        if(entity.alive()&&entity.team==team&&entity.kind==kind)result.push_back(entity.id);
    return result;
}

CommandResult send(Simulation& simulation,CommandType type,int team,std::vector<Id> units={},
                   Vec2 point={},Id target=0,Kind kind=Kind::Worker,int queueIndex=0) {
    return simulation.command({type,team,std::move(units),point,target,kind,queueIndex});
}

Entity* edit(Simulation& simulation,Id id) {
    for(auto& entity:const_cast<std::vector<Entity>&>(simulation.entities()))if(entity.id==id)return &entity;
    return nullptr;
}

Vec2 validPlacement(const Simulation& simulation,int team,Kind kind) {
    const Vec2 base=team==0?Vec2{600,600}:Vec2{4200,4200};
    for(float radius=230;radius<=650;radius+=35)for(int spoke=0;spoke<48;++spoke) {
        const float angle=6.283185307f*spoke/48;
        const Vec2 point{base.x+std::cos(angle)*radius,base.y+std::sin(angle)*radius};
        if(simulation.canPlace(team,kind,point))return point;
    }
    throw std::runtime_error("fixture cannot find a legal construction site");
}

const ProductionAssignment* assignment(const JobPlan& plan,Id producer) {
    const auto found=std::find_if(plan.assignments.begin(),plan.assignments.end(),[&](const ProductionAssignment& item){return item.producer==producer;});
    return found==plan.assignments.end()?nullptr:&*found;
}

void waitForConstruction(Simulation& simulation,Id building,const std::string& message) {
    for(int step=0;step<4000;++step) {
        const Entity* entity=simulation.find(building);
        if(entity&&entity->progress>=1)return;
        simulation.update(Simulation::Step);
    }
    check(false,message);
}

void advance(Simulation& simulation,float seconds) {
    for(int step=0;step<static_cast<int>(std::ceil(seconds/Simulation::Step));++step)simulation.update(Simulation::Step);
}

std::vector<std::string> readLines(const std::string& path) {
    std::ifstream input(path);std::vector<std::string> lines;std::string line;
    while(std::getline(input,line))lines.push_back(line);return lines;
}

void writeLines(const std::string& path,const std::vector<std::string>& lines) {
    std::ofstream output(path,std::ios::trunc);for(const auto& line:lines)output<<line<<'\n';
}

void stripSaveElevenRecordingModes(std::vector<std::string>& lines) {
    auto split=[](const std::string& row) {
        std::istringstream input(row);std::vector<std::string> values;std::string value;
        while(input>>value)values.push_back(value);return values;
    };
    auto join=[](const std::vector<std::string>& values) {
        std::ostringstream output;for(std::size_t index=0;index<values.size();++index)output<<(index?" ":"")<<values[index];return output.str();
    };
    const auto config=split(lines.at(1));const auto players=static_cast<std::size_t>(std::stoul(config.at(5)));std::size_t cursor=3+players;
    cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
    const auto entityCount=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
    for(std::size_t entity=0;entity<entityCount;++entity) {
        ++cursor;cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
        cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
    }
    const auto effectHeader=split(lines.at(cursor));cursor+=1+static_cast<std::size_t>(std::stoul(effectHeader.front()))+players*2;
    const auto recordingCount=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
    for(std::size_t recording=0;recording<recordingCount;++recording) {
        auto row=split(lines.at(cursor));row.erase(row.begin()+8);lines[cursor++]=join(row);
    }
}

void stripSaveThirteenFormation(std::vector<std::string>& lines) {
    auto split=[](const std::string& row) {
        std::istringstream input(row);std::vector<std::string> values;std::string value;
        while(input>>value)values.push_back(value);return values;
    };
    auto join=[](const std::vector<std::string>& values) {
        std::ostringstream output;for(std::size_t index=0;index<values.size();++index)output<<(index?" ":"")<<values[index];return output.str();
    };
    auto config=split(lines.at(1));
    if(lines.front()=="CINDERLINE 15") {check(config.size()==7,"current production save includes its map revision");config.pop_back();lines[1]=join(config);}
    const auto players=static_cast<std::size_t>(std::stoul(config.at(5)));std::size_t cursor=3+players;
    cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
    const auto entityCount=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
    for(std::size_t entity=0;entity<entityCount;++entity) {
        ++cursor;cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
        cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
    }
    const auto effectHeader=split(lines.at(cursor));cursor+=1+static_cast<std::size_t>(std::stoul(effectHeader.front()))+players*2;
    const auto recordingCount=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
    for(std::size_t recording=0;recording<recordingCount;++recording) {
        auto row=split(lines.at(cursor));
        check(row.size()>=13&&row.size()==13+static_cast<std::size_t>(std::stoul(row[12])),
              "save-thirteen production fixture has complete formation recording fields");
        row.erase(row.begin()+9,row.begin()+12);lines[cursor++]=join(row);
    }
    const auto sustained=std::find(lines.begin(),lines.end(),"SUSTAINED_ORDERS 1");
    const auto formation=std::find(lines.begin(),lines.end(),"FORMATION_ORDERS 1");
    check(sustained!=lines.end()&&formation!=lines.end()&&sustained<formation,
          "save-thirteen production fixture has ordered sustained and formation sections");
    lines.erase(formation,lines.end());
    lines.front()="CINDERLINE 12";
}

std::string replaceField(const std::string& row,std::size_t column,const std::string& replacement) {
    std::istringstream input(row);std::vector<std::string> fields;std::string value;
    while(input>>value)fields.push_back(value);check(column<fields.size(),"save fixture field exists");fields[column]=replacement;
    std::ostringstream output;for(std::size_t index=0;index<fields.size();++index)output<<(index?" ":"")<<fields[index];return output.str();
}

void remainingTimeAllocationAndAtomicGates() {
    auto simulation=fixture();
    simulation.debugResources(0,100000);
    const Id firstFoundry=simulation.debugSpawn(Kind::Foundry,0,{1050,900});
    const Id secondFoundry=simulation.debugSpawn(Kind::Foundry,0,{1280,900});
    const Id thirdFoundry=simulation.debugSpawn(Kind::Foundry,0,{1510,900});
    check(send(simulation,CommandType::Train,0,{firstFoundry},{},0,Kind::Striker).accepted,"first producer accepts its setup job");
    check(send(simulation,CommandType::Train,0,{secondFoundry},{},0,Kind::Lancer).accepted,"second producer accepts its setup job");

    const auto beforeStatus=simulation.stateHash();
    const JobPlan plan=simulation.autoTrainStatus(0,Kind::Striker,6);
    check(plan.accepted&&plan.quantity==6&&plan.totalCost==6*definition(Kind::Striker).cost&&plan.totalSupply==6*definition(Kind::Striker).supply,"batch preview reports exact aggregate cost and supply");
    check(simulation.stateHash()==beforeStatus,"batch preview is read only");
    const auto* firstPlan=assignment(plan,firstFoundry);
    const auto* secondPlan=assignment(plan,secondFoundry);
    const auto* thirdPlan=assignment(plan,thirdFoundry);
    check(firstPlan&&firstPlan->quantity==2&&firstPlan->queuedSeconds==definition(Kind::Striker).buildTime&&firstPlan->completionSeconds==3*definition(Kind::Striker).buildTime,"stable lower-ID tie gives two new jobs to the first producer");
    check(secondPlan&&secondPlan->quantity==1&&secondPlan->queuedSeconds==definition(Kind::Lancer).buildTime&&secondPlan->completionSeconds==definition(Kind::Lancer).buildTime+definition(Kind::Striker).buildTime,"existing remaining work participates in allocation");
    check(thirdPlan&&thirdPlan->quantity==3&&thirdPlan->queuedSeconds==0&&thirdPlan->completionSeconds==3*definition(Kind::Striker).buildTime,"idle producer receives the largest share of the batch");

    const int oreBefore=simulation.players()[0].ore;
    check(send(simulation,CommandType::AutoTrain,0,{}, {},0,Kind::Striker,6).accepted,"automatic batch executes synchronously");
    check(simulation.players()[0].ore==oreBefore-plan.totalCost,"automatic batch charges its complete exact cost once");
    check(simulation.find(firstFoundry)->queue.size()==3&&simulation.find(secondFoundry)->queue.size()==2&&simulation.find(thirdFoundry)->queue.size()==3,"executed batch matches preview distribution");
    for(Id producer:{firstFoundry,secondFoundry,thirdFoundry}) {
        const Entity* entity=simulation.find(producer);
        Id previous=0;
        for(const auto& item:entity->queue){check(item.id>previous&&item.id<entity->nextQueueId,"every producer owns a strictly increasing stable job sequence");previous=item.id;}
    }

    auto insufficient=fixture();
    insufficient.debugSpawn(Kind::Foundry,0,{1050,900});
    insufficient.debugResources(0,2*definition(Kind::Striker).cost-1);
    const auto insufficientBefore=insufficient.stateHash();
    check(!send(insufficient,CommandType::AutoTrain,0,{}, {},0,Kind::Striker,2).accepted&&insufficient.stateHash()==insufficientBefore,"insufficient ore rejects the whole batch without mutation");

    auto capped=fixture();
    const Id cappedFoundry=capped.debugSpawn(Kind::Foundry,0,{1050,900});
    capped.debugResources(0,100000);
    while(capped.supply(0)<capped.capacity(0))capped.debugSpawn(Kind::Worker,0,{900.0f+5*capped.supply(0),740});
    const auto cappedBefore=capped.stateHash();
    check(!send(capped,CommandType::AutoTrain,0,{}, {},cappedFoundry,Kind::Striker,1).accepted&&capped.stateHash()==cappedBefore,"insufficient supply rejects a pinned batch without mutation");

    auto queues=fixture();
    queues.debugResources(0,100000);
    for(int i=0;i<5;++i)queues.debugSpawn(Kind::Processor,0,{900.0f+i*170,1300});
    const Id queueA=queues.debugSpawn(Kind::Foundry,0,{1050,900});
    const Id queueB=queues.debugSpawn(Kind::Foundry,0,{1320,900});
    for(int item=0;item<Simulation::MaxQueue-1;++item) {
        check(send(queues,CommandType::Train,0,{queueA},{},0,Kind::Striker).accepted,"first queue setup remains legal");
        check(send(queues,CommandType::Train,0,{queueB},{},0,Kind::Striker).accepted,"second queue setup remains legal");
    }
    const auto queuesBefore=queues.stateHash();const int queuesOre=queues.players()[0].ore;
    check(!send(queues,CommandType::AutoTrain,0,{}, {},0,Kind::Striker,3).accepted&&queues.stateHash()==queuesBefore&&queues.players()[0].ore==queuesOre,"insufficient aggregate queue space rejects the whole batch");
}

void workerChoiceAndProducerGates() {
    auto idleFirst=fixture();idleFirst.debugResources(0,10000);
    const Id idle=idleFirst.debugSpawn(Kind::Worker,0,{920,620});
    const Vec2 idleSite=validPlacement(idleFirst,0,Kind::Foundry);
    const auto idlePlan=idleFirst.autoBuildStatus(0,Kind::Foundry,&idleSite);
    check(idlePlan.accepted&&idlePlan.worker==idle,"automatic construction prefers a reachable idle Drudge over miners");
    check(send(idleFirst,CommandType::AutoBuild,0,{},idleSite,0,Kind::Foundry).accepted&&idleFirst.find(idle)->order==Order::Construct&&idleFirst.find(idle)->resumeGather,"idle automatic builder records the post-build mining intent");
    const Id automaticFoundation=idleFirst.find(idle)->target;
    waitForConstruction(idleFirst,automaticFoundation,"automatic construction completes through ordinary simulation time");
    check(idleFirst.find(idle)->order==Order::Gather&&idleFirst.find(idle)->resourceTarget,"automatic idle builder starts mining after completing its structure");

    auto manualIdle=fixture();manualIdle.debugResources(0,10000);
    const Id manualWorker=manualIdle.debugSpawn(Kind::Worker,0,{920,620});
    const Vec2 manualSite=validPlacement(manualIdle,0,Kind::Foundry);
    check(send(manualIdle,CommandType::Build,0,{manualWorker},manualSite,0,Kind::Foundry).accepted&&manualIdle.find(manualWorker)->order==Order::Construct&&!manualIdle.find(manualWorker)->resumeGather,"manual construction keeps an idle builder's prior semantics");
    const Id manualFoundation=manualIdle.find(manualWorker)->target;
    waitForConstruction(manualIdle,manualFoundation,"manual construction completes through ordinary simulation time");
    check(manualIdle.find(manualWorker)->order==Order::Idle,"manual idle builder remains idle after completing its structure");

    auto miningFallback=fixture();miningFallback.debugResources(0,10000);
    const Vec2 miningSite=validPlacement(miningFallback,0,Kind::Foundry);
    const auto miningPlan=miningFallback.autoBuildStatus(0,Kind::Foundry,&miningSite);
    check(miningPlan.accepted&&miningPlan.worker&&miningFallback.find(miningPlan.worker)->order==Order::Gather,"automatic construction falls back to a reachable miner when no idle Drudge exists");
    check(send(miningFallback,CommandType::AutoBuild,0,{},miningSite,0,Kind::Foundry).accepted&&miningFallback.find(miningPlan.worker)->order==Order::Construct&&miningFallback.find(miningPlan.worker)->resumeGather,"mining fallback records that gathering should resume");
    Id incompleteFoundry=0;for(const auto& entity:miningFallback.entities())if(entity.team==0&&entity.kind==Kind::Foundry&&entity.progress<1)incompleteFoundry=entity.id;
    check(incompleteFoundry&&!miningFallback.autoTrainStatus(0,Kind::Striker,1,incompleteFoundry).accepted,"an incomplete pinned producer is rejected");

    auto busy=fixture();busy.debugResources(0,10000);
    const Vec2 busySite=validPlacement(busy,0,Kind::Foundry);
    const auto workers=ids(busy,0,Kind::Worker);
    check(send(busy,CommandType::Move,0,workers,{1100,1000}).accepted,"busy-worker fixture issues an ordinary move");
    const auto busyBefore=busy.stateHash();
    check(!busy.autoBuildStatus(0,Kind::Foundry,&busySite).accepted&&busy.stateHash()==busyBefore,"automatic construction never steals a busy Drudge");

    auto pins=fixture();pins.debugResources(0,10000);
    const Id ownFoundry=pins.debugSpawn(Kind::Foundry,0,{1050,900});
    const Id enemyFoundry=pins.debugSpawn(Kind::Foundry,1,{3800,3900});
    const Id wrongKind=pins.debugSpawn(Kind::Laboratory,0,{1320,900});
    const Id deadFoundry=pins.debugSpawn(Kind::Foundry,0,{1590,900});
    edit(pins,deadFoundry)->hp=0;
    check(!pins.autoTrainStatus(0,Kind::Striker,1,enemyFoundry).accepted,"foreign pinned producer is rejected");
    check(!pins.autoTrainStatus(0,Kind::Striker,1,wrongKind).accepted,"wrong-kind pinned producer is rejected");
    check(!pins.autoTrainStatus(0,Kind::Striker,1,deadFoundry).accepted,"dead pinned producer is rejected");
    check(!pins.autoTrainStatus(0,Kind::Bastion,1).accepted,"technology tier gates automatic training");
    check(pins.autoTrainStatus(0,Kind::Striker,1,ownFoundry).accepted,"owned completed eligible producer is accepted");
    edit(pins,ownFoundry)->nextQueueId=std::numeric_limits<Id>::max();
    const auto exhaustedBefore=pins.stateHash();
    check(!send(pins,CommandType::AutoTrain,0,{}, {},ownFoundry,Kind::Striker,1).accepted&&pins.stateHash()==exhaustedBefore,"exhausted producer job IDs reject without wrapping or mutation");
}

void stableCancellationAndResearch() {
    auto simulation=fixture();simulation.debugResources(0,10000);
    const Id foundry=simulation.debugSpawn(Kind::Foundry,0,{1050,900});
    check(send(simulation,CommandType::Train,0,{foundry},{},0,Kind::Striker).accepted,"first cancellation fixture job queues");
    check(send(simulation,CommandType::Train,0,{foundry},{},0,Kind::Lancer).accepted,"second cancellation fixture job queues");
    check(send(simulation,CommandType::Train,0,{foundry},{},0,Kind::Scout).accepted,"third cancellation fixture job queues");
    const auto original=simulation.find(foundry)->queue;
    check(original.size()==3&&original[0].id<original[1].id&&original[1].id<original[2].id,"fixture jobs receive stable increasing IDs");
    int ore=simulation.players()[0].ore;
    check(send(simulation,CommandType::CancelQueue,0,{foundry},{},original[1].id,Kind::Worker,0).accepted,"middle job cancels by stable ID even when the supplied legacy index is zero");
    check(simulation.players()[0].ore==ore+definition(Kind::Lancer).cost&&simulation.find(foundry)->queue.size()==2&&simulation.find(foundry)->queue[0].id==original[0].id&&simulation.find(foundry)->queue[1].id==original[2].id,"middle cancellation refunds its exact cost and preserves neighboring jobs");
    ore=simulation.players()[0].ore;
    check(send(simulation,CommandType::CancelQueue,0,{foundry},{},original[2].id,Kind::Worker,0).accepted,"shifted tail job remains addressable by its original ID");
    check(simulation.players()[0].ore==ore+definition(Kind::Scout).cost&&simulation.find(foundry)->queue.size()==1&&simulation.find(foundry)->queue.front().id==original[0].id,"shifted cancellation refunds and removes the intended job only");
    const auto missingBefore=simulation.stateHash();
    check(!send(simulation,CommandType::CancelQueue,0,{foundry},{},original[2].id,Kind::Worker,0).accepted&&simulation.stateHash()==missingBefore,"stale job ID cannot cancel a replacement or neighbor");

    auto research=fixture();research.debugResources(0,10000);
    const Id firstLab=research.debugSpawn(Kind::Laboratory,0,{1050,900});
    const Id secondLab=research.debugSpawn(Kind::Laboratory,0,{1320,900});
    const auto plan=research.autoResearchStatus(0,1);
    check(plan.accepted&&plan.assignments.size()==1&&plan.assignments.front().producer==firstLab,"equal research queues use the stable lower producer ID");
    check(send(research,CommandType::AutoResearch,0,{}, {},0,Kind::Worker,1).accepted&&research.find(firstLab)->queue.size()==1,"automatic research queues at the planned Resonator");
    const int oreAfter=research.players()[0].ore;const auto duplicateBefore=research.stateHash();
    check(!send(research,CommandType::AutoResearch,0,{}, {},secondLab,Kind::Worker,1).accepted&&research.players()[0].ore==oreAfter&&research.stateHash()==duplicateBefore,"duplicate research is rejected across all Resonators without charging ore");
    check(!research.autoResearchStatus(0,0,firstLab+999999).accepted,"unknown research pin is rejected");
    check(research.autoRallyStatus(0,Kind::Laboratory,secondLab).accepted&&!research.autoRallyStatus(0,Kind::Foundry,secondLab).accepted,"rally pin must match the requested production type");
    check(send(research,CommandType::AutoRally,0,{}, {1700,1450},secondLab,Kind::Laboratory).accepted&&research.find(secondLab)->rally.x==1700&&research.find(secondLab)->rally.y==1450,"pinned automatic rally changes only its eligible owned producer");
}

void persistentArmyRallyAndOverrides() {
    auto simulation=fixture();
    const Vec2 firstDefault{1800,1500},secondDefault{2050,1650},overridePoint{1250,1320};
    const Id hq=ids(simulation,0,Kind::Headquarters).front();
    check(!simulation.autoRallyStatus(0,Kind::Foundry,hq,true).accepted,"USE DEFAULT requires an existing team default and a combat producer");
    const auto invalidBefore=simulation.stateHash();
    check(!send(simulation,CommandType::AutoRally,0,{}, {},0,Kind::Resource,1).accepted&&simulation.stateHash()==invalidBefore,"USE DEFAULT requires a pinned producer without mutating state");
    check(simulation.autoRallyStatus(0).accepted,"team rally can be chosen before a combat producer exists");
    check(send(simulation,CommandType::AutoRally,0,{},firstDefault,0,Kind::Resource).accepted,"team rally is set before construction");
    check(simulation.players()[0].armyRallySet&&simulation.players()[0].armyRally.x==firstDefault.x&&simulation.players()[0].armyRally.y==firstDefault.y,"team stores its persistent army rally");

    const Id inherited=simulation.debugSpawn(Kind::Foundry,0,{1050,900});
    const Id independent=simulation.debugSpawn(Kind::MotorPool,0,{1320,900});
    check(simulation.find(inherited)->rally.x==firstDefault.x&&!simulation.find(inherited)->rallyOverride&&
          simulation.find(independent)->rally.x==firstDefault.x&&!simulation.find(independent)->rallyOverride,"future combat producers inherit the team default");
    check(send(simulation,CommandType::AutoRally,0,{},overridePoint,inherited,Kind::Foundry).accepted&&simulation.find(inherited)->rallyOverride,"a pinned rally becomes an explicit facility override");
    check(send(simulation,CommandType::AutoRally,0,{},secondDefault,0,Kind::Resource).accepted,"team rally can be changed later");
    check(simulation.find(inherited)->rally.x==overridePoint.x&&simulation.find(independent)->rally.x==secondDefault.x,"later team rally updates inheriting producers without replacing explicit overrides");
    check(simulation.autoRallyStatus(0,Kind::Foundry,inherited,true).accepted&&
          send(simulation,CommandType::AutoRally,0,{}, {},inherited,Kind::Foundry,1).accepted,"an explicit producer can return to the team default");
    check(!simulation.find(inherited)->rallyOverride&&simulation.find(inherited)->rally.x==secondDefault.x,"USE DEFAULT clears the override and copies the current team rally");

    const Vec2 oneShot{1450,1180};
    check(send(simulation,CommandType::AutoRally,0,{},oneShot,0,Kind::Foundry).accepted&&simulation.find(inherited)->rallyOverride,"legacy concrete-kind global rally remains a one-shot explicit override");
    const Id laterFoundry=simulation.debugSpawn(Kind::Foundry,0,{1590,900});
    check(!simulation.find(laterFoundry)->rallyOverride&&simulation.find(laterFoundry)->rally.x==secondDefault.x,"one-shot concrete rally does not replace the persistent default for later facilities");
    check(send(simulation,CommandType::Rally,0,{hq},{1400,1100}).accepted&&simulation.find(hq)->rallyOverride,"Anchor can hold an explicit worker rally");
    check(simulation.autoRallyStatus(0,Kind::Headquarters,hq,true).accepted&&send(simulation,CommandType::AutoRally,0,{}, {},hq,Kind::Headquarters,1).accepted,"Anchor USE DEFAULT restores automatic mining without requiring an army rally");
    check(!simulation.find(hq)->rallyOverride,"Anchor mining reset clears its worker rally override");
}

void freshWorkerMiningAssignment() {
    auto balanced=fixture();balanced.debugResources(0,10000);
    const Id hq=ids(balanced,0,Kind::Headquarters).front();
    std::vector<Id> originalWorkers=ids(balanced,0,Kind::Worker);
    std::vector<Id> resources=ids(balanced,-1,Kind::Resource);
    check(send(balanced,CommandType::Train,0,{hq},{},0,Kind::Worker).accepted,"Anchor queues a fresh Drudge");
    advance(balanced,definition(Kind::Worker).buildTime+Simulation::Step);
    const auto workersAfter=ids(balanced,0,Kind::Worker);check(workersAfter.size()==originalWorkers.size()+1,"training creates exactly one new Drudge");
    const Id fresh=*std::max_element(workersAfter.begin(),workersAfter.end());const Entity* worker=balanced.find(fresh);
    check(worker&&worker->order==Order::Gather&&worker->resourceTarget,"a fresh Drudge without a worker rally receives a reachable ore job immediately");
    int priorLoad=0,minimumLoad=1000;
    for(Id resource:resources) {
        int load=0;for(Id id:originalWorkers)if(balanced.find(id)->resourceTarget==resource)++load;
        minimumLoad=std::min(minimumLoad,load);if(resource==worker->resourceTarget)priorLoad=load;
    }
    check(priorLoad<=minimumLoad+1,"spawn assignment balances existing mining load while retaining a nearby economy route");
    const Entity* chosen=balanced.find(worker->resourceTarget);
    check(chosen&&std::hypot(chosen->pos.x-balanced.find(hq)->pos.x,chosen->pos.y-balanced.find(hq)->pos.y)<800,"spawn assignment prefers ore near an accessible drop-off");

    auto isolated=fixture();isolated.debugResources(0,10000);
    const Id isolatedHQ=ids(isolated,0,Kind::Headquarters).front();const auto existing=ids(isolated,0,Kind::Worker);
    check(send(isolated,CommandType::Hold,0,{existing.front()},{},0,Kind::Worker).accepted,"existing worker receives a manual order");
    for(const auto& entity:isolated.entities())if(entity.kind==Kind::Resource)edit(isolated,entity.id)->resource=0;
    check(send(isolated,CommandType::Train,0,{isolatedHQ},{},0,Kind::Worker).accepted,"no-ore fixture queues a Drudge");
    advance(isolated,definition(Kind::Worker).buildTime+Simulation::Step);
    const auto isolatedWorkers=ids(isolated,0,Kind::Worker);
    const Id unassigned=*std::max_element(isolatedWorkers.begin(),isolatedWorkers.end());
    check(isolated.find(existing.front())->order==Order::Hold,"newborn assignment never redirects a manually ordered existing worker");
    check(isolated.find(unassigned)->order!=Order::Gather&&isolated.find(unassigned)->resourceTarget==0&&isolated.alert()=="Drudge ready; no reachable ore job.","a fresh Drudge reports honestly when no valid ore cycle exists");

    auto explicitRally=fixture();explicitRally.debugResources(0,10000);
    const Id rallyHQ=ids(explicitRally,0,Kind::Headquarters).front();
    check(send(explicitRally,CommandType::Rally,0,{rallyHQ},{1700,1200}).accepted&&explicitRally.find(rallyHQ)->rallyOverride,"manual worker rally records an explicit producer override");
    const std::size_t before=ids(explicitRally,0,Kind::Worker).size();
    check(send(explicitRally,CommandType::Train,0,{rallyHQ},{},0,Kind::Worker).accepted,"explicit-rally fixture queues a Drudge");
    advance(explicitRally,definition(Kind::Worker).buildTime+Simulation::Step);
    const auto explicitWorkers=ids(explicitRally,0,Kind::Worker);check(explicitWorkers.size()==before+1,"explicit-rally worker completes");
    const Entity* rallied=explicitRally.find(*std::max_element(explicitWorkers.begin(),explicitWorkers.end()));
    check(rallied->order==Order::Move&&rallied->resourceTarget==0,"explicit worker rally suppresses automatic mining for that producer");
    check(send(explicitRally,CommandType::AutoRally,0,{}, {},rallyHQ,Kind::Headquarters,1).accepted,"Anchor worker rally can be reset without a combat rally default");
    check(send(explicitRally,CommandType::Train,0,{rallyHQ},{},0,Kind::Worker).accepted,"reset-rally fixture queues another Drudge");
    advance(explicitRally,definition(Kind::Worker).buildTime+Simulation::Step);
    const auto resetWorkers=ids(explicitRally,0,Kind::Worker);const Entity* resetMiner=explicitRally.find(*std::max_element(resetWorkers.begin(),resetWorkers.end()));
    check(resetMiner->order==Order::Gather&&resetMiner->resourceTarget,"restoring Anchor auto-mining makes the next fresh Drudge gather again");

    auto oreRally=fixture();oreRally.debugResources(0,10000);
    const Id oreHQ=ids(oreRally,0,Kind::Headquarters).front();const Id ore=ids(oreRally,-1,Kind::Resource).front();
    check(send(oreRally,CommandType::Rally,0,{oreHQ},oreRally.find(ore)->pos).accepted,"worker rally accepts an explored ore deposit");
    check(send(oreRally,CommandType::Train,0,{oreHQ},{},0,Kind::Worker).accepted,"ore-rally fixture queues a Drudge");
    advance(oreRally,definition(Kind::Worker).buildTime+Simulation::Step);
    const auto oreWorkers=ids(oreRally,0,Kind::Worker);const Entity* oreMiner=oreRally.find(*std::max_element(oreWorkers.begin(),oreWorkers.end()));
    check(oreMiner->order==Order::Gather&&oreMiner->resourceTarget==ore,"an explicit rally directly on ore gives the fresh Drudge that exact reachable mining job");
}

void deferredWorkerAssignmentPersistence() {
    auto simulation=fixture();simulation.debugResources(0,100000);
    for(int index=0;index<9;++index)simulation.debugSpawn(Kind::Headquarters,0,
        {900.0f+(index%4)*850.0f,300.0f+(index/4)*500.0f});
    const auto headquarters=ids(simulation,0,Kind::Headquarters);check(headquarters.size()==10,"budget fixture has ten worker producers");
    for(Id producer:headquarters)check(send(simulation,CommandType::Train,0,{producer},{},0,Kind::Worker).accepted,"simultaneous worker queue setup succeeds");
    std::vector<Id> deferredProducers;
    for(int step=0;step<300&&deferredProducers.size()<2;++step) {
        simulation.update(Simulation::Step);
        deferredProducers.clear();
        for(Id producer:headquarters)if(const Entity* entity=simulation.find(producer);entity&&!entity->queue.empty()&&
            entity->queue.front().kind==Kind::Worker&&entity->queue.front().remaining<=0&&!entity->queue.front().assignmentCandidates.empty()) {
            deferredProducers.push_back(producer);
        }
    }
    const Id deferredProducer=deferredProducers.empty()?0:deferredProducers.front();
    check(deferredProducer&&simulation.find(deferredProducer)->queue.front().assignmentCursor>=0,"route-budget exhaustion defers a paid worker job with a stable candidate list");
    check(simulation.navigationStats().budgetDeferrals>0,"deferred worker assignment is charged to the shared route-search budget");
    check(deferredProducers.size()>=2,"shared route budget defers later simultaneous worker jobs");
    Entity* changedProducer=edit(simulation,deferredProducers.back());
    check(send(simulation,CommandType::Rally,0,{changedProducer->id},{2600,900}).accepted,"a deferred Anchor accepts a new manual ground rally");
    check(changedProducer->queue.front().assignmentCandidates.empty()&&changedProducer->queue.front().assignmentCursor==0,"changing a worker rally clears stale deferred mining candidates");
    QueueItem& pending=edit(simulation,deferredProducer)->queue.front();
    check(pending.assignmentCandidates.size()>1,"deferred fixture freezes multiple ranked candidate identities");
    pending.assignmentCursor=1; // Represents one previously disproven candidate at a save boundary.
    const auto path=(std::filesystem::temp_directory_path()/"cinderline-deferred-worker-v8.sav").string();
    check(simulation.save(path),"mid-deferral worker assignment saves");Simulation loaded;check(loaded.load(path),"mid-deferral worker assignment loads");
    check(loaded.stateHash()==simulation.stateHash(),"save-load preserves the assignment cursor and frozen candidate identities");
    bool completed=false;
    for(int step=0;step<80&&!completed;++step) {
        simulation.update(Simulation::Step);loaded.update(Simulation::Step);
        check(loaded.stateHash()==simulation.stateHash(),"deferred assignment resumes deterministically after load");
        completed=std::all_of(headquarters.begin(),headquarters.end(),[&](Id producer){return simulation.find(producer)->queue.empty()&&loaded.find(producer)->queue.empty();});
    }
    check(completed&&simulation.players()[0].stats.produced==10,"every budget-deferred paid worker job resumes instead of starving behind earlier candidates");
    std::filesystem::remove(path);
}

void replayAndPersistence() {
    auto prepare=[] {
        auto simulation=fixture();simulation.debugResources(0,10000);
        simulation.debugSpawn(Kind::Processor,0,{900,1300});
        simulation.debugSpawn(Kind::Foundry,0,{1050,900});
        simulation.debugSpawn(Kind::Foundry,0,{1320,900});
        simulation.debugSpawn(Kind::Laboratory,0,{1590,900});
        return simulation;
    };
    auto simulation=prepare();
    check(send(simulation,CommandType::AutoTrain,0,{}, {},0,Kind::Striker,5).accepted,"persistence fixture queues a distributed batch");
    check(send(simulation,CommandType::AutoResearch,0,{}, {},0,Kind::Worker,1).accepted,"persistence fixture queues research");
    check(send(simulation,CommandType::AutoRally,0,{}, {1800,1500},0,Kind::Foundry).accepted,"persistence fixture records a global rally");
    check(send(simulation,CommandType::AutoRally,0,{}, {2100,1700},0,Kind::Resource).accepted,"persistence fixture records the persistent army rally");
    const auto recording=simulation.recording();
    check(recording.size()==4&&recording[0].command.type==CommandType::AutoTrain&&recording[0].command.units.empty()&&
          recording[0].command.queueIndex==5&&recording[0].command.queueMode==CommandQueueMode::Replace,
          "recording retains the complete replacing automatic batch request");

    auto replay=prepare();
    for(const auto& item:recording)check(replay.command(item.command).accepted,"recorded automatic command replays successfully");
    check(replay.stateHash()==simulation.stateHash(),"automatic allocation and stable job IDs replay deterministically");

    const auto path=(std::filesystem::temp_directory_path()/"cinderline-production-rallies-v13.sav").string();
    check(simulation.save(path),"stable production jobs save");
    Simulation loaded;check(loaded.load(path),"stable production jobs load");
    check(loaded.stateHash()==simulation.stateHash(),"save-load preserves the full authoritative hash");
    check(loaded.players()[0].armyRallySet&&loaded.players()[0].armyRally.x==simulation.players()[0].armyRally.x,"save-load preserves the persistent team rally");
    for(const auto& source:simulation.entities())if(source.team==0&&definition(source.kind).building) {
        const Entity* restored=loaded.find(source.id);check(restored&&restored->nextQueueId==source.nextQueueId&&restored->queue.size()==source.queue.size(),"save-load preserves each producer's next job ID and queue length");
        check(restored->rallyOverride==source.rallyOverride&&restored->rally.x==source.rally.x&&restored->rally.y==source.rally.y,"save-load preserves facility rally inheritance and explicit overrides");
        for(std::size_t index=0;index<source.queue.size();++index)check(restored->queue[index].id==source.queue[index].id,"save-load preserves every stable queue job ID");
    }
    check(loaded.recording().size()==recording.size()&&loaded.recording()[0].command.type==CommandType::AutoTrain&&
          loaded.recording()[0].command.queueIndex==5&&loaded.recording()[0].command.queueMode==CommandQueueMode::Replace,
          "save-load preserves automatic replay commands and their queue mode");

    const auto current=readLines(path);const auto marker=std::find(current.begin(),current.end(),"RALLY_STATE 1");
    check(marker!=current.end()&&marker+4<current.end(),"current save appends a complete tagged rally-state tail");
    const std::size_t markerIndex=static_cast<std::size_t>(marker-current.begin());
    auto missing=current;missing.erase(missing.begin()+static_cast<std::ptrdiff_t>(markerIndex),missing.end());writeLines(path,missing);
    Simulation incomplete;const auto incompleteBefore=incomplete.stateHash();
    check(!incomplete.load(path)&&incomplete.stateHash()==incompleteBefore,"version thirteen requires its complete rally, order-queue, sustained-order, and formation-order tail");
    auto legacy=current;stripSaveThirteenFormation(legacy);
    legacy.erase(legacy.begin()+static_cast<std::ptrdiff_t>(markerIndex),legacy.end());
    stripSaveElevenRecordingModes(legacy);legacy.front()="CINDERLINE 7";
    {std::istringstream input(legacy.at(1));int map=0,ai=0,length=0,players=0;std::uint32_t seed=0;float aggression=0;
     input>>map>>seed>>ai>>aggression>>length>>players;legacy[1]=std::to_string(map)+" "+std::to_string(seed)+" "+std::to_string(ai)+" "+std::to_string(aggression);}
    {std::istringstream input(legacy.at(2));std::vector<std::string> values;std::string value;while(input>>value)values.push_back(value);
     check(values.size()==6,"current fixture stores elimination state");values.pop_back();std::ostringstream output;
     for(std::size_t index=0;index<values.size();++index)output<<(index?" ":"")<<values[index];legacy[2]=output.str();}
    writeLines(path,legacy);
    Simulation migrated;check(migrated.load(path),"a pre-rally version-seven save without the optional tail still loads");
    check(!migrated.players()[0].armyRallySet,"missing rally tail starts without an invented team default");
    for(const auto& source:simulation.entities())if(source.team==0&&
        (source.kind==Kind::Headquarters||source.kind==Kind::Foundry||source.kind==Kind::MotorPool||source.kind==Kind::Laboratory)) {
        const Entity* restored=migrated.find(source.id);
        check(restored&&restored->rally.x==source.rally.x&&restored->rally.y==source.rally.y,"legacy migration preserves every saved facility rally position");
    }
    auto rejects=[&](std::vector<std::string> lines,const std::string& message) {
        writeLines(path,lines);Simulation untouched=fixture();const auto before=untouched.stateHash();
        check(!untouched.load(path)&&untouched.stateHash()==before,message);
    };
    auto unknown=current;*std::find(unknown.begin(),unknown.end(),"RALLY_STATE 1")="UNKNOWN_RALLY 1";rejects(unknown,"unknown rally tail is rejected atomically");
    auto truncated=current;truncated.resize(markerIndex+2);rejects(truncated,"truncated rally tail is rejected atomically");
    auto duplicated=current;duplicated.insert(duplicated.end(),current.begin()+static_cast<std::ptrdiff_t>(markerIndex),current.end());rejects(duplicated,"duplicate rally tail is rejected as trailing data");
    auto invalid=current;const auto invalidMarker=std::find(invalid.begin(),invalid.end(),"RALLY_STATE 1");
    auto& invalidEntity=invalid[static_cast<std::size_t>(invalidMarker-invalid.begin())+4];invalidEntity=invalidEntity.substr(0,invalidEntity.find(' '))+" 2";
    rejects(invalid,"malformed facility override state is rejected atomically");
    const auto& savedEntities=simulation.entities();
    const auto workerPosition=std::find_if(savedEntities.begin(),savedEntities.end(),[](const Entity& entity){return entity.kind==Kind::Worker;});
    auto nonProducer=current;const std::size_t workerIndex=static_cast<std::size_t>(workerPosition-savedEntities.begin());
    nonProducer[markerIndex+4+workerIndex]=replaceField(nonProducer[markerIndex+4+workerIndex],1,"1");
    rejects(nonProducer,"non-production entities cannot carry facility rally overrides");
    const auto inheritedProducer=std::find_if(savedEntities.begin(),savedEntities.end(),[](const Entity& entity){return entity.team==0&&entity.kind==Kind::Laboratory&&!entity.rallyOverride;});
    check(inheritedProducer!=savedEntities.end(),"semantic rally corruption fixture has an inheriting combat producer");
    const std::string producerPrefix=std::to_string(inheritedProducer->id)+" "+std::to_string(static_cast<int>(inheritedProducer->kind))+" "+std::to_string(inheritedProducer->team)+" ";
    const auto producerRow=std::find_if(current.begin(),marker,[&](const std::string& line){return line.rfind(producerPrefix,0)==0;});
    check(producerRow!=marker,"semantic rally corruption fixture locates the producer entity row");
    auto inconsistent=current;const std::size_t producerRowIndex=static_cast<std::size_t>(producerRow-current.begin());
    inconsistent.at(producerRowIndex)=replaceField(inconsistent.at(producerRowIndex),7,"123");
    rejects(inconsistent,"an inheriting combat producer must match the persisted team rally");
    std::filesystem::remove(path);
}

}

int main() {
    const std::vector<std::pair<std::string,void(*)()>> tests{
        {"remaining-time allocation and atomic gates",remainingTimeAllocationAndAtomicGates},
        {"worker choice and producer gates",workerChoiceAndProducerGates},
        {"stable cancellation and research",stableCancellationAndResearch},
        {"persistent army rally and overrides",persistentArmyRallyAndOverrides},
        {"fresh worker mining assignment",freshWorkerMiningAssignment},
        {"deferred worker assignment persistence",deferredWorkerAssignmentPersistence},
        {"replay and persistence",replayAndPersistence},
    };
    int failed=0;
    for(const auto& test:tests)try{test.second();std::cout<<"PASS "<<test.first<<'\n';}catch(const std::exception& exception){++failed;std::cerr<<"FAIL "<<test.first<<": "<<exception.what()<<'\n';}
    std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
    return failed?1:0;
}
