#pragma once

#include "CoreMinimal.h"
#include "Presentation/CinderCampaign.h"

#include <array>

namespace cinder { class Simulation; }

/** Monotonic, device-local campaign accomplishments. Every mission remains playable. */
struct FCinderCampaignProgress
{
    static constexpr uint32 SchemaVersion = 1;
    static constexpr int32 MissionCount = 6;

    uint32 ContentVersion = FCinderCampaign::ContentVersion;
    uint32 CompletedMask = 0;
    uint32 AssistedMask = 0;
    uint32 UnassistedMask = 0;
    std::array<uint32, MissionCount> MasteryMasks{};
    std::array<uint64, MissionCount> BestCompletionTicks{};
    int32 LastPlayedMission = 0;

    bool IsComplete(int32 Mission) const;
    int32 RecommendedMission() const;
    bool Complete(int32 Mission, bool bAssisted, uint32 MasteryMask, uint64 CompletionTicks);
};

enum class ECinderCampaignLoadResult : uint8
{
    Loaded,
    NotFound,
    Unsupported,
    Corrupt
};

/**
 * Owns campaign-only progress and objective-boundary checkpoints.
 * The injected root makes tests independent of the player's Saved directory.
 * Checkpoints write simulation format 15 and also restore matching format 14
 * generations after validating the director metadata, tick and gameplay hash.
 */
class CINDERLINE_API FCinderCampaignSave
{
public:
    explicit FCinderCampaignSave(FString RootDirectory = DefaultRootDirectory());
    ~FCinderCampaignSave();

    FCinderCampaignSave(const FCinderCampaignSave&) = delete;
    FCinderCampaignSave& operator=(const FCinderCampaignSave&) = delete;

    static FString DefaultRootDirectory();
    const FString& RootDirectory() const { return Root; }

    ECinderCampaignLoadResult LoadProgress(FCinderCampaignProgress& OutProgress, FString& OutMessage) const;
    bool SaveProgress(const FCinderCampaignProgress& Progress, FString& OutError) const;
    bool RecordVictory(int32 Mission, bool bAssisted, uint32 MasteryMask,
        uint64 CompletionTicks, FCinderCampaignProgress& InOutProgress, FString& OutError) const;

    /** Always refreshes the in-memory checkpoint. Disk persistence is explicit. */
    bool SaveCheckpoint(const cinder::Simulation& Sim, const FCinderCampaign& Campaign,
        FString& OutError, bool bPersist = true);
    /** Merge attempt-wide assistance into the stored boundary without moving its simulation time. */
    bool SaveAttemptMetadata(const FCinderCampaign& CurrentCampaign,
        FString& OutError, bool bPersist = true);
    ECinderCampaignLoadResult LoadCheckpoint(cinder::Simulation& OutSim,
        FCinderCampaign& OutCampaign, FString& OutMessage);
    bool RestoreMemoryCheckpoint(cinder::Simulation& OutSim,
        FCinderCampaign& OutCampaign, FString& OutError) const;
    bool HasMemoryCheckpoint() const { return MemorySimulation.IsValid() && MemoryCampaign.IsValid(); }

    /** Cached file-existence hint for menus; LoadCheckpoint performs full validation. */
    bool HasDiskCheckpointCandidate() const { return bHasDiskCandidate; }
    void RefreshDiskCheckpointCandidate();

    /** Removes campaign checkpoint generations only. Progress is retained. */
    void ClearCheckpoint();

private:
    FString Root;
    TUniquePtr<cinder::Simulation> MemorySimulation;
    TUniquePtr<FCinderCampaign> MemoryCampaign;
    bool bHasDiskCandidate = false;
};
