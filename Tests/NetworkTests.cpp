#include "Sim/Network.h"
#include "Sim/MapDefinition.h"
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
Entity* edit(Simulation& simulation,Id id){for(auto& entity:const_cast<std::vector<Entity>&>(simulation.entities()))if(entity.id==id)return &entity;return nullptr;}
void advance(Simulation& simulation,float seconds){for(int step=0;step<static_cast<int>(std::ceil(seconds/Simulation::Step));++step)simulation.update(Simulation::Step);}
const Entity* snapshotEntity(const net::Snapshot& snapshot,int team,Kind kind){for(const auto& entity:snapshot.entities)if(entity.team==team&&entity.kind==kind)return &entity;return nullptr;}
const Entity* snapshotAt(const net::Snapshot& snapshot,Vec2 point,Kind kind){for(const auto& entity:snapshot.entities)if(entity.kind==kind&&same(entity.pos,point))return &entity;return nullptr;}
std::uint16_t getU16(const std::vector<std::uint8_t>& bytes,std::size_t offset){return static_cast<std::uint16_t>(bytes.at(offset)|(static_cast<std::uint16_t>(bytes.at(offset+1))<<8));}
std::uint32_t getU32(const std::vector<std::uint8_t>& bytes,std::size_t offset){std::uint32_t value=0;for(int i=0;i<4;++i)value|=static_cast<std::uint32_t>(bytes.at(offset+i))<<(8*i);return value;}
void putU16(std::vector<std::uint8_t>& bytes,std::size_t offset,std::uint16_t value){for(int i=0;i<2;++i)bytes.at(offset+i)=static_cast<std::uint8_t>(value>>(8*i));}
void putU32(std::vector<std::uint8_t>& bytes,std::size_t offset,std::uint32_t value){for(int i=0;i<4;++i)bytes.at(offset+i)=static_cast<std::uint8_t>(value>>(8*i));}
constexpr std::size_t MapRevisionOffset=23;
constexpr std::size_t ObstacleCountOffset=106;
std::size_t firstEntityOffset(const std::vector<std::uint8_t>& bytes){return ObstacleCountOffset+2+static_cast<std::size_t>(getU16(bytes,ObstacleCountOffset))*16+4;}
std::size_t nextQueueIdOffset(const std::vector<std::uint8_t>& bytes,std::size_t offset){std::size_t cursor=offset+75;const auto queueCount=bytes.at(offset+74);cursor+=static_cast<std::size_t>(queueCount)*18;const auto pathCount=getU16(bytes,cursor);return cursor+2+static_cast<std::size_t>(pathCount)*8+9;}
std::size_t tacticalStateOffset(const std::vector<std::uint8_t>& bytes,std::size_t offset){return nextQueueIdOffset(bytes,offset)+4;}
std::size_t futureCountOffset(const std::vector<std::uint8_t>& bytes,std::size_t offset){const auto tactical=tacticalStateOffset(bytes,offset);const auto order=static_cast<Order>(bytes.at(offset+58));return tactical+9+(order==Order::Patrol?30:order==Order::Escort?17:0);}
std::size_t nextEntityOffset(const std::vector<std::uint8_t>& bytes,std::size_t offset){const auto future=futureCountOffset(bytes,offset);return future+1+static_cast<std::size_t>(bytes.at(future))*19;}
void rejectsSnapshot(const std::vector<std::uint8_t>& bytes,const std::string& message){net::Snapshot decoded;std::string error;check(!net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error)&&!error.empty(),message);}
void rejectsCommand(const std::vector<std::uint8_t>& bytes,const std::string& message){Command decoded;std::uint32_t sequence=99;std::string error;check(!net::decodeCommand(bytes.data(),bytes.size(),decoded,sequence,error)&&!error.empty(),message);}

void terrainRevisionCompatibility(){
    check(Config{}.mapRevision==CurrentMapRevision,"fresh matches select the current map revision");
    for(int revision=0;revision<=CurrentMapRevision;++revision)for(int map=0;map<3;++map)
        for(int players:{2,4})for(MatchLength length:{MatchLength::Short,MatchLength::Standard,MatchLength::Long}) {
            Config config;config.map=map;config.playerCount=players;config.matchLength=length;
            config.mapRevision=revision;config.ai=false;
            Simulation authority;authority.reset(config);
            const auto source=net::snapshotFor(authority,0);
            const auto bytes=net::encodeSnapshot(source);net::Snapshot decoded;std::string error;
            check(!bytes.empty()&&net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error),
                  "each supported map variant round trips with an explicit terrain revision");
            check(decoded.config.mapRevision==revision&&decoded.config.map==map&&
                  decoded.config.playerCount==players&&decoded.config.matchLength==length,
                  "snapshot retains the full map identity");
            Simulation replica;
            check(replica.applySnapshot(decoded,&error)&&replica.config().mapRevision==revision&&
                  replica.usesAuthoredTerrain()==authority.usesAuthoredTerrain(),
                  "replica selects the same authored or legacy terrain as its authority");
            for(Vec2 point:mapDefinition(map,players,length,revision).starts)
                check(replica.terrainHeight(point)==authority.terrainHeight(point),
                      "replica bases retain authoritative ground height");
        }

    Simulation current;
    auto source=net::snapshotFor(current,0);
    const auto bytes=net::encodeSnapshot(source);
    check(!bytes.empty()&&current.usesAuthoredTerrain(),"revision fixture uses the authored duel");
    for(int invalid:{-1,CurrentMapRevision+1}) {
        auto unsupported=source;unsupported.config.mapRevision=invalid;
        check(net::encodeSnapshot(unsupported).empty(),"encoder rejects unsupported terrain revisions");
        auto malformed=bytes;putU32(malformed,MapRevisionOffset,static_cast<std::uint32_t>(invalid));
        rejectsSnapshot(malformed,"decoder rejects unsupported terrain revisions");
        net::Snapshot destination=source;destination.tick=123456;std::string error;
        check(!net::decodeSnapshot(malformed.data(),malformed.size(),destination,error)&&destination.tick==123456,
              "rejected terrain revisions leave the destination snapshot untouched");
        Simulation replica;const auto before=replica.stateHash();
        check(!replica.applySnapshot(unsupported,&error)&&replica.stateHash()==before&&!replica.isReplica(),
              "unsupported terrain import cannot mutate a simulation");
    }
    auto disagreement=source;disagreement.obstacles.front().center.x+=1;
    check(net::encodeSnapshot(disagreement).empty(),"authored snapshots require canonical terrain obstacles");
    auto malformed=bytes;putU32(malformed,ObstacleCountOffset+2,0);
    rejectsSnapshot(malformed,"decoder rejects geometry that disagrees with authored terrain");
    disagreement=source;disagreement.obstacles.pop_back();
    check(net::encodeSnapshot(disagreement).empty(),"authored snapshots reject missing terrain barriers");
    disagreement=source;std::swap(disagreement.obstacles[0],disagreement.obstacles[1]);
    check(net::encodeSnapshot(disagreement).empty(),"authored snapshots require stable barrier ordering");

    Config legacyConfig;legacyConfig.mapRevision=0;legacyConfig.ai=false;
    Simulation legacy;legacy.reset(legacyConfig);
    auto legacyView=net::snapshotFor(legacy,0);legacyView.obstacles.clear();
    const auto legacyBytes=net::encodeSnapshot(legacyView);net::Snapshot decoded;std::string error;
    check(!legacyBytes.empty()&&net::decodeSnapshot(legacyBytes.data(),legacyBytes.size(),decoded,error),
          "legacy revision retains support for custom flat obstacle sets");
    Simulation replica;check(replica.applySnapshot(decoded,&error)&&!replica.usesAuthoredTerrain()&&
          replica.terrainHeight(mapDefinition(0,2,MatchLength::Standard).starts.front())==0,
          "legacy snapshots never acquire authored terrain height");
    legacyView.config.mapRevision=CurrentMapRevision;
    check(net::encodeSnapshot(legacyView).empty(),"legacy geometry cannot be relabeled as the new authored map");
}

void viewPrivacyAndHandles(){
    Simulation simulation;simulation.reset({0,42,false,1,MatchLength::Standard,2,0});
    const Id scout0=simulation.debugSpawn(Kind::Scout,0,{4060,4200});
    const Id scout1=simulation.debugSpawn(Kind::Scout,1,{740,600});
    const Id foundry0=simulation.debugSpawn(Kind::Foundry,0,{740,760});
    const Id foundry1=simulation.debugSpawn(Kind::Foundry,1,{4060,4040});
    check(simulation.command({CommandType::AutoRally,0,{}, {1500,1400},0,Kind::Resource,0}).accepted,"first team sets its private army rally state");
    check(simulation.command({CommandType::AutoRally,1,{}, {3300,3400},0,Kind::Resource,0}).accepted,"second team sets distinct private army rally state");
    check(simulation.command({CommandType::AutoRally,0,{}, {1250,1200},foundry0,Kind::Foundry,0}).accepted,"first team gives one producer an explicit override");
    check(simulation.command({CommandType::Defend,0,{scout0},{3940,4200}}).accepted,"first recipient can establish an authoritative defense anchor");
    check(simulation.command({CommandType::Defend,1,{scout1},{860,600}}).accepted,"second recipient can establish an authoritative defense anchor");
    for(Entity& entity:const_cast<std::vector<Entity>&>(simulation.entities()))if(entity.id==scout0||entity.id==scout1){
        entity.workTarget=entity.id+1000;entity.workPoint={1234,2345};entity.workPointValid=true;
        entity.navigationAnchor={3456,456};entity.stalledFor=7;entity.yieldFor=8;entity.navigationBestDistance=9;entity.pathGeometry=10;
        entity.navigationFailures=11;entity.avoidanceSide=-1;entity.navigationExhausted=true;
    }
    net::ViewMemory memory0,memory1;
    const auto view0=net::snapshotFor(simulation,0,&memory0),view1=net::snapshotFor(simulation,1,&memory1);
    for(int viewer=0;viewer<2;++viewer){
        const auto& view=viewer?view1:view0;
        check(view.player.ore==simulation.players()[viewer].ore,"snapshot sends only the recipient player economy");
        check(view.player.armyRallySet&&same(view.player.armyRally,simulation.players()[viewer].armyRally),"snapshot sends only the recipient's persistent army rally");
        const auto ownCount=static_cast<std::size_t>(std::count_if(simulation.entities().begin(),simulation.entities().end(),[&](const Entity& entity){return entity.alive()&&entity.team==viewer;}));
        check(static_cast<std::size_t>(std::count_if(view.entities.begin(),view.entities.end(),[](const Entity& entity){return entity.team==0;}))==ownCount,"recipient entities normalize to team zero");
        const Entity* ownedScout=snapshotAt(view,simulation.find(viewer?scout1:scout0)->pos,Kind::Scout);
        const Entity* enemyScout=snapshotAt(view,simulation.find(viewer?scout0:scout1)->pos,Kind::Scout);
        const Entity* ownedFoundry=snapshotAt(view,simulation.find(viewer?foundry1:foundry0)->pos,Kind::Foundry);
        check(ownedScout&&ownedScout->team==0&&ownedScout->navigationExhausted,"owned route failure is exposed to its recipient");
        check(enemyScout&&enemyScout->team==1&&!enemyScout->navigationExhausted,"visible enemy route failure is private");
        check(ownedFoundry&&ownedFoundry->rallyOverride==(viewer==0),"owned facility rally override state reaches only its recipient");
        for(const auto& entity:view.entities) {
            check(entity.workTarget==0&&same(entity.workPoint,{})&&!entity.workPointValid&&same(entity.navigationAnchor,{})&&entity.stalledFor==0&&entity.yieldFor==0&&entity.navigationBestDistance==0&&entity.pathGeometry==0&&entity.navigationFailures==0&&entity.avoidanceSide==0,"snapshot objects strip private navigation state");
        }
        for(const auto& entity:view.entities)if(entity.team==1){
            check(entity.order==Order::Idle&&entity.target==0&&entity.resourceTarget==0&&entity.builderId==0&&entity.queue.empty()&&entity.path.empty(),"visible enemy orders and references are stripped");
            check(entity.nextQueueId==1,"visible enemy producer job sequence is private");
            check(entity.carried==0&&entity.harvestTimer==0&&entity.resource==0&&entity.cooldown==0&&entity.goal.x==entity.pos.x&&entity.rally.x==entity.pos.x,"visible enemy private state is zeroed");
            check(!entity.navigationExhausted,"visible enemy route failure is private");
            check(!entity.rallyOverride,"visible enemy rally override state is private");
        }
        check(std::none_of(view.entities.begin(),view.entities.end(),[&](const Entity& entity){return entity.team==1&&!simulation.visible(viewer,entity.pos);}),"hidden enemies are absent from each recipient view");

        const auto bytes=net::encodeSnapshot(view);net::Snapshot decoded;std::string error;
        check(!bytes.empty()&&net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error),"navigation privacy snapshot round trips");
        const Entity* decodedScout=snapshotAt(decoded,ownedScout->pos,Kind::Scout);
        const Entity* decodedEnemyScout=snapshotAt(decoded,enemyScout->pos,Kind::Scout);
        check(decodedScout&&decodedScout->order==Order::Defend,"owned Defend order survives the snapshot wire codec");
        check(decodedScout&&decodedScout->navigationExhausted,"owned route failure survives the snapshot wire codec");
        check(decodedEnemyScout&&!decodedEnemyScout->navigationExhausted,"enemy route failure stays private through the snapshot wire codec");
        for(const auto& entity:decoded.entities)if(entity.team==1)check(!entity.navigationExhausted,"enemy route failure stays private after wire decoding");
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

    const Entity* ownFoundry=snapshotAt(view0,simulation.find(foundry0)->pos,Kind::Foundry);
    const Entity* enemyFoundry=snapshotAt(view0,simulation.find(foundry1)->pos,Kind::Foundry);
    check(ownFoundry&&enemyFoundry,"automation fixture exposes opaque owned and visible foreign producer handles");
    Command autoTrain{CommandType::AutoTrain,0,{}, {},ownFoundry->id,Kind::Striker,2};
    check(net::translateCommand(simulation,0,memory0,autoTrain,error)&&autoTrain.team==0&&autoTrain.target==foundry0&&autoTrain.units.empty(),"empty-selection automation translates its owned producer pin");
    check(simulation.command(autoTrain).accepted&&simulation.find(foundry0)->queue.size()==2,"translated automation reaches authoritative production rules");
    const Id jobId=simulation.find(foundry0)->queue.front().id;
    Command cancel{CommandType::CancelQueue,0,{ownFoundry->id},{},jobId,Kind::Worker,0};
    check(net::translateCommand(simulation,0,memory0,cancel,error)&&cancel.units==std::vector<Id>{foundry0}&&cancel.target==jobId,"queue cancellation translates its producer but preserves the stable job ID");
    check(simulation.command(cancel).accepted&&simulation.find(foundry0)->queue.size()==1,"translated stable-ID cancellation mutates only authoritative state");

    Command unknownPin{CommandType::AutoTrain,0,{}, {},unknown,Kind::Striker,1};
    const Command originalUnknownPin=unknownPin;
    check(!net::translateCommand(simulation,0,memory0,unknownPin,error)&&unknownPin.target==originalUnknownPin.target&&unknownPin.team==originalUnknownPin.team,"unknown automation pin is rejected atomically");
    Command foreignPin{CommandType::AutoTrain,0,{}, {},enemyFoundry->id,Kind::Striker,1};
    check(net::translateCommand(simulation,0,memory0,foreignPin,error)&&foreignPin.target==foundry1,"visible producer handles translate without granting ownership");
    const auto beforeForeign=simulation.stateHash();
    check(!simulation.command(foreignPin).accepted&&simulation.stateHash()==beforeForeign,"authoritative simulation rejects a translated foreign automation pin without mutation");
}

void resourceMemory(){
    Simulation simulation;simulation.reset({0,55,false,1,MatchLength::Standard,2,0});
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
    Simulation simulation;simulation.reset({0,71,false,1,MatchLength::Standard,2,0});
    const Id victim=simulation.debugSpawn(Kind::Worker,0,{2600,2400});const Id shooter=simulation.debugSpawn(Kind::Mortar,1,{2000,2400});
    check(!simulation.visible(0,simulation.find(shooter)->pos)&&simulation.visible(0,simulation.find(victim)->pos),"effect fixture hides its shooter and shows its victim");
    check(simulation.command({CommandType::Attack,1,{shooter},{},victim,Kind::Worker,0}).accepted,"authoritative hidden-source attack is legal");advance(simulation,Simulation::Step);
    net::ViewMemory memory;const auto view=net::snapshotFor(simulation,0,&memory);check(!view.effects.empty(),"visible combat geometry reaches recipient");
    for(const auto& effect:view.effects){check(!same(effect.from,simulation.find(shooter)->pos)&&!same(effect.to,simulation.find(shooter)->pos),"hidden source coordinates are stripped");check(effect.sourceKind!=Kind::Mortar,"hidden source kind is stripped");check(effect.id&&effect.id<=view.lastEffectId,"effect uses a bounded per-view event id");}
    const auto repeated=net::snapshotFor(simulation,0,&memory);check(repeated.effects.size()==view.effects.size(),"active view effects survive a full replacement snapshot");
    for(std::size_t i=0;i<view.effects.size();++i)check(view.effects[i].id==repeated.effects[i].id,"active effect IDs remain stable in a recipient view");
}

void fourPlayerViews(){
    Simulation simulation;simulation.reset({0,73,false,1,MatchLength::Standard,4});
    check(simulation.playerCount()==4&&simulation.winner()==-1&&simulation.eliminatedMask()==0,"four-player authority starts with four active seats");
    const std::array<Vec2,4> positions{{{2250,2300},{2350,2300},{2450,2300},{2550,2300}}};
    std::array<Id,4> scouts{};
    for(int team=0;team<4;++team)scouts[team]=simulation.debugSpawn(Kind::Scout,team,positions[team]);
    const Id victim=simulation.debugSpawn(Kind::Worker,0,{2500,2450});
    const Id shooter=simulation.debugSpawn(Kind::Mortar,3,{2000,2450});
    check(simulation.command({CommandType::Attack,3,{shooter},{},victim,Kind::Worker,0}).accepted,"four-player authority accepts an attack against a distinct opponent");
    advance(simulation,Simulation::Step);

    std::array<net::ViewMemory,Simulation::MaxPlayers> memories;
    for(int viewer=0;viewer<4;++viewer) {
        const auto view=net::snapshotFor(simulation,viewer,&memories[viewer]);
        check(view.config.playerCount==4&&view.winner==-1&&view.eliminatedMask==0,"each four-player view carries active match metadata");
        check(view.player.ore==simulation.players()[viewer].ore,"each four-player view exposes only its recipient economy");
        for(int team=0;team<4;++team) {
            const Entity* scout=snapshotAt(view,positions[team],Kind::Scout);
            const int normalized=(team-viewer+4)%4;
            check(scout&&scout->team==normalized,"four-player views retain distinct cyclic opponent identities");
            check(same(scout->pos,positions[team]),"four-player views preserve authoritative world coordinates");
            check(scout->id!=scouts[team],"four-player views use opaque entity handles");
            if(team!=viewer)check(scout->order==Order::Idle&&scout->queue.empty()&&scout->target==0,"every opponent keeps private orders, queues, and targets hidden");
        }
        const auto repeated=net::snapshotFor(simulation,viewer,&memories[viewer]);
        check(snapshotAt(repeated,positions[viewer],Kind::Scout)->id==snapshotAt(view,positions[viewer],Kind::Scout)->id,"each recipient keeps stable opaque handles");
        const auto bytes=net::encodeSnapshot(view);net::Snapshot decoded;std::string error;
        check(!bytes.empty()&&net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error)&&decoded.config.playerCount==4&&decoded.entities.size()==view.entities.size(),"each four-player snapshot round trips through the current protocol");
        Simulation replica;
        check(replica.applySnapshot(decoded,&error)&&replica.isReplica()&&replica.playerCount()==4,"four-player snapshots import into a normalized replica");
        const Entity* own=snapshotAt(view,positions[viewer],Kind::Scout);
        Command move{CommandType::Move,0,{own->id},{2700,2700},0,Kind::Worker,0};
        check(net::translateCommand(simulation,viewer,memories[viewer],move,error)&&move.team==viewer&&move.units==std::vector<Id>{scouts[viewer]},"opaque commands resolve to the correct authoritative seat");
        const Entity* foreign=snapshotAt(view,positions[(viewer+1)%4],Kind::Scout);
        Command stolen{CommandType::Move,0,{foreign->id},{2700,2700},0,Kind::Worker,0};
        check(!net::translateCommand(simulation,viewer,memories[viewer],stolen,error),"opaque handles never grant control of another FFA seat");
        const int expectedTeam=(3-viewer+4)%4;
        check(!view.effects.empty()&&std::any_of(view.effects.begin(),view.effects.end(),[&](const Effect& effect){return effect.team==expectedTeam;}),"visible effects retain cyclic source-team identity");
    }

    simulation.forfeit(1);
    check(simulation.winner()==-1&&simulation.eliminated(1),"one FFA forfeit eliminates only that seat and play continues");
    const auto afterOne=net::snapshotFor(simulation,2,&memories[2]);
    check(afterOne.winner==-1&&afterOne.eliminatedMask==(1u<<3),"elimination mask is cyclically normalized for its recipient");
    const auto eliminatedBytes=net::encodeSnapshot(afterOne);net::Snapshot eliminatedDecoded;std::string error;
    check(net::decodeSnapshot(eliminatedBytes.data(),eliminatedBytes.size(),eliminatedDecoded,error)&&eliminatedDecoded.eliminatedMask==afterOne.eliminatedMask,"elimination state survives the snapshot wire");
    simulation.forfeit(0);simulation.forfeit(3);
    check(simulation.winner()==2,"FFA emits a winner only after one team remains");
    const auto finalView=net::snapshotFor(simulation,3,&memories[3]);
    check(finalView.winner==3&&finalView.eliminatedMask==0x07,"winner and eliminated teams share the recipient's cyclic identity space");
}

void codecValidation(){
    check(static_cast<int>(Order::Defend)==static_cast<int>(Order::Construct)+1,"Defend appends the persisted order enum");
    check(static_cast<int>(CommandType::Defend)==static_cast<int>(CommandType::ResumeConstruction)+1,"Defend appends the command wire enum");
    check(static_cast<int>(CommandType::AutoBuild)==static_cast<int>(CommandType::Defend)+1&&static_cast<int>(CommandType::AutoRally)==static_cast<int>(CommandType::AutoBuild)+3,"automation commands append without renumbering existing wire values");
    check(static_cast<int>(Order::Patrol)==8&&static_cast<int>(Order::Escort)==9,"sustained orders append without renumbering persisted values");
    check(static_cast<int>(CommandType::Patrol)==19&&static_cast<int>(CommandType::Escort)==20,"sustained commands append without renumbering command values");
    check(net::ProtocolVersion==12,"authored terrain uses protocol version twelve");
    Simulation simulation;simulation.reset({0,88,false,1});
    const Id hq=first(simulation,0,Kind::Headquarters);
    check(simulation.command({CommandType::Train,0,{hq},{},0,Kind::Worker,0}).accepted&&simulation.command({CommandType::Train,0,{hq},{},0,Kind::Worker,0}).accepted,"snapshot fixture queues two stable jobs");
    net::ViewMemory memory;const auto source=net::snapshotFor(simulation,0,&memory);
    const auto bytes=net::encodeSnapshot(source);check(!bytes.empty()&&bytes.size()<net::MaxMessageBytes,"ordinary filtered snapshot encodes compactly");
    net::Snapshot decoded;std::string error;check(net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error)&&decoded.tick==source.tick&&decoded.entities.size()==source.entities.size()&&decoded.fog==source.fog&&decoded.config.matchLength==MatchLength::Standard&&decoded.config.playerCount==2&&decoded.config.mapRevision==source.config.mapRevision&&decoded.eliminatedMask==0,"ordinary two-player Standard snapshot preserves its terrain revision");
    auto feedback=source;feedback.workerPlanNotice="Queued Kiln skipped: the site became blocked.";feedback.workerPlanNoticeSerial=1;
    const auto feedbackBytes=net::encodeSnapshot(feedback);net::Snapshot decodedFeedback;
    check(!feedbackBytes.empty()&&net::decodeSnapshot(feedbackBytes.data(),feedbackBytes.size(),decodedFeedback,error)&&
          decodedFeedback.workerPlanNotice==feedback.workerPlanNotice&&decodedFeedback.workerPlanNoticeSerial==1,
          "bounded worker-plan failure feedback round trips in the snapshot tail");
    const std::size_t noticeSerialOffset=feedbackBytes.size()-feedback.workerPlanNotice.size()-10;
    auto malformedNotice=feedbackBytes;for(int byte=0;byte<8;++byte)malformedNotice.at(noticeSerialOffset+byte)=0;
    rejectsSnapshot(malformedNotice,"nonempty worker-plan feedback requires a nonzero serial");
    malformedNotice=feedbackBytes;putU16(malformedNotice,noticeSerialOffset+8,241);
    rejectsSnapshot(malformedNotice,"decoder rejects an oversized worker-plan feedback length");
    feedback.workerPlanNotice.assign(241,'x');
    check(net::encodeSnapshot(feedback).empty(),"worker-plan feedback rejects an oversized payload");
    feedback.workerPlanNotice="bad\nfeedback";
    check(net::encodeSnapshot(feedback).empty(),"worker-plan feedback rejects control characters");
    const Entity* sourceHQ=snapshotEntity(source,0,Kind::Headquarters);const Entity* decodedHQ=snapshotEntity(decoded,0,Kind::Headquarters);
    check(sourceHQ&&decodedHQ&&decodedHQ->queue.size()==2&&decodedHQ->queue[0].id==sourceHQ->queue[0].id&&decodedHQ->queue[1].id==sourceHQ->queue[1].id&&decodedHQ->nextQueueId==sourceHQ->nextQueueId,"queue job IDs and next producer sequence round trip in snapshots");
    auto oldProtocol=bytes;putU32(oldProtocol,4,11);rejectsSnapshot(oldProtocol,"protocol-eleven snapshot is rejected before reading terrain revisions");
    auto malformed=bytes;malformed.pop_back();rejectsSnapshot(malformed,"truncated snapshot is rejected");
    malformed=bytes;malformed.push_back(0);rejectsSnapshot(malformed,"snapshot trailing bytes are rejected");
    malformed=bytes;putU32(malformed,17,0x7fc00000U);rejectsSnapshot(malformed,"snapshot NaN is rejected");
    malformed=bytes;malformed.at(21)=static_cast<std::uint8_t>(MatchLength::Count);rejectsSnapshot(malformed,"invalid match-length preset is rejected");
    malformed=bytes;malformed.at(22)=3;rejectsSnapshot(malformed,"invalid snapshot player count is rejected");
    malformed=bytes;malformed.at(44)=4;rejectsSnapshot(malformed,"elimination bits outside the configured seats are rejected");
    malformed=bytes;malformed.at(44)=1;rejectsSnapshot(malformed,"ongoing snapshot cannot have fewer than two survivors");
    malformed=bytes;malformed.at(69)=2;rejectsSnapshot(malformed,"invalid army rally presence flag is rejected");
    const auto firstOffset=firstEntityOffset(bytes);malformed=bytes;malformed.at(firstOffset+4)=255;rejectsSnapshot(malformed,"snapshot enum is rejected");
    malformed=bytes;malformed.at(firstOffset+73)=2;rejectsSnapshot(malformed,"invalid producer rally override flag is rejected");
    check(bytes.at(firstOffset+74)==2,"snapshot byte fixture begins with the queued Anchor");
    malformed=bytes;putU32(malformed,firstOffset+89,0);rejectsSnapshot(malformed,"zero queue job ID is rejected");
    malformed=bytes;putU32(malformed,firstOffset+107,getU32(malformed,firstOffset+89));rejectsSnapshot(malformed,"duplicate queue job IDs are rejected");
    malformed=bytes;putU32(malformed,nextQueueIdOffset(malformed,firstOffset),getU32(malformed,firstOffset+107));rejectsSnapshot(malformed,"producer sequence must remain above every queued job ID");
    const auto secondOffset=nextEntityOffset(bytes,firstOffset);malformed=bytes;putU32(malformed,secondOffset,getU32(malformed,firstOffset));rejectsSnapshot(malformed,"duplicate snapshot entity IDs are rejected");
    malformed=bytes;malformed.at(nextQueueIdOffset(bytes,firstOffset)-1)=2;rejectsSnapshot(malformed,"invalid route failure flag is rejected");
    malformed=bytes;const std::size_t entityCountOffset=firstOffset-4;putU32(malformed,entityCountOffset,10001);rejectsSnapshot(malformed,"oversized snapshot entity count is rejected");
    std::vector<std::uint8_t> oversized(net::MaxMessageBytes+1);rejectsSnapshot(oversized,"oversized snapshot frame is rejected");
    auto draw=source;draw.winner=-2;draw.eliminatedMask=3;const auto drawBytes=net::encodeSnapshot(draw);net::Snapshot decodedDraw;
    check(!drawBytes.empty()&&net::decodeSnapshot(drawBytes.data(),drawBytes.size(),decodedDraw,error)&&decodedDraw.winner==-2&&decodedDraw.eliminatedMask==3,"zero-survivor draw state round trips as signed winner minus two");

    Simulation shortMatch;shortMatch.reset({0,89,false,1,MatchLength::Short});
    const auto shortBytes=net::encodeSnapshot(net::snapshotFor(shortMatch,0));net::Snapshot shortDecoded;
    check(!shortBytes.empty()&&net::decodeSnapshot(shortBytes.data(),shortBytes.size(),shortDecoded,error)&&shortDecoded.config.matchLength==MatchLength::Short,"Short preset snapshot uses its compact world bounds");
    Simulation longMatch;longMatch.reset({0,90,false,1,MatchLength::Long});
    longMatch.debugSpawn(Kind::Scout,0,{5500,5500});auto longSnapshot=net::snapshotFor(longMatch,0);const auto longBytes=net::encodeSnapshot(longSnapshot);net::Snapshot longDecoded;
    check(!longBytes.empty()&&net::decodeSnapshot(longBytes.data(),longBytes.size(),longDecoded,error)&&longDecoded.config.matchLength==MatchLength::Long&&snapshotAt(longDecoded,{5500,5500},Kind::Scout),"Long preset snapshot accepts positions beyond the Standard battlefield");
    auto wrongBounds=longSnapshot;wrongBounds.config.matchLength=MatchLength::Short;
    check(net::encodeSnapshot(wrongBounds).empty(),"Short preset rejects snapshot geometry outside its configured battlefield");

    const auto& own=source.entities;std::vector<Id> handles;for(const auto& entity:own)if(entity.team==0&&entity.kind==Kind::Worker&&handles.size()<2)handles.push_back(entity.id);check(handles.size()==2,"command codec fixture has two workers");
    Command command{CommandType::Move,1,handles,{900,900},0,Kind::Worker,0};const auto commandBytes=net::encodeCommand(command,19);check(!commandBytes.empty(),"ordinary multi-unit command encodes");
    Command decodedCommand;std::uint32_t sequence=0;check(net::decodeCommand(commandBytes.data(),commandBytes.size(),decodedCommand,sequence,error)&&sequence==19&&decodedCommand.units==handles&&decodedCommand.team==0,"ordinary command round trips without client team");
    Command longPoint{CommandType::Move,0,handles,{5900,5900},0,Kind::Worker,0};const auto longPointBytes=net::encodeCommand(longPoint,18);Command decodedLongPoint;
    check(!longPointBytes.empty()&&net::decodeCommand(longPointBytes.data(),longPointBytes.size(),decodedLongPoint,sequence,error)&&same(decodedLongPoint.point,{5900,5900}),"command codec accepts the largest supported battlefield before authoritative match validation");
    const Id shortWorker=first(shortMatch,0,Kind::Worker);const auto shortBefore=shortMatch.stateHash();
    check(!shortMatch.command({CommandType::Move,0,{shortWorker},{5900,5900},0,Kind::Worker,0}).accepted&&shortMatch.stateHash()==shortBefore,"Short authority rejects a decoded point outside its active battlefield");
    Command defend{CommandType::Defend,1,handles,{1100,1000},0,Kind::Worker,0};
    defend.spacing=FormationSpacing::Tight;defend.hasArrivalFacing=true;defend.arrivalFacing=1.5f;
    const auto defendBytes=net::encodeCommand(defend,20);check(!defendBytes.empty(),"Defend command accepts formation modifiers");
    Command decodedDefend;sequence=0;check(net::decodeCommand(defendBytes.data(),defendBytes.size(),decodedDefend,sequence,error)&&sequence==20&&decodedDefend.type==CommandType::Defend&&decodedDefend.units==handles&&decodedDefend.point.x==1100&&decodedDefend.point.y==1000&&decodedDefend.spacing==FormationSpacing::Tight&&decodedDefend.hasArrivalFacing&&decodedDefend.arrivalFacing==1.5f,"Defend command round trips exact formation modifiers");
    const Command autoTrain{CommandType::AutoTrain,1,{}, {},0,Kind::Striker,20};const auto autoBytes=net::encodeCommand(autoTrain,21);
    Command decodedAuto;sequence=0;check(!autoBytes.empty()&&net::decodeCommand(autoBytes.data(),autoBytes.size(),decodedAuto,sequence,error)&&sequence==21&&decodedAuto.type==CommandType::AutoTrain&&decodedAuto.units.empty()&&decodedAuto.kind==Kind::Striker&&decodedAuto.queueIndex==20,"empty-selection automatic batch command round trips at its maximum quantity");
    const std::vector<Command> otherAutomatic{
        {CommandType::AutoBuild,0,{}, {1250,1100},0,Kind::Foundry,0},
        {CommandType::AutoResearch,0,{}, {},0,Kind::Worker,2},
        {CommandType::AutoRally,0,{}, {1500,1350},0,Kind::Resource,0},
    };
    for(std::size_t index=0;index<otherAutomatic.size();++index) {
        const auto encodedAutomatic=net::encodeCommand(otherAutomatic[index],static_cast<std::uint32_t>(30+index));Command roundTrip;sequence=0;
        check(!encodedAutomatic.empty()&&net::decodeCommand(encodedAutomatic.data(),encodedAutomatic.size(),roundTrip,sequence,error)&&roundTrip.type==otherAutomatic[index].type&&roundTrip.units.empty()&&roundTrip.target==otherAutomatic[index].target&&roundTrip.kind==otherAutomatic[index].kind&&roundTrip.queueIndex==otherAutomatic[index].queueIndex&&same(roundTrip.point,otherAutomatic[index].point),"automatic build, research, and rally commands preserve their complete wire fields");
    }
    check(net::encodeCommand({CommandType::AutoTrain,0,handles,{},0,Kind::Striker,1},22).empty(),"automatic commands reject client selections");
    check(net::encodeCommand({CommandType::AutoTrain,0,{}, {},0,Kind::Striker,0},22).empty()&&net::encodeCommand({CommandType::AutoTrain,0,{}, {},0,Kind::Striker,21},22).empty(),"automatic training rejects quantities outside one through twenty");
    check(net::encodeCommand({CommandType::AutoBuild,0,{}, {1250,1100},1,Kind::Foundry,0},22).empty()&&net::encodeCommand({CommandType::AutoBuild,0,{}, {1250,1100},0,Kind::Striker,0},22).empty(),"automatic construction rejects pins and non-building kinds");
    check(net::encodeCommand({CommandType::AutoResearch,0,{}, {},0,Kind::Worker,3},22).empty()&&net::encodeCommand({CommandType::AutoRally,0,{}, {},0,Kind::Striker,0},22).empty(),"automatic research and rally reject invalid catalog values");
    check(net::encodeCommand({CommandType::AutoRally,0,{}, {},0,Kind::Resource,1},22).empty()&&net::encodeCommand({CommandType::AutoRally,0,{}, {},sourceHQ->id,Kind::Resource,2},22).empty(),"rally inheritance reset requires a pin and the exact reset mode");
    check(net::encodeCommand({CommandType::Move,0,{}, {900,900},0,Kind::Worker,0},22).empty(),"ordinary commands still require a nonempty selection");
    oldProtocol=defendBytes;putU32(oldProtocol,4,11);rejectsCommand(oldProtocol,"protocol-eleven command is rejected before a match with authored terrain");
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

void tacticalQueueCodecsAndPrivacy(){
    Simulation simulation;simulation.reset({0,73,false,1,MatchLength::Standard,2,0});
    const Id leader0=simulation.debugSpawn(Kind::Striker,0,{1400,1400});
    const Id mender0=simulation.debugSpawn(Kind::Mender,0,{1460,1400});
    const Id leader1=simulation.debugSpawn(Kind::Striker,1,{1560,1400});
    const Id mender1=simulation.debugSpawn(Kind::Mender,1,{1620,1400});
    for(const auto& [id,leader]:std::vector<std::pair<Id,Id>>{{mender0,leader0},{mender1,leader1}}) {
        auto& entity=*const_cast<Entity*>(simulation.find(id));
        entity.order=Order::AttackMove;entity.goal={2200,1800};entity.supportTarget=leader;
        entity.hasArrivalFacing=true;entity.arrivalFacing=0.5f;
        entity.futureOrders={{Order::Move,{2300,1900},0,true,-0.5f},{Order::AttackMove,{2400,2000},leader,false,0.0f}};
    }
    net::ViewMemory memories[2];
    for(int viewer=0;viewer<2;++viewer) {
        const Id ownId=viewer?mender1:mender0,leaderId=viewer?leader1:leader0,otherId=viewer?mender0:mender1;
        const auto view=net::snapshotFor(simulation,viewer,&memories[viewer]);
        const auto* own=snapshotAt(view,simulation.find(ownId)->pos,Kind::Mender);
        const auto* leader=snapshotAt(view,simulation.find(leaderId)->pos,Kind::Striker);
        const auto* enemy=snapshotAt(view,simulation.find(otherId)->pos,Kind::Mender);
        check(own&&leader&&enemy&&own->team==0&&enemy->team==1,"both recipient views include nearby tactical fixtures");
        check(own->supportTarget==leader->id&&leader->id!=leaderId&&own->futureOrders.size()==2&&
              own->futureOrders.back().supportTarget==leader->id&&own->hasArrivalFacing&&own->arrivalFacing==0.5f&&
              own->futureOrders.front().hasArrivalFacing&&own->futureOrders.front().arrivalFacing==-0.5f,
              "current and future support and facing use stable private owned state");
        check(enemy->supportTarget==0&&enemy->futureOrders.empty()&&!enemy->hasArrivalFacing&&
              enemy->arrivalFacing==0.0f&&!std::signbit(enemy->arrivalFacing),
              "enemy tactical plan and arrival facing are private canonical defaults");
        auto bytes=net::encodeSnapshot(view);net::Snapshot decoded;std::string error;
        check(!bytes.empty()&&net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error),"tactical snapshot round trips");
        const auto* restored=snapshotAt(decoded,own->pos,Kind::Mender);
        check(restored&&restored->supportTarget==leader->id&&restored->futureOrders.size()==2&&
              restored->futureOrders[0].order==Order::Move&&same(restored->futureOrders[0].point,{2300,1900})&&
              restored->futureOrders[1].supportTarget==leader->id&&restored->hasArrivalFacing&&
              restored->arrivalFacing==0.5f&&restored->futureOrders[0].hasArrivalFacing&&
              restored->futureOrders[0].buildingKind==Kind::Worker&&restored->futureOrders[1].buildingKind==Kind::Worker&&
              restored->futureOrders[0].arrivalFacing==-0.5f,"wire codec preserves the complete owned tactical plan");
        Simulation replica;check(replica.applySnapshot(decoded,&error)&&replica.find(own->id)->futureOrders.size()==2,
                                 "replica import retains its owned future steps");
        std::size_t offset=firstEntityOffset(bytes);
        while(getU32(bytes,offset)!=own->id)offset=nextEntityOffset(bytes,offset);
        const auto tactical=tacticalStateOffset(bytes,offset);
        std::size_t enemyOffset=firstEntityOffset(bytes);
        while(getU32(bytes,enemyOffset)!=enemy->id)enemyOffset=nextEntityOffset(bytes,enemyOffset);
        const auto enemyTactical=tacticalStateOffset(bytes,enemyOffset);
        auto hostileFacing=bytes;hostileFacing.at(enemyTactical+4)=1;putU32(hostileFacing,enemyTactical+5,0x3f000000u);
        rejectsSnapshot(hostileFacing,"decoder rejects hostile private current arrival facing");
        hostileFacing=bytes;putU32(hostileFacing,enemyTactical+5,0x3f000000u);
        rejectsSnapshot(hostileFacing,"decoder rejects hostile noncanonical default arrival angle");
        auto hostile=bytes;hostile.at(offset+5)=1;
        rejectsSnapshot(hostile,"decoder rejects a hostile opponent tactical payload");
        hostile=bytes;hostile.at(offset+58)=static_cast<std::uint8_t>(Order::Idle);
        rejectsSnapshot(hostile,"decoder rejects an idle unit with a future tail");
        hostile=bytes;hostile.at(tactical+9)=17;
        rejectsSnapshot(hostile,"decoder rejects oversized tactical count before allocation");
        hostile=bytes;hostile.at(tactical+10)=static_cast<std::uint8_t>(Order::Gather);
        rejectsSnapshot(hostile,"decoder rejects a non-tactical future step");
        hostile=bytes;putU32(hostile,tactical+11,0x7fc00000u);
        rejectsSnapshot(hostile,"decoder rejects a nonfinite future waypoint");
        hostile=bytes;putU32(hostile,tactical,enemy->id);
        rejectsSnapshot(hostile,"decoder rejects support pointing to an opponent");
        hostile=bytes;putU32(hostile,tactical+19,leader->id);
        rejectsSnapshot(hostile,"Move steps cannot carry an attack-move support relation");
        hostile=bytes;hostile.at(tactical+4)=2;
        rejectsSnapshot(hostile,"decoder rejects a malformed current arrival-facing flag");
        hostile=bytes;putU32(hostile,tactical+5,0x80000000u);
        rejectsSnapshot(hostile,"decoder rejects negative zero in stored current arrival facing");
        hostile=bytes;hostile.at(tactical+23)=2;
        rejectsSnapshot(hostile,"decoder rejects a malformed queued arrival-facing flag");
        hostile=bytes;putU32(hostile,tactical+24,0x40490fdbu);
        rejectsSnapshot(hostile,"decoder rejects a queued arrival facing at positive pi");
        hostile=bytes;hostile.at(tactical+28)=static_cast<std::uint8_t>(Kind::Foundry);
        rejectsSnapshot(hostile,"decoder rejects a building kind on an existing movement step");
        const auto repeated=net::snapshotFor(simulation,viewer,&memories[viewer]);
        check(snapshotAt(repeated,own->pos,Kind::Mender)->futureOrders.back().supportTarget==leader->id,
              "future support handles remain stable across repeated snapshots");
    }
    Command command{CommandType::AttackMove,0,{mender0,leader0},{2400,2000}};
    command.queueMode=CommandQueueMode::Append;command.spacing=FormationSpacing::Wide;
    command.hasArrivalFacing=true;command.arrivalFacing=-0.75f;
    auto bytes=net::encodeCommand(command,91);Command decoded;std::uint32_t sequence=0;std::string error;
    check(bytes.size()==39+command.units.size()*4&&net::decodeCommand(bytes.data(),bytes.size(),decoded,sequence,error)&&
          sequence==91&&decoded.queueMode==CommandQueueMode::Append&&decoded.units==command.units&&
          decoded.spacing==FormationSpacing::Wide&&decoded.hasArrivalFacing&&decoded.arrivalFacing==-0.75f,
          "append intent preserves exact formation spacing and facing");
    auto malformed=bytes;malformed.at(malformed.size()-7)=2;rejectsCommand(malformed,"unknown queue mode is rejected");
    malformed=bytes;malformed.at(malformed.size()-6)=3;rejectsCommand(malformed,"unknown formation spacing is rejected");
    malformed=bytes;malformed.at(malformed.size()-5)=2;rejectsCommand(malformed,"malformed arrival-facing flag is rejected");
    malformed=bytes;putU32(malformed,malformed.size()-4,0x7fc00000u);rejectsCommand(malformed,"nonfinite arrival facing is rejected");
    malformed=bytes;putU32(malformed,malformed.size()-4,0x40490fdbu);rejectsCommand(malformed,"arrival facing at positive pi is rejected");
    malformed=bytes;malformed.at(malformed.size()-5)=0;putU32(malformed,malformed.size()-4,0x3f800000u);rejectsCommand(malformed,"automatic facing rejects a nonzero angle");
    malformed=bytes;malformed.resize(malformed.size()-7);rejectsCommand(malformed,"missing command modifiers are rejected");
    malformed=bytes;putU32(malformed,4,10);rejectsCommand(malformed,"protocol-ten command is rejected");
    malformed=bytes;malformed.at(12)=static_cast<std::uint8_t>(CommandType::Gather);
    rejectsCommand(malformed,"malformed append Gather is rejected");
    Command zeroFacing=command;zeroFacing.queueMode=CommandQueueMode::Replace;
    zeroFacing.spacing=FormationSpacing::Standard;zeroFacing.hasArrivalFacing=true;
    zeroFacing.arrivalFacing=-0.0f;
    const auto zeroBytes=net::encodeCommand(zeroFacing,90);Command canonicalZero;std::uint32_t zeroSequence=0;
    check(!zeroBytes.empty()&&net::decodeCommand(zeroBytes.data(),zeroBytes.size(),canonicalZero,zeroSequence,error)&&
          canonicalZero.arrivalFacing==0.0f&&!std::signbit(canonicalZero.arrivalFacing),
          "submitted negative zero is normalized to canonical positive zero");
    command.type=CommandType::ClearOrders;command.queueMode=CommandQueueMode::Replace;
    command.spacing=FormationSpacing::Standard;command.hasArrivalFacing=false;command.arrivalFacing=0.0f;
    bytes=net::encodeCommand(command,92);
    check(!bytes.empty()&&net::decodeCommand(bytes.data(),bytes.size(),decoded,sequence,error)&&decoded.type==CommandType::ClearOrders,
          "clear-future-only command round trips independently of production cancellation");
    command.queueMode=CommandQueueMode::Append;
    check(net::encodeCommand(command,93).empty(),"ClearOrders cannot itself be appended");
    command.queueMode=CommandQueueMode::Replace;command.target=1;
    check(net::encodeCommand(command,94).empty(),"ClearOrders rejects an unrelated target");
    command.target=0;command.queueIndex=1;
    check(net::encodeCommand(command,95).empty(),"ClearOrders rejects a production queue index");
}

void sustainedOrderCodecsPrivacyAndProjection(){
    Simulation simulation;simulation.reset({0,0x9A71u,false,1,MatchLength::Standard,2,0});
    const Id patrol=simulation.debugSpawn(Kind::Striker,0,{1450,1450});
    const Id leader=simulation.debugSpawn(Kind::Scout,0,{1550,1450});
    const Id escort=simulation.debugSpawn(Kind::Mender,0,{1450,1540});
    const Id hostile=simulation.debugSpawn(Kind::Worker,1,{1750,1450});
    const Id enemyPatrol=simulation.debugSpawn(Kind::Striker,1,{1700,1550});
    auto& patrolEntity=*const_cast<Entity*>(simulation.find(patrol));
    patrolEntity.order=Order::Patrol;patrolEntity.goal={2300,1450};
    patrolEntity.target=0;patrolEntity.supportTarget=0;
    patrolEntity.sustained.patrolOrigin={1450,1450};
    patrolEntity.sustained.patrolDestination={2300,1450};
    patrolEntity.sustained.patrolTowardDestination=true;
    patrolEntity.sustained.pursuitTarget=hostile;
    patrolEntity.sustained.pursuitAnchor={1650,1450};
    patrolEntity.sustained.phase=SustainedOrderPhase::Pursuit;
    auto& escortEntity=*const_cast<Entity*>(simulation.find(escort));
    escortEntity.order=Order::Escort;escortEntity.goal={1454,1546};
    escortEntity.target=0;escortEntity.supportTarget=0;
    escortEntity.sustained.escortTarget=leader;escortEntity.sustained.escortOffset={-96,96};
    auto& enemyEntity=*const_cast<Entity*>(simulation.find(enemyPatrol));
    enemyEntity.order=Order::Patrol;enemyEntity.goal={2200,1550};
    enemyEntity.sustained.patrolOrigin={1700,1550};enemyEntity.sustained.patrolDestination={2200,1550};
    enemyEntity.sustained.patrolTowardDestination=true;

    net::ViewMemory memory;
    auto view=net::snapshotFor(simulation,0,&memory);
    const auto* ownPatrol=snapshotAt(view,{1450,1450},Kind::Striker);
    const auto* ownLeader=snapshotAt(view,{1550,1450},Kind::Scout);
    const auto* ownEscort=snapshotAt(view,{1450,1540},Kind::Mender);
    const auto* visibleHostile=snapshotAt(view,{1750,1450},Kind::Worker);
    const auto* hiddenIntent=snapshotAt(view,{1700,1550},Kind::Striker);
    check(ownPatrol&&ownLeader&&ownEscort&&visibleHostile&&hiddenIntent,
          "sustained snapshot fixture includes owned and visible hostile actors");
    check(ownPatrol->order==Order::Patrol&&ownPatrol->sustained.phase==SustainedOrderPhase::Pursuit&&
          ownPatrol->sustained.pursuitTarget==visibleHostile->id&&
          same(ownPatrol->sustained.pursuitAnchor,{1650,1450}),
          "owned Patrol exposes immutable endpoints and an opaque visible pursuit reference");
    check(ownEscort->order==Order::Escort&&ownEscort->sustained.escortTarget==ownLeader->id&&
          ownLeader->id!=leader&&same(ownEscort->sustained.escortOffset,{-96,96}),
          "owned Escort exposes its stable slot and opaque owned leader");
    check(hiddenIntent->team==1&&hiddenIntent->order==Order::Idle&&
          hiddenIntent->sustained.escortTarget==0&&hiddenIntent->sustained.pursuitTarget==0&&
          same(hiddenIntent->sustained.patrolOrigin,{})&&same(hiddenIntent->sustained.patrolDestination,{}),
          "visible enemy sustained intent is completely private");

    auto bytes=net::encodeSnapshot(view);net::Snapshot decoded;std::string error;
    check(!bytes.empty()&&net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error),
          "current sustained snapshot round trips");
    const auto* decodedPatrol=snapshotAt(decoded,{1450,1450},Kind::Striker);
    const auto* decodedEscort=snapshotAt(decoded,{1450,1540},Kind::Mender);
    check(decodedPatrol&&decodedPatrol->sustained.phase==SustainedOrderPhase::Pursuit&&
          same(decodedPatrol->sustained.patrolOrigin,{1450,1450})&&
          same(decodedPatrol->sustained.patrolDestination,{2300,1450})&&
          decodedEscort&&decodedEscort->sustained.escortTarget==ownLeader->id,
          "wire codec retains complete owned Patrol and Escort state");

    std::size_t patrolOffset=firstEntityOffset(bytes);
    while(getU32(bytes,patrolOffset)!=ownPatrol->id)patrolOffset=nextEntityOffset(bytes,patrolOffset);
    const auto patrolState=tacticalStateOffset(bytes,patrolOffset);
    auto malformed=bytes;malformed.at(patrolState+25)=2;
    rejectsSnapshot(malformed,"Patrol direction rejects non-boolean wire values");
    malformed=bytes;malformed.at(patrolState+38)=9;
    rejectsSnapshot(malformed,"Patrol rejects an unknown sustained phase");
    malformed=bytes;putU32(malformed,patrolState+26,0xfffffffeu);
    rejectsSnapshot(malformed,"Patrol rejects an unknown pursuit reference");
    std::size_t escortOffset=firstEntityOffset(bytes);
    while(getU32(bytes,escortOffset)!=ownEscort->id)escortOffset=nextEntityOffset(bytes,escortOffset);
    const auto escortState=tacticalStateOffset(bytes,escortOffset);
    malformed=bytes;putU32(malformed,escortState+9,ownEscort->id);
    rejectsSnapshot(malformed,"Escort rejects a self target");
    malformed=bytes;putU32(malformed,escortState+13,0x7fc00000u);
    rejectsSnapshot(malformed,"Escort rejects a nonfinite slot");
    malformed=bytes;putU32(malformed,escortState+13,0);putU32(malformed,escortState+17,0);
    rejectsSnapshot(malformed,"Escort rejects a zero slot inside the leader footprint");
    malformed=bytes;putU32(malformed,escortOffset+14,0);putU32(malformed,escortOffset+18,0);
    rejectsSnapshot(malformed,"Escort rejects a forged goal that does not match its stable slot");

    auto oversizedSlot=view;
    for(auto& entity:oversizedSlot.entities)if(entity.id==ownEscort->id)entity.sustained.escortOffset={Simulation::MaxEscortOffset+1,0};
    check(net::encodeSnapshot(oversizedSlot).empty(),"Escort rejects a slot outside the protocol bound");

    auto cyclic=view;
    Entity* cyclicLeader=nullptr;Entity* cyclicEscort=nullptr;
    for(auto& entity:cyclic.entities) {
        if(entity.id==ownLeader->id)cyclicLeader=&entity;
        if(entity.id==ownEscort->id)cyclicEscort=&entity;
    }
    check(cyclicLeader&&cyclicEscort,"cycle mutation finds both opaque owned actors");
    cyclicLeader->order=Order::Escort;cyclicLeader->goal={1546,1444};
    cyclicLeader->sustained.escortTarget=cyclicEscort->id;cyclicLeader->sustained.escortOffset={96,-96};
    check(net::encodeSnapshot(cyclic).empty(),"direct Escort cycle is rejected before encoding");

    auto overlapping=view;
    Entity* overlappingPatrol=nullptr;
    for(auto& entity:overlapping.entities)if(entity.id==ownPatrol->id)overlappingPatrol=&entity;
    check(overlappingPatrol,"overlap mutation finds a second owned follower");
    overlappingPatrol->order=Order::Escort;overlappingPatrol->goal=ownEscort->goal;overlappingPatrol->sustained={};
    overlappingPatrol->sustained.escortTarget=ownLeader->id;
    overlappingPatrol->sustained.escortOffset=ownEscort->sustained.escortOffset;
    check(net::encodeSnapshot(overlapping).empty(),"followers sharing a leader cannot overlap at one valid slot");

    auto indirect=view;
    Entity* indirectPatrol=nullptr;Entity* indirectLeader=nullptr;Entity* indirectEscort=nullptr;
    for(auto& entity:indirect.entities) {
        if(entity.id==ownPatrol->id)indirectPatrol=&entity;
        if(entity.id==ownLeader->id)indirectLeader=&entity;
        if(entity.id==ownEscort->id)indirectEscort=&entity;
    }
    check(indirectPatrol&&indirectLeader&&indirectEscort,"indirect cycle mutation finds all owned actors");
    indirectPatrol->order=Order::Escort;indirectPatrol->goal={1454,1546};indirectPatrol->sustained={};
    indirectPatrol->sustained.escortTarget=indirectLeader->id;indirectPatrol->sustained.escortOffset={-96,96};
    indirectLeader->order=Order::Escort;indirectLeader->goal={1546,1444};indirectLeader->sustained={};
    indirectLeader->sustained.escortTarget=indirectEscort->id;indirectLeader->sustained.escortOffset={96,-96};
    indirectEscort->goal={1546,1546};indirectEscort->sustained.escortTarget=indirectPatrol->id;
    indirectEscort->sustained.escortOffset={96,96};
    check(net::encodeSnapshot(indirect).empty(),"indirect Escort cycle is rejected before encoding");

    auto inactive=view;
    for(auto& entity:inactive.entities)if(entity.id==ownLeader->id)entity.sustained.escortOffset={1,0};
    check(net::encodeSnapshot(inactive).empty(),"inactive units cannot carry hidden sustained state");

    edit(simulation,hostile)->pos={3900,3900};
    auto fogged=net::snapshotFor(simulation,0,&memory);
    const auto* returning=snapshotAt(fogged,{1450,1450},Kind::Striker);
    check(returning&&returning->sustained.phase==SustainedOrderPhase::Return&&
          returning->sustained.pursuitTarget==0&&same(returning->sustained.pursuitAnchor,{1650,1450})&&
          !net::encodeSnapshot(fogged).empty(),
          "fogged pursuit projects Return with its owned anchor and no hostile identity leak");

    Command patrolCommand{CommandType::Patrol,0,{ownPatrol->id},{2500,1800},0,Kind::Worker,0};
    auto commandBytes=net::encodeCommand(patrolCommand,101);Command roundTrip;std::uint32_t sequence=0;
    check(!commandBytes.empty()&&net::decodeCommand(commandBytes.data(),commandBytes.size(),roundTrip,sequence,error)&&
          sequence==101&&roundTrip.type==CommandType::Patrol&&same(roundTrip.point,{2500,1800}),
          "Patrol command reuses the versioned command shape");
    Command escortCommand{CommandType::Escort,0,{ownEscort->id},{},ownLeader->id,Kind::Worker,0};
    commandBytes=net::encodeCommand(escortCommand,102);
    check(!commandBytes.empty()&&net::decodeCommand(commandBytes.data(),commandBytes.size(),roundTrip,sequence,error)&&
          sequence==102&&roundTrip.type==CommandType::Escort&&roundTrip.target==ownLeader->id,
          "Escort command carries its opaque owned target in the unchanged command shape");
    Command translated=escortCommand;
    check(net::translateCommand(simulation,0,memory,translated,error)&&translated.target==leader&&
          translated.units==std::vector<Id>{escort},"server translates Escort recipient and leader handles");
    auto foreignEscort=escortCommand;foreignEscort.target=visibleHostile->id;
    check(!net::translateCommand(simulation,0,memory,foreignEscort,error),
          "server rejects a visible foreign Escort target");
    patrolCommand.queueMode=CommandQueueMode::Append;
    check(net::encodeCommand(patrolCommand,103).empty(),"Patrol cannot be appended");
    patrolCommand.queueMode=CommandQueueMode::Replace;patrolCommand.spacing=FormationSpacing::Wide;
    check(net::encodeCommand(patrolCommand,103).empty(),"Patrol rejects formation spacing modifiers");
    patrolCommand.spacing=FormationSpacing::Standard;patrolCommand.hasArrivalFacing=true;patrolCommand.arrivalFacing=0.25f;
    check(net::encodeCommand(patrolCommand,103).empty(),"Patrol rejects arrival-facing modifiers");
    patrolCommand.hasArrivalFacing=false;patrolCommand.arrivalFacing=0.0f;
    escortCommand.queueMode=CommandQueueMode::Append;
    check(net::encodeCommand(escortCommand,104).empty(),"Escort cannot be appended");
    patrolCommand.queueMode=CommandQueueMode::Replace;patrolCommand.target=ownLeader->id;
    check(net::encodeCommand(patrolCommand,105).empty(),"Patrol rejects a target");
    escortCommand.queueMode=CommandQueueMode::Replace;escortCommand.point={1,0};
    check(net::encodeCommand(escortCommand,106).empty(),"Escort rejects a point payload");
    escortCommand.point={};escortCommand.target=0;
    check(net::encodeCommand(escortCommand,107).empty(),"Escort requires a target");

    Entity patrolCandidate=*simulation.find(patrol);
    patrolCandidate.order=Order::Patrol;patrolCandidate.goal={2600,1450};patrolCandidate.target=0;
    patrolCandidate.supportTarget=0;patrolCandidate.futureOrders.clear();patrolCandidate.sustained={};
    patrolCandidate.sustained.patrolOrigin=patrolCandidate.pos;
    patrolCandidate.sustained.patrolDestination=patrolCandidate.goal;
    patrolCandidate.sustained.patrolTowardDestination=true;
    const auto hash=simulation.stateHash();const auto recording=simulation.recording().size();
    check(net::sustainedPlanFitsSnapshot(simulation,0,{patrolCandidate}),
          "complete valid Patrol candidate fits the projected owned snapshot");
    Entity escortCandidate=*simulation.find(escort);
    escortCandidate.order=Order::Escort;escortCandidate.goal={1454,1546};escortCandidate.target=0;
    escortCandidate.supportTarget=0;escortCandidate.futureOrders.clear();escortCandidate.sustained={};
    escortCandidate.sustained.escortTarget=leader;escortCandidate.sustained.escortOffset={-96,96};
    check(net::sustainedPlanFitsSnapshot(simulation,0,{escortCandidate}),
          "complete valid Escort candidate fits the projected owned snapshot");
    Entity cycleLeader=*simulation.find(leader);cycleLeader.order=Order::Escort;cycleLeader.goal={1546,1444};
    cycleLeader.sustained={};cycleLeader.sustained.escortTarget=escort;cycleLeader.sustained.escortOffset={96,-96};
    check(!net::sustainedPlanFitsSnapshot(simulation,0,{escortCandidate,cycleLeader}),
          "projected Escort graph rejects a complete cycle");
    check(simulation.stateHash()==hash&&simulation.recording().size()==recording,
          "sustained projection never mutates authority or recording");
}

void tacticalPlanProjectionIsReadOnly(){
    Simulation simulation;simulation.reset({0,19,false,1,MatchLength::Standard,2,0});
    const Id unit=simulation.debugSpawn(Kind::Striker,0,{1500,1500});
    auto& entity=*const_cast<Entity*>(simulation.find(unit));entity.order=Order::Hold;
    const auto hash=simulation.stateHash();const auto recording=simulation.recording().size();
    check(net::orderPlanFitsSnapshot(simulation,0,{{unit,{Order::Move,{2000,1900},0,true,1.25f}}}),"valid appended destination and facing fit its full viewer snapshot");
    check(!net::orderPlanFitsSnapshot(simulation,0,{{unit,{Order::Move,{2000,1900},0,false,-0.0f}}}),"projected stored facing rejects negative zero");
    check(!net::orderPlanFitsSnapshot(simulation,0,{{unit,{Order::Gather,{2000,1900},0}}}),"projection rejects non-tactical order kind");
    check(!net::orderPlanFitsSnapshot(simulation,0,{{unit,{Order::Move,{2000,1900},0}},{unit,{Order::Move,{2100,1900},0}}}),"duplicate projected recipient is rejected");
    check(!net::orderPlanFitsSnapshot(simulation,1,{{unit,{Order::Move,{2000,1900},0}}}),"projection cannot address a foreign unit");
    check(simulation.stateHash()==hash&&simulation.recording().size()==recording&&entity.futureOrders.empty(),
          "successful and rejected projections leave authoritative state and recording unchanged");
    const Id worker=simulation.debugSpawn(Kind::Worker,0,{1650,1500});
    auto& workerEntity=*edit(simulation,worker);workerEntity.order=Order::Hold;
    TacticalOrder build{Order::Construct,{1900,1700},0,false,0.0f,Kind::Foundry};
    TacticalOrder resume{Order::Construct,{1950,1700},0xf001u,false,0.0f,Kind::Worker};
    TacticalOrder gather{Order::Gather,{2000,1700},0xf002u,false,0.0f,Kind::Worker};
    const auto workHash=simulation.stateHash();
    check(net::orderPlanFitsSnapshot(simulation,0,{{worker,build}}),
          "new queued construction reserves a future foundation in the projected snapshot");
    check(net::orderPlanFitsSnapshot(simulation,0,{{worker,resume}})&&
          net::orderPlanFitsSnapshot(simulation,0,{{worker,gather}}),
          "queued resume and gather accept opaque targets that may become stale before activation");
    auto invalid=build;invalid.buildingKind=Kind::Worker;
    check(!net::orderPlanFitsSnapshot(simulation,0,{{worker,invalid}}),
          "new queued construction requires an actual building kind");
    invalid=gather;invalid.supportTarget=0;
    check(!net::orderPlanFitsSnapshot(simulation,0,{{worker,invalid}}),
          "queued gathering requires an opaque ore target");
    invalid=resume;invalid.hasArrivalFacing=true;invalid.arrivalFacing=0.25f;
    check(!net::orderPlanFitsSnapshot(simulation,0,{{worker,invalid}}),
          "queued work rejects formation-facing modifiers");
    check(simulation.stateHash()==workHash&&workerEntity.futureOrders.empty(),
          "queued-work projection remains read-only");

    workerEntity.order=Order::Idle;workerEntity.futureOrders={build};workerEntity.resumeGather=true;
    const auto pendingHash=simulation.stateHash();
    TacticalOrder pendingMove{Order::Move,{2050,1750},0,false,0.0f,Kind::Worker};
    check(net::orderPlanFitsSnapshot(simulation,0,{{worker,pendingMove}}),
          "snapshot admission appends behind an idle budget-waiting worker plan");
    auto pendingSnapshot=net::snapshotFor(simulation,0);auto pendingBytes=net::encodeSnapshot(pendingSnapshot);
    net::Snapshot decodedPending;std::string pendingError;
    const bool pendingDecoded=!pendingBytes.empty()&&
        net::decodeSnapshot(pendingBytes.data(),pendingBytes.size(),decodedPending,pendingError);
    const auto* pendingWorker=pendingDecoded?snapshotAt(decodedPending,{1650,1500},Kind::Worker):nullptr;
    check(pendingWorker&&pendingWorker->order==Order::Idle&&pendingWorker->resumeGather&&
          pendingWorker->futureOrders.size()==1&&pendingWorker->futureOrders.front().order==Order::Construct&&
          simulation.stateHash()==pendingHash,
          "admission and snapshot round trip preserve the pending front and mining-resume state");
    workerEntity.order=Order::Hold;workerEntity.futureOrders.clear();workerEntity.resumeGather=false;

    auto workSnapshot=net::snapshotFor(simulation,0);
    auto projectedWorker=std::find_if(workSnapshot.entities.begin(),workSnapshot.entities.end(),
        [&](const Entity& candidate){return candidate.id==worker;});
    check(projectedWorker!=workSnapshot.entities.end(),"queued-work codec fixture retains its owned Drudge");
    projectedWorker->order=Order::Idle;projectedWorker->futureOrders={build,resume,gather};
    auto workBytes=net::encodeSnapshot(workSnapshot);net::Snapshot decodedWork;std::string workError;
    check(!workBytes.empty()&&net::decodeSnapshot(workBytes.data(),workBytes.size(),decodedWork,workError),
          "current snapshot round trips build, resume, and gather queue steps");
    const auto* decodedWorker=snapshotAt(decodedWork,{1650,1500},Kind::Worker);
    check(decodedWorker&&decodedWorker->order==Order::Idle&&decodedWorker->futureOrders.size()==3&&
          decodedWorker->futureOrders[0].buildingKind==Kind::Foundry&&
          decodedWorker->futureOrders[1].supportTarget==resume.supportTarget&&
          decodedWorker->futureOrders[2].order==Order::Gather,
          "an idle budget-waiting Drudge retains canonical queued-work rows and opaque targets");
    std::size_t workOffset=firstEntityOffset(workBytes);
    while(getU32(workBytes,workOffset)!=worker)workOffset=nextEntityOffset(workBytes,workOffset);
    const auto workFuture=futureCountOffset(workBytes,workOffset);
    auto malformedWork=workBytes;malformedWork.at(workFuture+19)=static_cast<std::uint8_t>(Kind::Worker);
    rejectsSnapshot(malformedWork,"decoder rejects a non-building kind for a new construction step");
    malformedWork=workBytes;malformedWork.at(workFuture+1+19+18)=static_cast<std::uint8_t>(Kind::Foundry);
    rejectsSnapshot(malformedWork,"decoder rejects a building kind on a resume step");
    malformedWork=workBytes;malformedWork.at(workFuture+1+2*19+13)=1;
    rejectsSnapshot(malformedWork,"decoder rejects arrival-facing intent on queued gathering");
    auto invalidWorkSnapshot=workSnapshot;
    auto invalidWorker=std::find_if(invalidWorkSnapshot.entities.begin(),invalidWorkSnapshot.entities.end(),
        [&](const Entity& candidate){return candidate.id==worker;});
    invalidWorker->futureOrders[0].supportTarget=0xf003u;
    check(net::encodeSnapshot(invalidWorkSnapshot).empty(),
          "resume construction uses the Worker sentinel instead of a concrete building kind");
    invalidWorkSnapshot=workSnapshot;
    invalidWorker=std::find_if(invalidWorkSnapshot.entities.begin(),invalidWorkSnapshot.entities.end(),
        [&](const Entity& candidate){return candidate.id==worker;});
    invalidWorker->futureOrders[2].supportTarget=worker;
    check(net::encodeSnapshot(invalidWorkSnapshot).empty(),
          "queued work rejects a forged self target without requiring the real target to remain alive");
    edit(simulation,unit)->futureOrders.assign(Simulation::MaxFutureOrders,{Order::Move,{2200,1900},0});
    check(!net::orderPlanFitsSnapshot(simulation,0,{{unit,{Order::Move,{2000,1900},0}}}),"projected queue cannot exceed per-unit allowance");
    edit(simulation,unit)->futureOrders.clear();
    // A valid collection can still exceed the encoded frame allowance. Build
    // a codec-scale fixture without changing the real one-MiB production cap.
    auto& entities=const_cast<std::vector<Entity>&>(simulation.entities());
    const Entity producer=*simulation.find(first(simulation,0,Kind::Headquarters));
    for(Id id=10000;id<12600;++id) {
        Entity copy=producer;copy.id=id;copy.queue.clear();copy.nextQueueId=21;
        for(Id job=1;job<=20;++job)copy.queue.push_back({Kind::Worker,12,12,60,false,job,0,{}});
        entities.push_back(std::move(copy));
    }
    const auto largeHash=simulation.stateHash();
    check(net::encodeSnapshot(net::snapshotFor(simulation,0)).empty(),"large valid payload reaches the actual wire byte cap");
    check(!net::orderPlanFitsSnapshot(simulation,0,{{unit,{Order::Move,{2000,1900},0}}})&&
          simulation.stateHash()==largeHash&&simulation.recording().size()==recording,
          "oversized projection is rejected without accepting or recording an order");
}

void replicaGuards(){
    Simulation authority;authority.reset({1,99,false,1,MatchLength::Long});authority.debugSpawn(Kind::Scout,1,{5500,5500});net::ViewMemory memory;auto snapshot=net::snapshotFor(authority,1,&memory);
    const auto path=(std::filesystem::temp_directory_path()/"cinderline-network-authority.sav").string();check(authority.save(path),"authoritative fixture saves");
    Simulation replica;std::string error;const auto untouched=replica.stateHash();auto invalid=snapshot;invalid.winner=7;
    check(!replica.applySnapshot(invalid,&error)&&replica.stateHash()==untouched,"invalid snapshot import is atomic");
    check(replica.applySnapshot(snapshot,&error)&&replica.isReplica(),"snapshot atomically enters replica mode");
    check(replica.config().matchLength==MatchLength::Long&&replica.worldSize()==6000&&std::any_of(replica.entities().begin(),replica.entities().end(),[](const Entity& entity){return entity.kind==Kind::Scout&&same(entity.pos,{5500,5500});}),"Long replica retains its preset and expanded world geometry");
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
    const std::vector<std::pair<std::string,void(*)()>> tests{{"terrain revision compatibility",terrainRevisionCompatibility},{"view privacy and handles",viewPrivacyAndHandles},{"resource memory",resourceMemory},{"effect privacy",effectsPrivacy},{"four-player views",fourPlayerViews},{"bounded codecs",codecValidation},{"tactical queue codecs and privacy",tacticalQueueCodecsAndPrivacy},{"sustained order codecs privacy and projection",sustainedOrderCodecsPrivacyAndProjection},{"tactical plan projection",tacticalPlanProjectionIsReadOnly},{"replica guards",replicaGuards}};
    int failed=0;for(const auto& test:tests)try{test.second();std::cout<<"PASS "<<test.first<<'\n';}catch(const std::exception& exception){++failed;std::cerr<<"FAIL "<<test.first<<": "<<exception.what()<<'\n';}
    std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';return failed?1:0;
}
