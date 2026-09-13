#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "IWebSocket.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderOnlineSubsystem.h"
#include "Presentation/CinderPlayerController.h"
#include "Sim/Network.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
// A connected transport adapter for handler/lifecycle checks. It never opens
// a network socket; all production message handlers and Tick still run.
class FConnectedRecoverySocket final : public IWebSocket
{
public:
    bool bConnected = true;
    int32 ConnectCalls = 0, CloseCalls = 0;
    TArray<FString> SentText;
    virtual void Connect() override { ++ConnectCalls; bConnected = true; }
    virtual void Close(int32 = 1000, const FString& = FString()) override { ++CloseCalls; bConnected = false; }
    virtual bool IsConnected() override { return bConnected; }
    virtual void Send(const FString& Data) override { SentText.Add(Data); }
    virtual void Send(const void*, SIZE_T, bool = false) override {}
    virtual void SetTextMessageMemoryLimit(uint64) override {}
    virtual FWebSocketConnectedEvent& OnConnected() override { return ConnectedEvent; }
    virtual FWebSocketConnectionErrorEvent& OnConnectionError() override { return ErrorEvent; }
    virtual FWebSocketClosedEvent& OnClosed() override { return ClosedEvent; }
    virtual FWebSocketMessageEvent& OnMessage() override { return MessageEvent; }
    virtual FWebSocketBinaryMessageEvent& OnBinaryMessage() override { return BinaryEvent; }
    virtual FWebSocketRawMessageEvent& OnRawMessage() override { return RawEvent; }
    virtual FWebSocketMessageSentEvent& OnMessageSent() override { return SentEvent; }
private:
    FWebSocketConnectedEvent ConnectedEvent;
    FWebSocketConnectionErrorEvent ErrorEvent;
    FWebSocketClosedEvent ClosedEvent;
    FWebSocketMessageEvent MessageEvent;
    FWebSocketBinaryMessageEvent BinaryEvent;
    FWebSocketRawMessageEvent RawEvent;
    FWebSocketMessageSentEvent SentEvent;
};

struct FOnlineRecoveryFixture
{
    // Direct handlers and Tick remain usable without initializing a transport.
    TStrongObjectPtr<UGameInstance> GameInstance{NewObject<UGameInstance>()};
    TStrongObjectPtr<UCinderOnlineSubsystem> Session{NewObject<UCinderOnlineSubsystem>(GameInstance.Get())};
};

struct FTemporaryOnlinePreferences
{
    FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(),
        TEXT("Automation"), TEXT("CinderlineOnlineRecovery"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    FString Filename = FPaths::Combine(Directory, TEXT("Online.ini"));

    FTemporaryOnlinePreferences() { IFileManager::Get().MakeDirectory(*Directory, true); }
    ~FTemporaryOnlinePreferences()
    {
        IFileManager::Get().Delete(*Filename, false, true);
        IFileManager::Get().DeleteDirectory(*Directory, false, false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderOnlineRecoveryIntegration,
    "Cinderline.Integration.OnlineRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderOnlineRecoveryIntegration::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const FString Welcome = TEXT("{\"type\":\"welcome\",\"room\":\"ABCDEF\",\"token\":\"fixture-seat-token-123456\",\"team\":0,\"playerCount\":2}");
    const FString Started = TEXT("{\"type\":\"started\"}");
    const FString Pong = TEXT("{\"type\":\"pong\",\"nonce\":7}");
    const FString WelcomeFour = TEXT("{\"type\":\"welcome\",\"room\":\"FOURAA\",\"token\":\"four-player-seat-token-123456\",\"team\":2,\"playerCount\":4}");
    const FString LobbyFour = TEXT("{\"type\":\"lobby\",\"playerCount\":4,\"map\":1,\"players\":[{\"name\":\"Alpha\",\"connected\":true,\"ready\":true},{\"name\":\"Bravo\",\"connected\":true,\"ready\":false},{\"name\":\"Charlie\",\"connected\":true,\"ready\":true},{\"name\":\"Delta\",\"connected\":true,\"ready\":false}]}");
    const auto HasFourPlayerRoster = [](const UCinderOnlineSubsystem& Session)
    {
        return Session.PlayerCount() == 4 && Session.LocalSeat() == 2 && Session.MapIndex() == 1
            && Session.SeatName(0) == TEXT("Alpha") && Session.SeatName(1) == TEXT("Bravo")
            && Session.SeatName(2) == TEXT("Charlie") && Session.SeatName(3) == TEXT("Delta")
            && Session.SeatConnected(0) && Session.SeatConnected(1) && Session.SeatConnected(2) && Session.SeatConnected(3)
            && Session.SeatReady(0) && !Session.SeatReady(1) && Session.SeatReady(2) && !Session.SeatReady(3);
    };
    const auto StartSession = [&](UCinderOnlineSubsystem& Session)
    {
        Session.HandleText(Welcome);
        Session.HandleText(Started);
        TestTrue(TEXT("Welcome and started retain a resumable seat while awaiting the first snapshot"),
            Session.State() == ECinderOnlineState::Starting && Session.HasMatch()
            && !Session.LatestSnapshot() && Session.PlayerCount() == 2
            && Session.CanLeaveSession() && Session.CanReconnectSession()
            && Session.SnapshotDeadline > FPlatformTime::Seconds() && !Session.Socket);
    };
    const auto ReceivePong = [&](UCinderOnlineSubsystem& Session)
    {
        Session.PingNonce = 7;
        Session.PingSentAt = FPlatformTime::Seconds() - 0.01;
        const double Before = FPlatformTime::Seconds();
        Session.HandleText(Pong);
        TestTrue(TEXT("An actual pong refreshes transport activity"), Session.LastReceivedAt >= Before);
    };

    cinder::Simulation Authority;
    cinder::Config Configuration;
    Configuration.ai = false;
    Authority.reset(Configuration);
    const cinder::net::Snapshot Frame = cinder::net::snapshotFor(Authority, 0);
    const auto Bytes = cinder::net::encodeSnapshot(Frame);
    if (!TestTrue(TEXT("Authoritative fixture encodes a complete binary snapshot"), Bytes.size() > 1)) return false;

    {
        FOnlineRecoveryFixture Fixture;
        UCinderOnlineSubsystem& Session = *Fixture.Session;
        const TSharedRef<FConnectedRecoverySocket> Transport = MakeShared<FConnectedRecoverySocket>();
        Session.Socket = Transport;
        Session.HandleText(Welcome);
        const FString ReadyLobby = TEXT("{\"type\":\"lobby\",\"playerCount\":2,\"players\":[{\"name\":\"Host\",\"connected\":true,\"ready\":true},{\"name\":\"Guest\",\"connected\":true,\"ready\":true}]}");
        const FString Capacity = TEXT("{\"type\":\"error\",\"code\":\"worker_capacity\",\"retryable\":true,\"message\":\"The server is waiting for match capacity.\"}");
        Session.HandleText(ReadyLobby);
        const uint64 Generation = Session.SocketGeneration;
        Session.HandleText(Capacity);
        Session.Tick(0);
        TestTrue(TEXT("Worker capacity preserves the connected ready lobby without closing or reconnecting its transport"),
            Session.State() == ECinderOnlineState::Lobby && Session.HasRoom() && !Session.HasMatch()
            && Session.Socket.Get() == &Transport.Get() && Transport->IsConnected()
            && Transport->CloseCalls == 0 && Transport->ConnectCalls == 0
            && Session.SocketGeneration == Generation && Session.ReconnectDeadline == 0
            && Session.ReconnectAttempts == 0 && Session.SnapshotDeadline == 0
            && Session.SeatReady(0) && Session.SeatReady(1)
            && Session.CanLeaveSession() && Session.ErrorText().IsEmpty());
        const FString WaitingStatus = Session.StatusText();
        TestTrue(TEXT("Capacity waiting has explicit connected-room feedback"), WaitingStatus.Contains(TEXT("capacity")) && Session.bWaitingForWorker);
        Session.HandleText(ReadyLobby);
        TestEqual(TEXT("Repeated ready lobby updates preserve capacity waiting feedback"), Session.StatusText(), WaitingStatus);
        Session.HandleText(TEXT("{\"type\":\"lobby\",\"playerCount\":2,\"players\":[{\"name\":\"Host\",\"connected\":true,\"ready\":false},{\"name\":\"Guest\",\"connected\":true,\"ready\":true}]}"));
        TestTrue(TEXT("Unreadying clears admission waiting while leaving the room connected"),
            !Session.bWaitingForWorker && Session.StatusText() != WaitingStatus && Transport->CloseCalls == 0);
        Session.HandleText(ReadyLobby);
        Session.HandleText(Capacity);
        Session.HandleText(Started);
        TestTrue(TEXT("A queued room enters normal snapshot synchronization when a worker becomes available"),
            Session.State() == ECinderOnlineState::Starting && Session.HasMatch() && !Session.bWaitingForWorker
            && Session.ErrorText().IsEmpty() && Session.StatusText() != WaitingStatus
            && Session.SnapshotDeadline > FPlatformTime::Seconds() && Transport->CloseCalls == 0);
        Session.HandleBinary(Bytes.data(), Bytes.size(), true);
        TestTrue(TEXT("The queued room's first real authoritative snapshot enables play on the original transport"),
            Session.State() == ECinderOnlineState::Playing && Session.CanSendOrders()
            && Session.LatestSnapshot() && Session.Socket.Get() == &Transport.Get()
            && Transport->CloseCalls == 0 && Transport->ConnectCalls == 0);
        Session.FinishLeave();
    }

    {
        cinder::Simulation FourAuthority;
        cinder::Config FourConfiguration;
        FourConfiguration.ai = false;
        FourConfiguration.map = 1;
        FourConfiguration.playerCount = 4;
        FourAuthority.reset(FourConfiguration);
        FourAuthority.debugResources(2, 777);
        const cinder::net::Snapshot InitialFourFrame = cinder::net::snapshotFor(FourAuthority, 2);
        const auto InitialFourBytes = cinder::net::encodeSnapshot(InitialFourFrame);
        if (!TestTrue(TEXT("A four-player authority encodes the seat-two recipient snapshot"),
            !InitialFourBytes.empty() && InitialFourFrame.config.playerCount == 4
            && InitialFourFrame.eliminatedMask == 0 && InitialFourFrame.winner == -1)) return false;
        bool bOwnHeadquartersNormalized = false;
        for (const cinder::Entity& Own : FourAuthority.entities())
            if (Own.alive() && Own.team == 2 && Own.kind == cinder::Kind::Headquarters)
                for (const cinder::Entity& Visible : InitialFourFrame.entities)
                    if (Visible.alive() && Visible.team == 0 && Visible.kind == Own.kind
                        && Visible.pos.x == Own.pos.x && Visible.pos.y == Own.pos.y)
                        bOwnHeadquartersNormalized = true;
        TestTrue(TEXT("Recipient snapshots normalize global seat two's ownership and economy to local player zero"),
            bOwnHeadquartersNormalized && InitialFourFrame.player.ore == 777);

        FOnlineRecoveryFixture Fixture;
        UCinderOnlineSubsystem& Session = *Fixture.Session;
        const TSharedRef<FConnectedRecoverySocket> Transport = MakeShared<FConnectedRecoverySocket>();
        Session.Socket = Transport;
        Session.HandleText(WelcomeFour);
        Session.HandleText(LobbyFour);
        TestTrue(TEXT("A four-player welcome and lobby publish all four distinct seats and readiness values"),
            Session.State() == ECinderOnlineState::Lobby && HasFourPlayerRoster(Session)
            && Session.RoomCode() == TEXT("FOURAA") && Session.RemainingPlayers() == 4 && !Session.IsEliminated());
        Session.HandleText(TEXT("{\"type\":\"peer\",\"team\":3,\"connected\":false}"));
        TestTrue(TEXT("A peer event updates its global seat without changing any other participant"),
            Session.SeatConnected(0) && Session.SeatConnected(1) && Session.SeatConnected(2) && !Session.SeatConnected(3)
            && Session.SeatName(3) == TEXT("Delta") && Session.LocalSeat() == 2);
        const FString PeerStatus = Session.StatusText();
        const FString InvalidPeers[] = {
            TEXT("{\"type\":\"peer\",\"team\":1.5,\"connected\":false}"),
            TEXT("{\"type\":\"peer\",\"team\":4,\"connected\":false}"),
            TEXT("{\"type\":\"peer\",\"team\":-1,\"connected\":false}"),
            TEXT("{\"type\":\"peer\",\"connected\":false}"),
        };
        for (const FString& Message : InvalidPeers)
        {
            Session.HandleText(Message);
            TestTrue(TEXT("Malformed peer seats are ignored without changing roster, feedback, or result state"),
                Session.State() == ECinderOnlineState::Lobby && Session.StatusText() == PeerStatus
                && Session.SeatConnected(0) && Session.SeatConnected(1) && Session.SeatConnected(2) && !Session.SeatConnected(3)
                && Session.SnapshotSerial() == 0 && Session.MatchWinner() == -1);
        }
        Session.HandleText(TEXT("{\"type\":\"peer\",\"team\":3,\"connected\":true}"));
        TestTrue(TEXT("The disconnected fourth seat can reconnect without losing its roster entry"), HasFourPlayerRoster(Session));
        Session.HandleText(Started);
        Session.HandleBinary(InitialFourBytes.data(), InitialFourBytes.size(), true);
        if (!TestNotNull(TEXT("The four-player client publishes its validated first snapshot"), Session.LatestSnapshot())) return false;
        TestTrue(TEXT("A live four-player client can issue orders before local elimination"),
            Session.State() == ECinderOnlineState::Playing && Session.PlayerCount() == 4
            && Session.CanSendOrders() && !Session.IsEliminated() && Session.RemainingPlayers() == 4
            && Session.LatestSnapshot()->config.playerCount == 4 && Session.LatestSnapshot()->player.ore == 777);

        const uint64 BeforeInvalidResult = Session.SnapshotSerial();
        const FString InvalidResults[] = {
            TEXT("{\"type\":\"result\",\"winner\":1.5}"),
            TEXT("{\"type\":\"result\",\"winner\":4}"),
            TEXT("{\"type\":\"result\",\"winner\":-3}"),
            TEXT("{\"type\":\"result\",\"winner\":-1}"),
            TEXT("{\"type\":\"result\",\"winner\":\"1\"}"),
        };
        for (const FString& Message : InvalidResults)
        {
            Session.HandleText(Message);
            TestTrue(TEXT("Malformed final winners are ignored atomically"),
                Session.State() == ECinderOnlineState::Playing && Session.MatchWinner() == -1
                && Session.LatestSnapshot()->winner == -1 && Session.SnapshotSerial() == BeforeInvalidResult
                && HasFourPlayerRoster(Session) && Session.CanSendOrders());
        }

        FourAuthority.forfeit(2);
        const cinder::net::Snapshot EliminatedFrame = cinder::net::snapshotFor(FourAuthority, 2);
        const auto EliminatedBytes = cinder::net::encodeSnapshot(EliminatedFrame);
        if (!TestTrue(TEXT("An actual seat-two forfeit yields local eliminated bit zero while three players remain"),
            !EliminatedBytes.empty() && FourAuthority.winner() == -1
            && FourAuthority.eliminated(2) && EliminatedFrame.eliminatedMask == 1 && EliminatedFrame.winner == -1)) return false;
        Session.HandleBinary(EliminatedBytes.data(), EliminatedBytes.size(), true);
        const int32 ControlsBeforeSurrender = Transport->SentText.Num();
        TestTrue(TEXT("Local elimination keeps the four-player match live while disabling orders and surrender"),
            Session.State() == ECinderOnlineState::Playing && Session.HasMatch() && Session.IsEliminated()
            && Session.RemainingPlayers() == 3 && !Session.CanSendOrders() && !Session.Surrender()
            && Session.MatchWinner() == -1 && Session.LatestSnapshot()->eliminatedMask == 1
            && Transport->SentText.Num() == ControlsBeforeSurrender && Transport->IsConnected()
            && Session.CanLeaveSession() && Transport->CloseCalls == 0);
        const uint64 BeforeSpectatingUpdate = Session.SnapshotSerial();
        const auto PreviousTick = Session.LatestSnapshot()->tick;
        FourAuthority.update(cinder::Simulation::Step * 2);
        const auto SpectatingBytes = cinder::net::encodeSnapshot(cinder::net::snapshotFor(FourAuthority, 2));
        if (!TestTrue(TEXT("The continuing authority can encode another update for an eliminated player"), !SpectatingBytes.empty())) return false;
        Session.HandleBinary(SpectatingBytes.data(), SpectatingBytes.size(), true);
        TestTrue(TEXT("Eliminated clients continue receiving battlefield updates without re-enabling their orders"),
            Session.State() == ECinderOnlineState::Playing && Session.SnapshotSerial() == BeforeSpectatingUpdate + 1
            && Session.LatestSnapshot()->tick > PreviousTick && Session.IsEliminated()
            && Session.RemainingPlayers() == 3 && !Session.CanSendOrders() && Transport->IsConnected());

        FourAuthority.forfeit(0);
        FourAuthority.forfeit(3);
        const cinder::net::Snapshot FinalFourFrame = cinder::net::snapshotFor(FourAuthority, 2);
        const auto FinalFourBytes = cinder::net::encodeSnapshot(FinalFourFrame);
        if (!TestTrue(TEXT("The final authority winner is global seat one and local seat three for this recipient"),
            !FinalFourBytes.empty() && FourAuthority.winner() == 1 && FinalFourFrame.winner == 3)) return false;
        Session.HandleText(TEXT("{\"type\":\"result\",\"winner\":1}"));
        TestTrue(TEXT("A four-player final result rotates the global winner around the recipient's seat"),
            Session.State() == ECinderOnlineState::Finished && Session.MatchWinner() == 3
            && Session.LatestSnapshot()->winner == 3 && Session.LatestSnapshot()->eliminatedMask == 7
            && Session.RemainingPlayers() == 1 && !Session.CanSendOrders() && !Session.Surrender());
        TestTrue(TEXT("The JSON winner produces a coherent snapshot that remains importable"),
            !cinder::net::encodeSnapshot(*Session.LatestSnapshot()).empty());
        Session.HandleBinary(SpectatingBytes.data(), SpectatingBytes.size(), true);
        TestTrue(TEXT("An in-flight ongoing snapshot cannot erase the confirmed winner or terminal eliminated mask"),
            Session.State() == ECinderOnlineState::Finished && Session.MatchWinner() == 3
            && Session.LatestSnapshot()->winner == 3 && Session.LatestSnapshot()->eliminatedMask == 7
            && Session.RemainingPlayers() == 1 && !cinder::net::encodeSnapshot(*Session.LatestSnapshot()).empty());
        Session.HandleBinary(FinalFourBytes.data(), FinalFourBytes.size(), true);
        TestTrue(TEXT("The matching final snapshot preserves the normalized winner and final survivor count"),
            Session.State() == ECinderOnlineState::Finished && Session.MatchWinner() == 3
            && Session.LatestSnapshot()->winner == 3 && Session.RemainingPlayers() == 1);
        TestEqual(TEXT("The four-player lifecycle never attempts a network connection"), Transport->ConnectCalls, 0);
        Session.FinishLeave();
        for (int32 Seat = 0; Seat < UCinderOnlineSubsystem::MaxPlayers; ++Seat)
            TestTrue(TEXT("Leaving clears all four stored roster slots"),
                Session.Names[Seat].IsEmpty() && !Session.Connected[Seat] && !Session.Ready[Seat]);

        FOnlineRecoveryFixture DrawFixture;
        UCinderOnlineSubsystem& DrawSession = *DrawFixture.Session;
        DrawSession.Socket = MakeShared<FConnectedRecoverySocket>();
        DrawSession.HandleText(WelcomeFour);
        DrawSession.HandleText(Started);
        DrawSession.HandleBinary(InitialFourBytes.data(), InitialFourBytes.size(), true);
        DrawSession.HandleText(TEXT("{\"type\":\"result\",\"winner\":-2}"));
        if (!TestNotNull(TEXT("The draw fixture has a published snapshot"), DrawSession.LatestSnapshot())) return false;
        TestTrue(TEXT("A draw is a terminal result rather than an ongoing or losing match"),
            DrawSession.State() == ECinderOnlineState::Finished && DrawSession.MatchWinner() == -2
            && DrawSession.LatestSnapshot()->winner == -2 && DrawSession.LatestSnapshot()->eliminatedMask == 15
            && DrawSession.RemainingPlayers() == 0 && !DrawSession.CanSendOrders() && !DrawSession.Surrender());
        TestTrue(TEXT("The JSON draw produces a coherent snapshot that remains importable"),
            !cinder::net::encodeSnapshot(*DrawSession.LatestSnapshot()).empty());
        DrawSession.HandleBinary(InitialFourBytes.data(), InitialFourBytes.size(), true);
        TestTrue(TEXT("A later in-flight ongoing snapshot cannot erase a confirmed draw"),
            DrawSession.State() == ECinderOnlineState::Finished && DrawSession.MatchWinner() == -2
            && DrawSession.LatestSnapshot()->winner == -2 && DrawSession.LatestSnapshot()->eliminatedMask == 15
            && DrawSession.RemainingPlayers() == 0
            && !cinder::net::encodeSnapshot(*DrawSession.LatestSnapshot()).empty());
        DrawSession.FinishLeave();

        // A control result may precede the final binary frame. Build a living,
        // mutually visible combat pair with a real target reference so codec
        // success alone cannot hide an unimportable terminal replica.
        cinder::Simulation TerminalAuthority;
        TerminalAuthority.reset(FourConfiguration);
        const cinder::Id TerminalAttacker = TerminalAuthority.debugSpawn(cinder::Kind::Striker, 2, {350, 350});
        const cinder::Id TerminalTarget = TerminalAuthority.debugSpawn(cinder::Kind::Striker, 1, {480, 350});
        cinder::Command TargetOrder; TargetOrder.type = cinder::CommandType::Attack; TargetOrder.team = 2;
        TargetOrder.units = {TerminalAttacker}; TargetOrder.target = TerminalTarget;
        if (!TestTrue(TEXT("Terminal fixture establishes a real attack reference across opposing teams"), TerminalAuthority.command(TargetOrder).accepted)) return false;
        TerminalAuthority.update(cinder::Simulation::Step);
        const auto BeforeTerminal = cinder::net::snapshotFor(TerminalAuthority, 2);
        const auto BeforeTerminalBytes = cinder::net::encodeSnapshot(BeforeTerminal);
        if (!TestTrue(TEXT("Terminal fixture begins with living opponents and visible combat effects"),
            !BeforeTerminalBytes.empty() && !BeforeTerminal.effects.empty()
            && TerminalAuthority.find(TerminalAttacker)->alive() && TerminalAuthority.find(TerminalTarget)->alive())) return false;
        for (int32 GlobalWinner : {2, 1, -2})
        {
            FOnlineRecoveryFixture TerminalFixture;
            auto& TerminalSession = *TerminalFixture.Session;
            TerminalSession.Socket = MakeShared<FConnectedRecoverySocket>();
            TerminalSession.HandleText(WelcomeFour);
            TerminalSession.HandleText(Started);
            TerminalSession.HandleBinary(BeforeTerminalBytes.data(), BeforeTerminalBytes.size(), true);
            cinder::Simulation Replica;
            if (!TestTrue(TEXT("The live pre-result snapshot imports into the actual passive simulation"), Replica.applySnapshot(*TerminalSession.LatestSnapshot()))) return false;
            const int32 ExpectedWinner = GlobalWinner == -2 ? -2 : (GlobalWinner - 2 + 4) % 4;
            const auto VerifyTerminalReplica = [&]()
            {
                const auto& Published = *TerminalSession.LatestSnapshot();
                std::string ImportError;
                if (!TestTrue(TEXT("A control-only terminal result remains importable by Simulation::applySnapshot"), Replica.applySnapshot(Published, &ImportError))) return false;
                TestTrue(TEXT("The actual replica exposes the confirmed result and local elimination to the HUD"),
                    Replica.isReplica() && Replica.winner() == ExpectedWinner && Replica.eliminated(0) == (ExpectedWinner != 0));
                for (std::size_t Cell = 0; Cell < Published.fog.size(); ++Cell)
                    if (!TestEqual(TEXT("Terminal synthesis never increases visibility or explored terrain"), Published.fog[Cell],
                        static_cast<std::uint8_t>(ExpectedWinner != 0 && BeforeTerminal.fog[Cell] == 2 ? 1 : BeforeTerminal.fog[Cell]))) return false;
                for (const auto& Entity : Published.entities)
                {
                    const cinder::Entity* Known = nullptr;
                    for (const auto& Previous : BeforeTerminal.entities) if (Previous.id == Entity.id) { Known = &Previous; break; }
                    if (!TestTrue(TEXT("Terminal synthesis retains only previously known positions"), Known && Known->pos.x == Entity.pos.x && Known->pos.y == Entity.pos.y)) return false;
                    if (!TestTrue(TEXT("A terminal snapshot contains no actor on an eliminated team"), Entity.team < 0 || !Replica.eliminated(Entity.team))) return false;
                }
                if (ExpectedWinner == 0)
                    TestTrue(TEXT("The winning unit survives with its removed enemy reference cleared"),
                        Replica.find(TerminalAttacker) && Replica.find(TerminalAttacker)->alive()
                        && Replica.find(TerminalAttacker)->target == 0 && !Replica.find(TerminalTarget));
                else
                    TestTrue(TEXT("An eliminated viewer retains neither units nor visible combat effects"),
                        !Replica.find(TerminalAttacker) && !Replica.find(TerminalTarget) && Published.effects.empty());
                return true;
            };
            TerminalSession.HandleText(FString::Printf(TEXT("{\"type\":\"result\",\"winner\":%d}"), GlobalWinner));
            if (!VerifyTerminalReplica()) return false;
            const uint64 BeforeLateFrame = TerminalSession.SnapshotSerial();
            TerminalSession.HandleBinary(BeforeTerminalBytes.data(), BeforeTerminalBytes.size(), true);
            TestEqual(TEXT("The same-tick pre-result frame is processed rather than merely ignored"), TerminalSession.SnapshotSerial(), BeforeLateFrame + 1);
            if (!VerifyTerminalReplica()) return false;
            TerminalSession.FinishLeave();
        }

        FOnlineRecoveryFixture MismatchedFixture;
        UCinderOnlineSubsystem& Mismatched = *MismatchedFixture.Session;
        Mismatched.HandleText(WelcomeFour);
        Mismatched.HandleText(Started);
        Mismatched.HandleBinary(InitialFourBytes.data(), InitialFourBytes.size(), true);
        const uint64 BeforeMismatchedFrame = Mismatched.SnapshotSerial();
        Mismatched.HandleBinary(Bytes.data(), Bytes.size(), true);
        TestTrue(TEXT("A valid two-player snapshot is rejected atomically by a four-player session"),
            Mismatched.State() == ECinderOnlineState::Error && Mismatched.PlayerCount() == 4
            && Mismatched.LocalSeat() == 2 && Mismatched.SnapshotSerial() == BeforeMismatchedFrame
            && Mismatched.LatestSnapshot() && Mismatched.LatestSnapshot()->config.playerCount == 4
            && Mismatched.LatestSnapshot()->player.ore == 777 && Mismatched.SnapshotDeadline == 0);
        Mismatched.FinishLeave();
    }

    {
        const FString MalformedSeats[] = {
            TEXT("{\"type\":\"welcome\",\"room\":\"BADAAA\",\"token\":\"different-seat-token-123456\",\"team\":0,\"playerCount\":3}"),
            TEXT("{\"type\":\"welcome\",\"room\":\"BADAAA\",\"token\":\"different-seat-token-123456\",\"team\":0,\"playerCount\":2.5}"),
            TEXT("{\"type\":\"welcome\",\"room\":\"BADAAA\",\"token\":\"different-seat-token-123456\",\"team\":0}"),
            TEXT("{\"type\":\"welcome\",\"room\":\"BADAAA\",\"token\":\"different-seat-token-123456\",\"team\":2.5,\"playerCount\":4}"),
            TEXT("{\"type\":\"welcome\",\"room\":\"BADAAA\",\"token\":\"different-seat-token-123456\",\"team\":4,\"playerCount\":4}"),
        };
        const FString MalformedLobbies[] = {
            TEXT("{\"type\":\"lobby\",\"playerCount\":3,\"map\":2,\"players\":[]}"),
            TEXT("{\"type\":\"lobby\",\"map\":2,\"players\":[]}"),
            TEXT("{\"type\":\"lobby\",\"playerCount\":4,\"map\":1.5,\"players\":[null,null,null,null]}"),
            TEXT("{\"type\":\"lobby\",\"playerCount\":4,\"map\":\"1\",\"players\":[null,null,null,null]}"),
            TEXT("{\"type\":\"lobby\",\"playerCount\":4,\"map\":2,\"players\":[{\"name\":\"Changed\",\"connected\":false,\"ready\":false}]}"),
            TEXT("{\"type\":\"lobby\",\"playerCount\":2,\"map\":2,\"players\":[{},{},{},{}]}"),
            TEXT("{\"type\":\"lobby\",\"playerCount\":4,\"map\":2,\"players\":[{\"name\":\"Changed\",\"connected\":false,\"ready\":false},null,null,42]}"),
        };
        const auto RejectMalformedRosterMessage = [&](const FString& Message)
        {
            FOnlineRecoveryFixture Fixture;
            UCinderOnlineSubsystem& Session = *Fixture.Session;
            Session.HandleText(WelcomeFour);
            Session.HandleText(LobbyFour);
            Session.NextSequence = 29;
            Session.HandleText(Message);
            TestTrue(TEXT("Malformed welcome and lobby messages fail before partially replacing valid room or roster data"),
                Session.State() == ECinderOnlineState::Error && !Session.ErrorText().IsEmpty()
                && Session.RoomCode() == TEXT("FOURAA") && Session.SeatToken == TEXT("four-player-seat-token-123456")
                && HasFourPlayerRoster(Session) && Session.NextSequence == 29
                && Session.SnapshotSerial() == 0 && Session.MatchWinner() == -1 && !Session.Socket);
            Session.FinishLeave();
        };
        for (const FString& Message : MalformedSeats)
        {
            FOnlineRecoveryFixture FreshFixture;
            UCinderOnlineSubsystem& Fresh = *FreshFixture.Session;
            Fresh.HandleText(Message);
            TestTrue(TEXT("An invalid initial player count or seat cannot establish room credentials"),
                Fresh.State() == ECinderOnlineState::Error && !Fresh.HasRoom() && Fresh.LocalSeat() == -1
                && Fresh.PlayerCount() == 2 && Fresh.SeatToken.IsEmpty() && !Fresh.CanReconnectSession()
                && !Fresh.HasMatch() && !Fresh.LatestSnapshot());
            Fresh.FinishLeave();
            RejectMalformedRosterMessage(Message);
        }
        for (const FString& Message : MalformedLobbies) RejectMalformedRosterMessage(Message);
    }

    {
        FOnlineRecoveryFixture Fixture;
        UCinderOnlineSubsystem& Session = *Fixture.Session;
        TestFalse(TEXT("A fresh subsystem has no session to leave"), Session.CanLeaveSession());
        TestFalse(TEXT("A fresh subsystem has no credentials to reconnect"), Session.CanReconnectSession());
        StartSession(Session);
        Session.NextSequence = 41;
        Session.SnapshotDeadline = FPlatformTime::Seconds() - 1;
        const double Deadline = Session.SnapshotDeadline;
        Session.HandleText(Started);
        TestEqual(TEXT("Repeated started messages cannot extend the first snapshot deadline"), Session.SnapshotDeadline, Deadline);
        ReceivePong(Session);
        Session.Tick(0);
        TestTrue(TEXT("Pongs cannot keep a started match waiting indefinitely for its first snapshot"),
            Session.State() == ECinderOnlineState::Reconnecting && Session.SnapshotDeadline == 0
            && Session.HasMatch() && !Session.LatestSnapshot() && !Session.Socket);
        TestTrue(TEXT("First snapshot timeout retains the seat and schedules a bounded recovery"),
            Session.RoomCode() == TEXT("ABCDEF") && Session.SeatToken == TEXT("fixture-seat-token-123456")
            && Session.CanReconnectSession() && Session.CanLeaveSession()
            && Session.ReconnectDeadline > FPlatformTime::Seconds()
            && Session.NextReconnectAt > FPlatformTime::Seconds());
        TestEqual(TEXT("First snapshot timeout preserves the next command sequence"), Session.NextSequence, uint32(41));

        // OpenSocket normally arms this deadline for a resumed match. Keep the
        // scheduled attempt in the future so this fixture never opens a socket.
        Session.NextReconnectAt = FPlatformTime::Seconds() + 600;
        Session.SnapshotDeadline = FPlatformTime::Seconds() - 1;
        ReceivePong(Session);
        const uint64 Generation = Session.SocketGeneration;
        Session.Tick(0);
        TestTrue(TEXT("A resumed match also expires its snapshot deadline despite receiving pongs"),
            Session.State() == ECinderOnlineState::Reconnecting && Session.SnapshotDeadline == 0
            && Session.SocketGeneration > Generation && !Session.Socket);
        TestEqual(TEXT("Resumed snapshot timeout preserves sequence numbering"), Session.NextSequence, uint32(41));
        Session.Leave();
    }

    {
        FOnlineRecoveryFixture Fixture;
        UCinderOnlineSubsystem& Session = *Fixture.Session;
        StartSession(Session);
        Session.PendingCommands.Add(101, FPlatformTime::Seconds());
        Session.PendingCommands.Add(102, FPlatformTime::Seconds());
        const uint64 FeedbackBefore = Session.FeedbackSerial();
        for (const TCHAR* InvalidSequence : {TEXT("101.5"), TEXT("\"101\""), TEXT("0"), TEXT("-1"),
            TEXT("4294967296"), TEXT("null"), TEXT("true")})
        {
            Session.HandleText(FString::Printf(TEXT("{\"type\":\"ack\",\"seq\":%s,\"accepted\":true}"), InvalidSequence));
            TestTrue(TEXT("Malformed acknowledgement cannot clear a pending command or publish feedback"),
                Session.PendingCommands.Num() == 2 && Session.PendingCommands.Contains(101)
                && Session.PendingCommands.Contains(102) && Session.FeedbackSerial() == FeedbackBefore);
        }
        Session.HandleText(TEXT("{\"type\":\"ack\",\"seq\":101,\"accepted\":true}"));
        TestTrue(TEXT("An integral acknowledgement resolves only its exact pending command"),
            Session.PendingCommands.Num() == 1 && Session.PendingCommands.Contains(102)
            && Session.LastOrderAccepted() && Session.FeedbackSerial() == FeedbackBefore + 1);
        Session.HandleText(TEXT("{\"type\":\"ack\",\"seq\":101,\"accepted\":false}"));
        TestTrue(TEXT("Duplicate acknowledgement cannot revise feedback or affect another command"),
            Session.PendingCommands.Contains(102) && Session.LastOrderAccepted()
            && Session.FeedbackSerial() == FeedbackBefore + 1);
        Session.Leave();
    }

    {
        FOnlineRecoveryFixture Fixture;
        UCinderOnlineSubsystem& Session = *Fixture.Session;
        StartSession(Session);
        const SIZE_T FirstSize = Bytes.size() / 2;
        const double Deadline = Session.SnapshotDeadline;
        Session.HandleBinary(Bytes.data(), FirstSize, false);
        TestTrue(TEXT("A partial snapshot does not mark the battlefield synchronized or extend its deadline"),
            Session.State() == ECinderOnlineState::Starting && !Session.LatestSnapshot()
            && Session.SnapshotSerial() == 0 && Session.SnapshotDeadline == Deadline);
        Session.ReconnectAttempts = 4;
        Session.ReconnectDeadline = FPlatformTime::Seconds() + 60;
        Session.HandleBinary(Bytes.data() + FirstSize, Bytes.size() - FirstSize, true);
        if (!TestNotNull(TEXT("A validated complete snapshot becomes available"), Session.LatestSnapshot())) return false;
        TestTrue(TEXT("The complete binary snapshot enters Playing and ends the recovery episode"),
            Session.State() == ECinderOnlineState::Playing && Session.HasMatch()
            && Session.SnapshotSerial() == 1 && Session.SnapshotDeadline == 0
            && Session.ReconnectDeadline == 0 && Session.ReconnectAttempts == 0
            && Session.BinaryBuffer.IsEmpty() && !Session.Socket);
        TestEqual(TEXT("The published snapshot matches the authoritative tick"), Session.LatestSnapshot()->tick, Frame.tick);
        TestFalse(TEXT("A snapshot alone cannot authorize orders without a connected transport"), Session.CanSendOrders());

        const double Now = FPlatformTime::Seconds();
        Session.PendingCommands.Add(101, Now - 6);
        Session.PendingCommands.Add(102, Now - 1);
        Session.NextSequence = 103;
        const uint64 FeedbackBefore = Session.FeedbackSerial();
        Session.Tick(0);
        TestTrue(TEXT("One expired acknowledgement resynchronizes even when the transport is already absent"),
            Session.State() == ECinderOnlineState::Reconnecting && Session.PendingCommands.IsEmpty()
            && !Session.LastOrderAccepted() && !Session.OrderFeedback().IsEmpty() && !Session.Socket);
        TestEqual(TEXT("The entire uncertain batch produces one feedback event"), Session.FeedbackSerial(), FeedbackBefore + 1);
        TestEqual(TEXT("Uncertain commands are not renumbered for replay"), Session.NextSequence, uint32(103));
        Session.NextReconnectAt = FPlatformTime::Seconds() + 600;
        Session.Tick(0);
        Session.HandleText(TEXT("{\"type\":\"ack\",\"seq\":101,\"accepted\":true,\"message\":\"Late acceptance\"}"));
        Session.HandleText(TEXT("{\"type\":\"ack\",\"seq\":102,\"accepted\":false,\"message\":\"Late rejection\"}"));
        TestTrue(TEXT("Later ticks and stale acknowledgements neither repeat feedback nor resolve discarded pending commands"),
            Session.FeedbackSerial() == FeedbackBefore + 1 && !Session.LastOrderAccepted()
            && Session.NextSequence == 103 && Session.PendingCommands.IsEmpty() && !Session.Socket);
        Session.HandleBinary(Bytes.data(), Bytes.size(), true);
        TestTrue(TEXT("A refreshed snapshot restores Playing without replaying uncertain commands"),
            Session.State() == ECinderOnlineState::Playing && Session.NextSequence == 103
            && Session.PendingCommands.IsEmpty() && Session.FeedbackSerial() == FeedbackBefore + 1
            && Session.SnapshotDeadline == 0 && Session.ReconnectDeadline == 0);
        Session.Leave();
    }

    for (const ECinderOnlineState State : {ECinderOnlineState::Reconnecting, ECinderOnlineState::Starting})
    {
        FOnlineRecoveryFixture Fixture;
        UCinderOnlineSubsystem& Session = *Fixture.Session;
        StartSession(Session);
        Session.CurrentState = State;
        Session.NextSequence = 73;
        Session.ReconnectAttempts = 5;
        Session.NextReconnectAt = FPlatformTime::Seconds() + 600;
        Session.ReconnectDeadline = FPlatformTime::Seconds() - 1;
        Session.SnapshotDeadline = FPlatformTime::Seconds() + 30;
        ReceivePong(Session);
        Session.Tick(0);
        TestTrue(TEXT("The automatic recovery window expires in both Reconnecting and Starting despite a future snapshot deadline"),
            Session.State() == ECinderOnlineState::Error && Session.SnapshotDeadline == 0
            && Session.CanReconnectSession() && Session.CanLeaveSession()
            && Session.RoomCode() == TEXT("ABCDEF") && !Session.SeatToken.IsEmpty() && !Session.Socket);
        TestEqual(TEXT("Recovery-window expiry retains the command sequence"), Session.NextSequence, uint32(73));

        Session.PendingCommands.Add(72, FPlatformTime::Seconds() - 1);
        const uint64 FeedbackBefore = Session.FeedbackSerial();
        const double RetryAt = FPlatformTime::Seconds();
        TestTrue(TEXT("Explicit retry preparation permits recovery of an expired seat"), Session.PrepareReconnect(RetryAt));
        TestEqual(TEXT("Explicit retry starts a fresh sixty-second episode"), Session.ReconnectDeadline, RetryAt + 60.0);
        TestTrue(TEXT("Explicit retry resets attempts and reports uncertainty once without changing sequence or opening a socket"),
            Session.ReconnectAttempts == 1 && Session.NextSequence == 73
            && Session.PendingCommands.IsEmpty() && Session.FeedbackSerial() == FeedbackBefore + 1 && !Session.Socket);

        // The public Reconnect method would set this state when opening the
        // socket. Exercise only its preparation helper in this fixture.
        Session.CurrentState = ECinderOnlineState::Reconnecting;
        for (int32 Attempt = 2; Attempt <= 12; ++Attempt)
            TestTrue(TEXT("Each attempt inside the recovery budget may be prepared"), Session.PrepareReconnect(RetryAt));
        TestEqual(TEXT("A recovery episode permits twelve attempts"), Session.ReconnectAttempts, 12);
        TestFalse(TEXT("A thirteenth automatic attempt is refused"), Session.PrepareReconnect(RetryAt));
        TestTrue(TEXT("Attempt exhaustion retains manual recovery and leave while preserving sequence and the original deadline"),
            Session.State() == ECinderOnlineState::Error && Session.CanReconnectSession() && Session.CanLeaveSession()
            && Session.NextSequence == 73 && Session.ReconnectDeadline == RetryAt + 60.0 && !Session.Socket);

        Session.CurrentState = ECinderOnlineState::Reconnecting;
        Session.NextReconnectAt = FPlatformTime::Seconds() + 600;
        Session.Tick(0);
        TestTrue(TEXT("Tick terminates an exhausted automatic episode even before its next attempt is due"),
            Session.State() == ECinderOnlineState::Error && Session.ReconnectAttempts == 12 && !Session.Socket);
        Session.Leave();
    }

    {
        FOnlineRecoveryFixture Fixture;
        UCinderOnlineSubsystem& Session = *Fixture.Session;
        StartSession(Session);
        Session.CurrentState = ECinderOnlineState::Reconnecting;
        Session.ReconnectAttempts = 4;
        const double Deadline = FPlatformTime::Seconds() + 60;
        Session.ReconnectDeadline = Deadline;
        Session.NextSequence = 91;
        const FString RaceMessages[] = {
            TEXT("{\"type\":\"error\",\"code\":\"seat_connected\",\"retryable\":true,\"message\":\"Previous transport still attached\"}"),
            TEXT("{\"type\":\"error\",\"message\":\"That seat is already connected.\"}"),
        };
        for (const FString& Message : RaceMessages)
        {
            Session.SnapshotDeadline = FPlatformTime::Seconds() + 12;
            const double Before = FPlatformTime::Seconds();
            Session.HandleText(Message);
            const double After = FPlatformTime::Seconds();
            TestTrue(TEXT("Structured and legacy seat races wait for the previous socket within the existing episode"),
                Session.State() == ECinderOnlineState::Reconnecting && Session.CanReconnectSession()
                && Session.ErrorText().IsEmpty() && Session.ReconnectDeadline == Deadline
                && Session.ReconnectAttempts == 4 && Session.NextSequence == 91
                && Session.SnapshotDeadline == 0 && !Session.Socket
                && Session.NextReconnectAt >= Before + 0.5 && Session.NextReconnectAt <= After + 5.0);
        }
        Session.HandleText(TEXT("{\"type\":\"error\",\"code\":\"invalid_credentials\",\"retryable\":false,\"message\":\"Resume rejected.\"}"));
        TestTrue(TEXT("Terminal structured credentials errors end recovery but leave the session dismissible"),
            Session.State() == ECinderOnlineState::Error && !Session.bResumeAllowed
            && !Session.CanReconnectSession() && Session.CanLeaveSession()
            && Session.ErrorText() == TEXT("Resume rejected.") && Session.SnapshotDeadline == 0);
        TestFalse(TEXT("Terminal credentials cannot prepare another reconnect"), Session.PrepareReconnect(FPlatformTime::Seconds()));
        TestEqual(TEXT("Terminal failure does not reuse command sequence numbers"), Session.NextSequence, uint32(91));
        Session.Leave();
    }

    for (int32 Attempt = -1; Attempt <= 32; ++Attempt)
        for (int32 Sample = 0; Sample < 4; ++Sample)
        {
            const double Delay = UCinderOnlineSubsystem::ReconnectDelay(Attempt);
            TestTrue(TEXT("Jittered retry delay remains finite and between half a second and five seconds"),
                FMath::IsFinite(Delay) && Delay >= 0.5 && Delay <= 5.0);
        }

    {
        FOnlineRecoveryFixture Fixture;
        UCinderOnlineSubsystem& Session = *Fixture.Session;
        StartSession(Session);
        Session.PendingCommands.Add(1, FPlatformTime::Seconds());
        Session.HandleText(TEXT("{\"type\":\"lobby\",\"playerCount\":2,\"players\":[{\"name\":\"First\",\"connected\":true,\"ready\":true},{\"name\":\"Second\",\"connected\":true,\"ready\":true}]}"));
        Session.Leave();
        TestTrue(TEXT("Leaving before the first snapshot clears match state and private recovery credentials"),
            Session.State() == ECinderOnlineState::Offline && !Session.HasMatch() && !Session.LatestSnapshot()
            && !Session.HasRoom() && Session.SeatToken.IsEmpty() && Session.LocalSeat() == -1
            && !Session.bResumeAllowed && !Session.CanLeaveSession() && !Session.CanReconnectSession()
            && Session.SnapshotDeadline == 0 && Session.ReconnectDeadline == 0
            && Session.PendingCommands.IsEmpty() && Session.BinaryBuffer.IsEmpty() && !Session.Socket);
        TestTrue(TEXT("Leaving also clears lobby seats and readiness"),
            !Session.SeatConnected(0) && !Session.SeatConnected(1) && !Session.SeatReady(0) && !Session.SeatReady(1)
            && Session.SeatName(0).IsEmpty() && Session.SeatName(1).IsEmpty());
    }

    {
        FTestWorldWrapper WorldOwner;
        if (!WorldOwner.CreateTestWorld(EWorldType::Game))
        {
            WorldOwner.ForwardErrorMessages(this);
            return false;
        }
        UWorld* World = WorldOwner.GetTestWorld();
        if (!TestNotNull(TEXT("Controller recovery world exists"), World)) return false;
        World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
        ACinderBattlefield* Battle = World->SpawnActor<ACinderBattlefield>();
        if (!TestNotNull(TEXT("Controller recovery battlefield exists"), Battle)) return false;
        if (!WorldOwner.BeginPlayInTestWorld())
        {
            WorldOwner.ForwardErrorMessages(this);
            return false;
        }
        ACinderPlayerController* Controller = World->SpawnActor<ACinderPlayerController>();
        if (!TestNotNull(TEXT("Controller recovery controller exists"), Controller)) return false;
        if (!TestTrue(TEXT("Controller recovery fixture resolves its battlefield"), Controller->Battlefield() == Battle)) return false;
        Controller->ExecuteAction(TEXT("start"), 0);
        UCinderOnlineSubsystem* Session = Controller->Online();
        if (!TestNotNull(TEXT("Controller uses the game instance online subsystem"), Session)) return false;
        StartSession(*Session);
        if (!TestTrue(TEXT("The transport has started while the battlefield remains a local match"),
            !Battle->IsMenu() && !Battle->IsOnlineMatch() && Session->HasMatch() && !Session->LatestSnapshot())) return false;
        const auto LocalHash = Battle->Sim().stateHash();
        const auto RecordingSize = Battle->Sim().recording().size();
        Controller->ExecuteAction(TEXT("onlineleave"));
        TestTrue(TEXT("Controller Leave clears a half-started session without requiring an online battlefield"),
            Session->State() == ECinderOnlineState::Offline && !Session->CanLeaveSession()
            && !Session->CanReconnectSession() && !Session->HasMatch() && !Session->LatestSnapshot()
            && !Session->HasRoom() && Session->SeatToken.IsEmpty() && !Session->Socket
            && !Controller->IsOnlineLeavePending());
        TestTrue(TEXT("Canceling that session leaves the local battlefield and recording unchanged"),
            !Battle->IsMenu() && !Battle->IsOnlineMatch() && Battle->Sim().stateHash() == LocalHash
            && Battle->Sim().recording().size() == RecordingSize);

        // Exercise the shared mouse/touch world-hit dispatcher against a real
        // authority so the resulting target/order is observable immediately.
        // Fixture placement supplies visible enemies without advancing combat.
        auto& InputSimulation = Battle->Sim();
        cinder::Config InputConfig; InputConfig.ai = false; InputConfig.playerCount = 4;
        InputSimulation.reset(InputConfig);
        Controller->ResetInteraction(true);
        const cinder::Id Attacker = InputSimulation.debugSpawn(cinder::Kind::Striker, 0, {350, 350});
        for (int32 EnemyTeam = 1; EnemyTeam < 4; ++EnemyTeam)
        {
            const cinder::Vec2 Point{450.0f + EnemyTeam * 40.0f, 350};
            const cinder::Id Target = InputSimulation.debugSpawn(cinder::Kind::Striker, EnemyTeam, Point);
            if (!TestTrue(TEXT("Every opponent tap fixture is alive and in the viewer's current vision"),
                InputSimulation.find(Target) && InputSimulation.find(Target)->alive() && InputSimulation.visible(0, Point))) return false;
            for (bool bForceCommand : {false, true})
            {
                if (!TestTrue(TEXT("A friendly attacker is selected before direct target input"), Controller->SelectOwnedEntity(Attacker))) return false;
                const auto BeforeTap = InputSimulation.recording().size();
                // A normal tap has a ground ray; desktop context targeting also
                // uses the entity point when that ray misses the world edge.
                Controller->HandleWorldTap(Target, bForceCommand ? nullptr : &Point, bForceCommand);
                if (!TestTrue(TEXT("Direct taps on every opponent submit exactly one Attack with that entity ID"),
                    InputSimulation.recording().size() == BeforeTap + 1)) return false;
                const auto& Issued = InputSimulation.recording().back().command;
                TestTrue(TEXT("Opponent teams one, two and three remain explicit targets rather than ground moves"),
                    Issued.type == cinder::CommandType::Attack && Issued.target == Target
                    && InputSimulation.find(Attacker)->order == cinder::Order::Attack
                    && InputSimulation.find(Attacker)->target == Target);
            }
        }
        const cinder::Id Friendly = InputSimulation.debugSpawn(cinder::Kind::Striker, 0, {400, 350});
        const auto BeforeFriendlyTap = InputSimulation.recording().size();
        const cinder::Vec2 FriendlyPoint = InputSimulation.find(Friendly)->pos;
        Controller->HandleWorldTap(Friendly, &FriendlyPoint);
        TestTrue(TEXT("Normal friendly taps still select without issuing a move or attack"),
            Controller->Selection().size() == 1 && Controller->Selection().front() == Friendly
            && InputSimulation.recording().size() == BeforeFriendlyTap);
    }

    {
        // SetConnectionMode normally persists the player's choice. Require the
        // real automation guard before exercising it; never override that flag.
        if (!TestTrue(TEXT("Preference tests run with the actual automation write guard enabled"), GIsAutomationTesting)) return false;
        const FString PlayerPreferences = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Config/Online.ini"));
        const bool bHadPlayerPreferences = IFileManager::Get().FileExists(*PlayerPreferences);
        TArray<uint8> PlayerPreferencesBefore;
        if (bHadPlayerPreferences && !TestTrue(TEXT("Existing player preferences can be read for preservation evidence"),
            FFileHelper::LoadFileToArray(PlayerPreferencesBefore, *PlayerPreferences))) return false;

        FTemporaryOnlinePreferences Files;
        const FString DefaultEndpoint = TEXT("ws://127.0.0.1:8787/play");
        const FString InternetEndpoint = TEXT("wss://internet.example:8443/play");
        const FString LANEndpoint = TEXT("ws://192.168.4.7:8787/play");
        const auto SeedDefaultEndpoints = [&](UCinderOnlineSubsystem& Session)
        {
            Session.InternetServerEndpoint = Session.LANServerEndpoint = Session.ServerEndpoint = DefaultEndpoint;
        };
        const auto WritePreferences = [&](const TCHAR* Legacy, const TCHAR* StoredMode,
            const TCHAR* Internet = nullptr, const TCHAR* LAN = nullptr)
        {
            FConfigFile Settings;
            Settings.bCanSaveAllSections = true;
            Settings.SetString(TEXT("OtherSettings"), TEXT("Keep"), TEXT("unchanged"));
            Settings.SetString(TEXT("Online"), TEXT("Extra"), TEXT("also unchanged"));
            Settings.SetString(TEXT("Online"), TEXT("Name"), TEXT("Fixture Commander"));
            if (Legacy) Settings.SetString(TEXT("Online"), TEXT("ServerUrl"), Legacy);
            if (StoredMode) Settings.SetString(TEXT("Online"), TEXT("ConnectionMode"), StoredMode);
            if (Internet) Settings.SetString(TEXT("Online"), TEXT("InternetServerUrl"), Internet);
            if (LAN) Settings.SetString(TEXT("Online"), TEXT("LANServerUrl"), LAN);
            return TestTrue(TEXT("The fixture writes only its unique temporary preferences file"), Settings.Write(Files.Filename));
        };

        struct FLegacyPreferenceCase
        {
            const TCHAR* Endpoint;
            ECinderConnectionMode Mode;
            const TCHAR* Normalized;
        };
        const FLegacyPreferenceCase LegacyCases[] = {
            {TEXT("wss://legacy.example/play"), ECinderConnectionMode::Internet, TEXT("wss://legacy.example/play")},
            {TEXT("ws://192.168.7.8:8787"), ECinderConnectionMode::LocalNetwork, TEXT("ws://192.168.7.8:8787/play")},
            {TEXT("ws://127.19.8.7:8787/play"), ECinderConnectionMode::Internet, TEXT("ws://127.19.8.7:8787/play")},
        };
        for (const FLegacyPreferenceCase& Case : LegacyCases)
        {
            if (!WritePreferences(Case.Endpoint, nullptr)) return false;
            FOnlineRecoveryFixture Fixture;
            UCinderOnlineSubsystem& Session = *Fixture.Session;
            SeedDefaultEndpoints(Session);
            Session.LoadPreferences(Files.Filename);
            TestTrue(FString::Printf(TEXT("Legacy endpoint migrates into its compatible connection mode: %s"), Case.Endpoint),
                Session.ConnectionMode() == Case.Mode && Session.Endpoint() == Case.Normalized
                && Session.PlayerName() == TEXT("Fixture Commander"));
            TestEqual(TEXT("Migrating one endpoint preserves the other mode's default"),
                Case.Mode == ECinderConnectionMode::Internet ? Session.LANServerEndpoint : Session.InternetServerEndpoint,
                DefaultEndpoint);
        }

        for (const TCHAR* InvalidMode : {TEXT("junk"), TEXT("2"), TEXT("-1"), TEXT("1junk"), TEXT("01")})
        {
            if (!WritePreferences(TEXT("ws://192.168.7.8/play"), InvalidMode, *InternetEndpoint, *LANEndpoint)) return false;
            FOnlineRecoveryFixture Fixture;
            UCinderOnlineSubsystem& Session = *Fixture.Session;
            SeedDefaultEndpoints(Session);
            Session.LoadPreferences(Files.Filename);
            TestTrue(TEXT("An invalid saved mode falls back to Internet without replacing either explicit endpoint"),
                Session.ConnectionMode() == ECinderConnectionMode::Internet && Session.Endpoint() == InternetEndpoint
                && Session.InternetServerEndpoint == InternetEndpoint && Session.LANServerEndpoint == LANEndpoint);
        }

        if (!WritePreferences(TEXT("wss://old.example/play"), TEXT("0"), *InternetEndpoint, *LANEndpoint)) return false;
        FOnlineRecoveryFixture Fixture;
        UCinderOnlineSubsystem& Session = *Fixture.Session;
        SeedDefaultEndpoints(Session);
        Session.LoadPreferences(Files.Filename);
        TestTrue(TEXT("Explicit Internet settings take precedence over the legacy endpoint"),
            Session.ConnectionMode() == ECinderConnectionMode::Internet && Session.Endpoint() == InternetEndpoint);
        TestTrue(TEXT("An idle session can choose its saved LAN endpoint"), Session.SetConnectionMode(ECinderConnectionMode::LocalNetwork));
        TestEqual(TEXT("LAN selection restores its own address"), Session.Endpoint(), LANEndpoint);
        TestTrue(TEXT("An idle session can return to its saved Internet endpoint"), Session.SetConnectionMode(ECinderConnectionMode::Internet));
        TestEqual(TEXT("Internet selection restores its own address"), Session.Endpoint(), InternetEndpoint);
        TestTrue(TEXT("Switching again retains both addresses"), Session.SetConnectionMode(ECinderConnectionMode::LocalNetwork)
            && Session.InternetServerEndpoint == InternetEndpoint && Session.LANServerEndpoint == LANEndpoint);
        Session.SavePreferences(Files.Filename);
        FConfigFile Readback;
        Readback.Read(Files.Filename);
        FString SavedInternet, SavedLAN, SavedLegacy, SavedMode, SavedName, Extra, Other;
        TestTrue(TEXT("Saving preserves both mode URLs, the active legacy URL, mode, and player name"),
            Readback.GetString(TEXT("Online"), TEXT("InternetServerUrl"), SavedInternet) && SavedInternet == InternetEndpoint
            && Readback.GetString(TEXT("Online"), TEXT("LANServerUrl"), SavedLAN) && SavedLAN == LANEndpoint
            && Readback.GetString(TEXT("Online"), TEXT("ServerUrl"), SavedLegacy) && SavedLegacy == LANEndpoint
            && Readback.GetString(TEXT("Online"), TEXT("ConnectionMode"), SavedMode) && SavedMode == TEXT("1")
            && Readback.GetString(TEXT("Online"), TEXT("Name"), SavedName) && SavedName == TEXT("Fixture Commander"));
        TestTrue(TEXT("Saving online preferences preserves unrelated keys in the same and other sections"),
            Readback.GetString(TEXT("Online"), TEXT("Extra"), Extra) && Extra == TEXT("also unchanged")
            && Readback.GetString(TEXT("OtherSettings"), TEXT("Keep"), Other) && Other == TEXT("unchanged"));

        FOnlineRecoveryFixture ReloadedFixture;
        UCinderOnlineSubsystem& Reloaded = *ReloadedFixture.Session;
        SeedDefaultEndpoints(Reloaded);
        Reloaded.LoadPreferences(Files.Filename);
        TestTrue(TEXT("A fresh subsystem reloads the selected mode and both independent endpoints"),
            Reloaded.ConnectionMode() == ECinderConnectionMode::LocalNetwork && Reloaded.Endpoint() == LANEndpoint
            && Reloaded.InternetServerEndpoint == InternetEndpoint && Reloaded.LANServerEndpoint == LANEndpoint);
        TestTrue(TEXT("The Internet endpoint survives saving while LAN was selected"),
            Reloaded.SetConnectionMode(ECinderConnectionMode::Internet) && Reloaded.Endpoint() == InternetEndpoint);
        TestTrue(TEXT("The LAN endpoint survives the full settings round trip"),
            Reloaded.SetConnectionMode(ECinderConnectionMode::LocalNetwork) && Reloaded.Endpoint() == LANEndpoint);
        TestFalse(TEXT("An unknown connection mode is rejected"), Reloaded.SetConnectionMode(static_cast<ECinderConnectionMode>(99)));
        TestTrue(TEXT("Rejecting an unknown mode preserves the current selection and endpoint"),
            Reloaded.ConnectionMode() == ECinderConnectionMode::LocalNetwork && Reloaded.Endpoint() == LANEndpoint);

        const ECinderOnlineState ActiveStates[] = {
            ECinderOnlineState::Connecting, ECinderOnlineState::Lobby, ECinderOnlineState::Starting,
            ECinderOnlineState::Playing, ECinderOnlineState::Reconnecting, ECinderOnlineState::Leaving,
            ECinderOnlineState::Finished,
        };
        for (const ECinderOnlineState State : ActiveStates)
        {
            Reloaded.CurrentState = State;
            TestFalse(TEXT("An active connection state refuses mode changes even before credentials arrive"),
                Reloaded.SetConnectionMode(ECinderConnectionMode::Internet));
            TestTrue(TEXT("A refused mode change preserves state, mode, and endpoint"),
                Reloaded.State() == State && Reloaded.ConnectionMode() == ECinderConnectionMode::LocalNetwork
                && Reloaded.Endpoint() == LANEndpoint && !Reloaded.Socket);
        }
        for (const ECinderOnlineState State : {ECinderOnlineState::Offline, ECinderOnlineState::Error})
        {
            Reloaded.CurrentState = State;
            Reloaded.Room = TEXT("ABCDEF");
            TestFalse(TEXT("A retained room prevents mode changes even in Offline or Error"),
                Reloaded.SetConnectionMode(ECinderConnectionMode::Internet));
            Reloaded.Room.Empty();
            Reloaded.bHasMatch = true;
            TestFalse(TEXT("A retained match prevents mode changes even without a room code"),
                Reloaded.SetConnectionMode(ECinderConnectionMode::Internet));
            Reloaded.bHasMatch = false;
            TestTrue(TEXT("Empty Offline and Error states permit changing to Internet"),
                Reloaded.SetConnectionMode(ECinderConnectionMode::Internet) && Reloaded.Endpoint() == InternetEndpoint);
            TestTrue(TEXT("Empty Offline and Error states permit changing back to LAN"),
                Reloaded.SetConnectionMode(ECinderConnectionMode::LocalNetwork) && Reloaded.Endpoint() == LANEndpoint);
        }
        Reloaded.Leave();

        const bool bHasPlayerPreferencesAfter = IFileManager::Get().FileExists(*PlayerPreferences);
        TArray<uint8> PlayerPreferencesAfter;
        const bool bReadPlayerPreferencesAfter = !bHasPlayerPreferencesAfter
            || FFileHelper::LoadFileToArray(PlayerPreferencesAfter, *PlayerPreferences);
        TestTrue(TEXT("Automatic mode persistence leaves the real player preferences file unchanged during automation"),
            bReadPlayerPreferencesAfter && bHadPlayerPreferences == bHasPlayerPreferencesAfter
            && PlayerPreferencesBefore == PlayerPreferencesAfter);
    }

    if (!HasAnyErrors())
        UE_LOG(LogTemp, Display, TEXT("CINDERLINE_ONLINE_RECOVERY_INTEGRATION PASS"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderOnlineEndpointPolicy,
    "Cinderline.Integration.OnlineEndpointPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderOnlineEndpointPolicy::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using Mode = ECinderConnectionMode;
    struct FEndpointCase
    {
        const TCHAR* Input;
        Mode ConnectionMode;
        bool bAccepted;
        const TCHAR* Normalized;
    };
    const FEndpointCase Cases[] = {
        {TEXT("wss://relay.example/play"), Mode::Internet, true, TEXT("wss://relay.example/play")},
        {TEXT(" \tWSS://RELAY.EXAMPLE:443 \r\n"), Mode::Internet, true, TEXT("wss://relay.example:443/play")},
        {TEXT("ws://localhost:8787"), Mode::Internet, true, TEXT("ws://localhost:8787/play")},
        {TEXT("ws://127.19.8.7/play"), Mode::Internet, true, TEXT("ws://127.19.8.7/play")},
        {TEXT("ws://[::1]:8787"), Mode::Internet, true, TEXT("ws://[::1]:8787/play")},
        {TEXT("wss://[2001:db8::1]:443/play"), Mode::Internet, true, TEXT("wss://[2001:db8::1]:443/play")},
        {TEXT("ws://relay.example/play"), Mode::Internet, false, TEXT("")},
        {TEXT("ws://192.168.4.2:8787/play"), Mode::Internet, false, TEXT("")},
        {TEXT("ws://cinder-host.local/play"), Mode::Internet, false, TEXT("")},
        {TEXT("ws://10.2.3.4:8787"), Mode::LocalNetwork, true, TEXT("ws://10.2.3.4:8787/play")},
        {TEXT("ws://172.16.0.1/play"), Mode::LocalNetwork, true, TEXT("ws://172.16.0.1/play")},
        {TEXT("ws://172.31.255.254/play"), Mode::LocalNetwork, true, TEXT("ws://172.31.255.254/play")},
        {TEXT("ws://192.168.4.2/play"), Mode::LocalNetwork, true, TEXT("ws://192.168.4.2/play")},
        {TEXT("ws://169.254.4.2/play"), Mode::LocalNetwork, true, TEXT("ws://169.254.4.2/play")},
        {TEXT("ws://127.19.8.7/play"), Mode::LocalNetwork, true, TEXT("ws://127.19.8.7/play")},
        {TEXT("ws://Cinder-Host.local:8787"), Mode::LocalNetwork, true, TEXT("ws://cinder-host.local:8787/play")},
        {TEXT("ws://CINDERHOST/play"), Mode::LocalNetwork, true, TEXT("ws://cinderhost/play")},
        {TEXT("ws://[::1]/play"), Mode::LocalNetwork, true, TEXT("ws://[::1]/play")},
        {TEXT("ws://[fc00::1]/play"), Mode::LocalNetwork, true, TEXT("ws://[fc00::1]/play")},
        {TEXT("ws://[fd12:3456::1]/play"), Mode::LocalNetwork, true, TEXT("ws://[fd12:3456::1]/play")},
        {TEXT("ws://[fe80::1]/play"), Mode::LocalNetwork, true, TEXT("ws://[fe80::1]/play")},
        {TEXT("ws://[febf::1]/play"), Mode::LocalNetwork, true, TEXT("ws://[febf::1]/play")},
        {TEXT("ws://[::ffff:192.168.4.2]/play"), Mode::LocalNetwork, true, TEXT("ws://[::ffff:192.168.4.2]/play")},
        {TEXT("wss://relay.example/play"), Mode::LocalNetwork, true, TEXT("wss://relay.example/play")},
        {TEXT("ws://172.15.255.254/play"), Mode::LocalNetwork, false, TEXT("")},
        {TEXT("ws://172.32.0.1/play"), Mode::LocalNetwork, false, TEXT("")},
        {TEXT("ws://100.64.0.1/play"), Mode::LocalNetwork, false, TEXT("")},
        {TEXT("ws://8.8.8.8/play"), Mode::LocalNetwork, false, TEXT("")},
        {TEXT("ws://relay.example/play"), Mode::LocalNetwork, false, TEXT("")},
        {TEXT("ws://[fec0::1]/play"), Mode::LocalNetwork, false, TEXT("")},
        {TEXT("ws://[2001:db8::1]/play"), Mode::LocalNetwork, false, TEXT("")},
        {TEXT("ws://[::ffff:8.8.8.8]/play"), Mode::LocalNetwork, false, TEXT("")},
        {TEXT("wss://relay.example:1/play"), Mode::Internet, true, TEXT("wss://relay.example:1/play")},
        {TEXT("wss://relay.example:65535/play"), Mode::Internet, true, TEXT("wss://relay.example:65535/play")},
    };
    for (const FEndpointCase& Case : Cases)
    {
        FString Normalized = TEXT("stale endpoint"), Error = TEXT("stale error");
        const FString Label = FString::Printf(TEXT("Endpoint policy for %s in mode %d"), Case.Input, static_cast<int32>(Case.ConnectionMode));
        const bool bAccepted = UCinderOnlineSubsystem::ValidateEndpoint(Case.Input, Case.ConnectionMode, Normalized, Error);
        TestEqual(Label, bAccepted, Case.bAccepted);
        TestEqual(Label + TEXT(" returns only the normalized accepted endpoint"), Normalized, FString(Case.Normalized));
        TestTrue(Label + TEXT(" gives feedback exactly when validation fails"), bAccepted ? Error.IsEmpty() : !Error.IsEmpty());
    }

    // The numeric parser logs each rejected address once per connection mode.
    // Match those two inputs only; inet_pton leaves the last socket error intact,
    // so its diagnostic error suffix can differ with earlier transport activity.
    AddExpectedMessage(TEXT("Could not serialize 256\\.1\\.2\\.3, got error code SE_[A-Z_]+ \\[[0-9]+\\]"),
        ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Exact, 2);
    AddExpectedMessage(TEXT("Could not serialize 2001::db8::1, got error code SE_[A-Z_]+ \\[[0-9]+\\]"),
        ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Exact, 2);
    const TCHAR* Malformed[] = {
        TEXT(""), TEXT("https://relay.example/play"), TEXT("wss://"),
        TEXT("wss://user:secret@relay.example/play"), TEXT("wss://relay.example/play?room=ABCDEF"),
        TEXT("wss://relay.example/play#room"), TEXT("wss://relay.example/other"),
        TEXT("wss://relay.example/play/"), TEXT("wss://relay.example/PLAY"),
        TEXT("wss://relay.example:0/play"), TEXT("wss://relay.example:65536/play"),
        TEXT("wss://relay.example:-1/play"), TEXT("wss://relay.example:abc/play"),
        TEXT("wss://relay.example:/play"), TEXT("wss://relay..example/play"),
        TEXT("wss://-relay.example/play"), TEXT("wss://relay-.example/play"),
        TEXT("wss://relay_name.example/play"), TEXT("wss://256.1.2.3/play"),
        TEXT("wss://::1/play"), TEXT("wss://[::1/play"), TEXT("wss://[::1]extra/play"),
        TEXT("wss://[2001::db8::1]/play"), TEXT("wss://[fe80::1%en0]/play"),
        TEXT("wss://relay.\u00e9xample/play"), TEXT("wss://relay .example/play"),
        TEXT("wss://relay.example/pl\nay"), TEXT("wss://relay.example\\play"),
    };
    for (const Mode ConnectionMode : {Mode::Internet, Mode::LocalNetwork})
        for (const TCHAR* Input : Malformed)
        {
            FString Normalized = TEXT("stale endpoint"), Error;
            TestFalse(FString::Printf(TEXT("Malformed endpoint is rejected in mode %d: %s"), static_cast<int32>(ConnectionMode), Input),
                UCinderOnlineSubsystem::ValidateEndpoint(Input, ConnectionMode, Normalized, Error));
            TestTrue(TEXT("Rejected endpoints clear any old destination and explain the validation failure"),
                Normalized.IsEmpty() && !Error.IsEmpty());
        }

    if (!HasAnyErrors())
        UE_LOG(LogTemp, Display, TEXT("CINDERLINE_ONLINE_ENDPOINT_POLICY PASS"));
    return !HasAnyErrors();
}

#endif
