#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CinderGameMode.generated.h"

UCLASS()
class CINDERLINE_API ACinderGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ACinderGameMode();
    virtual void StartPlay() override;
};
