#include "Presentation/CinderCamera.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Sim/Simulation.h"

namespace
{
constexpr double EdgeInset = 8;
constexpr float MinimumZoom = 650;
constexpr float MaximumZoom = 3200;
}

ACinderCamera::ACinderCamera()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CameraRoot"));
    Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    Boom->SetupAttachment(RootComponent);
    Boom->SetRelativeRotation(FRotator(-60, -45, 0));
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
    const double Available = cinder::Simulation::WorldSize - EdgeInset * 2;
    // An unusually tall viewport may need a zoom cap below the usual minimum.
    return static_cast<float>(FMath::Max(1.0, FMath::Min(static_cast<double>(MaximumZoom), Available / FMath::Max(Span.X, Span.Y))));
}

FVector ACinderCamera::BoundPosition(FVector Position, float Distance, const FGroundFootprint& Footprint) const
{
    constexpr double World = cinder::Simulation::WorldSize;
    if (!Footprint.bValid) return FVector(World * 0.5, World * 0.5, 0);
    const FVector2D Low = FVector2D(EdgeInset) - Footprint.Min * Distance;
    const FVector2D High = FVector2D(World - EdgeInset) - Footprint.Max * Distance;
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
        // Near a diagonal map corner, a contained view cannot always include a focus at full zoom.
        if (!ContainsFocus(BoundPosition(FocusAnchor, TargetDistance, Footprint), TargetDistance, Footprint))
        {
            float Low = MinDistance, High = TargetDistance;
            // Exact map corners can remain outside a contained rotated view even at minimum zoom.
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
    FocusAnchor = FVector(FMath::Clamp(Position.X, 0.0, static_cast<double>(cinder::Simulation::WorldSize)), FMath::Clamp(Position.Y, 0.0, static_cast<double>(cinder::Simulation::WorldSize)), 0);
    bKeepFocusVisible = true;
    UpdateTargetBounds(GroundFootprint());
    if (bInstant) { SetActorLocation(TargetPosition); Boom->TargetArmLength = TargetDistance; }
}
void ACinderCamera::Zoom(float Amount)
{
    TargetDistance += Amount;
    UpdateTargetBounds(GroundFootprint());
}
