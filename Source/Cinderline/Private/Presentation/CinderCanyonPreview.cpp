#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Landscape.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderLandscapeTerrain.h"
#include "Presentation/CinderPlayerController.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderCanyonPreview, Log, All);

namespace
{
constexpr uint32 CanyonPreviewSeed = 0xCA770001u;
TWeakObjectPtr<UWorld> CanyonCaptureWorld;
FTimerHandle CanyonCaptureTimer;

bool CircleTouchesObstacle(cinder::Vec2 Point, float Radius, const cinder::Obstacle& Obstacle)
{
    const float DX = FMath::Max(FMath::Abs(Point.x - Obstacle.center.x) - Obstacle.half.x, 0.0f);
    const float DY = FMath::Max(FMath::Abs(Point.y - Obstacle.center.y) - Obstacle.half.y, 0.0f);
    return DX * DX + DY * DY <= Radius * Radius;
}

bool FreeGround(const cinder::Simulation& Sim, cinder::Kind Kind, cinder::Vec2 Point)
{
    const float Radius = cinder::definition(Kind).radius + 10.0f;
    if (Point.x < Radius || Point.y < Radius
        || Point.x > cinder::Simulation::WorldSize - Radius
        || Point.y > cinder::Simulation::WorldSize - Radius) return false;
    for (const cinder::Obstacle& Obstacle : Sim.obstacles())
        if (CircleTouchesObstacle(Point, Radius, Obstacle)) return false;
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive()) continue;
        const float Separation = Radius + cinder::definition(Entity.kind).radius + 8.0f;
        const float DX = Point.x - Entity.pos.x, DY = Point.y - Entity.pos.y;
        if (DX * DX + DY * DY <= Separation * Separation) return false;
    }
    return true;
}

bool FindFreeGround(const cinder::Simulation& Sim, cinder::Kind Kind,
    cinder::Vec2 Preferred, cinder::Vec2& Out)
{
    constexpr float Step = 72.0f;
    for (int32 Ring = 0; Ring <= 12; ++Ring)
    {
        for (int32 Y = -Ring; Y <= Ring; ++Y) for (int32 X = -Ring; X <= Ring; ++X)
        {
            if (Ring > 0 && FMath::Abs(X) != Ring && FMath::Abs(Y) != Ring) continue;
            const cinder::Vec2 Candidate{Preferred.x + X * Step, Preferred.y + Y * Step};
            if (FreeGround(Sim, Kind, Candidate)) { Out = Candidate; return true; }
        }
    }
    return false;
}

bool SpawnHeld(cinder::Simulation& Sim, cinder::Kind Kind, cinder::Vec2 Preferred,
    std::vector<cinder::Id>& Spawned)
{
    cinder::Vec2 Point;
    if (!FindFreeGround(Sim, Kind, Preferred, Point)) return false;
    const cinder::Id Id = Sim.debugSpawn(Kind, 0, Point);
    if (!Id) return false;
    Spawned.push_back(Id);
    return true;
}

bool StageCanyonUnits(cinder::Simulation& Sim, const cinder::Obstacle& FocusObstacle, bool bFog)
{
    std::vector<cinder::Id> Spawned;
    const cinder::Kind Army[] = {cinder::Kind::Scout, cinder::Kind::Striker,
        cinder::Kind::Lancer, cinder::Kind::Bastion, cinder::Kind::Mortar, cinder::Kind::Mender};
    const float Left = FocusObstacle.center.x - FocusObstacle.half.x - 190.0f;
    const float Bottom = FocusObstacle.center.y - FocusObstacle.half.y - 150.0f;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Army); ++Index)
        if (!SpawnHeld(Sim, Army[Index], {Left + (Index % 3) * 100.0f, Bottom + (Index / 3) * 105.0f}, Spawned))
            return false;

    if (bFog)
    {
        const cinder::Vec2 Scouts[] = {
            {FocusObstacle.center.x - FocusObstacle.half.x - 300.0f, FocusObstacle.center.y},
            {FocusObstacle.center.x, FocusObstacle.center.y - FocusObstacle.half.y - 220.0f},
            {FocusObstacle.center.x, FocusObstacle.center.y + FocusObstacle.half.y + 220.0f},
        };
        for (cinder::Vec2 Point : Scouts)
            if (!SpawnHeld(Sim, cinder::Kind::Scout, Point, Spawned)) return false;
    }
    else
    {
        // A 1000 cm grid is covered by the Skim's ordinary 780 cm vision circle.
        // Fill any residual cells displaced by cliffs with more ordinary scouts.
        for (float Y = 400; Y <= 4400; Y += 1000) for (float X = 400; X <= 4400; X += 1000)
            if (!SpawnHeld(Sim, cinder::Kind::Scout, {X, Y}, Spawned)) return false;
        constexpr float Cell = cinder::Simulation::WorldSize / cinder::Simulation::FogSize;
        for (int32 Y = 0; Y < cinder::Simulation::FogSize; ++Y)
            for (int32 X = 0; X < cinder::Simulation::FogSize; ++X)
            {
                const cinder::Vec2 Point{(X + 0.5f) * Cell, (Y + 0.5f) * Cell};
                if (!Sim.explored(0, Point)
                    && !SpawnHeld(Sim, cinder::Kind::Scout, Point, Spawned)) return false;
            }
    }

    cinder::Command Hold;
    Hold.type = cinder::CommandType::Hold;
    Hold.team = 0;
    Hold.units = Spawned;
    return Sim.command(Hold).accepted;
}

void CaptureCanyon(UWorld* World, ACinderPlayerController* PC, ACinderBattlefield* Battle,
    const FString& State, int32 Map)
{
    if (!World || !World->IsGameWorld() || !PC || !Battle || PC->GetWorld() != World
        || PC->Battlefield() != Battle || Battle->IsMenu() || Battle->IsOnlineMatch()
        || Battle->MapIndex() != Map || Battle->Sim().config().map != Map
        || Battle->Sim().config().seed != CanyonPreviewSeed || Battle->Sim().config().ai)
    {
        UE_LOG(LogCinderCanyonPreview, Warning,
            TEXT("CINDERLINE_CANYON_PREVIEW state=%s capture=skipped reason=world_or_fixture_changed"), *State);
        return;
    }

    int32 FogUnknown = 0, FogExplored = 0, FogVisible = 0;
    for (uint8 Cell : Battle->FogCells())
        if (Cell == 0) ++FogUnknown; else if (Cell == 1) ++FogExplored; else if (Cell == 2) ++FogVisible;
    int32 ExploredObstacles = 0;
    for (const cinder::Obstacle& Obstacle : Battle->Sim().obstacles())
        if (Battle->Sim().explored(0, Obstacle.center)) ++ExploredObstacles;

    TArray<uint16> Heights;
    CinderLandscapeTerrain::BuildHeightData(Battle->Sim(), Heights);
    uint16 MinimumHeight = MAX_uint16, MaximumHeight = 0;
    int32 RaisedSamples = 0;
    for (uint16 Height : Heights)
    {
        MinimumHeight = FMath::Min(MinimumHeight, Height);
        MaximumHeight = FMath::Max(MaximumHeight, Height);
        if (Height > CinderLandscapeTerrain::FlatHeight) ++RaisedSamples;
    }
    int32 MatchingLandscapes = 0, VisibleLandscapes = 0;
    const FName MapTag = CinderLandscapeTerrain::MapTag(Map);
    const FName GeometryTag = CinderLandscapeTerrain::GeometryTag(
        CinderLandscapeTerrain::CanonicalGeometrySignature(Map));
    for (TActorIterator<ALandscape> It(World); It; ++It)
        if (It->ActorHasTag(MapTag) && It->ActorHasTag(GeometryTag))
        {
            ++MatchingLandscapes;
            if (!It->IsHidden()) ++VisibleLandscapes;
        }

    const FString Directory = FPaths::ProjectSavedDir() / TEXT("CanyonPreview");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = Directory / (State + TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(Filename, false, false);
    UE_LOG(LogCinderCanyonPreview, Display,
        TEXT("CINDERLINE_CANYON_PREVIEW_READY state=%s map=%d canonical=%d obstacles=%d explored_obstacles=%d fog_unknown=%d fog_explored=%d fog_visible=%d landscape_matching=%d landscape_visible=%d raised_samples=%d height_cm=%.1f..%.1f tick=%llu file=%s"),
        *State, Map, CinderLandscapeTerrain::IsCanonicalGeometry(Battle->Sim()),
        static_cast<int32>(Battle->Sim().obstacles().size()), ExploredObstacles,
        FogUnknown, FogExplored, FogVisible, MatchingLandscapes, VisibleLandscapes,
        RaisedSamples, (MinimumHeight - CinderLandscapeTerrain::FlatHeight) / 128.0f,
        (MaximumHeight - CinderLandscapeTerrain::FlatHeight) / 128.0f,
        Battle->Sim().tick(), *Filename);
}

FAutoConsoleCommandWithWorldAndArgs CanyonPreviewCommand(
    TEXT("cinder.canyonpreview"),
    TEXT("DEVELOPMENT: unattended fresh local menu only. Capture canonical raised canyon map0|map1|map2 or map0 fog boundary after 2 seconds. Uses ordinary vision and simulation updates; never saves."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        const FString State = Args.Num() == 1 ? Args[0].ToLower() : FString();
        const bool bFog = State == TEXT("fog");
        const int32 Map = State == TEXT("map0") || bFog ? 0
            : State == TEXT("map1") ? 1 : State == TEXT("map2") ? 2 : INDEX_NONE;
        auto* PC = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        auto* Rig = PC ? Cast<ACinderCamera>(PC->GetPawn()) : nullptr;
        if (Map == INDEX_NONE || !FApp::IsUnattended() || !World || !World->IsGameWorld()
            || !PC || !Battle || !Rig || !Battle->IsMenu() || Battle->IsOnlineMatch() || PC->IsHelpOpen())
        {
            UE_LOG(LogCinderCanyonPreview, Warning,
                TEXT("CINDERLINE_CANYON_PREVIEW refused=requires_unattended_fresh_local_menu_and_state_map0_map1_map2_or_fog"));
            return;
        }
        if (UWorld* Previous = CanyonCaptureWorld.Get())
            Previous->GetTimerManager().ClearTimer(CanyonCaptureTimer);

        if (PC->IsTutorialOfferPending()) PC->ExecuteAction(TEXT("onboardskip"));
        PC->ExecuteAction(TEXT("start"), Map);
        if (Battle->IsMenu() || Battle->MapIndex() != Map)
        {
            UE_LOG(LogCinderCanyonPreview, Error, TEXT("CINDERLINE_CANYON_PREVIEW failed=start map=%d"), Map);
            return;
        }
        cinder::Config Config;
        Config.map = Map;
        Config.seed = CanyonPreviewSeed;
        Config.ai = false;
        Battle->Sim().reset(Config);
        const int32 FocusObstacleIndex = Map == 1 ? 0 : 2;
        if (!Battle->Sim().obstacles().size()
            || FocusObstacleIndex >= static_cast<int32>(Battle->Sim().obstacles().size())
            || !StageCanyonUnits(Battle->Sim(), Battle->Sim().obstacles()[FocusObstacleIndex], bFog))
        {
            UE_LOG(LogCinderCanyonPreview, Error,
                TEXT("CINDERLINE_CANYON_PREVIEW failed=staging state=%s map=%d"), *State, Map);
            return;
        }

        // Let the ordinary fixed-step simulation settle traffic and vision, then
        // freeze only this disposable fixture while the camera and capture timer run.
        Battle->Sim().update(cinder::Simulation::Step * 4.0f);
        Battle->ResetPresentation();
        Battle->RenderState();
        Battle->SetActorTickEnabled(false);
        PC->Notify(FString());

        const cinder::Obstacle& FocusObstacle = Battle->Sim().obstacles()[FocusObstacleIndex];
        const FVector Focus = bFog
            ? FVector(FocusObstacle.center.x + 160.0f, FocusObstacle.center.y, 0)
            : FVector((FocusObstacle.center.x + 2400.0f) * 0.5f,
                (FocusObstacle.center.y + 2400.0f) * 0.5f, 0);
        const float Distance = bFog ? 2050.0f : 2900.0f;
        Rig->Zoom(Distance - Rig->Distance());
        Rig->Focus(Focus, true);

        CanyonCaptureWorld = World;
        const TWeakObjectPtr<UWorld> WeakWorld(World);
        const TWeakObjectPtr<ACinderPlayerController> WeakPC(PC);
        const TWeakObjectPtr<ACinderBattlefield> WeakBattle(Battle);
        World->GetTimerManager().SetTimer(CanyonCaptureTimer,
            FTimerDelegate::CreateLambda([WeakWorld, WeakPC, WeakBattle, State, Map]
            {
                CaptureCanyon(WeakWorld.Get(), WeakPC.Get(), WeakBattle.Get(), State, Map);
            }), 2.0f, false);
        UE_LOG(LogCinderCanyonPreview, Display,
            TEXT("CINDERLINE_CANYON_PREVIEW state=%s map=%d capture=scheduled delay=2.0 focus=(%.1f,%.1f) distance=%.1f no_save=1"),
            *State, Map, Focus.X, Focus.Y, Distance);
    }));
}

#endif
