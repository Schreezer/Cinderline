#include "Presentation/CinderCamera.h"
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
constexpr float MinimumZoom = 650;
constexpr float MaximumZoom = 3200;
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
    Camera->PostProcessSettings.AmbientOcclusionIntensity = 0.4f;
    Camera->PostProcessSettings.bOverride_AmbientOcclusionRadius = true;
    Camera->PostProcessSettings.AmbientOcclusionRadius = 25.0f;
    Camera->PostProcessSettings.bOverride_AmbientOcclusionRadiusInWS = true;
    Camera->PostProcessSettings.AmbientOcclusionRadiusInWS = true;
    Camera->PostProcessSettings.bOverride_AmbientOcclusionQuality = true;
    Camera->PostProcessSettings.AmbientOcclusionQuality = 80.0f;
#endif
    Camera->PostProcessSettings.bOverride_BloomIntensity = true;
    Camera->PostProcessSettings.BloomIntensity = 0.24f;
    Camera->PostProcessSettings.bOverride_BloomThreshold = true;
    Camera->PostProcessSettings.BloomThreshold = 1.0f;
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Camera->PostProcessSettings.MotionBlurAmount = 0.0f;
    Camera->PostProcessSettings.bOverride_DepthOfFieldEnabled = true;
    Camera->PostProcessSettings.DepthOfFieldEnabled = false;
    SetActorLocation(TargetPosition);
}

void ACinderCamera::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const FGroundFootprint Footprint = GroundFootprint();
    UpdateTargetBounds(Footprint);
    // Recheck the interpolated view too: zoom and translation use different smoothing rates.
    Boom->TargetArmLength = FMath::Min(MaximumDistance(Footprint), FMath::FInterpTo(Boom->TargetArmLength, TargetDistance, DeltaSeconds, 12));
    SetActorLocation(BoundPosition(FMath::VInterpTo(GetActorLocation(), TargetPosition, DeltaSeconds, 14), Boom->TargetArmLength, Footprint));
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

float ACinderCamera::MaximumDistance(const FGroundFootprint& Footprint) const
{
    if (!Footprint.bValid) return MinimumZoom;
    const FVector2D Span = Footprint.Max - Footprint.Min;
    const double Available = ActiveWorldSize - ZoomFitInset * 2;
    // An unusually tall viewport may need a zoom cap below the usual minimum.
    return static_cast<float>(FMath::Max(1.0, FMath::Min(static_cast<double>(MaximumZoom), Available / FMath::Max(Span.X, Span.Y))));
}

FVector ACinderCamera::BoundPosition(FVector Position, float Distance, const FGroundFootprint& Footprint) const
{
    const double World = ActiveWorldSize;
    if (!Footprint.bValid) return FVector(World * 0.5, World * 0.5, 0);
    // Reveal a finite black rim at the edge. Scale it with the view so close
    // zooms cannot drift far into empty space, and cap it for distant views.
    const FVector2D Span = (Footprint.Max - Footprint.Min) * Distance;
    const double BorderMargin = FMath::Clamp(FMath::Min(Span.X, Span.Y) * BorderViewFraction,
        MinimumBorderMargin, MaximumBorderMargin);
    const FVector2D Low = FVector2D(-BorderMargin) - Footprint.Min * Distance;
    const FVector2D High = FVector2D(World + BorderMargin) - Footprint.Max * Distance;
    auto ClampAxis = [](double Value, double Min, double Max) { return Min <= Max ? FMath::Clamp(Value, Min, Max) : (Min + Max) * 0.5; };
    return FVector(ClampAxis(Position.X, Low.X, High.X), ClampAxis(Position.Y, Low.Y, High.Y), 0);
}

bool ACinderCamera::ContainsFocus(FVector Position, float Distance, const FGroundFootprint& Footprint) const
{
    const FVector ToFocus = FocusAnchor - (Position - Footprint.Forward * Distance);
    const double Depth = FVector::DotProduct(ToFocus, Footprint.Forward);
    if (Depth <= 0) return false;
    // Keep the focus away from the screen edge so an Anchor's silhouette remains readable.
    return FMath::Abs(FVector::DotProduct(ToFocus, Footprint.Right)) <= Depth * Footprint.TanHalfX * 0.80
        && FMath::Abs(FVector::DotProduct(ToFocus, Footprint.Up)) <= Depth * Footprint.TanHalfY * 0.80;
}

void ACinderCamera::UpdateTargetBounds(const FGroundFootprint& Footprint)
{
    const float MaxDistance = MaximumDistance(Footprint);
    const float MinDistance = FMath::Min(MinimumZoom, MaxDistance);
    TargetDistance = FMath::Clamp(TargetDistance, MinDistance, MaxDistance);
    if (bKeepFocusVisible && Footprint.bValid)
    {
        // Near a diagonal map corner, the bounded view may need a closer zoom to frame a focus.
        if (!ContainsFocus(BoundPosition(FocusAnchor, TargetDistance, Footprint), TargetDistance, Footprint))
        {
            float Low = MinDistance, High = TargetDistance;
            // Exact corners can still lie outside the inner focus region of a rotated view.
            const bool bFitsAtMinimum = ContainsFocus(BoundPosition(FocusAnchor, Low, Footprint), Low, Footprint);
            for (int I = 0; bFitsAtMinimum && I < 18; ++I)
            {
                const float Mid = (Low + High) * 0.5f;
                if (ContainsFocus(BoundPosition(FocusAnchor, Mid, Footprint), Mid, Footprint)) Low = Mid; else High = Mid;
            }
            TargetDistance = Low;
        }
        TargetPosition = BoundPosition(FocusAnchor, TargetDistance, Footprint);
    }
    else TargetPosition = BoundPosition(TargetPosition, TargetDistance, Footprint);
}

void ACinderCamera::Pan(FVector Delta)
{
    if (Delta.IsNearlyZero()) return;
    bKeepFocusVisible = false;
    TargetPosition += Delta;
    UpdateTargetBounds(GroundFootprint());
}
void ACinderCamera::Focus(FVector Position, bool bInstant)
{
    FocusAnchor = FVector(FMath::Clamp(Position.X, 0.0, static_cast<double>(ActiveWorldSize)), FMath::Clamp(Position.Y, 0.0, static_cast<double>(ActiveWorldSize)), 0);
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
    Boom->TargetArmLength = FMath::Min(Boom->TargetArmLength, MaximumDistance(Footprint));
    SetActorLocation(BoundPosition(GetActorLocation(), Boom->TargetArmLength, Footprint));
}
