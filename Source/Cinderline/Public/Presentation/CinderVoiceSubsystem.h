#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "CinderVoiceSubsystem.generated.h"

class ACinderPlayerController;
class ACinderBattlefield;
class FCinderCommanderBridge;
class FCinderVoiceAudio;
class FJsonObject;
class IWebSocket;
struct FCinderVoiceCaptureQueue;

/** Explicitly connected voice transport. Provider credentials never enter the game. */
UCLASS()
class CINDERLINE_API UCinderVoiceSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
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

    void Toggle(ACinderPlayerController* Player);
    void ToggleMute();
    void EndSession();
    bool IsActive() const { return bActive; }
    bool IsMuted() const { return bMuted; }
    bool HasError() const { return bError; }
    const FString& StatusText() const { return Status; }
    const FString& CaptionText() const { return Caption; }
    const FString& ReceiptText() const { return Receipt; }

    static bool ValidateEndpoint(const FString& Endpoint, FString& Error);
private:
    void Connect(uint64 Generation);
    void Receive(const FString& Message);
    void Send(const TSharedRef<FJsonObject>& Message);
    void SendResult(const FString& RequestId, const TSharedPtr<FJsonObject>& Result);
    void Fail(const FString& Message);
    void Shutdown();
    void ApplicationBackgrounded();
    bool HasCurrentMatch() const;
    void DrainMicrophone(double Now);

    TWeakObjectPtr<ACinderPlayerController> Controller;
    TWeakObjectPtr<ACinderBattlefield> Battlefield;
    TSharedPtr<IWebSocket> Socket;
    TSharedPtr<FCinderVoiceAudio> Audio;
    TSharedPtr<FCinderCommanderBridge> Bridge;
    TSharedPtr<FCinderVoiceCaptureQueue, ESPMode::ThreadSafe> CaptureQueue;
    TMap<FString, FString> PendingRequests;
    FDelegateHandle BackgroundHandle;
    FString Status = TEXT("Voice"), Caption, Receipt, Endpoint, AccessToken;
    uint64 SessionGeneration = 0, MatchGeneration = 0;
    double StartedAt = 0, LastActivityAt = 0, NextPollAt = 0, LastSpeechAt = -1;
    double AudioClock = 0;
    bool bInitialized = false, bActive = false, bReady = false, bMuted = false, bError = false;
};
