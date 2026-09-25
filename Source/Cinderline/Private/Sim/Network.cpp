#include "Sim/Network.h"
#include "Sim/MapDefinition.h"
#include "Sim/SimulationRules.h"
#include "Sim/SustainedOrderRules.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <unordered_set>

namespace cinder::net {
namespace {
constexpr char SnapshotMagic[4] = {'C','S','N','P'};
constexpr char CommandMagic[4] = {'C','C','M','D'};
constexpr std::size_t MaxEntities = 10000;
constexpr std::size_t MaxObstacles = 1024;
constexpr std::size_t MaxEffects = 10000;
constexpr std::size_t MaxWorkerPlanNoticeBytes = 240;
constexpr float MaxNumber = 1000000000.0f;

bool finite(Vec2 point) { return std::isfinite(point.x) && std::isfinite(point.y); }
bool samePoint(Vec2 left,Vec2 right) { return left.x==right.x&&left.y==right.y; }
float distanceSquared(Vec2 left,Vec2 right) {
    const float x=left.x-right.x,y=left.y-right.y;return x*x+y*y;
}
bool inWorld(Vec2 point,float worldSize) {
    return finite(point) && point.x >= 0 && point.y >= 0 && point.x <= worldSize && point.y <= worldSize;
}
bool validWorkerPlanNotice(const std::string& notice,std::uint64_t serial) {
    if(notice.size()>MaxWorkerPlanNoticeBytes||(notice.empty()!=(serial==0)))return false;
    return std::all_of(notice.begin(),notice.end(),[](unsigned char byte) {
        return byte>=0x20&&byte<=0x7e;
    });
}
bool validMatchLength(MatchLength length) {
    const int value=static_cast<int>(length);
    return value>=static_cast<int>(MatchLength::Short)&&value<static_cast<int>(MatchLength::Count);
}
bool validOrder(Order order) {
    const int value = static_cast<int>(order);
    return value >= static_cast<int>(Order::Idle) && value <= static_cast<int>(Order::Escort);
}
bool validCommandType(CommandType type) {
    const int value = static_cast<int>(type);
    return value >= static_cast<int>(CommandType::Move) && value <= static_cast<int>(CommandType::Escort);
}
bool automaticCommand(CommandType type) {
    return type==CommandType::AutoBuild||type==CommandType::AutoTrain||type==CommandType::AutoResearch||type==CommandType::AutoRally;
}
bool formationCommand(CommandType type) {
    return type==CommandType::Move||type==CommandType::AttackMove||type==CommandType::Defend;
}
bool formationOrder(Order order) {
    return order==Order::Move||order==Order::AttackMove||order==Order::Defend;
}
bool queuedWorkOrder(Order order) {
    return order==Order::Construct||order==Order::Gather;
}
bool appendCommand(CommandType type) {
    return type==CommandType::Move||type==CommandType::AttackMove||type==CommandType::Build||
           type==CommandType::ResumeConstruction||type==CommandType::Gather;
}
bool validStoredArrivalFacing(bool hasFacing,float facing) {
    return rules::validCanonicalArrivalFacing(facing)&&(hasFacing||facing==0.0f);
}
bool validCommandArrivalFacing(bool hasFacing,float facing) {
    const float canonical=facing==0.0f?0.0f:facing;
    return rules::validCanonicalArrivalFacing(canonical)&&(hasFacing||facing==0.0f);
}
bool validCommand(const Command& command) {
    const float maximumWorld=matchLengthProfile(MatchLength::Long).worldSize;
    if(!validCommandType(command.type)||!rules::validKind(command.kind)||!inWorld(command.point,maximumWorld)||
       command.units.size()>MaxCommandUnits||command.queueIndex<0||command.queueIndex>Simulation::MaxQueue)return false;
    if(command.queueMode!=CommandQueueMode::Replace&&command.queueMode!=CommandQueueMode::Append)return false;
    if(command.queueMode==CommandQueueMode::Append&&!appendCommand(command.type))return false;
    if(!rules::validFormationSpacing(command.spacing)||
       !validCommandArrivalFacing(command.hasArrivalFacing,command.arrivalFacing))return false;
    if(!formationCommand(command.type)&&(command.spacing!=FormationSpacing::Standard||
       command.hasArrivalFacing||command.arrivalFacing!=0.0f))return false;
    if(automaticCommand(command.type)!=command.units.empty())return false;
    switch(command.type) {
        case CommandType::Build:
            return !command.target&&command.queueIndex==0&&command.kind!=Kind::Resource&&
                   definition(command.kind).building&&
                   (command.queueMode!=CommandQueueMode::Append||command.units.size()==1);
        case CommandType::ResumeConstruction:
            return command.target&&command.queueIndex==0&&command.kind==Kind::Worker&&
                   (command.queueMode!=CommandQueueMode::Append||command.units.size()==1);
        case CommandType::Gather:
            return command.target&&command.queueIndex==0&&command.kind==Kind::Worker;
        case CommandType::AutoBuild:
            return !command.target&&command.queueIndex==0&&command.kind!=Kind::Resource&&definition(command.kind).building;
        case CommandType::AutoTrain:
            return command.queueIndex>=1&&command.kind!=Kind::Resource&&!definition(command.kind).building;
        case CommandType::AutoResearch:
            return command.queueIndex<=2;
        case CommandType::AutoRally:
            return command.queueIndex<=1&&(command.queueIndex==0||command.target)&&
                   (command.kind==Kind::Resource||rules::productionKind(command.kind));
        case CommandType::CancelQueue:
            return command.units.size()==1&&command.queueIndex<Simulation::MaxQueue;
        case CommandType::ClearOrders:
            return !command.target&&command.queueIndex==0;
        case CommandType::Patrol:
            return command.queueMode==CommandQueueMode::Replace&&!command.target&&command.queueIndex==0;
        case CommandType::Escort:
            return command.queueMode==CommandQueueMode::Replace&&command.target&&command.queueIndex==0&&
                   command.point.x==0&&command.point.y==0;
        default:
            return true;
    }
}
bool validFutureOrderShape(const Entity& owner,const TacticalOrder& step,float worldSize) {
    if(!inWorld(step.point,worldSize)||!rules::validKind(step.buildingKind))return false;
    const bool worker=owner.team==0&&owner.alive()&&owner.kind==Kind::Worker;
    switch(step.order) {
        case Order::Move:
            return step.buildingKind==Kind::Worker&&!step.supportTarget&&
                   validStoredArrivalFacing(step.hasArrivalFacing,step.arrivalFacing);
        case Order::AttackMove:
            return step.buildingKind==Kind::Worker&&
                   validStoredArrivalFacing(step.hasArrivalFacing,step.arrivalFacing);
        case Order::Gather:
            return worker&&step.supportTarget&&step.supportTarget!=owner.id&&step.buildingKind==Kind::Worker&&
                   !step.hasArrivalFacing&&step.arrivalFacing==0.0f;
        case Order::Construct:
            return worker&&!step.hasArrivalFacing&&step.arrivalFacing==0.0f&&
                   (step.supportTarget?step.supportTarget!=owner.id&&step.buildingKind==Kind::Worker:
                    step.buildingKind!=Kind::Resource&&definition(step.buildingKind).building);
        default:
            return false;
    }
}
bool emptySustained(const SustainedOrderState& state) {
    return state.patrolOrigin.x==0&&state.patrolOrigin.y==0&&
           state.patrolDestination.x==0&&state.patrolDestination.y==0&&
           !state.patrolTowardDestination&&!state.escortTarget&&
           state.escortOffset.x==0&&state.escortOffset.y==0&&!state.pursuitTarget&&
           state.pursuitAnchor.x==0&&state.pursuitAnchor.y==0&&
           state.phase==SustainedOrderPhase::Travel;
}
bool validPhase(SustainedOrderPhase phase) {
    const int value=static_cast<int>(phase);
    return value>=static_cast<int>(SustainedOrderPhase::Travel)&&
           value<=static_cast<int>(SustainedOrderPhase::Return);
}
bool validEffectType(EffectType type) {
    const int value = static_cast<int>(type);
    return value >= static_cast<int>(EffectType::Weapon) && value <= static_cast<int>(EffectType::Death);
}
std::uint64_t mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

class Writer {
public:
    void bytes(const char* data, std::size_t count) { data_.insert(data_.end(), data, data + count); }
    void u8(std::uint8_t value) { data_.push_back(value); }
    void u16(std::uint16_t value) { for (int i=0;i<2;++i) u8(static_cast<std::uint8_t>(value >> (i*8))); }
    void u32(std::uint32_t value) { for (int i=0;i<4;++i) u8(static_cast<std::uint8_t>(value >> (i*8))); }
    void u64(std::uint64_t value) { for (int i=0;i<8;++i) u8(static_cast<std::uint8_t>(value >> (i*8))); }
    void i8(int value) { const auto narrowed=static_cast<std::int8_t>(value);std::uint8_t bits;std::memcpy(&bits,&narrowed,sizeof(bits));u8(bits); }
    void i32(int value) { const auto narrowed=static_cast<std::int32_t>(value);std::uint32_t bits;std::memcpy(&bits,&narrowed,sizeof(bits));u32(bits); }
    void real(float value) { std::uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));u32(bits); }
    void point(Vec2 value) { real(value.x);real(value.y); }
    void text(const std::string& value) { u16(static_cast<std::uint16_t>(value.size()));bytes(value.data(),value.size()); }
    std::vector<std::uint8_t> finish() { return std::move(data_); }
private:
    std::vector<std::uint8_t> data_;
};

class Reader {
public:
    Reader(const void* data,std::size_t size):data_(static_cast<const std::uint8_t*>(data)),size_(size){}
    bool bytes(const char* expected,std::size_t count) {
        if (!available(count) || std::memcmp(data_+offset_,expected,count)!=0) return false;
        offset_+=count;return true;
    }
    bool u8(std::uint8_t& value) { if(!available(1))return false;value=data_[offset_++];return true; }
    bool u16(std::uint16_t& value) { value=0;for(int i=0;i<2;++i){std::uint8_t byte;if(!u8(byte))return false;value|=static_cast<std::uint16_t>(byte)<<(i*8);}return true; }
    bool u32(std::uint32_t& value) { value=0;for(int i=0;i<4;++i){std::uint8_t byte;if(!u8(byte))return false;value|=static_cast<std::uint32_t>(byte)<<(i*8);}return true; }
    bool u64(std::uint64_t& value) { value=0;for(int i=0;i<8;++i){std::uint8_t byte;if(!u8(byte))return false;value|=static_cast<std::uint64_t>(byte)<<(i*8);}return true; }
    bool i8(int& value) { std::uint8_t bits;if(!u8(bits))return false;std::int8_t narrowed;std::memcpy(&narrowed,&bits,sizeof(bits));value=narrowed;return true; }
    bool i32(int& value) { std::uint32_t bits;if(!u32(bits))return false;std::int32_t narrowed;std::memcpy(&narrowed,&bits,sizeof(bits));value=narrowed;return true; }
    bool real(float& value) { std::uint32_t bits;if(!u32(bits))return false;std::memcpy(&value,&bits,sizeof(bits));return true; }
    bool point(Vec2& value) { return real(value.x)&&real(value.y); }
    bool text(std::string& value,std::size_t maximum) {
        std::uint16_t count=0;if(!u16(count)||count>maximum||!available(count))return false;
        value.assign(reinterpret_cast<const char*>(data_+offset_),count);offset_+=count;return true;
    }
    bool done() const { return offset_==size_; }
private:
    bool available(std::size_t count) const { return data_ && count<=size_-offset_; }
    const std::uint8_t* data_=nullptr;std::size_t size_=0,offset_=0;
};

bool validStats(const Stats& stats) {
    const int values[]{stats.gathered,stats.produced,stats.lost,stats.killed,stats.built,stats.buildingsDestroyed,stats.expansions,stats.upgrades};
    for(int value:values)if(value<0||value>static_cast<int>(MaxNumber))return false;
    return std::isfinite(stats.damage)&&stats.damage>=0&&stats.damage<=MaxNumber;
}
bool validPlayer(const Player& player) {
    return player.ore>=0&&player.ore<=static_cast<int>(MaxNumber)&&player.tier>=1&&player.tier<=3&&
           player.weapons>=0&&player.weapons<=3&&player.armor>=0&&player.armor<=3&&validStats(player.stats);
}
bool validSnapshot(const Snapshot& snapshot,std::string& error) {
    auto fail=[&](const char* message){error=message;return false;};
    if(snapshot.config.map<0||snapshot.config.map>2||!validMapRevision(snapshot.config.mapRevision)||!validMatchLength(snapshot.config.matchLength)||!rules::validPlayerCount(snapshot.config.playerCount)||!std::isfinite(snapshot.config.aiAggression)||snapshot.config.aiAggression<0.5f||snapshot.config.aiAggression>2.0f)
        return fail("Invalid snapshot configuration.");
    if(!validWorkerPlanNotice(snapshot.workerPlanNotice,snapshot.workerPlanNoticeSerial))
        return fail("Invalid worker-plan notice.");
    const float worldSize=matchLengthProfile(snapshot.config.matchLength).worldSize;
    if(!inWorld(snapshot.player.armyRally,worldSize))return fail("Invalid snapshot match state.");
    const std::uint8_t playerMask=rules::activePlayerMask(snapshot.config.playerCount);
    if(snapshot.winner < -2 || snapshot.winner >= snapshot.config.playerCount || (snapshot.eliminatedMask&~playerMask)!=0 || snapshot.lastEffectId==std::numeric_limits<std::uint64_t>::max() || !validPlayer(snapshot.player))return fail("Invalid snapshot match state.");
    if(!rules::validMatchOutcome(snapshot.config.playerCount,snapshot.winner,snapshot.eliminatedMask))return fail("Inconsistent snapshot elimination state.");
    if(snapshot.entities.size()>MaxEntities||snapshot.obstacles.size()>MaxObstacles||snapshot.effects.size()>MaxEffects)return fail("Snapshot collection is too large.");
    for(const auto& obstacle:snapshot.obstacles) {
        if(!inWorld(obstacle.center,worldSize)||!finite(obstacle.half)||obstacle.half.x<0||obstacle.half.y<0||obstacle.half.x>worldSize||obstacle.half.y>worldSize)
            return fail("Invalid snapshot obstacle.");
    }
    const auto& map=mapDefinition(snapshot.config.map,snapshot.config.playerCount,
                                  snapshot.config.matchLength,snapshot.config.mapRevision);
    if(map.authored()) {
        if(snapshot.obstacles.size()!=map.obstacles.size())
            return fail("Snapshot geometry does not match its terrain revision.");
        for(std::size_t index=0;index<map.obstacles.size();++index) {
            if(!samePoint(snapshot.obstacles[index].center,map.obstacles[index].center)||
               !samePoint(snapshot.obstacles[index].half,map.obstacles[index].half))
                return fail("Snapshot geometry does not match its terrain revision.");
        }
    }
    std::unordered_set<Id> ids;
    std::unordered_map<Id,const Entity*> indexed;
    std::size_t futureCount=0;
    for(const auto& entity:snapshot.entities) {
        if(!entity.id||!ids.insert(entity.id).second||!rules::validKind(entity.kind)||!validOrder(entity.order)||!inWorld(entity.pos,worldSize)||!inWorld(entity.goal,worldSize)||!inWorld(entity.rally,worldSize))
            return fail("Invalid or duplicate snapshot entity.");
        if((entity.kind==Kind::Resource&&entity.team!=-1)||(entity.kind!=Kind::Resource&&(entity.team<0||entity.team>=snapshot.config.playerCount)))return fail("Invalid snapshot entity team.");
        indexed.emplace(entity.id,&entity);
        const bool pendingWorkerJob=entity.team==0&&entity.alive()&&entity.kind==Kind::Worker&&
            entity.order==Order::Idle&&!entity.futureOrders.empty()&&queuedWorkOrder(entity.futureOrders.front().order);
        if(entity.resumeGather&&(entity.team!=0||!entity.alive()||entity.kind!=Kind::Worker||
           (entity.order==Order::Idle&&!pendingWorkerJob)))
            return fail("Invalid or private mining-resume state.");
        const bool formationOwner=entity.team==0&&entity.alive()&&!definition(entity.kind).building&&entity.kind!=Kind::Resource;
        if(!validStoredArrivalFacing(entity.hasArrivalFacing,entity.arrivalFacing)||
           (entity.hasArrivalFacing&&(!formationOwner||!formationOrder(entity.order)))||
           (entity.team!=0&&(entity.hasArrivalFacing||entity.arrivalFacing!=0.0f)))
            return fail("Invalid or private arrival facing.");
        const bool sustainedOwner=entity.team==0&&entity.alive()&&!definition(entity.kind).building&&entity.kind!=Kind::Resource;
        if(!validPhase(entity.sustained.phase))return fail("Invalid sustained-order phase.");
        if(!sustainedOwner&&(entity.order==Order::Patrol||entity.order==Order::Escort||!emptySustained(entity.sustained)))
            return fail("Invalid or private sustained order.");
        if(entity.order==Order::Patrol) {
            const auto& state=entity.sustained;
            if(!sustainedOwner||entity.target||entity.supportTarget||
               !inWorld(state.patrolOrigin,worldSize)||!inWorld(state.patrolDestination,worldSize)||
               state.escortTarget||state.escortOffset.x!=0||state.escortOffset.y!=0||
               !samePoint(entity.goal,state.patrolTowardDestination?state.patrolDestination:state.patrolOrigin)||
               (state.phase==SustainedOrderPhase::Travel&&
                    (state.pursuitTarget||state.pursuitAnchor.x!=0||state.pursuitAnchor.y!=0))||
               (state.phase==SustainedOrderPhase::Pursuit&&
                    (!state.pursuitTarget||!inWorld(state.pursuitAnchor,worldSize)))||
               (state.phase==SustainedOrderPhase::Return&&
                    (state.pursuitTarget||!inWorld(state.pursuitAnchor,worldSize))))
                return fail("Invalid Patrol state.");
        } else if(entity.order==Order::Escort) {
            const auto& state=entity.sustained;
            const float offsetLength=std::hypot(state.escortOffset.x,state.escortOffset.y);
            if(!sustainedOwner||entity.target||entity.supportTarget||
               state.patrolOrigin.x!=0||state.patrolOrigin.y!=0||
               state.patrolDestination.x!=0||state.patrolDestination.y!=0||state.patrolTowardDestination||
               !state.escortTarget||!finite(state.escortOffset)||offsetLength>Simulation::MaxEscortOffset||
               state.pursuitAnchor.x!=0||state.pursuitAnchor.y!=0||
               (state.phase==SustainedOrderPhase::Travel&&state.pursuitTarget)||
               (state.phase==SustainedOrderPhase::Pursuit&&!state.pursuitTarget)||
               (state.phase==SustainedOrderPhase::Return&&state.pursuitTarget))
                return fail("Invalid Escort state.");
        } else if(!emptySustained(entity.sustained))return fail("Inactive sustained state is not empty.");
        if(entity.futureOrders.size()>Simulation::MaxFutureOrders)return fail("Too many future tactical orders.");
        if((entity.supportTarget||!entity.futureOrders.empty())&&
           (entity.team!=0||!entity.alive()||definition(entity.kind).building||entity.kind==Kind::Resource))
            return fail("Invalid or private tactical plan.");
        if(!entity.futureOrders.empty()&&entity.order==Order::Idle&&!pendingWorkerJob)
            return fail("Only an idle worker awaiting queued work can retain future orders.");
        futureCount+=entity.futureOrders.size();
        if(futureCount>Simulation::MaxFutureOrdersPerPlayer)return fail("Tactical plan exceeds the player allowance.");
        for(const auto& step:entity.futureOrders)
            if(!validFutureOrderShape(entity,step,worldSize))
                return fail("Invalid future tactical destination.");
        if(entity.rallyOverride&&(entity.team!=0||!rules::productionKind(entity.kind)))return fail("Invalid or private snapshot rally override.");
        if(entity.team==0&&rules::combatProductionKind(entity.kind)&&snapshot.player.armyRallySet&&!entity.rallyOverride&&
           (entity.rally.x!=snapshot.player.armyRally.x||entity.rally.y!=snapshot.player.armyRally.y))return fail("Snapshot producer does not match its inherited rally.");
        const float values[]{entity.hp,entity.cooldown,entity.progress,entity.carried,entity.harvestTimer,entity.resource,entity.facing,entity.repath};
        for(float value:values)if(!std::isfinite(value)||std::fabs(value)>MaxNumber)return fail("Invalid snapshot entity value.");
        if(entity.hp<0||entity.cooldown<0||entity.progress<0||entity.progress>1||entity.carried<0||entity.harvestTimer<0||entity.resource<0||entity.repath<0)
            return fail("Invalid snapshot entity range.");
        if(entity.queue.size()>Simulation::MaxQueue||entity.path.size()>Simulation::FogSize*Simulation::FogSize+1||entity.pathIndex<0||entity.pathIndex>static_cast<int>(entity.path.size()))
            return fail("Invalid snapshot entity collection.");
        if(entity.team==0&&!entity.nextQueueId)return fail("Invalid snapshot queue sequence.");
        Id lastQueueId=0;
        for(const auto& item:entity.queue) {
            if(!rules::validKind(item.kind)||!std::isfinite(item.remaining)||!std::isfinite(item.total)||item.total<=0||item.remaining<0||item.remaining>item.total||item.cost<0||item.cost>static_cast<int>(MaxNumber)||
               !item.id||item.id<=lastQueueId||item.id>=entity.nextQueueId||item.assignmentCursor||!item.assignmentCandidates.empty())return fail("Invalid snapshot queue item.");
            lastQueueId=item.id;
        }
        for(Vec2 point:entity.path)if(!inWorld(point,worldSize))return fail("Invalid snapshot path point.");
    }
    for(const auto& entity:snapshot.entities) {
        for(Id reference:{entity.target,entity.resourceTarget,entity.builderId})if(reference&&!ids.count(reference))return fail("Snapshot entity contains an unknown reference.");
        auto validSupport=[&](Order order,Id id) {
            if(!id)return true;
            if(entity.team!=0||entity.kind!=Kind::Mender||order!=Order::AttackMove||id==entity.id)return false;
            const auto found=indexed.find(id);if(found==indexed.end())return false;
            const auto& leader=*found->second;
            return leader.alive()&&leader.team==0&&!definition(leader.kind).building&&definition(leader.kind).damage>0;
        };
        if(!validSupport(entity.order,entity.supportTarget))return fail("Invalid tactical support reference.");
        for(const auto& step:entity.futureOrders)
            if(!queuedWorkOrder(step.order)&&!validSupport(step.order,step.supportTarget))
                return fail("Invalid future support reference.");
        const auto validPursuit=[&](Id id) {
            if(!id)return true;
            const auto found=indexed.find(id);if(found==indexed.end())return false;
            const auto& target=*found->second;
            return entity.team==0&&entity.alive()&&definition(entity.kind).damage>0&&
                   target.alive()&&target.team>0&&target.kind!=Kind::Resource&&
                   (!definition(target.kind).air||definition(entity.kind).antiAir);
        };
        if(!validPursuit(entity.sustained.pursuitTarget))return fail("Invalid sustained pursuit reference.");
        if(entity.order==Order::Escort) {
            const auto found=indexed.find(entity.sustained.escortTarget);
            if(found==indexed.end())return fail("Escort target is missing.");
            const auto& leader=*found->second;
            if(!leader.alive()||leader.team!=0||leader.id==entity.id||definition(leader.kind).building||leader.kind==Kind::Resource)
                return fail("Invalid Escort target.");
            const float minimum=definition(entity.kind).radius+definition(leader.kind).radius;
            const Vec2 expected=rules::projectEscortFollowPoint(leader.pos,definition(leader.kind).radius,
                definition(entity.kind).radius,entity.sustained.escortOffset,worldSize,Simulation::EscortSpacing);
            if(distanceSquared(entity.sustained.escortOffset,{})<minimum*minimum||
               !samePoint(entity.goal,expected))
                return fail("Invalid Escort geometry.");
        }
    }
    for(const auto& entity:snapshot.entities)if(entity.order==Order::Escort) {
        std::unordered_set<Id> chain{entity.id};
        const Entity* node=&entity;
        while(node&&node->order==Order::Escort) {
            const Id next=node->sustained.escortTarget;
            if(!chain.insert(next).second)return fail("Escort graph contains a cycle.");
            const auto found=indexed.find(next);node=found==indexed.end()?nullptr:found->second;
        }
    }
    std::unordered_map<Id,std::vector<const Entity*>> escortGroups;
    for(const auto& entity:snapshot.entities)if(entity.order==Order::Escort)
        escortGroups[entity.sustained.escortTarget].push_back(&entity);
    for(auto& [leader,followers]:escortGroups) {
        (void)leader;
        float maximumRadius=0;
        for(const Entity* follower:followers)
            maximumRadius=std::max(maximumRadius,definition(follower->kind).radius);
        std::sort(followers.begin(),followers.end(),[](const Entity* left,const Entity* right) {
            return left->goal.x<right->goal.x||(left->goal.x==right->goal.x&&left->id<right->id);
        });
        for(std::size_t left=0;left<followers.size();++left) {
            const float leftRadius=definition(followers[left]->kind).radius;
            for(std::size_t right=left+1;right<followers.size();++right) {
                if(followers[right]->goal.x-followers[left]->goal.x>=leftRadius+maximumRadius)break;
                const float clearance=leftRadius+definition(followers[right]->kind).radius;
                if(distanceSquared(followers[left]->goal,followers[right]->goal)<clearance*clearance)
                    return fail("Escort followers overlap.");
            }
        }
    }
    std::unordered_set<std::uint64_t> effectIds;
    for(const auto& effect:snapshot.effects) {
        if(!effect.id||effect.id>snapshot.lastEffectId||!effectIds.insert(effect.id).second||!validEffectType(effect.type)||!rules::validKind(effect.sourceKind)||!rules::validKind(effect.targetKind)||
           effect.sourceKind==Kind::Resource||effect.targetKind==Kind::Resource||effect.team<0||effect.team>=snapshot.config.playerCount||!inWorld(effect.from,worldSize)||!inWorld(effect.to,worldSize)||
           !std::isfinite(effect.life)||!std::isfinite(effect.duration)||effect.life<=0||effect.duration<0.3f||effect.duration>10||effect.life>effect.duration||effect.fromVisibleMask>1||effect.toVisibleMask>1)
            return fail("Invalid or duplicate snapshot effect.");
        if((effect.type==EffectType::Impact||effect.type==EffectType::Death)&&
           (effect.from.x!=effect.to.x||effect.from.y!=effect.to.y||effect.fromVisibleMask!=effect.toVisibleMask))return fail("Invalid point effect geometry.");
        if(effect.type==EffectType::Death&&effect.sourceKind!=effect.targetKind)return fail("Invalid death effect.");
    }
    for(std::uint8_t value:snapshot.fog)if(value>2)return fail("Invalid snapshot fog value.");
    error.clear();return true;
}

void writeStats(Writer& writer,const Stats& stats) {
    writer.i32(stats.gathered);writer.i32(stats.produced);writer.i32(stats.lost);writer.i32(stats.killed);
    writer.i32(stats.built);writer.i32(stats.buildingsDestroyed);writer.i32(stats.expansions);writer.i32(stats.upgrades);writer.real(stats.damage);
}
bool readStats(Reader& reader,Stats& stats) {
    return reader.i32(stats.gathered)&&reader.i32(stats.produced)&&reader.i32(stats.lost)&&reader.i32(stats.killed)&&
           reader.i32(stats.built)&&reader.i32(stats.buildingsDestroyed)&&reader.i32(stats.expansions)&&reader.i32(stats.upgrades)&&reader.real(stats.damage);
}
void writeEntity(Writer& writer,const Entity& entity) {
    writer.u32(entity.id);writer.u8(static_cast<std::uint8_t>(entity.kind));writer.i8(entity.team);
    writer.point(entity.pos);writer.point(entity.goal);writer.point(entity.rally);
    writer.real(entity.hp);writer.real(entity.cooldown);writer.real(entity.progress);writer.real(entity.carried);writer.real(entity.harvestTimer);writer.real(entity.resource);writer.real(entity.facing);
    writer.u8(static_cast<std::uint8_t>(entity.order));writer.u32(entity.target);writer.u32(entity.resourceTarget);writer.u8(entity.returning?1:0);writer.u32(entity.builderId);writer.u8(entity.resumeGather?1:0);writer.u8(entity.rallyOverride?1:0);
    writer.u8(static_cast<std::uint8_t>(entity.queue.size()));
    for(const auto& item:entity.queue){writer.u8(static_cast<std::uint8_t>(item.kind));writer.real(item.remaining);writer.real(item.total);writer.i32(item.cost);writer.u8(item.research?1:0);writer.u32(item.id);}
    writer.u16(static_cast<std::uint16_t>(entity.path.size()));for(Vec2 point:entity.path)writer.point(point);
    writer.i32(entity.pathIndex);writer.real(entity.repath);writer.u8(entity.navigationExhausted?1:0);writer.u32(entity.nextQueueId);
    writer.u32(entity.supportTarget);
    writer.u8(entity.hasArrivalFacing?1:0);writer.real(entity.arrivalFacing);
    if(entity.order==Order::Patrol) {
        writer.point(entity.sustained.patrolOrigin);writer.point(entity.sustained.patrolDestination);
        writer.u8(entity.sustained.patrolTowardDestination?1:0);writer.u32(entity.sustained.pursuitTarget);
        writer.point(entity.sustained.pursuitAnchor);writer.u8(static_cast<std::uint8_t>(entity.sustained.phase));
    } else if(entity.order==Order::Escort) {
        writer.u32(entity.sustained.escortTarget);writer.point(entity.sustained.escortOffset);
        writer.u32(entity.sustained.pursuitTarget);writer.u8(static_cast<std::uint8_t>(entity.sustained.phase));
    }
    writer.u8(static_cast<std::uint8_t>(entity.futureOrders.size()));
    for(const auto& step:entity.futureOrders){writer.u8(static_cast<std::uint8_t>(step.order));writer.point(step.point);writer.u32(step.supportTarget);writer.u8(step.hasArrivalFacing?1:0);writer.real(step.arrivalFacing);writer.u8(static_cast<std::uint8_t>(step.buildingKind));}
}
bool readEntity(Reader& reader,Entity& entity) {
    std::uint8_t kind=0,order=0,flag=0,queueCount=0;int team=0;
    if(!reader.u32(entity.id)||!reader.u8(kind)||!reader.i8(team)||!reader.point(entity.pos)||!reader.point(entity.goal)||!reader.point(entity.rally)||
       !reader.real(entity.hp)||!reader.real(entity.cooldown)||!reader.real(entity.progress)||!reader.real(entity.carried)||!reader.real(entity.harvestTimer)||!reader.real(entity.resource)||!reader.real(entity.facing)||
       !reader.u8(order)||!reader.u32(entity.target)||!reader.u32(entity.resourceTarget)||!reader.u8(flag))return false;
    entity.kind=static_cast<Kind>(kind);entity.team=team;entity.order=static_cast<Order>(order);if(flag>1)return false;entity.returning=flag!=0;
    if(!reader.u32(entity.builderId)||!reader.u8(flag)||flag>1)return false;entity.resumeGather=flag!=0;
    if(!reader.u8(flag)||flag>1||!reader.u8(queueCount))return false;entity.rallyOverride=flag!=0;
    for(std::uint8_t i=0;i<queueCount;++i){QueueItem item;std::uint8_t itemKind=0,research=0;if(!reader.u8(itemKind)||!reader.real(item.remaining)||!reader.real(item.total)||!reader.i32(item.cost)||!reader.u8(research)||research>1||!reader.u32(item.id))return false;item.kind=static_cast<Kind>(itemKind);item.research=research!=0;entity.queue.push_back(item);}
    std::uint16_t pathCount=0;if(!reader.u16(pathCount))return false;
    for(std::uint16_t i=0;i<pathCount;++i){Vec2 point;if(!reader.point(point))return false;entity.path.push_back(point);}
    if(!reader.i32(entity.pathIndex)||!reader.real(entity.repath)||!reader.u8(flag)||flag>1||!reader.u32(entity.nextQueueId))return false;
    entity.navigationExhausted=flag!=0;
    if(!reader.u32(entity.supportTarget))return false;
    if(!reader.u8(flag)||flag>1||!reader.real(entity.arrivalFacing))return false;
    entity.hasArrivalFacing=flag!=0;
    if(entity.order==Order::Patrol) {
        std::uint8_t direction=0,phase=0;
        if(!reader.point(entity.sustained.patrolOrigin)||!reader.point(entity.sustained.patrolDestination)||
           !reader.u8(direction)||direction>1||!reader.u32(entity.sustained.pursuitTarget)||
           !reader.point(entity.sustained.pursuitAnchor)||!reader.u8(phase))return false;
        entity.sustained.patrolTowardDestination=direction!=0;
        entity.sustained.phase=static_cast<SustainedOrderPhase>(phase);
    } else if(entity.order==Order::Escort) {
        std::uint8_t phase=0;
        if(!reader.u32(entity.sustained.escortTarget)||!reader.point(entity.sustained.escortOffset)||
           !reader.u32(entity.sustained.pursuitTarget)||!reader.u8(phase))return false;
        entity.sustained.phase=static_cast<SustainedOrderPhase>(phase);
    }
    std::uint8_t futureCount=0;
    if(!reader.u8(futureCount)||futureCount>Simulation::MaxFutureOrders)return false;
    for(std::uint8_t i=0;i<futureCount;++i) {
        TacticalOrder step;std::uint8_t stepOrder=0;
        std::uint8_t buildingKind=0;
        if(!reader.u8(stepOrder)||!reader.point(step.point)||!reader.u32(step.supportTarget)||
           !reader.u8(flag)||flag>1||!reader.real(step.arrivalFacing)||!reader.u8(buildingKind))return false;
        step.hasArrivalFacing=flag!=0;
        step.order=static_cast<Order>(stepOrder);step.buildingKind=static_cast<Kind>(buildingKind);entity.futureOrders.push_back(step);
    }
    return true;
}
}

ViewMemory::ViewMemory() {
    std::random_device random;
    const std::uint64_t entropy=(static_cast<std::uint64_t>(random())<<32)^random();
    const auto clock=static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    secret_=mix(entropy^clock^static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this)));
}

Snapshot snapshotFor(const Simulation& simulation,int viewer,ViewMemory* memory) {
    Snapshot snapshot;const int playerCount=simulation.playerCount();if(viewer<0||viewer>=playerCount)return snapshot;
    const auto normalizeTeam=[&](int team){return (team-viewer+playerCount)%playerCount;};
    snapshot.config=simulation.config();snapshot.config.ai=false;snapshot.tick=simulation.tick();
    snapshot.winner=simulation.winner()<0?simulation.winner():normalizeTeam(simulation.winner());
    for(int team=0;team<playerCount;++team)if((simulation.eliminatedMask()&(1u<<team))!=0)snapshot.eliminatedMask|=static_cast<std::uint8_t>(1u<<normalizeTeam(team));
    snapshot.player=simulation.players()[viewer];snapshot.obstacles=simulation.obstacles();
    snapshot.workerPlanNotice=simulation.workerPlanNotice(viewer);
    snapshot.workerPlanNoticeSerial=simulation.workerPlanNoticeSerial(viewer);
    for(int cell=0;cell<Simulation::FogSize*Simulation::FogSize;++cell) {
        const int x=cell%Simulation::FogSize,y=cell/Simulation::FogSize;
        const Vec2 point{(x+0.5f)*simulation.worldSize()/Simulation::FogSize,(y+0.5f)*simulation.worldSize()/Simulation::FogSize};
        snapshot.fog[cell]=simulation.visible(viewer,point)?2:(simulation.explored(viewer,point)?1:0);
    }
    if(memory) {
        std::unordered_set<Id> live;
        for(const auto& entity:simulation.entities()) {
            if(entity.alive())live.insert(entity.id);
            if(entity.team==viewer&&entity.alive())for(const auto& step:entity.futureOrders)
                if(queuedWorkOrder(step.order)&&step.supportTarget)live.insert(step.supportTarget);
        }
        for(auto it=memory->handles_.begin();it!=memory->handles_.end();) {
            if(live.count(it->first)){++it;continue;}memory->entities_.erase(it->second);memory->resources_.erase(it->first);it=memory->handles_.erase(it);
        }
    }
    auto handle=[&](Id id) {
        if(!memory)return id;
        const auto found=memory->handles_.find(id);if(found!=memory->handles_.end())return found->second;
        Id candidate=0;
        do {candidate=static_cast<Id>(mix(memory->secret_^memory->nextHandle_++));} while(!candidate||candidate==id||memory->entities_.count(candidate));
        memory->handles_[id]=candidate;memory->entities_[candidate]=id;return candidate;
    };
    std::vector<const Entity*> included;
    for(const auto& entity:simulation.entities())if(entity.alive()) {
        bool include=entity.team==viewer||(entity.team>=0&&simulation.visible(viewer,entity.pos));
        if(entity.kind==Kind::Resource) {
            const bool visible=simulation.visible(viewer,entity.pos);
            if(memory&&visible)memory->resources_[entity.id]=entity.resource;
            include=memory?(memory->resources_.count(entity.id)!=0):(visible);
        }
        if(include)included.push_back(&entity);
    }
    std::unordered_set<Id> includedIds;for(const Entity* entity:included){includedIds.insert(entity->id);handle(entity->id);}
    auto reference=[&](Id id){return id&&includedIds.count(id)?handle(id):Id{0};};
    auto queuedReference=[&](Id id) {
        if(!id)return Id{0};
        if(includedIds.count(id))return handle(id);
        return memory?handle(id):id;
    };
    for(const Entity* source:included) {
        const bool owned=source->team==viewer;
        Entity entity=*source;entity.id=handle(source->id);entity.team=source->team<0?-1:normalizeTeam(source->team);
        entity.path.clear();entity.pathIndex=0;entity.repath=0;
        entity.workTarget=0;entity.workPoint={};entity.workPointValid=false;
        entity.navigationAnchor={};entity.stalledFor=0;entity.yieldFor=0;entity.navigationBestDistance=0;entity.pathGeometry=0;
        entity.navigationFailures=0;entity.avoidanceSide=0;entity.navigationExhausted=owned&&source->navigationExhausted;
        for(auto& item:entity.queue){item.assignmentCursor=0;item.assignmentCandidates.clear();}
        if(owned) {
            entity.supportTarget=reference(source->supportTarget);
            for(auto& step:entity.futureOrders)
                step.supportTarget=queuedWorkOrder(step.order)?queuedReference(step.supportTarget):reference(step.supportTarget);
            entity.sustained.escortTarget=reference(source->sustained.escortTarget);
            entity.sustained.pursuitTarget=reference(source->sustained.pursuitTarget);
            if(entity.sustained.phase==SustainedOrderPhase::Pursuit&&!entity.sustained.pursuitTarget)
                entity.sustained.phase=SustainedOrderPhase::Return;
        } else {entity.supportTarget=0;entity.futureOrders.clear();entity.sustained={};entity.hasArrivalFacing=false;entity.arrivalFacing=0.0f;}
        if(source->kind==Kind::Resource)entity.resource=memory?memory->resources_.at(source->id):source->resource;
        if(source->team>=0&&source->team!=viewer) {
            entity.goal=entity.pos;entity.rally=entity.pos;entity.cooldown=0;entity.carried=0;entity.harvestTimer=0;entity.resource=0;entity.facing=source->facing;
            entity.order=Order::Idle;entity.target=0;entity.resourceTarget=0;entity.returning=false;entity.builderId=0;entity.resumeGather=false;entity.rallyOverride=false;entity.queue.clear();entity.nextQueueId=1;
        } else {
            entity.target=reference(source->target);entity.resourceTarget=reference(source->resourceTarget);entity.builderId=reference(source->builderId);
        }
        snapshot.entities.push_back(std::move(entity));
    }
    std::unordered_set<std::uint64_t> activeEffects;
    for(const auto& effect:simulation.effects())activeEffects.insert(effect.id);
    if(memory)for(auto it=memory->effects_.begin();it!=memory->effects_.end();)it=activeEffects.count(it->first)?std::next(it):memory->effects_.erase(it);
    for(const auto& source:simulation.effects()) {
        const bool fromVisible=simulation.effectVisible(source,viewer,true),toVisible=simulation.effectVisible(source,viewer,false);
        if(!fromVisible&&!toVisible)continue;
        Effect effect=source;effect.team=normalizeTeam(source.team);effect.fromVisibleMask=fromVisible?1:0;effect.toVisibleMask=toVisible?1:0;
        if(effect.type==EffectType::Impact)effect.sourceKind=Kind::Worker;
        if(!fromVisible){effect.from=effect.to;effect.sourceKind=Kind::Worker;}
        if(!toVisible){effect.to=effect.from;effect.targetKind=Kind::Worker;}
        if(fromVisible&&toVisible&&!simulation.effectLinkVisible(source,viewer))effect.from=effect.to;
        if(memory) {
            auto found=memory->effects_.find(source.id);
            if(found==memory->effects_.end())found=memory->effects_.emplace(source.id,memory->nextEffectId_++).first;
            effect.id=found->second;
        }
        snapshot.effects.push_back(effect);
    }
    snapshot.lastEffectId=memory?memory->nextEffectId_-1:simulation.lastEffectId();
    return snapshot;
}

std::vector<std::uint8_t> encodeSnapshot(const Snapshot& snapshot) {
    std::string error;if(!validSnapshot(snapshot,error))return {};
    Writer writer;writer.bytes(SnapshotMagic,4);writer.u32(ProtocolVersion);
    writer.i32(snapshot.config.map);writer.u32(snapshot.config.seed);writer.u8(snapshot.config.ai?1:0);writer.real(snapshot.config.aiAggression);writer.u8(static_cast<std::uint8_t>(snapshot.config.matchLength));writer.u8(static_cast<std::uint8_t>(snapshot.config.playerCount));writer.i32(snapshot.config.mapRevision);
    writer.u64(snapshot.tick);writer.u64(snapshot.lastEffectId);writer.i8(snapshot.winner);writer.u8(snapshot.eliminatedMask);
    writer.i32(snapshot.player.ore);writer.i32(snapshot.player.tier);writer.i32(snapshot.player.weapons);writer.i32(snapshot.player.armor);
    writer.point(snapshot.player.armyRally);writer.u8(snapshot.player.armyRallySet?1:0);writeStats(writer,snapshot.player.stats);
    writer.u16(static_cast<std::uint16_t>(snapshot.obstacles.size()));for(const auto& obstacle:snapshot.obstacles){writer.point(obstacle.center);writer.point(obstacle.half);}
    writer.u32(static_cast<std::uint32_t>(snapshot.entities.size()));for(const auto& entity:snapshot.entities)writeEntity(writer,entity);
    writer.u32(static_cast<std::uint32_t>(snapshot.effects.size()));for(const auto& effect:snapshot.effects){writer.point(effect.from);writer.point(effect.to);writer.i8(effect.team);writer.real(effect.life);writer.real(effect.duration);writer.u64(effect.id);writer.u8(static_cast<std::uint8_t>(effect.type));writer.u8(static_cast<std::uint8_t>(effect.sourceKind));writer.u8(static_cast<std::uint8_t>(effect.targetKind));writer.u8(effect.fromVisibleMask);writer.u8(effect.toVisibleMask);}
    std::vector<std::pair<std::uint8_t,std::uint16_t>> runs;
    for(std::size_t start=0;start<snapshot.fog.size();) {std::size_t end=start+1;while(end<snapshot.fog.size()&&snapshot.fog[end]==snapshot.fog[start]&&end-start<std::numeric_limits<std::uint16_t>::max())++end;runs.push_back({snapshot.fog[start],static_cast<std::uint16_t>(end-start)});start=end;}
    writer.u16(static_cast<std::uint16_t>(runs.size()));for(const auto& run:runs){writer.u8(run.first);writer.u16(run.second);}
    writer.u64(snapshot.workerPlanNoticeSerial);writer.text(snapshot.workerPlanNotice);
    auto result=writer.finish();if(result.size()>MaxMessageBytes)return {};return result;
}

bool orderPlanFitsSnapshot(const Simulation& simulation,int team,
                          const std::vector<std::pair<Id,TacticalOrder>>& orders) {
    if(team<0||team>=simulation.playerCount()||orders.empty())return false;
    auto projected=snapshotFor(simulation,team);
    std::unordered_map<Id,Entity*> owned;
    for(auto& entity:projected.entities)if(entity.team==0)owned.emplace(entity.id,&entity);
    std::unordered_set<Id> recipients;
    for(const auto& [id,step]:orders) {
        const auto found=owned.find(id);
        if(found==owned.end()||!recipients.insert(id).second)return false;
        Entity& entity=*found->second;
        if(!entity.alive()||definition(entity.kind).building||entity.kind==Kind::Resource||
           !validFutureOrderShape(entity,step,simulation.worldSize()))return false;
        if(entity.order==Order::Idle) {
            const bool pendingWorkerJob=entity.kind==Kind::Worker&&!entity.futureOrders.empty()&&
                queuedWorkOrder(entity.futureOrders.front().order);
            if(pendingWorkerJob) {
                if(entity.futureOrders.size()>=Simulation::MaxFutureOrders)return false;
                entity.futureOrders.push_back(step);
            } else if(!entity.futureOrders.empty())return false;
            else if(entity.kind==Kind::Worker&&queuedWorkOrder(step.order))entity.futureOrders.push_back(step);
            else {
                entity.order=step.order;entity.goal=step.point;entity.target=0;entity.resourceTarget=0;
                entity.supportTarget=0;entity.resumeGather=false;
                entity.hasArrivalFacing=step.hasArrivalFacing;entity.arrivalFacing=step.arrivalFacing;
            }
        } else {
            if(entity.futureOrders.size()>=Simulation::MaxFutureOrders)return false;
            entity.futureOrders.push_back(step);
        }
    }
    // Every queued new-build step may later replace its 19-byte plan row with
    // a larger foundation entity. Reserve all of those rows, including plans
    // that existed before this command, so later activation cannot overflow a
    // snapshot that admission already declared safe.
    std::unordered_set<Id> reservedIds;
    reservedIds.reserve(projected.entities.size()+Simulation::MaxFutureOrdersPerPlayer);
    for(const auto& entity:projected.entities)reservedIds.insert(entity.id);
    Id nextFoundation=1;
    auto reserveFoundation=[&](Kind kind,Vec2 point) {
        while(reservedIds.count(nextFoundation)) {
            if(nextFoundation==std::numeric_limits<Id>::max())return false;
            ++nextFoundation;
        }
        const Id foundation=nextFoundation;reservedIds.insert(foundation);
        if(nextFoundation!=std::numeric_limits<Id>::max())++nextFoundation;
        Entity building;building.id=foundation;building.kind=kind;building.team=0;
        building.pos=point;building.goal=point;building.rally=point;
        building.hp=definition(kind).hp;building.progress=1;building.order=Order::Idle;
        building.nextQueueId=1;building.rallyOverride=rules::productionKind(building.kind);
        projected.entities.push_back(std::move(building));
        return true;
    };
    std::vector<std::pair<Kind,Vec2>> foundations;
    for(const auto& entity:projected.entities)for(const auto& step:entity.futureOrders)
        if(step.order==Order::Construct&&!step.supportTarget)foundations.push_back({step.buildingKind,step.point});
    for(const auto& [kind,point]:foundations)if(!reserveFoundation(kind,point))return false;
    projected.workerPlanNotice.assign(MaxWorkerPlanNoticeBytes,'x');
    if(!projected.workerPlanNoticeSerial)projected.workerPlanNoticeSerial=1;
    // The codec validates the complete projected representation, including
    // support relations and aggregate queue count, before enforcing byte size.
    return !encodeSnapshot(projected).empty();
}

bool sustainedPlanFitsSnapshot(const Simulation& simulation,int team,
                               const std::vector<Entity>& candidates) {
    if(team<0||team>=simulation.playerCount()||candidates.empty())return false;
    auto projected=snapshotFor(simulation,team);
    std::unordered_map<Id,Entity*> owned;
    for(auto& entity:projected.entities)if(entity.team==0)owned.emplace(entity.id,&entity);
    std::unordered_set<Id> recipients;
    for(const auto& candidate:candidates) {
        const Entity* authoritative=simulation.find(candidate.id);
        const auto found=owned.find(candidate.id);
        if(!authoritative||authoritative->team!=team||found==owned.end()||
           !recipients.insert(candidate.id).second||
           (candidate.order!=Order::Patrol&&candidate.order!=Order::Escort))return false;
        Entity& entity=*found->second;
        entity.order=candidate.order;entity.goal=candidate.goal;entity.target=candidate.target;
        entity.resourceTarget=candidate.resourceTarget;entity.returning=candidate.returning;
        entity.builderId=candidate.builderId;entity.resumeGather=candidate.resumeGather;
        entity.supportTarget=candidate.supportTarget;entity.futureOrders=candidate.futureOrders;
        entity.sustained=candidate.sustained;
        entity.hasArrivalFacing=candidate.hasArrivalFacing;
        entity.arrivalFacing=candidate.arrivalFacing;
    }
    return !encodeSnapshot(projected).empty();
}

bool decodeSnapshot(const void* data,std::size_t size,Snapshot& out,std::string& error) {
    auto fail=[&](const char* message){error=message;return false;};if(!data||size>MaxMessageBytes)return fail(size>MaxMessageBytes?"Snapshot exceeds the maximum frame size.":"Snapshot data is missing.");
    Reader reader(data,size);Snapshot snapshot;std::uint32_t version=0;std::uint8_t flag=0;
    if(!reader.bytes(SnapshotMagic,4)||!reader.u32(version)||version!=ProtocolVersion)return fail("Invalid snapshot header.");
    std::uint8_t matchLength=0,playerCount=0;
    if(!reader.i32(snapshot.config.map)||!reader.u32(snapshot.config.seed)||!reader.u8(flag)||flag>1||!reader.real(snapshot.config.aiAggression)||!reader.u8(matchLength)||matchLength>=static_cast<std::uint8_t>(MatchLength::Count)||!reader.u8(playerCount)||!rules::validPlayerCount(playerCount)||!reader.i32(snapshot.config.mapRevision)||!validMapRevision(snapshot.config.mapRevision)||!reader.u64(snapshot.tick)||!reader.u64(snapshot.lastEffectId)||!reader.i8(snapshot.winner)||!reader.u8(snapshot.eliminatedMask))return fail("Truncated or invalid snapshot header.");snapshot.config.ai=flag!=0;snapshot.config.matchLength=static_cast<MatchLength>(matchLength);snapshot.config.playerCount=playerCount;
    if(!reader.i32(snapshot.player.ore)||!reader.i32(snapshot.player.tier)||!reader.i32(snapshot.player.weapons)||!reader.i32(snapshot.player.armor)||
       !reader.point(snapshot.player.armyRally)||!reader.u8(flag)||flag>1||!readStats(reader,snapshot.player.stats))return fail("Truncated snapshot player.");
    snapshot.player.armyRallySet=flag!=0;
    std::uint16_t obstacleCount=0;if(!reader.u16(obstacleCount)||obstacleCount>MaxObstacles)return fail("Invalid snapshot obstacle count.");
    for(std::uint16_t i=0;i<obstacleCount;++i){Obstacle obstacle;if(!reader.point(obstacle.center)||!reader.point(obstacle.half))return fail("Truncated snapshot obstacle.");snapshot.obstacles.push_back(obstacle);}
    std::uint32_t entityCount=0;if(!reader.u32(entityCount)||entityCount>MaxEntities)return fail("Invalid snapshot entity count.");
    for(std::uint32_t i=0;i<entityCount;++i){Entity entity;if(!readEntity(reader,entity))return fail("Truncated or invalid snapshot entity.");snapshot.entities.push_back(std::move(entity));}
    std::uint32_t effectCount=0;if(!reader.u32(effectCount)||effectCount>MaxEffects)return fail("Invalid snapshot effect count.");
    for(std::uint32_t i=0;i<effectCount;++i){Effect effect;int team=0;std::uint8_t type=0,sourceKind=0,targetKind=0;if(!reader.point(effect.from)||!reader.point(effect.to)||!reader.i8(team)||!reader.real(effect.life)||!reader.real(effect.duration)||!reader.u64(effect.id)||!reader.u8(type)||!reader.u8(sourceKind)||!reader.u8(targetKind)||!reader.u8(effect.fromVisibleMask)||!reader.u8(effect.toVisibleMask))return fail("Truncated snapshot effect.");effect.team=team;effect.type=static_cast<EffectType>(type);effect.sourceKind=static_cast<Kind>(sourceKind);effect.targetKind=static_cast<Kind>(targetKind);snapshot.effects.push_back(effect);}
    std::uint16_t runCount=0;if(!reader.u16(runCount)||!runCount||runCount>snapshot.fog.size())return fail("Invalid snapshot fog run count.");
    std::size_t cell=0;for(std::uint16_t i=0;i<runCount;++i){std::uint8_t value=0;std::uint16_t length=0;if(!reader.u8(value)||!reader.u16(length)||value>2||!length||length>snapshot.fog.size()-cell)return fail("Invalid snapshot fog run.");std::fill(snapshot.fog.begin()+cell,snapshot.fog.begin()+cell+length,value);cell+=length;}
    if(cell!=snapshot.fog.size()||!reader.u64(snapshot.workerPlanNoticeSerial)||
       !reader.text(snapshot.workerPlanNotice,MaxWorkerPlanNoticeBytes)||!reader.done())
        return fail("Snapshot has incomplete fog, invalid notice, or trailing data.");
    if(!validSnapshot(snapshot,error))return false;out=std::move(snapshot);error.clear();return true;
}

std::vector<std::uint8_t> encodeCommand(const Command& command,std::uint32_t sequence) {
    if(!sequence||!validCommand(command))return {};
    std::unordered_set<Id> ids;for(Id id:command.units)if(!id||!ids.insert(id).second)return {};
    Writer writer;writer.bytes(CommandMagic,4);writer.u32(ProtocolVersion);writer.u32(sequence);writer.u8(static_cast<std::uint8_t>(command.type));writer.u16(static_cast<std::uint16_t>(command.units.size()));for(Id id:command.units)writer.u32(id);writer.point(command.point);writer.u32(command.target);writer.u8(static_cast<std::uint8_t>(command.kind));writer.i32(command.queueIndex);writer.u8(static_cast<std::uint8_t>(command.queueMode));writer.u8(static_cast<std::uint8_t>(command.spacing));writer.u8(command.hasArrivalFacing?1:0);writer.real(command.arrivalFacing==0.0f?0.0f:command.arrivalFacing);return writer.finish();
}

bool decodeCommand(const void* data,std::size_t size,Command& out,std::uint32_t& sequence,std::string& error) {
    auto fail=[&](const char* message){error=message;return false;};if(!data||size>MaxMessageBytes)return fail(size>MaxMessageBytes?"Command exceeds the maximum frame size.":"Command data is missing.");
    Reader reader(data,size);Command command;std::uint32_t version=0,decodedSequence=0;std::uint8_t type=0,kind=0,queueMode=0,spacing=0,hasFacing=0;std::uint16_t count=0;
    if(!reader.bytes(CommandMagic,4)||!reader.u32(version)||version!=ProtocolVersion)return fail("Invalid command header.");
    if(!reader.u32(decodedSequence)||!decodedSequence||!reader.u8(type)||!reader.u16(count)||count>MaxCommandUnits)return fail("Invalid command sequence or unit count.");
    std::unordered_set<Id> ids;for(std::uint16_t i=0;i<count;++i){Id id=0;if(!reader.u32(id)||!id||!ids.insert(id).second)return fail("Invalid or duplicate command unit.");command.units.push_back(id);}
    if(!reader.point(command.point)||!reader.u32(command.target)||!reader.u8(kind)||!reader.i32(command.queueIndex)||!reader.u8(queueMode)||
       !reader.u8(spacing)||!reader.u8(hasFacing)||hasFacing>1||!reader.real(command.arrivalFacing)||!reader.done())return fail("Truncated command or trailing data.");
    command.type=static_cast<CommandType>(type);command.kind=static_cast<Kind>(kind);command.queueMode=static_cast<CommandQueueMode>(queueMode);
    command.spacing=static_cast<FormationSpacing>(spacing);command.hasArrivalFacing=hasFacing!=0;command.team=0;
    if(command.arrivalFacing==0.0f)command.arrivalFacing=0.0f;
    if(!validCommand(command))return fail("Invalid command value.");
    out=std::move(command);sequence=decodedSequence;error.clear();return true;
}

bool translateCommand(const Simulation& simulation,int team,const ViewMemory& memory,Command& command,std::string& error) {
    auto fail=[&](const char* message){error=message;return false;};
    if(team<0||team>=simulation.playerCount()||!validCommand(command))return fail("Invalid command.");
    Command translated=command;translated.team=team;translated.units.clear();std::unordered_set<Id> ids;
    for(Id handle:command.units) {
        const auto found=memory.entities_.find(handle);if(found==memory.entities_.end()||!ids.insert(found->second).second)return fail("The selection contains an unknown or duplicate unit.");
        const Entity* entity=simulation.find(found->second);if(!entity||!entity->alive()||entity->team!=team||entity->kind==Kind::Resource)return fail("The selection contains unavailable or foreign units.");
        translated.units.push_back(entity->id);
    }
    if(command.queueMode==CommandQueueMode::Append&&
       (command.type==CommandType::Build||command.type==CommandType::ResumeConstruction||command.type==CommandType::Gather)) {
        if((command.type==CommandType::Build||command.type==CommandType::ResumeConstruction)&&translated.units.size()!=1)
            return fail("Queued construction requires one Drudge.");
        for(Id id:translated.units)if(simulation.find(id)->kind!=Kind::Worker)
            return fail("Queued work requires living owned Drudges.");
    }
    translated.target=command.type==CommandType::CancelQueue?command.target:0;
    if(command.target&&command.type!=CommandType::CancelQueue) {
        const auto found=memory.entities_.find(command.target);if(found==memory.entities_.end())return fail("The command target is unknown.");translated.target=found->second;
    }
    const Entity* target=translated.target&&command.type!=CommandType::CancelQueue?simulation.find(translated.target):nullptr;
    if(command.type==CommandType::Attack&&(!target||!target->alive()||target->team<0||target->team==team||!simulation.visible(team,target->pos)))return fail("Choose a visible enemy.");
    if(command.type==CommandType::Gather&&(!target||!target->alive()||target->kind!=Kind::Resource||target->resource<=0||!simulation.explored(team,target->pos)))return fail("Choose an explored ore deposit.");
    if(command.type==CommandType::ResumeConstruction&&(!target||!target->alive()||target->team!=team||!definition(target->kind).building||target->progress>=1))return fail("Choose your unfinished structure.");
    if(command.type==CommandType::Escort&&(!target||!target->alive()||target->team!=team||
       definition(target->kind).building||target->kind==Kind::Resource))return fail("Choose an owned mobile escort target.");
    command=std::move(translated);error.clear();return true;
}
}
