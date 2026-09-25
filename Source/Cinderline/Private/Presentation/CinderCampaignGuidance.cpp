#include "Presentation/CinderCampaign.h"
#include "Presentation/CinderGuidance.h"

#include <algorithm>

namespace
{
FString Name(cinder::Kind Kind)
{
    return UTF8_TO_TCHAR(cinder::definition(Kind).name);
}

bool CompleteFriendly(const cinder::Entity& Entity, cinder::Kind Kind)
{
    return Entity.alive() && Entity.team == 0 && Entity.kind == Kind && Entity.progress >= 1.0f;
}

const cinder::Entity* FindComplete(const cinder::Simulation& Sim, cinder::Kind Kind)
{
    for (const cinder::Entity& Entity : Sim.entities())
        if (CompleteFriendly(Entity, Kind)) return &Entity;
    return nullptr;
}

const cinder::Entity* FindFoundation(const cinder::Simulation& Sim, cinder::Kind Kind)
{
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0 && Entity.kind == Kind && Entity.progress < 1.0f) return &Entity;
    return nullptr;
}

const cinder::Entity* FindFoundationNear(const cinder::Simulation& Sim, cinder::Kind Kind,
    cinder::Vec2 Point, float Radius)
{
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != 0 || Entity.kind != Kind || Entity.progress >= 1.0f) continue;
        const float DX = Entity.pos.x - Point.x;
        const float DY = Entity.pos.y - Point.y;
        if (DX * DX + DY * DY <= Radius * Radius) return &Entity;
    }
    return nullptr;
}

const cinder::Entity* FindSelected(const cinder::Simulation& Sim,
    const FCinderTutorialContext& Context, cinder::Kind Kind)
{
    for (cinder::Id Id : Context.Selection)
    {
        const cinder::Entity* Entity = Sim.find(Id);
        if (Entity && CompleteFriendly(*Entity, Kind)) return Entity;
    }
    return nullptr;
}

bool SelectionContains(const FCinderTutorialContext& Context, cinder::Id Id)
{
    return Id != 0 && std::find(Context.Selection.begin(), Context.Selection.end(), Id) != Context.Selection.end();
}

bool IsCombat(const cinder::Entity& Entity)
{
    return Entity.alive() && Entity.team == 0 && Entity.progress >= 1.0f
        && !cinder::definition(Entity.kind).building && Entity.kind != cinder::Kind::Worker
        && Entity.kind != cinder::Kind::Resource;
}

const cinder::Entity* FindSelectedCombat(const cinder::Simulation& Sim,
    const FCinderTutorialContext& Context)
{
    for (cinder::Id Id : Context.Selection)
    {
        const cinder::Entity* Entity = Sim.find(Id);
        if (Entity && IsCombat(*Entity)) return Entity;
    }
    return nullptr;
}

const cinder::Entity* FindCombat(const cinder::Simulation& Sim)
{
    for (const cinder::Entity& Entity : Sim.entities())
        if (IsCombat(Entity)) return &Entity;
    return nullptr;
}

const cinder::Entity* FindLiveResource(const cinder::Simulation& Sim,
    const std::vector<cinder::Id>& Preferred, cinder::Vec2 Near, bool bAllowFallback)
{
    for (cinder::Id Id : Preferred)
    {
        const cinder::Entity* Entity = Sim.find(Id);
        if (Entity && Entity->alive() && Entity->kind == cinder::Kind::Resource
            && Entity->resource > 0 && Sim.visible(0, Entity->pos)) return Entity;
    }
    if (!bAllowFallback) return nullptr;
    const cinder::Entity* Best = nullptr;
    float BestDistance = TNumericLimits<float>::Max();
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.kind != cinder::Kind::Resource || Entity.resource <= 0
            || !Sim.visible(0, Entity.pos)) continue;
        const float DX = Entity.pos.x - Near.x;
        const float DY = Entity.pos.y - Near.y;
        const float Distance = DX * DX + DY * DY;
        if (Distance < BestDistance) { Best = &Entity; BestDistance = Distance; }
    }
    return Best;
}

int32 CountQueued(const cinder::Simulation& Sim, cinder::Kind Kind)
{
    int32 Count = 0;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0)
            for (const cinder::QueueItem& Item : Entity.queue)
                if (!Item.research && Item.kind == Kind) ++Count;
    return Count;
}

bool ResearchQueued(const cinder::Simulation& Sim, int32 ResearchIndex)
{
    const cinder::Kind Kind = ResearchIndex == 1 ? cinder::Kind::Striker
        : ResearchIndex == 2 ? cinder::Kind::Lancer : cinder::Kind::Worker;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team == 0)
            for (const cinder::QueueItem& Item : Entity.queue)
                if (Item.research && Item.kind == Kind) return true;
    return false;
}

bool AnyDestinationMode(const FCinderTutorialContext& Context)
{
    return Context.bAttackMove || Context.bMoveCommand || Context.bDefendCommand
        || Context.bPatrolCommand || Context.bEscortCommand || Context.bBuildMode
        || Context.bProductionRally;
}

bool IsProofPhase(int32 Mission, int32 Phase)
{
    if (Mission == 5) return true;
    static constexpr int32 ProofPhases[] = {7, 5, 4, 3, 5};
    return Mission >= 0 && Mission < 5 && Phase >= ProofPhases[Mission];
}

bool IsDemonstratePhase(int32 Mission, int32 Phase)
{
    switch (Mission)
    {
    case 0: return Phase >= 0 && Phase <= 5; // Camera through first Ember production.
    case 1: return Phase == 0 || Phase == 2; // Worker rally and the first Siphon.
    case 2: return Phase == 1;               // Explicit Move; production is already known.
    case 3: return Phase >= 0 && Phase <= 2; // Army rally, Ward and Defend.
    case 4: return Phase == 2;               // First technology research interaction.
    default: return false;
    }
}
}

FCinderCampaignText FCinderCampaign::Text(const cinder::Simulation& Sim) const
{
    FCinderCampaignText Result;
    Result.Mission = MissionIndex();
    Result.Phase = Phase();
    Result.PhaseCount = PhaseCount();
    Result.bAssisted = State.AssistanceMask != 0;

    if (State.Outcome == ECinderCampaignOutcome::Inactive) return Result;
    if (State.Outcome == ECinderCampaignOutcome::Victory)
    {
        Result.Title = TEXT("Route restored");
        Result.Body = TEXT("Mission complete.");
        Result.Progress = TEXT("Victory");
        return Result;
    }
    if (State.Outcome == ECinderCampaignOutcome::Defeat || State.Outcome == ECinderCampaignOutcome::Draw)
    {
        Result.Title = State.Outcome == ECinderCampaignOutcome::Draw ? TEXT("Battle ended") : TEXT("Mission lost");
        Result.Body = State.FailureCode.IsEmpty() ? TEXT("Retry the last checkpoint or restart the mission.") : State.FailureCode;
        Result.Hint = TEXT("Your last settled objective is available from Retry checkpoint.");
        Result.Progress = TEXT("Defeat");
        return Result;
    }

    const FCinderCampaignPhaseDefinition& Definition = PhaseDefinition();
    const bool bHintRequested = IsCurrentPhaseAssisted();
    const bool bDemonstrate = IsDemonstratePhase(MissionIndex(), Phase());
    Result.Title = Definition.Title;
    Result.Body = Definition.Body;
    Result.Hint = (bDemonstrate || bHintRequested) ? Definition.Hint : FString();
    Result.Progress = Definition.Progress.IsEmpty()
        ? FString::Printf(TEXT("Objective %d of %d"), Phase() + 1, PhaseCount())
        : Definition.Progress;

    if (MissionIndex() == 3 && (Phase() == 3 || Phase() == 4))
    {
        const int32 Wave = Phase() - 2;
        const std::uint64_t PreparationEnd = State.PhaseStartTick + WavePreparationTicks;
        const bool bPreparing = State.Counters.OpponentTrainOrders == 0
            && State.Authored.ActiveWave.empty() && !State.bWaveProductionFinished
            && Sim.tick() < PreparationEnd;
        if (bPreparing)
        {
            const double Ticks = static_cast<double>(PreparationEnd - Sim.tick());
            const int32 Seconds = FMath::Max(1, FMath::CeilToInt(Ticks * cinder::Simulation::Step));
            Result.Progress = FString::Printf(TEXT("Wave %d / Prepare: %d seconds"), Wave, Seconds);
        }
        else
        {
            int32 Alive = 0;
            for (cinder::Id Id : State.Authored.ActiveWave)
            {
                const cinder::Entity* Entity = Sim.find(Id);
                if (Entity && Entity->alive()) ++Alive;
            }
            const cinder::Entity* Producer = Sim.find(State.Authored.RaidProducer);
            int32 Queued = 0;
            if (Producer && Producer->alive())
                Queued = static_cast<int32>(std::count_if(Producer->queue.begin(), Producer->queue.end(),
                    [](const cinder::QueueItem& Item) { return !Item.research; }));
            const int32 Budget = Phase() == 3 ? 3 : 4;
            const int32 NotYetOrdered = Producer && Producer->alive()
                ? FMath::Max(0, Budget - State.Counters.OpponentTrainOrders) : 0;
            Result.Progress = State.bWaveProductionFinished
                ? FString::Printf(TEXT("Wave %d / Attackers remaining: %d"), Wave, Alive)
                : FString::Printf(TEXT("Wave %d / Active: %d / Incoming: %d"),
                    Wave, Alive, Queued + NotYetOrdered);
        }
    }

    if ((bDemonstrate || bHintRequested) && Definition.Action == ECinderCampaignActionKind::Train)
    {
        const int32 Queued = CountQueued(Sim, Definition.Kind);
        const cinder::JobPlan Status = Sim.autoTrainStatus(0, Definition.Kind,
            FMath::Max(1, Definition.RequiredCount));
        if (Queued > 0)
            Result.Hint = FString::Printf(TEXT("%s training is underway. Keep its producer operational."), *Name(Definition.Kind));
        else if (!Status.accepted)
            Result.Hint = UTF8_TO_TCHAR(Status.message.c_str());
        else if (Phase() > 0)
            Result.Hint = FString::Printf(TEXT("Queue replacement %s through TRAIN if an earlier job was cancelled or lost."), *Name(Definition.Kind));
    }
    else if ((bDemonstrate || bHintRequested) && Definition.Action == ECinderCampaignActionKind::Build)
    {
        if (MissionIndex() == 4 && Phase() == 4 && FindComplete(Sim, cinder::Kind::MotorPool))
        {
            const int32 Queued = CountQueued(Sim, cinder::Kind::Bastion);
            const cinder::JobPlan Status = Sim.autoTrainStatus(0, cinder::Kind::Bastion, 1);
            if (Queued > 0) Result.Hint = TEXT("An Anvil is training at the Crucible.");
            else if (!Status.accepted) Result.Hint = UTF8_TO_TCHAR(Status.message.c_str());
            else Result.Hint = TEXT("The Crucible is ready. Train one Anvil to complete the frontline.");
        }
        else if (const cinder::Entity* Site = FindFoundation(Sim, Definition.Kind))
            Result.Hint = Sim.constructionWorker(Site->id)
                ? FString::Printf(TEXT("The %s is under construction."), *Name(Definition.Kind))
                : TEXT("The unfinished site has no builder. Select it and assign a Drudge.");
        else
        {
            const cinder::JobPlan Status = Sim.autoBuildStatus(0, Definition.Kind);
            if (!Status.accepted) Result.Hint = UTF8_TO_TCHAR(Status.message.c_str());
        }
    }
    else if ((bDemonstrate || bHintRequested) && Definition.Action == ECinderCampaignActionKind::Research)
    {
        const int32 Research = FMath::Max(0, Definition.ResearchIndex);
        if (ResearchQueued(Sim, Research)) Result.Hint = TEXT("Research is underway. Keep the Resonator operational.");
        else
        {
            const cinder::JobPlan Status = Sim.autoResearchStatus(0, Research);
            if (!Status.accepted) Result.Hint = UTF8_TO_TCHAR(Status.message.c_str());
        }
    }
    else if (bHintRequested && MissionIndex() == 1 && Phase() == 5)
    {
        const cinder::Entity* Worker = Sim.find(State.Authored.MiningWorker);
        const cinder::Entity* Siphon = Sim.find(State.Authored.RemoteSiphon);
        if (!Siphon || !CompleteFriendly(*Siphon, cinder::Kind::Processor))
            Result.Hint = TEXT("The remote Siphon was lost. Rebuild one beside the marked ore patch.");
        else if (!Worker || !CompleteFriendly(*Worker, cinder::Kind::Worker))
            Result.Hint = TEXT("The remote Drudge was lost. Train a replacement and assign it to the marked patch.");
        else if (!FindLiveResource(Sim, State.Authored.RemoteOre, State.RemotePatchPoint, false))
            Result.Hint = TEXT("That patch is empty. Move the Drudge to the newly marked live remote ore.");
        else
            Result.Hint = TEXT("Keep the remote Drudge gathering until it delivers ore to the nearby Siphon.");
    }
    return Result;
}

FCinderTutorialGuide FCinderCampaign::Guide(const cinder::Simulation& Sim,
    const FCinderTutorialContext& Context, bool bTouch) const
{
    FCinderTutorialGuide Result;
    if (!IsRunning() || MissionIndex() < 0 || Phase() < 0 || Phase() >= PhaseCount()) return Result;

    const FCinderCampaignPhaseDefinition& Definition = PhaseDefinition();
    const FString Explanation = Definition.Hint.IsEmpty() ? Definition.Body : Definition.Hint;
    const bool bProof = IsProofPhase(MissionIndex(), Phase());
    const bool bDemonstrate = IsDemonstratePhase(MissionIndex(), Phase());
    const bool bHintRequested = IsCurrentPhaseAssisted();
    const bool bRemoteDeliveryRecovery = MissionIndex() == 1 && Phase() == 5 && bHintRequested;

    auto Button = [&](const FString& Instruction, const FString& Why,
        const TCHAR* Action, int32 Argument, int32 Index, int32 Count, cinder::Id Entity = 0)
    {
        Result.Instruction = Instruction;
        Result.Explanation = Why;
        Result.Target = ECinderTutorialGuideTarget::Button;
        Result.ButtonAction = Action;
        Result.ButtonArgument = Argument;
        Result.ButtonEntity = Entity;
        Result.ActionIndex = Index;
        Result.ActionCount = Count;
    };
    auto EntityTarget = [&](const FString& Instruction, const FString& Why,
        cinder::Id Entity, int32 Index, int32 Count)
    {
        Result.Instruction = Instruction;
        Result.Explanation = Why;
        Result.Target = ECinderTutorialGuideTarget::Entity;
        Result.Entity = Entity;
        Result.ActionIndex = Index;
        Result.ActionCount = Count;
    };
    auto Ground = [&](const FString& Instruction, const FString& Why,
        cinder::Vec2 Point, int32 Index, int32 Count)
    {
        Result.Instruction = Instruction;
        Result.Explanation = Why;
        Result.Target = ECinderTutorialGuideTarget::Ground;
        Result.Point = Point;
        Result.ActionIndex = Index;
        Result.ActionCount = Count;
    };
    auto Wait = [&](const FString& Instruction, const FString& Why, int32 Index, int32 Count)
    {
        Result.Instruction = Instruction;
        Result.Explanation = Why;
        Result.Target = ECinderTutorialGuideTarget::Wait;
        Result.ActionIndex = Index;
        Result.ActionCount = Count;
        Result.bWaiting = true;
    };
    auto Blocked = [&](const std::string& Message, const FString& Why, int32 Index, int32 Count)
    {
        Result.Instruction = UTF8_TO_TCHAR(Message.c_str());
        Result.Explanation = Why;
        Result.Target = ECinderTutorialGuideTarget::None;
        Result.ActionIndex = Index;
        Result.ActionCount = Count;
    };

    // Proof asks for an outcome. It deliberately has no automatic arrow or camera focus.
    if (bProof && !bRemoteDeliveryRecovery)
    {
        Result.Instruction = Definition.Body.IsEmpty() ? Definition.Title : Definition.Body;
        if (bHintRequested) Result.Explanation = Explanation;
        Result.Target = ECinderTutorialGuideTarget::None;
        Result.ActionIndex = Result.ActionCount = 1;
        if (bHintRequested && Definition.Action == ECinderCampaignActionKind::Destroy && MissionIndex() != 5)
        {
            cinder::Id LiveTarget = 0;
            for (const std::vector<cinder::Id>* Targets :
                {&State.Authored.PracticePatrol, &State.Authored.ForwardThreats})
            {
                for (cinder::Id Id : *Targets)
                {
                    const cinder::Entity* Entity = Sim.find(Id);
                    if (!Entity || !Entity->alive()) continue;
                    LiveTarget = Id;
                    break;
                }
                if (LiveTarget) break;
            }
            if (!LiveTarget)
            {
                const cinder::Entity* Anchor = Sim.find(State.Authored.EnemyAnchor);
                if (Anchor && Anchor->alive()) LiveTarget = Anchor->id;
            }
            const cinder::Entity* Entity = Sim.find(LiveTarget);
            if (Entity && Sim.visible(0, Entity->pos))
                EntityTarget(TEXT("Show the remaining objective."), Explanation, Entity->id, 1, 1);
            else
                Ground(TEXT("Show the last known objective area."), Explanation,
                    TargetPoint(ECinderCampaignTargetRole::EnemyObjective), 1, 1);
        }
        return Result;
    }


    // Coaching starts as an outcome-only objective. HINT marks the phase Assisted
    // and reveals the next single action; demonstrations reveal it immediately.
    if (!bDemonstrate && !bHintRequested)
    {
        Result.Instruction = Definition.Body.IsEmpty() ? Definition.Title : Definition.Body;
        Result.Target = ECinderTutorialGuideTarget::None;
        Result.ActionIndex = Result.ActionCount = 1;
        return Result;
    }

    const bool bAnyMode = AnyDestinationMode(Context);
    auto ClearPanelOrMode = [&](const FString& Why, int32 Index, int32 Count) -> bool
    {
        if (Context.Catalog != 0)
        {
            Button(TEXT("Close the open panel."), Why, TEXT("closesheet"), 0, Index, Count);
            return true;
        }
        if (bAnyMode)
        {
            Button(TEXT("Clear the current command mode."), Why, TEXT("tutorialclearmode"), 0, Index, Count);
            return true;
        }
        return false;
    };

    auto GuideTrain = [&](cinder::Kind Kind, int32 Quantity, const FString& Why)
    {
        Quantity = FMath::Clamp(Quantity, 1, cinder::Simulation::MaxQueue);
        const int32 Queued = CountQueued(Sim, Kind);
        if (Queued > 0)
        {
            if (Context.Catalog != 0) Button(TEXT("Close TRAIN."), Why, TEXT("closesheet"), 0, 5, 6);
            else Wait(FString::Printf(TEXT("Wait for %s training to finish."), *Name(Kind)), Why, 6, 6);
            return;
        }
        const cinder::JobPlan Status = Sim.autoTrainStatus(0, Kind, Quantity);
        if (!Status.accepted) { Blocked(Status.message, Why, 1, 6); return; }
        if (Context.Catalog != 8 || Context.PinnedProducer != 0)
        {
            Button(TEXT("Open TRAIN."), Why, TEXT("globalcatalog"), 8, 1, 6);
            return;
        }
        if (!Context.bTrainKindChosen || Context.TrainKind != Kind)
        {
            Button(FString::Printf(TEXT("Choose %s."), *Name(Kind)), Why,
                TEXT("trainkind"), static_cast<int32>(Kind), 2, 6);
            return;
        }
        if (!Context.bTrainQuantityChosen || Context.TrainQuantity != Quantity)
        {
            const int32 Preset = Quantity <= 2 ? 1 : Quantity <= 5 ? 3 : 6;
            if (!Context.bTrainQuantityChosen)
                Button(FString::Printf(TEXT("Choose quantity %d."), Preset), Why, TEXT("trainqty"), Preset, 3, 6);
            else Button(Context.TrainQuantity < Quantity ? TEXT("Tap + once.") : TEXT("Tap - once."), Why,
                TEXT("trainqtydelta"), Context.TrainQuantity < Quantity ? 1 : -1, 4, 6);
            return;
        }
        Button(FString::Printf(TEXT("Queue %d %s%s."), Quantity, *Name(Kind), Quantity == 1 ? TEXT("") : TEXT("s")),
            Why, TEXT("globaltrain"), 0, 5, 6);
    };

    auto GuideBuild = [&](cinder::Kind Kind, cinder::Vec2 PreferredPoint, const FString& Why,
        bool bRequireNear = false)
    {
        const cinder::Entity* Foundation = bRequireNear
            ? FindFoundationNear(Sim, Kind, PreferredPoint, 650.0f) : FindFoundation(Sim, Kind);
        if (const cinder::Entity* Site = Foundation)
        {
            GuideRecoverySite = Site->id;
            if (!Sim.constructionWorker(Site->id))
            {
                if (!SelectionContains(Context, Site->id))
                {
                    if (ClearPanelOrMode(Why, 4, 7)) return;
                    EntityTarget(bTouch ? TEXT("Tap the highlighted unfinished site.")
                        : TEXT("Click the highlighted unfinished site."), Why, Site->id, 4, 7);
                    return;
                }
                if (bAnyMode)
                {
                    Button(TEXT("Clear the current command mode."), Why, TEXT("tutorialclearmode"), 0, 5, 7);
                    return;
                }
                if (Context.bCompact && Context.Catalog != 1)
                {
                    Button(TEXT("Open SITE."), Why, TEXT("sheet"), 1, 5, 7);
                    return;
                }
                Button(TEXT("Choose ASSIGN DRUDGE."), Why, TEXT("resumeconstruction"), 0, 6, 7);
                return;
            }
            Wait(FString::Printf(TEXT("Wait for the %s to finish."), *Name(Kind)), Why, 4, 4);
            return;
        }
        GuideRecoverySite = 0;
        const cinder::JobPlan Ready = Sim.autoBuildStatus(0, Kind);
        if (!Ready.accepted) { Blocked(Ready.message, Why, 1, 4); return; }
        if (Context.bBuildMode && Context.BuildingKind == Kind)
        {
            const bool bValidationDue = !bGuideSiteCached || GuideSiteKind != Kind
                || Sim.tick() < GuideSiteValidatedTick || Sim.tick() - GuideSiteValidatedTick >= 10;
            if (bValidationDue)
            {
                bGuideSiteCached = false;
                GuideSiteKind = Kind;
                const float Step = 75.0f;
                for (int32 Ring = 0; Ring <= 10 && !bGuideSiteCached; ++Ring)
                {
                    for (int32 Offset = -Ring; Offset <= Ring && !bGuideSiteCached; ++Offset)
                    {
                        const cinder::Vec2 Candidates[4] = {
                            {PreferredPoint.x + Offset * Step, PreferredPoint.y - Ring * Step},
                            {PreferredPoint.x + Offset * Step, PreferredPoint.y + Ring * Step},
                            {PreferredPoint.x - Ring * Step, PreferredPoint.y + Offset * Step},
                            {PreferredPoint.x + Ring * Step, PreferredPoint.y + Offset * Step}
                        };
                        const int32 CandidateCount = Ring == 0 ? 1 : 4;
                        for (int32 CandidateIndex = 0; CandidateIndex < CandidateCount; ++CandidateIndex)
                        {
                            const cinder::Vec2 Candidate = Candidates[CandidateIndex];
                            if (!Sim.canPlace(0, Kind, Candidate)) continue;
                            if (!Sim.autoBuildStatus(0, Kind, &Candidate).accepted) continue;
                            GuideSite = Candidate;
                            bGuideSiteCached = true;
                            break;
                        }
                    }
                }
                GuideSiteValidatedTick = Sim.tick();
            }
            if (!bGuideSiteCached)
            {
                Result.Instruction = TEXT("Reveal clear reachable ground near the marker.");
                Result.Explanation = Why;
                Result.Target = ECinderTutorialGuideTarget::Camera;
                Result.Point = PreferredPoint;
                Result.ActionIndex = 3;
                Result.ActionCount = 4;
                return;
            }
            Ground(bTouch ? TEXT("Tap the highlighted build site.") : TEXT("Click the highlighted build site."),
                Why, GuideSite, 3, 4);
            return;
        }
        if (Context.Catalog != 7)
        {
            Button(TEXT("Open BUILD."), Why, TEXT("globalcatalog"), 7, 1, 4);
            return;
        }
        Button(FString::Printf(TEXT("Choose %s."), *Name(Kind)), Why,
            TEXT("globalbuild"), static_cast<int32>(Kind), 2, 4);
    };

    if (bRemoteDeliveryRecovery)
    {
        const cinder::Entity* Siphon = Sim.find(State.Authored.RemoteSiphon);
        const float SiphonDX = Siphon ? Siphon->pos.x - State.RemotePatchPoint.x : 0.0f;
        const float SiphonDY = Siphon ? Siphon->pos.y - State.RemotePatchPoint.y : 0.0f;
        const bool bSiphonReady = Siphon && CompleteFriendly(*Siphon, cinder::Kind::Processor)
            && SiphonDX * SiphonDX + SiphonDY * SiphonDY <= 520.0f * 520.0f;
        if (!bSiphonReady)
        {
            GuideBuild(cinder::Kind::Processor, TargetPoint(ECinderCampaignTargetRole::BuildSite),
                TEXT("The current ore patch needs an operational Siphon nearby for short deliveries."), true);
            return Result;
        }

        const cinder::Entity* Worker = FindSelected(Sim, Context, cinder::Kind::Worker);
        if (!Worker)
        {
            if (ClearPanelOrMode(TEXT("Select a Drudge for the remote ore route."), 1, 3)) return Result;
            const cinder::Entity* Available = FindComplete(Sim, cinder::Kind::Worker);
            if (Available)
                EntityTarget(bTouch ? TEXT("Tap a highlighted Drudge.") : TEXT("Click a highlighted Drudge."),
                    TEXT("This Drudge will carry ore from the remote patch to its Siphon."), Available->id, 1, 3);
            else
                GuideTrain(cinder::Kind::Worker, 1, TEXT("Train a replacement Drudge for the remote route."));
            return Result;
        }

        const cinder::Entity* Ore = FindLiveResource(Sim, State.Authored.RemoteOre,
            State.RemotePatchPoint, false);
        if (!Ore)
        {
            if (Context.bAttackMove || Context.bDefendCommand || Context.bPatrolCommand
                || Context.bEscortCommand || Context.bBuildMode || Context.bProductionRally)
                Button(TEXT("Clear the current command mode."), Explanation,
                    TEXT("tutorialclearmode"), 0, 2, 3);
            else if (!Context.bMoveCommand)
                Button(TEXT("Choose MOVE."), TEXT("Move the Drudge close enough to reveal the replacement patch."),
                    TEXT("move"), 0, 2, 3);
            else Ground(bTouch ? TEXT("Tap the highlighted remote patch.")
                : TEXT("Click the highlighted remote patch."),
                TEXT("Reveal the replacement ore before assigning Gather."), State.RemotePatchPoint, 3, 3);
            return Result;
        }

        if (Worker->order != cinder::Order::Gather || Worker->resourceTarget != Ore->id)
        {
            if (Context.bAttackMove || Context.bMoveCommand || Context.bDefendCommand
                || Context.bPatrolCommand || Context.bEscortCommand || Context.bBuildMode
                || Context.bProductionRally)
                Button(TEXT("Clear the current command mode."), Explanation,
                    TEXT("tutorialclearmode"), 0, 2, 2);
            else EntityTarget(bTouch ? TEXT("Tap the highlighted remote ore.")
                : TEXT("Secondary-click the highlighted remote ore."),
                TEXT("The selected Drudge will gather here and deliver to the nearby Siphon."), Ore->id, 2, 2);
            return Result;
        }

        Result.Instruction = TEXT("Keep the remote Drudge gathering until it completes a delivery.");
        Result.Explanation = TEXT("The objective completes when ore from this live patch reaches its nearby Siphon.");
        Result.Target = ECinderTutorialGuideTarget::None;
        Result.ActionIndex = Result.ActionCount = 1;
        return Result;
    }

    switch (Definition.Action)
    {
    case ECinderCampaignActionKind::Camera:
        Result.Instruction = bTouch ? TEXT("Drag the battlefield once.") : TEXT("Pan or zoom the battlefield once.");
        Result.Explanation = Explanation;
        Result.Target = ECinderTutorialGuideTarget::Camera;
        Result.Point = State.BasePoint;
        Result.ActionIndex = Result.ActionCount = 1;
        break;
    case ECinderCampaignActionKind::SelectWorker:
    {
        if (ClearPanelOrMode(Explanation, 1, 1)) break;
        const cinder::Entity* Worker = Sim.find(State.Authored.MiningWorker);
        if (!Worker || !CompleteFriendly(*Worker, cinder::Kind::Worker)) Worker = FindComplete(Sim, cinder::Kind::Worker);
        if (Worker) EntityTarget(bTouch ? TEXT("Tap the highlighted Drudge.") : TEXT("Click the highlighted Drudge."),
            Explanation, Worker->id, 1, 1);
        else Result.Instruction = TEXT("Train a replacement Drudge from the Anchor.");
        break;
    }
    case ECinderCampaignActionKind::Gather:
    {
        const bool bRemote = Definition.Target == ECinderCampaignTargetRole::RemoteOre;
        const std::vector<cinder::Id>& Preferred = bRemote ? State.Authored.RemoteOre : State.Authored.HomeOre;
        const cinder::Vec2 Near = bRemote ? State.RemotePatchPoint : State.BasePoint;
        const cinder::Entity* Worker = FindSelected(Sim, Context, cinder::Kind::Worker);
        if (!Worker)
        {
            if (ClearPanelOrMode(Explanation, 1, 2)) break;
            const cinder::Entity* Available = FindComplete(Sim, cinder::Kind::Worker);
            if (Available) EntityTarget(bTouch ? TEXT("Tap a highlighted Drudge.") : TEXT("Click a highlighted Drudge."),
                Explanation, Available->id, 1, 2);
            else Result.Instruction = TEXT("Train a replacement Drudge first.");
            break;
        }
        const cinder::Entity* Ore = FindLiveResource(Sim, Preferred, Near, !bRemote);
        if (Ore) EntityTarget(bTouch ? TEXT("Tap the highlighted ore deposit.")
            : TEXT("Secondary-click the highlighted ore deposit."), Explanation, Ore->id, 2, 2);
        else if (bRemote)
        {
            if (Context.bAttackMove || Context.bDefendCommand || Context.bPatrolCommand
                || Context.bEscortCommand || Context.bBuildMode || Context.bProductionRally)
                Button(TEXT("Clear the current command mode."), Explanation,
                    TEXT("tutorialclearmode"), 0, 2, 3);
            else if (!Context.bMoveCommand)
                Button(TEXT("Choose MOVE."), TEXT("Move the Drudge close enough to reveal the remote ore."),
                    TEXT("move"), 0, 2, 3);
            else Ground(bTouch ? TEXT("Tap the highlighted remote patch.")
                : TEXT("Click the highlighted remote patch."),
                TEXT("The Drudge will reveal the ore before you assign Gather."), State.RemotePatchPoint, 3, 3);
        }
        else Result.Instruction = TEXT("Reveal a reachable ore deposit near the Anchor.");
        break;
    }
    case ECinderCampaignActionKind::Train:
        if (Definition.Kind == cinder::Kind::Scout && !FindComplete(Sim, cinder::Kind::Foundry))
            GuideBuild(cinder::Kind::Foundry, State.BasePoint,
                TEXT("A Skim is trained at a Kiln, so build the Kiln first."));
        else
            GuideTrain(Definition.Kind, FMath::Max(1, Definition.RequiredCount), Explanation);
        break;
    case ECinderCampaignActionKind::Build:
        if ((Definition.Kind == cinder::Kind::Turret || Definition.Kind == cinder::Kind::Laboratory)
            && !FindComplete(Sim, cinder::Kind::Foundry))
            GuideBuild(cinder::Kind::Foundry, State.BasePoint,
                FString::Printf(TEXT("A %s requires an operational Kiln."), *Name(Definition.Kind)));
        else if (MissionIndex() == 4 && Phase() == 4 && FindComplete(Sim, cinder::Kind::MotorPool))
            GuideTrain(cinder::Kind::Bastion, 1,
                TEXT("An Anvil is an armored frontline unit trained at the Crucible."));
        else
            GuideBuild(Definition.Kind, TargetPoint(Definition.Target), Explanation);
        break;
    case ECinderCampaignActionKind::Rally:
    {
        if (Definition.Target == ECinderCampaignTargetRole::RemoteOre
            || Definition.Target == ECinderCampaignTargetRole::HomeOre)
        {
            const cinder::Entity* Anchor = Sim.find(State.Authored.PlayerAnchor);
            if (!Anchor || !CompleteFriendly(*Anchor, cinder::Kind::Headquarters))
            {
                Result.Instruction = TEXT("Retry the checkpoint: an Anchor is required to set a worker rally.");
                break;
            }
            if (!SelectionContains(Context, Anchor->id))
            {
                if (ClearPanelOrMode(Explanation, 1, 3)) break;
                EntityTarget(bTouch ? TEXT("Tap the highlighted Anchor.") : TEXT("Click the highlighted Anchor."),
                    Explanation, Anchor->id, 1, 3);
                break;
            }
            if (!Context.bProductionRally || Context.PinnedProducer != Anchor->id)
            {
                Button(TEXT("Choose RALLY."), Explanation, TEXT("productionrally"), 0, 2, 3, Anchor->id);
                break;
            }
            const std::vector<cinder::Id>& Preferred = Definition.Target == ECinderCampaignTargetRole::RemoteOre
                ? State.Authored.RemoteOre : State.Authored.HomeOre;
            const cinder::Entity* Ore = FindLiveResource(Sim, Preferred, TargetPoint(Definition.Target),
                Definition.Target == ECinderCampaignTargetRole::HomeOre);
            if (Ore) EntityTarget(bTouch ? TEXT("Tap the highlighted ore deposit.")
                : TEXT("Click the highlighted ore deposit."), Explanation, Ore->id, 3, 3);
            else Result.Instruction = TEXT("Reveal a live reachable ore deposit, then set the rally there.");
            break;
        }
        if (Context.Catalog == 0)
            Button(TEXT("Open ARMY."), Explanation, TEXT("army"), 0, 1, 4);
        else if (Context.Catalog != 5)
            Button(TEXT("Close the open panel."), Explanation, TEXT("closesheet"), 0, 1, 4);
        else if (Context.ArmyTab != 3)
            Button(TEXT("Open RALLY."), Explanation, TEXT("armytab"), 3, 2, 4);
        else if (!Context.bProductionRally)
            Button(TEXT("Choose SET RALLY."), Explanation, TEXT("productionrally"), 0, 3, 4);
        else Ground(bTouch ? TEXT("Tap the highlighted defense point.")
            : TEXT("Click the highlighted defense point."), Explanation, TargetPoint(Definition.Target), 4, 4);
        break;
    }
    case ECinderCampaignActionKind::Move:
    {
        const cinder::Entity* Scout = FindSelected(Sim, Context, cinder::Kind::Scout);
        if (!Scout)
        {
            if (ClearPanelOrMode(Explanation, 1, 3)) break;
            const cinder::Entity* Available = FindComplete(Sim, cinder::Kind::Scout);
            if (Available) EntityTarget(bTouch ? TEXT("Tap the highlighted Skim.") : TEXT("Click the highlighted Skim."),
                Explanation, Available->id, 1, 3);
            else GuideTrain(cinder::Kind::Scout, 1, TEXT("A Skim is replaceable through TRAIN."));
        }
        else if (!Context.bMoveCommand)
        {
            if (Context.bAttackMove || Context.bDefendCommand || Context.bPatrolCommand || Context.bEscortCommand)
                Button(TEXT("Clear the current command mode."), Explanation, TEXT("tutorialclearmode"), 0, 2, 3);
            else Button(TEXT("Choose MOVE."), Explanation, TEXT("move"), 0, 2, 3);
        }
        else Ground(bTouch ? TEXT("Tap the highlighted scout zone.") : TEXT("Click the highlighted scout zone."),
            Explanation, TargetPoint(Definition.Target), 3, 3);
        break;
    }
    case ECinderCampaignActionKind::AttackMove:
    case ECinderCampaignActionKind::Defend:
    {
        const bool bDefend = Definition.Action == ECinderCampaignActionKind::Defend;
        if (!FindSelectedCombat(Sim, Context))
        {
            if (ClearPanelOrMode(Explanation, 1, 3)) break;
            const cinder::Entity* Unit = FindCombat(Sim);
            if (Unit) EntityTarget(bTouch ? TEXT("Tap the highlighted combat unit.")
                : TEXT("Click the highlighted combat unit."), Explanation, Unit->id, 1, 3);
            else if (FindComplete(Sim, cinder::Kind::Foundry)) GuideTrain(cinder::Kind::Striker, 1,
                TEXT("Train a replacement Ember before continuing."));
            else GuideBuild(cinder::Kind::Foundry, State.BasePoint, TEXT("Rebuild a Kiln before replacing the army."));
        }
        else if ((bDefend && !Context.bDefendCommand) || (!bDefend && !Context.bAttackMove))
        {
            if (Context.bMoveCommand || (bDefend ? Context.bAttackMove : Context.bDefendCommand)
                || Context.bPatrolCommand || Context.bEscortCommand)
                Button(TEXT("Clear the current command mode."), Explanation, TEXT("tutorialclearmode"), 0, 2, 3);
            else Button(bDefend ? TEXT("Choose DEFEND.") : TEXT("Choose ATTACK."), Explanation,
                bDefend ? TEXT("defend") : TEXT("attack"), 0, 2, 3);
        }
        else Ground(bTouch ? TEXT("Tap the highlighted destination.") : TEXT("Click the highlighted destination."),
            Explanation, TargetPoint(Definition.Target), 3, 3);
        break;
    }
    case ECinderCampaignActionKind::Research:
    {
        const int32 Research = FMath::Max(0, Definition.ResearchIndex);
        if (ResearchQueued(Sim, Research))
        {
            if (Context.Catalog != 0) Button(TEXT("Close RESEARCH."), Explanation, TEXT("closesheet"), 0, 3, 4);
            else Wait(TEXT("Wait for research to finish."), Explanation, 4, 4);
            break;
        }
        const cinder::JobPlan Status = Sim.autoResearchStatus(0, Research);
        if (!Status.accepted) { Blocked(Status.message, Explanation, 1, 4); break; }
        if (Context.Catalog != 9 || Context.PinnedProducer != 0)
            Button(TEXT("Open RESEARCH."), Explanation, TEXT("globalcatalog"), 9, 1, 4);
        else Button(TEXT("Choose the highlighted research."), Explanation,
            TEXT("globalresearch"), Research, 2, 4);
        break;
    }
    case ECinderCampaignActionKind::Explore:
    {
        cinder::Vec2 Point = TargetPoint(Definition.Target);
        if (Definition.Target == ECinderCampaignTargetRole::ScoutZoneA && Sim.explored(0, State.ScoutZoneA))
            Point = State.ScoutZoneB;
        Ground(bTouch ? TEXT("Tap SHOW to view the next area, then scout it.")
            : TEXT("Use SHOW to view the next area, then scout it."), Explanation, Point, 1, 1);
        break;
    }
    case ECinderCampaignActionKind::Destroy:
    case ECinderCampaignActionKind::Wait:
    case ECinderCampaignActionKind::None:
        Result.Instruction = Definition.Body.IsEmpty() ? Definition.Title : Definition.Body;
        Result.Explanation = Explanation;
        Result.Target = ECinderTutorialGuideTarget::None;
        Result.ActionIndex = Result.ActionCount = 1;
        break;
    }
    return Result;
}
