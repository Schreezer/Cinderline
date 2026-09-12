#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Sim/Network.h"
#include "CinderOnlineSubsystem.generated.h"

class IWebSocket;
enum class ECinderOnlineState : uint8 { Offline, Connecting, Lobby, Starting, Playing, Reconnecting, Leaving, Finished, Error };

/** Room transport and private seat credentials. Gameplay authority stays on the server. */
UCLASS()
class CINDERLINE_API UCinderOnlineSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override { return !HasAnyFlags(RF_ClassDefaultObject) && bInitialized; }
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual TStatId GetStatId() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

    ECinderOnlineState State() const { return CurrentState; }
    const FString& Endpoint() const { return ServerEndpoint; }
    const FString& PlayerName() const { return DisplayName; }
    const FString& RoomCode() const { return Room; }
    const FString& StatusText() const { return Status; }
    const FString& ErrorText() const { return LastError; }
    FString SeatName(int32 Seat) const { return Seat >= 0 && Seat < 2 ? Names[Seat] : FString(); }
    bool SeatConnected(int32 Seat) const { return Seat >= 0 && Seat < 2 && Connected[Seat]; }
    bool SeatReady(int32 Seat) const { return Seat >= 0 && Seat < 2 && Ready[Seat]; }
    int32 LocalSeat() const { return Team; }
    int32 MapIndex() const { return Map; }
    float PingMilliseconds() const { return PingMs; }
    bool HasMatch() const { return bHasMatch; }
    bool HasRoom() const { return !Room.IsEmpty(); }
    bool CanSendOrders() const;
    int32 MatchWinner() const { return ResultWinner; } // Local perspective, -1 until complete.
    uint64 SnapshotSerial() const { return ReceivedSnapshotSerial; }
    const cinder::net::Snapshot* LatestSnapshot() const { return bHasSnapshot ? &Snapshot : nullptr; }
    double SnapshotReceivedAt() const { return LastSnapshotAt; }
    uint64 FeedbackSerial() const { return OrderFeedbackSerial; }
    const FString& OrderFeedback() const { return LastOrderFeedback; }
    bool LastOrderAccepted() const { return bLastOrderAccepted; }

    void CreateRoom(const FString& Url, const FString& Name, int32 MapIndex);
    void JoinRoom(const FString& Url, const FString& Name, const FString& Code);
    void SetReady(bool Value);
    void Reconnect();
    void Leave();
    bool Surrender();
    bool SendCommand(const cinder::Command& Command);
private:
    void BeginConnection(const FString& Url, const FString& Name, const FString& Code, bool bCreate);
    void OpenSocket(bool bResume);
    void CloseSocket();
    void HandleText(const FString& Message);
    void HandleBinary(const void* Data, SIZE_T Size, bool bLastFragment);
    void ConnectionLost(const FString& Reason);
    void FinishLeave();
    void Fail(const FString& Message);
    void SendControl(const TSharedRef<class FJsonObject>& Object);
    ECinderOnlineState CurrentState = ECinderOnlineState::Offline;
    TSharedPtr<IWebSocket> Socket;
    FString ServerEndpoint, DisplayName, Room, SeatToken, Status, LastError, PendingCode;
    FString Names[2];
    bool Connected[2] = {}, Ready[2] = {};
    int32 Team = -1, Map = 0, ResultWinner = -1;
    bool bInitialized = false, bCreating = false, bHasMatch = false, bHasSnapshot = false;
    bool bIntentionalClose = false, bLastOrderAccepted = false;
    uint64 SocketGeneration = 0, ReceivedSnapshotSerial = 0, OrderFeedbackSerial = 0;
    uint32 NextSequence = 1;
    TMap<uint32, double> PendingCommands;
    cinder::net::Snapshot Snapshot;
    TArray<uint8> BinaryBuffer;
    FString LastOrderFeedback;
    double LastSnapshotAt = 0, LastReceivedAt = 0, NextReconnectAt = 0, ReconnectDeadline = 0, LeaveDeadline = 0;
    double ConnectionStartedAt = 0, NextPingAt = 0, PingSentAt = 0;
    int32 ReconnectAttempts = 0, PingNonce = 0;
    float PingMs = -1;
};
