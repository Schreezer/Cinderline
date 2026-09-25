#include "Sim/Network.h"
#include "Sim/Simulation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace cinder;

namespace {

constexpr std::array<int,3> UnitCounts{{16,160,200}};
constexpr std::array<Kind,8> MobileMix{{
  Kind::Striker,Kind::Lancer,Kind::Scout,Kind::Bastion,
  Kind::Mortar,Kind::Mender,Kind::Kite,Kind::Worker,
}};

struct Distribution {
  double mean=0,p50=0,p95=0,maximum=0;
};

struct SnapshotStage {
  std::string name;
  std::size_t tailDepth=0;
  std::size_t futureSteps=0;
  std::size_t bytes=0;
  std::size_t encodedEntities=0;
  bool holdPreserved=false;
  bool pointsImmutable=false;
};

struct RunResult {
  int unitCount=0;
  bool valid=true;
  bool seventeenthRejected=false;
  bool rejectionStateUnchanged=false;
  bool rejectionRecordingUnchanged=false;
  std::uint64_t stateHash=0;
  std::size_t recordedCommands=0;
  double rejectionWallMs=0;
  std::vector<double> appendWallMs;
  std::array<SnapshotStage,3> stages{{
    {"empty",0,0,0,0,false,false},
    {"half",8,0,0,0,false,false},
    {"full",Simulation::MaxFutureOrders,0,0,0,false,false},
  }};
  std::vector<RecordedCommand> recording;
  std::vector<std::string> errors;
};

std::vector<Entity>& entities(Simulation& simulation) {
  return const_cast<std::vector<Entity>&>(simulation.entities());
}

std::vector<Obstacle>& obstacles(Simulation& simulation) {
  return const_cast<std::vector<Obstacle>&>(simulation.obstacles());
}

void require(RunResult& result,bool condition,const std::string& message) {
  if(!condition){result.valid=false;result.errors.push_back(message);}
}

Simulation emptyFixture(std::uint32_t seed) {
  Simulation simulation;
  simulation.reset({0,seed,false,1});
  entities(simulation).clear();
  obstacles(simulation).clear();
  simulation.debugSpawn(Kind::Headquarters,0,{250,250});
  simulation.debugSpawn(Kind::Headquarters,1,{4550,4550});
  return simulation;
}

std::vector<Id> spawnOwnedMix(Simulation& simulation,int count) {
  std::vector<Id> units;
  units.reserve(static_cast<std::size_t>(count));
  constexpr int Columns=20;
  for(int index=0;index<count;++index) {
    const Vec2 position{500.0f+76.0f*static_cast<float>(index%Columns),
                        500.0f+76.0f*static_cast<float>(index/Columns)};
    units.push_back(simulation.debugSpawn(
      MobileMix[static_cast<std::size_t>(index)%MobileMix.size()],0,position));
  }
  return units;
}

Vec2 waypoint(int index) {
  return {2600.0f+300.0f*static_cast<float>(index%4),
          1700.0f+350.0f*static_cast<float>(index/4)};
}

bool samePoint(Vec2 left,Vec2 right) {
  return left.x==right.x&&left.y==right.y;
}

bool sameOrder(const TacticalOrder& left,const TacticalOrder& right) {
  return left.order==right.order&&samePoint(left.point,right.point)&&
         left.supportTarget==right.supportTarget;
}

bool sameCommand(const Command& left,const Command& right) {
  return left.type==right.type&&left.team==right.team&&left.units==right.units&&
         samePoint(left.point,right.point)&&left.target==right.target&&left.kind==right.kind&&
         left.queueIndex==right.queueIndex&&left.queueMode==right.queueMode&&
         left.spacing==right.spacing&&left.hasArrivalFacing==right.hasArrivalFacing&&
         left.arrivalFacing==right.arrivalFacing;
}

bool sameRecording(const std::vector<RecordedCommand>& left,
                   const std::vector<RecordedCommand>& right) {
  if(left.size()!=right.size())return false;
  for(std::size_t index=0;index<left.size();++index)
    if(left[index].tick!=right[index].tick||
       !sameCommand(left[index].command,right[index].command))return false;
  return true;
}

void captureStage(RunResult& result,std::size_t stageIndex,const Simulation& simulation,
                  const std::vector<Id>& units,
                  const std::vector<std::vector<TacticalOrder>>& accepted,
                  net::ViewMemory& memory) {
  SnapshotStage& stage=result.stages.at(stageIndex);
  bool hold=true,immutable=true;
  std::size_t futureSteps=0;
  for(std::size_t unitIndex=0;unitIndex<units.size();++unitIndex) {
    const Entity* entity=simulation.find(units[unitIndex]);
    if(!entity){hold=false;immutable=false;continue;}
    hold=hold&&entity->order==Order::Hold&&entity->supportTarget==0;
    immutable=immutable&&entity->futureOrders.size()==stage.tailDepth&&
              accepted[unitIndex].size()==stage.tailDepth;
    const std::size_t comparable=std::min(entity->futureOrders.size(),accepted[unitIndex].size());
    for(std::size_t order=0;order<comparable;++order)
      immutable=immutable&&sameOrder(entity->futureOrders[order],accepted[unitIndex][order]);
    futureSteps+=entity->futureOrders.size();
  }
  stage.futureSteps=futureSteps;
  stage.holdPreserved=hold;
  stage.pointsImmutable=immutable;

  const net::Snapshot snapshot=net::snapshotFor(simulation,0,&memory);
  const auto bytes=net::encodeSnapshot(snapshot);
  stage.bytes=bytes.size();
  stage.encodedEntities=snapshot.entities.size();
  require(result,!bytes.empty()&&bytes.size()<=net::MaxMessageBytes,
          stage.name+" tactical snapshot encodes within the frame limit");

  std::vector<const Entity*> ownedMobiles;
  for(const auto& entity:snapshot.entities)
    if(entity.alive()&&entity.team==0&&!definition(entity.kind).building&&entity.kind!=Kind::Resource)
      ownedMobiles.push_back(&entity);
  require(result,ownedMobiles.size()==units.size(),
          stage.name+" snapshot retains every owned mobile unit");
  const std::size_t comparable=std::min(ownedMobiles.size(),units.size());
  for(std::size_t index=0;index<comparable;++index) {
    const Entity* source=simulation.find(units[index]);
    const Entity* encoded=ownedMobiles[index];
    require(result,source&&encoded->kind==source->kind&&samePoint(encoded->pos,source->pos)&&
            encoded->order==Order::Hold&&encoded->futureOrders.size()==stage.tailDepth,
            stage.name+" snapshot preserves held owned tactical state");
    const std::size_t orderCount=source?
      std::min(source->futureOrders.size(),encoded->futureOrders.size()):0;
    for(std::size_t order=0;order<orderCount;++order)
      require(result,encoded->futureOrders[order].order==source->futureOrders[order].order&&
              samePoint(encoded->futureOrders[order].point,source->futureOrders[order].point)&&
              ((encoded->futureOrders[order].supportTarget==0)==
               (source->futureOrders[order].supportTarget==0)),
              stage.name+" snapshot preserves immutable points and support presence");
  }
  require(result,stage.futureSteps==units.size()*stage.tailDepth,
          stage.name+" stage has the exact aggregate tactical depth");
  require(result,hold&&immutable,
          stage.name+" stage preserves Hold and every earlier accepted point");
}

RunResult run(int unitCount,bool collectTimings) {
  RunResult result;
  result.unitCount=unitCount;
  Simulation simulation=emptyFixture(0xA900u+static_cast<std::uint32_t>(unitCount));
  const std::vector<Id> units=spawnOwnedMix(simulation,unitCount);
  require(result,units.size()==static_cast<std::size_t>(unitCount)&&
          std::all_of(units.begin(),units.end(),[](Id id){return id!=0;}),
          "synthetic roster spawns every requested mobile unit");
  require(result,std::any_of(units.begin(),units.end(),[&](Id id) {
            const Entity* entity=simulation.find(id);return entity&&entity->kind==Kind::Mender;
          }),"synthetic mixed roster includes Menders");

  const CommandResult held=simulation.command(
    {CommandType::Hold,0,units,{},0,Kind::Worker,0,CommandQueueMode::Replace});
  require(result,held.accepted,"mixed roster accepts its current Hold order");
  std::vector<std::vector<TacticalOrder>> accepted(units.size());
  net::ViewMemory memory;
  captureStage(result,0,simulation,units,accepted,memory);

  for(std::size_t order=0;order<Simulation::MaxFutureOrders;++order) {
    Command command;
    command.type=order%2==0?CommandType::Move:CommandType::AttackMove;
    command.team=0;
    command.units=units;
    command.point=waypoint(static_cast<int>(order));
    command.queueMode=CommandQueueMode::Append;
    const auto started=std::chrono::steady_clock::now();
    const CommandResult response=simulation.command(command);
    const double elapsed=std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-started).count();
    if(collectTimings)result.appendWallMs.push_back(elapsed);
    require(result,response.accepted,"append "+std::to_string(order+1)+" is accepted");
    if(!response.accepted)break;
    for(std::size_t unitIndex=0;unitIndex<units.size();++unitIndex) {
      const Entity* entity=simulation.find(units[unitIndex]);
      require(result,entity&&entity->futureOrders.size()==order+1,
              "accepted append advances every recipient to the same depth");
      if(entity&&entity->futureOrders.size()==order+1)
        accepted[unitIndex].push_back(entity->futureOrders.back());
    }
    if(order+1==8)captureStage(result,1,simulation,units,accepted,memory);
  }
  if(accepted.front().size()==Simulation::MaxFutureOrders)
    captureStage(result,2,simulation,units,accepted,memory);
  else require(result,false,"full tactical stage was not reached");

  const auto stateBeforeRejection=simulation.stateHash();
  const auto recordingBeforeRejection=simulation.recording().size();
  Command overflow;
  overflow.type=CommandType::AttackMove;
  overflow.team=0;
  overflow.units=units;
  overflow.point={3900,3150};
  overflow.queueMode=CommandQueueMode::Append;
  const auto rejectedStarted=std::chrono::steady_clock::now();
  const CommandResult rejected=simulation.command(overflow);
  const double rejectedElapsed=std::chrono::duration<double,std::milli>(
    std::chrono::steady_clock::now()-rejectedStarted).count();
  if(collectTimings)result.rejectionWallMs=rejectedElapsed;
  result.seventeenthRejected=!rejected.accepted;
  result.rejectionStateUnchanged=simulation.stateHash()==stateBeforeRejection;
  result.rejectionRecordingUnchanged=simulation.recording().size()==recordingBeforeRejection;
  require(result,result.seventeenthRejected,"seventeenth group append is rejected");
  require(result,result.rejectionStateUnchanged,
          "rejected group append preserves the exact authoritative state hash");
  require(result,result.rejectionRecordingUnchanged,
          "rejected group append records no partial command");
  require(result,result.stages[2].futureSteps<=Simulation::MaxFutureOrdersPerPlayer,
          "full synthetic tail remains below the unchanged per-player aggregate cap");

  result.stateHash=simulation.stateHash();
  result.recordedCommands=simulation.recording().size();
  result.recording=simulation.recording();
  require(result,result.recordedCommands==1+Simulation::MaxFutureOrders,
          "recording contains Hold and exactly sixteen accepted appends");
  return result;
}

Distribution distribution(std::vector<double> values) {
  Distribution result;
  if(values.empty())return result;
  std::sort(values.begin(),values.end());
  for(double value:values)result.mean+=value;
  result.mean/=values.size();
  auto percentile=[&](double fraction) {
    const auto rank=static_cast<std::size_t>(std::ceil(fraction*values.size()));
    return values[std::min(values.size()-1,std::max<std::size_t>(1,rank)-1)];
  };
  result.p50=percentile(0.50);result.p95=percentile(0.95);result.maximum=values.back();
  return result;
}

std::string hex(std::uint64_t value) {
  std::ostringstream output;
  output<<"0x"<<std::hex<<std::setw(16)<<std::setfill('0')<<value;
  return output.str();
}

std::string escaped(const std::string& value) {
  std::string result;
  for(char character:value) {
    if(character=='"'||character=='\\')result.push_back('\\');
    if(character=='\n')result+="\\n";else result.push_back(character);
  }
  return result;
}

void writeDistribution(const std::vector<double>& values) {
  if(values.empty()){std::cout<<"null";return;}
  const Distribution stats=distribution(values);
  std::cout<<"{\"samples\": "<<values.size()<<", \"mean\": "<<stats.mean
           <<", \"p50\": "<<stats.p50<<", \"p95\": "<<stats.p95
           <<", \"max\": "<<stats.maximum<<'}';
}

void printResult(const RunResult& measured,const RunResult& repeat,bool last) {
  const bool stateDeterministic=measured.stateHash==repeat.stateHash;
  const bool recordingDeterministic=sameRecording(measured.recording,repeat.recording);
  const bool success=measured.valid&&repeat.valid&&stateDeterministic&&recordingDeterministic;
  std::cout<<"    {\n"
           <<"      \"unit_count\": "<<measured.unitCount<<",\n"
           <<"      \"future_steps_at_full\": "<<measured.stages[2].futureSteps<<",\n"
           <<"      \"append_acceptance_wall_ms\": ";
  writeDistribution(measured.appendWallMs);
  std::cout<<",\n      \"seventeenth_rejection_wall_ms\": "<<measured.rejectionWallMs<<",\n"
           <<"      \"snapshots\": [\n";
  for(std::size_t index=0;index<measured.stages.size();++index) {
    const SnapshotStage& stage=measured.stages[index];
    std::cout<<"        {\"tail\": \""<<stage.name<<"\", \"depth_per_unit\": "<<stage.tailDepth
             <<", \"future_steps\": "<<stage.futureSteps<<", \"bytes\": "<<stage.bytes
             <<", \"encoded_entities\": "<<stage.encodedEntities
             <<", \"hold_preserved\": "<<(stage.holdPreserved?"true":"false")
             <<", \"accepted_points_immutable\": "<<(stage.pointsImmutable?"true":"false")<<'}'
             <<(index+1==measured.stages.size()?"\n":",\n");
  }
  std::cout<<"      ],\n"
           <<"      \"seventeenth_group_append_rejected\": "<<(measured.seventeenthRejected?"true":"false")<<",\n"
           <<"      \"rejection_state_hash_unchanged\": "<<(measured.rejectionStateUnchanged?"true":"false")<<",\n"
           <<"      \"rejection_recording_count_unchanged\": "<<(measured.rejectionRecordingUnchanged?"true":"false")<<",\n"
           <<"      \"state_hash\": \""<<hex(measured.stateHash)<<"\",\n"
           <<"      \"repeat_state_hash\": \""<<hex(repeat.stateHash)<<"\",\n"
           <<"      \"deterministic_state\": "<<(stateDeterministic?"true":"false")<<",\n"
           <<"      \"recorded_commands\": "<<measured.recordedCommands<<",\n"
           <<"      \"repeat_recorded_commands\": "<<repeat.recordedCommands<<",\n"
           <<"      \"deterministic_recording\": "<<(recordingDeterministic?"true":"false")<<",\n"
           <<"      \"valid\": "<<(success?"true":"false")<<",\n"
           <<"      \"errors\": [";
  bool wrote=false;
  for(const RunResult* result:{&measured,&repeat})for(const auto& error:result->errors) {
    if(wrote)std::cout<<", ";
    std::cout<<'"'<<escaped(error)<<'"';wrote=true;
  }
  if(!stateDeterministic) {
    if(wrote)std::cout<<", ";
    std::cout<<"\"repeat state hash differs\"";wrote=true;
  }
  if(!recordingDeterministic) {
    if(wrote)std::cout<<", ";
    std::cout<<"\"repeat recording differs\"";
  }
  std::cout<<"]\n    }"<<(last?"\n":",\n");
}

} // namespace

int main() {
  std::cout<<std::fixed<<std::setprecision(6);
  std::vector<std::pair<RunResult,RunResult>> results;
  bool success=true;
  for(int count:UnitCounts) {
    RunResult measured=run(count,true);
    RunResult repeat=run(count,false);
    success=success&&measured.valid&&repeat.valid&&measured.stateHash==repeat.stateHash&&
            sameRecording(measured.recording,repeat.recording);
    results.push_back({std::move(measured),std::move(repeat)});
  }
  std::cout<<"{\n"
           <<"  \"schema\": \"cinderline.tactical_order_baseline.v1\",\n"
           <<"  \"success\": "<<(success?"true":"false")<<",\n"
           <<"  \"benchmark_scope\": \"synthetic_debug_spawn_command_and_snapshot_cpu\",\n"
           <<"  \"release_build_required\": true,\n"
           <<"  \"paid_production_workload\": false,\n"
           <<"  \"device_or_render_workload\": false,\n"
           <<"  \"simulation_ticks_advanced\": 0,\n"
           <<"  \"timings_excluded_from_determinism_checks\": true,\n"
           <<"  \"future_orders_per_unit\": "<<Simulation::MaxFutureOrders<<",\n"
           <<"  \"future_orders_per_player_cap\": "<<Simulation::MaxFutureOrdersPerPlayer<<",\n"
           <<"  \"snapshot_frame_byte_limit\": "<<net::MaxMessageBytes<<",\n"
           <<"  \"mixed_mobile_kinds\": [";
  for(std::size_t index=0;index<MobileMix.size();++index) {
    if(index)std::cout<<", ";
    std::cout<<'"'<<definition(MobileMix[index]).name<<'"';
  }
  std::cout<<"],\n  \"workloads\": [\n";
  for(std::size_t index=0;index<results.size();++index)
    printResult(results[index].first,results[index].second,index+1==results.size());
  std::cout<<"  ]\n}\n";
  return success?0:1;
}
