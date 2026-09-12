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
    const float Broad = Noise((X + Y * 0.24f) / 880.0f, Y / 340.0f, Features.Map + 31);
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
        Mineral = FMath::Max(Mineral, Feather(FMath::Sqrt(DX * DX + DY * DY), 115 + Broken * 125));
    }
    const float Ash = FMath::Clamp((Broad - 0.20f) * 1.55f + (Broken - 0.5f) * 0.12f, 0.0f, 1.0f);
    return FColor(static_cast<uint8>(Stone * 255), static_cast<uint8>(Mineral * 255),
        static_cast<uint8>(Ash * 255), 255);
}

void BuildPixels(const FFeatures& Features, TArray<uint8>& OutBGRA)
{
    OutBGRA.SetNumUninitialized(TextureSize * TextureSize * 4);
    for (int32 Y = 0; Y < TextureSize; ++Y) for (int32 X = 0; X < TextureSize; ++X)
    {
        const FColor Mask = Sample(Features, (X + 0.5f) * cinder::Simulation::WorldSize / TextureSize,
            (Y + 0.5f) * cinder::Simulation::WorldSize / TextureSize);
        const int32 Pixel = (Y * TextureSize + X) * 4;
        OutBGRA[Pixel] = Mask.B; OutBGRA[Pixel + 1] = Mask.G;
        OutBGRA[Pixel + 2] = Mask.R; OutBGRA[Pixel + 3] = 255;
    }
}
}
