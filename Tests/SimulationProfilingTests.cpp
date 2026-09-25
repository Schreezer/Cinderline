#include "Sim/Network.h"
#include "Sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace cinder;

namespace {

void check(bool condition,const std::string& message) {
  if(!condition)throw std::runtime_error(message);
}

Id first(const Simulation& simulation,int team,Kind kind) {
  for(const auto& entity:simulation.entities())
    if(entity.alive()&&entity.team==team&&entity.kind==kind)return entity.id;
  throw std::runtime_error("profiling fixture entity is missing");
}

void checkCleared(const SimulationStepProfile& profile,const std::string& context) {
  check(!profile.collected&&profile.tick==0,context+" retains a collected sample");
  check(profile.setupMs==0&&profile.productionMs==0&&profile.movementEconomyMs==0&&
        profile.visionMs==0&&profile.combatMs==0&&profile.aiMs==0&&
        profile.completionMs==0&&profile.totalMs==0,
        context+" retains clock data");
}

void checkSample(const Simulation& simulation,const std::string& context) {
  const auto& profile=simulation.lastStepProfile();
  check(profile.collected,context+" did not collect an enabled sample");
  check(profile.tick==simulation.tick(),context+" reports a stale sample tick");
  const std::vector<double> phases{
    profile.setupMs,profile.productionMs,profile.movementEconomyMs,profile.visionMs,
    profile.combatMs,profile.aiMs,profile.completionMs};
  for(double value:phases)
    check(std::isfinite(value)&&value>=0,context+" contains an invalid phase duration");
  check(std::isfinite(profile.totalMs)&&profile.totalMs>=0,
        context+" contains an invalid total duration");
  const double measured=profile.setupMs+profile.productionMs+profile.movementEconomyMs+
    profile.visionMs+profile.combatMs+profile.aiMs+profile.completionMs;
  const double clockSlack=std::max(0.01,profile.totalMs*0.02);
  check(measured<=profile.totalMs+clockSlack,
        context+" phase durations exceed the enclosing step duration");
  check(std::fabs(profile.totalMs-simulation.lastStepMilliseconds())<=clockSlack,
        context+" total does not match the existing step duration");
}

bool sameCommand(const Command& left,const Command& right) {
  return left.type==right.type&&left.team==right.team&&left.units==right.units&&
    left.point.x==right.point.x&&left.point.y==right.point.y&&left.target==right.target&&
    left.kind==right.kind&&left.queueIndex==right.queueIndex&&left.queueMode==right.queueMode&&
    left.spacing==right.spacing&&left.hasArrivalFacing==right.hasArrivalFacing&&
    left.arrivalFacing==right.arrivalFacing;
}

void checkRecording(const Simulation& left,const Simulation& right) {
  check(left.recording().size()==right.recording().size(),
        "profiling changed the number of recorded commands");
  for(std::size_t index=0;index<left.recording().size();++index) {
    check(left.recording()[index].tick==right.recording()[index].tick&&
          sameCommand(left.recording()[index].command,right.recording()[index].command),
          "profiling changed a recorded command");
  }
}

std::vector<char> readBytes(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);
  check(input.good(),"profiling fixture output cannot be read");
  return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}

void profilingLifecycle() {
  Simulation simulation;
  check(!simulation.profilingEnabled(),"profiling is enabled by default");
  checkCleared(simulation.lastStepProfile(),"default simulation");

  const auto initialTick=simulation.tick();
  simulation.update(0);
  simulation.update(-Simulation::Step);
  check(simulation.tick()==initialTick,"zero or negative update advanced the simulation");
  checkCleared(simulation.lastStepProfile(),"disabled no-op update");

  simulation.setProfilingEnabled(true);
  check(simulation.profilingEnabled(),"profiling opt-in was not retained");
  checkCleared(simulation.lastStepProfile(),"profiling enable");
  simulation.update(0);
  simulation.update(-1);
  checkCleared(simulation.lastStepProfile(),"enabled no-op update");
  simulation.update(Simulation::Step);
  checkSample(simulation,"enabled step");
  const auto sample=simulation.lastStepProfile();
  simulation.update(0);
  simulation.update(-Simulation::Step);
  check(simulation.lastStepProfile().collected&&
        simulation.lastStepProfile().tick==sample.tick&&
        simulation.lastStepProfile().totalMs==sample.totalMs,
        "no-op update replaced the latest completed sample");

  const auto beforeDisabledStep=simulation.tick();
  simulation.setProfilingEnabled(false);
  check(!simulation.profilingEnabled(),"profiling disable was not retained");
  checkCleared(simulation.lastStepProfile(),"profiling disable");
  simulation.update(Simulation::Step);
  check(simulation.tick()==beforeDisabledStep+1,"disabled profiling stopped simulation time");
  checkCleared(simulation.lastStepProfile(),"disabled future step");

  simulation.setProfilingEnabled(true);
  simulation.update(Simulation::Step);
  checkSample(simulation,"pre-reset step");
  simulation.reset({0,77,false,1});
  check(simulation.profilingEnabled(),"reset discarded the profiling preference");
  checkCleared(simulation.lastStepProfile(),"reset");
  simulation.update(Simulation::Step);
  checkSample(simulation,"post-reset step");

  const auto savePath=std::filesystem::temp_directory_path()/"cinderline-profiling-load.sav";
  check(simulation.save(savePath.string()),"profiling load fixture saves");
  Simulation loadTarget;
  loadTarget.setProfilingEnabled(true);
  loadTarget.update(Simulation::Step);
  checkSample(loadTarget,"pre-load step");
  check(loadTarget.load(savePath.string()),"profiling load fixture loads");
  std::filesystem::remove(savePath);
  check(!loadTarget.profilingEnabled(),"load retained diagnostics from the replaced instance");
  checkCleared(loadTarget.lastStepProfile(),"load replacement");

  net::ViewMemory memory;
  const auto snapshot=net::snapshotFor(simulation,0,&memory);
  Simulation replica;
  replica.setProfilingEnabled(true);
  replica.update(Simulation::Step);
  checkSample(replica,"pre-snapshot step");
  std::string error;
  check(replica.applySnapshot(snapshot,&error),"profiling snapshot fixture applies: "+error);
  check(replica.isReplica(),"profiling snapshot fixture did not become a replica");
  check(!replica.profilingEnabled(),"snapshot retained diagnostics from the replaced instance");
  checkCleared(replica.lastStepProfile(),"snapshot replacement");
}

void profilingPreservesAuthoritativeSimulation() {
  const Config config{0,0x51A7u,true,1};
  Simulation control;
  Simulation profiled;
  control.reset(config);
  profiled.reset(config);
  const auto initialHash=profiled.stateHash();
  profiled.setProfilingEnabled(true);
  check(profiled.stateHash()==initialHash,"enabling profiling changed authoritative state");
  check(!control.profilingEnabled()&&profiled.profilingEnabled(),
        "profiling preference leaked between simulation instances");

  const Id controlHeadquarters=first(control,0,Kind::Headquarters);
  const Id profiledHeadquarters=first(profiled,0,Kind::Headquarters);
  const Id controlWorker=first(control,0,Kind::Worker);
  const Id profiledWorker=first(profiled,0,Kind::Worker);
  check(control.command({CommandType::Train,0,{controlHeadquarters},{},0,Kind::Worker,0}).accepted&&
        profiled.command({CommandType::Train,0,{profiledHeadquarters},{},0,Kind::Worker,0}).accepted,
        "paired authorities reject the same paid Train command");
  check(control.command({CommandType::Move,0,{controlWorker},{1250,1050},0,Kind::Worker,0}).accepted&&
        profiled.command({CommandType::Move,0,{profiledWorker},{1250,1050},0,Kind::Worker,0}).accepted,
        "paired authorities reject the same movement command");
  const auto initialRecordingCount=control.recording().size();

  for(int stepIndex=0;stepIndex<300;++stepIndex) {
    const auto controlTick=control.tick();
    const auto profiledTick=profiled.tick();
    control.update(Simulation::Step);
    profiled.update(Simulation::Step);
    check(control.tick()==controlTick+1&&profiled.tick()==profiledTick+1,
          "paired profiling fixture stopped advancing");
    check(control.winner()==profiled.winner()&&control.stateHash()==profiled.stateHash(),
          "profiling changed an authoritative per-tick result");
    checkRecording(control,profiled);
    checkCleared(control.lastStepProfile(),"unprofiled twin");
    checkSample(profiled,"profiled twin");
  }

  check(control.players()[0].stats.produced>0&&profiled.players()[0].stats.produced>0,
        "paired profiling fixture did not complete paid Worker production");
  const bool controlAIRecorded=std::any_of(control.recording().begin()+initialRecordingCount,
    control.recording().end(),[](const RecordedCommand& item){return item.command.team==1;});
  const bool profiledAIRecorded=std::any_of(profiled.recording().begin()+initialRecordingCount,
    profiled.recording().end(),[](const RecordedCommand& item){return item.command.team==1;});
  check(controlAIRecorded&&profiledAIRecorded&&control.recording().size()>initialRecordingCount,
        "normal AI produced no recorded command during the profiled match");

  const auto controlPath=std::filesystem::temp_directory_path()/"cinderline-profiling-control.sav";
  const auto profiledPath=std::filesystem::temp_directory_path()/"cinderline-profiling-enabled.sav";
  check(control.save(controlPath.string())&&profiled.save(profiledPath.string()),
        "paired profiling fixture saves");
  check(readBytes(controlPath)==readBytes(profiledPath),
        "profiling changed authoritative save bytes");
  Simulation loadedControl;
  Simulation loadedProfiled;
  check(loadedControl.load(controlPath.string())&&loadedProfiled.load(profiledPath.string()),
        "paired profiling saves reload");
  std::filesystem::remove(controlPath);
  std::filesystem::remove(profiledPath);
  check(loadedControl.stateHash()==loadedProfiled.stateHash()&&
        loadedControl.stateHash()==control.stateHash(),
        "profiling changed save-load continuation state");
  check(!loadedControl.profilingEnabled()&&!loadedProfiled.profilingEnabled(),
        "saved diagnostics leaked into loaded simulations");
  checkCleared(loadedControl.lastStepProfile(),"loaded control");
  checkCleared(loadedProfiled.lastStepProfile(),"loaded profiled authority");

  net::ViewMemory memory;
  const auto controlBytes=net::encodeSnapshot(net::snapshotFor(control,0,&memory));
  const auto profiledBytes=net::encodeSnapshot(net::snapshotFor(profiled,0,&memory));
  check(!controlBytes.empty()&&controlBytes==profiledBytes,
        "profiling changed fog-filtered snapshot bytes");
}

} // namespace

int main() {
  const std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"profiling lifecycle",profilingLifecycle},
    {"authoritative profiling invariance",profilingPreservesAuthoritativeSimulation},
  };
  int failed=0;
  for(const auto& [name,test]:tests) {
    try {test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
  }
  std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
