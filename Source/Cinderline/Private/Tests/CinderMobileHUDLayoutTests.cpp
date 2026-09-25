#include "Presentation/CinderMobileHUDLayout.h"
#include "Misc/AutomationTest.h"

namespace
{
struct FMobileHUDCase
{
    const TCHAR* Name;
    FVector2D Size;
    FVector4 Insets;
    bool bWideDrawer;
};

bool NearlyEqual(double A, double B)
{
    return FMath::IsNearlyEqual(A, B, 0.001);
}

bool IsDoubled(const FCinderMobileHUDLayout& Base, const FCinderMobileHUDLayout& Doubled)
{
    return NearlyEqual(Doubled.Scale, Base.Scale * 2.0f)
        && NearlyEqual(Doubled.Left, Base.Left * 2.0f)
        && NearlyEqual(Doubled.Right, Base.Right * 2.0f)
        && NearlyEqual(Doubled.Top, Base.Top * 2.0f)
        && NearlyEqual(Doubled.Bottom, Base.Bottom * 2.0f)
        && NearlyEqual(Doubled.GlobalActions.Min.X, Base.GlobalActions.Min.X * 2.0f)
        && NearlyEqual(Doubled.GlobalActions.Min.Y, Base.GlobalActions.Min.Y * 2.0f)
        && NearlyEqual(Doubled.GlobalActions.Max.X, Base.GlobalActions.Max.X * 2.0f)
        && NearlyEqual(Doubled.GlobalActions.Max.Y, Base.GlobalActions.Max.Y * 2.0f)
        && NearlyEqual(Doubled.Navigation.Min.X, Base.Navigation.Min.X * 2.0f)
        && NearlyEqual(Doubled.Navigation.Min.Y, Base.Navigation.Min.Y * 2.0f)
        && NearlyEqual(Doubled.Navigation.Max.X, Base.Navigation.Max.X * 2.0f)
        && NearlyEqual(Doubled.Navigation.Max.Y, Base.Navigation.Max.Y * 2.0f)
        && NearlyEqual(Doubled.Commands.Min.X, Base.Commands.Min.X * 2.0f)
        && NearlyEqual(Doubled.Commands.Min.Y, Base.Commands.Min.Y * 2.0f)
        && NearlyEqual(Doubled.Commands.Max.X, Base.Commands.Max.X * 2.0f)
        && NearlyEqual(Doubled.Commands.Max.Y, Base.Commands.Max.Y * 2.0f)
        && NearlyEqual(Doubled.Portraits.Min.X, Base.Portraits.Min.X * 2.0f)
        && NearlyEqual(Doubled.Portraits.Min.Y, Base.Portraits.Min.Y * 2.0f)
        && NearlyEqual(Doubled.Portraits.Max.X, Base.Portraits.Max.X * 2.0f)
        && NearlyEqual(Doubled.Portraits.Max.Y, Base.Portraits.Max.Y * 2.0f)
        && NearlyEqual(Doubled.Identity.Min.X, Base.Identity.Min.X * 2.0f)
        && NearlyEqual(Doubled.Identity.Min.Y, Base.Identity.Min.Y * 2.0f)
        && NearlyEqual(Doubled.Identity.Max.X, Base.Identity.Max.X * 2.0f)
        && NearlyEqual(Doubled.Identity.Max.Y, Base.Identity.Max.Y * 2.0f)
        && NearlyEqual(Doubled.VoiceButton.Min.X, Base.VoiceButton.Min.X * 2.0f)
        && NearlyEqual(Doubled.VoiceButton.Max.Y, Base.VoiceButton.Max.Y * 2.0f)
        && NearlyEqual(Doubled.VoiceControls.Min.Y, Base.VoiceControls.Min.Y * 2.0f)
        && NearlyEqual(Doubled.VoiceStatus.Min.X, Base.VoiceStatus.Min.X * 2.0f)
        && NearlyEqual(Doubled.VoiceStatus.Max.Y, Base.VoiceStatus.Max.Y * 2.0f)
        && NearlyEqual(Doubled.Drawer.Min.X, Base.Drawer.Min.X * 2.0f)
        && NearlyEqual(Doubled.Drawer.Min.Y, Base.Drawer.Min.Y * 2.0f)
        && NearlyEqual(Doubled.Drawer.Max.X, Base.Drawer.Max.X * 2.0f)
        && NearlyEqual(Doubled.Drawer.Max.Y, Base.Drawer.Max.Y * 2.0f)
        && NearlyEqual(Doubled.Minimap.Min.X, Base.Minimap.Min.X * 2.0f)
        && NearlyEqual(Doubled.Minimap.Min.Y, Base.Minimap.Min.Y * 2.0f)
        && NearlyEqual(Doubled.Minimap.Max.X, Base.Minimap.Max.X * 2.0f)
        && NearlyEqual(Doubled.Minimap.Max.Y, Base.Minimap.Max.Y * 2.0f);
}

bool Separated(const FBox2D& A, const FBox2D& B)
{
    constexpr double Tolerance = 0.0001;
    const double OverlapX = FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X);
    const double OverlapY = FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y);
    return OverlapX <= Tolerance || OverlapY <= Tolerance;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderMobileHUDLayoutTest,
    "Cinderline.UI.MobileHUDLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderMobileHUDLayoutTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const FMobileHUDCase Cases[] = {
        {TEXT("iPhone 667x375"), FVector2D(667.0f, 375.0f), FVector4(0.0f, 0.0f, 0.0f, 0.0f), false},
        {TEXT("iPhone 844x390"), FVector2D(844.0f, 390.0f), FVector4(47.0f, 0.0f, 47.0f, 21.0f), true},
        {TEXT("iPhone 956x440"), FVector2D(956.0f, 440.0f), FVector4(62.0f, 0.0f, 62.0f, 34.0f), true},
        {TEXT("Landscape island left"), FVector2D(956.0f, 440.0f), FVector4(62.0f, 0.0f, 21.0f, 21.0f), true},
        {TEXT("Landscape island right"), FVector2D(956.0f, 440.0f), FVector4(21.0f, 0.0f, 62.0f, 21.0f), true},
        {TEXT("AEON native pixels island left"), FVector2D(2868.0f, 1320.0f), FVector4(186.0f, 0.0f, 63.0f, 63.0f), true},
        {TEXT("AEON native pixels island right"), FVector2D(2868.0f, 1320.0f), FVector4(63.0f, 0.0f, 186.0f, 63.0f), true},
        {TEXT("iPad 1024x768"), FVector2D(1024.0f, 768.0f), FVector4(0.0f, 0.0f, 0.0f, 20.0f), true},
    };

    for (const FMobileHUDCase& Case : Cases)
    {
        const FCinderMobileHUDLayout Layout = FCinderMobileHUDLayout::Make(Case.Size, Case.Insets);
        const FString Prefix(Case.Name);
        const float Width = static_cast<float>(Case.Size.X);
        const float Height = static_cast<float>(Case.Size.Y);
        const float SafeLeft = static_cast<float>(Case.Insets.X);
        const float SafeTop = static_cast<float>(Case.Insets.Y);
        const float SafeRight = static_cast<float>(Case.Insets.Z);
        const float SafeBottom = static_cast<float>(Case.Insets.W);
        // Mirrors the three device references the layout now selects between:
        // 4:3 tablet, 16:9 phone and 19.5:9 phone. The fixtures below cover
        // one aspect on each side of both thresholds.
        const float Aspect = Width / Height;
        const float ExpectedScale = Aspect < 1.65f
            ? FMath::Min(Width / 1024.0f, Height / 768.0f)
            : Aspect < 1.90f
                ? FMath::Min(Width / 667.0f, Height / 375.0f)
                : FMath::Min(Width / 844.0f, Height / 390.0f);

        TestTrue(*FString::Printf(TEXT("%s selects the expected logical scale"), *Prefix),
            NearlyEqual(Layout.Scale, ExpectedScale));
        // A 19.5:9 phone must be measured against the 844x390 reference, not
        // the 2017 16:9 pair that used to inflate every control on it.
        if (Aspect >= 1.90f)
            TestTrue(*FString::Printf(TEXT("%s uses the 844 by 390 modern phone reference"), *Prefix),
                NearlyEqual(Layout.Scale, FMath::Min(Width / 844.0f, Height / 390.0f))
                    && Layout.Scale < FMath::Min(Width / 667.0f, Height / 375.0f));
        TestTrue(*FString::Printf(TEXT("%s global tools fit a 92-unit icon grid"), *Prefix),
            NearlyEqual(Layout.GlobalActions.GetSize().X, 92.0f * Layout.Scale)
                && NearlyEqual(Layout.GlobalActions.GetSize().Y, 92.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s navigation fits one 188 by 44 row"), *Prefix),
            NearlyEqual(Layout.Navigation.GetSize().X, 188.0f * Layout.Scale)
                && NearlyEqual(Layout.Navigation.GetSize().Y, 44.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s command row is 232 by 44 logical units"), *Prefix),
            NearlyEqual(Layout.Commands.GetSize().X, 232.0f * Layout.Scale)
                && NearlyEqual(Layout.Commands.GetSize().Y, 44.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s portrait ribbon is 44 logical units high"), *Prefix),
            NearlyEqual(Layout.Portraits.GetSize().Y, 44.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s selection identity is 44 logical units high"), *Prefix),
            NearlyEqual(Layout.Identity.GetSize().Y, 44.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s minimap is a slim 84-unit square"), *Prefix),
            NearlyEqual(Layout.Minimap.GetSize().X, 84.0f * Layout.Scale)
                && NearlyEqual(Layout.Minimap.GetSize().Y, 84.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s regions use the requested edge anchors"), *Prefix),
            NearlyEqual(Layout.GlobalActions.Min.X, Layout.Left)
                && NearlyEqual(Layout.GlobalActions.Min.Y, Layout.Top + 46.0f * Layout.Scale)
                && NearlyEqual(Layout.Navigation.Max.X, Layout.Right)
                && NearlyEqual(Layout.Navigation.Min.Y, Layout.Top)
                && NearlyEqual(Layout.Commands.Max.X, Layout.Right)
                && NearlyEqual(Layout.Commands.Max.Y, Layout.Bottom));
        TestTrue(*FString::Printf(TEXT("%s minimap uses the bottom-left safe-area anchor"), *Prefix),
            NearlyEqual(Layout.Minimap.Min.X, Layout.Left)
                && NearlyEqual(Layout.Minimap.Max.Y, Layout.Bottom));
        TestTrue(*FString::Printf(TEXT("%s persistent global tools retain 44-unit targets"), *Prefix),
            FCinderMobileHUDLayout::GlobalBuildWidth >= 44.0f
                && FCinderMobileHUDLayout::GlobalTrainWidth >= 44.0f
                && FCinderMobileHUDLayout::GlobalResearchWidth >= 44.0f
                && FCinderMobileHUDLayout::GlobalArmyWidth >= 44.0f
                && FCinderMobileHUDLayout::GlobalActionHeight >= 44.0f);
        TestTrue(*FString::Printf(TEXT("%s four global icons use equal 44-unit touch faces"), *Prefix),
            NearlyEqual(FCinderMobileHUDLayout::GlobalBuildWidth, 44.0f)
                && NearlyEqual(FCinderMobileHUDLayout::GlobalTrainWidth, 44.0f)
                && NearlyEqual(FCinderMobileHUDLayout::GlobalResearchWidth, 44.0f)
                && NearlyEqual(FCinderMobileHUDLayout::GlobalArmyWidth, 44.0f)
                && NearlyEqual(FCinderMobileHUDLayout::GlobalActionGridWidth, 92.0f)
                && NearlyEqual(FCinderMobileHUDLayout::GlobalActionGridHeight, 92.0f));
        const float ToolGap = FCinderMobileHUDLayout::GlobalActionGap * Layout.Scale;
        const float ToolSize = FCinderMobileHUDLayout::GlobalActionHeight * Layout.Scale;
        const FBox2D BuildTarget(Layout.GlobalActions.Min,
            Layout.GlobalActions.Min + FVector2D(ToolSize, ToolSize));
        const FBox2D TrainTarget(BuildTarget.Min + FVector2D(ToolSize + ToolGap, 0.0f),
            BuildTarget.Max + FVector2D(ToolSize + ToolGap, 0.0f));
        const FBox2D ResearchTarget(BuildTarget.Min + FVector2D(0.0f, ToolSize + ToolGap),
            BuildTarget.Max + FVector2D(0.0f, ToolSize + ToolGap));
        const FBox2D ArmyTarget(TrainTarget.Min + FVector2D(0.0f, ToolSize + ToolGap),
            TrainTarget.Max + FVector2D(0.0f, ToolSize + ToolGap));
        TestTrue(*FString::Printf(TEXT("%s global grid has four separated touch targets"), *Prefix),
            NearlyEqual(BuildTarget.GetSize().X, ToolSize)
                && NearlyEqual(ArmyTarget.GetSize().Y, ToolSize)
                && NearlyEqual(ArmyTarget.Max.X, Layout.GlobalActions.Max.X)
                && NearlyEqual(ArmyTarget.Max.Y, Layout.GlobalActions.Max.Y)
                && Separated(BuildTarget, TrainTarget)
                && Separated(BuildTarget, ResearchTarget)
                && Separated(TrainTarget, ArmyTarget)
                && Separated(ResearchTarget, ArmyTarget));
        const float NavGap = FCinderMobileHUDLayout::NavigationGap * Layout.Scale;
        const FBox2D DeselectTarget(Layout.Navigation.Min,
            Layout.Navigation.Min + FVector2D(FCinderMobileHUDLayout::NavigationDeselectWidth * Layout.Scale,
                FCinderMobileHUDLayout::NavigationHeight * Layout.Scale));
        const FBox2D SelectTarget(
            FVector2D(DeselectTarget.Max.X + NavGap, Layout.Navigation.Min.Y),
            FVector2D(DeselectTarget.Max.X + NavGap
                + FCinderMobileHUDLayout::NavigationSelectWidth * Layout.Scale,
                DeselectTarget.Max.Y));
        const FBox2D HomeTarget(
            FVector2D(SelectTarget.Max.X + NavGap, Layout.Navigation.Min.Y),
            FVector2D(SelectTarget.Max.X + NavGap
                + FCinderMobileHUDLayout::NavigationHomeWidth * Layout.Scale,
                DeselectTarget.Max.Y));
        const FBox2D MenuTarget(
            FVector2D(HomeTarget.Max.X + NavGap, Layout.Navigation.Min.Y),
            Layout.Navigation.Max);
        TestTrue(*FString::Printf(TEXT("%s one-row navigation has four equal 44-unit targets"), *Prefix),
            NearlyEqual(DeselectTarget.GetSize().X, 44.0f * Layout.Scale)
                && NearlyEqual(SelectTarget.GetSize().X, 44.0f * Layout.Scale)
                && NearlyEqual(HomeTarget.GetSize().X, 44.0f * Layout.Scale)
                && NearlyEqual(MenuTarget.GetSize().X, 44.0f * Layout.Scale)
                && NearlyEqual(DeselectTarget.GetSize().Y, 44.0f * Layout.Scale)
                && NearlyEqual(SelectTarget.GetSize().Y, 44.0f * Layout.Scale)
                && NearlyEqual(HomeTarget.GetSize().Y, 44.0f * Layout.Scale)
                && NearlyEqual(MenuTarget.GetSize().Y, 44.0f * Layout.Scale)
                && NearlyEqual(SelectTarget.Min.X - DeselectTarget.Max.X, NavGap)
                && NearlyEqual(HomeTarget.Min.X - SelectTarget.Max.X, NavGap)
                && NearlyEqual(MenuTarget.Min.X - HomeTarget.Max.X, NavGap));
        // Check right-edge positions in logical units. At native pixel scales,
        // subtraction of two pixel coordinates loses enough float precision to
        // make a 1e-4 physical-pixel comparison unreliable.
        const auto UnitsFromRight = [&](double X) { return (Layout.Right - X) / Layout.Scale; };
        TestTrue(*FString::Printf(TEXT("%s aligns the navigation row to the right safe edge"), *Prefix),
            FMath::IsNearlyEqual(UnitsFromRight(DeselectTarget.Min.X), 188.0, 0.001)
                && FMath::IsNearlyEqual(UnitsFromRight(SelectTarget.Min.X), 140.0, 0.001)
                && FMath::IsNearlyEqual(UnitsFromRight(HomeTarget.Min.X), 92.0, 0.001)
                && FMath::IsNearlyEqual(UnitsFromRight(MenuTarget.Min.X), 44.0, 0.001)
                && FMath::IsNearlyEqual(UnitsFromRight(MenuTarget.Max.X), 0.0, 0.001));
        TestTrue(*FString::Printf(TEXT("%s navigation targets remain inside the safe frame and do not overlap"), *Prefix),
            DeselectTarget.Min.X >= Layout.Left - 0.0001f
                && MenuTarget.Max.X <= Layout.Right + 0.0001f
                && Separated(DeselectTarget, SelectTarget)
                && Separated(SelectTarget, HomeTarget)
                && Separated(HomeTarget, MenuTarget));
        TestTrue(*FString::Printf(TEXT("%s remains right-aligned when Clear is hidden"), *Prefix),
            NearlyEqual(SelectTarget.Min.X,
                Layout.Right - (3.0f * 44.0f + 2.0f * 4.0f) * Layout.Scale)
                && NearlyEqual(MenuTarget.Max.X, Layout.Right));
        TestTrue(*FString::Printf(TEXT("%s four context commands retain 44-unit targets"), *Prefix),
            (Layout.Commands.GetSize().X - 3.0f * 4.0f * Layout.Scale) / 4.0f
                >= 44.0f * Layout.Scale);
        TestTrue(*FString::Printf(TEXT("%s identity is 224 by 44 and sits six units above commands"), *Prefix),
            NearlyEqual(Layout.Identity.GetSize().X, 224.0f * Layout.Scale)
                && NearlyEqual(Layout.Identity.GetSize().Y, 44.0f * Layout.Scale)
                && NearlyEqual(Layout.Commands.Min.Y - Layout.Identity.Max.Y, 6.0f * Layout.Scale)
                && NearlyEqual(Layout.Identity.Max.X, Layout.Right));
        TestTrue(*FString::Printf(TEXT("%s voice controls retain full touch faces above the selection dock"), *Prefix),
            NearlyEqual(Layout.VoiceButton.GetSize().X, 88.0f * Layout.Scale)
                && NearlyEqual(Layout.VoiceButton.GetSize().Y, 44.0f * Layout.Scale)
                && NearlyEqual(Layout.VoiceControls.GetSize().X, 188.0f * Layout.Scale)
                && NearlyEqual(Layout.Identity.Min.Y - Layout.VoiceControls.Max.Y, 6.0f * Layout.Scale)
                && NearlyEqual(Layout.VoiceControls.Min.Y - Layout.VoiceStatus.Max.Y, 4.0f * Layout.Scale)
                && NearlyEqual(Layout.VoiceControls.Max.X, Layout.Right));
        TestTrue(*FString::Printf(TEXT("%s portrait ribbon fits six comfortable targets"), *Prefix),
            Layout.Portraits.GetSize().X >= (6.0f * 44.0f + 5.0f * 3.0f) * Layout.Scale
                && NearlyEqual(Layout.Portraits.GetSize().Y, 44.0f * Layout.Scale));
        const float ExpectedDrawerWidth = Case.bWideDrawer ? 384.0f : 300.0f;
        TestTrue(*FString::Printf(TEXT("%s sheet sits beside the icon grid with its device-width variant"), *Prefix),
            NearlyEqual(Layout.Drawer.Min.X, Layout.Left + 104.0f * Layout.Scale)
                && NearlyEqual(Layout.Drawer.Min.Y, Layout.Top + 46.0f * Layout.Scale)
                && NearlyEqual(Layout.Drawer.GetSize().X, ExpectedDrawerWidth * Layout.Scale)
                && NearlyEqual(Layout.Drawer.GetSize().Y, 190.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s sheet leaves a 12-unit grid gutter"), *Prefix),
            NearlyEqual(Layout.Drawer.Min.X - Layout.GlobalActions.Max.X, 12.0f * Layout.Scale));
        const float DrawerNavGap = static_cast<float>((Layout.Navigation.Min.X - Layout.Drawer.Max.X) / Layout.Scale);
        TestTrue(*FString::Printf(TEXT("%s sheet clears the right navigation cluster"), *Prefix),
            DrawerNavGap >= 36.0f - 0.001f);
        TestTrue(*FString::Printf(TEXT("%s expanded roster fits four 44-unit individual cards"), *Prefix),
            (Layout.Drawer.GetSize().X - (28.0f + 3.0f * 4.0f) * Layout.Scale) / 4.0f
                >= 44.0f * Layout.Scale);
        const float HeaderRight = Layout.Drawer.Max.X;
        const FBox2D RosterNext(
            FVector2D(HeaderRight - 290.0f * Layout.Scale, Layout.Drawer.Min.Y),
            FVector2D(HeaderRight - 246.0f * Layout.Scale, Layout.Drawer.Min.Y + 44.0f * Layout.Scale));
        const FBox2D RosterTab(
            FVector2D(HeaderRight - 242.0f * Layout.Scale, Layout.Drawer.Min.Y),
            FVector2D(HeaderRight - 182.0f * Layout.Scale, Layout.Drawer.Min.Y + 44.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s roster paging leaves four units before the roster tab"), *Prefix),
            NearlyEqual(RosterTab.Min.X - RosterNext.Max.X, 4.0f * Layout.Scale)
                && RosterNext.Max.X <= RosterTab.Min.X);
        TestTrue(*FString::Printf(TEXT("%s grid frees at least 118 units above the minimap"), *Prefix),
            Layout.Minimap.Min.Y - Layout.GlobalActions.Max.Y >= 118.0f * Layout.Scale);
        const FBox2D* Regions[] = { &Layout.GlobalActions, &Layout.Navigation, &Layout.Commands,
            &Layout.Portraits, &Layout.Identity, &Layout.Drawer, &Layout.Minimap,
            &Layout.VoiceControls, &Layout.VoiceStatus };
        const int32 RegionCount = static_cast<int32>(UE_ARRAY_COUNT(Regions));
        bool bRegionsSeparated = true;
        for (int32 I = 0; I < RegionCount; ++I)
            for (int32 J = I + 1; J < RegionCount; ++J)
                bRegionsSeparated &= Separated(*Regions[I], *Regions[J]);
        TestTrue(*FString::Printf(TEXT("%s has no overlapping base HUD regions"), *Prefix),
            bRegionsSeparated);
        TestTrue(*FString::Printf(TEXT("%s keeps explicit gaps around bottom regions"), *Prefix),
            NearlyEqual(Layout.Portraits.Min.X - Layout.Minimap.Max.X, 12.0f * Layout.Scale)
                && NearlyEqual(Layout.Commands.Min.X - Layout.Portraits.Max.X, 10.0f * Layout.Scale));
        const FBox2D OpenWorld(
            FVector2D(Layout.GlobalActions.Max.X + 8.0f * Layout.Scale,
                Layout.Navigation.Max.Y + 8.0f * Layout.Scale),
            FVector2D(Layout.Identity.Min.X - 8.0f * Layout.Scale,
                Layout.Identity.Min.Y - 8.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s reserves an open middle battlefield area"), *Prefix),
            OpenWorld.GetSize().X >= 300.0f * Layout.Scale
                && OpenWorld.GetSize().Y >= 180.0f * Layout.Scale);
        for (const FBox2D* Region : Regions)
        {
            TestTrue(*FString::Printf(TEXT("%s keeps every layout region inside the safe frame"), *Prefix),
                Region->Min.X >= Layout.Left - 0.0001f && Region->Max.X <= Layout.Right + 0.0001f
                    && Region->Min.Y >= Layout.Top - 0.0001f && Region->Max.Y <= Layout.Bottom + 0.0001f);
        }
        TestTrue(*FString::Printf(TEXT("%s keeps the left safe-area padding"), *Prefix),
            NearlyEqual(Layout.Left,
                FMath::Max(16.0f * Layout.Scale, SafeLeft + 12.0f * Layout.Scale)));
        TestTrue(*FString::Printf(TEXT("%s keeps the right safe-area padding"), *Prefix),
            NearlyEqual(Layout.Right, Width - FMath::Max(
                16.0f * Layout.Scale, SafeRight + 12.0f * Layout.Scale)));
        TestTrue(*FString::Printf(TEXT("%s keeps the top safe-area padding"), *Prefix),
            NearlyEqual(Layout.Top,
                FMath::Max(10.0f * Layout.Scale, SafeTop + 6.0f * Layout.Scale)));
        TestTrue(*FString::Printf(TEXT("%s keeps the bottom safe-area padding"), *Prefix),
            NearlyEqual(Layout.Bottom, Height - FMath::Max(
                12.0f * Layout.Scale, SafeBottom + 8.0f * Layout.Scale)));

        const FCinderMobileHUDLayout Doubled = FCinderMobileHUDLayout::Make(
            Case.Size * 2.0f,
            FVector4(
                Case.Insets.X * 2.0f,
                Case.Insets.Y * 2.0f,
                Case.Insets.Z * 2.0f,
                Case.Insets.W * 2.0f));
        TestTrue(*FString::Printf(TEXT("%s is invariant under a 2x resolution multiplier"), *Prefix),
            IsDoubled(Layout, Doubled));
    }

    const FCinderMobileHUDLayout Desktop = FCinderMobileHUDLayout::Make(
        FVector2D(1024.0f, 768.0f), FVector4(0, 0, 0, 0), true);
    TestTrue(TEXT("Desktop layout uses the 1280 by 720 reference scale"),
        NearlyEqual(Desktop.Scale, 0.8f));
    TestTrue(TEXT("Desktop navigation bounds include both selection and zoom rows"),
        NearlyEqual(Desktop.Navigation.GetSize().X, 188.0f * Desktop.Scale)
            && NearlyEqual(Desktop.Navigation.GetSize().Y, 92.0f * Desktop.Scale)
            && NearlyEqual(Desktop.Navigation.Max.Y, Desktop.Top + 92.0f * Desktop.Scale));
    TestTrue(TEXT("Desktop zoom controls have distinct 84-unit faces in the navigation bounds"),
        NearlyEqual(FCinderMobileHUDLayout::NavigationZoomWidth, 84.0f)
            && 2.0f * FCinderMobileHUDLayout::NavigationZoomWidth
                + FCinderMobileHUDLayout::NavigationGap <= FCinderMobileHUDLayout::NavigationWidth
            && Desktop.Navigation.GetSize().Y / Desktop.Scale
                >= 2.0f * FCinderMobileHUDLayout::NavigationHeight
                    + FCinderMobileHUDLayout::NavigationGap - 0.001f);
    const FCinderMobileHUDLayout TinyDesktop = FCinderMobileHUDLayout::Make(
        FVector2D(480.0f, 270.0f), FVector4(0, 0, 0, 0), true);
    TestTrue(TEXT("Desktop layout scale has a 0.45 floor"), NearlyEqual(TinyDesktop.Scale, 0.45f));
    return true;
}
