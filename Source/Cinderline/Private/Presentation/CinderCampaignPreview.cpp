#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderHUD.h"
#include "Presentation/CinderPlayerController.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderCampaignPreview, Log, All);

namespace
{
FAutoConsoleCommandWithWorldAndArgs CampaignPreviewCommand(
    TEXT("cinder.campaignpreview"),
    TEXT("Unattended fresh-menu visual fixtures: menu, worker, train, pause, defeat, victory, finale."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || !FApp::IsUnattended()) return;
        const FString State = Args.Num() ? Args[0].ToLower() : TEXT("menu");
        const TArray<FString> States{TEXT("menu"), TEXT("worker"), TEXT("train"), TEXT("pause"), TEXT("defeat"), TEXT("victory"), TEXT("finale")};
        if (!States.Contains(State)) return;
        ACinderPlayerController* PC = nullptr;
        if (TActorIterator<ACinderPlayerController> It(World); It) PC = *It;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        if (!PC || !Battle || !Battle->IsMenu() || Battle->IsOnlineMatch())
        {
            UE_LOG(LogCinderCampaignPreview, Warning, TEXT("CINDERLINE_CAMPAIGN_PREVIEW refused=fresh-menu-required"));
            return;
        }
        PC->ExecuteAction(TEXT("campaignmenu"));
        if (State != TEXT("menu"))
        {
            PC->ExecuteAction(TEXT("campaignstart"), State == TEXT("finale") ? 5 : 0);
            if (!Battle->Campaign().IsActive())
            { UE_LOG(LogCinderCampaignPreview, Error, TEXT("CINDERLINE_CAMPAIGN_PREVIEW failed=initialization")); return; }
            if (State == TEXT("worker") || State == TEXT("train")) Battle->GuidanceCameraInput();
            if (State == TEXT("pause")) Battle->SetPaused(true);
            // Result views are authored diagnostic fixtures, not playthrough evidence.
            if (State == TEXT("defeat") || State == TEXT("victory"))
            {
                Battle->Sim().forfeit(State == TEXT("defeat") ? 0 : 1);
                Battle->Campaign().Observe(Battle->Sim(), {});
            }
        }
        if (State == TEXT("train"))
        {
            // A diagnostic objective fixture, not evidence of completing earlier lessons.
            FCinderCampaignState TrainingState = Battle->Campaign().ExportState();
            TrainingState.Phase = TrainingState.CheckpointPhase = 3;
            TrainingState.CompletedObjectiveMask = 7;
            TrainingState.bCameraObserved = true;
            FString Error;
            if (!Battle->Campaign().ImportState(Battle->Sim(), TrainingState, Error))
            { UE_LOG(LogCinderCampaignPreview, Error, TEXT("CINDERLINE_CAMPAIGN_PREVIEW failed=train-fixture %s"), *Error); return; }
            if (auto* HUD = Cast<ACinderHUD>(PC->GetHUD())) HUD->OpenGlobalPanel(TEXT("train"));
        }
        TWeakObjectPtr<ACinderPlayerController> WeakPC(PC);
        FTimerHandle Timer;
        World->GetTimerManager().SetTimer(Timer, [WeakPC, State]()
        {
            auto* Controller = WeakPC.Get();
            auto* HUD = Controller ? Cast<ACinderHUD>(Controller->GetHUD()) : nullptr;
            if (!Controller || !HUD) return;
            HUD->LogCampaignHUD();
            const FString Directory = FPaths::ProjectSavedDir() / TEXT("CampaignPreview");
            IFileManager::Get().MakeDirectory(*Directory, true);
            FScreenshotRequest::RequestScreenshot(Directory / (State + TEXT(".png")), false, false);
            UE_LOG(LogCinderCampaignPreview, Display, TEXT("CINDERLINE_CAMPAIGN_PREVIEW state=%s fixture=1"), *State);
        }, 0.65f, false);
    }));
}
#endif
