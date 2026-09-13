#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Presentation/CinderGameEngine.h"
#include "Presentation/CinderIOSPerformancePolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderIOSPerformancePolicyTest,
    "Cinderline.Presentation.IOSPerformancePolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderIOSPerformancePolicyTest::RunTest(const FString& Parameters)
{
    using FrameState = ECinderFramePacingState;
    using Pressure = ECinderThermalPressure;
    using Quality = ECinderMobileQuality;

    const auto Decide = [](FrameState State, Pressure Thermal = Pressure::Nominal,
                            bool bLowPower = false, bool bSupports15 = true, bool bSupports20 = true)
    {
        return FCinderIOSPerformancePolicy::Evaluate(State, Thermal, bLowPower, bSupports15, bSupports20);
    };

    TestEqual(TEXT("Sustainable gameplay defaults to 30 FPS"), Decide(FrameState::Gameplay).FramePace, 30);
    TestEqual(TEXT("Main menu uses 15 FPS when the display pacer supports it"), Decide(FrameState::Menu).FramePace, 15);
    TestEqual(TEXT("Pause uses 15 FPS while retaining the visible world"), Decide(FrameState::Paused).FramePace, 15);
    TestEqual(TEXT("Results use the same idle cadence"), Decide(FrameState::Results).FramePace, 15);
    TestEqual(TEXT("Idle cadence falls back to 30 FPS when 15 is unsupported"),
        Decide(FrameState::Menu, Pressure::Nominal, false, false).FramePace, 30);

    const FCinderIOSPerformanceDecision LowPower = Decide(FrameState::Gameplay, Pressure::Fair, true);
    TestEqual(TEXT("Low Power Mode lowers active gameplay to 20 FPS"), LowPower.FramePace, 20);
    TestTrue(TEXT("Low Power Mode reduces GPU quality"), LowPower.Quality == Quality::Reduced);
    const FCinderIOSPerformanceDecision Fair = Decide(FrameState::Gameplay, Pressure::Fair);
    TestEqual(TEXT("Fair thermal state retains the sustainable 30 FPS cadence"), Fair.FramePace, 30);
    TestTrue(TEXT("Fair thermal state reduces GPU quality"), Fair.Quality == Quality::Reduced);
    const FCinderIOSPerformanceDecision Serious = Decide(FrameState::Gameplay, Pressure::Serious);
    TestEqual(TEXT("Serious thermal state lowers gameplay to 20 FPS"), Serious.FramePace, 20);
    TestTrue(TEXT("Serious thermal state selects minimum GPU quality"), Serious.Quality == Quality::Minimum);
    const FCinderIOSPerformanceDecision Critical = Decide(FrameState::Gameplay, Pressure::Critical);
    TestEqual(TEXT("Critical thermal state uses a supported 15 FPS display pace"), Critical.FramePace, 15);
    TestEqual(TEXT("Critical state falls back to supported 20 FPS"),
        Decide(FrameState::Gameplay, Pressure::Critical, false, false, true).FramePace, 20);

    TestEqual(TEXT("A lower profile cap remains authoritative"),
        FCinderIOSPerformancePolicy::ApplyCap(12.0f, 30), 12.0f);
    TestEqual(TEXT("An unlimited engine adopts the policy cap"),
        FCinderIOSPerformancePolicy::ApplyCap(0.0f, 30), 30.0f);
    TestEqual(TEXT("Background suspension keeps the engine's existing timing policy"),
        FCinderIOSPerformancePolicy::ApplyCap(7.0f, Decide(FrameState::Background).FramePace), 7.0f);
    const FCinderIOSPerformanceDecision BackgroundSerious = Decide(FrameState::Background, Pressure::Serious);
    TestEqual(TEXT("Background releases frame pacing to the engine suspension path"), BackgroundSerious.FramePace, 0);
    TestTrue(TEXT("Background keeps serious thermal GPU reductions active"),
        BackgroundSerious.Quality == Quality::Minimum);
    const FCinderIOSPerformanceDecision ForegroundSerious = Decide(FrameState::Gameplay, Pressure::Serious);
    TestTrue(TEXT("Foreground resumes with the same latched thermal quality"),
        ForegroundSerious.Quality == BackgroundSerious.Quality);
    TestEqual(TEXT("Foreground resumes serious gameplay pacing"), ForegroundSerious.FramePace, 20);
    TestTrue(TEXT("Bypass releases pacing without clearing Low Power GPU reductions"),
        Decide(FrameState::Bypass, Pressure::Fair, true).Quality == Quality::Reduced);
    TestEqual(TEXT("Automatic zero screen percentage resolves to 85 while reduced"),
        FCinderIOSPerformancePolicy::ResolveScreenPercentage(0.0f, Quality::Reduced), 85.0f);
    TestEqual(TEXT("Negative automatic screen percentage resolves to 70 at minimum"),
        FCinderIOSPerformancePolicy::ResolveScreenPercentage(-1.0f, Quality::Minimum), 70.0f);
    TestEqual(TEXT("Explicit 100 percent baseline reduces to 85"),
        FCinderIOSPerformancePolicy::ResolveScreenPercentage(100.0f, Quality::Reduced), 85.0f);
    TestEqual(TEXT("An existing lower positive percentage is never raised"),
        FCinderIOSPerformancePolicy::ResolveScreenPercentage(60.0f, Quality::Minimum), 60.0f);
    TestEqual(TEXT("Full quality restores the original automatic zero sentinel"),
        FCinderIOSPerformancePolicy::ResolveScreenPercentage(0.0f, Quality::Full), 0.0f);

    FCinderIOSPerformancePolicy Stateful;
    Stateful.Reset(Pressure::Nominal, false, 0.0);
    Stateful.Update(Pressure::Serious, false, 10.0);
    TestTrue(TEXT("Serious pressure applies immediately"), Stateful.EffectivePressure() == Pressure::Serious);
    Stateful.Update(Pressure::Nominal, false, 20.0);
    Stateful.Update(Pressure::Nominal, false, 79.9);
    TestTrue(TEXT("Recovery remains degraded before 60 nominal seconds"), Stateful.EffectivePressure() == Pressure::Serious);
    Stateful.Update(Pressure::Nominal, false, 80.0);
    TestTrue(TEXT("Recovery completes after 60 continuous nominal seconds"), Stateful.EffectivePressure() == Pressure::Nominal);
    Stateful.Update(Pressure::Nominal, true, 90.0);
    TestTrue(TEXT("Low Power Mode immediately latches reduced pressure"), Stateful.EffectivePressure() == Pressure::Fair);
    Stateful.Update(Pressure::Nominal, false, 100.0);
    TestTrue(TEXT("Low Power cadence remains latched during nominal cooldown"),
        FCinderIOSPerformancePolicy::Evaluate(FrameState::Gameplay, Stateful.EffectivePressure(),
            Stateful.IsLowPowerPolicyActive(), true, true).FramePace == 20);
    Stateful.Update(Pressure::Fair, false, 130.0);
    Stateful.Update(Pressure::Nominal, false, 140.0);
    Stateful.Update(Pressure::Nominal, false, 199.9);
    TestTrue(TEXT("Renewed thermal pressure restarts recovery cooldown"), Stateful.EffectivePressure() == Pressure::Fair);
    Stateful.Update(Pressure::Nominal, false, 200.0);
    TestTrue(TEXT("Second continuous cooldown recovers"), Stateful.EffectivePressure() == Pressure::Nominal);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
