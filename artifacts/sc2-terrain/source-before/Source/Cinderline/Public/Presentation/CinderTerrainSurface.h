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
    float WorldSize = cinder::Simulation::WorldSize;
    TArray<cinder::Obstacle> Cliffs;
    TArray<cinder::Vec2> Minerals;
    TArray<cinder::Obstacle> ServicePads;
    TArray<TPair<cinder::Vec2, cinder::Vec2>> Roads;
};

/** Packed linear mask: R exposed stone, G mineral stain, B windblown ash, A service ground. */
FColor Sample(const FFeatures& Features, float X, float Y);
void BuildPixels(const FFeatures& Features, TArray<uint8>& OutBGRA);
}
