#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Presentation/CinderCampaign.h"
#include "Presentation/CinderCampaignSave.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sim/Simulation.h"

namespace
{
struct FTemporaryCampaignDirectory
{
    FString Parent = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(),
        TEXT("Automation/CinderlineCampaignSave"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    FString Campaign = FPaths::Combine(Parent, TEXT("Campaign"));
    FString Skirmish = FPaths::Combine(Parent, TEXT("Matches/skirmish.cinder"));

    FTemporaryCampaignDirectory()
    {
        IFileManager::Get().MakeDirectory(*Campaign, true);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Skirmish), true);
    }

    ~FTemporaryCampaignDirectory()
    {
        IFileManager::Get().DeleteDirectory(*Parent, false, true);
    }
};

bool ReplaceJsonInteger(const FString& Filename, const FString& Field, int32 Value)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Filename)) return false;
    const FString Prefix = FString::Printf(TEXT("\"%s\":"), *Field);
    const int32 FieldAt = Text.Find(Prefix);
    if (FieldAt == INDEX_NONE) return false;
    int32 Start = FieldAt + Prefix.Len();
    while (Start < Text.Len() && FChar::IsWhitespace(Text[Start])) ++Start;
    int32 End = Start;
    while (End < Text.Len() && (FChar::IsDigit(Text[End]) || Text[End] == TEXT('-'))) ++End;
    if (End == Start) return false;
    Text = Text.Left(Start) + FString::FromInt(Value) + Text.Mid(End);
    return FFileHelper::SaveStringToFile(Text, *Filename);
}

bool ReadTestJson(const FString& Filename, TSharedPtr<FJsonObject>& Out)
{
    FString Text;
    return FFileHelper::LoadFileToString(Text, *Filename) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
}

bool HasJsonInteger(const FString& Filename, const TCHAR* Field, int32 Expected)
{
    TSharedPtr<FJsonObject> Object;
    double Value = 0;
    return ReadTestJson(Filename, Object) && Object->TryGetNumberField(Field, Value) && Value == Expected;
}

bool ReplaceJsonString(const FString& Filename, const TCHAR* Field, const FString& Value)
{
    TSharedPtr<FJsonObject> Object;
    if (!ReadTestJson(Filename, Object)) return false;
    Object->SetStringField(Field, Value);
    FString Text;
    return FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)) &&
        FFileHelper::SaveStringToFile(Text, *Filename);
}

bool DowngradeRevisionZeroSimulationToFourteen(const FString& Filename)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Filename) || !Text.StartsWith(TEXT("CINDERLINE 15\n"))) return false;
    const int32 HeaderEnd = Text.Find(TEXT("\n"));
    const int32 ConfigEnd = Text.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, HeaderEnd + 1);
    if (ConfigEnd == INDEX_NONE) return false;
    TArray<FString> Fields;
    Text.Mid(HeaderEnd + 1, ConfigEnd - HeaderEnd - 1).ParseIntoArrayWS(Fields);
    if (Fields.Num() != 7 || Fields.Last() != TEXT("0")) return false;
    Fields.Pop();
    const FString Legacy = TEXT("CINDERLINE 14\n") + FString::Join(Fields, TEXT(" ")) + Text.Mid(ConfigEnd);
    return FFileHelper::SaveStringToFile(Legacy, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCampaignProgressPersistenceTest,
    "Cinderline.Campaign.ProgressPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCampaignProgressPersistenceTest::RunTest(const FString& Parameters)
{
    FTemporaryCampaignDirectory Files;
    FCinderCampaignSave Save(Files.Campaign);
    FCinderCampaignProgress Progress;

    TestEqual(TEXT("Fresh progress recommends the first mission"), Progress.RecommendedMission(), 0);
    TestFalse(TEXT("Invalid mission completion is rejected"), Progress.Complete(-1, false, 0, 10));
    TestTrue(TEXT("Out-of-order practice completion is accepted"), Progress.Complete(3, true, 0x1, 900));
    TestTrue(TEXT("Completion is recorded"), Progress.IsComplete(3));
    TestEqual(TEXT("Recommendation remains the first unfinished mission"), Progress.RecommendedMission(), 0);
    TestTrue(TEXT("A later cleaner replay improves monotonic mastery"), Progress.Complete(3, false, 0x4, 700));
    TestTrue(TEXT("Assisted history remains recorded"), (Progress.AssistedMask & (1u << 3)) != 0);
    TestTrue(TEXT("Unassisted mastery remains recorded"), (Progress.UnassistedMask & (1u << 3)) != 0);
    TestEqual(TEXT("Mastery marks accumulate"), Progress.MasteryMasks[3], uint32(0x5));
    TestEqual(TEXT("Best completion time improves"), Progress.BestCompletionTicks[3], uint64(700));

    FString Message;
    if (!TestTrue(TEXT("Initial progress saves"), Save.SaveProgress(Progress, Message))) return false;
    FCinderCampaignProgress Newer = Progress;
    Newer.Complete(0, false, 0x2, 300);
    if (!TestTrue(TEXT("A second progress revision saves with backup"), Save.SaveProgress(Newer, Message))) return false;

    FCinderCampaignProgress Loaded;
    TestTrue(TEXT("Current progress loads"), Save.LoadProgress(Loaded, Message) == ECinderCampaignLoadResult::Loaded);
    TestTrue(TEXT("Current revision includes the newer mission"), Loaded.IsComplete(0));

    const FString Current = FPaths::Combine(Files.Campaign, TEXT("progress.json"));
    if (!TestTrue(TEXT("Corruption fixture overwrites only campaign progress"),
        FFileHelper::SaveStringToFile(TEXT("not-json"), *Current))) return false;
    TestTrue(TEXT("Corrupt current progress falls back to the previous valid revision"),
        Save.LoadProgress(Loaded, Message) == ECinderCampaignLoadResult::Loaded && Loaded.IsComplete(3) && !Loaded.IsComplete(0));
    TestTrue(TEXT("Fallback is disclosed"), !Message.IsEmpty());

    if (!TestTrue(TEXT("Valid progress can be written again"), Save.SaveProgress(Newer, Message))) return false;
    if (!TestTrue(TEXT("Repeated-corruption fixture overwrites only the repaired current file"),
        FFileHelper::SaveStringToFile(TEXT("not-json-again"), *Current))) return false;
    TestTrue(TEXT("Repairing a corrupt current file preserved the known-good backup"),
        Save.LoadProgress(Loaded, Message) == ECinderCampaignLoadResult::Loaded && Loaded.IsComplete(3) && !Loaded.IsComplete(0));
    if (!TestTrue(TEXT("Progress repairs a second corrupt current file"), Save.SaveProgress(Newer, Message))) return false;
    if (!TestTrue(TEXT("Unsupported-version fixture edits the campaign schema only"),
        ReplaceJsonInteger(Current, TEXT("schema"), 999))) return false;
    TestTrue(TEXT("Unsupported current progress is not silently rolled back"),
        Save.LoadProgress(Loaded, Message) == ECinderCampaignLoadResult::Unsupported);

    FString SkirmishContents;
    TestFalse(TEXT("Progress never creates the skirmish save"), FFileHelper::LoadFileToString(SkirmishContents, *Files.Skirmish));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCampaignCheckpointPersistenceTest,
    "Cinderline.Campaign.CheckpointPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCampaignCheckpointPersistenceTest::RunTest(const FString& Parameters)
{
    FTemporaryCampaignDirectory Files;
    const FString SkirmishSentinel = TEXT("SKIRMISH-SENTINEL");
    if (!TestTrue(TEXT("Isolation fixture writes its sibling skirmish sentinel"),
        FFileHelper::SaveStringToFile(SkirmishSentinel, *Files.Skirmish))) return false;

    FCinderCampaignSave Save(Files.Campaign);
    cinder::Simulation Sim;
    FCinderCampaign Campaign;
    if (!TestTrue(TEXT("A real first-mission director initializes transactionally"), Campaign.InitializeMission(Sim, 0))) return false;
    TestTrue(TEXT("Mission entry is a settled checkpoint"), Campaign.IsCheckpointSettled() && Campaign.CheckpointSerial() > 0);

    FString Message;
    if (!TestTrue(TEXT("Memory-only save captures Retry state"), Save.SaveCheckpoint(Sim, Campaign, Message, false))) return false;
    TestTrue(TEXT("Memory-only save does not advertise disk resume"),
        Save.HasMemoryCheckpoint() && !Save.HasDiskCheckpointCandidate());
    cinder::Simulation MemorySim;
    FCinderCampaign MemoryCampaign;
    TestTrue(TEXT("Memory Retry restores matching authoritative state"),
        Save.RestoreMemoryCheckpoint(MemorySim, MemoryCampaign, Message) && MemorySim.stateHash() == Sim.stateHash() &&
        MemoryCampaign.MissionIndex() == Campaign.MissionIndex() && MemoryCampaign.Phase() == Campaign.Phase());

    if (!TestTrue(TEXT("First settled generation persists"), Save.SaveCheckpoint(Sim, Campaign, Message))) return false;
    const uint64 FirstHash = Sim.stateHash();
    TestTrue(TEXT("Menu cache sees a disk checkpoint candidate"), Save.HasDiskCheckpointCandidate());

    Campaign.RequestHint();
    if (!TestTrue(TEXT("Hint metadata recommits the existing boundary"),
        Campaign.WasAssisted() && Save.SaveAttemptMetadata(Campaign, Message))) return false;
    TestTrue(TEXT("Memory Retry keeps Assisted without moving the boundary simulation"),
        Save.RestoreMemoryCheckpoint(MemorySim, MemoryCampaign, Message) && MemoryCampaign.WasAssisted() &&
        MemorySim.stateHash() == FirstHash);
    cinder::Simulation LoadedSim;
    FCinderCampaign LoadedCampaign;
    FCinderCampaignSave RelaunchedSave(Files.Campaign);
    TestTrue(TEXT("Disk resume also keeps Assisted at the same boundary"),
        RelaunchedSave.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Loaded &&
        LoadedCampaign.WasAssisted() && LoadedSim.stateHash() == FirstHash);

    // A torn manifest must not make the already validated generation unreachable.
    const FString Manifest = FPaths::Combine(Files.Campaign, TEXT("checkpoint-manifest.json"));
    if (!TestTrue(TEXT("Torn-manifest fixture changes only campaign metadata"),
        FFileHelper::SaveStringToFile(TEXT("{\"schema\":1,\"generation\":1,\"commit\":\"999\"}"), *Manifest))) return false;
    TestTrue(TEXT("Missing referenced generation falls back to the validated generation"),
        Save.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Loaded &&
        LoadedSim.stateHash() == FirstHash && !Message.IsEmpty());

    // Commit a newer A generation, then corrupt it. B remains the rollback point.
    Sim.update(cinder::Simulation::Step * 2);
    if (!TestTrue(TEXT("Second settled generation persists"), Save.SaveCheckpoint(Sim, Campaign, Message))) return false;
    const FString NewestDirector = FPaths::Combine(Files.Campaign, TEXT("checkpoint-a.json"));
    if (!TestTrue(TEXT("Newest-generation corruption is isolated"),
        FFileHelper::SaveStringToFile(TEXT("truncated"), *NewestDirector))) return false;
    TestTrue(TEXT("Corrupt newest generation restores the previous complete pair"),
        Save.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Loaded &&
        LoadedSim.stateHash() == FirstHash && !Message.IsEmpty());

    FString SkirmishAfter;
    TestTrue(TEXT("Checkpoint writes never touch the skirmish slot"),
        FFileHelper::LoadFileToString(SkirmishAfter, *Files.Skirmish) && SkirmishAfter == SkirmishSentinel);

    cinder::Simulation NextMissionSim;
    FCinderCampaign NextMission;
    if (!TestTrue(TEXT("A different practice mission initializes before replacing the old checkpoint"),
        NextMission.InitializeMission(NextMissionSim, 1))) return false;
    if (!TestTrue(TEXT("A validated new mission replaces the old mission checkpoint set"),
        Save.SaveCheckpoint(NextMissionSim, NextMission, Message))) return false;
    const FString ReplacementDirector = FPaths::Combine(Files.Campaign, TEXT("checkpoint-a.json"));
    if (!TestTrue(TEXT("Replacement corruption fixture damages only the new mission"),
        FFileHelper::SaveStringToFile(TEXT("truncated"), *ReplacementDirector))) return false;
    TestTrue(TEXT("A new mission cannot fall back into an older mission"),
        Save.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Corrupt);

    FCinderCampaignProgress Progress;
    Progress.Complete(0, false, 1, 100);
    if (!TestTrue(TEXT("Progress exists before checkpoint cleanup"), Save.SaveProgress(Progress, Message))) return false;
    Save.ClearCheckpoint();
    TestFalse(TEXT("Checkpoint cleanup clears memory and disk hints"), Save.HasMemoryCheckpoint() || Save.HasDiskCheckpointCandidate());
    FCinderCampaignProgress ProgressAfter;
    TestTrue(TEXT("Checkpoint cleanup preserves campaign progress"),
        Save.LoadProgress(ProgressAfter, Message) == ECinderCampaignLoadResult::Loaded && ProgressAfter.IsComplete(0));

    // An explicitly unsupported active checkpoint is never interpreted as corruption or rolled back.
    if (!TestTrue(TEXT("Fresh checkpoint persists for version handling"), Save.SaveCheckpoint(Sim, Campaign, Message))) return false;
    const FString DirectorA = FPaths::Combine(Files.Campaign, TEXT("checkpoint-a.json"));
    if (!TestTrue(TEXT("Unsupported checkpoint fixture edits only the director schema"),
        ReplaceJsonInteger(DirectorA, TEXT("state_schema"), 999))) return false;
    TestTrue(TEXT("Unsupported checkpoint requests restart instead of loading partial state"),
        Save.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Unsupported);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCampaignCheckpointSimulationVersionTest,
    "Cinderline.Campaign.CheckpointSimulationVersion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCampaignCheckpointSimulationVersionTest::RunTest(const FString& Parameters)
{
    FTemporaryCampaignDirectory Files;
    FCinderCampaignSave Save(Files.Campaign);
    cinder::Simulation Sim;
    FCinderCampaign Campaign;
    if (!TestTrue(TEXT("Version fixture initializes the actual campaign"), Campaign.InitializeMission(Sim, 0))) return false;
    TestEqual(TEXT("Campaign retains original authored encounter geometry"), Sim.config().mapRevision, 0);
    const uint64 Hash = Sim.stateHash();
    const uint32 ContentVersion = Campaign.ExportState().ContentVersion;
    FString Message;
    if (!TestTrue(TEXT("New simulation format persists a campaign checkpoint"), Save.SaveCheckpoint(Sim, Campaign, Message))) return false;

    const FString DirectorA = FPaths::Combine(Files.Campaign, TEXT("checkpoint-a.json"));
    const FString SimulationA = FPaths::Combine(Files.Campaign, TEXT("checkpoint-a.cinder"));
    FString SimulationText;
    TestTrue(TEXT("New checkpoint metadata names simulation format fifteen"),
        HasJsonInteger(DirectorA, TEXT("simulation_save_version"), 15));
    TestTrue(TEXT("New checkpoint simulation has the matching format fifteen header"),
        FFileHelper::LoadFileToString(SimulationText, *SimulationA) && SimulationText.StartsWith(TEXT("CINDERLINE 15\n")));

    cinder::Simulation LoadedSim;
    FCinderCampaign LoadedCampaign;
    TestTrue(TEXT("New campaign checkpoint resumes with the exact saved hash"),
        Save.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Loaded && LoadedSim.stateHash() == Hash);
    if (!TestTrue(TEXT("Mismatch fixture changes only the declared simulation version"),
        ReplaceJsonInteger(DirectorA, TEXT("simulation_save_version"), 14))) return false;
    TestTrue(TEXT("Format fourteen metadata cannot load a format fifteen simulation"),
        Save.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Corrupt && LoadedSim.stateHash() == Hash);
    if (!TestTrue(TEXT("Version metadata can be restored without changing the generation"),
        ReplaceJsonInteger(DirectorA, TEXT("simulation_save_version"), 15))) return false;
    if (!TestTrue(TEXT("Legacy fixture removes the map revision field as well as changing the header"),
        DowngradeRevisionZeroSimulationToFourteen(SimulationA))) return false;
    TestTrue(TEXT("Format fifteen metadata cannot load a format fourteen simulation"),
        Save.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Corrupt && LoadedSim.stateHash() == Hash);
    if (!TestTrue(TEXT("Genuine legacy generation declares its matching format"),
        ReplaceJsonInteger(DirectorA, TEXT("simulation_save_version"), 14))) return false;
    FCinderCampaignSave Relaunched(Files.Campaign);
    TestTrue(TEXT("Format fourteen checkpoint restores original geometry and exact gameplay hash"),
        Relaunched.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Loaded &&
        LoadedSim.config().mapRevision == 0 && LoadedSim.stateHash() == Hash);
    TestEqual(TEXT("Simulation format migration leaves campaign content version unchanged"),
        LoadedCampaign.ExportState().ContentVersion, ContentVersion);

    FString ValidLegacyDirector;
    if (!TestTrue(TEXT("Hash fixture preserves the validated legacy director"),
        FFileHelper::LoadFileToString(ValidLegacyDirector, *DirectorA))) return false;
    if (!TestTrue(TEXT("Legacy mismatch fixture changes only its saved gameplay hash"),
        ReplaceJsonString(DirectorA, TEXT("sim_hash"), TEXT("0")))) return false;
    TestTrue(TEXT("Legacy format compatibility still requires matching simulation checksum"),
        Relaunched.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Corrupt && LoadedSim.stateHash() == Hash);
    if (!TestTrue(TEXT("Legacy director is restored before the upgrade save"),
        FFileHelper::SaveStringToFile(ValidLegacyDirector, *DirectorA))) return false;
    if (!TestTrue(TEXT("Unsupported old format fixture is explicit"),
        ReplaceJsonInteger(DirectorA, TEXT("simulation_save_version"), 13))) return false;
    TestTrue(TEXT("Checkpoint support does not expand to arbitrary old simulation formats"),
        Relaunched.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Unsupported);
    if (!TestTrue(TEXT("Supported legacy director is restored"),
        FFileHelper::SaveStringToFile(ValidLegacyDirector, *DirectorA))) return false;

    if (!TestTrue(TEXT("Restored legacy campaign writes a current checkpoint generation"),
        Relaunched.SaveCheckpoint(LoadedSim, LoadedCampaign, Message))) return false;
    const FString DirectorB = FPaths::Combine(Files.Campaign, TEXT("checkpoint-b.json"));
    const FString SimulationB = FPaths::Combine(Files.Campaign, TEXT("checkpoint-b.cinder"));
    TestTrue(TEXT("Upgraded director declares format fifteen"), HasJsonInteger(DirectorB, TEXT("simulation_save_version"), 15));
    TestTrue(TEXT("Upgraded simulation uses format fifteen with explicit revision zero"),
        FFileHelper::LoadFileToString(SimulationText, *SimulationB) && SimulationText.StartsWith(TEXT("CINDERLINE 15\n")));
    TestTrue(TEXT("Upgraded campaign resumes without changing its boundary hash"),
        Relaunched.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Loaded && LoadedSim.stateHash() == Hash);
    if (!TestTrue(TEXT("Future-version fixture edits the committed generation only"),
        ReplaceJsonInteger(DirectorB, TEXT("simulation_save_version"), 16))) return false;
    TestTrue(TEXT("Unknown future simulation format remains unsupported even with a valid legacy fallback"),
        Relaunched.LoadCheckpoint(LoadedSim, LoadedCampaign, Message) == ECinderCampaignLoadResult::Unsupported && LoadedSim.stateHash() == Hash);
    return !HasAnyErrors();
}

#endif
