#include "Presentation/CinderTerrainSurface.h"

namespace CinderTerrainSurface
{
namespace
{
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

float Feather(float Distance, float Radius)
{
    const float T = FMath::Clamp(1 - Distance / Radius, 0.0f, 1.0f);
    return T * T * (3 - 2 * T);
}
}

FColor Sample(const FFeatures& Features, float X, float Y)
{
    // The ash channel is the only macro-scale signal anywhere in the ground pipeline, and a
    // single octave of it gave the whole battlefield one flat statistical texture: no basins,
    // no drifts, nothing for the eye to compose against at the RTS camera height. Three
    // octaves instead - a ~900 cm stretched drift carrying the composition, a ~1800 cm swell
    // that decides which half of the map is pale, and a ~400 cm break that keeps the edges of
    // the drifts from reading as airbrush. Weights sum to 1 so the channel still spans 0..1
    // and the Ash contrast curve below is unchanged. All three seeds key off Features.Map so
    // a map change still moves this channel. CPU-only inside the hash-gated BuildPixels: the
    // GPU pays exactly one texture fetch for this whether it is one octave or ten.
    const float Broad = Noise((X + Y * 0.24f) / 880.0f, Y / 340.0f, Features.Map + 31) * 0.55f
        + Noise(X / 1900.0f, Y / 1650.0f, Features.Map + 11) * 0.30f
        + Noise(X / 430.0f, Y / 390.0f, Features.Map + 53) * 0.15f;
    const float Broken = Noise(X / 125.0f, Y / 125.0f, Features.Map + 73);
    float Stone = 0, Mineral = 0;
    for (const auto& Cliff : Features.Cliffs)
    {
        const float DX = FMath::Max(0.0f, FMath::Abs(X - Cliff.center.x) - Cliff.half.x);
        const float DY = FMath::Max(0.0f, FMath::Abs(Y - Cliff.center.y) - Cliff.half.y);
        // Flat surface staining may feather into walkable ground; geometry never does.
        Stone = FMath::Max(Stone, Feather(FMath::Sqrt(DX * DX + DY * DY), 115 + Broken * 135));
    }
    for (const auto& Ore : Features.Minerals)
    {
        const float DX = X - Ore.x, DY = Y - Ore.y;
        // 115 + 125 stained barely past the node's own footprint, so an ore cluster read as
        // three dots rather than as a basin worth fighting over. 150 + 170 widens the radius
        // by about a third: at 4800 cm across 256 texels that is 8 to 17 texels of falloff
        // instead of 6 to 13, thick enough to survive the 80% resolve and FXAA and to let
        // the three nodes of a cluster pool into one stain. Same invariant as the cliff
        // above: flat surface staining may feather into walkable ground because it changes
        // nothing a unit can collide with; the heightfield geometry never does.
        Mineral = FMath::Max(Mineral, Feather(FMath::Sqrt(DX * DX + DY * DY), 150 + Broken * 170));
    }
    float Service = 0;
    for (const auto& Pad : Features.ServicePads)
    {
        const float DX = FMath::Max(0.0f, FMath::Abs(X - Pad.center.x) - Pad.half.x);
        const float DY = FMath::Max(0.0f, FMath::Abs(Y - Pad.center.y) - Pad.half.y);
        Service = FMath::Max(Service, Feather(FMath::Sqrt(DX * DX + DY * DY), 32 + Broken * 24));
    }
    for (const auto& Road : Features.Roads)
    {
        const float DX = Road.Value.x - Road.Key.x, DY = Road.Value.y - Road.Key.y;
        const float LengthSq = DX * DX + DY * DY;
        const float Along = LengthSq > 1 ? FMath::Clamp(((X - Road.Key.x) * DX + (Y - Road.Key.y) * DY) / LengthSq, 0.0f, 1.0f) : 0;
        const float PX = X - Road.Key.x - Along * DX, PY = Y - Road.Key.y - Along * DY;
        Service = FMath::Max(Service, Feather(FMath::Max(0.0f, FMath::Sqrt(PX * PX + PY * PY) - 24), 28 + Broken * 12));
    }
    const float Ash = FMath::Clamp((Broad - 0.20f) * 1.55f + (Broken - 0.5f) * 0.12f, 0.0f, 1.0f);
    return FColor(static_cast<uint8>(Stone * 255), static_cast<uint8>(Mineral * 255),
        static_cast<uint8>(Ash * 255), static_cast<uint8>(Service * 255));
}

void BuildPixels(const FFeatures& Features, TArray<uint8>& OutBGRA)
{
    const float WorldSize = FMath::Max(1.0f, Features.WorldSize);
    OutBGRA.SetNumUninitialized(TextureSize * TextureSize * 4);
    for (int32 Y = 0; Y < TextureSize; ++Y) for (int32 X = 0; X < TextureSize; ++X)
    {
        const FColor Mask = Sample(Features, (X + 0.5f) * WorldSize / TextureSize,
            (Y + 0.5f) * WorldSize / TextureSize);
        const int32 Pixel = (Y * TextureSize + X) * 4;
        OutBGRA[Pixel] = Mask.B; OutBGRA[Pixel + 1] = Mask.G;
        OutBGRA[Pixel + 2] = Mask.R; OutBGRA[Pixel + 3] = Mask.A;
    }
}
}
