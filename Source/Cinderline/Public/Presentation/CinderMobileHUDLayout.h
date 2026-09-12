#pragma once

#include "CoreMinimal.h"

struct FCinderMobileHUDLayout
{
    float Scale = 1.0f;
    float Left = 0.0f;
    float Right = 0.0f;
    float Top = 0.0f;
    float Bottom = 0.0f;
    FBox2D Navigation;
    FBox2D Commands;

    static FCinderMobileHUDLayout Make(
        FVector2D Size,
        FVector4 Insets = FVector4(0.0f, 0.0f, 0.0f, 0.0f))
    {
        FCinderMobileHUDLayout Layout;
        const float Width = static_cast<float>(Size.X);
        const float Height = static_cast<float>(Size.Y);
        const float SafeLeft = static_cast<float>(Insets.X);
        const float SafeTop = static_cast<float>(Insets.Y);
        const float SafeRight = static_cast<float>(Insets.Z);
        const float SafeBottom = static_cast<float>(Insets.W);
        const float Aspect = Width / Height;
        Layout.Scale = Aspect < 1.65f
            ? FMath::Min(Width / 1024.0f, Height / 768.0f)
            : FMath::Min(Width / 667.0f, Height / 375.0f);

        Layout.Left = FMath::Max(16.0f * Layout.Scale, SafeLeft + 12.0f * Layout.Scale);
        Layout.Right = Width - FMath::Max(16.0f * Layout.Scale, SafeRight + 12.0f * Layout.Scale);
        Layout.Top = FMath::Max(10.0f * Layout.Scale, SafeTop + 6.0f * Layout.Scale);
        Layout.Bottom = Height - FMath::Max(12.0f * Layout.Scale, SafeBottom + 8.0f * Layout.Scale);

        const float RowHeight = 44.0f * Layout.Scale;
        Layout.Navigation = FBox2D(
            FVector2D(Layout.Left, Layout.Bottom - RowHeight),
            FVector2D(Layout.Left + 192.0f * Layout.Scale, Layout.Bottom));
        Layout.Commands = FBox2D(
            FVector2D(Layout.Right - 224.0f * Layout.Scale, Layout.Bottom - RowHeight),
            FVector2D(Layout.Right, Layout.Bottom));
        return Layout;
    }
};
