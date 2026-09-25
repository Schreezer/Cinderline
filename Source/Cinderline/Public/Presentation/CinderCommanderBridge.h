#pragma once

#include "CoreMinimal.h"
#include "Sim/Simulation.h"

class ACinderBattlefield;
class ACinderPlayerController;
class FJsonObject;

/** Game-thread-only boundary. Candidate resolution and execution never run in provider callbacks. */
class CINDERLINE_API FCinderCommanderBridge
{
public:
    TSharedPtr<FJsonObject> Capture(ACinderPlayerController* Controller);
    TSharedPtr<FJsonObject> Execute(ACinderPlayerController* Controller,
        const TSharedPtr<FJsonObject>& Params);
    TArray<TSharedPtr<FJsonObject>> Poll(ACinderPlayerController* Controller);
    void Cancel(const FString& OperationId);
    void Reset();

private:
    friend class FCinderCommanderBridgeIntegration;
    struct FCapture
    {
        FString Generation;
        double CreatedAt = 0;
        TMap<FString, std::vector<cinder::Id>> Groups;
        TMap<FString, cinder::Vec2> Locations;
        TMap<FString, cinder::Id> Targets;
    };
    struct FOperation
    {
        TSharedPtr<FJsonObject> Receipt;
        uint32 Sequence = 0;
        double SubmittedAt = 0;
        bool bPending = false;
        bool bDeliver = false;
    };
    void Synchronize(ACinderPlayerController* Controller);
    void PruneCaptures(double Now);
    TWeakObjectPtr<ACinderBattlefield> Battlefield;
    TWeakObjectPtr<ACinderPlayerController> PlayerController;
    FString Generation;
    uint64 MatchSerial = 0;
    uint64 LastTick = 0;
    TMap<FString, FCapture> Captures;
    TMap<FString, FOperation> Operations;
    /** Stable only within this match; does not reveal offline simulation allocation IDs. */
    TMap<cinder::Id, uint32> PublicHandles;
    uint32 NextPublicHandle = 1;
};
