#include "Presentation/CinderTutorial.h"

#include <algorithm>
#include <cmath>

namespace
{
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

std::vector<cinder::Id> FriendlyWorkers(const cinder::Simulation& Sim)
{
    std::vector<cinder::Id> Result;
    for (const cinder::Entity& Entity : Sim.entities())
        if (IsCompleteFriendly(Entity, cinder::Kind::Worker)) Result.push_back(Entity.id);
    return Result;
}

FString Name(cinder::Kind Kind)
{
    return UTF8_TO_TCHAR(cinder::definition(Kind).name);
}

FString CommandFailure(const cinder::CommandResult& Result)
{
    return UTF8_TO_TCHAR(Result.message.c_str());
}

FString BuildHint(const cinder::Simulation& Sim, cinder::Kind Kind, bool bWasOrdered)
{
    if (const cinder::Entity* Site = FindFoundation(Sim, Kind))
    {
        if (!Sim.constructionWorker(Site->id))
            return FString::Printf(TEXT("%s construction has no builder. Select the site, then assign a Drudge."), *Name(Kind));
        return FString::Printf(TEXT("Keep the assigned Drudge on the %s site until construction finishes."), *Name(Kind));
    }
    if (bWasOrdered)
        return FString::Printf(TEXT("That %s was canceled. Select a Drudge and place it again."), *Name(Kind));
    const cinder::CommandResult Status = Sim.buildStatus(0, Kind, FriendlyWorkers(Sim));
    if (!Status.accepted) return CommandFailure(Status);
    return FString::Printf(TEXT("Select a Drudge, choose BUILD, then place %s on visible clear ground near it."), *Name(Kind));
}

FString TrainHint(const cinder::Simulation& Sim, cinder::Kind Kind)
{
    const cinder::Definition& Unit = cinder::definition(Kind);
    const cinder::Entity* Producer = FindComplete(Sim, Unit.producer);
    if (!Producer)
        return FString::Printf(TEXT("You need an operational %s before you can train %s."), *Name(Unit.producer), *Name(Kind));
    if (Sim.players()[0].tier < Unit.tier)
        return FString::Printf(TEXT("%s is locked until technology tier %d."), *Name(Kind), Unit.tier);
    if (Sim.players()[0].ore < Unit.cost)
        return FString::Printf(TEXT("Keep mining. %s costs %d ore."), *Name(Kind), Unit.cost);
    if (Sim.supply(0) + Unit.supply > Sim.capacity(0))
        return FString::Printf(TEXT("Crew is full at %d/%d. Complete a Siphon before queuing %s."), Sim.supply(0), Sim.capacity(0), *Name(Kind));
    if (static_cast<int32>(Producer->queue.size()) >= cinder::Simulation::MaxQueue)
        return FString::Printf(TEXT("The %s queue is full at %d items. Cancel one or wait."), *Name(Unit.producer), cinder::Simulation::MaxQueue);
    return FString::Printf(TEXT("Select the %s and queue %s for %d ore."), *Name(Unit.producer), *Name(Kind), Unit.cost);
}
}

void FCinderTutorial::Reset()
{
    *this = FCinderTutorial{};
}

void FCinderTutorial::Start(const cinder::Simulation& Sim, cinder::Id PracticeTarget)
{
    Reset();
    const cinder::Entity* PracticeEntity = Sim.find(PracticeTarget);
    if (Sim.config().seed != Seed || Sim.config().ai || !PracticeEntity || !PracticeEntity->alive() ||
        PracticeEntity->team != 1 || PracticeEntity->kind != cinder::Kind::Worker || PracticeEntity->order != cinder::Order::Hold) return;
    bActive = true;
    Target = PracticeTarget;
    WorkerCount = CountComplete(Sim, cinder::Kind::Worker);
    EmberCount = CountComplete(Sim, cinder::Kind::Striker);
    if (const cinder::Entity* Anchor = FindComplete(Sim, cinder::Kind::Headquarters)) Base = Anchor->pos;
}

void FCinderTutorial::Advance()
{
    while (CurrentStep != ECinderTutorialStep::Complete && Completed[StepIndex(CurrentStep)])
        CurrentStep = static_cast<ECinderTutorialStep>(StepIndex(CurrentStep) + 1);
}

void FCinderTutorial::Observe(const cinder::Simulation& Sim, const std::vector<cinder::Id>& Selection)
{
    if (!bActive) return;
    if (Sim.config().seed != Seed)
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

    Advance();
}

void FCinderTutorial::AcceptedCommand(const cinder::Simulation& Sim, const cinder::Command& Command)
{
    if (!bActive) return;
    if (Sim.config().seed != Seed)
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
    else if (Command.type == cinder::CommandType::Train)
    {
        if (Command.kind == cinder::Kind::Worker)
        {
            if (!bWorkerOrdered) WorkerProducedAtOrder = Sim.players()[0].stats.produced;
            bWorkerOrdered = true;
        }
        else if (Command.kind == cinder::Kind::Striker)
        {
            if (!bEmbersOrdered) EmberProducedAtOrder = Sim.players()[0].stats.produced;
            bEmbersOrdered = true;
            ++EmberOrdersAccepted;
        }
    }
    else if (Command.type == cinder::CommandType::Build)
    {
        if (Command.kind == cinder::Kind::Foundry) bKilnOrdered = true;
        else if (Command.kind == cinder::Kind::Processor) bSiphonOrdered = true;
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
                bArmyAttackMoveOrdered = true;
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
        ? TEXT("All 9 practice objectives complete")
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
        Result.Title = TEXT("Train a sixth Drudge");
        Result.Body = FString::Printf(TEXT("Select your Anchor and queue a Drudge. It costs %d ore and takes %.0f seconds."),
            cinder::definition(cinder::Kind::Worker).cost, cinder::definition(cinder::Kind::Worker).buildTime);
        Result.Hint = bWorkerOrdered ? TEXT("The Drudge must finish training before this objective completes.") : TrainHint(Sim, cinder::Kind::Worker);
        Result.Progress = FString::Printf(TEXT("Drudges %d / 6  /  Objective %d of %d"), FMath::Min(Workers, 6), Number, StepCount);
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
        Result.Body = FString::Printf(TEXT("Queue three Embers at the Kiln. Each costs %d ore, takes %.0f seconds, and reserves %d crew."),
            cinder::definition(cinder::Kind::Striker).cost, cinder::definition(cinder::Kind::Striker).buildTime,
            cinder::definition(cinder::Kind::Striker).supply);
        Result.Hint = EmberOrdersAccepted < 3 ? TrainHint(Sim, cinder::Kind::Striker)
            : TEXT("Wait for all three Embers to leave the Kiln. Replace any canceled queue item or lost Ember.");
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
        Result.Title = TEXT("Scout beyond the base");
        Result.Body = FString::Printf(TEXT("Train a %s at the Kiln for %d ore, then send it toward the marked scouting area."),
            *Name(cinder::Kind::Scout), cinder::definition(cinder::Kind::Scout).cost);
        if (Scouts == 0) Result.Hint = TrainHint(Sim, cinder::Kind::Scout);
        else if (!bScoutOrdered) Result.Hint = FString::Printf(TEXT("Select the %s and give it a move or attack-move order toward the marker."), *Name(cinder::Kind::Scout));
        else Result.Hint = FString::Printf(TEXT("Let the ordered %s travel beyond the base perimeter."), *Name(cinder::Kind::Scout));
        Result.Progress += FString::Printf(TEXT("  /  %s ready %d"), *Name(cinder::Kind::Scout), Scouts);
        Result.HelpPage = 4;
        break;
    }
    case ECinderTutorialStep::AttackMove:
    {
        const cinder::Entity* PracticeTarget = Sim.find(Target);
        Result.Title = TEXT("Attack-move the Ember squad");
        Result.Body = TEXT("Select at least one Ember, arm ATTACK MOVE, and send the squad toward the combat marker. Defeat the hostile target.");
        if (CountComplete(Sim, cinder::Kind::Striker) == 0) Result.Hint = TEXT("Train a replacement Ember at the Kiln before advancing.");
        else if (!bArmyAttackMoveOrdered) Result.Hint = TEXT("A normal move or direct attack does not count. Use ATTACK MOVE with an Ember selected.");
        else if (PracticeTarget && PracticeTarget->alive()) Result.Hint = TEXT("Keep the Ember squad moving toward the combat marker until it finds the target.");
        else Result.Hint = TEXT("The target is down. Your accepted Ember attack-move completes the lesson.");
        Result.Progress += FString::Printf(TEXT("  /  Attack-move %s  /  Target %s"),
            bArmyAttackMoveOrdered ? TEXT("issued") : TEXT("needed"),
            PracticeTarget && PracticeTarget->alive() ? TEXT("active") : TEXT("defeated"));
        Result.HelpPage = 3;
        break;
    }
    case ECinderTutorialStep::Complete:
        Result.Title = TEXT("Practice complete");
        Result.Body = TEXT("You built an economy, raised a squad, scouted, and cleared the practice target under normal match rules.");
        Result.Hint = TEXT("Training is not saved. Your normal skirmish save remains untouched.");
        Result.HelpPage = 7;
        break;
    }
    return Result;
}

bool FCinderTutorial::FocusPoint(const cinder::Simulation& Sim, cinder::Vec2& Out) const
{
    if (!bActive || Sim.config().seed != Seed) return false;
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
    case ECinderTutorialStep::Complete:
        Out = Base;
        return true;
    }
    if (!Entity) return false;
    Out = Entity->pos;
    return true;
}
