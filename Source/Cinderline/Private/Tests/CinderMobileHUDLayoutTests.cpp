#include "Presentation/CinderMobileHUDLayout.h"
#include "Misc/AutomationTest.h"

namespace
{
struct FMobileHUDCase
{
    const TCHAR* Name;
    FVector2D Size;
    FVector4 Insets;
};

bool NearlyEqual(double A, double B)
{
    return FMath::IsNearlyEqual(A, B, 0.0001);
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
        {TEXT("iPhone 667x375"), FVector2D(667.0f, 375.0f), FVector4(0.0f, 0.0f, 0.0f, 0.0f)},
        {TEXT("iPhone 844x390"), FVector2D(844.0f, 390.0f), FVector4(47.0f, 0.0f, 47.0f, 21.0f)},
        {TEXT("iPhone 956x440"), FVector2D(956.0f, 440.0f), FVector4(62.0f, 0.0f, 62.0f, 34.0f)},
        {TEXT("Landscape island left"), FVector2D(956.0f, 440.0f), FVector4(62.0f, 0.0f, 21.0f, 21.0f)},
        {TEXT("Landscape island right"), FVector2D(956.0f, 440.0f), FVector4(21.0f, 0.0f, 62.0f, 21.0f)},
        {TEXT("AEON native pixels island left"), FVector2D(2868.0f, 1320.0f), FVector4(186.0f, 0.0f, 63.0f, 63.0f)},
        {TEXT("AEON native pixels island right"), FVector2D(2868.0f, 1320.0f), FVector4(63.0f, 0.0f, 186.0f, 63.0f)},
        {TEXT("iPad 1024x768"), FVector2D(1024.0f, 768.0f), FVector4(0.0f, 0.0f, 0.0f, 20.0f)},
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
        const float ExpectedScale = Width / Height < 1.65f
            ? FMath::Min(Width / 1024.0f, Height / 768.0f)
            : FMath::Min(Width / 667.0f, Height / 375.0f);

        TestTrue(*FString::Printf(TEXT("%s selects the expected logical scale"), *Prefix),
            NearlyEqual(Layout.Scale, ExpectedScale));
        TestTrue(*FString::Printf(TEXT("%s global rail is 48 by 188 logical units"), *Prefix),
            NearlyEqual(Layout.GlobalActions.GetSize().X, 48.0f * Layout.Scale)
                && NearlyEqual(Layout.GlobalActions.GetSize().Y, 188.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s navigation trio is 140 by 44 logical units"), *Prefix),
            NearlyEqual(Layout.Navigation.GetSize().X, 140.0f * Layout.Scale)
                && NearlyEqual(Layout.Navigation.GetSize().Y, 44.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s command row is 208 by 44 logical units"), *Prefix),
            NearlyEqual(Layout.Commands.GetSize().X, 208.0f * Layout.Scale)
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
                && NearlyEqual(Layout.Navigation.Min.Y, Layout.Top + 46.0f * Layout.Scale)
                && NearlyEqual(Layout.Commands.Max.X, Layout.Right)
                && NearlyEqual(Layout.Commands.Max.Y, Layout.Bottom));
        TestTrue(*FString::Printf(TEXT("%s minimap uses the bottom-left safe-area anchor"), *Prefix),
            NearlyEqual(Layout.Minimap.Min.X, Layout.Left)
                && NearlyEqual(Layout.Minimap.Max.Y, Layout.Bottom));
        TestTrue(*FString::Printf(TEXT("%s persistent global tabs retain 44-unit targets"), *Prefix),
            FCinderMobileHUDLayout::GlobalBuildWidth >= 44.0f
                && FCinderMobileHUDLayout::GlobalTrainWidth >= 44.0f
                && FCinderMobileHUDLayout::GlobalResearchWidth >= 44.0f
                && FCinderMobileHUDLayout::GlobalArmyWidth >= 44.0f
                && FCinderMobileHUDLayout::GlobalActionHeight >= 44.0f);
        TestTrue(*FString::Printf(TEXT("%s global tabs use four 48-wide faces on a 188-unit vertical rail"), *Prefix),
            NearlyEqual(FCinderMobileHUDLayout::GlobalBuildWidth, 48.0f)
                && NearlyEqual(FCinderMobileHUDLayout::GlobalTrainWidth, 48.0f)
                && NearlyEqual(FCinderMobileHUDLayout::GlobalResearchWidth, 48.0f)
                && NearlyEqual(FCinderMobileHUDLayout::GlobalArmyWidth, 48.0f)
                && NearlyEqual(4.0f * FCinderMobileHUDLayout::GlobalActionHeight
                    + 3.0f * FCinderMobileHUDLayout::GlobalActionGap, 188.0f));
        const float NavTarget = 44.0f * Layout.Scale;
        const float NavGap = 4.0f * Layout.Scale;
        const FBox2D DeselectTarget(Layout.Navigation.Min,
            Layout.Navigation.Min + FVector2D(NavTarget, NavTarget));
        const FBox2D SelectTarget(
            FVector2D(Layout.Navigation.Min.X + NavTarget + NavGap, Layout.Navigation.Min.Y),
            FVector2D(Layout.Navigation.Min.X + 2.0f * NavTarget + NavGap, Layout.Navigation.Max.Y));
        const FBox2D HomeTarget(
            FVector2D(Layout.Navigation.Min.X + 2.0f * (NavTarget + NavGap), Layout.Navigation.Min.Y),
            Layout.Navigation.Max);
        TestTrue(*FString::Printf(TEXT("%s deselect, select and home retain distinct 44-unit targets"), *Prefix),
            NearlyEqual(DeselectTarget.GetSize().X, NavTarget)
                && NearlyEqual(SelectTarget.GetSize().X, NavTarget)
                && NearlyEqual(HomeTarget.GetSize().X, NavTarget)
                && NearlyEqual(SelectTarget.Min.X - DeselectTarget.Max.X, NavGap)
                && NearlyEqual(HomeTarget.Min.X - SelectTarget.Max.X, NavGap));
        TestTrue(*FString::Printf(TEXT("%s keeps existing select and home positions while adding deselect left"), *Prefix),
            NearlyEqual(DeselectTarget.Min.X, Layout.Right - 140.0f * Layout.Scale)
                && NearlyEqual(SelectTarget.Min.X, Layout.Right - 92.0f * Layout.Scale)
                && NearlyEqual(HomeTarget.Min.X, Layout.Right - 44.0f * Layout.Scale)
                && NearlyEqual(HomeTarget.Max.X, Layout.Right));
        TestTrue(*FString::Printf(TEXT("%s navigation targets remain inside the safe frame and do not overlap"), *Prefix),
            DeselectTarget.Min.X >= Layout.Left - 0.0001f
                && HomeTarget.Max.X <= Layout.Right + 0.0001f
                && Separated(DeselectTarget, SelectTarget)
                && Separated(SelectTarget, HomeTarget));
        TestTrue(*FString::Printf(TEXT("%s four context commands retain 44-unit targets"), *Prefix),
            (Layout.Commands.GetSize().X - 3.0f * 4.0f * Layout.Scale) / 4.0f
                >= 44.0f * Layout.Scale);
        TestTrue(*FString::Printf(TEXT("%s identity is 224 by 44 and sits six units above commands"), *Prefix),
            NearlyEqual(Layout.Identity.GetSize().X, 224.0f * Layout.Scale)
                && NearlyEqual(Layout.Identity.GetSize().Y, 44.0f * Layout.Scale)
                && NearlyEqual(Layout.Commands.Min.Y - Layout.Identity.Max.Y, 6.0f * Layout.Scale)
                && NearlyEqual(Layout.Identity.Max.X, Layout.Right));
        TestTrue(*FString::Printf(TEXT("%s portrait ribbon fits six comfortable targets"), *Prefix),
            Layout.Portraits.GetSize().X >= (6.0f * 44.0f + 5.0f * 3.0f) * Layout.Scale
                && NearlyEqual(Layout.Portraits.GetSize().Y, 44.0f * Layout.Scale));
        const float LogicalWidth = (Layout.Right - Layout.Left) / Layout.Scale;
        const float RemainingLogicalWidth = LogicalWidth - 56.0f;
        const float ExpectedDrawerWidth = RemainingLogicalWidth >= 640.0f ? 416.0f : 336.0f;
        TestTrue(*FString::Printf(TEXT("%s drawer uses the compact width and 190-unit default height"), *Prefix),
            NearlyEqual(Layout.Drawer.Min.X, Layout.Left + 56.0f * Layout.Scale)
                && NearlyEqual(Layout.Drawer.Min.Y, Layout.Top + 46.0f * Layout.Scale)
                && NearlyEqual(Layout.Drawer.GetSize().X, ExpectedDrawerWidth * Layout.Scale)
                && NearlyEqual(Layout.Drawer.GetSize().Y, 190.0f * Layout.Scale));
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
        TestTrue(*FString::Printf(TEXT("%s left rail clears the bottom-left minimap"), *Prefix),
            Layout.Minimap.Min.Y - Layout.GlobalActions.Max.Y >= 8.0f * Layout.Scale);
        TestTrue(*FString::Printf(TEXT("%s base HUD regions do not overlap"), *Prefix),
            Separated(Layout.GlobalActions, Layout.Navigation)
                && Separated(Layout.GlobalActions, Layout.Drawer)
                && Separated(Layout.Navigation, Layout.Drawer)
                && Separated(Layout.GlobalActions, Layout.Minimap)
                && Separated(Layout.Navigation, Layout.Minimap)
                && Separated(Layout.Minimap, Layout.Portraits)
                && Separated(Layout.Portraits, Layout.Commands)
                && Separated(Layout.Portraits, Layout.Identity)
                && Separated(Layout.Identity, Layout.Commands));
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
                && OpenWorld.GetSize().Y >= 120.0f * Layout.Scale);
        for (const FBox2D* Region : { &Layout.GlobalActions, &Layout.Navigation, &Layout.Commands,
                &Layout.Portraits, &Layout.Identity, &Layout.Drawer, &Layout.Minimap })
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
        NearlyEqual(Desktop.Navigation.GetSize().X, 140.0f * Desktop.Scale)
            && NearlyEqual(Desktop.Navigation.GetSize().Y, 92.0f * Desktop.Scale)
            && NearlyEqual(Desktop.Navigation.Max.Y, Desktop.Top + 138.0f * Desktop.Scale));
    const FCinderMobileHUDLayout TinyDesktop = FCinderMobileHUDLayout::Make(
        FVector2D(480.0f, 270.0f), FVector4(0, 0, 0, 0), true);
    TestTrue(TEXT("Desktop layout scale has a 0.45 floor"), NearlyEqual(TinyDesktop.Scale, 0.45f));
    return true;
}
