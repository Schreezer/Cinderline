#include "Presentation/CinderCampaignSave.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <cmath>
#include <limits>

namespace
{
constexpr int32 PersistenceSchema = 1;
constexpr int32 SimulationSaveVersion = 15;
constexpr int32 LegacySimulationSaveVersion = 14;
constexpr TCHAR ProgressFilename[] = TEXT("progress.json");
constexpr TCHAR ProgressBackupFilename[] = TEXT("progress.backup.json");
constexpr TCHAR ManifestFilename[] = TEXT("checkpoint-manifest.json");

FString GenerationSimulation(int32 Generation)
{
    return FString::Printf(TEXT("checkpoint-%c.cinder"), Generation == 0 ? TEXT('a') : TEXT('b'));
}

FString GenerationDirector(int32 Generation)
{
    return FString::Printf(TEXT("checkpoint-%c.json"), Generation == 0 ? TEXT('a') : TEXT('b'));
}

FString In(const FString& Root, const FString& Name)
{
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(Root, Name));
}

bool MoveReplacing(const FString& Destination, const FString& Source)
{
    IFileManager& Files = IFileManager::Get();
    if (Files.FileExists(*Destination)) Files.Delete(*Destination, false, true);
    return Files.Move(*Destination, *Source, true, true, false, true);
}

bool WriteJsonFile(const FString& Filename, const TSharedRef<FJsonObject>& Object)
{
    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    return FJsonSerializer::Serialize(Object, Writer) && FFileHelper::SaveStringToFile(Text, *Filename);
}

bool ReadJsonFile(const FString& Filename, TSharedPtr<FJsonObject>& Out)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Filename)) return false;
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
}

bool HasSimulationHeader(const FString& Filename, int32 ExpectedVersion)
{
    FString Text;
    const FString Header = FString::Printf(TEXT("CINDERLINE %d"), ExpectedVersion);
    return FFileHelper::LoadFileToString(Text, *Filename) &&
        (Text.StartsWith(Header + TEXT("\n")) || Text.StartsWith(Header + TEXT("\r\n")));
}

void PutU64(const TSharedRef<FJsonObject>& Object, const TCHAR* Field, uint64 Value)
{
    Object->SetStringField(Field, FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(Value)));
}

bool GetU64(const FJsonObject& Object, const TCHAR* Field, uint64& Out)
{
    FString Text;
    if (!Object.TryGetStringField(Field, Text) || Text.IsEmpty()) return false;
    uint64 Value = 0;
    for (TCHAR Character : Text)
    {
        if (Character < TEXT('0') || Character > TEXT('9')) return false;
        const uint64 Digit = static_cast<uint64>(Character - TEXT('0'));
        if (Value > (std::numeric_limits<uint64>::max() - Digit) / 10) return false;
        Value = Value * 10 + Digit;
    }
    Out = Value;
    return true;
}

bool GetInteger(const FJsonObject& Object, const TCHAR* Field, int64 Minimum, int64 Maximum, int64& Out)
{
    double Number = 0;
    if (!Object.TryGetNumberField(Field, Number) || !std::isfinite(Number) || std::floor(Number) != Number ||
        Number < static_cast<double>(Minimum) || Number > static_cast<double>(Maximum)) return false;
    Out = static_cast<int64>(Number);
    return true;
}

bool GetBool(const FJsonObject& Object, const TCHAR* Field, bool& Out)
{
    return Object.TryGetBoolField(Field, Out);
}

TSharedRef<FJsonObject> Point(cinder::Vec2 Value)
{
    const auto Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("x"), Value.x);
    Object->SetNumberField(TEXT("y"), Value.y);
    return Object;
}

bool ReadPoint(const FJsonObject& Parent, const TCHAR* Field, cinder::Vec2& Out)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    double X = 0, Y = 0;
    if (!Parent.TryGetObjectField(Field, Object) || !Object || !Object->IsValid() ||
        !(*Object)->TryGetNumberField(TEXT("x"), X) || !(*Object)->TryGetNumberField(TEXT("y"), Y) ||
        !std::isfinite(X) || !std::isfinite(Y) || std::fabs(X) > std::numeric_limits<float>::max() ||
        std::fabs(Y) > std::numeric_limits<float>::max()) return false;
    Out = {static_cast<float>(X), static_cast<float>(Y)};
    return true;
}

TArray<TSharedPtr<FJsonValue>> IdArray(const std::vector<cinder::Id>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    Result.Reserve(static_cast<int32>(Values.size()));
    for (const cinder::Id Value : Values) Result.Add(MakeShared<FJsonValueNumber>(Value));
    return Result;
}

bool ReadIdArray(const FJsonObject& Object, const TCHAR* Field, std::vector<cinder::Id>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object.TryGetArrayField(Field, Values) || !Values || Values->Num() > 4096) return false;
    std::vector<cinder::Id> Candidate;
    Candidate.reserve(Values->Num());
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        double Number = 0;
        if (!Value.IsValid() || !Value->TryGetNumber(Number) || !std::isfinite(Number) ||
            std::floor(Number) != Number || Number <= 0 || Number > std::numeric_limits<cinder::Id>::max()) return false;
        Candidate.push_back(static_cast<cinder::Id>(Number));
    }
    Out = std::move(Candidate);
    return true;
}

TSharedRef<FJsonObject> AuthoredJson(const FCinderCampaignAuthoredIds& Value)
{
    const auto Object = MakeShared<FJsonObject>();
#define CINDER_ID(Name) Object->SetNumberField(TEXT(#Name), Value.Name)
    CINDER_ID(PlayerAnchor); CINDER_ID(EnemyAnchor); CINDER_ID(MiningWorker); CINDER_ID(RallyWorker);
    CINDER_ID(Kiln); CINDER_ID(PlayerSiphon); CINDER_ID(RemoteSiphon); CINDER_ID(Resonator);
    CINDER_ID(MotorPool); CINDER_ID(RaidProducer);
#undef CINDER_ID
#define CINDER_IDS(Name) Object->SetArrayField(TEXT(#Name), IdArray(Value.Name))
    CINDER_IDS(HomeOre); CINDER_IDS(RemoteOre); CINDER_IDS(PracticePatrol); CINDER_IDS(ScoutPosts);
    CINDER_IDS(ForwardThreats); CINDER_IDS(SiegeGuard); CINDER_IDS(SiegeProducers);
    CINDER_IDS(OrderedUnits); CINDER_IDS(ActiveWave);
#undef CINDER_IDS
    return Object;
}

bool ReadAuthored(const FJsonObject& Parent, FCinderCampaignAuthoredIds& Out)
{
    const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
    if (!Parent.TryGetObjectField(TEXT("authored"), ObjectPtr) || !ObjectPtr || !ObjectPtr->IsValid()) return false;
    const FJsonObject& Object = *ObjectPtr->Get();
    FCinderCampaignAuthoredIds Candidate;
    int64 Number = 0;
#define CINDER_ID(Name) if (!GetInteger(Object, TEXT(#Name), 0, std::numeric_limits<cinder::Id>::max(), Number)) return false; Candidate.Name = static_cast<cinder::Id>(Number)
    CINDER_ID(PlayerAnchor); CINDER_ID(EnemyAnchor); CINDER_ID(MiningWorker); CINDER_ID(RallyWorker);
    CINDER_ID(Kiln); CINDER_ID(PlayerSiphon); CINDER_ID(RemoteSiphon); CINDER_ID(Resonator);
    CINDER_ID(MotorPool); CINDER_ID(RaidProducer);
#undef CINDER_ID
#define CINDER_IDS(Name) if (!ReadIdArray(Object, TEXT(#Name), Candidate.Name)) return false
    CINDER_IDS(HomeOre); CINDER_IDS(RemoteOre); CINDER_IDS(PracticePatrol); CINDER_IDS(ScoutPosts);
    CINDER_IDS(ForwardThreats); CINDER_IDS(SiegeGuard); CINDER_IDS(SiegeProducers);
    CINDER_IDS(OrderedUnits); CINDER_IDS(ActiveWave);
#undef CINDER_IDS
    Out = std::move(Candidate);
    return true;
}

TSharedRef<FJsonObject> CountersJson(const FCinderCampaignCounters& Value)
{
    const auto Object = MakeShared<FJsonObject>();
#define CINDER_COUNTER(Name) Object->SetNumberField(TEXT(#Name), Value.Name)
    CINDER_COUNTER(MissionStartProduced); CINDER_COUNTER(MissionStartBuilt); CINDER_COUNTER(MissionStartGathered);
    CINDER_COUNTER(MissionStartUpgrades); CINDER_COUNTER(PhaseStartProduced); CINDER_COUNTER(PhaseStartBuilt);
    CINDER_COUNTER(PhaseStartGathered); CINDER_COUNTER(PhaseStartUpgrades); CINDER_COUNTER(PhaseStartWorkers);
    CINDER_COUNTER(PhaseStartEmbers); CINDER_COUNTER(PhaseStartSiphons); CINDER_COUNTER(PhaseStartResonators);
    CINDER_COUNTER(PhaseStartMotorPools); CINDER_COUNTER(PhaseStartAnvils); CINDER_COUNTER(OpponentTrainOrders);
    CINDER_COUNTER(WaveIndex); CINDER_COUNTER(StableTicks);
#undef CINDER_COUNTER
    Object->SetNumberField(TEXT("LastTrackedCarried"), Value.LastTrackedCarried);
    return Object;
}

bool ReadCounters(const FJsonObject& Parent, FCinderCampaignCounters& Out)
{
    const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
    if (!Parent.TryGetObjectField(TEXT("counters"), ObjectPtr) || !ObjectPtr || !ObjectPtr->IsValid()) return false;
    const FJsonObject& Object = *ObjectPtr->Get();
    FCinderCampaignCounters Candidate;
    int64 Number = 0;
#define CINDER_COUNTER(Name) if (!GetInteger(Object, TEXT(#Name), std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max(), Number)) return false; Candidate.Name = static_cast<int32>(Number)
    CINDER_COUNTER(MissionStartProduced); CINDER_COUNTER(MissionStartBuilt); CINDER_COUNTER(MissionStartGathered);
    CINDER_COUNTER(MissionStartUpgrades); CINDER_COUNTER(PhaseStartProduced); CINDER_COUNTER(PhaseStartBuilt);
    CINDER_COUNTER(PhaseStartGathered); CINDER_COUNTER(PhaseStartUpgrades); CINDER_COUNTER(PhaseStartWorkers);
    CINDER_COUNTER(PhaseStartEmbers); CINDER_COUNTER(PhaseStartSiphons); CINDER_COUNTER(PhaseStartResonators);
    CINDER_COUNTER(PhaseStartMotorPools); CINDER_COUNTER(PhaseStartAnvils); CINDER_COUNTER(OpponentTrainOrders);
    CINDER_COUNTER(WaveIndex); CINDER_COUNTER(StableTicks);
#undef CINDER_COUNTER
    double Carried = 0;
    if (!Object.TryGetNumberField(TEXT("LastTrackedCarried"), Carried) || !std::isfinite(Carried) ||
        std::fabs(Carried) > std::numeric_limits<float>::max()) return false;
    Candidate.LastTrackedCarried = static_cast<float>(Carried);
    Out = Candidate;
    return true;
}

TSharedRef<FJsonObject> StateJson(const FCinderCampaignState& State, const cinder::Simulation& Sim, uint64 Commit)
{
    const auto Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("persistence_schema"), PersistenceSchema);
    Object->SetNumberField(TEXT("simulation_save_version"), SimulationSaveVersion);
    PutU64(Object, TEXT("commit"), Commit);
    PutU64(Object, TEXT("sim_tick"), Sim.tick());
    PutU64(Object, TEXT("sim_hash"), Sim.stateHash());
    Object->SetNumberField(TEXT("state_schema"), State.SchemaVersion);
    Object->SetNumberField(TEXT("content_version"), State.ContentVersion);
    Object->SetNumberField(TEXT("mission"), State.Mission);
    Object->SetNumberField(TEXT("phase"), State.Phase);
    Object->SetNumberField(TEXT("outcome"), static_cast<int32>(State.Outcome));
    Object->SetStringField(TEXT("failure_code"), State.FailureCode);
    PutU64(Object, TEXT("mission_start_tick"), State.MissionStartTick);
    PutU64(Object, TEXT("phase_start_tick"), State.PhaseStartTick);
    PutU64(Object, TEXT("objective_start_tick"), State.ObjectiveStartTick);
    PutU64(Object, TEXT("next_opponent_decision_tick"), State.NextOpponentDecisionTick);
    Object->SetNumberField(TEXT("checkpoint_serial"), State.CheckpointSerial);
    Object->SetNumberField(TEXT("checkpoint_phase"), State.CheckpointPhase);
    PutU64(Object, TEXT("accepted_command_mask"), State.AcceptedCommandMask);
    PutU64(Object, TEXT("phase_command_mask"), State.PhaseCommandMask);
    PutU64(Object, TEXT("completed_objective_mask"), State.CompletedObjectiveMask);
    PutU64(Object, TEXT("optional_mask"), State.OptionalMask);
    PutU64(Object, TEXT("assistance_mask"), State.AssistanceMask);
    Object->SetNumberField(TEXT("coaching_level"), State.CoachingLevel);
    Object->SetObjectField(TEXT("authored"), AuthoredJson(State.Authored));
    Object->SetObjectField(TEXT("counters"), CountersJson(State.Counters));
    Object->SetObjectField(TEXT("base_point"), Point(State.BasePoint));
    Object->SetObjectField(TEXT("home_ore_point"), Point(State.HomeOrePoint));
    Object->SetObjectField(TEXT("enemy_objective_point"), Point(State.EnemyObjectivePoint));
    Object->SetObjectField(TEXT("local_build_point"), Point(State.LocalBuildPoint));
    Object->SetObjectField(TEXT("remote_patch_point"), Point(State.RemotePatchPoint));
    Object->SetObjectField(TEXT("build_point"), Point(State.BuildPoint));
    Object->SetObjectField(TEXT("scout_zone_a"), Point(State.ScoutZoneA));
    Object->SetObjectField(TEXT("scout_zone_b"), Point(State.ScoutZoneB));
    Object->SetObjectField(TEXT("forward_approach"), Point(State.ForwardApproach));
    Object->SetObjectField(TEXT("home_defense"), Point(State.HomeDefense));
#define CINDER_BOOL(JsonName, Name) Object->SetBoolField(TEXT(JsonName), State.Name)
    CINDER_BOOL("camera_observed", bCameraObserved);
    CINDER_BOOL("anchor_ore_rally_accepted", bAnchorOreRallyAccepted);
    CINDER_BOOL("army_rally_accepted", bArmyRallyAccepted);
    CINDER_BOOL("gather_delivery_observed", bGatherDeliveryObserved);
    CINDER_BOOL("rally_worker_assigned", bRallyWorkerAssigned);
    CINDER_BOOL("remote_worker_carried", bRemoteWorkerCarried);
    CINDER_BOOL("remote_delivery_observed", bRemoteDeliveryObserved);
    CINDER_BOOL("scout_a_observed", bScoutAObserved);
    CINDER_BOOL("scout_b_observed", bScoutBObserved);
    CINDER_BOOL("wave_production_finished", bWaveProductionFinished);
    CINDER_BOOL("checkpoint_settled", bCheckpointSettled);
#undef CINDER_BOOL
    Object->SetNumberField(TEXT("highest_setup_entity_id"), State.HighestSetupEntityId);
    Object->SetNumberField(TEXT("rally_baseline_entity_id"), State.RallyBaselineEntityId);
    return Object;
}

enum class EStateRead { Ok, Unsupported, Corrupt };

EStateRead ReadState(const FJsonObject& Object, FCinderCampaignState& OutState,
    uint64& OutCommit, uint64& OutTick, uint64& OutHash, int32& OutSimulationVersion)
{
    int64 Number = 0;
    if (!GetInteger(Object, TEXT("persistence_schema"), 0, std::numeric_limits<int32>::max(), Number)) return EStateRead::Corrupt;
    if (Number != PersistenceSchema) return EStateRead::Unsupported;
    if (!GetInteger(Object, TEXT("simulation_save_version"), 0, std::numeric_limits<int32>::max(), Number)) return EStateRead::Corrupt;
    if (Number != SimulationSaveVersion && Number != LegacySimulationSaveVersion) return EStateRead::Unsupported;
    OutSimulationVersion = static_cast<int32>(Number);
    FCinderCampaignState State;
    if (!GetInteger(Object, TEXT("state_schema"), 0, std::numeric_limits<uint32>::max(), Number)) return EStateRead::Corrupt;
    State.SchemaVersion = static_cast<uint32>(Number);
    if (!GetInteger(Object, TEXT("content_version"), 0, std::numeric_limits<uint32>::max(), Number)) return EStateRead::Corrupt;
    State.ContentVersion = static_cast<uint32>(Number);
    if (State.SchemaVersion != FCinderCampaign::StateSchemaVersion || State.ContentVersion != FCinderCampaign::ContentVersion)
        return EStateRead::Unsupported;
    if (!GetU64(Object, TEXT("commit"), OutCommit) || !GetU64(Object, TEXT("sim_tick"), OutTick) ||
        !GetU64(Object, TEXT("sim_hash"), OutHash) ||
        !GetInteger(Object, TEXT("mission"), -1, FCinderCampaign::MissionCount - 1, Number)) return EStateRead::Corrupt;
    State.Mission = static_cast<int32>(Number);
    if (!GetInteger(Object, TEXT("phase"), 0, 63, Number)) return EStateRead::Corrupt; State.Phase = static_cast<int32>(Number);
    if (!GetInteger(Object, TEXT("outcome"), 0, static_cast<int32>(ECinderCampaignOutcome::Draw), Number)) return EStateRead::Corrupt;
    State.Outcome = static_cast<ECinderCampaignOutcome>(Number);
    if (!Object.TryGetStringField(TEXT("failure_code"), State.FailureCode) ||
        !GetU64(Object, TEXT("mission_start_tick"), State.MissionStartTick) ||
        !GetU64(Object, TEXT("phase_start_tick"), State.PhaseStartTick) ||
        !GetU64(Object, TEXT("objective_start_tick"), State.ObjectiveStartTick) ||
        !GetU64(Object, TEXT("next_opponent_decision_tick"), State.NextOpponentDecisionTick)) return EStateRead::Corrupt;
    if (!GetInteger(Object, TEXT("checkpoint_serial"), 0, std::numeric_limits<uint32>::max(), Number)) return EStateRead::Corrupt;
    State.CheckpointSerial = static_cast<uint32>(Number);
    if (!GetInteger(Object, TEXT("checkpoint_phase"), 0, 63, Number)) return EStateRead::Corrupt;
    State.CheckpointPhase = static_cast<int32>(Number);
    if (!GetU64(Object, TEXT("accepted_command_mask"), State.AcceptedCommandMask) ||
        !GetU64(Object, TEXT("phase_command_mask"), State.PhaseCommandMask) ||
        !GetU64(Object, TEXT("completed_objective_mask"), State.CompletedObjectiveMask) ||
        !GetU64(Object, TEXT("optional_mask"), State.OptionalMask) ||
        !GetU64(Object, TEXT("assistance_mask"), State.AssistanceMask)) return EStateRead::Corrupt;
    if (!GetInteger(Object, TEXT("coaching_level"), std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max(), Number)) return EStateRead::Corrupt;
    State.CoachingLevel = static_cast<int32>(Number);
    if (!ReadAuthored(Object, State.Authored) || !ReadCounters(Object, State.Counters) ||
        !ReadPoint(Object, TEXT("base_point"), State.BasePoint) ||
        !ReadPoint(Object, TEXT("home_ore_point"), State.HomeOrePoint) ||
        !ReadPoint(Object, TEXT("enemy_objective_point"), State.EnemyObjectivePoint) ||
        !ReadPoint(Object, TEXT("local_build_point"), State.LocalBuildPoint) ||
        !ReadPoint(Object, TEXT("remote_patch_point"), State.RemotePatchPoint) ||
        !ReadPoint(Object, TEXT("build_point"), State.BuildPoint) ||
        !ReadPoint(Object, TEXT("scout_zone_a"), State.ScoutZoneA) ||
        !ReadPoint(Object, TEXT("scout_zone_b"), State.ScoutZoneB) ||
        !ReadPoint(Object, TEXT("forward_approach"), State.ForwardApproach) ||
        !ReadPoint(Object, TEXT("home_defense"), State.HomeDefense)) return EStateRead::Corrupt;
#define CINDER_BOOL(JsonName, Name) if (!GetBool(Object, TEXT(JsonName), State.Name)) return EStateRead::Corrupt
    CINDER_BOOL("camera_observed", bCameraObserved);
    CINDER_BOOL("anchor_ore_rally_accepted", bAnchorOreRallyAccepted);
    CINDER_BOOL("army_rally_accepted", bArmyRallyAccepted);
    CINDER_BOOL("gather_delivery_observed", bGatherDeliveryObserved);
    CINDER_BOOL("rally_worker_assigned", bRallyWorkerAssigned);
    CINDER_BOOL("remote_worker_carried", bRemoteWorkerCarried);
    CINDER_BOOL("remote_delivery_observed", bRemoteDeliveryObserved);
    CINDER_BOOL("scout_a_observed", bScoutAObserved);
    CINDER_BOOL("scout_b_observed", bScoutBObserved);
    CINDER_BOOL("wave_production_finished", bWaveProductionFinished);
    CINDER_BOOL("checkpoint_settled", bCheckpointSettled);
#undef CINDER_BOOL
    if (!GetInteger(Object, TEXT("highest_setup_entity_id"), 0, std::numeric_limits<cinder::Id>::max(), Number)) return EStateRead::Corrupt;
    State.HighestSetupEntityId = static_cast<cinder::Id>(Number);
    if (!GetInteger(Object, TEXT("rally_baseline_entity_id"), 0, std::numeric_limits<cinder::Id>::max(), Number)) return EStateRead::Corrupt;
    State.RallyBaselineEntityId = static_cast<cinder::Id>(Number);
    OutState = std::move(State);
    return EStateRead::Ok;
}

struct FGeneration
{
    int32 Index = -1;
    uint64 Commit = 0;
    TUniquePtr<cinder::Simulation> Simulation;
    FCinderCampaignState State;
    EStateRead Read = EStateRead::Corrupt;
    FString Error;
};

FGeneration LoadGeneration(const FString& Root, int32 Index)
{
    FGeneration Result;
    Result.Index = Index;
    TSharedPtr<FJsonObject> Object;
    if (!ReadJsonFile(In(Root, GenerationDirector(Index)), Object))
    {
        Result.Error = TEXT("Campaign checkpoint metadata is missing or corrupt.");
        return Result;
    }
    uint64 Tick = 0, Hash = 0;
    int32 SimulationVersion = 0;
    Result.Read = ReadState(*Object, Result.State, Result.Commit, Tick, Hash, SimulationVersion);
    if (Result.Read == EStateRead::Unsupported)
    {
        Result.Error = TEXT("Campaign checkpoint was created by an unsupported campaign version.");
        return Result;
    }
    if (Result.Read != EStateRead::Ok)
    {
        Result.Error = TEXT("Campaign checkpoint metadata is corrupt.");
        return Result;
    }
    Result.Simulation = MakeUnique<cinder::Simulation>();
    const FString SimulationPath = In(Root, GenerationSimulation(Index));
    const FString ExternalSimulationPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*SimulationPath);
    // A supported director version must match its exact simulation generation.
    // Revision-zero v14 loads preserve their hash; v15 retains the explicit revision.
    if (!HasSimulationHeader(SimulationPath, SimulationVersion) ||
        !Result.Simulation->load(TCHAR_TO_UTF8(*ExternalSimulationPath)))
    {
        Result.Error = TEXT("Campaign simulation checkpoint is missing or corrupt.");
        Result.Simulation.Reset();
        return Result;
    }
    if (Result.Simulation->tick() != Tick || Result.Simulation->stateHash() != Hash)
    {
        Result.Error = TEXT("Campaign checkpoint simulation and director records do not match.");
        Result.Simulation.Reset();
        return Result;
    }
    FString Validation;
    if (!FCinderCampaign::ValidateState(*Result.Simulation, Result.State, Validation))
    {
        Result.Error = Validation.IsEmpty() ? TEXT("Campaign director checkpoint is invalid.") : Validation;
        Result.Simulation.Reset();
    }
    return Result;
}

bool Valid(const FGeneration& Generation) { return Generation.Simulation.IsValid() && Generation.Read == EStateRead::Ok; }

bool ReadManifest(const FString& Root, int32& OutGeneration, uint64& OutCommit)
{
    TSharedPtr<FJsonObject> Object;
    int64 Number = 0;
    if (!ReadJsonFile(In(Root, ManifestFilename), Object) ||
        !GetInteger(*Object, TEXT("schema"), 1, 1, Number)) return false;
    if (!GetInteger(*Object, TEXT("generation"), 0, 1, Number)) return false;
    OutGeneration = static_cast<int32>(Number);
    return GetU64(*Object, TEXT("commit"), OutCommit);
}

bool WriteManifest(const FString& Root, int32 Generation, uint64 Commit)
{
    const auto Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("schema"), PersistenceSchema);
    Object->SetNumberField(TEXT("generation"), Generation);
    PutU64(Object, TEXT("commit"), Commit);
    const FString Temporary = In(Root, FString(ManifestFilename) + TEXT(".tmp"));
    if (!WriteJsonFile(Temporary, Object)) return false;
    return MoveReplacing(In(Root, ManifestFilename), Temporary);
}

TSharedRef<FJsonObject> ProgressJson(const FCinderCampaignProgress& Progress)
{
    const auto Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("schema"), FCinderCampaignProgress::SchemaVersion);
    Object->SetNumberField(TEXT("content_version"), Progress.ContentVersion);
    Object->SetNumberField(TEXT("completed_mask"), Progress.CompletedMask);
    Object->SetNumberField(TEXT("assisted_mask"), Progress.AssistedMask);
    Object->SetNumberField(TEXT("unassisted_mask"), Progress.UnassistedMask);
    Object->SetNumberField(TEXT("last_played_mission"), Progress.LastPlayedMission);
    TArray<TSharedPtr<FJsonValue>> Mastery, Best;
    for (int32 Mission = 0; Mission < FCinderCampaignProgress::MissionCount; ++Mission)
    {
        Mastery.Add(MakeShared<FJsonValueNumber>(Progress.MasteryMasks[Mission]));
        Best.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%llu"),
            static_cast<unsigned long long>(Progress.BestCompletionTicks[Mission]))));
    }
    Object->SetArrayField(TEXT("mastery_masks"), Mastery);
    Object->SetArrayField(TEXT("best_completion_ticks"), Best);
    return Object;
}

EStateRead ReadProgressObject(const FJsonObject& Object, FCinderCampaignProgress& Out)
{
    int64 Number = 0;
    if (!GetInteger(Object, TEXT("schema"), 0, std::numeric_limits<uint32>::max(), Number)) return EStateRead::Corrupt;
    if (Number != FCinderCampaignProgress::SchemaVersion) return EStateRead::Unsupported;
    FCinderCampaignProgress Candidate;
    if (!GetInteger(Object, TEXT("content_version"), 0, std::numeric_limits<uint32>::max(), Number)) return EStateRead::Corrupt;
    Candidate.ContentVersion = static_cast<uint32>(Number);
    if (Candidate.ContentVersion != FCinderCampaign::ContentVersion) return EStateRead::Unsupported;
    const uint32 ValidMask = (uint32{1} << FCinderCampaignProgress::MissionCount) - 1;
#define CINDER_PROGRESS_MASK(JsonName, Name) if (!GetInteger(Object, TEXT(JsonName), 0, ValidMask, Number)) return EStateRead::Corrupt; Candidate.Name = static_cast<uint32>(Number)
    CINDER_PROGRESS_MASK("completed_mask", CompletedMask);
    CINDER_PROGRESS_MASK("assisted_mask", AssistedMask);
    CINDER_PROGRESS_MASK("unassisted_mask", UnassistedMask);
#undef CINDER_PROGRESS_MASK
    if (!GetInteger(Object, TEXT("last_played_mission"), 0, FCinderCampaignProgress::MissionCount - 1, Number)) return EStateRead::Corrupt;
    Candidate.LastPlayedMission = static_cast<int32>(Number);
    const TArray<TSharedPtr<FJsonValue>>* Mastery = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Best = nullptr;
    if (!Object.TryGetArrayField(TEXT("mastery_masks"), Mastery) || !Mastery || Mastery->Num() != FCinderCampaignProgress::MissionCount ||
        !Object.TryGetArrayField(TEXT("best_completion_ticks"), Best) || !Best || Best->Num() != FCinderCampaignProgress::MissionCount) return EStateRead::Corrupt;
    for (int32 Mission = 0; Mission < FCinderCampaignProgress::MissionCount; ++Mission)
    {
        double MasteryNumber = 0;
        if (!(*Mastery)[Mission].IsValid() || !(*Mastery)[Mission]->TryGetNumber(MasteryNumber) ||
            !std::isfinite(MasteryNumber) || std::floor(MasteryNumber) != MasteryNumber ||
            MasteryNumber < 0 || MasteryNumber > std::numeric_limits<uint32>::max()) return EStateRead::Corrupt;
        Candidate.MasteryMasks[Mission] = static_cast<uint32>(MasteryNumber);
        FString TickText;
        if (!(*Best)[Mission].IsValid() || !(*Best)[Mission]->TryGetString(TickText)) return EStateRead::Corrupt;
        const auto TickObject = MakeShared<FJsonObject>(); TickObject->SetStringField(TEXT("tick"), TickText);
        if (!GetU64(*TickObject, TEXT("tick"), Candidate.BestCompletionTicks[Mission])) return EStateRead::Corrupt;
    }
    if ((Candidate.AssistedMask & ~Candidate.CompletedMask) || (Candidate.UnassistedMask & ~Candidate.CompletedMask)) return EStateRead::Corrupt;
    Out = Candidate;
    return EStateRead::Ok;
}

ECinderCampaignLoadResult ReadProgressFile(const FString& Filename, FCinderCampaignProgress& Out)
{
    if (!IFileManager::Get().FileExists(*Filename)) return ECinderCampaignLoadResult::NotFound;
    TSharedPtr<FJsonObject> Object;
    if (!ReadJsonFile(Filename, Object)) return ECinderCampaignLoadResult::Corrupt;
    switch (ReadProgressObject(*Object, Out))
    {
    case EStateRead::Ok: return ECinderCampaignLoadResult::Loaded;
    case EStateRead::Unsupported: return ECinderCampaignLoadResult::Unsupported;
    default: return ECinderCampaignLoadResult::Corrupt;
    }
}
}

bool FCinderCampaignProgress::IsComplete(int32 Mission) const
{
    return Mission >= 0 && Mission < MissionCount && (CompletedMask & (uint32{1} << Mission)) != 0;
}

int32 FCinderCampaignProgress::RecommendedMission() const
{
    for (int32 Mission = 0; Mission < MissionCount; ++Mission) if (!IsComplete(Mission)) return Mission;
    return FMath::Clamp(LastPlayedMission, 0, MissionCount - 1);
}

bool FCinderCampaignProgress::Complete(int32 Mission, bool bAssisted, uint32 MasteryMask, uint64 CompletionTicks)
{
    if (Mission < 0 || Mission >= MissionCount) return false;
    const uint32 Bit = uint32{1} << Mission;
    CompletedMask |= Bit;
    if (bAssisted) AssistedMask |= Bit; else UnassistedMask |= Bit;
    MasteryMasks[Mission] |= MasteryMask;
    if (CompletionTicks && (!BestCompletionTicks[Mission] || CompletionTicks < BestCompletionTicks[Mission]))
        BestCompletionTicks[Mission] = CompletionTicks;
    LastPlayedMission = Mission;
    return true;
}

FCinderCampaignSave::FCinderCampaignSave(FString RootDirectory)
    : Root(FPaths::ConvertRelativePathToFull(std::move(RootDirectory)))
{
    RefreshDiskCheckpointCandidate();
}

FCinderCampaignSave::~FCinderCampaignSave() = default;

FString FCinderCampaignSave::DefaultRootDirectory()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Campaign"));
}

ECinderCampaignLoadResult FCinderCampaignSave::LoadProgress(FCinderCampaignProgress& OutProgress, FString& OutMessage) const
{
    FCinderCampaignProgress Candidate;
    ECinderCampaignLoadResult Result = ReadProgressFile(In(Root, ProgressFilename), Candidate);
    if (Result == ECinderCampaignLoadResult::Loaded)
    {
        OutProgress = Candidate; OutMessage.Empty(); return Result;
    }
    // A newer writer may have produced this file. Do not silently roll the
    // player back to an older backup that this build happens to understand.
    if (Result == ECinderCampaignLoadResult::Unsupported)
    {
        OutProgress = FCinderCampaignProgress{};
        OutMessage = TEXT("Campaign progress is from an unsupported version. Starting with safe local defaults.");
        return Result;
    }
    const ECinderCampaignLoadResult Backup = ReadProgressFile(In(Root, ProgressBackupFilename), Candidate);
    if (Backup == ECinderCampaignLoadResult::Loaded)
    {
        OutProgress = Candidate;
        OutMessage = TEXT("The newest campaign progress was unreadable; the previous valid progress was restored.");
        return Backup;
    }
    OutProgress = FCinderCampaignProgress{};
    if (Result == ECinderCampaignLoadResult::Unsupported || Backup == ECinderCampaignLoadResult::Unsupported)
    {
        OutMessage = TEXT("Campaign progress is from an unsupported version. Starting with safe local defaults.");
        return ECinderCampaignLoadResult::Unsupported;
    }
    if (Result == ECinderCampaignLoadResult::Corrupt || Backup == ECinderCampaignLoadResult::Corrupt)
    {
        OutMessage = TEXT("Campaign progress is corrupt. Starting with safe local defaults.");
        return ECinderCampaignLoadResult::Corrupt;
    }
    OutMessage.Empty();
    return ECinderCampaignLoadResult::NotFound;
}

bool FCinderCampaignSave::SaveProgress(const FCinderCampaignProgress& Progress, FString& OutError) const
{
    FCinderCampaignProgress Validated;
    if (ReadProgressObject(*ProgressJson(Progress), Validated) != EStateRead::Ok)
    {
        OutError = TEXT("Campaign progress contains invalid fields."); return false;
    }
    IFileManager::Get().MakeDirectory(*Root, true);
    const FString Current = In(Root, ProgressFilename);
    const FString Backup = In(Root, ProgressBackupFilename);
    const FString Temporary = Current + TEXT(".tmp");
    if (!WriteJsonFile(Temporary, ProgressJson(Progress)))
    {
        OutError = TEXT("Could not write campaign progress."); return false;
    }
    FCinderCampaignProgress Verify;
    if (ReadProgressFile(Temporary, Verify) != ECinderCampaignLoadResult::Loaded)
    {
        IFileManager::Get().Delete(*Temporary, false, true);
        OutError = TEXT("Campaign progress failed validation after writing."); return false;
    }
    IFileManager& Files = IFileManager::Get();
    FCinderCampaignProgress ExistingProgress;
    const ECinderCampaignLoadResult Existing = ReadProgressFile(Current, ExistingProgress);
    if (Existing == ECinderCampaignLoadResult::Unsupported)
    {
        Files.Delete(*Temporary, false, true);
        OutError = TEXT("Campaign progress was written by an unsupported version and was not overwritten.");
        return false;
    }
    if (Existing == ECinderCampaignLoadResult::Loaded)
    {
        Files.Delete(*Backup, false, true);
        if (!Files.Move(*Backup, *Current, true, true, false, true))
        {
            Files.Delete(*Temporary, false, true);
            OutError = TEXT("Could not preserve the previous campaign progress."); return false;
        }
    }
    else if (Files.FileExists(*Current)) Files.Delete(*Current, false, true);
    if (!MoveReplacing(Current, Temporary))
    {
        if (Existing == ECinderCampaignLoadResult::Loaded && Files.FileExists(*Backup)) MoveReplacing(Current, Backup);
        OutError = TEXT("Could not commit campaign progress."); return false;
    }
    OutError.Empty(); return true;
}

bool FCinderCampaignSave::RecordVictory(int32 Mission, bool bAssisted, uint32 MasteryMask,
    uint64 CompletionTicks, FCinderCampaignProgress& InOutProgress, FString& OutError) const
{
    FCinderCampaignProgress Candidate = InOutProgress;
    if (!Candidate.Complete(Mission, bAssisted, MasteryMask, CompletionTicks))
    {
        OutError = TEXT("Campaign mission index is invalid."); return false;
    }
    if (!SaveProgress(Candidate, OutError)) return false;
    InOutProgress = Candidate;
    return true;
}

bool FCinderCampaignSave::SaveCheckpoint(const cinder::Simulation& Sim, const FCinderCampaign& Campaign,
    FString& OutError, bool bPersist)
{
    const FCinderCampaignState State = Campaign.ExportState();
    if (!Campaign.IsRunning() || !Campaign.IsCheckpointSettled() || Campaign.CheckpointSerial() == 0 ||
        Campaign.CheckpointBoundary() != Campaign.Phase() || !FCinderCampaign::ValidateState(Sim, State, OutError))
    {
        if (OutError.IsEmpty()) OutError = TEXT("Campaign has no settled objective boundary to save.");
        return false;
    }
    // Build the replacement before touching the stored checkpoint. This also
    // handles SaveAttemptMetadata passing the current memory snapshot back into
    // this method; replacing the member first would dangle the Sim reference.
    TUniquePtr<cinder::Simulation> MemorySimulationCandidate = MakeUnique<cinder::Simulation>(Sim);
    TUniquePtr<FCinderCampaign> MemoryCampaignCandidate = MakeUnique<FCinderCampaign>();
    if (!MemoryCampaignCandidate->ImportState(*MemorySimulationCandidate, State, OutError)) return false;
    MemorySimulation = MoveTemp(MemorySimulationCandidate);
    MemoryCampaign = MoveTemp(MemoryCampaignCandidate);
    if (!bPersist) { OutError.Empty(); return true; }
    const cinder::Simulation& StoredSimulation = *MemorySimulation;

    IFileManager::Get().MakeDirectory(*Root, true);
    FGeneration Generations[2] = {LoadGeneration(Root, 0), LoadGeneration(Root, 1)};
    const bool bDifferentMission = (Valid(Generations[0]) && Generations[0].State.Mission != State.Mission) ||
        (Valid(Generations[1]) && Generations[1].State.Mission != State.Mission);
    if (bDifferentMission)
    {
        // Reaching this call proves the replacement mission initialized and its
        // first boundary validated. An older mission must never become the A/B
        // fallback for the new run.
        IFileManager& Files = IFileManager::Get();
        Files.Delete(*In(Root, ManifestFilename), false, true);
        for (int32 Generation = 0; Generation < 2; ++Generation)
        {
            Files.Delete(*In(Root, GenerationSimulation(Generation)), false, true);
            Files.Delete(*In(Root, GenerationDirector(Generation)), false, true);
            Generations[Generation] = FGeneration{};
            Generations[Generation].Index = Generation;
        }
    }
    int32 Active = -1; uint64 ManifestCommit = 0;
    if (!ReadManifest(Root, Active, ManifestCommit) || Active < 0 || Active > 1 ||
        !Valid(Generations[Active]) || Generations[Active].Commit != ManifestCommit)
    {
        if (Valid(Generations[0]) || Valid(Generations[1]))
            Active = !Valid(Generations[1]) || (Valid(Generations[0]) && Generations[0].Commit >= Generations[1].Commit) ? 0 : 1;
        else Active = -1;
    }
    const int32 Target = Active == 0 ? 1 : 0;
    const uint64 Commit = FMath::Max(Generations[0].Commit, Generations[1].Commit) + 1;
    const FString SimTemporary = In(Root, GenerationSimulation(Target) + TEXT(".tmp"));
    const FString DirectorTemporary = In(Root, GenerationDirector(Target) + TEXT(".tmp"));
    IFileManager::Get().Delete(*SimTemporary, false, true);
    IFileManager::Get().Delete(*DirectorTemporary, false, true);
    const FString ExternalSimTemporary = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*SimTemporary);
    if (!StoredSimulation.save(TCHAR_TO_UTF8(*ExternalSimTemporary)) ||
        !WriteJsonFile(DirectorTemporary, StateJson(State, StoredSimulation, Commit)))
    {
        IFileManager::Get().Delete(*SimTemporary, false, true);
        IFileManager::Get().Delete(*DirectorTemporary, false, true);
        OutError = TEXT("Could not write the campaign checkpoint candidate."); return false;
    }
    const FString FinalSim = In(Root, GenerationSimulation(Target));
    const FString FinalDirector = In(Root, GenerationDirector(Target));
    if (!MoveReplacing(FinalSim, SimTemporary) || !MoveReplacing(FinalDirector, DirectorTemporary))
    {
        OutError = TEXT("Could not stage the campaign checkpoint generation.");
        RefreshDiskCheckpointCandidate(); return false;
    }
    FGeneration Verify = LoadGeneration(Root, Target);
    if (!Valid(Verify) || Verify.Commit != Commit)
    {
        OutError = Verify.Error.IsEmpty() ? TEXT("Campaign checkpoint failed validation after writing.") : Verify.Error;
        RefreshDiskCheckpointCandidate(); return false;
    }
    if (!WriteManifest(Root, Target, Commit))
    {
        OutError = TEXT("Could not commit the campaign checkpoint manifest.");
        RefreshDiskCheckpointCandidate(); return false;
    }
    bHasDiskCandidate = true;
    OutError.Empty(); return true;
}

bool FCinderCampaignSave::SaveAttemptMetadata(const FCinderCampaign& CurrentCampaign,
    FString& OutError, bool bPersist)
{
    if (!HasMemoryCheckpoint())
    {
        if (!bPersist)
        {
            OutError = TEXT("No settled campaign checkpoint is available for attempt metadata.");
            return false;
        }
        cinder::Simulation DiskSimulation;
        FCinderCampaign DiskCampaign;
        FString LoadMessage;
        if (LoadCheckpoint(DiskSimulation, DiskCampaign, LoadMessage) != ECinderCampaignLoadResult::Loaded)
        {
            OutError = LoadMessage.IsEmpty() ?
                TEXT("No settled campaign checkpoint is available for attempt metadata.") : LoadMessage;
            return false;
        }
    }

    FCinderCampaignState Boundary = MemoryCampaign->ExportState();
    const FCinderCampaignState Current = CurrentCampaign.ExportState();
    if (!CurrentCampaign.IsRunning() || Current.Mission != Boundary.Mission || Current.Phase != Boundary.Phase)
    {
        OutError = TEXT("Attempt metadata does not belong to the stored campaign boundary.");
        return false;
    }
    Boundary.AssistanceMask |= Current.AssistanceMask;
    Boundary.CoachingLevel = FMath::Max(Boundary.CoachingLevel, FMath::Max(1, Current.CoachingLevel));
    FCinderCampaign UpdatedBoundary;
    if (!UpdatedBoundary.ImportState(*MemorySimulation, Boundary, OutError)) return false;
    return SaveCheckpoint(*MemorySimulation, UpdatedBoundary, OutError, bPersist);
}

ECinderCampaignLoadResult FCinderCampaignSave::LoadCheckpoint(cinder::Simulation& OutSim,
    FCinderCampaign& OutCampaign, FString& OutMessage)
{
    FGeneration Generations[2] = {LoadGeneration(Root, 0), LoadGeneration(Root, 1)};
    int32 Preferred = -1; uint64 ManifestCommit = 0;
    const bool bManifest = ReadManifest(Root, Preferred, ManifestCommit) && Preferred >= 0 && Preferred <= 1;
    if (bManifest && Generations[Preferred].Read == EStateRead::Unsupported)
    {
        bHasDiskCandidate = true;
        OutMessage = TEXT("Campaign checkpoint is from an unsupported version. Restart the mission.");
        return ECinderCampaignLoadResult::Unsupported;
    }
    const bool bCommittedPreferred = bManifest && Valid(Generations[Preferred]) &&
        Generations[Preferred].Commit == ManifestCommit;
    int32 Order[2] = {0, 1};
    if (bCommittedPreferred) { Order[0] = Preferred; Order[1] = 1 - Preferred; }
    else if (Valid(Generations[1]) && (!Valid(Generations[0]) || Generations[1].Commit > Generations[0].Commit))
    { Order[0] = 1; Order[1] = 0; }

    for (int32 Attempt = 0; Attempt < 2; ++Attempt)
    {
        FGeneration& Candidate = Generations[Order[Attempt]];
        if (!Valid(Candidate)) continue;
        FCinderCampaign CampaignCandidate;
        FString Error;
        if (!CampaignCandidate.ImportState(*Candidate.Simulation, Candidate.State, Error)) continue;
        OutSim = std::move(*Candidate.Simulation);
        OutCampaign = std::move(CampaignCandidate);
        MemorySimulation = MakeUnique<cinder::Simulation>(OutSim);
        MemoryCampaign = MakeUnique<FCinderCampaign>(OutCampaign);
        bHasDiskCandidate = true;
        const bool bUsedCommitted = bCommittedPreferred && Order[Attempt] == Preferred;
        OutMessage = bUsedCommitted ? FString() :
            TEXT("The newest campaign checkpoint was unreadable; the previous settled boundary was restored.");
        return ECinderCampaignLoadResult::Loaded;
    }

    RefreshDiskCheckpointCandidate();
    const bool bAnyFiles = IFileManager::Get().FileExists(*In(Root, GenerationDirector(0))) ||
        IFileManager::Get().FileExists(*In(Root, GenerationDirector(1))) ||
        IFileManager::Get().FileExists(*In(Root, ManifestFilename));
    if (!bAnyFiles) { OutMessage.Empty(); return ECinderCampaignLoadResult::NotFound; }
    if (Generations[0].Read == EStateRead::Unsupported || Generations[1].Read == EStateRead::Unsupported)
    {
        OutMessage = TEXT("Campaign checkpoint is from an unsupported version. Restart the mission.");
        return ECinderCampaignLoadResult::Unsupported;
    }
    OutMessage = TEXT("Campaign checkpoint is corrupt. Restart the mission.");
    return ECinderCampaignLoadResult::Corrupt;
}

bool FCinderCampaignSave::RestoreMemoryCheckpoint(cinder::Simulation& OutSim,
    FCinderCampaign& OutCampaign, FString& OutError) const
{
    if (!HasMemoryCheckpoint())
    {
        OutError = TEXT("No in-memory campaign checkpoint is available."); return false;
    }
    cinder::Simulation SimulationCandidate = *MemorySimulation;
    FCinderCampaign CampaignCandidate;
    const FCinderCampaignState State = MemoryCampaign->ExportState();
    if (!CampaignCandidate.ImportState(SimulationCandidate, State, OutError)) return false;
    OutSim = std::move(SimulationCandidate);
    OutCampaign = std::move(CampaignCandidate);
    OutError.Empty(); return true;
}

void FCinderCampaignSave::RefreshDiskCheckpointCandidate()
{
    bHasDiskCandidate = IFileManager::Get().FileExists(*In(Root, ManifestFilename)) ||
        (IFileManager::Get().FileExists(*In(Root, GenerationSimulation(0))) && IFileManager::Get().FileExists(*In(Root, GenerationDirector(0)))) ||
        (IFileManager::Get().FileExists(*In(Root, GenerationSimulation(1))) && IFileManager::Get().FileExists(*In(Root, GenerationDirector(1))));
}

void FCinderCampaignSave::ClearCheckpoint()
{
    MemorySimulation.Reset(); MemoryCampaign.Reset();
    IFileManager& Files = IFileManager::Get();
    Files.Delete(*In(Root, ManifestFilename), false, true);
    Files.Delete(*In(Root, FString(ManifestFilename) + TEXT(".tmp")), false, true);
    for (int32 Generation = 0; Generation < 2; ++Generation)
    {
        Files.Delete(*In(Root, GenerationSimulation(Generation)), false, true);
        Files.Delete(*In(Root, GenerationDirector(Generation)), false, true);
        Files.Delete(*In(Root, GenerationSimulation(Generation) + TEXT(".tmp")), false, true);
        Files.Delete(*In(Root, GenerationDirector(Generation) + TEXT(".tmp")), false, true);
    }
    bHasDiskCandidate = false;
}
