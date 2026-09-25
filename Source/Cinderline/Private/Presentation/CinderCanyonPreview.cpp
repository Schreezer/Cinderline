#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CoreGlobals.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Landscape.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Guid.h"
#include "Containers/UnrealString.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderLandscapeTerrain.h"
#include "Presentation/CinderPlayerController.h"
#include "Sim/MapDefinition.h"
#include "TimerManager.h"
#include "UnrealClient.h"

#if WITH_EDITOR
#include "ShaderCompiler.h"
#include "StaticMeshCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCinderCanyonPreview, Log, All);

namespace
{
constexpr uint32 CanyonPreviewSeed = 0xCA770001u;
TWeakObjectPtr<UWorld> CanyonCaptureWorld;
FTimerHandle CanyonCaptureTimer;

bool IsFallbackPreview(const FString& State)
{
    return State == TEXT("shortmap2") || State == TEXT("long4map1");
}

cinder::Config PreviewConfig(const FString& State, int32 Map)
{
    cinder::Config Config;
    Config.map = Map;
    Config.seed = CanyonPreviewSeed;
    Config.ai = false;
    if (State == TEXT("shortmap2")) Config.matchLength = cinder::MatchLength::Short;
    if (State == TEXT("long4map1"))
    {
        Config.matchLength = cinder::MatchLength::Long;
        Config.playerCount = 4;
    }
    return Config;
}

void FinishCanyonPreviewCompilation(const FString& State)
{
#if WITH_EDITOR
    const int32 ShaderJobsBefore = GShaderCompilingManager
        ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
    const int32 MeshesBefore = FStaticMeshCompilingManager::Get().GetNumRemainingMeshes();
    FStaticMeshCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    UE_LOG(LogCinderCanyonPreview, Display,
        TEXT("CINDERLINE_CANYON_PREVIEW_COMPILED state=%s shader_jobs=%d->%d static_meshes=%d->%d"),
        *State, ShaderJobsBefore,
        GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0,
        MeshesBefore, FStaticMeshCompilingManager::Get().GetNumRemainingMeshes());
#endif
}

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
        || Point.x > Sim.worldSize() - Radius
        || Point.y > Sim.worldSize() - Radius) return false;
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
        if (Sim.config().matchLength == cinder::MatchLength::Standard && Sim.playerCount() == 2)
        {
            // Preserve the established canonical overview fixtures exactly.
            for (float Y = 400; Y <= 4400; Y += 1000) for (float X = 400; X <= 4400; X += 1000)
                if (!SpawnHeld(Sim, cinder::Kind::Scout, {X, Y}, Spawned)) return false;
        }
        else
        {
            // Vision radii do not scale with match length; keep grid spacing <= 1000 cm.
            const int32 AxisCount = FMath::CeilToInt(Sim.worldSize() / 1000.0f);
            const float Spacing = Sim.worldSize() / AxisCount;
            for (int32 Y = 0; Y < AxisCount; ++Y) for (int32 X = 0; X < AxisCount; ++X)
                if (!SpawnHeld(Sim, cinder::Kind::Scout,
                    {(X + 0.5f) * Spacing, (Y + 0.5f) * Spacing}, Spawned)) return false;
        }
        const float Cell = Sim.worldSize() / cinder::Simulation::FogSize;
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

bool StageCanyonSector(cinder::Simulation& Sim)
{
    // Use the real western expansion and its ore, with the cliff forming the
    // base's southern edge and the eastern opening remaining a traversable lane.
    // Only normal entity vision reveals this sector; the rest of the map stays fogged.
    auto SpawnChecked = [&Sim](cinder::Kind Kind, cinder::Vec2 Point)
    {
        std::string Reason;
        if (!FreeGround(Sim, Kind, Point)
            || (cinder::definition(Kind).building && !Sim.canPlace(0, Kind, Point, &Reason)))
        {
            UE_LOG(LogCinderCanyonPreview, Error,
                TEXT("CINDERLINE_CANYON_PREVIEW failed=sector_spawn kind=%s point=(%.0f,%.0f) reason=%s"),
                UTF8_TO_TCHAR(cinder::definition(Kind).name), Point.x, Point.y,
                Reason.empty() ? TEXT("occupied_ground") : UTF8_TO_TCHAR(Reason.c_str()));
            return static_cast<cinder::Id>(0);
        }
        return Sim.debugSpawn(Kind, 0, Point);
    };

    if (Sim.usesAuthoredTerrain())
    {
        // A developed main and its real natural share the authored ramp. Every
        // structure passes normal placement; ordinary units reveal the sector.
        const auto& Definition = cinder::mapDefinition(Sim.config().map, Sim.playerCount(),
            Sim.config().matchLength, Sim.config().mapRevision);
        std::vector<cinder::Id> Defenders;
        const cinder::Id Scout = SpawnChecked(cinder::Kind::Scout, {2240, 1350});
        if (!Scout) return false;
        Defenders.push_back(Scout);
        const TPair<cinder::Kind, cinder::Vec2> Buildings[] = {
            {cinder::Kind::Headquarters, cinder::Vec2{2260, 950}},
            {cinder::Kind::Foundry, cinder::Vec2{1180, 820}},
            {cinder::Kind::Processor, cinder::Vec2{1180, 500}},
            {cinder::Kind::Turret, cinder::Vec2{1300, 1350}},
        };
        for (const auto& Entry : Buildings)
            if (!SpawnChecked(Entry.Key, Entry.Value)) return false;
        const TPair<cinder::Kind, cinder::Vec2> Army[] = {
            {cinder::Kind::Striker, cinder::Vec2{1360, 1190}},
            {cinder::Kind::Lancer, cinder::Vec2{1770, 1230}},
            {cinder::Kind::Bastion, cinder::Vec2{2140, 1200}},
            {cinder::Kind::Mender, cinder::Vec2{1250, 1160}},
        };
        for (const auto& Entry : Army)
        {
            const cinder::Id Id = SpawnChecked(Entry.Key, Entry.Value);
            if (!Id) return false;
            Defenders.push_back(Id);
        }
        cinder::Command Hold;
        Hold.type = cinder::CommandType::Hold;
        Hold.team = 0;
        Hold.units = Defenders;
        if (!Sim.command(Hold).accepted) return false;
        const cinder::MapResourceSite* Natural = nullptr;
        for (const auto& Site : Definition.sites)
            if (Site.owner == 0 && Site.role == cinder::MapSiteRole::Natural) Natural = &Site;
        if (!Natural || Natural->nodes.size() != 3) return false;
        for (size_t Index = 0; Index < Natural->nodes.size(); ++Index)
        {
            const cinder::Vec2 Node = Natural->nodes[Index];
            cinder::Id Deposit = 0;
            for (const auto& Entity : Sim.entities())
                if (Entity.kind == cinder::Kind::Resource && Entity.resource > 0
                    && FMath::IsNearlyEqual(Entity.pos.x, Node.x) && FMath::IsNearlyEqual(Entity.pos.y, Node.y))
                    Deposit = Entity.id;
            if (!Deposit) return false;
            const cinder::Id Worker = SpawnChecked(cinder::Kind::Worker,
                {Node.x + (Index == 0 ? -72.0f : 72.0f), Node.y + 66.0f});
            if (!Worker) return false;
            cinder::Command Gather;
            Gather.type = cinder::CommandType::Gather;
            Gather.team = 0;
            Gather.units = {Worker};
            Gather.target = Deposit;
            if (!Sim.command(Gather).accepted) return false;
        }
        UE_LOG(LogCinderCanyonPreview, Display,
            TEXT("CINDERLINE_CANYON_SECTOR_READY buildings=4 mining_workers=3 army=5 ore_nodes=3 map_revision=%d authored=1 main=(720,680) natural=(2260,950) ramp=(1490,1230)-(2050,1230) ordinary_vision=1"),
            Sim.config().mapRevision);
        return true;
    }

    std::vector<cinder::Id> Defenders;
    const cinder::Id Scout = SpawnChecked(cinder::Kind::Scout, {1320, 2690});
    if (!Scout || !Sim.explored(0, {1320, 2400})) return false;
    Defenders.push_back(Scout);

    const TPair<cinder::Kind, cinder::Vec2> Buildings[] = {
        {cinder::Kind::Headquarters, cinder::Vec2{1500, 2920}},
        {cinder::Kind::Foundry, cinder::Vec2{1770, 2860}},
        {cinder::Kind::Processor, cinder::Vec2{1240, 3180}},
        {cinder::Kind::Turret, cinder::Vec2{1960, 2740}},
    };
    for (const auto& Entry : Buildings)
        if (!SpawnChecked(Entry.Key, Entry.Value)) return false;

    const TPair<cinder::Kind, cinder::Vec2> Army[] = {
        {cinder::Kind::Striker, cinder::Vec2{1790, 2670}},
        {cinder::Kind::Lancer, cinder::Vec2{1890, 2580}},
        {cinder::Kind::Bastion, cinder::Vec2{1920, 2460}},
        {cinder::Kind::Mender, cinder::Vec2{1610, 2730}},
    };
    for (const auto& Entry : Army)
    {
        const cinder::Id Id = SpawnChecked(Entry.Key, Entry.Value);
        if (!Id) return false;
        Defenders.push_back(Id);
    }
    cinder::Command Hold;
    Hold.type = cinder::CommandType::Hold;
    Hold.team = 0;
    Hold.units = Defenders;
    if (!Sim.command(Hold).accepted) return false;

    TArray<cinder::Id> Deposits;
    for (const cinder::Entity& Entity : Sim.entities())
        if (Entity.kind == cinder::Kind::Resource && Entity.resource > 0
            && Entity.pos.x >= 850 && Entity.pos.x <= 1150
            && Entity.pos.y >= 2750 && Entity.pos.y <= 2940) Deposits.Add(Entity.id);
    if (Deposits.Num() != 3) return false;
    Deposits.Sort([&Sim](cinder::Id Left, cinder::Id Right)
    {
        const cinder::Entity* A = Sim.find(Left);
        const cinder::Entity* B = Sim.find(Right);
        return A && B ? A->pos.x < B->pos.x : Left < Right;
    });
    for (int32 Index = 0; Index < Deposits.Num(); ++Index)
    {
        const cinder::Entity* Deposit = Sim.find(Deposits[Index]);
        if (!Deposit) return false;
        const cinder::Vec2 Point{Deposit->pos.x + (Index == 0 ? -72.0f : 72.0f), Deposit->pos.y + 66.0f};
        const cinder::Id Worker = SpawnChecked(cinder::Kind::Worker, Point);
        if (!Worker) return false;
        cinder::Command Gather;
        Gather.type = cinder::CommandType::Gather;
        Gather.team = 0;
        Gather.units = {Worker};
        Gather.target = Deposits[Index];
        if (!Sim.command(Gather).accepted) return false;
    }
    UE_LOG(LogCinderCanyonPreview, Display,
        TEXT("CINDERLINE_CANYON_SECTOR_READY buildings=4 mining_workers=3 army=5 ore_nodes=3 cliff=(1320,2400) entrance=(1920,2600) ordinary_vision=1"));
    return true;
}

bool StartSectorOrders(cinder::Simulation& Sim)
{
    cinder::Id Foundry = 0;
    std::vector<cinder::Id> PatrolUnits;
    for (const cinder::Entity& Entity : Sim.entities())
    {
        if (!Entity.alive() || Entity.team != 0) continue;
        if (Entity.kind == cinder::Kind::Foundry) Foundry = Entity.id;
        if (Entity.kind == cinder::Kind::Striker || Entity.kind == cinder::Kind::Lancer)
            PatrolUnits.push_back(Entity.id);
    }
    if (!Foundry || PatrolUnits.size() != 2) return false;
    cinder::Command Train;
    Train.type = cinder::CommandType::Train;
    Train.team = 0;
    Train.units = {Foundry};
    Train.kind = cinder::Kind::Striker;
    if (!Sim.command(Train).accepted) return false;
    cinder::Command Patrol;
    Patrol.type = cinder::CommandType::Patrol;
    Patrol.team = 0;
    Patrol.units = PatrolUnits;
    Patrol.point = Sim.usesAuthoredTerrain() ? cinder::Vec2{2390, 1560} : cinder::Vec2{2010, 2370};
    if (!Sim.command(Patrol).accepted) return false;
    UE_LOG(LogCinderCanyonPreview, Display,
        TEXT("CINDERLINE_CANYON_SECTOR_LIVE queued=1 patrol_units=2 destination=(%.0f,%.0f) paid_production=1"),
        Patrol.point.x, Patrol.point.y);
    return true;
}

bool CaptureCanyon(UWorld* World, ACinderPlayerController* PC, ACinderBattlefield* Battle,
    const FString& State, int32 Map, bool bTakeScreenshot = true)
{
    const cinder::Config Expected = PreviewConfig(State, Map);
    if (!World || !World->IsGameWorld() || !PC || !Battle || PC->GetWorld() != World
        || PC->Battlefield() != Battle || Battle->IsMenu() || Battle->IsOnlineMatch()
        || Battle->MapIndex() != Map || Battle->Sim().config().map != Map
        || Battle->Sim().config().seed != CanyonPreviewSeed || Battle->Sim().config().ai
        || Battle->Sim().config().matchLength != Expected.matchLength
        || Battle->Sim().config().mapRevision != Expected.mapRevision
        || Battle->Sim().playerCount() != Expected.playerCount
        || Battle->Sim().worldSize() != cinder::matchLengthProfile(Expected.matchLength).worldSize)
    {
        UE_LOG(LogCinderCanyonPreview, Warning,
            TEXT("CINDERLINE_CANYON_PREVIEW state=%s capture=skipped reason=world_or_fixture_changed"), *State);
        return false;
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
    int32 MatchingLandscapes = 0, VisibleLandscapes = 0, AllVisibleLandscapes = 0;
    const FName MapTag = CinderLandscapeTerrain::MapTag(Map);
    const FName GeometryTag = CinderLandscapeTerrain::GeometryTag(
        CinderLandscapeTerrain::CanonicalGeometrySignature(Map));
    for (TActorIterator<ALandscape> It(World); It; ++It)
    {
        for (int32 TaggedMap = 0; TaggedMap < CinderLandscapeTerrain::MapCount; ++TaggedMap)
            if (It->ActorHasTag(CinderLandscapeTerrain::MapTag(TaggedMap)))
            {
                if (!It->IsHidden()) ++AllVisibleLandscapes;
                break;
            }
        if (It->ActorHasTag(MapTag) && It->ActorHasTag(GeometryTag))
        {
            ++MatchingLandscapes;
            if (!It->IsHidden()) ++VisibleLandscapes;
        }
    }

    // Match the actual single-instance planes submitted by RefreshEnvironment,
    // not transient component names or the separate four-instance map border.
    int32 GroundVisible = 0, FogSheetVisible = 0;
    const float WorldSize = Battle->Sim().worldSize();
    TInlineComponentArray<UInstancedStaticMeshComponent*> SurfaceMeshes(Battle);
    for (const UInstancedStaticMeshComponent* Mesh : SurfaceMeshes)
    {
        FTransform Transform;
        if (!Mesh->IsRegistered() || !Mesh->ShouldRender() || !Mesh->GetStaticMesh()
            || Mesh->GetInstanceCount() != 1 || !Mesh->GetInstanceTransform(0, Transform, false)
            || !Transform.GetRotation().Equals(FQuat::Identity)) continue;
        const FString MeshPath = Mesh->GetStaticMesh()->GetPathName();
        if (MeshPath == TEXT("/Engine/BasicShapes/Cube.Cube")
            && Transform.GetLocation().Equals(FVector(WorldSize * 0.5f, WorldSize * 0.5f, -16), 0.1f)
            && Transform.GetScale3D().Equals(FVector(WorldSize / 100, WorldSize / 100, 0.3f), 0.001f)) ++GroundVisible;
        if (MeshPath == TEXT("/Engine/BasicShapes/Plane.Plane")
            && Transform.GetLocation().Equals(FVector(WorldSize * 0.5f, WorldSize * 0.5f, 2.5f), 0.1f)
            && Transform.GetScale3D().Equals(FVector(WorldSize / 100, WorldSize / 100, 1), 0.001f)) ++FogSheetVisible;
    }
    int32 RuntimeTerrainMeshes = 0;
    UCinderLandscapeTerrain* Terrain = Battle->FindComponentByClass<UCinderLandscapeTerrain>();
    TInlineComponentArray<UStaticMeshComponent*> AllStaticMeshes(Battle);
    for (const UStaticMeshComponent* Mesh : AllStaticMeshes)
        if (Terrain && !Cast<UInstancedStaticMeshComponent>(Mesh) && Mesh->IsRegistered() && Mesh->ShouldRender()
            && Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetOuter() == Terrain) ++RuntimeTerrainMeshes;

    int32 Headquarters = 0, HeadquartersTeams = 0, EntitiesOutsideWorld = 0;
    for (const cinder::Entity& Entity : Battle->Sim().entities())
    {
        if (!Entity.alive()) continue;
        const float Radius = cinder::definition(Entity.kind).radius;
        if (!FMath::IsFinite(Entity.pos.x) || !FMath::IsFinite(Entity.pos.y)
            || Entity.pos.x < Radius || Entity.pos.y < Radius
            || Entity.pos.x > Battle->Sim().worldSize() - Radius
            || Entity.pos.y > Battle->Sim().worldSize() - Radius) ++EntitiesOutsideWorld;
        if (Entity.kind == cinder::Kind::Headquarters)
        {
            ++Headquarters;
            if (Entity.team >= 0 && Entity.team < Expected.playerCount) HeadquartersTeams |= 1 << Entity.team;
        }
    }
    const bool bCanonical = CinderLandscapeTerrain::IsCanonicalGeometry(Battle->Sim());
    const int32 ObstacleCount = static_cast<int32>(Battle->Sim().obstacles().size());
    if (State == TEXT("sectorfallback") && (!bCanonical || !Battle->HasTerrainRelief()
        || RuntimeTerrainMeshes != 1 || AllVisibleLandscapes != 0 || GroundVisible != 0 || FogSheetVisible != 0))
    {
        UE_LOG(LogCinderCanyonPreview, Warning,
            TEXT("CINDERLINE_CANYON_PREVIEW state=%s capture=skipped reason=runtime_terrain_surface_changed runtime_meshes=%d landscape_visible_all=%d ground_visible=%d fog_sheet_visible=%d terrain_relief=%d"),
            *State, RuntimeTerrainMeshes, AllVisibleLandscapes, GroundVisible, FogSheetVisible, Battle->HasTerrainRelief());
        return false;
    }
    if (IsFallbackPreview(State) && (bCanonical || Battle->HasTerrainRelief() || AllVisibleLandscapes != 0
        || GroundVisible != 1 || FogSheetVisible != 1
        || ObstacleCount != (Map == 2 ? 6 : 5) || ExploredObstacles != ObstacleCount
        || Headquarters != Expected.playerCount || HeadquartersTeams != (1 << Expected.playerCount) - 1
        || EntitiesOutsideWorld != 0 || FogUnknown != 0 || FogVisible == 0))
    {
        UE_LOG(LogCinderCanyonPreview, Warning,
            TEXT("CINDERLINE_CANYON_PREVIEW state=%s capture=skipped reason=fallback_fixture_or_surface_changed canonical=%d terrain_relief=%d landscape_visible_all=%d obstacles=%d explored_obstacles=%d headquarters=%d hq_teams=%d entities_outside_world=%d fog_unknown=%d fog_visible=%d ground_visible=%d fog_sheet_visible=%d"),
            *State, bCanonical, Battle->HasTerrainRelief(), AllVisibleLandscapes, ObstacleCount,
            ExploredObstacles, Headquarters, HeadquartersTeams, EntitiesOutsideWorld, FogUnknown, FogVisible,
            GroundVisible, FogSheetVisible);
        return false;
    }

    const FString Directory = FPaths::ProjectSavedDir() / TEXT("CanyonPreview");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = bTakeScreenshot ? Directory / (State + TEXT(".png")) : TEXT("motion_sequence");
    if (bTakeScreenshot) FScreenshotRequest::RequestScreenshot(Filename, false, false);
    UE_LOG(LogCinderCanyonPreview, Display,
        TEXT("CINDERLINE_CANYON_PREVIEW_READY state=%s map=%d canonical=%d obstacles=%d explored_obstacles=%d fog_unknown=%d fog_explored=%d fog_visible=%d landscape_matching=%d landscape_visible=%d landscape_visible_all=%d length=%s players=%d world=%.0f seed=%08X ai=%d terrain_relief=%d presented_ground=%s ground_visible=%d fog_sheet_visible=%d headquarters=%d hq_teams=%d entities_outside_world=%d raised_samples=%d height_cm=%.1f..%.1f tick=%llu file=%s map_revision=%d authored=%d framing=%s runtime_meshes=%d"),
        *State, Map, bCanonical, ObstacleCount, ExploredObstacles,
        FogUnknown, FogExplored, FogVisible, MatchingLandscapes, VisibleLandscapes,
        AllVisibleLandscapes, UTF8_TO_TCHAR(cinder::matchLengthName(Expected.matchLength)),
        Battle->Sim().playerCount(), Battle->Sim().worldSize(), Battle->Sim().config().seed,
        Battle->Sim().config().ai, Battle->HasTerrainRelief(),
        RuntimeTerrainMeshes == 1 && Battle->HasTerrainRelief() ? TEXT("runtime_mesh")
            : Battle->HasTerrainRelief() ? TEXT("landscape") : TEXT("flat"),
        GroundVisible, FogSheetVisible,
        Headquarters, HeadquartersTeams, EntitiesOutsideWorld,
        RaisedSamples,
        // Samples are stored as FlatHeight + cm * HeightEncodeScale, so the decode divisor
        // is that constant - not Unreal's fixed 1/128 display scale, which this used to
        // hardcode. HeightEncodeScale has been 64 since relief needed a 511 cm ceiling, so
        // every height_cm this command printed was exactly half the truth, in the one log
        // anyone reads to judge whether a sculpted cliff is tall enough.
        (MinimumHeight - CinderLandscapeTerrain::FlatHeight) / CinderLandscapeTerrain::HeightEncodeScale,
        (MaximumHeight - CinderLandscapeTerrain::FlatHeight) / CinderLandscapeTerrain::HeightEncodeScale,
        Battle->Sim().tick(), *Filename, Battle->Sim().config().mapRevision,
        Battle->Sim().usesAuthoredTerrain(), State == TEXT("map0") ? TEXT("orthographic_overview")
            : TEXT("gameplay_camera"), RuntimeTerrainMeshes);
    return true;
}

// Runtime evidence only: capture after each world's actor update, before its
// viewport draw. The processed callback runs after the synchronous PNG write.
// No timer catch-up, desktop recording, simulation mutation, or skipped frames.
class FCanyonMotionCapture
{
public:
    bool Start(UWorld* World, ACinderPlayerController* PC, ACinderBattlefield* Battle, int32 Frames)
    {
        if (bActive || !World || !PC || !Battle || Frames < 60 || Frames > 90) return false;
        Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("CanyonPreview/Motion")
            / FGuid::NewGuid().ToString(EGuidFormats::Digits));
        if (!IFileManager::Get().MakeDirectory(*Directory, true)) return false;
        CaptureWorld = World; CapturePC = PC; CaptureBattle = Battle;
        TargetFrames = Frames; CapturedFrames = 0; WarmupFrames = 0; bPending = false;
        OriginalDelta = FApp::GetFixedDeltaTime();
        bOriginalFixedStep = FApp::UseFixedTimeStep();
        bActive = true;
        FApp::SetFixedDeltaTime(1.0 / 30.0);
        FApp::SetUseFixedTimeStep(true);
        TickHandle = FWorldDelegates::OnWorldTickEnd.AddRaw(this, &FCanyonMotionCapture::OnWorldTick);
        ScreenshotHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddRaw(
            this, &FCanyonMotionCapture::OnScreenshotProcessed);
        CleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda([this](UWorld* EndingWorld, bool, bool)
        { if (EndingWorld == CaptureWorld.Get()) Abort(TEXT("world_cleanup")); });
        ExitHandle = FCoreDelegates::OnEnginePreExit.AddLambda([this] { Abort(TEXT("engine_exit")); });
        UE_LOG(LogCinderCanyonPreview, Display,
            TEXT("CINDERLINE_CANYON_MOTION_BEGIN frames=%d fps=30 fixed_delta=0.033333333 directory=%s"),
            TargetFrames, *Directory);
        return true;
    }

private:
    TWeakObjectPtr<UWorld> CaptureWorld;
    TWeakObjectPtr<ACinderPlayerController> CapturePC;
    TWeakObjectPtr<ACinderBattlefield> CaptureBattle;
    FDelegateHandle TickHandle, ScreenshotHandle, CleanupHandle, ExitHandle;
    FString Directory, PendingPath;
    int32 TargetFrames = 0, CapturedFrames = 0, WarmupFrames = 0;
    uint64 PendingFrame = 0, PendingTick = 0;
    float PendingTime = 0;
    double OriginalDelta = 0;
    bool bActive = false, bPending = false, bOriginalFixedStep = false, bOriginalScreenMessages = false;

    void OnWorldTick(UWorld* World, ELevelTick, float DeltaSeconds)
    {
        if (!bActive || World != CaptureWorld.Get()) return;
        ACinderBattlefield* Battle = CaptureBattle.Get();
        ACinderPlayerController* PC = CapturePC.Get();
        if (!Battle || !PC || PC->Battlefield() != Battle || Battle->IsMenu() || Battle->IsPaused()
            || Battle->IsOnlineMatch() || Battle->Sim().config().seed != CanyonPreviewSeed)
        { Abort(TEXT("fixture_changed")); return; }
        if (!FApp::UseFixedTimeStep() || !FMath::IsNearlyEqual(DeltaSeconds, 1.0f / 30.0f, 0.00001f))
        { Abort(TEXT("fixed_timestep_changed")); return; }
        if (bPending || FScreenshotRequest::IsScreenshotRequested())
        { Abort(TEXT("screenshot_busy_or_previous_frame_missing")); return; }
        // Two simulation seconds settle worker approaches and temporal AA.
        if (++WarmupFrames <= 60) return;
        if (CapturedFrames == 0 && !CaptureCanyon(World, PC, Battle, TEXT("sectorlive"), 0, false))
        { Abort(TEXT("canonical_scene_unavailable")); return; }
        const FString Filename = Directory / FString::Printf(TEXT("frame-%04d.png"), CapturedFrames);
        if (IFileManager::Get().FileExists(*Filename))
        { Abort(TEXT("frame_already_exists")); return; }
        PendingFrame = GFrameCounter;
        PendingTick = Battle->Sim().tick();
        PendingTime = Battle->Sim().time();
        bOriginalScreenMessages = GAreScreenMessagesEnabled;
        bPending = true;
        FScreenshotRequest::RequestScreenshot(Filename, false, false);
        PendingPath = FScreenshotRequest::GetFilename();
    }

    void OnScreenshotProcessed()
    {
        if (!bActive || !bPending) return;
        bPending = false;
        if (GFrameCounter != PendingFrame || IFileManager::Get().FileSize(*PendingPath) <= 0)
        { Abort(TEXT("frame_not_saved_in_requested_tick")); return; }
        UE_LOG(LogCinderCanyonPreview, Display,
            TEXT("CINDERLINE_CANYON_MOTION_FRAME index=%d tick=%llu time=%.6f file=%s"),
            CapturedFrames, PendingTick, PendingTime, *PendingPath);
        ++CapturedFrames;
        if (CapturedFrames == TargetFrames)
        {
            UE_LOG(LogCinderCanyonPreview, Display,
                TEXT("CINDERLINE_CANYON_MOTION_DONE frames=%d fps=30"), CapturedFrames);
            if (ACinderBattlefield* Battle = CaptureBattle.Get()) Battle->SetActorTickEnabled(false);
            Detach();
        }
    }

    void Abort(const TCHAR* Reason)
    {
        if (!bActive) return;
        UE_LOG(LogCinderCanyonPreview, Error,
            TEXT("CINDERLINE_CANYON_MOTION_ABORT reason=%s frames=%d/%d"), Reason, CapturedFrames, TargetFrames);
        Detach();
    }

    void Detach()
    {
        FWorldDelegates::OnWorldTickEnd.Remove(TickHandle);
        FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);
        FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
        FCoreDelegates::OnEnginePreExit.Remove(ExitHandle);
        if (bPending && FScreenshotRequest::GetFilename() == PendingPath)
        {
            FScreenshotRequest::Reset();
            GAreScreenMessagesEnabled = bOriginalScreenMessages;
        }
        FApp::SetFixedDeltaTime(OriginalDelta);
        FApp::SetUseFixedTimeStep(bOriginalFixedStep);
        bActive = false; bPending = false;
        CaptureWorld.Reset(); CapturePC.Reset(); CaptureBattle.Reset();
        TickHandle.Reset(); ScreenshotHandle.Reset(); CleanupHandle.Reset(); ExitHandle.Reset();
    }
};

FCanyonMotionCapture CanyonMotionCapture;

FAutoConsoleCommandWithWorldAndArgs CanyonPreviewCommand(
    TEXT("cinder.canyonpreview"),
    TEXT("DEVELOPMENT: unattended fresh local menu only. Capture map0|map1|map2|fog|sector|sectorbase|sectorlive|sectorfallback|shortmap2|long4map1. sectorlive accepts optional 60..90 frames for fixed-step PNG motion evidence. Uses ordinary vision and simulation updates; never saves."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        const FString State = Args.Num() >= 1 ? Args[0].ToLower() : FString();
        const bool bFog = State == TEXT("fog");
        const bool bLive = State == TEXT("sectorlive");
        const bool bRuntimeFallback = State == TEXT("sectorfallback");
        const bool bBase = State == TEXT("sectorbase");
        const bool bSector = State == TEXT("sector") || bLive || bRuntimeFallback || bBase;
        const bool bFallback = IsFallbackPreview(State);
        int32 MotionFrames = 0;
        const bool bValidArgs = Args.Num() == 1 || (bLive && Args.Num() == 2
            && LexTryParseString(MotionFrames, *Args[1]) && MotionFrames >= 60 && MotionFrames <= 90);
        const int32 Map = State == TEXT("map0") || bFog || bSector ? 0
            : State == TEXT("map1") || State == TEXT("long4map1") ? 1
            : State == TEXT("map2") || State == TEXT("shortmap2") ? 2 : INDEX_NONE;
        auto* PC = World ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        auto* Rig = PC ? Cast<ACinderCamera>(PC->GetPawn()) : nullptr;
        if (!bValidArgs || Map == INDEX_NONE || !FApp::IsUnattended() || !World || !World->IsGameWorld()
            || !PC || !Battle || !Rig || !Battle->IsMenu() || Battle->IsOnlineMatch() || PC->IsHelpOpen())
        {
            UE_LOG(LogCinderCanyonPreview, Warning,
                TEXT("CINDERLINE_CANYON_PREVIEW refused=requires_unattended_fresh_local_menu_valid_state_and_optional_sectorlive_frame_count_60_to_90"));
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
        Battle->Sim().reset(PreviewConfig(State, Map));
        const bool bAuthored = Battle->Sim().usesAuthoredTerrain();
        const int32 FocusObstacleIndex = bAuthored ? 5 : Map == 1 ? 0 : 2;
        if (!Battle->Sim().obstacles().size()
            || FocusObstacleIndex >= static_cast<int32>(Battle->Sim().obstacles().size())
            || !((bSector || (bAuthored && bFog)) ? StageCanyonSector(Battle->Sim())
                : StageCanyonUnits(Battle->Sim(), Battle->Sim().obstacles()[FocusObstacleIndex], bFog)))
        {
            UE_LOG(LogCinderCanyonPreview, Error,
                TEXT("CINDERLINE_CANYON_PREVIEW failed=staging state=%s map=%d"), *State, Map);
            return;
        }
        if (bLive && !StartSectorOrders(Battle->Sim()))
        {
            UE_LOG(LogCinderCanyonPreview, Error, TEXT("CINDERLINE_CANYON_PREVIEW failed=sector_live_orders"));
            return;
        }

        // Let the ordinary fixed-step simulation settle traffic and vision, then
        // freeze still fixtures; sectorlive continues through ordinary actor ticking.
        Battle->Sim().update(cinder::Simulation::Step * 4.0f);
        if (bRuntimeFallback)
        {
            const FName MapTag = CinderLandscapeTerrain::MapTag(Map);
            const FName SignatureTag = CinderLandscapeTerrain::GeometryTag(
                CinderLandscapeTerrain::CanonicalGeometrySignature(Map));
            int32 InvalidatedBakes = 0;
            for (TActorIterator<ALandscape> It(World); It; ++It)
                if (It->ActorHasTag(MapTag) && It->ActorHasTag(SignatureTag))
                {
                    It->Tags.Remove(SignatureTag);
                    ++InvalidatedBakes;
                }
            auto* Terrain = Battle->FindComponentByClass<UCinderLandscapeTerrain>();
            if (!Terrain || InvalidatedBakes == 0)
            {
                UE_LOG(LogCinderCanyonPreview, Error, TEXT("CINDERLINE_CANYON_PREVIEW failed=runtime_fallback_fixture"));
                return;
            }
            // Keep each map tag so discovery hides the stale bake. No assets or
            // unrelated actors are removed, and this unattended process never saves.
            Terrain->Initialize();
            UE_LOG(LogCinderCanyonPreview, Display,
                TEXT("CINDERLINE_CANYON_RUNTIME_FIXTURE invalidated_bakes=%d map_tag_retained=1 no_save=1"), InvalidatedBakes);
        }
        Battle->ResetPresentation();
        Battle->RenderState();
        FinishCanyonPreviewCompilation(State);
        Battle->SetActorTickEnabled(bLive);
        PC->Notify(FString());

        const cinder::Obstacle& FocusObstacle = Battle->Sim().obstacles()[FocusObstacleIndex];
        const float WorldCenter = Battle->Sim().worldSize() * 0.5f;
        // Include the cliff's full height and the ore delivery side of the base
        // above the bottom HUD; overview modes retain their established framing.
        const FVector Focus = bAuthored && bBase ? FVector(720, 680, 0)
            : bAuthored && bFog ? FVector(1900, 1900, 0)
            : bAuthored && bSector ? FVector(1500, 1130, 0) : bSector ? FVector(1410, 2690, 0) : bFog
            ? FVector(FocusObstacle.center.x + 160.0f, FocusObstacle.center.y, 0)
            : FVector((FocusObstacle.center.x + WorldCenter) * 0.5f,
                (FocusObstacle.center.y + WorldCenter) * 0.5f, 0);
        const float Distance = bBase ? ACinderCamera::DefaultDistance
            : bAuthored && bFog ? 1800.0f : bAuthored && bSector ? 1500.0f : bSector ? 2600.0f : bFog ? 2450.0f : bFallback
            ? FMath::Min(ACinderCamera::FarthestDistance,
                2900.0f * Battle->Sim().worldSize() / cinder::Simulation::WorldSize) : 2900.0f;
        // Fit new broad fallback views around their own center before zoom bounds
        // are evaluated, instead of inheriting the starting base's focus anchor.
        if (bFallback || (bAuthored && (bSector || bFog))) Rig->Focus(Focus, true);
        Rig->Zoom(Distance - Rig->Distance());
        Rig->Focus(Focus, true);

        if (bAuthored && bBase)
        {
            // Exercise the same Home action a player uses, at opening gameplay
            // distance. Its framing extent keeps the complete elevated Anchor.
            PC->ExecuteAction(TEXT("home"));
            Rig->Zoom(ACinderCamera::DefaultDistance - Rig->Distance());
            Rig->Focus(Focus, true, FVector(125, 125, 92.5f));
        }
        if (bAuthored && (bSector || bFog))
            UE_LOG(LogCinderCanyonPreview, Display,
                TEXT("CINDERLINE_CANYON_GAMEPLAY_CAMERA state=%s framing=%s actual_distance=%.1f pivot_z=%.1f terrain_z=%.1f view_target_is_rig=%d"),
                *State, bBase ? TEXT("home_action") : bFog ? TEXT("fog_frontier") : TEXT("ramp_entrance"), Rig->Distance(), Rig->GetActorLocation().Z,
                Battle->PickingGroundHeight({static_cast<float>(Focus.X), static_cast<float>(Focus.Y)}), PC->GetViewTarget() == Rig);

        if (State == TEXT("map0"))
        {
            // A capture-only map overview deliberately exceeds gameplay framing.
            // Leave camera controls and their normal playable-bound clamps intact.
            auto* Overview = World->SpawnActor<ACameraActor>();
            if (!Overview)
            {
                UE_LOG(LogCinderCanyonPreview, Error, TEXT("CINDERLINE_CANYON_PREVIEW failed=overview_camera"));
                return;
            }
            Overview->SetActorLocation(FVector(WorldCenter, WorldCenter, 6500.0f));
            Overview->SetActorRotation(FRotator(-90.0f, -90.0f, 0.0f));
            auto* OverviewCamera = Overview->GetCameraComponent();
            if (auto* GameplayCamera = Rig->FindComponentByClass<UCameraComponent>())
            {
                OverviewCamera->PostProcessSettings = GameplayCamera->PostProcessSettings;
                OverviewCamera->PostProcessBlendWeight = GameplayCamera->PostProcessBlendWeight;
            }
            int32 Width = 0, Height = 0;
            PC->GetViewportSize(Width, Height);
            const float Aspect = Height > 0 ? static_cast<float>(Width) / Height : 1.6f;
            OverviewCamera->SetProjectionMode(ECameraProjectionMode::Orthographic);
            OverviewCamera->SetAspectRatio(Aspect);
            OverviewCamera->bOverrideAspectRatioAxisConstraint = true;
            OverviewCamera->SetAspectRatioAxisConstraint(AspectRatio_MaintainYFOV);
            OverviewCamera->SetOrthoWidth(Battle->Sim().worldSize() * 1.25f);
            OverviewCamera->bConstrainAspectRatio = false;
            PC->SetViewTarget(Overview);
        }

        if (MotionFrames > 0)
        {
            if (!CanyonMotionCapture.Start(World, PC, Battle, MotionFrames))
                UE_LOG(LogCinderCanyonPreview, Error, TEXT("CINDERLINE_CANYON_PREVIEW failed=motion_start"));
            return;
        }

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
            TEXT("CINDERLINE_CANYON_PREVIEW state=%s map=%d capture=scheduled delay=2.0 focus=(%.1f,%.1f) distance=%.1f actual_distance=%.1f no_save=1"),
            *State, Map, Focus.X, Focus.Y, Distance, Rig->Distance());
    }));
}

#endif
