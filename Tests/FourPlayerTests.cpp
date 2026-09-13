#include "Sim/Network.h"
#include "Sim/Simulation.h"
#include "Sim/SimulationRules.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cinder;

namespace {

void check(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}

bool close(float left,float right,float epsilon=0.01f) {
    return std::fabs(left-right)<=epsilon;
}

float distanceSquared(Vec2 left,Vec2 right) {
    const float x=left.x-right.x,y=left.y-right.y;return x*x+y*y;
}

Config fourPlayerConfig(int map=0,MatchLength length=MatchLength::Standard,std::uint32_t seed=4040) {
    Config config{map,seed,true,1.0f,length};config.playerCount=4;return config;
}

const Entity* firstOf(const Simulation& simulation,int team,Kind kind) {
    const auto found=std::find_if(simulation.entities().begin(),simulation.entities().end(),[&](const Entity& entity) {
        return entity.alive()&&entity.team==team&&entity.kind==kind;
    });
    return found==simulation.entities().end()?nullptr:&*found;
}

Entity* edit(Simulation& simulation,Id id) {
    for(auto& entity:const_cast<std::vector<Entity>&>(simulation.entities()))if(entity.id==id)return &entity;
    return nullptr;
}

void sharedRulesAreExhaustive() {
    for(int value=-1;value<=15;++value) {
        const Kind kind=static_cast<Kind>(value);
        check(rules::validKind(kind)==(value>=0&&value<=14),"kind validity covers every enum value and adjacent invalid values");
        const bool production=value==8||value==10||value==11||value==12;
        const bool combatProduction=value==10||value==11||value==12;
        check(rules::productionKind(kind)==production,"production-kind policy matches the established producer set");
        check(rules::combatProductionKind(kind)==combatProduction,"combat-production policy matches the established producer set");
    }
    check(rules::researchKind(-1)==Kind::Worker&&rules::researchKind(0)==Kind::Worker&&
          rules::researchKind(1)==Kind::Striker&&rules::researchKind(2)==Kind::Lancer&&
          rules::researchKind(3)==Kind::Worker,"research queue sentinel mapping remains byte-compatible");
    for(int players=-1;players<=6;++players)check(rules::validPlayerCount(players)==(players==2||players==4),"only two and four-player matches are valid");
    check(rules::activePlayerMask(2)==0x03&&rules::activePlayerMask(4)==0x0f&&rules::activePlayerMask(3)==0,
          "active-player masks retain their exact two/four-player values");

    for(int players:{2,4})for(int winner=-3;winner<=players;++winner)for(int rawMask=0;rawMask<32;++rawMask) {
        const auto mask=static_cast<std::uint8_t>(rawMask);
        const int activeMask=(1<<players)-1;
        int eliminated=0;for(int bits=rawMask&activeMask;bits;bits&=bits-1)++eliminated;
        const int survivors=players-eliminated;
        const bool inRange=(rawMask&~activeMask)==0&&winner>=-2&&winner<players;
        const bool expected=inRange&&((winner==-1&&survivors>=2)||(winner==-2&&survivors==0)||
            (winner>=0&&survivors==1&&(rawMask&(1<<winner))==0));
        check(rules::validMatchOutcome(players,winner,mask)==expected,"every winner and elimination-mask combination follows the shared outcome invariant");
    }
    check(!rules::validMatchOutcome(3,-1,0),"unsupported player counts never form valid outcomes");
}

std::vector<std::string> readLines(const std::string& path) {
    std::ifstream input(path);std::vector<std::string> lines;std::string line;
    while(std::getline(input,line))lines.push_back(line);return lines;
}

void writeLines(const std::string& path,const std::vector<std::string>& lines) {
    std::ofstream output(path,std::ios::trunc);for(const auto& line:lines)output<<line<<'\n';
}

std::vector<std::string> fields(const std::string& line) {
    std::istringstream input(line);std::vector<std::string> values;std::string value;
    while(input>>value)values.push_back(value);return values;
}

std::string join(const std::vector<std::string>& values) {
    std::ostringstream output;for(std::size_t index=0;index<values.size();++index)output<<(index?" ":"")<<values[index];return output.str();
}

bool circleTouchesBox(Vec2 point,float radius,const Obstacle& obstacle) {
    const float x=std::max(std::fabs(point.x-obstacle.center.x)-obstacle.half.x,0.0f);
    const float y=std::max(std::fabs(point.y-obstacle.center.y)-obstacle.half.y,0.0f);
    return x*x+y*y<radius*radius;
}

void generationAndRoutes() {
    const std::array<MatchLength,3> lengths{MatchLength::Short,MatchLength::Standard,MatchLength::Long};
    for(int map=0;map<3;++map)for(MatchLength length:lengths) {
        Simulation simulation;simulation.reset(fourPlayerConfig(map,length,9000+map*10+static_cast<unsigned>(length)));
        check(simulation.playerCount()==4&&!simulation.config().ai&&simulation.winner()==-1&&simulation.eliminatedMask()==0,
              "four-player reset normalizes AI off and starts with four active players");
        for(int team=0;team<4;++team) {
            const auto anchors=std::count_if(simulation.entities().begin(),simulation.entities().end(),[&](const Entity& entity){return entity.alive()&&entity.team==team&&entity.kind==Kind::Headquarters;});
            const auto workers=std::count_if(simulation.entities().begin(),simulation.entities().end(),[&](const Entity& entity){return entity.alive()&&entity.team==team&&entity.kind==Kind::Worker;});
            check(anchors==1&&workers==5&&simulation.players()[team].ore==500,"every corner starts with the same base, workers and funds");
            const Entity* anchor=firstOf(simulation,team,Kind::Headquarters);check(anchor&&simulation.visible(team,anchor->pos),"each player sees its own starting base");
            for(int opponent=0;opponent<4;++opponent)if(opponent!=team)check(!simulation.visible(opponent,anchor->pos),"starting bases do not leak through another player's fog");
            std::vector<const Entity*> resources;
            for(const auto& entity:simulation.entities())if(entity.alive()&&entity.kind==Kind::Resource)resources.push_back(&entity);
            std::sort(resources.begin(),resources.end(),[&](const Entity* left,const Entity* right){return distanceSquared(left->pos,anchor->pos)<distanceSquared(right->pos,anchor->pos);});
            check(resources.size()>=4,"four-player map has home ore");
            float homeOre=0;for(int index=0;index<4;++index)homeOre+=resources[index]->resource;
            check(close(homeOre,4.0f*matchLengthProfile(length).homeNodeOre),"every corner receives the same four finite home deposits");
        }
        for(const auto& entity:simulation.entities())if(entity.alive()) {
            check(entity.pos.x>=definition(entity.kind).radius&&entity.pos.y>=definition(entity.kind).radius&&
                  entity.pos.x<=simulation.worldSize()-definition(entity.kind).radius&&entity.pos.y<=simulation.worldSize()-definition(entity.kind).radius,
                  "four-player actors stay within the selected battlefield");
            for(const auto& obstacle:simulation.obstacles())check(!circleTouchesBox(entity.pos,definition(entity.kind).radius,obstacle),"four-player starts and ore remain clear of terrain");
        }
        for(std::size_t left=0;left<simulation.entities().size();++left)for(std::size_t right=left+1;right<simulation.entities().size();++right) {
            const Entity& a=simulation.entities()[left];const Entity& b=simulation.entities()[right];
            if(!a.alive()||!b.alive()||definition(a.kind).air||definition(b.kind).air)continue;
            const float separation=definition(a.kind).radius+definition(b.kind).radius;
            if(distanceSquared(a.pos,b.pos)<separation*separation)std::cerr<<"OVERLAP map="<<map<<" length="<<static_cast<int>(length)<<" a="<<a.id<<" kind="<<static_cast<int>(a.kind)<<" at="<<a.pos.x<<','<<a.pos.y<<" b="<<b.id<<" kind="<<static_cast<int>(b.kind)<<" at="<<b.pos.x<<','<<b.pos.y<<" distance="<<std::sqrt(distanceSquared(a.pos,b.pos))<<" required="<<separation<<'\n';
            check(distanceSquared(a.pos,b.pos)>=separation*separation,"four-player starting actors do not overlap");
        }

        Navigation navigation;std::vector<NavBox> boxes;std::vector<NavCircle> circles;
        for(const auto& obstacle:simulation.obstacles())boxes.push_back({obstacle.center,obstacle.half});
        for(const auto& entity:simulation.entities())if(entity.alive()&&(definition(entity.kind).building||entity.kind==Kind::Resource))circles.push_back({entity.id,entity.pos,definition(entity.kind).radius});
        navigation.sync(simulation.worldSize(),boxes,circles);
        for(int from=0;from<4;++from)for(int to=from+1;to<4;++to) {
            const Entity* start=firstOf(simulation,from,Kind::Worker);const Entity* goal=firstOf(simulation,to,Kind::Worker);
            check(start&&goal&&navigation.route(start->pos,{goal->pos},definition(Kind::Worker).radius).reached,"every pair of starting corners has a ground route");
        }

        const auto rotate=[&](Vec2 point){return Vec2{simulation.worldSize()-point.y,point.x};};
        for(const auto& obstacle:simulation.obstacles()) {
            const Vec2 center=rotate(obstacle.center);const Vec2 half{obstacle.half.y,obstacle.half.x};
            check(std::any_of(simulation.obstacles().begin(),simulation.obstacles().end(),[&](const Obstacle& candidate){return close(candidate.center.x,center.x)&&close(candidate.center.y,center.y)&&close(candidate.half.x,half.x)&&close(candidate.half.y,half.y);}),
                  "four-player terrain is symmetric under quarter turns");
        }
        for(const auto& resource:simulation.entities())if(resource.kind==Kind::Resource) {
            const Vec2 position=rotate(resource.pos);
            check(std::any_of(simulation.entities().begin(),simulation.entities().end(),[&](const Entity& candidate){return candidate.kind==Kind::Resource&&close(candidate.pos.x,position.x)&&close(candidate.pos.y,position.y)&&close(candidate.resource,resource.resource);}),
                  "four-player ore is symmetric under quarter turns");
        }
    }

    Simulation invalid;Config config=fourPlayerConfig();config.playerCount=3;invalid.reset(config);
    check(invalid.playerCount()==2&&invalid.config().ai,"unsupported player counts normalize to the existing two-player mode");
}

void economyCommandsAndHostility() {
    for(int map=0;map<3;++map) {
        Simulation first,second;const Config config=fourPlayerConfig(map,MatchLength::Standard,7171+map);first.reset(config);second.reset(config);
        for(int team=0;team<4;++team) {
            const Entity* worker=firstOf(first,team,Kind::Worker);const Entity* copy=firstOf(second,team,Kind::Worker);
            check(worker&&copy,"command fixture has a worker for every player");
            const Vec2 goal{worker->pos.x+(team<2?45.0f:-45.0f),worker->pos.y};
            check(first.command({CommandType::Move,team,{worker->id},goal}).accepted&&second.command({CommandType::Move,team,{copy->id},goal}).accepted,
                  "every active player can issue authoritative commands");
        }
        for(int step=0;step<80;++step){first.update(Simulation::Step);second.update(Simulation::Step);}
        check(first.stateHash()==second.stateHash(),"four-player commands and movement remain deterministic");

        Simulation economy;economy.reset(config);
        for(int step=0;step<600;++step)economy.update(Simulation::Step);
        std::array<int,4> gathered{};for(int team=0;team<4;++team)gathered[team]=economy.players()[team].stats.gathered;
        check(*std::min_element(gathered.begin(),gathered.end())>0&&
              *std::max_element(gathered.begin(),gathered.end())-*std::min_element(gathered.begin(),gathered.end())<=18,
              "four symmetric economies deliver within one cargo cycle over equal time");
    }

    Simulation combat;combat.reset(fourPlayerConfig());
    const Id attacker=combat.debugSpawn(Kind::Striker,0,{2400,2400});
    for(int opponent=1;opponent<4;++opponent) {
        const Id target=combat.debugSpawn(Kind::Worker,opponent,{2400.0f+opponent*50.0f,2400});
        const float hp=combat.find(target)->hp;
        check(combat.command({CommandType::Attack,0,{attacker},{},target}).accepted,"direct attacks accept every opposing team");
        for(int step=0;step<40&&combat.find(target)->hp==hp;++step)combat.update(Simulation::Step);
        check(combat.find(target)->hp<hp,"combat treats every other active team as hostile");
    }
}

void eliminationAndDraw() {
    Simulation simulation;simulation.reset(fourPlayerConfig());
    const Entity* original=firstOf(simulation,2,Kind::Headquarters);check(original,"elimination fixture has team two Anchor");
    const Id reserve=simulation.debugSpawn(Kind::Headquarters,2,{4200,1200});check(reserve,"team two can own a second Anchor");
    edit(simulation,original->id)->hp=0;simulation.update(Simulation::Step);
    check(!simulation.eliminated(2)&&simulation.winner()==-1,"a player survives while any Anchor remains");
    const Vec2 formerVision=simulation.find(reserve)->pos;edit(simulation,reserve)->hp=0;simulation.update(Simulation::Step);
    check(simulation.eliminated(2)&&simulation.eliminatedMask()==4&&simulation.winner()==-1,"losing every Anchor eliminates one player without ending a four-player match");
    check(std::none_of(simulation.entities().begin(),simulation.entities().end(),[](const Entity& entity){return entity.alive()&&entity.team==2;}),"elimination disables every remaining actor owned by that player");
    check(!simulation.visible(2,formerVision),"elimination immediately removes the defeated player's live vision");
    check(!simulation.command({CommandType::Move,2,{1},{1000,1000}}).accepted,"eliminated players cannot issue commands");
    const Entity* survivingWorker=firstOf(simulation,3,Kind::Worker);
    check(survivingWorker&&simulation.command({CommandType::Hold,3,{survivingWorker->id},{},0,Kind::Worker,0}).accepted,"surviving players continue commanding after an elimination");

    simulation.forfeit(1);check(simulation.eliminatedMask()==6&&simulation.winner()==-1,"a four-player forfeit removes one seat and leaves two survivors playing");
    simulation.forfeit(3);check(simulation.eliminatedMask()==14&&simulation.winner()==0,"the sole surviving player wins after later forfeits");
    const auto finalHash=simulation.stateHash();simulation.forfeit(0);check(simulation.stateHash()==finalHash,"a completed match rejects later forfeits");

    Simulation draw;draw.reset(fourPlayerConfig());
    for(auto& entity:const_cast<std::vector<Entity>&>(draw.entities()))if(entity.kind==Kind::Headquarters)entity.hp=0;
    draw.update(Simulation::Step);
    check(draw.winner()==-2&&draw.eliminatedMask()==15,"simultaneous loss of every Anchor resolves as a draw");
    check(std::none_of(draw.entities().begin(),draw.entities().end(),[](const Entity& entity){return entity.alive()&&entity.team>=0;}),"draw resolution disables all remaining armies");
}

void persistenceMigrationAndReplica() {
    const auto path=(std::filesystem::temp_directory_path()/"cinderline-four-player-v10.sav").string();
    Simulation simulation;simulation.reset(fourPlayerConfig(2,MatchLength::Long,9191));simulation.forfeit(2);
    const Entity* worker=firstOf(simulation,3,Kind::Worker);check(worker&&simulation.command({CommandType::Move,3,{worker->id},{1800,4200}}).accepted,"persistence fixture records a surviving-player order");
    for(int step=0;step<20;++step)simulation.update(Simulation::Step);
    check(simulation.save(path),"four-player state saves");
    const auto current=readLines(path);check(current.size()>3&&current.front()=="CINDERLINE 10","four-player persistence declares save version ten");
    check(fields(current[1]).size()==6&&fields(current[1]).back()=="4"&&fields(current[2]).size()==6&&fields(current[2]).back()=="4","version ten stores player count and elimination mask explicitly");
    Simulation loaded;check(loaded.load(path)&&loaded.playerCount()==4&&loaded.eliminated(2)&&loaded.stateHash()==simulation.stateHash(),"four-player save round trip preserves elimination and deterministic state");
    for(int step=0;step<40;++step){simulation.update(Simulation::Step);loaded.update(Simulation::Step);}
    check(loaded.stateHash()==simulation.stateHash(),"loaded four-player state continues deterministically");

    auto rejects=[&](std::vector<std::string> lines,const std::string& message) {
        writeLines(path,lines);Simulation untouched;const auto before=untouched.stateHash();check(!untouched.load(path)&&untouched.stateHash()==before,message);
    };
    auto invalidCount=current;auto config=fields(invalidCount[1]);config.back()="3";invalidCount[1]=join(config);rejects(invalidCount,"version ten rejects unsupported player counts atomically");
    auto invalidMask=current;auto timeline=fields(invalidMask[2]);timeline.back()="16";invalidMask[2]=join(timeline);rejects(invalidMask,"version ten rejects elimination bits outside the active player range");
    auto inconsistentWinner=current;timeline=fields(inconsistentWinner[2]);timeline[4]="0";inconsistentWinner[2]=join(timeline);rejects(inconsistentWinner,"version ten rejects a winner while multiple players survive");
    auto missingElimination=current;timeline=fields(missingElimination[2]);timeline.back()="0";missingElimination[2]=join(timeline);rejects(missingElimination,"version ten rejects a player without an Anchor unless that player is eliminated");

    Simulation standard;standard.reset({1,5151,false,1.0f,MatchLength::Standard});check(standard.save(path),"legacy migration fixture saves");
    auto legacy=readLines(path);legacy.front()="CINDERLINE 9";config=fields(legacy[1]);config.pop_back();legacy[1]=join(config);timeline=fields(legacy[2]);timeline.pop_back();legacy[2]=join(timeline);writeLines(path,legacy);
    Simulation migrated;check(migrated.load(path)&&migrated.playerCount()==2&&migrated.eliminatedMask()==0&&migrated.stateHash()==standard.stateHash(),"version nine migrates to an ongoing two-player match without changing gameplay state");

    net::ViewMemory memory;const net::Snapshot view=net::snapshotFor(simulation,2,&memory);
    check(view.config.playerCount==4&&view.eliminatedMask==1&&view.winner==-1,"an eliminated recipient receives normalized four-player elimination state");
    Simulation replica;std::string error;check(replica.applySnapshot(view,&error)&&replica.isReplica()&&replica.playerCount()==4&&replica.eliminated(0),"replica imports checked normalized elimination state");
    for(int team=1;team<Simulation::MaxPlayers;++team)check(replica.players()[team].ore==0&&replica.players()[team].tier==0,"replica sanitizes every non-recipient player slot");
    std::filesystem::remove(path);
}

} // namespace

int main() {
    try {
        sharedRulesAreExhaustive();
        generationAndRoutes();
        economyCommandsAndHostility();
        eliminationAndDraw();
        persistenceMigrationAndReplica();
        std::cout<<"FOUR_PLAYER_TESTS passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL "<<error.what()<<'\n';return 1;
    }
}
