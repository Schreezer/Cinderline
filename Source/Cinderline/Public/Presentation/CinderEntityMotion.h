#pragma once

#include "CoreMinimal.h"
#include "Sim/Simulation.h"

enum class ECinderMotionPart : uint8
{
    Body,
    LegFrontLeft,
    LegFrontRight,
    LegRearLeft,
    LegRearRight,
    Weapon,
    Tool,
    Count
};

struct FCinderMotionAssetPart
{
    ECinderMotionPart Part = ECinderMotionPart::Body;
    const TCHAR* AssetName = nullptr;
    FVector Pivot = FVector::ZeroVector;
};

/** Generated from RawAssets/Motion/manifest.json; runtime never reads authoring files. */
CINDERLINE_API TConstArrayView<FCinderMotionAssetPart> CinderMotionAssetParts(cinder::Kind Kind);

struct FCinderMotionObservation
{
    cinder::Id Id = 0;
    cinder::Kind Kind = cinder::Kind::Worker;
    cinder::Vec2 Position;
    cinder::Order Order = cinder::Order::Idle;
    float Facing = 0;
    float Cooldown = 0;
    float HarvestTimer = 0;
    bool bReturning = false;
    bool bConstructionActive = false;
    uint64 Tick = 0;
    float Time = 0;
};

/** Local cosmetic transforms derived only from observed authoritative samples. */
struct FCinderEntityPose
{
    bool bMoving = false;
    bool bToolActive = false;
    bool bRecoil = false;
    float BodyZ = 0;
    FRotator BodyRotation = FRotator::ZeroRotator;
    FRotator LegFrontLeftRotation = FRotator::ZeroRotator;
    FRotator LegFrontRightRotation = FRotator::ZeroRotator;
    FRotator LegRearLeftRotation = FRotator::ZeroRotator;
    FRotator LegRearRightRotation = FRotator::ZeroRotator;
    FRotator WeaponRotation = FRotator::ZeroRotator;
    FVector WeaponOffset = FVector::ZeroVector;
    FRotator ToolRotation = FRotator::ZeroRotator;

    FRotator Rotation(ECinderMotionPart Part) const;
    FVector Offset(ECinderMotionPart Part) const;
};

class CINDERLINE_API FCinderEntityMotion
{
public:
    void BeginFrame();
    const FCinderEntityPose& Observe(const FCinderMotionObservation& Observation);
    void EndFrame();
    void Reset();
    int32 CachedEntities() const { return Samples.Num(); }
    bool HasSample(cinder::Id Id) const { return Samples.Contains(Id); }

    /** Pure pose calculation used by automation without a world or asset load. */
    static FCinderEntityPose CalculatePose(const FCinderMotionObservation& Current,
        const FCinderMotionObservation* Previous, float RecoilStartedAt = -1);

private:
    struct FSample
    {
        FCinderMotionObservation Observation;
        FCinderEntityPose Pose;
        float RecoilStartedAt = -1;
        bool bSeen = false;
    };
    TMap<cinder::Id, FSample> Samples;
};

/** Rotate a centimeter-space part mesh around its manifest pivot, then place it at the authoritative root. */
CINDERLINE_API FTransform CinderPartWorldTransform(const FVector& Root, const FQuat& Facing,
    const FVector& Pivot, const FRotator& LocalRotation, const FVector& LocalOffset = FVector::ZeroVector,
    const FVector& Scale = FVector::OneVector);
