#pragma once

#include "CoreMinimal.h"

struct FCinderMobileHUDLayout
{
    static constexpr float GlobalActionGap = 4.0f;
    // Four icon-only tools form a compact 2x2 grid with full-size touch faces.
    static constexpr float GlobalBuildWidth = 44.0f;
    static constexpr float GlobalTrainWidth = 44.0f;
    static constexpr float GlobalResearchWidth = 44.0f;
    static constexpr float GlobalArmyWidth = 44.0f;
    static constexpr float GlobalActionHeight = 44.0f;
    static constexpr float GlobalActionGridWidth = GlobalBuildWidth + GlobalTrainWidth + GlobalActionGap;
    static constexpr float GlobalActionGridHeight = 2.0f * GlobalActionHeight + GlobalActionGap;
    static constexpr float NavigationMenuWidth = 44.0f;
    static constexpr float NavigationDeselectWidth = 44.0f;
    static constexpr float NavigationSelectWidth = 44.0f;
    static constexpr float NavigationHomeWidth = 44.0f;
    static constexpr float NavigationZoomWidth = 84.0f;
    static constexpr float NavigationGap = 4.0f;
    static constexpr float NavigationHeight = 44.0f;
    static constexpr float NavigationWidth = NavigationDeselectWidth + NavigationSelectWidth
        + NavigationHomeWidth + NavigationMenuWidth + 3.0f * NavigationGap;
    static constexpr float CommandWidth = 232.0f;
    static constexpr float DrawerLeftOffset = 104.0f;
    static constexpr float DrawerNarrowWidth = 300.0f;
    static constexpr float DrawerWideWidth = 384.0f;

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
    FBox2D VoiceButton;
    FBox2D VoiceControls;
    FBox2D VoiceStatus;
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
        // Every reference pair below is a real device measured in landscape
        // logical points: a 4:3 iPad (1024x768), a 16:9 phone (667x375, the
        // iPhone 8 and the SE) and a 19.5:9 phone (844x390, every iPhone from
        // the 12 on). A single 667x375 pair used to cover everything at or
        // above 1.65, so on a 19.5:9 phone the height term always won against
        // a 2017 reference: a 16 Pro Max drew each 44-unit target at 51.6
        // physical points instead of 49.6 and spent that 4% on chrome rather
        // than battlefield. The 1.90 split sits clear of both populations -
        // a 16:9 phone is 1.78 and a 19.5:9 phone is 2.16 - so the older
        // devices keep exactly the density they ship with today.
        constexpr float ModernPhoneAspect = 1.90f;
        Layout.Scale = bDesktop
            ? FMath::Max(0.45f, FMath::Min(Width / 1280.0f, Height / 720.0f))
            : Aspect < 1.65f
                ? FMath::Min(Width / 1024.0f, Height / 768.0f)
                : Aspect < ModernPhoneAspect
                    ? FMath::Min(Width / 667.0f, Height / 375.0f)
                    : FMath::Min(Width / 844.0f, Height / 390.0f);

        Layout.Left = FMath::Max(16.0f * Layout.Scale, SafeLeft + 12.0f * Layout.Scale);
        Layout.Right = Width - FMath::Max(16.0f * Layout.Scale, SafeRight + 12.0f * Layout.Scale);
        Layout.Top = FMath::Max(10.0f * Layout.Scale, SafeTop + 6.0f * Layout.Scale);
        Layout.Bottom = Height - FMath::Max(12.0f * Layout.Scale, SafeBottom + 8.0f * Layout.Scale);

        const float RowHeight = 44.0f * Layout.Scale;
        Layout.Commands = FBox2D(
            FVector2D(Layout.Right - CommandWidth * Layout.Scale, Layout.Bottom - RowHeight),
            FVector2D(Layout.Right, Layout.Bottom));
        Layout.GlobalActions = FBox2D(
            FVector2D(Layout.Left, Layout.Top + 46.0f * Layout.Scale),
            FVector2D(Layout.Left + GlobalActionGridWidth * Layout.Scale,
                Layout.Top + (46.0f + GlobalActionGridHeight) * Layout.Scale));
        Layout.Navigation = FBox2D(
            FVector2D(Layout.Right - NavigationWidth * Layout.Scale, Layout.Top),
            FVector2D(Layout.Right, Layout.Top + (bDesktop ? 92.0f : 44.0f) * Layout.Scale));
        Layout.Portraits = FBox2D(
            FVector2D(Layout.Left + 96.0f * Layout.Scale, Layout.Bottom - RowHeight),
            FVector2D(Layout.Commands.Min.X - 10.0f * Layout.Scale, Layout.Bottom));
        Layout.Identity = FBox2D(
            FVector2D(Layout.Right - 224.0f * Layout.Scale, Layout.Bottom - 94.0f * Layout.Scale),
            FVector2D(Layout.Right, Layout.Bottom - 50.0f * Layout.Scale));
        // Voice stays above the existing identity/Workers dock. Its expanded
        // controls share the navigation rail's width, clearing open drawers.
        Layout.VoiceControls = FBox2D(
            FVector2D(Layout.Right - NavigationWidth * Layout.Scale, Layout.Bottom - 144.0f * Layout.Scale),
            FVector2D(Layout.Right, Layout.Bottom - 100.0f * Layout.Scale));
        Layout.VoiceButton = FBox2D(
            FVector2D(Layout.Right - 88.0f * Layout.Scale, Layout.VoiceControls.Min.Y),
            Layout.VoiceControls.Max);
        Layout.VoiceStatus = FBox2D(
            FVector2D(Layout.VoiceControls.Min.X, Layout.Bottom - 200.0f * Layout.Scale),
            FVector2D(Layout.Right, Layout.Bottom - 148.0f * Layout.Scale));
        const float LogicalWidth = (Layout.Right - Layout.Left) / Layout.Scale;
        // The sheet sits 12 units beside the icon grid and leaves room for the
        // navigation row on either a narrow or wide phone.
        const bool bWideDrawer = LogicalWidth >= DrawerLeftOffset + DrawerWideWidth
            + NavigationWidth + NavigationGap;
        const float DrawerWidth = (bWideDrawer ? DrawerWideWidth : DrawerNarrowWidth) * Layout.Scale;
        const float DrawerTop = Layout.Top + 46.0f * Layout.Scale;
        Layout.Drawer = FBox2D(
            FVector2D(Layout.Left + DrawerLeftOffset * Layout.Scale, DrawerTop),
            FVector2D(Layout.Left + DrawerLeftOffset * Layout.Scale + DrawerWidth,
                DrawerTop + 190.0f * Layout.Scale));
        const float MinimapSize = 84.0f * Layout.Scale;
        const FVector2D MinimapOrigin(Layout.Left, Layout.Bottom - MinimapSize);
        Layout.Minimap = FBox2D(MinimapOrigin, MinimapOrigin + FVector2D(MinimapSize));
        return Layout;
    }
};
