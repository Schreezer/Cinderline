#include "Presentation/CinderHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "GlobalRenderResources.h"

void ACinderHUD::Surface(float X, float Y, float W, float H, FLinearColor Color, float Radius)
{
    if (!Canvas || W <= 0 || H <= 0 || Color.A <= 0) return;

    constexpr int32 SegmentsPerCorner = 4;
    constexpr int32 CornerCount = 4;
    constexpr int32 PerimeterCount = SegmentsPerCorner * CornerCount + CornerCount;
    // Four samples per quarter arc, plus both endpoints. These fixed unit
    // offsets keep the rounded geometry identical without doing 20 sin/cos
    // pairs for every surface on every HUD frame.
    static const FVector2D UnitPerimeter[PerimeterCount] =
    {
        {0, -1}, {0.38268343, -0.92387953}, {0.70710678, -0.70710678}, {0.92387953, -0.38268343}, {1, 0},
        {1, 0}, {0.92387953, 0.38268343}, {0.70710678, 0.70710678}, {0.38268343, 0.92387953}, {0, 1},
        {0, 1}, {-0.38268343, 0.92387953}, {-0.70710678, 0.70710678}, {-0.92387953, 0.38268343}, {-1, 0},
        {-1, 0}, {-0.92387953, -0.38268343}, {-0.70710678, -0.70710678}, {-0.38268343, -0.92387953}, {0, -1}
    };
    const float R = FMath::Clamp(Radius, 0.5f, FMath::Min(W, H) * 0.5f);
    const FVector2D Centers[CornerCount] =
    {
        {X + W - R, Y + R},
        {X + W - R, Y + H - R},
        {X + R, Y + H - R},
        {X + R, Y + R}
    };
    FVector2D Perimeter[PerimeterCount];
    int32 PointIndex = 0;
    for (int32 Corner = 0; Corner < CornerCount; ++Corner)
    {
        for (int32 Segment = 0; Segment <= SegmentsPerCorner; ++Segment)
        {
            Perimeter[PointIndex] = Centers[Corner] + UnitPerimeter[PointIndex] * R;
            ++PointIndex;
        }
    }

    const FVector2D Center(X + W * 0.5f, Y + H * 0.5f);
    const auto MakeTriangle = [&Color, &Center](const FVector2D& A, const FVector2D& B)
    {
        FCanvasUVTri Triangle;
        Triangle.V0_Pos = Center;
        Triangle.V1_Pos = A;
        Triangle.V2_Pos = B;
        Triangle.V0_UV = Triangle.V1_UV = Triangle.V2_UV = FVector2D::ZeroVector;
        Triangle.V0_Color = Triangle.V1_Color = Triangle.V2_Color = Color;
        return Triangle;
    };

    FCanvasTriangleItem Item(MakeTriangle(Perimeter[0], Perimeter[1]), GWhiteTexture);
    Item.TriangleList.Reserve(PerimeterCount);
    for (int32 Index = 1; Index < PerimeterCount; ++Index)
        Item.TriangleList.Add(MakeTriangle(Perimeter[Index], Perimeter[(Index + 1) % PerimeterCount]));
    Item.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Item);
}

bool ACinderHUD::ActionGlyph(const FString& Action, int32 Arg, float X, float Y,
    float Size, FLinearColor Color)
{
    if (Size <= 0) return false;

    const float S = UIScale;
    const float Stroke = 2 * S;
    const float L = X + Size * 0.16f;
    const float R = X + Size * 0.84f;
    const float T = Y + Size * 0.16f;
    const float B = Y + Size * 0.84f;
    const float CX = X + Size * 0.5f;
    const float CY = Y + Size * 0.5f;
    const float U = Size;
    const auto Line = [this, Color, Stroke](float AX, float AY, float BX, float BY)
    {
        DrawLine(AX, AY, BX, BY, Color, Stroke);
    };
    const auto Circle = [&Line](float CenterX, float CenterY, float Radius, int32 Segments = 12)
    {
        FVector2D Previous(CenterX + Radius, CenterY);
        for (int32 Segment = 1; Segment <= Segments; ++Segment)
        {
            const float Angle = UE_TWO_PI * static_cast<float>(Segment) / Segments;
            const FVector2D Next(CenterX + FMath::Cos(Angle) * Radius,
                CenterY + FMath::Sin(Angle) * Radius);
            Line(Previous.X, Previous.Y, Next.X, Next.Y);
            Previous = Next;
        }
    };
    const auto Polygon = [&Line](const FVector2D* Points, int32 Count)
    {
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const FVector2D& A = Points[Index];
            const FVector2D& BPoint = Points[(Index + 1) % Count];
            Line(A.X, A.Y, BPoint.X, BPoint.Y);
        }
    };
    const auto Dot = [this, Color, S](float CenterX, float CenterY, float Diameter)
    {
        const float D = FMath::Max(S, Diameter);
        Panel(CenterX - D * 0.5f, CenterY - D * 0.5f, D, D, Color);
    };
    const auto Head = [&Circle, &Line, U](float CenterX, float CenterY, float Scale)
    {
        const float Radius = U * 0.075f * Scale;
        Circle(CenterX, CenterY - U * 0.075f * Scale, Radius, 8);
        Line(CenterX - U * 0.11f * Scale, CenterY + U * 0.105f * Scale,
            CenterX - U * 0.07f * Scale, CenterY + U * 0.025f * Scale);
        Line(CenterX - U * 0.07f * Scale, CenterY + U * 0.025f * Scale,
            CenterX + U * 0.07f * Scale, CenterY + U * 0.025f * Scale);
        Line(CenterX + U * 0.07f * Scale, CenterY + U * 0.025f * Scale,
            CenterX + U * 0.11f * Scale, CenterY + U * 0.105f * Scale);
    };
    const auto People = [&Head, U, CX, CY]()
    {
        Head(CX, CY - U * 0.07f, 1.0f);
        Head(CX - U * 0.20f, CY + U * 0.07f, 0.78f);
        Head(CX + U * 0.20f, CY + U * 0.07f, 0.78f);
    };
    const auto List = [&Line, &Dot, L, R, T, U]()
    {
        for (int32 Row = 0; Row < 3; ++Row)
        {
            const float RowY = T + U * (0.10f + Row * 0.19f);
            Dot(L + U * 0.035f, RowY, U * 0.045f);
            Line(L + U * 0.12f, RowY, R, RowY);
        }
    };
    const auto Shield = [&Polygon, &Line, CX, T, B, U]()
    {
        const FVector2D Points[] =
        {
            {CX, T}, {CX + U * 0.25f, T + U * 0.10f},
            {CX + U * 0.20f, B - U * 0.16f}, {CX, B},
            {CX - U * 0.20f, B - U * 0.16f}, {CX - U * 0.25f, T + U * 0.10f}
        };
        Polygon(Points, UE_ARRAY_COUNT(Points));
        Line(CX, T + U * 0.04f, CX, B - U * 0.08f);
    };

    const bool bBuild = Action == TEXT("build") || Action == TEXT("buildmenu")
        || (Action == TEXT("globalcatalog") && Arg == 7);
    const bool bTrain = Action == TEXT("train") || Action == TEXT("globaltrain")
        || (Action == TEXT("globalcatalog") && Arg == 8);
    const bool bResearch = Action == TEXT("research") || Action == TEXT("globalresearch")
        || (Action == TEXT("globalcatalog") && Arg == 9);

    if (bBuild)
    {
        Line(L + U * 0.08f, B - U * 0.02f, R - U * 0.13f, T + U * 0.13f);
        Line(R - U * 0.31f, T, R, T + U * 0.31f);
        Line(R - U * 0.36f, T + U * 0.05f, R - U * 0.05f, T + U * 0.36f);
        return true;
    }
    if (bTrain)
    {
        People();
        Line(R - U * 0.10f, T, R - U * 0.10f, T + U * 0.20f);
        Line(R - U * 0.20f, T + U * 0.10f, R, T + U * 0.10f);
        return true;
    }
    if (bResearch)
    {
        Line(CX - U * 0.08f, T, CX + U * 0.08f, T);
        Line(CX - U * 0.05f, T, CX - U * 0.05f, CY - U * 0.08f);
        Line(CX + U * 0.05f, T, CX + U * 0.05f, CY - U * 0.08f);
        const FVector2D Flask[] =
        {
            {CX - U * 0.05f, CY - U * 0.08f}, {L + U * 0.05f, B - U * 0.04f},
            {R - U * 0.05f, B - U * 0.04f}, {CX + U * 0.05f, CY - U * 0.08f}
        };
        Polygon(Flask, UE_ARRAY_COUNT(Flask));
        Line(L + U * 0.13f, CY + U * 0.12f, R - U * 0.13f, CY + U * 0.12f);
        return true;
    }
    if (Action == TEXT("army") || Action == TEXT("crew"))
    {
        People();
        return true;
    }
    if (Action == TEXT("move"))
    {
        Line(L, CY, R, CY);
        Line(R - U * 0.19f, CY - U * 0.19f, R, CY);
        Line(R - U * 0.19f, CY + U * 0.19f, R, CY);
        return true;
    }
    if (Action == TEXT("attack"))
    {
        Circle(CX, CY, U * 0.23f);
        Circle(CX, CY, U * 0.07f, 8);
        Line(CX, T, CX, CY - U * 0.23f);
        Line(CX, CY + U * 0.23f, CX, B);
        Line(L, CY, CX - U * 0.23f, CY);
        Line(CX + U * 0.23f, CY, R, CY);
        return true;
    }
    if (Action == TEXT("defend") || Action == TEXT("hold"))
    {
        Shield();
        return true;
    }
    if (Action == TEXT("stop"))
    {
        const float Inset = U * 0.08f;
        const FVector2D Square[] =
        {
            {L + Inset, T + Inset}, {R - Inset, T + Inset},
            {R - Inset, B - Inset}, {L + Inset, B - Inset}
        };
        Polygon(Square, UE_ARRAY_COUNT(Square));
        return true;
    }
    if (Action == TEXT("deselect"))
    {
        const float Arm = U * 0.16f;
        Line(L, T + Arm, L, T); Line(L, T, L + Arm, T);
        Line(R - Arm, T, R, T); Line(R, T, R, T + Arm);
        Line(R, B - Arm, R, B); Line(R, B, R - Arm, B);
        Line(L + Arm, B, L, B); Line(L, B, L, B - Arm);
        const float Clear = U * 0.13f;
        Line(CX - Clear, CY - Clear, CX + Clear, CY + Clear);
        Line(CX + Clear, CY - Clear, CX - Clear, CY + Clear);
        return true;
    }
    if (Action == TEXT("box"))
    {
        const float Arm = U * 0.18f;
        Line(L, T + Arm, L, T); Line(L, T, L + Arm, T);
        Line(R - Arm, T, R, T); Line(R, T, R, T + Arm);
        Line(R, B - Arm, R, B); Line(R, B, R - Arm, B);
        Line(L + Arm, B, L, B); Line(L, B, L, B - Arm);
        return true;
    }
    if (Action == TEXT("home"))
    {
        Line(L, CY, CX, T); Line(CX, T, R, CY);
        Line(L + U * 0.08f, CY - U * 0.03f, L + U * 0.08f, B);
        Line(R - U * 0.08f, CY - U * 0.03f, R - U * 0.08f, B);
        Line(L + U * 0.08f, B, R - U * 0.08f, B);
        Line(CX - U * 0.07f, B, CX - U * 0.07f, CY + U * 0.12f);
        Line(CX + U * 0.07f, B, CX + U * 0.07f, CY + U * 0.12f);
        return true;
    }
    if (Action == TEXT("help") || Action == TEXT("info"))
    {
        Circle(CX, CY, U * 0.30f);
        Dot(CX, CY - U * 0.17f, U * 0.055f);
        Line(CX, CY - U * 0.04f, CX, CY + U * 0.19f);
        return true;
    }
    if (Action == TEXT("pause"))
    {
        Line(L, CY - U * 0.18f, R, CY - U * 0.18f);
        Line(L, CY, R, CY);
        Line(L, CY + U * 0.18f, R, CY + U * 0.18f);
        return true;
    }
    if (Action.Contains(TEXT("close")) || Action.Contains(TEXT("cancel")))
    {
        Line(L, T, R, B); Line(R, T, L, B);
        return true;
    }
    if (Action == TEXT("workers"))
    {
        const FVector2D HeadBox[] =
        {
            {L + U * 0.08f, T + U * 0.12f}, {R - U * 0.08f, T + U * 0.12f},
            {R - U * 0.08f, CY + U * 0.06f}, {L + U * 0.08f, CY + U * 0.06f}
        };
        Polygon(HeadBox, UE_ARRAY_COUNT(HeadBox));
        Line(CX, T + U * 0.12f, CX, T); Dot(CX, T, U * 0.05f);
        Dot(CX - U * 0.12f, CY - U * 0.04f, U * 0.05f);
        Dot(CX + U * 0.12f, CY - U * 0.04f, U * 0.05f);
        Line(L + U * 0.15f, CY + U * 0.18f, R - U * 0.15f, CY + U * 0.18f);
        Line(L + U * 0.15f, CY + U * 0.18f, L + U * 0.15f, B);
        Line(R - U * 0.15f, CY + U * 0.18f, R - U * 0.15f, B);
        return true;
    }
    if (Action == TEXT("productionrally"))
    {
        Line(L + U * 0.12f, T, L + U * 0.12f, B);
        const FVector2D Flag[] =
        {
            {L + U * 0.12f, T}, {R, T + U * 0.10f},
            {R - U * 0.12f, CY}, {L + U * 0.12f, CY - U * 0.04f}
        };
        Polygon(Flag, UE_ARRAY_COUNT(Flag));
        Line(L, B, L + U * 0.30f, B);
        return true;
    }
    if (Action == TEXT("producerjobs") || Action == TEXT("orders")
        || (Action == TEXT("sheet") && Arg == 3))
    {
        List();
        return true;
    }
    if (Action == TEXT("zoom+") || Action == TEXT("zoom-"))
    {
        Line(L, CY, R, CY);
        if (Action == TEXT("zoom+")) Line(CX, T, CX, B);
        return true;
    }
    if (Action == TEXT("ore"))
    {
        const FVector2D Crystal[] =
        {
            {CX, T}, {R, CY - U * 0.03f}, {CX + U * 0.14f, B},
            {CX - U * 0.14f, B}, {L, CY - U * 0.03f}
        };
        Polygon(Crystal, UE_ARRAY_COUNT(Crystal));
        Line(CX, T, CX - U * 0.14f, B);
        Line(CX, T, CX + U * 0.14f, B);
        return true;
    }
    if (Action == TEXT("tech"))
    {
        FVector2D Hex[6];
        for (int32 Index = 0; Index < 6; ++Index)
        {
            const float Angle = UE_TWO_PI * static_cast<float>(Index) / 6;
            Hex[Index] = FVector2D(CX + FMath::Cos(Angle) * U * 0.29f,
                CY + FMath::Sin(Angle) * U * 0.29f);
        }
        Polygon(Hex, UE_ARRAY_COUNT(Hex));
        Dot(CX, CY, U * 0.07f);
        Line(CX, CY, Hex[0].X, Hex[0].Y);
        Line(CX, CY, Hex[2].X, Hex[2].Y);
        Line(CX, CY, Hex[4].X, Hex[4].Y);
        return true;
    }
    if (Action == TEXT("clock"))
    {
        Circle(CX, CY, U * 0.30f);
        Line(CX, CY, CX, CY - U * 0.17f);
        Line(CX, CY, CX + U * 0.14f, CY + U * 0.08f);
        return true;
    }
    return false;
}
