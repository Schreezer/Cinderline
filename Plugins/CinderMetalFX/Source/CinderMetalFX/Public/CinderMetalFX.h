#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

struct FCinderMetalFXDiagnostics
{
    bool bRequested = false;
    bool bRHIIsMetal = false;
    bool bOSSupported = false;
    bool bDeviceSupported = false;
    bool bBridgeAvailable = false;
    bool bInterfaceActive = false;
    float RequestedPercentage = 100.0f;
    float EffectiveFraction = 1.0f;
    uint64 RegisteredFrames = 0;
    uint64 EncodedFrames = 0;
    uint64 ScalerCreations = 0;
    uint64 ValidationProbes = 0;
    uint64 Fallbacks = 0;
    int32 LastInputWidth = 0;
    int32 LastInputHeight = 0;
    int32 LastOutputWidth = 0;
    int32 LastOutputHeight = 0;
    FString FallbackReason;
};

class CINDERMETALFX_API ICinderMetalFXModule : public IModuleInterface
{
public:
    static ICinderMetalFXModule& Get()
    {
        return FModuleManager::LoadModuleChecked<ICinderMetalFXModule>(TEXT("CinderMetalFX"));
    }

    static bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded(TEXT("CinderMetalFX"));
    }

    virtual FCinderMetalFXDiagnostics GetDiagnostics() const = 0;
};
