#include "Sim/Simulation.h"
#include <iostream>
using namespace cinder;
int main() {
 Simulation s;s.reset({0,4242,false,1});
 const_cast<std::vector<Entity>&>(s.entities()).clear();
 const_cast<std::vector<Obstacle>&>(s.obstacles())={{{2050,1500},{250,450}}};
 s.debugSpawn(Kind::Headquarters,0,{700,700});s.debugSpawn(Kind::Headquarters,1,{4400,4400});
 auto soldier=s.debugSpawn(Kind::Striker,0,{1400,1500});
 s.debugSpawn(Kind::Scout,0,{1600,1500});
 auto flyer=s.debugSpawn(Kind::Kite,1,{2050,1500});
 s.update(Simulation::Step);
 auto ack=s.command({CommandType::Attack,0,{soldier},{},flyer});
 for(int i=0;i<20;++i)s.update(Simulation::Step);
 std::cout<<"accepted="<<ack.accepted<<" blocked="<<s.find(soldier)->navigationExhausted<<" pos="<<s.find(soldier)->pos.x<<","<<s.find(soldier)->pos.y<<"\n";
 s.command({CommandType::Move,1,{flyer},{1650,1100}});
 for(int i=0;i<140;++i)s.update(Simulation::Step);
 auto a=s.find(soldier);auto b=s.find(flyer);
 std::cout<<"soldier="<<a->pos.x<<","<<a->pos.y<<" exhausted="<<a->navigationExhausted<<" flyer="<<b->pos.x<<","<<b->pos.y<<" hp="<<b->hp<<" searches="<<s.navigationStats().searches<<"\n";
}
