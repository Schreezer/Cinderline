#pragma once

#include "CoreMinimal.h"
#include "Sim/Simulation.h"

enum class ECinderTutorialStep : uint8
{
    Camera, SelectWorker, GatherOre, TrainWorker, BuildKiln, TrainEmbers,
    BuildSiphon, Scout, AttackMove, BuildResonator, ResearchWeapons,
    Reinforce, Defend, DestroyAnchor, Complete
};

enum class ECinderTutorialPrimaryAction : uint8
{
    FocusWorld,
    OpenBuild,
    OpenTrain,
    OpenResearch
};

struct FCinderTutorialText
{
    FString Title, Body, Hint, Progress;
    int32 HelpPage = 0;
};

enum class ECinderTutorialGuideTarget : uint8
{
    None,
    Button,
    Entity,
    Ground,
    Camera,
    Wait
};

/** Read-only HUD state used to choose the next single tutorial action. */
struct FCinderTutorialContext
{
    std::vector<cinder::Id> Selection;
    int32 Catalog = 0;
    cinder::Kind TrainKind = cinder::Kind::Resource;
    bool bTrainKindChosen = false;
    int32 TrainQuantity = 1;
    bool bTrainQuantityChosen = false;
    bool bCompact = false;
    bool bBuildMode = false;
    cinder::Kind BuildingKind = cinder::Kind::Resource;
    bool bAttackMove = false;
    bool bMoveCommand = false;
    bool bDefendCommand = false;
    bool bProductionRally = false;
    cinder::Id PinnedProducer = 0;
    int32 ArmyTab = 0;
};

/** One visible instruction and one exact arrow target. */
struct FCinderTutorialGuide
{
    FString Instruction;
    FString Explanation;
    ECinderTutorialGuideTarget Target = ECinderTutorialGuideTarget::None;
    FString ButtonAction;
    int32 ButtonArgument = 0;
    cinder::Id ButtonEntity = 0;
    cinder::Id Entity = 0;
    cinder::Vec2 Point = {};
    int32 ActionIndex = 0;
    int32 ActionCount = 0;
    bool bWaiting = false;
};

/** Practice objectives observe normal rules and accepted player commands. */
class CINDERLINE_API FCinderTutorial
{
public:
    static constexpr uint32 Seed = 0x7A170A;
    static constexpr int32 StepCount = static_cast<int32>(ECinderTutorialStep::Complete);
    static cinder::Vec2 ScoutPoint() { return {1400, 1100}; }
    static cinder::Vec2 CombatPoint() { return {1850, 1950}; }
    static cinder::Vec2 DefensePoint() { return {1050, 1050}; }
    static cinder::Vec2 EnemyAnchorPoint() { return {4200, 4200}; }
    bool InitializeScenario(cinder::Simulation& Sim);
    void Start(const cinder::Simulation& Sim, cinder::Id PracticeTarget);
    void Reset();
    void TickOpponent(cinder::Simulation& Sim);
    void Observe(const cinder::Simulation& Sim, const std::vector<cinder::Id>& Selection);
    void AcceptedCommand(const cinder::Simulation& Sim, const cinder::Command& Command);
    void CameraInput();
    bool IsActive() const { return bActive; }
    bool IsComplete() const { return bActive && CurrentStep == ECinderTutorialStep::Complete; }
    ECinderTutorialStep Step() const { return CurrentStep; }
    int32 OpponentOrdersIssued() const { return OpponentOrderCount; }
    cinder::Id PracticeTarget() const { return Target; }
    FCinderTutorialText Text(const cinder::Simulation& Sim, bool bTouch) const;
    FCinderTutorialGuide Guide(const cinder::Simulation& Sim,
        const FCinderTutorialContext& Context, bool bTouch) const;
    ECinderTutorialPrimaryAction PrimaryAction(const cinder::Simulation& Sim) const;
    bool FocusPoint(const cinder::Simulation& Sim, cinder::Vec2& Out,
        cinder::Id* OutEntity = nullptr) const;

private:
    void Advance();
    bool bActive = false;
    ECinderTutorialStep CurrentStep = ECinderTutorialStep::Camera;
    bool Completed[StepCount] = {};
    bool bGatherOrdered = false, bWorkerOrdered = false, bEmbersOrdered = false;
    bool bScoutTrainOrdered = false, bScoutOrdered = false, bArmyAttackMoveOrdered = false;
    bool bKilnOrdered = false, bSiphonOrdered = false, bResonatorOrdered = false;
    bool bWeaponsOrdered = false, bRallyOrdered = false;
    bool bScenarioInitialized = false, bOpponentActivated = false;
    bool bRaidLaunched = false, bDefenderHeld = false;
    bool bDefendAttackMoveOrdered = false, bDestroyAttackMoveOrdered = false;
    cinder::Id MiningWorker = 0, Target = 0, EnemyFoundry = 0, Defender = 0;
    const cinder::Simulation* ScenarioSimulation = nullptr;
    std::vector<cinder::Id> OrderedScouts;
    std::vector<cinder::Id> OpponentUnits;
    std::vector<cinder::Id> RaidUnits;
    float LastMiningTimer = 0, LastCarried = 0;
    float NextOpponentDecision = 0;
    std::uint64_t LastScenarioTick = 0;
    int32 WorkerCount = 0, EmberCount = 0, EmberOrdersAccepted = 0;
    int32 OpponentTrainOrders = 0, OpponentOrderCount = 0;
    int32 WorkerProducedAtOrder = -1, EmberProducedAtOrder = -1;
    int32 UpgradesAtWeaponOrder = -1;
    float KilnProgress = 0, SiphonProgress = 0, ResonatorProgress = 0;
    cinder::Vec2 Base = {600, 600};
    mutable bool bGuideSiteCached = false;
    mutable cinder::Kind GuideSiteKind = cinder::Kind::Resource;
    mutable cinder::Vec2 GuideSite = {};
    mutable std::uint64_t GuideSiteValidatedTick = 0;
    mutable cinder::Id GuideRecoverySite = 0;
};
