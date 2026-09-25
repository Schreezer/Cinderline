#include "CinderVoiceAudio.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, CinderVoiceAudio)

#if PLATFORM_MAC || PLATFORM_IOS
TUniquePtr<FCinderVoiceAudio> CreateCinderAppleVoiceAudio();
#else
namespace
{
class FUnsupportedCinderVoiceAudio final : public FCinderVoiceAudio
{
public:
    virtual void Start(FPCMCallback, FStateCallback OnState) override
    {
        if (OnState)
        {
            OnState(ECinderVoiceAudioState::Error,
                TEXT("Voice microphone support is currently available on Mac and iOS."));
        }
    }
    virtual void SetMuted(bool) override {}
    virtual void QueuePlayback(TArray<uint8>) override {}
    virtual void ClearPlayback() override {}
    virtual void Stop() override {}
};
}
#endif

TUniquePtr<FCinderVoiceAudio> FCinderVoiceAudio::Create()
{
#if PLATFORM_MAC || PLATFORM_IOS
    return CreateCinderAppleVoiceAudio();
#else
    return MakeUnique<FUnsupportedCinderVoiceAudio>();
#endif
}
