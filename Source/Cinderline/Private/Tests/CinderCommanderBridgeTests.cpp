#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCommanderBridge.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderPlayerController.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCommanderBridgeIntegration,
    "Cinderline.Integration.VoiceCommanderBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCommanderBridgeIntegration::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace cinder;
    FTestWorldWrapper Owner;
    if (!Owner.CreateTestWorld(EWorldType::Game)) { Owner.ForwardErrorMessages(this); return false; }
    UWorld* World = Owner.GetTestWorld();
    World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
    auto* Battle = World->SpawnActor<ACinderBattlefield>();
    if (!Owner.BeginPlayInTestWorld()) { Owner.ForwardErrorMessages(this); return false; }
    auto* Controller = World->SpawnActor<ACinderPlayerController>();
    if (!TestNotNull(TEXT("Voice fixture controller"), Controller) || !TestNotNull(TEXT("Voice fixture battlefield"), Battle)) return false;
    Controller->ExecuteAction(TEXT("start"), 0);
    if (!TestTrue(TEXT("Voice fixture gameplay active"), Controller->CommanderGameplayActive())) return false;

    Simulation& Sim = Battle->Sim();
    Config Setup; Setup.ai = false; Setup.mapRevision = 0;
    Sim.reset(Setup);
    Battle->ResetPresentation();
    const Id A = Sim.debugSpawn(Kind::Striker, 0, {800, 800});
    const Id B = Sim.debugSpawn(Kind::Striker, 0, {900, 800});
    const Id Hidden = Sim.debugSpawn(Kind::Kite, 1, {4200, 4200});
    const Id Visible = Sim.debugSpawn(Kind::Lancer, 1, {950, 800});
    Controller->CommanderSelect({A});
    FCinderCommanderBridge Bridge;
    auto Captured = Bridge.Capture(Controller);
    const FString ContextId = Captured->GetStringField(TEXT("contextId"));
    TestFalse(TEXT("Hidden enemy fixture is outside player vision"), Sim.visible(0, Sim.find(Hidden)->pos));
    const auto* PrivateCapture = Bridge.Captures.Find(ContextId);
    if (!TestNotNull(TEXT("Capture retained private candidates"), PrivateCapture)) return false;
    FString VisibleCandidate;
    for (const auto& Target : PrivateCapture->Targets)
    {
        TestNotEqual(TEXT("Hidden enemy never enters provider targets"), Target.Value, Hidden);
        if (Target.Value == Visible) VisibleCandidate = Target.Key;
    }
    TestFalse(TEXT("Visible enemy has a typed candidate"), VisibleCandidate.IsEmpty());
    int32 ExpectedVisibleEnemies = 0;
    for (const Entity& Entity : Sim.entities())
        if (Entity.alive() && Entity.team > 0 && Sim.visible(0, Entity.pos)) ++ExpectedVisibleEnemies;
    TestEqual(TEXT("Summary includes only visible enemy count"),
        Captured->GetObjectField(TEXT("summary"))->GetIntegerField(TEXT("visibleEnemyCount")), ExpectedVisibleEnemies);

    auto Params = [](const TSharedPtr<FJsonObject>& Observation, const FString& Operation,
        const TCHAR* Action = TEXT("hold"), const TCHAR* Queue = TEXT("replace"))
    {
        auto Request = MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("operationId"), Operation);
        Request->SetStringField(TEXT("contextId"), Observation->GetStringField(TEXT("contextId")));
        Request->SetStringField(TEXT("generation"), Observation->GetStringField(TEXT("generation")));
        auto Proposal = MakeShared<FJsonObject>();
        Proposal->SetStringField(TEXT("action"), Action);
        Proposal->SetStringField(TEXT("unitGroup"), TEXT("selected"));
        Proposal->SetStringField(TEXT("queueMode"), Queue);
        Request->SetObjectField(TEXT("proposal"), Proposal);
        return Request;
    };
    Controller->CommanderSelect({B});
    auto Hold = Params(Captured, TEXT("frozen-selection"));
    auto Result = Bridge.Execute(Controller, Hold);
    TestEqual(TEXT("Frozen selected group command accepted"), Result->GetStringField(TEXT("status")), FString(TEXT("accepted")));
    TestTrue(TEXT("Originally captured unit receives hold"), Sim.find(A)->order == Order::Hold);
    TestTrue(TEXT("Later UI selection does not receive that command"), Sim.find(B)->order != Order::Hold);
    const size_t AcceptedCount = Sim.recording().size();
    Bridge.Execute(Controller, Hold);
    TestEqual(TEXT("Duplicate operation produces no second game command"), Sim.recording().size(), AcceptedCount);

    Bridge.Cancel(TEXT("cancel-before-submit"));
    Result = Bridge.Execute(Controller, Params(Captured, TEXT("cancel-before-submit")));
    TestEqual(TEXT("Cancelled operation cannot execute late result"), Result->GetStringField(TEXT("status")), FString(TEXT("cancelled")));
    Bridge.Cancel(TEXT("frozen-selection"));
    TestTrue(TEXT("Cancellation never undoes accepted game order"), Sim.find(A)->order == Order::Hold);
    Result = Bridge.Execute(Controller, Params(Captured, TEXT("illegal-append"), TEXT("hold"), TEXT("append")));
    TestEqual(TEXT("Unsupported appended order rejected"), Result->GetStringField(TEXT("status")), FString(TEXT("rejected")));

    auto Forged = Params(Captured, TEXT("invented-destination"), TEXT("move"));
    Forged->GetObjectField(TEXT("proposal"))->SetStringField(TEXT("destination"), TEXT("invented_coordinates"));
    Result = Bridge.Execute(Controller, Forged);
    TestEqual(TEXT("Model cannot invent a new destination"), Result->GetStringField(TEXT("status")), FString(TEXT("rejected")));

    auto Attack = Params(Captured, TEXT("visibility-changed"), TEXT("attack"));
    Attack->GetObjectField(TEXT("proposal"))->SetStringField(TEXT("target"), VisibleCandidate);
    auto* Enemy = const_cast<Entity*>(Sim.find(Visible));
    Enemy->pos = {4100, 4100};
    Enemy->goal = Enemy->pos;
    Sim.update(Simulation::Step);
    Result = Bridge.Execute(Controller, Attack);
    TestEqual(TEXT("Enemy leaving vision invalidates stale target"), Result->GetStringField(TEXT("status")), FString(TEXT("rejected")));
    const Id NewlyVisible = Sim.debugSpawn(Kind::Scout, 1, {940, 800});
    const auto Refreshed = Bridge.Capture(Controller);
    const auto& RefreshCandidates = Bridge.Captures[Refreshed->GetStringField(TEXT("contextId"))].Targets;
    for (const auto& Target : RefreshCandidates)
    {
        if (Target.Value == NewlyVisible)
            TestNotEqual(TEXT("Fresh target cannot alias the old captured target ID"), Target.Key, VisibleCandidate);
        for (const auto& Previous : Bridge.Captures[ContextId].Targets)
            if (Target.Value == Previous.Value)
                TestEqual(TEXT("The same visible entity keeps its ID across captures"), Target.Key, Previous.Key);
    }

    // An advancing tick does not invalidate an unrelated order on the frozen recipients.
    Result = Bridge.Execute(Controller, Params(Captured, TEXT("later-tick"), TEXT("stop")));
    TestEqual(TEXT("Relevant conditions remain legal across a newer tick"), Result->GetStringField(TEXT("status")), FString(TEXT("accepted")));
    Bridge.Captures[ContextId].CreatedAt = FPlatformTime::Seconds() - 121.0;
    Result = Bridge.Execute(Controller, Params(Captured, TEXT("expired")));
    TestEqual(TEXT("Expired captured selection is rejected"), Result->GetStringField(TEXT("status")), FString(TEXT("rejected")));

    auto BeforeReset = Bridge.Capture(Controller);
    const uint64 BeforeSerial = Battle->MatchGeneration();
    Battle->StartMatch(0);
    TestTrue(TEXT("Restart explicitly advances match generation"), Battle->MatchGeneration() > BeforeSerial);
    Result = Bridge.Execute(Controller, Params(BeforeReset, TEXT("old-match")));
    TestEqual(TEXT("Same-world restart cannot reuse old handles"), Result->GetStringField(TEXT("status")), FString(TEXT("rejected")));
    auto AfterReset = Bridge.Capture(Controller);
    TestNotEqual(TEXT("New match exposes a new opaque generation"),
        BeforeReset->GetStringField(TEXT("generation")), AfterReset->GetStringField(TEXT("generation")));

    // A submitted operation remains unknown when its transport loses the ACK.
    FCinderCommanderBridge::FOperation Pending;
    Pending.Sequence = 37;
    Pending.SubmittedAt = FPlatformTime::Seconds();
    Pending.bPending = true;
    Pending.Receipt = MakeShared<FJsonObject>();
    Bridge.Operations.Add(TEXT("lost-ack"), Pending);
    const auto Receipts = Bridge.Poll(Controller);
    TestEqual(TEXT("Lost acknowledgement emits one terminal receipt"), Receipts.Num(), 1);
    if (Receipts.Num())
        TestEqual(TEXT("Submission without acknowledgement remains uncertain"), Receipts[0]->GetStringField(TEXT("status")), FString(TEXT("uncertain")));
    TestEqual(TEXT("Terminal receipt is delivered only once"), Bridge.Poll(Controller).Num(), 0);
    auto Retry = Params(AfterReset, TEXT("lost-ack"));
    Result = Bridge.Execute(Controller, Retry);
    TestEqual(TEXT("Unknown outcome is cached and never blindly replayed"), Result->GetStringField(TEXT("status")), FString(TEXT("uncertain")));
    return true;
}

#endif
