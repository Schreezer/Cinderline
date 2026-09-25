#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Presentation/CinderFogMask.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderFogMaskPrivacy,
    "Cinderline.Integration.FogMaskPrivacy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderFogMaskPrivacy::RunTest(const FString& Parameters)
{
    using namespace CinderFogMask;
    TArray<uint8> CellStates;
    CellStates.SetNumZeroed(Cells * Cells);

    // A visible block inside a larger explored block, with unknown ground around
    // both, gives every boundary the feathers have to respect.
    constexpr int32 ExploredMin = 10, ExploredMax = 30;
    constexpr int32 VisibleMin = 16, VisibleMax = 24;
    for (int32 Y = ExploredMin; Y <= ExploredMax; ++Y)
        for (int32 X = ExploredMin; X <= ExploredMax; ++X) CellStates[Y * Cells + X] = 1;
    for (int32 Y = VisibleMin; Y <= VisibleMax; ++Y)
        for (int32 X = VisibleMin; X <= VisibleMax; ++X) CellStates[Y * Cells + X] = 2;

    bool bUnknownStayedUnknown = true;
    bool bUnknownStayedOpaque = true;
    bool bHiddenStayedOpaque = true;
    bool bGuardTexelsOpaque = true;
    bool bAnyClear = false;
    bool bAnyPartialExplored = false;

    for (int32 Y = 0; Y < TextureSize; ++Y) for (int32 X = 0; X < TextureSize; ++X)
    {
        const FTexel Texel = SampleTexel(CellStates, X, Y);
        const int32 CX = X / PixelsPerCell, CY = Y / PixelsPerCell;
        const uint8 State = CellStates[CY * Cells + CX];

        if (State == 0)
        {
            // The core privacy rule: a texel in an unknown cell may never leak
            // any explored or cleared value, however wide a neighbour's ramp is.
            if (Texel.Explored != 0) bUnknownStayedUnknown = false;
            if (Texel.Opacity != 1) bUnknownStayedOpaque = false;
        }
        if (State != 2 && Texel.Opacity != 1) bHiddenStayedOpaque = false;
        if (Texel.Opacity < 1) bAnyClear = true;
        if (Texel.Explored > 0 && Texel.Explored < 1) bAnyPartialExplored = true;

        // A visible texel within the guard band of a hidden cell must stay fully
        // opaque, so bilinear filtering cannot sample cleared ground across it.
        if (State == 2)
        {
            bool bTouchesHidden = false;
            for (int32 NY = CY - 1; NY <= CY + 1 && !bTouchesHidden; ++NY)
                for (int32 NX = CX - 1; NX <= CX + 1; ++NX)
                    if (NX < 0 || NY < 0 || NX >= Cells || NY >= Cells
                        || CellStates[NY * Cells + NX] != 2) { bTouchesHidden = true; break; }
            if (bTouchesHidden)
            {
                const float NearestEdge = FMath::Min(
                    FMath::Min(X - VisibleMin * PixelsPerCell, (VisibleMax + 1) * PixelsPerCell - 1 - X),
                    FMath::Min(Y - VisibleMin * PixelsPerCell, (VisibleMax + 1) * PixelsPerCell - 1 - Y));
                if (NearestEdge < VisibleGuardTexels && Texel.Opacity != 1) bGuardTexelsOpaque = false;
            }
        }
    }

    TestTrue(TEXT("An unknown cell never reports any explored value"), bUnknownStayedUnknown);
    TestTrue(TEXT("An unknown cell stays fully fogged"), bUnknownStayedOpaque);
    TestTrue(TEXT("Every cell that is not currently visible stays fully fogged"), bHiddenStayedOpaque);
    TestTrue(TEXT("Visible texels beside a hidden cell stay opaque through the guard band"), bGuardTexelsOpaque);
    TestTrue(TEXT("The visible interior actually clears, so the feather is not opaque everywhere"), bAnyClear);
    TestTrue(TEXT("The explored edge ramps rather than stepping from unknown to explored"), bAnyPartialExplored);

    // An entirely unknown map must stay completely dark and fogged.
    TArray<uint8> Blank;
    Blank.SetNumZeroed(Cells * Cells);
    const FTexel BlankTexel = SampleTexel(Blank, TextureSize / 2, TextureSize / 2);
    TestEqual(TEXT("A fully unknown map reports no explored ground"), BlankTexel.Explored, 0.0f);
    TestEqual(TEXT("A fully unknown map stays fully fogged"), BlankTexel.Opacity, 1.0f);

    // A malformed grid must fail closed rather than reading out of bounds.
    const FTexel Malformed = SampleTexel(TConstArrayView<uint8>(), 4, 4);
    TestEqual(TEXT("A malformed cell grid fails closed on explored"), Malformed.Explored, 0.0f);
    TestEqual(TEXT("A malformed cell grid fails closed on opacity"), Malformed.Opacity, 1.0f);
    return true;
}
