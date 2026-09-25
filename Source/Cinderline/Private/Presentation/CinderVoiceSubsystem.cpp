#include "Presentation/CinderVoiceSubsystem.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCommanderBridge.h"
#include "Presentation/CinderPlayerController.h"
#include "CinderVoiceAudio.h"
#include "Async/Async.h"
#include "Containers/Queue.h"
#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"
#include "IWebSocket.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "WebSocketsModule.h"

// Audio threads only touch this bounded inbox, never a UObject or the simulation.
struct FCinderVoiceCaptureQueue
{
    FCriticalSection Mutex;
    TQueue<TArray<uint8>> Chunks;
    int32 Bytes = 0;
    bool bOverflowed = false;
    void Push(TArray<uint8> PCM)
    {
        FScopeLock Lock(&Mutex);
        if (PCM.Num() <= 0 || PCM.Num() > 9600 || PCM.Num() % 2 != 0) return;
        if (Bytes + PCM.Num() > 48000) { bOverflowed = true; return; }
        Bytes += PCM.Num();
        Chunks.Enqueue(MoveTemp(PCM));
    }
    bool Pop(TArray<uint8>& PCM)
    {
        FScopeLock Lock(&Mutex);
        if (!Chunks.Dequeue(PCM)) return false;
        Bytes -= PCM.Num();
        return true;
    }
    bool Overflowed() { FScopeLock Lock(&Mutex); return bOverflowed; }
    void Clear()
    {
        FScopeLock Lock(&Mutex);
        Chunks.Empty(); Bytes = 0; bOverflowed = false;
    }
};

namespace
{
TSharedRef<FJsonObject> VoiceMessage(const TCHAR* Type)
{
    auto Message = MakeShared<FJsonObject>();
    Message->SetStringField(TEXT("type"), Type);
    return Message;
}
FString Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
{
    FString Value;
    if (Object) Object->TryGetStringField(Name, Value);
    return Value;
}
TSharedPtr<FJsonObject> VoiceFailure(const TCHAR* Message)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("status"), TEXT("rejected"));
    Result->SetStringField(TEXT("message"), Message);
    return Result;
}
bool SafeToken(const FString& Token)
{
    if (Token.Len() < 24 || Token.Len() > 512 || Token.StartsWith(TEXT("sk-"))) return false;
    for (const TCHAR C : Token) if (C <= 32 || C >= 127) return false;
    return true;
}
}

void UCinderVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Bridge = MakeShared<FCinderCommanderBridge>();
    BackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(
        this, &UCinderVoiceSubsystem::ApplicationBackgrounded);
    bInitialized = true;
}

void UCinderVoiceSubsystem::Deinitialize()
{
    EndSession();
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
    Bridge.Reset();
    bInitialized = false;
    Super::Deinitialize();
}

TStatId UCinderVoiceSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UCinderVoiceSubsystem, STATGROUP_Tickables);
}

bool UCinderVoiceSubsystem::ValidateEndpoint(const FString& URL, FString& Error)
{
    Error.Empty();
    const auto Reject = [&Error](const TCHAR* Why) { Error = Why; return false; };
    if (URL.Len() > 512 || !URL.EndsWith(TEXT("/voice"), ESearchCase::CaseSensitive)
        || URL.Contains(TEXT("@")) || URL.Contains(TEXT("?")) || URL.Contains(TEXT("#"))
        || URL.Contains(TEXT("%")) || URL.Contains(TEXT("\\")))
        return Reject(TEXT("Configure a commander /voice address without credentials or query parameters."));
    for (const TCHAR C : URL)
        if (C <= 32 || C >= 127) return Reject(TEXT("The commander address contains invalid characters."));
    const bool bTLS = URL.StartsWith(TEXT("wss://"), ESearchCase::CaseSensitive);
    const bool bPlain = URL.StartsWith(TEXT("ws://"), ESearchCase::CaseSensitive);
    if (!bTLS && !bPlain) return Reject(TEXT("Configure a wss:// commander address."));
    const FString Authority = URL.Mid(bTLS ? 6 : 5, URL.Len() - (bTLS ? 6 : 5) - 6);
    if (Authority.IsEmpty() || Authority.Contains(TEXT("/")))
        return Reject(TEXT("Configure a valid commander host at /voice."));
    FString Host = Authority, Port;
    if (Authority.StartsWith(TEXT("[")))
    {
        int32 Close;
        if (!Authority.FindChar(']', Close)) return Reject(TEXT("The commander IPv6 address needs brackets."));
        Host = Authority.Left(Close + 1);
        if (Close + 1 < Authority.Len())
        {
            if (Authority[Close + 1] != ':') return Reject(TEXT("The commander port is invalid."));
            Port = Authority.Mid(Close + 2);
            if (Port.IsEmpty()) return Reject(TEXT("The commander port is invalid."));
        }
    }
    else
    {
        int32 Colon;
        if (Authority.FindChar(':', Colon))
        {
            Host = Authority.Left(Colon); Port = Authority.Mid(Colon + 1);
            if (Port.IsEmpty()) return Reject(TEXT("The commander port is invalid."));
        }
        for (const TCHAR C : Host)
            if (!FChar::IsAlnum(C) && C != '.' && C != '-')
                return Reject(TEXT("The commander host is invalid."));
    }
    if (Host.IsEmpty()) return Reject(TEXT("The commander address needs a host."));
    if (!Port.IsEmpty())
    {
        for (const TCHAR C : Port) if (C < '0' || C > '9') return Reject(TEXT("The commander port is invalid."));
        if (Port.Len() > 5 || FCString::Atoi(*Port) < 1 || FCString::Atoi(*Port) > 65535)
            return Reject(TEXT("The commander port is invalid."));
    }
    const bool bLoopback = Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase)
        || Host == TEXT("127.0.0.1") || Host == TEXT("[::1]");
    if (!bTLS && !bLoopback) return Reject(TEXT("Remote voice connections require wss://. Plain ws:// is only allowed on this device."));
    return true;
}

void UCinderVoiceSubsystem::Toggle(ACinderPlayerController* Player)
{
    if (bActive) { EndSession(); return; }
    if (!Player || !Player->CommanderGameplayActive()) return;
    Controller = Player;
    Endpoint.Empty(); AccessToken.Empty(); Caption.Empty(); Receipt.Empty(); bError = false;
    FConfigFile Config;
    Config.Read(FPaths::ProjectSavedDir() / TEXT("Config/Voice.ini"));
    Config.GetString(TEXT("CinderVoice"), TEXT("Endpoint"), Endpoint);
    Config.GetString(TEXT("CinderVoice"), TEXT("AccessToken"), AccessToken);
    FParse::Value(FCommandLine::Get(), TEXT("CinderVoiceEndpoint="), Endpoint);
    FString Error;
    if (!ValidateEndpoint(Endpoint, Error))
    {
        Fail(Endpoint.IsEmpty() ? TEXT("Voice needs a commander gateway. Configure Saved/Config/Voice.ini first.") : Error);
        return;
    }
    if (!SafeToken(AccessToken))
    {
        Fail(TEXT("Configure a commander access token in Voice.ini. Provider API keys belong only on the gateway."));
        return;
    }
    Battlefield = Player->Battlefield();
    MatchGeneration = Battlefield.IsValid() ? Battlefield->MatchGeneration() : 0;
    Bridge->Reset();
    bActive = true; bReady = false; bMuted = false;
    StartedAt = LastActivityAt = FPlatformTime::Seconds();
    AudioClock = 0; LastSpeechAt = -1; NextPollAt = 0;
    Status = TEXT("Microphone...");
    const uint64 Generation = ++SessionGeneration;
    const TWeakObjectPtr<UCinderVoiceSubsystem> WeakThis(this);
    CaptureQueue = MakeShared<FCinderVoiceCaptureQueue, ESPMode::ThreadSafe>();
    const auto Queue = CaptureQueue;
    Audio = MakeShareable(FCinderVoiceAudio::Create().Release());
    Audio->Start([Queue](TArray<uint8> PCM) { Queue->Push(MoveTemp(PCM)); },
        [WeakThis, Generation](ECinderVoiceAudioState State, const FString& Message)
        {
            AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation, State, Message]
            {
                auto* Self = WeakThis.Get();
                if (!Self || !Self->bActive || Self->SessionGeneration != Generation) return;
                if (State == ECinderVoiceAudioState::Listening) Self->Connect(Generation);
                else if (State == ECinderVoiceAudioState::Error) Self->Fail(Message.IsEmpty()
                    ? TEXT("The microphone could not start.") : Message);
                else if (State == ECinderVoiceAudioState::Stopped) Self->EndSession();
            });
        });
}

void UCinderVoiceSubsystem::Connect(uint64 Generation)
{
    if (Socket || !bActive || Generation != SessionGeneration) return;
    if (!HasCurrentMatch()) { EndSession(); return; }
    Status = TEXT("Connecting...");
    TMap<FString, FString> Headers;
    Headers.Add(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
    Socket = FWebSocketsModule::Get().CreateWebSocket(Endpoint, TArray<FString>(), Headers);
    AccessToken.Empty();
    const TWeakObjectPtr<UCinderVoiceSubsystem> WeakThis(this);
    Socket->OnConnected().AddLambda([WeakThis, Generation]
    {
        AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation]
        {
            auto* Self = WeakThis.Get();
            if (!Self || !Self->bActive || Self->SessionGeneration != Generation) return;
            if (!Self->HasCurrentMatch()) { Self->EndSession(); return; }
            auto Message = VoiceMessage(TEXT("session.open"));
            Message->SetNumberField(TEXT("protocol"), 1);
            Message->SetObjectField(TEXT("observation"), Self->Bridge->Capture(Self->Controller.Get()));
            Self->Send(Message);
        });
    });
    Socket->OnMessage().AddLambda([WeakThis, Generation](const FString& Message)
    {
        if (Message.Len() > 131072) return;
        AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation, Message]
        {
            auto* Self = WeakThis.Get();
            if (Self && Self->bActive && Self->SessionGeneration == Generation) Self->Receive(Message);
        });
    });
    Socket->OnConnectionError().AddLambda([WeakThis, Generation](const FString&)
    {
        AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation]
        {
            auto* Self = WeakThis.Get();
            if (Self && Self->bActive && Self->SessionGeneration == Generation)
                Self->Fail(TEXT("Voice could not connect. Check the gateway address and access token."));
        });
    });
    Socket->OnClosed().AddLambda([WeakThis, Generation](int32, const FString&, bool)
    {
        AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation]
        {
            auto* Self = WeakThis.Get();
            if (Self && Self->bActive && Self->SessionGeneration == Generation)
                Self->Fail(TEXT("Voice disconnected. Tap Voice to connect again. Orders with unknown outcomes are not retried."));
        });
    });
    Socket->Connect();
}

void UCinderVoiceSubsystem::Send(const TSharedRef<FJsonObject>& Message)
{
    if (!Socket || !Socket->IsConnected()) return;
    FString Text;
    const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
    if (FJsonSerializer::Serialize(Message, Writer)) Socket->Send(Text);
}

void UCinderVoiceSubsystem::SendResult(const FString& RequestId, const TSharedPtr<FJsonObject>& Result)
{
    auto Message = VoiceMessage(TEXT("rpc.result"));
    Message->SetStringField(TEXT("id"), RequestId);
    Message->SetObjectField(TEXT("result"), Result ? Result : VoiceFailure(TEXT("No result available.")));
    Send(Message);
}

void UCinderVoiceSubsystem::Receive(const FString& Text)
{
    TSharedPtr<FJsonObject> Message;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Message) || !Message)
    { Fail(TEXT("Voice received an invalid gateway message.")); return; }
    const FString Type = Field(Message, TEXT("type"));
    if (Type == TEXT("session.ready"))
    {
        bReady = true; Status = bMuted ? TEXT("Muted") : TEXT("Listening");
        if (CaptureQueue) CaptureQueue->Clear();
        auto Mute = VoiceMessage(TEXT("session.mute"));
        Mute->SetBoolField(TEXT("muted"), bMuted); Send(Mute);
    }
    else if (Type == TEXT("session.state"))
    {
        const FString State = Field(Message, TEXT("state"));
        if (State == TEXT("error")) { Fail(Field(Message, TEXT("message"))); return; }
        if (State == TEXT("ended")) { EndSession(); return; }
        if (bMuted) Status = TEXT("Muted");
        else if (State == TEXT("working")) Status = TEXT("Working...");
        else if (State == TEXT("listening")) Status = TEXT("Listening");
    }
    else if (Type == TEXT("caption"))
    {
        Caption = Field(Message, TEXT("text")).Left(512);
        LastActivityAt = FPlatformTime::Seconds();
    }
    else if (Type == TEXT("audio.delta"))
    {
        const FString Encoded = Field(Message, TEXT("audio"));
        TArray<uint8> PCM;
        if (Encoded.Len() > 65536 || !FBase64::Decode(Encoded, PCM) || PCM.Num() % 2 != 0)
        { Fail(TEXT("Voice received invalid audio.")); return; }
        if (Audio && bReady) Audio->QueuePlayback(MoveTemp(PCM));
    }
    else if (Type == TEXT("audio.clear")) { if (Audio) Audio->ClearPlayback(); }
    else if (Type == TEXT("receipt"))
    {
        Receipt = Field(Message, TEXT("message")).Left(512);
        Caption.Empty();
        LastActivityAt = FPlatformTime::Seconds();
    }
    else if (Type == TEXT("operation.cancel")) Bridge->Cancel(Field(Message, TEXT("operationId")));
    else if (Type == TEXT("rpc.request"))
    {
        const FString Id = Field(Message, TEXT("id"));
        const FString Method = Field(Message, TEXT("method"));
        if (Id.IsEmpty() || Id.Len() > 160) return;
        if (!HasCurrentMatch()) { SendResult(Id, VoiceFailure(TEXT("The match is no longer active."))); EndSession(); return; }
        if (Method == TEXT("get_observation")) SendResult(Id, Bridge->Capture(Controller.Get()));
        else if (Method == TEXT("execute"))
        {
            const TSharedPtr<FJsonObject>* Params = nullptr;
            if (!Message->TryGetObjectField(TEXT("params"), Params) || !Params || !*Params)
            { SendResult(Id, VoiceFailure(TEXT("The command parameters are missing."))); return; }
            if (PendingRequests.Num() >= 16)
            { SendResult(Id, VoiceFailure(TEXT("Too many voice orders are awaiting acknowledgement."))); return; }
            const auto Result = Bridge->Execute(Controller.Get(), *Params);
            if (Field(Result, TEXT("status")) == TEXT("pending"))
                PendingRequests.Add(Field(Result, TEXT("operationId")), Id);
            SendResult(Id, Result);
        }
        else SendResult(Id, VoiceFailure(TEXT("This voice tool is not supported.")));
    }
}

bool UCinderVoiceSubsystem::HasCurrentMatch() const
{
    return Controller.IsValid() && Battlefield.IsValid()
        && Controller->GetWorld() == GetWorld() && Controller->Battlefield() == Battlefield.Get()
        && Battlefield->MatchGeneration() == MatchGeneration && Controller->CommanderGameplayActive();
}

void UCinderVoiceSubsystem::DrainMicrophone(double Now)
{
    if (!CaptureQueue) return;
    if (CaptureQueue->Overflowed())
    { Fail(TEXT("Voice audio could not keep up. Tap Voice to reconnect.")); return; }
    TArray<uint8> PCM;
    for (int32 Count = 0; Count < 16 && CaptureQueue->Pop(PCM); ++Count)
    {
        if (!bReady || bMuted) continue;
        const double Duration = static_cast<double>(PCM.Num()) / 48000.0;
        double Energy = 0;
        for (int32 I = 0; I + 1 < PCM.Num(); I += 2)
        {
            const int16 Sample = static_cast<int16>(static_cast<uint16>(PCM[I]) | (static_cast<uint16>(PCM[I + 1]) << 8));
            const double Amplitude = static_cast<double>(Sample) / 32768.0;
            Energy += Amplitude * Amplitude;
        }
        const bool bSpeech = PCM.Num() > 0 && FMath::Sqrt(Energy / (PCM.Num() / 2)) > 0.018;
        if (bSpeech)
        {
            if (LastSpeechAt < 0 || AudioClock - LastSpeechAt >= 0.7)
            {
                auto Context = VoiceMessage(TEXT("context.capture"));
                Context->SetObjectField(TEXT("observation"), Bridge->Capture(Controller.Get()));
                Send(Context);
                // Stop already queued speech immediately when the player interrupts.
                if (Audio) Audio->ClearPlayback();
            }
            LastSpeechAt = AudioClock;
            LastActivityAt = Now;
        }
        AudioClock += Duration;
        auto Message = VoiceMessage(TEXT("audio.append"));
        Message->SetStringField(TEXT("audio"), FBase64::Encode(PCM));
        Send(Message);
    }
}

void UCinderVoiceSubsystem::Tick(float DeltaTime)
{
    if (!bActive) return;
    if (!HasCurrentMatch()) { EndSession(); return; }
    const double Now = FPlatformTime::Seconds();
    if (!bReady && Now - StartedAt > 45.0)
    { Fail(TEXT("Voice connection timed out. Tap Voice to try again.")); return; }
    if (bReady && Now - LastActivityAt > 180.0)
    { EndSession(); Status = TEXT("Voice ended after inactivity"); return; }
    DrainMicrophone(Now);
    if (!bActive || Now < NextPollAt) return;
    NextPollAt = Now + 0.05;
    for (const auto& Result : Bridge->Poll(Controller.Get()))
    {
        const FString Operation = Field(Result, TEXT("operationId"));
        if (const FString* Id = PendingRequests.Find(Operation))
        {
            SendResult(*Id, Result);
            PendingRequests.Remove(Operation);
        }
    }
}

void UCinderVoiceSubsystem::ToggleMute()
{
    if (!bActive) return;
    bMuted = !bMuted;
    if (Audio) Audio->SetMuted(bMuted);
    if (CaptureQueue) CaptureQueue->Clear();
    LastSpeechAt = -1;
    Status = bMuted ? TEXT("Muted") : bReady ? TEXT("Listening") : TEXT("Connecting...");
    auto Message = VoiceMessage(TEXT("session.mute"));
    Message->SetBoolField(TEXT("muted"), bMuted); Send(Message);
}

void UCinderVoiceSubsystem::Shutdown()
{
    ++SessionGeneration;
    bActive = false; bReady = false; bMuted = false;
    if (Audio) { Audio->Stop(); Audio.Reset(); }
    CaptureQueue.Reset();
    if (Socket)
    {
        Socket->OnConnected().Clear(); Socket->OnMessage().Clear();
        Socket->OnConnectionError().Clear(); Socket->OnClosed().Clear();
        Socket->Close(1000, TEXT("Voice session ended")); Socket.Reset();
    }
    if (Bridge) Bridge->Reset();
    PendingRequests.Reset();
    AccessToken.Empty(); Battlefield.Reset();
}

void UCinderVoiceSubsystem::EndSession()
{
    if (bActive) Send(VoiceMessage(TEXT("session.end")));
    Shutdown();
    bError = false; Status = TEXT("Voice ended"); Caption.Empty();
}

void UCinderVoiceSubsystem::Fail(const FString& Message)
{
    if (bActive) Send(VoiceMessage(TEXT("session.end")));
    Shutdown();
    bError = true; Status = TEXT("Voice unavailable");
    Caption = Message.IsEmpty() ? TEXT("Voice is unavailable. Check the commander gateway.") : Message.Left(512);
    if (Controller.IsValid()) Controller->Notify(Caption);
}

void UCinderVoiceSubsystem::ApplicationBackgrounded()
{
    if (bActive) EndSession();
}
