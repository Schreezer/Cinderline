#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sim/Simulation.h"
#include "CinderScenery.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;

struct FCinderSceneryDiagnostics
{
    bool bInitialized = false;
    uint64 Rebuilds = 0;
    uint64 BatchUploads = 0;
    uint64 TallRockBatchUploads = 0;
    uint64 IndustrialBatchUploads = 0;
    uint64 DebrisBatchUploads = 0;
    uint64 UnchangedBatchSkips = 0;
    int32 BatchCount = 0;
    int32 TotalInstances = 0;
    int32 TallRockInstances = 0;
    int32 RockVariantInstances = 0;
    int32 CliffMassInstances = 0;
    int32 IndustrialInstances = 0;
    int32 RoadInstances = 0;
    int32 DebrisInstances = 0;
    int32 CanyonMeshBatches = 0;
    int32 LegacyCanyonFallbackBatches = 0;
    int32 PrimitiveCanyonFallbackBatches = 0;
    int32 CollisionEnabledBatches = 0;
    int32 MultiRowMesaInstances = 0;
    float MinimumMesaHeightCm = 0.0f;
    float MaximumMesaHeightCm = 0.0f;
    bool bCanyonMaterialLoaded = false;
    bool bCanyonGroundMaterialLoaded = false;
    int32 OutOfBoundsTallInstances = 0;
    int32 FogRejectedResourceInstances = 0;
    int32 FogRejectedRoads = 0;
};

/** Fog-safe, collision-free scenery derived from state the local player already knows. */
UCLASS(ClassGroup=(Cinderline), meta=(BlueprintSpawnableComponent))
class CINDERLINE_API UCinderScenery : public UActorComponent
{
    GENERATED_BODY()

public:
    UCinderScenery();

    /** Create the fixed ISM set after the owner enters the world. Safe to call again with the same parent. */
    void Initialize(USceneComponent* AttachParent, UMaterialInterface* TerrainMaterial = nullptr);

    /** Rebuild only when explored obstacles, friendly complete buildings, or known resources change. */
    void Update(const cinder::Simulation& Simulation, const std::vector<cinder::Entity>& KnownResources);

    /** Clear submitted scenery and cached observed state. */
    void Reset();

    bool IsInitialized() const;
    FCinderSceneryDiagnostics Diagnostics() const;

private:
    enum class EBatch : uint8
    {
        RockA,
        RockB,
        RockC,
        RockD,
        RockE,
        RockF,
        CliffMass,
        Pad,
        Road,
        Pipe,
        Crate,
        Debris,
        Count
    };

    UInstancedStaticMeshComponent* AddBatch(EBatch Batch, UStaticMesh* Mesh, UMaterialInterface* Material,
                                            USceneComponent* AttachParent, bool bCastShadow, int32 EndCullDistance);
    bool AddInstance(EBatch Batch, const FTransform& Transform);
    void SubmitPending();
    uint32 ObservedStateHash(const cinder::Simulation& Simulation,
                             const std::vector<cinder::Entity>& KnownResources) const;

    UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> Batches;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> LoadedMeshes;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> LoadedMaterials;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> AttachedParent;
    TArray<TArray<FTransform>> PendingTransforms;
    TArray<TArray<FTransform>> SubmittedTransforms;
    uint32 LastObservedHash = 0;
    uint64 RebuildCount = 0;
    uint64 BatchUploadCount = 0;
    uint64 TallRockBatchUploadCount = 0;
    uint64 IndustrialBatchUploadCount = 0;
    uint64 DebrisBatchUploadCount = 0;
    uint64 UnchangedBatchSkipCount = 0;
    int32 LastOutOfBoundsTallInstances = 0;
    int32 LastFogRejectedResourceInstances = 0;
    int32 LastFogRejectedRoads = 0;
    int32 CanyonMeshBatchCount = 0;
    int32 LegacyCanyonFallbackBatchCount = 0;
    int32 PrimitiveCanyonFallbackBatchCount = 0;
    int32 LastMultiRowMesaInstances = 0;
    float LastMinimumMesaHeightCm = 0.0f;
    float LastMaximumMesaHeightCm = 0.0f;
    bool bCanyonMaterialLoaded = false;
    bool bCanyonGroundMaterialLoaded = false;
    bool bHasObservedHash = false;
};
