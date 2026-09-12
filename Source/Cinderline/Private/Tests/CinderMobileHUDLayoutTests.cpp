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
        && NearlyEqual(Doubled.Navigation.Min.X, Base.Navigation.Min.X * 2.0f)
        && NearlyEqual(Doubled.Navigation.Min.Y, Base.Navigation.Min.Y * 2.0f)
        && NearlyEqual(Doubled.Navigation.Max.X, Base.Navigation.Max.X * 2.0f)
        && NearlyEqual(Doubled.Navigation.Max.Y, Base.Navigation.Max.Y * 2.0f)
        && NearlyEqual(Doubled.Commands.Min.X, Base.Commands.Min.X * 2.0f)
        && NearlyEqual(Doubled.Commands.Min.Y, Base.Commands.Min.Y * 2.0f)
        && NearlyEqual(Doubled.Commands.Max.X, Base.Commands.Max.X * 2.0f)
        && NearlyEqual(Doubled.Commands.Max.Y, Base.Commands.Max.Y * 2.0f);
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
        TestTrue(*FString::Printf(TEXT("%s navigation row is 44 logical units high"), *Prefix),
            NearlyEqual(Layout.Navigation.GetSize().Y, 44.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s command row is 44 logical units high"), *Prefix),
            NearlyEqual(Layout.Commands.GetSize().Y, 44.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s navigation row is 192 logical units wide"), *Prefix),
            NearlyEqual(Layout.Navigation.GetSize().X, 192.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s command row is 224 logical units wide"), *Prefix),
            NearlyEqual(Layout.Commands.GetSize().X, 224.0f * Layout.Scale));
        TestTrue(*FString::Printf(TEXT("%s rows use the requested edge anchors"), *Prefix),
            NearlyEqual(Layout.Navigation.Min.X, Layout.Left)
                && NearlyEqual(Layout.Commands.Max.X, Layout.Right)
                && NearlyEqual(Layout.Navigation.Max.Y, Layout.Bottom)
                && NearlyEqual(Layout.Commands.Max.Y, Layout.Bottom));
        TestTrue(*FString::Printf(TEXT("%s keeps the two touch rows separate"), *Prefix),
            Layout.Navigation.Max.X <= Layout.Commands.Min.X);
        TestTrue(*FString::Printf(TEXT("%s leaves a central pass-through gap of 70 logical units"), *Prefix),
            Layout.Commands.Min.X - Layout.Navigation.Max.X >= 70.0f * Layout.Scale);
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
    return true;
}
