#include "Sim/Network.h"
#include "MatchSnapshotMetrics.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cinder;

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::array<Kind,8> Mix{{Kind::Striker,Kind::Lancer,Kind::Scout,Kind::Bastion,
    Kind::Mortar,Kind::Mender,Kind::Kite,Kind::Worker}};
constexpr int RequiredPatrolLegs = 4;
constexpr int MaximumTicks = 9600;

float distance(Vec2 a,Vec2 b) { return std::hypot(a.x-b.x,a.y-b.y); }
bool same(Vec2 a,Vec2 b) { return a.x==b.x&&a.y==b.y; }
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
void word(std::uint64_t& hash,std::uint64_t value) {
    for(int byte=0;byte<8;++byte){hash^=(value>>(byte*8))&255;hash*=1099511628211ull;}
}
void point(std::uint64_t& hash,Vec2 value) {
    for(float number:{value.x,value.y}) {
        std::uint32_t bits=0;std::memcpy(&bits,&number,sizeof(bits));word(hash,bits);
    }
}
std::uint64_t recordingHash(const Simulation& simulation) {
    std::uint64_t hash=1469598103934665603ull;
    for(const auto& recorded:simulation.recording()) {
        const auto& command=recorded.command;
        word(hash,recorded.tick);word(hash,static_cast<int>(command.type));word(hash,command.team);
        word(hash,command.units.size());for(Id id:command.units)word(hash,id);
        point(hash,command.point);word(hash,command.target);word(hash,static_cast<int>(command.kind));
        word(hash,command.queueIndex);word(hash,static_cast<int>(command.queueMode));
        word(hash,static_cast<int>(command.spacing));word(hash,command.hasArrivalFacing);
        std::uint32_t facingBits=0;std::memcpy(&facingBits,&command.arrivalFacing,sizeof(facingBits));
        word(hash,facingBits);
    }
    return hash;
}
void send(Simulation& simulation,CommandType type,std::vector<Id> ids,Vec2 destination={},
          Id target=0,CommandQueueMode mode=CommandQueueMode::Replace) {
    const auto result=simulation.command({type,0,std::move(ids),destination,target,Kind::Worker,0,mode});
    if(!result.accepted)throw std::runtime_error("Fixture command rejected: "+result.message);
}
Simulation fixture() {
    Simulation simulation;simulation.reset({0,0x512E5u,false,1});
    // Explicit synthetic open terrain, never a paid-match or production claim.
    const_cast<std::vector<Entity>&>(simulation.entities()).clear();
    const_cast<std::vector<Obstacle>&>(simulation.obstacles()).clear();
    simulation.debugSpawn(Kind::Headquarters,0,{250,250});
    simulation.debugSpawn(Kind::Headquarters,1,{4550,4550});
    return simulation;
}
std::vector<Id> spawnMix(Simulation& simulation,int count) {
    std::vector<Id> ids;const int columns=static_cast<int>(std::ceil(std::sqrt(count)));
    for(int index=0;index<count;++index)
        ids.push_back(simulation.debugSpawn(Mix[index%Mix.size()],0,
            {600.0f+76.0f*(index%columns),1500.0f+76.0f*(index/columns)}));
    return ids;
}
struct Result {
    std::string mode;
    int mobiles=0,recipients=0,completed=0,ticks=0,movingLeaderTicks=0,followersMovedDuringLeaderRoute=0;
    std::uint64_t state=0,trajectory=1469598103934665603ull,recording=0;
    double commandMs=0;
    std::vector<double> steps;
    baseline::SnapshotMetrics snapshots;
    bool acceptedPointsUnchanged=true;
};
void advance(Simulation& simulation,Result& result,
             std::array<net::ViewMemory,Simulation::MaxPlayers>& views) {
    const auto started=Clock::now();const auto previous=simulation.tick();
    simulation.update(Simulation::Step);
    result.steps.push_back(std::chrono::duration<double,std::milli>(Clock::now()-started).count());
    require(simulation.tick()==previous+1&&simulation.winner()==-1,"Fixture match ended or failed to advance");
    ++result.ticks;word(result.trajectory,simulation.stateHash());
    if(result.ticks%2==0)result.snapshots.sample(simulation,views);
}
Result runPatrol(int count) {
    auto simulation=fixture();auto ids=spawnMix(simulation,count);
    Result result;result.mode="patrol";result.mobiles=count;result.recipients=count;
    const auto started=Clock::now();send(simulation,CommandType::Patrol,ids,{3100,1800});
    result.commandMs=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
    std::vector<Vec2> origins,ends;std::vector<bool> outbound;std::vector<int> legs(ids.size(),0);
    for(std::size_t index=0;index<ids.size();++index) {
        const Id id=ids[index];
        const auto& entity=*simulation.find(id);
        require(entity.order==Order::Patrol,"Patrol command did not install its order");
        origins.push_back(entity.sustained.patrolOrigin);ends.push_back(entity.sustained.patrolDestination);
        outbound.push_back(entity.sustained.patrolTowardDestination);
        for(std::size_t earlier=0;earlier<index;++earlier)
            require(distance(ends[earlier],ends[index])>=definition(entity.kind).radius+
                    definition(simulation.find(ids[earlier])->kind).radius,
                    "Patrol destinations overlap instead of retaining distinct formation slots");
    }
    std::array<net::ViewMemory,Simulation::MaxPlayers> views;
    while(result.ticks<MaximumTicks&&result.completed<count) {
        std::vector<Vec2> previousPositions;
        for(Id id:ids)previousPositions.push_back(simulation.find(id)->pos);
        advance(simulation,result,views);result.completed=0;
        for(std::size_t index=0;index<ids.size();++index) {
            const auto* entity=simulation.find(ids[index]);
            require(entity&&entity->alive()&&entity->order==Order::Patrol,"Patrol recipient lost its persistent order");
            result.acceptedPointsUnchanged=result.acceptedPointsUnchanged&&
                same(origins[index],entity->sustained.patrolOrigin)&&same(ends[index],entity->sustained.patrolDestination);
            if(outbound[index]!=entity->sustained.patrolTowardDestination) {
                const Vec2 reached=outbound[index]?ends[index]:origins[index];
                // Travel may flip at the beginning of its movement update and
                // spend the same tick moving away on the next leg. The first
                // 160-unit run observed a Scout at 19.939 before the flip and
                // 32.171 afterward, exactly one 12.25-unit movement step later.
                const float afterTickBound=20.001f+definition(entity->kind).speed*Simulation::Step;
                if(distance(previousPositions[index],reached)>20.001f&&distance(entity->pos,reached)>afterTickBound) {
                    std::ostringstream detail;
                    detail<<"Patrol reversal lacks a sampled endpoint visit: id="<<entity->id
                          <<" kind="<<static_cast<int>(entity->kind)<<" tick="<<result.ticks
                          <<" before="<<distance(previousPositions[index],reached)
                          <<" after="<<distance(entity->pos,reached)
                          <<" displacement="<<distance(previousPositions[index],entity->pos)
                          <<" speed_step="<<definition(entity->kind).speed*Simulation::Step;
                    throw std::runtime_error(detail.str());
                }
                outbound[index]=entity->sustained.patrolTowardDestination;++legs[index];
            }
            result.completed+=legs[index]>=RequiredPatrolLegs;
        }
    }
    require(result.completed==count&&result.acceptedPointsUnchanged,"Patrol failed two complete laps or changed an endpoint");
    result.state=simulation.stateHash();result.recording=recordingHash(simulation);return result;
}
Result runEscort(int count) {
    auto simulation=fixture();const Id leader=simulation.debugSpawn(Kind::Bastion,0,{1800,2300});
    auto ids=spawnMix(simulation,count-1);
    send(simulation,CommandType::Move,{leader},{2800,2300});
    send(simulation,CommandType::Move,{leader},{2800,3300},0,CommandQueueMode::Append);
    send(simulation,CommandType::Move,{leader},{1800,3300},0,CommandQueueMode::Append);
    const Entity before=*simulation.find(leader);auto selection=ids;selection.push_back(leader);
    Result result;result.mode="escort";result.mobiles=count;result.recipients=count-1;
    const auto started=Clock::now();send(simulation,CommandType::Escort,selection,{},leader);
    result.commandMs=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
    const auto& after=*simulation.find(leader);
    require(before.order==after.order&&same(before.goal,after.goal)&&before.futureOrders.size()==after.futureOrders.size(),
            "Escort retasked its selected leader");
    std::vector<Vec2> offsets,starts;std::vector<bool> followedWhileMoving(ids.size(),false);
    for(std::size_t index=0;index<ids.size();++index) {
        const auto& entity=*simulation.find(ids[index]);
        const Vec2 offset=entity.sustained.escortOffset;offsets.push_back(offset);starts.push_back(entity.pos);
        require(distance(offset,{})<=Simulation::MaxEscortOffset&&
                distance(offset,{})>=definition(before.kind).radius+definition(entity.kind).radius,
                "Escort offset is outside the bound or inside the leader footprint");
        for(std::size_t earlier=0;earlier<index;++earlier)
            require(distance(offsets[earlier],offset)>=definition(entity.kind).radius+
                    definition(simulation.find(ids[earlier])->kind).radius,
                    "Escort offsets overlap instead of retaining distinct slots");
    }
    std::array<net::ViewMemory,Simulation::MaxPlayers> views;int stableTicks=0;
    while(result.ticks<MaximumTicks&&stableTicks<20) {
        advance(simulation,result,views);result.completed=0;
        const auto& target=*simulation.find(leader);
        const bool leaderMoving=target.order!=Order::Idle;
        if(leaderMoving)++result.movingLeaderTicks;
        for(std::size_t index=0;index<ids.size();++index) {
            const auto* entity=simulation.find(ids[index]);
            require(entity&&entity->alive()&&entity->order==Order::Escort&&entity->sustained.escortTarget==leader,
                    "Escort recipient lost its owned leader");
            result.acceptedPointsUnchanged=result.acceptedPointsUnchanged&&same(offsets[index],entity->sustained.escortOffset);
            if(leaderMoving&&distance(entity->pos,starts[index])>=128)followedWhileMoving[index]=true;
            const float radius=definition(entity->kind).radius;
            const Vec2 expected{std::clamp(target.pos.x+offsets[index].x,radius,simulation.worldSize()-radius),
                                std::clamp(target.pos.y+offsets[index].y,radius,simulation.worldSize()-radius)};
            if(target.order==Order::Idle&&distance(entity->pos,expected)<=32)++result.completed;
        }
        stableTicks=result.completed==count-1?stableTicks+1:0;
    }
    result.followersMovedDuringLeaderRoute=static_cast<int>(std::count(followedWhileMoving.begin(),followedWhileMoving.end(),true));
    require(result.followersMovedDuringLeaderRoute==count-1,"Some escorts never moved during the leader route");
    if(stableTicks!=20||!result.acceptedPointsUnchanged) {
        const auto& target=*simulation.find(leader);
        const std::string recoveryPath="artifacts/patrol-escort/escort-jam-"+std::to_string(count)+
            "-"+std::to_string(simulation.stateHash())+".cinder";
        if(simulation.save(recoveryPath))std::cerr<<"Saved synthetic failure state: "<<recoveryPath<<'\n';
        std::cerr<<"Escort convergence diagnostic: count="<<count<<" tick="<<result.ticks
                 <<" completed="<<result.completed<<" leader="<<target.pos.x<<','<<target.pos.y
                 <<" leader_order="<<static_cast<int>(target.order)<<'\n';
        for(Id id:ids) {
            const auto& entity=*simulation.find(id);
            if(distance(entity.pos,entity.goal)<=32)continue;
            std::cerr<<" follower="<<id<<" kind="<<static_cast<int>(entity.kind)
                     <<" pos="<<entity.pos.x<<','<<entity.pos.y
                     <<" goal="<<entity.goal.x<<','<<entity.goal.y
                     <<" offset="<<entity.sustained.escortOffset.x<<','<<entity.sustained.escortOffset.y
                     <<" exhausted="<<entity.navigationExhausted<<" failures="<<entity.navigationFailures
                     <<" path="<<entity.pathIndex<<'/'<<entity.path.size()<<" yield="<<entity.yieldFor<<'\n';
            for(const auto& other:simulation.entities())if(other.alive()&&other.id!=id&&distance(other.pos,entity.pos)<150)
                std::cerr<<"  neighbor="<<other.id<<" kind="<<static_cast<int>(other.kind)
                         <<" pos="<<other.pos.x<<','<<other.pos.y
                         <<" goal="<<other.goal.x<<','<<other.goal.y
                         <<" order="<<static_cast<int>(other.order)<<" yield="<<other.yieldFor<<'\n';
        }
    }
    require(stableTicks==20&&result.acceptedPointsUnchanged,"Escort failed stable convergence at its accepted slots");
    result.state=simulation.stateHash();result.recording=recordingHash(simulation);return result;
}
void distribution(std::ostream& out,std::vector<double> values) {
    std::sort(values.begin(),values.end());
    const auto at=[&](double p){return values[static_cast<std::size_t>(std::ceil(values.size()*p))-1];};
    out<<"{\"p50\":"<<at(.5)<<",\"p95\":"<<at(.95)<<",\"p99\":"<<at(.99)<<",\"max\":"<<values.back()<<'}';
}
void write(std::ostream& out,const Result& result,const Result& repeat) {
    require(result.state==repeat.state&&result.trajectory==repeat.trajectory&&result.recording==repeat.recording&&
            result.ticks==repeat.ticks&&result.completed==repeat.completed,"Sustained workload repeat diverged");
    out<<"{\"mode\":"<<std::quoted(result.mode)<<",\"mobile_units\":"<<result.mobiles
       <<",\"recipients\":"<<result.recipients<<",\"completed\":"<<result.completed<<",\"ticks\":"<<result.ticks
       <<",\"moving_leader_ticks\":"<<result.movingLeaderTicks
       <<",\"followers_moved_during_leader_route\":"<<result.followersMovedDuringLeaderRoute
       <<",\"accepted_points_unchanged\":true,\"deterministic_repeat\":true,\"state_hash\":\"0x"<<std::hex<<result.state
       <<"\",\"trajectory_hash\":\"0x"<<result.trajectory<<"\",\"recording_hash\":\"0x"<<result.recording<<std::dec
       <<"\",\"command_ms\":"<<result.commandMs<<",\"step_ms\":";
    distribution(out,result.steps);out<<",\"snapshots\":";result.snapshots.writeJson(out);out<<'}';
}
}

int main() {
    try {
        // Retain full results until every original workload and repeat passes.
        std::vector<std::pair<Result,Result>> results;
        for(int count:{16,160,200})for(bool patrol:{true,false}) {
            std::cerr<<(patrol?"Patrol":"Escort")<<' '<<count<<" mobiles: primary and repeat\n";
            auto first=patrol?runPatrol(count):runEscort(count);
            auto repeat=patrol?runPatrol(count):runEscort(count);
            results.emplace_back(std::move(first),std::move(repeat));
        }
        std::cout<<std::fixed<<std::setprecision(6)
            <<"{\"schema\":\"cinderline.sustained_order_baseline.v1\",\"success\":true,"
              "\"scope\":\"synthetic open terrain, mixed mobile types, simulation and snapshot CPU only\","
              "\"patrol_required_legs\":"<<RequiredPatrolLegs
            <<",\"escort_stable_ticks\":20,\"maximum_ticks\":"<<MaximumTicks
            <<",\"paid_economy\":false,\"rendering_or_transport\":false,\"workloads\":[";
        for(std::size_t index=0;index<results.size();++index) {
            if(index)std::cout<<',';write(std::cout,results[index].first,results[index].second);
        }
        std::cout<<"]}\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Sustained workload failed: "<<error.what()<<'\n';
        std::cout<<"{\"schema\":\"cinderline.sustained_order_baseline.v1\",\"success\":false,\"error\":"
                 <<std::quoted(error.what())<<"}\n";return 1;
    }
}
