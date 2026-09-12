#include "Sim/Network.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cinder;
namespace {
void check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
bool same(Vec2 left,Vec2 right){return std::fabs(left.x-right.x)<0.01f&&std::fabs(left.y-right.y)<0.01f;}
std::vector<Id> ids(const Simulation& simulation,int team,Kind kind){std::vector<Id> result;for(const auto& entity:simulation.entities())if(entity.alive()&&entity.team==team&&entity.kind==kind)result.push_back(entity.id);return result;}
Id first(const Simulation& simulation,int team,Kind kind){const auto found=ids(simulation,team,kind);check(!found.empty(),"fixture entity missing");return found.front();}
void advance(Simulation& simulation,float seconds){for(int step=0;step<static_cast<int>(std::ceil(seconds/Simulation::Step));++step)simulation.update(Simulation::Step);}
const Entity* snapshotEntity(const net::Snapshot& snapshot,int team,Kind kind){for(const auto& entity:snapshot.entities)if(entity.team==team&&entity.kind==kind)return &entity;return nullptr;}
const Entity* snapshotAt(const net::Snapshot& snapshot,Vec2 point,Kind kind){for(const auto& entity:snapshot.entities)if(entity.kind==kind&&same(entity.pos,point))return &entity;return nullptr;}
std::uint16_t getU16(const std::vector<std::uint8_t>& bytes,std::size_t offset){return static_cast<std::uint16_t>(bytes.at(offset)|(static_cast<std::uint16_t>(bytes.at(offset+1))<<8));}
std::uint32_t getU32(const std::vector<std::uint8_t>& bytes,std::size_t offset){std::uint32_t value=0;for(int i=0;i<4;++i)value|=static_cast<std::uint32_t>(bytes.at(offset+i))<<(8*i);return value;}
void putU16(std::vector<std::uint8_t>& bytes,std::size_t offset,std::uint16_t value){for(int i=0;i<2;++i)bytes.at(offset+i)=static_cast<std::uint8_t>(value>>(8*i));}
void putU32(std::vector<std::uint8_t>& bytes,std::size_t offset,std::uint32_t value){for(int i=0;i<4;++i)bytes.at(offset+i)=static_cast<std::uint8_t>(value>>(8*i));}
std::size_t firstEntityOffset(const std::vector<std::uint8_t>& bytes){const std::size_t obstacleCountOffset=90;return obstacleCountOffset+2+static_cast<std::size_t>(getU16(bytes,obstacleCountOffset))*16+4;}
std::size_t nextEntityOffset(const std::vector<std::uint8_t>& bytes,std::size_t offset){std::size_t cursor=offset+74;const auto queueCount=bytes.at(offset+73);cursor+=static_cast<std::size_t>(queueCount)*14;const auto pathCount=getU16(bytes,cursor);cursor+=2+static_cast<std::size_t>(pathCount)*8+8;return cursor;}
void rejectsSnapshot(const std::vector<std::uint8_t>& bytes,const std::string& message){net::Snapshot decoded;std::string error;check(!net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error)&&!error.empty(),message);}
void rejectsCommand(const std::vector<std::uint8_t>& bytes,const std::string& message){Command decoded;std::uint32_t sequence=99;std::string error;check(!net::decodeCommand(bytes.data(),bytes.size(),decoded,sequence,error)&&!error.empty(),message);}

void viewPrivacyAndHandles(){
    Simulation simulation;simulation.reset({0,42,false,1});
    const Id scout0=simulation.debugSpawn(Kind::Scout,0,{4060,4200});
    simulation.debugSpawn(Kind::Scout,1,{740,600});
    net::ViewMemory memory0,memory1;
    const auto view0=net::snapshotFor(simulation,0,&memory0),view1=net::snapshotFor(simulation,1,&memory1);
    for(int viewer=0;viewer<2;++viewer){
        const auto& view=viewer?view1:view0;
        check(view.player.ore==simulation.players()[viewer].ore,"snapshot sends only the recipient player economy");
        const auto ownCount=static_cast<std::size_t>(std::count_if(simulation.entities().begin(),simulation.entities().end(),[&](const Entity& entity){return entity.alive()&&entity.team==viewer;}));
        check(static_cast<std::size_t>(std::count_if(view.entities.begin(),view.entities.end(),[](const Entity& entity){return entity.team==0;}))==ownCount,"recipient entities normalize to team zero");
        for(const auto& entity:view.entities)if(entity.team==1){
            check(entity.order==Order::Idle&&entity.target==0&&entity.resourceTarget==0&&entity.builderId==0&&entity.queue.empty()&&entity.path.empty(),"visible enemy orders and references are stripped");
            check(entity.carried==0&&entity.harvestTimer==0&&entity.resource==0&&entity.cooldown==0&&entity.goal.x==entity.pos.x&&entity.rally.x==entity.pos.x,"visible enemy private state is zeroed");
        }
        check(std::none_of(view.entities.begin(),view.entities.end(),[&](const Entity& entity){return entity.team==1&&!simulation.visible(viewer,entity.pos);}),"hidden enemies are absent from each recipient view");
    }
    const auto again=net::snapshotFor(simulation,0,&memory0);
    check(snapshotEntity(view0,0,Kind::Headquarters)->id==snapshotEntity(again,0,Kind::Headquarters)->id,"opaque handles remain stable for reconnect snapshots");
    check(snapshotEntity(view0,0,Kind::Headquarters)->id!=first(simulation,0,Kind::Headquarters),"network handles do not expose authoritative creation IDs");

    const Entity* ownScout=snapshotAt(view0,simulation.find(scout0)->pos,Kind::Scout);const Entity* enemyHQ=snapshotEntity(view0,1,Kind::Headquarters);
    check(ownScout&&enemyHQ,"translation fixture includes an owned attacker and visible enemy");
    Command attack{CommandType::Attack,1,{ownScout->id},{},enemyHQ->id,Kind::Worker,0};
    auto encoded=net::encodeCommand(attack,7);check(!encoded.empty(),"ordinary attack command encodes");
    Command decoded;std::uint32_t sequence=0;std::string error;check(net::decodeCommand(encoded.data(),encoded.size(),decoded,sequence,error)&&sequence==7&&decoded.team==0,"command wire data omits untrusted team identity");
    check(net::translateCommand(simulation,0,memory0,decoded,error)&&decoded.team==0&&decoded.units.front()==scout0,"server resolves the recipient's opaque handles");
    check(simulation.command(decoded).accepted,"translated ordinary rule command remains accepted");
    Command foreign{CommandType::Move,0,{enemyHQ->id},{1000,1000},0,Kind::Worker,0};
    check(!net::translateCommand(simulation,0,memory0,foreign,error),"enemy handles cannot be submitted as owned units");
    Id unknown=1;while(std::any_of(view0.entities.begin(),view0.entities.end(),[&](const Entity& entity){return entity.id==unknown;}))++unknown;
    Command forged{CommandType::Move,0,{unknown},{1000,1000},0,Kind::Worker,0};
    check(!net::translateCommand(simulation,0,memory0,forged,error),"unknown opaque handles are rejected");
}

void resourceMemory(){
    Simulation simulation;simulation.reset({0,55,false,1});
    const Entity* chosen=nullptr;float farthest=0;
    for(const auto& entity:simulation.entities())if(entity.kind==Kind::Resource){const float distance=std::hypot(entity.pos.x-600,entity.pos.y-600);if(distance>farthest){farthest=distance;chosen=&entity;}}
    check(chosen,"resource fixture exists");const Id resourceId=chosen->id;const Vec2 resourcePos=chosen->pos;
    const Id scout=simulation.debugSpawn(Kind::Scout,0,{resourcePos.x-100,resourcePos.y});
    net::ViewMemory memory;const auto observed=net::snapshotFor(simulation,0,&memory);const Entity* firstResource=snapshotAt(observed,resourcePos,Kind::Resource);
    check(firstResource,"visible resource enters per-recipient memory");const float remembered=firstResource->resource;const Id handle=firstResource->id;
    const Id miner=simulation.debugSpawn(Kind::Worker,1,{resourcePos.x+80,resourcePos.y});
    check(simulation.command({CommandType::Move,0,{scout},{600,600},0,Kind::Worker,0}).accepted,"observer leaves resource normally");
    check(simulation.command({CommandType::Gather,1,{miner},{},resourceId,Kind::Worker,0}).accepted,"opponent mines observed resource normally");
    advance(simulation,30);
    check(!simulation.visible(0,resourcePos)&&simulation.find(resourceId)->resource<remembered,"resource changes while outside recipient vision");
    const auto hidden=net::snapshotFor(simulation,0,&memory);const Entity* retained=snapshotAt(hidden,resourcePos,Kind::Resource);
    check(retained&&retained->id==handle&&retained->resource==remembered,"hidden resource retains its last observed amount and stable handle");
}

void effectsPrivacy(){
    Simulation simulation;simulation.reset({0,71,false,1});
    const Id victim=simulation.debugSpawn(Kind::Worker,0,{2600,2400});const Id shooter=simulation.debugSpawn(Kind::Mortar,1,{2000,2400});
    check(!simulation.visible(0,simulation.find(shooter)->pos)&&simulation.visible(0,simulation.find(victim)->pos),"effect fixture hides its shooter and shows its victim");
    check(simulation.command({CommandType::Attack,1,{shooter},{},victim,Kind::Worker,0}).accepted,"authoritative hidden-source attack is legal");advance(simulation,Simulation::Step);
    net::ViewMemory memory;const auto view=net::snapshotFor(simulation,0,&memory);check(!view.effects.empty(),"visible combat geometry reaches recipient");
    for(const auto& effect:view.effects){check(!same(effect.from,simulation.find(shooter)->pos)&&!same(effect.to,simulation.find(shooter)->pos),"hidden source coordinates are stripped");check(effect.sourceKind!=Kind::Mortar,"hidden source kind is stripped");check(effect.id&&effect.id<=view.lastEffectId,"effect uses a bounded per-view event id");}
    const auto repeated=net::snapshotFor(simulation,0,&memory);check(repeated.effects.size()==view.effects.size(),"active view effects survive a full replacement snapshot");
    for(std::size_t i=0;i<view.effects.size();++i)check(view.effects[i].id==repeated.effects[i].id,"active effect IDs remain stable in a recipient view");
}

void codecValidation(){
    Simulation simulation;simulation.reset({0,88,false,1});net::ViewMemory memory;const auto source=net::snapshotFor(simulation,0,&memory);
    const auto bytes=net::encodeSnapshot(source);check(!bytes.empty()&&bytes.size()<net::MaxMessageBytes,"ordinary filtered snapshot encodes compactly");
    net::Snapshot decoded;std::string error;check(net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error)&&decoded.tick==source.tick&&decoded.entities.size()==source.entities.size()&&decoded.fog==source.fog,"ordinary snapshot round trips through explicit codec");
    auto malformed=bytes;malformed.pop_back();rejectsSnapshot(malformed,"truncated snapshot is rejected");
    malformed=bytes;malformed.push_back(0);rejectsSnapshot(malformed,"snapshot trailing bytes are rejected");
    malformed=bytes;putU32(malformed,17,0x7fc00000U);rejectsSnapshot(malformed,"snapshot NaN is rejected");
    const auto firstOffset=firstEntityOffset(bytes);malformed=bytes;malformed.at(firstOffset+4)=255;rejectsSnapshot(malformed,"snapshot enum is rejected");
    const auto secondOffset=nextEntityOffset(bytes,firstOffset);malformed=bytes;putU32(malformed,secondOffset,getU32(malformed,firstOffset));rejectsSnapshot(malformed,"duplicate snapshot entity IDs are rejected");
    malformed=bytes;const std::size_t entityCountOffset=firstOffset-4;putU32(malformed,entityCountOffset,10001);rejectsSnapshot(malformed,"oversized snapshot entity count is rejected");
    std::vector<std::uint8_t> oversized(net::MaxMessageBytes+1);rejectsSnapshot(oversized,"oversized snapshot frame is rejected");

    const auto& own=source.entities;std::vector<Id> handles;for(const auto& entity:own)if(entity.team==0&&entity.kind==Kind::Worker&&handles.size()<2)handles.push_back(entity.id);check(handles.size()==2,"command codec fixture has two workers");
    Command command{CommandType::Move,1,handles,{900,900},0,Kind::Worker,0};const auto commandBytes=net::encodeCommand(command,19);check(!commandBytes.empty(),"ordinary multi-unit command encodes");
    Command decodedCommand;std::uint32_t sequence=0;check(net::decodeCommand(commandBytes.data(),commandBytes.size(),decodedCommand,sequence,error)&&sequence==19&&decodedCommand.units==handles&&decodedCommand.team==0,"ordinary command round trips without client team");
    malformed=commandBytes;malformed.pop_back();rejectsCommand(malformed,"truncated command is rejected");
    malformed=commandBytes;malformed.push_back(0);rejectsCommand(malformed,"command trailing bytes are rejected");
    malformed=commandBytes;putU32(malformed,8,0);rejectsCommand(malformed,"zero command sequence is rejected");
    malformed=commandBytes;malformed.at(12)=255;rejectsCommand(malformed,"command enum is rejected");
    malformed=commandBytes;putU32(malformed,23,0x7fc00000U);rejectsCommand(malformed,"command NaN is rejected");
    malformed=commandBytes;putU32(malformed,19,getU32(malformed,15));rejectsCommand(malformed,"duplicate command handles are rejected");
    malformed=commandBytes;putU32(malformed,15,0);rejectsCommand(malformed,"zero command handle is rejected");
    malformed=commandBytes;putU16(malformed,13,257);rejectsCommand(malformed,"oversized command selection is rejected");
    oversized.assign(net::MaxMessageBytes+1,0);rejectsCommand(oversized,"oversized command frame is rejected");
}

void replicaGuards(){
    Simulation authority;authority.reset({1,99,false,1});net::ViewMemory memory;auto snapshot=net::snapshotFor(authority,1,&memory);
    const auto path=(std::filesystem::temp_directory_path()/"cinderline-network-authority.sav").string();check(authority.save(path),"authoritative fixture saves");
    Simulation replica;std::string error;const auto untouched=replica.stateHash();auto invalid=snapshot;invalid.winner=7;
    check(!replica.applySnapshot(invalid,&error)&&replica.stateHash()==untouched,"invalid snapshot import is atomic");
    check(replica.applySnapshot(snapshot,&error)&&replica.isReplica(),"snapshot atomically enters replica mode");
    check(replica.players()[0].ore==authority.players()[1].ore&&replica.players()[1].ore==0&&replica.players()[1].stats.damage==0,"replica owns normalized player zero and no opponent economy");
    const auto before=replica.stateHash();const Id own=first(replica,0,Kind::Worker);const int ore=replica.players()[0].ore;
    replica.update(10);check(!replica.command({CommandType::Move,0,{own},{1000,1000},0,Kind::Worker,0}).accepted,"replica rejects local commands");
    check(!replica.save(path+".replica")&&!replica.load(path),"replica rejects offline save and load");
    check(replica.debugSpawn(Kind::Scout,0,{1000,1000})==0,"replica rejects debug spawning");replica.debugResources(0,ore+5000);replica.forfeit(0);
    check(replica.stateHash()==before,"all replica mutation entry points preserve imported state");
    replica.reset({});check(!replica.isReplica(),"normal reset restores authoritative mode");check(replica.load(path)&&!replica.isReplica(),"normal load restores authoritative mode");
    Simulation forfeited;forfeited.forfeit(0);check(forfeited.winner()==1,"authoritative forfeit selects the opposing winner");
    std::filesystem::remove(path);std::filesystem::remove(path+".replica");
}
}

int main(){
    const std::vector<std::pair<std::string,void(*)()>> tests{{"view privacy and handles",viewPrivacyAndHandles},{"resource memory",resourceMemory},{"effect privacy",effectsPrivacy},{"bounded codecs",codecValidation},{"replica guards",replicaGuards}};
    int failed=0;for(const auto& test:tests)try{test.second();std::cout<<"PASS "<<test.first<<'\n';}catch(const std::exception& exception){++failed;std::cerr<<"FAIL "<<test.first<<": "<<exception.what()<<'\n';}
    std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';return failed?1:0;
}
