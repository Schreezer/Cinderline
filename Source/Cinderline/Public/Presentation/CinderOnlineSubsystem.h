#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Sim/Network.h"
#include "CinderOnlineSubsystem.generated.h"

class IWebSocket;
enum class ECinderConnectionMode : uint8 { Internet, LocalNetwork };
enum class ECinderOnlineState : uint8 { Offline, Connecting, Lobby, Starting, Playing, Reconnecting, Leaving, Finished, Error };

struct FCinderOnlineCommandAcknowledgement
{
    bool bAccepted = false;
    FString Message;
};

/** Room transport and private seat credentials. Gameplay authority stays on the server. */
UCLASS()
class CINDERLINE_API UCinderOnlineSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    static constexpr int32 MaxPlayers = 4;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override { return !HasAnyFlags(RF_ClassDefaultObject) && bInitialized; }
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual TStatId GetStatId() const override;
    virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

    ECinderOnlineState State() const { return CurrentState; }
    const FString& Endpoint() const { return ServerEndpoint; }
    ECinderConnectionMode ConnectionMode() const { return Mode; }
    bool SetConnectionMode(ECinderConnectionMode Value);
    static bool ValidateEndpoint(const FString& Url, ECinderConnectionMode Mode, FString& Normalized, FString& Error);
    const FString& PlayerName() const { return DisplayName; }
    const FString& RoomCode() const { return Room; }
    const FString& StatusText() const { return Status; }
    const FString& ErrorText() const { return LastError; }
    FString SeatName(int32 Seat) const { return Seat >= 0 && Seat < Players ? Names[Seat] : FString(); }
    bool SeatConnected(int32 Seat) const { return Seat >= 0 && Seat < Players && Connected[Seat]; }
    bool SeatReady(int32 Seat) const { return Seat >= 0 && Seat < Players && Ready[Seat]; }
    int32 PlayerCount() const { return Players; }
    int32 LocalSeat() const { return Team; }
    int32 MapIndex() const { return Map; }
    float PingMilliseconds() const { return PingMs; }
    bool HasMatch() const { return bHasMatch; }
    bool HasRoom() const { return !Room.IsEmpty(); }
    bool CanLeaveSession() const;
    bool CanReconnectSession() const;
    bool CanSendOrders() const;
    bool IsEliminated() const;
    int32 RemainingPlayers() const;
    int32 MatchWinner() const { return ResultWinner; } // Cyclic local perspective; -1 ongoing, -2 draw.
    uint64 SnapshotSerial() const { return ReceivedSnapshotSerial; }
    const cinder::net::Snapshot* LatestSnapshot() const { return bHasSnapshot ? &Snapshot : nullptr; }
    double SnapshotReceivedAt() const { return LastSnapshotAt; }
    uint64 FeedbackSerial() const { return OrderFeedbackSerial; }
    const FString& OrderFeedback() const { return LastOrderFeedback; }
    bool LastOrderAccepted() const { return bLastOrderAccepted; }

    void CreateRoom(const FString& Url, const FString& Name, int32 MapIndex, int32 PlayerCount = 2);
    void JoinRoom(const FString& Url, const FString& Name, const FString& Code);
    void SetReady(bool Value);
    void Reconnect();
    void RecoverConnection();
    void Leave();
    bool Surrender();
    bool SendCommand(const cinder::Command& Command, uint32* OutSequence = nullptr);
    bool IsCommandPending(uint32 Sequence) const { return PendingCommands.Contains(Sequence); }
    bool ConsumeCommandAcknowledgement(uint32 Sequence, FCinderOnlineCommandAcknowledgement& Out);
private:
    friend class FCinderOnboardingIntegration;
    friend class FCinderArmyControlIntegration;
    friend class FCinderOnlineRecoveryIntegration;
    friend class FCinderTacticalOrderIntegration;
    friend class FCinderPatrolEscortIntegration;
    void BeginConnection(const FString& Url, const FString& Name, const FString& Code, bool bCreate);
    void OpenSocket(bool bResume);
    void CloseSocket();
    void HandleText(const FString& Message);
    void HandleBinary(const void* Data, SIZE_T Size, bool bLastFragment);
    void ApplyConfirmedResult();
    void ConnectionLost(const FString& Reason);
    void FinishLeave();
    void Fail(const FString& Message);
    void ReportUncertainOrders(const FString& Reason);
    bool PrepareReconnect(double Now);
    static double ReconnectDelay(int32 Attempts);
    void LoadPreferences(const FString& Filename);
    void SavePreferences(const FString& Filename) const;
    void SendControl(const TSharedRef<class FJsonObject>& Object);
    ECinderOnlineState CurrentState = ECinderOnlineState::Offline;
    TSharedPtr<IWebSocket> Socket;
    FString ServerEndpoint, DisplayName, Room, SeatToken, Status, LastError, PendingCode;
    FString InternetServerEndpoint, LANServerEndpoint;
    ECinderConnectionMode Mode = ECinderConnectionMode::Internet;
    FString Names[MaxPlayers];
    bool Connected[MaxPlayers] = {}, Ready[MaxPlayers] = {};
    int32 Team = -1, Map = 0, Players = 2, ResultWinner = -1;
    bool bInitialized = false, bCreating = false, bHasMatch = false, bHasSnapshot = false;
    bool bIntentionalClose = false, bLastOrderAccepted = false;
    bool bResumeAllowed = false, bWaitingForWorker = false;
    uint64 SocketGeneration = 0, ReceivedSnapshotSerial = 0, OrderFeedbackSerial = 0;
    uint32 NextSequence = 1;
    TMap<uint32, double> PendingCommands;
    TMap<uint32, FCinderOnlineCommandAcknowledgement> CommandAcknowledgements;
    cinder::net::Snapshot Snapshot;
    TArray<uint8> BinaryBuffer;
    FString LastOrderFeedback;
    double LastSnapshotAt = 0, LastReceivedAt = 0, NextReconnectAt = 0, ReconnectDeadline = 0, LeaveDeadline = 0;
    double ConnectionStartedAt = 0, NextPingAt = 0, PingSentAt = 0;
    double SnapshotDeadline = 0;
    int32 ReconnectAttempts = 0, PingNonce = 0;
    float PingMs = -1;
};
