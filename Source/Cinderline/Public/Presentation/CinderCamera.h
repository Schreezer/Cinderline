#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "CinderCamera.generated.h"

class USpringArmComponent;
class UCameraComponent;

UCLASS()
class CINDERLINE_API ACinderCamera : public APawn
{
    GENERATED_BODY()
public:
    ACinderCamera();
    virtual void Tick(float DeltaSeconds) override;
    void Pan(FVector Delta);
    void Focus(FVector Position, bool bInstant = false);
    void Zoom(float Amount);
    /** Update playable bounds after a reset, load, tutorial, or network snapshot. */
    void SetWorldSize(float InWorldSize);
    float Distance() const { return TargetDistance; }
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FCinderCameraBoundaryTest;
#endif
    struct FGroundFootprint
    {
        FVector Forward, Right, Up;
        FVector2D Min, Max;
        double TanHalfX = 0, TanHalfY = 0;
        bool bValid = false;
    };
    FGroundFootprint GroundFootprint() const;
    FVector BoundPosition(FVector Position, float Distance, const FGroundFootprint& Footprint) const;
    bool ContainsFocus(FVector Position, float Distance, const FGroundFootprint& Footprint) const;
    float MaximumDistance(const FGroundFootprint& Footprint) const;
    void UpdateTargetBounds(const FGroundFootprint& Footprint);
    UPROPERTY() TObjectPtr<USpringArmComponent> Boom;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    FVector TargetPosition = FVector(700, 700, 0);
    FVector FocusAnchor = FVector(700, 700, 0);
    bool bKeepFocusVisible = true;
    float TargetDistance = 1650;
    float ActiveWorldSize = 4800.0f;
};
