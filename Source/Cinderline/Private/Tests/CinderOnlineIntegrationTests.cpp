#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderOnlineSubsystem.h"
#include "Presentation/CinderPlayerController.h"
#include "Tests/AutomationCommon.h"
#include <limits>

namespace CinderOnlineIntegration
{
constexpr double TestTimeoutSeconds = 30.0;

const cinder::Entity* FindOwnWorker(const cinder::net::Snapshot& Snapshot)
{
    for (const cinder::Entity& Entity : Snapshot.entities)
        if (Entity.team == 0 && Entity.kind == cinder::Kind::Worker && Entity.alive()) return &Entity;
    return nullptr;
}

const cinder::Entity* FindEntity(const cinder::net::Snapshot& Snapshot, cinder::Id Id)
{
    for (const cinder::Entity& Entity : Snapshot.entities)
        if (Entity.id == Id) return &Entity;
    return nullptr;
}

bool HasNormalizedOwnArmy(const cinder::net::Snapshot& Snapshot)
{
    bool bFoundOwnEntity = false;
    for (const cinder::Entity& Entity : Snapshot.entities)
    {
        if (Entity.team == -1)
        {
            if (Entity.kind != cinder::Kind::Resource) return false;
            continue;
        }
        if (Entity.team < 0 || Entity.team > 1) return false;
        bFoundOwnEntity |= Entity.team == 0 && Entity.alive();
    }
    return bFoundOwnEntity && FindOwnWorker(Snapshot) != nullptr;
}

struct FTransportFixture
{
    FTestWorldWrapper HostWorld;
    FTestWorldWrapper GuestWorld;
    UCinderOnlineSubsystem* Host = nullptr;
    UCinderOnlineSubsystem* Guest = nullptr;
    ACinderBattlefield* Battle = nullptr;
    ACinderPlayerController* Controller = nullptr;

    bool Initialize(FAutomationTestBase& Test)
    {
        if (!HostWorld.CreateTestWorld(EWorldType::Game))
        {
            HostWorld.ForwardErrorMessages(&Test);
            return false;
        }
        UWorld* HostWorldObject = HostWorld.GetTestWorld();
        if (!Test.TestNotNull(TEXT("Host transient game world exists"), HostWorldObject)) return false;
        HostWorldObject->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
        Battle = HostWorldObject->SpawnActor<ACinderBattlefield>();
        if (!Test.TestNotNull(TEXT("Host world spawned the real battlefield adapter"), Battle)) return false;
        if (!HostWorld.BeginPlayInTestWorld())
        {
            HostWorld.ForwardErrorMessages(&Test);
            return false;
        }
        Controller = HostWorldObject->SpawnActor<ACinderPlayerController>();
        if (!Test.TestNotNull(TEXT("Host world spawned the real player controller"), Controller)) return false;
        // Manually spawned controllers do not receive SetPlayer's local-player
        // initialization. Use the engine's normal path to create UPlayerInput
        // and the controller's bound UInputComponent before calling PlayerTick.
        Controller->InitInputSystem();
        if (!Test.TestNotNull(TEXT("Host controller initialized its real input component"), Controller->InputComponent.Get()))
            return false;

        if (!GuestWorld.CreateTestWorld(EWorldType::Game))
        {
            GuestWorld.ForwardErrorMessages(&Test);
            return false;
        }

        UWorld* GuestWorldObject = GuestWorld.GetTestWorld();
        UGameInstance* HostGameInstance = HostWorldObject ? HostWorldObject->GetGameInstance() : nullptr;
        UGameInstance* GuestGameInstance = GuestWorldObject ? GuestWorldObject->GetGameInstance() : nullptr;
        if (!Test.TestNotNull(TEXT("Host transient world has a game instance"), HostGameInstance)
            || !Test.TestNotNull(TEXT("Guest transient world has a game instance"), GuestGameInstance))
            return false;

        Host = HostGameInstance->GetSubsystem<UCinderOnlineSubsystem>();
        Guest = GuestGameInstance->GetSubsystem<UCinderOnlineSubsystem>();
        Test.TestTrue(TEXT("Transport clients use separate game instances"), HostGameInstance != GuestGameInstance);
        Test.TestNotNull(TEXT("Host game instance initialized the online subsystem"), Host);
        Test.TestNotNull(TEXT("Guest game instance initialized the online subsystem"), Guest);
        Test.TestTrue(TEXT("Transport clients are separate subsystem objects"), Host && Guest && Host != Guest);
        Test.TestTrue(TEXT("Host world began play with the Cinderline game mode"),
            HostWorldObject->HasBegunPlay() && HostWorldObject->GetAuthGameMode<ACinderGameMode>() != nullptr);
        Test.TestTrue(TEXT("Host adapter actors received BeginPlay"),
            Battle->HasActorBegunPlay() && Controller->HasActorBegunPlay());
        Test.TestTrue(TEXT("Controller resolves the host battlefield"), Controller->Battlefield() == Battle);
        Test.TestTrue(TEXT("Controller resolves the host online session"), Controller->Online() == Host);
        HostWorld.ForwardErrorMessages(&Test);
        GuestWorld.ForwardErrorMessages(&Test);
        return Host && Guest && !Test.HasAnyErrors();
    }

    void Close()
    {
        if (Guest) Guest->Leave();
        if (Host) Host->Leave();
    }

    ~FTransportFixture()
    {
        Close();
        // FTestWorldWrapper shuts down each GameInstance and its subsystem.
    }
};

enum class ETransportPhase : uint8
{
    Initialize,
    WaitForRoom,
    WaitForGuest,
    WaitForSnapshots,
    WaitForAcceptedOrder,
    WaitForReplicatedOrder,
    WaitForRejectedOrder,
    WaitForReconnect,
    WaitForPausedSnapshot,
    WaitForResult,
    WaitForLeave
};

class FCinderOnlineTransportCommand final : public IAutomationLatentCommand
{
public:
    FCinderOnlineTransportCommand(FAutomationTestBase* InTest, FString InEndpoint)
        : Test(InTest), Endpoint(MoveTemp(InEndpoint)), StartedAt(FPlatformTime::Seconds())
    {
    }

    virtual bool Update() override
    {
        // Returning false yields to the editor main loop. That frame pumps the
        // WebSockets module and dispatches its callbacks on the game thread.
        if (FPlatformTime::Seconds() - StartedAt > TestTimeoutSeconds)
            return Fail(FString::Printf(TEXT("Transport integration timed out after %.0f seconds in phase %d"),
                TestTimeoutSeconds, static_cast<int32>(Phase)));

        if (Phase != ETransportPhase::Initialize)
        {
            if (Fixture.Host->State() == ECinderOnlineState::Error)
                return Fail(FString::Printf(TEXT("Host transport failed: %s"), *Fixture.Host->ErrorText()));
            if (Fixture.Guest->State() == ECinderOnlineState::Error)
                return Fail(FString::Printf(TEXT("Guest transport failed: %s"), *Fixture.Guest->ErrorText()));
        }

        switch (Phase)
        {
        case ETransportPhase::Initialize:
            if (!Fixture.Initialize(*Test)) return Finish();
            Fixture.Host->CreateRoom(Endpoint, TEXT("Transport Host"), 1);
            Phase = ETransportPhase::WaitForRoom;
            return false;

        case ETransportPhase::WaitForRoom:
            if (!Fixture.Host->HasRoom() || Fixture.Host->State() != ECinderOnlineState::Lobby) return false;
            if (!Test->TestEqual(TEXT("Server assigned the room creator to seat zero"), Fixture.Host->LocalSeat(), 0))
                return Finish();
            if (!Test->TestTrue(TEXT("Server returned a nonempty private room code"), !Fixture.Host->RoomCode().IsEmpty()))
                return Finish();
            Fixture.Guest->JoinRoom(Endpoint, TEXT("Transport Guest"), Fixture.Host->RoomCode());
            Phase = ETransportPhase::WaitForGuest;
            return false;

        case ETransportPhase::WaitForGuest:
            if (!Fixture.Guest->HasRoom() || Fixture.Guest->State() != ECinderOnlineState::Lobby
                || !Fixture.Host->SeatConnected(0) || !Fixture.Host->SeatConnected(1)
                || !Fixture.Guest->SeatConnected(0) || !Fixture.Guest->SeatConnected(1)) return false;
            if (!Test->TestEqual(TEXT("Joiner entered the creator's room"), Fixture.Guest->RoomCode(), Fixture.Host->RoomCode())
                || !Test->TestEqual(TEXT("Server assigned the joiner to seat one"), Fixture.Guest->LocalSeat(), 1)
                || !Test->TestEqual(TEXT("Host uses the explicit test endpoint"), Fixture.Host->Endpoint(), Endpoint)
                || !Test->TestEqual(TEXT("Guest uses the explicit test endpoint"), Fixture.Guest->Endpoint(), Endpoint)
                || !Test->TestEqual(TEXT("Host lobby received the joiner's name"), Fixture.Host->SeatName(1), FString(TEXT("Transport Guest")))
                || !Test->TestEqual(TEXT("Guest lobby received the host's name"), Fixture.Guest->SeatName(0), FString(TEXT("Transport Host"))))
                return Finish();
            Fixture.Host->SetReady(true);
            Fixture.Guest->SetReady(true);
            Phase = ETransportPhase::WaitForSnapshots;
            return false;

        case ETransportPhase::WaitForSnapshots:
        {
            const cinder::net::Snapshot* HostSnapshot = Fixture.Host->LatestSnapshot();
            const cinder::net::Snapshot* GuestSnapshot = Fixture.Guest->LatestSnapshot();
            if (HostSnapshot && !bCapturedHostFirstSnapshot)
            {
                HostFirstSnapshot = *HostSnapshot;
                bCapturedHostFirstSnapshot = true;
            }
            if (GuestSnapshot && !bCapturedGuestFirstSnapshot)
            {
                GuestFirstSnapshot = *GuestSnapshot;
                bCapturedGuestFirstSnapshot = true;
            }
            if (Fixture.Host->State() != ECinderOnlineState::Playing
                || Fixture.Guest->State() != ECinderOnlineState::Playing
                || !HostSnapshot || !GuestSnapshot
                || !bCapturedHostFirstSnapshot || !bCapturedGuestFirstSnapshot) return false;
            if (!Test->TestTrue(TEXT("Both ready states reached the host"), Fixture.Host->SeatReady(0) && Fixture.Host->SeatReady(1))
                || !Test->TestTrue(TEXT("Both ready states reached the guest"), Fixture.Guest->SeatReady(0) && Fixture.Guest->SeatReady(1))
                || !Test->TestTrue(TEXT("Host's first observed snapshot normalizes its army to team zero"), HasNormalizedOwnArmy(HostFirstSnapshot))
                || !Test->TestTrue(TEXT("Guest's first observed snapshot normalizes its army to team zero"), HasNormalizedOwnArmy(GuestFirstSnapshot))
                || !Test->TestEqual(TEXT("Host's first snapshot retains the selected map"), HostFirstSnapshot.config.map, 1)
                || !Test->TestEqual(TEXT("Guest's first snapshot retains the selected map"), GuestFirstSnapshot.config.map, 1))
                return Finish();

            if (!Test->TestFalse(TEXT("Battlefield starts offline before PlayerTick observes the session"), Fixture.Battle->IsOnlineMatch()))
                return Finish();
            Fixture.Controller->PlayerTick(0.01f);
            if (!Test->TestTrue(TEXT("PlayerTick starts the host battlefield from the online snapshot"), Fixture.Battle->IsOnlineMatch())
                || !Test->TestFalse(TEXT("Online snapshot leaves the menu and starts presentation"), Fixture.Battle->IsMenu())
                || !Test->TestTrue(TEXT("Online battlefield owns a passive simulation replica"), Fixture.Battle->Sim().isReplica())
                || !Test->TestEqual(TEXT("PlayerTick imports the authoritative snapshot tick"),
                    Fixture.Battle->Sim().tick(), HostSnapshot->tick))
                return Finish();

            // StartOnlineMatch has the snapshot state but the adapter has not yet
            // recorded the session serial. Consume that serial before proving an
            // arbitrary DeltaSeconds cannot advance the replica.
            Fixture.Battle->Tick(0.0f);
            const uint64 AdapterTickWithoutSnapshot = Fixture.Battle->Sim().tick();
            const uint64 AdapterHashWithoutSnapshot = Fixture.Battle->Sim().stateHash();
            const uint64 SessionSerialWithoutSnapshot = Fixture.Host->SnapshotSerial();
            Fixture.Battle->Tick(5.0f);
            if (!Test->TestTrue(TEXT("Battlefield Tick does not step without a newer server snapshot"),
                Fixture.Host->SnapshotSerial() == SessionSerialWithoutSnapshot
                    && Fixture.Battle->Sim().tick() == AdapterTickWithoutSnapshot
                    && Fixture.Battle->Sim().stateHash() == AdapterHashWithoutSnapshot))
                return Finish();

            const uint64 GuardedStateHash = Fixture.Battle->Sim().stateHash();
            const uint64 GuardedTick = Fixture.Battle->Sim().tick();
            Fixture.Battle->StartMatch(2);
            Fixture.Battle->StartTutorial();
            if (!Test->TestTrue(TEXT("Offline match start is rejected while the battlefield is online"),
                    Fixture.Battle->IsOnlineMatch() && Fixture.Battle->MapIndex() == 1
                        && Fixture.Battle->Sim().tick() == GuardedTick
                        && Fixture.Battle->Sim().stateHash() == GuardedStateHash)
                || !Test->TestFalse(TEXT("Tutorial start is rejected while the battlefield is online"),
                    Fixture.Battle->Tutorial().IsActive())
                || !Test->TestFalse(TEXT("Online battlefield cannot write the offline save slot"), Fixture.Battle->SaveMatch())
                || !Test->TestFalse(TEXT("Online battlefield cannot load the offline save slot"), Fixture.Battle->LoadMatch()))
                return Finish();

            const cinder::Entity* Worker = FindOwnWorker(*HostSnapshot);
            if (!Test->TestNotNull(TEXT("Host snapshot contains an addressable own worker"), Worker)) return Finish();
            OrderedWorker = Worker->id;
            SnapshotSerialBeforeOrder = Fixture.Host->SnapshotSerial();
            SnapshotTickBeforeOrder = HostSnapshot->tick;
            WorkerOrderBeforeCommand = Worker->order;

            Fixture.Host->Tick(5.0f);
            const cinder::net::Snapshot* AfterTransportTick = Fixture.Host->LatestSnapshot();
            if (!Test->TestTrue(TEXT("Transport Tick does not run a local simulation step"),
                AfterTransportTick && Fixture.Host->SnapshotSerial() == SnapshotSerialBeforeOrder
                    && AfterTransportTick->tick == SnapshotTickBeforeOrder
                    && FindEntity(*AfterTransportTick, OrderedWorker)
                    && FindEntity(*AfterTransportTick, OrderedWorker)->order == WorkerOrderBeforeCommand))
                return Finish();

            cinder::Command Command;
            Command.type = cinder::CommandType::Hold;
            Command.team = 0;
            Command.units = {OrderedWorker};
            FeedbackBeforeOrder = Fixture.Host->FeedbackSerial();
            const uint64 AdapterHashBeforeCommand = Fixture.Battle->Sim().stateHash();
            const cinder::CommandResult Submitted = Fixture.Battle->SubmitCommand(Command);
            if (!Test->TestTrue(TEXT("Battlefield submits the valid command through the real WebSocket"), Submitted.accepted))
                return Finish();
            const cinder::net::Snapshot* AfterSend = Fixture.Host->LatestSnapshot();
            if (!Test->TestTrue(TEXT("Sending intent does not mutate the local snapshot"),
                AfterSend && Fixture.Host->SnapshotSerial() == SnapshotSerialBeforeOrder
                    && AfterSend->tick == SnapshotTickBeforeOrder
                    && FindEntity(*AfterSend, OrderedWorker)
                    && FindEntity(*AfterSend, OrderedWorker)->order == WorkerOrderBeforeCommand)
                || !Test->TestEqual(TEXT("Submitting intent does not mutate the battlefield replica"),
                    Fixture.Battle->Sim().stateHash(), AdapterHashBeforeCommand))
                return Finish();
            Phase = ETransportPhase::WaitForAcceptedOrder;
            return false;
        }

        case ETransportPhase::WaitForAcceptedOrder:
            if (Fixture.Host->FeedbackSerial() <= FeedbackBeforeOrder) return false;
            if (!Test->TestTrue(TEXT("Authoritative worker accepted the own-unit command"), Fixture.Host->LastOrderAccepted()))
                return Finish();
            Fixture.Controller->PlayerTick(0.01f);
            if (!Test->TestEqual(TEXT("PlayerTick exposes the server acknowledgement through controller feedback"),
                Fixture.Controller->Feedback(), Fixture.Host->OrderFeedback())) return Finish();
            Phase = ETransportPhase::WaitForReplicatedOrder;
            return false;

        case ETransportPhase::WaitForReplicatedOrder:
        {
            const cinder::net::Snapshot* Snapshot = Fixture.Host->LatestSnapshot();
            const cinder::Entity* Worker = Snapshot ? FindEntity(*Snapshot, OrderedWorker) : nullptr;
            if (!Snapshot || Fixture.Host->SnapshotSerial() <= SnapshotSerialBeforeOrder
                || !Worker || Worker->order != cinder::Order::Hold) return false;
            Fixture.Battle->Tick(0.1f);
            const cinder::Entity* AdapterWorker = Fixture.Battle->Sim().find(OrderedWorker);
            if (!Test->TestTrue(TEXT("Accepted order returned through a newer authoritative snapshot"),
                    Snapshot->tick > SnapshotTickBeforeOrder)
                || !Test->TestTrue(TEXT("Battlefield imports the server-accepted Hold order"),
                    AdapterWorker && AdapterWorker->order == cinder::Order::Hold
                        && Fixture.Battle->Sim().tick() == Snapshot->tick))
                return Finish();

            cinder::Command Invalid;
            Invalid.type = cinder::CommandType::Hold;
            Invalid.team = 0;
            Invalid.units = {std::numeric_limits<cinder::Id>::max() - 7};
            FeedbackBeforeInvalid = Fixture.Host->FeedbackSerial();
            if (!Test->TestTrue(TEXT("Battlefield transmits the invalid entity command for server validation"),
                Fixture.Battle->SubmitCommand(Invalid).accepted))
                return Finish();
            Phase = ETransportPhase::WaitForRejectedOrder;
            return false;
        }

        case ETransportPhase::WaitForRejectedOrder:
            if (Fixture.Host->FeedbackSerial() <= FeedbackBeforeInvalid) return false;
            if (!Test->TestFalse(TEXT("Authoritative worker rejected an unknown entity ID"), Fixture.Host->LastOrderAccepted())
                || !Test->TestTrue(TEXT("Rejected command returned explanatory feedback"), !Fixture.Host->OrderFeedback().IsEmpty()))
                return Finish();
            if (const cinder::net::Snapshot* Snapshot = Fixture.Host->LatestSnapshot())
            {
                const cinder::Entity* Worker = FindEntity(*Snapshot, OrderedWorker);
                if (!Test->TestTrue(TEXT("Accepted Hold order is present before reconnect"),
                    Worker && Worker->order == cinder::Order::Hold)) return Finish();
                TickBeforeReconnect = Snapshot->tick;
            }
            else
            {
                Test->AddError(TEXT("Host lost its authoritative snapshot before reconnect"));
                return Finish();
            }
            SnapshotSerialBeforeReconnect = Fixture.Host->SnapshotSerial();
            SeatBeforeReconnect = Fixture.Host->LocalSeat();
            RoomBeforeReconnect = Fixture.Host->RoomCode();
            Fixture.Host->Reconnect();
            if (!Test->TestTrue(TEXT("Explicit reconnect enters the reconnecting state"),
                Fixture.Host->State() == ECinderOnlineState::Reconnecting)) return Finish();
            Phase = ETransportPhase::WaitForReconnect;
            return false;

        case ETransportPhase::WaitForReconnect:
        {
            const cinder::net::Snapshot* Snapshot = Fixture.Host->LatestSnapshot();
            const cinder::Entity* Worker = Snapshot ? FindEntity(*Snapshot, OrderedWorker) : nullptr;
            if (Fixture.Host->State() != ECinderOnlineState::Playing
                || Fixture.Host->SnapshotSerial() <= SnapshotSerialBeforeReconnect
                || !Snapshot || Snapshot->tick <= TickBeforeReconnect) return false;
            if (!Test->TestEqual(TEXT("Reconnect restores the same private room"), Fixture.Host->RoomCode(), RoomBeforeReconnect)
                || !Test->TestEqual(TEXT("Reconnect restores the same seat"), Fixture.Host->LocalSeat(), SeatBeforeReconnect)
                || !Test->TestTrue(TEXT("Reconnect retains the stable own-worker ID"), Worker != nullptr)
                || !Test->TestTrue(TEXT("Reconnect snapshot retains the accepted Hold order"),
                    Worker && Worker->order == cinder::Order::Hold)
                || !Test->TestTrue(TEXT("Guest remains in the active match while the host reconnects"),
                    Fixture.Guest->State() == ECinderOnlineState::Playing))
                return Finish();
            Fixture.Battle->Tick(0.1f);
            const cinder::Entity* AdapterWorker = Fixture.Battle->Sim().find(OrderedWorker);
            if (!Test->TestTrue(TEXT("Battlefield imports the post-reconnect authoritative snapshot"),
                Fixture.Battle->Sim().tick() == Snapshot->tick
                    && AdapterWorker && AdapterWorker->order == cinder::Order::Hold))
                return Finish();
            PausedAdapterTick = Fixture.Battle->Sim().tick();
            PausedSessionSerial = Fixture.Host->SnapshotSerial();
            Fixture.Controller->ExecuteAction(TEXT("pause"));
            if (!Test->TestTrue(TEXT("Host pause action opens the paused online menu state"), Fixture.Battle->IsPaused()))
                return Finish();
            Phase = ETransportPhase::WaitForPausedSnapshot;
            return false;
        }

        case ETransportPhase::WaitForPausedSnapshot:
        {
            const cinder::net::Snapshot* Snapshot = Fixture.Host->LatestSnapshot();
            if (!Snapshot || Fixture.Host->SnapshotSerial() <= PausedSessionSerial
                || Snapshot->tick <= PausedAdapterTick) return false;
            Fixture.Battle->Tick(0.1f);
            if (!Test->TestTrue(TEXT("Paused online menu still imports a newer authoritative tick"),
                Fixture.Battle->IsPaused() && Fixture.Battle->Sim().tick() == Snapshot->tick
                    && Fixture.Battle->Sim().tick() > PausedAdapterTick))
                return Finish();

            Fixture.Controller->ExecuteAction(TEXT("onlinesurrender"));
            if (!Test->TestTrue(TEXT("Online surrender opens the explicit confirmation state"),
                Fixture.Controller->IsOnlineLeavePending() && Fixture.Controller->IsOnlineSurrenderPending()
                    && Fixture.Battle->IsPaused()))
                return Finish();
            Fixture.Controller->ExecuteAction(TEXT("onlineconfirm"));
            if (!Test->TestTrue(TEXT("Confirmed surrender keeps the socket open while awaiting the result"),
                !Fixture.Controller->IsOnlineLeavePending() && !Fixture.Controller->IsOnlineSurrenderPending()
                    && !Fixture.Battle->IsPaused() && Fixture.Host->HasRoom()
                    && Fixture.Host->State() != ECinderOnlineState::Offline))
                return Finish();
            Phase = ETransportPhase::WaitForResult;
            return false;
        }

        case ETransportPhase::WaitForResult:
            if (Fixture.Host->State() != ECinderOnlineState::Finished
                || Fixture.Guest->State() != ECinderOnlineState::Finished) return false;
            if (!Test->TestEqual(TEXT("Surrendering host receives a local-perspective loss"), Fixture.Host->MatchWinner(), 1)
                || !Test->TestEqual(TEXT("Opponent receives a local-perspective win"), Fixture.Guest->MatchWinner(), 0))
                return Finish();
            Fixture.Close();
            if (!Test->TestTrue(TEXT("Leave clears active match state before socket drain completes"),
                !Fixture.Host->HasMatch() && !Fixture.Guest->HasMatch())) return Finish();
            Phase = ETransportPhase::WaitForLeave;
            return false;

        case ETransportPhase::WaitForLeave:
            if (Fixture.Host->State() != ECinderOnlineState::Offline
                || Fixture.Guest->State() != ECinderOnlineState::Offline) return false;
            if (!Test->TestFalse(TEXT("Drained leave clears the host room"), Fixture.Host->HasRoom())
                || !Test->TestFalse(TEXT("Drained leave clears the guest room"), Fixture.Guest->HasRoom()))
                return Finish();
            Fixture.Battle->ReturnToMenu();
            Fixture.Battle->StartMatch(2);
            if (!Test->TestTrue(TEXT("Fresh offline match starts after leave and return to menu"),
                !Fixture.Battle->IsOnlineMatch() && !Fixture.Battle->IsMenu()
                    && Fixture.Battle->MapIndex() == 2 && !Fixture.Battle->Sim().isReplica()))
                return Finish();
            return Finish();
        }
        return Fail(TEXT("Transport integration entered an unknown phase"));
    }

private:
    bool Fail(const FString& Message)
    {
        Test->AddError(Message);
        return Finish();
    }

    bool Finish()
    {
        Fixture.Close();
        if (Fixture.Host)
            Test->TestTrue(TEXT("Host cleanup closes or begins draining its socket"),
                Fixture.Host->State() == ECinderOnlineState::Offline || Fixture.Host->State() == ECinderOnlineState::Leaving);
        if (Fixture.Guest)
            Test->TestTrue(TEXT("Guest cleanup closes or begins draining its socket"),
                Fixture.Guest->State() == ECinderOnlineState::Offline || Fixture.Guest->State() == ECinderOnlineState::Leaving);
        return true;
    }

    FAutomationTestBase* Test = nullptr;
    FString Endpoint;
    double StartedAt = 0;
    ETransportPhase Phase = ETransportPhase::Initialize;
    FTransportFixture Fixture;
    cinder::Id OrderedWorker = 0;
    cinder::Order WorkerOrderBeforeCommand = cinder::Order::Idle;
    uint64 SnapshotSerialBeforeOrder = 0;
    uint64 SnapshotTickBeforeOrder = 0;
    uint64 FeedbackBeforeOrder = 0;
    uint64 FeedbackBeforeInvalid = 0;
    uint64 SnapshotSerialBeforeReconnect = 0;
    uint64 TickBeforeReconnect = 0;
    uint64 PausedSessionSerial = 0;
    uint64 PausedAdapterTick = 0;
    int32 SeatBeforeReconnect = -1;
    FString RoomBeforeReconnect;
    cinder::net::Snapshot HostFirstSnapshot;
    cinder::net::Snapshot GuestFirstSnapshot;
    bool bCapturedHostFirstSnapshot = false;
    bool bCapturedGuestFirstSnapshot = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderOnlineTransportIntegration,
    "Cinderline.Online.Transport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderOnlineTransportIntegration::RunTest(const FString& Parameters)
{
    const FString Endpoint = FPlatformMisc::GetEnvironmentVariable(TEXT("CINDERLINE_TEST_SERVER"));
    const bool bPublicTlsTest = FPlatformMisc::GetEnvironmentVariable(TEXT("CINDERLINE_TEST_PUBLIC")) == TEXT("1");
    const FString Prefix = TEXT("ws://127.0.0.1:");
    if (bPublicTlsTest)
    {
        if (!TestTrue(TEXT("CINDERLINE_TEST_PUBLIC=1 supplies an explicit TLS WebSocket /play endpoint"),
            Endpoint.StartsWith(TEXT("wss://")) && Endpoint.EndsWith(TEXT("/play")))) return false;
    }
    else
    {
        if (!TestTrue(TEXT("CINDERLINE_TEST_SERVER supplies an explicit loopback WebSocket endpoint"),
            Endpoint.StartsWith(Prefix) && Endpoint.EndsWith(TEXT("/play")))) return false;

        const FString PortText = Endpoint.Mid(Prefix.Len(), Endpoint.Len() - Prefix.Len() - FString(TEXT("/play")).Len());
        int32 Port = 0;
        if (!TestTrue(TEXT("CINDERLINE_TEST_SERVER contains a valid nonzero loopback port"),
            LexTryParseString(Port, *PortText) && Port > 0 && Port <= 65535)) return false;
    }

    ADD_LATENT_AUTOMATION_COMMAND(CinderOnlineIntegration::FCinderOnlineTransportCommand(this, Endpoint));
    return true;
}

#endif
