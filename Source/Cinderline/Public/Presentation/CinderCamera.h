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
    float Distance() const { return TargetDistance; }
private:
    UPROPERTY() TObjectPtr<USpringArmComponent> Boom;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    FVector TargetPosition = FVector(700, 700, 0);
    float TargetDistance = 1650;
};
