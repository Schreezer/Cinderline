#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CinderAudioSubsystem.generated.h"

class USoundBase;
class UWorld;

UENUM(BlueprintType)
enum class ECinderCue : uint8
{
    UI_Click,
    Order_Ack,
    Order_Invalid,
    Unit_Ready,
    Building_Ready,
    Weapon_Pulse,
    Impact,
    Explosion
};

struct FCinderCueDiagnostics
{
    uint64 Requested = 0;
    uint64 Submitted = 0;
    uint64 Throttled = 0;
    uint64 Missing = 0;
    uint64 Unavailable = 0;
};

/** Optional presentation audio. Never reads or mutates authoritative match state. */
UCLASS()
class CINDERLINE_API UCinderAudioSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /** Volume is a 0..1 caller multiplier, applied after the cue's conservative mix gain. */
    UFUNCTION(BlueprintCallable, Category="Cinderline|Audio", meta=(WorldContext="WorldContextObject"))
    static void Play(const UObject* WorldContextObject, ECinderCue Cue, float Volume = 1.0f);

    /** Explicit diagnostics only; also available through the cinder.audio console command. */
    void LogStatus() const;
    FCinderCueDiagnostics CueDiagnostics(ECinderCue Cue) const;
    /** Valid PlaySound2D calls, not a guarantee of audible output. */
    uint64 PlaybackSubmissionCount() const { return PlayedCount; }

    virtual void Deinitialize() override;

private:
    void PlayInWorld(UWorld* World, ECinderCue Cue, float Volume);
    USoundBase* ResolveSound(ECinderCue Cue, const TCHAR* AssetName);

    UPROPERTY(Transient)
    TMap<ECinderCue, TObjectPtr<USoundBase>> CachedSounds;

    TSet<ECinderCue> AttemptedLoads;
    TMap<ECinderCue, double> LastPlaybackSeconds;
    uint64 RequestedCount = 0;
    uint64 PlayedCount = 0;
    uint64 ThrottledCount = 0;
    uint64 MissingCount = 0;
    uint64 UnavailableCount = 0;
    FCinderCueDiagnostics PerCueDiagnostics[8];
};
