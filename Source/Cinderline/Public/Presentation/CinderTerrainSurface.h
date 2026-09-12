#pragma once
#include "CoreMinimal.h"
#include "Sim/Simulation.h"

namespace CinderTerrainSurface
{
constexpr int32 TextureSize = 256;

/** Immutable visual inputs. Include only explored cliffs and observed ore positions. */
struct FFeatures
{
    int32 Map = 0;
    TArray<cinder::Obstacle> Cliffs;
    TArray<cinder::Vec2> Minerals;
};

/** Packed linear mask: R exposed stone, G mineral stain, B windblown ash. */
FColor Sample(const FFeatures& Features, float X, float Y);
void BuildPixels(const FFeatures& Features, TArray<uint8>& OutBGRA);
}
