#include "Presentation/CinderCamera.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

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
    Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Camera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
    Camera->PostProcessSettings.AutoExposureBias = 0;
    SetActorLocation(TargetPosition);
}

void ACinderCamera::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    SetActorLocation(FMath::VInterpTo(GetActorLocation(), TargetPosition, DeltaSeconds, 14));
    Boom->TargetArmLength = FMath::FInterpTo(Boom->TargetArmLength, TargetDistance, DeltaSeconds, 12);
}

void ACinderCamera::Pan(FVector Delta) { Focus(TargetPosition + Delta); }
void ACinderCamera::Focus(FVector Position, bool bInstant)
{
    TargetPosition = FVector(FMath::Clamp(Position.X, 150.0, 4650.0), FMath::Clamp(Position.Y, 150.0, 4650.0), 0);
    if (bInstant) SetActorLocation(TargetPosition);
}
void ACinderCamera::Zoom(float Amount) { TargetDistance = FMath::Clamp(TargetDistance + Amount, 650.0f, 3200.0f); }
