#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderPlayerController.h"
#include "Sim/Simulation.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderCameraPreview, Log, All);

namespace
{
TWeakObjectPtr<UWorld> CameraBorderTimerWorld;
FTimerHandle CameraBorderCaptureTimer;

void ClearCameraBorderTimer()
{
    if (UWorld* World = CameraBorderTimerWorld.Get())
        World->GetTimerManager().ClearTimer(CameraBorderCaptureTimer);
    CameraBorderCaptureTimer.Invalidate();
    CameraBorderTimerWorld.Reset();
}

bool BorderDirection(const FString& Edge, FVector& Out)
{
    if (Edge == TEXT("west")) Out = FVector(-1, 0, 0);
    else if (Edge == TEXT("east")) Out = FVector(1, 0, 0);
    else if (Edge == TEXT("north")) Out = FVector(0, 1, 0);
    else if (Edge == TEXT("south")) Out = FVector(0, -1, 0);
    else if (Edge == TEXT("nw")) Out = FVector(-1, 1, 0);
    else if (Edge == TEXT("ne")) Out = FVector(1, 1, 0);
    else if (Edge == TEXT("sw")) Out = FVector(-1, -1, 0);
    else if (Edge == TEXT("se")) Out = FVector(1, -1, 0);
    else if (Edge == TEXT("center")) Out = FVector::ZeroVector;
    else return false;
    return true;
}

void CaptureCameraBorder(UWorld* World, ACinderPlayerController* PC, ACinderCamera* Rig,
    ACinderBattlefield* Battle, const FString& Edge, int32 RequestedDistance, float StartedAt)
{
    if (!World || !World->IsGameWorld() || !PC || !Rig || !Battle || PC->GetWorld() != World || Rig->GetWorld() != World
        || PC->Battlefield() != Battle || PC->GetPawn() != Rig || Battle->IsMenu() || Battle->IsOnlineMatch())
    {
        UE_LOG(LogCinderCameraPreview, Warning,
            TEXT("CINDERLINE_MAP_BORDER edge=%s distance=%d capture=skipped reason=world_or_gameplay_changed"),
            *Edge, RequestedDistance);
        ClearCameraBorderTimer();
        return;
    }

    int32 Width = 0, Height = 0;
    PC->GetViewportSize(Width, Height);
    const FVector2D ScreenCorners[] = {
        FVector2D(0, 0), FVector2D(Width, 0), FVector2D(Width, Height), FVector2D(0, Height)
    };
    cinder::Vec2 GroundCorners[4] = {};
    bool GroundValid[4] = {};
    FVector2D MapCorners[4] = {};
    bool MapValid[4] = {};
    constexpr float WorldSize = cinder::Simulation::WorldSize;
    const FVector MapPoints[] = {
        FVector(0, 0, 0), FVector(WorldSize, 0, 0),
        FVector(WorldSize, WorldSize, 0), FVector(0, WorldSize, 0)
    };
    for (int32 Index = 0; Index < 4; ++Index)
    {
        GroundValid[Index] = PC->GroundPoint(ScreenCorners[Index], GroundCorners[Index]);
        MapValid[Index] = PC->ProjectWorldLocationToScreen(MapPoints[Index], MapCorners[Index], false);
    }

    const USpringArmComponent* Boom = Rig->FindComponentByClass<USpringArmComponent>();
    const float ActualDistance = Boom ? Boom->TargetArmLength : Rig->Distance();
    const FVector RigLocation = Rig->GetActorLocation();
    const FVector CameraLocation = PC->PlayerCameraManager
        ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("MapBorder");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = Directory / FString::Printf(TEXT("%s-%d.png"), *Edge, RequestedDistance);
    FScreenshotRequest::RequestScreenshot(Filename, false, false);

    UE_LOG(LogCinderCameraPreview, Display,
        TEXT("CINDERLINE_MAP_BORDER edge=%s requested_distance=%d actual_distance=%.1f rig=(%.1f,%.1f,%.1f) camera=(%.1f,%.1f,%.1f) viewport=%dx%d elapsed=%.2f file=%s"),
        *Edge, RequestedDistance, ActualDistance, RigLocation.X, RigLocation.Y, RigLocation.Z,
        CameraLocation.X, CameraLocation.Y, CameraLocation.Z, Width, Height,
        World->GetTimeSeconds() - StartedAt, *Filename);
    UE_LOG(LogCinderCameraPreview, Display,
        TEXT("CINDERLINE_MAP_BORDER_GROUND edge=%s screen_tl=%d:(%.1f,%.1f) screen_tr=%d:(%.1f,%.1f) screen_br=%d:(%.1f,%.1f) screen_bl=%d:(%.1f,%.1f) map_sw=%d:(%.1f,%.1f) map_se=%d:(%.1f,%.1f) map_ne=%d:(%.1f,%.1f) map_nw=%d:(%.1f,%.1f)"),
        *Edge,
        GroundValid[0], GroundCorners[0].x, GroundCorners[0].y,
        GroundValid[1], GroundCorners[1].x, GroundCorners[1].y,
        GroundValid[2], GroundCorners[2].x, GroundCorners[2].y,
        GroundValid[3], GroundCorners[3].x, GroundCorners[3].y,
        MapValid[0], MapCorners[0].X, MapCorners[0].Y,
        MapValid[1], MapCorners[1].X, MapCorners[1].Y,
        MapValid[2], MapCorners[2].X, MapCorners[2].Y,
        MapValid[3], MapCorners[3].X, MapCorners[3].Y);
    ClearCameraBorderTimer();
}

FAutoConsoleCommandWithWorldAndArgs CameraBorderCommand(
    TEXT("cinder.cameraborder"),
    TEXT("DEVELOPMENT: on an existing local match, focus center or push the real camera clamp toward west|east|north|south|nw|ne|sw|se, then capture after 2s. Optional distance is the camera's own closest..farthest range, default the opening view. Run cinder.artpreview battlelive first for the standard scene. Never resets or saves a match."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        const FString Edge = Args.IsEmpty() ? FString() : Args[0].ToLower();
        FVector Direction;
        if (!BorderDirection(Edge, Direction) || Args.Num() > 2)
        {
            UE_LOG(LogCinderCameraPreview, Warning,
                TEXT("CINDERLINE_MAP_BORDER refused=usage expected=cinder.cameraborder_<west|east|north|south|nw|ne|sw|se|center>_[650..3200]"));
            return;
        }

        int32 Distance = static_cast<int32>(ACinderCamera::DefaultDistance);
        if (Args.Num() == 2 && (!LexTryParseString(Distance, *Args[1])
            || Distance < static_cast<int32>(ACinderCamera::ClosestDistance)
            || Distance > static_cast<int32>(ACinderCamera::FarthestDistance)))
        {
            UE_LOG(LogCinderCameraPreview, Warning,
                TEXT("CINDERLINE_MAP_BORDER edge=%s refused=invalid_distance value=%s allowed=%d..%d"),
                *Edge, *Args[1], static_cast<int32>(ACinderCamera::ClosestDistance),
                static_cast<int32>(ACinderCamera::FarthestDistance));
            return;
        }

        ACinderPlayerController* PC = World
            ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        ACinderCamera* Rig = PC ? Cast<ACinderCamera>(PC->GetPawn()) : nullptr;
        if (!World || !World->IsGameWorld() || !PC || !Battle || !Rig)
        {
            UE_LOG(LogCinderCameraPreview, Warning,
                TEXT("CINDERLINE_MAP_BORDER edge=%s distance=%d refused=missing_gameplay_world"), *Edge, Distance);
            return;
        }
        if (Battle->IsMenu())
        {
            UE_LOG(LogCinderCameraPreview, Warning,
                TEXT("CINDERLINE_MAP_BORDER edge=%s distance=%d refused=menu run=cinder.artpreview_battlelive_first"),
                *Edge, Distance);
            return;
        }
        if (Battle->IsOnlineMatch())
        {
            UE_LOG(LogCinderCameraPreview, Warning,
                TEXT("CINDERLINE_MAP_BORDER edge=%s distance=%d refused=online_match"), *Edge, Distance);
            return;
        }

        ClearCameraBorderTimer();
        constexpr float WorldSize = cinder::Simulation::WorldSize;
        Rig->Focus(FVector(WorldSize * 0.5f, WorldSize * 0.5f, 0), true);
        Rig->Zoom(static_cast<float>(Distance) - Rig->Distance());
        if (!Direction.IsNearlyZero()) Rig->Pan(Direction * (WorldSize * 4.0f));

        CameraBorderTimerWorld = World;
        const TWeakObjectPtr<UWorld> WeakWorld = World;
        const TWeakObjectPtr<ACinderPlayerController> WeakPC = PC;
        const TWeakObjectPtr<ACinderCamera> WeakRig = Rig;
        const TWeakObjectPtr<ACinderBattlefield> WeakBattle = Battle;
        const float StartedAt = World->GetTimeSeconds();
        World->GetTimerManager().SetTimer(CameraBorderCaptureTimer,
            FTimerDelegate::CreateLambda([WeakWorld, WeakPC, WeakRig, WeakBattle, Edge, Distance, StartedAt]
            {
                CaptureCameraBorder(WeakWorld.Get(), WeakPC.Get(), WeakRig.Get(), WeakBattle.Get(),
                    Edge, Distance, StartedAt);
            }), 2.0f, false);
        UE_LOG(LogCinderCameraPreview, Display,
            TEXT("CINDERLINE_MAP_BORDER edge=%s distance=%d capture=scheduled delay=2.0"), *Edge, Distance);
    }));
}

#endif
