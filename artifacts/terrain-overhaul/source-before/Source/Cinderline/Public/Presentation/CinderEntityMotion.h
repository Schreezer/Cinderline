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
    int32 Team = 0;
    cinder::Vec2 Position;
    cinder::Order Order = cinder::Order::Idle;
    float Facing = 0;
    float Cooldown = 0;
    float HarvestTimer = 0;
    bool bReturning = false;
    bool bConstructionActive = false;
    // Observed health, used only to edge-detect a drop for the damage flinch.
    // MaxHp additionally scales that flinch by chassis weight; zero is tolerated
    // and simply produces the lightest scaling.
    float Hp = 0;
    float MaxHp = 0;
    // Bearing to the entity's current target in world radians, valid only while
    // bHasTarget. The weapon is the single part allowed to point away from the
    // authoritative facing, and only inside its chassis traverse arc.
    float TargetBearing = 0;
    bool bHasTarget = false;
    // LOCAL VIEW STATE, never authoritative. Selection exists on one client's
    // screen only: two clients watching the same replay legitimately produce
    // different poses from this flag. Nothing derived from it may ever reach
    // cinder::Simulation, the state hash, a save or a network snapshot, and it
    // must never be folded into an observation hash by a later change.
    bool bSelected = false;
    uint64 Tick = 0;
    float Time = 0;
};

/**
 * Per-entity presentation memory. Threaded explicitly through the pose
 * calculation rather than read from a member, exactly as RecoilStartedAt was,
 * so the pose stays a pure function of (observation, previous, memory) and can
 * be exercised by automation with no cache, no world and no asset load.
 */
struct FCinderMotionMemory
{
    float RecoilStartedAt = -1;
    float FlinchStartedAt = -1;
    float FlinchAmount = 0;
    float BirthAt = -1;
    /** Smoothed heading the hull is rendered at; the authoritative facing is never altered. */
    float RenderFacing = 0;
    float FacingRate = 0;
    /** Integrated gait angle, so cadence can change without the stride popping. */
    float GaitPhase = 0;
    float WeaponYaw = 0;
};

/** An entity that stopped being observed while its cell was still visible. */
struct FCinderDyingEntity
{
    cinder::Id Id = 0;
    cinder::Kind Kind = cinder::Kind::Worker;
    int32 Team = 0;
    cinder::Vec2 Position;
    float Facing = 0;
    float StartedAt = 0;
};

/** Local cosmetic transforms derived only from observed authoritative samples. */
struct FCinderEntityPose
{
    bool bMoving = false;
    bool bToolActive = false;
    bool bRecoil = false;
    float BodyZ = 0;
    /** Birth scale-in and nothing else; presentation may scale, never displace the XY root. */
    float UniformScale = 1.0f;
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
    /** A toppling hull is held for this long, then dropped from the ring. */
    static constexpr float DeathDuration = 0.7f;
    /** Hard bound on concurrent topples: 24 x at most 6 parts of extra transforms into existing batches. */
    static constexpr int32 MaxDyingEntities = 24;

    void BeginFrame();
    const FCinderEntityPose& Observe(const FCinderMotionObservation& Observation);
    /** Legacy boundary: evicts unobserved entities and records nothing. */
    void EndFrame();
    /** Expires finished topples on the presentation clock; still records nothing new. */
    void EndFrame(float Time);
    /**
     * The only overload that may record a death. An entity that stopped being
     * observed because its cell went dark is merely fogged, and toppling it
     * would leak the fact that something died out of sight; only a still-visible
     * cell that no longer reports the entity is a kill the viewer has earned.
     */
    void EndFrame(float Time, TFunctionRef<bool(const cinder::Vec2&)> IsVisible);
    void Reset();
    int32 CachedEntities() const { return Samples.Num(); }
    bool HasSample(cinder::Id Id) const { return Samples.Contains(Id); }
    TConstArrayView<FCinderDyingEntity> DyingEntities() const { return Dying; }

    /**
     * Pure pose calculation used by automation without a world or asset load.
     * AdvancedMemory, when supplied, receives the integrated state (gait phase,
     * render facing, weapon yaw) that the caller must store back for the next
     * frame; integrated channels stall rather than misbehave when it is null.
     */
    static FCinderEntityPose CalculatePose(const FCinderMotionObservation& Current,
        const FCinderMotionObservation* Previous, const FCinderMotionMemory& Memory,
        FCinderMotionMemory* AdvancedMemory = nullptr);
    /**
     * Retained memoryless form. Every call site and test that predates the
     * motion memory keeps compiling and keeps its exact behaviour: the render
     * facing is seeded to the authoritative facing, so the hull leads nothing.
     */
    static FCinderEntityPose CalculatePose(const FCinderMotionObservation& Current,
        const FCinderMotionObservation* Previous, float RecoilStartedAt = -1);
    /** Recoil envelope over normalised impulse time; exposed so its sign can be asserted. */
    static float RecoilImpulse(float Alpha);
    static FCinderEntityPose CalculateDeathPose(const FCinderDyingEntity& Entry, float Time);

private:
    struct FSample
    {
        FCinderMotionObservation Observation;
        FCinderEntityPose Pose;
        FCinderMotionMemory Memory;
        bool bSeen = false;
    };
    void EvictUnseen(float Time, const TFunctionRef<bool(const cinder::Vec2&)>* IsVisible);
    TMap<cinder::Id, FSample> Samples;
    TArray<FCinderDyingEntity> Dying;
    /**
     * cinder assigns ids from a strictly monotonic counter (e.id = nextId_++),
     * so an id above the high water mark has never been seen by this client and
     * is genuinely new. Anything at or below it is an existing unit walking back
     * into vision, which must reveal rather than be born.
     */
    uint64 HighWaterId = 0;
};

/** Rotate a centimeter-space part mesh around its manifest pivot, then place it at the authoritative root. */
CINDERLINE_API FTransform CinderPartWorldTransform(const FVector& Root, const FQuat& Facing,
    const FVector& Pivot, const FRotator& LocalRotation, const FVector& LocalOffset = FVector::ZeroVector,
    const FVector& Scale = FVector::OneVector);
