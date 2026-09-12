#include "Presentation/CinderOnlineSubsystem.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformTime.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderOnline, Log, All);

namespace
{
TSharedRef<FJsonObject> Control(const TCHAR* Type)
{
    auto Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("type"), Type);
    return Object;
}
FString Preferences()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Config/Online.ini"));
}
bool SafeText(const FString& Text)
{
    for (TCHAR Char : Text) if (Char < 32 || Char == 127) return false;
    return true;
}
}

void UCinderOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bInitialized = true;
    ServerEndpoint = TEXT("ws://127.0.0.1:8787/play");
    DisplayName = TEXT("Commander");
    if (GConfig)
    {
        GConfig->GetString(TEXT("Cinderline.Online"), TEXT("ServerUrl"), ServerEndpoint, GGameIni);
        if (!FApp::IsUnattended() && !GIsAutomationTesting)
        {
            FConfigFile SavedPreferences;
            SavedPreferences.Read(Preferences());
            SavedPreferences.GetString(TEXT("Online"), TEXT("ServerUrl"), ServerEndpoint);
            SavedPreferences.GetString(TEXT("Online"), TEXT("Name"), DisplayName);
        }
    }
    // UE ini values containing // must be quoted; recover from older defaults.
    if (!ServerEndpoint.StartsWith(TEXT("ws://")) && !ServerEndpoint.StartsWith(TEXT("wss://")))
        ServerEndpoint = TEXT("ws://127.0.0.1:8787/play");
    Status = TEXT("Create a private room or join a friend's room code.");
}

void UCinderOnlineSubsystem::Deinitialize()
{
    bInitialized = false;
    // App termination is a disconnect, allowing the server's grace period to apply.
    CloseSocket();
    SeatToken.Empty();
    Super::Deinitialize();
}

TStatId UCinderOnlineSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UCinderOnlineSubsystem, STATGROUP_Tickables);
}

void UCinderOnlineSubsystem::CloseSocket()
{
    ++SocketGeneration;
    bIntentionalClose = true;
    if (Socket)
    {
        Socket->OnConnected().Clear(); Socket->OnConnectionError().Clear();
        Socket->OnClosed().Clear(); Socket->OnMessage().Clear(); Socket->OnBinaryMessage().Clear();
        Socket->Close(); Socket.Reset();
    }
    BinaryBuffer.Reset();
    bIntentionalClose = false;
}

void UCinderOnlineSubsystem::Fail(const FString& Message)
{
    CloseSocket();
    CurrentState = ECinderOnlineState::Error;
    LastError = Message.Left(240); Status = LastError;
    PendingCommands.Reset();
    UE_LOG(LogCinderOnline, Display, TEXT("CINDERLINE_ONLINE error: %s"), *LastError);
}

void UCinderOnlineSubsystem::CreateRoom(const FString& Url, const FString& Name, int32 MapIndex)
{
    if (HasRoom() || bHasMatch) return;
    Map = FMath::Clamp(MapIndex, 0, 2);
    BeginConnection(Url, Name, FString(), true);
}

void UCinderOnlineSubsystem::JoinRoom(const FString& Url, const FString& Name, const FString& Code)
{
    if (HasRoom() || bHasMatch) return;
    BeginConnection(Url, Name, Code, false);
}

void UCinderOnlineSubsystem::BeginConnection(const FString& Url, const FString& Name, const FString& Code, bool bCreate)
{
    CloseSocket();
    FString Endpoint = Url.TrimStartAndEnd();
    const FString CleanName = Name.TrimStartAndEnd();
    const FString CleanCode = Code.TrimStartAndEnd().ToUpper();
    const bool bScheme = Endpoint.StartsWith(TEXT("wss://")) || Endpoint.StartsWith(TEXT("ws://"));
    if (!bScheme || Endpoint.Len() > 512 || Endpoint.Contains(TEXT(" ")) || Endpoint.Contains(TEXT("@")) ||
        Endpoint.Contains(TEXT("?")) || Endpoint.Contains(TEXT("#")) || !SafeText(Endpoint))
    { Fail(TEXT("Enter a ws:// or wss:// server address without credentials or query parameters.")); return; }
    const int32 HostStart = Endpoint.StartsWith(TEXT("wss://")) ? 6 : 5;
    const int32 Slash = Endpoint.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, HostStart);
    if (Endpoint.Len() <= HostStart || Slash == HostStart)
    { Fail(TEXT("The server address needs a hostname.")); return; }
    if (Slash == INDEX_NONE) Endpoint += TEXT("/play");
    else if (Endpoint.Mid(Slash) != TEXT("/play"))
    { Fail(TEXT("Use the game server's /play WebSocket endpoint.")); return; }
    if (CleanName.IsEmpty() || CleanName.Len() > 24 || !SafeText(CleanName))
    { Fail(TEXT("Use a player name between 1 and 24 characters.")); return; }
    if (!bCreate)
    {
        bool bCode = CleanCode.Len() == 6;
        for (TCHAR Char : CleanCode) bCode &= (Char >= 'A' && Char <= 'Z') || (Char >= '0' && Char <= '9');
        if (!bCode) { Fail(TEXT("Enter the six-character room code.")); return; }
    }
    ServerEndpoint = Endpoint; DisplayName = CleanName; PendingCode = CleanCode; bCreating = bCreate;
    Room.Empty(); SeatToken.Empty(); Team = -1; ResultWinner = -1;
    bHasMatch = bHasSnapshot = false; NextSequence = 1;
    Connected[0] = Connected[1] = Ready[0] = Ready[1] = false;
    Names[0].Empty(); Names[1].Empty(); LastError.Empty(); LastOrderFeedback.Empty();
    PendingCommands.Reset(); ReconnectDeadline = 0; ReconnectAttempts = 0; PingMs = -1;
    if (GConfig && !FApp::IsUnattended() && !GIsAutomationTesting)
    {
        IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("Config")), true);
        FConfigFile SavedPreferences;
        SavedPreferences.bCanSaveAllSections = true;
        SavedPreferences.SetString(TEXT("Online"), TEXT("ServerUrl"), *ServerEndpoint);
        SavedPreferences.SetString(TEXT("Online"), TEXT("Name"), *DisplayName);
        SavedPreferences.Write(Preferences());
    }
    OpenSocket(false);
}

void UCinderOnlineSubsystem::OpenSocket(bool bResume)
{
    CloseSocket();
    const uint64 Generation = SocketGeneration;
    TWeakObjectPtr<UCinderOnlineSubsystem> WeakThis(this);
    CurrentState = bResume ? ECinderOnlineState::Reconnecting : ECinderOnlineState::Connecting;
    Status = bResume ? TEXT("Reconnecting to your match...") : TEXT("Connecting to the game server...");
    LastError.Empty();
    ConnectionStartedAt = LastReceivedAt = FPlatformTime::Seconds();
    NextPingAt = ConnectionStartedAt + 3;
    Socket = FWebSocketsModule::Get().CreateWebSocket(ServerEndpoint);
    Socket->SetTextMessageMemoryLimit(16 * 1024);
    Socket->OnConnected().AddLambda([WeakThis, Generation, bResume]()
    {
        auto* Self = WeakThis.Get(); if (!Self || Self->SocketGeneration != Generation) return;
        auto Message = Control(bResume ? TEXT("reconnect") : Self->bCreating ? TEXT("create") : TEXT("join"));
        Message->SetNumberField(TEXT("version"), cinder::net::ProtocolVersion);
        if (bResume)
        {
            Message->SetStringField(TEXT("room"), Self->Room);
            Message->SetStringField(TEXT("token"), Self->SeatToken);
        }
        else
        {
            Message->SetStringField(TEXT("name"), Self->DisplayName);
            if (Self->bCreating) Message->SetNumberField(TEXT("map"), Self->Map);
            else Message->SetStringField(TEXT("room"), Self->PendingCode);
        }
        Self->SendControl(Message);
    });
    Socket->OnConnectionError().AddLambda([WeakThis, Generation](const FString&)
    {
        auto* Self = WeakThis.Get(); if (Self && Self->SocketGeneration == Generation)
            Self->ConnectionLost(TEXT("Could not reach the server. Check its address and connection."));
    });
    Socket->OnClosed().AddLambda([WeakThis, Generation](int32, const FString&, bool)
    {
        auto* Self = WeakThis.Get(); if (Self && Self->SocketGeneration == Generation)
            Self->ConnectionLost(TEXT("Connection to the game server was lost."));
    });
    Socket->OnMessage().AddLambda([WeakThis, Generation](const FString& Message)
    {
        auto* Self = WeakThis.Get(); if (Self && Self->SocketGeneration == Generation) Self->HandleText(Message);
    });
    Socket->OnBinaryMessage().AddLambda([WeakThis, Generation](const void* Data, SIZE_T Size, bool bLast)
    {
        auto* Self = WeakThis.Get(); if (Self && Self->SocketGeneration == Generation) Self->HandleBinary(Data, Size, bLast);
    });
    Socket->Connect();
}

void UCinderOnlineSubsystem::SendControl(const TSharedRef<FJsonObject>& Object)
{
    if (!Socket || !Socket->IsConnected()) return;
    FString Message;
    auto Writer = TJsonWriterFactory<>::Create(&Message);
    FJsonSerializer::Serialize(Object, Writer);
    Socket->Send(Message);
}

void UCinderOnlineSubsystem::HandleText(const FString& Message)
{
    if (CurrentState == ECinderOnlineState::Leaving) return;
    if (Message.Len() > 16384) { Fail(TEXT("Server response exceeded the message limit.")); return; }
    TSharedPtr<FJsonObject> Object;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Message), Object) || !Object)
    { Fail(TEXT("The server sent an invalid response.")); return; }
    FString Type;
    if (!Object->TryGetStringField(TEXT("type"), Type)) return;
    LastReceivedAt = FPlatformTime::Seconds();
    if (Type == TEXT("welcome"))
    {
        FString NewRoom, Token; double Seat = -1;
        if (!Object->TryGetStringField(TEXT("room"), NewRoom) || NewRoom.Len() != 6 ||
            !Object->TryGetStringField(TEXT("token"), Token) || Token.Len() < 16 || Token.Len() > 128 ||
            !Object->TryGetNumberField(TEXT("team"), Seat) || (Seat != 0 && Seat != 1))
        { Fail(TEXT("The server returned an invalid seat.")); return; }
        Room = NewRoom; SeatToken = Token; Team = static_cast<int32>(Seat);
        CurrentState = bHasMatch ? ECinderOnlineState::Reconnecting : ECinderOnlineState::Lobby;
        Status = TEXT("Room connected. Both players must be ready.");
        if (!bHasMatch) { ReconnectDeadline = 0; ReconnectAttempts = 0; }
        UE_LOG(LogCinderOnline, Display, TEXT("CINDERLINE_ONLINE seat=%d joined"), Team);
    }
    else if (Type == TEXT("lobby"))
    {
        double MapValue;
        if (Object->TryGetNumberField(TEXT("map"), MapValue) && MapValue >= 0 && MapValue <= 2) Map = static_cast<int32>(MapValue);
        const TArray<TSharedPtr<FJsonValue>>* Players;
        if (Object->TryGetArrayField(TEXT("players"), Players) && Players->Num() == 2)
            for (int32 Index = 0; Index < 2; ++Index)
            {
                const auto Player = (*Players)[Index]->AsObject();
                Names[Index].Empty(); Connected[Index] = Ready[Index] = false;
                if (Player)
                {
                    Player->TryGetStringField(TEXT("name"), Names[Index]); Names[Index] = Names[Index].Left(24);
                    Player->TryGetBoolField(TEXT("connected"), Connected[Index]);
                    Player->TryGetBoolField(TEXT("ready"), Ready[Index]);
                }
            }
        if (!bHasMatch)
        {
            CurrentState = ECinderOnlineState::Lobby;
            Status = Connected[0] && Connected[1] ? TEXT("Both players connected. Ready up to begin.") : TEXT("Share the room code. Waiting for the other player.");
        }
    }
    else if (Type == TEXT("started"))
    {
        bHasMatch = true; CurrentState = ECinderOnlineState::Starting;
        Status = TEXT("Synchronizing the battlefield...");
    }
    else if (Type == TEXT("ack"))
    {
        double Seq; bool Accepted; FString Text;
        if (!Object->TryGetNumberField(TEXT("seq"), Seq) || Seq < 1 || Seq > MAX_uint32 ||
            !Object->TryGetBoolField(TEXT("accepted"), Accepted)) return;
        const uint32 Sequence = static_cast<uint32>(Seq);
        if (!PendingCommands.Remove(Sequence)) return;
        Object->TryGetStringField(TEXT("message"), Text);
        bLastOrderAccepted = Accepted;
        LastOrderFeedback = Text.Left(240);
        if (LastOrderFeedback.IsEmpty()) LastOrderFeedback = Accepted ? TEXT("Order accepted") : TEXT("Order rejected");
        ++OrderFeedbackSerial;
    }
    else if (Type == TEXT("peer"))
    {
        bool bPeerConnected = true; Object->TryGetBoolField(TEXT("connected"), bPeerConnected);
        if (Team >= 0) Connected[1 - Team] = bPeerConnected;
        Status = bPeerConnected ? TEXT("Opponent reconnected.") : TEXT("Opponent disconnected. The match continues during their reconnect window.");
    }
    else if (Type == TEXT("result"))
    {
        double Winner;
        if (!Object->TryGetNumberField(TEXT("winner"), Winner) || (Winner != 0 && Winner != 1) || Team < 0) return;
        ResultWinner = static_cast<int32>(Winner) == Team ? 0 : 1;
        if (bHasSnapshot) { Snapshot.winner = ResultWinner; ++ReceivedSnapshotSerial; }
        CurrentState = ECinderOnlineState::Finished; bHasMatch = true;
        Status = ResultWinner == 0 ? TEXT("Victory confirmed by the server.") : TEXT("Defeat confirmed by the server.");
        PendingCommands.Reset();
        UE_LOG(LogCinderOnline, Display, TEXT("CINDERLINE_ONLINE result_local=%d"), ResultWinner);
    }
    else if (Type == TEXT("pong"))
    {
        double Nonce;
        if (Object->TryGetNumberField(TEXT("nonce"), Nonce) && Nonce == PingNonce)
            PingMs = static_cast<float>((LastReceivedAt - PingSentAt) * 1000);
    }
    else if (Type == TEXT("error"))
    {
        FString Text; Object->TryGetStringField(TEXT("message"), Text);
        if (Text.IsEmpty()) Text = TEXT("The server could not complete that request.");
        if (CurrentState == ECinderOnlineState::Reconnecting && Text == TEXT("That seat is already connected."))
        {
            // The replacement socket can reach the server before it observes the old
            // socket's close. Retain the credentials and retry inside the grace window.
            CloseSocket(); PendingCommands.Reset();
            LastError.Empty(); Status = TEXT("Waiting for the previous connection to close...");
            NextReconnectAt = FPlatformTime::Seconds() + 0.5;
            return;
        }
        LastError = Text.Left(240); Status = LastError;
        if (!HasRoom() || CurrentState == ECinderOnlineState::Reconnecting || CurrentState == ECinderOnlineState::Connecting) Fail(LastError);
    }
}

void UCinderOnlineSubsystem::HandleBinary(const void* Data, SIZE_T Size, bool bLastFragment)
{
    if (CurrentState == ECinderOnlineState::Leaving) return;
    if (Size > cinder::net::MaxMessageBytes || static_cast<SIZE_T>(BinaryBuffer.Num()) > cinder::net::MaxMessageBytes - Size)
    { Fail(TEXT("Server snapshot exceeded the size limit.")); return; }
    if (Size) BinaryBuffer.Append(static_cast<const uint8*>(Data), static_cast<int32>(Size));
    if (!bLastFragment) return;
    cinder::net::Snapshot Next; std::string Error;
    const bool bValid = Team >= 0 && cinder::net::decodeSnapshot(BinaryBuffer.GetData(), BinaryBuffer.Num(), Next, Error);
    BinaryBuffer.Reset();
    if (!bValid) { Fail(TEXT("Server snapshot failed protocol validation.")); return; }
    if (bHasSnapshot && Next.tick < Snapshot.tick) return;
    Snapshot = MoveTemp(Next); bHasSnapshot = bHasMatch = true;
    if (CurrentState == ECinderOnlineState::Finished && ResultWinner >= 0) Snapshot.winner = ResultWinner;
    LastSnapshotAt = LastReceivedAt = FPlatformTime::Seconds();
    ++ReceivedSnapshotSerial;
    ReconnectDeadline = 0; ReconnectAttempts = 0; LastError.Empty();
    if (Snapshot.winner >= 0)
    {
        ResultWinner = Snapshot.winner; CurrentState = ECinderOnlineState::Finished;
        Status = ResultWinner == 0 ? TEXT("Victory confirmed by the server.") : TEXT("Defeat confirmed by the server.");
    }
    else if (CurrentState != ECinderOnlineState::Finished)
    {
        const bool bRecovered = CurrentState != ECinderOnlineState::Playing;
        CurrentState = ECinderOnlineState::Playing;
        if (bRecovered) Status = TEXT("Online match live. Menus do not pause the match.");
    }
}

bool UCinderOnlineSubsystem::CanSendOrders() const
{
    return CurrentState == ECinderOnlineState::Playing && Socket && Socket->IsConnected() &&
        bHasSnapshot && FPlatformTime::Seconds() - LastSnapshotAt < 3.0 && PendingCommands.Num() < 64;
}

bool UCinderOnlineSubsystem::SendCommand(const cinder::Command& Command)
{
    if (!CanSendOrders() || NextSequence == 0) return false;
    const uint32 Sequence = NextSequence++;
    const auto Bytes = cinder::net::encodeCommand(Command, Sequence);
    if (Bytes.empty()) return false;
    PendingCommands.Add(Sequence, FPlatformTime::Seconds());
    Socket->Send(Bytes.data(), Bytes.size(), true);
    return true;
}

void UCinderOnlineSubsystem::SetReady(bool Value)
{
    if (CurrentState != ECinderOnlineState::Lobby) return;
    auto Message = Control(TEXT("ready")); Message->SetBoolField(TEXT("ready"), Value); SendControl(Message);
}

bool UCinderOnlineSubsystem::Surrender()
{
    if (!bHasMatch || CurrentState == ECinderOnlineState::Finished || !Socket || !Socket->IsConnected()) return false;
    SendControl(Control(TEXT("surrender")));
    return true;
}

void UCinderOnlineSubsystem::Leave()
{
    if (CurrentState == ECinderOnlineState::Leaving) return;
    bHasMatch = bHasSnapshot = false;
    PendingCommands.Reset();
    if (!Socket || !Socket->IsConnected()) { FinishLeave(); return; }
    CurrentState = ECinderOnlineState::Leaving;
    LeaveDeadline = FPlatformTime::Seconds() + 3.0;
    Status = TEXT("Leaving the online room..."); LastError.Empty();
    SendControl(Control(TEXT("leave")));
}

void UCinderOnlineSubsystem::FinishLeave()
{
    CloseSocket();
    CurrentState = ECinderOnlineState::Offline;
    Room.Empty(); SeatToken.Empty(); PendingCode.Empty(); Team = -1; ResultWinner = -1;
    bHasMatch = bHasSnapshot = false; ReconnectDeadline = LeaveDeadline = 0; PendingCommands.Reset();
    Connected[0] = Connected[1] = Ready[0] = Ready[1] = false;
    Names[0].Empty(); Names[1].Empty();
    Status = TEXT("Left the online room."); LastError.Empty();
}

void UCinderOnlineSubsystem::Reconnect()
{
    if (CurrentState == ECinderOnlineState::Leaving || Room.IsEmpty() || SeatToken.IsEmpty()) return;
    if (ReconnectDeadline == 0) ReconnectDeadline = FPlatformTime::Seconds() + 60;
    ++ReconnectAttempts; OpenSocket(true);
}

void UCinderOnlineSubsystem::ConnectionLost(const FString& Reason)
{
    if (CurrentState == ECinderOnlineState::Leaving) { FinishLeave(); return; }
    if (bIntentionalClose) return;
    if (!HasRoom() || SeatToken.IsEmpty()) { Fail(Reason); return; }
    CloseSocket();
    PendingCommands.Reset();
    if (CurrentState == ECinderOnlineState::Finished) return;
    const double Now = FPlatformTime::Seconds();
    if (ReconnectDeadline == 0) ReconnectDeadline = Now + 60;
    CurrentState = ECinderOnlineState::Reconnecting;
    Status = TEXT("Connection lost. Reconnecting; the match may continue on the server.");
    NextReconnectAt = Now + FMath::Min(5.0, 0.5 * FMath::Pow(2.0, FMath::Min(ReconnectAttempts, 4)));
}

void UCinderOnlineSubsystem::Tick(float DeltaTime)
{
    const double Now = FPlatformTime::Seconds();
    if (CurrentState == ECinderOnlineState::Leaving)
    {
        if (!Socket || !Socket->IsConnected() || Now >= LeaveDeadline) FinishLeave();
        return;
    }
    if (CurrentState == ECinderOnlineState::Reconnecting)
    {
        if (ReconnectDeadline > 0 && Now > ReconnectDeadline)
        { Fail(TEXT("Reconnect window expired. Return to the lobby to start another match.")); return; }
        if (!Socket && Now >= NextReconnectAt) Reconnect();
    }
    if ((CurrentState == ECinderOnlineState::Connecting || CurrentState == ECinderOnlineState::Reconnecting ||
        CurrentState == ECinderOnlineState::Starting) && Socket && Now - LastReceivedAt > 12)
    { ConnectionLost(TEXT("The server did not respond in time.")); return; }
    if (Socket && Socket->IsConnected())
    {
        if (Now - LastReceivedAt > 12) { ConnectionLost(TEXT("The server stopped responding.")); return; }
        if (CurrentState == ECinderOnlineState::Playing && Now - LastSnapshotAt > 4)
        { ConnectionLost(TEXT("Gameplay updates stopped. Reconnecting.")); return; }
        if (Now >= NextPingAt)
        {
            PingSentAt = Now; NextPingAt = Now + 3; ++PingNonce;
            auto Message = Control(TEXT("ping")); Message->SetNumberField(TEXT("nonce"), PingNonce); SendControl(Message);
        }
    }
    for (auto It = PendingCommands.CreateIterator(); It; ++It)
        if (Now - It.Value() > 5)
        {
            It.RemoveCurrent(); bLastOrderAccepted = false; ++OrderFeedbackSerial;
            LastOrderFeedback = TEXT("Order acknowledgement was lost. Check the current battlefield before issuing it again.");
        }
}
