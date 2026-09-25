// Pending P0.1 regression: completing a direct attack should not walk to its corpse.
#include "Sim/Simulation.h"
#include <cmath>
#include <iostream>
using namespace cinder;
int main() {
    Simulation simulation;
    simulation.reset({0,4242,false,1});
    const_cast<std::vector<Entity>&>(simulation.entities()).clear();
    const_cast<std::vector<Obstacle>&>(simulation.obstacles()).clear();
    simulation.debugSpawn(Kind::Headquarters,0,{700,700});
    simulation.debugSpawn(Kind::Headquarters,1,{4400,4400});
    const Id attacker=simulation.debugSpawn(Kind::Striker,0,{1400,1500});
    const Id target=simulation.debugSpawn(Kind::Worker,1,{1650,1500});
    if(!simulation.command({CommandType::Hold,1,{target}}).accepted ||
       !simulation.command({CommandType::Attack,0,{attacker},{},target}).accepted) return 2;
    for(int step=0;step<400;++step) {
        const auto* victim=simulation.find(target);
        if(!victim||!victim->alive())break;
        simulation.update(Simulation::Step);
    }
    const auto* victim=simulation.find(target);
    if(victim&&victim->alive())return 2;
    const Vec2 atKill=simulation.find(attacker)->pos;
    for(int step=0;step<20;++step)simulation.update(Simulation::Step);
    const auto* unit=simulation.find(attacker);
    const float drift=std::hypot(unit->pos.x-atKill.x,unit->pos.y-atKill.y);
    std::cout<<"position_at_kill="<<atKill.x<<','<<atKill.y
        <<" position_after_one_second="<<unit->pos.x<<','<<unit->pos.y
        <<" goal="<<unit->goal.x<<','<<unit->goal.y
        <<" order="<<static_cast<int>(unit->order)<<" drift="<<drift<<'\n';
    return drift>1.0f?1:0;
}
