#include "Sim/AIDifficulty.h"
#include "Sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cinder;

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void advance(Simulation& simulation, float seconds) {
    const int steps = static_cast<int>(std::ceil(seconds / Simulation::Step));
    for (int step = 0; step < steps; ++step) simulation.update(Simulation::Step);
}

bool combatKind(Kind kind) {
    return kind >= Kind::Striker && kind <= Kind::Kite;
}

int count(const Simulation& simulation, Kind kind, bool completeOnly = false) {
    return static_cast<int>(std::count_if(simulation.entities().begin(), simulation.entities().end(),
        [&](const Entity& entity) {
            return entity.alive() && entity.team == 1 && entity.kind == kind &&
                   (!completeOnly || entity.progress >= 1.0f);
        }));
}

std::vector<Kind> combatTraining(const Simulation& simulation, std::size_t from = 0) {
    std::vector<Kind> trained;
    for (std::size_t i = from; i < simulation.recording().size(); ++i) {
        const Command& command = simulation.recording()[i].command;
        if (command.team == 1 && command.type == CommandType::Train && combatKind(command.kind)) {
            trained.push_back(command.kind);
        }
    }
    return trained;
}

Simulation hard(std::uint32_t seed = 7300) {
    Simulation simulation;
    simulation.reset({0, seed, true, aiDifficultyAggression(AIDifficulty::Hard)});
    return simulation;
}

// This entire scenario uses the real opening, ore income and command prices.
// A save after substantial development also exercises resumed economic decisions.
void paidOpeningScalesAndKeepsProducing() {
    Simulation simulation = hard();
    advance(simulation, 180.0f);
    const auto path = std::filesystem::temp_directory_path() / "cinderline-hard-economy-7300.sav";
    check(simulation.save(path.string()), "developed Hard economy could not save");
    Simulation restored;
    const bool loaded = restored.load(path.string());
    std::filesystem::remove(path);
    check(loaded, "developed Hard economy could not load");
    check(restored.stateHash() == simulation.stateHash(), "developed economy changed across save/load");
    for (int interval = 0; interval < 30; ++interval) {
        advance(simulation, 2.0f);
        advance(restored, 2.0f);
        check(restored.stateHash() == simulation.stateHash(),
              "loaded Hard economic decisions diverged from the uninterrupted match");
    }
    const int foundries = count(simulation, Kind::Foundry, true);
    const int workers = count(simulation, Kind::Worker);
    const std::size_t purchases = combatTraining(simulation).size();
    std::cout << "EVIDENCE paid_hard_240s foundries=" << foundries << " workers=" << workers
              << " combat_purchases=" << purchases << " ore=" << simulation.players()[1].ore
              << " gathered=" << simulation.players()[1].stats.gathered << '\n';
    check(foundries >= 3, "paid Hard did not complete three infantry producers by four minutes");
    check(workers >= 12, "paid Hard did not establish its worker economy");
    check(purchases >= 14, "paid Hard did not turn its ordinary income into a fighting army");

    const std::size_t nextCommand = simulation.recording().size();
    advance(simulation, 90.0f);
    const std::size_t reinforcementPurchases = combatTraining(simulation, nextCommand).size();
    std::cout << "EVIDENCE paid_hard_reinforcement observed_seconds=" << simulation.time()
              << " winner=" << simulation.winner() << " additional_combat_purchases=" << reinforcementPurchases
              << " foundries=" << count(simulation, Kind::Foundry, true)
              << " expansions=" << simulation.players()[1].stats.expansions << '\n';
    check(reinforcementPurchases >= 4 || (simulation.winner() == 1 && reinforcementPurchases > 0),
          "Hard stopped purchasing reinforcements after its first army");
    check(simulation.players()[1].ore >= 0, "Hard spent resources it did not own");
}

void pendingTierDoesNotReserveItsCostAgain() {
    Simulation simulation = hard(7301);
    // Advance the real clock without allowing purchases; no private state or save
    // fields are changed. Infrastructure below isolates the pending-tier decision.
    for (int step = 0; step < static_cast<int>(220.0f / Simulation::Step); ++step) {
        simulation.debugResources(1, 0);
        simulation.update(Simulation::Step);
    }
    simulation.debugSpawn(Kind::Foundry, 1, {3850, 4450});
    simulation.debugSpawn(Kind::Foundry, 1, {4150, 4550});
    simulation.debugSpawn(Kind::Processor, 1, {4500, 4500});
    simulation.debugSpawn(Kind::Processor, 1, {4550, 3950});
    simulation.debugSpawn(Kind::Processor, 1, {3850, 4000});
    simulation.debugSpawn(Kind::Turret, 1, {4400, 4200});
    const Id laboratory = simulation.debugSpawn(Kind::Laboratory, 1, {4100, 3850});
    for (int i = count(simulation, Kind::Worker); i < 14; ++i) {
        simulation.debugSpawn(Kind::Worker, 1, {3950.0f + (i % 5) * 45.0f, 4250.0f + (i / 5) * 45.0f});
    }
    for (int i = 0; i < 8; ++i) {
        simulation.debugSpawn(Kind::Striker, 1, {3750.0f + i * 48.0f, 3700});
    }
    simulation.debugResources(1, 800);
    Command research;
    research.type = CommandType::Research;
    research.team = 1;
    research.units = {laboratory};
    research.queueIndex = 0;
    check(simulation.command(research).accepted, "pending tier fixture could not buy ordinary research");
    check(simulation.players()[1].ore == 300, "tier research did not charge its ordinary 500 ore");
    const std::size_t nextCommand = simulation.recording().size();
    advance(simulation, 2.1f);
    const auto* lab = simulation.find(laboratory);
    check(lab && !lab->queue.empty() && lab->queue.front().research && simulation.players()[1].tier == 1,
          "tier research must still be underway when production is checked");
    check(!combatTraining(simulation, nextCommand).empty(),
          "Hard reserved an already-paid tier upgrade again and froze affordable army production");
}

void supplyAnticipatesSeveralProducers() {
    Simulation simulation = hard(7302);
    for (Vec2 point : {Vec2{3850, 4450}, Vec2{4150, 4550}, Vec2{4500, 4150}, Vec2{4500, 3850}}) {
        simulation.debugSpawn(Kind::Foundry, 1, point);
    }
    simulation.debugSpawn(Kind::Processor, 1, {4500, 4500});
    for (int i = 0; i < 13; ++i) {
        simulation.debugSpawn(Kind::Striker, 1, {3600.0f + (i % 7) * 48.0f, 3650.0f + (i / 7) * 48.0f});
    }
    simulation.debugSpawn(Kind::Worker, 1, {4050, 4100});
    check(simulation.capacity(1) - simulation.supply(1) == 12,
          "supply fixture must begin above the former fixed supply threshold");
    simulation.debugResources(1, definition(Kind::Processor).cost);
    const std::size_t nextCommand = simulation.recording().size();
    simulation.update(Simulation::Step);
    bool beganSupply = false;
    for (std::size_t i = nextCommand; i < simulation.recording().size(); ++i) {
        const Command& command = simulation.recording()[i].command;
        beganSupply |= command.team == 1 && command.type == CommandType::Build && command.kind == Kind::Processor;
    }
    check(beganSupply && count(simulation, Kind::Processor) == 2,
          "four producers failed to start supply before exhausting their available headroom");
    check(simulation.players()[1].ore == 0, "proactive supply was not purchased at its ordinary cost");
}

Simulation economyAfterMinerLoss(float seconds, std::uint32_t seed) {
    Simulation simulation = hard(seed);
    std::vector<Id> initialWorkers;
    for (const Entity& entity : simulation.entities()) {
        if (entity.team == 1 && entity.kind == Kind::Worker) initialWorkers.push_back(entity.id);
    }
    // Remove miners through ordinary damage so the recovery fixture preserves
    // all death, ownership and economic bookkeeping instead of editing entities.
    simulation.debugSpawn(Kind::Turret, 0, {1000, 300});
    simulation.debugSpawn(Kind::Turret, 0, {1000, 500});
    simulation.debugSpawn(Kind::Turret, 0, {800, 300});
    Command exposeMiners;
    exposeMiners.type = CommandType::Move;
    exposeMiners.team = 1;
    exposeMiners.units = initialWorkers;
    exposeMiners.point = {1050, 300};
    check(simulation.command(exposeMiners).accepted, "recovery fixture could not move its miners");
    for (int step = 0; step < static_cast<int>(seconds / Simulation::Step); ++step) {
        simulation.debugResources(1, 0);
        simulation.update(Simulation::Step);
    }
    check(count(simulation, Kind::Worker) == 0 && simulation.players()[1].stats.lost >= 5,
          "recovery fixture must lose all five miners through actual combat");
    return simulation;
}

void lostMinersCanBeReplacedWhileSavingForInfrastructure() {
    Simulation simulation = economyAfterMinerLoss(100.0f, 7305);
    simulation.debugSpawn(Kind::Foundry, 1, {3900, 4200});
    simulation.debugSpawn(Kind::Processor, 1, {3900, 4550});
    simulation.debugSpawn(Kind::Turret, 1, {4400, 4150});
    simulation.debugResources(1, 100);
    const std::size_t nextCommand = simulation.recording().size();
    advance(simulation, 20.0f);
    check(count(simulation, Kind::Worker) >= 1,
          "saving for another Foundry permanently blocked replacement of all lost miners");
    const bool paidForWorker = std::any_of(simulation.recording().begin() + nextCommand,
        simulation.recording().end(), [](const RecordedCommand& recorded) {
            return recorded.command.team == 1 && recorded.command.type == CommandType::Train &&
                   recorded.command.kind == Kind::Worker;
        });
    check(paidForWorker, "economic recovery must buy a replacement worker through ordinary production");
}

void expansionDispatchKeepsWorkerRecoveryBudget() {
    Simulation simulation = economyAfterMinerLoss(300.0f, 7306);
    simulation.debugSpawn(Kind::Foundry, 1, {3900, 4200});
    simulation.debugSpawn(Kind::Foundry, 1, {4500, 4600});
    simulation.debugSpawn(Kind::Foundry, 1, {4700, 4200});
    simulation.debugSpawn(Kind::Processor, 1, {3900, 4550});
    simulation.debugSpawn(Kind::Turret, 1, {4400, 4150});
    simulation.debugSpawn(Kind::Worker, 1, {2900, 3800});
    simulation.debugResources(1, definition(Kind::Headquarters).cost);
    const std::size_t nextCommand = simulation.recording().size();
    // Observe exactly the next AI decision, allowing its ordinary timer to fire.
    for (int step = 0; step < 42 && simulation.recording().size() == nextCommand; ++step) {
        simulation.update(Simulation::Step);
    }
    int queuedWorkers = 0;
    for (const Entity& entity : simulation.entities()) {
        if (!entity.alive() || entity.team != 1) continue;
        for (const QueueItem& queued : entity.queue) {
            if (!queued.research && queued.kind == Kind::Worker) ++queuedWorkers;
        }
    }
    check(count(simulation, Kind::Headquarters) == 1,
          "expansion dispatch bypassed the worker budget and spent all recovery ore on a Headquarters");
    check(queuedWorkers == 1 && simulation.players()[1].ore ==
              definition(Kind::Headquarters).cost - definition(Kind::Worker).cost,
          "a worker at a discovered expansion must retain enough ore to buy its replacement miners");
}

void minersSpreadOutWithoutMassReassignment() {
    Simulation simulation = hard(7303);
    for (int i = 0; i < 9; ++i) {
        simulation.debugSpawn(Kind::Worker, 1, {4020.0f + (i % 3) * 38.0f, 4370.0f + (i / 3) * 38.0f});
    }
    Id resource = 0;
    std::vector<Id> workers;
    for (const Entity& entity : simulation.entities()) {
        if (entity.team == 1 && entity.kind == Kind::Worker) workers.push_back(entity.id);
        if (entity.kind == Kind::Resource && entity.pos.x > 3500 && entity.pos.y > 3500 && resource == 0) {
            resource = entity.id;
        }
    }
    check(resource != 0, "worker fixture has no home mineral line");
    Command gather;
    gather.type = CommandType::Gather;
    gather.team = 1;
    gather.units = workers;
    gather.target = resource;
    check(simulation.command(gather).accepted, "worker fixture could not saturate a mineral patch");
    simulation.debugResources(1, 0);
    const std::size_t nextCommand = simulation.recording().size();
    simulation.update(Simulation::Step);
    int reassigned = 0;
    std::set<Id> patches;
    for (Id worker : workers) patches.insert(simulation.find(worker)->resourceTarget);
    for (std::size_t i = nextCommand; i < simulation.recording().size(); ++i) {
        const Command& command = simulation.recording()[i].command;
        if (command.team == 1 && command.type == CommandType::Gather && command.target != resource) {
            reassigned += static_cast<int>(command.units.size());
        }
    }
    check(patches.size() >= 2 && reassigned > 0, "Hard left every worker crowded onto one mineral patch");
    check(reassigned <= 2, "Hard disrupted the whole mineral line instead of moving a small worker batch");
}

struct HiddenOutcome {
    std::vector<Kind> purchases;
    std::vector<RecordedCommand> commands;
    int ore = 0;
    int gathered = 0;
};

HiddenOutcome purchasesWithHiddenEnemy(Kind enemyKind) {
    Simulation simulation = hard(7304);
    simulation.debugSpawn(Kind::Foundry, 1, {3850, 4450});
    simulation.debugSpawn(Kind::Foundry, 1, {4150, 4550});
    simulation.debugSpawn(Kind::Foundry, 1, {4500, 4150});
    simulation.debugSpawn(Kind::Processor, 1, {4500, 4500});
    simulation.debugSpawn(Kind::Scout, 1, {4500, 3850});
    const Id hidden = simulation.debugSpawn(enemyKind, 0, {600, 4500});
    check(!simulation.visible(1, simulation.find(hidden)->pos), "composition fixture must remain hidden");
    simulation.debugResources(1, 2000);
    for (int step = 0; step < static_cast<int>(20.0f / Simulation::Step); ++step) {
        simulation.update(Simulation::Step);
        check(!simulation.visible(1, simulation.find(hidden)->pos), "hidden composition fixture became visible");
        check(std::none_of(simulation.aiSightings().begin(), simulation.aiSightings().end(),
                          [&](const AISighting& sighting) { return sighting.id == hidden; }),
              "Hard learned the composition of an unseen enemy army");
    }
    HiddenOutcome result;
    result.purchases = combatTraining(simulation);
    result.ore = simulation.players()[1].ore;
    result.gathered = simulation.players()[1].stats.gathered;
    for (const RecordedCommand& recorded : simulation.recording()) {
        if (recorded.command.team == 1) result.commands.push_back(recorded);
    }
    return result;
}

bool sameDecisions(const HiddenOutcome& a, const HiddenOutcome& b) {
    if (a.commands.size() != b.commands.size() || a.ore != b.ore || a.gathered != b.gathered) return false;
    for (std::size_t i = 0; i < a.commands.size(); ++i) {
        const Command& x = a.commands[i].command;
        const Command& y = b.commands[i].command;
        if (a.commands[i].tick != b.commands[i].tick || x.type != y.type || x.team != y.team ||
            x.units != y.units || x.point.x != y.point.x || x.point.y != y.point.y ||
            x.target != y.target || x.kind != y.kind || x.queueIndex != y.queueIndex ||
            x.queueMode != y.queueMode || x.spacing != y.spacing ||
            x.hasArrivalFacing != y.hasArrivalFacing || x.arrivalFacing != y.arrivalFacing) return false;
    }
    return true;
}

void purchasesRespectHiddenEnemyComposition() {
    const auto air = purchasesWithHiddenEnemy(Kind::Kite);
    const auto armor = purchasesWithHiddenEnemy(Kind::Bastion);
    const auto light = purchasesWithHiddenEnemy(Kind::Lancer);
    check(!air.purchases.empty() && air.purchases == armor.purchases && air.purchases == light.purchases,
          "Hard changed purchases using hidden enemy composition");
    check(sameDecisions(air, armor) && sameDecisions(air, light),
          "Hard changed commands or economic results using hidden enemy composition");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"paid development and deterministic persistence", paidOpeningScalesAndKeepsProducing},
        {"pending tier leaves affordable production available", pendingTierDoesNotReserveItsCostAgain},
        {"supply anticipates production capacity", supplyAnticipatesSeveralProducers},
        {"lost miners take priority over infrastructure savings", lostMinersCanBeReplacedWhileSavingForInfrastructure},
        {"expansion dispatch preserves worker recovery funds", expansionDispatchKeepsWorkerRecoveryBudget},
        {"mineral line worker distribution", minersSpreadOutWithoutMassReassignment},
        {"purchases respect fog privacy", purchasesRespectHiddenEnemyComposition},
    };
    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
