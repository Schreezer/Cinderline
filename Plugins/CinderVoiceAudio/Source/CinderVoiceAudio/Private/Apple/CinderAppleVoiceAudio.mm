#include "CinderVoiceAudio.h"

#if PLATFORM_MAC || PLATFORM_IOS

#include "Async/Async.h"
#include <atomic>
#include <memory>

#if PLATFORM_MAC
// CoreServices declares its own FVector. Use Unreal's system header wrapper to isolate Apple aliases/macros.
#include "Mac/MacSystemIncludes.h"
#elif PLATFORM_IOS
#include "IOS/IOSSystemIncludes.h"
#endif
#import <AVFoundation/AVFoundation.h>
#import <AVFAudio/AVFAudio.h>

namespace
{
constexpr uint32 MaxPendingCaptureBuffers = 4;
constexpr uint32 MaxPlaybackFrames = FCinderVoiceAudio::SampleRate * 3;
constexpr int32 MaxPlaybackChunkBytes = FCinderVoiceAudio::SampleRate * 2 * 2;

dispatch_queue_t AudioQueue()
{
    static dispatch_queue_t Queue = dispatch_queue_create("com.cinderline.voice.audio", DISPATCH_QUEUE_SERIAL);
    return Queue;
}

struct FPlaybackBudget
{
    std::atomic<uint32> Frames{0};
};

struct FVoiceSession
{
    FCinderVoiceAudio::FPCMCallback OnPCM;
    FCinderVoiceAudio::FStateCallback OnState;
    std::atomic<bool> Cancelled{false};
    std::atomic<bool> Muted{false};
    std::atomic<uint64> MuteRevision{0};
    std::atomic<bool> CaptureInvalid{false};
    std::atomic<uint32> PendingCaptureBuffers{0};
    std::atomic<uint64> PlaybackEpoch{0};
    // Only game-thread controls replace Budget; completion handlers retain their original generation.
    std::shared_ptr<FPlaybackBudget> Budget = std::make_shared<FPlaybackBudget>();
    dispatch_queue_t Queue = AudioQueue();
    AVAudioEngine* Engine = nil;
    AVAudioPlayerNode* Player = nil;
    AVAudioFormat* WireFormat = nil;
    AVAudioConverter* InputConverter = nil;
    id ConfigurationObserver = nil;
    id InterruptionObserver = nil;
    bool TapInstalled = false;
#if PLATFORM_IOS
    NSString* PreviousCategory = nil;
    NSString* PreviousMode = nil;
    AVAudioSessionCategoryOptions PreviousOptions = 0;
    bool ChangedAudioSession = false;
#endif
};

using FSession = std::shared_ptr<FVoiceSession>;

void EmitState(const FSession& Session, ECinderVoiceAudioState State, const FString& Message,
    bool AllowCancelled = false)
{
    AsyncTask(ENamedThreads::GameThread, [Session, State, Message, AllowCancelled]
    {
        if ((!Session->Cancelled.load() || AllowCancelled) && Session->OnState)
        {
            Session->OnState(State, Message);
        }
    });
}

// All AVAudioEngine mutation is serialized here, away from the simulation/game thread.
void TearDown(const FSession& Session)
{
    NSNotificationCenter* Notifications = [NSNotificationCenter defaultCenter];
    if (Session->ConfigurationObserver)
        [Notifications removeObserver:Session->ConfigurationObserver];
    if (Session->InterruptionObserver)
        [Notifications removeObserver:Session->InterruptionObserver];
    Session->ConfigurationObserver = nil;
    Session->InterruptionObserver = nil;
    if (Session->TapInstalled)
    {
        [Session->Engine.inputNode removeTapOnBus:0];
        Session->TapInstalled = false;
    }
    [Session->Player stop];
    [Session->Engine stop];
    Session->Engine = nil;
    Session->Player = nil;
    Session->InputConverter = nil;
    Session->WireFormat = nil;
    Session->PlaybackEpoch.fetch_add(1);
#if PLATFORM_IOS
    if (Session->ChangedAudioSession)
    {
        // Unreal owns the app-wide audio session too. Restore its category without deactivating it.
        AVAudioSession* AudioSession = [AVAudioSession sharedInstance];
        if ([AudioSession.category isEqualToString:AVAudioSessionCategoryPlayAndRecord] &&
            [AudioSession.mode isEqualToString:AVAudioSessionModeVoiceChat])
        {
            [AudioSession setCategory:Session->PreviousCategory mode:Session->PreviousMode
                options:Session->PreviousOptions error:nil];
        }
        Session->ChangedAudioSession = false;
    }
#endif
}

void Fail(const FSession& Session, const FString& Message)
{
    if (!Session->Cancelled.exchange(true))
    {
        TearDown(Session);
        EmitState(Session, ECinderVoiceAudioState::Error, Message, true);
    }
}

void ConvertCapture(const FSession& Session, AVAudioPCMBuffer* Input)
{
    if (Session->Cancelled.load() || Session->Muted.load() || Session->CaptureInvalid.load() ||
        !Session->InputConverter) return;
    const double Ratio = static_cast<double>(FCinderVoiceAudio::SampleRate) / Input.format.sampleRate;
    const AVAudioFrameCount Capacity = static_cast<AVAudioFrameCount>(ceil(Input.frameLength * Ratio)) + 64;
    AVAudioPCMBuffer* Output = [[AVAudioPCMBuffer alloc] initWithPCMFormat:Session->WireFormat
        frameCapacity:Capacity];
    if (!Output)
    {
        Fail(Session, TEXT("Microphone audio memory is unavailable. Reconnect voice and try again."));
        return;
    }
    __block bool Supplied = false;
    NSError* Error = nil;
    const AVAudioConverterOutputStatus Status = [Session->InputConverter convertToBuffer:Output
        error:&Error withInputFromBlock:^AVAudioBuffer* (AVAudioPacketCount, AVAudioConverterInputStatus* OutStatus)
    {
        if (Supplied)
        {
            *OutStatus = AVAudioConverterInputStatus_NoDataNow;
            return nil;
        }
        Supplied = true;
        *OutStatus = AVAudioConverterInputStatus_HaveData;
        return Input;
    }];
    if (Error || Status == AVAudioConverterOutputStatus_Error)
    {
        Fail(Session, TEXT("Microphone format conversion failed. End voice and reconnect."));
        return;
    }
    if (Output.frameLength == 0 || !Output.floatChannelData) return;
    TArray<uint8> PCM;
    PCM.SetNumUninitialized(static_cast<int32>(Output.frameLength) * 2);
    const float* Samples = Output.floatChannelData[0];
    for (AVAudioFrameCount Index = 0; Index < Output.frameLength; ++Index)
    {
        const float Sample = FMath::IsFinite(Samples[Index]) ? Samples[Index] : 0.0f;
        const int32 Scaled = FMath::RoundToInt(FMath::Clamp(Sample, -1.0f, 1.0f) * 32767.0f);
        const uint16 Bits = static_cast<uint16>(static_cast<int16>(Scaled));
        PCM[Index * 2] = static_cast<uint8>(Bits & 0xff);
        PCM[Index * 2 + 1] = static_cast<uint8>(Bits >> 8);
    }
    if (!Session->Cancelled.load() && !Session->Muted.load() && !Session->CaptureInvalid.load() && Session->OnPCM)
        Session->OnPCM(MoveTemp(PCM));
}

void BeginAudio(const FSession& Session)
{
    if (Session->Cancelled.load()) return;
    @autoreleasepool
    {
        NSError* Error = nil;
#if PLATFORM_IOS
        AVAudioSession* AudioSession = [AVAudioSession sharedInstance];
        Session->PreviousCategory = AudioSession.category;
        Session->PreviousMode = AudioSession.mode;
        Session->PreviousOptions = AudioSession.categoryOptions;
        if (![AudioSession setCategory:AVAudioSessionCategoryPlayAndRecord
            mode:AVAudioSessionModeVoiceChat
            options:AVAudioSessionCategoryOptionDefaultToSpeaker | AVAudioSessionCategoryOptionAllowBluetoothHFP |
                AVAudioSessionCategoryOptionMixWithOthers error:&Error])
        {
            Fail(Session, TEXT("Could not enable the voice audio session."));
            return;
        }
        Session->ChangedAudioSession = true;
        if (![AudioSession setActive:YES error:&Error])
        {
            Fail(Session, TEXT("Microphone audio is unavailable. Close other audio calls and try again."));
            return;
        }
#endif
        Session->Engine = [[AVAudioEngine alloc] init];
        AVAudioInputNode* InputNode = Session->Engine.inputNode;
        if (![InputNode setVoiceProcessingEnabled:YES error:&Error])
        {
            Fail(Session, TEXT("Voice echo cancellation could not start on this audio device."));
            return;
        }
        InputNode.voiceProcessingBypassed = NO;
        InputNode.voiceProcessingAGCEnabled = YES;
        InputNode.voiceProcessingInputMuted = Session->Muted.load();
        AVAudioFormat* InputFormat = [InputNode outputFormatForBus:0];
        if (InputFormat.sampleRate <= 0 || InputFormat.channelCount == 0)
        {
            Fail(Session, TEXT("No microphone is available. Connect a microphone and try again."));
            return;
        }
        Session->WireFormat = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
            sampleRate:FCinderVoiceAudio::SampleRate channels:1 interleaved:NO];
        Session->InputConverter = [[AVAudioConverter alloc] initFromFormat:InputFormat toFormat:Session->WireFormat];
        if (!Session->InputConverter)
        {
            Fail(Session, TEXT("This microphone audio format is not supported."));
            return;
        }
        Session->Player = [[AVAudioPlayerNode alloc] init];
        [Session->Engine attachNode:Session->Player];
        [Session->Engine connect:Session->Player to:Session->Engine.mainMixerNode format:Session->WireFormat];

        // A weak capture avoids Engine -> tap -> Session -> Engine ownership cycles.
        const std::weak_ptr<FVoiceSession> WeakSession = Session;
        [InputNode installTapOnBus:0 bufferSize:1024 format:InputFormat
            block:^(AVAudioPCMBuffer* Buffer, AVAudioTime*)
        {
            const FSession Active = WeakSession.lock();
            if (!Active || Active->Cancelled.load() || Active->Muted.load() || Active->CaptureInvalid.load()) return;
            const uint32 Queued = Active->PendingCaptureBuffers.fetch_add(1);
            if (Queued >= MaxPendingCaptureBuffers)
            {
                Active->PendingCaptureBuffers.fetch_sub(1);
                if (!Active->CaptureInvalid.exchange(true))
                {
                    dispatch_async(Active->Queue, ^
                    {
                        Fail(Active, TEXT("Microphone processing fell behind. Reconnect voice and repeat your request."));
                    });
                }
                return; // End the session; missing words must never silently change a command.
            }
            AVAudioPCMBuffer* Copy = [[AVAudioPCMBuffer alloc] initWithPCMFormat:Buffer.format
                frameCapacity:Buffer.frameLength];
            if (!Copy)
            {
                Active->PendingCaptureBuffers.fetch_sub(1);
                if (!Active->CaptureInvalid.exchange(true))
                    dispatch_async(Active->Queue, ^
                    {
                        Fail(Active, TEXT("Microphone audio memory is unavailable. Reconnect and repeat your request."));
                    });
                return;
            }
            Copy.frameLength = Buffer.frameLength;
            const AudioBufferList* Source = Buffer.audioBufferList;
            AudioBufferList* Destination = Copy.mutableAudioBufferList;
            for (uint32 Index = 0; Index < Source->mNumberBuffers; ++Index)
            {
                const uint32 Bytes = FMath::Min(Source->mBuffers[Index].mDataByteSize,
                    Destination->mBuffers[Index].mDataByteSize);
                FMemory::Memcpy(Destination->mBuffers[Index].mData, Source->mBuffers[Index].mData, Bytes);
            }
            dispatch_async(Active->Queue, ^
            {
                @autoreleasepool { ConvertCapture(Active, Copy); }
                Active->PendingCaptureBuffers.fetch_sub(1);
            });
        }];
        Session->TapInstalled = true;
        [Session->Engine prepare];
        if (![Session->Engine startAndReturnError:&Error])
        {
            Fail(Session, TEXT("Microphone audio could not start. Check your audio device and try again."));
            return;
        }
        [Session->Player play];
        NSNotificationCenter* Notifications = [NSNotificationCenter defaultCenter];
        Session->ConfigurationObserver = [Notifications addObserverForName:AVAudioEngineConfigurationChangeNotification
            object:Session->Engine queue:nil usingBlock:^(NSNotification*)
        {
            if (const FSession Active = WeakSession.lock())
                dispatch_async(Active->Queue, ^
                {
                    Fail(Active, TEXT("The audio device changed. Reconnect voice to use the new device."));
                });
        }];
#if PLATFORM_IOS
        Session->InterruptionObserver = [Notifications addObserverForName:AVAudioSessionInterruptionNotification
            object:AudioSession queue:nil usingBlock:^(NSNotification* Notification)
        {
            NSNumber* Type = Notification.userInfo[AVAudioSessionInterruptionTypeKey];
            if (Type.unsignedIntegerValue != AVAudioSessionInterruptionTypeBegan) return;
            if (const FSession Active = WeakSession.lock())
                dispatch_async(Active->Queue, ^
                {
                    Fail(Active, TEXT("Voice was interrupted by another audio session. Reconnect when ready."));
                });
        }];
#endif
        EmitState(Session, ECinderVoiceAudioState::Listening, TEXT("Microphone ready"));
    }
}

class FAppleCinderVoiceAudio final : public FCinderVoiceAudio
{
public:
    virtual ~FAppleCinderVoiceAudio() override { Stop(); }

    virtual void Start(FPCMCallback OnPCM, FStateCallback OnState) override
    {
        Stop();
        const FSession Session = std::make_shared<FVoiceSession>();
        Session->OnPCM = MoveTemp(OnPCM);
        Session->OnState = MoveTemp(OnState);
        Current = Session;
        EmitState(Session, ECinderVoiceAudioState::Starting, TEXT("Requesting microphone access"));
        dispatch_async(dispatch_get_main_queue(), ^
        {
            if (Session->Cancelled.load()) return;
            NSString* Usage = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"NSMicrophoneUsageDescription"];
            if (![Usage isKindOfClass:[NSString class]] || Usage.length == 0)
            {
                dispatch_async(Session->Queue, ^
                {
                    Fail(Session, TEXT("This build is missing its microphone permission description."));
                });
                return;
            }
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL Granted)
            {
                dispatch_async(Session->Queue, ^
                {
                    if (Session->Cancelled.load()) return;
                    if (!Granted)
                        Fail(Session, TEXT("Microphone access was denied. Enable it for Cinderline in system settings."));
                    else
                        BeginAudio(Session);
                });
            }];
        });
    }

    virtual void SetMuted(bool Muted) override
    {
        const FSession Session = Current;
        if (!Session || Session->Cancelled.load()) return;
        const uint64 Revision = Session->MuteRevision.fetch_add(1) + 1;
        // Block network delivery immediately; unmute waits for the native input and converter reset.
        Session->Muted.store(true);
        dispatch_async(Session->Queue, ^
        {
            if (Session->Cancelled.load() || Session->MuteRevision.load() != Revision) return;
            [Session->InputConverter reset];
            Session->Engine.inputNode.voiceProcessingInputMuted = Muted;
            Session->Muted.store(Muted);
        });
    }

    virtual void QueuePlayback(TArray<uint8> PCM) override
    {
        const FSession Session = Current;
        if (!Session || Session->Cancelled.load() || PCM.IsEmpty()) return;
        if ((PCM.Num() & 1) != 0 || PCM.Num() > MaxPlaybackChunkBytes)
        {
            dispatch_async(Session->Queue, ^ { Fail(Session, TEXT("The voice service sent invalid audio.")); });
            return;
        }
        const uint32 Frames = static_cast<uint32>(PCM.Num() / 2);
        const auto Budget = Session->Budget;
        uint32 Queued = Budget->Frames.load();
        do
        {
            if (Queued + Frames > MaxPlaybackFrames)
            {
                dispatch_async(Session->Queue, ^
                {
                    Fail(Session, TEXT("Voice playback fell behind. Reconnect voice to continue."));
                });
                return;
            }
        } while (!Budget->Frames.compare_exchange_weak(Queued, Queued + Frames));
        const uint64 Epoch = Session->PlaybackEpoch.load();
        // The owned payload bounds both queued dispatch work and scheduled player buffers.
        const auto Payload = std::make_shared<TArray<uint8>>(MoveTemp(PCM));
        dispatch_async(Session->Queue, ^
        {
            @autoreleasepool
            {
                if (Session->Cancelled.load() || Session->PlaybackEpoch.load() != Epoch)
                {
                    Budget->Frames.fetch_sub(Frames);
                    return;
                }
                if (!Session->Player || !Session->WireFormat)
                {
                    Budget->Frames.fetch_sub(Frames);
                    return;
                }
                AVAudioPCMBuffer* Buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:Session->WireFormat
                    frameCapacity:Frames];
                if (!Buffer || !Buffer.floatChannelData)
                {
                    Budget->Frames.fetch_sub(Frames);
                    Fail(Session, TEXT("Voice playback memory is unavailable. Reconnect voice and try again."));
                    return;
                }
                Buffer.frameLength = Frames;
                float* Samples = Buffer.floatChannelData[0];
                for (uint32 Index = 0; Index < Frames; ++Index)
                {
                    const uint16 Bits = static_cast<uint16>((*Payload)[Index * 2]) |
                        (static_cast<uint16>((*Payload)[Index * 2 + 1]) << 8);
                    Samples[Index] = static_cast<int16>(Bits) / 32768.0f;
                }
                [Session->Player scheduleBuffer:Buffer completionCallbackType:AVAudioPlayerNodeCompletionDataPlayedBack
                    completionHandler:^(AVAudioPlayerNodeCompletionCallbackType)
                {
                    // A buffer always releases its own generation's reservation, even after a clear.
                    Budget->Frames.fetch_sub(Frames);
                }];
                if (!Session->Player.isPlaying) [Session->Player play];
            }
        });
    }

    virtual void ClearPlayback() override
    {
        const FSession Session = Current;
        if (!Session || Session->Cancelled.load()) return;
        Session->PlaybackEpoch.fetch_add(1);
        // Game-thread producers reserve from the new epoch immediately. Reset before accepting the next packet.
        Session->Budget = std::make_shared<FPlaybackBudget>();
        dispatch_async(Session->Queue, ^
        {
            if (!Session->Cancelled.load())
            {
                [Session->Player stop];
                [Session->Player play];
            }
        });
    }

    virtual void Stop() override
    {
        const FSession Session = MoveTemp(Current);
        if (!Session) return;
        const bool WasCancelled = Session->Cancelled.exchange(true);
        dispatch_async(Session->Queue, ^
        {
            TearDown(Session);
            if (!WasCancelled) EmitState(Session, ECinderVoiceAudioState::Stopped, TEXT("Voice ended"), true);
        });
    }

private:
    FSession Current;
};
}

TUniquePtr<FCinderVoiceAudio> CreateCinderAppleVoiceAudio()
{
    return MakeUnique<FAppleCinderVoiceAudio>();
}

#endif
