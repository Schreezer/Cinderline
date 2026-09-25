#pragma once

#include "CoreMinimal.h"

enum class ECinderVoiceAudioState : uint8
{
    Starting,
    Listening,
    Stopped,
    Error
};

/** Native audio only. No provider keys, networking, or game commands enter this module. */
class CINDERVOICEAUDIO_API FCinderVoiceAudio
{
public:
    static constexpr int32 SampleRate = 24000;
    static constexpr int32 Channels = 1;
    static constexpr int32 BytesPerSample = 2;

    // PCM is mono signed 16-bit little endian, 24 kHz, without a container header.
    // Capture callbacks run on a serial audio processing queue, never the game tick.
    using FPCMCallback = TFunction<void(TArray<uint8>)>;
    // State callbacks run on the game thread. Callers must guard their session lifetime.
    using FStateCallback = TFunction<void(ECinderVoiceAudioState, const FString&)>;

    static TUniquePtr<FCinderVoiceAudio> Create();
    virtual ~FCinderVoiceAudio() = default;

    // Call controls from the game thread. Start is the only operation that requests microphone access.
    virtual void Start(FPCMCallback OnPCM, FStateCallback OnState) = 0;
    virtual void SetMuted(bool bMuted) = 0;
    virtual void QueuePlayback(TArray<uint8> PCM16LE24000) = 0;
    // Drops queued playback immediately, including work waiting on the processing queue.
    virtual void ClearPlayback() = 0;
    // Invalidates queued capture and releases the microphone; never waits on the game thread.
    virtual void Stop() = 0;
};
