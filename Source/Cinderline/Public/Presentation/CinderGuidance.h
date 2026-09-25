#pragma once

#include "CoreMinimal.h"
#include "Sim/Simulation.h"

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

/** Read-only HUD state used to choose the next single guided action. */
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
    bool bPatrolCommand = false;
    bool bEscortCommand = false;
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
