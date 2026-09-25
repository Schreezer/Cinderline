#include "Presentation/CinderCamera.h"
#include "Presentation/CinderBattlefield.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Sim/Simulation.h"

namespace
{
constexpr double ZoomFitInset = 8;
constexpr double BorderViewFraction = 0.15;
constexpr double MinimumBorderMargin = 72;
constexpr double MaximumBorderMargin = 360;
}

ACinderCamera::ACinderCamera()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CameraRoot"));
    Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    Boom->SetupAttachment(RootComponent);
    Boom->SetRelativeRotation(FRotator(-54, -45, 0));
    Boom->TargetArmLength = TargetDistance;
    Boom->bDoCollisionTest = false;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Boom);
    Camera->FieldOfView = 52;
    Camera->bConstrainAspectRatio = false;
    // Match the perspective footprint calculation on every viewport shape.
    Camera->bOverrideAspectRatioAxisConstraint = true;
    Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainXFOV);
    Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Camera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
    Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
    Camera->PostProcessSettings.AutoExposureBias = 0;
#if PLATFORM_MAC
    // Ground contact and a restrained core glow give the small RTS silhouettes depth.
    Camera->PostProcessSettings.bOverride_AmbientOcclusionIntensity = true;
    Camera->PostProcessSettings.AmbientOcclusionIntensity = 0.58f;
    Camera->PostProcessSettings.bOverride_AmbientOcclusionRadius = true;
    Camera->PostProcessSettings.AmbientOcclusionRadius = 32.0f;
    Camera->PostProcessSettings.bOverride_AmbientOcclusionRadiusInWS = true;
    Camera->PostProcessSettings.AmbientOcclusionRadiusInWS = true;
    Camera->PostProcessSettings.bOverride_AmbientOcclusionQuality = true;
    Camera->PostProcessSettings.AmbientOcclusionQuality = 80.0f;
#endif
    // Threshold 0.62 is chosen against the material palette, not by eye: the
    // CoreGlow emissive peaks at 1.20 and so blooms, while the TeamPanel emissive
    // peaks at 0.242 and so does not. Team identity therefore stays legible after
    // the thermal ladder zeroes r.BloomQuality at Minimum quality, and bloom only
    // ever adds garnish on top of a frame that already reads without it.
    Camera->PostProcessSettings.bOverride_BloomIntensity = true;
    Camera->PostProcessSettings.BloomIntensity = 0.36f;
    Camera->PostProcessSettings.bOverride_BloomThreshold = true;
    Camera->PostProcessSettings.BloomThreshold = 0.62f;
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Camera->PostProcessSettings.MotionBlurAmount = 0.0f;
    Camera->PostProcessSettings.bOverride_DepthOfFieldEnabled = true;
    Camera->PostProcessSettings.DepthOfFieldEnabled = false;
    // A restrained warm key/cool fill grade preserves material colors and the
    // broad rock planes without clipping pale hulls or exaggerating soil noise.
    Camera->PostProcessSettings.bOverride_ColorSaturation = true;
    Camera->PostProcessSettings.ColorSaturation = FVector4(1.10f, 1.10f, 1.10f, 1.0f);
    Camera->PostProcessSettings.bOverride_ColorContrast = true;
    Camera->PostProcessSettings.ColorContrast = FVector4(1.10f, 1.10f, 1.10f, 1.0f);
    Camera->PostProcessSettings.bOverride_ColorGamma = true;
    Camera->PostProcessSettings.ColorGamma = FVector4(0.96f, 0.98f, 1.03f, 1.0f);
    Camera->PostProcessSettings.bOverride_ColorGainShadows = true;
    Camera->PostProcessSettings.ColorGainShadows = FVector4(0.84f, 0.94f, 1.10f, 1.0f);
    Camera->PostProcessSettings.bOverride_ColorGainHighlights = true;
    Camera->PostProcessSettings.ColorGainHighlights = FVector4(1.04f, 1.01f, 0.95f, 1.0f);
    Camera->PostProcessSettings.bOverride_ColorCorrectionShadowsMax = true;
    Camera->PostProcessSettings.ColorCorrectionShadowsMax = 0.32f;
    Camera->PostProcessSettings.bOverride_ColorCorrectionHighlightsMin = true;
    Camera->PostProcessSettings.ColorCorrectionHighlightsMin = 0.55f;
    Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
    Camera->PostProcessSettings.VignetteIntensity = 0.24f;
    SetActorLocation(TargetPosition);
}

void ACinderCamera::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const FGroundFootprint Footprint = GroundFootprint();
    UpdateTargetBounds(Footprint);
    // Recheck the interpolated view too: zoom and translation use different smoothing rates.
    const FVector Interpolated = FMath::VInterpTo(GetActorLocation(), TargetPosition, DeltaSeconds, 14);
    Boom->TargetArmLength = FMath::Min(MaximumDistance(Footprint, Interpolated.Z),
        FMath::FInterpTo(Boom->TargetArmLength, TargetDistance, DeltaSeconds, 12));
    // XY and Z ease together. Bounds use that same rendered height rather than
    // pretending the raised camera is still on the old zero-height plane.
    SetActorLocation(BoundPosition(Interpolated, Boom->TargetArmLength, Footprint, false));
}

ACinderCamera::FGroundFootprint ACinderCamera::GroundFootprint() const
{
    FGroundFootprint Result;
    int Width = 0, Height = 0;
    if (const auto* PC = Cast<APlayerController>(GetController())) PC->GetViewportSize(Width, Height);
    const double Aspect = Width > 0 && Height > 0 ? static_cast<double>(Width) / Height : Camera->AspectRatio;
    if (Aspect <= 0) return Result;
    const FQuat Rotation = Boom->GetTargetRotation().Quaternion();
    Result.Forward = Rotation.GetForwardVector();
    Result.Right = Rotation.GetRightVector();
    Result.Up = Rotation.GetUpVector();
    Result.TanHalfX = FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView * 0.5));
    Result.TanHalfY = Result.TanHalfX / Aspect;
    // With the zero-offset boom mount, ground intersections scale linearly with arm length.
    const FVector Origin = -Result.Forward;
    if (Origin.Z <= 0) return Result;
    FBox2D Bounds(ForceInit);
    for (int X : { -1, 1 }) for (int Y : { -1, 1 })
    {
        const FVector Ray = Result.Forward + Result.Right * (X * Result.TanHalfX) + Result.Up * (Y * Result.TanHalfY);
        if (Ray.Z >= -0.0001) return Result; // A horizon-crossing viewport has no finite ground footprint.
        const FVector Corner = Origin + Ray * (-Origin.Z / Ray.Z);
        Bounds += FVector2D(Corner.X, Corner.Y);
    }
    Result.Min = Bounds.Min; Result.Max = Bounds.Max; Result.bValid = true;
    return Result;
}

float ACinderCamera::MaximumDistance(const FGroundFootprint& Footprint, float Height) const
{
    if (!Footprint.bValid) return ACinderCamera::ClosestDistance;
    const FVector2D Span = Footprint.Max - Footprint.Min;
    const double Available = ActiveWorldSize - ZoomFitInset * 2;
    // An unusually tall viewport may need a zoom cap below the usual minimum.
    const double HeightDistance = Height / FMath::Max(0.001, -Footprint.Forward.Z);
    return static_cast<float>(FMath::Max(1.0, FMath::Min(static_cast<double>(ACinderCamera::FarthestDistance),
        Available / FMath::Max(Span.X, Span.Y) - HeightDistance)));
}

float ACinderCamera::SurfaceHeight(FVector Position) const
{
    if (const ACinderBattlefield* Battlefield = TerrainSource.Get())
        return Battlefield->IsMenu() ? 0.0f : Battlefield->PickingGroundHeight({static_cast<float>(Position.X), static_cast<float>(Position.Y)});
    return FMath::IsFinite(Position.Z) ? Position.Z : 0.0f;
}

void ACinderCamera::SetTerrainSource(ACinderBattlefield* Battlefield)
{
    TerrainSource = Battlefield;
}

void ACinderCamera::PositionBounds(float Distance, float Height, const FGroundFootprint& Footprint,
    FVector2D& Low, FVector2D& High, bool bUseFullBorder) const
{
    const double HeightDistance = Height / FMath::Max(0.001, -Footprint.Forward.Z);
    const double PlaneDistance = FMath::Max(1.0, Distance + HeightDistance);
    const FVector2D HeightShift(Footprint.Forward.X * HeightDistance, Footprint.Forward.Y * HeightDistance);
    const FVector2D Span = (Footprint.Max - Footprint.Min) * PlaneDistance;
    const double BorderMargin = bUseFullBorder ? MaximumBorderMargin
        : FMath::Clamp(FMath::Min(Span.X, Span.Y) * BorderViewFraction, MinimumBorderMargin, MaximumBorderMargin);
    Low = FVector2D(-BorderMargin) - HeightShift - Footprint.Min * PlaneDistance;
    High = FVector2D(ActiveWorldSize + BorderMargin) - HeightShift - Footprint.Max * PlaneDistance;
}

FVector ACinderCamera::BoundPosition(FVector Position, float Distance, const FGroundFootprint& Footprint,
    bool bResolveSurface) const
{
    const double World = ActiveWorldSize;
    if (!Footprint.bValid) return FVector(World * 0.5, World * 0.5, 0);
    // Reveal a finite black rim at the edge. Scale it with the view so close
    // zooms cannot drift far into empty space, and cap it for distant views.
    auto ClampAxis = [](double Value, double Min, double Max) { return Min <= Max ? FMath::Clamp(Value, Min, Max) : (Min + Max) * 0.5; };
    // Raising the pivot by H is equivalent, on the world-zero boundary plane,
    // to a longer arm D + H/-Forward.Z and a small forward XY displacement.
    // Iterate because an XY clamp can move the pivot from a plateau to low ground.
    for (int32 Attempt = 0; Attempt < (bResolveSurface && TerrainSource.IsValid() ? 4 : 1); ++Attempt)
    {
        if (bResolveSurface && TerrainSource.IsValid())
            Position.Z = SurfaceHeight(Position) + (bKeepFocusVisible ? FocusHalfExtent.Z : 0.0f);
        FVector2D Low, High;
        // Full-subject focus may use the existing hard rim to keep an elevated
        // roof in view. Preserve that allowance while its rendered view eases;
        // ordinary panning retains the tighter margin scaled with view size.
        PositionBounds(Distance, Position.Z, Footprint, Low, High,
            bKeepFocusVisible && !FocusHalfExtent.IsNearlyZero());
        const FVector Clamped(ClampAxis(Position.X, Low.X, High.X), ClampAxis(Position.Y, Low.Y, High.Y), Position.Z);
        if (FVector2D(Clamped).Equals(FVector2D(Position), 0.001)) return Clamped;
        Position = Clamped;
    }
    return Position;
}

bool ACinderCamera::FindFocusPosition(float Distance, const FGroundFootprint& Footprint, FVector& Position,
    bool bUseFullBorder) const
{
    // At a fixed camera height and distance, both the map bounds and the
    // projected subject bounds are half-planes in camera XY. Their intersection
    // uses free space on either axis instead of clipping a raised subject after
    // an independent X/Y clamp near the rotated map's corners.
    using FPolygon = TArray<FVector2D, TInlineAllocator<40>>;
    auto RetryWithFocusBorder = [&]()
    {
        return !bUseFullBorder && !FocusHalfExtent.IsNearlyZero()
            && FindFocusPosition(Distance, Footprint, Position, true);
    };
    double Height = FocusAnchor.Z;
    for (int32 Attempt = 0; Attempt < 4; ++Attempt)
    {
        FVector2D Low, High;
        PositionBounds(Distance, Height, Footprint, Low, High, bUseFullBorder);
        if (Low.X > High.X || Low.Y > High.Y) return RetryWithFocusBorder();
        FPolygon Polygon = {{Low.X, Low.Y}, {High.X, Low.Y}, {High.X, High.Y}, {Low.X, High.Y}};
        auto Clip = [&Polygon](const FVector2D& Normal, double Limit)
        {
            if (Polygon.IsEmpty()) return;
            FPolygon Clipped;
            FVector2D Previous = Polygon.Last();
            double PreviousSide = FVector2D::DotProduct(Previous, Normal) - Limit;
            for (const FVector2D& Current : Polygon)
            {
                const double CurrentSide = FVector2D::DotProduct(Current, Normal) - Limit;
                if ((CurrentSide >= 0) != (PreviousSide >= 0))
                    Clipped.Add(Previous + (Current - Previous) * (PreviousSide / (PreviousSide - CurrentSide)));
                if (CurrentSide >= 0) Clipped.Add(Current);
                Previous = Current;
                PreviousSide = CurrentSide;
            }
            Polygon = MoveTemp(Clipped);
        };
        for (int32 X : {-1, 1}) for (int32 Y : {-1, 1}) for (int32 Z : {-1, 1})
        {
            const FVector Corner = FocusAnchor + FocusHalfExtent * FVector(X, Y, Z);
            const FVector Relative = Corner + Footprint.Forward * Distance - FVector(0, 0, Height);
            for (int32 Sign : {-1, 1})
            {
                // A tiny inset keeps numerical tangency safely inside ContainsFocus's 80% region.
                const FVector Horizontal = Footprint.Right * Sign - Footprint.Forward * (Footprint.TanHalfX * 0.79999);
                const FVector Vertical = Footprint.Up * Sign - Footprint.Forward * (Footprint.TanHalfY * 0.79999);
                Clip(FVector2D(Horizontal), FVector::DotProduct(Horizontal, Relative));
                Clip(FVector2D(Vertical), FVector::DotProduct(Vertical, Relative));
            }
        }
        if (Polygon.IsEmpty()) return RetryWithFocusBorder();
        const FVector2D Desired(FocusAnchor);
        FVector2D Nearest = Desired;
        double BestDistanceSquared = TNumericLimits<double>::Max();
        bool bInside = Polygon.Num() >= 3;
        for (int32 Index = 0; Index < Polygon.Num(); ++Index)
        {
            const FVector2D A = Polygon[Index], B = Polygon[(Index + 1) % Polygon.Num()];
            const FVector2D Edge = B - A;
            bInside &= FVector2D::CrossProduct(Edge, Desired - A) >= -0.000001;
            const double T = FMath::Clamp(FVector2D::DotProduct(Desired - A, Edge)
                / FMath::Max(Edge.SizeSquared(), 0.000001), 0.0, 1.0);
            const FVector2D Candidate = A + Edge * T;
            const double DistanceSquared = FVector2D::DistSquared(Candidate, Desired);
            if (DistanceSquared < BestDistanceSquared)
            {
                BestDistanceSquared = DistanceSquared;
                Nearest = Candidate;
            }
        }
        if (bInside) Nearest = Desired;
        Position = FVector(Nearest, Height);
        const double PresentedHeight = TerrainSource.IsValid() ? SurfaceHeight(Position) + FocusHalfExtent.Z : Height;
        if (FMath::IsNearlyEqual(Height, PresentedHeight, 0.001))
            return ContainsFocus(Position, Distance, Footprint) || RetryWithFocusBorder();
        Height = PresentedHeight;
    }
    return RetryWithFocusBorder();
}

bool ACinderCamera::ContainsFocus(FVector Position, float Distance, const FGroundFootprint& Footprint) const
{
    for (int32 X : {-1, 1}) for (int32 Y : {-1, 1}) for (int32 Z : {-1, 1})
    {
        const FVector ToFocus = FocusAnchor + FocusHalfExtent * FVector(X, Y, Z)
            - (Position - Footprint.Forward * Distance);
        const double Depth = FVector::DotProduct(ToFocus, Footprint.Forward);
        if (Depth <= 0 || FMath::Abs(FVector::DotProduct(ToFocus, Footprint.Right)) > Depth * Footprint.TanHalfX * 0.80
            || FMath::Abs(FVector::DotProduct(ToFocus, Footprint.Up)) > Depth * Footprint.TanHalfY * 0.80) return false;
    }
    return true;
}

float ACinderCamera::MinimumSubjectDistance(const FGroundFootprint& Footprint) const
{
    float Distance = ClosestDistance;
    if (!Footprint.bValid || FocusHalfExtent.IsNearlyZero()) return Distance;
    for (int32 X : {-1, 1}) for (int32 Y : {-1, 1}) for (int32 Z : {-1, 1})
    {
        const FVector Corner = FocusHalfExtent * FVector(X, Y, Z);
        const double Depth = FVector::DotProduct(Corner, Footprint.Forward);
        Distance = FMath::Max(Distance, static_cast<float>(FMath::Abs(FVector::DotProduct(Corner, Footprint.Right))
            / (Footprint.TanHalfX * 0.80) - Depth));
        Distance = FMath::Max(Distance, static_cast<float>(FMath::Abs(FVector::DotProduct(Corner, Footprint.Up))
            / (Footprint.TanHalfY * 0.80) - Depth));
    }
    return Distance + (FocusHalfExtent.IsNearlyZero() ? 0.0f : 0.1f);
}

void ACinderCamera::UpdateTargetBounds(const FGroundFootprint& Footprint)
{
    if (TerrainSource.IsValid())
    {
        FocusAnchor.Z = SurfaceHeight(FocusAnchor) + FocusHalfExtent.Z;
        TargetPosition.Z = SurfaceHeight(TargetPosition) + (bKeepFocusVisible ? FocusHalfExtent.Z : 0.0f);
    }
    const float MaxDistance = MaximumDistance(Footprint, bKeepFocusVisible ? FocusAnchor.Z : TargetPosition.Z);
    const float MinDistance = FMath::Min(bKeepFocusVisible ? MinimumSubjectDistance(Footprint) : ClosestDistance, MaxDistance);
    TargetDistance = FMath::Clamp(TargetDistance, MinDistance, MaxDistance);
    if (bKeepFocusVisible && Footprint.bValid)
    {
        // Near a diagonal map corner, the bounded view may need a closer zoom to frame a focus.
        FVector FramedPosition;
        if (!FindFocusPosition(TargetDistance, Footprint, FramedPosition))
        {
            float Low = MinDistance, High = TargetDistance;
            // Exact corners can still lie outside the inner focus region of a rotated view.
            const bool bFitsAtMinimum = FindFocusPosition(Low, Footprint, FramedPosition);
            for (int I = 0; bFitsAtMinimum && I < 18; ++I)
            {
                const float Mid = (Low + High) * 0.5f;
                FVector Candidate;
                if (FindFocusPosition(Mid, Footprint, Candidate)) Low = Mid; else High = Mid;
            }
            TargetDistance = Low;
            if (!FindFocusPosition(TargetDistance, Footprint, FramedPosition))
                FramedPosition = BoundPosition(FocusAnchor, TargetDistance, Footprint);
        }
        TargetPosition = FramedPosition;
    }
    else TargetPosition = BoundPosition(TargetPosition, TargetDistance, Footprint);
}

void ACinderCamera::Pan(FVector Delta)
{
    if (Delta.IsNearlyZero()) return;
    bKeepFocusVisible = false;
    FocusHalfExtent = FVector::ZeroVector;
    TargetPosition += Delta;
    UpdateTargetBounds(GroundFootprint());
}
void ACinderCamera::Focus(FVector Position, bool bInstant, FVector FramingExtent)
{
    FocusHalfExtent = FramingExtent.GetAbs().ComponentMin(FVector(400.0f));
    FocusAnchor = FVector(FMath::Clamp(Position.X, 0.0, static_cast<double>(ActiveWorldSize)),
        FMath::Clamp(Position.Y, 0.0, static_cast<double>(ActiveWorldSize)), SurfaceHeight(Position) + FocusHalfExtent.Z);
    bKeepFocusVisible = true;
    UpdateTargetBounds(GroundFootprint());
    if (bInstant) { SetActorLocation(TargetPosition); Boom->TargetArmLength = TargetDistance; }
}
void ACinderCamera::Zoom(float Amount)
{
    TargetDistance += Amount;
    UpdateTargetBounds(GroundFootprint());
}

void ACinderCamera::SetWorldSize(float InWorldSize)
{
    const float Sanitized = FMath::IsFinite(InWorldSize) && InWorldSize > 1.0f
        ? InWorldSize : cinder::Simulation::WorldSize;
    if (FMath::IsNearlyEqual(ActiveWorldSize, Sanitized)) return;
    ActiveWorldSize = Sanitized;
    FocusAnchor.X = FMath::Clamp(FocusAnchor.X, 0.0, static_cast<double>(ActiveWorldSize));
    FocusAnchor.Y = FMath::Clamp(FocusAnchor.Y, 0.0, static_cast<double>(ActiveWorldSize));
    const FGroundFootprint Footprint = GroundFootprint();
    UpdateTargetBounds(Footprint);
    Boom->TargetArmLength = FMath::Min(Boom->TargetArmLength, MaximumDistance(Footprint, GetActorLocation().Z));
    SetActorLocation(BoundPosition(GetActorLocation(), Boom->TargetArmLength, Footprint));
}
