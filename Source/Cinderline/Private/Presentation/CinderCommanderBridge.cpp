#include "Presentation/CinderCommanderBridge.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderOnlineSubsystem.h"
#include "Presentation/CinderPlayerController.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Sim/MapDefinition.h"
#include "Sim/Network.h"
#include <algorithm>

namespace
{
constexpr double CaptureLifetime = 120.0;
constexpr int32 MaxCaptures = 24;
constexpr int32 MaxOperations = 1024;
constexpr int32 MaxTargets = 64;

TSharedPtr<FJsonObject> Receipt(const FString& OperationId, const TCHAR* Status,
    const FString& Message, uint32 Sequence = 0)
{
    auto Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("operationId"), OperationId);
    Object->SetStringField(TEXT("status"), Status);
    Object->SetStringField(TEXT("message"), Message);
    if (Sequence) Object->SetNumberField(TEXT("sequence"), Sequence);
    return Object;
}

bool ReadId(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Out)
{
    return Object && Object->TryGetStringField(Key, Out) && !Out.IsEmpty() && Out.Len() <= 128;
}

bool Owned(const cinder::Entity* Entity)
{
    return Entity && Entity->alive() && Entity->team == 0 && Entity->kind != cinder::Kind::Resource;
}

bool KnownPoint(const cinder::Simulation& Simulation, cinder::Vec2 Point)
{
    return FMath::IsFinite(Point.x) && FMath::IsFinite(Point.y)
        && Point.x >= 0 && Point.y >= 0 && Point.x <= Simulation.worldSize()
        && Point.y <= Simulation.worldSize() && Simulation.explored(0, Point);
}
}

void FCinderCommanderBridge::Synchronize(ACinderPlayerController* Controller)
{
    ACinderBattlefield* Current = IsValid(Controller) ? Controller->Battlefield() : nullptr;
    const uint64 Serial = Current ? Current->MatchGeneration() : 0;
    const uint64 Tick = Current ? Current->Sim().tick() : 0;
    if (Generation.IsEmpty() || Current != Battlefield.Get() || Controller != PlayerController.Get()
        || Serial != MatchSerial || Tick < LastTick)
    {
        Generation = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Captures.Reset();
        PublicHandles.Reset();
        NextPublicHandle = 1;
        for (auto& Pair : Operations)
        {
            auto& Operation = Pair.Value;
            if (!Operation.bPending) continue;
            Operation.Receipt = Receipt(Pair.Key, TEXT("uncertain"),
                TEXT("The match changed before this order was acknowledged. It was not replayed."), Operation.Sequence);
            Operation.bPending = false;
            Operation.bDeliver = true;
        }
    }
    Battlefield = Current;
    PlayerController = Controller;
    MatchSerial = Serial;
    LastTick = Tick;
}

void FCinderCommanderBridge::PruneCaptures(double Now)
{
    for (auto It = Captures.CreateIterator(); It; ++It)
        if (Now - It.Value().CreatedAt > CaptureLifetime) It.RemoveCurrent();
    while (Captures.Num() >= MaxCaptures)
    {
        FString Oldest;
        double OldestAt = TNumericLimits<double>::Max();
        for (const auto& Pair : Captures)
            if (Pair.Value.CreatedAt < OldestAt) { OldestAt = Pair.Value.CreatedAt; Oldest = Pair.Key; }
        Captures.Remove(Oldest);
    }
}

TSharedPtr<FJsonObject> FCinderCommanderBridge::Capture(ACinderPlayerController* Controller)
{
    check(IsInGameThread());
    Synchronize(Controller);
    const double Now = FPlatformTime::Seconds();
    PruneCaptures(Now);
    FCapture Context;
    Context.CreatedAt = Now;
    Context.Generation = Generation;
    const FString ContextId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto Observation = MakeShared<FJsonObject>();
    Observation->SetStringField(TEXT("contextId"), ContextId);
    Observation->SetStringField(TEXT("generation"), Generation);
    Observation->SetStringField(TEXT("revision"), LexToString(LastTick));
    Observation->SetNumberField(TEXT("capturedAt"), static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000.0);
    Observation->SetStringField(TEXT("selected_group"), TEXT("selected"));
    TArray<TSharedPtr<FJsonValue>> Groups, Locations, Targets;
    auto Summary = MakeShared<FJsonObject>();
    auto* Battle = Battlefield.Get();
    const bool bActive = Controller && Battle && Controller->CommanderGameplayActive();
    Summary->SetBoolField(TEXT("gameplayActive"), bActive);
    if (bActive)
    {
        const auto& Simulation = Battle->Sim();
        // Offline Simulation is omniscient. Only the established viewer projection enters
        // this serializer; the allowlist below also excludes orders, queues and hidden state.
        const auto View = cinder::net::snapshotFor(Simulation, 0);
        TMap<cinder::Id, uint32> Handles;
        for (const auto& Entity : View.entities)
        {
            if (!PublicHandles.Contains(Entity.id) && PublicHandles.Num() < 8192)
                PublicHandles.Add(Entity.id, NextPublicHandle++);
            if (Owned(&Entity) && PublicHandles.Contains(Entity.id)) Handles.Add(Entity.id, PublicHandles[Entity.id]);
        }
        auto AddGroup = [&](const FString& Id, const FString& Label, const std::vector<cinder::Id>& Input)
        {
            std::vector<cinder::Id> Units;
            for (const cinder::Id Unit : Input)
            {
                // A missing member must not silently change the meaning of a captured group.
                if (!Handles.Contains(Unit)) return;
                if (std::find(Units.begin(), Units.end(), Unit) == Units.end()) Units.push_back(Unit);
            }
            // Never silently turn "all" or a captured selection into a truncated subset.
            if (Units.empty() || Units.size() > cinder::net::MaxCommandUnits) return;
            auto Group = MakeShared<FJsonObject>();
            Group->SetStringField(TEXT("id"), Id);
            Group->SetStringField(TEXT("label"), Label);
            Group->SetBoolField(TEXT("commandable"), true);
            TArray<TSharedPtr<FJsonValue>> UnitHandles;
            for (cinder::Id Unit : Units) UnitHandles.Add(MakeShared<FJsonValueNumber>(Handles[Unit]));
            Group->SetArrayField(TEXT("units"), UnitHandles);
            Group->SetNumberField(TEXT("count"), Units.size());
            Groups.Add(MakeShared<FJsonValueObject>(Group));
            Context.Groups.Add(Id, std::move(Units));
        };
        AddGroup(TEXT("selected"), TEXT("The units selected when you started speaking"), Controller->Selection());
        for (int32 Index = 0; Index < ACinderPlayerController::SquadCount; ++Index)
            AddGroup(FString::Printf(TEXT("squad_%d"), Index + 1), FString::Printf(TEXT("Squad %c"), TCHAR('A' + Index)), Controller->Squad(Index));
        std::vector<cinder::Id> Army;
        for (const auto& Definition : cinder::definitions())
        {
            if (Definition.kind == cinder::Kind::Resource) continue;
            std::vector<cinder::Id> Units;
            for (const auto& Entity : View.entities)
                if (Owned(&Entity) && Entity.kind == Definition.kind)
                {
                    Units.push_back(Entity.id);
                    if (!Definition.building && Entity.kind != cinder::Kind::Worker) Army.push_back(Entity.id);
                }
            AddGroup(FString::Printf(TEXT("kind_%d"), static_cast<int32>(Definition.kind)),
                FString::Printf(TEXT("All owned %s units"), UTF8_TO_TCHAR(Definition.name)), Units);
        }
        AddGroup(TEXT("army"), TEXT("All owned combat units"), Army);
        auto AddLocation = [&](const FString& Id, const FString& Label, cinder::Vec2 Point)
        {
            if (!KnownPoint(Simulation, Point) || Locations.Num() >= 64) return;
            auto Location = MakeShared<FJsonObject>();
            Location->SetStringField(TEXT("id"), Id);
            Location->SetStringField(TEXT("label"), Label);
            Location->SetBoolField(TEXT("commandable"), true);
            Location->SetBoolField(TEXT("explored"), true);
            Locations.Add(MakeShared<FJsonValueObject>(Location));
            Context.Locations.Add(Id, Point);
        };
        cinder::Vec2 Point{};
        if (Controller->CommanderCameraPoint(Point)) AddLocation(TEXT("camera"), TEXT("Current camera center"), Point);
        if (Controller->CommanderPointedLocation(Point)) AddLocation(TEXT("pointed"), TEXT("Last ground location you pointed at"), Point);
        bool bHome = false;
        int32 OwnedCount = 0, EnemyCount = 0;
        for (const auto& Entity : View.entities)
        {
            if (Owned(&Entity))
            {
                ++OwnedCount;
                if (Entity.kind == cinder::Kind::Headquarters)
                {
                    if (!bHome) { AddLocation(TEXT("home"), TEXT("Your main Anchor base"), Entity.pos); bHome = true; }
                    if (Handles.Contains(Entity.id))
                        AddLocation(FString::Printf(TEXT("base_%u"), Handles[Entity.id]),
                            FString::Printf(TEXT("Your Anchor base %u"), Handles[Entity.id]), Entity.pos);
                }
            }
            else if (Entity.team > 0) ++EnemyCount;
        }
        // Give visible opponents room in the bounded list even when many friendly units exist.
        for (int32 Pass = 0; Pass < 2; ++Pass)
        for (const auto& Entity : View.entities)
        {
            if (Entity.team < 0 || (Pass == 0 ? Entity.team == 0 : Entity.team != 0)
                || !PublicHandles.Contains(Entity.id) || !Simulation.visible(0, Entity.pos) || Targets.Num() >= MaxTargets) continue;
            const uint32 Handle = PublicHandles[Entity.id];
            const FString TargetId = FString::Printf(TEXT("target_%u"), Handle);
            auto Target = MakeShared<FJsonObject>();
            Target->SetStringField(TEXT("id"), TargetId);
            Target->SetStringField(TEXT("label"), FString::Printf(TEXT("%s %s %u"),
                Entity.team == 0 ? TEXT("Friendly") : TEXT("Visible enemy"),
                UTF8_TO_TCHAR(cinder::definition(Entity.kind).name), Handle));
            Target->SetBoolField(TEXT("visible"), true);
            Target->SetStringField(TEXT("relationship"), Entity.team == 0 ? TEXT("friendly") : TEXT("enemy"));
            Targets.Add(MakeShared<FJsonValueObject>(Target));
            Context.Targets.Add(TargetId, Entity.id);
        }
        if (Simulation.usesAuthoredTerrain())
        {
            const auto& Config = Simulation.config();
            const auto& Map = cinder::mapDefinition(Config.map, Config.playerCount, Config.matchLength, Config.mapRevision);
            for (size_t Index = 0; Index < Map.ramps.size(); ++Index)
            {
                const auto& Ramp = Map.ramps[Index];
                const cinder::Vec2 Center{(Ramp.high.x + Ramp.low.x) * 0.5f, (Ramp.high.y + Ramp.low.y) * 0.5f};
                AddLocation(FString::Printf(TEXT("ramp_%d"), static_cast<int32>(Index + 1)),
                    FString::Printf(TEXT("Explored ramp %d (%s %s)"), static_cast<int32>(Index + 1),
                        Center.y < Simulation.worldSize() * 0.5f ? TEXT("north") : TEXT("south"),
                        Center.x < Simulation.worldSize() * 0.5f ? TEXT("west") : TEXT("east")), Center);
            }
        }
        Summary->SetNumberField(TEXT("ore"), View.player.ore);
        Summary->SetNumberField(TEXT("tier"), View.player.tier);
        Summary->SetNumberField(TEXT("supply"), Simulation.supply(0));
        Summary->SetNumberField(TEXT("capacity"), Simulation.capacity(0));
        Summary->SetNumberField(TEXT("ownedEntityCount"), OwnedCount);
        Summary->SetNumberField(TEXT("visibleEnemyCount"), EnemyCount);
        Summary->SetNumberField(TEXT("seconds"), Simulation.time());
        Summary->SetBoolField(TEXT("online"), Battle->IsOnlineMatch());
    }
    Observation->SetArrayField(TEXT("unit_groups"), Groups);
    Observation->SetArrayField(TEXT("locations"), Locations);
    Observation->SetArrayField(TEXT("targets"), Targets);
    Observation->SetObjectField(TEXT("summary"), Summary);
    Captures.Add(ContextId, std::move(Context));
    return Observation;
}

TSharedPtr<FJsonObject> FCinderCommanderBridge::Execute(ACinderPlayerController* Controller,
    const TSharedPtr<FJsonObject>& Params)
{
    check(IsInGameThread());
    Synchronize(Controller);
    FString OperationId;
    if (!ReadId(Params, TEXT("operationId"), OperationId))
        return Receipt(TEXT(""), TEXT("rejected"), TEXT("Missing valid operation ID."));
    if (const FOperation* Existing = Operations.Find(OperationId)) return Existing->Receipt;
    if (Operations.Num() >= MaxOperations)
        return Receipt(OperationId, TEXT("rejected"), TEXT("Voice command session limit reached. End and reconnect voice."));
    auto Finish = [&](const TCHAR* Status, const FString& Message)
    {
        FOperation Operation;
        Operation.Receipt = Receipt(OperationId, Status, Message);
        Operations.Add(OperationId, Operation);
        return Operation.Receipt;
    };
    if (!Controller || !Controller->CommanderGameplayActive())
        return Finish(TEXT("rejected"), TEXT("The battlefield is not accepting orders."));
    FString ContextId, RequestedGeneration;
    if (!ReadId(Params, TEXT("contextId"), ContextId) || !ReadId(Params, TEXT("generation"), RequestedGeneration))
        return Finish(TEXT("rejected"), TEXT("Missing captured command context."));
    const FCapture* Context = Captures.Find(ContextId);
    if (!Context || Context->Generation != Generation || RequestedGeneration != Generation
        || FPlatformTime::Seconds() - Context->CreatedAt > CaptureLifetime)
        return Finish(TEXT("rejected"), TEXT("This command context expired or belongs to a different match. Please repeat the order."));
    const TSharedPtr<FJsonObject>* ProposalPtr = nullptr;
    if (!Params->TryGetObjectField(TEXT("proposal"), ProposalPtr) || !ProposalPtr || !*ProposalPtr)
        return Finish(TEXT("rejected"), TEXT("Missing typed command proposal."));
    const TSharedPtr<FJsonObject>& Proposal = *ProposalPtr;
    FString Action, QueueMode;
    if (!ReadId(Proposal, TEXT("action"), Action) || !ReadId(Proposal, TEXT("queueMode"), QueueMode)
        || (QueueMode != TEXT("replace") && QueueMode != TEXT("append")))
        return Finish(TEXT("rejected"), TEXT("Unsupported command or queue mode."));
    const bool bDestination = Action == TEXT("move") || Action == TEXT("attack_move") || Action == TEXT("defend")
        || Action == TEXT("patrol") || Action == TEXT("focus_location");
    const bool bTarget = Action == TEXT("attack") || Action == TEXT("escort");
    const bool bQueueable = Action == TEXT("move") || Action == TEXT("attack_move") || Action == TEXT("patrol");
    if (QueueMode == TEXT("append") && !bQueueable)
        return Finish(TEXT("rejected"), TEXT("This action cannot be appended to an order queue."));
    const auto& Simulation = Battlefield->Sim();
    cinder::Command Command;
    Command.queueMode = QueueMode == TEXT("append") ? cinder::CommandQueueMode::Append : cinder::CommandQueueMode::Replace;
    if (bDestination)
    {
        FString Destination;
        if (!ReadId(Proposal, TEXT("destination"), Destination) || !Context->Locations.Contains(Destination))
            return Finish(TEXT("rejected"), TEXT("The destination is not a captured location."));
        Command.point = Context->Locations[Destination];
        if (!KnownPoint(Simulation, Command.point)) return Finish(TEXT("rejected"), TEXT("That location is no longer known."));
    }
    if (Action == TEXT("focus_location"))
        return Controller->CommanderFocus(Command.point) ? Finish(TEXT("accepted"), TEXT("Location shown."))
            : Finish(TEXT("rejected"), TEXT("The camera could not focus that location."));
    FString Group;
    if (!ReadId(Proposal, TEXT("unitGroup"), Group) || !Context->Groups.Contains(Group))
        return Finish(TEXT("rejected"), TEXT("The unit group is not in the captured selection."));
    Command.units = Context->Groups[Group];
    if (Command.units.empty() || Command.units.size() > cinder::net::MaxCommandUnits)
        return Finish(TEXT("rejected"), TEXT("The captured group has no supported recipients."));
    for (cinder::Id Id : Command.units)
        if (!Owned(Simulation.find(Id)))
            return Finish(TEXT("rejected"), TEXT("A captured unit is no longer available. Please repeat the order."));
    if (Action == TEXT("select_units"))
        return Controller->CommanderSelect(Command.units) ? Finish(TEXT("accepted"), Controller->Feedback())
            : Finish(TEXT("rejected"), TEXT("Those units could not be selected."));
    if (bTarget)
    {
        FString Target;
        if (!ReadId(Proposal, TEXT("target"), Target) || !Context->Targets.Contains(Target))
            return Finish(TEXT("rejected"), TEXT("The target is not a captured visible target."));
        Command.target = Context->Targets[Target];
        const auto* Entity = Simulation.find(Command.target);
        if (!Entity || !Entity->alive() || !Simulation.visible(0, Entity->pos))
            return Finish(TEXT("rejected"), TEXT("The target is no longer visible."));
        if ((Action == TEXT("attack") && Entity->team <= 0) || (Action == TEXT("escort") && Entity->team != 0))
            return Finish(TEXT("rejected"), TEXT("The target relationship does not support this order."));
    }
    if (Action == TEXT("move")) Command.type = cinder::CommandType::Move;
    else if (Action == TEXT("attack_move")) Command.type = cinder::CommandType::AttackMove;
    else if (Action == TEXT("attack")) Command.type = cinder::CommandType::Attack;
    else if (Action == TEXT("hold")) Command.type = cinder::CommandType::Hold;
    else if (Action == TEXT("stop")) Command.type = cinder::CommandType::Stop;
    else if (Action == TEXT("defend")) Command.type = cinder::CommandType::Defend;
    else if (Action == TEXT("patrol")) Command.type = cinder::CommandType::Patrol;
    else if (Action == TEXT("escort")) Command.type = cinder::CommandType::Escort;
    else return Finish(TEXT("rejected"), TEXT("This action is not registered for voice control."));
    // Reject all-or-nothing instead of letting Simulation silently skip immobile recipients.
    for (cinder::Id Id : Command.units)
        if (cinder::definition(Simulation.find(Id)->kind).building)
            return Finish(TEXT("rejected"), TEXT("This captured group contains structures; select mobile units for this order."));
    uint32 Sequence = 0;
    if (!Controller->CommanderIssue(std::move(Command), &Sequence))
        return Finish(TEXT("rejected"), Controller->Feedback().IsEmpty() ? TEXT("The game rejected this order.") : Controller->Feedback());
    if (!Battlefield->IsOnlineMatch()) return Finish(TEXT("accepted"), Controller->Feedback());
    FOperation Operation;
    Operation.Sequence = Sequence;
    Operation.SubmittedAt = FPlatformTime::Seconds();
    Operation.bPending = Sequence != 0;
    Operation.Receipt = Receipt(OperationId, Sequence ? TEXT("pending") : TEXT("uncertain"),
        Sequence ? TEXT("Order sent. Waiting for the server.") : TEXT("No acknowledgement identifier was returned. The order was not replayed."), Sequence);
    Operations.Add(OperationId, Operation);
    return Operation.Receipt;
}

TArray<TSharedPtr<FJsonObject>> FCinderCommanderBridge::Poll(ACinderPlayerController* Controller)
{
    check(IsInGameThread());
    Synchronize(Controller);
    TArray<TSharedPtr<FJsonObject>> Results;
    UCinderOnlineSubsystem* Online = Controller ? Controller->Online() : nullptr;
    for (auto& Pair : Operations)
    {
        auto& Operation = Pair.Value;
        if (Operation.bPending)
        {
            FCinderOnlineCommandAcknowledgement Ack;
            if (Online && Online->ConsumeCommandAcknowledgement(Operation.Sequence, Ack))
            {
                Operation.Receipt = Receipt(Pair.Key, Ack.bAccepted ? TEXT("accepted") : TEXT("rejected"), Ack.Message, Operation.Sequence);
                Operation.bPending = false;
                Operation.bDeliver = true;
            }
            else if (!Online || !Online->IsCommandPending(Operation.Sequence)
                || FPlatformTime::Seconds() - Operation.SubmittedAt > 20.0)
            {
                Operation.Receipt = Receipt(Pair.Key, TEXT("uncertain"), TEXT("The server acknowledgement was lost. Check the battlefield; this order was not replayed."), Operation.Sequence);
                Operation.bPending = false;
                Operation.bDeliver = true;
            }
        }
        if (Operation.bDeliver) { Results.Add(Operation.Receipt); Operation.bDeliver = false; }
    }
    return Results;
}

void FCinderCommanderBridge::Cancel(const FString& OperationId)
{
    check(IsInGameThread());
    if (OperationId.IsEmpty() || OperationId.Len() > 128 || Operations.Contains(OperationId) || Operations.Num() >= MaxOperations) return;
    FOperation Operation;
    Operation.Receipt = Receipt(OperationId, TEXT("cancelled"), TEXT("Cancelled before submission."));
    Operations.Add(OperationId, Operation);
}

void FCinderCommanderBridge::Reset()
{
    check(IsInGameThread());
    Captures.Reset();
    Operations.Reset();
    PublicHandles.Reset();
    NextPublicHandle = 1;
    Battlefield.Reset();
    PlayerController.Reset();
    Generation.Empty();
    MatchSerial = LastTick = 0;
}
