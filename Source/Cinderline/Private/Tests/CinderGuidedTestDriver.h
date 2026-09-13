#pragma once

#include "CoreMinimal.h"
#include "Presentation/CinderTutorial.h"
#include "Sim/Simulation.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace CinderGuidedTestDriver
{
inline int32 StepNumber(const FCinderTutorial& Tutorial)
{
    return static_cast<int32>(Tutorial.Step());
}

inline bool HasReached(const FCinderTutorial& Tutorial, ECinderTutorialStep Target)
{
    return StepNumber(Tutorial) >= static_cast<int32>(Target);
}

inline void AdvanceOneSecond(cinder::Simulation& Sim, FCinderTutorial& Tutorial)
{
    Sim.update(1.0f);
    Tutorial.TickOpponent(Sim);
    Tutorial.Observe(Sim, {});
}

inline bool Fail(FString& Failure, const TCHAR* Message)
{
    Failure = Message;
    return false;
}

inline bool WaitUntil(cinder::Simulation& Sim, FCinderTutorial& Tutorial,
    const std::function<bool()>& Predicate, int32 Seconds, const TCHAR* Timeout, FString& Failure)
{
    for (int32 Second = 0; Second < Seconds; ++Second)
    {
        if (Predicate()) return true;
        if (!Tutorial.IsActive()) return Fail(Failure, TEXT("Guided scenario reset while waiting."));
        if (Sim.winner() >= 0 && !Predicate()) break;
        AdvanceOneSecond(Sim, Tutorial);
    }
    if (Predicate()) return true;
    return Fail(Failure, Timeout);
}

inline bool Issue(cinder::Simulation& Sim, FCinderTutorial& Tutorial,
    const cinder::Command& Command, FString& Failure)
{
    const cinder::CommandResult Result = Sim.command(Command);
    if (!Result.accepted)
    {
        Failure = FString::Printf(TEXT("Command rejected: %s"), UTF8_TO_TCHAR(Result.message.c_str()));
        return false;
    }
    Tutorial.AcceptedCommand(Sim, Command);
    return true;
}

inline std::vector<cinder::Id> CompleteFriendly(cinder::Simulation& Sim, cinder::Kind Kind)
{
    std::vector<cinder::Id> Result;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == Kind && Entity.progress >= 1.0f)
            Result.push_back(Entity.id);
    return Result;
}

inline std::vector<cinder::Id> CompleteFriendlyAttackers(cinder::Simulation& Sim)
{
    std::vector<cinder::Id> Result;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.progress >= 1.0f
            && !cinder::definition(Entity.kind).building && cinder::definition(Entity.kind).damage > 0.0f)
            Result.push_back(Entity.id);
    return Result;
}

inline cinder::Id CompleteFriendlyOfKind(cinder::Simulation& Sim, cinder::Kind Kind)
{
    const std::vector<cinder::Id> Entities = CompleteFriendly(Sim, Kind);
    return Entities.empty() ? 0 : Entities.front();
}

inline int32 CompleteFriendlyCount(cinder::Simulation& Sim, cinder::Kind Kind)
{
    return static_cast<int32>(CompleteFriendly(Sim, Kind).size());
}

inline cinder::Id IncompleteFriendlyOfKind(cinder::Simulation& Sim, cinder::Kind Kind)
{
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == Kind && Entity.progress < 1.0f)
            return Entity.id;
    return 0;
}

inline bool WaitForStep(cinder::Simulation& Sim, FCinderTutorial& Tutorial,
    ECinderTutorialStep Target, int32 Seconds, FString& Failure)
{
    return WaitUntil(Sim, Tutorial, [&] { return HasReached(Tutorial, Target); }, Seconds,
        TEXT("Timed out waiting for the next guided objective."), Failure);
}

inline bool QueueUnits(cinder::Simulation& Sim, FCinderTutorial& Tutorial,
    cinder::Kind Kind, int32 Quantity, FString& Failure)
{
    for (int32 Second = 0; Second < 600; ++Second)
    {
        if (!Tutorial.IsActive() || Sim.winner() >= 0)
            return Fail(Failure, TEXT("Guided scenario ended before unit production was accepted."));
        cinder::Command Train;
        Train.type = cinder::CommandType::AutoTrain;
        Train.team = 0;
        Train.kind = Kind;
        Train.queueIndex = Quantity;
        const cinder::CommandResult Result = Sim.command(Train);
        if (Result.accepted)
        {
            Tutorial.AcceptedCommand(Sim, Train);
            return true;
        }
        AdvanceOneSecond(Sim, Tutorial);
    }
    Failure = FString::Printf(TEXT("Could not queue %s through normal production."),
        UTF8_TO_TCHAR(cinder::definition(Kind).name));
    return false;
}

inline bool QueueUnit(cinder::Simulation& Sim, FCinderTutorial& Tutorial,
    cinder::Kind Kind, FString& Failure)
{
    return QueueUnits(Sim, Tutorial, Kind, 1, Failure);
}

inline bool BuildStructure(cinder::Simulation& Sim, FCinderTutorial& Tutorial,
    cinder::Kind Kind, FString& Failure)
{
    for (int32 Second = 0; Second < 900; ++Second)
    {
        if (!Tutorial.IsActive() || Sim.winner() >= 0)
            return Fail(Failure, TEXT("Guided scenario ended before construction was accepted."));
        if (CompleteFriendlyOfKind(Sim, Kind))
        {
            Tutorial.Observe(Sim, {});
            return true;
        }
        const cinder::Id Foundation = IncompleteFriendlyOfKind(Sim, Kind);
        if (Foundation && !Sim.constructionWorker(Foundation))
        {
            const std::vector<cinder::Id> Workers = CompleteFriendly(Sim, cinder::Kind::Worker);
            cinder::Command Resume;
            Resume.type = cinder::CommandType::ResumeConstruction;
            Resume.team = 0;
            Resume.units = Workers;
            Resume.target = Foundation;
            if (Sim.command(Resume).accepted)
                Tutorial.AcceptedCommand(Sim, Resume);
        }
        else if (!Foundation)
        {
            for (float Y = 350.0f; Y <= 1700.0f; Y += 75.0f)
            {
                for (float X = 700.0f; X <= 1750.0f; X += 75.0f)
                {
                    const cinder::Vec2 Point{X, Y};
                    if (!Sim.autoBuildStatus(0, Kind, &Point).accepted) continue;
                    cinder::Command Build;
                    Build.type = cinder::CommandType::AutoBuild;
                    Build.team = 0;
                    Build.kind = Kind;
                    Build.point = Point;
                    if (Sim.command(Build).accepted)
                    {
                        Tutorial.AcceptedCommand(Sim, Build);
                        return true;
                    }
                }
            }
        }
        AdvanceOneSecond(Sim, Tutorial);
    }
    Failure = FString::Printf(TEXT("Could not build %s through normal construction."),
        UTF8_TO_TCHAR(cinder::definition(Kind).name));
    return false;
}

inline bool QueueWeapons(cinder::Simulation& Sim, FCinderTutorial& Tutorial, FString& Failure)
{
    for (int32 Second = 0; Second < 600; ++Second)
    {
        if (!Tutorial.IsActive() || Sim.winner() >= 0)
            return Fail(Failure, TEXT("Guided scenario ended before weapons research was accepted."));
        cinder::Command Research;
        Research.type = cinder::CommandType::AutoResearch;
        Research.team = 0;
        Research.queueIndex = 1;
        const cinder::CommandResult Result = Sim.command(Research);
        if (Result.accepted)
        {
            Tutorial.AcceptedCommand(Sim, Research);
            return true;
        }
        AdvanceOneSecond(Sim, Tutorial);
    }
    return Fail(Failure, TEXT("Could not queue weapons research through the Resonator."));
}
}

inline bool DriveGuidedTutorialToStep(cinder::Simulation& Sim, FCinderTutorial& Tutorial,
    ECinderTutorialStep Target, FString& Failure)
{
    using namespace CinderGuidedTestDriver;
    Failure.Reset();
    if (!Tutorial.IsActive()) return Fail(Failure, TEXT("Guided scenario is not active."));
    if (static_cast<int32>(Target) < 0 || Target > ECinderTutorialStep::Complete)
        return Fail(Failure, TEXT("Requested guided objective is invalid."));

    for (int32 Actions = 0; Actions < 80 && !HasReached(Tutorial, Target); ++Actions)
    {
        if (!Tutorial.IsActive()) return Fail(Failure, TEXT("Guided scenario reset while it was being driven."));
        if (Sim.winner() >= 0) return Fail(Failure, TEXT("The match ended before the requested guided objective."));

        switch (Tutorial.Step())
        {
        case ECinderTutorialStep::Camera:
            Tutorial.CameraInput();
            break;
        case ECinderTutorialStep::SelectWorker:
        {
            const cinder::Id Worker = CompleteFriendlyOfKind(Sim, cinder::Kind::Worker);
            if (!Worker) return Fail(Failure, TEXT("No completed Drudge is available for selection."));
            Tutorial.Observe(Sim, {Worker});
            break;
        }
        case ECinderTutorialStep::GatherOre:
        {
            const cinder::Id Worker = CompleteFriendlyOfKind(Sim, cinder::Kind::Worker);
            const cinder::Entity* WorkerEntity = Sim.find(Worker);
            if (!WorkerEntity || !WorkerEntity->resourceTarget)
                return Fail(Failure, TEXT("No explored ore target is available to the selected Drudge."));
            cinder::Command Gather;
            Gather.type = cinder::CommandType::Gather;
            Gather.team = 0;
            Gather.units = {Worker};
            Gather.target = WorkerEntity->resourceTarget;
            if (!Issue(Sim, Tutorial, Gather, Failure)
                || !WaitForStep(Sim, Tutorial, ECinderTutorialStep::TrainWorker, 30, Failure)) return false;
            break;
        }
        case ECinderTutorialStep::TrainWorker:
            if (!QueueUnit(Sim, Tutorial, cinder::Kind::Worker, Failure)
                || !WaitForStep(Sim, Tutorial, ECinderTutorialStep::BuildKiln, 45, Failure)) return false;
            break;
        case ECinderTutorialStep::BuildKiln:
            if (!BuildStructure(Sim, Tutorial, cinder::Kind::Foundry, Failure)
                || !WaitForStep(Sim, Tutorial, ECinderTutorialStep::TrainEmbers, 140, Failure)) return false;
            break;
        case ECinderTutorialStep::TrainEmbers:
            if (!QueueUnits(Sim, Tutorial, cinder::Kind::Striker, 3, Failure)) return false;
            if (!WaitForStep(Sim, Tutorial, ECinderTutorialStep::BuildSiphon, 140, Failure)) return false;
            break;
        case ECinderTutorialStep::BuildSiphon:
            if (!BuildStructure(Sim, Tutorial, cinder::Kind::Processor, Failure)
                || !WaitForStep(Sim, Tutorial, ECinderTutorialStep::Scout, 140, Failure)) return false;
            break;
        case ECinderTutorialStep::Scout:
        {
            if (!QueueUnit(Sim, Tutorial, cinder::Kind::Scout, Failure)) return false;
            if (!WaitUntil(Sim, Tutorial, [&] { return CompleteFriendlyCount(Sim, cinder::Kind::Scout) > 0; },
                50, TEXT("The paid Skim did not finish training."), Failure)) return false;
            cinder::Command Move;
            Move.type = cinder::CommandType::Move;
            Move.team = 0;
            Move.units = {CompleteFriendlyOfKind(Sim, cinder::Kind::Scout)};
            Move.point = FCinderTutorial::ScoutPoint();
            if (!Issue(Sim, Tutorial, Move, Failure)
                || !WaitForStep(Sim, Tutorial, ECinderTutorialStep::AttackMove, 45, Failure)) return false;
            break;
        }
        case ECinderTutorialStep::AttackMove:
        {
            cinder::Command Attack;
            Attack.type = cinder::CommandType::AttackMove;
            Attack.team = 0;
            Attack.units = CompleteFriendly(Sim, cinder::Kind::Striker);
            Attack.point = FCinderTutorial::CombatPoint();
            if (Attack.units.empty()) return Fail(Failure, TEXT("No Ember squad survived to the combat lesson."));
            if (!Issue(Sim, Tutorial, Attack, Failure)
                || !WaitForStep(Sim, Tutorial, ECinderTutorialStep::BuildResonator, 90, Failure)) return false;
            break;
        }
        case ECinderTutorialStep::BuildResonator:
            if (!BuildStructure(Sim, Tutorial, cinder::Kind::Laboratory, Failure)
                || !WaitForStep(Sim, Tutorial, ECinderTutorialStep::ResearchWeapons, 160, Failure)) return false;
            break;
        case ECinderTutorialStep::ResearchWeapons:
            if (!QueueWeapons(Sim, Tutorial, Failure)
                || !WaitForStep(Sim, Tutorial, ECinderTutorialStep::Reinforce, 90, Failure)) return false;
            break;
        case ECinderTutorialStep::Reinforce:
        {
            const cinder::Id Foundry = CompleteFriendlyOfKind(Sim, cinder::Kind::Foundry);
            if (!Foundry) return Fail(Failure, TEXT("The Kiln is unavailable for reinforcement."));
            cinder::Command Rally;
            Rally.type = cinder::CommandType::AutoRally;
            Rally.team = 0;
            Rally.kind = cinder::Kind::Foundry;
            Rally.target = Foundry;
            Rally.point = FCinderTutorial::DefensePoint();
            if (!Issue(Sim, Tutorial, Rally, Failure)) return false;
            while (CompleteFriendlyCount(Sim, cinder::Kind::Striker) < 6)
            {
                if (!QueueUnit(Sim, Tutorial, cinder::Kind::Striker, Failure)) return false;
                const int32 OrderedCount = CompleteFriendlyCount(Sim, cinder::Kind::Striker);
                if (!WaitUntil(Sim, Tutorial,
                    [&] { return CompleteFriendlyCount(Sim, cinder::Kind::Striker) > OrderedCount; }, 50,
                    TEXT("A paid reinforcement did not finish training."), Failure)) return false;
            }
            if (!WaitForStep(Sim, Tutorial, ECinderTutorialStep::Defend, 20, Failure)) return false;
            break;
        }
        case ECinderTutorialStep::Defend:
        {
            cinder::Command Defend;
            Defend.type = cinder::CommandType::AttackMove;
            Defend.team = 0;
            Defend.units = CompleteFriendly(Sim, cinder::Kind::Striker);
            Defend.point = FCinderTutorial::DefensePoint();
            if (!Issue(Sim, Tutorial, Defend, Failure)
                || !WaitForStep(Sim, Tutorial, ECinderTutorialStep::DestroyAnchor, 180, Failure)) return false;
            break;
        }
        case ECinderTutorialStep::DestroyAnchor:
        {
            while (CompleteFriendlyCount(Sim, cinder::Kind::Striker) < 6)
            {
                if (!QueueUnit(Sim, Tutorial, cinder::Kind::Striker, Failure)) return false;
                const int32 OrderedCount = CompleteFriendlyCount(Sim, cinder::Kind::Striker);
                if (!WaitUntil(Sim, Tutorial,
                    [&] { return CompleteFriendlyCount(Sim, cinder::Kind::Striker) > OrderedCount; }, 50,
                    TEXT("A replacement Ember did not finish training."), Failure)) return false;
            }
            cinder::Command Assault;
            Assault.type = cinder::CommandType::AttackMove;
            Assault.team = 0;
            Assault.units = CompleteFriendlyAttackers(Sim);
            Assault.point = FCinderTutorial::EnemyAnchorPoint();
            if (!Issue(Sim, Tutorial, Assault, Failure)
                || !WaitUntil(Sim, Tutorial, [&] { return Tutorial.IsComplete() && Sim.winner() == 0; }, 240,
                    TEXT("The ordinary final assault did not destroy the opposing Anchor."), Failure)) return false;
            break;
        }
        case ECinderTutorialStep::Complete:
            break;
        }
    }
    if (!HasReached(Tutorial, Target))
        return Fail(Failure, TEXT("The guided pilot exceeded its bounded action count."));
    return true;
}
