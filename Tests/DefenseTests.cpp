#include "Sim/Network.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace cinder;

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

float distance(Vec2 left, Vec2 right) {
    return std::hypot(left.x - right.x, left.y - right.y);
}

std::vector<Id> ids(const Simulation& simulation, int team, Kind kind) {
    std::vector<Id> result;
    for (const Entity& entity : simulation.entities()) {
        if (entity.alive() && entity.team == team && entity.kind == kind) result.push_back(entity.id);
    }
    return result;
}

Id first(const Simulation& simulation, int team, Kind kind) {
    const std::vector<Id> found = ids(simulation, team, kind);
    check(!found.empty(), "fixture entity is missing");
    return found.front();
}

CommandResult send(Simulation& simulation, CommandType type, int team,
                   std::vector<Id> units, Vec2 point = {}, Id target = 0) {
    Command command;
    command.type = type;
    command.team = team;
    command.units = std::move(units);
    command.point = point;
    command.target = target;
    return simulation.command(command);
}

Simulation fixture(std::uint32_t seed) {
    Simulation simulation;
    // Defense distances and anchors below describe the original flat layout.
    simulation.reset({0, seed, false, 1.0f, MatchLength::Standard, 2, 0});
    for (int team = 0; team < 2; ++team) {
        std::vector<Id> workers = ids(simulation, team, Kind::Worker);
        check(send(simulation, CommandType::Stop, team, workers).accepted,
              "fixture workers stop cleanly");
    }
    return simulation;
}

std::vector<std::string> saveFields(const std::string& line) {
    std::istringstream input(line);std::vector<std::string> values;std::string value;
    while(input>>value)values.push_back(value);return values;
}

std::string joinSaveFields(const std::vector<std::string>& values) {
    std::ostringstream output;for(std::size_t index=0;index<values.size();++index)output<<(index?" ":"")<<values[index];return output.str();
}

void downgradeSaveThirteenToTen(std::vector<std::string>& lines) {
    if(lines.front()=="CINDERLINE 15") {
        auto config=saveFields(lines.at(1));check(config.size()==7&&config.back()=="0","legacy defense migration uses flat-map revision zero");
        config.pop_back();lines[1]=joinSaveFields(config);
    }
    const auto config=saveFields(lines.at(1));const auto players=static_cast<std::size_t>(std::stoul(config.back()));std::size_t cursor=3+players;
    cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
    const auto entityCount=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
    for(std::size_t entity=0;entity<entityCount;++entity) {
        ++cursor;cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
        cursor+=1+static_cast<std::size_t>(std::stoul(lines.at(cursor)));
    }
    const auto effectHeader=saveFields(lines.at(cursor));cursor+=1+static_cast<std::size_t>(std::stoul(effectHeader.front()))+players*2;
    const auto recordingCount=static_cast<std::size_t>(std::stoul(lines.at(cursor++)));
    for(std::size_t recording=0;recording<recordingCount;++recording) {
        auto row=saveFields(lines.at(cursor));
        check(row.size()>=13&&row.size()==13+static_cast<std::size_t>(std::stoul(row[12])),
              "version-thirteen recording layout contains formation fields");
        row.erase(row.begin()+9,row.begin()+12);
        check(row.size()>=10&&row.size()==10+static_cast<std::size_t>(std::stoul(row[9])),
              "version-eleven recording layout contains queue mode");
        row.erase(row.begin()+8);lines[cursor++]=joinSaveFields(row);
    }
    const auto orders=std::find(lines.begin(),lines.end(),"ORDER_QUEUES 1");
    const auto sustained=std::find(lines.begin(),lines.end(),"SUSTAINED_ORDERS 1");
    const auto formation=std::find(lines.begin(),lines.end(),"FORMATION_ORDERS 1");
    const auto queuedWork=std::find(lines.begin(),lines.end(),"QUEUED_WORK 1");
    check(orders!=lines.end()&&sustained!=lines.end()&&formation!=lines.end()&&queuedWork!=lines.end()&&
          orders<sustained&&sustained<formation&&formation<queuedWork,
          "save-fourteen defense fixture contains ordered tactical, sustained, formation, and queued-work state");
    lines.erase(queuedWork,lines.end());
    lines.erase(formation,lines.end());
    lines.erase(orders,lines.end());
}

template <typename Predicate>
bool advanceUntil(Simulation& simulation, float seconds, Predicate predicate) {
    const int steps = static_cast<int>(std::ceil(seconds / Simulation::Step));
    for (int step = 0; step < steps; ++step) {
        if (predicate()) return true;
        simulation.update(Simulation::Step);
    }
    return predicate();
}

void advance(Simulation& simulation, float seconds) {
    advanceUntil(simulation, seconds, [] { return false; });
}

void deterministicArrival() {
    Simulation moving = fixture(401);
    Simulation defending = fixture(401);
    std::vector<Id> movers;
    std::vector<Id> defenders;
    for (int index = 0; index < 6; ++index) {
        const Kind kind = index % 2 == 0 ? Kind::Striker : Kind::Lancer;
        const Vec2 point{900.0f + static_cast<float>(index % 3) * 55.0f,
                         1450.0f + static_cast<float>(index / 3) * 60.0f};
        movers.push_back(moving.debugSpawn(kind, 0, point));
        defenders.push_back(defending.debugSpawn(kind, 0, point));
    }
    check(movers == defenders, "matching fixtures allocate matching unit IDs");
    const Vec2 destination{1750, 1550};
    check(send(moving, CommandType::Move, 0, movers, destination).accepted,
          "reference group Move is accepted");
    check(send(defending, CommandType::Defend, 0, defenders, destination).accepted,
          "group Defend is accepted");

    for (Id id : defenders) {
        const Entity* move = moving.find(id);
        const Entity* defend = defending.find(id);
        check(move && defend && distance(move->goal, defend->goal) < 0.001f,
              "Defend reuses Move's deterministic legal formation slots");
        check(defend->order == Order::Defend, "Defend remains explicit during travel");
    }
    check(!send(defending, CommandType::Defend, 0,
                {first(defending, 0, Kind::Headquarters)}, destination).accepted,
          "a building-only selection cannot defend-move");
    check(send(defending, CommandType::Defend, 0,
               {first(defending, 0, Kind::Worker)}, {1050, 950}).accepted,
          "a mobile worker can receive Defend");

    const bool arrived = advanceUntil(defending, 30.0f, [&] {
        return std::all_of(defenders.begin(), defenders.end(), [&](Id id) {
            const Entity* entity = defending.find(id);
            return entity && distance(entity->pos, entity->goal) <= 8.0f;
        });
    });
    check(arrived, "defending group reaches its assigned anchors");
    for (Id id : defenders) {
        const Entity* entity = defending.find(id);
        check(entity && entity->order == Order::Defend && entity->target == 0,
              "arrival preserves Defend rather than falling back to Idle");
    }
}

void rangeHealingAndOverrides() {
    Simulation simulation = fixture(402);
    const Id guard = simulation.debugSpawn(Kind::Striker, 0, {1000, 1600});
    const Vec2 anchor{1400, 1600};
    check(send(simulation, CommandType::Defend, 0, {guard}, anchor).accepted,
          "guard accepts Defend");
    check(advanceUntil(simulation, 12.0f, [&] {
        return distance(simulation.find(guard)->pos, anchor) <= 8.0f;
    }), "guard reaches its defense anchor");

    const Id farEnemy = simulation.debugSpawn(Kind::Worker, 1, {1750, 1600});
    check(send(simulation, CommandType::Hold, 1, {farEnemy}).accepted,
          "far target holds for the range fixture");
    const float farHealth = simulation.find(farEnemy)->hp;
    advance(simulation, 2.0f);
    check(simulation.find(farEnemy)->hp == farHealth,
          "Defend does not acquire an enemy outside weapon range but inside vision");
    check(distance(simulation.find(guard)->pos, anchor) <= 8.0f,
          "Defend does not chase a visible out-of-range enemy");

    const Id closeEnemy = simulation.debugSpawn(Kind::Worker, 1, {1600, 1600});
    check(send(simulation, CommandType::Hold, 1, {closeEnemy}).accepted,
          "near target holds for the firing fixture");
    const float closeHealth = simulation.find(closeEnemy)->hp;
    advance(simulation, 1.0f);
    check(simulation.find(closeEnemy) && simulation.find(closeEnemy)->hp < closeHealth,
          "Defend fires at an enemy inside weapon range");
    check(distance(simulation.find(guard)->pos, anchor) <= 8.0f,
          "firing does not pull a defender off its anchor");

    check(send(simulation, CommandType::Attack, 0, {guard}, {}, farEnemy).accepted,
          "explicit Attack overrides Defend");
    check(simulation.find(guard)->order == Order::Attack,
          "guard enters Attack immediately after the overriding order");
    check(send(simulation, CommandType::Move, 0, {guard}, {1400, 2200}).accepted,
          "explicit Move overrides Attack and Defend");
    check(simulation.find(guard)->order == Order::Move,
          "guard enters Move immediately after the overriding order");

    Simulation healing = fixture(403);
    const Id patient = healing.debugSpawn(Kind::Striker, 0, {1500, 1750});
    const Id attacker = healing.debugSpawn(Kind::Striker, 1, {1680, 1750});
    check(send(healing, CommandType::Hold, 0, {patient}).accepted,
          "patient holds for real damage");
    check(send(healing, CommandType::Attack, 1, {attacker}, {}, patient).accepted,
          "enemy applies ordinary combat damage");
    healing.update(Simulation::Step);
    const float wounded = healing.find(patient)->hp;
    check(wounded < definition(Kind::Striker).hp, "patient took real damage");
    check(send(healing, CommandType::Move, 1, {attacker}, {3000, 1750}).accepted,
          "attacker leaves the healing fixture");
    const Id mender = healing.debugSpawn(Kind::Mender, 0, {1000, 1600});
    const Vec2 mendAnchor{1400, 1600};
    check(send(healing, CommandType::Defend, 0, {mender}, mendAnchor).accepted,
          "Mend accepts Defend without a follow target");
    const bool mendSucceeded = advanceUntil(healing, 12.0f, [&] {
        return healing.find(patient)->hp > wounded &&
               distance(healing.find(mender)->pos, mendAnchor) <= 8.0f;
    });
    if (!mendSucceeded) {
        const Entity* actualMender = healing.find(mender);
        const Entity* actualPatient = healing.find(patient);
        std::cerr << "MEND_DIAGNOSTIC patientHp=" << (actualPatient ? actualPatient->hp : -1.0f)
                  << " wounded=" << wounded
                  << " menderPos=" << (actualMender ? actualMender->pos.x : -1.0f) << ','
                  << (actualMender ? actualMender->pos.y : -1.0f)
                  << " goal=" << (actualMender ? actualMender->goal.x : -1.0f) << ','
                  << (actualMender ? actualMender->goal.y : -1.0f)
                  << " order=" << (actualMender ? static_cast<int>(actualMender->order) : -1)
                  << " exhausted=" << (actualMender && actualMender->navigationExhausted)
                  << '\n';
    }
    check(mendSucceeded, "defending Mend reaches its anchor and repairs an in-range ally");
    check(healing.find(mender)->order == Order::Defend && healing.find(mender)->target == 0,
          "defending Mend heals without following an attack leader");
    check(send(healing, CommandType::Move, 0, {patient}, {2600, 1750}).accepted,
          "patient can leave the defended position");
    advance(healing, 8.0f);
    check(distance(healing.find(mender)->pos, mendAnchor) <= 8.0f &&
          healing.find(mender)->order == Order::Defend,
          "Mend does not follow an ally out of defense range");
}

void trafficAndPersistence() {
    Simulation simulation = fixture(404);
    const Id guard = simulation.debugSpawn(Kind::Striker, 0, {900, 2250});
    const Vec2 anchor{1800, 2250};
    check(send(simulation, CommandType::Defend, 0, {guard}, anchor).accepted,
          "traffic guard accepts Defend");
    check(advanceUntil(simulation, 15.0f, [&] {
        return distance(simulation.find(guard)->pos, anchor) <= 8.0f;
    }), "traffic guard reaches its anchor");

    std::vector<Id> traffic;
    for (int index = 0; index < 48; ++index) {
        traffic.push_back(simulation.debugSpawn(
            Kind::Striker, 0,
            {1100.0f + static_cast<float>(index % 8) * 42.0f,
             1900.0f + static_cast<float>(index / 8) * 42.0f}));
    }
    check(send(simulation, CommandType::Move, 0, traffic, {2250, 2550}).accepted,
          "friendly traffic crosses the defended line");
    advance(simulation, 30.0f);
    check(simulation.find(guard)->order == Order::Defend &&
          distance(simulation.find(guard)->pos, anchor) <= 10.0f,
          "defender retains and reclaims its anchor after friendly traffic");

    const auto path = (std::filesystem::temp_directory_path() /
                       "cinderline-defense-v13.sav").string();
    check(simulation.save(path), "Defend state saves in the compatible format");
    std::ifstream saved(path);
    std::vector<std::string> saveLines;std::string saveLine;
    while(std::getline(saved,saveLine))saveLines.push_back(saveLine);
    const auto production=std::find(saveLines.begin(),saveLines.end(),"PRODUCTION_JOBS 1");
    check(!saveLines.empty()&&saveLines.front()=="CINDERLINE 15"&&production!=saveLines.end(), "fresh Defend save includes map revision, player count, match length, stable production jobs, tactical orders, sustained orders, formation orders, and queued work");
    Simulation loaded;
    check(loaded.load(path), "Defend state loads from the compatible format");
    check(loaded.stateHash() == simulation.stateHash(),
          "save-load preserves defense orders, anchors and navigation state");
    auto writeVersion = [&](int version,bool includeProduction) {
        const std::string candidate = path + ".version-" + std::to_string(version);
        auto source=saveLines;if(version<=10)downgradeSaveThirteenToTen(source);
        const auto sourceProduction=std::find(source.begin(),source.end(),"PRODUCTION_JOBS 1");
        {
            std::ofstream output(candidate);
            for(auto line=source.begin();line!=(includeProduction?source.end():sourceProduction);++line) {
                std::string value=line==source.begin()?"CINDERLINE "+std::to_string(version):*line;
                if(version<10&&line==source.begin()+1) {
                    auto values=std::vector<std::string>{};std::istringstream input(value);std::string field;while(input>>field)values.push_back(field);
                    values.pop_back();if(version<9)values.pop_back();value.clear();for(std::size_t i=0;i<values.size();++i)value+=(i?" ":"")+values[i];
                }
                if(version<10&&line==source.begin()+2) {
                    auto values=std::vector<std::string>{};std::istringstream input(value);std::string field;while(input>>field)values.push_back(field);
                    values.pop_back();value.clear();for(std::size_t i=0;i<values.size();++i)value+=(i?" ":"")+values[i];
                }
                output<<value<<'\n';
            }
        }
        return candidate;
    };
    const std::string versionSix=writeVersion(6,false);
    Simulation migratedSix;
    check(migratedSix.load(versionSix)&&migratedSix.stateHash()==simulation.stateHash(),"version six Defend saves remain readable and migrate default production identities");
    std::filesystem::remove(versionSix);
    auto rejectsVersion = [&](int version,bool includeProduction,const std::string& message) {
        const std::string candidate=writeVersion(version,includeProduction);
        Simulation current = fixture(499);
        const std::uint64_t before = current.stateHash();
        check(!current.load(candidate) && current.stateHash() == before, message);
        std::filesystem::remove(candidate);
    };
    rejectsVersion(5,false,"version five rejects the later Defend enum while preserving the active match");
    rejectsVersion(16,true,"future save versions are rejected while preserving the active match");
    std::filesystem::remove(path);
    for (int step = 0; step < 80; ++step) {
        simulation.update(Simulation::Step);
        loaded.update(Simulation::Step);
        check(loaded.stateHash() == simulation.stateHash(),
              "loaded defense continues deterministically");
    }

    net::ViewMemory memory;
    const net::Snapshot snapshot = net::snapshotFor(simulation, 0, &memory);
    const Entity* networkGuard = nullptr;
    for (const Entity& entity : snapshot.entities) {
        if (entity.team == 0 && entity.kind == Kind::Striker && entity.order == Order::Defend &&
            distance(entity.goal, anchor) <= 0.001f) {
            networkGuard = &entity;
            break;
        }
    }
    check(networkGuard, "recipient snapshot exposes its own Defend order and anchor");
    const std::vector<std::uint8_t> snapshotBytes = net::encodeSnapshot(snapshot);
    net::Snapshot decodedSnapshot;
    std::string error;
    check(net::decodeSnapshot(snapshotBytes.data(), snapshotBytes.size(), decodedSnapshot, error),
          "snapshot containing Defend round trips");

    Command defend;
    defend.type = CommandType::Defend;
    defend.units = {networkGuard->id};
    defend.point = {1900, 2350};
    const std::vector<std::uint8_t> commandBytes = net::encodeCommand(defend, 77);
    Command decodedCommand;
    std::uint32_t sequence = 0;
    check(net::decodeCommand(commandBytes.data(), commandBytes.size(), decodedCommand, sequence, error) &&
          sequence == 77 && decodedCommand.type == CommandType::Defend,
          "Defend command round trips through protocol version four");
    check(net::translateCommand(simulation, 0, memory, decodedCommand, error) &&
          decodedCommand.units == std::vector<Id>{guard},
          "server translation resolves the recipient's opaque defense handle");
    check(simulation.command(decodedCommand).accepted,
          "translated Defend executes under authoritative simulation rules");
}

} // namespace

int main() {
    try {
        deterministicArrival();
        rangeHealingAndOverrides();
        trafficAndPersistence();
        std::cout << "DEFENSE_REGRESSIONS arrival=1 no_chase=1 ranged_fire=1 mend=1 traffic=1 override=1 persistence=1 network=1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DEFENSE_TEST_FAILURE: " << error.what() << '\n';
        return 1;
    }
}
