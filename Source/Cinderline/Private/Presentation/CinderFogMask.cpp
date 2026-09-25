#include "Presentation/CinderFogMask.h"

namespace
{
/** Distance in texels from a texel centre to a cell's rectangle, zero inside it. */
float TexelDistanceToCell(int32 X, int32 Y, int32 CellX, int32 CellY)
{
    const float DX = FMath::Max(0.0f, FMath::Max(
        CellX * CinderFogMask::PixelsPerCell - (X + 0.5f),
        (X + 0.5f) - (CellX + 1) * CinderFogMask::PixelsPerCell));
    const float DY = FMath::Max(0.0f, FMath::Max(
        CellY * CinderFogMask::PixelsPerCell - (Y + 0.5f),
        (Y + 0.5f) - (CellY + 1) * CinderFogMask::PixelsPerCell));
    return FMath::Sqrt(DX * DX + DY * DY);
}

float SmoothStep(float Alpha)
{
    return Alpha * Alpha * (3 - 2 * Alpha);
}
}

namespace CinderFogMask
{
FTexel SampleTexel(TConstArrayView<uint8> CellStates, int32 X, int32 Y)
{
    FTexel Texel;
    if (CellStates.Num() != Cells * Cells
        || X < 0 || Y < 0 || X >= TextureSize || Y >= TextureSize) return Texel;
    const int32 CX = X / PixelsPerCell, CY = Y / PixelsPerCell;
    const uint8 State = CellStates[CY * Cells + CX];

    if (State == 2)
    {
        // Out-of-bounds counts as hidden, so the map edge never feathers open.
        float Distance = PixelsPerCell;
        for (int32 NY = CY - 1; NY <= CY + 1; ++NY) for (int32 NX = CX - 1; NX <= CX + 1; ++NX)
        {
            if (NX >= 0 && NY >= 0 && NX < Cells && NY < Cells
                && CellStates[NY * Cells + NX] == 2) continue;
            Distance = FMath::Min(Distance, TexelDistanceToCell(X, Y, NX, NY));
        }
        const float T = FMath::Clamp((Distance - VisibleGuardTexels) / VisibleFeatherTexels, 0.0f, 1.0f);
        Texel.Opacity = 1 - SmoothStep(T);
    }

    if (State)
    {
        float Distance = ExploredFeatherCells * PixelsPerCell;
        for (int32 NY = CY - ExploredFeatherCells; NY <= CY + ExploredFeatherCells; ++NY)
            for (int32 NX = CX - ExploredFeatherCells; NX <= CX + ExploredFeatherCells; ++NX)
            {
                if (NX >= 0 && NY >= 0 && NX < Cells && NY < Cells
                    && CellStates[NY * Cells + NX]) continue;
                Distance = FMath::Min(Distance, TexelDistanceToCell(X, Y, NX, NY));
            }
        const float E = FMath::Clamp((Distance - VisibleGuardTexels) / ExploredFeatherTexels, 0.0f, 1.0f);
        Texel.Explored = SmoothStep(E);
    }
    return Texel;
}
}
