#include "Sim/MapDefinition.h"
#include "Sim/Simulation.h"

#include <algorithm>
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
void check(bool value,const std::string& message) {
 if(!value)throw std::runtime_error(message);
}
float distance(Vec2 a,Vec2 b) { return std::hypot(a.x-b.x,a.y-b.y); }
Vec2 lerp(Vec2 a,Vec2 b,float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t}; }
void advance(Simulation& simulation,int steps) {
 for(int i=0;i<steps;++i)simulation.update(Simulation::Step);
}
Config configFor(int revision=CurrentMapRevision) {
 Config config;config.ai=false;config.seed=4242;config.mapRevision=revision;return config;
}
Simulation quiet(int revision=CurrentMapRevision) {
 Simulation simulation;simulation.reset(configFor(revision));
 for(int team=0;team<2;++team) {
  std::vector<Id> workers;
  for(const auto& entity:simulation.entities())if(entity.team==team&&entity.kind==Kind::Worker)workers.push_back(entity.id);
  check(simulation.command({CommandType::Stop,team,workers,{}}).accepted,"initial workers stop");
 }
 return simulation;
}
std::vector<std::string> readLines(const std::filesystem::path& path) {
 std::ifstream input(path);std::vector<std::string> lines;std::string line;
 while(std::getline(input,line))lines.push_back(line);return lines;
}
void writeLines(const std::filesystem::path& path,const std::vector<std::string>& lines) {
 std::ofstream output(path);for(const auto& line:lines)output<<line<<'\n';
}

void resetAndCompatibility() {
 check(Config{}.mapRevision==CurrentMapRevision,"fresh configuration opts into the authored revision");
 for(int revision:{0,CurrentMapRevision})for(int map=0;map<3;++map)
  for(int players:{2,4})for(MatchLength length:{MatchLength::Short,MatchLength::Standard,MatchLength::Long}) {
   Config config=configFor(revision);config.map=map;config.playerCount=players;config.matchLength=length;
   Simulation simulation;simulation.reset(config);
   const auto& definition=mapDefinition(map,players,length,revision);
   check(simulation.usesAuthoredTerrain()==definition.authored(),"only migrated variants bind authored terrain");
   check(simulation.obstacles().size()==definition.obstacles.size(),"reset consumes the shared collision geometry");
   std::size_t entityIndex=0;
   for(int team=0;team<players;++team) {
    const auto& anchor=simulation.entities()[entityIndex++];
    check(anchor.id==entityIndex&&anchor.kind==Kind::Headquarters&&anchor.team==team&&
          distance(anchor.pos,definition.starts[team])<0.001f,"headquarters keeps team and entity ordering");
    for(Vec2 point:definition.sites[team].nodes) {
     const auto& node=simulation.entities()[entityIndex++];
     check(node.id==entityIndex&&node.kind==Kind::Resource&&distance(node.pos,point)<0.001f&&
           node.resource==definition.sites[team].nodeOre,"home nodes retain coordinates, reserves and entity ordering");
    }
    for(int worker=0;worker<5;++worker) {
     const auto& entity=simulation.entities()[entityIndex++];
     check(entity.id==entityIndex&&entity.kind==Kind::Worker&&entity.team==team&&entity.order==Order::Gather,
           "starting workers keep their identifiers and mining orders");
    }
   }
   for(std::size_t site=players;site<definition.sites.size();++site)for(Vec2 point:definition.sites[site].nodes) {
    const auto& node=simulation.entities()[entityIndex++];
    check(node.id==entityIndex&&node.kind==Kind::Resource&&distance(node.pos,point)<0.001f&&
          node.resource==definition.sites[site].nodeOre,"expansion ore follows shared site ordering");
   }
   check(entityIndex==simulation.entities().size(),"shared map accounts for every starting actor");
  }
 Simulation legacy;legacy.reset(configFor(0));
 check(distance(legacy.entities().front().pos,{600,600})<0.001f&&legacy.obstacles().size()==4,
       "revision zero preserves the original duel start and four barriers");
 Simulation authored;authored.reset(configFor());
 check(authored.usesAuthoredTerrain()&&authored.terrainHeight(authored.entities().front().pos)>0,
       "fresh flagship main base is on a gameplay plateau");
 auto& custom=const_cast<std::vector<Obstacle>&>(authored.obstacles());custom.clear();
 check(!authored.usesAuthoredTerrain()&&authored.terrainHeight(authored.entities().front().pos)==0,
       "custom obstacle replacement cannot silently inherit unrelated height rules");
 Config flatConfig=configFor(0);flatConfig.map=1;legacy.reset(flatConfig);flatConfig.mapRevision=1;authored.reset(flatConfig);
 check(legacy.entities().size()==authored.entities().size()&&legacy.stateHash()!=authored.stateHash(),
       "map revision participates in deterministic state even for an unmigrated variant");
}

void placementAndRampMovement() {
 auto simulation=quiet();const auto& map=mapDefinition(0,2,MatchLength::Standard);
 check(!map.ramps.empty(),"authored map has a ramp");const auto& ramp=map.ramps.front();
 const Vec2 middle=lerp(ramp.high,ramp.low,0.5f);
 simulation.debugSpawn(Kind::Kite,0,middle);
 std::string reason;
 check(simulation.visible(0,middle)&&!simulation.canPlace(0,Kind::Turret,middle,&reason)&&
       reason.find("flat ground")!=std::string::npos,"ramp blocks construction with an explicit terrain reason");
 const auto& plateau=map.plateaus.front();
 const Vec2 edge{plateau.bounds.center.x+plateau.bounds.half.x-20,plateau.bounds.center.y};
 simulation.debugSpawn(Kind::Kite,0,edge);
 check(!simulation.canPlace(0,Kind::Headquarters,edge,&reason),"a footprint cannot straddle a plateau boundary");
 bool flatSite=false;
 for(float x=plateau.bounds.center.x-plateau.bounds.half.x+160;x<plateau.bounds.center.x+plateau.bounds.half.x-160&&!flatSite;x+=70)
  for(float y=plateau.bounds.center.y-plateau.bounds.half.y+160;y<plateau.bounds.center.y+plateau.bounds.half.y-160&&!flatSite;y+=70)
   flatSite=simulation.canPlace(0,Kind::Foundry,{x,y});
 check(flatSite,"main terrace retains legal room for production buildings");

 const Vec2 start=lerp(ramp.high,ramp.low,-0.3f),goal=lerp(ramp.high,ramp.low,1.3f);
 const Id vehicle=simulation.debugSpawn(Kind::Bastion,0,start);
 check(simulation.command({CommandType::Move,0,{vehicle},goal}).accepted,"largest vehicle accepts a ramp traversal");
 advance(simulation,450);
 check(simulation.find(vehicle)&&distance(simulation.find(vehicle)->pos,goal)<45,
       "largest vehicle physically descends the authored ramp without getting stuck");
 check(simulation.terrainHeight(simulation.find(vehicle)->pos)<1,"vehicle reaches the lower gameplay surface");
}

void heightVisibilityAndCombat() {
 auto simulation=quiet();const auto& map=mapDefinition(0,2,MatchLength::Standard);
 const auto& ramp=map.ramps.back();
 const Vec2 upper=lerp(ramp.high,ramp.low,-0.04f),lower=lerp(ramp.high,ramp.low,1.01f);
 check(distance(upper,lower)<definition(Kind::Mortar).range,"visibility combat fixture stays within siege range");
 const Id mortar=simulation.debugSpawn(Kind::Mortar,0,lower);
 const Id target=simulation.debugSpawn(Kind::Scout,1,upper);
 check(simulation.command({CommandType::Hold,1,{target},{}}).accepted,"upper observer holds its position");
 check(simulation.visible(1,lower),"upper ground source reveals downhill");
 check(!simulation.visible(0,upper),"lower ground source cannot reveal the upper surface");
 check(!simulation.command({CommandType::Attack,0,{mortar},{},target}).accepted,
       "an explicit attack cannot target a hidden upper unit");
 const float before=simulation.find(target)->hp;advance(simulation,4);
 check(simulation.find(target)->hp==before,"unseen upper target takes no acquired siege damage");
 const Id aircraft=simulation.debugSpawn(Kind::Kite,0,lower);
 check(aircraft&&simulation.visible(0,upper),"air vision explicitly reveals the upper surface");
 check(simulation.command({CommandType::Attack,0,{mortar},{},target}).accepted,
       "shared air vision permits the same attack target");
 advance(simulation,4);
 check(simulation.find(target)->hp<before,"siege can fire once a spotter makes the target visible");

 auto rampVision=quiet();
 const Vec2 halfway=lerp(ramp.high,ramp.low,0.5f),nearBottom=lerp(ramp.high,ramp.low,0.9f);
 rampVision.debugSpawn(Kind::Scout,0,halfway);
 check(rampVision.visible(0,nearBottom)&&!rampVision.visible(0,upper),
       "mid-ramp ground vision reaches downhill and stays below the upper crest");

 auto edgeVision=quiet();const auto& firstPlateau=map.plateaus.front();
 const Vec2 edge{firstPlateau.bounds.center.x+firstPlateau.bounds.half.x-1,600};
 const Vec2 outside{edge.x+141,edge.y};
 const float cell=edgeVision.worldSize()/Simulation::FogSize;
 const Vec2 center{(std::floor(edge.x/cell)+0.5f)*cell,(std::floor(edge.y/cell)+0.5f)*cell};
 check(map.terrainHeight(center)==0&&map.terrainHeight(edge)>0,
       "edge fixture crosses a fog cell whose center is lower than part of its footprint");
 edgeVision.debugSpawn(Kind::Scout,1,outside);
 check(!edgeVision.visible(1,edge),"lower-centered fog cells do not leak their upper terrace corner");
 edgeVision.debugSpawn(Kind::Kite,1,outside);
 check(edgeVision.visible(1,edge),"air observer reveals the same mixed-height fog cell");
}

void adjacentAnchorRampVision() {
 const auto& map=mapDefinition(0,2,MatchLength::Standard);
 for(int side=0;side<2;++side) {
  auto simulation=quiet();const int observer=1-side;
  const auto mirror=[&](Vec2 point) {return side?Vec2{map.worldSize-point.x,map.worldSize-point.y}:point;};
  const Vec2 anchor=mirror({2260,950}),upper=mirror({1490,950});
  const std::vector<Vec2> slope={{1537.5f,1237.5f},{1770,1230},{1950,1230},{1987.5f,1237.5f},{2020,1230}};
  for(Vec2 point:slope)check(!simulation.visible(observer,mirror(point)),
                            "remote starting base does not reveal the ramp fixture");
  check(simulation.debugSpawn(Kind::Headquarters,observer,anchor)!=0,"adjacent Anchor spawns at the natural");
  int nearbySources=0;
  for(const auto& entity:simulation.entities())if(entity.alive()&&entity.team==observer&&
      entity.kind!=Kind::Resource&&distance(entity.pos,mirror({1950,1230}))<definition(entity.kind).vision+75)
   ++nearbySources;
  check(nearbySources==1,"the adjacent Anchor is the only source that can reveal the tested ramp");
  for(Vec2 point:slope) {
   point=mirror(point);
   check(map.onRamp(point)&&distance(anchor,point)<definition(Kind::Headquarters).vision,
         "ramp samples are on the slope and inside the adjacent Anchor's normal range");
   check(simulation.visible(observer,point),"adjacent low-ground Anchor reveals the gradual ramp, including its middle and foot");
  }
  check(map.terrainHeight(upper)>0&&!map.onRamp(upper)&&
        distance(anchor,upper)<definition(Kind::Headquarters).vision,
        "upper plateau privacy sample is within range and outside the ramp");
  check(!simulation.visible(observer,upper),"adjacent Anchor cannot reveal the actual upper plateau");

  auto remote=quiet();const Vec2 remoteAnchor=mirror({3300,950});
  remote.debugSpawn(Kind::Headquarters,observer,remoteAnchor);
  const Vec2 nearFoot=mirror({1987.5f,1237.5f});
  check(distance(remoteAnchor,nearFoot)>definition(Kind::Headquarters).vision+75&&
        !remote.visible(observer,nearFoot),"ramp visibility remains bounded by the observer's sight radius");

  simulation.debugSpawn(Kind::Kite,observer,lerp(anchor,upper,0.25f));
  check(simulation.visible(observer,upper),"air vision still reveals the actual upper plateau");
  auto high=quiet();high.debugSpawn(Kind::Scout,observer,upper);
  check(high.visible(observer,mirror({1950,1230})),"upper ground vision still reveals the downhill ramp");
 }
}

void movingRampObserverVision() {
 const auto& map=mapDefinition(0,2,MatchLength::Standard);
 for(std::size_t side=0;side<map.ramps.size();++side) {
  const auto& ramp=map.ramps[side];const int observer=1-static_cast<int>(side);
  auto simulation=quiet();const Vec2 top=lerp(ramp.high,ramp.low,-0.2f),bottom=lerp(ramp.high,ramp.low,1.2f);
  const Id scout=simulation.debugSpawn(Kind::Scout,observer,top);
  const float dx=ramp.low.x-ramp.high.x,dy=ramp.low.y-ramp.high.y,lengthSquared=dx*dx+dy*dy;
  for(Vec2 goal:{bottom,top}) {
   check(simulation.command({CommandType::Move,observer,{scout},goal}).accepted,"ramp observer accepts each traversal leg");
   int interiorSamples=0;
   for(int step=0;step<240;++step) {
    const Entity* unit=simulation.find(scout);check(unit&&unit->alive(),"ramp observer survives the traversal");
    const float along=((unit->pos.x-ramp.high.x)*dx+(unit->pos.y-ramp.high.y)*dy)/lengthSquared;
    const float across=std::abs((unit->pos.x-ramp.high.x)*-dy+(unit->pos.y-ramp.high.y)*dx)/std::sqrt(lengthSquared);
    // Exclude the crest's mixed plateau cell: the separate privacy test must
    // continue protecting any real upper surface in that shared cell.
    if(along>=0.2f&&along<=0.95f&&across<ramp.halfWidth-75) {
     ++interiorSamples;
     check(simulation.visible(observer,unit->pos),"ground observer has no invisible own-cell gaps while ascending or descending the ramp interior");
    }
    if(distance(unit->pos,goal)<25)break;
    simulation.update(Simulation::Step);
   }
   check(distance(simulation.find(scout)->pos,goal)<25&&interiorSamples>10,
         "both traversal directions cover the ramp interior and reach their destination");
  }
 }
}

void rampVisionPersistence() {
 const auto path=std::filesystem::temp_directory_path()/"cinderline-ramp-vision-roundtrip.save";
 const auto& ramp=mapDefinition(0,2,MatchLength::Standard).ramps.front();
 auto simulation=quiet();const int observer=1;
 const Id scout=simulation.debugSpawn(Kind::Scout,observer,lerp(ramp.high,ramp.low,0.4f));
 check(simulation.command({CommandType::Patrol,observer,{scout},lerp(ramp.high,ramp.low,1.2f)}).accepted,
       "roundtrip fixture starts a sustained ramp patrol");
 Simulation loaded;
 for(int step=0;step<120;++step) {
  if(step%20==0) {
   check(simulation.save(path.string())&&loaded.load(path.string()),"ramp vision survives repeated save/load roundtrips");
   check(simulation.stateHash()==loaded.stateHash(),"ramp save/load retains the exact authoritative state");
  }
  const auto* unit=simulation.find(scout);
  const float along=(unit->pos.x-ramp.high.x)/(ramp.low.x-ramp.high.x);
  if(along>=0.2f&&along<=0.95f)
   check(simulation.visible(observer,unit->pos)&&loaded.visible(observer,unit->pos),
         "a reloaded ground observer retains current ramp visibility");
  const float cell=simulation.worldSize()/Simulation::FogSize;
  for(int y=0;y<Simulation::FogSize;++y)for(int x=0;x<Simulation::FogSize;++x) {
   const Vec2 point{(x+0.5f)*cell,(y+0.5f)*cell};
   check(simulation.visible(observer,point)==loaded.visible(observer,point)&&
         simulation.explored(observer,point)==loaded.explored(observer,point),
         "reloaded ramp vision and exploration agree across the complete fog grid");
  }
  simulation.update(Simulation::Step);loaded.update(Simulation::Step);
  check(simulation.stateHash()==loaded.stateHash(),"ramp patrol remains deterministic after repeated reloads");
 }
 std::filesystem::remove(path);
}

void persistenceAndReplay() {
 const auto path=std::filesystem::temp_directory_path()/"cinderline-map-terrain-simulation.save";
 for(int revision:{0,CurrentMapRevision}) {
  Simulation simulation;simulation.reset(configFor(revision));
  const auto& map=mapDefinition(0,2,MatchLength::Standard,revision);
  Id worker=0;for(const auto& entity:simulation.entities())if(entity.team==0&&entity.kind==Kind::Worker){worker=entity.id;break;}
  advance(simulation,4);
  check(simulation.command({CommandType::Move,0,{worker},map.sites[2].center}).accepted,"recorded terrain movement accepted");
  advance(simulation,40);
  check(simulation.save(path.string()),"terrain match saves");
  const auto savedHash=simulation.stateHash();
  auto lines=readLines(path);check(lines.front()=="CINDERLINE 15","save declares revision-aware format");
  Simulation loaded;
  check(loaded.load(path.string())&&loaded.config().mapRevision==revision&&loaded.stateHash()==simulation.stateHash(),
        "save reload retains exact map revision and deterministic state");
  for(int i=0;i<40;++i) {
   simulation.update(Simulation::Step);loaded.update(Simulation::Step);
   check(simulation.stateHash()==loaded.stateHash(),"loaded terrain match continues deterministically");
  }
  Simulation replay;replay.reset(simulation.config());std::size_t next=0;
  while(replay.tick()<simulation.tick()) {
   while(next<simulation.recording().size()&&simulation.recording()[next].tick==replay.tick())
    check(replay.command(simulation.recording()[next++].command).accepted,"terrain recording replays");
   replay.update(Simulation::Step);
  }
  check(next==simulation.recording().size()&&replay.stateHash()==simulation.stateHash(),
        "recorded match reproduces its revision-specific state");
  if(revision==0) {
   // Version fourteen contains the same actor state, but predates map revisions.
   lines[0]="CINDERLINE 14";lines[1].erase(lines[1].find_last_of(' '));writeLines(path,lines);
   Simulation legacy;check(legacy.load(path.string())&&legacy.config().mapRevision==0,
                           "legacy save explicitly restores revision zero rather than new topology");
   check(legacy.stateHash()==savedHash,"legacy state retains its exact pre-revision gameplay hash");
  }
 }
 auto saved=quiet();check(saved.save(path.string()),"revision rejection fixture saves");
 auto lines=readLines(path);lines[1].erase(lines[1].find_last_of(' '));lines[1]+=" 99";writeLines(path,lines);
 auto untouched=quiet(0);const auto hash=untouched.stateHash();
 check(!untouched.load(path.string())&&untouched.stateHash()==hash,"unknown map revision fails atomically");
 std::filesystem::remove(path);
}

void authoredAIScouting() {
 Config config=configFor();config.ai=true;
 const auto& map=mapDefinition(config.map,config.playerCount,config.matchLength,config.mapRevision);
 const Vec2 scoutStart{map.starts[1].x-260,map.starts[1].y-200};
 Simulation early;early.reset(config);
 const Id earlyScout=early.debugSpawn(Kind::Scout,1,scoutStart);
 advance(early,60);
 const auto firstMove=std::find_if(early.recording().begin(),early.recording().end(),[&](const RecordedCommand& item) {
  return item.command.team==1&&item.command.type==CommandType::Move&&
         std::find(item.command.units.begin(),item.command.units.end(),earlyScout)!=item.command.units.end();
 });
 check(firstMove!=early.recording().end()&&distance(firstMove->command.point,map.starts[0])<0.01f,
       "AI reconnaissance targets the authored enemy start rather than the legacy corner");

 Simulation late;late.reset(config);
 for(int step=0;step<5020;++step) {late.debugResources(1,0);late.update(Simulation::Step);}
 const auto natural=std::find_if(map.sites.begin(),map.sites.end(),[](const MapResourceSite& site) {
  return site.owner==1&&site.role==MapSiteRole::Natural;
 });
 check(natural!=map.sites.end(),"AI scouting fixture has its authored natural expansion");
 const Id lateScout=late.debugSpawn(Kind::Scout,1,scoutStart);
 const auto recordingStart=late.recording().size();advance(late,60);
 const auto expansionMove=std::find_if(late.recording().begin()+recordingStart,late.recording().end(),[&](const RecordedCommand& item) {
  return item.command.team==1&&item.command.type==CommandType::Move&&distance(item.command.point,natural->center)<0.01f&&
         std::find(item.command.units.begin(),item.command.units.end(),lateScout)!=item.command.units.end();
 });
 check(expansionMove!=late.recording().end(),"AI expansion scout uses the shared natural site");
}
}

int main() {
 try {
  resetAndCompatibility();placementAndRampMovement();heightVisibilityAndCombat();
  adjacentAnchorRampVision();movingRampObserverVision();rampVisionPersistence();
  persistenceAndReplay();authoredAIScouting();
  std::cout<<"Authored map simulation checks passed\n";return 0;
 } catch(const std::exception& error) {
  std::cerr<<"FAIL "<<error.what()<<'\n';return 1;
 }
}
