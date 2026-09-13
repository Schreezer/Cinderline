#include "Sim/Simulation.h"

#include <cstdint>
#include <iostream>

int main() {
    constexpr int SampleSteps = 800;
    constexpr int SampleCount = 7;
    for (int map = 0; map < 3; ++map) {
        cinder::Simulation simulation;
        simulation.reset({map, 77000u + static_cast<std::uint32_t>(map), true, 1.0f});
        std::cout << "map=" << map << " tick=" << simulation.tick()
                  << " hash=" << simulation.stateHash() << '\n';
        for (int sample = 1; sample <= SampleCount; ++sample) {
            for (int step = 0; step < SampleSteps; ++step) {
                simulation.update(cinder::Simulation::Step);
            }
            std::cout << "map=" << map << " tick=" << simulation.tick()
                      << " hash=" << simulation.stateHash() << '\n';
        }
    }
}
