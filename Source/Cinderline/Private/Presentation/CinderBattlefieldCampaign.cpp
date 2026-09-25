#include "Presentation/CinderBattlefield.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Presentation/CinderPlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderCampaign, Log, All);

bool ACinderBattlefield::CanPersistCampaign() const
{
    // Automation and development captures never touch a human player's profile.
    return !FApp::IsUnattended() && !GIsAutomationTesting && GetWorld() && GetWorld()->IsGameWorld();
}

void ACinderBattlefield::LoadCampaignProgress()
{
    if (!CanPersistCampaign()) return;
    CampaignStorage->LoadProgress(CampaignRecord, CampaignStorageMessage);
    CampaignStorage->RefreshDiskCheckpointCandidate();
    bCampaignCheckpointAvailable = CampaignStorage->HasDiskCheckpointCandidate();
}

bool ACinderBattlefield::StartCampaignMission(int32 Mission)
{
    if (bOnlineMatch) return false;
    cinder::Simulation CandidateSim;
    FCinderCampaign CandidateDirector;
    if (!CandidateDirector.InitializeMission(CandidateSim, Mission))
    {
        CampaignStorageMessage = TEXT("This mission could not be prepared. Your current match is kept.");
        return false;
    }
    *Simulation = MoveTemp(CandidateSim);
    ResetPresentation();
    CampaignDirector = MoveTemp(CandidateDirector);
    CurrentMap = Simulation->config().map;
    SetActorTickEnabled(true);
    bMenu = bPaused = false;
    CampaignRecord.LastPlayedMission = Mission;
    CampaignStorageMessage.Empty();
    SettleCampaignBoundary();
    RenderState();
    UE_LOG(LogCinderCampaign, Display, TEXT("CINDERLINE_CAMPAIGN start mission=%d phase=%d tick=%llu"),
        Mission, CampaignDirector.Phase(), Simulation->tick());
    return true;
}

bool ACinderBattlefield::ResumeCampaignCheckpoint()
{
    if (bOnlineMatch || !bCampaignCheckpointAvailable) return false;
    cinder::Simulation CandidateSim;
    FCinderCampaign CandidateDirector;
    FString Message;
    bool bRestored = CampaignStorage->RestoreMemoryCheckpoint(CandidateSim, CandidateDirector, Message);
    if (!bRestored && CanPersistCampaign())
        bRestored = CampaignStorage->LoadCheckpoint(CandidateSim, CandidateDirector, Message) == ECinderCampaignLoadResult::Loaded;
    if (!bRestored)
    {
        CampaignStorageMessage = Message.IsEmpty()
            ? TEXT("No valid checkpoint is available. Restart the mission.") : Message;
        bCampaignCheckpointAvailable = false;
        return false;
    }
    *Simulation = MoveTemp(CandidateSim);
    ResetPresentation();
    CampaignDirector = MoveTemp(CandidateDirector);
    SavedCampaignBoundary = CampaignDirector.CheckpointSerial();
    CurrentMap = Simulation->config().map;
    SetActorTickEnabled(true);
    bMenu = bPaused = false;
    CampaignStorageMessage = Message;
    RenderState();
    UE_LOG(LogCinderCampaign, Display, TEXT("CINDERLINE_CAMPAIGN restored mission=%d phase=%d tick=%llu"),
        CampaignDirector.MissionIndex(), CampaignDirector.Phase(), Simulation->tick());
    return true;
}

void ACinderBattlefield::GuidanceCameraInput()
{
    if (bMenu || bPaused || bOnlineMatch || IsMatchOver()) return;
    Training.CameraInput();
    CampaignDirector.CameraInput();
}

void ACinderBattlefield::RequestCampaignHint()
{
    if (bMenu || bPaused || bOnlineMatch || !CampaignDirector.IsRunning()) return;
    CampaignDirector.RequestHint();
    // Preserve assistance on the already-saved boundary without capturing this
    // arbitrary mid-objective simulation state. Retry must not erase hint use.
    CampaignStorage->SaveAttemptMetadata(CampaignDirector, CampaignStorageMessage, CanPersistCampaign());
}

void ACinderBattlefield::ObserveCampaign()
{
    if (!CampaignDirector.IsActive() || bMenu || bPaused || bOnlineMatch) return;
    std::vector<cinder::Id> Selection;
    if (GetWorld())
        for (TActorIterator<ACinderPlayerController> It(GetWorld()); It; ++It)
            if (It->Battlefield() == this) { Selection = It->Selection(); break; }
    const int32 PreviousPhase = CampaignDirector.Phase();
    if (CampaignDirector.IsRunning()) CampaignDirector.Observe(*Simulation, Selection);
    if (PreviousPhase != CampaignDirector.Phase())
        UE_LOG(LogCinderCampaign, Display, TEXT("CINDERLINE_CAMPAIGN phase mission=%d phase=%d tick=%llu"),
            CampaignDirector.MissionIndex(), CampaignDirector.Phase(), Simulation->tick());
    SettleCampaignBoundary();
}

void ACinderBattlefield::SettleCampaignBoundary()
{
    if (CampaignDirector.Outcome() == ECinderCampaignOutcome::Victory)
    {
        if (bCampaignVictoryRecorded) return;
        // Award locally even if storage is unavailable; a failed write remains visible.
        CampaignRecord.Complete(CampaignDirector.MissionIndex(), CampaignDirector.WasAssisted(),
            CampaignDirector.OptionalOutcomes(), Simulation->tick());
        if (CanPersistCampaign())
        {
            if (CampaignStorage->SaveProgress(CampaignRecord, CampaignStorageMessage))
                CampaignStorage->ClearCheckpoint();
        }
        bCampaignCheckpointAvailable = false;
        bCampaignVictoryRecorded = true;
        UE_LOG(LogCinderCampaign, Display, TEXT("CINDERLINE_CAMPAIGN victory mission=%d assisted=%d tick=%llu"),
            CampaignDirector.MissionIndex(), CampaignDirector.WasAssisted(), Simulation->tick());
        return;
    }
    if (CampaignDirector.IsTerminal()) return; // Keep the prior running boundary for Retry.
    if (!CampaignDirector.IsActive() || !CampaignDirector.IsCheckpointSettled()
        || SavedCampaignBoundary == CampaignDirector.CheckpointSerial()) return;
    const bool bSaved = CampaignStorage->SaveCheckpoint(*Simulation, CampaignDirector,
        CampaignStorageMessage, CanPersistCampaign());
    bCampaignCheckpointAvailable = CampaignStorage->HasMemoryCheckpoint() || CampaignStorage->HasDiskCheckpointCandidate();
    // Do not retry a failing filesystem on every frame. The next settled boundary
    // can retry, and the validated memory copy remains available in this session.
    SavedCampaignBoundary = CampaignDirector.CheckpointSerial();
    UE_LOG(LogCinderCampaign, Display, TEXT("CINDERLINE_CAMPAIGN checkpoint mission=%d phase=%d serial=%u saved=%d persistent=%d"),
        CampaignDirector.MissionIndex(), CampaignDirector.Phase(), SavedCampaignBoundary, bSaved, CanPersistCampaign());
}
