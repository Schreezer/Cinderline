#include "CinderMetalFX.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "IMetalDynamicRHI.h"
#include "Misc/CoreDelegates.h"
#include "PixelFormat.h"
#include "PostProcess/PostProcessUpscale.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "SceneRendering.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"

#import <MetalFX/MetalFX.h>
#include <dlfcn.h>

DEFINE_LOG_CATEGORY_STATIC(LogCinderMetalFX, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarCinderMetalFXEnabled(
    TEXT("r.CinderMetalFX.Enabled"),
    1,
    TEXT("Enables MetalFX spatial upscaling when the OS, GPU, and engine bridge support it."),
    ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarCinderMetalFXScreenPercentage(
    TEXT("r.CinderMetalFX.ScreenPercentage"),
    80.0f,
    TEXT("Requested MetalFX render percentage. The effective value never raises a lower engine percentage."),
    ECVF_RenderThreadSafe);

using FCinderBridgeCallback = void (*)(MTL::CommandBuffer*, MTL::Fence*, void*);
using FCinderBridgeFunction = bool (*)(FRHICommandListBase*, FCinderBridgeCallback, void*);

#if PLATFORM_IOS && CINDER_METALFX_ENGINE_BRIDGE
extern "C" bool CinderMetalRHIRunOnCurrentCommandBuffer(
    FRHICommandListBase* RHICmdList,
    FCinderBridgeCallback Callback,
    void* UserData);
#endif

FCinderBridgeFunction FindEngineBridge()
{
#if PLATFORM_IOS && CINDER_METALFX_ENGINE_BRIDGE
    // This strong reference retains the bridge in a monolithic iOS static link.
    return &CinderMetalRHIRunOnCurrentCommandBuffer;
#else
    // Mac modules use a runtime lookup so a plugin built with the isolated engine
    // still loads against a stock engine and takes the built-in upscale fallback.
    return reinterpret_cast<FCinderBridgeFunction>(
        dlsym(RTLD_DEFAULT, "CinderMetalRHIRunOnCurrentCommandBuffer"));
#endif
}

bool IsRuntimeOSSupported()
{
#if PLATFORM_MAC
    if (@available(macOS 13.0, *))
    {
        return true;
    }
#elif PLATFORM_IOS
    if (@available(iOS 16.0, *))
    {
        return true;
    }
#endif
    return false;
}

MTLPixelFormat ResolveMetalPixelFormat(const FRDGTextureDesc& Desc)
{
    MTLPixelFormat Format = static_cast<MTLPixelFormat>(GPixelFormats[Desc.Format].PlatformFormat);
    if (EnumHasAnyFlags(Desc.Flags, TexCreate_SRGB))
    {
        switch (Format)
        {
        case MTLPixelFormatRGBA8Unorm:
            Format = MTLPixelFormatRGBA8Unorm_sRGB;
            break;
        case MTLPixelFormatBGRA8Unorm:
            Format = MTLPixelFormatBGRA8Unorm_sRGB;
            break;
        default:
            break;
        }
    }
    return Format;
}

MTLTextureUsage ResolveMetalUsage(ETextureCreateFlags Flags)
{
    MTLTextureUsage Usage = MTLTextureUsageUnknown;
    if (EnumHasAnyFlags(Flags, TexCreate_ShaderResource))
    {
        Usage |= MTLTextureUsageShaderRead;
    }
    if (EnumHasAnyFlags(Flags, TexCreate_UAV))
    {
        Usage |= MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    }
    if (EnumHasAnyFlags(Flags, TexCreate_RenderTargetable))
    {
        Usage |= MTLTextureUsageRenderTarget;
    }
    return Usage;
}

struct FCinderMetalFXScalerKey
{
    FIntPoint InputExtent = FIntPoint::ZeroValue;
    FIntPoint OutputExtent = FIntPoint::ZeroValue;
    MTLPixelFormat InputFormat = MTLPixelFormatInvalid;
    MTLPixelFormat OutputFormat = MTLPixelFormatInvalid;

    bool operator==(const FCinderMetalFXScalerKey& Other) const
    {
        return InputExtent == Other.InputExtent &&
            OutputExtent == Other.OutputExtent &&
            InputFormat == Other.InputFormat &&
            OutputFormat == Other.OutputFormat;
    }
};

struct FCinderMetalFXScaler
{
    FCinderMetalFXScaler(id InScaler, MTLTextureUsage InInputUsage, MTLTextureUsage InOutputUsage)
        : Scaler(InScaler)
        , RequiredInputUsage(InInputUsage)
        , RequiredOutputUsage(InOutputUsage)
    {
    }

    ~FCinderMetalFXScaler()
    {
        [Scaler release];
    }

    bool TryBeginProbe()
    {
        FScopeLock Lock(&StatusMutex);
        if (ValidationState != 0)
        {
            return false;
        }
        ValidationState = 1;
        return true;
    }

    bool IsValidated() const
    {
        FScopeLock Lock(&StatusMutex);
        return ValidationState == 2;
    }

    bool IsFailed() const
    {
        FScopeLock Lock(&StatusMutex);
        return ValidationState == 3;
    }

    void MarkValidated()
    {
        FScopeLock Lock(&StatusMutex);
        ValidationState = 2;
    }

    void MarkFailed()
    {
        FScopeLock Lock(&StatusMutex);
        ValidationState = 3;
    }

    id Scaler = nil;
    MTLTextureUsage RequiredInputUsage = MTLTextureUsageUnknown;
    MTLTextureUsage RequiredOutputUsage = MTLTextureUsageUnknown;
    FCriticalSection EncodeMutex;

private:
    mutable FCriticalSection StatusMutex;
    int32 ValidationState = 0; // New, probe pending, validated, failed.
};

struct FCinderMetalFXScalerEntry
{
    FCinderMetalFXScalerKey Key;
    TSharedPtr<FCinderMetalFXScaler, ESPMode::ThreadSafe> Native;
    uint64 LastUse = 0;
};

class FCinderMetalFXState : public TSharedFromThis<FCinderMetalFXState, ESPMode::ThreadSafe>
{
public:
    FCinderMetalFXDiagnostics Snapshot() const
    {
        FScopeLock Lock(&Mutex);
        return Diagnostics;
    }

    void SetEligibility(
        bool bRequested,
        bool bMetal,
        bool bOS,
        bool bDevice,
        bool bBridge,
        const TCHAR* Reason)
    {
        FScopeLock Lock(&Mutex);
        Diagnostics.bRequested = bRequested;
        Diagnostics.bRHIIsMetal = bMetal;
        Diagnostics.bOSSupported = bOS;
        Diagnostics.bDeviceSupported = bDevice;
        Diagnostics.bBridgeAvailable = bBridge;
        Diagnostics.bInterfaceActive = false;
        Diagnostics.RequestedPercentage = CVarCinderMetalFXScreenPercentage.GetValueOnAnyThread();
        if (Diagnostics.Fallbacks == 0)
        {
            Diagnostics.FallbackReason = Reason;
        }
    }

    void RecordRegistration()
    {
        FScopeLock Lock(&Mutex);
        Diagnostics.bInterfaceActive = true;
        if (Diagnostics.Fallbacks == 0)
        {
            Diagnostics.FallbackReason.Reset();
        }
        ++Diagnostics.RegisteredFrames;
    }

    void RecordFallback(const TCHAR* Reason)
    {
        FScopeLock Lock(&Mutex);
        if (Diagnostics.Fallbacks == 0)
        {
            Diagnostics.FallbackReason = Reason;
        }
        ++Diagnostics.Fallbacks;
    }

    void RecordEncode(FIntPoint InputSize, FIntPoint OutputSize)
    {
        FScopeLock Lock(&Mutex);
        Diagnostics.LastInputWidth = InputSize.X;
        Diagnostics.LastInputHeight = InputSize.Y;
        Diagnostics.LastOutputWidth = OutputSize.X;
        Diagnostics.LastOutputHeight = OutputSize.Y;
        Diagnostics.EffectiveFraction = OutputSize.X > 0 && OutputSize.Y > 0
            ? FMath::Min(
                static_cast<float>(InputSize.X) / static_cast<float>(OutputSize.X),
                static_cast<float>(InputSize.Y) / static_cast<float>(OutputSize.Y))
            : 1.0f;
        if (Diagnostics.Fallbacks == 0)
        {
            Diagnostics.FallbackReason.Reset();
        }
        ++Diagnostics.EncodedFrames;
    }

    void RecordProbe()
    {
        FScopeLock Lock(&Mutex);
        ++Diagnostics.ValidationProbes;
    }

    FCinderBridgeFunction ResolveBridge()
    {
        FScopeLock Lock(&Mutex);
        if (!Bridge)
        {
            Bridge = FindEngineBridge();
        }
        Diagnostics.bBridgeAvailable = Bridge != nullptr;
        return Bridge;
    }

    TSharedPtr<FCinderMetalFXScaler, ESPMode::ThreadSafe> FindOrCreateScaler(
        const FCinderMetalFXScalerKey& Key,
        MTLTextureUsage InputUsage,
        MTLTextureUsage OutputUsage)
    {
        FScopeLock Lock(&Mutex);
        for (FCinderMetalFXScalerEntry& Entry : Scalers)
        {
            if (Entry.Key == Key)
            {
                Entry.LastUse = ++CacheUseSerial;
                return Entry.Native;
            }
        }

        id Scaler = nil;
        MTLTextureUsage RequiredInputUsage = MTLTextureUsageUnknown;
        MTLTextureUsage RequiredOutputUsage = MTLTextureUsageUnknown;
        if (@available(macOS 13.0, iOS 16.0, *))
        {
            id<MTLDevice> Device = (__bridge id<MTLDevice>)(void*)GetIMetalDynamicRHI()->RHIGetDevice();
            MTLFXSpatialScalerDescriptor* Descriptor = [MTLFXSpatialScalerDescriptor new];
            Descriptor.colorTextureFormat = Key.InputFormat;
            Descriptor.outputTextureFormat = Key.OutputFormat;
            Descriptor.inputWidth = static_cast<NSUInteger>(Key.InputExtent.X);
            Descriptor.inputHeight = static_cast<NSUInteger>(Key.InputExtent.Y);
            Descriptor.outputWidth = static_cast<NSUInteger>(Key.OutputExtent.X);
            Descriptor.outputHeight = static_cast<NSUInteger>(Key.OutputExtent.Y);
            Descriptor.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
            id<MTLFXSpatialScaler> NativeScaler = [Descriptor newSpatialScalerWithDevice:Device];
            [Descriptor release];
            if (NativeScaler)
            {
                RequiredInputUsage = NativeScaler.colorTextureUsage;
                RequiredOutputUsage = NativeScaler.outputTextureUsage;
                if ((RequiredInputUsage & InputUsage) == RequiredInputUsage &&
                    (RequiredOutputUsage & OutputUsage) == RequiredOutputUsage)
                {
                    Scaler = NativeScaler;
                }
                else
                {
                    [NativeScaler release];
                }
            }
        }

        // Four entries cover normal orientation and quality transitions. Captured
        // shared pointers keep an evicted scaler alive through queued RHI work.
        if (Scalers.Num() >= 4)
        {
            int32 OldestIndex = 0;
            for (int32 Index = 1; Index < Scalers.Num(); ++Index)
            {
                if (Scalers[Index].LastUse < Scalers[OldestIndex].LastUse)
                {
                    OldestIndex = Index;
                }
            }
            Scalers.RemoveAtSwap(OldestIndex, 1, EAllowShrinking::No);
        }

        FCinderMetalFXScalerEntry& Entry = Scalers.AddDefaulted_GetRef();
        Entry.Key = Key;
        Entry.LastUse = ++CacheUseSerial;
        if (Scaler)
        {
            Entry.Native = MakeShared<FCinderMetalFXScaler, ESPMode::ThreadSafe>(
                Scaler, RequiredInputUsage, RequiredOutputUsage);
            ++Diagnostics.ScalerCreations;
        }
        return Entry.Native;
    }

private:
    mutable FCriticalSection Mutex;
    FCinderMetalFXDiagnostics Diagnostics;
    FCinderBridgeFunction Bridge = nullptr;
    TArray<FCinderMetalFXScalerEntry> Scalers;
    uint64 CacheUseSerial = 0;
};

BEGIN_SHADER_PARAMETER_STRUCT(FCinderMetalFXPassParameters, )
    RDG_TEXTURE_ACCESS(InputTexture, ERHIAccess::SRVCompute)
    RDG_TEXTURE_ACCESS(OutputTexture, ERHIAccess::UAVCompute)
END_SHADER_PARAMETER_STRUCT()

struct FCinderMetalFXEncodeCall
{
    TSharedPtr<FCinderMetalFXState, ESPMode::ThreadSafe> State;
    TSharedPtr<FCinderMetalFXScaler, ESPMode::ThreadSafe> Native;
    FTextureRHIRef Input;
    FTextureRHIRef Output;
    FIntPoint InputExtent = FIntPoint::ZeroValue;
    FIntPoint InputContent = FIntPoint::ZeroValue;
    FIntPoint OutputExtent = FIntPoint::ZeroValue;
    MTLPixelFormat InputFormat = MTLPixelFormatInvalid;
    MTLPixelFormat OutputFormat = MTLPixelFormatInvalid;
    MTLTextureUsage RequiredInputUsage = MTLTextureUsageUnknown;
    MTLTextureUsage RequiredOutputUsage = MTLTextureUsageUnknown;
    bool bProbe = false;
};

void EncodeMetalFX(MTL::CommandBuffer* CommandBuffer, MTL::Fence* Fence, void* UserData)
{
    FCinderMetalFXEncodeCall& Call = *static_cast<FCinderMetalFXEncodeCall*>(UserData);
    if (@available(macOS 13.0, iOS 16.0, *))
    {
        id<MTLTexture> Input = (__bridge id<MTLTexture>)Call.Input->GetNativeResource();
        id<MTLTexture> Output = (__bridge id<MTLTexture>)Call.Output->GetNativeResource();
        id<MTLCommandBuffer> NativeCommandBuffer = (__bridge id<MTLCommandBuffer>)(void*)CommandBuffer;
        id<MTLFXSpatialScaler> Scaler = (id<MTLFXSpatialScaler>)Call.Native->Scaler;

        const bool bResourcesValid = Input && Output && NativeCommandBuffer && Scaler;
        const bool bFormatsValid = Input && Output &&
            Input.pixelFormat == Call.InputFormat && Output.pixelFormat == Call.OutputFormat;
        const bool bInputUsageValid = Input &&
            (Input.usage & Call.RequiredInputUsage) == Call.RequiredInputUsage;
        const bool bOutputUsageValid = Output &&
            (Output.usage & Call.RequiredOutputUsage) == Call.RequiredOutputUsage;
        const bool bStorageValid = Output && Output.storageMode == MTLStorageModePrivate;
        const bool bDimensionsValid = Input && Output &&
            static_cast<int32>(Input.width) == Call.InputExtent.X &&
            static_cast<int32>(Input.height) == Call.InputExtent.Y &&
            static_cast<int32>(Output.width) == Call.OutputExtent.X &&
            static_cast<int32>(Output.height) == Call.OutputExtent.Y;
        const bool bFenceValid = Fence || (Input && Output &&
            Input.hazardTrackingMode != MTLHazardTrackingModeUntracked &&
            Output.hazardTrackingMode != MTLHazardTrackingModeUntracked);
        const bool bNativeContractValid = bResourcesValid && bFormatsValid &&
            bInputUsageValid && bOutputUsageValid && bStorageValid &&
            bDimensionsValid && bFenceValid;
        if (!bNativeContractValid)
        {
            Call.Native->MarkFailed();
            const FString Detail = FString::Printf(
                TEXT("native_texture_mismatch checks[resources=%d formats=%d input_usage=%d output_usage=%d storage=%d dimensions=%d fence=%d] resources[input=%d output=%d command_buffer=%d scaler=%d] formats[input_actual=%llu input_expected=%llu output_actual=%llu output_expected=%llu] usage[input_actual=0x%llx input_required=0x%llx output_actual=0x%llx output_required=0x%llx] storage[input_actual=%lld output_actual=%lld output_required=%lld] dimensions[input_actual=%llux%llu input_expected=%dx%d input_content=%dx%d output_actual=%llux%llu output_expected=%dx%d] hazard[input=%lld output=%lld fence_present=%d]"),
                bResourcesValid, bFormatsValid, bInputUsageValid, bOutputUsageValid,
                bStorageValid, bDimensionsValid, bFenceValid,
                Input != nil, Output != nil, NativeCommandBuffer != nil, Scaler != nil,
                static_cast<uint64>(Input ? Input.pixelFormat : MTLPixelFormatInvalid),
                static_cast<uint64>(Call.InputFormat),
                static_cast<uint64>(Output ? Output.pixelFormat : MTLPixelFormatInvalid),
                static_cast<uint64>(Call.OutputFormat),
                static_cast<uint64>(Input ? Input.usage : MTLTextureUsageUnknown),
                static_cast<uint64>(Call.RequiredInputUsage),
                static_cast<uint64>(Output ? Output.usage : MTLTextureUsageUnknown),
                static_cast<uint64>(Call.RequiredOutputUsage),
                Input ? static_cast<int64>(Input.storageMode) : -1,
                Output ? static_cast<int64>(Output.storageMode) : -1,
                static_cast<int64>(MTLStorageModePrivate),
                static_cast<uint64>(Input ? Input.width : 0),
                static_cast<uint64>(Input ? Input.height : 0),
                Call.InputExtent.X, Call.InputExtent.Y,
                Call.InputContent.X, Call.InputContent.Y,
                static_cast<uint64>(Output ? Output.width : 0),
                static_cast<uint64>(Output ? Output.height : 0),
                Call.OutputExtent.X, Call.OutputExtent.Y,
                Input ? static_cast<int64>(Input.hazardTrackingMode) : -1,
                Output ? static_cast<int64>(Output.hazardTrackingMode) : -1,
                Fence != nullptr);
            Call.State->RecordFallback(*Detail);
            UE_LOG(LogCinderMetalFX, Error, TEXT("CINDER_METALFX_NATIVE_CONTRACT %s"), *Detail);
            if (!Call.bProbe)
            {
                UE_LOG(LogCinderMetalFX, Fatal,
                    TEXT("MetalFX native texture contract changed after this scaler key passed validation."));
            }
            return;
        }

        FScopeLock EncodeLock(&Call.Native->EncodeMutex);
        Scaler.colorTexture = Input;
        Scaler.inputContentWidth = static_cast<NSUInteger>(Call.InputContent.X);
        Scaler.inputContentHeight = static_cast<NSUInteger>(Call.InputContent.Y);
        Scaler.outputTexture = Output;
        Scaler.fence = (__bridge id<MTLFence>)(void*)Fence;
        [Scaler encodeToCommandBuffer:NativeCommandBuffer];
        Scaler.colorTexture = nil;
        Scaler.outputTexture = nil;
        Scaler.fence = nil;
        if (Call.bProbe)
        {
            Call.Native->MarkValidated();
        }
        else
        {
            Call.State->RecordEncode(Call.InputContent, Call.OutputExtent);
        }
        return;
    }
    Call.Native->MarkFailed();
    Call.State->RecordFallback(TEXT("os_unsupported_during_encode"));
    if (!Call.bProbe)
    {
        UE_LOG(LogCinderMetalFX, Fatal,
            TEXT("MetalFX OS availability changed after this scaler key passed validation."));
    }
}

class FCinderMetalFXSpatialUpscaler final : public ISpatialUpscaler
{
public:
    explicit FCinderMetalFXSpatialUpscaler(
        TSharedRef<FCinderMetalFXState, ESPMode::ThreadSafe> InState)
        : State(InState)
    {
    }

    virtual const TCHAR* GetDebugName() const override
    {
        return TEXT("CinderMetalFXSpatial");
    }

    virtual ISpatialUpscaler* Fork_GameThread(const FSceneViewFamily&) const override
    {
        return new FCinderMetalFXSpatialUpscaler(State);
    }

    virtual FScreenPassTexture AddPasses(
        FRDGBuilder& GraphBuilder,
        const FViewInfo& View,
        const FInputs& Inputs) const override
    {
        check(Inputs.SceneColor.IsValid());

        auto DefaultUpscale = [&]()
        {
            return ISpatialUpscaler::AddDefaultUpscalePass(
                GraphBuilder, View, Inputs, EUpscaleMethod::Bilinear, View.LensDistortionLUT);
        };

        if (Inputs.Stage != EUpscaleStage::PrimaryToSecondary &&
            Inputs.Stage != EUpscaleStage::PrimaryToOutput)
        {
            State->RecordFallback(TEXT("unsupported_stage"));
            return DefaultUpscale();
        }

        const FIntRect InputRect = Inputs.SceneColor.ViewRect;
        const FIntPoint OutputSize = Inputs.Stage == EUpscaleStage::PrimaryToSecondary
            ? View.GetSecondaryViewRectSize()
            : View.UnscaledViewRect.Size();
        if (InputRect.Min != FIntPoint::ZeroValue || InputRect.IsEmpty() ||
            OutputSize.X <= InputRect.Width() || OutputSize.Y <= InputRect.Height())
        {
            State->RecordFallback(TEXT("unsupported_view_rect"));
            return DefaultUpscale();
        }

        FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
            OutputSize,
            Inputs.SceneColor.Texture->Desc.Format,
            FClearValueBinding::Black,
            TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable);
        if (EnumHasAnyFlags(Inputs.SceneColor.Texture->Desc.Flags, TexCreate_SRGB))
        {
            OutputDesc.Flags |= TexCreate_SRGB;
        }

        const FCinderMetalFXScalerKey Key {
            Inputs.SceneColor.Texture->Desc.Extent,
            OutputDesc.Extent,
            ResolveMetalPixelFormat(Inputs.SceneColor.Texture->Desc),
            ResolveMetalPixelFormat(OutputDesc)
        };
        if (Key.InputFormat == MTLPixelFormatInvalid || Key.OutputFormat == MTLPixelFormatInvalid)
        {
            State->RecordFallback(TEXT("invalid_pixel_format"));
            return DefaultUpscale();
        }

        FCinderBridgeFunction Bridge = State->ResolveBridge();
        if (!Bridge)
        {
            State->RecordFallback(TEXT("engine_bridge_unavailable"));
            return DefaultUpscale();
        }

        TSharedPtr<FCinderMetalFXScaler, ESPMode::ThreadSafe> Native = State->FindOrCreateScaler(
            Key,
            ResolveMetalUsage(Inputs.SceneColor.Texture->Desc.Flags),
            ResolveMetalUsage(OutputDesc.Flags));
        if (!Native.IsValid())
        {
            State->RecordFallback(TEXT("scaler_creation_failed"));
            return DefaultUpscale();
        }
        if (Native->IsFailed())
        {
            return DefaultUpscale();
        }

        const bool bProbe = Native->TryBeginProbe();
        const bool bValidated = Native->IsValidated();
        if (!bProbe && !bValidated)
        {
            // Another view already queued this key's validation pass. It will
            // publish the result after the current graph reaches the RHI thread.
            return DefaultUpscale();
        }
        if (bProbe)
        {
            State->RecordProbe();
        }

        FRDGTextureRef MetalFXOutput = GraphBuilder.CreateTexture(OutputDesc, TEXT("CinderMetalFX.Output"));
        FCinderMetalFXPassParameters* PassParameters = GraphBuilder.AllocParameters<FCinderMetalFXPassParameters>();
        PassParameters->InputTexture = Inputs.SceneColor.Texture;
        PassParameters->OutputTexture = MetalFXOutput;

        const TSharedRef<FCinderMetalFXState, ESPMode::ThreadSafe> LocalState = State;
        GraphBuilder.AddPass(
            RDG_EVENT_NAME("CinderMetalFX %dx%d -> %dx%d",
                InputRect.Width(), InputRect.Height(), OutputSize.X, OutputSize.Y),
            PassParameters,
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            [LocalState, Native, Bridge, Key, PassParameters, InputRect, OutputSize, bProbe](FRHIComputeCommandList& RHICmdList)
            {
                const FTextureRHIRef InputRHI = PassParameters->InputTexture->GetRHI();
                const FTextureRHIRef OutputRHI = PassParameters->OutputTexture->GetRHI();
                RHICmdList.EnqueueLambda(
                    TEXT("CinderMetalFX.Encode"),
                    [LocalState, Native, Bridge, Key, InputRHI, OutputRHI, InputRect, OutputSize, bProbe](FRHICommandListBase& ExecutingCmdList)
                    {
                        FCinderMetalFXEncodeCall Call {
                            LocalState,
                            Native,
                            InputRHI,
                            OutputRHI,
                            InputRHI->GetDesc().Extent,
                            InputRect.Size(),
                            OutputSize,
                            Key.InputFormat,
                            Key.OutputFormat,
                            Native->RequiredInputUsage,
                            Native->RequiredOutputUsage,
                            bProbe
                        };
                        if (!Bridge(&ExecutingCmdList, &EncodeMetalFX, &Call))
                        {
                            Native->MarkFailed();
                            LocalState->RecordFallback(TEXT("engine_bridge_rejected"));
                            if (!bProbe)
                            {
                                UE_LOG(LogCinderMetalFX, Fatal,
                                    TEXT("MetalFX bridge rejected a scaler key after its validation pass."));
                            }
                        }
                    });
            });

        if (bProbe)
        {
            // The first frame for a native key displays UE's ordinary upscale.
            // The NeverCull pass above validates the real RHI textures and bridge
            // into a private scratch output before a later frame may consume it.
            return DefaultUpscale();
        }

        FScreenPassTexture MetalFXResult(MetalFXOutput, FIntRect(FIntPoint::ZeroValue, OutputSize));
        if (Inputs.OverrideOutput.IsValid())
        {
            AddDrawTexturePass(GraphBuilder, View, MetalFXResult, Inputs.OverrideOutput);
            return Inputs.OverrideOutput;
        }
        return MetalFXResult;
    }

private:
    TSharedRef<FCinderMetalFXState, ESPMode::ThreadSafe> State;
};

class FCinderMetalFXViewExtension final : public FSceneViewExtensionBase
{
public:
    FCinderMetalFXViewExtension(
        const FAutoRegister& AutoRegister,
        TSharedRef<FCinderMetalFXState, ESPMode::ThreadSafe> InState)
        : FSceneViewExtensionBase(AutoRegister)
        , State(InState)
    {
    }

    virtual void BeginRenderViewFamily(FSceneViewFamily& ViewFamily) override
    {
        // Diagnostics drive the main viewport's resolution policy. Scene
        // captures and other auxiliary families must not overwrite that state.
        if (!ViewFamily.EngineShowFlags.Game || ViewFamily.Views.IsEmpty() || !ViewFamily.bIsMainViewFamily)
        {
            return;
        }

        const bool bRequested = CVarCinderMetalFXEnabled.GetValueOnGameThread() != 0;
        const bool bMetal = IsRHIMetal();
        const bool bOS = IsRuntimeOSSupported();
        const bool bBridge = State->ResolveBridge() != nullptr;
        bool bDevice = false;
        if (@available(macOS 13.0, iOS 16.0, *))
        {
            if (bRequested && bMetal && bOS && bBridge)
            {
                id<MTLDevice> Device = (__bridge id<MTLDevice>)(void*)GetIMetalDynamicRHI()->RHIGetDevice();
                bDevice = Device && [MTLFXSpatialScalerDescriptor supportsDevice:Device];
            }
        }

        const TCHAR* Reason = TEXT("");
        if (!bRequested) Reason = TEXT("disabled");
        else if (!bMetal) Reason = TEXT("non_metal_rhi");
        else if (!bOS) Reason = TEXT("os_unsupported");
        else if (!bBridge) Reason = TEXT("engine_bridge_unavailable");
        else if (!bDevice) Reason = TEXT("device_unsupported");
        State->SetEligibility(bRequested, bMetal, bOS, bDevice, bBridge, Reason);
        if (!bRequested || !bMetal || !bOS || !bBridge || !bDevice)
        {
            return;
        }

        if (ViewFamily.bIsHDR)
        {
            State->SetEligibility(bRequested, bMetal, bOS, bDevice, bBridge, TEXT("hdr_output_unsupported"));
            return;
        }
        if (ViewFamily.GetTemporalUpscalerInterface() || ViewFamily.GetPrimarySpatialUpscalerInterface())
        {
            State->SetEligibility(bRequested, bMetal, bOS, bDevice, bBridge, TEXT("upscaler_already_assigned"));
            return;
        }

        bool bUsesSpatialPrimary = false;
        for (const FSceneView* View : ViewFamily.Views)
        {
            bUsesSpatialPrimary |= View &&
                View->PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::SpatialUpscale;
        }
        if (!bUsesSpatialPrimary)
        {
            State->SetEligibility(bRequested, bMetal, bOS, bDevice, bBridge, TEXT("primary_method_not_spatial"));
            return;
        }

        ViewFamily.SetPrimarySpatialUpscalerInterface(new FCinderMetalFXSpatialUpscaler(State));
        State->RecordRegistration();
    }

private:
    TSharedRef<FCinderMetalFXState, ESPMode::ThreadSafe> State;
};
}

class FCinderMetalFXModule final : public ICinderMetalFXModule
{
public:
    virtual void StartupModule() override
    {
        State = MakeShared<FCinderMetalFXState, ESPMode::ThreadSafe>();
        if (GEngine)
        {
            RegisterViewExtension();
        }
        else
        {
            PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(
                this, &FCinderMetalFXModule::RegisterViewExtension);
        }
        StatusCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("r.CinderMetalFX.Status"),
            TEXT("Logs MetalFX eligibility, interface activity, native encode counters, dimensions, and fallback reason."),
            FConsoleCommandDelegate::CreateRaw(this, &FCinderMetalFXModule::LogStatus),
            ECVF_Default);
    }

    virtual void ShutdownModule() override
    {
        if (PostEngineInitHandle.IsValid())
        {
            FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
            PostEngineInitHandle.Reset();
        }
        if (StatusCommand)
        {
            IConsoleManager::Get().UnregisterConsoleObject(StatusCommand);
            StatusCommand = nullptr;
        }
        ViewExtension.Reset();
        FlushRenderingCommands();
        State.Reset();
    }

    virtual FCinderMetalFXDiagnostics GetDiagnostics() const override
    {
        return State.IsValid() ? State->Snapshot() : FCinderMetalFXDiagnostics{};
    }

private:
    void RegisterViewExtension()
    {
        if (GEngine && State.IsValid() && !ViewExtension.IsValid())
        {
            ViewExtension = FSceneViewExtensions::NewExtension<FCinderMetalFXViewExtension>(State.ToSharedRef());
        }
    }

    void LogStatus()
    {
        const FCinderMetalFXDiagnostics D = GetDiagnostics();
        UE_LOG(LogCinderMetalFX, Display,
            TEXT("CINDER_METALFX_STATUS requested=%d metal_rhi=%d os_supported=%d device_supported=%d bridge_available=%d interface_active=%d requested_percent=%.2f effective_fraction=%.4f registered_frames=%llu encoded_frames=%llu scaler_creations=%llu validation_probes=%llu fallbacks=%llu last_input=%dx%d last_output=%dx%d fallback_reason=%s"),
            D.bRequested, D.bRHIIsMetal, D.bOSSupported, D.bDeviceSupported,
            D.bBridgeAvailable, D.bInterfaceActive, D.RequestedPercentage, D.EffectiveFraction,
            D.RegisteredFrames, D.EncodedFrames, D.ScalerCreations, D.ValidationProbes, D.Fallbacks,
            D.LastInputWidth, D.LastInputHeight, D.LastOutputWidth, D.LastOutputHeight,
            D.FallbackReason.IsEmpty() ? TEXT("none") : *D.FallbackReason);
    }

    TSharedPtr<FCinderMetalFXState, ESPMode::ThreadSafe> State;
    TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> ViewExtension;
    FDelegateHandle PostEngineInitHandle;
    IConsoleObject* StatusCommand = nullptr;
};

IMPLEMENT_MODULE(FCinderMetalFXModule, CinderMetalFX)
