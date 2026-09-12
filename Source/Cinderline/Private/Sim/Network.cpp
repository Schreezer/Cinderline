#include "Sim/Network.h"
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
constexpr float MaxNumber = 1000000000.0f;

bool finite(Vec2 point) { return std::isfinite(point.x) && std::isfinite(point.y); }
bool inWorld(Vec2 point) {
    return finite(point) && point.x >= 0 && point.y >= 0 && point.x <= Simulation::WorldSize && point.y <= Simulation::WorldSize;
}
bool validKind(Kind kind) {
    const int value = static_cast<int>(kind);
    return value >= static_cast<int>(Kind::Worker) && value <= static_cast<int>(Kind::Resource);
}
bool validOrder(Order order) {
    const int value = static_cast<int>(order);
    return value >= static_cast<int>(Order::Idle) && value <= static_cast<int>(Order::Construct);
}
bool validCommandType(CommandType type) {
    const int value = static_cast<int>(type);
    return value >= static_cast<int>(CommandType::Move) && value <= static_cast<int>(CommandType::ResumeConstruction);
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
    if(snapshot.config.map<0||snapshot.config.map>2||!std::isfinite(snapshot.config.aiAggression)||snapshot.config.aiAggression<0.5f||snapshot.config.aiAggression>2.0f)
        return fail("Invalid snapshot configuration.");
    if(snapshot.winner < -1 || snapshot.winner > 1 || snapshot.lastEffectId==std::numeric_limits<std::uint64_t>::max() || !validPlayer(snapshot.player))return fail("Invalid snapshot match state.");
    if(snapshot.entities.size()>MaxEntities||snapshot.obstacles.size()>MaxObstacles||snapshot.effects.size()>MaxEffects)return fail("Snapshot collection is too large.");
    for(const auto& obstacle:snapshot.obstacles) {
        if(!inWorld(obstacle.center)||!finite(obstacle.half)||obstacle.half.x<0||obstacle.half.y<0||obstacle.half.x>Simulation::WorldSize||obstacle.half.y>Simulation::WorldSize)
            return fail("Invalid snapshot obstacle.");
    }
    std::unordered_set<Id> ids;
    for(const auto& entity:snapshot.entities) {
        if(!entity.id||!ids.insert(entity.id).second||!validKind(entity.kind)||!validOrder(entity.order)||!inWorld(entity.pos)||!inWorld(entity.goal)||!inWorld(entity.rally))
            return fail("Invalid or duplicate snapshot entity.");
        if((entity.kind==Kind::Resource&&entity.team!=-1)||(entity.kind!=Kind::Resource&&(entity.team<0||entity.team>1)))return fail("Invalid snapshot entity team.");
        const float values[]{entity.hp,entity.cooldown,entity.progress,entity.carried,entity.harvestTimer,entity.resource,entity.facing,entity.repath};
        for(float value:values)if(!std::isfinite(value)||std::fabs(value)>MaxNumber)return fail("Invalid snapshot entity value.");
        if(entity.hp<0||entity.cooldown<0||entity.progress<0||entity.progress>1||entity.carried<0||entity.harvestTimer<0||entity.resource<0||entity.repath<0)
            return fail("Invalid snapshot entity range.");
        if(entity.queue.size()>Simulation::MaxQueue||entity.path.size()>Simulation::FogSize*Simulation::FogSize+1||entity.pathIndex<0||entity.pathIndex>static_cast<int>(entity.path.size()))
            return fail("Invalid snapshot entity collection.");
        for(const auto& item:entity.queue)if(!validKind(item.kind)||!std::isfinite(item.remaining)||!std::isfinite(item.total)||item.total<=0||item.remaining<0||item.remaining>item.total||item.cost<0||item.cost>static_cast<int>(MaxNumber))
            return fail("Invalid snapshot queue item.");
        for(Vec2 point:entity.path)if(!inWorld(point))return fail("Invalid snapshot path point.");
    }
    for(const auto& entity:snapshot.entities) {
        for(Id reference:{entity.target,entity.resourceTarget,entity.builderId})if(reference&&!ids.count(reference))return fail("Snapshot entity contains an unknown reference.");
    }
    std::unordered_set<std::uint64_t> effectIds;
    for(const auto& effect:snapshot.effects) {
        if(!effect.id||effect.id>snapshot.lastEffectId||!effectIds.insert(effect.id).second||!validEffectType(effect.type)||!validKind(effect.sourceKind)||!validKind(effect.targetKind)||
           effect.sourceKind==Kind::Resource||effect.targetKind==Kind::Resource||effect.team<0||effect.team>1||!inWorld(effect.from)||!inWorld(effect.to)||
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
    writer.u8(static_cast<std::uint8_t>(entity.order));writer.u32(entity.target);writer.u32(entity.resourceTarget);writer.u8(entity.returning?1:0);writer.u32(entity.builderId);writer.u8(entity.resumeGather?1:0);
    writer.u8(static_cast<std::uint8_t>(entity.queue.size()));
    for(const auto& item:entity.queue){writer.u8(static_cast<std::uint8_t>(item.kind));writer.real(item.remaining);writer.real(item.total);writer.i32(item.cost);writer.u8(item.research?1:0);}
    writer.u16(static_cast<std::uint16_t>(entity.path.size()));for(Vec2 point:entity.path)writer.point(point);
    writer.i32(entity.pathIndex);writer.real(entity.repath);
}
bool readEntity(Reader& reader,Entity& entity) {
    std::uint8_t kind=0,order=0,flag=0,queueCount=0;int team=0;
    if(!reader.u32(entity.id)||!reader.u8(kind)||!reader.i8(team)||!reader.point(entity.pos)||!reader.point(entity.goal)||!reader.point(entity.rally)||
       !reader.real(entity.hp)||!reader.real(entity.cooldown)||!reader.real(entity.progress)||!reader.real(entity.carried)||!reader.real(entity.harvestTimer)||!reader.real(entity.resource)||!reader.real(entity.facing)||
       !reader.u8(order)||!reader.u32(entity.target)||!reader.u32(entity.resourceTarget)||!reader.u8(flag))return false;
    entity.kind=static_cast<Kind>(kind);entity.team=team;entity.order=static_cast<Order>(order);if(flag>1)return false;entity.returning=flag!=0;
    if(!reader.u32(entity.builderId)||!reader.u8(flag)||flag>1||!reader.u8(queueCount))return false;entity.resumeGather=flag!=0;
    for(std::uint8_t i=0;i<queueCount;++i){QueueItem item;std::uint8_t itemKind=0,research=0;if(!reader.u8(itemKind)||!reader.real(item.remaining)||!reader.real(item.total)||!reader.i32(item.cost)||!reader.u8(research)||research>1)return false;item.kind=static_cast<Kind>(itemKind);item.research=research!=0;entity.queue.push_back(item);}
    std::uint16_t pathCount=0;if(!reader.u16(pathCount))return false;
    for(std::uint16_t i=0;i<pathCount;++i){Vec2 point;if(!reader.point(point))return false;entity.path.push_back(point);}
    return reader.i32(entity.pathIndex)&&reader.real(entity.repath);
}
}

ViewMemory::ViewMemory() {
    std::random_device random;
    const std::uint64_t entropy=(static_cast<std::uint64_t>(random())<<32)^random();
    const auto clock=static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    secret_=mix(entropy^clock^static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this)));
}

Snapshot snapshotFor(const Simulation& simulation,int viewer,ViewMemory* memory) {
    Snapshot snapshot;if(viewer<0||viewer>1)return snapshot;
    snapshot.config=simulation.config();snapshot.config.ai=false;snapshot.tick=simulation.tick();snapshot.winner=simulation.winner()<0?-1:(simulation.winner()==viewer?0:1);
    snapshot.player=simulation.players()[viewer];snapshot.obstacles=simulation.obstacles();
    for(int cell=0;cell<Simulation::FogSize*Simulation::FogSize;++cell) {
        const int x=cell%Simulation::FogSize,y=cell/Simulation::FogSize;
        const Vec2 point{(x+0.5f)*Simulation::WorldSize/Simulation::FogSize,(y+0.5f)*Simulation::WorldSize/Simulation::FogSize};
        snapshot.fog[cell]=simulation.visible(viewer,point)?2:(simulation.explored(viewer,point)?1:0);
    }
    if(memory) {
        std::unordered_set<Id> live;
        for(const auto& entity:simulation.entities())if(entity.alive())live.insert(entity.id);
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
    for(const Entity* source:included) {
        Entity entity=*source;entity.id=handle(source->id);entity.team=source->team<0?-1:(source->team==viewer?0:1);
        entity.path.clear();entity.pathIndex=0;entity.repath=0;
        if(source->kind==Kind::Resource)entity.resource=memory?memory->resources_.at(source->id):source->resource;
        if(source->team>=0&&source->team!=viewer) {
            entity.goal=entity.pos;entity.rally=entity.pos;entity.cooldown=0;entity.carried=0;entity.harvestTimer=0;entity.resource=0;entity.facing=source->facing;
            entity.order=Order::Idle;entity.target=0;entity.resourceTarget=0;entity.returning=false;entity.builderId=0;entity.resumeGather=false;entity.queue.clear();
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
        Effect effect=source;effect.team=source.team==viewer?0:1;effect.fromVisibleMask=fromVisible?1:0;effect.toVisibleMask=toVisible?1:0;
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
    writer.i32(snapshot.config.map);writer.u32(snapshot.config.seed);writer.u8(snapshot.config.ai?1:0);writer.real(snapshot.config.aiAggression);
    writer.u64(snapshot.tick);writer.u64(snapshot.lastEffectId);writer.i8(snapshot.winner);
    writer.i32(snapshot.player.ore);writer.i32(snapshot.player.tier);writer.i32(snapshot.player.weapons);writer.i32(snapshot.player.armor);writeStats(writer,snapshot.player.stats);
    writer.u16(static_cast<std::uint16_t>(snapshot.obstacles.size()));for(const auto& obstacle:snapshot.obstacles){writer.point(obstacle.center);writer.point(obstacle.half);}
    writer.u32(static_cast<std::uint32_t>(snapshot.entities.size()));for(const auto& entity:snapshot.entities)writeEntity(writer,entity);
    writer.u32(static_cast<std::uint32_t>(snapshot.effects.size()));for(const auto& effect:snapshot.effects){writer.point(effect.from);writer.point(effect.to);writer.i8(effect.team);writer.real(effect.life);writer.real(effect.duration);writer.u64(effect.id);writer.u8(static_cast<std::uint8_t>(effect.type));writer.u8(static_cast<std::uint8_t>(effect.sourceKind));writer.u8(static_cast<std::uint8_t>(effect.targetKind));writer.u8(effect.fromVisibleMask);writer.u8(effect.toVisibleMask);}
    std::vector<std::pair<std::uint8_t,std::uint16_t>> runs;
    for(std::size_t start=0;start<snapshot.fog.size();) {std::size_t end=start+1;while(end<snapshot.fog.size()&&snapshot.fog[end]==snapshot.fog[start]&&end-start<std::numeric_limits<std::uint16_t>::max())++end;runs.push_back({snapshot.fog[start],static_cast<std::uint16_t>(end-start)});start=end;}
    writer.u16(static_cast<std::uint16_t>(runs.size()));for(const auto& run:runs){writer.u8(run.first);writer.u16(run.second);}
    auto result=writer.finish();if(result.size()>MaxMessageBytes)return {};return result;
}

bool decodeSnapshot(const void* data,std::size_t size,Snapshot& out,std::string& error) {
    auto fail=[&](const char* message){error=message;return false;};if(!data||size>MaxMessageBytes)return fail(size>MaxMessageBytes?"Snapshot exceeds the maximum frame size.":"Snapshot data is missing.");
    Reader reader(data,size);Snapshot snapshot;std::uint32_t version=0;std::uint8_t flag=0;
    if(!reader.bytes(SnapshotMagic,4)||!reader.u32(version)||version!=ProtocolVersion)return fail("Invalid snapshot header.");
    if(!reader.i32(snapshot.config.map)||!reader.u32(snapshot.config.seed)||!reader.u8(flag)||flag>1||!reader.real(snapshot.config.aiAggression)||!reader.u64(snapshot.tick)||!reader.u64(snapshot.lastEffectId)||!reader.i8(snapshot.winner))return fail("Truncated snapshot header.");snapshot.config.ai=flag!=0;
    if(!reader.i32(snapshot.player.ore)||!reader.i32(snapshot.player.tier)||!reader.i32(snapshot.player.weapons)||!reader.i32(snapshot.player.armor)||!readStats(reader,snapshot.player.stats))return fail("Truncated snapshot player.");
    std::uint16_t obstacleCount=0;if(!reader.u16(obstacleCount)||obstacleCount>MaxObstacles)return fail("Invalid snapshot obstacle count.");
    for(std::uint16_t i=0;i<obstacleCount;++i){Obstacle obstacle;if(!reader.point(obstacle.center)||!reader.point(obstacle.half))return fail("Truncated snapshot obstacle.");snapshot.obstacles.push_back(obstacle);}
    std::uint32_t entityCount=0;if(!reader.u32(entityCount)||entityCount>MaxEntities)return fail("Invalid snapshot entity count.");
    for(std::uint32_t i=0;i<entityCount;++i){Entity entity;if(!readEntity(reader,entity))return fail("Truncated or invalid snapshot entity.");snapshot.entities.push_back(std::move(entity));}
    std::uint32_t effectCount=0;if(!reader.u32(effectCount)||effectCount>MaxEffects)return fail("Invalid snapshot effect count.");
    for(std::uint32_t i=0;i<effectCount;++i){Effect effect;int team=0;std::uint8_t type=0,sourceKind=0,targetKind=0;if(!reader.point(effect.from)||!reader.point(effect.to)||!reader.i8(team)||!reader.real(effect.life)||!reader.real(effect.duration)||!reader.u64(effect.id)||!reader.u8(type)||!reader.u8(sourceKind)||!reader.u8(targetKind)||!reader.u8(effect.fromVisibleMask)||!reader.u8(effect.toVisibleMask))return fail("Truncated snapshot effect.");effect.team=team;effect.type=static_cast<EffectType>(type);effect.sourceKind=static_cast<Kind>(sourceKind);effect.targetKind=static_cast<Kind>(targetKind);snapshot.effects.push_back(effect);}
    std::uint16_t runCount=0;if(!reader.u16(runCount)||!runCount||runCount>snapshot.fog.size())return fail("Invalid snapshot fog run count.");
    std::size_t cell=0;for(std::uint16_t i=0;i<runCount;++i){std::uint8_t value=0;std::uint16_t length=0;if(!reader.u8(value)||!reader.u16(length)||value>2||!length||length>snapshot.fog.size()-cell)return fail("Invalid snapshot fog run.");std::fill(snapshot.fog.begin()+cell,snapshot.fog.begin()+cell+length,value);cell+=length;}
    if(cell!=snapshot.fog.size()||!reader.done())return fail("Snapshot has incomplete fog or trailing data.");
    if(!validSnapshot(snapshot,error))return false;out=std::move(snapshot);error.clear();return true;
}

std::vector<std::uint8_t> encodeCommand(const Command& command,std::uint32_t sequence) {
    if(!sequence||!validCommandType(command.type)||!validKind(command.kind)||command.units.empty()||command.units.size()>MaxCommandUnits||!inWorld(command.point)||command.queueIndex<0||command.queueIndex>Simulation::MaxQueue)return {};
    std::unordered_set<Id> ids;for(Id id:command.units)if(!id||!ids.insert(id).second)return {};
    Writer writer;writer.bytes(CommandMagic,4);writer.u32(ProtocolVersion);writer.u32(sequence);writer.u8(static_cast<std::uint8_t>(command.type));writer.u16(static_cast<std::uint16_t>(command.units.size()));for(Id id:command.units)writer.u32(id);writer.point(command.point);writer.u32(command.target);writer.u8(static_cast<std::uint8_t>(command.kind));writer.i32(command.queueIndex);return writer.finish();
}

bool decodeCommand(const void* data,std::size_t size,Command& out,std::uint32_t& sequence,std::string& error) {
    auto fail=[&](const char* message){error=message;return false;};if(!data||size>MaxMessageBytes)return fail(size>MaxMessageBytes?"Command exceeds the maximum frame size.":"Command data is missing.");
    Reader reader(data,size);Command command;std::uint32_t version=0,decodedSequence=0;std::uint8_t type=0,kind=0;std::uint16_t count=0;
    if(!reader.bytes(CommandMagic,4)||!reader.u32(version)||version!=ProtocolVersion)return fail("Invalid command header.");
    if(!reader.u32(decodedSequence)||!decodedSequence||!reader.u8(type)||!reader.u16(count)||!count||count>MaxCommandUnits)return fail("Invalid command sequence or unit count.");
    std::unordered_set<Id> ids;for(std::uint16_t i=0;i<count;++i){Id id=0;if(!reader.u32(id)||!id||!ids.insert(id).second)return fail("Invalid or duplicate command unit.");command.units.push_back(id);}
    if(!reader.point(command.point)||!reader.u32(command.target)||!reader.u8(kind)||!reader.i32(command.queueIndex)||!reader.done())return fail("Truncated command or trailing data.");
    command.type=static_cast<CommandType>(type);command.kind=static_cast<Kind>(kind);command.team=0;
    if(!validCommandType(command.type)||!validKind(command.kind)||!inWorld(command.point)||command.queueIndex<0||command.queueIndex>Simulation::MaxQueue)return fail("Invalid command value.");
    out=std::move(command);sequence=decodedSequence;error.clear();return true;
}

bool translateCommand(const Simulation& simulation,int team,const ViewMemory& memory,Command& command,std::string& error) {
    auto fail=[&](const char* message){error=message;return false;};
    if(team<0||team>1||!validCommandType(command.type)||!validKind(command.kind)||!inWorld(command.point)||command.units.empty()||command.units.size()>MaxCommandUnits||command.queueIndex<0||command.queueIndex>Simulation::MaxQueue)return fail("Invalid command.");
    Command translated=command;translated.team=team;translated.units.clear();std::unordered_set<Id> ids;
    for(Id handle:command.units) {
        const auto found=memory.entities_.find(handle);if(found==memory.entities_.end()||!ids.insert(found->second).second)return fail("The selection contains an unknown or duplicate unit.");
        const Entity* entity=simulation.find(found->second);if(!entity||!entity->alive()||entity->team!=team||entity->kind==Kind::Resource)return fail("The selection contains unavailable or foreign units.");
        translated.units.push_back(entity->id);
    }
    translated.target=0;
    if(command.target) {
        const auto found=memory.entities_.find(command.target);if(found==memory.entities_.end())return fail("The command target is unknown.");translated.target=found->second;
    }
    const Entity* target=translated.target?simulation.find(translated.target):nullptr;
    if(command.type==CommandType::Attack&&(!target||!target->alive()||target->team<0||target->team==team||!simulation.visible(team,target->pos)))return fail("Choose a visible enemy.");
    if(command.type==CommandType::Gather&&(!target||!target->alive()||target->kind!=Kind::Resource||target->resource<=0||!simulation.explored(team,target->pos)))return fail("Choose an explored ore deposit.");
    if(command.type==CommandType::ResumeConstruction&&(!target||!target->alive()||target->team!=team||!definition(target->kind).building||target->progress>=1))return fail("Choose your unfinished structure.");
    command=std::move(translated);error.clear();return true;
}
}
