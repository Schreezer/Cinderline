#pragma once

#include "CoreMinimal.h"
#include "Sim/Simulation.h"

enum class ECinderTutorialStep : uint8
{
    Camera, SelectWorker, GatherOre, TrainWorker, BuildKiln, TrainEmbers,
    BuildSiphon, Scout, AttackMove, Complete
};

struct FCinderTutorialText
{
    FString Title, Body, Hint, Progress;
    int32 HelpPage = 0;
};

/** Practice objectives observe normal rules and accepted player commands. */
class CINDERLINE_API FCinderTutorial
{
public:
    static constexpr uint32 Seed = 0x7A170A;
    static constexpr int32 StepCount = static_cast<int32>(ECinderTutorialStep::Complete);
    static cinder::Vec2 ScoutPoint() { return {1400, 1100}; }
    static cinder::Vec2 CombatPoint() { return {1850, 1950}; }
    void Start(const cinder::Simulation& Sim, cinder::Id PracticeTarget);
    void Reset();
    void Observe(const cinder::Simulation& Sim, const std::vector<cinder::Id>& Selection);
    void AcceptedCommand(const cinder::Simulation& Sim, const cinder::Command& Command);
    void CameraInput();
    bool IsActive() const { return bActive; }
    bool IsComplete() const { return bActive && CurrentStep == ECinderTutorialStep::Complete; }
    ECinderTutorialStep Step() const { return CurrentStep; }
    FCinderTutorialText Text(const cinder::Simulation& Sim, bool bTouch) const;
    bool FocusPoint(const cinder::Simulation& Sim, cinder::Vec2& Out) const;

private:
    void Advance();
    bool bActive = false;
    ECinderTutorialStep CurrentStep = ECinderTutorialStep::Camera;
    bool Completed[StepCount] = {};
    bool bGatherOrdered = false, bWorkerOrdered = false, bEmbersOrdered = false;
    bool bScoutOrdered = false, bArmyAttackMoveOrdered = false;
    bool bKilnOrdered = false, bSiphonOrdered = false;
    cinder::Id MiningWorker = 0, Target = 0;
    std::vector<cinder::Id> OrderedScouts;
    float LastMiningTimer = 0, LastCarried = 0;
    int32 WorkerCount = 0, EmberCount = 0, EmberOrdersAccepted = 0;
    int32 WorkerProducedAtOrder = -1, EmberProducedAtOrder = -1;
    float KilnProgress = 0, SiphonProgress = 0;
    cinder::Vec2 Base = {600, 600};
};
