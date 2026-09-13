#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Presentation/CinderMetalFXResolution.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderMetalFXResolutionTest,
    "Cinderline.Presentation.MetalFXResolutionOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderMetalFXResolutionTest::RunTest(const FString&)
{
    FCinderMetalFXResolution Policy;
    TestEqual(TEXT("Unsupported devices keep native resolution"), Policy.Update(100, 80, false), 100.0f);
    TestEqual(TEXT("Supported device adopts spatial quality cap"), Policy.Update(100, 80, true), 80.0f);
    TestEqual(TEXT("Stable frames do not compound the scale"), Policy.Update(80, 80, true), 80.0f);
    TestEqual(TEXT("Turning off restores original native resolution"), Policy.Update(80, 80, false), 100.0f);
    TestEqual(TEXT("Automatic baseline is normalized only while active"), Policy.Update(0, 80, true), 80.0f);
    TestEqual(TEXT("Automatic sentinel restored exactly"), Policy.Update(80, 80, false), 0.0f);
    TestEqual(TEXT("Thermal policy's lower budget wins"), Policy.Update(70, 80, true), 70.0f);
    TestEqual(TEXT("Turning off retains lower thermal limit"), Policy.Update(70, 80, false), 70.0f);
    TestEqual(TEXT("Thermal recovery restores MetalFX quality"), Policy.Update(100, 80, true), 80.0f);
    TestEqual(TEXT("External lower user setting is adopted"), Policy.Update(60, 80, true), 60.0f);
    TestEqual(TEXT("Disabling never undoes a newer user setting"), Policy.Update(60, 80, false), 60.0f);
    TestEqual(TEXT("Bad request releases override"), Policy.Update(60, -1, true), 60.0f);
    TestEqual(TEXT("Oversized quality request cannot supersample"), Policy.Update(100, 200, true), 100.0f);
    return true;
}
#endif
