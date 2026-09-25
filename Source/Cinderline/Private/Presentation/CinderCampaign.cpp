#include "Presentation/CinderCampaign.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace
{
constexpr std::uint32_t CampaignSeed = 0xE8B30100u;
constexpr std::uint64_t OpponentDecisionTicks = 15;
constexpr float PointTolerance = 130.0f;
constexpr std::uint64_t CurrentPhaseProof = std::uint64_t{1} << 63;

const std::array<FCinderCampaignMissionDefinition, FCinderCampaign::MissionCount> Missions{{
    {TEXT("First Shift"), TEXT("Bring the first Emberline relay back online."),
        TEXT("Camera, Drudges, ore, Kilns, Embers and Attack-move"), 8, 0, cinder::MatchLength::Short, false,
        TEXT("Deliver ore"), TEXT("Train three Embers"), TEXT("Clear the patrol")},
    {TEXT("Keep the Fires Fed"), TEXT("Move the salvage column onto a fresh seam."),
        TEXT("Resource rallies, crew, Siphons and relocation"), 6, 0, cinder::MatchLength::Short, false,
        TEXT("Rally a new miner"), TEXT("Open remote delivery"), TEXT("Deliver remote ore")},
    {TEXT("Eyes Beyond"), TEXT("Find the splinter posts hidden beyond the relay light."),
        TEXT("Skims, fog, Move, scouting and Attack-move"), 5, 1, cinder::MatchLength::Short, false,
        TEXT("Reveal both positions"), TEXT("Commit after scouting"), TEXT("Clear both threats")},
    {TEXT("Hold the Line"), TEXT("Hold the restored route against two organized strikes."),
        TEXT("Army rally, Wards, Defend and reinforcement"), 5, 2, cinder::MatchLength::Short, false,
        TEXT("Prepare the line"), TEXT("Clear the first wave"), TEXT("Clear the second wave")},
    {TEXT("Break the Siege"), TEXT("Read the siege line, advance the column and break its Anchor."),
        TEXT("Scouting, research, counters and advanced production"), 6, 1, cinder::MatchLength::Short, false,
        TEXT("Reveal the siege"), TEXT("Reach Tier 2 and upgrade"), TEXT("Destroy the rival Anchor")},
    {TEXT("Trial by Fire"), TEXT("Restore the last span under ordinary battle conditions."),
        TEXT("Independent Standard match against Normal AI"), 1, 0, cinder::MatchLength::Standard, true,
        TEXT("Build your own plan"), TEXT("Adapt to the opponent"), TEXT("Win the match")}
}};

using PhaseArray = std::vector<FCinderCampaignPhaseDefinition>;
const std::array<PhaseArray, FCinderCampaign::MissionCount> Phases{{
    PhaseArray{
        {TEXT("Look over the relay"), TEXT("Pan or zoom the battlefield."), TEXT("The camera lets you inspect distant work and threats without changing unit orders."), TEXT("Move the camera"), ECinderCampaignActionKind::Camera, ECinderCampaignTargetRole::None, cinder::Kind::Resource, 1, -1, true},
        {TEXT("Meet a Drudge"), TEXT("Select a Drudge — the robot that mines ore."), TEXT("A Drudge is your mining and construction robot."), TEXT("Select 1 Drudge"), ECinderCampaignActionKind::SelectWorker, ECinderCampaignTargetRole::PlayerAnchor, cinder::Kind::Worker, 1, -1, false},
        {TEXT("Bring ore home"), TEXT("Assign that Drudge to ore and let it complete one delivery."), TEXT("Workers carry ore home. That income pays for troops and buildings."), TEXT("Deliver ore"), ECinderCampaignActionKind::Gather, ECinderCampaignTargetRole::HomeOre, cinder::Kind::Worker, 1, -1, true},
        {TEXT("Grow the shift"), TEXT("Train one new Drudge."), TEXT("More Drudges gather faster, but every unit uses crew room."), TEXT("Train 1 Drudge"), ECinderCampaignActionKind::Train, ECinderCampaignTargetRole::PlayerAnchor, cinder::Kind::Worker, 1, -1, true},
        {TEXT("Raise a Kiln"), TEXT("Build the structure that trains field units."), TEXT("A Kiln trains your first combat troops."), TEXT("Complete 1 Kiln"), ECinderCampaignActionKind::Build, ECinderCampaignTargetRole::BuildSite, cinder::Kind::Foundry, 1, -1, true},
        {TEXT("Form a patrol"), TEXT("Train three Embers from the Kiln."), TEXT("Embers are affordable ranged troops. Replacements count if one is lost."), TEXT("Train 3 Embers"), ECinderCampaignActionKind::Train, ECinderCampaignTargetRole::BuildSite, cinder::Kind::Striker, 3, -1, false},
        {TEXT("Advance fighting"), TEXT("Send combat troops toward the hostile patrol with Attack-move."), TEXT("Attack-move advances while stopping to fight threats along the route."), TEXT("Issue Attack-move"), ECinderCampaignActionKind::AttackMove, ECinderCampaignTargetRole::ForwardApproach, cinder::Kind::Striker, 1, -1, true},
        {TEXT("Clear the relay"), TEXT("Defeat the hostile patrol."), TEXT("Keep reinforcing if the first attack fails; the patrol cannot replace its losses."), TEXT("Patrol remaining"), ECinderCampaignActionKind::Destroy, ECinderCampaignTargetRole::EnemyObjective, cinder::Kind::Striker, 0, -1, false}
    },
    PhaseArray{
        {TEXT("Direct new miners"), TEXT("Set the Anchor's worker rally to a live ore node."), TEXT("A resource rally sends new Drudges to that mine; current workers keep their orders."), TEXT("Set ore rally"), ECinderCampaignActionKind::Rally, ECinderCampaignTargetRole::HomeOre, cinder::Kind::Headquarters, 1, -1, true},
        {TEXT("Follow the rally"), TEXT("Train a Drudge and let it take the assigned mine."), TEXT("New workers save attention by beginning useful work automatically."), TEXT("Train 1 rallied Drudge"), ECinderCampaignActionKind::Train, ECinderCampaignTargetRole::PlayerAnchor, cinder::Kind::Worker, 1, -1, false},
        {TEXT("Add crew room"), TEXT("Complete a Siphon to add fourteen crew capacity."), TEXT("Siphons add fourteen crew room and receive ore, so production can keep going."), TEXT("Complete 1 Siphon"), ECinderCampaignActionKind::Build, ECinderCampaignTargetRole::BuildSite, cinder::Kind::Processor, 1, -1, false},
        {TEXT("Move to a fresh patch"), TEXT("Assign a Drudge to the marked remote ore patch."), TEXT("Ore is finite. Relocating workers keeps income flowing when a patch runs low."), TEXT("Relocate 1 Drudge"), ECinderCampaignActionKind::Gather, ECinderCampaignTargetRole::RemoteOre, cinder::Kind::Worker, 1, -1, true},
        {TEXT("Open remote delivery"), TEXT("Complete a Siphon beside the new patch."), TEXT("A nearby Siphon shortens each delivery trip and secures more crew room."), TEXT("Complete remote Siphon"), ECinderCampaignActionKind::Build, ECinderCampaignTargetRole::BuildSite, cinder::Kind::Processor, 1, -1, false},
        {TEXT("Feed the Emberline"), TEXT("Deliver ore gathered from the new patch to its Siphon."), TEXT("Income only arrives after a Drudge carries ore back to an Anchor or Siphon."), TEXT("Deliver remote ore"), ECinderCampaignActionKind::Wait, ECinderCampaignTargetRole::RemoteOre, cinder::Kind::Worker, 1, -1, false}
    },
    PhaseArray{
        {TEXT("Train an eye"), TEXT("Train a Skim for fast reconnaissance."), TEXT("A Skim moves quickly and sees farther, making it useful before committing an army."), TEXT("Train 1 Skim"), ECinderCampaignActionKind::Train, ECinderCampaignTargetRole::PlayerAnchor, cinder::Kind::Scout, 1, -1, false},
        {TEXT("Move without engaging"), TEXT("Move a Skim to the first marked lookout."), TEXT("Move prioritizes reaching a destination; Attack-move stops to fight along the way."), TEXT("Reach lookout 1"), ECinderCampaignActionKind::Move, ECinderCampaignTargetRole::ScoutZoneA, cinder::Kind::Scout, 1, -1, true},
        {TEXT("Map both positions"), TEXT("Explore both forward positions before committing the army."), TEXT("Fog hides current threats. Scouting turns an unknown route into a decision."), TEXT("Explore 2 positions"), ECinderCampaignActionKind::Explore, ECinderCampaignTargetRole::ScoutZoneB, cinder::Kind::Scout, 2, -1, false},
        {TEXT("Advance ready to fight"), TEXT("Attack-move a combat force toward the revealed line."), TEXT("Attack-move is safer through known danger because troops engage threats they encounter."), TEXT("Issue Attack-move"), ECinderCampaignActionKind::AttackMove, ECinderCampaignTargetRole::ForwardApproach, cinder::Kind::Striker, 1, -1, true},
        {TEXT("Remove the forward threat"), TEXT("Destroy both revealed positions. Your route and force are your choice."), TEXT("Use the scouting result to choose a route, then replenish losses before the second position."), TEXT("Threats remaining"), ECinderCampaignActionKind::Destroy, ECinderCampaignTargetRole::EnemyObjective, cinder::Kind::Resource, 0, -1, false}
    },
    PhaseArray{
        {TEXT("Route reinforcements"), TEXT("Set the shared army rally inside the defense area."), TEXT("New combat units travel here automatically; units already in the field keep their orders."), TEXT("Set army rally"), ECinderCampaignActionKind::Rally, ECinderCampaignTargetRole::HomeDefense, cinder::Kind::Resource, 1, -1, true},
        {TEXT("Raise a Ward"), TEXT("Complete a Ward inside the defended approach."), TEXT("A Ward controls one approach against ground and air, but an unsupported Ward can be overwhelmed."), TEXT("Complete 1 Ward"), ECinderCampaignActionKind::Build, ECinderCampaignTargetRole::BuildSite, cinder::Kind::Turret, 1, -1, false},
        {TEXT("Anchor the line"), TEXT("Give friendly combat troops a Defend order in the marked area."), TEXT("Defend lets troops engage nearby threats, then return to their assigned ground."), TEXT("Issue Defend"), ECinderCampaignActionKind::Defend, ECinderCampaignTargetRole::HomeDefense, cinder::Kind::Striker, 1, -1, true},
        {TEXT("First strike"), TEXT("Repel the first raiding force."), TEXT("Keep producing during the attack so losses do not empty the line."), TEXT("Prepare: 10 seconds"), ECinderCampaignActionKind::Wait, ECinderCampaignTargetRole::HomeDefense, cinder::Kind::Resource, 0, -1, false},
        {TEXT("Second strike"), TEXT("Prepare for the second raid, then defeat its attackers."), TEXT("Replenish the line before the next raid. Destroying its producer ends future training."), TEXT("Prepare: 10 seconds"), ECinderCampaignActionKind::Wait, ECinderCampaignTargetRole::HomeDefense, cinder::Kind::Resource, 0, -1, false}
    },
    PhaseArray{
        {TEXT("Read the siege"), TEXT("Explore the fortified position before choosing a force."), TEXT("The siege includes armored Anvils. Seeing the force first lets you choose a useful answer."), TEXT("Reveal the siege"), ECinderCampaignActionKind::Explore, ECinderCampaignTargetRole::ScoutZoneA, cinder::Kind::Scout, 1, -1, false},
        {TEXT("Build a Resonator"), TEXT("Complete the structure that researches technology and upgrades."), TEXT("A Resonator advances technology, improves the army, and trains Mend support."), TEXT("Complete 1 Resonator"), ECinderCampaignActionKind::Build, ECinderCampaignTargetRole::BuildSite, cinder::Kind::Laboratory, 1, -1, false},
        {TEXT("Advance technology"), TEXT("Complete Tier 2 research at the Resonator."), TEXT("Tier 2 unlocks the Crucible, Anvil frontline units, and Mend support."), TEXT("Reach Tier 2"), ECinderCampaignActionKind::Research, ECinderCampaignTargetRole::BuildSite, cinder::Kind::Worker, 1, 0, false},
        {TEXT("Sharpen weapons"), TEXT("Complete one weapons upgrade."), TEXT("Weapons research improves the damage of all your attackers."), TEXT("Complete Weapons 1"), ECinderCampaignActionKind::Research, ECinderCampaignTargetRole::BuildSite, cinder::Kind::Striker, 1, 1, false},
        {TEXT("Build the frontline"), TEXT("Complete a Crucible and train an advanced frontline or support unit."), TEXT("Needles pierce armored Anvils; your own Anvils hold against infantry while Mend repairs nearby units."), TEXT("Crucible + advanced unit"), ECinderCampaignActionKind::Build, ECinderCampaignTargetRole::BuildSite, cinder::Kind::MotorPool, 1, -1, false},
        {TEXT("Break the Anchor"), TEXT("Destroy the rival Anchor. Any successful composition counts."), TEXT("Use what scouting revealed, keep replacing losses, and commit when the force is ready."), TEXT("Destroy enemy Anchor"), ECinderCampaignActionKind::Destroy, ECinderCampaignTargetRole::EnemyObjective, cinder::Kind::Headquarters, 1, -1, false}
    },
    PhaseArray{
        {TEXT("Trial by Fire"), TEXT("Win an ordinary Standard match against Normal AI."), TEXT("Read the battlefield and choose when to scout, expand, reinforce, or research."), TEXT("Destroy the enemy Anchor"), ECinderCampaignActionKind::Destroy, ECinderCampaignTargetRole::EnemyObjective, cinder::Kind::Headquarters, 1, -1, false}
    }
}};

float DistanceSquared(cinder::Vec2 A, cinder::Vec2 B)
{
    const float X = A.x - B.x;
    const float Y = A.y - B.y;
    return X * X + Y * Y;
}

bool Near(cinder::Vec2 A, cinder::Vec2 B, float Radius = PointTolerance)
{
    return DistanceSquared(A, B) <= Radius * Radius;
}

bool CompleteFriendly(const cinder::Entity& Entity, cinder::Kind Kind)
{
    return Entity.alive() && Entity.team == 0 && Entity.kind == Kind && Entity.progress >= 1.0f;
}

int CountComplete(const cinder::Simulation& Sim, int Team, cinder::Kind Kind)
{
    return static_cast<int>(std::count_if(Sim.entities().begin(), Sim.entities().end(),
        [Team, Kind](const cinder::Entity& Entity)
        {
            return Entity.alive() && Entity.team == Team && Entity.kind == Kind && Entity.progress >= 1.0f;
        }));
}

int CountNewComplete(const cinder::Simulation& Sim, int Team, cinder::Kind Kind, cinder::Id After)
{
    return static_cast<int>(std::count_if(Sim.entities().begin(), Sim.entities().end(),
        [Team, Kind, After](const cinder::Entity& Entity)
        {
            return Entity.id > After && Entity.alive() && Entity.team == Team &&
                Entity.kind == Kind && Entity.progress >= 1.0f;
        }));
}

cinder::Id HighestEntityId(const cinder::Simulation& Sim)
{
    cinder::Id Highest = 0;
    for (const cinder::Entity& Entity : Sim.entities()) Highest = std::max(Highest, Entity.id);
    return Highest;
}

int CountFriendlyCombatKinds(const cinder::Simulation& Sim)
{
    std::array<bool, 8> Seen{};
    for (const cinder::Entity& Entity : Sim.entities())
    {
        const int Kind = static_cast<int>(Entity.kind);
        if (Entity.alive() && Entity.team == 0 && Entity.progress >= 1.0f && Kind >= 1 && Kind <= 7)
            Seen[static_cast<std::size_t>(Kind)] = true;
    }
    return static_cast<int>(std::count(Seen.begin(), Seen.end(), true));
}

bool AllDead(const cinder::Simulation& Sim, const std::vector<cinder::Id>& Ids)
{
    return !Ids.empty() && std::all_of(Ids.begin(), Ids.end(), [&Sim](cinder::Id Id)
    {
        const cinder::Entity* Entity = Sim.find(Id);
        return !Entity || !Entity->alive();
    });
}

bool Contains(const std::vector<cinder::Id>& Ids, cinder::Id Id)
{
    return std::find(Ids.begin(), Ids.end(), Id) != Ids.end();
}

bool ContainsPoint(const cinder::Simulation& Sim, const std::vector<cinder::Id>& Ids, cinder::Vec2 Point)
{
    for (cinder::Id Id : Ids)
    {
        const cinder::Entity* Entity = Sim.find(Id);
        if (Entity && Entity->alive() && Near(Entity->pos, Point, 170.0f)) return true;
    }
    return false;
}

cinder::Id FindComplete(const cinder::Simulation& Sim, int Team, cinder::Kind Kind)
{
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == Team && Entity.kind == Kind && Entity.progress >= 1.0f) return Entity.id;
    return 0;
}

std::vector<cinder::Id> ResourcePatch(const cinder::Simulation& Sim, cinder::Vec2 Center, float Radius)
{
    std::vector<cinder::Id> Result;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.kind == cinder::Kind::Resource && Entity.resource > 0 && Near(Entity.pos, Center, Radius))
            Result.push_back(Entity.id);
    std::sort(Result.begin(), Result.end());
    return Result;
}

cinder::Vec2 FindOpenPoint(const cinder::Simulation& Sim, cinder::Vec2 Preferred, cinder::Kind Kind);

std::vector<cinder::Vec2> ReachableWorkPoints(const cinder::Navigation& Navigation,
    const cinder::Entity& Target, float WorkerRadius, float Padding)
{
    std::vector<cinder::Vec2> Points;
    const float Reach = cinder::definition(Target.kind).radius + WorkerRadius + Padding;
    for (int Spoke = 0; Spoke < 32; ++Spoke)
    {
        const float Angle = static_cast<float>(Spoke) * 6.28318530718f / 32.0f;
        const cinder::Vec2 Point{Target.pos.x + std::cos(Angle) * Reach,
            Target.pos.y + std::sin(Angle) * Reach};
        if (Navigation.pointClear(Point, WorkerRadius + 0.25f) &&
            Navigation.segmentClear(Point, Target.pos, WorkerRadius, Target.id)) Points.push_back(Point);
    }
    return Points;
}

bool HasReachableRecoveryRoute(const cinder::Simulation& Sim, const cinder::Entity& Resource,
    cinder::Vec2 BuildPoint, const cinder::Navigation& Navigation)
{
    const float WorkerRadius = cinder::definition(cinder::Kind::Worker).radius;
    const std::vector<cinder::Vec2> ResourceGoals = ReachableWorkPoints(Navigation, Resource, WorkerRadius, 9.0f);
    if (ResourceGoals.empty()) return false;

    std::vector<cinder::Vec2> DepotGoals;
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != 0 || Entity.progress < 1.0f ||
            (Entity.kind != cinder::Kind::Headquarters && Entity.kind != cinder::Kind::Processor)) continue;
        std::vector<cinder::Vec2> Goals = ReachableWorkPoints(Navigation, Entity, WorkerRadius, 15.0f);
        DepotGoals.insert(DepotGoals.end(), Goals.begin(), Goals.end());
    }
    if (DepotGoals.empty()) return false;

    cinder::Navigation Proposed = Navigation;
    const cinder::Id ProposedSiphonId = std::numeric_limits<cinder::Id>::max();
    Proposed.addCircle({ProposedSiphonId, BuildPoint, cinder::definition(cinder::Kind::Processor).radius});
    cinder::Entity ProposedSiphon;
    ProposedSiphon.id = ProposedSiphonId;
    ProposedSiphon.kind = cinder::Kind::Processor;
    ProposedSiphon.pos = BuildPoint;
    const std::vector<cinder::Vec2> BuildGoals = ReachableWorkPoints(Proposed, ProposedSiphon, WorkerRadius, 20.0f);
    if (BuildGoals.empty()) return false;

    for (const cinder::Entity& Worker : Sim.entities())
    {
        if (!CompleteFriendly(Worker, cinder::Kind::Worker)) continue;
        const cinder::NavigationResult Outward = Navigation.route(Worker.pos, ResourceGoals, WorkerRadius, Worker.id);
        if (!Outward.reached || Outward.exhausted) continue;
        const cinder::Vec2 ResourcePoint = Outward.points.empty() ? Outward.origin : Outward.points.back();
        const cinder::NavigationResult Home = Navigation.route(ResourcePoint, DepotGoals, WorkerRadius, Worker.id);
        if (!Home.reached || Home.exhausted) continue;
        const cinder::NavigationResult Build = Proposed.route(Worker.pos, BuildGoals, WorkerRadius, Worker.id);
        if (Build.reached && !Build.exhausted) return true;
    }
    return false;
}

bool RetargetRemotePatch(cinder::Simulation const& Sim, FCinderCampaignState& State)
{
    std::vector<cinder::NavBox> Boxes;
    Boxes.reserve(Sim.obstacles().size());
    for (const cinder::Obstacle& Obstacle : Sim.obstacles()) Boxes.push_back({Obstacle.center, Obstacle.half});
    std::vector<cinder::NavCircle> Circles;
    Circles.reserve(Sim.entities().size());
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive()) continue;
        const cinder::Definition& Definition = cinder::definition(Entity.kind);
        if (Definition.building || (Entity.kind == cinder::Kind::Resource && Entity.resource > 0))
            Circles.push_back({Entity.id, Entity.pos, Definition.radius});
    }
    cinder::Navigation Navigation;
    Navigation.sync(Sim.worldSize(), Boxes, Circles);

    const cinder::Entity* BestNode = nullptr;
    cinder::Vec2 BestBuildPoint{};
    float BestDistance = std::numeric_limits<float>::max();
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.kind != cinder::Kind::Resource || Entity.resource <= 0 ||
            Contains(State.Authored.HomeOre, Entity.id) || Contains(State.Authored.RemoteOre, Entity.id)) continue;
        const float FromEnemy = DistanceSquared(Entity.pos, State.EnemyObjectivePoint);
        if (FromEnemy < 650.0f * 650.0f) continue;
        const cinder::Vec2 BuildPoint = FindOpenPoint(Sim, Entity.pos, cinder::Kind::Processor);
        if ((BuildPoint.x == 0.0f && BuildPoint.y == 0.0f) || !Near(BuildPoint, Entity.pos, 520.0f) ||
            !HasReachableRecoveryRoute(Sim, Entity, BuildPoint, Navigation)) continue;
        const float FromPreviousPatch = DistanceSquared(Entity.pos, State.RemotePatchPoint);
        if (FromPreviousPatch < BestDistance)
        {
            BestNode = &Entity;
            BestBuildPoint = BuildPoint;
            BestDistance = FromPreviousPatch;
        }
    }
    if (!BestNode) return false;

    std::vector<cinder::Id> Patch = ResourcePatch(Sim, BestNode->pos, 340.0f);
    if (Patch.empty()) return false;

    State.Authored.RemoteOre = std::move(Patch);
    State.RemotePatchPoint = BestNode->pos;
    State.BuildPoint = BestBuildPoint;
    State.Authored.MiningWorker = 0;
    State.Authored.RemoteSiphon = 0;
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (CompleteFriendly(Entity, cinder::Kind::Processor) && Near(Entity.pos, State.RemotePatchPoint, 520.0f))
        {
            State.Authored.RemoteSiphon = Entity.id;
            break;
        }
    }
    State.Counters.LastTrackedCarried = 0.0f;
    State.Counters.PhaseStartGathered = Sim.players()[0].stats.gathered;
    State.bRemoteWorkerCarried = false;
    State.bRemoteDeliveryObserved = false;
    return true;
}

cinder::Vec2 FindOpenPoint(const cinder::Simulation& Sim, cinder::Vec2 Preferred, cinder::Kind Kind)
{
    const float Radius = cinder::definition(Kind).radius;
    const float World = Sim.worldSize();
    for (int Ring = 0; Ring <= 12; ++Ring)
    {
        const int Spokes = Ring == 0 ? 1 : 16;
        for (int Spoke = 0; Spoke < Spokes; ++Spoke)
        {
            const float Angle = static_cast<float>(Spoke) * 6.28318530718f / static_cast<float>(Spokes);
            cinder::Vec2 Point{Preferred.x + std::cos(Angle) * Ring * 90.0f,
                Preferred.y + std::sin(Angle) * Ring * 90.0f};
            if (Point.x < Radius || Point.y < Radius || Point.x > World - Radius || Point.y > World - Radius) continue;
            bool Open = true;
            for (const cinder::Obstacle& Obstacle : Sim.obstacles())
            {
                const float Dx = std::max(std::fabs(Point.x - Obstacle.center.x) - Obstacle.half.x, 0.0f);
                const float Dy = std::max(std::fabs(Point.y - Obstacle.center.y) - Obstacle.half.y, 0.0f);
                if (Dx * Dx + Dy * Dy < (Radius + 20.0f) * (Radius + 20.0f)) { Open = false; break; }
            }
            if (!Open) continue;
            for (const cinder::Entity& Entity : Sim.entities())
            {
                if (!Entity.alive()) continue;
                const float Clearance = Radius + cinder::definition(Entity.kind).radius + 20.0f;
                if (Near(Point, Entity.pos, Clearance)) { Open = false; break; }
            }
            if (Open) return Point;
        }
    }
    return {};
}

bool ValidPoint(cinder::Vec2 Point, float World)
{
    return std::isfinite(Point.x) && std::isfinite(Point.y) && Point.x >= 0 && Point.y >= 0 &&
        Point.x <= World && Point.y <= World;
}

bool ValidIdVector(const cinder::Simulation& Sim, const std::vector<cinder::Id>& Ids,
    std::size_t Limit, bool bAllowMissing, FString& Error)
{
    if (Ids.size() > Limit) { Error = TEXT("Campaign ID collection is too large."); return false; }
    std::unordered_set<cinder::Id> Unique;
    for (cinder::Id Id : Ids)
    {
        if (!Id || !Unique.insert(Id).second || (!bAllowMissing && !Sim.find(Id)))
        {
            Error = TEXT("Campaign ID collection contains an invalid or duplicate actor.");
            return false;
        }
    }
    return true;
}

bool CommandHasKind(const cinder::Simulation& Sim, const cinder::Command& Command, cinder::Kind Kind)
{
    return std::any_of(Command.units.begin(), Command.units.end(), [&Sim, Kind](cinder::Id Id)
    {
        const cinder::Entity* Entity = Sim.find(Id);
        return Entity && Entity->alive() && Entity->team == 0 && Entity->kind == Kind;
    });
}
}

bool FCinderCampaign::ExpectedConfig(int32 Mission, cinder::Config& OutConfig)
{
    if (Mission < 0 || Mission >= MissionCount) return false;
    OutConfig = {};
    OutConfig.mapRevision = 0; // Campaign encounter coordinates use the original maps.
    OutConfig.map = Missions[Mission].Map;
    OutConfig.seed = CampaignSeed + static_cast<std::uint32_t>(Mission);
    OutConfig.ai = Missions[Mission].bNormalAI;
    OutConfig.aiAggression = 1.0f;
    OutConfig.matchLength = Missions[Mission].MatchLength;
    OutConfig.playerCount = 2;
    return true;
}

const FCinderCampaignMissionDefinition& FCinderCampaign::Definition(int32 Mission)
{
    static const FCinderCampaignMissionDefinition Invalid{};
    return Mission >= 0 && Mission < MissionCount ? Missions[Mission] : Invalid;
}

int32 FCinderCampaign::PhaseCount() const
{
    return State.Mission >= 0 && State.Mission < MissionCount ? Missions[State.Mission].PhaseCount : 0;
}

const FCinderCampaignPhaseDefinition& FCinderCampaign::PhaseDefinition() const
{
    static const FCinderCampaignPhaseDefinition Invalid{};
    if (State.Mission < 0 || State.Mission >= MissionCount || State.Phase < 0 ||
        State.Phase >= static_cast<int32>(Phases[State.Mission].size())) return Invalid;
    return Phases[State.Mission][State.Phase];
}

void FCinderCampaign::Reset()
{
    State = {};
    bGuideSiteCached = false;
    GuideSiteKind = cinder::Kind::Resource;
    GuideSite = {};
    GuideSiteValidatedTick = 0;
    GuideRecoverySite = 0;
    NextRemoteRetargetAttemptTick = 0;
}

bool FCinderCampaign::InitializeMission(cinder::Simulation& Sim, int32 Mission)
{
    cinder::Config Config;
    if (!ExpectedConfig(Mission, Config)) return false;

    cinder::Simulation CandidateSim;
    CandidateSim.reset(Config);
    FCinderCampaign Candidate;
    Candidate.State.SchemaVersion = StateSchemaVersion;
    Candidate.State.ContentVersion = ContentVersion;
    Candidate.State.Mission = Mission;
    Candidate.State.Phase = 0;
    Candidate.State.Outcome = ECinderCampaignOutcome::Running;
    Candidate.State.MissionStartTick = CandidateSim.tick();
    Candidate.State.PhaseStartTick = CandidateSim.tick();
    Candidate.State.ObjectiveStartTick = CandidateSim.tick();
    Candidate.State.NextOpponentDecisionTick = CandidateSim.tick();
    Candidate.State.CheckpointSerial = 1;
    Candidate.State.CheckpointPhase = 0;
    Candidate.State.bCheckpointSettled = true;
    if (!Candidate.SetupScenario(CandidateSim)) return false;
    Candidate.CapturePhaseBaseline(CandidateSim);

    FString Error;
    if (!ValidateState(CandidateSim, Candidate.State, Error)) return false;
    Sim = std::move(CandidateSim);
    *this = std::move(Candidate);
    return true;
}

bool FCinderCampaign::SetupScenario(cinder::Simulation& Sim)
{
    State.Authored.PlayerAnchor = FindComplete(Sim, 0, cinder::Kind::Headquarters);
    State.Authored.EnemyAnchor = FindComplete(Sim, 1, cinder::Kind::Headquarters);
    const cinder::Entity* PlayerAnchor = Sim.find(State.Authored.PlayerAnchor);
    const cinder::Entity* EnemyAnchor = Sim.find(State.Authored.EnemyAnchor);
    if (!PlayerAnchor || !EnemyAnchor) return false;
    const cinder::Vec2 PlayerBase = PlayerAnchor->pos;
    const cinder::Vec2 EnemyBase = EnemyAnchor->pos;
    State.BasePoint = PlayerBase;
    State.EnemyObjectivePoint = EnemyBase;
    State.HomeDefense = {PlayerBase.x + 420.0f, PlayerBase.y + 420.0f};
    State.Authored.HomeOre = ResourcePatch(Sim, PlayerBase, 700.0f);
    if (State.Authored.HomeOre.empty()) return false;
    State.HomeOrePoint = Sim.find(State.Authored.HomeOre.front())->pos;
    for (const cinder::Entity& Entity : Sim.entities())
        if (CompleteFriendly(Entity, cinder::Kind::Worker)) { State.Authored.MiningWorker = Entity.id; break; }

    const cinder::Entity* Remote = nullptr;
    float Best = std::numeric_limits<float>::max();
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.kind != cinder::Kind::Resource || Entity.resource <= 0 ||
            Contains(State.Authored.HomeOre, Entity.id)) continue;
        const float FromHome = DistanceSquared(Entity.pos, PlayerBase);
        const float FromEnemy = DistanceSquared(Entity.pos, EnemyBase);
        if (FromHome < 750.0f * 750.0f || FromEnemy < 650.0f * 650.0f || FromHome >= Best) continue;
        Remote = &Entity;
        Best = FromHome;
    }
    if (!Remote) return false;
    State.RemotePatchPoint = Remote->pos;
    State.Authored.RemoteOre = ResourcePatch(Sim, Remote->pos, 340.0f);
    if (State.Authored.RemoteOre.empty()) return false;

    const cinder::Vec2 LocalBuildPreferred = State.Mission == 3
        ? cinder::Vec2{PlayerBase.x + 360.0f, PlayerBase.y + 360.0f}
        : cinder::Vec2{PlayerBase.x + 360.0f, PlayerBase.y + 110.0f};
    State.LocalBuildPoint = FindOpenPoint(Sim, LocalBuildPreferred,
        State.Mission == 3 ? cinder::Kind::Turret : State.Mission == 4 ? cinder::Kind::Laboratory :
        State.Mission == 1 ? cinder::Kind::Processor : cinder::Kind::Foundry);
    State.BuildPoint = State.Mission == 1 ? FindOpenPoint(Sim, State.RemotePatchPoint, cinder::Kind::Processor) :
        State.LocalBuildPoint;
    if (!ValidPoint(State.BuildPoint, Sim.worldSize()) || (State.BuildPoint.x == 0 && State.BuildPoint.y == 0)) return false;
    if (State.Mission == 3) State.HomeDefense = State.LocalBuildPoint;

    State.ForwardApproach = {Sim.worldSize() * 0.48f, Sim.worldSize() * 0.48f};
    State.ScoutZoneA = FindOpenPoint(Sim, {Sim.worldSize() * 0.40f, Sim.worldSize() * 0.58f}, cinder::Kind::Striker);
    State.ScoutZoneB = FindOpenPoint(Sim, {Sim.worldSize() * 0.60f, Sim.worldSize() * 0.40f}, cinder::Kind::Striker);
    if (!ValidPoint(State.ScoutZoneA, Sim.worldSize()) || !ValidPoint(State.ScoutZoneB, Sim.worldSize())) return false;

    auto SpawnHeld = [&Sim](cinder::Kind Kind, cinder::Vec2 Preferred, std::vector<cinder::Id>& Into)
    {
        const cinder::Vec2 Point = FindOpenPoint(Sim, Preferred, Kind);
        if (Point.x == 0 && Point.y == 0) return false;
        const cinder::Id Id = Sim.debugSpawn(Kind, 1, Point);
        if (!Id) return false;
        cinder::Command Hold;
        Hold.type = cinder::CommandType::Hold;
        Hold.team = 1;
        Hold.units = {Id};
        if (!Sim.command(Hold).accepted) return false;
        Into.push_back(Id);
        return true;
    };
    auto SpawnFriendlyHeld = [&Sim](cinder::Kind Kind, cinder::Vec2 Preferred, cinder::Id* Out)
    {
        const cinder::Vec2 Point = FindOpenPoint(Sim, Preferred, Kind);
        if (Point.x == 0 && Point.y == 0) return false;
        const cinder::Id Id = Sim.debugSpawn(Kind, 0, Point);
        if (!Id) return false;
        cinder::Command Hold;
        Hold.type = cinder::CommandType::Hold;
        Hold.team = 0;
        Hold.units = {Id};
        if (!Sim.command(Hold).accepted) return false;
        if (Out) *Out = Id;
        return true;
    };

    if (State.Mission == 0)
    {
        for (int Index = 0; Index < 3; ++Index)
            if (!SpawnHeld(cinder::Kind::Striker,
                {State.ForwardApproach.x + Index * 65.0f, State.ForwardApproach.y + (Index % 2) * 80.0f},
                State.Authored.PracticePatrol)) return false;
    }
    else if (State.Mission == 2)
    {
        if (!SpawnHeld(cinder::Kind::Striker, State.ScoutZoneA, State.Authored.ForwardThreats) ||
            !SpawnHeld(cinder::Kind::Lancer, State.ScoutZoneB, State.Authored.ForwardThreats)) return false;
        State.Authored.ScoutPosts = State.Authored.ForwardThreats;
    }
    else if (State.Mission == 3)
    {
        const cinder::Vec2 KilnSite = FindOpenPoint(Sim,
            {PlayerBase.x + 260.0f, PlayerBase.y + 310.0f}, cinder::Kind::Foundry);
        if (KilnSite.x == 0 && KilnSite.y == 0) return false;
        State.Authored.Kiln = Sim.debugSpawn(cinder::Kind::Foundry, 0, KilnSite);
        if (!State.Authored.Kiln) return false;
        for (int Index = 0; Index < 4; ++Index)
            if (!SpawnFriendlyHeld(cinder::Kind::Striker,
                {State.HomeDefense.x + Index * 55.0f, State.HomeDefense.y + (Index % 2) * 65.0f}, nullptr)) return false;
        const cinder::Vec2 Preferred{EnemyBase.x - 360.0f, EnemyBase.y - 260.0f};
        const cinder::Vec2 Site = FindOpenPoint(Sim, Preferred, cinder::Kind::Foundry);
        if (Site.x == 0 && Site.y == 0) return false;
        State.Authored.RaidProducer = Sim.debugSpawn(cinder::Kind::Foundry, 1, Site);
        if (!State.Authored.RaidProducer) return false;
        Sim.debugResources(1, 2600);
    }
    else if (State.Mission == 4)
    {
        const cinder::Vec2 KilnSite = FindOpenPoint(Sim,
            {PlayerBase.x + 260.0f, PlayerBase.y + 310.0f}, cinder::Kind::Foundry);
        if (KilnSite.x == 0 && KilnSite.y == 0) return false;
        State.Authored.Kiln = Sim.debugSpawn(cinder::Kind::Foundry, 0, KilnSite);
        if (!State.Authored.Kiln) return false;
        for (int Index = 0; Index < 2; ++Index)
        {
            const cinder::Vec2 Site = FindOpenPoint(Sim,
                {EnemyBase.x - 430.0f - Index * 210.0f, EnemyBase.y - 260.0f + Index * 230.0f},
                cinder::Kind::Foundry);
            if (Site.x == 0 && Site.y == 0) return false;
            const cinder::Id Producer = Sim.debugSpawn(cinder::Kind::Foundry, 1, Site);
            if (!Producer) return false;
            State.Authored.SiegeProducers.push_back(Producer);
        }
        for (int Index = 0; Index < 2; ++Index)
            if (!SpawnHeld(cinder::Kind::Bastion,
                {State.ScoutZoneA.x + Index * 90.0f, State.ScoutZoneA.y + Index * 70.0f},
                State.Authored.SiegeGuard)) return false;
    }

    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 1 && !cinder::definition(Entity.kind).building &&
            Entity.kind != cinder::Kind::Worker && Entity.kind != cinder::Kind::Resource)
            State.Authored.OrderedUnits.push_back(Entity.id);
    std::sort(State.Authored.OrderedUnits.begin(), State.Authored.OrderedUnits.end());
    State.HighestSetupEntityId = HighestEntityId(Sim);
    return true;
}

void FCinderCampaign::CapturePhaseBaseline(const cinder::Simulation& Sim)
{
    const cinder::Stats& Stats = Sim.players()[0].stats;
    if (State.Phase == 0)
    {
        State.Counters.MissionStartProduced = Stats.produced;
        State.Counters.MissionStartBuilt = Stats.built;
        State.Counters.MissionStartGathered = Stats.gathered;
        State.Counters.MissionStartUpgrades = Stats.upgrades;
    }
    State.Counters.PhaseStartProduced = Stats.produced;
    State.Counters.PhaseStartBuilt = Stats.built;
    State.Counters.PhaseStartGathered = Stats.gathered;
    State.Counters.PhaseStartUpgrades = Stats.upgrades;
    State.Counters.PhaseStartWorkers = CountComplete(Sim, 0, cinder::Kind::Worker);
    State.Counters.PhaseStartEmbers = CountComplete(Sim, 0, cinder::Kind::Striker);
    State.Counters.PhaseStartSiphons = CountComplete(Sim, 0, cinder::Kind::Processor);
    State.Counters.PhaseStartResonators = CountComplete(Sim, 0, cinder::Kind::Laboratory);
    State.Counters.PhaseStartMotorPools = CountComplete(Sim, 0, cinder::Kind::MotorPool);
    State.Counters.PhaseStartAnvils = CountComplete(Sim, 0, cinder::Kind::Bastion);
    State.Counters.StableTicks = 0;
    State.PhaseCommandMask = 0;
    State.PhaseStartTick = Sim.tick();
    State.ObjectiveStartTick = Sim.tick();
    State.bCheckpointSettled = true;
    bGuideSiteCached = false;
    GuideRecoverySite = 0;
}

void FCinderCampaign::Advance(const cinder::Simulation& Sim)
{
    if (!IsRunning()) return;
    State.CompletedObjectiveMask |= std::uint64_t{1} << State.Phase;
    if (State.Phase + 1 >= PhaseCount())
    {
        Finish(ECinderCampaignOutcome::Victory);
        return;
    }
    ++State.Phase;
    ++State.CheckpointSerial;
    State.CheckpointPhase = State.Phase;
    CapturePhaseBaseline(Sim);
    if (State.Mission == 3 && (State.Phase == 3 || State.Phase == 4))
        State.NextOpponentDecisionTick = Sim.tick() + WavePreparationTicks;
    if (State.Mission == 3 && State.Phase == 4)
    {
        State.Counters.WaveIndex = 1;
        State.Counters.OpponentTrainOrders = 0;
        State.Authored.ActiveWave.clear();
        State.bWaveProductionFinished = false;
    }
}

void FCinderCampaign::Finish(ECinderCampaignOutcome NewOutcome, const FString& Reason)
{
    if (!IsRunning()) return;
    State.Outcome = NewOutcome;
    State.FailureCode = Reason;
    State.bCheckpointSettled = false;
}

void FCinderCampaign::CameraInput()
{
    if (!IsRunning()) return;
    State.bCameraObserved = true;
}

void FCinderCampaign::RequestHint()
{
    if (!IsRunning()) return;
    State.AssistanceMask |= std::uint64_t{1} << State.Phase;
    State.CoachingLevel = std::min(State.CoachingLevel + 1, 2);
}

void FCinderCampaign::AcceptedCommand(const cinder::Simulation& Sim, const cinder::Command& Command)
{
    if (!IsRunning() || Command.team != 0 || State.Phase < 0 || State.Phase >= 32) return;
    State.AcceptedCommandMask |= CommandBit(Command.type);
    State.PhaseCommandMask |= CommandBit(Command.type);
    State.bCheckpointSettled = false;

    if (State.Mission == 0 && State.Phase == 2 && Command.type == cinder::CommandType::Gather &&
        !Command.units.empty() && Contains(State.Authored.HomeOre, Command.target))
    {
        State.Authored.MiningWorker = Command.units.front();
        if (const cinder::Entity* Worker = Sim.find(State.Authored.MiningWorker)) State.Counters.LastTrackedCarried = Worker->carried;
        State.PhaseCommandMask |= CurrentPhaseProof;
    }
    else if (State.Mission == 0 && State.Phase == 3 &&
        (Command.type == cinder::CommandType::Train || Command.type == cinder::CommandType::AutoTrain) &&
        Command.kind == cinder::Kind::Worker)
    {
        State.RallyBaselineEntityId = HighestEntityId(Sim);
        State.PhaseCommandMask |= CurrentPhaseProof;
    }
    else if (State.Mission == 0 && State.Phase == 4 &&
        (Command.type == cinder::CommandType::Build || Command.type == cinder::CommandType::AutoBuild) &&
        Command.kind == cinder::Kind::Foundry)
    {
        // An appended build is only a plan until the Drudge reaches it. Keep
        // the accepted site so observation can bind its paid foundation later.
        State.BuildPoint = Command.point;
        State.Authored.Kiln = 0;
        for (const cinder::Entity& Entity : Sim.entities())
            if (Entity.id > State.HighestSetupEntityId && Entity.team == 0 && Entity.kind == cinder::Kind::Foundry &&
                Near(Entity.pos, Command.point, 3.0f)) State.Authored.Kiln = Entity.id;
        State.PhaseCommandMask |= CurrentPhaseProof;
    }
    else if (State.Mission == 0 && State.Phase == 6 && Command.type == cinder::CommandType::AttackMove &&
        std::any_of(Command.units.begin(), Command.units.end(), [&Sim](cinder::Id Id)
        {
            const cinder::Entity* Entity = Sim.find(Id);
            return Entity && Entity->alive() && Entity->team == 0 && !cinder::definition(Entity->kind).building &&
                cinder::definition(Entity->kind).damage > 0;
        })) State.PhaseCommandMask |= CurrentPhaseProof;
    else if (State.Mission == 1 && State.Phase == 0 &&
        (Command.type == cinder::CommandType::Rally || Command.type == cinder::CommandType::AutoRally))
    {
        for (cinder::Id Ore : State.Authored.HomeOre)
        {
            const cinder::Entity* Node = Sim.find(Ore);
            if (Node && Node->alive() && Node->resource > 0 && Near(Command.point, Node->pos, 100.0f))
            {
                State.Authored.OrderedUnits.clear();
                for (const cinder::Entity& Entity : Sim.entities())
                    if (CompleteFriendly(Entity, cinder::Kind::Worker)) State.Authored.OrderedUnits.push_back(Entity.id);
                State.RallyBaselineEntityId = HighestEntityId(Sim);
                State.bAnchorOreRallyAccepted = (Command.type == cinder::CommandType::Rally &&
                    Contains(Command.units, State.Authored.PlayerAnchor)) ||
                    (Command.type == cinder::CommandType::AutoRally && Command.target == State.Authored.PlayerAnchor);
                if (State.bAnchorOreRallyAccepted) State.PhaseCommandMask |= CurrentPhaseProof;
                break;
            }
        }
    }
    else if (State.Mission == 1 && State.Phase == 3 && Command.type == cinder::CommandType::Gather &&
        !Command.units.empty() && Contains(State.Authored.RemoteOre, Command.target))
    {
        State.Authored.MiningWorker = Command.units.front();
        State.Counters.LastTrackedCarried = 0.0f;
        State.PhaseCommandMask |= CurrentPhaseProof;
    }
    else if (State.Mission == 2 && State.Phase == 1 && Command.type == cinder::CommandType::Move &&
        CommandHasKind(Sim, Command, cinder::Kind::Scout) && Near(Command.point, State.ScoutZoneA, 260.0f))
    {
        for (cinder::Id Id : Command.units)
        {
            const cinder::Entity* Entity = Sim.find(Id);
            if (Entity && CompleteFriendly(*Entity, cinder::Kind::Scout))
            { State.Authored.RallyWorker = Id; break; }
        }
        if (State.Authored.RallyWorker) State.PhaseCommandMask |= CurrentPhaseProof;
    }
    else if (State.Mission == 2 && State.Phase == 3 && Command.type == cinder::CommandType::AttackMove &&
        std::any_of(Command.units.begin(), Command.units.end(), [&Sim](cinder::Id Id)
        {
            const cinder::Entity* Entity = Sim.find(Id);
            return Entity && Entity->alive() && Entity->team == 0 && cinder::definition(Entity->kind).damage > 0;
        })) State.PhaseCommandMask |= CurrentPhaseProof;
    else if (State.Mission == 3 && State.Phase == 0 && Command.type == cinder::CommandType::AutoRally &&
        Command.target == 0 && Command.kind == cinder::Kind::Resource && Near(Command.point, State.HomeDefense, 300.0f))
    { State.bArmyRallyAccepted = true; State.PhaseCommandMask |= CurrentPhaseProof; }
    else if (State.Mission == 3 && State.Phase == 2 && Command.type == cinder::CommandType::Defend &&
        std::any_of(Command.units.begin(), Command.units.end(), [&Sim](cinder::Id Id)
        {
            const cinder::Entity* Entity = Sim.find(Id);
            return Entity && Entity->alive() && Entity->team == 0 && cinder::definition(Entity->kind).damage > 0;
        })) State.PhaseCommandMask |= CurrentPhaseProof;
}

void FCinderCampaign::Observe(const cinder::Simulation& Sim, const std::vector<cinder::Id>& Selection)
{
    if (!IsRunning()) return;
    if (Sim.eliminated(0))
    {
        Finish(ECinderCampaignOutcome::Defeat, TEXT("anchor_lost"));
        return;
    }
    if (Sim.winner() >= 0)
    {
        if (Sim.winner() == 0) Finish(ECinderCampaignOutcome::Victory);
        else Finish(ECinderCampaignOutcome::Defeat, TEXT("anchor_lost"));
        return;
    }
    if (Sim.winner() == -2)
    {
        Finish(ECinderCampaignOutcome::Draw, TEXT("draw"));
        return;
    }

    const cinder::Stats& Stats = Sim.players()[0].stats;
    bool Complete = false;

    switch (State.Mission)
    {
    case 0:
        switch (State.Phase)
        {
        case 0: Complete = State.bCameraObserved; break;
        case 1:
            Complete = std::any_of(Selection.begin(), Selection.end(), [&Sim](cinder::Id Id)
            {
                const cinder::Entity* Entity = Sim.find(Id);
                return Entity && CompleteFriendly(*Entity, cinder::Kind::Worker);
            });
            break;
        case 2:
            if (const cinder::Entity* Worker = Sim.find(State.Authored.MiningWorker))
            {
                if (Worker->alive() && Worker->kind == cinder::Kind::Worker && Worker->carried > State.Counters.LastTrackedCarried + 0.001f)
                    State.bRemoteWorkerCarried = true;
                if (State.bRemoteWorkerCarried && Worker->carried + 0.001f < State.Counters.LastTrackedCarried &&
                    Stats.gathered > State.Counters.PhaseStartGathered) State.bGatherDeliveryObserved = true;
                State.Counters.LastTrackedCarried = Worker->carried;
            }
            Complete = (State.PhaseCommandMask & CurrentPhaseProof) && State.bGatherDeliveryObserved;
            break;
        case 3: Complete = (State.PhaseCommandMask & CurrentPhaseProof) &&
            CountNewComplete(Sim, 0, cinder::Kind::Worker, State.RallyBaselineEntityId) > 0; break;
        case 4:
            if ((State.PhaseCommandMask & CurrentPhaseProof) && !State.Authored.Kiln)
                for (const cinder::Entity& Entity : Sim.entities())
                    if (Entity.alive() && Entity.id > State.HighestSetupEntityId && Entity.team == 0 &&
                        Entity.kind == cinder::Kind::Foundry && Near(Entity.pos, State.BuildPoint, 3.0f))
                    {
                        State.Authored.Kiln = Entity.id;
                        break;
                    }
            if (const cinder::Entity* Kiln = Sim.find(State.Authored.Kiln)) Complete =
                (State.PhaseCommandMask & CurrentPhaseProof) && CompleteFriendly(*Kiln, cinder::Kind::Foundry);
            break;
        case 5: Complete = CountNewComplete(Sim, 0, cinder::Kind::Striker, State.HighestSetupEntityId) >= 3; break;
        case 6: Complete = (State.PhaseCommandMask & CurrentPhaseProof) != 0; break;
        case 7: Complete = AllDead(Sim, State.Authored.PracticePatrol); break;
        }
        break;
    case 1:
        switch (State.Phase)
        {
        case 0:
        {
            const cinder::Entity* Anchor = Sim.find(State.Authored.PlayerAnchor);
            Complete = State.bAnchorOreRallyAccepted && (State.PhaseCommandMask & CurrentPhaseProof) && Anchor &&
                ContainsPoint(Sim, State.Authored.HomeOre, Anchor->rally);
            break;
        }
        case 1:
            for (const cinder::Entity& Entity : Sim.entities())
                if (CompleteFriendly(Entity, cinder::Kind::Worker) && Entity.id > State.RallyBaselineEntityId &&
                    Contains(State.Authored.HomeOre, Entity.resourceTarget))
                { State.Authored.RallyWorker = Entity.id; State.bRallyWorkerAssigned = true; break; }
            Complete = State.bRallyWorkerAssigned;
            break;
        case 2:
            Complete = CountNewComplete(Sim, 0, cinder::Kind::Processor, State.HighestSetupEntityId) > 0;
            if (Complete)
            {
                for (const cinder::Entity& Entity : Sim.entities())
                    if (CompleteFriendly(Entity, cinder::Kind::Processor)) State.Authored.PlayerSiphon = Entity.id;
            }
            break;
        case 3:
            if (const cinder::Entity* Worker = Sim.find(State.Authored.MiningWorker))
                Complete = (State.PhaseCommandMask & CurrentPhaseProof) && Worker->alive() &&
                    Worker->order == cinder::Order::Gather && Contains(State.Authored.RemoteOre, Worker->resourceTarget);
            break;
        case 4:
            for (const cinder::Entity& Entity : Sim.entities())
                if (CompleteFriendly(Entity, cinder::Kind::Processor) && Near(Entity.pos, State.RemotePatchPoint, 520.0f))
                { State.Authored.RemoteSiphon = Entity.id; Complete = true; break; }
            break;
        case 5:
        {
            const bool bTargetPatchDepleted = std::none_of(State.Authored.RemoteOre.begin(),
                State.Authored.RemoteOre.end(), [&Sim](cinder::Id Id)
                {
                    const cinder::Entity* Ore = Sim.find(Id);
                    return Ore && Ore->alive() && Ore->kind == cinder::Kind::Resource && Ore->resource > 0;
                });
            const cinder::Entity* PendingWorker = Sim.find(State.Authored.MiningWorker);
            const cinder::Entity* PendingSiphon = Sim.find(State.Authored.RemoteSiphon);
            const bool bTrackedDeliveryCompleted = PendingWorker && PendingWorker->alive() &&
                PendingWorker->kind == cinder::Kind::Worker && State.bRemoteWorkerCarried &&
                PendingWorker->carried + 0.001f < State.Counters.LastTrackedCarried &&
                Stats.gathered > State.Counters.PhaseStartGathered && PendingSiphon &&
                CompleteFriendly(*PendingSiphon, cinder::Kind::Processor) &&
                Near(PendingWorker->pos, PendingSiphon->pos,
                    cinder::definition(cinder::Kind::Processor).radius + 90.0f);
            if (bTrackedDeliveryCompleted) State.bRemoteDeliveryObserved = true;
            const bool bPendingTargetDelivery = bTrackedDeliveryCompleted ||
                (PendingWorker && PendingWorker->alive() && PendingWorker->kind == cinder::Kind::Worker &&
                    Contains(State.Authored.RemoteOre, PendingWorker->resourceTarget) &&
                    PendingWorker->carried > 0.001f);
            if (bTargetPatchDepleted && !bPendingTargetDelivery &&
                Sim.tick() >= NextRemoteRetargetAttemptTick)
            {
                NextRemoteRetargetAttemptTick = Sim.tick() + 100;
                if (RetargetRemotePatch(Sim, State))
                {
                    bGuideSiteCached = false;
                    GuideRecoverySite = 0;
                }
            }

            const cinder::Entity* Worker = Sim.find(State.Authored.MiningWorker);
            if (!Worker || !Worker->alive() || !Contains(State.Authored.RemoteOre, Worker->resourceTarget))
            {
                for (const cinder::Entity& Candidate : Sim.entities())
                    if (CompleteFriendly(Candidate, cinder::Kind::Worker) && Contains(State.Authored.RemoteOre, Candidate.resourceTarget))
                    { State.Authored.MiningWorker = Candidate.id; Worker = &Candidate; State.Counters.LastTrackedCarried = Candidate.carried; break; }
            }
            const cinder::Entity* Siphon = Sim.find(State.Authored.RemoteSiphon);
            if (!Siphon || !CompleteFriendly(*Siphon, cinder::Kind::Processor) ||
                !Near(Siphon->pos, State.RemotePatchPoint, 520.0f))
            {
                State.Authored.RemoteSiphon = 0;
                for (const cinder::Entity& Candidate : Sim.entities())
                {
                    if (CompleteFriendly(Candidate, cinder::Kind::Processor) &&
                        Near(Candidate.pos, State.RemotePatchPoint, 520.0f))
                    {
                        State.Authored.RemoteSiphon = Candidate.id;
                        Siphon = &Candidate;
                        break;
                    }
                }
            }
            if (Worker && Worker->alive() && Worker->kind == cinder::Kind::Worker &&
                Contains(State.Authored.RemoteOre, Worker->resourceTarget))
            {
                if (Worker->carried > 0.001f) State.bRemoteWorkerCarried = true;
                if (State.bRemoteWorkerCarried && Worker->carried + 0.001f < State.Counters.LastTrackedCarried &&
                    Stats.gathered > State.Counters.PhaseStartGathered && Siphon && Siphon->alive() &&
                    Near(Worker->pos, Siphon->pos, cinder::definition(cinder::Kind::Processor).radius + 90.0f))
                    State.bRemoteDeliveryObserved = true;
                State.Counters.LastTrackedCarried = Worker->carried;
            }
            Complete = State.bRemoteDeliveryObserved;
            break;
        }
        }
        break;
    case 2:
        switch (State.Phase)
        {
        case 0: Complete = CountNewComplete(Sim, 0, cinder::Kind::Scout, State.HighestSetupEntityId) > 0; break;
        case 1:
            if (const cinder::Entity* Scout = Sim.find(State.Authored.RallyWorker))
                Complete = (State.PhaseCommandMask & CurrentPhaseProof) &&
                    CompleteFriendly(*Scout, cinder::Kind::Scout) && Near(Scout->pos, State.ScoutZoneA, 230.0f);
            break;
        case 2:
            State.bScoutAObserved = State.bScoutAObserved || Sim.explored(0, State.ScoutZoneA);
            State.bScoutBObserved = State.bScoutBObserved || Sim.explored(0, State.ScoutZoneB);
            Complete = State.bScoutAObserved && State.bScoutBObserved;
            break;
        case 3: Complete = (State.PhaseCommandMask & CurrentPhaseProof) != 0; break;
        case 4: Complete = AllDead(Sim, State.Authored.ForwardThreats); break;
        }
        break;
    case 3:
        switch (State.Phase)
        {
        case 0: Complete = State.bArmyRallyAccepted && (State.PhaseCommandMask & CurrentPhaseProof); break;
        case 1: Complete = CountNewComplete(Sim, 0, cinder::Kind::Turret, State.HighestSetupEntityId) > 0; break;
        case 2: Complete = (State.PhaseCommandMask & CurrentPhaseProof) != 0; break;
        case 3:
        case 4: Complete = State.bWaveProductionFinished &&
            (State.Authored.ActiveWave.empty() || AllDead(Sim, State.Authored.ActiveWave)); break;
        }
        break;
    case 4:
        switch (State.Phase)
        {
        case 0:
            State.bScoutAObserved = State.bScoutAObserved || Sim.explored(0, State.ScoutZoneA) ||
                std::any_of(State.Authored.SiegeGuard.begin(), State.Authored.SiegeGuard.end(), [&Sim](cinder::Id Id)
                {
                    const cinder::Entity* Entity = Sim.find(Id);
                    return Entity && Sim.visible(0, Entity->pos);
                });
            Complete = State.bScoutAObserved;
            break;
        case 1: Complete = CountNewComplete(Sim, 0, cinder::Kind::Laboratory, State.HighestSetupEntityId) > 0; break;
        case 2: Complete = Sim.players()[0].tier >= 2; break;
        case 3: Complete = Sim.players()[0].weapons >= 1; break;
        case 4:
            Complete = CountNewComplete(Sim, 0, cinder::Kind::MotorPool, State.HighestSetupEntityId) > 0 &&
                (CountNewComplete(Sim, 0, cinder::Kind::Bastion, State.HighestSetupEntityId) > 0 ||
                    CountNewComplete(Sim, 0, cinder::Kind::Kite, State.HighestSetupEntityId) > 0 ||
                    CountNewComplete(Sim, 0, cinder::Kind::Mortar, State.HighestSetupEntityId) > 0 ||
                    CountNewComplete(Sim, 0, cinder::Kind::Mender, State.HighestSetupEntityId) > 0);
            break;
        case 5:
        {
            const cinder::Entity* Anchor = Sim.find(State.Authored.EnemyAnchor);
            Complete = !Anchor || !Anchor->alive();
            break;
        }
        }
        break;
    case 5:
        Complete = Sim.winner() == 0;
        if (CountFriendlyCombatKinds(Sim) >= 3) State.OptionalMask |= 1u;
        if (CountComplete(Sim, 0, cinder::Kind::Processor) > 0) State.OptionalMask |= 1u << 1;
        if (Sim.players()[0].tier >= 2) State.OptionalMask |= 1u << 2;
        break;
    }

    if (Complete) Advance(Sim);
}

void FCinderCampaign::TrackProducedIds(const cinder::Simulation& Sim)
{
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != 1 || cinder::definition(Entity.kind).building ||
            Entity.kind == cinder::Kind::Worker || Entity.kind == cinder::Kind::Resource ||
            Contains(State.Authored.OrderedUnits, Entity.id)) continue;
        State.Authored.OrderedUnits.push_back(Entity.id);
        State.Authored.ActiveWave.push_back(Entity.id);
    }
}

void FCinderCampaign::TickWaves(cinder::Simulation& Sim)
{
    if (State.Mission != 3 || State.Phase < 3 || State.Phase > 4) return;
    const cinder::Entity* Producer = Sim.find(State.Authored.RaidProducer);
    const int Budget = State.Phase == 3 ? 3 : 4;
    if (Producer && Producer->alive() && Producer->progress >= 1.0f && State.Counters.OpponentTrainOrders < Budget)
    {
        cinder::Command Train;
        Train.type = cinder::CommandType::Train;
        Train.team = 1;
        Train.units = {Producer->id};
        Train.kind = State.Phase == 4 && State.Counters.OpponentTrainOrders % 2 ? cinder::Kind::Lancer : cinder::Kind::Striker;
        if (Sim.command(Train).accepted) ++State.Counters.OpponentTrainOrders;
    }
    TrackProducedIds(Sim);
    Producer = Sim.find(State.Authored.RaidProducer);
    const bool QueueEmpty = !Producer || !Producer->alive() || std::none_of(Producer->queue.begin(), Producer->queue.end(),
        [](const cinder::QueueItem& Item) { return !Item.research; });
    State.bWaveProductionFinished = (!Producer || !Producer->alive() || State.Counters.OpponentTrainOrders >= Budget) && QueueEmpty;

    std::vector<cinder::Id> Attackers;
    for (cinder::Id Id : State.Authored.ActiveWave)
    {
        const cinder::Entity* Entity = Sim.find(Id);
        if (Entity && Entity->alive() && Entity->order != cinder::Order::AttackMove) Attackers.push_back(Id);
    }
    // Once the announced preparation window has elapsed, each paid completion
    // joins the raid immediately. The finite queue keeps producing behind it,
    // so the player sees sustained pressure instead of waiting for a whole
    // serial queue to finish offscreen.
    if (!Attackers.empty())
    {
        cinder::Command Attack;
        Attack.type = cinder::CommandType::AttackMove;
        Attack.team = 1;
        Attack.units = std::move(Attackers);
        Attack.point = State.HomeDefense;
        Sim.command(Attack);
    }
}

void FCinderCampaign::TickOpponent(cinder::Simulation& Sim)
{
    if (!IsRunning()) return;
    cinder::Config Expected;
    if (!ExpectedConfig(State.Mission, Expected) || Sim.config().map != Expected.map ||
        Sim.config().seed != Expected.seed || Sim.config().ai != Expected.ai ||
        Sim.config().matchLength != Expected.matchLength || Sim.config().mapRevision != Expected.mapRevision || Sim.config().playerCount != 2)
    {
        Finish(ECinderCampaignOutcome::Defeat, TEXT("campaign_state_mismatch"));
        return;
    }
    if (State.Mission == 5) return;
    if (State.Mission == 3 && State.Phase >= 3 && State.Phase <= 4)
    {
        const cinder::Entity* Producer = Sim.find(State.Authored.RaidProducer);
        if (!Producer || !Producer->alive())
        {
            // A destroyed producer has no raid to prepare. Resolve the empty
            // paid wave immediately so recovery does not wait through a fake countdown.
            TickWaves(Sim);
            return;
        }
    }
    if (Sim.tick() < State.NextOpponentDecisionTick) return;
    State.NextOpponentDecisionTick = Sim.tick() + OpponentDecisionTicks;
    TickWaves(Sim);
}

cinder::Vec2 FCinderCampaign::TargetPoint(ECinderCampaignTargetRole Role) const
{
    switch (Role)
    {
    case ECinderCampaignTargetRole::PlayerAnchor: return State.BasePoint;
    case ECinderCampaignTargetRole::HomeOre: return State.HomeOrePoint;
    case ECinderCampaignTargetRole::RemoteOre: return State.RemotePatchPoint;
    case ECinderCampaignTargetRole::BuildSite:
        return State.Mission == 1 && State.Phase == 2 ? State.LocalBuildPoint : State.BuildPoint;
    case ECinderCampaignTargetRole::ScoutZoneA: return State.ScoutZoneA;
    case ECinderCampaignTargetRole::ScoutZoneB: return State.ScoutZoneB;
    case ECinderCampaignTargetRole::ForwardApproach: return State.ForwardApproach;
    case ECinderCampaignTargetRole::HomeDefense: return State.HomeDefense;
    case ECinderCampaignTargetRole::EnemyObjective:
        if (State.Mission == 0 && !State.Authored.PracticePatrol.empty()) return State.ForwardApproach;
        if (State.Mission == 2 && !State.Authored.ForwardThreats.empty()) return State.ForwardApproach;
        return State.EnemyObjectivePoint;
    case ECinderCampaignTargetRole::None: break;
    }
    return {};
}

bool FCinderCampaign::ValidateState(const cinder::Simulation& Sim, const FCinderCampaignState& Candidate,
    FString& OutError)
{
    auto Fail = [&OutError](const TCHAR* Message) { OutError = Message; return false; };
    if (Candidate.SchemaVersion != StateSchemaVersion || Candidate.ContentVersion != ContentVersion)
        return Fail(TEXT("Unsupported campaign state version."));
    if (Candidate.Mission < 0 || Candidate.Mission >= MissionCount || Candidate.Phase < 0 ||
        Candidate.Phase >= Missions[Candidate.Mission].PhaseCount || Candidate.CheckpointPhase != Candidate.Phase)
        return Fail(TEXT("Campaign mission or phase is invalid."));
    const int Outcome = static_cast<int>(Candidate.Outcome);
    if (Outcome < static_cast<int>(ECinderCampaignOutcome::Running) || Outcome > static_cast<int>(ECinderCampaignOutcome::Draw))
        return Fail(TEXT("Campaign outcome is invalid."));
    if (Candidate.MissionStartTick > Candidate.PhaseStartTick || Candidate.PhaseStartTick > Sim.tick() ||
        Candidate.ObjectiveStartTick > Sim.tick() || Candidate.NextOpponentDecisionTick > Sim.tick() + 1000000)
        return Fail(TEXT("Campaign tick ordering is invalid."));
    if (Candidate.CheckpointSerial == 0 || Candidate.CoachingLevel < 0 || Candidate.CoachingLevel > 2 ||
        Candidate.Counters.WaveIndex < 0 || Candidate.Counters.WaveIndex > 1 ||
        Candidate.Counters.OpponentTrainOrders < 0 || Candidate.Counters.OpponentTrainOrders > 8)
        return Fail(TEXT("Campaign counters are invalid."));

    cinder::Config Expected;
    if (!ExpectedConfig(Candidate.Mission, Expected) || Sim.config().map != Expected.map ||
        Sim.config().seed != Expected.seed || Sim.config().ai != Expected.ai ||
        Sim.config().matchLength != Expected.matchLength || Sim.config().mapRevision != Expected.mapRevision || Sim.config().playerCount != Expected.playerCount)
        return Fail(TEXT("Campaign simulation configuration does not match the mission."));
    const float World = Sim.worldSize();
    for (cinder::Vec2 Point : {Candidate.BasePoint, Candidate.HomeOrePoint, Candidate.EnemyObjectivePoint,
        Candidate.LocalBuildPoint, Candidate.RemotePatchPoint, Candidate.BuildPoint, Candidate.ScoutZoneA,
        Candidate.ScoutZoneB, Candidate.ForwardApproach, Candidate.HomeDefense})
        if (!ValidPoint(Point, World)) return Fail(TEXT("Campaign authored point is invalid."));

    constexpr std::uint64_t CommandBits = (std::uint64_t{1} <<
        (static_cast<int>(cinder::CommandType::Escort) + 1)) - 1;
    const std::uint64_t ObjectiveBits = (std::uint64_t{1} << Missions[Candidate.Mission].PhaseCount) - 1;
    const std::uint64_t ExpectedCompleted = Candidate.Phase == 0 ? 0 : (std::uint64_t{1} << Candidate.Phase) - 1;
    if ((Candidate.AcceptedCommandMask & ~CommandBits) != 0 ||
        (Candidate.PhaseCommandMask & ~(CommandBits | CurrentPhaseProof)) != 0 ||
        (Candidate.CompletedObjectiveMask & ~ObjectiveBits) != 0 ||
        (Candidate.AssistanceMask & ~ObjectiveBits) != 0 || (Candidate.OptionalMask & ~std::uint64_t{0xffff}) != 0)
        return Fail(TEXT("Campaign masks contain unsupported bits."));
    if (Candidate.Outcome == ECinderCampaignOutcome::Running &&
        Candidate.CompletedObjectiveMask != ExpectedCompleted)
        return Fail(TEXT("Campaign completed-objective mask does not match its phase."));
    const cinder::Id CurrentHighestId = HighestEntityId(Sim);
    cinder::Id DeclaredHighestId = std::max({Candidate.Authored.PlayerAnchor, Candidate.Authored.EnemyAnchor,
        Candidate.Authored.MiningWorker, Candidate.Authored.RallyWorker, Candidate.Authored.Kiln,
        Candidate.Authored.PlayerSiphon, Candidate.Authored.RemoteSiphon, Candidate.Authored.Resonator,
        Candidate.Authored.MotorPool, Candidate.Authored.RaidProducer});
    for (const std::vector<cinder::Id>* Ids : {&Candidate.Authored.HomeOre, &Candidate.Authored.RemoteOre,
        &Candidate.Authored.PracticePatrol, &Candidate.Authored.ScoutPosts, &Candidate.Authored.ForwardThreats,
        &Candidate.Authored.SiegeGuard, &Candidate.Authored.SiegeProducers, &Candidate.Authored.OrderedUnits,
        &Candidate.Authored.ActiveWave})
        for (cinder::Id Id : *Ids) DeclaredHighestId = std::max(DeclaredHighestId, Id);
    const cinder::Id PlausibleHighestId = std::max(CurrentHighestId, DeclaredHighestId);
    if (!Candidate.HighestSetupEntityId || Candidate.HighestSetupEntityId > PlausibleHighestId ||
        Candidate.RallyBaselineEntityId > PlausibleHighestId)
        return Fail(TEXT("Campaign entity baseline is invalid."));

    auto CheckRole = [&Sim, &Fail](cinder::Id Id, int Team, cinder::Kind Kind, bool Required)
    {
        if (!Id) return !Required;
        const cinder::Entity* Entity = Sim.find(Id);
        if (!Entity) return !Required;
        return Entity->team == Team && Entity->kind == Kind ? true :
            Fail(TEXT("Campaign authored actor has the wrong role."));
    };
    if (!CheckRole(Candidate.Authored.PlayerAnchor, 0, cinder::Kind::Headquarters, true) ||
        !CheckRole(Candidate.Authored.EnemyAnchor, 1, cinder::Kind::Headquarters, true) ||
        !CheckRole(Candidate.Authored.MiningWorker, 0, cinder::Kind::Worker, false) ||
        !(Candidate.Authored.RallyWorker == 0 ||
            (Candidate.Mission == 2 ? CheckRole(Candidate.Authored.RallyWorker, 0, cinder::Kind::Scout, false) :
                CheckRole(Candidate.Authored.RallyWorker, 0, cinder::Kind::Worker, false))) ||
        !CheckRole(Candidate.Authored.Kiln, 0, cinder::Kind::Foundry, false) ||
        !CheckRole(Candidate.Authored.PlayerSiphon, 0, cinder::Kind::Processor, false) ||
        !CheckRole(Candidate.Authored.RemoteSiphon, 0, cinder::Kind::Processor, false) ||
        !CheckRole(Candidate.Authored.Resonator, 0, cinder::Kind::Laboratory, false) ||
        !CheckRole(Candidate.Authored.MotorPool, 0, cinder::Kind::MotorPool, false)) return false;
    if (Candidate.Authored.RaidProducer)
    {
        const cinder::Entity* Producer = Sim.find(Candidate.Authored.RaidProducer);
        if (Producer && (Producer->team != 1 || Producer->kind != cinder::Kind::Foundry))
            return Fail(TEXT("Campaign raid producer has the wrong role."));
        if (!Producer && (Candidate.Mission != 3 || Candidate.Phase < 3))
            return Fail(TEXT("Campaign raid producer is missing."));
    }

    if (!ValidIdVector(Sim, Candidate.Authored.HomeOre, 8, false, OutError) ||
        !ValidIdVector(Sim, Candidate.Authored.RemoteOre, 8, false, OutError) ||
        !ValidIdVector(Sim, Candidate.Authored.PracticePatrol, 8, true, OutError) ||
        !ValidIdVector(Sim, Candidate.Authored.ScoutPosts, 8, true, OutError) ||
        !ValidIdVector(Sim, Candidate.Authored.ForwardThreats, 8, true, OutError) ||
        !ValidIdVector(Sim, Candidate.Authored.SiegeGuard, 8, true, OutError) ||
        !ValidIdVector(Sim, Candidate.Authored.SiegeProducers, 8, true, OutError) ||
        !ValidIdVector(Sim, Candidate.Authored.OrderedUnits, 512, true, OutError) ||
        !ValidIdVector(Sim, Candidate.Authored.ActiveWave, 8, true, OutError)) return false;
    if (Candidate.Authored.HomeOre.empty() || Candidate.Authored.RemoteOre.empty())
        return Fail(TEXT("Campaign ore roles are incomplete."));
    if (Candidate.Mission == 0 && Candidate.Authored.PracticePatrol.size() != 3)
        return Fail(TEXT("First Shift patrol roles are incomplete."));
    if (Candidate.Mission == 2 && (Candidate.Authored.ForwardThreats.size() != 2 ||
        Candidate.Authored.ScoutPosts != Candidate.Authored.ForwardThreats))
        return Fail(TEXT("Eyes Beyond threat roles are incomplete."));
    if (Candidate.Mission == 3 && !Candidate.Authored.RaidProducer)
        return Fail(TEXT("Hold the Line producer role is incomplete."));
    if (Candidate.Mission == 4 && (Candidate.Authored.SiegeGuard.size() != 2 ||
        Candidate.Authored.SiegeProducers.size() != 2))
        return Fail(TEXT("Break the Siege roles are incomplete."));
    for (cinder::Id Id : Candidate.Authored.HomeOre)
        if (Sim.find(Id)->kind != cinder::Kind::Resource) return Fail(TEXT("Campaign home ore role is invalid."));
    for (cinder::Id Id : Candidate.Authored.RemoteOre)
        if (Sim.find(Id)->kind != cinder::Kind::Resource) return Fail(TEXT("Campaign remote ore role is invalid."));
    auto CheckEnemyCombat = [&Sim, &Fail](const std::vector<cinder::Id>& Ids)
    {
        for (cinder::Id Id : Ids) if (const cinder::Entity* Entity = Sim.find(Id))
            if (Entity->team != 1 || cinder::definition(Entity->kind).building || Entity->kind == cinder::Kind::Resource)
                return Fail(TEXT("Campaign enemy combat role is invalid."));
        return true;
    };
    if (!CheckEnemyCombat(Candidate.Authored.PracticePatrol) ||
        !CheckEnemyCombat(Candidate.Authored.ForwardThreats) ||
        !CheckEnemyCombat(Candidate.Authored.SiegeGuard) ||
        !CheckEnemyCombat(Candidate.Authored.ActiveWave)) return false;
    for (cinder::Id Id : Candidate.Authored.SiegeProducers) if (const cinder::Entity* Entity = Sim.find(Id))
        if (Entity->team != 1 || Entity->kind != cinder::Kind::Foundry)
            return Fail(TEXT("Campaign siege producer role is invalid."));
    OutError.Reset();
    return true;
}

bool FCinderCampaign::ImportState(const cinder::Simulation& Sim, const FCinderCampaignState& Candidate,
    FString& OutError)
{
    if (!ValidateState(Sim, Candidate, OutError)) return false;
    FCinderCampaign Replacement;
    Replacement.State = Candidate;
    *this = std::move(Replacement);
    return true;
}
