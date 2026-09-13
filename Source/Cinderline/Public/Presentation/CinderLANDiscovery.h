#pragma once

#include "CoreMinimal.h"

/** A resolved Cinderline server advertised on the local network. */
struct CINDERLINE_API FCinderLANService
{
    FString StableId;
    FString Name;
    FString Host;
    FString Address;
    int32 Port = 0;
    int32 ProtocolVersion = -1;
    bool bCompatible = false;

    /** WebSocket endpoint suitable for UCinderOnlineSubsystem. Empty until resolved. */
    FString Endpoint() const;
};

/**
 * Explicit, polling Bonjour discovery facade for the Local Network panel.
 * Construction is inert: Start must be called by the UI before any LAN access occurs.
 */
class CINDERLINE_API FCinderLANDiscovery
{
public:
    FCinderLANDiscovery();
    ~FCinderLANDiscovery();

    FCinderLANDiscovery(const FCinderLANDiscovery&) = delete;
    FCinderLANDiscovery& operator=(const FCinderLANDiscovery&) = delete;

    void Start();
    void Stop();
    bool IsRunning() const;
    TArray<FCinderLANService> Services() const;
    FString Status() const;
    FString Error() const;

private:
    void* NativeHandle = nullptr;
};
