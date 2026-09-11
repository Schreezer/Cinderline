#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderHUD.h"
#include "Presentation/CinderPlayerController.h"
#include "EngineUtils.h"

ACinderGameMode::ACinderGameMode()
{
    DefaultPawnClass = ACinderCamera::StaticClass();
    PlayerControllerClass = ACinderPlayerController::StaticClass();
    HUDClass = ACinderHUD::StaticClass();
}
void ACinderGameMode::StartPlay()
{
    bool bHasBattlefield = false;
    for (TActorIterator<ACinderBattlefield> It(GetWorld()); It; ++It) { bHasBattlefield = true; break; }
    if (!bHasBattlefield) GetWorld()->SpawnActor<ACinderBattlefield>();
    Super::StartPlay();
}
