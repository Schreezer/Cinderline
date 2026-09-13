#pragma once

#include "CoreMinimal.h"

struct FCinderMobileHUDLayout
{
    static constexpr float GlobalActionGap = 4.0f;
    static constexpr float GlobalBuildWidth = 48.0f;
    static constexpr float GlobalTrainWidth = 48.0f;
    static constexpr float GlobalResearchWidth = 48.0f;
    static constexpr float GlobalArmyWidth = 48.0f;
    static constexpr float GlobalActionHeight = 44.0f;

    float Scale = 1.0f;
    float Left = 0.0f;
    float Right = 0.0f;
    float Top = 0.0f;
    float Bottom = 0.0f;
    FBox2D GlobalActions;
    FBox2D Navigation;
    FBox2D Commands;
    FBox2D Portraits;
    FBox2D Identity;
    FBox2D Drawer;
    FBox2D Minimap;

    static FCinderMobileHUDLayout Make(
        FVector2D Size,
        FVector4 Insets = FVector4(0.0f, 0.0f, 0.0f, 0.0f),
        bool bDesktop = false)
    {
        FCinderMobileHUDLayout Layout;
        const float Width = static_cast<float>(Size.X);
        const float Height = static_cast<float>(Size.Y);
        const float SafeLeft = static_cast<float>(Insets.X);
        const float SafeTop = static_cast<float>(Insets.Y);
        const float SafeRight = static_cast<float>(Insets.Z);
        const float SafeBottom = static_cast<float>(Insets.W);
        const float Aspect = Width / Height;
        Layout.Scale = bDesktop
            ? FMath::Max(0.45f, FMath::Min(Width / 1280.0f, Height / 720.0f))
            : Aspect < 1.65f
                ? FMath::Min(Width / 1024.0f, Height / 768.0f)
                : FMath::Min(Width / 667.0f, Height / 375.0f);

        Layout.Left = FMath::Max(16.0f * Layout.Scale, SafeLeft + 12.0f * Layout.Scale);
        Layout.Right = Width - FMath::Max(16.0f * Layout.Scale, SafeRight + 12.0f * Layout.Scale);
        Layout.Top = FMath::Max(10.0f * Layout.Scale, SafeTop + 6.0f * Layout.Scale);
        Layout.Bottom = Height - FMath::Max(12.0f * Layout.Scale, SafeBottom + 8.0f * Layout.Scale);

        const float RowHeight = 44.0f * Layout.Scale;
        Layout.Commands = FBox2D(
            FVector2D(Layout.Right - 208.0f * Layout.Scale, Layout.Bottom - RowHeight),
            FVector2D(Layout.Right, Layout.Bottom));
        Layout.GlobalActions = FBox2D(
            FVector2D(Layout.Left, Layout.Top + 46.0f * Layout.Scale),
            FVector2D(Layout.Left + 48.0f * Layout.Scale,
                Layout.Top + (46.0f + 4.0f * GlobalActionHeight
                    + 3.0f * GlobalActionGap) * Layout.Scale));
        Layout.Navigation = FBox2D(
            FVector2D(Layout.Right - 140.0f * Layout.Scale, Layout.Top + 46.0f * Layout.Scale),
            FVector2D(Layout.Right, Layout.Top + (bDesktop ? 138.0f : 90.0f) * Layout.Scale));
        Layout.Portraits = FBox2D(
            FVector2D(Layout.Left + 96.0f * Layout.Scale, Layout.Bottom - RowHeight),
            FVector2D(Layout.Commands.Min.X - 10.0f * Layout.Scale, Layout.Bottom));
        Layout.Identity = FBox2D(
            FVector2D(Layout.Right - 224.0f * Layout.Scale, Layout.Bottom - 94.0f * Layout.Scale),
            FVector2D(Layout.Right, Layout.Bottom - 50.0f * Layout.Scale));
        const float LogicalWidth = (Layout.Right - Layout.Left) / Layout.Scale;
        const float RemainingLogicalWidth = LogicalWidth - 56.0f;
        const float DrawerWidth = (RemainingLogicalWidth >= 640.0f ? 416.0f : 336.0f) * Layout.Scale;
        const float DrawerTop = Layout.Top + 46.0f * Layout.Scale;
        Layout.Drawer = FBox2D(
            FVector2D(Layout.Left + 56.0f * Layout.Scale, DrawerTop),
            FVector2D(Layout.Left + 56.0f * Layout.Scale + DrawerWidth,
                DrawerTop + 190.0f * Layout.Scale));
        const float MinimapSize = 84.0f * Layout.Scale;
        const FVector2D MinimapOrigin(Layout.Left, Layout.Bottom - MinimapSize);
        Layout.Minimap = FBox2D(MinimapOrigin, MinimapOrigin + FVector2D(MinimapSize));
        return Layout;
    }
};
