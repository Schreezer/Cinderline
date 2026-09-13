#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderHUD.h"
#include "Presentation/CinderPlayerController.h"
#include "Sim/AIDifficulty.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderDifficultyPreview, Log, All);

namespace
{
TWeakObjectPtr<UWorld> DifficultyCaptureWorld;
FTimerHandle DifficultyCaptureTimer;

FAutoConsoleCommandWithWorldAndArgs DifficultyPreviewCommand(
    TEXT("cinder.difficultypreview"),
    TEXT("DEVELOPMENT: select main-menu AI level 0..4 and capture after 2 seconds. Refuses active matches. Use -unattended to keep preview choices out of player preferences."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        int32 Index = -1;
        ACinderPlayerController* PC = World
            ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        if (Args.Num() != 1 || !LexTryParseString(Index, *Args[0])
            || Index < 0 || Index >= static_cast<int32>(cinder::kAIDifficultyCount) || !World
            || !World->IsGameWorld() || !PC || !Battle || !Battle->IsMenu()
            || Battle->IsOnlineMatch() || PC->IsHelpOpen())
        {
            UE_LOG(LogCinderDifficultyPreview, Warning,
                TEXT("CINDERLINE_DIFFICULTY_PREVIEW refused=requires_local_main_menu_and_level_0_to_4"));
            return;
        }

        if (UWorld* Previous = DifficultyCaptureWorld.Get())
            Previous->GetTimerManager().ClearTimer(DifficultyCaptureTimer);
        PC->ExecuteAction(TEXT("difficulty"), Index);
        DifficultyCaptureWorld = World;
        const TWeakObjectPtr<UWorld> WeakWorld(World);
        const TWeakObjectPtr<ACinderPlayerController> WeakPC(PC);
        World->GetTimerManager().SetTimer(DifficultyCaptureTimer,
            FTimerDelegate::CreateLambda([WeakWorld, WeakPC, Index]
            {
                UWorld* CurrentWorld = WeakWorld.Get();
                ACinderPlayerController* CurrentPC = WeakPC.Get();
                ACinderBattlefield* CurrentBattle = CurrentPC ? CurrentPC->Battlefield() : nullptr;
                ACinderHUD* HUD = CurrentPC ? Cast<ACinderHUD>(CurrentPC->GetHUD()) : nullptr;
                if (!CurrentWorld || !CurrentPC || CurrentPC->GetWorld() != CurrentWorld
                    || !CurrentBattle || !CurrentBattle->IsMenu() || CurrentBattle->IsOnlineMatch()
                    || CurrentPC->IsHelpOpen() || !HUD)
                    return;
                HUD->LogMobileLayout();
                const FString Directory = FPaths::ProjectSavedDir() / TEXT("AIDifficulty");
                IFileManager::Get().MakeDirectory(*Directory, true);
                const FString Filename = Directory / FString::Printf(TEXT("menu-%d.png"), Index);
                FScreenshotRequest::RequestScreenshot(Filename, false, false);
                const auto Selected = CurrentPC->SelectedAIDifficulty();
                UE_LOG(LogCinderDifficultyPreview, Display,
                    TEXT("CINDERLINE_DIFFICULTY_PREVIEW requested=%d selected=%d name=%s file=%s"),
                    Index, static_cast<int32>(Selected), UTF8_TO_TCHAR(cinder::aiDifficultyName(Selected)), *Filename);
            }), 2.0f, false);
    }));
}

#endif
