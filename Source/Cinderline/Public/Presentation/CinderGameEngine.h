#pragma once

#include "CoreMinimal.h"
#include "Engine/GameEngine.h"
#include "CinderGameEngine.generated.h"

class ACinderBattlefield;

enum class ECinderFramePacingState : uint8
{
    Bypass,
    Gameplay,
    Idle,
    Background
};

/** Limits desktop presentation work without changing the user's frame-rate setting. */
UCLASS()
class CINDERLINE_API UCinderGameEngine : public UGameEngine
{
    GENERATED_BODY()

public:
    virtual float GetMaxTickRate(float DeltaTime, bool bAllowFrameRateSmoothing = true) const override;

    ECinderFramePacingState GetFramePacingState() const;
    static ECinderFramePacingState ClassifyMatchState(const ACinderBattlefield* Battlefield, bool bForeground);
    static float ApplyStateCap(float EngineLimit, ECinderFramePacingState State);

private:
    mutable bool bReportedFrameState = false;
    mutable ECinderFramePacingState LastReportedFrameState = ECinderFramePacingState::Bypass;
};
