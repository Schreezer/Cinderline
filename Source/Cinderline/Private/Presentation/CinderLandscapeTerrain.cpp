#include "Presentation/CinderLandscapeTerrain.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include <array>
#include <cstring>

namespace CinderLandscapeTerrain
{
namespace
{
constexpr uint64 FnvOffset = 1469598103934665603ull;
constexpr uint64 FnvPrime = 1099511628211ull;

void HashU32(uint64& Hash, uint32 Value)
{
    for (int32 Byte = 0; Byte < 4; ++Byte)
    {
        Hash ^= static_cast<uint8>(Value & 0xffu);
        Hash *= FnvPrime;
        Value >>= 8;
    }
}

void HashFloat(uint64& Hash, float Value)
{
    uint32 Bits = 0;
    static_assert(sizeof(Bits) == sizeof(Value));
    FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
    HashU32(Hash, Bits);
}

uint64 LegacyGeometrySignature(const cinder::Simulation& Simulation)
{
    uint64 Hash = FnvOffset;
    HashU32(Hash, static_cast<uint32>(Simulation.config().map));
    HashU32(Hash, static_cast<uint32>(Simulation.obstacles().size()));
    for (const cinder::Obstacle& Obstacle : Simulation.obstacles())
    {
        HashFloat(Hash, Obstacle.center.x);
        HashFloat(Hash, Obstacle.center.y);
        HashFloat(Hash, Obstacle.half.x);
        HashFloat(Hash, Obstacle.half.y);
    }
    return Hash;
}
}

static uint64 LegacyCanonicalGeometrySignature(int32 Map)
{
    static const std::array<uint64, MapCount> Signatures = []
    {
        std::array<uint64, MapCount> Result{};
        for (int32 CanonicalMap = 0; CanonicalMap < MapCount; ++CanonicalMap)
        {
            cinder::Config Config;
            Config.map = CanonicalMap;
            cinder::Simulation Simulation;
            Simulation.reset(Config);
            Result[CanonicalMap] = LegacyGeometrySignature(Simulation);
        }
        return Result;
    }();
    return Signatures[FMath::Clamp(Map, 0, MapCount - 1)];
}

float VertexSpacing(float WorldSize)
{
    return FMath::Max(1.0f, WorldSize) / static_cast<float>(QuadsPerAxis);
}

uint64 GeometrySignature(const cinder::Simulation& Simulation)
{
    uint64 Hash = FnvOffset;
    HashU32(Hash, static_cast<uint32>(Simulation.config().map));
    HashFloat(Hash, Simulation.worldSize());
    HashU32(Hash, static_cast<uint32>(Simulation.obstacles().size()));
    for (const cinder::Obstacle& Obstacle : Simulation.obstacles())
    {
        HashFloat(Hash, Obstacle.center.x);
        HashFloat(Hash, Obstacle.center.y);
        HashFloat(Hash, Obstacle.half.x);
        HashFloat(Hash, Obstacle.half.y);
    }
    return Hash;
}

uint64 CanonicalGeometrySignature(int32 Map)
{
    static const std::array<uint64, MapCount> Signatures = []
    {
        std::array<uint64, MapCount> Result{};
        for (int32 CanonicalMap = 0; CanonicalMap < MapCount; ++CanonicalMap)
        {
            cinder::Config Config;
            Config.map = CanonicalMap;
            cinder::Simulation Simulation;
            Simulation.reset(Config);
            Result[CanonicalMap] = GeometrySignature(Simulation);
        }
        return Result;
    }();
    return Signatures[FMath::Clamp(Map, 0, MapCount - 1)];
}

bool IsCanonicalGeometry(const cinder::Simulation& Simulation)
{
    const int32 Map = Simulation.config().map;
    return Map >= 0 && Map < MapCount && GeometrySignature(Simulation) == CanonicalGeometrySignature(Map);
}

FName MapTag(int32 Map)
{
    return FName(*FString::Printf(TEXT("CinderLandscapeMap%d"), Map));
}

FName GeometryTag(uint64 Signature)
{
    return FName(*FString::Printf(TEXT("CinderLandscapeGeometry_%016llX"),
        static_cast<unsigned long long>(Signature)));
}

void BuildCanonicalHeightData(int32 Map, TArray<uint16>& OutHeights)
{
    cinder::Config Config;
    Config.map = FMath::Clamp(Map, 0, MapCount - 1);
    cinder::Simulation Simulation;
    Simulation.reset(Config);
    BuildHeightData(Simulation, OutHeights);
}

void BuildHeightData(const cinder::Simulation& Simulation, TArray<uint16>& OutHeights)
{
    OutHeights.Init(FlatHeight, SamplesPerAxis * SamplesPerAxis);
    const float Spacing = VertexSpacing(Simulation.worldSize());
    for (int32 Y = 0; Y < SamplesPerAxis; ++Y)
    {
        const float WorldY = Y * Spacing;
        for (int32 X = 0; X < SamplesPerAxis; ++X)
        {
            const float WorldX = X * Spacing;
            float Height = 0.0f;
            int32 ObstacleIndex = 0;
            for (const cinder::Obstacle& Obstacle : Simulation.obstacles())
            {
                const float InsetX = Obstacle.half.x - FMath::Abs(WorldX - Obstacle.center.x);
                const float InsetY = Obstacle.half.y - FMath::Abs(WorldY - Obstacle.center.y);
                const float Inset = FMath::Min(InsetX, InsetY);
                if (Inset < Spacing) { ++ObstacleIndex; continue; }
                const float ContourWave = 0.5f + 0.25f * FMath::Sin(WorldX * 0.013f + ObstacleIndex * 1.71f)
                    + 0.25f * FMath::Sin(WorldY * 0.017f - ObstacleIndex * 1.19f);
                const float SculptedInset = Inset - Spacing * (0.12f + 0.78f * ContourWave);
                if (SculptedInset < Spacing) { ++ObstacleIndex; continue; }
                const float RampWidth = FMath::Max(Spacing, FMath::Min(150.0f,
                    FMath::Min(Obstacle.half.x, Obstacle.half.y) - Spacing));
                const float Alpha = FMath::Clamp((SculptedInset - Spacing) / RampWidth, 0.0f, 1.0f);
                const float Smooth = Alpha * Alpha * (3.0f - 2.0f * Alpha);
                const float Shelf = FMath::FloorToFloat(Smooth * 5.0f) / 5.0f;
                const float Terraced = FMath::Lerp(Smooth, Shelf, 0.16f);
                const float Peak = FMath::Clamp(FMath::Min(Obstacle.half.x, Obstacle.half.y) * 0.72f,
                    96.0f, 220.0f);
                const float BroadVariation = 0.90f + 0.10f * (0.5f
                    + 0.25f * FMath::Sin(WorldX * 0.007f + ObstacleIndex * 0.83f)
                    + 0.25f * FMath::Sin(WorldY * 0.009f + ObstacleIndex * 1.37f));
                Height = FMath::Max(Height,
                    FMath::Min(220.0f, FMath::Lerp(14.0f, Peak * BroadVariation, Terraced)));
                ++ObstacleIndex;
            }
            const int32 Encoded = FlatHeight + FMath::RoundToInt(Height * 128.0f);
            OutHeights[Y * SamplesPerAxis + X] = static_cast<uint16>(FMath::Clamp(Encoded, 0, 65535));
        }
    }
}
}

UCinderLandscapeTerrain::UCinderLandscapeTerrain()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UCinderLandscapeTerrain::Initialize()
{
    Landscapes.Init(nullptr, CinderLandscapeTerrain::MapCount);
    BoundFogMasks.Init(nullptr, CinderLandscapeTerrain::MapCount);
    BoundTerrainLayers.Init(nullptr, CinderLandscapeTerrain::MapCount);
    ActiveMap = INDEX_NONE;
    UWorld* World = GetWorld();
    if (!World) { bInitialized = true; return; }

    TArray<bool> Duplicate;
    Duplicate.Init(false, CinderLandscapeTerrain::MapCount);
    for (TActorIterator<ALandscape> It(World); It; ++It)
    {
        ALandscape* Landscape = *It;
        int32 TaggedMap = INDEX_NONE;
        for (int32 Map = 0; Map < CinderLandscapeTerrain::MapCount; ++Map)
            if (Landscape->ActorHasTag(CinderLandscapeTerrain::MapTag(Map)))
            {
                TaggedMap = Map;
                break;
            }
        // Landscapes without a Cinderline map tag belong to the level author.
        if (TaggedMap == INDEX_NONE) continue;
        Landscape->SetActorHiddenInGame(true);
        Landscape->SetActorEnableCollision(false);
        Landscape->bUsedForNavigation = false;
        TInlineComponentArray<UPrimitiveComponent*> Components(Landscape);
        for (UPrimitiveComponent* Component : Components)
        {
            // This small fixed grid keeps its flat guard at every camera zoom.
            // Coarse height mips can interpolate relief into playable lanes.
            if (auto* TerrainComponent = Cast<ULandscapeComponent>(Component))
                TerrainComponent->SetForcedLOD(0);
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Component->SetCanEverAffectNavigation(false);
            Component->SetCastShadow(false);
        }
        const bool bCurrentTag = Landscape->ActorHasTag(CinderLandscapeTerrain::GeometryTag(
            CinderLandscapeTerrain::CanonicalGeometrySignature(TaggedMap)));
        const bool bLegacyStandardTag = Landscape->ActorHasTag(CinderLandscapeTerrain::GeometryTag(
            CinderLandscapeTerrain::LegacyCanonicalGeometrySignature(TaggedMap)));
        if (!bCurrentTag && !bLegacyStandardTag) continue;
        if (Landscapes[TaggedMap]) Duplicate[TaggedMap] = true;
        else Landscapes[TaggedMap] = Landscape;
    }
    for (int32 Map = 0; Map < CinderLandscapeTerrain::MapCount; ++Map)
        if (Duplicate[Map]) Landscapes[Map] = nullptr;
    bInitialized = true;
}

void UCinderLandscapeTerrain::HideAll()
{
    if (Landscapes.IsValidIndex(ActiveMap) && Landscapes[ActiveMap])
        Landscapes[ActiveMap]->SetActorHiddenInGame(true);
    ActiveMap = INDEX_NONE;
}

bool UCinderLandscapeTerrain::BindMaterialTextures(ALandscape* Landscape,
    UTexture2D* FogMask, UTexture2D* TerrainLayers, float WorldSizeInverse)
{
    UWorld* World = GetWorld();
    if (!Landscape || !World || !FogMask || !TerrainLayers) return false;

    if (World->GetFeatureLevel() != ERHIFeatureLevel::ES3_1)
    {
        Landscape->SetLandscapeMaterialTextureParameterValue(TEXT("FogMask"), FogMask);
        Landscape->SetLandscapeMaterialTextureParameterValue(TEXT("TerrainLayers"), TerrainLayers);
        Landscape->SetLandscapeMaterialScalarParameterValue(TEXT("CinderWorldSizeInverse"), WorldSizeInverse);
        return true;
    }

    TInlineComponentArray<ULandscapeComponent*> Components(Landscape);
    if (Components.IsEmpty()) return false;
    for (ULandscapeComponent* Component : Components)
    {
        if (!Component || Component->MobileMaterialInterfaces.IsEmpty()) return false;
        for (UMaterialInterface* Material : Component->MobileMaterialInterfaces)
            if (!Material) return false;
    }

    for (ULandscapeComponent* Component : Components)
    {
        bool bReplacedInterface = false;
        for (TObjectPtr<UMaterialInterface>& Material : Component->MobileMaterialInterfaces)
        {
            UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material);
            if (!Dynamic)
            {
                Dynamic = UMaterialInstanceDynamic::Create(Material, Component);
                if (!Dynamic) return false;
                Material = Dynamic;
                bReplacedInterface = true;
            }
            Dynamic->SetTextureParameterValue(TEXT("FogMask"), FogMask);
            Dynamic->SetTextureParameterValue(TEXT("TerrainLayers"), TerrainLayers);
            Dynamic->SetScalarParameterValue(TEXT("CinderWorldSizeInverse"), WorldSizeInverse);
        }
        // The ES3_1 scene proxy reads MobileMaterialInterfaces only when constructed.
        if (bReplacedInterface) Component->MarkRenderStateDirty();
    }
    return true;
}

bool UCinderLandscapeTerrain::Update(const cinder::Simulation& Simulation,
    UTexture2D* FogMask, UTexture2D* TerrainLayers)
{
    if (!bInitialized) Initialize();
    const int32 Map = Simulation.config().map;
    if (!FogMask || !TerrainLayers
        || Map < 0 || Map >= CinderLandscapeTerrain::MapCount
        || !CinderLandscapeTerrain::IsCanonicalGeometry(Simulation)
        || !Landscapes.IsValidIndex(Map) || !Landscapes[Map])
    {
        HideAll();
        return false;
    }

    ALandscape* Selected = Landscapes[Map];
    if (BoundFogMasks[Map] != FogMask || BoundTerrainLayers[Map] != TerrainLayers)
    {
        if (!BindMaterialTextures(Selected, FogMask, TerrainLayers,
            1.0f / FMath::Max(1.0f, Simulation.worldSize())))
        {
            HideAll();
            return false;
        }
        BoundFogMasks[Map] = FogMask;
        BoundTerrainLayers[Map] = TerrainLayers;
    }
    if (ActiveMap != Map)
    {
        HideAll();
        Selected->SetActorHiddenInGame(false);
        ActiveMap = Map;
    }
    return true;
}
