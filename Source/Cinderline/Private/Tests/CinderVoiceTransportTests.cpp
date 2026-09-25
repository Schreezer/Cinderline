#include "Presentation/CinderVoiceSubsystem.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderVoiceEndpointTest,
    "Cinderline.Voice.EndpointBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderVoiceEndpointTest::RunTest(const FString& Parameters)
{
    FString Error;
    const TCHAR* Valid[] = {
        TEXT("ws://127.0.0.1:8788/voice"), TEXT("ws://localhost:8788/voice"),
        TEXT("ws://[::1]:8788/voice"), TEXT("wss://commander.example.com/voice"),
        TEXT("wss://192.168.1.15:8788/voice")
    };
    for (const TCHAR* URL : Valid)
        TestTrue(FString::Printf(TEXT("Accepts explicit gateway %s"), URL),
            UCinderVoiceSubsystem::ValidateEndpoint(URL, Error));
    const TCHAR* Invalid[] = {
        TEXT(""), TEXT("ws://192.168.1.15:8788/voice"), TEXT("ws://commander.example.com/voice"),
        TEXT("ws://localhost.attacker.test/voice"), TEXT("wss://user:token@host/voice"),
        TEXT("wss://host/voice?token=credential"), TEXT("wss://host/voice#fragment"),
        TEXT("wss://host/play"), TEXT("https://host/voice"), TEXT("wss://host:0/voice"),
        TEXT("wss://host:99999/voice"), TEXT("wss://host:abc/voice"), TEXT("wss://host:/voice"),
        TEXT("wss:///voice"), TEXT("wss://host/path/voice"), TEXT("wss://host\r\n/voice")
    };
    for (const TCHAR* URL : Invalid)
    {
        TestFalse(FString::Printf(TEXT("Rejects unsafe endpoint %s"), URL),
            UCinderVoiceSubsystem::ValidateEndpoint(URL, Error));
        TestFalse(TEXT("Rejection explains the configuration problem"), Error.IsEmpty());
    }
    return true;
}
