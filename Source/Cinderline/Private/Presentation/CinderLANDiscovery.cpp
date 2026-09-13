#include "Presentation/CinderLANDiscovery.h"

#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Sim/Network.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#endif

namespace
{
constexpr int32 CurrentLANProtocol = static_cast<int32>(cinder::net::ProtocolVersion);
static_assert(CurrentLANProtocol == 7, "Bonjour compatibility must track the four-player wire protocol");

struct FCinderLANFallbackState
{
    mutable FCriticalSection Mutex;
    bool bRunning = false;
    FString Status;
    FString Error;
};
}

bool CinderLANParseProtocol(const FString& Value, int32& OutVersion)
{
    OutVersion = -1;
    if (Value.IsEmpty())
    {
        return false;
    }
    int32 Parsed = 0;
    for (const TCHAR Character : Value)
    {
        if (Character < TEXT('0') || Character > TEXT('9'))
        {
            return false;
        }
        const int32 Digit = static_cast<int32>(Character - TEXT('0'));
        if (Parsed > (MAX_int32 - Digit) / 10)
        {
            return false;
        }
        Parsed = Parsed * 10 + Digit;
    }
    OutVersion = Parsed;
    return OutVersion == CurrentLANProtocol;
}

#if PLATFORM_MAC || PLATFORM_IOS
void* CinderLANAppleCreate();
void CinderLANAppleDestroy(void* Handle);
void CinderLANAppleStart(void* Handle);
void CinderLANAppleStop(void* Handle);
bool CinderLANAppleIsRunning(void* Handle);
TArray<FCinderLANService> CinderLANAppleServices(void* Handle);
FString CinderLANAppleStatus(void* Handle);
FString CinderLANAppleError(void* Handle);
#endif

FString FCinderLANService::Endpoint() const
{
    if (Port <= 0 || Port > 65535)
    {
        return FString();
    }

    FString EndpointHost = Address.IsEmpty() ? Host : Address;
    EndpointHost.TrimStartAndEndInline();
    while (EndpointHost.RemoveFromEnd(TEXT("."))) {}
    if (EndpointHost.IsEmpty())
    {
        return FString();
    }
    if (EndpointHost.Contains(TEXT("/")) || EndpointHost.Contains(TEXT("\\")) ||
        EndpointHost.Contains(TEXT("?")) || EndpointHost.Contains(TEXT("#")) ||
        EndpointHost.Contains(TEXT("@")) || EndpointHost.Contains(TEXT("%")))
    {
        return FString();
    }
    for (const TCHAR Character : EndpointHost)
    {
        if (Character <= 32 || Character >= 127) return FString();
    }
    if (EndpointHost.Contains(TEXT(":")) && !EndpointHost.StartsWith(TEXT("[")))
    {
        EndpointHost = FString::Printf(TEXT("[%s]"), *EndpointHost);
    }
    return FString::Printf(TEXT("ws://%s:%d/play"), *EndpointHost, Port);
}

FCinderLANDiscovery::FCinderLANDiscovery()
{
#if PLATFORM_MAC || PLATFORM_IOS
    NativeHandle = CinderLANAppleCreate();
#else
    NativeHandle = new FCinderLANFallbackState();
#endif
}

FCinderLANDiscovery::~FCinderLANDiscovery()
{
#if PLATFORM_MAC || PLATFORM_IOS
    CinderLANAppleDestroy(NativeHandle);
#else
    delete static_cast<FCinderLANFallbackState*>(NativeHandle);
#endif
    NativeHandle = nullptr;
}

void FCinderLANDiscovery::Start()
{
#if PLATFORM_MAC || PLATFORM_IOS
    CinderLANAppleStart(NativeHandle);
#else
    auto* State = static_cast<FCinderLANFallbackState*>(NativeHandle);
    if (!State) return;
    FScopeLock Lock(&State->Mutex);
    State->bRunning = false;
    State->Status = TEXT("Local game discovery is unavailable on this platform.");
    State->Error = State->Status;
#endif
}

void FCinderLANDiscovery::Stop()
{
#if PLATFORM_MAC || PLATFORM_IOS
    CinderLANAppleStop(NativeHandle);
#else
    auto* State = static_cast<FCinderLANFallbackState*>(NativeHandle);
    if (!State) return;
    FScopeLock Lock(&State->Mutex);
    State->bRunning = false;
    State->Status.Empty();
    State->Error.Empty();
#endif
}

bool FCinderLANDiscovery::IsRunning() const
{
#if PLATFORM_MAC || PLATFORM_IOS
    return CinderLANAppleIsRunning(NativeHandle);
#else
    auto* State = static_cast<FCinderLANFallbackState*>(NativeHandle);
    if (!State) return false;
    FScopeLock Lock(&State->Mutex);
    return State->bRunning;
#endif
}

TArray<FCinderLANService> FCinderLANDiscovery::Services() const
{
#if PLATFORM_MAC || PLATFORM_IOS
    return CinderLANAppleServices(NativeHandle);
#else
    return {};
#endif
}

FString FCinderLANDiscovery::Status() const
{
#if PLATFORM_MAC || PLATFORM_IOS
    return CinderLANAppleStatus(NativeHandle);
#else
    auto* State = static_cast<FCinderLANFallbackState*>(NativeHandle);
    if (!State) return FString();
    FScopeLock Lock(&State->Mutex);
    return State->Status;
#endif
}

FString FCinderLANDiscovery::Error() const
{
#if PLATFORM_MAC || PLATFORM_IOS
    return CinderLANAppleError(NativeHandle);
#else
    auto* State = static_cast<FCinderLANFallbackState*>(NativeHandle);
    if (!State) return FString();
    FScopeLock Lock(&State->Mutex);
    return State->Error;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
class FCinderWaitForLANService final : public IAutomationLatentCommand
{
public:
    FCinderWaitForLANService(
        FAutomationTestBase* InTest,
        TSharedRef<FCinderLANDiscovery> InDiscovery,
        FString InExpectedName,
        int32 InExpectedPort)
        : Test(InTest)
        , Discovery(MoveTemp(InDiscovery))
        , ExpectedName(MoveTemp(InExpectedName))
        , ExpectedPort(InExpectedPort)
        , StartedAt(FPlatformTime::Seconds())
    {
    }

    virtual bool Update() override
    {
        for (const FCinderLANService& Service : Discovery->Services())
        {
            if (Service.Name != ExpectedName) continue;
            Test->TestFalse(TEXT("Advertised service has a stable identity"), Service.StableId.IsEmpty());
            Test->TestFalse(TEXT("Advertised service resolves a host"), Service.Host.IsEmpty());
            Test->TestEqual(TEXT("Advertised service resolves the expected port"), Service.Port, ExpectedPort);
            Test->TestEqual(TEXT("Advertised service declares the current protocol"), Service.ProtocolVersion, CurrentLANProtocol);
            Test->TestTrue(TEXT("Advertised service is compatible"), Service.bCompatible);
            Test->TestFalse(TEXT("Advertised service produces a WebSocket endpoint"), Service.Endpoint().IsEmpty());
            Discovery->Stop();
            Test->TestFalse(TEXT("Stop ends discovery synchronously for its caller"), Discovery->IsRunning());
            Test->TestEqual(TEXT("Stop clears the visible service snapshot"), Discovery->Services().Num(), 0);
            return true;
        }

        if (!Discovery->Error().IsEmpty() && !Discovery->IsRunning())
        {
            Test->AddError(FString::Printf(TEXT("LAN discovery stopped: %s"), *Discovery->Error()));
            Discovery->Stop();
            return true;
        }
        if (FPlatformTime::Seconds() - StartedAt >= 12.0)
        {
            Test->AddError(FString::Printf(
                TEXT("Timed out waiting for Bonjour service '%s' on port %d. Status: %s Error: %s"),
                *ExpectedName, ExpectedPort, *Discovery->Status(), *Discovery->Error()));
            Discovery->Stop();
            return true;
        }
        return false;
    }

private:
    FAutomationTestBase* Test;
    TSharedRef<FCinderLANDiscovery> Discovery;
    FString ExpectedName;
    int32 ExpectedPort;
    double StartedAt;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderLANDiscoveryValueTest,
    "Cinderline.Online.LANDiscoveryValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderLANDiscoveryValueTest::RunTest(const FString& Parameters)
{
    FCinderLANService IPv4;
    IPv4.Address = TEXT("192.168.1.42");
    IPv4.Host = TEXT("cinderline.local.");
    IPv4.Port = 8787;
    TestEqual(TEXT("Resolved IPv4 is preferred over the hostname"), IPv4.Endpoint(), TEXT("ws://192.168.1.42:8787/play"));

    FCinderLANService IPv6;
    IPv6.Address = TEXT("fd00::42");
    IPv6.Port = 8787;
    TestEqual(TEXT("IPv6 WebSocket hosts are bracketed"), IPv6.Endpoint(), TEXT("ws://[fd00::42]:8787/play"));

    int32 Version = -1;
    TestTrue(TEXT("Current advertised protocol is compatible"), CinderLANParseProtocol(TEXT("7"), Version));
    TestEqual(TEXT("Current protocol is retained"), Version, 7);
    TestFalse(TEXT("Older advertised protocol is incompatible"), CinderLANParseProtocol(TEXT("6"), Version));
    TestFalse(TEXT("Malformed advertised protocol is incompatible"), CinderLANParseProtocol(TEXT("7beta"), Version));
    TestEqual(TEXT("Malformed protocol has no usable version"), Version, -1);

    FCinderLANService Unresolved;
    Unresolved.Host = TEXT("cinderline.local.");
    Unresolved.Port = 8787;
    TestEqual(TEXT("A resolved hostname remains a usable fallback"), Unresolved.Endpoint(), TEXT("ws://cinderline.local:8787/play"));
    Unresolved.Port = 0;
    TestTrue(TEXT("An unresolved service cannot produce an endpoint"), Unresolved.Endpoint().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderLANDiscoveryTransportTest,
    "Cinderline.Online.LANDiscoveryTransport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderLANDiscoveryTransportTest::RunTest(const FString& Parameters)
{
    const FString ExpectedName = FPlatformMisc::GetEnvironmentVariable(TEXT("CINDERLINE_TEST_LAN_SERVICE"));
    const FString PortText = FPlatformMisc::GetEnvironmentVariable(TEXT("CINDERLINE_TEST_LAN_PORT"));
    if (ExpectedName.IsEmpty() && PortText.IsEmpty())
    {
        AddInfo(TEXT("SKIPPED: set CINDERLINE_TEST_LAN_SERVICE and CINDERLINE_TEST_LAN_PORT to test a live Bonjour advertisement."));
        return true;
    }
    int32 ExpectedPort = 0;
    if (ExpectedName.IsEmpty() || !LexTryParseString(ExpectedPort, *PortText) || ExpectedPort <= 0 || ExpectedPort > 65535)
    {
        AddError(TEXT("CINDERLINE_TEST_LAN_SERVICE and a valid CINDERLINE_TEST_LAN_PORT must be set together."));
        return false;
    }
#if PLATFORM_MAC || PLATFORM_IOS
    TSharedRef<FCinderLANDiscovery> Discovery = MakeShared<FCinderLANDiscovery>();
    Discovery->Start();
    TestTrue(TEXT("Explicit Start begins Apple Bonjour discovery"), Discovery->IsRunning());
    ADD_LATENT_AUTOMATION_COMMAND(FCinderWaitForLANService(this, Discovery, ExpectedName, ExpectedPort));
    return true;
#else
    AddError(TEXT("Live Bonjour discovery is only available on Apple platforms."));
    return false;
#endif
}
#endif
