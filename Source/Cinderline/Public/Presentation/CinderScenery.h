#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sim/Simulation.h"
#include "CinderScenery.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;
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
    uint64 FoliageBatchUploads = 0;
    uint64 UnchangedBatchSkips = 0;
    int32 BatchCount = 0;
    int32 TotalInstances = 0;
    int32 TallRockInstances = 0;
    int32 RockVariantInstances = 0;
    int32 CliffMassInstances = 0;
    int32 IndustrialInstances = 0;
    int32 RoadInstances = 0;
    int32 DebrisInstances = 0;
    int32 AmbientScatterInstances = 0;
    int32 FoliageInstances = 0;
    int32 TalusInstances = 0;
    int32 FogRejectedFoliageInstances = 0;
    bool bFoliageAvailable = false;
    int32 ForeignBuildingSites = 0;
    int32 CanyonMeshBatches = 0;
    int32 LegacyCanyonFallbackBatches = 0;
    int32 PrimitiveCanyonFallbackBatches = 0;
    int32 CollisionEnabledBatches = 0;
    int32 MultiRowMesaInstances = 0;
    int32 AuthoredCliffObstacles = 0;
    int32 AuthoredCliffBodyInstances = 0;
    int32 AuthoredCliffButtressInstances = 0;
    float MinimumAuthoredCliffHeightCm = 0.0f;
    float MaximumAuthoredCliffHeightCm = 0.0f;
    float MinimumMesaHeightCm = 0.0f;
    float MaximumMesaHeightCm = 0.0f;
    bool bCanyonMaterialLoaded = false;
    bool bCanyonVertexFogAvailable = false;
    bool bCanyonGroundMaterialLoaded = false;
    int32 OutOfBoundsTallInstances = 0;
    int32 FogRejectedResourceInstances = 0;
    int32 FogRejectedRoads = 0;
    int32 RetainingWallInstances = 0;
    int32 AuthoredPavingInstances = 0;
    int32 RampInstances = 0;
    int32 BoundarySceneryInstances = 0;
    int32 FogRejectedArchitectureInstances = 0;
};

/** Fog-safe, collision-free scenery derived from state the local player already knows. */
UCLASS(ClassGroup=(Cinderline), meta=(BlueprintSpawnableComponent))
class CINDERLINE_API UCinderScenery : public UActorComponent
{
    GENERATED_BODY()

public:
    UCinderScenery();

    /** Create the fixed ISM set after the owner enters the world. Safe to call again with the same parent. */
    void Initialize(USceneComponent* AttachParent, UMaterialInterface* TerrainMaterial = nullptr,
                    UTexture2D* FogMask = nullptr);

    /** Rebuild only when explored obstacles, visible complete buildings, known resources or explored ground change. */
    void Update(const cinder::Simulation& Simulation, const std::vector<cinder::Entity>& KnownResources,
                bool bTerrainRelief = true);

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
        Foliage,
        Count
    };

    UInstancedStaticMeshComponent* AddBatch(EBatch Batch, UStaticMesh* Mesh, UMaterialInterface* Material,
                                            USceneComponent* AttachParent, bool bCastShadow, int32 EndCullDistance);
    bool AddInstance(EBatch Batch, const FTransform& Transform, bool bSeatAtBoundsBase = false);
    void SubmitPending();
    /** Terrain relief at an XY, or zero while the flat fallback ground is presented. */
    float SurfaceRelief(const cinder::Simulation& Simulation, float X, float Y) const;
    FVector GroundPoint(const cinder::Simulation& Simulation, float X, float Y, float Offset) const;
    uint32 ObservedStateHash(const cinder::Simulation& Simulation,
                             const std::vector<cinder::Entity>& KnownResources) const;

    UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> Batches;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMesh>> LoadedMeshes;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> LoadedMaterials;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> AttachedParent;
    /** Canyon rock instance carrying the palette and, when supplied, the runtime fog mask. */
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CanyonFogMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FoliageMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ArchitectureMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ArchitectureRoadMaterial;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> LegacyPadMesh;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> LegacyRoadMesh;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> ArchitectureMesh;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> LegacyPadMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> LegacyRoadMaterial;
    TArray<TArray<FTransform>> PendingTransforms;
    TArray<TArray<FTransform>> SubmittedTransforms;
    uint32 LastObservedHash = 0;
    uint64 RebuildCount = 0;
    uint64 BatchUploadCount = 0;
    uint64 TallRockBatchUploadCount = 0;
    uint64 IndustrialBatchUploadCount = 0;
    uint64 DebrisBatchUploadCount = 0;
    uint64 FoliageBatchUploadCount = 0;
    uint64 UnchangedBatchSkipCount = 0;
    int32 LastOutOfBoundsTallInstances = 0;
    int32 LastFogRejectedResourceInstances = 0;
    int32 LastFogRejectedRoads = 0;
    int32 LastRetainingWallInstances = 0;
    int32 LastAuthoredPavingInstances = 0;
    int32 LastRampInstances = 0;
    int32 LastBoundarySceneryInstances = 0;
    int32 LastFogRejectedArchitectureInstances = 0;
    int32 CanyonMeshBatchCount = 0;
    int32 LegacyCanyonFallbackBatchCount = 0;
    int32 PrimitiveCanyonFallbackBatchCount = 0;
    int32 LastMultiRowMesaInstances = 0;
    int32 LastAuthoredCliffObstacles = 0;
    int32 LastAuthoredCliffBodyInstances = 0;
    int32 LastAuthoredCliffButtressInstances = 0;
    float LastMinimumAuthoredCliffHeightCm = 0.0f;
    float LastMaximumAuthoredCliffHeightCm = 0.0f;
    // Ambient scatter shares the debris component with obstacle rubble and ore chips.
    // Diagnostics subtract this so the authored-dressing counters keep meaning exactly
    // what the bounds and fog regressions assert about them.
    int32 LastScatterInstances = 0;
    int32 LastTalusInstances = 0;
    int32 LastFogRejectedFoliageInstances = 0;
    int32 LastForeignBuildingSites = 0;
    float LastMinimumMesaHeightCm = 0.0f;
    float LastMaximumMesaHeightCm = 0.0f;
    bool bCanyonMaterialLoaded = false;
    bool bCanyonVertexFogAvailable = false;
    bool bCanyonGroundMaterialLoaded = false;
    bool bHasObservedHash = false;
    bool bAuthoredArchitecture = false;
    /** Mirrors the battlefield's landscape binding; false means the ground is flat. */
    bool bSurfaceRelief = true;
};
