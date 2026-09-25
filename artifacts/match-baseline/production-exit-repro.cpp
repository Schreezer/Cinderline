// Isolated synthetic fixture derived from the legal four-player enclosure.
// This is a diagnostic for the pending production-exit fix, not a passing test.
#include "Sim/Simulation.h"
#include <cmath>
#include <iostream>
#include <vector>

using namespace cinder;

int main() {
    Simulation simulation;
    Config config; config.ai = false;
    simulation.reset(config);
    auto& entities = const_cast<std::vector<Entity>&>(simulation.entities());
    entities.clear();
    const_cast<std::vector<Obstacle>&>(simulation.obstacles()).clear();
    simulation.debugSpawn(Kind::Headquarters, 0, {600, 4200});
    simulation.debugSpawn(Kind::Headquarters, 1, {4200, 600});
    simulation.debugSpawn(Kind::Processor, 0, {387.868f, 4412.13f});
    simulation.debugSpawn(Kind::Processor, 0, {431.619f, 4606.51f});
    simulation.debugSpawn(Kind::Processor, 0, {193.493f, 4368.38f});
    const Id producer = simulation.debugSpawn(Kind::MotorPool, 0, {196.949f, 4603.05f});
    simulation.debugResources(0, 10000);
    const_cast<Player&>(simulation.players()[0]).tier = 3;
    const Vec2 rally{1120, 3680};
    Command rallyCommand; rallyCommand.type = CommandType::Rally;
    rallyCommand.team = 0; rallyCommand.units = {producer}; rallyCommand.point = rally;
    if (!simulation.command(rallyCommand).accepted) return 2;
    Command train; train.type = CommandType::Train; train.team = 0;
    train.units = {producer}; train.kind = Kind::Mortar;
    const auto response = simulation.command(train);
    std::cout << "train accepted=" << response.accepted << " message=" << response.message << '\n';
    if (!response.accepted) return 2;
    Id trained = 0;
    for (int tick = 0; tick < 6000 && !trained; ++tick) {
        const auto before = simulation.tick();
        simulation.update(Simulation::Step);
        if (simulation.tick() != before + 1) return 2;
        for (const auto& entity : simulation.entities())
            if (entity.alive() && entity.kind == Kind::Mortar && entity.team == 0) trained = entity.id;
    }
    if (!trained) return 2;
    const Entity& unit = *simulation.find(trained);
    const Entity& facility = *simulation.find(producer);
    std::vector<NavCircle> circles;
    for (const auto& entity : simulation.entities())
        if (entity.alive() && definition(entity.kind).building)
            circles.push_back({entity.id, entity.pos, definition(entity.kind).radius});
    Navigation navigation; navigation.sync(simulation.worldSize(), {}, circles);
    const float radius = definition(unit.kind).radius;
    const auto chosen = navigation.route(unit.pos, {unit.goal}, radius);
    std::cout << "spawn=" << unit.pos.x << ',' << unit.pos.y
              << " goal=" << unit.goal.x << ',' << unit.goal.y
              << " locally_clear=" << navigation.pointClear(unit.pos, radius)
              << " route_reached=" << chosen.reached << " search_exhausted=" << chosen.exhausted << '\n';
    bool alternative = false;
    for (int ring = 0; ring < 6 && !alternative; ++ring) for (int spoke = 0; spoke < 16; ++spoke) {
        const float angle = std::atan2(rally.y - facility.pos.y, rally.x - facility.pos.x)
                          + spoke * 3.14159265358979323846f / 8;
        const float distance = definition(facility.kind).radius + radius + 28 + ring * 35;
        const Vec2 point{facility.pos.x + std::cos(angle) * distance,
                         facility.pos.y + std::sin(angle) * distance};
        if (!navigation.pointClear(point, radius)) continue;
        if (navigation.route(point, {unit.goal}, radius).reached) {
            alternative = true;
            std::cout << "reachable_alternate_exit=" << point.x << ',' << point.y
                      << " ring=" << ring << " spoke=" << spoke << '\n';
            break;
        }
    }
    const bool reproduced = !chosen.reached && !chosen.exhausted && alternative;
    std::cout << "unreachable_first_exit_with_reachable_alternative=" << reproduced << '\n';
    return reproduced ? 0 : 1;
}
