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
    // Muted olive regolith against warm worn routes. Keep the ground away from
    // the cyan team accents and reserve the brightest values for units and ore.
    float Desaturation = 0.93f;
    FLinearColor SandTint{0.19f, 0.22f, 0.16f, 1.0f};
    FLinearColor WindblownDust{0.43f, 0.40f, 0.29f, 1.0f};
    FLinearColor ExposedSandstone{0.34f, 0.29f, 0.22f, 1.0f};
    FLinearColor MineralStaining{0.25f, 0.34f, 0.38f, 1.0f};
    FLinearColor PackedServiceGround{0.45f, 0.42f, 0.34f, 1.0f};
    FLinearColor ValleyShade{0.72f, 0.78f, 0.75f, 1.0f};
    FLinearColor CrestShade{1.18f, 1.12f, 1.00f, 1.0f};

    // The rock albedo carries strata contrast. A gentle face multiplier keeps
    // the unlit side readable instead of darkening already-dark rock twice.
    FLinearColor CliffFaceShade{0.94f, 0.91f, 0.85f, 1.0f};
    FLinearColor CanyonShadow{0.24f, 0.20f, 0.16f, 1.0f};
    FLinearColor CanyonSandstone{0.48f, 0.36f, 0.24f, 1.0f};
};

CINDERLINE_API const FPalette& Active();

/** Push onto the flat generated ground plane's dynamic instance. */
CINDERLINE_API void Apply(UMaterialInstanceDynamic* GroundSurface);

/** Push onto an authored Landscape, covering both the desktop and ES3_1 paths. */
CINDERLINE_API void Apply(ALandscape* Landscape);
}
