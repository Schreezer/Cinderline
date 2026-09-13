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
std::size_t firstEntityOffset(const std::vector<std::uint8_t>& bytes){const std::size_t obstacleCountOffset=102;return obstacleCountOffset+2+static_cast<std::size_t>(getU16(bytes,obstacleCountOffset))*16+4;}
std::size_t nextQueueIdOffset(const std::vector<std::uint8_t>& bytes,std::size_t offset){std::size_t cursor=offset+75;const auto queueCount=bytes.at(offset+74);cursor+=static_cast<std::size_t>(queueCount)*18;const auto pathCount=getU16(bytes,cursor);return cursor+2+static_cast<std::size_t>(pathCount)*8+9;}
std::size_t nextEntityOffset(const std::vector<std::uint8_t>& bytes,std::size_t offset){return nextQueueIdOffset(bytes,offset)+4;}
void rejectsSnapshot(const std::vector<std::uint8_t>& bytes,const std::string& message){net::Snapshot decoded;std::string error;check(!net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error)&&!error.empty(),message);}
void rejectsCommand(const std::vector<std::uint8_t>& bytes,const std::string& message){Command decoded;std::uint32_t sequence=99;std::string error;check(!net::decodeCommand(bytes.data(),bytes.size(),decoded,sequence,error)&&!error.empty(),message);}

void viewPrivacyAndHandles(){
    Simulation simulation;simulation.reset({0,42,false,1});
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
        check(!bytes.empty()&&net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error)&&decoded.config.playerCount==4&&decoded.entities.size()==view.entities.size(),"each four-player snapshot round trips through protocol seven");
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
    check(net::ProtocolVersion==7,"four-player snapshots use protocol version seven");
    Simulation simulation;simulation.reset({0,88,false,1});
    const Id hq=first(simulation,0,Kind::Headquarters);
    check(simulation.command({CommandType::Train,0,{hq},{},0,Kind::Worker,0}).accepted&&simulation.command({CommandType::Train,0,{hq},{},0,Kind::Worker,0}).accepted,"snapshot fixture queues two stable jobs");
    net::ViewMemory memory;const auto source=net::snapshotFor(simulation,0,&memory);
    const auto bytes=net::encodeSnapshot(source);check(!bytes.empty()&&bytes.size()<net::MaxMessageBytes,"ordinary filtered snapshot encodes compactly");
    net::Snapshot decoded;std::string error;check(net::decodeSnapshot(bytes.data(),bytes.size(),decoded,error)&&decoded.tick==source.tick&&decoded.entities.size()==source.entities.size()&&decoded.fog==source.fog&&decoded.config.matchLength==MatchLength::Standard&&decoded.config.playerCount==2&&decoded.eliminatedMask==0,"ordinary two-player Standard snapshot round trips through protocol seven");
    const Entity* sourceHQ=snapshotEntity(source,0,Kind::Headquarters);const Entity* decodedHQ=snapshotEntity(decoded,0,Kind::Headquarters);
    check(sourceHQ&&decodedHQ&&decodedHQ->queue.size()==2&&decodedHQ->queue[0].id==sourceHQ->queue[0].id&&decodedHQ->queue[1].id==sourceHQ->queue[1].id&&decodedHQ->nextQueueId==sourceHQ->nextQueueId,"queue job IDs and next producer sequence round trip in snapshots");
    auto oldProtocol=bytes;putU32(oldProtocol,4,6);rejectsSnapshot(oldProtocol,"protocol-six snapshot is rejected after player count extends the header");
    auto malformed=bytes;malformed.pop_back();rejectsSnapshot(malformed,"truncated snapshot is rejected");
    malformed=bytes;malformed.push_back(0);rejectsSnapshot(malformed,"snapshot trailing bytes are rejected");
    malformed=bytes;putU32(malformed,17,0x7fc00000U);rejectsSnapshot(malformed,"snapshot NaN is rejected");
    malformed=bytes;malformed.at(21)=static_cast<std::uint8_t>(MatchLength::Count);rejectsSnapshot(malformed,"invalid match-length preset is rejected");
    malformed=bytes;malformed.at(22)=3;rejectsSnapshot(malformed,"invalid snapshot player count is rejected");
    malformed=bytes;malformed.at(40)=4;rejectsSnapshot(malformed,"elimination bits outside the configured seats are rejected");
    malformed=bytes;malformed.at(40)=1;rejectsSnapshot(malformed,"ongoing snapshot cannot have fewer than two survivors");
    malformed=bytes;malformed.at(65)=2;rejectsSnapshot(malformed,"invalid army rally presence flag is rejected");
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
    const Command defend{CommandType::Defend,1,handles,{1100,1000},0,Kind::Worker,0};const auto defendBytes=net::encodeCommand(defend,20);check(!defendBytes.empty(),"Defend command encodes without changing the wire layout");
    Command decodedDefend;sequence=0;check(net::decodeCommand(defendBytes.data(),defendBytes.size(),decodedDefend,sequence,error)&&sequence==20&&decodedDefend.type==CommandType::Defend&&decodedDefend.units==handles&&decodedDefend.point.x==1100&&decodedDefend.point.y==1000,"Defend command round trips through the existing protocol");
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
    oldProtocol=defendBytes;putU32(oldProtocol,4,6);rejectsCommand(oldProtocol,"protocol-six command is rejected after FFA compatibility changes");
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
    const std::vector<std::pair<std::string,void(*)()>> tests{{"view privacy and handles",viewPrivacyAndHandles},{"resource memory",resourceMemory},{"effect privacy",effectsPrivacy},{"four-player views",fourPlayerViews},{"bounded codecs",codecValidation},{"replica guards",replicaGuards}};
    int failed=0;for(const auto& test:tests)try{test.second();std::cout<<"PASS "<<test.first<<'\n';}catch(const std::exception& exception){++failed;std::cerr<<"FAIL "<<test.first<<": "<<exception.what()<<'\n';}
    std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';return failed?1:0;
}
