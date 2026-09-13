#include "Sim/AIDifficulty.h"
#include "Sim/Network.h"
#include "Sim/Simulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cinder;

namespace {

constexpr std::array<AIDifficulty, kAIDifficultyCount> Difficulties{
    AIDifficulty::VeryEasy,
    AIDifficulty::Easy,
    AIDifficulty::Normal,
    AIDifficulty::Hard,
    AIDifficulty::Expert,
};
constexpr std::array<float, kAIDifficultyCount> Aggressions{0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
constexpr std::array<const char*, kAIDifficultyCount> Names{
    "Very Easy", "Easy", "Normal", "Hard", "Expert",
};

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void advance(Simulation& simulation, float seconds) {
    const int steps = static_cast<int>(std::ceil(seconds / Simulation::Step));
    for (int step = 0; step < steps; ++step) {
        simulation.update(Simulation::Step);
    }
}

std::vector<Id> ids(const Simulation& simulation, int team, Kind kind) {
    std::vector<Id> result;
    for (const Entity& entity : simulation.entities()) {
        if (entity.alive() && entity.team == team && entity.kind == kind) {
            result.push_back(entity.id);
        }
    }
    return result;
}

Id first(const Simulation& simulation, int team, Kind kind) {
    const auto result = ids(simulation, team, kind);
    check(!result.empty(), "fixture is missing an expected entity");
    return result.front();
}

bool contains(const std::vector<Id>& values, Id value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void presetContract() {
    check(kAIDifficultyCount == 5, "difficulty selector must expose five choices");
    for (std::size_t index = 0; index < kAIDifficultyCount; ++index) {
        const AIDifficulty difficulty = Difficulties[index];
        check(aiDifficultyAt(index) == difficulty, "difficulty index mapping changed");
        check(aiDifficultyAggression(difficulty) == Aggressions[index],
              "difficulty aggression preset changed");
        check(aiDifficultyFromAggression(Aggressions[index]) == difficulty,
              "difficulty preset does not round trip through Config.aiAggression");
        check(std::string(aiDifficultyName(difficulty)) == Names[index],
              "difficulty display name changed");
        check(std::string(aiDifficultyDescription(difficulty)).size() >= 16,
              "difficulty description is missing");
    }
    check(aiDifficultyAt(kAIDifficultyCount) == AIDifficulty::Normal,
          "out-of-range selector fallback must remain Normal");
    check(aiDifficultyAggression(AIDifficulty::Count) == 1.0f,
          "sentinel aggression fallback must remain Normal");
    check(aiDifficultyFromAggression(-1.0f) == AIDifficulty::Normal,
          "invalid negative aggression fallback must remain Normal");
}

void deterministicSeededBehaviorAtEveryLevel() {
    for (std::size_t index = 0; index < kAIDifficultyCount; ++index) {
        const Config config{static_cast<int>(index % 3), 7300u + static_cast<std::uint32_t>(index),
                            true, Aggressions[index]};
        Simulation firstRun;
        Simulation replay;
        firstRun.reset(config);
        replay.reset(config);
        check(firstRun.stateHash() == replay.stateHash(), "equal seeded AI resets diverged");
        for (int step = 0; step < 800; ++step) {
            firstRun.update(Simulation::Step);
            replay.update(Simulation::Step);
            if (step % 40 == 39) {
                check(firstRun.stateHash() == replay.stateHash(),
                      std::string(Names[index]) + " AI is not deterministic for a fixed seed");
            }
        }
    }
}

void normalPresetMatchesExplicitDefaultAggression() {
    Config preset{1, 8192, true, aiDifficultyAggression(AIDifficulty::Normal)};
    Config explicitDefault{1, 8192, true, 1.0f};
    Simulation selected;
    Simulation defaulted;
    selected.reset(preset);
    defaulted.reset(explicitDefault);
    for (int step = 0; step < 1600; ++step) {
        selected.update(Simulation::Step);
        defaulted.update(Simulation::Step);
    }
    check(selected.stateHash() == defaulted.stateHash(),
          "Normal preset no longer matches explicit default aggression 1.0");
}

void workerTargetsAreDifferentiated() {
    constexpr std::array<int, kAIDifficultyCount> WorkerTargets{8, 10, 12, 14, 16};
    for (std::size_t index = 0; index < kAIDifficultyCount; ++index) {
        Simulation simulation;
        simulation.reset({0, 9000u + static_cast<std::uint32_t>(index), true, Aggressions[index]});
        simulation.update(Simulation::Step);
        const std::string expected = "Workers 5/" + std::to_string(WorkerTargets[index]);
        check(simulation.aiStatus().find(expected) != std::string::npos,
              std::string(Names[index]) + " did not select its one-base worker target");
    }
}

float firstAttackTime(AIDifficulty difficulty, std::uint32_t seed) {
    Simulation simulation;
    simulation.reset({0, seed, true, aiDifficultyAggression(difficulty)});
    std::vector<Id> fixtureArmy;
    for (int index = 0; index < 11; ++index) {
        fixtureArmy.push_back(simulation.debugSpawn(
            Kind::Striker, 1,
            {3900.0f + static_cast<float>(index % 4) * 45.0f,
             3900.0f + static_cast<float>(index / 4) * 45.0f}));
    }

    std::size_t nextCommand = 0;
    constexpr float MaximumObservationSeconds = 465.0f;
    const int maximumSteps = static_cast<int>(MaximumObservationSeconds / Simulation::Step);
    for (int step = 0; step < maximumSteps; ++step) {
        simulation.update(Simulation::Step);
        const auto& recording = simulation.recording();
        while (nextCommand < recording.size()) {
            const RecordedCommand& recorded = recording[nextCommand++];
            const Command& command = recorded.command;
            const bool attackOrder =
                command.type == CommandType::Attack || command.type == CommandType::AttackMove;
            const bool usesFixtureArmy = std::any_of(
                command.units.begin(), command.units.end(),
                [&](Id unit) { return contains(fixtureArmy, unit); });
            if (command.team == 1 && attackOrder && usesFixtureArmy) {
                return static_cast<float>(recorded.tick) * Simulation::Step;
            }
        }
    }
    throw std::runtime_error(std::string(aiDifficultyName(difficulty)) +
                             " issued no bounded attack order");
}

void attackCadenceIsDifferentiated() {
    constexpr std::array<float, kAIDifficultyCount> Earliest{420.0f, 320.0f, 240.0f, 190.0f, 150.0f};
    constexpr std::array<float, kAIDifficultyCount> SchedulingWindows{40.0f, 32.0f, 24.0f, 18.0f, 12.0f};
    std::array<float, kAIDifficultyCount> observed{};
    for (std::size_t index = 0; index < kAIDifficultyCount; ++index) {
        observed[index] = firstAttackTime(Difficulties[index],
                                          10000u + static_cast<std::uint32_t>(index));
        check(observed[index] + 0.1f >= Earliest[index],
              std::string(Names[index]) + " attacked before its configured buildup");
        check(observed[index] <= Earliest[index] + SchedulingWindows[index] + 2.1f,
              std::string(Names[index]) + " missed its configured attack window");
    }
    for (std::size_t index = 1; index < observed.size(); ++index) {
        check(observed[index - 1] > observed[index],
              "difficulty attack buildup is not strictly differentiated");
    }
    std::cout << "EVIDENCE first_attack_seconds";
    for (std::size_t index = 0; index < observed.size(); ++index) {
        std::cout << ' ' << Names[index] << '=' << observed[index];
    }
    std::cout << '\n';
}

struct RuleOutcome {
    int trainCost = 0;
    int ore = 0;
    int gathered = 0;
    float targetHp = 0;
    float damage = 0;
    std::array<bool, 8> fog{};
};

RuleOutcome controlledRules(float aggression) {
    Simulation simulation;
    simulation.reset({0, 424242, false, aggression});
    check(simulation.aiStatus() == "Opponent AI disabled",
          "ai=false unexpectedly enabled opponent decisions");

    const int oreBefore = simulation.players()[0].ore;
    Command train;
    train.type = CommandType::Train;
    train.team = 0;
    train.units = {first(simulation, 0, Kind::Headquarters)};
    train.kind = Kind::Worker;
    check(simulation.command(train).accepted, "controlled worker training was rejected");
    const int trainCost = oreBefore - simulation.players()[0].ore;
    advance(simulation, 12.0f);

    const Id attacker = simulation.debugSpawn(Kind::Striker, 0, {1200.0f, 1200.0f});
    const Id target = simulation.debugSpawn(Kind::Striker, 1, {1400.0f, 1200.0f});
    Command attack;
    attack.type = CommandType::Attack;
    attack.team = 0;
    attack.units = {attacker};
    attack.target = target;
    check(simulation.command(attack).accepted, "controlled combat command was rejected");
    advance(simulation, 1.5f);

    RuleOutcome result;
    result.trainCost = trainCost;
    result.ore = simulation.players()[0].ore;
    result.gathered = simulation.players()[0].stats.gathered;
    result.targetHp = simulation.find(target)->hp;
    result.damage = simulation.players()[0].stats.damage;
    const std::array<Vec2, 4> samples{{{600, 600}, {1200, 1200}, {2400, 2400}, {4200, 4200}}};
    for (std::size_t index = 0; index < samples.size(); ++index) {
        result.fog[index * 2] = simulation.visible(0, samples[index]);
        result.fog[index * 2 + 1] = simulation.explored(0, samples[index]);
    }
    return result;
}

void difficultyDoesNotChangeSimulationRulesAndAiFalseIsUnaffected() {
    const RuleOutcome baseline = controlledRules(Aggressions[0]);
    check(baseline.trainCost == definition(Kind::Worker).cost,
          "controlled training did not use the ordinary unit cost");
    check(baseline.gathered > 0, "controlled workers did not gather ordinary resources");
    check(baseline.damage > 0 && baseline.targetHp < definition(Kind::Striker).hp,
          "controlled combat did not apply ordinary damage");

    for (std::size_t index = 1; index < kAIDifficultyCount; ++index) {
        const RuleOutcome current = controlledRules(Aggressions[index]);
        check(current.trainCost == baseline.trainCost, "difficulty changed unit costs");
        check(current.ore == baseline.ore && current.gathered == baseline.gathered,
              "difficulty changed resource rules while AI was disabled");
        check(current.targetHp == baseline.targetHp && current.damage == baseline.damage,
              "difficulty changed combat damage while AI was disabled");
        check(current.fog == baseline.fog,
              "difficulty changed fog visibility while AI was disabled");
    }
}

void persistenceNetworkAndReplayRetainDifficulty() {
    for (std::size_t index = 0; index < kAIDifficultyCount; ++index) {
        const Config config{static_cast<int>(index % 3), 51000u + static_cast<std::uint32_t>(index),
                            true, Aggressions[index]};
        Simulation simulation;
        simulation.reset(config);
        advance(simulation, 6.0f);

        const auto path = std::filesystem::temp_directory_path() /
            ("cinderline-ai-difficulty-" + std::to_string(config.seed) + ".sav");
        check(simulation.save(path.string()), "difficulty fixture did not save");
        Simulation loaded;
        check(loaded.load(path.string()), "difficulty fixture did not load");
        std::filesystem::remove(path);
        check(loaded.config().aiAggression == Aggressions[index],
              "save/load changed Config.aiAggression");
        check(aiDifficultyFromAggression(loaded.config().aiAggression) == Difficulties[index],
              "save/load changed the selected difficulty");
        check(loaded.stateHash() == simulation.stateHash(),
              "difficulty save/load changed deterministic state");
        for (int step = 0; step < 80; ++step) {
            simulation.update(Simulation::Step);
            loaded.update(Simulation::Step);
        }
        check(loaded.stateHash() == simulation.stateHash(),
              "loaded difficulty changed subsequent AI decisions");

        const net::Snapshot snapshot = net::snapshotFor(simulation, 0);
        const auto bytes = net::encodeSnapshot(snapshot);
        net::Snapshot decoded;
        std::string error;
        check(!bytes.empty() &&
                  net::decodeSnapshot(bytes.data(), bytes.size(), decoded, error),
              "difficulty network snapshot did not round trip");
        check(decoded.config.aiAggression == Aggressions[index],
              "network snapshot changed Config.aiAggression");
        check(aiDifficultyFromAggression(decoded.config.aiAggression) == Difficulties[index],
              "network snapshot changed the selected difficulty");

        Simulation original;
        Config replayConfig{0, 61000u + static_cast<std::uint32_t>(index), false,
                            Aggressions[index]};
        original.reset(replayConfig);
        const Id worker = first(original, 0, Kind::Worker);
        Command move;
        move.type = CommandType::Move;
        move.team = 0;
        move.units = {worker};
        move.point = {950, 1000};
        check(original.command(move).accepted, "difficulty replay move was rejected");
        advance(original, 2.0f);
        Command hold;
        hold.type = CommandType::Hold;
        hold.team = 0;
        hold.units = {worker};
        check(original.command(hold).accepted, "difficulty replay hold was rejected");
        advance(original, 2.0f);

        Simulation replay;
        replay.reset(replayConfig);
        std::size_t next = 0;
        const auto recording = original.recording();
        while (replay.tick() < original.tick()) {
            while (next < recording.size() && recording[next].tick == replay.tick()) {
                check(replay.command(recording[next].command).accepted,
                      "difficulty replay command was rejected");
                ++next;
            }
            replay.update(Simulation::Step);
        }
        check(next == recording.size() && replay.stateHash() == original.stateHash(),
              "replay did not retain difficulty configuration and deterministic state");
    }
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"preset contract", presetContract},
        {"deterministic seeded behavior", deterministicSeededBehaviorAtEveryLevel},
        {"Normal preset and default aggression", normalPresetMatchesExplicitDefaultAggression},
        {"worker target differentiation", workerTargetsAreDifferentiated},
        {"attack cadence differentiation", attackCadenceIsDifferentiated},
        {"unchanged simulation rules and ai=false", difficultyDoesNotChangeSimulationRulesAndAiFalseIsUnaffected},
        {"persistence, network, and replay", persistenceNetworkAndReplayRetainDifficulty},
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& exception) {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << exception.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
