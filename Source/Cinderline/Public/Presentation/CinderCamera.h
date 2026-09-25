#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "CinderCamera.generated.h"

class USpringArmComponent;
class UCameraComponent;
class ACinderBattlefield;

UCLASS()
class CINDERLINE_API ACinderCamera : public APawn
{
    GENERATED_BODY()
public:
    // Boom lengths. The opening view frames units large enough to read their
    // silhouettes at ordinary phone zoom; the closest view is a deliberate
    // inspection distance, not a normal playing position.
    static constexpr float DefaultDistance = 980.0f;
    static constexpr float ClosestDistance = 520.0f;
    static constexpr float FarthestDistance = 3200.0f;
    ACinderCamera();
    virtual void Tick(float DeltaSeconds) override;
    void Pan(FVector Delta);
    /** Position is a ground point; optional extents frame a complete ground-standing subject. */
    void Focus(FVector Position, bool bInstant = false, FVector FramingExtent = FVector::ZeroVector);
    /** Follow only the surface actually presented by this battlefield, including fog flattening. */
    void SetTerrainSource(ACinderBattlefield* Battlefield);
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
    FVector BoundPosition(FVector Position, float Distance, const FGroundFootprint& Footprint,
        bool bResolveSurface = true) const;
    void PositionBounds(float Distance, float Height, const FGroundFootprint& Footprint,
        FVector2D& Low, FVector2D& High, bool bUseFullBorder = false) const;
    bool FindFocusPosition(float Distance, const FGroundFootprint& Footprint, FVector& Position,
        bool bUseFullBorder = false) const;
    bool ContainsFocus(FVector Position, float Distance, const FGroundFootprint& Footprint) const;
    float MaximumDistance(const FGroundFootprint& Footprint, float Height = 0.0f) const;
    float MinimumSubjectDistance(const FGroundFootprint& Footprint) const;
    float SurfaceHeight(FVector Position) const;
    void UpdateTargetBounds(const FGroundFootprint& Footprint);
    UPROPERTY() TObjectPtr<USpringArmComponent> Boom;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(Transient) TWeakObjectPtr<ACinderBattlefield> TerrainSource;
    FVector TargetPosition = FVector(700, 700, 0);
    FVector FocusAnchor = FVector(700, 700, 0);
    FVector FocusHalfExtent = FVector::ZeroVector;
    bool bKeepFocusVisible = true;
    float TargetDistance = DefaultDistance;
    float ActiveWorldSize = 4800.0f;
};
