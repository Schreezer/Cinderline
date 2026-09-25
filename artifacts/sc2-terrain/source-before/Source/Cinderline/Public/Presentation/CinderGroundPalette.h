#pragma once

#include "CoreMinimal.h"

class ALandscape;
class UMaterialInstanceDynamic;

/**
 * The battlefield's ground colour, in one place.
 *
 * Every value here is a live named parameter on the ground materials, because
 * unreal_visual_upgrade.py gives each Graph.scalar/Graph.color node a
 * parameter_name equal to its label. That means the whole surface can be
 * repainted from C++ with no .uasset edit, no generator run and no editor
 * round-trip - which is also why this is worth centralising rather than
 * scattering literals through the generator scripts.
 *
 * It MUST be pushed to both surfaces. The flat generated plane and the authored
 * Landscape are different materials and exactly one of them is visible at a
 * time: the plane is what you see whenever geometry is not canonical (every
 * four-player match, every Short and Long match), and the Landscape takes over
 * when it is. Pushing to only one leaves half the game's match types on the old
 * palette, and the swap is invisible because nothing logs which surface won.
 *
 * Parameters absent from a given material are ignored by Unreal, so the same
 * table is safe to push at both sites.
 */
namespace CinderGroundPalette
{
struct FPalette
{
    /** How far the sampled grain is pushed toward greyscale before tinting.
        Counter-intuitively this wants to be HIGH. Every layer below multiplies
        the SAME desaturated grain by its own tint, so the grain's own warm
        colour is common to all of them: leaving it in (a low value) drags every
        layer back toward the same warm brown and cancels the hue spread. Push
        the grain to near-grey and the tints are what the eye actually sees.

        The layers below were never broken - they were all authored inside one
        narrow warm-brown band, so four working masks blended into a single
        indistinguishable sheet. Hue SPREAD is what makes them visible, not mask
        strength: cool sage against warm sand is the largest composition win
        available here, and it costs nothing because the fetches already happen.
        Keep these four meaningfully apart in hue or the ground goes flat again
        with no error anywhere. */
    float Desaturation = 0.93f;
    /** TWO-TONE COMPOSITION. The field is deep green and the built ground is pale
        stone; everything else is an accent on that pair. This is a deliberate
        move away from the old monochrome warm sand, which could not show its own
        layers: four working masks all tinted inside one narrow brown band blended
        into a single indistinguishable sheet.

        Hue clearance matters as much as boldness. Team 0's accent is teal near
        170 degrees, so the ground greens are pushed toward yellow (95 and 72
        degrees) to stay roughly 60-100 degrees clear. Do not drift these toward
        cyan or a team's identity colour stops reading against its own ground. */
    FLinearColor SandTint{0.24f, 0.34f, 0.16f, 1.0f};
    /** Macro octaves (880/1900/430 cm) - the large sunlit and shaded patches.
        This is the only layer covering the whole map, so it carries the
        large-scale variation. It is deliberately much LIGHTER than the base:
        the blend strength is a graph constant at 0.62 against a mask averaging
        ~0.47, so roughly a 29% blend - only a wide value gap survives that. */
    FLinearColor WindblownDust{0.54f, 0.62f, 0.28f, 1.0f};
    FLinearColor ExposedSandstone{0.42f, 0.33f, 0.21f, 1.0f};
    /** Stains around ore. Kept in the same family as the azure crystals so a
        deposit and its basin read as one feature rather than two. */
    FLinearColor MineralStaining{0.22f, 0.46f, 0.66f, 1.0f};
    /** Worn ground under bases and along routes - the reference's laid plaza. */
    FLinearColor PackedServiceGround{0.56f, 0.57f, 0.54f, 1.0f};
    /** Altitude shading. Relief this shallow reaches the eye only through these
        two multipliers, so they carry the entire read of the heightfield. */
    FLinearColor ValleyShade{0.68f, 0.78f, 0.74f, 1.0f};
    FLinearColor CrestShade{1.24f, 1.17f, 0.95f, 1.0f};
    /** CLIFF FACES. These three are the only values here that reach a near-vertical
        surface, because the material gates them behind a slope mask that no walkable
        sample can enter: WalkableGradient holds every lane at a normal Z of 0.965 or
        better, and the mask is fully off above 0.62.

        CliffFaceShade is a MULTIPLIER sharing CrestShade's slot, not an albedo. At the
        shipped camera pitch a plateau top is roughly half of an obstacle's projected area
        and is the brightest surface in the frame, so a face that keeps the cap's own
        multiplier makes the whole mesa read as one flat blob. 0.72 against CrestShade's
        1.24 is the intended 0.58x separation - push it toward 1.0 and the cliff flattens.

        The other two are the bedding ramp. Keep them dark and close in hue: they are
        multiplied by a bounded grain and land on faces that, for one of the two visible
        sides of every axis-aligned obstacle, receive exactly zero key light. */
    FLinearColor CliffFaceShade{0.72f, 0.68f, 0.62f, 1.0f};
    FLinearColor CanyonShadow{0.178f, 0.127f, 0.099f, 1.0f};
    FLinearColor CanyonSandstone{0.315f, 0.205f, 0.140f, 1.0f};
};

CINDERLINE_API const FPalette& Active();

/** Push onto the flat generated ground plane's dynamic instance. */
CINDERLINE_API void Apply(UMaterialInstanceDynamic* GroundSurface);

/** Push onto an authored Landscape, covering both the desktop and ES3_1 paths. */
CINDERLINE_API void Apply(ALandscape* Landscape);
}
