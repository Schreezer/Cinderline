#pragma once

#include "CoreMinimal.h"
#include "Presentation/CinderCampaign.h"
#include "Sim/Simulation.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace CinderCampaignTestDriver
{
inline bool Fail(FString& Failure, const TCHAR* Message)
{
    Failure = Message;
    return false;
}

inline FString Diagnostics(const cinder::Simulation& Sim, const FCinderCampaign& Campaign)
{
    int32 FriendlyCombat = 0;
    int32 FriendlyDefending = 0;
    int32 LiveAuthoredThreats = 0;
    int32 VisibleSiegeGuards = 0;
    cinder::Vec2 ScoutPoint{-1.0f, -1.0f};
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (Entity.alive() && Entity.team == 0 && !cinder::definition(Entity.kind).building
            && cinder::definition(Entity.kind).damage > 0.0f)
        {
            ++FriendlyCombat;
            if (Entity.order == cinder::Order::Defend) ++FriendlyDefending;
        }
        if (Entity.alive() && Entity.team == 0 && Entity.kind == cinder::Kind::Scout)
            ScoutPoint = Entity.pos;
    }
    for (const std::vector<cinder::Id>* Ids : {&Campaign.Authored().PracticePatrol,
        &Campaign.Authored().ForwardThreats, &Campaign.Authored().ActiveWave})
        for (cinder::Id Id : *Ids)
            if (const cinder::Entity* Entity = Sim.find(Id); Entity && Entity->alive()) ++LiveAuthoredThreats;
    for (cinder::Id Id : Campaign.Authored().SiegeGuard)
        if (const cinder::Entity* Entity = Sim.find(Id); Entity && Entity->alive() && Sim.visible(0, Entity->pos))
            ++VisibleSiegeGuards;
    const cinder::Entity* Producer = Sim.find(Campaign.Authored().RaidProducer);
    const FCinderCampaignState State = Campaign.ExportState();
    return FString::Printf(TEXT("phase=%d/%d tick=%llu outcome=%d friendlyCombat=%d defending=%d "
        "liveAuthored=%d recordedOrders=%d wave=%d trainOrders=%d waveFinished=%d producerAlive=%d producerQueue=%d "
        "scout=(%.1f,%.1f) scoutZoneA=(%.1f,%.1f) approach=(%.1f,%.1f) exploredA=%d visibleGuards=%d"),
        Campaign.Phase(), Campaign.PhaseCount(), static_cast<unsigned long long>(Sim.tick()),
        static_cast<int32>(Campaign.Outcome()), FriendlyCombat, FriendlyDefending, LiveAuthoredThreats,
        static_cast<int32>(Sim.recording().size()), State.Counters.WaveIndex,
        State.Counters.OpponentTrainOrders, State.bWaveProductionFinished ? 1 : 0,
        Producer && Producer->alive() ? 1 : 0, Producer ? static_cast<int32>(Producer->queue.size()) : -1,
        ScoutPoint.x, ScoutPoint.y,
        Campaign.TargetPoint(ECinderCampaignTargetRole::ScoutZoneA).x,
        Campaign.TargetPoint(ECinderCampaignTargetRole::ScoutZoneA).y,
        Campaign.TargetPoint(ECinderCampaignTargetRole::ForwardApproach).x,
        Campaign.TargetPoint(ECinderCampaignTargetRole::ForwardApproach).y,
        Sim.explored(0, Campaign.TargetPoint(ECinderCampaignTargetRole::ScoutZoneA)) ? 1 : 0,
        VisibleSiegeGuards);
}

inline void AdvanceOneSecond(cinder::Simulation& Sim, FCinderCampaign& Campaign)
{
    Sim.update(1.0f);
    Campaign.TickOpponent(Sim);
    Campaign.Observe(Sim, {});
}

inline bool WaitUntil(cinder::Simulation& Sim, FCinderCampaign& Campaign,
    const std::function<bool()>& Predicate, int32 Seconds, const TCHAR* Timeout, FString& Failure)
{
    for (int32 Second = 0; Second < Seconds; ++Second)
    {
        if (Predicate()) return true;
        if (!Campaign.IsRunning()) break;
        AdvanceOneSecond(Sim, Campaign);
    }
    if (Predicate()) return true;
    Failure = FString::Printf(TEXT("%s [%s]"), Timeout, *Diagnostics(Sim, Campaign));
    return false;
}

inline bool WaitForPhase(cinder::Simulation& Sim, FCinderCampaign& Campaign,
    int32 Phase, int32 Seconds, FString& Failure)
{
    return WaitUntil(Sim, Campaign, [&] { return Campaign.Phase() >= Phase || Campaign.IsTerminal(); },
        Seconds, TEXT("Timed out waiting for the next campaign objective."), Failure);
}

inline bool Issue(cinder::Simulation& Sim, FCinderCampaign& Campaign,
    const cinder::Command& Command, FString& Failure)
{
    const cinder::CommandResult Result = Sim.command(Command);
    if (!Result.accepted)
    {
        Failure = FString::Printf(TEXT("Command rejected: %s"), UTF8_TO_TCHAR(Result.message.c_str()));
        return false;
    }
    Campaign.AcceptedCommand(Sim, Command);
    Campaign.Observe(Sim, {});
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

inline std::vector<cinder::Id> CompleteAttackers(cinder::Simulation& Sim)
{
    std::vector<cinder::Id> Result;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.progress >= 1.0f
            && !cinder::definition(Entity.kind).building && cinder::definition(Entity.kind).damage > 0.0f)
            Result.push_back(Entity.id);
    return Result;
}

inline cinder::Id FirstFriendly(cinder::Simulation& Sim, cinder::Kind Kind)
{
    const std::vector<cinder::Id> Result = CompleteFriendly(Sim, Kind);
    return Result.empty() ? 0 : Result.front();
}

inline bool Queue(cinder::Simulation& Sim, FCinderCampaign& Campaign,
    cinder::Kind Kind, int32 Quantity, FString& Failure)
{
    for (int32 Second = 0; Second < 900 && Campaign.IsRunning(); ++Second)
    {
        cinder::Command Command;
        Command.type = cinder::CommandType::AutoTrain;
        Command.team = 0;
        Command.kind = Kind;
        Command.queueIndex = Quantity;
        const cinder::CommandResult Result = Sim.command(Command);
        if (Result.accepted)
        {
            Campaign.AcceptedCommand(Sim, Command);
            return true;
        }
        AdvanceOneSecond(Sim, Campaign);
    }
    Failure = FString::Printf(TEXT("Could not queue %s through paid production."),
        UTF8_TO_TCHAR(cinder::definition(Kind).name));
    return false;
}

inline bool BuildNear(cinder::Simulation& Sim, FCinderCampaign& Campaign,
    cinder::Kind Kind, cinder::Vec2 Center, FString& Failure)
{
    for (int32 Second = 0; Second < 1200 && Campaign.IsRunning(); ++Second)
    {
        for (float Radius = 0.0f; Radius <= 900.0f; Radius += 75.0f)
        {
            const int32 Spokes = Radius == 0.0f ? 1 : 24;
            for (int32 Spoke = 0; Spoke < Spokes; ++Spoke)
            {
                const float Angle = 2.0f * PI * static_cast<float>(Spoke) / static_cast<float>(Spokes);
                const cinder::Vec2 Point{
                    FMath::Clamp(Center.x + FMath::Cos(Angle) * Radius, 1.0f, Sim.worldSize() - 1.0f),
                    FMath::Clamp(Center.y + FMath::Sin(Angle) * Radius, 1.0f, Sim.worldSize() - 1.0f)};
                if (!Sim.autoBuildStatus(0, Kind, &Point).accepted) continue;
                cinder::Command Command;
                Command.type = cinder::CommandType::AutoBuild;
                Command.team = 0;
                Command.kind = Kind;
                Command.point = Point;
                if (Issue(Sim, Campaign, Command, Failure)) return true;
            }
        }
        AdvanceOneSecond(Sim, Campaign);
    }
    Failure = FString::Printf(TEXT("Could not place %s through ordinary construction."),
        UTF8_TO_TCHAR(cinder::definition(Kind).name));
    return false;
}

inline bool Move(cinder::Simulation& Sim, FCinderCampaign& Campaign,
    const std::vector<cinder::Id>& Units, cinder::Vec2 Point, cinder::CommandType Type, FString& Failure)
{
    if (Units.empty()) return Fail(Failure, TEXT("No eligible friendly units survived for the order."));
    cinder::Command Command;
    Command.type = Type;
    Command.team = 0;
    Command.units = Units;
    Command.point = Point;
    return Issue(Sim, Campaign, Command, Failure);
}

inline bool Research(cinder::Simulation& Sim, FCinderCampaign& Campaign,
    int32 ResearchIndex, FString& Failure)
{
    for (int32 Second = 0; Second < 900 && Campaign.IsRunning(); ++Second)
    {
        cinder::Command Command;
        Command.type = cinder::CommandType::Research;
        Command.team = 0;
        Command.units = {FirstFriendly(Sim, cinder::Kind::Laboratory)};
        Command.queueIndex = ResearchIndex;
        if (!Command.units.front())
            return Fail(Failure, TEXT("No operational Resonator exists for research."));
        const cinder::CommandResult Result = Sim.command(Command);
        if (Result.accepted)
        {
            Campaign.AcceptedCommand(Sim, Command);
            Campaign.Observe(Sim, {});
            return true;
        }
        AdvanceOneSecond(Sim, Campaign);
    }
    return Fail(Failure, TEXT("Could not begin paid research through an operational Resonator."));
}

inline bool DriveAuthoredMission(cinder::Simulation& Sim, FCinderCampaign& Campaign, FString& Failure,
    bool* OutRaidDispatchedWhileQueuePending = nullptr)
{
    Failure.Reset();
    if (OutRaidDispatchedWhileQueuePending) *OutRaidDispatchedWhileQueuePending = false;
    const int32 Mission = Campaign.MissionIndex();
    if (!Campaign.IsRunning() || Mission < 0 || Mission >= 5)
        return Fail(Failure, TEXT("The driver requires an active authored mission from one through five."));

    for (int32 Action = 0; Action < 80 && Campaign.IsRunning(); ++Action)
    {
        Campaign.Observe(Sim, {});
        if (!Campaign.IsRunning()) break;
        const int32 Phase = Campaign.Phase();
        const FCinderCampaignAuthoredIds& Authored = Campaign.Authored();
        if (Mission == 0)
        {
            if (Phase == 0)
            {
                Campaign.CameraInput();
                Campaign.Observe(Sim, {});
            }
            else if (Phase == 1)
            {
                const cinder::Id Worker = Authored.MiningWorker ? Authored.MiningWorker : FirstFriendly(Sim, cinder::Kind::Worker);
                Campaign.Observe(Sim, {Worker});
            }
            else if (Phase == 2)
            {
                const cinder::Id Worker = Authored.MiningWorker ? Authored.MiningWorker : FirstFriendly(Sim, cinder::Kind::Worker);
                const cinder::Entity* WorkerEntity = Sim.find(Worker);
                const cinder::Id Ore = !Authored.HomeOre.empty() ? Authored.HomeOre.front()
                    : (WorkerEntity ? WorkerEntity->resourceTarget : 0);
                cinder::Command Gather;
                Gather.type = cinder::CommandType::Gather; Gather.team = 0; Gather.units = {Worker}; Gather.target = Ore;
                if (!Issue(Sim, Campaign, Gather, Failure) || !WaitForPhase(Sim, Campaign, 3, 90, Failure)) return false;
            }
            else if (Phase == 3)
            {
                if (!Queue(Sim, Campaign, cinder::Kind::Worker, 1, Failure)
                    || !WaitForPhase(Sim, Campaign, 4, 90, Failure)) return false;
            }
            else if (Phase == 4)
            {
                if (!BuildNear(Sim, Campaign, cinder::Kind::Foundry, Campaign.TargetPoint(ECinderCampaignTargetRole::BuildSite), Failure)
                    || !WaitForPhase(Sim, Campaign, 5, 180, Failure)) return false;
            }
            else if (Phase == 5)
            {
                if (!Queue(Sim, Campaign, cinder::Kind::Striker, 3, Failure)
                    || !WaitForPhase(Sim, Campaign, 6, 180, Failure)) return false;
            }
            else if (Phase == 6)
            {
                const int32 Existing = static_cast<int32>(CompleteFriendly(Sim, cinder::Kind::Striker).size());
                if (Existing < 4)
                {
                    if (!Queue(Sim, Campaign, cinder::Kind::Striker, 1, Failure)
                        || !WaitUntil(Sim, Campaign,
                            [&] { return static_cast<int32>(CompleteFriendly(Sim, cinder::Kind::Striker).size()) > Existing; },
                            90, TEXT("An additional paid Ember did not finish before the patrol attack."), Failure)) return false;
                }
                if (!Move(Sim, Campaign, CompleteAttackers(Sim),
                    Campaign.TargetPoint(ECinderCampaignTargetRole::EnemyObjective), cinder::CommandType::AttackMove, Failure)
                    || !WaitForPhase(Sim, Campaign, 7, 30, Failure)) return false;
            }
            else if (!WaitUntil(Sim, Campaign, [&] { return Campaign.IsTerminal(); }, 240,
                TEXT("The normally produced Ember force did not clear the practice patrol."), Failure)) return false;
        }
        else if (Mission == 1)
        {
            if (Phase == 0)
            {
                const cinder::Id Ore = Authored.HomeOre.empty() ? 0 : Authored.HomeOre.front();
                const cinder::Entity* OreEntity = Sim.find(Ore);
                cinder::Command Rally;
                Rally.type = cinder::CommandType::AutoRally; Rally.team = 0;
                Rally.kind = cinder::Kind::Headquarters; Rally.target = Authored.PlayerAnchor;
                Rally.point = OreEntity ? OreEntity->pos : Campaign.TargetPoint(ECinderCampaignTargetRole::HomeOre);
                if (!Issue(Sim, Campaign, Rally, Failure)) return false;
            }
            else if (Phase == 1)
            {
                if (!Queue(Sim, Campaign, cinder::Kind::Worker, 1, Failure)
                    || !WaitForPhase(Sim, Campaign, 2, 120, Failure)) return false;
            }
            else if (Phase == 2)
            {
                if (!BuildNear(Sim, Campaign, cinder::Kind::Processor, Campaign.TargetPoint(ECinderCampaignTargetRole::BuildSite), Failure)
                    || !WaitForPhase(Sim, Campaign, 3, 180, Failure)) return false;
            }
            else if (Phase == 3)
            {
                const cinder::Id Worker = FirstFriendly(Sim, cinder::Kind::Worker);
                const cinder::Id Ore = Authored.RemoteOre.empty() ? 0 : Authored.RemoteOre.front();
                cinder::Command Gather;
                Gather.type = cinder::CommandType::Gather; Gather.team = 0; Gather.units = {Worker}; Gather.target = Ore;
                if (!Issue(Sim, Campaign, Gather, Failure)) return false;
            }
            else if (Phase == 4)
            {
                if (!BuildNear(Sim, Campaign, cinder::Kind::Processor,
                    Campaign.TargetPoint(ECinderCampaignTargetRole::RemoteOre), Failure)
                    || !WaitForPhase(Sim, Campaign, 5, 180, Failure)) return false;
            }
            else if (!WaitUntil(Sim, Campaign, [&] { return Campaign.IsTerminal(); }, 240,
                TEXT("The relocated worker did not deliver ore to the remote Siphon."), Failure)) return false;
        }
        else if (Mission == 2)
        {
            if (Phase == 0)
            {
                if (!FirstFriendly(Sim, cinder::Kind::Foundry)
                    && (!BuildNear(Sim, Campaign, cinder::Kind::Foundry,
                        Campaign.TargetPoint(ECinderCampaignTargetRole::BuildSite), Failure)
                        || !WaitUntil(Sim, Campaign, [&] { return FirstFriendly(Sim, cinder::Kind::Foundry) != 0; }, 180,
                            TEXT("The prerequisite Kiln did not finish for Eyes Beyond."), Failure))) return false;
                if (!Queue(Sim, Campaign, cinder::Kind::Scout, 1, Failure)
                    || !WaitForPhase(Sim, Campaign, 1, 120, Failure)) return false;
            }
            else if (Phase == 1)
            {
                const int32 Existing = static_cast<int32>(CompleteFriendly(Sim, cinder::Kind::Striker).size());
                if (Existing < 4)
                {
                    if (!Queue(Sim, Campaign, cinder::Kind::Striker, 4 - Existing, Failure)
                        || !WaitUntil(Sim, Campaign,
                            [&] { return static_cast<int32>(CompleteFriendly(Sim, cinder::Kind::Striker).size()) >= 4; }, 180,
                            TEXT("The paid scouting escort did not finish production."), Failure)) return false;
                }
                const cinder::Id Scout = FirstFriendly(Sim, cinder::Kind::Scout);
                std::vector<cinder::Id> MixedSelection{FirstFriendly(Sim, cinder::Kind::Striker), Scout};
                if (!Move(Sim, Campaign, MixedSelection,
                    Campaign.TargetPoint(ECinderCampaignTargetRole::ScoutZoneA), cinder::CommandType::Move, Failure)) return false;
                if (Campaign.Authored().RallyWorker != Scout)
                    return Fail(Failure, TEXT("Mixed Move provenance did not retain the qualifying Skim ID."));
                if (!WaitForPhase(Sim, Campaign, 2, 120, Failure)) return false;
            }
            else if (Phase == 2)
            {
                if (!Move(Sim, Campaign, CompleteFriendly(Sim, cinder::Kind::Scout),
                    Campaign.TargetPoint(ECinderCampaignTargetRole::ScoutZoneB), cinder::CommandType::Move, Failure)
                    || !WaitForPhase(Sim, Campaign, 3, 120, Failure)) return false;
            }
            else if (Phase == 3)
            {
                if (!Move(Sim, Campaign, CompleteAttackers(Sim),
                    Campaign.TargetPoint(ECinderCampaignTargetRole::EnemyObjective), cinder::CommandType::AttackMove, Failure)) return false;
            }
            else
            {
                for (cinder::Id ThreatId : Authored.ForwardThreats)
                {
                    const cinder::Entity* Threat = Sim.find(ThreatId);
                    if (!Threat || !Threat->alive()) continue;
                    const cinder::Vec2 ThreatPoint = Threat->pos;
                    if (!Move(Sim, Campaign, CompleteAttackers(Sim), ThreatPoint,
                        cinder::CommandType::AttackMove, Failure)
                        || !WaitUntil(Sim, Campaign, [&]
                            {
                                const cinder::Entity* Current = Sim.find(ThreatId);
                                return !Current || !Current->alive() || Campaign.IsTerminal();
                            }, 180, TEXT("The paid force did not clear an authored forward threat."), Failure)) return false;
                }
                if (!WaitUntil(Sim, Campaign, [&] { return Campaign.IsTerminal(); }, 30,
                    TEXT("Both forward threats died without completing Eyes Beyond."), Failure)) return false;
            }
        }
        else if (Mission == 3)
        {
            if (Phase == 0)
            {
                cinder::Command Rally;
                Rally.type = cinder::CommandType::AutoRally; Rally.team = 0;
                Rally.kind = cinder::Kind::Resource;
                Rally.point = Campaign.TargetPoint(ECinderCampaignTargetRole::HomeDefense);
                if (!Issue(Sim, Campaign, Rally, Failure)) return false;
            }
            else if (Phase == 1)
            {
                if (!FirstFriendly(Sim, cinder::Kind::Foundry)
                    && (!BuildNear(Sim, Campaign, cinder::Kind::Foundry,
                        Campaign.TargetPoint(ECinderCampaignTargetRole::BuildSite), Failure)
                        || !WaitUntil(Sim, Campaign, [&] { return FirstFriendly(Sim, cinder::Kind::Foundry) != 0; }, 180,
                            TEXT("The defensive Kiln did not finish construction."), Failure))) return false;
                constexpr int32 DesiredDefenders = 10;
                const int32 Existing = static_cast<int32>(CompleteFriendly(Sim, cinder::Kind::Striker).size());
                const int32 RequiredSupply = cinder::definition(cinder::Kind::Striker).supply
                    * FMath::Max(0, DesiredDefenders - Existing);
                if (Sim.capacity(0) - Sim.supply(0) < RequiredSupply
                    && (!BuildNear(Sim, Campaign, cinder::Kind::Processor,
                        Campaign.TargetPoint(ECinderCampaignTargetRole::BuildSite), Failure)
                        || !WaitUntil(Sim, Campaign, [&] { return Sim.capacity(0) - Sim.supply(0) >= RequiredSupply; }, 180,
                            TEXT("The defensive Siphon did not provide reinforcement capacity."), Failure))) return false;
                if (Existing < DesiredDefenders)
                {
                    if (!Queue(Sim, Campaign, cinder::Kind::Striker, DesiredDefenders - Existing, Failure)
                        || !WaitUntil(Sim, Campaign,
                            [&] { return static_cast<int32>(CompleteFriendly(Sim, cinder::Kind::Striker).size()) >= DesiredDefenders; }, 300,
                            TEXT("The paid defensive force did not finish production."), Failure)) return false;
                }
                if (!BuildNear(Sim, Campaign, cinder::Kind::Turret,
                    Campaign.TargetPoint(ECinderCampaignTargetRole::HomeDefense), Failure)
                    || !WaitForPhase(Sim, Campaign, 2, 180, Failure)) return false;
            }
            else if (Phase == 2)
            {
                if (!Move(Sim, Campaign, CompleteAttackers(Sim),
                    Campaign.TargetPoint(ECinderCampaignTargetRole::HomeDefense), cinder::CommandType::Defend, Failure)) return false;
            }
            else
            {
                for (int32 Second = 0; Second < 600 && Campaign.IsRunning(); ++Second)
                {
                    const std::size_t RecordingBefore = Sim.recording().size();
                    AdvanceOneSecond(Sim, Campaign);
                    const cinder::Entity* Producer = Sim.find(Campaign.Authored().RaidProducer);
                    const bool bPending = Producer && std::any_of(Producer->queue.begin(), Producer->queue.end(),
                        [](const cinder::QueueItem& Item) { return !Item.research; });
                    const auto FirstNew = Sim.recording().begin()
                        + static_cast<std::vector<cinder::RecordedCommand>::difference_type>(RecordingBefore);
                    const bool bAttackRecorded = std::any_of(FirstNew, Sim.recording().end(),
                        [](const cinder::RecordedCommand& Recorded)
                        {
                            return Recorded.command.team == 1
                                && Recorded.command.type == cinder::CommandType::AttackMove;
                        });
                    if (OutRaidDispatchedWhileQueuePending && bPending && bAttackRecorded)
                        *OutRaidDispatchedWhileQueuePending = true;
                }
                if (!Campaign.IsTerminal())
                {
                    Failure = FString::Printf(TEXT("The bounded paid defensive waves did not resolve. [%s]"),
                        *Diagnostics(Sim, Campaign));
                    return false;
                }
            }
        }
        else if (Mission == 4)
        {
            if (Phase == 0)
            {
                if (!FirstFriendly(Sim, cinder::Kind::Foundry)
                    && (!BuildNear(Sim, Campaign, cinder::Kind::Foundry,
                        Campaign.TargetPoint(ECinderCampaignTargetRole::BuildSite), Failure)
                        || !WaitUntil(Sim, Campaign, [&] { return FirstFriendly(Sim, cinder::Kind::Foundry) != 0; }, 180,
                            TEXT("The prerequisite Kiln did not finish for Break the Siege."), Failure))) return false;
                if (!FirstFriendly(Sim, cinder::Kind::Scout))
                {
                    if (!Queue(Sim, Campaign, cinder::Kind::Scout, 1, Failure)
                        || !WaitUntil(Sim, Campaign, [&] { return FirstFriendly(Sim, cinder::Kind::Scout) != 0; }, 120,
                            TEXT("The paid siege scout did not finish production."), Failure)) return false;
                }
                std::vector<cinder::Id> Scouts = CompleteFriendly(Sim, cinder::Kind::Scout);
                if (Scouts.empty()) Scouts = CompleteAttackers(Sim);
                if (!Move(Sim, Campaign, Scouts,
                    Campaign.TargetPoint(ECinderCampaignTargetRole::ScoutZoneA), cinder::CommandType::Move, Failure)
                    || !WaitForPhase(Sim, Campaign, 1, 180, Failure)) return false;
            }
            else if (Phase == 1)
            {
                if (!BuildNear(Sim, Campaign, cinder::Kind::Laboratory,
                    Campaign.TargetPoint(ECinderCampaignTargetRole::BuildSite), Failure)
                    || !WaitForPhase(Sim, Campaign, 2, 180, Failure)) return false;
            }
            else if (Phase == 2)
            {
                if (!Research(Sim, Campaign, 0, Failure)
                    || !WaitForPhase(Sim, Campaign, 3, 180, Failure)) return false;
            }
            else if (Phase == 3)
            {
                if (!Research(Sim, Campaign, 1, Failure)
                    || !WaitForPhase(Sim, Campaign, 4, 120, Failure)) return false;
            }
            else if (Phase == 4)
            {
                if (!BuildNear(Sim, Campaign, cinder::Kind::MotorPool,
                    Campaign.TargetPoint(ECinderCampaignTargetRole::BuildSite), Failure)
                    || !WaitUntil(Sim, Campaign, [&] { return FirstFriendly(Sim, cinder::Kind::MotorPool) != 0; }, 240,
                        TEXT("The paid Crucible did not finish construction."), Failure)
                    || !Queue(Sim, Campaign, cinder::Kind::Bastion, 1, Failure)
                    || !WaitForPhase(Sim, Campaign, 5, 180, Failure)) return false;
            }
            else
            {
                const int32 NeedleSupply = cinder::definition(cinder::Kind::Lancer).supply * 6;
                if (Sim.capacity(0) - Sim.supply(0) < NeedleSupply
                    && (!BuildNear(Sim, Campaign, cinder::Kind::Processor,
                        Campaign.TargetPoint(ECinderCampaignTargetRole::BuildSite), Failure)
                        || !WaitUntil(Sim, Campaign,
                            [&] { return Sim.capacity(0) - Sim.supply(0) >= NeedleSupply; }, 180,
                            TEXT("The siege force Siphon did not provide enough crew capacity."), Failure))) return false;
                const int32 ExistingNeedles = static_cast<int32>(CompleteFriendly(Sim, cinder::Kind::Lancer).size());
                if (ExistingNeedles < 6)
                {
                    if (!Queue(Sim, Campaign, cinder::Kind::Lancer, 6 - ExistingNeedles, Failure)
                        || !WaitUntil(Sim, Campaign,
                            [&] { return static_cast<int32>(CompleteFriendly(Sim, cinder::Kind::Lancer).size()) >= 6; }, 300,
                            TEXT("The paid anti-armor force did not finish production."), Failure)) return false;
                }
                cinder::Vec2 EnemyPoint = Campaign.TargetPoint(ECinderCampaignTargetRole::EnemyObjective);
                if (const cinder::Entity* EnemyAnchor = Sim.find(Campaign.Authored().EnemyAnchor)) EnemyPoint = EnemyAnchor->pos;
                if (!Move(Sim, Campaign, CompleteAttackers(Sim),
                    EnemyPoint, cinder::CommandType::AttackMove, Failure)
                    || !WaitUntil(Sim, Campaign, [&] { return Campaign.IsTerminal(); }, 600,
                        TEXT("The paid mixed force did not destroy the siege Anchor."), Failure)) return false;
            }
        }
    }
    return Campaign.Outcome() == ECinderCampaignOutcome::Victory
        || Fail(Failure, TEXT("The authored mission did not reach victory."));
}
}
