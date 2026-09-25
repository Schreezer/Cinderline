#include "Presentation/CinderLandscapeTerrain.h"

#include "Presentation/CinderGroundPalette.h"
#include "Sim/MapDefinition.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "Misc/App.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
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
    HashU32(Hash, Simulation.config().playerCount);
    HashU32(Hash, Simulation.config().mapRevision);
    const auto& Definition = cinder::mapDefinition(Simulation.config().map,
        Simulation.playerCount(), Simulation.config().matchLength, Simulation.config().mapRevision);
    for (const auto& Plateau : Definition.plateaus)
    {
        HashFloat(Hash, Plateau.bounds.center.x); HashFloat(Hash, Plateau.bounds.center.y);
        HashFloat(Hash, Plateau.bounds.half.x); HashFloat(Hash, Plateau.bounds.half.y);
        HashFloat(Hash, Plateau.height); HashU32(Hash, Plateau.level);
    }
    for (const auto& Ramp : Definition.ramps)
    {
        HashFloat(Hash, Ramp.high.x); HashFloat(Hash, Ramp.high.y);
        HashFloat(Hash, Ramp.low.x); HashFloat(Hash, Ramp.low.y);
        HashFloat(Hash, Ramp.halfWidth); HashFloat(Hash, Ramp.highHeight); HashFloat(Hash, Ramp.lowHeight);
    }
    for (const auto& Route : Definition.routes)
    {
        HashU32(Hash, static_cast<uint32>(Route.kind)); HashFloat(Hash, Route.halfWidth);
        for (const auto Point : Route.points) { HashFloat(Hash, Point.x); HashFloat(Hash, Point.y); }
    }
    // The GENERATOR is part of the geometry, not just the obstacle layout it is generated
    // from. Without these four terms an edit to HeightAt leaves the tag unchanged, so a
    // stale bake matches itself and the rendered ground silently disagrees with the heights
    // every prop, unit and selection ring is seated against. See CliffProfileVersion.
    HashU32(Hash, CliffProfileVersion);
    HashU32(Hash, static_cast<uint32>(QuadsPerAxis));
    HashFloat(Hash, HeightEncodeScale);
    HashFloat(Hash, MaxRelief);
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

namespace
{
// =======================================================================================
// WALKABLE RELIEF
//
// Everything below this line generates the ground a unit can actually stand on, and it is
// governed by one fact about the simulation: cinder::Vec2 is {float x, float y}. There is
// no elevation, no terrain cost and no slope anywhere in Sim, and an obstacle rectangle is
// the only impassable geometry that exists. So relief raised inside an obstacle is a cliff
// and relief raised anywhere else is decoration a unit walks straight through. The whole
// design therefore answers one question: how much shape can the open field carry before a
// player starts reading a slope as a wall? WalkableRelief and WalkableGradient are that
// answer, and every width here is sized backwards from them.
// =======================================================================================

// Deterministic value noise. Lifted from CinderTerrainSurface's Grain/Noise/Feather and
// copied rather than shared, exactly as the motion lane copied its id hash: the ground mask
// and the heightfield key off different seeds at different scales, and a shared helper would
// turn a tweak to the ash drift into a silent reshaping of every hill on every map. The
// constants stay local so the two can drift apart on purpose instead of by accident.
float Grain(int32 X, int32 Y, int32 Seed)
{
    uint32 Value = static_cast<uint32>(X) * 0x8da6b343u
        ^ static_cast<uint32>(Y) * 0xd8163841u ^ static_cast<uint32>(Seed) * 0xcb1ab31fu;
    Value ^= Value >> 13;
    Value *= 0x85ebca6bu;
    Value ^= Value >> 16;
    return static_cast<float>(Value & 65535u) / 65535.0f;
}

float Noise(float X, float Y, int32 Seed)
{
    const int32 IX = FMath::FloorToInt(X), IY = FMath::FloorToInt(Y);
    const float FX = X - IX, FY = Y - IY;
    const float SX = FX * FX * (3 - 2 * FX), SY = FY * FY * (3 - 2 * FY);
    return FMath::Lerp(FMath::Lerp(Grain(IX, IY, Seed), Grain(IX + 1, IY, Seed), SX),
        FMath::Lerp(Grain(IX, IY + 1, Seed), Grain(IX + 1, IY + 1, Seed), SX), SY);
}

/** 1 at Distance 0, 0 at Distance >= Radius, smoothstep between. Peak slope is 1.5 / Radius. */
float Feather(float Distance, float Radius)
{
    const float T = FMath::Clamp(1 - Distance / Radius, 0.0f, 1.0f);
    return T * T * (3 - 2 * T);
}

/** 0 at Alpha <= 0, 1 at Alpha >= 1, smoothstep between. */
float SmoothRise(float Alpha)
{
    const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);
    return T * T * (3 - 2 * T);
}

// --- The hill field -------------------------------------------------------------------
//
// Three octaves. Amplitudes are half-ranges in centimetres, so the analytic span of the sum
// is +/- 73 cm and the measured span over every map, every match length and both player
// counts is +/- 41 cm - the extremes of three independent noise fields effectively never
// coincide. Wavelengths are the noise CELL size, not the visible feature size; a feature
// spans about two cells, so the 1700 cm octave reads as a swell roughly a third of the
// standard map wide.
//
// The amplitude-to-wavelength ratio is the thing under test, not the amplitude: a smoothstep
// value-noise octave peaks at 3 * Amplitude / Wavelength, which is 0.081, 0.070 and 0.059
// here for a worst-case sum of 0.21. That sum is unreachable (it needs all three octaves at
// maximum slope in the same direction at the same point); the measured worst single-sample
// gradient over all nine standard heightfields is 0.148 against the 0.20 ceiling.
//
// Wavelengths are absolute centimetres and are deliberately NOT scaled by worldSize. The
// grain of a landscape is a physical scale - a hill is a hill whether the match is Short or
// Long - and scaling it would make Short 33% steeper at the same time as Short samples the
// field 33% more finely, which is precisely the case the gradient ceiling has least slack in.
// Retuned upward after looking at the first rendered capture. At 46/19/8 the field peaked
// near 41 cm on a 4800 cm map and read as a single soft swell rather than as hills: the
// relief was there in the sun shading but had no mid-scale structure to catch it. The lift
// is deliberately weighted toward octaves 1 and 2, because the 1700 cm octave spans most of
// the visible viewport at the shipped camera distance and reads as a gradient no matter how
// tall it is, while the 820 and 410 cm octaves are what actually resolve as rolling ground.
constexpr float HillAmplitude[3] = {62.0f, 36.0f, 15.0f};
constexpr float HillWavelength[3] = {1700.0f, 820.0f, 410.0f};
// Seeds spread 101 apart per octave and 31 apart per map so no two of the nine octave fields
// share a seed; without this the three maps would wear the same hills in the same places.
constexpr int32 HillSeedBase = 17;
constexpr int32 HillSeedPerOctave = 101;
constexpr int32 HillSeedPerMap = 31;

// --- Troughs: shallow meandering drainage channels -------------------------------------
//
// TroughDepth over TroughShoulder is the gradient that matters: 1.5 * 24 / 560 = 0.064. The
// originally sketched 180 cm half width would have been 1.5 * 24 / 180 = 0.20, which is the
// ENTIRE walkable budget spent on the trough wall alone, leaving nothing for the hills the
// channel cuts through. So the channel keeps its narrow flat floor (60 cm of core) and grows
// a genuinely wide shoulder instead; that also happens to be what a dry wash actually looks
// like. TroughDamp flattens the hills inside the channel toward its floor rather than simply
// subtracting from them, which is both how erosion behaves and what bounds the result: at
// full weight the relief is 0.6 * Hills - 24, so the channel can never push |relief| past
// what the hills alone already reach.
constexpr float TroughShoulder = 560.0f;
// Deepened with the hills so a channel still reads as a channel against the taller field.
constexpr float TroughDepth = 34.0f;
constexpr float TroughDamp = 0.40f;

// --- Pathways: the worn routes players actually march and build on ---------------------
//
// PRESENTATION ONLY. These do not have to match pathfinding and nothing in Sim knows they
// exist; they only have to lie in open ground and read as the roads of the place. What they
// buy is gameplay readability: a route DAMPS the surrounding relief toward flat and sinks it
// slightly, so the ground players move and build on is the flattest ground on the map and a
// flat building footprint never ends up sitting on a visible slope. Measured local gradient
// on route is 0.04 against 0.148 in the open field.
//
// PathDamp 0.85 leaves 15% of the hill field in the roadbed so a long route still follows the
// land instead of reading as a ruler-straight cut. The 6 cm sink is small on purpose: it has
// to be visible as wear without becoming a ditch, and 1.5 * 6 / 520 = 0.017 of gradient is
// almost free. The 520 cm shoulder is set by the damping term rather than the sink, since
// removing up to 41 cm of hill over the shoulder costs 1.5 * 41 * 0.85 / 520 = 0.10.
constexpr float PathShoulder = 520.0f;
constexpr float PathSink = 6.0f;
constexpr float PathDamp = 0.85f;

// --- The two mandatory feathers --------------------------------------------------------
//
// ObstacleFeatherRadius takes the walkable relief to exactly zero on and inside every
// obstacle rectangle, so the existing cliff ramp still rises out of flat ground and none of
// the terracing math above sees a moved baseline. 520 cm is wide enough that the relief is
// already near zero where the ramp skirt begins; it is also why the ground immediately
// around a cliff - the ground buildings crowd against - is the other flat part of the map.
//
// BoundaryFeather takes it to exactly zero at the world edge. 900 cm looks extravagant until
// you price it: the boundary has to shed up to 41 cm of relief, and doing that over a narrow
// band is a wall. 1.5 * 41 / 900 = 0.068.
constexpr float ObstacleFeatherRadius = 520.0f;
constexpr float BoundaryFeather = 900.0f;

// Route geometry is shared with the portable simulation definition.
constexpr int32 PathwaySamplesPerSegment = 3;
const cinder::MapDefinition& DefinitionFor(const cinder::Simulation& Simulation)
{
    const auto& Config = Simulation.config();
    return cinder::mapDefinition(Config.map, Config.playerCount, Config.matchLength, Config.mapRevision);
}
struct FRoutePoint { float X; float Y; };

float SegmentDistance(float X, float Y, const FRoutePoint& A, const FRoutePoint& B, float Scale)
{
    const float AX = A.X * Scale, AY = A.Y * Scale;
    const float DX = B.X * Scale - AX, DY = B.Y * Scale - AY;
    const float LengthSq = DX * DX + DY * DY;
    // A degenerate segment collapses to its first point rather than dividing by zero; the
    // closed four-player ring route repeats its start point as its end point.
    const float Along = LengthSq > 1.0f
        ? FMath::Clamp(((X - AX) * DX + (Y - AY) * DY) / LengthSq, 0.0f, 1.0f) : 0.0f;
    const float PX = X - AX - Along * DX, PY = Y - AY - Along * DY;
    return FMath::Sqrt(PX * PX + PY * PY);
}

/** Strongest route influence, including the authoritative authored width. */
float RouteWeight(const cinder::MapDefinition& Definition, bool bDrainage, float X, float Y,
    float Shoulder)
{
    float Weight = 0;
    const float Scale = Definition.worldSize / cinder::Simulation::WorldSize;
    for (const auto& Route : Definition.routes)
    {
        if ((Route.kind == cinder::MapRouteKind::Drainage) != bDrainage) continue;
        for (size_t Point = 0; Point + 1 < Route.points.size(); ++Point)
        {
            const auto A = Route.points[Point], B = Route.points[Point + 1];
            const float Distance = SegmentDistance(X, Y, {A.x, A.y}, {B.x, B.y}, 1);
            Weight = FMath::Max(Weight, Feather(FMath::Max(0.0f,
                Distance - Route.halfWidth), Shoulder * Scale));
        }
    }
    return Weight;
}

/** Signed centimetres of rolling hill before troughs, routes and the two feathers. */
float RollingHills(float X, float Y, int32 Map)
{
    float Relief = 0.0f;
    for (int32 Octave = 0; Octave < 3; ++Octave)
    {
        const int32 Seed = HillSeedBase + Octave * HillSeedPerOctave + Map * HillSeedPerMap;
        // Noise spans 0..1; 2n-1 recentres it so the hill field has no DC offset and the flat
        // baseline stays the average height of the map rather than its floor.
        Relief += HillAmplitude[Octave] * (2.0f * Noise(X / HillWavelength[Octave],
            Y / HillWavelength[Octave], Seed) - 1.0f);
    }
    return Relief;
}
}

void GatherPathways(const cinder::Simulation& Simulation,
    TArray<TPair<cinder::Vec2, cinder::Vec2>>& OutSegments)
{
    OutSegments.Reset();
    for (const auto& Route : DefinitionFor(Simulation).routes)
    {
        if (Route.kind == cinder::MapRouteKind::Drainage) continue;
        for (size_t Index = 0; Index + 1 < Route.points.size(); ++Index)
            OutSegments.Emplace(Route.points[Index], Route.points[Index + 1]);
    }
}

int32 PathwaySampleCount(const cinder::Simulation& Simulation)
{
    int32 Count = 0;
    for (const auto& Route : DefinitionFor(Simulation).routes)
        if (Route.kind != cinder::MapRouteKind::Drainage && Route.points.size() > 1)
            Count += static_cast<int32>(Route.points.size() - 1) * PathwaySamplesPerSegment;
    return Count;
}

void PathwaySample(const cinder::Simulation& Simulation, int32 Index, float& OutX, float& OutY)
{
    OutX = OutY = Simulation.worldSize() * 0.5f;
    if (Index < 0) return;
    for (const auto& Route : DefinitionFor(Simulation).routes)
    {
        if (Route.kind == cinder::MapRouteKind::Drainage || Route.points.size() < 2) continue;
        const int32 Count = static_cast<int32>(Route.points.size() - 1) * PathwaySamplesPerSegment;
        if (Index >= Count) { Index -= Count; continue; }
        const int32 Segment = Index / PathwaySamplesPerSegment;
        const auto A = Route.points[Segment], B = Route.points[Segment + 1];
        const float Along = static_cast<float>(Index % PathwaySamplesPerSegment + 1)
            / static_cast<float>(PathwaySamplesPerSegment + 1);
        OutX = FMath::Lerp(A.x, B.x, Along); OutY = FMath::Lerp(A.y, B.y, Along);
        return;
    }
}

bool UsesAuthoredCliffBody(const cinder::Simulation& Simulation, int32 ObstacleIndex)
{
    const auto& Definition = DefinitionFor(Simulation);
    if (ObstacleIndex < 0 || static_cast<size_t>(ObstacleIndex) >= Definition.obstacles.size()
        || static_cast<size_t>(ObstacleIndex) >= Simulation.obstacles().size()) return false;
    const auto& Expected = Definition.obstacles[ObstacleIndex];
    const auto& Actual = Simulation.obstacles()[ObstacleIndex];
    return FMath::IsNearlyEqual(Actual.center.x, Expected.center.x, 0.01f)
        && FMath::IsNearlyEqual(Actual.center.y, Expected.center.y, 0.01f)
        && FMath::IsNearlyEqual(Actual.half.x, Expected.half.x, 0.01f)
        && FMath::IsNearlyEqual(Actual.half.y, Expected.half.y, 0.01f);
}

float VisualObstacleTopAt(const cinder::Simulation& Simulation, float X, float Y,
    bool bTerrainRelief, float FootprintRadius)
{
    const float Scale = FMath::Max(1.0f, Simulation.worldSize()) / cinder::Simulation::WorldSize;
    const float Shoulder = 520.0f * Scale;
    const float HullRadius = FMath::Max(0.0f, FootprintRadius);
    float Top = 0.0f;
    for (int32 Index = 0; Index < static_cast<int32>(Simulation.obstacles().size()); ++Index)
    {
        const cinder::Obstacle& Obstacle = Simulation.obstacles()[Index];
        const float DX = FMath::Max(0.0f, FMath::Abs(X - Obstacle.center.x) - Obstacle.half.x);
        const float DY = FMath::Max(0.0f, FMath::Abs(Y - Obstacle.center.y) - Obstacle.half.y);
        const float Distance = FMath::Max(0.0f, FMath::Sqrt(DX * DX + DY * DY) - HullRadius);
        if (Distance >= Shoulder) continue;
        // The low support terrain is not the top of an authored rock body. Flat
        // fallback walls share the same conservative 340 cm bound. Legacy landscape
        // cliffs still carry up to 120 cm of cap dressing above their own peak.
        const float Peak = FMath::Clamp(FMath::Min(Obstacle.half.x, Obstacle.half.y) * 2.0f,
            CliffPeakFloor, MaxRelief);
        const auto& Definition = DefinitionFor(Simulation);
        const bool bWall = Simulation.usesAuthoredTerrain()
            && Definition.obstacleKinds[Index] == cinder::MapObstacleKind::RetainingWall;
        const float ObstacleTop = bWall ? 210.0f : UsesAuthoredCliffBody(Simulation, Index) || !bTerrainRelief
            ? AuthoredCliffBodyMaxHeight : Peak + 120.0f;
        Top = FMath::Max(Top, (ObstacleTop + (bWall ? 0.0f : Simulation.terrainHeight(Obstacle.center))) * Feather(Distance, Shoulder));
    }
    return Top;
}

float HeightAt(const cinder::Simulation& Simulation, float X, float Y)
{
    // One sample of the terrain generator, in closed form. BuildHeightData calls straight
    // through to this for every vertex, so the heightfield the landscape renders and the
    // relief other presentation systems seat props against can never disagree.
    //
    // This is also a hot runtime path - roughly 1,300 calls per scenery rebuild - so it stays
    // O(obstacles + trough segments + route segments) with no allocation, no container built
    // per call and no iteration over Simulation.entities(). Every route is a constant scaled
    // by worldSize; the only thing read out of the Simulation is its obstacle rectangles.
    const float WorldSize = FMath::Max(1.0f, Simulation.worldSize());
    const float Spacing = VertexSpacing(WorldSize);
    // Routes are authored over the standard 4800 cm map, so every other match length reads
    // the same layout stretched over its own coordinates, exactly as Simulation::reset does
    // with its obstacle and cluster tables.
    const float Scale = WorldSize / cinder::Simulation::WorldSize;
    const int32 Map = FMath::Clamp(Simulation.config().map, 0, MapCount - 1);

    // Authored traversal height is exact on pads and ramps. Noise would undermine
    // footprint placement and slope readability; lowland variation belongs in material.
    if (Simulation.usesAuthoredTerrain())
    {
        const auto& Definition = DefinitionFor(Simulation);
        float AuthoredHeight = Definition.terrainHeight({X, Y});
        for (size_t Index = 0; Index < Definition.obstacles.size(); ++Index)
        {
            if (Definition.obstacleKinds[Index] != cinder::MapObstacleKind::RockMass) continue;
            const auto& Obstacle = Definition.obstacles[Index];
            const float Inset = FMath::Min(Obstacle.half.x - FMath::Abs(X - Obstacle.center.x),
                Obstacle.half.y - FMath::Abs(Y - Obstacle.center.y));
            if (Inset >= Spacing) AuthoredHeight += AuthoredCliffFoundationHeight
                * SmoothRise((Inset - Spacing) / (Spacing * 1.5f));
        }
        return AuthoredHeight;
    }
    float Height = 0.0f;
    // How close this sample is to the nearest obstacle rectangle, 1 on or inside one and 0
    // beyond the feather. Accumulated in the same pass as the cliffs so the obstacle list is
    // walked exactly once.
    float ObstacleProximity = 0.0f;
    const int32 ObstacleCount = static_cast<int32>(Simulation.obstacles().size());
    for (int32 ObstacleIndex = 0; ObstacleIndex < ObstacleCount; ++ObstacleIndex)
    {
        const cinder::Obstacle& Obstacle = Simulation.obstacles()[ObstacleIndex];
        const float OutsideX = FMath::Max(0.0f, FMath::Abs(X - Obstacle.center.x) - Obstacle.half.x);
        const float OutsideY = FMath::Max(0.0f, FMath::Abs(Y - Obstacle.center.y) - Obstacle.half.y);
        ObstacleProximity = FMath::Max(ObstacleProximity,
            Feather(FMath::Sqrt(OutsideX * OutsideX + OutsideY * OutsideY),
                ObstacleFeatherRadius * Scale));

        const float InsetX = Obstacle.half.x - FMath::Abs(X - Obstacle.center.x);
        const float InsetY = Obstacle.half.y - FMath::Abs(Y - Obstacle.center.y);
        const float Inset = FMath::Min(InsetX, InsetY);
        // THE ONE MANDATORY GUARD, AND IT IS EXACTLY ONE VERTEX WIDE. A heightfield is
        // bilinear between samples Spacing apart, and the rectangle edge lands at an
        // arbitrary phase inside the quad that contains it. Zeroing every vertex with
        // Inset < Spacing is what makes BOTH corners of that quad zero at every phase,
        // which is what keeps the cliff term exactly 0.000000 at and outside the rectangle
        // line. Units render at Z = 0, so relief that leaked past the boundary would be
        // walkable-looking ground a unit cannot enter. This is correctness, not margin.
        if (Inset < Spacing) continue;
        if (UsesAuthoredCliffBody(Simulation, ObstacleIndex))
        {
            // Full-height meshes supply this silhouette from the fixed world baseline.
            // Terrain supplies only a buried core, preserving its exact guard and the
            // surrounding walkable field. Stacking a mesa under the kit would retain
            // the rectangular tabletop these authored formations replace.
            Height = FMath::Max(Height, AuthoredCliffFoundationHeight
                * SmoothRise((Inset - Spacing) / (Spacing * 1.5f)));
            continue;
        }
        // ContourWave SURVIVES, but as crest and toe variation ALONG the edge rather than
        // as inward erosion. The old code subtracted a further Spacing * (0.12 + 0.78*Wave)
        // of setback - 4.6 to 34.3 cm per side of ragged sine wander - which is exactly what
        // stopped the drawn rock from following the rectangle the simulation actually
        // blocks. The footprint IS the rectangle now; the wander moved into the profile.
        const float ContourWave = 0.5f + 0.25f * FMath::Sin(X * 0.013f + ObstacleIndex * 1.71f)
            + 0.25f * FMath::Sin(Y * 0.017f - ObstacleIndex * 1.19f);
        const float MinHalf = FMath::Min(Obstacle.half.x, Obstacle.half.y);
        // 1.05 made 30 of 32 obstacles clamp flat onto the old 180 floor, so a thin ridge
        // and a thick plateau rendered at identical heights. 2.0 against the real obstacle
        // tables gives map 1 {480, 342, 353, 267, 267}, a 1.80 spread.
        const float Peak = FMath::Clamp(MinHalf * 2.0f, CliffPeakFloor, MaxRelief);
        // Expressed in QUADS, not centimetres. The old 150 cm absolute was 1.3-3.9 quads
        // depending on match length, so the same obstacle had a different face steepness on
        // a Short map than on a Long one. The 0.55 keeps the crest inside the narrowest
        // rectangle's own half-width.
        const float RampWidth = FMath::Min(CliffFaceQuads * Spacing,
            FMath::Max(Spacing, (MinHalf - Spacing) * 0.55f));
        const float Alpha = FMath::Clamp((Inset - Spacing) / RampWidth, 0.0f, 1.0f);
        // Two stages, branchless by construction because SmoothStep clamps: below BenchAlpha
        // the crest term is 0 and this is the steep toe rise; above it the toe term is 1 and
        // this is the gentle crest bench. Continuous at the join.
        //
        // This replaces four geometric terraces. They needed four quads of ramp; the
        // narrowest obstacle in the game has 2.15 spacings of sculptable half-width, so the
        // strata were sub-resolution on every obstacle at 126 quads and never reached the
        // screen at all. Their successor is the material's procedural bedding, which costs
        // no geometry and therefore reads at any obstacle size.
        const float BenchAlpha = 1.0f - CliffCrestQuadFraction;
        const float ToeShape = FMath::SmoothStep(0.0f, 1.0f, Alpha / BenchAlpha);
        const float CrestShape = FMath::SmoothStep(0.0f, 1.0f,
            (Alpha - BenchAlpha) / CliffCrestQuadFraction);
        const float Shape = CliffBenchHeight * ToeShape + (1.0f - CliffBenchHeight) * CrestShape;
        const float Toe = CliffToeRelief * FMath::Lerp(0.80f, 1.20f, ContourWave);
        // Broad planar fragments and a shallow diagonal weathering cut break up the
        // tabletop and give the crest a height rhythm without eroding its blocked
        // footprint. A half-period spans at least four quads; finer folds disappear
        // into this 127-sample landscape or turn its cap into noisy teeth.
        const float LocalX = X - Obstacle.center.x;
        const float LocalY = Y - Obstacle.center.y;
        const float PatchSpan = FMath::Max(Spacing * 8.0f, MinHalf * 0.85f);
        const float PhaseA = (LocalX * 0.94f + LocalY * 0.34f) / PatchSpan
            + ObstacleIndex * 0.317f;
        const float PhaseB = (-LocalX * 0.42f + LocalY * 0.91f) / (PatchSpan * 1.47f)
            + ObstacleIndex * 0.193f + 0.27f;
        const float FacetA = FMath::Abs(2.0f * (PhaseA - FMath::FloorToFloat(PhaseA)) - 1.0f);
        const float FacetB = FMath::Abs(2.0f * (PhaseB - FMath::FloorToFloat(PhaseB)) - 1.0f);
        const float CutDistance = FMath::Abs(LocalX * 0.56f - LocalY * 0.83f
            + MinHalf * (ObstacleIndex % 2 == 0 ? 0.23f : -0.29f));
        const float Cut = Feather(CutDistance, FMath::Max(Spacing * 3.2f, MinHalf * 0.38f));
        // Thin ridges carry modest cap relief; broad mesas can show a deeper incision.
        // The cap floor remains above the tested obstacle-height floor, so no cut can
        // become a low passage across an obstacle that the simulation still blocks.
        const float CapRelief = FMath::Clamp((MinHalf - Spacing) * 0.20f, 18.0f, 72.0f);
        const float CapHeight = FMath::Max(CliffPeakFloor * 0.92f,
            Peak - CapRelief * (FacetA * 0.72f + FacetB * 0.28f + Cut * 0.46f));
        Height = FMath::Max(Height,
            FMath::Min(MaxRelief, FMath::Lerp(Toe, CapHeight, Shape)));
    }

    // --- Walkable relief, everywhere the cliffs are not --------------------------------
    float Relief = RollingHills(X, Y, Map);

    // A trough flattens the hills toward its floor and then sinks that floor. Damping rather
    // than plain subtraction is what bounds the result: at full weight this is
    // 0.6 * Hills - 24, so a channel can never drive |relief| past what the hills reach on
    // their own, and it is also simply what a drainage channel does to the ground it cuts.
    const float Trough = RouteWeight(DefinitionFor(Simulation), true, X, Y, TroughShoulder);
    Relief = Relief * (1.0f - TroughDamp * Trough) - TroughDepth * Trough;

    // A worn route does the same thing far harder: it is the flattest ground on the map by
    // construction, which is the whole point of authoring routes rather than letting the
    // noise decide where the level ground is.
    const float Pathway = RouteWeight(DefinitionFor(Simulation), false, X, Y, PathShoulder);
    Relief = Relief * (1.0f - PathDamp * Pathway) - PathSink * Pathway;

    // Both feathers are mandatory and both are exact, not approximate.
    //
    // (1 - ObstacleProximity) is exactly 0 for every point on or inside an obstacle rectangle,
    // because the outside distance there is exactly 0 and Feather(0, R) is exactly 1. So the
    // cliff terracing above is computed against an untouched flat baseline, the ramp skirt
    // still rises out of flat ground, and no walkable relief can leak into the one place
    // where relief means "impassable".
    //
    // SmoothRise(0) is exactly 0, so the four boundary rows and columns of the heightfield
    // encode to exactly FlatHeight and the landscape still meets the generated ground plane
    // of a non-canonical map without a seam.
    const float EdgeDistance = FMath::Min(FMath::Min(X, Y),
        FMath::Min(WorldSize - X, WorldSize - Y));
    Relief *= (1.0f - ObstacleProximity) * SmoothRise(EdgeDistance / (BoundaryFeather * Scale));

    // Belt and braces. The field is tuned so this never engages - the measured extreme over
    // every map, match length and player count is 41 cm against a 62 cm ceiling - but HeightAt
    // is also called at arbitrary off-grid positions to seat props, and the invariant that
    // walkable ground never looks like a wall has to hold for every query, not just for the
    // 16,129 that happen to land on a vertex.
    Relief = FMath::Clamp(Relief, -WalkableRelief, WalkableRelief);
    return Height + Relief;
}

void BuildHeightData(const cinder::Simulation& Simulation, TArray<uint16>& OutHeights)
{
    // If MaxRelief ever climbs past what the uint16 encoding can hold, the tallest samples
    // wrap and the plateau tops render as inverted pits with no runtime error of any kind.
    // This is the guard that makes that failure a compile error instead of a screenshot.
    static_assert(MaxRelief < MaxEncodableHeight,
        "Generated relief must stay inside the uint16 height encoding; raise HeightEncodeScale's "
        "ceiling by lowering it and doubling HeightActorZScale, never by lifting MaxRelief alone.");
    // The whole design rests on a player never confusing walkable relief with an obstacle
    // cliff. A walkable ceiling that crept up toward the cliff ceiling would break that by
    // eye long before any test noticed, so the separation is a compile error instead. 2x is
    // the loosest ratio at which the two still read as different kinds of thing; the shipped
    // ratio is 7.7x against the CEILING, but that was never the ratio a player saw: with the
    // old Peak formula 30 of 32 obstacles clamped onto the floor, so the real shipped ratio
    // was about 3.7x. It is an honest 2.5-4.6x now that Peak actually varies with size.
    static_assert(WalkableRelief * 2.0f < MaxRelief,
        "Walkable relief must stay far below the obstacle cliff ceiling; ground a unit strolls "
        "over must never be mistakable for the one thing on the map that actually blocks it.");
    // Heightfield-owned cliffs still need a tall silhouette. Authored mesh bodies instead
    // enclose a low support core; their height/footprint contract lives in scenery tests.
    static_assert(CliffPeakFloor > WalkableRelief * 2.0f,
        "The cliff FLOOR, not just the ceiling, must clear walkable relief.");
    static_assert(CliffPeakFloor <= MaxRelief, "The cliff floor cannot exceed the cliff ceiling.");
    static_assert(CliffToeRelief > WalkableRelief,
        "The toe must clear walkable relief within one quad or the mandatory guard band reads "
        "as a walkable ledge wrapped around every obstacle.");
    static_assert(AuthoredCliffFoundationHeight < WalkableRelief,
        "The support beneath an authored cliff body must not become a second terrain mesa.");
    OutHeights.Init(FlatHeight, SamplesPerAxis * SamplesPerAxis);
    const float Spacing = VertexSpacing(Simulation.worldSize());
    for (int32 Y = 0; Y < SamplesPerAxis; ++Y)
    {
        const float WorldY = Y * Spacing;
        for (int32 X = 0; X < SamplesPerAxis; ++X)
        {
            const float WorldX = X * Spacing;
            const float Height = HeightAt(Simulation, WorldX, WorldY);
            const int32 Encoded = FlatHeight + FMath::RoundToInt(Height * HeightEncodeScale);
            OutHeights[Y * SamplesPerAxis + X] = static_cast<uint16>(FMath::Clamp(Encoded, 0, 65535));
        }
    }
}
}

namespace
{
/**
 * Relief only reads as relief when it occludes the sun. Exactly one shadow-casting light
 * exists in this project, so the landscape's own cast shadow is the single cue that turns
 * the terraces into ground instead of a painted texture, and it is the only cue that still
 * works once the thermal ladder zeroes bloom.
 *
 * Only ever enabled on the one visible Landscape: at forced LOD 0 the active map is 31,752
 * triangles through two 1024 CSM cascades, which is measurable but bounded. The hidden maps
 * stay off so a map switch cannot leave three landscapes in the shadow depth pass.
 *
 * REVERT THIS FIRST if GPU time regresses more than 1.2 ms: it is the largest single GPU
 * cost added by the terrain pass and the geometry still reads (worse) without it.
 */
void SetLandscapeShadowCasting(ALandscape* Landscape, bool bCastShadow)
{
    if (!Landscape) return;
    TInlineComponentArray<UPrimitiveComponent*> Components(Landscape);
    // SetCastShadow already no-ops when the flag is unchanged, so a redundant map switch
    // costs nothing and never dirties a render state.
    for (UPrimitiveComponent* Component : Components) Component->SetCastShadow(bCastShadow);
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
            // This small fixed grid keeps its relief honest at every camera zoom. A coarse
            // height mip averages a 480 cm cliff against the walkable ground beside it, which
            // both softens the mesa silhouette and, far worse, fabricates a slope across a
            // playable lane that the generator's own gradient ceiling never permitted.
            if (auto* TerrainComponent = Cast<ULandscapeComponent>(Component))
                TerrainComponent->SetForcedLOD(0);
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Component->SetCanEverAffectNavigation(false);
            // Off for every map at discovery time. Update() turns it back on for the one
            // active Landscape, which is where visibility is decided.
            Component->SetCastShadow(false);
        }
        // One tag, no escape hatch. The legacy signature this used to also accept did not
        // hash worldSize at all, so a Short or Long bake could satisfy a Standard tag - a
        // second way for the rendered heightfield to disagree with HeightAt, behind the
        // first. A landscape that does not carry the current tag is stale by definition.
        if (!Landscape->ActorHasTag(CinderLandscapeTerrain::GeometryTag(
            CinderLandscapeTerrain::CanonicalGeometrySignature(TaggedMap)))) continue;
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
    {
        // Visibility and shadow casting flip together so no hidden map can keep paying for
        // a shadow depth pass, and so the two can never be reasoned about separately.
        SetLandscapeShadowCasting(Landscapes[ActiveMap], false);
        Landscapes[ActiveMap]->SetActorHiddenInGame(true);
    }
    if (FallbackComponent)
    {
        FallbackComponent->SetCastShadow(false);
        FallbackComponent->SetHiddenInGame(true);
        FallbackComponent->SetVisibility(false);
    }
    ActiveMap = INDEX_NONE;
}

void UCinderLandscapeTerrain::BuildFallbackMeshDescription(
    const cinder::Simulation& Simulation, FMeshDescription& OutMesh)
{
    using namespace CinderLandscapeTerrain;
    OutMesh.Empty();
    FStaticMeshAttributes Attributes(OutMesh);
    Attributes.Register();
    auto Positions = Attributes.GetVertexPositions();
    auto Normals = Attributes.GetVertexInstanceNormals();
    auto Tangents = Attributes.GetVertexInstanceTangents();
    auto BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
    auto Colors = Attributes.GetVertexInstanceColors();
    auto UVs = Attributes.GetVertexInstanceUVs();
    UVs.SetNumChannels(1);
    const FPolygonGroupID Group = OutMesh.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Ground");

    const int32 SampleCount = SamplesPerAxis * SamplesPerAxis;
    const float Spacing = VertexSpacing(Simulation.worldSize());
    OutMesh.ReserveNewVertices(SampleCount);
    OutMesh.ReserveNewVertexInstances(SampleCount);
    OutMesh.ReserveNewTriangles(QuadsPerAxis * QuadsPerAxis * 2);
    OutMesh.ReserveNewPolygons(QuadsPerAxis * QuadsPerAxis * 2);
    OutMesh.ReserveNewEdges(QuadsPerAxis * (QuadsPerAxis * 3 + 2));
    TArray<float> Heights;
    Heights.SetNumUninitialized(SampleCount);
    for (int32 Y = 0; Y < SamplesPerAxis; ++Y)
        for (int32 X = 0; X < SamplesPerAxis; ++X)
            Heights[Y * SamplesPerAxis + X] = HeightAt(Simulation, X * Spacing, Y * Spacing);

    TArray<FVertexInstanceID> Instances;
    Instances.SetNumUninitialized(SampleCount);
    for (int32 Y = 0; Y < SamplesPerAxis; ++Y)
    {
        for (int32 X = 0; X < SamplesPerAxis; ++X)
        {
            const int32 Index = Y * SamplesPerAxis + X;
            const FVertexID Vertex = OutMesh.CreateVertex();
            Positions[Vertex] = FVector3f(X * Spacing, Y * Spacing, BaselineZ + Heights[Index]);
            const FVertexInstanceID Instance = OutMesh.CreateVertexInstance(Vertex);
            Instances[Index] = Instance;
            const int32 X0 = FMath::Max(0, X - 1), X1 = FMath::Min(QuadsPerAxis, X + 1);
            const int32 Y0 = FMath::Max(0, Y - 1), Y1 = FMath::Min(QuadsPerAxis, Y + 1);
            const float DX = (Heights[Y * SamplesPerAxis + X1] - Heights[Y * SamplesPerAxis + X0])
                / ((X1 - X0) * Spacing);
            const float DY = (Heights[Y1 * SamplesPerAxis + X] - Heights[Y0 * SamplesPerAxis + X])
                / ((Y1 - Y0) * Spacing);
            Normals[Instance] = FVector3f(-DX, -DY, 1.0f).GetSafeNormal();
            Tangents[Instance] = FVector3f(1.0f, 0.0f, DX).GetSafeNormal();
            BinormalSigns[Instance] = 1.0f;
            Colors[Instance] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
            UVs.Set(Instance, 0, FVector2f(static_cast<float>(X) / QuadsPerAxis,
                static_cast<float>(Y) / QuadsPerAxis));
        }
    }
    for (int32 Y = 0; Y < QuadsPerAxis; ++Y)
    {
        for (int32 X = 0; X < QuadsPerAxis; ++X)
        {
            const int32 I00 = Y * SamplesPerAxis + X;
            const int32 I10 = I00 + 1, I01 = I00 + SamplesPerAxis, I11 = I01 + 1;
            // Match Landscape's diagonal and winding, so a missing bake cannot change
            // which side of a ramp is rendered or how each grid square interpolates.
            const FVertexInstanceID First[] = {Instances[I00], Instances[I11], Instances[I10]};
            const FVertexInstanceID Second[] = {Instances[I00], Instances[I01], Instances[I11]};
            OutMesh.CreateTriangle(Group, MakeArrayView(First));
            OutMesh.CreateTriangle(Group, MakeArrayView(Second));
        }
    }
}

bool UCinderLandscapeTerrain::UpdateFallback(const cinder::Simulation& Simulation,
    UTexture2D* FogMask, UTexture2D* TerrainLayers)
{
    AActor* Owner = GetOwner();
    if (!Simulation.usesAuthoredTerrain() || !Owner || !GetWorld()
        || !FogMask || !TerrainLayers || !FApp::CanEverRender()) return false;

    if (!bTriedFallbackMaterial)
    {
        bTriedFallbackMaterial = true;
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/Art/Canyon/Materials/M_CinderCanyonGround.M_CinderCanyonGround"), nullptr, LOAD_NoWarn);
        if (Material) FallbackMaterial = UMaterialInstanceDynamic::Create(Material, this);
        if (FallbackMaterial) CinderGroundPalette::Apply(FallbackMaterial);
        else UE_LOG(LogTemp, Error, TEXT("Cinderline authored terrain fallback requires M_CinderCanyonGround."));
    }
    if (!FallbackMaterial) return false;

    const uint64 Signature = CinderLandscapeTerrain::GeometrySignature(Simulation);
    if (FailedFallbackSignatures.Contains(Signature)) return false;
    UStaticMesh* Mesh = FallbackMeshes.FindRef(Signature);
    if (!Mesh)
    {
        FMeshDescription Description;
        BuildFallbackMeshDescription(Simulation, Description);
        Mesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
        Mesh->GetStaticMaterials().Add(FStaticMaterial(FallbackMaterial, TEXT("Ground")));
        Mesh->bSupportRayTracing = false;
        Mesh->MarkAsNotHavingNavigationData();
        // Fast builds create a BlockAll body setup even when simple collision is
        // disabled. SetStaticMesh can then visit physics creation while registering
        // this visual-only surface. Opt out before building, using the same contract
        // as collision-free HLOD meshes, so no triangle cook or CPU copy is requested.
        Mesh->CreateBodySetup();
        UBodySetup* BodySetup = Mesh->GetBodySetup();
        BodySetup->DefaultInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
        BodySetup->bNeverNeedsCookedCollisionData = true;
        BodySetup->bHasCookedCollisionData = false;
        UStaticMesh::FBuildMeshDescriptionsParams Parameters;
        Parameters.bFastBuild = true; // Required by packaged runtime builds.
        Parameters.bBuildSimpleCollision = false;
        Parameters.bCommitMeshDescription = false;
        Parameters.bMarkPackageDirty = false;
        // Navigation/collision are entirely simulation-owned; no CPU physics copy needed.
        Parameters.bAllowCpuAccess = false;
        if (!Mesh->BuildFromMeshDescriptions({&Description}, Parameters))
        {
            FailedFallbackSignatures.Add(Signature);
            UE_LOG(LogTemp, Error, TEXT("Cinderline authored terrain fallback mesh build failed for geometry %llu."),
                static_cast<unsigned long long>(Signature));
            return false;
        }
        FallbackMeshes.Add(Signature, Mesh);
        UE_LOG(LogTemp, Display, TEXT("Cinderline authored terrain fallback generated for map %d, geometry %llu (%d triangles)."),
            Simulation.config().map, static_cast<unsigned long long>(Signature), Description.Triangles().Num());
    }
    if (!FallbackComponent)
    {
        FallbackComponent = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
        Owner->AddInstanceComponent(FallbackComponent);
        FallbackComponent->SetMobility(EComponentMobility::Movable);
        FallbackComponent->SetupAttachment(Owner->GetRootComponent());
        FallbackComponent->SetAbsolute(true, true, true);
        FallbackComponent->SetWorldTransform(FTransform::Identity);
        FallbackComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        FallbackComponent->SetCanEverAffectNavigation(false);
        FallbackComponent->SetHiddenInGame(true);
        FallbackComponent->SetVisibility(false);
        FallbackComponent->RegisterComponent();
    }
    if (FallbackComponent->GetStaticMesh() != Mesh) FallbackComponent->SetStaticMesh(Mesh);
    if (FallbackFogMask != FogMask || FallbackTerrainLayers != TerrainLayers
        || FallbackWorldSize != Simulation.worldSize())
    {
        FallbackMaterial->SetTextureParameterValue(TEXT("FogMask"), FogMask);
        FallbackMaterial->SetTextureParameterValue(TEXT("TerrainLayers"), TerrainLayers);
        FallbackMaterial->SetScalarParameterValue(TEXT("CinderWorldSizeInverse"),
            1.0f / FMath::Max(1.0f, Simulation.worldSize()));
        FallbackMaterial->SetScalarParameterValue(TEXT("AuthoredMap"), 1.0f);
        UTexture* BoundFog = nullptr;
        UTexture* BoundLayers = nullptr;
        if (!FallbackMaterial->GetTextureParameterValue(TEXT("FogMask"), BoundFog)
            || !FallbackMaterial->GetTextureParameterValue(TEXT("TerrainLayers"), BoundLayers)
            || BoundFog != FogMask || BoundLayers != TerrainLayers) return false;
        FallbackFogMask = FogMask;
        FallbackTerrainLayers = TerrainLayers;
        FallbackWorldSize = Simulation.worldSize();
    }
    FallbackComponent->SetMaterial(0, FallbackMaterial);
    FallbackComponent->SetCastShadow(true);
    FallbackComponent->SetHiddenInGame(false);
    FallbackComponent->SetVisibility(true);
    return true;
}

bool UCinderLandscapeTerrain::BindMaterialTextures(ALandscape* Landscape,
    UTexture2D* FogMask, UTexture2D* TerrainLayers, float WorldSizeInverse, bool bAuthoredMap)
{
    UWorld* World = GetWorld();
    if (!Landscape || !World || !FogMask || !TerrainLayers) return false;

    CinderGroundPalette::Apply(Landscape);
    if (World->GetFeatureLevel() != ERHIFeatureLevel::ES3_1)
    {
        Landscape->SetLandscapeMaterialTextureParameterValue(TEXT("FogMask"), FogMask);
        Landscape->SetLandscapeMaterialTextureParameterValue(TEXT("TerrainLayers"), TerrainLayers);
        Landscape->SetLandscapeMaterialScalarParameterValue(TEXT("CinderWorldSizeInverse"), WorldSizeInverse);
        Landscape->SetLandscapeMaterialScalarParameterValue(TEXT("AuthoredMap"), bAuthoredMap ? 1.0f : 0.0f);
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
            Dynamic->SetScalarParameterValue(TEXT("AuthoredMap"), bAuthoredMap ? 1.0f : 0.0f);
        }
        // The ES3_1 scene proxy reads MobileMaterialInterfaces only when constructed.
        if (bReplacedInterface) Component->MarkRenderStateDirty();
    }
    return true;
}

bool UCinderLandscapeTerrain::Update(const cinder::Simulation& Simulation,
    UTexture2D* FogMask, UTexture2D* TerrainLayers)
{
    // A headless success would incorrectly tell callers that rendered relief exists.
    // In particular, never build runtime static-mesh RHI resources under NullRHI.
    if (!FApp::CanEverRender()) { HideAll(); return false; }
    if (!bInitialized) Initialize();
    const int32 Map = Simulation.config().map;
    if (!FogMask || !TerrainLayers
        || Map < 0 || Map >= CinderLandscapeTerrain::MapCount)
    {
        HideAll();
        return false;
    }

    if (!CinderLandscapeTerrain::IsCanonicalGeometry(Simulation)
        || !Landscapes.IsValidIndex(Map) || !Landscapes[Map])
    {
        if (ActiveMap != INDEX_NONE) HideAll();
        if (UpdateFallback(Simulation, FogMask, TerrainLayers)) return true;
        HideAll();
        return false;
    }

    ALandscape* Selected = Landscapes[Map];
    if (BoundFogMasks[Map] != FogMask || BoundTerrainLayers[Map] != TerrainLayers)
    {
        if (!BindMaterialTextures(Selected, FogMask, TerrainLayers,
            1.0f / FMath::Max(1.0f, Simulation.worldSize()), Simulation.usesAuthoredTerrain()))
        {
            HideAll();
            // This bake cannot safely display the current fog/material bindings. Reject
            // it once so a working runtime fallback does not toggle visibility per frame.
            Landscapes[Map] = nullptr;
            return UpdateFallback(Simulation, FogMask, TerrainLayers);
        }
        BoundFogMasks[Map] = FogMask;
        BoundTerrainLayers[Map] = TerrainLayers;
    }
    if (ActiveMap != Map)
    {
        HideAll();
        Selected->SetActorHiddenInGame(false);
        SetLandscapeShadowCasting(Selected, true);
        ActiveMap = Map;
    }
    return true;
}
