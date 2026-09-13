#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInterface.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderHUD.h"
#include "Presentation/CinderMobileHUDLayout.h"
#include "Presentation/CinderPlayerController.h"
#include "Sim/MatchLength.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderMatchLengthPreview, Log, All);

namespace
{
struct FMatchLengthPreviewRequest
{
    TWeakObjectPtr<UWorld> World;
    FString State;
    cinder::MatchLength Length = cinder::MatchLength::Standard;
    int32 LengthIndex = 1;
    int32 Map = 0;
    int32 Attempts = 0;
    int32 FocusAttempts = 0;
    int32 FocusCoordinate = -1;
    cinder::Id VisionScout = 0;
    bool bMap = false;
    bool bMapSelected = false;
};

FMatchLengthPreviewRequest PreviewRequest;
FTimerHandle PreviewReadyTimer;
FTimerHandle PreviewFocusTimer;
FTimerHandle PreviewCaptureTimer;

void ClearPreviewTimers()
{
    if (UWorld* World = PreviewRequest.World.Get())
    {
        World->GetTimerManager().ClearTimer(PreviewReadyTimer);
        World->GetTimerManager().ClearTimer(PreviewFocusTimer);
        World->GetTimerManager().ClearTimer(PreviewCaptureTimer);
    }
    PreviewReadyTimer.Invalidate();
    PreviewFocusTimer.Invalidate();
    PreviewCaptureTimer.Invalidate();
}

void FailPreview(const FString& Reason)
{
    UE_LOG(LogCinderMatchLengthPreview, Error,
        TEXT("CINDERLINE_MATCH_LENGTH_PREVIEW failed=%s state=%s"), *Reason, *PreviewRequest.State);
    ClearPreviewTimers();
    PreviewRequest = {};
}

void CapturePreview()
{
    UWorld* World = PreviewRequest.World.Get();
    ACinderPlayerController* PC = World
        ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
    ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
    ACinderHUD* HUD = PC ? Cast<ACinderHUD>(PC->GetHUD()) : nullptr;
    ACinderCamera* Rig = PC ? Cast<ACinderCamera>(PC->GetPawn()) : nullptr;
    const bool bExpectedMenu = !PreviewRequest.bMap;
    if (!World || !World->IsGameWorld() || !PC || !Battle || !HUD
        || Battle->IsOnlineMatch() || Battle->IsMenu() != bExpectedMenu
        || PC->SelectedMatchLength() != PreviewRequest.Length
        || (PreviewRequest.bMap && (Battle->MatchLength() != PreviewRequest.Length
            || !Rig || Battle->MapIndex() != PreviewRequest.Map)))
    {
        FailPreview(TEXT("state_changed_before_capture"));
        return;
    }

    HUD->LogMobileLayout();
    const cinder::MatchLength ActiveLength = bExpectedMenu
        ? PC->SelectedMatchLength() : Battle->MatchLength();
    const float ActiveWorldSize = cinder::matchLengthProfile(ActiveLength).worldSize;
    const cinder::MatchLength SimulationLength = Battle->Sim().config().matchLength;
    const float SimulationWorldSize = Battle->Sim().worldSize();
    const float FarCoordinate = PreviewRequest.FocusCoordinate;
    const FVector RigLocation = Rig ? Rig->GetActorLocation() : FVector::ZeroVector;
    FString FogMaterialName = TEXT("none"), GroundMaterialName = TEXT("none");
    float FogWorldSizeInverse = 0.0f, GroundWorldSizeInverse = 0.0f;
    if (PreviewRequest.bMap)
    {
        TArray<UInstancedStaticMeshComponent*> Components;
        Battle->GetComponents(Components);
        for (UInstancedStaticMeshComponent* Component : Components)
        {
            UMaterialInterface* Material = Component ? Component->GetMaterial(0) : nullptr;
            UMaterial* Base = Material ? Material->GetBaseMaterial() : nullptr;
            if (!Base) continue;
            const FString Name = Base->GetName();
            float Value = 0.0f;
            if (Name == TEXT("M_CinderFogV2") && Material->GetScalarParameterValue(
                FMaterialParameterInfo(TEXT("CinderWorldSizeInverse")), Value))
            {
                FogMaterialName = Name;
                FogWorldSizeInverse = Value;
            }
            else if ((Name == TEXT("M_CinderCanyonGround") || Name == TEXT("M_CinderGroundV4")
                || Name == TEXT("M_CinderGroundV3")) && Material->GetScalarParameterValue(
                    FMaterialParameterInfo(TEXT("CinderWorldSizeInverse")), Value))
            {
                GroundMaterialName = Name;
                GroundWorldSizeInverse = Value;
            }
        }
        const float ExpectedInverse = 1.0f / FMath::Max(1.0f, ActiveWorldSize);
        if (FogMaterialName == TEXT("none") || GroundMaterialName == TEXT("none")
            || !FMath::IsNearlyEqual(FogWorldSizeInverse, ExpectedInverse, 1.e-8f)
            || !FMath::IsNearlyEqual(GroundWorldSizeInverse, ExpectedInverse, 1.e-8f))
        {
            FailPreview(TEXT("world_size_material_parameters_invalid"));
            return;
        }
    }
    int32 Width = 0, Height = 0;
    PC->GetViewportSize(Width, Height);

    const FString Directory = FPaths::ProjectSavedDir() / TEXT("MatchLength");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = Directory / (PreviewRequest.State + TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(Filename, false, false);
    UE_LOG(LogCinderMatchLengthPreview, Display,
        TEXT("CINDERLINE_MATCH_LENGTH_PREVIEW state=%s menu=%d selected_length=%d config_length=%d config_name=%s config_world_size=%.1f simulation_length=%d simulation_world_size=%.1f map=%d far_focus=%d scripted_scout=%u focus=(%.1f,%.1f) rig=(%.1f,%.1f) distance=%.1f fog_material=%s fog_inverse=%.9f ground_material=%s ground_inverse=%.9f viewport=%dx%d file=%s"),
        *PreviewRequest.State, bExpectedMenu ? 1 : 0,
        static_cast<int32>(PC->SelectedMatchLength()), static_cast<int32>(ActiveLength),
        UTF8_TO_TCHAR(cinder::matchLengthName(ActiveLength)), ActiveWorldSize,
        static_cast<int32>(SimulationLength), SimulationWorldSize, Battle->MapIndex(),
        PreviewRequest.bMap ? 1 : 0, PreviewRequest.VisionScout, FarCoordinate, FarCoordinate,
        RigLocation.X, RigLocation.Y, Rig ? Rig->Distance() : 0.0f,
        *FogMaterialName, FogWorldSizeInverse, *GroundMaterialName, GroundWorldSizeInverse,
        Width, Height, *Filename);
    ClearPreviewTimers();
    PreviewRequest = {};
}

void FocusMapPreview()
{
    UWorld* World = PreviewRequest.World.Get();
    ACinderPlayerController* PC = World
        ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
    ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
    ACinderHUD* HUD = PC ? Cast<ACinderHUD>(PC->GetHUD()) : nullptr;
    if (!World || !PC || !Battle || !HUD || Battle->IsMenu() || Battle->IsOnlineMatch())
    {
        FailPreview(TEXT("map_changed_before_minimap_tap"));
        return;
    }
    if (++PreviewRequest.FocusAttempts > 50)
    {
        FailPreview(TEXT("minimap_not_ready"));
        return;
    }
    int32 Width = 0, Height = 0;
    PC->GetViewportSize(Width, Height);
    const bool bCompact = FParse::Param(FCommandLine::Get(), TEXT("mobilehud"))
        || Width > Height * 2 || Height < 500;
    const FCinderMobileHUDLayout Layout = FCinderMobileHUDLayout::Make(
        FVector2D(Width, Height), FVector4(0, 0, 0, 0), !bCompact);
    constexpr float TapFraction = 0.93f;
    const FVector2D TapPoint = Layout.Minimap.Min + Layout.Minimap.GetSize() * TapFraction;
    if (!HUD->HandleTap(TapPoint)) return;

    PreviewRequest.FocusCoordinate = static_cast<int32>(Battle->Sim().worldSize() * TapFraction);
    Battle->SetActorTickEnabled(false);
    World->GetTimerManager().ClearTimer(PreviewFocusTimer);
    World->GetTimerManager().SetTimer(PreviewCaptureTimer,
        FTimerDelegate::CreateStatic(&CapturePreview), 2.0f, false);
}

void ConfigurePreview()
{
    UWorld* World = PreviewRequest.World.Get();
    ACinderPlayerController* PC = World
        ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
    ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
    ACinderHUD* HUD = PC ? Cast<ACinderHUD>(PC->GetHUD()) : nullptr;
    if (!World || !World->IsGameWorld() || !PC || !Battle || !HUD
        || Battle->IsOnlineMatch() || !Battle->IsMenu() || PC->IsHelpOpen())
    {
        FailPreview(TEXT("menu_changed_before_configuration"));
        return;
    }
    if (++PreviewRequest.Attempts > 100)
    {
        FailPreview(TEXT("menu_buttons_not_ready"));
        return;
    }
    if (PC->IsTutorialOfferPending())
    {
        HUD->TapPreviewAction(TEXT("onboardskip"));
        return;
    }
    if (!HUD->TapPreviewAction(TEXT("matchlength"), PreviewRequest.LengthIndex)) return;
    if (PC->SelectedMatchLength() != PreviewRequest.Length)
    {
        FailPreview(TEXT("length_button_rejected"));
        return;
    }

    if (PreviewRequest.bMap)
    {
        // Selecting a map changes the next rendered Start button's argument.
        // Wait for that real button to redraw before pressing it.
        if (!PreviewRequest.bMapSelected)
        {
            if (!HUD->TapPreviewAction(TEXT("map"), PreviewRequest.Map)) return;
            PreviewRequest.bMapSelected = true;
            return;
        }
        if (!HUD->TapPreviewAction(TEXT("start"), PreviewRequest.Map)
            || Battle->IsMenu() || Battle->MatchLength() != PreviewRequest.Length)
        {
            FailPreview(TEXT("map_start_button_rejected"));
            return;
        }
        Battle->SetActorTickEnabled(false);
        const float Far = Battle->Sim().worldSize() * 0.93f;
        PreviewRequest.VisionScout = Battle->Sim().debugSpawn(cinder::Kind::Scout, 0, {Far, Far});
        if (!PreviewRequest.VisionScout)
        {
            FailPreview(TEXT("far_vision_fixture_failed"));
            return;
        }
        Battle->RenderState();
        if (!Cast<ACinderCamera>(PC->GetPawn()))
        {
            FailPreview(TEXT("camera_missing"));
            return;
        }
        World->GetTimerManager().ClearTimer(PreviewReadyTimer);
        World->GetTimerManager().SetTimer(PreviewFocusTimer,
            FTimerDelegate::CreateStatic(&FocusMapPreview), 0.1f, true, 0.2f);
        return;
    }

    World->GetTimerManager().ClearTimer(PreviewReadyTimer);
    World->GetTimerManager().SetTimer(PreviewCaptureTimer,
        FTimerDelegate::CreateStatic(&CapturePreview), 0.8f, false);
}

FAutoConsoleCommandWithWorldAndArgs MatchLengthPreviewCommand(
    TEXT("cinder.matchlengthpreview"),
    TEXT("DEVELOPMENT: unattended fresh local menu only. Uses rendered menu buttons to capture menu-short|menu-standard|menu-long|map-short|map-long without writing saves or preferences."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        const FString State = Args.Num() == 1 ? Args[0].ToLower() : FString();
        const bool bMenu = State.StartsWith(TEXT("menu-"));
        const bool bMap = State.StartsWith(TEXT("map-"));
        const FString LengthName = State.Mid(State.Find(TEXT("-")) + 1);
        int32 LengthIndex = INDEX_NONE;
        if (LengthName == TEXT("short")) LengthIndex = 0;
        else if (LengthName == TEXT("standard")) LengthIndex = 1;
        else if (LengthName == TEXT("long")) LengthIndex = 2;
        ACinderPlayerController* PC = World
            ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        if (Args.Num() != 1 || (!bMenu && !bMap) || LengthIndex == INDEX_NONE
            || (bMap && LengthIndex == 1) || !FApp::IsUnattended() || !World
            || !World->IsGameWorld() || !PC || !Battle || !Battle->IsMenu()
            || Battle->IsOnlineMatch() || PC->IsHelpOpen())
        {
            UE_LOG(LogCinderMatchLengthPreview, Warning,
                TEXT("CINDERLINE_MATCH_LENGTH_PREVIEW refused=requires_unattended_fresh_local_menu_and_state_menu-short_menu-standard_menu-long_map-short_map-long"));
            return;
        }

        ClearPreviewTimers();
        PreviewRequest.World = World;
        PreviewRequest.State = State;
        PreviewRequest.LengthIndex = LengthIndex;
        PreviewRequest.Length = cinder::matchLengthAt(LengthIndex);
        PreviewRequest.Map = LengthIndex == 2 ? 2 : 0;
        PreviewRequest.Attempts = 0;
        PreviewRequest.FocusAttempts = 0;
        PreviewRequest.FocusCoordinate = -1;
        PreviewRequest.VisionScout = 0;
        PreviewRequest.bMap = bMap;
        PreviewRequest.bMapSelected = false;
        World->GetTimerManager().SetTimer(PreviewReadyTimer,
            FTimerDelegate::CreateStatic(&ConfigurePreview), 0.1f, true, 0.1f);
    }));
}

#endif
