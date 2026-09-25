#include "Sim/Simulation.h"
#include "Sim/AIDifficulty.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace cinder {
namespace {

float distanceSquared(Vec2 a, Vec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool isCombatUnit(Kind kind) {
    return kind == Kind::Striker || kind == Kind::Lancer || kind == Kind::Scout ||
           kind == Kind::Bastion || kind == Kind::Mortar || kind == Kind::Mender ||
           kind == Kind::Kite;
}

struct AITuning {
    float developmentTimeScale = 1.0f;
    int oneBaseWorkers = 12;
    int multipleBaseWorkers = 18;
    int armyPercent = 100;
    float attackStartSeconds = 240.0f;
    std::size_t attackGroupSize = 7;
    std::uint64_t attackIntervalSlots = 12;
};

constexpr AITuning aiTuning(AIDifficulty difficulty) {
    switch (difficulty) {
        case AIDifficulty::VeryEasy: return {1.5f, 8, 12, 65, 420.0f, 4, 20};
        case AIDifficulty::Easy: return {1.25f, 10, 15, 80, 320.0f, 5, 16};
        case AIDifficulty::Normal: return {};
        case AIDifficulty::Hard: return {0.8f, 14, 21, 120, 190.0f, 9, 9};
        case AIDifficulty::Expert: return {0.65f, 16, 24, 145, 150.0f, 11, 6};
        case AIDifficulty::Count: return {};
    }
    return {};
}

struct AISnapshot {
    Id id = 0;
    Kind kind = Kind::Worker;
    Vec2 pos{};
    Vec2 goal{};
    float hp = 0;
    float maxHp = 1;
    float resourceRemaining = 0;
    float progress = 0;
    Order order = Order::Idle;
    Id target = 0;
    bool currentlyVisible = false;
    std::size_t queueSize = 0;
    bool queueHasResearch = false;
};

} // namespace

void Simulation::updateAI() {
    if (config_.playerCount != 2 || eliminated(1)) {
        return;
    }
    constexpr int team = 1;
    constexpr float pi = 3.14159265358979323846f;
    const float now = time();
    const float mapScale = worldSize() / WorldSize;
    const auto mapPoint = [&](Vec2 point) {
        return Vec2{point.x * mapScale, point.y * mapScale};
    };
    const AIDifficulty difficulty = aiDifficultyFromAggression(config_.aiAggression);
    const AITuning tuning = aiTuning(difficulty);
    const bool advancedAI = difficulty == AIDifficulty::Hard || difficulty == AIDifficulty::Expert;
    const auto developmentTime = [&](float normalSeconds) {
        return normalSeconds * tuning.developmentTimeScale;
    };
    const auto armyTarget = [&](int normalTarget) {
        return (normalTarget * tuning.armyPercent + 50) / 100;
    };
    updateAIKnowledge();

    std::vector<AISnapshot> own;
    std::vector<AISnapshot> visibleEnemies;
    std::vector<AISnapshot> resources;
    std::array<int, 15> queuedUnits{};
    own.reserve(entities_.size());
    visibleEnemies.reserve(entities_.size());
    resources.reserve(entities_.size());

    for (const Entity& entity : entities_) {
        const bool currentlyVisible = visible(team, entity.pos);
        if (entity.team != team && entity.kind != Kind::Resource && !currentlyVisible) {
            continue;
        }
        if (!entity.alive()) {
            continue;
        }

        AISnapshot snapshot;
        snapshot.id = entity.id;
        snapshot.kind = entity.kind;
        snapshot.pos = entity.pos;
        snapshot.goal = entity.goal;
        snapshot.hp = entity.hp;
        snapshot.maxHp = std::max(1.0f, definition(entity.kind).hp);
        snapshot.progress = entity.progress;
        snapshot.order = entity.order;
        snapshot.target = entity.target;
        snapshot.currentlyVisible = currentlyVisible;
        if (entity.kind == Kind::Resource && snapshot.currentlyVisible) {
            snapshot.resourceRemaining = entity.resource;
        }
        snapshot.queueSize = entity.queue.size();
        snapshot.queueHasResearch = std::any_of(
            entity.queue.begin(), entity.queue.end(), [](const QueueItem& item) { return item.research; });

        if (entity.team == team) {
            for (const QueueItem& item : entity.queue) {
                const int queuedKind = static_cast<int>(item.kind);
                if (!item.research && queuedKind >= 0 && queuedKind < static_cast<int>(queuedUnits.size())) {
                    ++queuedUnits[queuedKind];
                }
            }
            own.push_back(snapshot);
        } else if (entity.kind == Kind::Resource) {
            if (explored(team, entity.pos)) {
                resources.push_back(snapshot);
            }
        } else if (entity.team == 0 && visible(team, entity.pos)) {
            visibleEnemies.push_back(snapshot);
        }
    }

    auto ownOfKind = [&](Kind kind, bool completeOnly = false) {
        return static_cast<int>(std::count_if(own.begin(), own.end(), [&](const AISnapshot& entity) {
            return entity.kind == kind && (!completeOnly || entity.progress >= 1.0f);
        }));
    };
    auto plannedOfKind = [&](Kind kind) {
        return ownOfKind(kind) + queuedUnits[static_cast<int>(kind)];
    };

    // A recent reconnaissance report can change purchases; old reports still
    // guide searches for bases, but cannot lock the army into obsolete counters.
    std::array<int, 15> observedUnits{};
    int observedBuildings = 0;
    int observedArmor = 0;
    int observedAir = 0;
    int observedInfantry = 0;
    for (const AISighting& sighting : aiSightings()) {
        if (tick_ - sighting.lastSeenTick > static_cast<std::uint64_t>(60.0f / Step)) {
            continue;
        }
        ++observedUnits[static_cast<int>(sighting.kind)];
        const Definition& seen = definition(sighting.kind);
        if (seen.building) {
            ++observedBuildings;
        } else if (isCombatUnit(sighting.kind)) {
            observedArmor += seen.armor >= 4 ? 1 : 0;
            observedAir += seen.air ? 1 : 0;
            observedInfantry += sighting.kind == Kind::Striker || sighting.kind == Kind::Lancer ? 1 : 0;
        }
    }

    std::vector<Id> workers;
    std::vector<Id> combat;
    std::vector<Id> healthyCombat;
    std::vector<Id> hurtCombat;
    std::vector<Vec2> headquarters;
    std::vector<Vec2> operationalHeadquarters;
    Vec2 armyCenter{};
    for (const AISnapshot& entity : own) {
        if (entity.kind == Kind::Worker) {
            workers.push_back(entity.id);
        }
        if (entity.kind == Kind::Headquarters) {
            headquarters.push_back(entity.pos);
            if (entity.progress >= 1.0f) {
                operationalHeadquarters.push_back(entity.pos);
            }
        }
        if (isCombatUnit(entity.kind) && entity.progress >= 1.0f) {
            combat.push_back(entity.id);
            armyCenter.x += entity.pos.x;
            armyCenter.y += entity.pos.y;
            if (entity.hp / entity.maxHp < 0.35f) {
                hurtCombat.push_back(entity.id);
            } else {
                healthyCombat.push_back(entity.id);
            }
        }
    }
    if (!combat.empty()) {
        armyCenter.x /= static_cast<float>(combat.size());
        armyCenter.y /= static_cast<float>(combat.size());
    }

    const std::vector<Vec2>& retreatBases = operationalHeadquarters.empty() ? headquarters : operationalHeadquarters;
    const Vec2 authoredHome = mapPoint({4200.0f, 4200.0f});
    Vec2 home = retreatBases.empty() ? authoredHome : retreatBases.front();
    float bestSafety = -1.0f;
    for (Vec2 base : retreatBases) {
        float nearestVisibleEnemy = std::numeric_limits<float>::max();
        for (const AISnapshot& enemy : visibleEnemies) {
            nearestVisibleEnemy = std::min(nearestVisibleEnemy, distanceSquared(base, enemy.pos));
        }
        const bool safer = nearestVisibleEnemy > bestSafety;
        const bool equallySafeAndCloserToStart = nearestVisibleEnemy == bestSafety &&
            distanceSquared(base, authoredHome) < distanceSquared(home, authoredHome);
        if (safer || equallySafeAndCloserToStart) {
            home = base;
            bestSafety = nearestVisibleEnemy;
        }
    }

    std::string action;
    auto issue = [&](Command next, const std::string& description) {
        const CommandResult result = command(next);
        if (result.accepted && action.empty()) {
            action = description;
        }
        return result.accepted;
    };

    // Commands below can change assignments after AISnapshot was captured. Check
    // live orders so a resumed builder cannot be harvested or dispatched again in
    // this AI update. Construct also reserves workers still travelling to a site.
    auto availableWorker = [&](Id id) {
        const Entity* worker = find(id);
        return worker != nullptr && worker->alive() && worker->team == team &&
               worker->kind == Kind::Worker && worker->progress >= 1.0f &&
               worker->order != Order::Construct;
    };

    auto closestWorker = [&](Vec2 point) -> const AISnapshot* {
        const AISnapshot* result = nullptr;
        float best = std::numeric_limits<float>::max();
        for (const AISnapshot& entity : own) {
            if (!availableWorker(entity.id)) {
                continue;
            }
            const float d = distanceSquared(entity.pos, point);
            if (d < best || (d == best && (result == nullptr || entity.id < result->id))) {
                best = d;
                result = &entity;
            }
        }
        return result;
    };

    // Recover already-paid foundations before planning more infrastructure.
    // constructionWorker recognizes the assigned worker even while en route;
    // !constructionActive alone would incorrectly replace travelling builders.
    bool orphanedConstructionPending = false;
    for (const AISnapshot& foundation : own) {
        if (!definition(foundation.kind).building || foundation.progress >= 1.0f ||
            constructionWorker(foundation.id) != 0) {
            continue;
        }
        if (const AISnapshot* builder = closestWorker(foundation.pos)) {
            Command resume;
            resume.type = CommandType::ResumeConstruction;
            resume.team = team;
            resume.units = {builder->id};
            resume.target = foundation.id;
            issue(resume, std::string("resuming ") + definition(foundation.kind).name);
        }
        if (constructionWorker(foundation.id) == 0) {
            orphanedConstructionPending = true;
        }
    }

    // Balance mining by delivery distance and current assignments. Only redirect
    // empty workers, in small batches, so loaded miners finish their delivery.
    std::vector<int> resourceWorkers(resources.size(), 0);
    for (Id id : workers) {
        const Entity* worker = find(id);
        if (!worker || worker->order != Order::Gather) continue;
        for (std::size_t i = 0; i < resources.size(); ++i) {
            if (worker->resourceTarget == resources[i].id) ++resourceWorkers[i];
        }
    }
    auto miningScore = [&](const AISnapshot& worker, std::size_t index) {
        const AISnapshot& node = resources[index];
        float delivery = std::numeric_limits<float>::max();
        for (const AISnapshot& depot : own) {
            if (depot.progress >= 1.0f &&
                (depot.kind == Kind::Headquarters || depot.kind == Kind::Processor)) {
                delivery = std::min(delivery, distanceSquared(depot.pos, node.pos));
            }
        }
        const bool threatened = std::any_of(visibleEnemies.begin(), visibleEnemies.end(), [&](const AISnapshot& enemy) {
            return definition(enemy.kind).damage > 0 &&
                   distanceSquared(enemy.pos, node.pos) < 650.0f * 650.0f;
        });
        if (threatened || delivery > 1100.0f * 1100.0f) return std::numeric_limits<float>::max();
        const float load = static_cast<float>(resourceWorkers[index]);
        return delivery + 0.15f * distanceSquared(worker.pos, node.pos) + load * load * 18000.0f;
    };
    int reassignedMiners = 0;

    // New workers and workers released from completed construction resume
    // harvesting through the same checks as player-issued gather commands.
    for (const AISnapshot& worker : own) {
        if (!availableWorker(worker.id)) {
            continue;
        }
        const Entity* liveWorker = find(worker.id);
        const bool rebalance = advancedAI && (tick_ / 40) % 8 == 0 && reassignedMiners < 2 &&
            liveWorker->order == Order::Gather && !liveWorker->returning && liveWorker->carried == 0 &&
            liveWorker->futureOrders.empty();
        if (liveWorker->order != Order::Idle && liveWorker->order != Order::Hold && !rebalance) {
            continue;
        }
        const AISnapshot* resource = nullptr;
        float best = std::numeric_limits<float>::max();
        for (const AISnapshot& candidate : resources) {
            if (!candidate.currentlyVisible || candidate.resourceRemaining <= 0) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(&candidate - resources.data());
            const float d = advancedAI ? miningScore(worker, index) : distanceSquared(worker.pos, candidate.pos);
            if (d < best) {
                best = d;
                resource = &candidate;
            }
        }
        if (resource != nullptr && (!rebalance || resource->id != liveWorker->resourceTarget)) {
            if (rebalance) {
                const auto current = std::find_if(resources.begin(), resources.end(), [&](const AISnapshot& node) {
                    return node.id == liveWorker->resourceTarget;
                });
                if (current != resources.end()) {
                    const std::size_t index = static_cast<std::size_t>(current - resources.begin());
                    // Include the arriving worker's load before requiring a useful improvement.
                    const std::size_t next = static_cast<std::size_t>(resource - resources.data());
                    ++resourceWorkers[next];
                    const float improved = miningScore(worker, next);
                    --resourceWorkers[next];
                    if (improved + 40000.0f >= miningScore(worker, index)) continue;
                    --resourceWorkers[index];
                }
            }
            Command gather;
            gather.type = CommandType::Gather;
            gather.team = team;
            gather.units = {worker.id};
            gather.target = resource->id;
            if (issue(gather, "assigning workers")) {
                ++resourceWorkers[static_cast<std::size_t>(resource - resources.data())];
                if (rebalance) ++reassignedMiners;
            }
        }
    }

    auto tryBuild = [&](Kind kind, Vec2 anchor, bool expansion, bool purchase = true) {
        static constexpr std::array<float, 4> localRadii{220.0f, 340.0f, 470.0f, 610.0f};
        static constexpr std::array<float, 4> expansionRadii{280.0f, 390.0f, 500.0f, 620.0f};
        static constexpr std::array<float, 4> depotRadii{160.0f, 220.0f, 290.0f, 380.0f};
        const auto& radii = expansion ? expansionRadii :
            (advancedAI && kind == Kind::Processor ? depotRadii : localRadii);
        const int phase = (static_cast<int>(kind) * 5 + static_cast<int>(tick_ / 40)) % 16;

        for (std::size_t ring = 0; purchase && ring < radii.size(); ++ring) {
            const float radius = radii[ring];
            for (int step = 0; step < 16; ++step) {
                const int spoke = (step + phase) % 16;
                const float angle = 2.0f * pi * static_cast<float>(spoke) / 16.0f;
                const Vec2 point{anchor.x + std::cos(angle) * radius,
                                 anchor.y + std::sin(angle) * radius};
                if (point.x < 80.0f || point.y < 80.0f ||
                    point.x > worldSize() - 80.0f || point.y > worldSize() - 80.0f ||
                    !explored(team, point) || !canPlace(team, kind, point)) {
                    continue;
                }

                const AISnapshot* builder = closestWorker(point);
                if (builder == nullptr || distanceSquared(builder->pos, point) > 700.0f * 700.0f) {
                    continue;
                }
                const Id builderId = builder->id;
                Command build;
                build.type = CommandType::Build;
                build.team = team;
                build.units = {builderId};
                build.point = point;
                build.kind = kind;
                return issue(build, std::string("building ") + definition(kind).name);
            }
        }

        if (expansion || kind == Kind::Turret) {
            const AISnapshot* builder = closestWorker(anchor);
            if (builder != nullptr && distanceSquared(builder->pos, anchor) > 360.0f * 360.0f &&
                (builder->order != Order::Move || distanceSquared(builder->goal, anchor) > 100.0f * 100.0f)) {
                Command move;
                move.type = CommandType::Move;
                move.team = team;
                move.units = {builder->id};
                move.point = anchor;
                issue(move, expansion ? "moving an expansion worker" : "moving a construction worker");
            }
        }
        return false;
    };

    const bool hasFoundry = ownOfKind(Kind::Foundry) > 0;
    const bool hasProcessor = ownOfKind(Kind::Processor) > 0;
    const bool hasLaboratory = ownOfKind(Kind::Laboratory) > 0;
    const bool hasMotorPool = ownOfKind(Kind::MotorPool) > 0;
    const int hqCount = ownOfKind(Kind::Headquarters);
    const int processorCount = ownOfKind(Kind::Processor);
    const bool processorUnderConstruction = std::any_of(own.begin(), own.end(), [](const AISnapshot& entity) {
        return entity.kind == Kind::Processor && entity.progress < 1.0f;
    });

    auto remainingOreNear = [&](Vec2 point, float radius) {
        float total = 0;
        for (const AISnapshot& resource : resources) {
            if (resource.currentlyVisible && resource.resourceRemaining > 0 &&
                distanceSquared(point, resource.pos) <= radius * radius) {
                total += resource.resourceRemaining;
            }
        }
        return total;
    };

    Vec2 exposedHeadquarters{};
    bool needsTurret = false;
    for (const AISnapshot& hq : own) {
        if (hq.kind != Kind::Headquarters) {
            continue;
        }
        const bool covered = std::any_of(own.begin(), own.end(), [&](const AISnapshot& defense) {
            return defense.kind == Kind::Turret &&
                   distanceSquared(defense.pos, hq.pos) <= 900.0f * 900.0f;
        });
        if (!covered) {
            exposedHeadquarters = hq.pos;
            needsTurret = true;
            break;
        }
    }

    const std::array<Vec2, 4> expansionSites{{
        mapPoint({2900.0f, 3800.0f}), mapPoint({3800.0f, 2000.0f}),
        mapPoint({1900.0f, 1000.0f}), mapPoint({1000.0f, 2800.0f})}};
    // These are public map landmarks, not coordinates read from hidden actors.
    // Observation age makes a cleared start lose priority to unsearched sites.
    const std::array<Vec2, 7> reconnaissanceSites{{
        mapPoint({600.0f, 600.0f}), mapPoint({2900.0f, 3800.0f}), mapPoint({3800.0f, 2000.0f}),
        mapPoint({1900.0f, 1000.0f}), mapPoint({1000.0f, 2800.0f}), mapPoint({600.0f, 4200.0f}),
        mapPoint({4200.0f, 600.0f})}};
    auto reconnaissanceTarget = [&](Vec2 origin, bool preferUnseenStart) {
        Vec2 chosen = reconnaissanceSites.front();
        std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
        float nearestDistance = std::numeric_limits<float>::max();
        bool found = false;
        for (std::size_t i = 0; i < reconnaissanceSites.size(); ++i) {
            const Vec2 site = reconnaissanceSites[i];
            const bool owned = std::any_of(headquarters.begin(), headquarters.end(), [&](Vec2 hq) {
                return distanceSquared(hq, site) < 850.0f * 850.0f;
            });
            if (owned) {
                continue;
            }
            const std::uint64_t observed = aiLastObserved(site);
            const float d = distanceSquared(origin, site);
            const bool retainUnseenStart = preferUnseenStart && found && oldest == 0 &&
                distanceSquared(chosen, reconnaissanceSites.front()) < 1.0f;
            if (!found || observed < oldest ||
                (observed == oldest && !retainUnseenStart && d < nearestDistance)) {
                chosen = site;
                oldest = observed;
                nearestDistance = d;
                found = true;
            }
        }
        if (!found) {
            // Unusual development/custom states may own every landmark. Search
            // the oldest fog cell instead of falling back to the enemy start.
            const float cellSize = worldSize() / FogSize;
            for (int y = 0; y < FogSize; ++y) {
                for (int x = 0; x < FogSize; ++x) {
                    const Vec2 site{(x + 0.5f) * cellSize, (y + 0.5f) * cellSize};
                    const bool terrainBlocked = std::any_of(obstacles_.begin(), obstacles_.end(), [&](const Obstacle& obstacle) {
                        const float radius = definition(Kind::Scout).radius;
                        return std::abs(site.x - obstacle.center.x) < obstacle.half.x + radius &&
                               std::abs(site.y - obstacle.center.y) < obstacle.half.y + radius;
                    });
                    if (terrainBlocked) {
                        continue;
                    }
                    const std::uint64_t observed = aiLastObserved(site);
                    const float d = distanceSquared(origin, site);
                    if (observed < oldest || (observed == oldest && d < nearestDistance)) {
                        chosen = site;
                        oldest = observed;
                        nearestDistance = d;
                    }
                }
            }
        }
        return chosen;
    };
    const Vec2 strategicOrigin = combat.empty() ? home : armyCenter;
    Vec2 strategicTarget = reconnaissanceTarget(strategicOrigin, true);
    std::string objective = "searching an unobserved site";
    const AISighting* rememberedObjective = nullptr;
    int objectivePriority = std::numeric_limits<int>::max();
    float objectiveDistance = std::numeric_limits<float>::max();
    for (const AISighting& sighting : aiSightings()) {
        if (!definition(sighting.kind).building) {
            continue;
        }
        const int priority = sighting.kind == Kind::Headquarters ? 0 :
            (sighting.kind == Kind::Foundry || sighting.kind == Kind::MotorPool ? 1 :
             (sighting.kind == Kind::Turret ? 3 : 2));
        const float d = distanceSquared(strategicOrigin, sighting.pos);
        if (priority < objectivePriority ||
            (priority == objectivePriority && (d < objectiveDistance ||
             (d == objectiveDistance && rememberedObjective != nullptr && sighting.id < rememberedObjective->id)))) {
            rememberedObjective = &sighting;
            objectivePriority = priority;
            objectiveDistance = d;
        }
    }
    if (rememberedObjective != nullptr) {
        strategicTarget = rememberedObjective->pos;
        objective = std::string("checking known ") + definition(rememberedObjective->kind).name;
    } else if (aiLastObserved(strategicTarget) != 0) {
        objective = "rechecking an old scouting report";
    }
    int productiveHeadquarters = 0;
    for (Vec2 hq : headquarters) {
        if (remainingOreNear(hq, 900.0f) >= 650.0f) {
            ++productiveHeadquarters;
        }
    }
    Vec2 bestExpansion{};
    float bestExpansionOre = 0;
    for (Vec2 site : expansionSites) {
        const bool occupied = std::any_of(headquarters.begin(), headquarters.end(), [&](Vec2 hq) {
            return distanceSquared(hq, site) < 850.0f * 850.0f;
        });
        if (occupied) {
            continue;
        }
        const bool unsafe = advancedAI && std::any_of(aiSightings().begin(), aiSightings().end(), [&](const AISighting& sighting) {
            return (definition(sighting.kind).damage > 0 || sighting.kind == Kind::Headquarters) &&
                   (definition(sighting.kind).building || tick_ - sighting.lastSeenTick < 30.0f / Step) &&
                   distanceSquared(site, sighting.pos) < 1000.0f * 1000.0f;
        });
        if (unsafe) continue;
        const float remainingOre = remainingOreNear(site, 600.0f);
        if (remainingOre > bestExpansionOre) {
            bestExpansion = site;
            bestExpansionOre = remainingOre;
        }
    }
    const bool richExpansionDiscovered = hqCount < 4 && productiveHeadquarters < 2 &&
                                         bestExpansionOre >= 650.0f;
    Vec2 expansionScoutTarget{};
    bool needsExpansionScouting = false;
    if (hqCount < 4 && productiveHeadquarters < 2 && !richExpansionDiscovered) {
        for (const AISnapshot& scout : own) {
            if (scout.kind != Kind::Scout || scout.progress < 1.0f || scout.order != Order::Move) {
                continue;
            }
            const auto destination = std::find_if(expansionSites.begin(), expansionSites.end(), [&](Vec2 site) {
                const bool occupied = std::any_of(headquarters.begin(), headquarters.end(), [&](Vec2 hq) {
                    return distanceSquared(hq, site) < 850.0f * 850.0f;
                });
                return !occupied && distanceSquared(scout.goal, site) <= 100.0f * 100.0f;
            });
            if (destination != expansionSites.end()) {
                expansionScoutTarget = *destination;
                needsExpansionScouting = true;
                break;
            }
        }
        for (Vec2 site : expansionSites) {
            if (needsExpansionScouting) {
                break;
            }
            const bool occupied = std::any_of(headquarters.begin(), headquarters.end(), [&](Vec2 hq) {
                return distanceSquared(hq, site) < 850.0f * 850.0f;
            });
            if (occupied) {
                continue;
            }
            bool hasVisibleResourceInformation = false;
            for (const AISnapshot& resource : resources) {
                if (resource.currentlyVisible && distanceSquared(site, resource.pos) <= 600.0f * 600.0f) {
                    hasVisibleResourceInformation = true;
                    break;
                }
            }
            if (!hasVisibleResourceInformation || remainingOreNear(site, 600.0f) >= 650.0f) {
                expansionScoutTarget = site;
                needsExpansionScouting = true;
                break;
            }
        }
    }

    const bool baseUnderThreat = std::any_of(visibleEnemies.begin(), visibleEnemies.end(), [&](const AISnapshot& enemy) {
        return definition(enemy.kind).damage > 0 &&
            std::any_of(headquarters.begin(), headquarters.end(), [&](Vec2 base) {
                return distanceSquared(base, enemy.pos) < 1200.0f * 1200.0f;
            });
    });
    const int foundries = ownOfKind(Kind::Foundry);
    const int pools = ownOfKind(Kind::MotorPool);
    const bool researchPending = std::any_of(own.begin(), own.end(), [](const AISnapshot& building) {
        return building.queueHasResearch;
    });
    const int productionSupplyBuffer = std::min(24, std::max(10, foundries * 4 + pools * 6));
    const bool needsSupply = capacity(team) < 200 &&
        capacity(team) - supply(team) < productionSupplyBuffer && !processorUnderConstruction;
    const int desiredFoundries = now < developmentTime(60.0f) ? 1 :
        (now < developmentTime(150.0f) ? 2 :
         (now < developmentTime(difficulty == AIDifficulty::Expert ? 240.0f : 360.0f) ? 3 :
          (difficulty == AIDifficulty::Expert || hqCount >= 2 ? 4 : 3)));
    const bool enoughArmyForTech = combat.size() >= 8 || players_[team].ore >= 1000;
    bool buildingIssued = false;
    Kind wantedBuilding = Kind::Resource;
    Vec2 buildAnchor = home;
    bool wantsExpansion = false;

    if (advancedAI) {
        // Supply and fighting capacity take precedence over long tech/expansion
        // savings. Every structure is still built by a paid, available worker.
        if (!hasFoundry) {
            wantedBuilding = Kind::Foundry;
        } else if (needsSupply || (!hasProcessor && now >= developmentTime(30.0f))) {
            wantedBuilding = Kind::Processor;
        } else if (baseUnderThreat && needsTurret) {
            wantedBuilding = Kind::Turret;
            buildAnchor = exposedHeadquarters;
        } else if (foundries < desiredFoundries &&
                   (foundries < 2 || players_[team].ore >= 450 || hqCount >= 2)) {
            wantedBuilding = Kind::Foundry;
        } else if (!baseUnderThreat && richExpansionDiscovered && now >= developmentTime(280.0f)) {
            wantedBuilding = Kind::Headquarters;
            buildAnchor = bestExpansion;
            wantsExpansion = true;
        } else if (!hasLaboratory && enoughArmyForTech && now >= developmentTime(210.0f)) {
            wantedBuilding = Kind::Laboratory;
        } else if (!hasMotorPool && players_[team].tier >= 2) {
            wantedBuilding = Kind::MotorPool;
        } else if (needsTurret && now >= developmentTime(160.0f) && combat.size() >= 6) {
            wantedBuilding = Kind::Turret;
            buildAnchor = exposedHeadquarters;
        } else if (players_[team].tier >= 2 && pools < (players_[team].tier >= 3 ? 3 : 2) &&
                   players_[team].ore >= 700) {
            wantedBuilding = Kind::MotorPool;
        } else if (foundries < (difficulty == AIDifficulty::Expert ? 8 : 6) &&
                   players_[team].ore >= 700 && now >= developmentTime(180.0f) &&
                   ownOfKind(Kind::Foundry, true) == foundries &&
                   std::all_of(own.begin(), own.end(), [](const AISnapshot& producer) {
                       return producer.kind != Kind::Foundry || producer.queueSize > 0;
                   })) {
            // A growing bank and busy queues signal a throughput bottleneck.
            // Add capacity after essential supply, tech and expansion plans.
            wantedBuilding = Kind::Foundry;
        }
        if (wantedBuilding == Kind::Processor) {
            // Put delivery/supply infrastructure beside working mineral lines.
            float longestDelivery = 0;
            for (const AISnapshot& resource : resources) {
                if (!resource.currentlyVisible || resource.resourceRemaining < 200) continue;
                float delivery = std::numeric_limits<float>::max();
                for (const AISnapshot& depot : own) {
                    if (depot.kind == Kind::Headquarters || depot.kind == Kind::Processor) {
                        delivery = std::min(delivery, distanceSquared(depot.pos, resource.pos));
                    }
                }
                if (delivery > longestDelivery && delivery < 900.0f * 900.0f) {
                    longestDelivery = delivery;
                    buildAnchor = resource.pos;
                }
            }
        }
    } else if (!hasFoundry) {
        wantedBuilding = Kind::Foundry;
    } else if (!hasProcessor && now >= developmentTime(30.0f)) {
        wantedBuilding = Kind::Processor;
    } else if (needsTurret && now >= developmentTime(100.0f)) {
        wantedBuilding = Kind::Turret;
        buildAnchor = exposedHeadquarters;
    } else if (!hasLaboratory && now >= developmentTime(210.0f)) {
        wantedBuilding = Kind::Laboratory;
    } else if (richExpansionDiscovered && now >= developmentTime(400.0f)) {
        buildAnchor = bestExpansion;
        wantedBuilding = Kind::Headquarters;
        wantsExpansion = true;
    } else if (!hasMotorPool && players_[team].tier >= 2) {
        wantedBuilding = Kind::MotorPool;
    } else if (capacity(team) - supply(team) < 12 && processorCount < 10 && !processorUnderConstruction) {
        wantedBuilding = Kind::Processor;
        if (!headquarters.empty()) {
            buildAnchor = headquarters[static_cast<std::size_t>(processorCount) % headquarters.size()];
        }
    }

    const bool rebuildingEconomy = advancedAI &&
        static_cast<int>(workers.size()) + queuedUnits[static_cast<int>(Kind::Worker)] < 6;
    const int workerRecoveryBudget = rebuildingEconomy ? definition(Kind::Worker).cost : 0;
    if (!orphanedConstructionPending && wantedBuilding != Kind::Resource &&
        players_[team].ore >= definition(wantedBuilding).cost + workerRecoveryBudget) {
        buildingIssued = tryBuild(wantedBuilding, buildAnchor, wantsExpansion);
    } else if (!orphanedConstructionPending && wantsExpansion) {
        // Exploration takes time, so dispatch the builder while the economy saves the ore.
        tryBuild(Kind::Headquarters, buildAnchor, true, false);
    }

    bool researchIssued = false;
    for (const AISnapshot& lab : own) {
        if (lab.kind != Kind::Laboratory || lab.progress < 1.0f || lab.queueSize != 0 ||
            lab.queueHasResearch || now < developmentTime(285.0f) ||
            (advancedAI && (rebuildingEconomy || !enoughArmyForTech || needsSupply ||
                            (baseUnderThreat && players_[team].ore < 900)))) {
            continue;
        }

        int queueIndex = -1;
        Kind researchKind = Kind::Worker;
        if (players_[team].tier < 2) {
            queueIndex = 0;
        } else if (players_[team].weapons < 1) {
            queueIndex = 1;
            researchKind = Kind::Striker;
        } else if (players_[team].armor < 1) {
            queueIndex = 2;
            researchKind = Kind::Lancer;
        } else if (players_[team].tier < 3 && now >= developmentTime(600.0f)) {
            queueIndex = 0;
        } else if (players_[team].weapons < (advancedAI ? players_[team].tier : 3)) {
            queueIndex = 1;
            researchKind = Kind::Striker;
        } else if (players_[team].armor < (advancedAI ? players_[team].tier : 3)) {
            queueIndex = 2;
            researchKind = Kind::Lancer;
        }

        if (queueIndex >= 0) {
            Command research;
            research.type = CommandType::Research;
            research.team = team;
            research.units = {lab.id};
            research.kind = researchKind;
            research.queueIndex = queueIndex;
            researchIssued = issue(research, queueIndex == 0 ? "researching the next tier" : "researching upgrades");
        }
        break;
    }

    int reserve = 0;
    if (!hasFoundry) {
        reserve = definition(Kind::Foundry).cost;
    } else if (!hasProcessor && now >= developmentTime(20.0f)) {
        reserve = definition(Kind::Processor).cost;
    } else if (!hasLaboratory && now >= developmentTime(190.0f)) {
        reserve = definition(Kind::Laboratory).cost;
    } else if (players_[team].tier < 2 && hasLaboratory && now >= developmentTime(270.0f)) {
        reserve = 500 * players_[team].tier;
    } else if (richExpansionDiscovered && now >= developmentTime(370.0f)) {
        reserve = definition(Kind::Headquarters).cost;
    } else if (!hasMotorPool && players_[team].tier >= 2) {
        reserve = definition(Kind::MotorPool).cost;
    } else if (capacity(team) - supply(team) < 12 && processorCount < 10 && !processorUnderConstruction) {
        reserve = definition(Kind::Processor).cost;
    }
    if (advancedAI) {
        reserve = 0;
        if (wantedBuilding != Kind::Resource && !orphanedConstructionPending) {
            const bool urgent = wantedBuilding == Kind::Processor || !hasFoundry ||
                                (wantedBuilding == Kind::Turret && baseUnderThreat);
            if (urgent || !baseUnderThreat) reserve = definition(wantedBuilding).cost;
        } else if (!baseUnderThreat && enoughArmyForTech && !researchPending &&
                   ownOfKind(Kind::Laboratory, true) > 0 && players_[team].tier < 2 &&
                   now >= developmentTime(270.0f)) {
            reserve = 500 * players_[team].tier;
        }
    }
    if (buildingIssued || researchIssued) {
        reserve = 0;
    }

    auto canAffordProduction = [&](Kind kind) {
        // Saving for the next factory must never prevent a raided economy from
        // replacing its miners, including the zero-worker recovery case.
        const int productionReserve = rebuildingEconomy && kind == Kind::Worker ? 0 : reserve;
        return players_[team].ore - definition(kind).cost >= productionReserve &&
               supply(team) + definition(kind).supply <= capacity(team);
    };
    auto train = [&](Id producer, Kind kind) {
        if (!canAffordProduction(kind)) {
            return false;
        }
        Command commandToIssue;
        commandToIssue.type = CommandType::Train;
        commandToIssue.team = team;
        commandToIssue.units = {producer};
        commandToIssue.kind = kind;
        const bool accepted = issue(commandToIssue, std::string("training ") + definition(kind).name);
        if (accepted) {
            ++queuedUnits[static_cast<int>(kind)];
        }
        return accepted;
    };

    const int desiredWorkers = hqCount >= 2 ? tuning.multipleBaseWorkers : tuning.oneBaseWorkers;
    int queuedWorkers = 0;
    for (const AISnapshot& entity : own) {
        if (entity.kind == Kind::Headquarters) {
            queuedWorkers += static_cast<int>(entity.queueSize);
        }
    }
    for (const AISnapshot& hq : own) {
        if (hq.kind == Kind::Headquarters && hq.progress >= 1.0f && hq.queueSize < 2 &&
            static_cast<int>(workers.size()) + queuedWorkers < desiredWorkers && train(hq.id, Kind::Worker)) {
            ++queuedWorkers;
        }
    }

    const int scheduledArmy = now < developmentTime(240.0f) ? armyTarget(7) :
        (now < developmentTime(400.0f) ? armyTarget(advancedAI ? 24 : 14) :
         (now < developmentTime(800.0f) ? armyTarget(advancedAI ? 44 : 34) : armyTarget(advancedAI ? 70 : 60)));
    // The old Hard opening capped production below its own attack threshold
    // after reserving a scout, so it could never field the scheduled first wave.
    const int desiredArmy = advancedAI ? std::max(scheduledArmy,
        static_cast<int>(tuning.attackGroupSize) + 3) : scheduledArmy;
    int plannedArmy = static_cast<int>(combat.size());
    for (std::size_t i = 0; i < queuedUnits.size(); ++i) {
        if (isCombatUnit(static_cast<Kind>(i))) {
            plannedArmy += queuedUnits[i];
        }
    }

    const int desiredCounterLancers = std::min((desiredArmy * 3 + 4) / 5,
                                               2 * (observedArmor + observedAir));
    const int vulnerableInfantry = observedUnits[static_cast<int>(Kind::Lancer)] +
                                  observedUnits[static_cast<int>(Kind::Scout)];
    const int desiredCounterStrikers = std::min((desiredArmy * 3 + 4) / 5, 2 * vulnerableInfantry);
    const int desiredCounterBastions = std::min(std::max(1, desiredArmy / 4), (observedInfantry + 2) / 3);
    const int desiredCounterMortars = std::min(std::max(1, desiredArmy / 5), (observedBuildings + 1) / 2);
    const int desiredCounterKites = std::min(std::max(1, desiredArmy / 4),
        observedUnits[static_cast<int>(Kind::Mortar)] + observedAir);
    std::string productionFocus = "balanced production";
    if (observedArmor + observedAir > 0) {
        productionFocus = "Needles counter observed armor/air";
    } else if (vulnerableInfantry > 0) {
        productionFocus = "Embers counter observed light units";
    } else if (observedBuildings > 0 && players_[team].tier >= 3) {
        productionFocus = "siege counters observed structures";
    }

    for (const AISnapshot& foundry : own) {
        if (foundry.kind != Kind::Foundry || foundry.progress < 1.0f || foundry.queueSize >= 2 ||
            plannedArmy >= desiredArmy) {
            continue;
        }
        Kind next = Kind::Striker;
        if (plannedOfKind(Kind::Scout) == 0) {
            next = Kind::Scout;
        } else if (plannedOfKind(Kind::Lancer) < desiredCounterLancers) {
            next = Kind::Lancer;
        } else if (plannedOfKind(Kind::Striker) < desiredCounterStrikers) {
            next = Kind::Striker;
        } else if (plannedOfKind(Kind::Lancer) * 2 < plannedOfKind(Kind::Striker)) {
            next = Kind::Lancer;
        }
        if (train(foundry.id, next)) {
            ++plannedArmy;
        }
    }

    if (!researchIssued && players_[team].tier >= 2) {
        for (const AISnapshot& lab : own) {
            if (lab.kind == Kind::Laboratory && lab.progress >= 1.0f && lab.queueSize == 0 &&
                plannedOfKind(Kind::Mender) < std::max(1, plannedArmy / 8) && plannedArmy < desiredArmy &&
                train(lab.id, Kind::Mender)) {
                ++plannedArmy;
            }
        }
    }

    for (const AISnapshot& pool : own) {
        if (pool.kind != Kind::MotorPool || pool.progress < 1.0f || pool.queueSize >= 2 ||
            plannedArmy >= desiredArmy) {
            continue;
        }
        Kind next = Kind::Bastion;
        if (players_[team].tier >= 3) {
            const int bastions = plannedOfKind(Kind::Bastion);
            const int mortars = plannedOfKind(Kind::Mortar);
            const int kites = plannedOfKind(Kind::Kite);
            if (kites < desiredCounterKites) {
                next = Kind::Kite;
            } else if (mortars < desiredCounterMortars) {
                next = Kind::Mortar;
            } else if (bastions < desiredCounterBastions) {
                next = Kind::Bastion;
            } else if (kites == 0) {
                next = Kind::Kite;
            } else if (mortars == 0 || mortars * 2 < bastions) {
                next = Kind::Mortar;
            } else if (kites * 2 < bastions) {
                next = Kind::Kite;
            }
        }
        if (train(pool.id, next)) {
            ++plannedArmy;
        }
    }

    if (advancedAI) {
        // Recover the battle plan from ordinary unit orders. It therefore survives
        // saves/replays without private AI state or access to hidden opponents.
        auto strength = [&](const AISnapshot& unit) {
            const Definition& d = definition(unit.kind);
            if (unit.progress < 1.0f || (d.damage <= 0 && unit.kind != Kind::Mender)) {
                return 0.0f;
            }
            const float dps = unit.kind == Kind::Mender ? 9.0f : d.damage / std::max(0.1f, d.cooldown);
            const float role = unit.kind == Kind::Worker ? 0.2f : 1.0f;
            return role * std::sqrt(unit.hp * dps) * (1.0f + d.armor * 0.065f) *
                   (1.0f + std::min(610.0f, d.range) / 1800.0f);
        };
        auto enemyStrengthNear = [&](Vec2 point, float radius) {
            float total = 0.0f;
            for (const AISnapshot& enemy : visibleEnemies) {
                if (distanceSquared(enemy.pos, point) <= radius * radius) {
                    total += strength(enemy);
                }
            }
            return total;
        };
        auto sendGroup = [&](CommandType type, const std::vector<Id>& ids, Vec2 point,
                             const std::string& description) {
            if (ids.empty()) {
                return false;
            }
            Command next;
            next.type = type;
            next.team = team;
            next.units = ids;
            next.point = point;
            return issue(next, description);
        };
        auto goingTo = [&](const AISnapshot& unit, Order order, Vec2 point, float tolerance = 260.0f) {
            return unit.order == order && distanceSquared(unit.goal, point) <= tolerance * tolerance;
        };
        auto boundedPoint = [&](Vec2 point) {
            return Vec2{std::clamp(point.x, 70.0f, worldSize() - 70.0f),
                        std::clamp(point.y, 70.0f, worldSize() - 70.0f)};
        };

        // One scout keeps its route even while the main army can see an enemy.
        // A scout retreats from actual weapon reach, not from harmless buildings.
        Id scoutId = 0;
        for (const AISnapshot& unit : own) {
            if (unit.kind != Kind::Scout || unit.progress < 1.0f || unit.hp < unit.maxHp * 0.35f) {
                continue;
            }
            scoutId = unit.id;
            const AISnapshot* danger = nullptr;
            float dangerDistance = std::numeric_limits<float>::max();
            for (const AISnapshot& enemy : visibleEnemies) {
                const Definition& d = definition(enemy.kind);
                const float distance = distanceSquared(unit.pos, enemy.pos);
                const float safetyRange = d.range + 180.0f;
                if (d.damage > 0 && enemy.kind != Kind::Worker && enemy.progress >= 1.0f &&
                    distance < safetyRange * safetyRange && distance < dangerDistance) {
                    danger = &enemy;
                    dangerDistance = distance;
                }
            }
            if (danger != nullptr) {
                const float distance = std::sqrt(std::max(1.0f, dangerDistance));
                const Vec2 escape = boundedPoint({unit.pos.x + (unit.pos.x - danger->pos.x) * 550.0f / distance,
                                                 unit.pos.y + (unit.pos.y - danger->pos.y) * 550.0f / distance});
                if (!goingTo(unit, Order::Move, escape, 180.0f)) {
                    sendGroup(CommandType::Move, {scoutId}, escape, "preserving reconnaissance");
                }
            } else if (unit.order != Order::Move || visible(team, unit.goal)) {
                const Vec2 destination = needsExpansionScouting && now >= developmentTime(180.0f) ?
                    expansionScoutTarget : reconnaissanceTarget(unit.pos, true);
                if (!goingTo(unit, Order::Move, destination, 100.0f)) {
                    sendGroup(CommandType::Move, {scoutId}, destination, "scouting enemy and expansion routes");
                }
            }
            break;
        }

        std::vector<const AISnapshot*> soldiers;
        std::vector<const AISnapshot*> healers;
        for (const AISnapshot& unit : own) {
            if (!isCombatUnit(unit.kind) || unit.progress < 1.0f || unit.id == scoutId) {
                continue;
            }
            (unit.kind == Kind::Mender ? healers : soldiers).push_back(&unit);
        }

        // Unarmed economy structures cannot scare an army into retreating. Local
        // force estimates include weapon output, remaining health, armor and range.
        const AISnapshot* baseThreat = nullptr;
        float baseThreatPower = 0.0f;
        for (const AISnapshot& enemy : visibleEnemies) {
            if (definition(enemy.kind).damage <= 0 || enemy.kind == Kind::Worker || enemy.progress < 1.0f) {
                continue;
            }
            const bool nearBase = std::any_of(headquarters.begin(), headquarters.end(), [&](Vec2 base) {
                return distanceSquared(base, enemy.pos) <= 1000.0f * 1000.0f;
            });
            if (!nearBase) {
                continue;
            }
            const float power = enemyStrengthNear(enemy.pos, 650.0f);
            if (power > baseThreatPower) {
                baseThreatPower = power;
                baseThreat = &enemy;
            }
        }
        std::vector<Id> defenders;
        if (baseThreat != nullptr) {
            auto nearest = soldiers;
            std::stable_sort(nearest.begin(), nearest.end(), [&](const AISnapshot* a, const AISnapshot* b) {
                return distanceSquared(a->pos, baseThreat->pos) < distanceSquared(b->pos, baseThreat->pos);
            });
            float airNeeded = 0.0f;
            float groundNeeded = 0.0f;
            for (const AISnapshot& enemy : visibleEnemies) {
                if (distanceSquared(enemy.pos, baseThreat->pos) > 650.0f * 650.0f) {
                    continue;
                }
                (definition(enemy.kind).air ? airNeeded : groundNeeded) += strength(enemy) * 1.4f;
            }
            // Cover aircraft with actual anti-air weapons first. Ground troops
            // still answer a mixed raid even when its first spotted unit flies.
            for (const AISnapshot* unit : nearest) {
                if (airNeeded <= 0.0f) break;
                if (!definition(unit->kind).antiAir) continue;
                defenders.push_back(unit->id);
                const float power = strength(*unit);
                groundNeeded -= std::max(0.0f, power - airNeeded);
                airNeeded -= power;
            }
            for (const AISnapshot* unit : nearest) {
                if (groundNeeded <= 0.0f) break;
                if (std::find(defenders.begin(), defenders.end(), unit->id) != defenders.end()) continue;
                defenders.push_back(unit->id);
                groundNeeded -= strength(*unit);
            }
        }
        auto defending = [&](Id id) {
            return std::find(defenders.begin(), defenders.end(), id) != defenders.end();
        };

        Vec2 assaultTarget = strategicTarget;
        Vec2 forwardCenter{};
        int forwardCount = 0;
        int marchingCount = 0;
        Vec2 marchingGoal{};
        int withdrawingCount = 0;
        for (const AISnapshot* unit : soldiers) {
            if (defending(unit->id)) {
                continue;
            }
            if (goingTo(*unit, Order::Move, home, 400.0f) &&
                distanceSquared(unit->pos, home) > 650.0f * 650.0f) {
                ++withdrawingCount;
            }
            if (unit->order == Order::AttackMove && distanceSquared(unit->goal, home) > 1000.0f * 1000.0f) {
                marchingGoal.x += unit->goal.x;
                marchingGoal.y += unit->goal.y;
                ++marchingCount;
            }
            if ((unit->order == Order::Attack || unit->order == Order::AttackMove) &&
                distanceSquared(unit->pos, home) > 850.0f * 850.0f) {
                forwardCenter.x += unit->pos.x;
                forwardCenter.y += unit->pos.y;
                ++forwardCount;
            }
        }
        if (marchingCount > 0) {
            assaultTarget = {marchingGoal.x / marchingCount, marchingGoal.y / marchingCount};
            // Finish the accepted route until the army actually clears the area;
            // merely spotting a closer unit must not redirect the whole attack.
            const bool areaClear = visible(team, assaultTarget) && std::none_of(
                visibleEnemies.begin(), visibleEnemies.end(), [&](const AISnapshot& enemy) {
                    return distanceSquared(enemy.pos, assaultTarget) < 650.0f * 650.0f;
                });
            if (areaClear) {
                assaultTarget = strategicTarget;
            }
        }
        if (forwardCount > 0) {
            forwardCenter.x /= forwardCount;
            forwardCenter.y /= forwardCount;
        } else {
            forwardCenter = home;
        }
        bool committed = marchingCount > 0 || forwardCount >= 2;
        const bool regrouping = withdrawingCount >= 2 && withdrawingCount * 2 >= static_cast<int>(soldiers.size());
        committed = committed && !regrouping;

        // Assemble around the largest nearby group, rather than counting units
        // scattered across the map as one army. Existing assaults continue below
        // the launch threshold; reinforcements travel in packets or join nearby.
        Vec2 assembly = home;
        int clustered = 0;
        for (const AISnapshot* candidate : soldiers) {
            if (defending(candidate->id)) {
                continue;
            }
            int nearby = 0;
            for (const AISnapshot* unit : soldiers) {
                if (!defending(unit->id) && distanceSquared(candidate->pos, unit->pos) < 700.0f * 700.0f) {
                    ++nearby;
                }
            }
            if (nearby > clustered) {
                clustered = nearby;
                assembly = candidate->pos;
            }
        }
        const int available = static_cast<int>(soldiers.size() - defenders.size());
        const bool launch = !regrouping && now >= tuning.attackStartSeconds &&
            available >= static_cast<int>(tuning.attackGroupSize) &&
            clustered >= std::max(static_cast<int>(tuning.attackGroupSize), (available * 2 + 2) / 3);
        std::vector<Id> rearReinforcements;
        for (const AISnapshot* unit : soldiers) {
            if (!defending(unit->id) && unit->order != Order::Attack && unit->order != Order::AttackMove &&
                distanceSquared(unit->pos, forwardCenter) > 950.0f * 950.0f) {
                rearReinforcements.push_back(unit->id);
            }
        }
        const bool reinforce = rearReinforcements.size() >= 3;
        std::vector<Id> advance;
        std::vector<Id> gather;
        std::vector<Id> retreat;
        std::vector<std::pair<Id, float>> assignedDamage;
        for (const AISnapshot* unit : soldiers) {
            const Definition& d = definition(unit->kind);
            const bool defend = defending(unit->id);
            const Vec2 destination = defend ? baseThreat->pos : assaultTarget;
            const bool rear = std::find(rearReinforcements.begin(), rearReinforcements.end(), unit->id) != rearReinforcements.end();
            const bool attacking = defend || launch || (committed && (!rear || reinforce));
            float friendly = 0.0f;
            for (const AISnapshot& ally : own) {
                if (distanceSquared(unit->pos, ally.pos) < 750.0f * 750.0f) {
                    friendly += strength(ally);
                }
            }
            const float opposition = enemyStrengthNear(unit->pos, 700.0f);
            const bool losingAway = opposition > friendly * 1.9f && opposition > 90.0f &&
                distanceSquared(unit->pos, home) > 1100.0f * 1100.0f;
            if (!defend && (regrouping || losingAway)) {
                if (!goingTo(*unit, Order::Move, home, 400.0f)) {
                    retreat.push_back(unit->id);
                }
                continue;
            }

            // A damaged unit is useful until there is a real recovery route. Do
            // not permanently bench every survivor below an arbitrary HP cutoff.
            const AISnapshot* medic = nullptr;
            float medicDistance = 800.0f * 800.0f;
            for (const AISnapshot* healer : healers) {
                const float distance = distanceSquared(unit->pos, healer->pos);
                if (distance < medicDistance && enemyStrengthNear(healer->pos, 320.0f) < friendly * 0.5f) {
                    medicDistance = distance;
                    medic = healer;
                }
            }
            if (medic != nullptr && unit->hp < unit->maxHp * (unit->order == Order::Hold ? 0.65f : 0.25f)) {
                if (medicDistance <= 180.0f * 180.0f) {
                    if (unit->order != Order::Hold) {
                        sendGroup(CommandType::Hold, {unit->id}, unit->pos, "repairing damaged veterans");
                    }
                } else if (!goingTo(*unit, Order::Move, medic->pos, 150.0f)) {
                    sendGroup(CommandType::Move, {unit->id}, medic->pos, "pulling damaged units toward support");
                }
                continue;
            }

            const AISnapshot* target = nullptr;
            float bestScore = -std::numeric_limits<float>::max();
            for (const AISnapshot& enemy : visibleEnemies) {
                const Definition& ed = definition(enemy.kind);
                if (ed.air && !d.antiAir) {
                    continue;
                }
                const float distance = std::sqrt(distanceSquared(unit->pos, enemy.pos));
                const float acquisition = std::min(d.vision, d.range + 170.0f) + ed.radius;
                if (distance > acquisition || (!attacking && distance > d.range + ed.radius)) {
                    continue;
                }
                float damage = d.damage;
                if (unit->kind == Kind::Lancer && (ed.armor >= 4 || ed.air)) damage *= 1.8f;
                if (unit->kind == Kind::Striker && (enemy.kind == Kind::Scout || enemy.kind == Kind::Lancer)) damage *= 1.35f;
                if (unit->kind == Kind::Bastion && (enemy.kind == Kind::Worker || enemy.kind == Kind::Striker || enemy.kind == Kind::Lancer)) damage *= 1.4f;
                if (unit->kind == Kind::Mortar && ed.building) damage *= 1.65f;
                damage = std::max(1.0f, damage - ed.armor);
                float allocated = 0.0f;
                for (const auto& assignment : assignedDamage) {
                    if (assignment.first == enemy.id) allocated += assignment.second;
                }
                float score = (enemy.kind == Kind::Worker ? 160.0f : ed.damage > 0 ? 260.0f :
                               enemy.kind == Kind::Mender ? 240.0f : 70.0f) - distance * 0.3f;
                score += damage / std::max(0.1f, d.cooldown) * 2.0f;
                score += (1.0f - enemy.hp / enemy.maxHp) * 90.0f;
                // A slow siege volley should not chase a nearly dead worker
                // when its damage could destroy valuable infrastructure.
                const float wastedShot = std::max(0.0f, damage - enemy.hp) / damage;
                score -= wastedShot * (unit->kind == Kind::Mortar ? 100.0f : 25.0f);
                if (distance <= d.range + ed.radius) score += 95.0f;
                if (unit->target == enemy.id) score += 30.0f;
                if (allocated >= enemy.hp) score -= 300.0f;
                if (unit->kind == Kind::Mortar && ed.building) score += 200.0f;
                if (unit->kind == Kind::Mortar && distance < 150.0f) score -= 150.0f;
                if (score > bestScore) {
                    bestScore = score;
                    target = &enemy;
                }
            }
            if (target != nullptr) {
                assignedDamage.emplace_back(target->id, d.damage);
                if (unit->order != Order::Attack || unit->target != target->id) {
                    Command attack;
                    attack.type = CommandType::Attack;
                    attack.team = team;
                    attack.units = {unit->id};
                    attack.target = target->id;
                    issue(attack, defend ? "intercepting a base raid" : "focusing local combat targets");
                }
            } else if (attacking) {
                if (!goingTo(*unit, Order::AttackMove, destination, 420.0f)) {
                    if (defend) {
                        sendGroup(CommandType::AttackMove, {unit->id}, destination, "reinforcing the threatened base");
                    } else {
                        advance.push_back(unit->id);
                    }
                }
            } else {
                const Vec2 gatherPoint = regrouping || committed ? home : assembly;
                if (distanceSquared(unit->pos, gatherPoint) > 450.0f * 450.0f &&
                    !goingTo(*unit, Order::Move, gatherPoint, 300.0f)) {
                    gather.push_back(unit->id);
                } else if (unit->order == Order::Hold) {
                    sendGroup(CommandType::Stop, {unit->id}, unit->pos, "returning repaired units to the army");
                }
            }
        }
        sendGroup(CommandType::Move, retreat, home, "withdrawing from a losing local battle");
        sendGroup(CommandType::Move, gather, committed || regrouping ? home : assembly, "assembling reinforcements");
        sendGroup(CommandType::AttackMove, advance, assaultTarget, committed ? "reinforcing the committed assault" : "launching a concentrated assault");

        // Escort is a real command with normal healing rules. Unlike reissuing a
        // mixed Attack, attaching support never resets its frontline leader.
        for (const AISnapshot* healer : healers) {
            const AISnapshot* leader = nullptr;
            float best = std::numeric_limits<float>::max();
            for (const AISnapshot* unit : soldiers) {
                const Entity* live = find(unit->id);
                if (live == nullptr || live->order == Order::Move) {
                    continue;
                }
                const float score = distanceSquared(healer->pos, unit->pos) /
                    (unit->kind == Kind::Bastion ? 1.5f : 1.0f);
                if (score < best) {
                    best = score;
                    leader = unit;
                }
            }
            const Entity* liveHealer = find(healer->id);
            if (leader != nullptr && liveHealer != nullptr &&
                (liveHealer->order != Order::Escort || liveHealer->sustained.escortTarget != leader->id)) {
                Command escort;
                escort.type = CommandType::Escort;
                escort.team = team;
                escort.units = {healer->id};
                escort.target = leader->id;
                issue(escort, "keeping repair support with the frontline");
            }
        }
    } else {
    if (!hurtCombat.empty()) {
        std::vector<Id> unitsToRetreat;
        for (Id id : hurtCombat) {
            const auto found = std::find_if(own.begin(), own.end(), [&](const AISnapshot& entity) {
                return entity.id == id;
            });
            if (found != own.end() && distanceSquared(found->pos, home) > 450.0f * 450.0f &&
                (found->order != Order::Move || distanceSquared(found->goal, home) > 100.0f * 100.0f)) {
                unitsToRetreat.push_back(id);
            }
        }
        Command retreat;
        retreat.type = CommandType::Move;
        retreat.team = team;
        retreat.units = unitsToRetreat;
        retreat.point = home;
        if (!unitsToRetreat.empty()) {
            issue(retreat, "retreating damaged units");
        }
    }

    Id reconnaissanceScoutId = 0;
    if (now >= developmentTime(350.0f) && needsExpansionScouting && visibleEnemies.empty()) {
        for (const AISnapshot& scout : own) {
            if (scout.kind == Kind::Scout && scout.progress >= 1.0f && scout.order == Order::Move &&
                scout.hp / scout.maxHp >= 0.35f &&
                distanceSquared(scout.goal, expansionScoutTarget) <= 100.0f * 100.0f) {
                reconnaissanceScoutId = scout.id;
                break;
            }
        }
        if (reconnaissanceScoutId == 0) {
            for (const AISnapshot& scout : own) {
                if (scout.kind != Kind::Scout || scout.progress < 1.0f || scout.hp / scout.maxHp < 0.35f) {
                    continue;
                }
                Command move;
                move.type = CommandType::Move;
                move.team = team;
                move.units = {scout.id};
                move.point = expansionScoutTarget;
                if (issue(move, "checking an expansion site")) {
                    reconnaissanceScoutId = scout.id;
                }
                break;
            }
        }
    }

    // Keep one healthy Skim gathering information independently of army orders.
    // Finish an unseen route before choosing another; once the destination is
    // observed, its newer timestamp sends the scout toward an older report.
    if (reconnaissanceScoutId == 0 && visibleEnemies.empty()) {
        for (const AISnapshot& scout : own) {
            if (scout.kind != Kind::Scout || scout.progress < 1.0f || scout.hp / scout.maxHp < 0.35f) {
                continue;
            }
            const bool knownRoute = std::any_of(reconnaissanceSites.begin(), reconnaissanceSites.end(), [&](Vec2 site) {
                return distanceSquared(scout.goal, site) <= 100.0f * 100.0f;
            });
            if (scout.order == Order::Move && knownRoute && !visible(team, scout.goal)) {
                reconnaissanceScoutId = scout.id;
                break;
            }
            const Vec2 destination = reconnaissanceTarget(scout.pos, false);
            Command move;
            move.type = CommandType::Move;
            move.team = team;
            move.units = {scout.id};
            move.point = destination;
            if (issue(move, "scouting the least recently observed site")) {
                reconnaissanceScoutId = scout.id;
            }
            break;
        }
    }

    std::vector<const AISnapshot*> combatTargets;
    for (const AISnapshot& enemy : visibleEnemies) {
        const bool threatensBase = std::any_of(headquarters.begin(), headquarters.end(), [&](Vec2 hq) {
            return distanceSquared(hq, enemy.pos) <= 1200.0f * 1200.0f;
        });
        if (threatensBase ||
            (now >= tuning.attackStartSeconds && healthyCombat.size() >= tuning.attackGroupSize)) {
            combatTargets.push_back(&enemy);
        }
    }

    int visibleEnemyCombat = 0;
    for (const AISnapshot* enemy : combatTargets) {
        if (isCombatUnit(enemy->kind) || definition(enemy->kind).building) {
            ++visibleEnemyCombat;
        }
    }

    const bool overwhelmed = visibleEnemyCombat > static_cast<int>(healthyCombat.size()) * 2 &&
                              visibleEnemyCombat >= 4;
    if (overwhelmed && !healthyCombat.empty()) {
        std::vector<Id> unitsToRetreat;
        for (Id id : healthyCombat) {
            const auto found = std::find_if(own.begin(), own.end(), [&](const AISnapshot& entity) {
                return entity.id == id;
            });
            if (found != own.end() && distanceSquared(found->pos, home) > 450.0f * 450.0f &&
                (found->order != Order::Move || distanceSquared(found->goal, home) > 100.0f * 100.0f)) {
                unitsToRetreat.push_back(id);
            }
        }
        Command retreat;
        retreat.type = CommandType::Move;
        retreat.team = team;
        retreat.units = unitsToRetreat;
        retreat.point = home;
        if (!unitsToRetreat.empty()) {
            issue(retreat, "regrouping at the base");
        }
    } else if (!healthyCombat.empty() && !combatTargets.empty()) {
        const AISnapshot* target = nullptr;
        float best = std::numeric_limits<float>::max();
        for (const AISnapshot* enemy : combatTargets) {
            const float d = distanceSquared(armyCenter, enemy->pos);
            if (d < best || (d == best && (target == nullptr || enemy->id < target->id))) {
                best = d;
                target = enemy;
            }
        }
        if (target != nullptr) {
            std::vector<Id> unitsToAttack;
            bool hasArmedLeader = false;
            bool hasMenderNeedingLeader = false;
            auto eligibleArmedUnit = [&](const AISnapshot& unit) {
                const Definition& unitDefinition = definition(unit.kind);
                return !unitDefinition.building && unitDefinition.damage > 0 &&
                       (!definition(target->kind).air || unitDefinition.antiAir);
            };
            for (Id id : healthyCombat) {
                const auto found = std::find_if(own.begin(), own.end(), [&](const AISnapshot& entity) {
                    return entity.id == id;
                });
                if (found == own.end()) {
                    continue;
                }
                if (found->kind == Kind::Mender) {
                    const auto leader = std::find_if(own.begin(), own.end(), [&](const AISnapshot& entity) {
                        return entity.id == found->target && !definition(entity.kind).building &&
                               definition(entity.kind).damage > 0;
                    });
                    const bool validFollow = found->order == Order::Attack && leader != own.end();
                    if (!validFollow) {
                        unitsToAttack.push_back(id);
                        hasMenderNeedingLeader = true;
                    }
                } else if (eligibleArmedUnit(*found) &&
                           (found->order != Order::Attack || found->target != target->id)) {
                    unitsToAttack.push_back(id);
                    hasArmedLeader = true;
                }
            }
            if (hasMenderNeedingLeader && !hasArmedLeader) {
                for (Id id : healthyCombat) {
                    const auto leader = std::find_if(own.begin(), own.end(), [&](const AISnapshot& entity) {
                        return entity.id == id;
                    });
                    if (leader != own.end() && eligibleArmedUnit(*leader)) {
                        unitsToAttack.push_back(id);
                        hasArmedLeader = true;
                        break;
                    }
                }
            }
            Command attack;
            attack.type = CommandType::Attack;
            attack.team = team;
            attack.units = unitsToAttack;
            attack.target = target->id;
            if (!unitsToAttack.empty() && (!hasMenderNeedingLeader || hasArmedLeader)) {
                issue(attack, "engaging a visible threat");
            }
        }
    } else if (now >= tuning.attackStartSeconds &&
               ((tick_ / 40) % tuning.attackIntervalSlots == 0)) {
        std::vector<Id> advancingUnits;
        for (Id id : healthyCombat) {
            if (id != reconnaissanceScoutId) {
                advancingUnits.push_back(id);
            }
        }
        Command attackMove;
        attackMove.type = CommandType::AttackMove;
        attackMove.team = team;
        attackMove.units = advancingUnits;
        attackMove.point = strategicTarget;
        if (advancingUnits.size() >= tuning.attackGroupSize) {
            issue(attackMove, objective);
        }
    }

    }

    aiStatus_ = "Workers " + std::to_string(workers.size()) + "/" +
                std::to_string(desiredWorkers) + ", army " + std::to_string(combat.size());
    if (!action.empty()) {
        aiStatus_ += ", " + action;
    } else if (reserve > players_[team].ore) {
        aiStatus_ += ", saving " + std::to_string(reserve - players_[team].ore) + " ore";
    } else {
        aiStatus_ += ", holding";
    }
    aiStatus_ += "; " + objective + "; " + productionFocus;
}

} // namespace cinder
