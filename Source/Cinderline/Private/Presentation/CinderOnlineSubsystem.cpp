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
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

DEFINE_LOG_CATEGORY_STATIC(LogCinderOnline, Log, All);

namespace
{
constexpr double SnapshotTimeoutSeconds = 12.0;
constexpr double ReconnectWindowSeconds = 60.0;
constexpr int32 MaxReconnectAttempts = 12;
bool ReadIntegerField(const FJsonObject& Object, const TCHAR* Field, double Minimum, double Maximum, double& Value)
{
    // UE's numeric getter also accepts numeric strings. Wire identifiers must be
    // JSON numbers with an exact integral value before any narrowing conversion.
    return Object.HasTypedField<EJson::Number>(Field) && Object.TryGetNumberField(Field, Value)
        && Value >= Minimum && Value <= Maximum && Value == std::trunc(Value);
}
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
bool LocalIPv4(const TArray<uint8>& Bytes, int32 Offset, bool& bLoopback)
{
    bLoopback = Bytes[Offset] == 127;
    return bLoopback || Bytes[Offset] == 10 || (Bytes[Offset] == 172 && Bytes[Offset + 1] >= 16 && Bytes[Offset + 1] <= 31)
        || (Bytes[Offset] == 192 && Bytes[Offset + 1] == 168) || (Bytes[Offset] == 169 && Bytes[Offset + 1] == 254);
}
}

bool UCinderOnlineSubsystem::ValidateEndpoint(const FString& Url, ECinderConnectionMode Mode, FString& Normalized, FString& Error)
{
    Normalized.Empty(); Error.Empty();
    auto Reject = [&Error](const TCHAR* Reason) { Error = Reason; return false; };
    if (Mode != ECinderConnectionMode::Internet && Mode != ECinderConnectionMode::LocalNetwork)
        return Reject(TEXT("Choose Internet or Local Network."));
    const FString Endpoint = Url.TrimStartAndEnd();
    const bool bTLS = Endpoint.StartsWith(TEXT("wss://"));
    if ((!bTLS && !Endpoint.StartsWith(TEXT("ws://"))) || Endpoint.Len() > 512)
        return Reject(TEXT("Enter a ws:// or wss:// server address."));
    for (TCHAR Char : Endpoint)
        if (Char <= 32 || Char >= 127) return Reject(TEXT("The server address must contain only printable ASCII characters without spaces."));
    if (Endpoint.Contains(TEXT("@")) || Endpoint.Contains(TEXT("?")) || Endpoint.Contains(TEXT("#"))
        || Endpoint.Contains(TEXT("%")) || Endpoint.Contains(TEXT("\\")))
        return Reject(TEXT("Use a server address without credentials, query parameters, fragments, or escapes."));
    const int32 HostStart = bTLS ? 6 : 5;
    const int32 Slash = Endpoint.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, HostStart);
    if (Slash != INDEX_NONE && !Endpoint.Mid(Slash).Equals(TEXT("/play"), ESearchCase::CaseSensitive))
        return Reject(TEXT("Use the game server's /play WebSocket endpoint."));
    const FString Authority = Slash == INDEX_NONE ? Endpoint.Mid(HostStart) : Endpoint.Mid(HostStart, Slash - HostStart);
    FString Host, Port;
    bool bIPv6 = Authority.StartsWith(TEXT("[")), bExplicitPort = false;
    if (bIPv6)
    {
        int32 Closing = INDEX_NONE;
        if (!Authority.FindChar(TEXT(']'), Closing) || Closing < 2) return Reject(TEXT("Use a bracketed IPv6 address."));
        Host = Authority.Mid(1, Closing - 1);
        const FString Suffix = Authority.Mid(Closing + 1);
        if (!Suffix.IsEmpty())
        {
            if (!Suffix.StartsWith(TEXT(":"))) return Reject(TEXT("Enter a valid hostname and port."));
            bExplicitPort = true; Port = Suffix.Mid(1);
        }
        if (!Host.Contains(TEXT(":"))) return Reject(TEXT("Only IPv6 addresses use brackets."));
    }
    else
    {
        int32 Colon = INDEX_NONE;
        if (Authority.FindChar(TEXT(':'), Colon))
        { Host = Authority.Left(Colon); Port = Authority.Mid(Colon + 1); bExplicitPort = true; }
        else Host = Authority;
    }
    if (Host.IsEmpty() || Host.Len() > 253) return Reject(TEXT("The server address needs a valid hostname."));
    if (bExplicitPort)
    {
        if (Port.IsEmpty() || Port.Len() > 5) return Reject(TEXT("Use a port between 1 and 65535."));
        int32 Value = 0;
        for (TCHAR Char : Port)
        {
            if (Char < '0' || Char > '9') return Reject(TEXT("Use a numeric port between 1 and 65535."));
            Value = Value * 10 + Char - '0';
        }
        if (Value < 1 || Value > 65535) return Reject(TEXT("Use a port between 1 and 65535."));
    }
    Host.ToLowerInline();
    bool bNumericIPv4 = !bIPv6;
    for (TCHAR Char : Host) if ((Char < '0' || Char > '9') && Char != '.') bNumericIPv4 = false;
    bool bLoopback = false, bLocal = false;
    if (bIPv6 || bNumericIPv4)
    {
        ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        const TSharedPtr<FInternetAddr> Address = Sockets ? Sockets->GetAddressFromString(Host) : nullptr;
        if (!Address) return Reject(TEXT("Enter a valid IP address; IPv6 addresses need brackets."));
        const TArray<uint8> Bytes = Address->GetRawIp();
        if (Bytes.Num() == 4) bLocal = LocalIPv4(Bytes, 0, bLoopback);
        else if (Bytes.Num() == 16)
        {
            bool bLeadingZero = true;
            for (int32 Index = 0; Index < 10; ++Index) bLeadingZero &= Bytes[Index] == 0;
            if (bLeadingZero && Bytes[10] == 255 && Bytes[11] == 255) bLocal = LocalIPv4(Bytes, 12, bLoopback);
            else
            {
                bLoopback = bLeadingZero && Bytes[10] == 0 && Bytes[11] == 0 && Bytes[12] == 0
                    && Bytes[13] == 0 && Bytes[14] == 0 && Bytes[15] == 1;
                bLocal = bLoopback || (Bytes[0] & 0xfe) == 0xfc || (Bytes[0] == 0xfe && (Bytes[1] & 0xc0) == 0x80);
            }
        }
        else return Reject(TEXT("The server returned an unsupported IP address format."));
    }
    else
    {
        if (Host.EndsWith(TEXT("."))) Host.LeftChopInline(1);
        TArray<FString> Labels; Host.ParseIntoArray(Labels, TEXT("."), false);
        for (const FString& Label : Labels)
        {
            if (Label.IsEmpty() || Label.Len() > 63 || Label.StartsWith(TEXT("-")) || Label.EndsWith(TEXT("-")))
                return Reject(TEXT("Enter a valid hostname."));
            for (TCHAR Char : Label)
                if (!((Char >= 'a' && Char <= 'z') || (Char >= '0' && Char <= '9') || Char == '-'))
                    return Reject(TEXT("Enter a valid hostname."));
        }
        bLoopback = Host == TEXT("localhost");
        bLocal = bLoopback || Labels.Num() == 1 || Host.EndsWith(TEXT(".local"));
    }
    if (!bTLS && Mode == ECinderConnectionMode::Internet && !bLoopback)
        return Reject(TEXT("Internet games require a secure wss:// address. Use Local Network for a nearby server."));
    if (!bTLS && Mode == ECinderConnectionMode::LocalNetwork && !bLocal)
        return Reject(TEXT("Local Network needs a private IP, a .local name, or a local computer name. Public servers require wss://."));
    Normalized = FString(bTLS ? TEXT("wss://") : TEXT("ws://")) + (bIPv6 ? TEXT("[") + Host + TEXT("]") : Host)
        + (bExplicitPort ? TEXT(":") + Port : FString()) + TEXT("/play");
    return true;
}

void UCinderOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bInitialized = true;
    ServerEndpoint = TEXT("ws://127.0.0.1:8787/play");
    InternetServerEndpoint = LANServerEndpoint = ServerEndpoint;
    DisplayName = TEXT("Commander");
    if (GConfig)
    {
        GConfig->GetString(TEXT("Cinderline.Online"), TEXT("ServerUrl"), ServerEndpoint, GGameIni);
        InternetServerEndpoint = ServerEndpoint;
        GConfig->GetString(TEXT("Cinderline.Online"), TEXT("InternetServerUrl"), InternetServerEndpoint, GGameIni);
        GConfig->GetString(TEXT("Cinderline.Online"), TEXT("LANServerUrl"), LANServerEndpoint, GGameIni);
        if (!FApp::IsUnattended() && !GIsAutomationTesting)
            LoadPreferences(Preferences());
    }
    ServerEndpoint = Mode == ECinderConnectionMode::Internet ? InternetServerEndpoint : LANServerEndpoint;
    // UE ini values containing // must be quoted; recover from older defaults.
    if (!ServerEndpoint.StartsWith(TEXT("ws://")) && !ServerEndpoint.StartsWith(TEXT("wss://")))
    {
        ServerEndpoint = TEXT("ws://127.0.0.1:8787/play");
        (Mode == ECinderConnectionMode::Internet ? InternetServerEndpoint : LANServerEndpoint) = ServerEndpoint;
    }
    Status = TEXT("Create a private room or join a friend's room code.");
}

void UCinderOnlineSubsystem::LoadPreferences(const FString& Filename)
{
    FConfigFile Saved;
    Saved.Read(Filename);
    FString Legacy, StoredMode;
    const bool bHasInternet = Saved.GetString(TEXT("Online"), TEXT("InternetServerUrl"), InternetServerEndpoint);
    const bool bHasLAN = Saved.GetString(TEXT("Online"), TEXT("LANServerUrl"), LANServerEndpoint);
    const bool bHasMode = Saved.GetString(TEXT("Online"), TEXT("ConnectionMode"), StoredMode);
    Mode = bHasMode && StoredMode == TEXT("1") ? ECinderConnectionMode::LocalNetwork : ECinderConnectionMode::Internet;
    if (Saved.GetString(TEXT("Online"), TEXT("ServerUrl"), Legacy))
    {
        FString Normalized, Error;
        const bool bLegacyLAN = !ValidateEndpoint(Legacy, ECinderConnectionMode::Internet, Normalized, Error)
            && ValidateEndpoint(Legacy, ECinderConnectionMode::LocalNetwork, Normalized, Error);
        if (bLegacyLAN)
        {
            if (!bHasLAN) LANServerEndpoint = Normalized;
            if (!bHasMode && !bHasInternet && !bHasLAN) Mode = ECinderConnectionMode::LocalNetwork;
        }
        else if (!bHasInternet) InternetServerEndpoint = Legacy;
    }
    Saved.GetString(TEXT("Online"), TEXT("Name"), DisplayName);
    ServerEndpoint = Mode == ECinderConnectionMode::Internet ? InternetServerEndpoint : LANServerEndpoint;
}

void UCinderOnlineSubsystem::SavePreferences(const FString& Filename) const
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FConfigFile Saved;
    Saved.Read(Filename);
    Saved.bCanSaveAllSections = true;
    Saved.SetString(TEXT("Online"), TEXT("InternetServerUrl"), *InternetServerEndpoint);
    Saved.SetString(TEXT("Online"), TEXT("LANServerUrl"), *LANServerEndpoint);
    Saved.SetString(TEXT("Online"), TEXT("ServerUrl"), *ServerEndpoint);
    Saved.SetString(TEXT("Online"), TEXT("ConnectionMode"), Mode == ECinderConnectionMode::LocalNetwork ? TEXT("1") : TEXT("0"));
    Saved.SetString(TEXT("Online"), TEXT("Name"), *DisplayName);
    Saved.Write(Filename);
}

bool UCinderOnlineSubsystem::SetConnectionMode(ECinderConnectionMode Value)
{
    if ((Value != ECinderConnectionMode::Internet && Value != ECinderConnectionMode::LocalNetwork)
        || HasRoom() || bHasMatch || (CurrentState != ECinderOnlineState::Offline && CurrentState != ECinderOnlineState::Error)) return false;
    if (Mode == Value) return true;
    Mode = Value;
    ServerEndpoint = Mode == ECinderConnectionMode::Internet ? InternetServerEndpoint : LANServerEndpoint;
    CurrentState = ECinderOnlineState::Offline;
    LastError.Empty();
    Status = Mode == ECinderConnectionMode::Internet ? TEXT("Create a private room or join a friend's room code.")
        : TEXT("Choose a nearby game or enter its local server address.");
    if (GConfig && !FApp::IsUnattended() && !GIsAutomationTesting) SavePreferences(Preferences());
    return true;
}

void UCinderOnlineSubsystem::Deinitialize()
{
    bInitialized = false;
    // App termination is a disconnect, allowing the server's grace period to apply.
    CloseSocket();
    SeatToken.Empty();
    PendingCommands.Reset();
    CommandAcknowledgements.Reset();
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
    SnapshotDeadline = 0;
    CurrentState = ECinderOnlineState::Error;
    LastError = Message.Left(240); Status = LastError;
    ReportUncertainOrders(TEXT("Connection interrupted."));
    UE_LOG(LogCinderOnline, Display, TEXT("CINDERLINE_ONLINE error: %s"), *LastError);
}

bool UCinderOnlineSubsystem::CanLeaveSession() const
{
    return CurrentState != ECinderOnlineState::Offline || HasRoom() || bHasMatch;
}

bool UCinderOnlineSubsystem::CanReconnectSession() const
{
    return bResumeAllowed && HasRoom() && !SeatToken.IsEmpty()
        && CurrentState != ECinderOnlineState::Leaving && CurrentState != ECinderOnlineState::Finished;
}

void UCinderOnlineSubsystem::ReportUncertainOrders(const FString& Reason)
{
    const int32 Count = PendingCommands.Num();
    CommandAcknowledgements.Reset();
    if (!Count) return;
    PendingCommands.Reset();
    bLastOrderAccepted = false;
    LastOrderFeedback = FString::Printf(TEXT("%s %d order(s) have no acknowledgement. Check the refreshed battlefield and jobs before issuing them again."), *Reason, Count);
    ++OrderFeedbackSerial;
}

double UCinderOnlineSubsystem::ReconnectDelay(int32 Attempts)
{
    const double Base = FMath::Min(5.0, 0.5 * FMath::Pow(2.0, FMath::Clamp(Attempts, 0, 4)));
    return FMath::Clamp(Base * FMath::FRandRange(0.85f, 1.15f), 0.5, 5.0);
}

void UCinderOnlineSubsystem::CreateRoom(const FString& Url, const FString& Name, int32 MapIndex, int32 PlayerCount)
{
    if (HasRoom() || bHasMatch) return;
    if (PlayerCount != 2 && PlayerCount != 4) { Fail(TEXT("Choose a two-player or four-player room.")); return; }
    Players = PlayerCount;
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
    FString Endpoint, EndpointError;
    const FString CleanName = Name.TrimStartAndEnd();
    const FString CleanCode = Code.TrimStartAndEnd().ToUpper();
    if (!ValidateEndpoint(Url, Mode, Endpoint, EndpointError)) { Fail(EndpointError); return; }
    if (CleanName.IsEmpty() || CleanName.Len() > 24 || !SafeText(CleanName))
    { Fail(TEXT("Use a player name between 1 and 24 characters.")); return; }
    if (!bCreate)
    {
        bool bCode = CleanCode.Len() == 6;
        for (TCHAR Char : CleanCode) bCode &= (Char >= 'A' && Char <= 'Z') || (Char >= '0' && Char <= '9');
        if (!bCode) { Fail(TEXT("Enter the six-character room code.")); return; }
    }
    ServerEndpoint = Endpoint; DisplayName = CleanName; PendingCode = CleanCode; bCreating = bCreate;
    (Mode == ECinderConnectionMode::Internet ? InternetServerEndpoint : LANServerEndpoint) = Endpoint;
    Room.Empty(); SeatToken.Empty(); Team = -1; ResultWinner = -1;
    bHasMatch = bHasSnapshot = bResumeAllowed = bWaitingForWorker = false; NextSequence = 1;
    for (int32 Seat = 0; Seat < MaxPlayers; ++Seat) { Names[Seat].Empty(); Connected[Seat] = Ready[Seat] = false; }
    LastError.Empty(); LastOrderFeedback.Empty();
    PendingCommands.Reset(); CommandAcknowledgements.Reset();
    ReconnectDeadline = SnapshotDeadline = 0; ReconnectAttempts = 0; PingMs = -1;
    if (GConfig && !FApp::IsUnattended() && !GIsAutomationTesting)
        SavePreferences(Preferences());
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
    SnapshotDeadline = bResume && bHasMatch ? ConnectionStartedAt + SnapshotTimeoutSeconds : 0;
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
            if (Self->bCreating)
            {
                Message->SetNumberField(TEXT("map"), Self->Map);
                Message->SetNumberField(TEXT("playerCount"), Self->Players);
            }
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
        FString NewRoom, Token; double Seat = -1, Count = 0;
        if (!Object->TryGetStringField(TEXT("room"), NewRoom) || NewRoom.Len() != 6 ||
            !Object->TryGetStringField(TEXT("token"), Token) || Token.Len() < 16 || Token.Len() > 128 ||
            !ReadIntegerField(*Object, TEXT("playerCount"), 2, MaxPlayers, Count) || (Count != 2 && Count != 4) ||
            !ReadIntegerField(*Object, TEXT("team"), 0, Count - 1, Seat) ||
            (HasRoom() && (Count != Players || Seat != Team || NewRoom != Room)))
        { Fail(TEXT("The server returned an invalid seat.")); return; }
        Room = NewRoom; SeatToken = Token; Team = static_cast<int32>(Seat); Players = static_cast<int32>(Count); bResumeAllowed = true;
        bWaitingForWorker = false;
        CurrentState = bHasMatch ? ECinderOnlineState::Reconnecting : ECinderOnlineState::Lobby;
        Status = FString::Printf(TEXT("Room connected. All %d players must be ready."), Players);
        if (!bHasMatch) { ReconnectDeadline = 0; ReconnectAttempts = 0; }
        UE_LOG(LogCinderOnline, Display, TEXT("CINDERLINE_ONLINE seat=%d joined"), Team);
    }
    else if (Type == TEXT("lobby"))
    {
        double Count = 0;
        const TArray<TSharedPtr<FJsonValue>>* Roster = nullptr;
        if (!HasRoom() || !ReadIntegerField(*Object, TEXT("playerCount"), 2, MaxPlayers, Count) || Count != Players
            || !Object->TryGetArrayField(TEXT("players"), Roster) || Roster->Num() != Players)
        { Fail(TEXT("The server returned an invalid room roster.")); return; }
        double MapValue = Map;
        if (Object->HasField(TEXT("map")) && !ReadIntegerField(*Object, TEXT("map"), 0, 2, MapValue))
        { Fail(TEXT("The server returned an invalid room map.")); return; }
        FString NewNames[MaxPlayers]; bool NewConnected[MaxPlayers] = {}, NewReady[MaxPlayers] = {};
        bool bAllConnected = true, bAllReady = true;
        for (int32 Index = 0; Index < Players; ++Index)
        {
            const auto& Value = (*Roster)[Index];
            if (!Value || (Value->Type != EJson::Null && Value->Type != EJson::Object))
            { Fail(TEXT("The server returned an invalid room player.")); return; }
            if (Value->Type == EJson::Object)
            {
                const auto Player = Value->AsObject();
                if (!Player || !Player->TryGetStringField(TEXT("name"), NewNames[Index])
                    || !Player->TryGetBoolField(TEXT("connected"), NewConnected[Index])
                    || !Player->TryGetBoolField(TEXT("ready"), NewReady[Index]))
                { Fail(TEXT("The server returned an invalid room player.")); return; }
                NewNames[Index] = NewNames[Index].Left(24);
            }
            bAllConnected &= NewConnected[Index]; bAllReady &= NewReady[Index];
        }
        for (int32 Index = 0; Index < MaxPlayers; ++Index)
        { Names[Index] = MoveTemp(NewNames[Index]); Connected[Index] = NewConnected[Index]; Ready[Index] = NewReady[Index]; }
        Map = static_cast<int32>(MapValue);
        if (!bHasMatch)
        {
            CurrentState = ECinderOnlineState::Lobby;
            if (!bAllConnected || !bAllReady) bWaitingForWorker = false;
            Status = bWaitingForWorker ? TEXT("Waiting for server match capacity. Your room is still connected; you can wait or leave.")
                : bAllConnected ? FString::Printf(TEXT("All %d players connected. Ready up to begin."), Players)
                : TEXT("Share the room code. Waiting for the remaining players.");
        }
    }
    else if (Type == TEXT("started"))
    {
        if (SnapshotDeadline == 0) SnapshotDeadline = LastReceivedAt + SnapshotTimeoutSeconds;
        bHasMatch = true; bWaitingForWorker = false; CurrentState = ECinderOnlineState::Starting;
        LastError.Empty();
        Status = TEXT("Synchronizing the battlefield...");
    }
    else if (Type == TEXT("ack"))
    {
        double Seq; bool Accepted; FString Text;
        if (!ReadIntegerField(*Object, TEXT("seq"), 1, MAX_uint32, Seq) ||
            !Object->TryGetBoolField(TEXT("accepted"), Accepted)) return;
        const uint32 Sequence = static_cast<uint32>(Seq);
        if (!PendingCommands.Remove(Sequence)) return;
        Object->TryGetStringField(TEXT("message"), Text);
        bLastOrderAccepted = Accepted;
        LastOrderFeedback = Text.Left(240);
        if (LastOrderFeedback.IsEmpty()) LastOrderFeedback = Accepted ? TEXT("Order accepted") : TEXT("Order rejected");
        if (CommandAcknowledgements.Num() >= 128)
        {
            uint32 Oldest = MAX_uint32;
            for (const auto& Entry : CommandAcknowledgements) Oldest = FMath::Min(Oldest, Entry.Key);
            CommandAcknowledgements.Remove(Oldest);
        }
        FCinderOnlineCommandAcknowledgement Acknowledgement;
        Acknowledgement.bAccepted = Accepted;
        Acknowledgement.Message = LastOrderFeedback;
        CommandAcknowledgements.Add(Sequence, MoveTemp(Acknowledgement));
        ++OrderFeedbackSerial;
    }
    else if (Type == TEXT("peer"))
    {
        double Seat = -1; bool bPeerConnected = false;
        if (!ReadIntegerField(*Object, TEXT("team"), 0, Players - 1, Seat) || Seat == Team
            || !Object->TryGetBoolField(TEXT("connected"), bPeerConnected)) return;
        const int32 Peer = static_cast<int32>(Seat);
        Connected[Peer] = bPeerConnected;
        const FString Name = Names[Peer].IsEmpty() ? FString::Printf(TEXT("Player %d"), Peer + 1) : Names[Peer];
        Status = Name + (bPeerConnected ? TEXT(" reconnected.") : TEXT(" disconnected. The match continues during their reconnect window."));
    }
    else if (Type == TEXT("result"))
    {
        double Winner;
        if (!ReadIntegerField(*Object, TEXT("winner"), -2, Players - 1, Winner) || Winner == -1 || Team < 0) return;
        ResultWinner = Winner == -2 ? -2 : (static_cast<int32>(Winner) - Team + Players) % Players;
        if (bHasSnapshot)
        {
            ApplyConfirmedResult();
            ++ReceivedSnapshotSerial;
        }
        CurrentState = ECinderOnlineState::Finished; bHasMatch = true; SnapshotDeadline = ReconnectDeadline = 0;
        Status = ResultWinner == -2 ? TEXT("Draw confirmed by the server.")
            : ResultWinner == 0 ? TEXT("Victory confirmed by the server.") : TEXT("Defeat confirmed by the server.");
        PendingCommands.Reset();
        CommandAcknowledgements.Reset();
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
        FString Text, Code; bool bRetryable = false;
        Object->TryGetStringField(TEXT("message"), Text);
        Object->TryGetStringField(TEXT("code"), Code);
        const bool bHasRetryable = Object->TryGetBoolField(TEXT("retryable"), bRetryable);
        if (Text.IsEmpty()) Text = TEXT("The server could not complete that request.");
        if (Code == TEXT("worker_capacity") && bHasRetryable && bRetryable
            && CurrentState == ECinderOnlineState::Lobby && HasRoom() && !bHasMatch
            && Socket && Socket->IsConnected())
        {
            // Admission is queued on this live room. Disconnecting here would
            // remove its seats and lose its place in the server's worker queue.
            bWaitingForWorker = true;
            LastError.Empty();
            Status = TEXT("Waiting for server match capacity. Your room is still connected; you can wait or leave.");
            return;
        }
        const bool bSeatRace = Code == TEXT("seat_connected")
            || (Code.IsEmpty() && Text == TEXT("That seat is already connected."));
        if (CurrentState == ECinderOnlineState::Reconnecting && bSeatRace && (!bHasRetryable || bRetryable))
        {
            // The replacement socket can reach the server before it observes the old
            // socket's close. Retain the credentials and retry inside the grace window.
            CloseSocket(); SnapshotDeadline = 0;
            ReportUncertainOrders(TEXT("Reconnecting."));
            LastError.Empty(); Status = TEXT("Waiting for the previous connection to close...");
            NextReconnectAt = FPlatformTime::Seconds() + ReconnectDelay(0);
            return;
        }
        if (bHasRetryable && bRetryable && CanReconnectSession())
        { ConnectionLost(Text.Left(240)); return; }
        if (bHasRetryable && !bRetryable && (CurrentState == ECinderOnlineState::Connecting
            || CurrentState == ECinderOnlineState::Reconnecting || CurrentState == ECinderOnlineState::Starting))
        { bResumeAllowed = false; Fail(Text); return; }
        LastError = Text.Left(240); Status = LastError;
        if (!HasRoom() || CurrentState == ECinderOnlineState::Reconnecting || CurrentState == ECinderOnlineState::Connecting
            || CurrentState == ECinderOnlineState::Starting) Fail(LastError);
    }
}

void UCinderOnlineSubsystem::ApplyConfirmedResult()
{
    if (!bHasSnapshot || ResultWinner == -1) return;
    Snapshot.winner = ResultWinner;
    Snapshot.eliminatedMask = static_cast<std::uint8_t>(((1u << Players) - 1u)
        & (ResultWinner == -2 ? ~0u : ~(1u << ResultWinner)));
    const bool bLocalEliminated = (Snapshot.eliminatedMask & 1u) != 0;
    // A result can arrive before the final frame. Preserve only previously
    // known actors that can still exist; never manufacture an opposing army.
    Snapshot.entities.erase(std::remove_if(Snapshot.entities.begin(), Snapshot.entities.end(), [&](const cinder::Entity& Entity)
    {
        return Entity.team >= 0 && (bLocalEliminated || (Snapshot.eliminatedMask & (1u << Entity.team)) != 0);
    }), Snapshot.entities.end());
    std::unordered_set<cinder::Id> Retained;
    for (const auto& Entity : Snapshot.entities) Retained.insert(Entity.id);
    for (auto& Entity : Snapshot.entities)
        for (cinder::Id* Reference : {&Entity.target, &Entity.resourceTarget, &Entity.builderId, &Entity.workTarget})
            if (*Reference && !Retained.count(*Reference)) *Reference = 0;
    if (bLocalEliminated)
    {
        // Eliminated players retain explored terrain and known ore only. They
        // gain no spectator vision, even from a late pre-result snapshot.
        for (auto& Cell : Snapshot.fog) if (Cell == 2) Cell = 1;
        Snapshot.effects.clear();
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
    if (!bValid || Next.config.playerCount != Players) { Fail(TEXT("Server snapshot failed protocol validation.")); return; }
    if (bHasSnapshot && Next.tick < Snapshot.tick) return;
    const bool bWasEliminated = IsEliminated();
    Snapshot = MoveTemp(Next); bHasSnapshot = bHasMatch = true;
    if (CurrentState == ECinderOnlineState::Finished && ResultWinner != -1) Snapshot.winner = ResultWinner;
    LastSnapshotAt = LastReceivedAt = FPlatformTime::Seconds();
    ++ReceivedSnapshotSerial;
    ReconnectDeadline = SnapshotDeadline = 0; ReconnectAttempts = 0; LastError.Empty();
    if (Snapshot.winner != -1)
    {
        ResultWinner = Snapshot.winner; CurrentState = ECinderOnlineState::Finished;
        ApplyConfirmedResult();
        Status = ResultWinner == -2 ? TEXT("Draw confirmed by the server.")
            : ResultWinner == 0 ? TEXT("Victory confirmed by the server.") : TEXT("Defeat confirmed by the server.");
        PendingCommands.Reset();
        CommandAcknowledgements.Reset();
    }
    else if (CurrentState != ECinderOnlineState::Finished)
    {
        const bool bRecovered = CurrentState != ECinderOnlineState::Playing;
        CurrentState = ECinderOnlineState::Playing;
        if (IsEliminated())
        {
            PendingCommands.Reset();
            CommandAcknowledgements.Reset();
            if (bRecovered || !bWasEliminated)
                Status = TEXT("You are eliminated. The remaining players are still fighting. You can leave the match.");
        }
        else if (bRecovered) Status = TEXT("Online match live. Menus do not pause the match.");
    }
}

bool UCinderOnlineSubsystem::IsEliminated() const
{
    return bHasSnapshot && (Snapshot.eliminatedMask & 1u) != 0;
}

int32 UCinderOnlineSubsystem::RemainingPlayers() const
{
    int32 Remaining = Players;
    if (bHasSnapshot)
        for (int32 Seat = 0; Seat < Players; ++Seat)
            if ((Snapshot.eliminatedMask & (1u << Seat)) != 0) --Remaining;
    return Remaining;
}

bool UCinderOnlineSubsystem::CanSendOrders() const
{
    return CurrentState == ECinderOnlineState::Playing && Socket && Socket->IsConnected() &&
        bHasSnapshot && !IsEliminated() && FPlatformTime::Seconds() - LastSnapshotAt < 3.0 && PendingCommands.Num() < 64;
}

bool UCinderOnlineSubsystem::SendCommand(const cinder::Command& Command, uint32* OutSequence)
{
    if (OutSequence) *OutSequence = 0;
    if (!CanSendOrders() || NextSequence == 0) return false;
    const uint32 Sequence = NextSequence++;
    const auto Bytes = cinder::net::encodeCommand(Command, Sequence);
    if (Bytes.empty()) return false;
    PendingCommands.Add(Sequence, FPlatformTime::Seconds());
    Socket->Send(Bytes.data(), Bytes.size(), true);
    if (OutSequence) *OutSequence = Sequence;
    return true;
}

bool UCinderOnlineSubsystem::ConsumeCommandAcknowledgement(uint32 Sequence,
    FCinderOnlineCommandAcknowledgement& Out)
{
    if (FCinderOnlineCommandAcknowledgement* Found = CommandAcknowledgements.Find(Sequence))
    {
        Out = MoveTemp(*Found);
        CommandAcknowledgements.Remove(Sequence);
        return true;
    }
    return false;
}

void UCinderOnlineSubsystem::SetReady(bool Value)
{
    if (CurrentState != ECinderOnlineState::Lobby) return;
    auto Message = Control(TEXT("ready")); Message->SetBoolField(TEXT("ready"), Value); SendControl(Message);
}

bool UCinderOnlineSubsystem::Surrender()
{
    if (!bHasMatch || IsEliminated() || CurrentState == ECinderOnlineState::Finished || !Socket || !Socket->IsConnected()) return false;
    SendControl(Control(TEXT("surrender")));
    return true;
}

void UCinderOnlineSubsystem::Leave()
{
    if (CurrentState == ECinderOnlineState::Leaving) return;
    bHasMatch = bHasSnapshot = false; SnapshotDeadline = 0;
    PendingCommands.Reset();
    CommandAcknowledgements.Reset();
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
    bHasMatch = bHasSnapshot = bResumeAllowed = bWaitingForWorker = false;
    ReconnectDeadline = SnapshotDeadline = LeaveDeadline = 0; ReconnectAttempts = 0;
    PendingCommands.Reset(); CommandAcknowledgements.Reset();
    for (int32 Seat = 0; Seat < MaxPlayers; ++Seat) { Names[Seat].Empty(); Connected[Seat] = Ready[Seat] = false; }
    Players = 2;
    Status = TEXT("Left the online room."); LastError.Empty();
}

void UCinderOnlineSubsystem::Reconnect()
{
    if (PrepareReconnect(FPlatformTime::Seconds())) OpenSocket(true);
}

bool UCinderOnlineSubsystem::PrepareReconnect(double Now)
{
    if (!CanReconnectSession()) return false;
    // An explicit retry after expiry may still recover a retained match result.
    // Automatic retry checks its existing deadline before reaching this method.
    if (CurrentState == ECinderOnlineState::Error || ReconnectDeadline == 0 || Now >= ReconnectDeadline)
    { ReconnectDeadline = Now + ReconnectWindowSeconds; ReconnectAttempts = 0; }
    if (ReconnectAttempts >= MaxReconnectAttempts)
    { Fail(TEXT("Reconnect attempts exhausted. Retry to recover the match result, or leave the session.")); return false; }
    ReportUncertainOrders(TEXT("Reconnecting."));
    ++ReconnectAttempts;
    return true;
}

void UCinderOnlineSubsystem::RecoverConnection()
{
    if (!CanReconnectSession()) return;
    const double Now = FPlatformTime::Seconds();
    if (CurrentState == ECinderOnlineState::Error || (ReconnectDeadline > 0 && Now >= ReconnectDeadline))
    { Reconnect(); return; }
    if (!Socket || !Socket->IsConnected() || Now - LastReceivedAt > 12
        || (bHasSnapshot && Now - LastSnapshotAt >= 3))
    {
        if (CurrentState != ECinderOnlineState::Reconnecting) ConnectionLost(TEXT("Refreshing the connection after returning to the game."));
        if (!Socket) Reconnect();
    }
}

void UCinderOnlineSubsystem::ConnectionLost(const FString& Reason)
{
    if (CurrentState == ECinderOnlineState::Leaving) { FinishLeave(); return; }
    if (bIntentionalClose) return;
    if (!HasRoom() || SeatToken.IsEmpty()) { Fail(Reason); return; }
    CloseSocket();
    SnapshotDeadline = 0;
    ReportUncertainOrders(Reason);
    if (CurrentState == ECinderOnlineState::Finished) return;
    const double Now = FPlatformTime::Seconds();
    if (ReconnectDeadline == 0) ReconnectDeadline = Now + ReconnectWindowSeconds;
    CurrentState = ECinderOnlineState::Reconnecting;
    LastError.Empty();
    Status = Reason.Left(160) + TEXT(" Reconnecting; the match may continue on the server.");
    NextReconnectAt = Now + ReconnectDelay(ReconnectAttempts);
}

void UCinderOnlineSubsystem::Tick(float DeltaTime)
{
    const double Now = FPlatformTime::Seconds();
    if (CurrentState == ECinderOnlineState::Leaving)
    {
        if (!Socket || !Socket->IsConnected() || Now >= LeaveDeadline) FinishLeave();
        return;
    }
    if (ReconnectDeadline > 0 && Now >= ReconnectDeadline
        && (CurrentState == ECinderOnlineState::Reconnecting || CurrentState == ECinderOnlineState::Starting))
    { Fail(TEXT("Reconnect window expired. Retry to recover the match result, or leave the session.")); return; }
    if (CurrentState == ECinderOnlineState::Reconnecting)
    {
        if (!Socket && ReconnectAttempts >= MaxReconnectAttempts)
        { Fail(TEXT("Reconnect attempts exhausted. Retry to recover the match result, or leave the session.")); return; }
        if (!Socket && Now >= NextReconnectAt) Reconnect();
    }
    if (SnapshotDeadline > 0 && Now >= SnapshotDeadline && CurrentState != ECinderOnlineState::Finished)
    { ConnectionLost(TEXT("The first battlefield update did not arrive in time.")); return; }
    if ((CurrentState == ECinderOnlineState::Connecting || CurrentState == ECinderOnlineState::Reconnecting)
        && Socket && Now - ConnectionStartedAt >= SnapshotTimeoutSeconds)
    { ConnectionLost(TEXT("The room connection did not finish in time.")); return; }
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
    bool bAcknowledgementExpired = false;
    for (const auto& Pending : PendingCommands)
        if (Now - Pending.Value > 5) { bAcknowledgementExpired = true; break; }
    if (bAcknowledgementExpired)
        ConnectionLost(TEXT("Order acknowledgements timed out. Resynchronizing the battlefield."));
}
