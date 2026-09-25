#include "Sim/Simulation.h"

#include <cmath>
#include <iostream>

using namespace cinder;

namespace {

float distance(Vec2 a,Vec2 b) { return std::hypot(a.x-b.x,a.y-b.y); }

void print(const Simulation& simulation,Id id) {
  const Entity* entity=simulation.find(id);
  if(!entity) { std::cout<<" missing="<<id; return; }
  std::cout<<" id="<<id<<" pos="<<entity->pos.x<<','<<entity->pos.y
           <<" goal_distance="<<distance(entity->pos,entity->goal)
           <<" order="<<static_cast<int>(entity->order)
           <<" path="<<entity->pathIndex<<'/'<<entity->path.size()
           <<" stalled="<<entity->stalledFor<<" failures="<<entity->navigationFailures
           <<" yield="<<entity->yieldFor<<" exhausted="<<entity->navigationExhausted;
}

} // namespace

int main(int argc,char** argv) {
  if(argc!=2) { std::cerr<<"usage: crowd_stall_continuation SAVE\n"; return 2; }
  Simulation simulation;
  if(!simulation.load(argv[1])) { std::cerr<<"load failed\n"; return 2; }
  std::cout<<"start tick="<<simulation.tick()<<" hash="<<simulation.stateHash();
  print(simulation,186); print(simulation,262); std::cout<<'\n';
  for(int step=1;step<=2400;++step) {
    simulation.update(Simulation::Step);
    if(step%400==0) {
      std::cout<<"after_steps="<<step<<" tick="<<simulation.tick();
      print(simulation,186); print(simulation,262); std::cout<<'\n';
    }
  }
  return 0;
}
