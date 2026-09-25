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
    // These ground tints multiply a grain value centered near 0.20 linear.
    // Keep dust and worn routes close to the olive soil so shapes and units
    // provide the contrast, without pale painted ribbons across the battlefield.
    float Desaturation = 0.93f;
    FLinearColor SandTint{0.21f, 0.26f, 0.13f, 1.0f};
    FLinearColor WindblownDust{0.34f, 0.29f, 0.17f, 1.0f};
    FLinearColor ExposedSandstone{0.31f, 0.29f, 0.19f, 1.0f};
    FLinearColor MineralStaining{0.20f, 0.28f, 0.29f, 1.0f};
    FLinearColor PackedServiceGround{0.34f, 0.30f, 0.21f, 1.0f};
    FLinearColor ValleyShade{0.82f, 0.90f, 0.80f, 1.0f};
    FLinearColor CrestShade{1.08f, 1.06f, 0.97f, 1.0f};

    // Rock tints are absolute albedo with near-unity grain, shared with the
    // fractured slate props. Lighting and geometry supply the face contrast.
    FLinearColor CliffFaceShade{0.98f, 0.99f, 0.97f, 1.0f};
    FLinearColor CanyonShadow{0.065f, 0.082f, 0.078f, 1.0f};
    FLinearColor CanyonSandstone{0.135f, 0.15f, 0.12f, 1.0f};
};

CINDERLINE_API const FPalette& Active();

/** Push onto the flat generated ground plane's dynamic instance. */
CINDERLINE_API void Apply(UMaterialInstanceDynamic* GroundSurface);

/** Push onto an authored Landscape, covering both the desktop and ES3_1 paths. */
CINDERLINE_API void Apply(ALandscape* Landscape);
}
