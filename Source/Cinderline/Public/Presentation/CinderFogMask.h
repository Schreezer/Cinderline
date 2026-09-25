#pragma once

#include "CoreMinimal.h"
#include "Sim/Simulation.h"

/**
 * Fog mask texel generation, kept separate from the battlefield so the privacy
 * rule below can be tested directly rather than inferred from a rendered frame.
 *
 * Cell states are 0 unknown, 1 explored, 2 currently visible.
 *
 * Privacy rule: both feathers only ever shrink what the player can see. A texel
 * may report less visibility than its cell, never more, so bilinear filtering
 * cannot uncover ground outside an observed cell and no unexplored ground can be
 * revealed by a gradient. Every change here must preserve that direction.
 */
namespace CinderFogMask
{
constexpr int32 TextureSize = 256;
constexpr int32 Cells = cinder::Simulation::FogSize;
constexpr int32 PixelsPerCell = TextureSize / Cells;
static_assert(PixelsPerCell == 4, "Fog edge guards assume four pixels per simulation cell");

/** Visible edge: fully opaque within this many texels of a hidden cell. */
constexpr float VisibleGuardTexels = 0.75f;
constexpr float VisibleFeatherTexels = 2.25f;
/** Explored edge: searched in cells, ramped in texels, both inward only. */
constexpr int32 ExploredFeatherCells = 2;
constexpr float ExploredFeatherTexels = 5.0f;

struct FTexel
{
    /** 1 fully fogged, 0 clear. */
    float Opacity = 1;
    /** 0 unknown, 1 explored. */
    float Explored = 0;
};

/** CellStates must hold Cells*Cells entries. X and Y are texel coordinates. */
CINDERLINE_API FTexel SampleTexel(TConstArrayView<uint8> CellStates, int32 X, int32 Y);
}
