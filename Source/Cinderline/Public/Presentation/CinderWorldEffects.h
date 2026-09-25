#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sim/Simulation.h"
#include "CinderWorldEffects.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;

struct FCinderWorldEffectsStats
{
    uint64 Updates = 0;
    uint64 Uploads = 0;
    uint64 HiddenEvents = 0;
    uint64 DroppedForBudget = 0;
    int32 RenderedInstances = 0;
    int32 PeakInstances = 0;
};

/** Bounded instanced world geometry driven by visible simulation state. */
UCLASS()
class CINDERLINE_API UCinderWorldEffects : public UActorComponent
{
    GENERATED_BODY()

public:
    UCinderWorldEffects();

    /** Create the fixed ISM batches once the battlefield has loaded its primitive meshes. */
    void Initialize(USceneComponent* AttachRoot, UStaticMesh* Sphere, UStaticMesh* Cylinder,
        UStaticMesh* Cone, UStaticMesh* Plane, UMaterialInterface* FallbackMaterial);

    /** Submit one visible effect frame using the actual bound terrain mode.
        Standalone callers default to flat ground; the component never mutates simulation. */
    void Update(const cinder::Simulation& Simulation, int32 ViewerTeam = 0, bool bTerrainRelief = false);

    /** Clear all transient geometry. SuppressThroughId prevents saved effects replaying after load. */
    void Reset(uint64 SuppressThroughId = 0);

    bool IsInitialized() const { return bInitialized; }
    const FCinderWorldEffectsStats& Stats() const { return Diagnostics; }

private:
    enum class EBatch : uint8
    {
        TeamGlow0,
        TeamGlow1,
        TeamGlow2,
        TeamGlow3,
        WarmGlow,
        WarmShard,
        HealGlow,
        TeamBeam0,
        TeamBeam1,
        TeamBeam2,
        TeamBeam3,
        HealBeam,
        Dust,
        Scorch,
        Count
    };

    UInstancedStaticMeshComponent* CreateBatch(USceneComponent* AttachRoot, UStaticMesh* Mesh,
        UMaterialInterface* Material, FLinearColor Tint, float GlowIntensity, float Opacity);
    bool Add(EBatch Batch, const FTransform& Transform);
    void Submit();

    static constexpr int32 MaxInstances = 160;
    static constexpr int32 MaxBatchInstances = 56;
    /** Wrecks a battlefield remembers at once, oldest replaced first. */
    static constexpr int32 MaxWrecks = 24;
    static constexpr float WreckLifetimeSeconds = 26.0f;

    /** A scorch mark left where something died. Local cosmetic memory only: it is
        never saved, never sent, and never consulted by the simulation. */
    struct FWreck
    {
        cinder::Vec2 Position;
        float Radius = 0;
        float RecordedAt = 0;
        uint64 EffectId = 0;
    };
    TArray<FWreck> Wrecks;
    int32 NextWreck = 0;

    bool bInitialized = false;
    bool bSoftDust = false;
    int32 PendingInstanceCount = 0;
    int32 PendingViewerTeam = INDEX_NONE;
    int32 SubmittedViewerTeam = INDEX_NONE;
    uint64 SuppressedThroughId = 0;
    uint64 LastSimulationEffectId = 0;
    FCinderWorldEffectsStats Diagnostics;
    TArray<TArray<FTransform>> Pending;
    TArray<TArray<FTransform>> Submitted;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> Components;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> Materials;
};
