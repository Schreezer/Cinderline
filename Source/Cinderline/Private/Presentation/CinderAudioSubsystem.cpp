#include "Presentation/CinderAudioSubsystem.h"

#include "AudioDeviceHandle.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderAudio, Log, All);

namespace
{
struct FCinderCueSettings
{
    const TCHAR* AssetName;
    double MinimumInterval;
    float MixGain;
};

constexpr FCinderCueSettings CueSettings[] = {
    {TEXT("UI_Click"),       0.045, 0.40f},
    {TEXT("Order_Ack"),      0.120, 0.50f},
    {TEXT("Order_Invalid"),  0.250, 0.50f},
    {TEXT("Unit_Ready"),     0.200, 0.60f},
    {TEXT("Building_Ready"), 0.500, 0.65f},
    {TEXT("Weapon_Pulse"),   0.070, 0.16f},
    {TEXT("Impact"),         0.100, 0.20f},
    {TEXT("Explosion"),      0.250, 0.30f}
};
static_assert(UE_ARRAY_COUNT(CueSettings) == 8);

FAutoConsoleCommandWithWorld AudioStatusCommand(
    TEXT("cinder.audio"),
    TEXT("Report optional cue cache and playback-request counters; does not play audio."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
        UCinderAudioSubsystem* Audio = Instance ? Instance->GetSubsystem<UCinderAudioSubsystem>() : nullptr;
        if (Audio)
        {
            Audio->LogStatus();
        }
        else
        {
            UE_LOG(LogCinderAudio, Display, TEXT("CinderAudio: no game-instance audio subsystem in this world."));
        }
    }));
}

void UCinderAudioSubsystem::Play(const UObject* WorldContextObject, ECinderCue Cue, float Volume)
{
    // ReturnNull avoids the diagnostic emitted by GameplayStatics for test worlds
    // or optional contexts. World-only automation fixtures may have no instance.
    if (!IsInGameThread() || !GEngine || !IsValid(WorldContextObject))
    {
        return;
    }
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
    UCinderAudioSubsystem* Audio = Instance ? Instance->GetSubsystem<UCinderAudioSubsystem>() : nullptr;
    if (Audio)
    {
        Audio->PlayInWorld(World, Cue, Volume);
    }
}

void UCinderAudioSubsystem::PlayInWorld(UWorld* World, ECinderCue Cue, float Volume)
{
    ++RequestedCount;
    const int32 Index = static_cast<int32>(Cue);
    if (!World || !GEngine || IsRunningCommandlet() || !GEngine->UseSound() ||
        !World->AllowAudioPlayback() || World->IsNetMode(NM_DedicatedServer) ||
        !World->GetAudioDevice().IsValid() || !FMath::IsFinite(Volume) || Volume <= 0.0f ||
        Index < 0 || Index >= UE_ARRAY_COUNT(CueSettings))
    {
        ++UnavailableCount;
        return;
    }

    const FCinderCueSettings& Settings = CueSettings[Index];
    const double Now = FPlatformTime::Seconds();
    if (const double* Previous = LastPlaybackSeconds.Find(Cue))
    {
        if (Now - *Previous < Settings.MinimumInterval)
        {
            ++ThrottledCount;
            return;
        }
    }

    USoundBase* Sound = ResolveSound(Cue, Settings.AssetName);
    if (!Sound)
    {
        ++MissingCount;
        return;
    }

    LastPlaybackSeconds.Add(Cue, Now);
    UGameplayStatics::PlaySound2D(World, Sound, Settings.MixGain * FMath::Clamp(Volume, 0.0f, 1.0f));
    // This counts a valid submission to PlaySound2D, not audible output.
    ++PlayedCount;
}

USoundBase* UCinderAudioSubsystem::ResolveSound(ECinderCue Cue, const TCHAR* AssetName)
{
    if (const TObjectPtr<USoundBase>* Existing = CachedSounds.Find(Cue))
    {
        return Existing->Get();
    }
    if (AttemptedLoads.Contains(Cue))
    {
        return nullptr;
    }
    AttemptedLoads.Add(Cue);

    const FString PackagePath = FString::Printf(TEXT("/Game/Art/Audio/%s"), AssetName);
    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackagePath, AssetName);
    USoundBase* Sound = FindObject<USoundBase>(nullptr, *ObjectPath);
    if (!Sound && FPackageName::DoesPackageExist(PackagePath))
    {
        Sound = Cast<USoundBase>(StaticLoadObject(USoundBase::StaticClass(), nullptr,
            *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet));
    }
    if (Sound)
    {
        CachedSounds.Add(Cue, Sound);
    }
    return Sound;
}

void UCinderAudioSubsystem::LogStatus() const
{
    UE_LOG(LogCinderAudio, Display,
        TEXT("CinderAudio: loaded=%d/8 requested=%llu played=%llu throttled=%llu missing=%llu unavailable=%llu; played counts PlaySound2D submissions, not listening proof."),
        CachedSounds.Num(), RequestedCount, PlayedCount, ThrottledCount, MissingCount, UnavailableCount);
}

void UCinderAudioSubsystem::Deinitialize()
{
    CachedSounds.Reset();
    AttemptedLoads.Reset();
    LastPlaybackSeconds.Reset();
    Super::Deinitialize();
}
