#include "Sim/Simulation.h"

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
    constexpr int team = 1;
    constexpr float pi = 3.14159265358979323846f;
    const float now = time();

    std::vector<AISnapshot> own;
    std::vector<AISnapshot> visibleEnemies;
    std::vector<AISnapshot> resources;
    std::array<int, 15> queuedUnits{};
    own.reserve(entities_.size());
    visibleEnemies.reserve(entities_.size());
    resources.reserve(entities_.size());

    for (const Entity& entity : entities_) {
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
        snapshot.currentlyVisible = visible(team, entity.pos);
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
    Vec2 home = retreatBases.empty() ? Vec2{4200.0f, 4200.0f} : retreatBases.front();
    float bestSafety = -1.0f;
    for (Vec2 base : retreatBases) {
        float nearestVisibleEnemy = std::numeric_limits<float>::max();
        for (const AISnapshot& enemy : visibleEnemies) {
            nearestVisibleEnemy = std::min(nearestVisibleEnemy, distanceSquared(base, enemy.pos));
        }
        const bool safer = nearestVisibleEnemy > bestSafety;
        const bool equallySafeAndCloserToStart = nearestVisibleEnemy == bestSafety &&
            distanceSquared(base, Vec2{4200.0f, 4200.0f}) <
                distanceSquared(home, Vec2{4200.0f, 4200.0f});
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

    // New workers and workers released from completed construction resume
    // harvesting through the same checks as player-issued gather commands.
    for (const AISnapshot& worker : own) {
        if (!availableWorker(worker.id)) {
            continue;
        }
        const Entity* liveWorker = find(worker.id);
        if (liveWorker->order != Order::Idle && liveWorker->order != Order::Hold) {
            continue;
        }
        const AISnapshot* resource = nullptr;
        float best = std::numeric_limits<float>::max();
        for (const AISnapshot& candidate : resources) {
            if (!candidate.currentlyVisible || candidate.resourceRemaining <= 0) {
                continue;
            }
            const float d = distanceSquared(worker.pos, candidate.pos);
            if (d < best) {
                best = d;
                resource = &candidate;
            }
        }
        if (resource != nullptr) {
            Command gather;
            gather.type = CommandType::Gather;
            gather.team = team;
            gather.units = {worker.id};
            gather.target = resource->id;
            issue(gather, "assigning workers");
        }
    }

    auto tryBuild = [&](Kind kind, Vec2 anchor, bool expansion) {
        static constexpr std::array<float, 4> localRadii{220.0f, 340.0f, 470.0f, 610.0f};
        static constexpr std::array<float, 4> expansionRadii{280.0f, 390.0f, 500.0f, 620.0f};
        const auto& radii = expansion ? expansionRadii : localRadii;
        const int phase = (static_cast<int>(kind) * 5 + static_cast<int>(tick_ / 40)) % 16;

        for (float radius : radii) {
            for (int step = 0; step < 16; ++step) {
                const int spoke = (step + phase) % 16;
                const float angle = 2.0f * pi * static_cast<float>(spoke) / 16.0f;
                const Vec2 point{anchor.x + std::cos(angle) * radius,
                                 anchor.y + std::sin(angle) * radius};
                if (point.x < 80.0f || point.y < 80.0f ||
                    point.x > WorldSize - 80.0f || point.y > WorldSize - 80.0f ||
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

    static constexpr std::array<Vec2, 4> expansionSites{{
        {2900.0f, 3800.0f}, {3800.0f, 2000.0f}, {1900.0f, 1000.0f}, {1000.0f, 2800.0f}}};
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

    bool buildingIssued = false;
    Kind wantedBuilding = Kind::Resource;
    Vec2 buildAnchor = home;
    bool wantsExpansion = false;

    if (!hasFoundry) {
        wantedBuilding = Kind::Foundry;
    } else if (!hasProcessor && now >= 30.0f) {
        wantedBuilding = Kind::Processor;
    } else if (needsTurret && now >= 100.0f) {
        wantedBuilding = Kind::Turret;
        buildAnchor = exposedHeadquarters;
    } else if (!hasLaboratory && now >= 210.0f) {
        wantedBuilding = Kind::Laboratory;
    } else if (richExpansionDiscovered && now >= 400.0f) {
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

    if (!orphanedConstructionPending && wantedBuilding != Kind::Resource &&
        players_[team].ore >= definition(wantedBuilding).cost) {
        buildingIssued = tryBuild(wantedBuilding, buildAnchor, wantsExpansion);
    } else if (!orphanedConstructionPending && wantsExpansion) {
        // Exploration takes time, so dispatch the builder while the economy saves the ore.
        tryBuild(Kind::Headquarters, buildAnchor, true);
    }

    bool researchIssued = false;
    for (const AISnapshot& lab : own) {
        if (lab.kind != Kind::Laboratory || lab.progress < 1.0f || lab.queueSize != 0 ||
            lab.queueHasResearch || now < 285.0f) {
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
        } else if (players_[team].tier < 3 && now >= 600.0f) {
            queueIndex = 0;
        } else if (players_[team].weapons < 3) {
            queueIndex = 1;
            researchKind = Kind::Striker;
        } else if (players_[team].armor < 3) {
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
    } else if (!hasProcessor && now >= 20.0f) {
        reserve = definition(Kind::Processor).cost;
    } else if (!hasLaboratory && now >= 190.0f) {
        reserve = definition(Kind::Laboratory).cost;
    } else if (players_[team].tier < 2 && hasLaboratory && now >= 270.0f) {
        reserve = 500 * players_[team].tier;
    } else if (richExpansionDiscovered && now >= 370.0f) {
        reserve = definition(Kind::Headquarters).cost;
    } else if (!hasMotorPool && players_[team].tier >= 2) {
        reserve = definition(Kind::MotorPool).cost;
    } else if (capacity(team) - supply(team) < 12 && processorCount < 10 && !processorUnderConstruction) {
        reserve = definition(Kind::Processor).cost;
    }
    if (buildingIssued || researchIssued) {
        reserve = 0;
    }

    auto canAffordProduction = [&](Kind kind) {
        return players_[team].ore - definition(kind).cost >= reserve &&
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
        return issue(commandToIssue, std::string("training ") + definition(kind).name);
    };

    const int desiredWorkers = hqCount >= 2 ? 18 : 12;
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

    const int desiredArmy = now < 240.0f ? 7 : (now < 400.0f ? 14 : (now < 800.0f ? 34 : 60));
    int plannedArmy = static_cast<int>(combat.size());
    for (const AISnapshot& entity : own) {
        if ((entity.kind == Kind::Foundry || entity.kind == Kind::MotorPool ||
             entity.kind == Kind::Laboratory) && entity.queueSize > 0) {
            plannedArmy += static_cast<int>(entity.queueSize);
        }
    }

    for (const AISnapshot& foundry : own) {
        if (foundry.kind != Kind::Foundry || foundry.progress < 1.0f || foundry.queueSize >= 2 ||
            plannedArmy >= desiredArmy) {
            continue;
        }
        Kind next = Kind::Striker;
        if (plannedOfKind(Kind::Scout) == 0) {
            next = Kind::Scout;
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
            if (kites == 0) {
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

    Id expansionScoutId = 0;
    if (now >= 350.0f && needsExpansionScouting && visibleEnemies.empty()) {
        for (const AISnapshot& scout : own) {
            if (scout.kind == Kind::Scout && scout.progress >= 1.0f && scout.order == Order::Move &&
                distanceSquared(scout.goal, expansionScoutTarget) <= 100.0f * 100.0f) {
                expansionScoutId = scout.id;
                break;
            }
        }
        if (expansionScoutId == 0) {
            for (const AISnapshot& scout : own) {
                if (scout.kind != Kind::Scout || scout.progress < 1.0f) {
                    continue;
                }
                Command move;
                move.type = CommandType::Move;
                move.team = team;
                move.units = {scout.id};
                move.point = expansionScoutTarget;
                if (issue(move, "checking an expansion site")) {
                    expansionScoutId = scout.id;
                }
                break;
            }
        }
    }

    std::vector<const AISnapshot*> combatTargets;
    for (const AISnapshot& enemy : visibleEnemies) {
        const bool threatensBase = std::any_of(headquarters.begin(), headquarters.end(), [&](Vec2 hq) {
            return distanceSquared(hq, enemy.pos) <= 1200.0f * 1200.0f;
        });
        if (threatensBase || (now >= 240.0f && healthyCombat.size() >= 7)) {
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
    } else if (now >= 240.0f && ((tick_ / 40) % 12 == 0)) {
        std::vector<Id> advancingUnits;
        for (Id id : healthyCombat) {
            if (id != expansionScoutId) {
                advancingUnits.push_back(id);
            }
        }
        Command attackMove;
        attackMove.type = CommandType::AttackMove;
        attackMove.team = team;
        attackMove.units = advancingUnits;
        attackMove.point = {600.0f, 600.0f};
        if (advancingUnits.size() >= 7) {
            issue(attackMove, "advancing on the enemy start");
        }
    } else if (now < 240.0f && ownOfKind(Kind::Scout) > 0 && visibleEnemies.empty()) {
        for (const AISnapshot& scout : own) {
            if (scout.kind != Kind::Scout || scout.progress < 1.0f ||
                (scout.order != Order::Idle && scout.order != Order::Hold)) {
                continue;
            }
            static constexpr std::array<Vec2, 4> scoutRoute{{
                {2900.0f, 3800.0f}, {3800.0f, 2000.0f}, {1900.0f, 1000.0f}, {1000.0f, 2800.0f}}};
            const Vec2 destination = scoutRoute[(tick_ / 40 + scout.id) % scoutRoute.size()];
            Command move;
            move.type = CommandType::Move;
            move.team = team;
            move.units = {scout.id};
            move.point = destination;
            issue(move, "scouting the map");
            break;
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
}

} // namespace cinder
