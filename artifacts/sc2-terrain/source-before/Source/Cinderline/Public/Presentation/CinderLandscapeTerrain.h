#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sim/Simulation.h"
#include "CinderLandscapeTerrain.generated.h"

class ALandscape;
class UTexture2D;

namespace CinderLandscapeTerrain
{
constexpr int32 MapCount = 3;
constexpr int32 ComponentCountPerAxis = 2;
constexpr int32 SubsectionsPerComponent = 1;
constexpr int32 SubsectionSizeQuads = 63;
constexpr int32 QuadsPerAxis = ComponentCountPerAxis * SubsectionsPerComponent * SubsectionSizeQuads;
constexpr int32 SamplesPerAxis = QuadsPerAxis + 1;
constexpr uint16 FlatHeight = 32768;
constexpr float BaselineZ = -1.0f;

// Heights ride in a uint16 as FlatHeight + Height * HeightEncodeScale, so the tallest
// representable relief is (65535 - FlatHeight) / HeightEncodeScale centimetres. This used
// to be 128, which capped relief at 255.9 cm; the 480 cm mesas below would have wrapped
// the uint16 and rendered the plateau tops as inverted pits with no compile or test error
// anywhere. 64 buys a 511.5 cm ceiling and still resolves 1/64 cm, two orders of magnitude
// finer than the 38.1 cm vertex spacing can express, so nothing visible is lost.
//
// THE PAIRED HALF OF THIS: Unreal decodes a landscape sample as
// (Stored - 32768) * (1/128) * ActorScale.Z, so halving the encode scale MUST be paid back
// with a doubled actor Z scale or every mesa silently renders at half height. The authoring
// library takes HeightActorZScale straight from this constant so the two can never drift.
constexpr float HeightEncodeScale = 64.0f;
constexpr float HeightActorZScale = 128.0f / HeightEncodeScale;
constexpr float MaxEncodableHeight = (65535.0f - static_cast<float>(FlatHeight)) / HeightEncodeScale;
/** Hard ceiling on generated relief. Kept below MaxEncodableHeight by static_assert. */
constexpr float MaxRelief = 480.0f;

/** Bumped BY HAND whenever HeightAt's output changes shape for a given obstacle layout, and
 *  folded into GeometrySignature so a stale bake cannot match its own tag.
 *
 *  THIS IS THE ONLY THING STANDING BETWEEN A FORGOTTEN RE-BAKE AND A SILENTLY WRONG MAP.
 *  The heightfield is baked into Frontier.umap, and nothing in this repo ever compares the
 *  baked asset to the generator - CinderLandscapeTerrainTests only checks BuildHeightData
 *  against HeightAt, both generated fresh in-process, so both move together and agree. The
 *  signature used to hash only map index, worldSize and obstacle rectangles: edit HeightAt,
 *  skip the bake, and the tag still matched. The player then saw the OLD heightfield while
 *  HeightAt returned the NEW heights to seat every unit, building, selection ring and prop
 *  against it. No test, no log line and no assert fired anywhere.
 *
 *  With the version folded in, a forgotten bake fails the tag check, Update() returns false
 *  and the game falls back to the flat plane WITH its props - loud and visibly wrong rather
 *  than quietly divergent. */
constexpr uint32 CliffProfileVersion = 3;

/** Relief at the first sculpted vertex, one VertexSpacing inside the rectangle. This is the
 *  CliffMass skirt prop moved into the heightfield: it turns the mandatory one-quad guard
 *  band from a flat apron into the base of a wall. 14 cm over a quad was a 20 degree mound;
 *  120 cm over a quad is a 72 degree wall base. */
constexpr float CliffToeRelief = 120.0f;

/** Floor on an obstacle's plateau. The tallest building in the game is the Anchor at 185 cm,
 *  and the shipped profile peaked at 186 cm on map 2 - a cliff exactly as tall as the base
 *  the player parks beside it, which is why obstacles never read as impassable without the
 *  prop mesas stacked on top of them. */
constexpr float CliffPeakFloor = 280.0f;

/** Widest cliff face, in vertex spacings. 2.4 is the most a 120-half obstacle (3.15 spacings
 *  of half-width, 2.15 of it sculptable after the guard) can spend without losing its own
 *  plateau. */
constexpr float CliffFaceQuads = 2.4f;

/** Fraction of the face spent on the crest bench, and the height that bench reaches.
 *
 *  THIS PAIR IS A LIGHTING CONSTANT, NOT A STYLE ONE, AND IT REPLACES THE FOUR TERRACES.
 *  The one shadow-casting light is FRotator(-46, 45, 0), so on an axis-aligned rectangle
 *  N dot -L is +0.49 on the -X face, EXACTLY 0.00 on the +Y face, and 0.72 on anything
 *  facing up. The camera boom is yaw -45, so the player always sees one lit face and one
 *  face with no key light at all. A face picks key light back up only past 34.3 degrees from
 *  vertical, i.e. N.z >= 0.564.
 *
 *  Four terraces would clear that, but they need four quads of ramp and the narrowest
 *  obstacle in the game has 2.15 spacings of sculptable half-width - they were sub-resolution
 *  on EVERY obstacle at 126 quads and never reached the screen, which is why they are gone.
 *  Spending the last half of the face on a gentle bench instead puts the CREST above 0.564,
 *  so every obstacle gets a lit rim along the top of its dark face. Raising CliffBenchHeight
 *  or lowering CliffCrestQuadFraction steepens the crest and takes that rim away. */
constexpr float CliffCrestQuadFraction = 0.50f;
constexpr float CliffBenchHeight = 0.62f;

/**
 * Hard ceiling on |relief| ANYWHERE a unit can stand, which means everywhere outside an
 * obstacle rectangle's interior.
 *
 * cinder::Vec2 is {float x, float y}. The simulation has no elevation, no terrain cost and
 * no slope, and an obstacle rectangle is the only impassable geometry there is. So relief
 * raised inside an obstacle is a CLIFF (impassable, up to MaxRelief), and relief raised
 * anywhere else is WALKABLE: a unit strolls straight over it and never even slows down.
 * That makes "shallow enough that no player could mistake it for a barrier" a correctness
 * requirement rather than a matter of taste - ground that reads as blocking but is not is
 * strictly worse than flat ground, because it teaches the player a lie about the map.
 * 62 cm against the 480 cm obstacle cliffs is a 1:7.7 ratio, so at the RTS camera pitch
 * the two silhouettes are never confusable.
 */
constexpr float WalkableRelief = 105.0f;
/**
 * Hard ceiling on the rise between two ADJACENT heightfield samples outside the obstacle
 * interiors, expressed as a gradient: |dRelief| / VertexSpacing, where adjacent samples sit
 * VertexSpacing apart (38.1 cm on the standard 4800 cm world, 28.6 cm on Short - Short is
 * the binding case because it samples the same centimetre-scale field most finely).
 * 0.20 is about 11 degrees: a slope a walking figure reads as a hillside, never as a step.
 *
 * Every width in the generator is sized backwards from this number. A smoothstep shoulder
 * that drops D centimetres over a radius R peaks at 1.5 * D / R, so a 24 cm trough with the
 * originally sketched 180 cm half width would peak at 0.20 on its own and leave no budget
 * at all for the hills it cuts through. That is why the troughs and worn routes below carry
 * shoulders five to twenty times wider than they are deep.
 */
constexpr float WalkableGradient = 0.27f;
/**
 * The same measurement taken on an authored worn route. Routes exist so that the ground
 * players actually march and build on is the flattest ground on the map - a flat building
 * footprint must never end up sitting on a visible slope - so they are held to well under
 * half the general walkable ceiling rather than merely to it.
 */
constexpr float PathwayGradient = 0.08f;

CINDERLINE_API float VertexSpacing(float WorldSize = cinder::Simulation::WorldSize);
CINDERLINE_API uint64 GeometrySignature(const cinder::Simulation& Simulation);
CINDERLINE_API uint64 CanonicalGeometrySignature(int32 Map);
CINDERLINE_API bool IsCanonicalGeometry(const cinder::Simulation& Simulation);
CINDERLINE_API FName MapTag(int32 Map);
CINDERLINE_API FName GeometryTag(uint64 Signature);
/**
 * Centimetres of terrain relief above the flat baseline at one world XY, 0 on flat ground.
 * This is the closed-form generator BuildHeightData rasterises, not a query against the
 * sampled landscape surface: presentation must never read back a render resource, and a
 * sampled query would drift with LOD and with the landscape's own bilinear filtering.
 * Use it to seat props (rocks, debris) on the relief without touching simulation XY.
 *
 * Relief is now signed: rolling hills rise, drainage troughs and worn routes cut below the
 * baseline, so this returns negative centimetres in the low ground. Anything anchored to
 * the surface must add it rather than assume a floor of zero. ACinderBattlefield::GroundHeight
 * already does exactly that and stays the entry point for gameplay-facing callers.
 */
CINDERLINE_API float HeightAt(const cinder::Simulation& Simulation, float X, float Y);
/**
 * The authored worn routes of the active map, sampled as world-space points along each
 * polyline. Presentation-only: these are the roads of the place, not a pathfinding
 * structure, and nothing in the simulation knows they exist. Exposed so the pathway
 * flatness invariant can be measured against the real route data instead of hardcoded
 * guesses, and so props that want to sit beside a road can find one. Derived entirely from
 * constants scaled by worldSize, so it allocates nothing and never walks Simulation
 * entities. Index in [0, PathwaySampleCount); out of range yields the world centre.
 */
CINDERLINE_API int32 PathwaySampleCount(const cinder::Simulation& Simulation);
CINDERLINE_API void PathwaySample(const cinder::Simulation& Simulation, int32 Index,
    float& OutX, float& OutY);
CINDERLINE_API void BuildCanonicalHeightData(int32 Map, TArray<uint16>& OutHeights);
CINDERLINE_API void BuildHeightData(const cinder::Simulation& Simulation, TArray<uint16>& OutHeights);
}

/** Selects a compatible pre-authored Standard Landscape; other sizes use the generated ground plane. */
UCLASS(ClassGroup=(Cinderline), meta=(BlueprintSpawnableComponent))
class CINDERLINE_API UCinderLandscapeTerrain : public UActorComponent
{
    GENERATED_BODY()
public:
    UCinderLandscapeTerrain();
    void Initialize();
    bool Update(const cinder::Simulation& Simulation, UTexture2D* FogMask, UTexture2D* TerrainLayers);

private:
    bool BindMaterialTextures(ALandscape* Landscape, UTexture2D* FogMask, UTexture2D* TerrainLayers,
        float WorldSizeInverse);
    void HideAll();
    UPROPERTY(Transient) TArray<TObjectPtr<ALandscape>> Landscapes;
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> BoundFogMasks;
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> BoundTerrainLayers;
    bool bInitialized = false;
    int32 ActiveMap = INDEX_NONE;
};
