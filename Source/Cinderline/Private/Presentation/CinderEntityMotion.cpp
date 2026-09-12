#include "Presentation/CinderEntityMotion.h"

namespace
{
constexpr float MovementEpsilonSquared = 0.25f;
constexpr float RecoilDuration = 0.22f;

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
    if (Existing && Existing->Observation.Tick == Observation.Tick)
    {
        Existing->bSeen = true;
        return Existing->Pose;
    }

    FSample Next;
    Next.bSeen = true;
    Next.RecoilStartedAt = Existing ? Existing->RecoilStartedAt : -1;
    if (Existing && Observation.Cooldown > Existing->Observation.Cooldown + KINDA_SMALL_NUMBER)
        Next.RecoilStartedAt = Observation.Time;
    if (Next.RecoilStartedAt >= 0 && Observation.Time - Next.RecoilStartedAt > RecoilDuration)
        Next.RecoilStartedAt = -1;
    Next.Pose = CalculatePose(Observation, Existing ? &Existing->Observation : nullptr, Next.RecoilStartedAt);
    Next.Observation = Observation;
    Samples.Add(Observation.Id, MoveTemp(Next));
    return Samples.FindChecked(Observation.Id).Pose;
}

void FCinderEntityMotion::EndFrame()
{
    for (auto It = Samples.CreateIterator(); It; ++It)
        if (!It.Value().bSeen) It.RemoveCurrent();
}

void FCinderEntityMotion::Reset()
{
    Samples.Reset();
}

FCinderEntityPose FCinderEntityMotion::CalculatePose(const FCinderMotionObservation& Current,
    const FCinderMotionObservation* Previous, float RecoilStartedAt)
{
    FCinderEntityPose Pose;
    float DX = 0, DY = 0;
    if (Previous && Current.Tick > Previous->Tick)
    {
        DX = Current.Position.x - Previous->Position.x;
        DY = Current.Position.y - Previous->Position.y;
        Pose.bMoving = DX * DX + DY * DY > MovementEpsilonSquared;
        const bool bHarvestAdvanced = !Current.bReturning && Current.Order == cinder::Order::Gather
            && !FMath::IsNearlyEqual(Current.HarvestTimer, Previous->HarvestTimer, KINDA_SMALL_NUMBER);
        Pose.bToolActive = bHarvestAdvanced
            || (Current.Order == cinder::Order::Construct && Current.bConstructionActive);
    }
    Pose.bRecoil = RecoilStartedAt >= 0 && Current.Time >= RecoilStartedAt
        && Current.Time - RecoilStartedAt <= RecoilDuration;

    const float Phase = Current.Time * 8.4f + static_cast<float>(Current.Id) * 0.71f;
    const float Step = FMath::Sin(Phase);
    if (Pose.bMoving && !IsHoverUnit(Current.Kind))
    {
        if (IsTracked(Current.Kind))
        {
            Pose.BodyZ = 1.2f + FMath::Abs(Step) * 1.3f;
            Pose.BodyRotation.Pitch = Step * 1.4f;
            Pose.LegFrontLeftRotation.Roll = Step * 2.2f;
            Pose.LegFrontRightRotation.Roll = -Step * 2.2f;
        }
        else
        {
            Pose.BodyZ = FMath::Abs(Step) * 1.8f;
            Pose.BodyRotation.Pitch = FMath::Sin(Phase * 2) * 0.8f;
            Pose.LegFrontLeftRotation.Pitch = Step * 24.0f;
            Pose.LegRearRightRotation.Pitch = Step * 24.0f;
            Pose.LegFrontRightRotation.Pitch = -Step * 24.0f;
            Pose.LegRearLeftRotation.Pitch = -Step * 24.0f;
        }
    }

    if (Pose.bToolActive)
    {
        const float WorkPhase = Current.Time * (Current.Order == cinder::Order::Construct ? 5.2f : 7.0f)
            + static_cast<float>(Current.Id) * 0.41f;
        Pose.ToolRotation.Pitch = -18.0f + FMath::Sin(WorkPhase) * 28.0f;
    }

    if (Pose.bRecoil)
    {
        const float Alpha = FMath::Clamp((Current.Time - RecoilStartedAt) / RecoilDuration, 0.0f, 1.0f);
        const float Recoil = FMath::Sin(Alpha * PI) * (Current.Kind == cinder::Kind::Mortar ? 7.0f : 4.0f);
        Pose.WeaponOffset = Current.Kind == cinder::Kind::Mortar
            ? FVector(-Recoil * 0.9777859f, 0, -Recoil * 0.2096061f)
            : FVector(-Recoil, 0, 0);
        if (Current.Kind == cinder::Kind::Mortar) Pose.WeaponRotation.Pitch = -FMath::Sin(Alpha * PI) * 4.0f;
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
    return Pose;
}

FTransform CinderPartWorldTransform(const FVector& Root, const FQuat& Facing,
    const FVector& Pivot, const FRotator& LocalRotation, const FVector& LocalOffset, const FVector& Scale)
{
    const FQuat PartRotation = LocalRotation.Quaternion();
    const FVector PivotCorrection = Pivot - PartRotation.RotateVector(Pivot) + LocalOffset;
    return FTransform(Facing * PartRotation, Root + Facing.RotateVector(PivotCorrection), Scale);
}
