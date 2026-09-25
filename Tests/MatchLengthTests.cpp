#include "Sim/Simulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace cinder;

namespace {

int passed=0,failed=0;
void check(bool condition,const std::string& message) {
 if(condition){++passed;return;}++failed;std::cerr<<"FAIL "<<message<<'\n';
}
bool close(float a,float b,float epsilon=0.002f){return std::fabs(a-b)<=epsilon;}
Config configFor(int map,MatchLength length,std::uint32_t seed=4242){return {map,seed,false,1.0f,length};}

const Entity* firstOf(const Simulation& simulation,int team,Kind kind) {
 const auto found=std::find_if(simulation.entities().begin(),simulation.entities().end(),[&](const Entity& entity){
  return entity.alive()&&entity.team==team&&entity.kind==kind;
 });
 return found==simulation.entities().end()?nullptr:&*found;
}

Vec2 legalSite(const Simulation& simulation,int team,Kind kind,Id worker) {
 const Entity* anchor=firstOf(simulation,team,Kind::Headquarters);
 if(!anchor)return {};
 for(float radius:{240.0f,320.0f,410.0f,520.0f,640.0f})for(int spoke=0;spoke<32;++spoke) {
  const float angle=spoke*6.28318530718f/32.0f;
  const Vec2 point{anchor->pos.x+std::cos(angle)*radius,anchor->pos.y+std::sin(angle)*radius};
  if(simulation.buildStatus(team,kind,{worker},&point).accepted)return point;
 }
 return {};
}

std::vector<std::string> readLines(const std::string& path) {
 std::ifstream input(path);std::vector<std::string> lines;std::string line;
 while(std::getline(input,line))lines.push_back(line);return lines;
}
void writeLines(const std::string& path,const std::vector<std::string>& lines) {
 std::ofstream output(path,std::ios::trunc);for(const auto& line:lines)output<<line<<'\n';
}
std::vector<std::string> fields(const std::string& row) {
 std::istringstream input(row);std::vector<std::string> result;std::string field;
 while(input>>field)result.push_back(field);return result;
}
std::string joined(const std::vector<std::string>& values) {
 std::ostringstream output;for(std::size_t i=0;i<values.size();++i)output<<(i?" ":"")<<values[i];return output.str();
}

void downgradeSaveFifteenToTen(std::vector<std::string>& lines) {
 auto config=fields(lines.at(1));
 check(config.size()==7&&config.back()=="0","legacy migration fixture uses revision zero");
 config.pop_back();lines[1]=joined(config);
 const auto players=static_cast<std::size_t>(std::stoul(config.back()));std::size_t cursor=3+players;
 cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
 const auto entityCount=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
 for(std::size_t entity=0;entity<entityCount;++entity) {
  ++cursor;cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
  cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
 }
 const auto effectHeader=fields(lines.at(cursor));cursor+=1+static_cast<std::size_t>(std::stoul(effectHeader.front()))+players*2;
 const auto recordingCount=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
 for(std::size_t recording=0;recording<recordingCount;++recording) {
  auto row=fields(lines.at(cursor));
  check(row.size()>=13&&row.size()==13+static_cast<std::size_t>(std::stoul(row[12])),
        "version-thirteen recording layout contains formation fields");
  row.erase(row.begin()+9,row.begin()+12);
  check(row.size()>=10&&row.size()==10+static_cast<std::size_t>(std::stoul(row[9])),
        "version-eleven recording layout contains queue mode");
  row.erase(row.begin()+8);lines[cursor++]=joined(row);
 }
 const auto orders=std::find(lines.begin(),lines.end(),"ORDER_QUEUES 1");
 const auto sustained=std::find(lines.begin(),lines.end(),"SUSTAINED_ORDERS 1");
 const auto formation=std::find(lines.begin(),lines.end(),"FORMATION_ORDERS 1");
 const auto queuedWork=std::find(lines.begin(),lines.end(),"QUEUED_WORK 1");
 check(orders!=lines.end()&&sustained!=lines.end()&&formation!=lines.end()&&queuedWork!=lines.end()&&orders<sustained&&sustained<formation&&formation<queuedWork,
       "save-fourteen preset fixture contains ordered tactical, sustained, formation, and queued-work state");
 lines.erase(queuedWork,lines.end());
 lines.erase(formation,lines.end());
 lines.erase(orders,lines.end());
}

void profilesAndGeneration() {
 check(matchLengthAt(-1)==MatchLength::Standard&&matchLengthAt(3)==MatchLength::Standard,
       "invalid match length indices resolve to Standard");
 check(std::string(matchLengthName(MatchLength::Short))=="Short"&&
       std::string(matchLengthName(MatchLength::Standard))=="Standard"&&
       std::string(matchLengthName(MatchLength::Long))=="Long","match length names are stable");
 check(matchLengthProfile(MatchLength::Short).productionTimeScale==0.8f&&
       matchLengthProfile(MatchLength::Standard).productionTimeScale==1.0f&&
       matchLengthProfile(MatchLength::Long).productionTimeScale==1.0f,"only Short accelerates production");

 const std::array<MatchLength,3> lengths{MatchLength::Short,MatchLength::Standard,MatchLength::Long};
 const std::array<float,3> expectedSizes{3600,4800,6000};
 const std::array<int,3> expectedOre{42000,70400,105600};
 for(int map=0;map<3;++map)for(std::size_t lengthIndex=0;lengthIndex<lengths.size();++lengthIndex) {
  Simulation simulation;simulation.reset(configFor(map,lengths[lengthIndex]));
  check(simulation.worldSize()==expectedSizes[lengthIndex],"active world size matches the selected profile");
  int ore=0,resources=0;
  for(const auto& entity:simulation.entities()) {
   check(entity.pos.x>=0&&entity.pos.y>=0&&entity.pos.x<=simulation.worldSize()&&entity.pos.y<=simulation.worldSize()&&
         entity.goal.x>=0&&entity.goal.y>=0&&entity.goal.x<=simulation.worldSize()&&entity.goal.y<=simulation.worldSize()&&
         entity.rally.x>=0&&entity.rally.y>=0&&entity.rally.x<=simulation.worldSize()&&entity.rally.y<=simulation.worldSize(),
         "generated actors and destinations stay inside the active world");
   if(entity.kind==Kind::Resource){++resources;ore+=static_cast<int>(entity.resource);}
  }
  check(resources==20&&ore==expectedOre[lengthIndex],"preset keeps the authored nodes and applies its finite ore reserve");
  for(std::size_t left=0;left<simulation.entities().size();++left)for(std::size_t right=left+1;right<simulation.entities().size();++right) {
   const Entity& a=simulation.entities()[left];const Entity& b=simulation.entities()[right];
   if(!a.alive()||!b.alive()||definition(a.kind).air||definition(b.kind).air)continue;
   const float separation=definition(a.kind).radius+definition(b.kind).radius;
   const float dx=a.pos.x-b.pos.x,dy=a.pos.y-b.pos.y;
   check(dx*dx+dy*dy>=separation*separation,"generated ground actors do not overlap each other");
  }
  for(const auto& obstacle:simulation.obstacles())check(
   obstacle.center.x-obstacle.half.x>=0&&obstacle.center.y-obstacle.half.y>=0&&
   obstacle.center.x+obstacle.half.x<=simulation.worldSize()&&obstacle.center.y+obstacle.half.y<=simulation.worldSize(),
   "scaled terrain stays inside the active world");
  for(const auto& obstacle:simulation.obstacles())check(std::any_of(simulation.obstacles().begin(),simulation.obstacles().end(),[&](const Obstacle& mirror){
   return close(mirror.center.x,simulation.worldSize()-obstacle.center.x)&&close(mirror.center.y,simulation.worldSize()-obstacle.center.y)&&
          close(mirror.half.x,obstacle.half.x)&&close(mirror.half.y,obstacle.half.y);
  }),"terrain geometry remains rotationally symmetric");
  // Terrain symmetry alone does not make a duel fair. Ore is what players fight
  // over, so every deposit must have a half-turn partner carrying equal reserve.
  for(const auto& entity:simulation.entities()) {
   if(entity.kind!=Kind::Resource)continue;
   check(std::any_of(simulation.entities().begin(),simulation.entities().end(),[&](const Entity& mirror) {
    return mirror.kind==Kind::Resource&&close(mirror.pos.x,simulation.worldSize()-entity.pos.x)&&
           close(mirror.pos.y,simulation.worldSize()-entity.pos.y)&&close(mirror.resource,entity.resource);
   }),"two-player ore is symmetric under half turns");
  }

  Navigation navigation;std::vector<NavBox> boxes;std::vector<NavCircle> circles;
  for(const auto& obstacle:simulation.obstacles())boxes.push_back({obstacle.center,obstacle.half});
  for(const auto& entity:simulation.entities())if(entity.alive()&&(definition(entity.kind).building||entity.kind==Kind::Resource))
   circles.push_back({entity.id,entity.pos,definition(entity.kind).radius});
  navigation.sync(simulation.worldSize(),boxes,circles);
  const Entity* from=firstOf(simulation,0,Kind::Worker);const Entity* to=firstOf(simulation,1,Kind::Worker);
  check(from&&to&&navigation.route(from->pos,{to->pos},definition(Kind::Worker).radius).reached,
        "every authored map and length has a static ground route between starting sides");
  check(from&&legalSite(simulation,0,Kind::Foundry,from->id).x>0&&to&&legalSite(simulation,1,Kind::Foundry,to->id).x>0,
        "both starting bases have an accessible legal first production site");

  Simulation duplicate;duplicate.reset(configFor(map,lengths[lengthIndex]));
  check(duplicate.stateHash()==simulation.stateHash(),"map and length generation is deterministic for a fixed seed");

  Simulation economy;economy.reset(configFor(map,lengths[lengthIndex],6000));
  for(int step=0;step<600;++step)economy.update(Simulation::Step);
  // Collision-safe corner routes can put mirrored workers on opposite sides
  // of a delivery tick. Allow one in-flight load, as in the four-player gate.
  const int firstGathered=economy.players()[0].stats.gathered,secondGathered=economy.players()[1].stats.gathered;
  check(firstGathered>0&&secondGathered>0&&std::abs(firstGathered-secondGathered)<=18,
        "mirrored bases deliver starting ore within one cargo load over equal time");
 }

 Simulation defaulted,standard;defaulted.reset({1,77,false,1.0f});standard.reset(configFor(1,MatchLength::Standard,77));
 check(defaulted.stateHash()==standard.stateHash(),"default Config preserves the original Standard match exactly");
}

void aiUsesActiveWorld() {
 for(int map=0;map<3;++map)for(MatchLength length:{MatchLength::Short,MatchLength::Standard,MatchLength::Long}) {
  Config config=configFor(map,length,7100+map*10+static_cast<unsigned>(length));config.ai=true;
  Simulation first,second;first.reset(config);second.reset(config);
  for(int step=0;step<800;++step){first.update(Simulation::Step);second.update(Simulation::Step);}
  check(first.stateHash()==second.stateHash(),"AI remains deterministic for every map and match length");
  for(const auto& entity:first.entities())check(entity.pos.x>=0&&entity.pos.y>=0&&entity.pos.x<=first.worldSize()&&entity.pos.y<=first.worldSize()&&
      entity.goal.x>=0&&entity.goal.y>=0&&entity.goal.x<=first.worldSize()&&entity.goal.y<=first.worldSize(),
      "AI actor positions and goals stay inside the active world");
  for(const auto& item:first.recording())check(item.command.point.x>=0&&item.command.point.y>=0&&
      item.command.point.x<=first.worldSize()&&item.command.point.y<=first.worldSize(),
      "AI authored command targets stay inside the active world");
 }

 Config lateConfig=configFor(0,MatchLength::Short,7199);lateConfig.ai=true;
 Simulation late;late.reset(lateConfig);
 bool scaledScoutTarget=false;std::uint64_t scaledScoutTick=0;
 const float scale=late.worldSize()/Simulation::WorldSize;
 const std::array<Vec2,7> scaledPublicTargets{{
  {600*scale,600*scale},{2900*scale,3800*scale},{3800*scale,2000*scale},
  {1900*scale,1000*scale},{1000*scale,2800*scale},{600*scale,4200*scale},{4200*scale,600*scale}}};
 for(int step=0;step<9000&&!scaledScoutTarget;++step) {
  late.update(Simulation::Step);
  for(const auto& item:late.recording())if(item.command.team==1) {
   if(item.command.type==CommandType::Move&&std::any_of(scaledPublicTargets.begin(),scaledPublicTargets.end(),[&](Vec2 point){
      const float dx=point.x-item.command.point.x,dy=point.y-item.command.point.y;return dx*dx+dy*dy<1.0f;
   })){scaledScoutTarget=true;scaledScoutTick=item.tick;}
  }
 }
 check(scaledScoutTarget&&scaledScoutTick>800,"Short AI reaches and accepts a later scaled reconnaissance or expansion landmark");
}

void shortPaidVictoryPath() {
 Simulation simulation;simulation.reset(configFor(0,MatchLength::Short,8181));
 const Entity* worker=firstOf(simulation,0,Kind::Worker);const Vec2 site=worker?legalSite(simulation,0,Kind::Foundry,worker->id):Vec2{};
 check(worker&&site.x>0&&simulation.command({CommandType::Build,0,{worker->id},site,0,Kind::Foundry,0}).accepted,
       "Short economy pays for and starts its required first producer");
 Id foundry=0;for(const auto& entity:simulation.entities())if(entity.team==0&&entity.kind==Kind::Foundry)foundry=entity.id;
 for(int step=0;step<2400&&foundry&&simulation.find(foundry)->progress<1;++step)simulation.update(Simulation::Step);
 check(foundry&&simulation.find(foundry)->progress>=1,"Short workers can finish the paid producer without starving");
 for(int step=0;step<1600&&simulation.players()[0].ore<6*definition(Kind::Striker).cost;++step)simulation.update(Simulation::Step);
 check(simulation.command({CommandType::AutoTrain,0,{}, {},foundry,Kind::Striker,6}).accepted,
       "Short finite economy funds an ordinary six-unit attack group");
 for(int step=0;step<3000;++step) {
  int strikers=0;for(const auto& entity:simulation.entities())if(entity.alive()&&entity.team==0&&entity.kind==Kind::Striker)++strikers;
  if(strikers>=6)break;simulation.update(Simulation::Step);
 }
 std::vector<Id> army;for(const auto& entity:simulation.entities())if(entity.alive()&&entity.team==0&&entity.kind==Kind::Striker)army.push_back(entity.id);
 const Entity* enemy=firstOf(simulation,1,Kind::Headquarters);
 check(army.size()==6&&enemy&&simulation.command({CommandType::AttackMove,0,army,enemy->pos}).accepted,
       "the paid Short attack group can receive a cross-map attack order");
 for(int step=0;step<5000&&simulation.winner()==-1;++step)simulation.update(Simulation::Step);
 check(simulation.winner()==0,"the ordinary paid Short economy and army can complete a victory path");
}

void boundsAndMovement() {
 Simulation longMatch;longMatch.reset(configFor(0,MatchLength::Long));
 const Id scout=longMatch.debugSpawn(Kind::Scout,0,{5000,4000});
 check(scout&&longMatch.find(scout)->pos.x>Simulation::WorldSize,"Long can create actors beyond the old Standard boundary");
 check(longMatch.command({CommandType::Move,0,{scout},{5700,4000}}).accepted,"Long accepts destinations beyond the old boundary");
 for(int step=0;step<20;++step)longMatch.update(Simulation::Step);
 check(longMatch.find(scout)->pos.x>5000,"a Long unit moves normally in the expanded world");

 Simulation shortMatch;shortMatch.reset(configFor(0,MatchLength::Short));
 const Entity* worker=firstOf(shortMatch,0,Kind::Worker);check(worker!=nullptr,"Short bound fixture has a worker");
 const auto before=shortMatch.stateHash();
 check(!shortMatch.command({CommandType::Move,0,{worker?worker->id:0},{4000,4000}}).accepted&&shortMatch.stateHash()==before,
       "Short rejects a destination outside its active boundary without mutation");
 check(!shortMatch.visible(0,{4000,4000})&&!shortMatch.explored(0,{4000,4000}),
       "Short fog queries reject points outside its active boundary");
}

void pacing() {
 struct Timing {float train=0,research=0,constructionStep=0;};
 auto measure=[](MatchLength length) {
  Simulation simulation;simulation.reset(configFor(0,length,902));simulation.debugResources(0,100000);
  const Id foundry=simulation.debugSpawn(Kind::Foundry,0,{simulation.worldSize()*0.28f,simulation.worldSize()*0.22f});
  const Id laboratory=simulation.debugSpawn(Kind::Laboratory,0,{simulation.worldSize()*0.35f,simulation.worldSize()*0.22f});
  check(simulation.command({CommandType::Train,0,{foundry},{},0,Kind::Striker,0}).accepted,
        "manual training accepts in pacing fixture");
  check(simulation.command({CommandType::Research,0,{laboratory},{},0,Kind::Worker,0}).accepted,
        "manual research accepts in pacing fixture");
  Timing timing;timing.train=simulation.find(foundry)->queue.front().total;
  timing.research=simulation.find(laboratory)->queue.front().total;

  const Entity* sourceWorker=firstOf(simulation,0,Kind::Worker);Id worker=sourceWorker?sourceWorker->id:0;Vec2 site{};
  bool found=false;
  for(float radius:{180.0f,260.0f,340.0f,430.0f})for(int spoke=0;spoke<16&&!found;++spoke) {
   const Entity* current=simulation.find(worker);const float angle=spoke*6.28318530718f/16.0f;
   const Vec2 candidate{current->pos.x+std::cos(angle)*radius,current->pos.y+std::sin(angle)*radius};
   if(simulation.buildStatus(0,Kind::Foundry,{worker},&candidate).accepted){site=candidate;found=true;}
  }
  check(found&&simulation.command({CommandType::Build,0,{worker},site,0,Kind::Foundry,0}).accepted,
        "construction pacing fixture finds a real accessible site");
  Id foundation=0;for(const auto& entity:simulation.entities())if(entity.team==0&&entity.kind==Kind::Foundry&&entity.progress<1)foundation=entity.id;
  for(int step=0;step<3000&&foundation&&!simulation.constructionActive(foundation);++step)simulation.update(Simulation::Step);
  const float before=foundation?simulation.find(foundation)->progress:0;simulation.update(Simulation::Step);
  const float after=foundation?simulation.find(foundation)->progress:0;timing.constructionStep=after-before;
  return timing;
 };
 const Timing shortTiming=measure(MatchLength::Short),standardTiming=measure(MatchLength::Standard),longTiming=measure(MatchLength::Long);
 check(close(shortTiming.train,definition(Kind::Striker).buildTime*0.8f)&&close(standardTiming.train,definition(Kind::Striker).buildTime)&&close(longTiming.train,standardTiming.train),
       "manual training uses the selected production pace");
 check(close(shortTiming.research,80.0f)&&close(standardTiming.research,100.0f)&&close(longTiming.research,100.0f),
       "manual research uses the selected production pace");
 check(shortTiming.constructionStep>standardTiming.constructionStep*1.24f&&close(standardTiming.constructionStep,longTiming.constructionStep,0.0001f),
       "Short construction advances at the accelerated pace while Standard and Long match");

 Simulation automatic;automatic.reset(configFor(0,MatchLength::Short,903));automatic.debugResources(0,100000);
 automatic.debugSpawn(Kind::Foundry,0,{1100,1050});automatic.debugSpawn(Kind::Laboratory,0,{1450,1050});
 const auto trainPlan=automatic.autoTrainStatus(0,Kind::Striker,2),researchPlan=automatic.autoResearchStatus(0,0);
 check(trainPlan.accepted&&trainPlan.assignments.size()==1&&close(trainPlan.assignments.front().completionSeconds,definition(Kind::Striker).buildTime*0.8f*2),
       "automatic batch estimates use Short production timing");
 check(researchPlan.accepted&&close(researchPlan.assignments.front().completionSeconds,80.0f),
       "automatic research estimates use Short production timing");

 auto crossingTicks=[](MatchLength length) {
  Simulation simulation;simulation.reset(configFor(0,length,904));
  const float size=simulation.worldSize();const Vec2 destination{size*0.85f,size*0.5f};
  const Id scout=simulation.debugSpawn(Kind::Kite,0,{size*0.15f,size*0.5f});
  check(simulation.command({CommandType::Move,0,{scout},destination}).accepted,"travel pacing fixture accepts its in-bounds order");
  int ticks=0;for(;ticks<1000;++ticks) {
   const Entity* moving=simulation.find(scout);const float dx=moving->pos.x-destination.x,dy=moving->pos.y-destination.y;
   if(dx*dx+dy*dy<20.0f*20.0f)break;simulation.update(Simulation::Step);
  }
  check(ticks<1000,"travel pacing fixture reaches its destination");return ticks;
 };
 const int shortTravel=crossingTicks(MatchLength::Short),standardTravel=crossingTicks(MatchLength::Standard),longTravel=crossingTicks(MatchLength::Long);
 check(shortTravel<standardTravel&&standardTravel<longTravel,
       "battlefield traversal provides deterministic Short, Standard, and Long pacing separation");
 std::cout<<"MATCH_LENGTH_PACING travel_ticks="<<shortTravel<<','<<standardTravel<<','<<longTravel
          <<" train_seconds="<<shortTiming.train<<','<<standardTiming.train<<','<<longTiming.train<<'\n';
}

void persistenceReplayAndMigration() {
 const auto path=(std::filesystem::temp_directory_path()/"cinderline-match-length-v15.sav").string();
 for(MatchLength length:{MatchLength::Short,MatchLength::Standard,MatchLength::Long}) {
  const Config config=configFor(2,length,1200+static_cast<unsigned>(length));Simulation simulation;simulation.reset(config);
  const Entity* worker=firstOf(simulation,0,Kind::Worker);
  const Vec2 goal{simulation.worldSize()*0.45f,simulation.worldSize()*0.42f};
  check(worker&&simulation.command({CommandType::Move,0,{worker->id},goal}).accepted,"recording fixture accepts an in-profile move");
  for(int step=0;step<12;++step)simulation.update(Simulation::Step);
  check(simulation.save(path),"each match length saves");Simulation loaded;
  check(loaded.load(path)&&loaded.config().matchLength==length&&loaded.worldSize()==simulation.worldSize()&&loaded.stateHash()==simulation.stateHash(),
        "current save/load preserves the selected match length, player count and deterministic state");

  Simulation replay;replay.reset(config);std::size_t next=0;
  for(int step=0;step<12;++step) {
   while(next<simulation.recording().size()&&simulation.recording()[next].tick==replay.tick())check(replay.command(simulation.recording()[next++].command).accepted,"recorded preset command replays");
   replay.update(Simulation::Step);
  }
  check(next==simulation.recording().size()&&replay.stateHash()==simulation.stateHash(),
        "recording replays deterministically with the selected world profile");
  for(int step=0;step<20;++step){simulation.update(Simulation::Step);loaded.update(Simulation::Step);}
  check(loaded.stateHash()==simulation.stateHash(),"loaded preset continues deterministically");
 }

 Config legacyConfig=configFor(1,MatchLength::Standard,404);legacyConfig.mapRevision=0;
 Simulation standard;standard.reset(legacyConfig);check(standard.save(path),"Standard migration fixture saves");
 const auto current=readLines(path);check(current.size()>2&&current.front()=="CINDERLINE 15","current save declares revision-aware version fifteen");
 auto versionNine=current;downgradeSaveFifteenToTen(versionNine);versionNine.front()="CINDERLINE 9";auto configFields=fields(versionNine[1]);
 check(configFields.size()==6,"current config stores match length and player count");configFields.pop_back();versionNine[1]=joined(configFields);
 auto timelineFields=fields(versionNine[2]);check(timelineFields.size()==6,"current timeline stores elimination state");timelineFields.pop_back();versionNine[2]=joined(timelineFields);writeLines(path,versionNine);
 Simulation migratedNine;check(migratedNine.load(path)&&migratedNine.config().matchLength==MatchLength::Standard&&migratedNine.playerCount()==2&&migratedNine.stateHash()==standard.stateHash(),
       "version nine retains its match length and migrates to two players");
 auto legacy=versionNine;legacy.front()="CINDERLINE 8";configFields=fields(legacy[1]);
 check(configFields.size()==5,"version nine config stores match length");configFields.pop_back();legacy[1]=joined(configFields);writeLines(path,legacy);
 Simulation migrated;check(migrated.load(path)&&migrated.config().matchLength==MatchLength::Standard&&migrated.stateHash()==standard.stateHash(),
       "version eight migrates explicitly to Standard without changing state");

 auto invalid=current;auto invalidFields=fields(invalid[1]);invalidFields[4]="3";invalid[1]=joined(invalidFields);writeLines(path,invalid);
 Simulation untouched;untouched.reset(configFor(0,MatchLength::Long,99));const auto before=untouched.stateHash();
 check(!untouched.load(path)&&untouched.stateHash()==before,"invalid current match length is rejected atomically");
 auto future=current;future.front()="CINDERLINE 16";writeLines(path,future);
 check(!untouched.load(path)&&untouched.stateHash()==before,"future save version is rejected atomically");
 std::filesystem::remove(path);
}

} // namespace

int main() {
 profilesAndGeneration();boundsAndMovement();aiUsesActiveWorld();pacing();shortPaidVictoryPath();persistenceReplayAndMigration();
 std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';return failed?1:0;
}
