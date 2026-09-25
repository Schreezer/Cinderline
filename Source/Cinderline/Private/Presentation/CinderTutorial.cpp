#include "Presentation/CinderTutorial.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
constexpr int32 OpponentUnitLimit = 4;
constexpr int32 RaidUnitCount = 3;
constexpr float OpponentDecisionInterval = 0.75f;

cinder::Vec2 EnemyFoundryPoint()
{
    return {3800, 4200};
}

bool IsTutorialConfig(const cinder::Simulation& Sim)
{
    return Sim.config().map == 0 && Sim.config().seed == FCinderTutorial::Seed && !Sim.config().ai;
}

int32 StepIndex(ECinderTutorialStep Step)
{
    return static_cast<int32>(Step);
}

float DistanceSquared(cinder::Vec2 A, cinder::Vec2 B)
{
    const float X = A.x - B.x;
    const float Y = A.y - B.y;
    return X * X + Y * Y;
}

bool IsCompleteFriendly(const cinder::Entity& Entity, cinder::Kind Kind)
{
    return Entity.alive() && Entity.team == 0 && Entity.kind == Kind && Entity.progress >= 1.0f;
}

int32 CountComplete(const cinder::Simulation& Sim, cinder::Kind Kind)
{
    return static_cast<int32>(std::count_if(Sim.entities().begin(), Sim.entities().end(), [Kind](const cinder::Entity& Entity)
    {
        return IsCompleteFriendly(Entity, Kind);
    }));
}

int32 CountQueued(const cinder::Simulation& Sim, int Team, cinder::Kind Kind)
{
    int32 Result = 0;
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != Team) continue;
        Result += static_cast<int32>(std::count_if(Entity.queue.begin(), Entity.queue.end(), [Kind](const cinder::QueueItem& Item)
        {
            return !Item.research && Item.kind == Kind;
        }));
    }
    return Result;
}

bool OpponentProductionClosed(const cinder::Simulation& Sim, cinder::Id FoundryId, int32 TrainOrders)
{
    const cinder::Entity* Foundry = Sim.find(FoundryId);
    return !Foundry || !Foundry->alive() || Foundry->progress < 1.0f ||
        (TrainOrders >= OpponentUnitLimit && CountQueued(Sim, 1, cinder::Kind::Striker) == 0);
}

bool HasQueuedResearch(const cinder::Simulation& Sim, int Team, cinder::Kind Kind)
{
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != Team) continue;
        if (std::any_of(Entity.queue.begin(), Entity.queue.end(), [Kind](const cinder::QueueItem& Item)
        {
            return Item.research && Item.kind == Kind;
        })) return true;
    }
    return false;
}

const cinder::Entity* FindComplete(const cinder::Simulation& Sim, cinder::Kind Kind)
{
    const auto It = std::find_if(Sim.entities().begin(), Sim.entities().end(), [Kind](const cinder::Entity& Entity)
    {
        return IsCompleteFriendly(Entity, Kind);
    });
    return It == Sim.entities().end() ? nullptr : &*It;
}

const cinder::Entity* FindFoundation(const cinder::Simulation& Sim, cinder::Kind Kind)
{
    const cinder::Entity* Best = nullptr;
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != 0 || Entity.kind != Kind || Entity.progress >= 1.0f) continue;
        if (!Best || Entity.progress > Best->progress) Best = &Entity;
    }
    return Best;
}

bool SelectionContains(const FCinderTutorialContext& Context, cinder::Id Id)
{
    return Id && std::find(Context.Selection.begin(), Context.Selection.end(), Id) != Context.Selection.end();
}

const cinder::Entity* SelectedFriendly(const cinder::Simulation& Sim,
    const FCinderTutorialContext& Context, cinder::Kind Kind)
{
    for (const cinder::Id Id : Context.Selection)
    {
        const cinder::Entity* Entity = Sim.find(Id);
        if (Entity && IsCompleteFriendly(*Entity, Kind)) return Entity;
    }
    return nullptr;
}

const cinder::Entity* FindOre(const cinder::Simulation& Sim, const cinder::Entity* Worker)
{
    if (Worker && Worker->resourceTarget)
    {
        const cinder::Entity* Resource = Sim.find(Worker->resourceTarget);
        if (Resource && Resource->alive() && Resource->kind == cinder::Kind::Resource
            && Resource->resource > 0 && Sim.visible(0, Resource->pos))
            return Resource;
    }
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.kind == cinder::Kind::Resource && Entity.resource > 0 && Sim.visible(0, Entity.pos))
            return &Entity;
    return nullptr;
}

bool AllFriendlyCombatSelected(const cinder::Simulation& Sim, const FCinderTutorialContext& Context)
{
    int32 Combat = 0;
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != 0 || Entity.progress < 1.0f) continue;
        const cinder::Definition& Definition = cinder::definition(Entity.kind);
        if (Definition.building || Entity.kind == cinder::Kind::Worker || Entity.kind == cinder::Kind::Resource) continue;
        ++Combat;
        if (!SelectionContains(Context, Entity.id)) return false;
    }
    return Combat > 0;
}

bool IsFriendlyCombat(const cinder::Entity& Entity)
{
    if (!Entity.alive() || Entity.team != 0 || Entity.progress < 1.0f) return false;
    const cinder::Definition& Definition = cinder::definition(Entity.kind);
    return !Definition.building && Entity.kind != cinder::Kind::Worker
        && Entity.kind != cinder::Kind::Resource;
}

int32 CountFriendlyCombat(const cinder::Simulation& Sim)
{
    return static_cast<int32>(std::count_if(Sim.entities().begin(), Sim.entities().end(),
        [](const cinder::Entity& Entity) { return IsFriendlyCombat(Entity); }));
}

bool HasActiveOrderToward(const cinder::Simulation& Sim, const cinder::Entity& Entity,
    cinder::Vec2 Objective, float Radius = 650.0f)
{
    if (!Entity.alive() || Entity.team != 0 || Entity.progress < 1.0f) return false;
    const float RadiusSquared = Radius * Radius;
    if (Entity.order == cinder::Order::Move || Entity.order == cinder::Order::AttackMove
        || Entity.order == cinder::Order::Defend)
        return DistanceSquared(Entity.goal, Objective) <= RadiusSquared;
    if (Entity.order != cinder::Order::Attack) return false;
    const cinder::Entity* Target = Sim.find(Entity.target);
    return Target && Target->alive()
        && (DistanceSquared(Target->pos, Objective) <= RadiusSquared
            || DistanceSquared(Entity.pos, Objective) <= RadiusSquared);
}

bool HasFriendlyCombatOrderToward(const cinder::Simulation& Sim, cinder::Vec2 Objective,
    cinder::Kind RequiredKind = cinder::Kind::Resource)
{
    return std::any_of(Sim.entities().begin(), Sim.entities().end(),
        [&Sim, Objective, RequiredKind](const cinder::Entity& Entity)
        {
            return IsFriendlyCombat(Entity)
                && (RequiredKind == cinder::Kind::Resource || Entity.kind == RequiredKind)
                && HasActiveOrderToward(Sim, Entity, Objective);
        });
}

FString Name(cinder::Kind Kind)
{
    return UTF8_TO_TCHAR(cinder::definition(Kind).name);
}

FString BuildHint(const cinder::Simulation& Sim, cinder::Kind Kind, bool bWasOrdered)
{
    if (const cinder::Entity* Site = FindFoundation(Sim, Kind))
    {
        if (!Sim.constructionWorker(Site->id))
            return FString::Printf(TEXT("%s construction has no builder. Open JOBS or select the site to assign a Drudge."), *Name(Kind));
        return FString::Printf(TEXT("Keep the assigned Drudge on the %s site until construction finishes."), *Name(Kind));
    }
    if (bWasOrdered)
        return FString::Printf(TEXT("That %s was canceled. Open BUILD and place it again."), *Name(Kind));
    const cinder::JobPlan Status = Sim.autoBuildStatus(0, Kind);
    if (!Status.accepted) return UTF8_TO_TCHAR(Status.message.c_str());
    return FString::Printf(TEXT("Open BUILD, choose %s, then place it on visible clear ground. A reachable Drudge is assigned automatically."), *Name(Kind));
}

FString TrainHint(const cinder::Simulation& Sim, cinder::Kind Kind)
{
    const cinder::Definition& Unit = cinder::definition(Kind);
    const cinder::JobPlan Status = Sim.autoTrainStatus(0, Kind);
    if (!Status.accepted) return UTF8_TO_TCHAR(Status.message.c_str());
    return FString::Printf(TEXT("Open TRAIN and queue %s for %d ore. An available %s is chosen automatically."),
        *Name(Kind), Unit.cost, *Name(Unit.producer));
}
}

void FCinderTutorial::Reset()
{
    *this = FCinderTutorial{};
}

bool FCinderTutorial::InitializeScenario(cinder::Simulation& Sim)
{
    cinder::Simulation CandidateSim;
    FCinderTutorial CandidateTutorial;
    cinder::Config Config;
    Config.map = 0;
    Config.seed = Seed;
    Config.ai = false;
    CandidateSim.reset(Config);

    const cinder::Vec2 FoundryPoint = EnemyFoundryPoint();
    if (!CandidateSim.canPlace(1, cinder::Kind::Foundry, FoundryPoint)) return false;
    // Authored starting actors establish the lesson. Every later enemy unit
    // comes from this Foundry through an ordinary paid production queue.
    const cinder::Id Foundry = CandidateSim.debugSpawn(cinder::Kind::Foundry, 1, FoundryPoint);
    const cinder::Id Practice = CandidateSim.debugSpawn(cinder::Kind::Striker, 1, CombatPoint());
    if (!Foundry || !Practice) return false;
    const cinder::Entity* FoundryEntity = CandidateSim.find(Foundry);
    const cinder::Entity* PracticeEntity = CandidateSim.find(Practice);
    if (!FoundryEntity || !PracticeEntity ||
        DistanceSquared(FoundryEntity->pos, FoundryPoint) > 1.0f ||
        DistanceSquared(PracticeEntity->pos, CombatPoint()) > 1.0f) return false;

    cinder::Command Hold;
    Hold.type = cinder::CommandType::Hold;
    Hold.team = 1;
    Hold.units = {Practice};
    if (!CandidateSim.command(Hold).accepted) return false;
    CandidateTutorial.Start(CandidateSim, Practice);
    if (!CandidateTutorial.bActive || CandidateTutorial.EnemyFoundry != Foundry) return false;

    CandidateTutorial.bScenarioInitialized = true;
    CandidateTutorial.LastScenarioTick = CandidateSim.tick();
    Sim = std::move(CandidateSim);
    *this = std::move(CandidateTutorial);
    ScenarioSimulation = &Sim;
    return true;
}

void FCinderTutorial::Start(const cinder::Simulation& Sim, cinder::Id PracticeTarget)
{
    Reset();
    const cinder::Entity* PracticeEntity = Sim.find(PracticeTarget);
    if (!IsTutorialConfig(Sim) || !PracticeEntity || !PracticeEntity->alive() ||
        PracticeEntity->team != 1 ||
        (PracticeEntity->kind != cinder::Kind::Worker && PracticeEntity->kind != cinder::Kind::Striker) ||
        PracticeEntity->order != cinder::Order::Hold) return;
    bActive = true;
    Target = PracticeTarget;
    WorkerCount = CountComplete(Sim, cinder::Kind::Worker);
    EmberCount = CountComplete(Sim, cinder::Kind::Striker);
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != 1) continue;
        if (Entity.kind == cinder::Kind::Foundry && Entity.progress >= 1.0f)
        {
            EnemyFoundry = Entity.id;
        }
        if (Entity.kind == cinder::Kind::Striker) OpponentUnits.push_back(Entity.id);
    }
    NextOpponentDecision = Sim.time();
    if (const cinder::Entity* Anchor = FindComplete(Sim, cinder::Kind::Headquarters)) Base = Anchor->pos;
}

void FCinderTutorial::TickOpponent(cinder::Simulation& Sim)
{
    if (!bActive) return;
    if (!IsTutorialConfig(Sim))
    {
        Reset();
        return;
    }
    const std::vector<cinder::RecordedCommand>& Recording = Sim.recording();
    const bool bHoldSignature = !Recording.empty() && Recording.front().tick == 0 &&
        Recording.front().command.type == cinder::CommandType::Hold &&
        Recording.front().command.team == 1 && Recording.front().command.units.size() == 1 &&
        Recording.front().command.units.front() == Target;
    if (!bScenarioInitialized || ScenarioSimulation != &Sim || !bHoldSignature ||
        Sim.tick() < LastScenarioTick)
    {
        Reset();
        return;
    }
    LastScenarioTick = Sim.tick();
    if (Sim.winner() != -1)
    {
        Observe(Sim, {});
        return;
    }
    if (Sim.time() + 0.0001f < NextOpponentDecision) return;
    NextOpponentDecision = Sim.time() + OpponentDecisionInterval;
    Observe(Sim, {});
    if (!bActive) return;

    const bool bSixPaidEmbers = EmberOrdersAccepted >= 6 && EmberProducedAtOrder >= 0 &&
        Sim.players()[0].stats.produced >= EmberProducedAtOrder + 6 &&
        CountComplete(Sim, cinder::Kind::Striker) >= EmberCount + 6;
    const bool bPaidWeaponUpgrade = bWeaponsOrdered && UpgradesAtWeaponOrder >= 0 &&
        Sim.players()[0].stats.upgrades > UpgradesAtWeaponOrder && Sim.players()[0].weapons >= 1;
    if (!bOpponentActivated && bSixPaidEmbers && bPaidWeaponUpgrade) bOpponentActivated = true;
    if (!bOpponentActivated) return;

    const cinder::Entity* Foundry = Sim.find(EnemyFoundry);
    if (OpponentTrainOrders < OpponentUnitLimit && Foundry && Foundry->alive() && Foundry->progress >= 1.0f)
    {
        cinder::Command Train;
        Train.type = cinder::CommandType::Train;
        Train.team = 1;
        Train.units = {EnemyFoundry};
        Train.kind = cinder::Kind::Striker;
        if (Sim.command(Train).accepted)
        {
            ++OpponentTrainOrders;
            ++OpponentOrderCount;
        }
    }

    std::vector<cinder::Id> NewUnits;
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != 1 || Entity.kind != cinder::Kind::Striker || Entity.id == Target) continue;
        if (std::find(OpponentUnits.begin(), OpponentUnits.end(), Entity.id) == OpponentUnits.end()) NewUnits.push_back(Entity.id);
    }
    std::sort(NewUnits.begin(), NewUnits.end());
    for (cinder::Id Id : NewUnits)
    {
        OpponentUnits.push_back(Id);
        if (RaidUnits.size() < RaidUnitCount) RaidUnits.push_back(Id);
    }

    std::vector<cinder::Id> SurvivingRaiders;
    for (cinder::Id Id : RaidUnits)
    {
        const cinder::Entity* Raider = Sim.find(Id);
        if (Raider && Raider->alive()) SurvivingRaiders.push_back(Id);
    }
    const bool bProductionClosed = OpponentProductionClosed(Sim, EnemyFoundry, OpponentTrainOrders);
    if (!bRaidLaunched && !SurvivingRaiders.empty() &&
        (RaidUnits.size() >= RaidUnitCount || bProductionClosed))
    {
        cinder::Command Raid;
        Raid.type = cinder::CommandType::AttackMove;
        Raid.team = 1;
        Raid.units = SurvivingRaiders;
        Raid.point = DefensePoint();
        if (Sim.command(Raid).accepted)
        {
            bRaidLaunched = true;
            ++OpponentOrderCount;
        }
    }

    if (!bDefenderHeld && OpponentUnits.size() >= static_cast<std::size_t>(OpponentUnitLimit + 1))
    {
        Defender = OpponentUnits.back();
        cinder::Command Hold;
        Hold.type = cinder::CommandType::Hold;
        Hold.team = 1;
        Hold.units = {Defender};
        if (Sim.command(Hold).accepted)
        {
            bDefenderHeld = true;
            ++OpponentOrderCount;
        }
    }
}

void FCinderTutorial::Advance()
{
    while (CurrentStep != ECinderTutorialStep::Complete && Completed[StepIndex(CurrentStep)])
        CurrentStep = static_cast<ECinderTutorialStep>(StepIndex(CurrentStep) + 1);
}

void FCinderTutorial::Observe(const cinder::Simulation& Sim, const std::vector<cinder::Id>& Selection)
{
    if (!bActive) return;
    if (!IsTutorialConfig(Sim))
    {
        Reset();
        return;
    }

    for (cinder::Id Id : Selection)
    {
        const cinder::Entity* Entity = Sim.find(Id);
        if (Entity && IsCompleteFriendly(*Entity, cinder::Kind::Worker))
        {
            Completed[StepIndex(ECinderTutorialStep::SelectWorker)] = true;
            break;
        }
    }

    if (bGatherOrdered)
    {
        const cinder::Entity* Worker = Sim.find(MiningWorker);
        if (Worker && IsCompleteFriendly(*Worker, cinder::Kind::Worker) && Worker->order == cinder::Order::Gather &&
            (std::fabs(Worker->harvestTimer - LastMiningTimer) > 0.0001f || Worker->carried > LastCarried + 0.0001f))
            Completed[StepIndex(ECinderTutorialStep::GatherOre)] = true;
    }

    if (bWorkerOrdered && WorkerProducedAtOrder >= 0 && Sim.players()[0].stats.produced > WorkerProducedAtOrder &&
        CountComplete(Sim, cinder::Kind::Worker) >= std::max(6, WorkerCount + 1))
        Completed[StepIndex(ECinderTutorialStep::TrainWorker)] = true;

    KilnProgress = 0.0f;
    SiphonProgress = 0.0f;
    ResonatorProgress = 0.0f;
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != 0) continue;
        if (Entity.kind == cinder::Kind::Foundry)
        {
            KilnProgress = std::max(KilnProgress, Entity.progress);
            if (Entity.progress >= 1.0f) Completed[StepIndex(ECinderTutorialStep::BuildKiln)] = true;
        }
        else if (Entity.kind == cinder::Kind::Processor)
        {
            SiphonProgress = std::max(SiphonProgress, Entity.progress);
            if (Entity.progress >= 1.0f) Completed[StepIndex(ECinderTutorialStep::BuildSiphon)] = true;
        }
        else if (Entity.kind == cinder::Kind::Laboratory)
        {
            ResonatorProgress = std::max(ResonatorProgress, Entity.progress);
            if (bResonatorOrdered && Entity.progress >= 1.0f)
                Completed[StepIndex(ECinderTutorialStep::BuildResonator)] = true;
        }
    }

    if (EmberOrdersAccepted >= 3 && EmberProducedAtOrder >= 0 &&
        Sim.players()[0].stats.produced >= EmberProducedAtOrder + 3 &&
        CountComplete(Sim, cinder::Kind::Striker) >= EmberCount + 3)
        Completed[StepIndex(ECinderTutorialStep::TrainEmbers)] = true;

    if (bScoutOrdered)
    {
        for (cinder::Id Id : OrderedScouts)
        {
            const cinder::Entity* Scout = Sim.find(Id);
            if (Scout && IsCompleteFriendly(*Scout, cinder::Kind::Scout) && DistanceSquared(Scout->pos, Base) >= 650.0f * 650.0f)
            {
                Completed[StepIndex(ECinderTutorialStep::Scout)] = true;
                break;
            }
        }
    }

    const cinder::Entity* PracticeTarget = Sim.find(Target);
    if (bArmyAttackMoveOrdered && Target != 0 && (!PracticeTarget || !PracticeTarget->alive()))
        Completed[StepIndex(ECinderTutorialStep::AttackMove)] = true;

    if (bWeaponsOrdered && UpgradesAtWeaponOrder >= 0 &&
        Sim.players()[0].stats.upgrades > UpgradesAtWeaponOrder && Sim.players()[0].weapons >= 1)
        Completed[StepIndex(ECinderTutorialStep::ResearchWeapons)] = true;

    if (EmberOrdersAccepted >= 6 && EmberProducedAtOrder >= 0 &&
        Sim.players()[0].stats.produced >= EmberProducedAtOrder + 6 &&
        CountComplete(Sim, cinder::Kind::Striker) >= EmberCount + 6)
        Completed[StepIndex(ECinderTutorialStep::Reinforce)] = true;

    if (bOpponentActivated)
    {
        if (Sim.winner() == 0)
            Completed[StepIndex(ECinderTutorialStep::Defend)] = true;
        else
        {
            const bool bRaidDefeated = std::all_of(RaidUnits.begin(), RaidUnits.end(), [&Sim](cinder::Id Id)
            {
                const cinder::Entity* Entity = Sim.find(Id);
                return !Entity || !Entity->alive();
            });
            if (bRaidDefeated &&
                (bRaidLaunched || OpponentProductionClosed(Sim, EnemyFoundry, OpponentTrainOrders)))
                Completed[StepIndex(ECinderTutorialStep::Defend)] = true;
        }
    }

    if (Sim.winner() == 0)
        Completed[StepIndex(ECinderTutorialStep::DestroyAnchor)] = true;

    Advance();
}

void FCinderTutorial::AcceptedCommand(const cinder::Simulation& Sim, const cinder::Command& Command)
{
    if (!bActive) return;
    if (!IsTutorialConfig(Sim))
    {
        Reset();
        return;
    }
    if (Command.team != 0) return;

    if (Command.type == cinder::CommandType::Gather)
    {
        const cinder::Entity* Resource = Sim.find(Command.target);
        if (Resource && Resource->alive() && Resource->kind == cinder::Kind::Resource && Resource->resource > 0)
        {
            for (cinder::Id Id : Command.units)
            {
                const cinder::Entity* Worker = Sim.find(Id);
                if (!Worker || !IsCompleteFriendly(*Worker, cinder::Kind::Worker)) continue;
                bGatherOrdered = true;
                MiningWorker = Id;
                LastMiningTimer = Worker->harvestTimer;
                LastCarried = Worker->carried;
                break;
            }
        }
    }
    else if (Command.type == cinder::CommandType::Train || Command.type == cinder::CommandType::AutoTrain)
    {
        const int32 Quantity = Command.type == cinder::CommandType::AutoTrain
            ? FMath::Clamp(Command.queueIndex, 1, cinder::Simulation::MaxQueue) : 1;
        if (Command.kind == cinder::Kind::Worker)
        {
            if (!bWorkerOrdered) WorkerProducedAtOrder = Sim.players()[0].stats.produced;
            bWorkerOrdered = true;
        }
        else if (Command.kind == cinder::Kind::Striker)
        {
            if (!bEmbersOrdered) EmberProducedAtOrder = Sim.players()[0].stats.produced;
            bEmbersOrdered = true;
            EmberOrdersAccepted += Quantity;
        }
        else if (Command.kind == cinder::Kind::Scout)
        {
            bScoutTrainOrdered = true;
        }
    }
    else if (Command.type == cinder::CommandType::Build || Command.type == cinder::CommandType::AutoBuild)
    {
        if (Command.kind == cinder::Kind::Foundry) bKilnOrdered = true;
        else if (Command.kind == cinder::Kind::Processor) bSiphonOrdered = true;
        else if (Command.kind == cinder::Kind::Laboratory) bResonatorOrdered = true;
    }
    else if ((Command.type == cinder::CommandType::Research || Command.type == cinder::CommandType::AutoResearch)
        && Command.queueIndex == 1)
    {
        if (!bWeaponsOrdered) UpgradesAtWeaponOrder = Sim.players()[0].stats.upgrades;
        bWeaponsOrdered = true;
    }
    else if (Command.type == cinder::CommandType::Rally || Command.type == cinder::CommandType::AutoRally)
    {
        if (Command.type == cinder::CommandType::AutoRally)
        {
            const cinder::Entity* Pinned = Command.target ? Sim.find(Command.target) : nullptr;
            bRallyOrdered = (Command.kind == cinder::Kind::Resource || Command.kind == cinder::Kind::Foundry)
                && (!Command.target || (Pinned && Pinned->alive() && Pinned->team == 0
                    && Pinned->kind == cinder::Kind::Foundry && Pinned->progress >= 1.0f));
        }
        else bRallyOrdered = std::any_of(Command.units.begin(), Command.units.end(), [&Sim](cinder::Id Id)
        {
            const cinder::Entity* Entity = Sim.find(Id);
            return Entity && Entity->alive() && Entity->team == 0 &&
                Entity->kind == cinder::Kind::Foundry && Entity->progress >= 1.0f;
        });
    }

    if (Command.type == cinder::CommandType::Move || Command.type == cinder::CommandType::AttackMove)
    {
        for (cinder::Id Id : Command.units)
        {
            const cinder::Entity* Entity = Sim.find(Id);
            if (!Entity || !Entity->alive() || Entity->team != 0) continue;
            if (Entity->kind == cinder::Kind::Scout)
            {
                bScoutOrdered = true;
                if (std::find(OrderedScouts.begin(), OrderedScouts.end(), Id) == OrderedScouts.end()) OrderedScouts.push_back(Id);
            }
            if (Command.type == cinder::CommandType::AttackMove && Entity->kind == cinder::Kind::Striker)
            {
                bArmyAttackMoveOrdered = true;
                if (CurrentStep == ECinderTutorialStep::Defend) bDefendAttackMoveOrdered = true;
                if (CurrentStep == ECinderTutorialStep::DestroyAnchor) bDestroyAttackMoveOrdered = true;
            }
        }
    }

    Observe(Sim, {});
}

void FCinderTutorial::CameraInput()
{
    if (!bActive) return;
    Completed[StepIndex(ECinderTutorialStep::Camera)] = true;
    Advance();
}

FCinderTutorialText FCinderTutorial::Text(const cinder::Simulation& Sim, bool bTouch) const
{
    FCinderTutorialText Result;
    const int32 Number = FMath::Min(StepCount, StepIndex(CurrentStep) + 1);
    Result.Progress = CurrentStep == ECinderTutorialStep::Complete
        ? TEXT("All 14 guided objectives complete")
        : FString::Printf(TEXT("Objective %d of %d"), Number, StepCount);

    switch (CurrentStep)
    {
    case ECinderTutorialStep::Camera:
        Result.Title = TEXT("Move the camera");
        Result.Body = TEXT("Look around your Anchor and the nearby ore before giving orders.");
        if (bTouch) Result.Hint = TEXT("Drag empty ground to pan. Pinch with two fingers to zoom.");
#if PLATFORM_MAC
        else Result.Hint = TEXT("Option-drag to pan. Scroll to zoom.");
#else
        else Result.Hint = TEXT("Alt-drag to pan. Scroll to zoom.");
#endif
        Result.HelpPage = 0;
        break;
    case ECinderTutorialStep::SelectWorker:
        Result.Title = TEXT("Select a Drudge");
        Result.Body = TEXT("Drudges harvest ore, build structures, and occupy one crew slot.");
        Result.Hint = bTouch ? TEXT("Tap a friendly Drudge near your Anchor.") : TEXT("Click a friendly Drudge near your Anchor.");
        Result.HelpPage = 0;
        break;
    case ECinderTutorialStep::GatherOre:
        Result.Title = TEXT("Give a mining order");
        Result.Body = TEXT("Your opening Drudges start mining. Give one a fresh ore order so you can see the full work cycle.");
        Result.Hint = bTouch ? TEXT("With a Drudge selected, tap an explored ore deposit.") : TEXT("With a Drudge selected, secondary-click an explored ore deposit.");
        Result.Progress += bGatherOrdered ? TEXT("  /  Waiting for the Drudge to mine") : TEXT("  /  Mining order needed");
        Result.HelpPage = 1;
        break;
    case ECinderTutorialStep::TrainWorker:
    {
        const int32 Workers = CountComplete(Sim, cinder::Kind::Worker);
        const int32 RequiredWorkers = std::max(6, WorkerCount + 1);
        const int32 QueuedWorkers = CountQueued(Sim, 0, cinder::Kind::Worker);
        Result.Title = TEXT("Train a sixth Drudge");
        Result.Body = FString::Printf(TEXT("Open TRAIN and queue a Drudge. It costs %d ore and takes %.0f seconds; an available Anchor is chosen automatically."),
            cinder::definition(cinder::Kind::Worker).cost, cinder::definition(cinder::Kind::Worker).buildTime);
        if (bWorkerOrdered && QueuedWorkers == 0 && Workers < RequiredWorkers)
            Result.Hint = TEXT("The Drudge job was canceled or the new Drudge was lost. Open TRAIN and queue a replacement.");
        else if (QueuedWorkers > 0)
            Result.Hint = TEXT("The Drudge must finish training before this objective completes.");
        else Result.Hint = TrainHint(Sim, cinder::Kind::Worker);
        Result.Progress = FString::Printf(TEXT("Drudges %d / %d  /  Objective %d of %d"),
            FMath::Min(Workers, RequiredWorkers), RequiredWorkers, Number, StepCount);
        Result.HelpPage = 1;
        break;
    }
    case ECinderTutorialStep::BuildKiln:
        Result.Title = TEXT("Build a Kiln");
        Result.Body = FString::Printf(TEXT("A Kiln trains your first combat units. It costs %d ore and takes %.0f seconds to build."),
            cinder::definition(cinder::Kind::Foundry).cost, cinder::definition(cinder::Kind::Foundry).buildTime);
        Result.Hint = BuildHint(Sim, cinder::Kind::Foundry, bKilnOrdered);
        Result.Progress = FString::Printf(TEXT("Kiln %d%%  /  Objective %d of %d"), FMath::RoundToInt(KilnProgress * 100.0f), Number, StepCount);
        Result.HelpPage = 2;
        break;
    case ECinderTutorialStep::TrainEmbers:
    {
        const int32 Embers = FMath::Clamp(CountComplete(Sim, cinder::Kind::Striker) - EmberCount, 0, 3);
        Result.Title = TEXT("Train three Embers");
        Result.Body = FString::Printf(TEXT("Open TRAIN and queue three Embers. Work is balanced across available Kilns; each costs %d ore, takes %.0f seconds, and reserves %d crew."),
            cinder::definition(cinder::Kind::Striker).cost, cinder::definition(cinder::Kind::Striker).buildTime,
            cinder::definition(cinder::Kind::Striker).supply);
        Result.Hint = EmberOrdersAccepted < 3 ? TrainHint(Sim, cinder::Kind::Striker)
            : TEXT("Wait for all three Embers to finish. Replace any canceled job or lost Ember through TRAIN.");
        Result.Progress = FString::Printf(TEXT("Embers ready %d / 3, orders %d / 3  /  Objective %d of %d"),
            Embers, FMath::Min(EmberOrdersAccepted, 3), Number, StepCount);
        Result.HelpPage = 3;
        break;
    }
    case ECinderTutorialStep::BuildSiphon:
        Result.Title = TEXT("Build a Siphon");
        Result.Body = FString::Printf(TEXT("A Siphon accepts ore deliveries and adds 14 crew capacity. It costs %d ore and takes %.0f seconds."),
            cinder::definition(cinder::Kind::Processor).cost, cinder::definition(cinder::Kind::Processor).buildTime);
        Result.Hint = BuildHint(Sim, cinder::Kind::Processor, bSiphonOrdered);
        Result.Progress = FString::Printf(TEXT("Siphon %d%%  /  Crew %d / %d  /  Objective %d of %d"),
            FMath::RoundToInt(SiphonProgress * 100.0f), Sim.supply(0), Sim.capacity(0), Number, StepCount);
        Result.HelpPage = 2;
        break;
    case ECinderTutorialStep::Scout:
    {
        const int32 Scouts = CountComplete(Sim, cinder::Kind::Scout);
        const int32 QueuedScouts = CountQueued(Sim, 0, cinder::Kind::Scout);
        const bool bOrderedScoutAdvancing = std::any_of(OrderedScouts.begin(), OrderedScouts.end(), [&Sim](cinder::Id Id)
        {
            const cinder::Entity* Scout = Sim.find(Id);
            return Scout && IsCompleteFriendly(*Scout, cinder::Kind::Scout) &&
                (Scout->order == cinder::Order::Move || Scout->order == cinder::Order::AttackMove);
        });
        Result.Title = TEXT("Scout beyond the base");
        Result.Body = FString::Printf(TEXT("Open TRAIN and queue a %s for %d ore, then send it toward the marked scouting area."),
            *Name(cinder::Kind::Scout), cinder::definition(cinder::Kind::Scout).cost);
        if (Scouts == 0 && QueuedScouts > 0) Result.Hint = FString::Printf(TEXT("Wait for the queued %s to finish training."), *Name(cinder::Kind::Scout));
        else if (Scouts == 0 && (bScoutTrainOrdered || bScoutOrdered)) Result.Hint = FString::Printf(TEXT("The %s job was canceled or the unit was lost. Open TRAIN and queue a replacement."), *Name(cinder::Kind::Scout));
        else if (Scouts == 0) Result.Hint = TrainHint(Sim, cinder::Kind::Scout);
        else if (!bOrderedScoutAdvancing) Result.Hint = bTouch
            ? FString::Printf(TEXT("Select a living %s. Tap ATTACK, then tap near the marker to attack-move."), *Name(cinder::Kind::Scout))
            : FString::Printf(TEXT("Select a living %s. Click ATTACK, then click near the marker to attack-move."), *Name(cinder::Kind::Scout));
        else Result.Hint = FString::Printf(TEXT("Let the ordered %s travel beyond the base perimeter."), *Name(cinder::Kind::Scout));
        Result.Progress += FString::Printf(TEXT("  /  %s ready %d"), *Name(cinder::Kind::Scout), Scouts);
        Result.HelpPage = 4;
        break;
    }
    case ECinderTutorialStep::AttackMove:
    {
        const cinder::Entity* PracticeTarget = Sim.find(Target);
        Result.Title = TEXT("Attack-move the Ember squad");
        Result.Body = bTouch
            ? TEXT("Select at least one Ember. Tap ATTACK, then tap near the combat marker to attack-move and defeat the hostile target.")
            : TEXT("Select at least one Ember. Click ATTACK, then click near the combat marker to attack-move and defeat the hostile target.");
        if (CountComplete(Sim, cinder::Kind::Striker) == 0) Result.Hint = TEXT("Open TRAIN and queue a replacement Ember before advancing.");
        else if (!bArmyAttackMoveOrdered) Result.Hint = bTouch
            ? TEXT("A normal move or direct attack does not count. Select an Ember, tap ATTACK, then tap ground.")
            : TEXT("A normal move or direct attack does not count. Select an Ember, click ATTACK, then click ground.");
        else if (PracticeTarget && PracticeTarget->alive()) Result.Hint = TEXT("Keep the Ember squad moving toward the combat marker until it finds the target.");
        else Result.Hint = TEXT("The target is down. Your accepted Ember attack-move completes the lesson.");
        Result.Progress += FString::Printf(TEXT("  /  Attack-move %s  /  Target %s"),
            bArmyAttackMoveOrdered ? TEXT("issued") : TEXT("needed"),
            PracticeTarget && PracticeTarget->alive() ? TEXT("active") : TEXT("defeated"));
        Result.HelpPage = 3;
        break;
    }
    case ECinderTutorialStep::BuildResonator:
        Result.Title = TEXT("Build a Resonator");
        Result.Body = FString::Printf(TEXT("A Resonator unlocks weapons research. It costs %d ore and takes %.0f seconds to build."),
            cinder::definition(cinder::Kind::Laboratory).cost, cinder::definition(cinder::Kind::Laboratory).buildTime);
        Result.Hint = BuildHint(Sim, cinder::Kind::Laboratory, bResonatorOrdered);
        Result.Progress = FString::Printf(TEXT("Resonator %d%%  /  Objective %d of %d"),
            FMath::RoundToInt(ResonatorProgress * 100.0f), Number, StepCount);
        Result.HelpPage = 5;
        break;
    case ECinderTutorialStep::ResearchWeapons:
    {
        const cinder::Entity* Resonator = FindComplete(Sim, cinder::Kind::Laboratory);
        Result.Title = TEXT("Research weapons level 1");
        Result.Body = TEXT("Open RESEARCH and queue WEAPONS. An available Resonator is chosen automatically, and the upgrade raises damage for every attacking unit on your side.");
        if (!Resonator) Result.Hint = TEXT("Rebuild the Resonator before queuing weapons research.");
        else if (bWeaponsOrdered && !HasQueuedResearch(Sim, 0, cinder::Kind::Striker))
            Result.Hint = TEXT("The weapons job was canceled. Open RESEARCH and queue WEAPONS again.");
        else if (!bWeaponsOrdered) Result.Hint = TEXT("Open RESEARCH and choose WEAPONS. Level 1 costs 200 ore.");
        else Result.Hint = TEXT("Keep the Resonator operational until weapons research finishes.");
        Result.Progress = FString::Printf(TEXT("Weapons level %d / 1  /  Objective %d of %d"),
            FMath::Min(Sim.players()[0].weapons, 1), Number, StepCount);
        Result.HelpPage = 5;
        break;
    }
    case ECinderTutorialStep::Reinforce:
    {
        const int32 Embers = CountComplete(Sim, cinder::Kind::Striker);
        const int32 Queued = CountQueued(Sim, 0, cinder::Kind::Striker);
        Result.Title = TEXT("Reinforce to six Embers");
        Result.Body = TEXT("Select one Kiln and set its rally near the defense marker, then open TRAIN for a six-Ember batch. Each Ember uses two crew, so add Siphons if supply is full.");
        if (Embers == 0) Result.Hint = TEXT("Your squad is gone. Open TRAIN and queue replacement Embers.");
        else if (Sim.supply(0) + cinder::definition(cinder::Kind::Striker).supply > Sim.capacity(0))
            Result.Hint = TEXT("Crew is full. Build another Siphon, then resume Ember production.");
        else if (EmberOrdersAccepted >= 6 && Queued == 0 && Embers < 6)
            Result.Hint = TEXT("A queue item was canceled or an Ember was lost. Queue replacements until six are ready.");
        else if (!bRallyOrdered) Result.Hint = TEXT("Select one Kiln and set that building's rally at the defense marker before training replacements.");
        else Result.Hint = TrainHint(Sim, cinder::Kind::Striker);
        Result.Progress = FString::Printf(TEXT("Embers ready %d / 6, queued %d  /  Objective %d of %d"),
            FMath::Min(Embers, 6), Queued, Number, StepCount);
        Result.HelpPage = 3;
        break;
    }
    case ECinderTutorialStep::Defend:
    {
        const int32 RaidersAlive = static_cast<int32>(std::count_if(RaidUnits.begin(), RaidUnits.end(), [&Sim](cinder::Id Id)
        {
            const cinder::Entity* Entity = Sim.find(Id);
            return Entity && Entity->alive();
        }));
        Result.Title = TEXT("Defend the Anchor");
        Result.Body = bTouch
            ? TEXT("A small Ember raid is forming. Open ARMY, choose ALL, tap ATTACK, then tap near the defense marker to attack-move.")
            : TEXT("A small Ember raid is forming. Open ARMY, choose ALL, click ATTACK, then click near the defense marker to attack-move.");
        if (!bRaidLaunched) Result.Hint = TEXT("Hold near the defense marker while the enemy raid assembles. It will not grow after this wave.");
        else if (CountComplete(Sim, cinder::Kind::Striker) == 0)
            Result.Hint = TEXT("Your army is gone. Queue replacement Embers if your Anchor and Kiln still stand.");
        else Result.Hint = bTouch
            ? TEXT("Open ARMY, choose ALL, tap ATTACK, then tap near DEFEND HERE. Defeat every raider.")
            : TEXT("Open ARMY, choose ALL, click ATTACK, then click near DEFEND HERE. Defeat every raider.");
        Result.Progress = FString::Printf(TEXT("Raiders remaining %d / %d  /  Objective %d of %d"),
            RaidersAlive, RaidUnitCount, Number, StepCount);
        Result.HelpPage = 3;
        break;
    }
    case ECinderTutorialStep::DestroyAnchor:
        Result.Title = TEXT("Destroy the enemy Anchor");
        Result.Body = bTouch
            ? TEXT("Open ARMY and choose ALL. Tap ATTACK, then tap near the marked enemy Anchor. Destroying it wins the match.")
            : TEXT("Open ARMY and choose ALL. Click ATTACK, then click near the marked enemy Anchor. Destroying it wins the match.");
        if (CountComplete(Sim, cinder::Kind::Striker) == 0)
            Result.Hint = TEXT("Open TRAIN and rebuild an army before crossing the map.");
        else Result.Hint = bTouch
            ? TEXT("Open ARMY, choose ALL, tap ATTACK, then tap near ENEMY ANCHOR.")
            : TEXT("Open ARMY, choose ALL, click ATTACK, then click near ENEMY ANCHOR.");
        Result.Progress += Sim.winner() == 0 ? TEXT("  /  Enemy Anchor destroyed") : TEXT("  /  Enemy Anchor standing");
        Result.HelpPage = 7;
        break;
    case ECinderTutorialStep::Complete:
        Result.Title = TEXT("Training victory");
        Result.Body = TEXT("You built an economy, upgraded and rallied an army, stopped a raid, and destroyed the enemy Anchor.");
        Result.Hint = TEXT("Training is not saved. Your normal skirmish save remains untouched.");
        Result.HelpPage = 7;
        break;
    }
    if (Sim.winner() > 0 || Sim.winner() == -2)
    {
        Result.Hint = TEXT("Your Anchor was destroyed. Restart training and rebuild the current objective with ordinary production.");
        Result.Progress += TEXT("  /  Match lost; restart needed");
    }
    return Result;
}

FCinderTutorialGuide FCinderTutorial::Guide(const cinder::Simulation& Sim,
    const FCinderTutorialContext& Context, bool bTouch) const
{
    FCinderTutorialGuide Result;
    if (!bActive || !IsTutorialConfig(Sim)) return Result;

    auto Button = [&](const FString& Instruction, const FString& Explanation,
        const TCHAR* Action, int32 Argument, int32 Index, int32 Count)
    {
        Result.Instruction = Instruction;
        Result.Explanation = Explanation;
        Result.Target = ECinderTutorialGuideTarget::Button;
        Result.ButtonAction = Action;
        Result.ButtonArgument = Argument;
        Result.ActionIndex = Index;
        Result.ActionCount = Count;
    };
    auto EntityTarget = [&](const FString& Instruction, const FString& Explanation,
        cinder::Id Id, int32 Index, int32 Count)
    {
        Result.Instruction = Instruction;
        Result.Explanation = Explanation;
        Result.Target = ECinderTutorialGuideTarget::Entity;
        Result.Entity = Id;
        Result.ActionIndex = Index;
        Result.ActionCount = Count;
    };
    auto Ground = [&](const FString& Instruction, const FString& Explanation,
        cinder::Vec2 Point, int32 Index, int32 Count)
    {
        Result.Instruction = Instruction;
        Result.Explanation = Explanation;
        Result.Target = ECinderTutorialGuideTarget::Ground;
        Result.Point = Point;
        Result.ActionIndex = Index;
        Result.ActionCount = Count;
    };
    auto Wait = [&](const FString& Instruction, const FString& Explanation, int32 Index, int32 Count)
    {
        Result.Instruction = Instruction;
        Result.Explanation = Explanation;
        Result.Target = ECinderTutorialGuideTarget::Wait;
        Result.ActionIndex = Index;
        Result.ActionCount = Count;
        Result.bWaiting = true;
    };
    auto Blocked = [&](const cinder::JobPlan& Status, const FString& Explanation, int32 Index, int32 Count)
    {
        Result.Instruction = UTF8_TO_TCHAR(Status.message.c_str());
        Result.Explanation = Explanation;
        Result.Target = ECinderTutorialGuideTarget::None;
        Result.ActionIndex = Index;
        Result.ActionCount = Count;
    };
    const bool bAnyDestinationMode = Context.bAttackMove || Context.bMoveCommand
        || Context.bDefendCommand || Context.bPatrolCommand || Context.bEscortCommand
        || Context.bBuildMode || Context.bProductionRally;
    auto GuideTrain = [&](cinder::Kind Kind, int32 Quantity, bool bOrdered,
        const FString& Explanation, int32 Offset, int32 Count)
    {
        Quantity = FMath::Clamp(Quantity, 1, cinder::Simulation::MaxQueue);
        const int32 Preset = Quantity <= 2 ? 1 : Quantity <= 5 ? 3 : 6;
        const int32 DeltaCount = FMath::Abs(Quantity - Preset);
        const int32 Total = Count + DeltaCount;
        if (bOrdered && CountQueued(Sim, 0, Kind) > 0)
        {
            if (Context.Catalog != 0)
                Button(TEXT("Close TRAIN."), Explanation, TEXT("closesheet"), 0, Offset + 5 + DeltaCount, Total);
            else Wait(FString::Printf(TEXT("Wait for %s training to finish."), *Name(Kind)), Explanation,
                Offset + 6 + DeltaCount, Total);
            return;
        }
        const cinder::JobPlan Status = Sim.autoTrainStatus(0, Kind, Quantity);
        if (!Status.accepted)
        {
            Blocked(Status, Explanation, Offset + 1, Total);
            return;
        }
        if (Context.Catalog != 8 || Context.PinnedProducer != 0)
        {
            Button(TEXT("Open TRAIN."), Explanation, TEXT("globalcatalog"), 8, Offset + 1, Total);
            return;
        }
        if (!Context.bTrainKindChosen || Context.TrainKind != Kind)
        {
            Button(FString::Printf(TEXT("Choose %s."), *Name(Kind)), Explanation,
                TEXT("trainkind"), static_cast<int32>(Kind), Offset + 2, Total);
            return;
        }
        if (!Context.bTrainQuantityChosen)
        {
            Button(FString::Printf(TEXT("Choose quantity %d."), Preset), Explanation,
                TEXT("trainqty"), Preset, Offset + 3, Total);
            return;
        }
        if (Context.TrainQuantity != Quantity)
        {
            const int32 Direction = Context.TrainQuantity < Quantity ? 1 : -1;
            Button(Direction > 0 ? TEXT("Tap + once.") : TEXT("Tap - once."), Explanation,
                TEXT("trainqtydelta"), Direction,
                Offset + 4 + FMath::Min(FMath::Abs(Context.TrainQuantity - Preset), DeltaCount), Total);
            return;
        }
        Button(FString::Printf(TEXT("Queue %d %s%s."), Quantity, *Name(Kind), Quantity == 1 ? TEXT("") : TEXT("s")),
            Explanation, TEXT("globaltrain"), 0, Offset + 4 + DeltaCount, Total);
    };
    auto GuideBuild = [&](cinder::Kind Kind, bool bWasOrdered, const FString& Explanation)
    {
        if (const cinder::Entity* Site = FindFoundation(Sim, Kind))
        {
            if (!Sim.constructionWorker(Site->id))
            {
                GuideRecoverySite = Site->id;
                if (!SelectionContains(Context, Site->id))
                {
                    if (Context.Catalog != 0)
                    {
                        Button(TEXT("Close the open panel."), Explanation, TEXT("closesheet"), 0, 4, 8);
                        return;
                    }
                    if (bAnyDestinationMode)
                    {
                        Button(TEXT("Clear the current command mode."), Explanation,
                            TEXT("tutorialclearmode"), 0, 4, 8);
                        return;
                    }
                    EntityTarget(bTouch ? TEXT("Tap the highlighted unfinished site.") : TEXT("Click the highlighted unfinished site."),
                        Explanation, Site->id, 4, 8);
                    return;
                }
                if (bAnyDestinationMode)
                {
                    Button(TEXT("Clear the current command mode."), Explanation,
                        TEXT("tutorialclearmode"), 0, 5, 8);
                    return;
                }
                if (Context.bCompact && Context.Catalog != 1)
                {
                    if (Context.Catalog != 0)
                        Button(TEXT("Close the open panel."), Explanation, TEXT("closesheet"), 0, 5, 8);
                    else Button(TEXT("Open SITE."), Explanation, TEXT("sheet"), 1, 5, 8);
                    return;
                }
                Button(TEXT("Choose ASSIGN DRUDGE."), Explanation, TEXT("resumeconstruction"), 0, 6, 8);
                return;
            }
            if (GuideRecoverySite == Site->id)
            {
                if (Context.Catalog != 0)
                {
                    Button(TEXT("Close SITE."), Explanation, TEXT("closesheet"), 0, 7, 8);
                    return;
                }
                Wait(FString::Printf(TEXT("Wait for the %s to finish."), *Name(Kind)), Explanation, 8, 8);
                return;
            }
            Wait(FString::Printf(TEXT("Wait for the %s to finish."), *Name(Kind)), Explanation, 4, 4);
            return;
        }
        GuideRecoverySite = 0;
        const cinder::JobPlan Ready = Sim.autoBuildStatus(0, Kind);
        if (!Ready.accepted)
        {
            Blocked(Ready, Explanation, 1, 4);
            return;
        }
        if (Context.bBuildMode && Context.BuildingKind == Kind)
        {
            const bool bValidationDue = !bGuideSiteCached || GuideSiteKind != Kind
                || Sim.tick() < GuideSiteValidatedTick || Sim.tick() - GuideSiteValidatedTick >= 10;
            if (bValidationDue && (!bGuideSiteCached || GuideSiteKind != Kind
                || !Sim.autoBuildStatus(0, Kind, &GuideSite).accepted))
            {
                bGuideSiteCached = false;
                GuideSiteKind = Kind;
                for (float Y = 350.0f; Y <= 1700.0f && !bGuideSiteCached; Y += 75.0f)
                    for (float X = 700.0f; X <= 1750.0f && !bGuideSiteCached; X += 75.0f)
                    {
                        const cinder::Vec2 Candidate{X, Y};
                        if (!Sim.autoBuildStatus(0, Kind, &Candidate).accepted) continue;
                        GuideSite = Candidate;
                        bGuideSiteCached = true;
                    }
            }
            if (bValidationDue) GuideSiteValidatedTick = Sim.tick();
            if (bGuideSiteCached)
            {
                Ground(bTouch ? TEXT("Tap the highlighted build site.") : TEXT("Click the highlighted build site."),
                    Explanation, GuideSite, 3, 4);
                return;
            }
            Result.Instruction = TEXT("Reveal clear ground near your base.");
            Result.Explanation = Explanation;
            Result.Target = ECinderTutorialGuideTarget::Camera;
            Result.Point = Base;
            Result.ActionIndex = 3;
            Result.ActionCount = 4;
            return;
        }
        if (Context.Catalog != 7)
        {
            Button(TEXT("Open BUILD."), Explanation, TEXT("globalcatalog"), 7, 1, 4);
            return;
        }
        Button(FString::Printf(TEXT("Choose %s."), *Name(Kind)), Explanation,
            TEXT("globalbuild"), static_cast<int32>(Kind), 2, 4);
        if (bWasOrdered) Result.Explanation += TEXT(" The earlier site was canceled, so place a new one.");
    };

    switch (CurrentStep)
    {
    case ECinderTutorialStep::Camera:
        if (bTouch) Result.Instruction = TEXT("Drag the battlefield once.");
#if PLATFORM_MAC
        else Result.Instruction = TEXT("Option-drag the battlefield once.");
#else
        else Result.Instruction = TEXT("Alt-drag the battlefield once.");
#endif
        Result.Explanation = TEXT("The camera moves your view without moving any units.");
        Result.Target = ECinderTutorialGuideTarget::Camera;
        Result.Point = Base;
        Result.ActionIndex = Result.ActionCount = 1;
        break;
    case ECinderTutorialStep::SelectWorker:
    {
        if (Context.Catalog != 0)
        {
            Button(TEXT("Close the open panel."), TEXT("Close the panel before selecting the worker."),
                TEXT("closesheet"), 0, 1, 1);
            break;
        }
        if (bAnyDestinationMode)
        {
            Button(TEXT("Clear the current command mode."), TEXT("Clear the mode before selecting the worker."),
                TEXT("tutorialclearmode"), 0, 1, 1);
            break;
        }
        const cinder::Entity* Worker = FindComplete(Sim, cinder::Kind::Worker);
        if (Worker) EntityTarget(bTouch ? TEXT("Tap the highlighted worker.") : TEXT("Click the highlighted worker."),
            TEXT("This worker is called a Drudge. It mines ore and constructs buildings."), Worker->id, 1, 1);
        else Wait(TEXT("Wait for a Drudge."), TEXT("A Drudge is your mining and construction robot."), 1, 1);
        break;
    }
    case ECinderTutorialStep::GatherOre:
    {
        if (Context.Catalog != 0)
        {
            Button(TEXT("Close the open panel."), TEXT("The battlefield must be clear before selecting a worker or ore."),
                TEXT("closesheet"), 0, 1, 2);
            break;
        }
        if (bAnyDestinationMode)
        {
            Button(TEXT("Clear the current command mode."), TEXT("Clear the mode before selecting a Drudge or ore."),
                TEXT("tutorialclearmode"), 0, 1, 2);
            break;
        }
        const cinder::Entity* Worker = SelectedFriendly(Sim, Context, cinder::Kind::Worker);
        if (!Worker)
        {
            const cinder::Entity* Available = FindComplete(Sim, cinder::Kind::Worker);
            if (Available) EntityTarget(bTouch ? TEXT("Tap the highlighted Drudge.") : TEXT("Click the highlighted Drudge."),
                TEXT("A Drudge mines ore and carries it back to a base."), Available->id, 1, 2);
            else Wait(TEXT("Wait for a Drudge."), TEXT("A Drudge is needed to mine ore."), 1, 2);
        }
        else if (bGatherOrdered)
            Wait(TEXT("Wait for the Drudge to mine ore."), TEXT("The Drudge will collect ore and return it automatically."), 2, 2);
        else if (const cinder::Entity* Ore = FindOre(Sim, Worker))
            EntityTarget(bTouch ? TEXT("Tap the highlighted ore deposit.") : TEXT("Secondary-click the highlighted ore deposit."),
                TEXT("Ore pays for units, buildings and research."), Ore->id, 2, 2);
        else Wait(TEXT("Reveal an ore deposit."), TEXT("Ore pays for units, buildings and research."), 2, 2);
        break;
    }
    case ECinderTutorialStep::TrainWorker:
        GuideTrain(cinder::Kind::Worker, 1, bWorkerOrdered,
            TEXT("A Drudge is the worker that mines ore and constructs buildings."), 0, 6);
        break;
    case ECinderTutorialStep::BuildKiln:
        GuideBuild(cinder::Kind::Foundry, bKilnOrdered,
            TEXT("A Kiln is the building that trains Ember soldiers."));
        break;
    case ECinderTutorialStep::TrainEmbers:
        GuideTrain(cinder::Kind::Striker,
            FMath::Max(1, FMath::Max(3 - EmberOrdersAccepted,
                EmberCount + 3 - CountComplete(Sim, cinder::Kind::Striker))), EmberOrdersAccepted >= 3,
            TEXT("An Ember is a basic ranged soldier trained at a Kiln."), 0, 6);
        break;
    case ECinderTutorialStep::BuildSiphon:
        GuideBuild(cinder::Kind::Processor, bSiphonOrdered,
            TEXT("A Siphon receives ore and increases your crew capacity."));
        break;
    case ECinderTutorialStep::Scout:
    {
        const cinder::Entity* Scout = FindComplete(Sim, cinder::Kind::Scout);
        if (!Scout)
        {
            GuideTrain(cinder::Kind::Scout, 1, bScoutTrainOrdered,
                TEXT("A Skim is a fast scout with a wide view."), 0, 10);
            break;
        }
        if (Context.Catalog != 0)
        {
            Button(TEXT("Close TRAIN."), TEXT("Close the catalog before selecting the new Skim."),
                TEXT("closesheet"), 0, 6, 10);
            break;
        }
        const bool bScoutAdvancing = std::any_of(OrderedScouts.begin(), OrderedScouts.end(),
            [&Sim](cinder::Id Id)
            {
                const cinder::Entity* Ordered = Sim.find(Id);
                return Ordered && Ordered->kind == cinder::Kind::Scout
                    && HasActiveOrderToward(Sim, *Ordered, FCinderTutorial::ScoutPoint());
            });
        if (bScoutOrdered && bScoutAdvancing)
        {
            Wait(TEXT("Wait for the Skim to reach the scout point."), TEXT("A Skim reveals routes before your army advances."), 10, 10);
            break;
        }
        if (!SelectionContains(Context, Scout->id) && bAnyDestinationMode)
        {
            Button(TEXT("Clear the current command mode."), TEXT("Clear the mode before selecting the Skim."),
                TEXT("tutorialclearmode"), 0, 7, 10);
            break;
        }
        if (!SelectionContains(Context, Scout->id))
            EntityTarget(bTouch ? TEXT("Tap the highlighted Skim.") : TEXT("Click the highlighted Skim."),
                TEXT("A Skim is a fast scout with a wide view."), Scout->id, 7, 10);
        else if (Context.bMoveCommand || Context.bDefendCommand)
            Button(TEXT("Clear the current command mode."), TEXT("The next action uses ATTACK for attack-move."),
                TEXT("tutorialclearmode"), 0, 8, 10);
        else if (!Context.bAttackMove)
            Button(TEXT("Choose ATTACK."), TEXT("ATTACK arms attack-move, so the scout can fight threats on its route."),
                TEXT("attack"), 0, 8, 10);
        else Ground(bTouch ? TEXT("Tap the highlighted scout point.") : TEXT("Click the highlighted scout point."),
            TEXT("The Skim will reveal the route as it travels."), ScoutPoint(), 9, 10);
        break;
    }
    case ECinderTutorialStep::AttackMove:
    {
        if (Context.Catalog != 0)
        {
            Button(TEXT("Close the open panel."), TEXT("Close the panel before selecting an Ember."),
                TEXT("closesheet"), 0, 1, 4);
            break;
        }
        if (bArmyAttackMoveOrdered
            && HasFriendlyCombatOrderToward(Sim, CombatPoint(), cinder::Kind::Striker))
        {
            Wait(TEXT("Wait for the Embers to defeat the target."), TEXT("Attack-move fights enemies found along the route."), 4, 4);
            break;
        }
        const cinder::Entity* Ember = SelectedFriendly(Sim, Context, cinder::Kind::Striker);
        if (!Ember)
        {
            if (bAnyDestinationMode)
            {
                Button(TEXT("Clear the current command mode."), TEXT("Clear the mode before selecting an Ember."),
                    TEXT("tutorialclearmode"), 0, 1, 4);
                break;
            }
            const cinder::Entity* Available = FindComplete(Sim, cinder::Kind::Striker);
            if (Available) EntityTarget(bTouch ? TEXT("Tap the highlighted Ember.") : TEXT("Click the highlighted Ember."),
                TEXT("An Ember is your basic ranged soldier."), Available->id, 1, 4);
            else if (FindComplete(Sim, cinder::Kind::Foundry))
                GuideTrain(cinder::Kind::Striker, 1, CountQueued(Sim, 0, cinder::Kind::Striker) > 0,
                    TEXT("An Ember is needed for this attack-move lesson."), 0, 6);
            else GuideBuild(cinder::Kind::Foundry, bKilnOrdered,
                TEXT("A Kiln is needed to train a replacement Ember."));
        }
        else if (Context.bMoveCommand || Context.bDefendCommand)
            Button(TEXT("Clear the current command mode."), TEXT("The next action uses ATTACK for attack-move."),
                TEXT("tutorialclearmode"), 0, 2, 4);
        else if (!Context.bAttackMove)
            Button(TEXT("Choose ATTACK."), TEXT("ATTACK arms attack-move."), TEXT("attack"), 0, 2, 4);
        else Ground(bTouch ? TEXT("Tap the highlighted attack point.") : TEXT("Click the highlighted attack point."),
            TEXT("Attack-move advances while fighting enemies encountered on the way."), CombatPoint(), 3, 4);
        break;
    }
    case ECinderTutorialStep::BuildResonator:
        GuideBuild(cinder::Kind::Laboratory, bResonatorOrdered,
            TEXT("A Resonator is the building that researches technology and upgrades."));
        break;
    case ECinderTutorialStep::ResearchWeapons:
        if (bWeaponsOrdered && HasQueuedResearch(Sim, 0, cinder::Kind::Striker))
        {
            if (Context.Catalog != 0)
                Button(TEXT("Close RESEARCH."), TEXT("Weapons research continues at the Resonator."),
                    TEXT("closesheet"), 0, 3, 4);
            else Wait(TEXT("Wait for weapons research to finish."), TEXT("Weapons research raises the damage of every armed unit."), 4, 4);
        }
        else if (!Sim.autoResearchStatus(0, 1).accepted)
            Blocked(Sim.autoResearchStatus(0, 1), TEXT("A Resonator performs research for your whole army."), 1, 4);
        else if (Context.Catalog != 9 || Context.PinnedProducer != 0)
            Button(TEXT("Open RESEARCH."), TEXT("A Resonator performs research for your whole army."),
                TEXT("globalcatalog"), 9, 1, 4);
        else Button(TEXT("Choose WEAPONS."), TEXT("Weapons level 1 raises the damage of every armed unit."),
            TEXT("globalresearch"), 1, 2, 4);
        break;
    case ECinderTutorialStep::Reinforce:
    {
        if (!bRallyOrdered)
        {
            const cinder::Entity* Kiln = FindComplete(Sim, cinder::Kind::Foundry);
            if (!Kiln) Wait(TEXT("Rebuild a Kiln first."), TEXT("A Kiln trains Ember soldiers."), 1, 8);
            else if (!SelectionContains(Context, Kiln->id) && Context.Catalog != 0)
                Button(TEXT("Close the open panel."), TEXT("Close the panel before selecting one Kiln."),
                    TEXT("closesheet"), 0, 1, 9);
            else if (!SelectionContains(Context, Kiln->id) && bAnyDestinationMode)
                Button(TEXT("Clear the current command mode."), TEXT("Clear the mode before selecting one Kiln."),
                    TEXT("tutorialclearmode"), 0, 1, 9);
            else if (!SelectionContains(Context, Kiln->id))
                EntityTarget(bTouch ? TEXT("Tap the highlighted Kiln.") : TEXT("Click the highlighted Kiln."),
                    TEXT("A rally point tells new units where to gather."), Kiln->id, 1, 9);
            else if (Context.bProductionRally && Context.PinnedProducer == Kiln->id)
                Ground(bTouch ? TEXT("Tap the highlighted defense point.") : TEXT("Click the highlighted defense point."),
                    TEXT("New Embers from this Kiln will gather here."), DefensePoint(), 4, 10);
            else if (bAnyDestinationMode)
                Button(TEXT("Clear the current command mode."), TEXT("Clear the mode before setting this Kiln's rally."),
                    TEXT("tutorialclearmode"), 0, 2, 10);
            else if ((Context.bCompact && (Context.Catalog != 8 || Context.PinnedProducer != Kiln->id))
                || (!Context.bCompact && (Context.Catalog != 5 || Context.ArmyTab != 2
                    || Context.PinnedProducer != Kiln->id)))
            {
                Button(Context.bCompact ? TEXT("Open this Kiln's TRAIN panel.") : TEXT("Open this Kiln's JOBS panel."),
                    TEXT("This panel controls only the selected Kiln."),
                    Context.bCompact ? TEXT("globalcatalog") : TEXT("producerjobs"), Context.bCompact ? 8 : 0, 2, 10);
                Result.ButtonEntity = Kiln->id;
            }
            else
            {
                Button(TEXT("Choose RALLY THIS."), TEXT("This changes only the selected Kiln's rally point."),
                    TEXT("productionrally"), 0, 3, 10);
                Result.ButtonEntity = Kiln->id;
            }
            break;
        }
        GuideTrain(cinder::Kind::Striker,
            EmberOrdersAccepted < 6 ? 6 : FMath::Max(1, EmberCount + 6 - CountComplete(Sim, cinder::Kind::Striker)),
            EmberOrdersAccepted >= 6, TEXT("A batch order is balanced across every available Kiln."), 4, 10);
        break;
    }
    case ECinderTutorialStep::Defend:
        if (CountFriendlyCombat(Sim) == 0)
        {
            if (FindComplete(Sim, cinder::Kind::Foundry))
                GuideTrain(cinder::Kind::Striker, 1, CountQueued(Sim, 0, cinder::Kind::Striker) > 0,
                    TEXT("Train a replacement Ember to defend the Anchor."), 0, 6);
            else GuideBuild(cinder::Kind::Foundry, bKilnOrdered,
                TEXT("Rebuild a Kiln before training a defending Ember."));
        }
        else if (!bRaidLaunched)
            Wait(TEXT("Wait for the enemy raid to form."), TEXT("Your next task is to defend the Anchor, your main base."), 1, 6);
        else if (bDefendAttackMoveOrdered && HasFriendlyCombatOrderToward(Sim, DefensePoint()))
            Wait(TEXT("Wait for your army to defeat the raiders."), TEXT("Your army is fighting near the defense point."), 6, 6);
        else if (!AllFriendlyCombatSelected(Sim, Context))
        {
            if (Context.Catalog == 0)
                Button(TEXT("Open ARMY."), TEXT("ARMY lists your combat units without changing production."),
                    TEXT("army"), 0, 1, 6);
            else if (Context.Catalog != 5)
                Button(TEXT("Close the open panel."), TEXT("Close this panel before opening ARMY."),
                    TEXT("closesheet"), 0, 1, 6);
            else if (Context.ArmyTab != 0)
                Button(TEXT("Open ROSTER."), TEXT("ROSTER contains the ALL army selection."),
                    TEXT("armytab"), 0, 2, 6);
            else Button(TEXT("Choose ALL."), TEXT("ALL selects every combat unit."),
                TEXT("armyall"), 0, 2, 6);
        }
        else if (Context.Catalog != 0)
            Button(TEXT("Close ARMY."), TEXT("Your combat units stay selected when the drawer closes."),
                TEXT("closesheet"), 0, 3, 6);
        else if (Context.bMoveCommand || Context.bDefendCommand)
            Button(TEXT("Clear the current command mode."), TEXT("The next action uses ATTACK for attack-move."),
                TEXT("tutorialclearmode"), 0, 4, 6);
        else if (!Context.bAttackMove)
            Button(TEXT("Choose ATTACK."), TEXT("ATTACK arms attack-move for the selected army."), TEXT("attack"), 0, 4, 6);
        else Ground(bTouch ? TEXT("Tap the highlighted defense point.") : TEXT("Click the highlighted defense point."),
            TEXT("Your army will fight raiders it meets near this point."), DefensePoint(), 5, 6);
        break;
    case ECinderTutorialStep::DestroyAnchor:
        if (CountFriendlyCombat(Sim) == 0)
        {
            if (FindComplete(Sim, cinder::Kind::Foundry))
                GuideTrain(cinder::Kind::Striker, 1, CountQueued(Sim, 0, cinder::Kind::Striker) > 0,
                    TEXT("Train a replacement Ember for the final assault."), 0, 6);
            else GuideBuild(cinder::Kind::Foundry, bKilnOrdered,
                TEXT("Rebuild a Kiln before training a new army."));
        }
        else if (bDestroyAttackMoveOrdered && HasFriendlyCombatOrderToward(Sim, EnemyAnchorPoint()))
            Wait(TEXT("Wait for your army to destroy the enemy Anchor."), TEXT("The Anchor is the enemy's main base."), 6, 6);
        else if (!AllFriendlyCombatSelected(Sim, Context))
        {
            if (Context.Catalog == 0)
                Button(TEXT("Open ARMY."), TEXT("ARMY lists every combat unit you control."),
                    TEXT("army"), 0, 1, 6);
            else if (Context.Catalog != 5)
                Button(TEXT("Close the open panel."), TEXT("Close this panel before opening ARMY."),
                    TEXT("closesheet"), 0, 1, 6);
            else if (Context.ArmyTab != 0)
                Button(TEXT("Open ROSTER."), TEXT("ROSTER contains the ALL army selection."),
                    TEXT("armytab"), 0, 2, 6);
            else Button(TEXT("Choose ALL."), TEXT("ALL selects every combat unit."),
                TEXT("armyall"), 0, 2, 6);
        }
        else if (Context.Catalog != 0)
            Button(TEXT("Close ARMY."), TEXT("Your combat units stay selected when the drawer closes."),
                TEXT("closesheet"), 0, 3, 6);
        else if (Context.bMoveCommand || Context.bDefendCommand)
            Button(TEXT("Clear the current command mode."), TEXT("The next action uses ATTACK for attack-move."),
                TEXT("tutorialclearmode"), 0, 4, 6);
        else if (!Context.bAttackMove)
            Button(TEXT("Choose ATTACK."), TEXT("ATTACK arms attack-move for the final assault."), TEXT("attack"), 0, 4, 6);
        else Ground(bTouch ? TEXT("Tap the highlighted enemy Anchor.") : TEXT("Click the highlighted enemy Anchor."),
            TEXT("Destroying the enemy Anchor wins the match."), EnemyAnchorPoint(), 5, 6);
        break;
    case ECinderTutorialStep::Complete:
        Result.Instruction = TEXT("Tutorial complete.");
        Result.Explanation = TEXT("You completed all fourteen lessons with normal game actions.");
        Result.Target = ECinderTutorialGuideTarget::None;
        Result.ActionIndex = Result.ActionCount = 1;
        break;
    }
    return Result;
}

ECinderTutorialPrimaryAction FCinderTutorial::PrimaryAction(const cinder::Simulation& Sim) const
{
    switch (CurrentStep)
    {
    case ECinderTutorialStep::BuildKiln:
        return FindFoundation(Sim, cinder::Kind::Foundry)
            ? ECinderTutorialPrimaryAction::FocusWorld : ECinderTutorialPrimaryAction::OpenBuild;
    case ECinderTutorialStep::BuildSiphon:
        return FindFoundation(Sim, cinder::Kind::Processor)
            ? ECinderTutorialPrimaryAction::FocusWorld : ECinderTutorialPrimaryAction::OpenBuild;
    case ECinderTutorialStep::BuildResonator:
        return FindFoundation(Sim, cinder::Kind::Laboratory)
            ? ECinderTutorialPrimaryAction::FocusWorld : ECinderTutorialPrimaryAction::OpenBuild;
    case ECinderTutorialStep::TrainWorker:
    case ECinderTutorialStep::TrainEmbers:
        return ECinderTutorialPrimaryAction::OpenTrain;
    case ECinderTutorialStep::Scout:
        return FindComplete(Sim, cinder::Kind::Scout)
            ? ECinderTutorialPrimaryAction::FocusWorld : ECinderTutorialPrimaryAction::OpenTrain;
    case ECinderTutorialStep::ResearchWeapons:
        return ECinderTutorialPrimaryAction::OpenResearch;
    default:
        return ECinderTutorialPrimaryAction::FocusWorld;
    }
}

bool FCinderTutorial::FocusPoint(const cinder::Simulation& Sim, cinder::Vec2& Out,
    cinder::Id* OutEntity) const
{
    if (OutEntity) *OutEntity = 0;
    if (!bActive || !IsTutorialConfig(Sim)) return false;
    const cinder::Entity* Entity = nullptr;
    switch (CurrentStep)
    {
    case ECinderTutorialStep::Camera:
        Out = Base;
        return true;
    case ECinderTutorialStep::SelectWorker:
        Entity = FindComplete(Sim, cinder::Kind::Worker);
        break;
    case ECinderTutorialStep::GatherOre:
        Entity = Sim.find(MiningWorker);
        if (!Entity || !IsCompleteFriendly(*Entity, cinder::Kind::Worker)) Entity = FindComplete(Sim, cinder::Kind::Worker);
        break;
    case ECinderTutorialStep::TrainWorker:
        Entity = FindComplete(Sim, cinder::Kind::Headquarters);
        break;
    case ECinderTutorialStep::BuildKiln:
        Entity = FindFoundation(Sim, cinder::Kind::Foundry);
        if (!Entity) Entity = FindComplete(Sim, cinder::Kind::Worker);
        break;
    case ECinderTutorialStep::TrainEmbers:
        Entity = FindComplete(Sim, cinder::Kind::Foundry);
        break;
    case ECinderTutorialStep::BuildSiphon:
        Entity = FindFoundation(Sim, cinder::Kind::Processor);
        if (!Entity) Entity = FindComplete(Sim, cinder::Kind::Worker);
        break;
    case ECinderTutorialStep::Scout:
        for (cinder::Id Id : OrderedScouts)
        {
            const cinder::Entity* Candidate = Sim.find(Id);
            if (Candidate && IsCompleteFriendly(*Candidate, cinder::Kind::Scout)) { Entity = Candidate; break; }
        }
        if (!Entity) Entity = FindComplete(Sim, cinder::Kind::Scout);
        if (!Entity) { Out = ScoutPoint(); return true; }
        break;
    case ECinderTutorialStep::AttackMove:
        Entity = FindComplete(Sim, cinder::Kind::Striker);
        if (!Entity) { Out = CombatPoint(); return true; }
        break;
    case ECinderTutorialStep::BuildResonator:
        Entity = FindFoundation(Sim, cinder::Kind::Laboratory);
        if (!Entity) Entity = FindComplete(Sim, cinder::Kind::Worker);
        break;
    case ECinderTutorialStep::ResearchWeapons:
        Entity = FindComplete(Sim, cinder::Kind::Laboratory);
        break;
    case ECinderTutorialStep::Reinforce:
        Entity = FindComplete(Sim, cinder::Kind::Foundry);
        break;
    case ECinderTutorialStep::Defend:
        Out = DefensePoint();
        return true;
    case ECinderTutorialStep::DestroyAnchor:
        Out = EnemyAnchorPoint();
        return true;
    case ECinderTutorialStep::Complete:
        Out = Base;
        return true;
    }
    if (!Entity) return false;
    Out = Entity->pos;
    if (OutEntity) *OutEntity = Entity->id;
    return true;
}
