#pragma once

#include "CoreMinimal.h"
#include "Sim/Simulation.h"

#include <cstdint>
#include <vector>

struct FCinderTutorialContext;
struct FCinderTutorialGuide;

enum class ECinderCampaignOutcome : uint8
{
    Inactive,
    Running,
    Victory,
    Defeat,
    Draw
};

enum class ECinderCampaignActionKind : uint8
{
    None,
    Camera,
    SelectWorker,
    Gather,
    Train,
    Build,
    Rally,
    Move,
    AttackMove,
    Defend,
    Research,
    Explore,
    Destroy,
    Wait
};

enum class ECinderCampaignTargetRole : uint8
{
    None,
    PlayerAnchor,
    HomeOre,
    RemoteOre,
    BuildSite,
    ScoutZoneA,
    ScoutZoneB,
    ForwardApproach,
    HomeDefense,
    EnemyObjective
};

struct FCinderCampaignMissionDefinition
{
    FString Name;
    FString Story;
    FString Skills;
    int32 PhaseCount = 0;
    int32 Map = 0;
    cinder::MatchLength MatchLength = cinder::MatchLength::Short;
    bool bNormalAI = false;
    FString Outcome1;
    FString Outcome2;
    FString Outcome3;
};

struct FCinderCampaignPhaseDefinition
{
    FString Title;
    FString Body;
    FString Hint;
    FString Progress;
    ECinderCampaignActionKind Action = ECinderCampaignActionKind::None;
    ECinderCampaignTargetRole Target = ECinderCampaignTargetRole::None;
    cinder::Kind Kind = cinder::Kind::Resource;
    int32 RequiredCount = 0;
    int32 ResearchIndex = -1;
    bool bAcceptedCommandRequired = false;
};

struct FCinderCampaignText
{
    FString Title;
    FString Body;
    FString Hint;
    FString Progress;
    int32 Mission = -1;
    int32 Phase = 0;
    int32 PhaseCount = 0;
    bool bAssisted = false;
};

struct FCinderCampaignAuthoredIds
{
    cinder::Id PlayerAnchor = 0;
    cinder::Id EnemyAnchor = 0;
    cinder::Id MiningWorker = 0;
    cinder::Id RallyWorker = 0;
    cinder::Id Kiln = 0;
    cinder::Id PlayerSiphon = 0;
    cinder::Id RemoteSiphon = 0;
    cinder::Id Resonator = 0;
    cinder::Id MotorPool = 0;
    cinder::Id RaidProducer = 0;
    std::vector<cinder::Id> HomeOre;
    std::vector<cinder::Id> RemoteOre;
    std::vector<cinder::Id> PracticePatrol;
    std::vector<cinder::Id> ScoutPosts;
    std::vector<cinder::Id> ForwardThreats;
    std::vector<cinder::Id> SiegeGuard;
    std::vector<cinder::Id> SiegeProducers;
    std::vector<cinder::Id> OrderedUnits;
    std::vector<cinder::Id> ActiveWave;
};

struct FCinderCampaignCounters
{
    int32 MissionStartProduced = 0;
    int32 MissionStartBuilt = 0;
    int32 MissionStartGathered = 0;
    int32 MissionStartUpgrades = 0;
    int32 PhaseStartProduced = 0;
    int32 PhaseStartBuilt = 0;
    int32 PhaseStartGathered = 0;
    int32 PhaseStartUpgrades = 0;
    int32 PhaseStartWorkers = 0;
    int32 PhaseStartEmbers = 0;
    int32 PhaseStartSiphons = 0;
    int32 PhaseStartResonators = 0;
    int32 PhaseStartMotorPools = 0;
    int32 PhaseStartAnvils = 0;
    int32 OpponentTrainOrders = 0;
    int32 WaveIndex = 0;
    int32 StableTicks = 0;
    float LastTrackedCarried = 0.0f;
};

/** Serializable director state. Simulation state is stored separately in its normal v13 save. */
struct FCinderCampaignState
{
    uint32 SchemaVersion = 1;
    uint32 ContentVersion = 1;
    int32 Mission = -1;
    int32 Phase = 0;
    ECinderCampaignOutcome Outcome = ECinderCampaignOutcome::Inactive;
    FString FailureCode;
    std::uint64_t MissionStartTick = 0;
    std::uint64_t PhaseStartTick = 0;
    std::uint64_t ObjectiveStartTick = 0;
    std::uint64_t NextOpponentDecisionTick = 0;
    uint32 CheckpointSerial = 0;
    int32 CheckpointPhase = 0;
    std::uint64_t AcceptedCommandMask = 0;
    std::uint64_t PhaseCommandMask = 0;
    std::uint64_t CompletedObjectiveMask = 0;
    std::uint64_t OptionalMask = 0;
    std::uint64_t AssistanceMask = 0;
    int32 CoachingLevel = 0;
    FCinderCampaignAuthoredIds Authored;
    FCinderCampaignCounters Counters;
    cinder::Vec2 BasePoint{};
    cinder::Vec2 HomeOrePoint{};
    cinder::Vec2 EnemyObjectivePoint{};
    cinder::Vec2 LocalBuildPoint{};
    cinder::Vec2 RemotePatchPoint{};
    cinder::Vec2 BuildPoint{};
    cinder::Vec2 ScoutZoneA{};
    cinder::Vec2 ScoutZoneB{};
    cinder::Vec2 ForwardApproach{};
    cinder::Vec2 HomeDefense{};
    bool bCameraObserved = false;
    bool bAnchorOreRallyAccepted = false;
    bool bArmyRallyAccepted = false;
    bool bGatherDeliveryObserved = false;
    bool bRallyWorkerAssigned = false;
    bool bRemoteWorkerCarried = false;
    bool bRemoteDeliveryObserved = false;
    bool bScoutAObserved = false;
    bool bScoutBObserved = false;
    bool bWaveProductionFinished = false;
    bool bCheckpointSettled = false;
    cinder::Id HighestSetupEntityId = 0;
    cinder::Id RallyBaselineEntityId = 0;
};

/** Campaign-only scenario director. It never owns or retains a Simulation pointer. */
class CINDERLINE_API FCinderCampaign
{
public:
    static constexpr uint32 StateSchemaVersion = 1;
    static constexpr uint32 ContentVersion = 1;
    static constexpr int32 MissionCount = 6;
    static constexpr std::uint64_t WavePreparationTicks = 200;

    static bool ExpectedConfig(int32 Mission, cinder::Config& OutConfig);
    static const FCinderCampaignMissionDefinition& Definition(int32 Mission);

    bool InitializeMission(cinder::Simulation& Sim, int32 Mission);
    void Reset();
    void Observe(const cinder::Simulation& Sim, const std::vector<cinder::Id>& Selection);
    void AcceptedCommand(const cinder::Simulation& Sim, const cinder::Command& Command);
    void CameraInput();
    void RequestHint();
    void TickOpponent(cinder::Simulation& Sim);

    bool IsActive() const { return State.Outcome != ECinderCampaignOutcome::Inactive; }
    bool IsRunning() const { return State.Outcome == ECinderCampaignOutcome::Running; }
    bool IsTerminal() const { return State.Outcome == ECinderCampaignOutcome::Victory ||
        State.Outcome == ECinderCampaignOutcome::Defeat || State.Outcome == ECinderCampaignOutcome::Draw; }
    int32 MissionIndex() const { return State.Mission; }
    int32 Phase() const { return State.Phase; }
    int32 PhaseCount() const;
    ECinderCampaignOutcome Outcome() const { return State.Outcome; }
    bool WasAssisted() const { return State.AssistanceMask != 0; }
    bool IsCurrentPhaseAssisted() const { return State.Phase >= 0 && State.Phase < 64 &&
        (State.AssistanceMask & (std::uint64_t{1} << State.Phase)) != 0; }
    uint32 OptionalOutcomes() const { return static_cast<uint32>(State.OptionalMask); }
    const FString& FailureCode() const { return State.FailureCode; }
    const FCinderCampaignPhaseDefinition& PhaseDefinition() const;
    const FCinderCampaignAuthoredIds& Authored() const { return State.Authored; }
    cinder::Vec2 TargetPoint(ECinderCampaignTargetRole Role) const;

    uint32 CheckpointSerial() const { return State.CheckpointSerial; }
    int32 CheckpointBoundary() const { return State.CheckpointPhase; }
    bool IsCheckpointSettled() const { return State.bCheckpointSettled; }

    FCinderCampaignState ExportState() const { return State; }
    static bool ValidateState(const cinder::Simulation& Sim, const FCinderCampaignState& Candidate,
        FString& OutError);
    bool ImportState(const cinder::Simulation& Sim, const FCinderCampaignState& Candidate,
        FString& OutError);

    FCinderCampaignText Text(const cinder::Simulation& Sim) const;
    FCinderTutorialGuide Guide(const cinder::Simulation& Sim,
        const FCinderTutorialContext& Context, bool bTouch) const;

private:
    static constexpr std::uint64_t CommandBit(cinder::CommandType Type)
    {
        return std::uint64_t{1} << static_cast<int32>(Type);
    }
    void Advance(const cinder::Simulation& Sim);
    void Finish(ECinderCampaignOutcome NewOutcome, const FString& Reason = FString());
    bool SetupScenario(cinder::Simulation& Sim);
    void CapturePhaseBaseline(const cinder::Simulation& Sim);
    void TrackProducedIds(const cinder::Simulation& Sim);
    void TickWaves(cinder::Simulation& Sim);

    FCinderCampaignState State;
    mutable bool bGuideSiteCached = false;
    mutable cinder::Kind GuideSiteKind = cinder::Kind::Resource;
    mutable cinder::Vec2 GuideSite{};
    mutable std::uint64_t GuideSiteValidatedTick = 0;
    mutable cinder::Id GuideRecoverySite = 0;
    std::uint64_t NextRemoteRetargetAttemptTick = 0;
};
