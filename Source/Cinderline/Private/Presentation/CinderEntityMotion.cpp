#include "Presentation/CinderEntityMotion.h"

namespace
{
// Rate-relative movement gate. The presentation clock now runs at the display
// rate rather than the fixed 0.05s simulation step, so a per-frame distance
// threshold would silently re-classify motion every time the frame time moved:
// the same unit would walk at 30fps and stand still at 60. 100 cm^2/s^2 is
// 10 cm/s, which is exactly the old 0.25 cm^2 measured over one 0.05s step.
constexpr float MovementSpeedEpsilonSquared = 100.0f;
constexpr float RecoilDuration = 0.22f;
constexpr float FlinchDuration = 0.18f;
constexpr float BirthDuration = 0.35f;
// Stride rate of a unit running at its full definition speed. Everything slower
// scales down from here so a Skim (speed 245) and a Cinderthrow (speed 86) both
// plant a foot per metre travelled instead of per second elapsed.
constexpr float WalkCadence = 8.4f;
// Facing follower. 9 rad/s reaches a 90 degree turn in roughly 0.18s, and a
// damping ratio of 0.78 leaves a slight settle rather than a dead stop, which
// is what reads as mass. Explicit Euler on a second order system diverges once
// the step passes ~2*Zeta/W (0.17s), so the integrator step is clamped well
// under that: one hitched frame must never make a hull spin.
constexpr float FacingStiffness = 9.0f;
constexpr float FacingDamping = 0.78f;
constexpr float MaxFollowerStep = 0.05f;
constexpr float WeaponSlewDegreesPerSecond = 140.0f;
// Idle life. Nothing on the field should sit perfectly still, but an idle unit
// must never be mistakable for a moving one, so every amplitude here stays far
// below the walk cycle (which rises 4.2cm and swings legs 30 degrees) and the
// frequencies are slow enough to read as settling rather than juddering. Each
// unit's phase is offset by its id so a standing army breathes out of step
// instead of pulsing in unison. The rise and the sweep are sized against the
// effective ~1.0 px/cm the default camera gives after MetalFX resolves at 80%:
// the previous 0.45cm rise was literally half a pixel and could not be seen.
// IdleBodyTilt is deliberately barely moved, because roll is the one idle
// channel that was already at the edge of reading as a stumble.
constexpr float IdleBodyRise = 1.15f;
constexpr float IdleBodyTilt = 0.38f;
constexpr float IdleWeaponSweep = 6.0f;
// Selection is local view state, so it may only ever widen a purely cosmetic
// channel. 1.2 keeps the lifted breath inside the idle budget above.
constexpr float SelectedIdleLift = 1.2f;

const FCinderMotionAssetPart DrudgeParts[] = {
    {ECinderMotionPart::Body, TEXT("SM_Drudge_Body"), FVector::ZeroVector},
    {ECinderMotionPart::LegFrontLeft, TEXT("SM_Drudge_LegFL"), FVector(1.88624, -4.67572, 14.39791)},
    {ECinderMotionPart::LegFrontRight, TEXT("SM_Drudge_LegFR"), FVector(1.88624, 4.67572, 14.39791)},
    {ECinderMotionPart::LegRearLeft, TEXT("SM_Drudge_LegRL"), FVector(-5.61366, -4.67572, 14.39791)},
    {ECinderMotionPart::LegRearRight, TEXT("SM_Drudge_LegRR"), FVector(-5.61366, 4.67572, 14.39791)},
    {ECinderMotionPart::Tool, TEXT("SM_Drudge_Tools"), FVector(6.71087, 0, 17.01571)},
};
const FCinderMotionAssetPart EmberParts[] = {
    {ECinderMotionPart::Body, TEXT("SM_Ember_Body"), FVector::ZeroVector},
    {ECinderMotionPart::LegFrontLeft, TEXT("SM_Ember_LegL"), FVector(-6.41142, -7.38656, 20.70588)},
    {ECinderMotionPart::LegFrontRight, TEXT("SM_Ember_LegR"), FVector(-6.41142, 7.38656, 20.70588)},
    {ECinderMotionPart::Weapon, TEXT("SM_Ember_Weapon"), FVector(0.37656, 8.63925, 33.88235)},
};
const FCinderMotionAssetPart NeedleParts[] = {
    {ECinderMotionPart::Body, TEXT("SM_Needle_Body"), FVector::ZeroVector},
    {ECinderMotionPart::LegFrontLeft, TEXT("SM_Needle_LegL"), FVector(-7.21718, -5.69932, 19.04937)},
    {ECinderMotionPart::LegFrontRight, TEXT("SM_Needle_LegR"), FVector(-7.21718, 5.69932, 19.04937)},
    {ECinderMotionPart::Weapon, TEXT("SM_Needle_Weapon"), FVector(-3.95943, 0, 36.36699)},
};
const FCinderMotionAssetPart AnvilParts[] = {
    {ECinderMotionPart::Body, TEXT("SM_Anvil_Body"), FVector::ZeroVector},
    {ECinderMotionPart::Weapon, TEXT("SM_Anvil_Weapon"), FVector(7.00725, 0, 30.74896)},
};
const FCinderMotionAssetPart CinderthrowParts[] = {
    {ECinderMotionPart::Body, TEXT("SM_Cinderthrow_Body"), FVector::ZeroVector},
    {ECinderMotionPart::Weapon, TEXT("SM_Cinderthrow_Weapon"), FVector(-11.99535, 0, 25.97186)},
};

bool IsTracked(cinder::Kind Kind)
{
    return Kind == cinder::Kind::Bastion || Kind == cinder::Kind::Mortar;
}

TConstArrayView<FCinderMotionAssetPart> MotionAssetPartsInternal(cinder::Kind Kind)
{
    switch (Kind)
    {
    case cinder::Kind::Worker: return MakeArrayView(DrudgeParts);
    case cinder::Kind::Striker: return MakeArrayView(EmberParts);
    case cinder::Kind::Lancer: return MakeArrayView(NeedleParts);
    case cinder::Kind::Bastion: return MakeArrayView(AnvilParts);
    case cinder::Kind::Mortar: return MakeArrayView(CinderthrowParts);
    default: return {};
    }
}

bool IsHoverUnit(cinder::Kind Kind)
{
    return Kind == cinder::Kind::Scout || Kind == cinder::Kind::Mender || Kind == cinder::Kind::Kite;
}

/**
 * Deterministic in the id alone. The integer avalanche is lifted verbatim from
 * CinderTerrainSurface::Grain (constants copied, not shared, because that file
 * belongs to the terrain surface) so a crowd desynchronises its stride without
 * a random source: two clients replaying one match must agree on every unit's
 * gait offset, or the same replay looks different on two screens.
 */
float IdHash01(cinder::Id Id)
{
    uint32 Value = static_cast<uint32>(Id) * 0x8da6b343u ^ 0xd8163841u;
    Value ^= Value >> 13;
    Value *= 0x85ebca6bu;
    Value ^= Value >> 16;
    return static_cast<float>(Value & 65535u) / 65535.0f;
}

/**
 * Odd-symmetric foot shaping. Raising |sin| to 0.62 flattens the top of the
 * swing, which is what a planted foot looks like: the leg spends longer near
 * full extension and snaps through the middle. The ODD form is chosen
 * deliberately, because Shaped(sin(P + PI)) == -Shaped(sin(P)) by construction,
 * so the diagonal pair FL/FR is exactly opposite at EVERY phase. An asymmetric
 * stance/swing split would look marginally better on one leg and would break
 * that opposition at some samples, and with it the guarantee that a walking
 * unit never has both front legs forward.
 */
float ShapedStep(float Sine)
{
    return FMath::Sign(Sine) * FMath::Pow(FMath::Abs(Sine), 0.62f);
}

float SlewToward(float Current, float Target, float MaxDelta)
{
    const float Delta = Target - Current;
    return FMath::Abs(Delta) <= MaxDelta ? Target : Current + FMath::Sign(Delta) * MaxDelta;
}

/**
 * Manifest recoil_direction, which already points BACKWARD along the barrel.
 * The offset is therefore Kick * Impulse * Axis with a positive sign; writing
 * it as -Kick * Impulse * Axis would drive the muzzle forward of its rest pose.
 */
FVector RecoilAxis(cinder::Kind Kind)
{
    return Kind == cinder::Kind::Mortar
        ? FVector(-0.9777859f, 0, -0.2096061f)
        : FVector(-1.0f, 0, 0);
}

// Travel in centimetres and barrel pitch in degrees, scaled by how much the
// weapon is supposed to hurt: the Cinderthrow is a siege mortar and should
// visibly shove its own chassis, the Needle is a rifle and should not.
float RecoilKick(cinder::Kind Kind)
{
    switch (Kind)
    {
    case cinder::Kind::Mortar: return 9.0f;
    case cinder::Kind::Bastion: return 6.0f;
    case cinder::Kind::Striker: return 4.0f;
    default: return 3.0f;
    }
}

float RecoilKickPitch(cinder::Kind Kind)
{
    switch (Kind)
    {
    case cinder::Kind::Mortar: return 9.0f;
    case cinder::Kind::Bastion: return 6.0f;
    case cinder::Kind::Striker: return 3.5f;
    default: return 2.0f;
    }
}

// Traverse limit per chassis. Past this the barrel would sweep through the
// hull, so the body has to turn instead and the aim reads as a real mount.
float TraverseArc(cinder::Kind Kind)
{
    switch (Kind)
    {
    case cinder::Kind::Bastion: return 45.0f;
    case cinder::Kind::Mortar: return 30.0f;
    default: return 20.0f;
    }
}
}

TConstArrayView<FCinderMotionAssetPart> CinderMotionAssetParts(cinder::Kind Kind)
{
    return MotionAssetPartsInternal(Kind);
}

FRotator FCinderEntityPose::Rotation(ECinderMotionPart Part) const
{
    switch (Part)
    {
    case ECinderMotionPart::Body: return BodyRotation;
    case ECinderMotionPart::LegFrontLeft: return LegFrontLeftRotation;
    case ECinderMotionPart::LegFrontRight: return LegFrontRightRotation;
    case ECinderMotionPart::LegRearLeft: return LegRearLeftRotation;
    case ECinderMotionPart::LegRearRight: return LegRearRightRotation;
    case ECinderMotionPart::Weapon: return WeaponRotation;
    case ECinderMotionPart::Tool: return ToolRotation;
    default: return FRotator::ZeroRotator;
    }
}

FVector FCinderEntityPose::Offset(ECinderMotionPart Part) const
{
    return Part == ECinderMotionPart::Weapon ? WeaponOffset : FVector::ZeroVector;
}

void FCinderEntityMotion::BeginFrame()
{
    for (auto& Pair : Samples) Pair.Value.bSeen = false;
}

const FCinderEntityPose& FCinderEntityMotion::Observe(const FCinderMotionObservation& Observation)
{
    FSample* Existing = Samples.Find(Observation.Id);
    // Same frame, or no presentation time elapsed since the last one: return the
    // cached pose untouched. The time guard is what makes a pose pass idempotent.
    // Every integrated channel — gait phase, the facing follower, weapon slew —
    // advances by the observed delta, and re-entering with a fresh serial but an
    // unchanged clock would step them all again on the floor applied to guard the
    // speed division, so two submissions of one instant would disagree.
    if (Existing && (Existing->Observation.Tick == Observation.Tick
        || Observation.Time <= Existing->Observation.Time))
    {
        Existing->bSeen = true;
        return Existing->Pose;
    }

    FSample Next;
    Next.bSeen = true;
    if (Existing)
    {
        Next.Memory = Existing->Memory;
    }
    else
    {
        // Seed the follower to the authoritative heading so a unit that has just
        // entered vision settles instead of spinning up from an arbitrary zero.
        Next.Memory.RenderFacing = Observation.Facing;
        // Strictly monotonic ids make this a genuine production, not a reveal.
        if (static_cast<uint64>(Observation.Id) > HighWaterId) Next.Memory.BirthAt = Observation.Time;
    }
    HighWaterId = FMath::Max(HighWaterId, static_cast<uint64>(Observation.Id));

    if (Existing && Observation.Cooldown > Existing->Observation.Cooldown + KINDA_SMALL_NUMBER)
        Next.Memory.RecoilStartedAt = Observation.Time;
    if (Next.Memory.RecoilStartedAt >= 0 && Observation.Time - Next.Memory.RecoilStartedAt > RecoilDuration)
        Next.Memory.RecoilStartedAt = -1;

    // Damage is edge-detected on an hp DROP exactly as the cooldown RISE above:
    // the simulation reports a level, never an event, so the only honest signal
    // is the difference between two observations this client actually saw.
    if (Existing && Observation.Hp < Existing->Observation.Hp - KINDA_SMALL_NUMBER)
    {
        Next.Memory.FlinchStartedAt = Observation.Time;
        Next.Memory.FlinchAmount = FMath::Clamp(
            (Existing->Observation.Hp - Observation.Hp) / FMath::Max(1.0f, Observation.MaxHp), 0.0f, 0.5f);
    }
    if (Next.Memory.FlinchStartedAt >= 0 && Observation.Time - Next.Memory.FlinchStartedAt > FlinchDuration)
        Next.Memory.FlinchStartedAt = -1;

    FCinderMotionMemory Advanced = Next.Memory;
    Next.Pose = CalculatePose(Observation, Existing ? &Existing->Observation : nullptr, Next.Memory, &Advanced);
    Next.Memory = Advanced;
    Next.Observation = Observation;
    Samples.Add(Observation.Id, MoveTemp(Next));
    return Samples.FindChecked(Observation.Id).Pose;
}

void FCinderEntityMotion::EvictUnseen(float Time, const TFunctionRef<bool(const cinder::Vec2&)>* IsVisible)
{
    // Expire finished topples before recording new ones so the fixed budget is
    // always spent on the most recent deaths rather than held by stale ones.
    Dying.RemoveAll([Time](const FCinderDyingEntity& Entry) { return Time - Entry.StartedAt >= DeathDuration; });
    for (auto It = Samples.CreateIterator(); It; ++It)
    {
        if (It.Value().bSeen) continue;
        const FCinderMotionObservation& Gone = It.Value().Observation;
        // Fog privacy is correctness, not polish. An entity stops being observed
        // for two reasons that look identical from here: it died, or its cell
        // went dark. Without a visibility predicate the two cannot be told
        // apart, so nothing is recorded; guessing would animate a topple for a
        // kill made out of sight and leak it to the player.
        if (IsVisible && (*IsVisible)(Gone.Position))
        {
            if (Dying.Num() >= MaxDyingEntities) Dying.RemoveAt(0);
            FCinderDyingEntity& Entry = Dying.AddDefaulted_GetRef();
            Entry.Id = Gone.Id;
            Entry.Kind = Gone.Kind;
            Entry.Team = Gone.Team;
            Entry.Position = Gone.Position;
            Entry.Facing = Gone.Facing;
            Entry.StartedAt = Time;
        }
        It.RemoveCurrent();
    }
}

void FCinderEntityMotion::EndFrame()
{
    for (auto It = Samples.CreateIterator(); It; ++It)
        if (!It.Value().bSeen) It.RemoveCurrent();
}

void FCinderEntityMotion::EndFrame(float Time)
{
    EvictUnseen(Time, nullptr);
}

void FCinderEntityMotion::EndFrame(float Time, TFunctionRef<bool(const cinder::Vec2&)> IsVisible)
{
    EvictUnseen(Time, &IsVisible);
}

void FCinderEntityMotion::Reset()
{
    Samples.Reset();
    Dying.Reset();
    HighWaterId = 0;
}

float FCinderEntityMotion::RecoilImpulse(float Alpha)
{
    const float A = FMath::Clamp(Alpha, 0.0f, 1.0f);
    // The first 14% of the 0.22s window is the strike: a smoothstep to full kick
    // in 31ms, one frame at 30fps, so the shot lands on the frame it is fired.
    if (A < 0.14f) return FMath::SmoothStep(0.0f, 1.0f, A / 0.14f);
    // The rest is follow-through: an exponential settle at 5.2 carrying a ring
    // at 11 rad/unit. The ring is DELIBERATELY biased strictly positive
    // (0.82 +/- 0.18, so never under 0.64 of the envelope). A true damped cosine
    // gives Impulse(0.5) = exp(-1.872) * cos(3.456) = -0.1465, which flips both
    // WeaponOffset.X and WeaponOffset.Z positive mid-impulse: the muzzle lunges
    // forward of its rest pose, out in front of the hull. Keeping the
    // oscillation above zero preserves the sign of the whole impulse; the
    // visible overshoot is delivered by the body counter-rock and the weapon
    // pitch instead. At A = 1 this evaluates to ~0.007, so the impulse still
    // terminates inside RecoilDuration. Do not "fix" this into a real damped
    // cosine.
    return FMath::Exp(-5.2f * (A - 0.14f)) * (0.82f + 0.18f * FMath::Cos((A - 0.14f) * 11.0f));
}

FCinderEntityPose FCinderEntityMotion::CalculatePose(const FCinderMotionObservation& Current,
    const FCinderMotionObservation* Previous, float RecoilStartedAt)
{
    FCinderMotionMemory Memory;
    Memory.RecoilStartedAt = RecoilStartedAt;
    // No history means no lead: the hull renders at exactly the authoritative
    // heading, which is what every call site predating the motion memory saw.
    Memory.RenderFacing = Current.Facing;
    return CalculatePose(Current, Previous, Memory, nullptr);
}

FCinderEntityPose FCinderEntityMotion::CalculatePose(const FCinderMotionObservation& Current,
    const FCinderMotionObservation* Previous, const FCinderMotionMemory& InMemory,
    FCinderMotionMemory* AdvancedMemory)
{
    FCinderMotionMemory Memory = InMemory;
    FCinderEntityPose Pose;
    // Previous->Time is the presentation clock, not the simulation step, so it
    // is the only legitimate source of dt here. Without a previous sample there
    // is nothing to integrate against and one simulation step is the honest
    // default for the slew rates.
    const float Dt = Previous
        ? FMath::Max(1e-4f, Current.Time - Previous->Time)
        : cinder::Simulation::Step;
    if (Previous && Current.Tick > Previous->Tick)
    {
        const bool bHasSimulationTicks = Current.SimulationTick != MAX_uint64
            && Previous->SimulationTick != MAX_uint64;
        const bool bSimulationAdvanced = bHasSimulationTicks
            && Current.SimulationTick > Previous->SimulationTick;
        const bool bSimulationRewound = bHasSimulationTicks
            && Current.SimulationTick < Previous->SimulationTick;
        // A local root is held between fixed steps. Zero displacement on an
        // intervening display frame says nothing about whether that unit stopped.
        // Online roots really interpolate per frame, so retain their measured
        // display velocity instead of prolonging a finished interpolation.
        if (!bHasSimulationTicks || bSimulationAdvanced || Current.bInterpolatedPosition)
        {
            const float MovementDt = bHasSimulationTicks && !Current.bInterpolatedPosition
                ? static_cast<float>(Current.SimulationTick - Previous->SimulationTick) * cinder::Simulation::Step
                : Dt;
            Memory.MovementVelocity = {
                (Current.Position.x - Previous->Position.x) / FMath::Max(MovementDt, 1e-4f),
                (Current.Position.y - Previous->Position.y) / FMath::Max(MovementDt, 1e-4f)};
        }
        if (!bHasSimulationTicks || bSimulationAdvanced)
            Memory.bMiningActive = !FMath::IsNearlyEqual(
                Current.HarvestTimer, Previous->HarvestTimer, KINDA_SMALL_NUMBER);
        // Commands can change an order before the next fixed step. Discard
        // cached activity immediately, then wait for fresh observed movement.
        if (bSimulationRewound || Current.bInterpolatedPosition != Previous->bInterpolatedPosition
            || (bHasSimulationTicks && !bSimulationAdvanced && Current.Order != Previous->Order))
        {
            Memory.MovementVelocity = {};
            Memory.bMiningActive = false;
        }
    }
    else
    {
        Memory.MovementVelocity = {};
        Memory.bMiningActive = false;
    }
    const float SpeedSq = FMath::Square(Memory.MovementVelocity.x) + FMath::Square(Memory.MovementVelocity.y);
    Pose.bMoving = SpeedSq > MovementSpeedEpsilonSquared;
    if (Current.Order != cinder::Order::Gather || Current.bReturning || Pose.bMoving)
        Memory.bMiningActive = false;
    Pose.bToolActive = Memory.bMiningActive
        || (Current.Order == cinder::Order::Construct && Current.bConstructionActive);
    // Bank from a consistent step's travel, independent of the display rate.
    const float DX = Memory.MovementVelocity.x * cinder::Simulation::Step;
    const float DY = Memory.MovementVelocity.y * cinder::Simulation::Step;
    Pose.bRecoil = Memory.RecoilStartedAt >= 0 && Current.Time >= Memory.RecoilStartedAt
        && Current.Time - Memory.RecoilStartedAt <= RecoilDuration;

    const cinder::Definition& Def = cinder::definition(Current.Kind);
    // Cadence is INTEGRATED rather than evaluated: sampling Time * constant
    // would make the whole stride jump whenever the rate changed, so a unit
    // slowing into a crowd would teleport its legs. Integrating means the phase
    // is continuous and only its derivative moves. A unit at a third of its
    // definition speed steps a third as often, so a Skim and a Cinderthrow plant
    // feet at the same rate per metre, and a crowd-blocked unit stops
    // scissoring its feet in place entirely.
    const float DefSpeed = FMath::Max(1.0f, Def.speed);
    float Cadence = WalkCadence * FMath::Clamp(FMath::Sqrt(SpeedSq) / DefSpeed, 0.0f, 1.35f)
        * (0.94f + 0.12f * IdHash01(Current.Id));
    // A loaded Drudge is heavier on its feet; the shortened stride is half of
    // what makes the return leg of the gather loop read differently.
    if (Current.bReturning) Cadence *= 0.86f;
    Memory.GaitPhase = FMath::Fmod(Memory.GaitPhase + Cadence * Dt, UE_TWO_PI);
    // The per-id offset survives the rewrite: it is what keeps neighbours out of
    // phase so a marching column does not pulse as one body.
    const float Phase = Memory.GaitPhase + static_cast<float>(Current.Id) * 0.71f;
    const float Step = FMath::Sin(Phase);

    if (Pose.bMoving && !IsHoverUnit(Current.Kind))
    {
        if (IsTracked(Current.Kind))
        {
            // Tracked chassis have no legs in the manifest, so the entire read
            // has to come from the hull: a heavier ride height, a pitch that
            // rocks fore and aft over the tracks, and a roll on the suspension.
            Pose.BodyZ = 1.8f + FMath::Abs(Step) * 3.0f;
            Pose.BodyRotation.Pitch = Step * 4.0f;
            Pose.LegFrontLeftRotation.Roll = Step * 5.0f;
            Pose.LegFrontRightRotation.Roll = -Step * 5.0f;
        }
        else
        {
            // The bob is phased to the PLANT, not to the swing: |cos| peaks when
            // sin crosses zero, which is the instant a foot is directly under
            // the hull carrying the weight. |sin| peaked mid-stride instead,
            // which is why the old cycle floated.
            Pose.BodyZ = 4.2f * FMath::Abs(FMath::Cos(Phase));
            Pose.BodyRotation.Pitch = FMath::Sin(Phase * 2) * 3.4f;
            const float Swing = 30.0f * ShapedStep(Step);
            Pose.LegFrontLeftRotation.Pitch = Swing;
            Pose.LegRearRightRotation.Pitch = Swing;
            Pose.LegFrontRightRotation.Pitch = -Swing;
            Pose.LegRearLeftRotation.Pitch = -Swing;
        }
    }

    if (Pose.bToolActive)
    {
        const float WorkPhase = Current.Time * (Current.Order == cinder::Order::Construct ? 5.2f : 7.0f)
            + static_cast<float>(Current.Id) * 0.41f;
        Pose.ToolRotation.Pitch = -18.0f + FMath::Sin(WorkPhase) * 28.0f;
    }

    // Structures stay anchored; aircraft already hover below. A standing ground
    // unit settles on its legs, and any weapon that is not mid-attack sweeps
    // slowly, so a held line still reads as crewed machines rather than props.
    const bool bStructure = Def.building;
    if (!Pose.bMoving && !bStructure && !IsHoverUnit(Current.Kind))
    {
        const float IdlePhase = Current.Time * 1.15f + static_cast<float>(Current.Id) * 0.73f;
        const float Lift = Current.bSelected ? SelectedIdleLift : 1.0f;
        Pose.BodyZ = FMath::Sin(IdlePhase) * IdleBodyRise * Lift;
        Pose.BodyRotation.Roll = FMath::Sin(IdlePhase * 0.78f + 1.9f) * IdleBodyTilt;
    }

    if (IsHoverUnit(Current.Kind))
    {
        Pose.BodyZ = FMath::Sin(Current.Time * 1.7f + Current.Id * 0.73f) * 2.5f;
        if (Pose.bMoving)
        {
            const float RightX = -FMath::Sin(Current.Facing), RightY = FMath::Cos(Current.Facing);
            const float ForwardX = FMath::Cos(Current.Facing), ForwardY = FMath::Sin(Current.Facing);
            const float LocalRight = DX * RightX + DY * RightY;
            const float LocalForward = DX * ForwardX + DY * ForwardY;
            Pose.BodyRotation.Roll = FMath::Clamp(-LocalRight * 0.22f, -9.0f, 9.0f);
            Pose.BodyRotation.Pitch = FMath::Clamp(-LocalForward * 0.08f, -4.0f, 4.0f);
        }
    }

    if (Current.bReturning)
    {
        // The loaded carry. A Drudge hauling ore is the single most watched
        // silhouette in the game and today it is pixel-identical to an empty
        // one: the tool is hoisted clear of the ground, the chassis leans into
        // the weight and rides lower on its legs.
        Pose.ToolRotation.Pitch = -62.0f;
        Pose.BodyRotation.Pitch += 4.5f;
        Pose.BodyZ -= 0.9f;
    }

    if (Pose.bRecoil)
    {
        const float Impulse = RecoilImpulse((Current.Time - Memory.RecoilStartedAt) / RecoilDuration);
        Pose.WeaponOffset = RecoilAxis(Current.Kind) * (RecoilKick(Current.Kind) * Impulse);
        Pose.WeaponRotation.Pitch = -Impulse * RecoilKickPitch(Current.Kind);
        // The chassis has to absorb the shot or the weapon looks detached from
        // it. Heft is the definition damage normalised against the heaviest gun
        // in the game (Cinderthrow, 64), floored so even a Drudge-scale weapon
        // registers something rather than nothing.
        const float Heft = FMath::Clamp(Def.damage / 64.0f, 0.15f, 1.0f);
        Pose.BodyRotation.Pitch += Impulse * 3.6f * Heft;
        Pose.BodyZ -= Impulse * 1.4f * Heft;
    }

    if (Memory.FlinchStartedAt >= 0)
    {
        const float FlinchAlpha = FMath::Clamp((Current.Time - Memory.FlinchStartedAt) / FlinchDuration, 0.0f, 1.0f);
        // Half cosine: full amplitude on the frame the damage was observed, back
        // to rest 0.18s later. Scaled against 140hp so a 480hp Anvil shrugs off
        // a hit that visibly staggers a 70hp Drudge, which is the difference a
        // player is actually reading. MaxHp is floored because an observation
        // with no health filled in must not divide by zero.
        const float Envelope = 0.5f + 0.5f * FMath::Cos(FlinchAlpha * UE_PI);
        const float MassScale = FMath::Clamp(140.0f / FMath::Max(1.0f, Current.MaxHp), 0.35f, 1.5f);
        const float Amount = Memory.FlinchAmount * 26.0f * MassScale * Envelope;
        Pose.BodyRotation.Pitch += Amount;
        // A fixed per-id side, so the same unit always recoils the same way and
        // a line taking fire does not roll in unison.
        Pose.BodyRotation.Roll += Amount * 0.6f * (IdHash01(Current.Id) * 2.0f - 1.0f);
        if (FlinchAlpha >= 1.0f) Memory.FlinchStartedAt = -1;
    }

    if (Current.bHasTarget)
    {
        // The weapon is the only part permitted to point away from the
        // authoritative facing, and only inside its mount's arc: past that the
        // barrel would sweep through the hull and the body has to turn instead.
        const float Arc = TraverseArc(Current.Kind);
        const float Desired = FMath::RadiansToDegrees(
            FMath::UnwindRadians(Current.TargetBearing - Current.Facing));
        Memory.WeaponYaw = SlewToward(Memory.WeaponYaw, FMath::Clamp(Desired, -Arc, Arc),
            WeaponSlewDegreesPerSecond * Dt);
    }
    else
    {
        // Losing a target relaxes the barrel back onto the idle sweep at the
        // same traverse rate rather than snapping to it, so a unit that has just
        // finished a fight visibly stands down.
        const float Sweep = (!Pose.bMoving && !Pose.bRecoil && Current.Cooldown <= 0)
            ? FMath::Sin(Current.Time * 0.55f + static_cast<float>(Current.Id) * 0.37f) * IdleWeaponSweep
            : 0.0f;
        Memory.WeaponYaw = SlewToward(Memory.WeaponYaw, Sweep, WeaponSlewDegreesPerSecond * Dt);
    }
    Pose.WeaponRotation.Yaw = Memory.WeaponYaw;

    // cinder writes entity.facing with a bare atan2, so an order change can whip
    // the authoritative heading 180 degrees inside one 50ms tick. The follower
    // renders a smoothed heading instead. Because CinderPartWorldTransform
    // applies Facing * PartRotation and the Body pivot is the origin, a yaw in
    // BodyRotation spins the hull about its own axis while the legs and weapon
    // stay on the raw facing: the body visibly leads the turn, which is
    // anticipation for free and costs no extra transform.
    const float FollowDt = FMath::Min(Dt, MaxFollowerStep);
    const float Error = FMath::UnwindRadians(Current.Facing - Memory.RenderFacing);
    Memory.FacingRate += (FacingStiffness * FacingStiffness * Error
        - 2.0f * FacingDamping * FacingStiffness * Memory.FacingRate) * FollowDt;
    Memory.RenderFacing = FMath::UnwindRadians(Memory.RenderFacing + Memory.FacingRate * FollowDt);
    Pose.BodyRotation.Yaw = FMath::RadiansToDegrees(
        FMath::UnwindRadians(Memory.RenderFacing - Current.Facing));
    Pose.BodyRotation.Roll += FMath::Clamp(-Memory.FacingRate * 2.6f, -7.0f, 7.0f);

    if (Memory.BirthAt >= 0)
    {
        const float BirthAlpha = FMath::Clamp((Current.Time - Memory.BirthAt) / BirthDuration, 0.0f, 1.0f);
        // Scale-in with an overshoot. The eased base reaches ~0.94 at alpha
        // 0.775, where the half-sine window peaks at +0.12, so the silhouette
        // crests at about 1.06 and lands on exactly 1.0 at 0.35s. That crest is
        // what makes production read as a machine being pushed out of a bay
        // rather than a mesh switching on, and it costs one float on the pose.
        const float Eased = FMath::SmoothStep(0.0f, 1.0f, BirthAlpha);
        const float Window = FMath::Clamp((BirthAlpha - 0.55f) / 0.45f, 0.0f, 1.0f);
        Pose.UniformScale = FMath::Lerp(0.55f, 1.0f, Eased) + 0.12f * FMath::Sin(Window * UE_PI);
        if (BirthAlpha >= 1.0f) Memory.BirthAt = -1;
    }

    if (AdvancedMemory) *AdvancedMemory = Memory;
    return Pose;
}

FCinderEntityPose FCinderEntityMotion::CalculateDeathPose(const FCinderDyingEntity& Entry, float Time)
{
    FCinderEntityPose Pose;
    const float Alpha = FMath::Clamp((Time - Entry.StartedAt) / DeathDuration, 0.0f, 1.0f);
    // The topple accelerates. Smoothstepping a squared alpha hangs the hull for
    // a beat and then drops it, which is the only weight cue available: at
    // Minimum quality there is no shadow to ground the unit and no bloom to
    // flash the kill, so the fall itself has to carry it.
    const float Fall = FMath::SmoothStep(0.0f, 1.0f, Alpha * Alpha);
    Pose.BodyRotation.Pitch = 82.0f * Fall;
    Pose.BodyZ = -18.0f * Fall;
    // Legs splay as the chassis gives, holding the same diagonal pairing as the
    // walk so a wreck still reads as the machine it was a moment ago.
    const float Splay = 40.0f * Fall;
    Pose.LegFrontLeftRotation.Pitch = Splay;
    Pose.LegRearRightRotation.Pitch = Splay;
    Pose.LegFrontRightRotation.Pitch = -Splay;
    Pose.LegRearLeftRotation.Pitch = -Splay;
    Pose.WeaponRotation.Pitch = 30.0f * Fall;
    Pose.ToolRotation.Pitch = 30.0f * Fall;
    return Pose;
}

FTransform CinderPartWorldTransform(const FVector& Root, const FQuat& Facing,
    const FVector& Pivot, const FRotator& LocalRotation, const FVector& LocalOffset, const FVector& Scale)
{
    const FQuat PartRotation = LocalRotation.Quaternion();
    const FVector PivotCorrection = Pivot - PartRotation.RotateVector(Pivot) + LocalOffset;
    return FTransform(Facing * PartRotation, Root + Facing.RotateVector(PivotCorrection), Scale);
}
