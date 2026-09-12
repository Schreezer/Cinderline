#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Presentation/CinderEntityMotion.h"
#include "Presentation/CinderTutorial.h"
#include "Sim/Simulation.h"
#include "Sim/Network.h"
#include "CinderBattlefield.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UTexture2D;
class UTextureCube;
class UCinderScenery;
class UCinderWorldEffects;

/** Adapter requests are counted even in automation worlds without an audio subsystem. */
struct FCinderCombatFeedbackStats
{
    uint64 ProcessedHighWater = 0;
    uint64 ConsumedEvents = 0;
    uint64 WeaponRequests = 0;
    uint64 ImpactRequests = 0;
    uint64 DeathRequests = 0;
    uint64 HiddenEvents = 0;
    uint64 OffscreenEvents = 0;
    uint64 CoalescedEvents = 0;
};

/** The presentation adapter is the sole owner of the portable authoritative simulation. */
UCLASS()
class CINDERLINE_API ACinderBattlefield : public AActor
{
    GENERATED_BODY()
public:
    ACinderBattlefield();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    cinder::Simulation& Sim() { return Simulation; }
    const cinder::Simulation& Sim() const { return Simulation; }
    const std::vector<cinder::Entity>& KnownResources() const { return ResourceMemory; }
    void StartMatch(int MapIndex);
    void StartTutorial();
    bool StartOnlineMatch(const cinder::net::Snapshot& Snapshot);
    bool IsOnlineMatch() const { return bOnlineMatch; }
    cinder::CommandResult SubmitCommand(const cinder::Command& Command);
    cinder::Vec2 RenderPosition(const cinder::Entity& Entity) const;
    FCinderTutorial& Tutorial() { return Training; }
    const FCinderTutorial& Tutorial() const { return Training; }
    void ReturnToMenu();
    bool IsMenu() const { return bMenu; }
    bool IsPaused() const { return bPaused; }
    void SetPaused(bool Value);
    int MapIndex() const { return CurrentMap; }
    void RenderState();
    bool SaveMatch() const;
    bool LoadMatch();
    void LogModelStatus() const;
    const FCinderCombatFeedbackStats& CombatFeedbackStats() const { return CombatFeedback; }
    /** Clear observed scenery, motion and feedback after a direct Sim().reset/load. */
    void ResetPresentation();
    /** Resynchronize combat feedback without discarding observed scenery. */
    void ResetFeedback(bool bClearCombatCounters = true);
    /** Consume current events once. Tick owns the menu/pause gate; fixtures can call this directly. */
    void UpdateCombatFeedback();
    void LogCombatStatus() const;
    bool HasWorldEffects() const;

private:
    friend class FCinderWorldLifecycleIntegration;
    struct FBatch
    {
        UInstancedStaticMeshComponent* Mesh = nullptr;
        TArray<FTransform> Transforms;
        TArray<FTransform> SubmittedTransforms;
        bool bDynamic = true;
        bool bDirty = true;
    };
    struct FInstanceUploadCounters
    {
        uint64 Passes = 0;
        uint64 DeltaCalls = 0;
        uint64 Transforms = 0;
        uint64 Added = 0;
        uint64 Removed = 0;
        uint64 FullRebuilds = 0;
        uint64 UnchangedSkips = 0;
        uint64 StaticSkips = 0;
    };
    FBatch& AddBatch(UStaticMesh* Mesh, FLinearColor Color, bool bCastShadow = false, bool bDynamic = true);
    void LoadModelBatches();
    void InitializeEnvironment();
    void InvalidateEnvironment();
    void RefreshEnvironment();
    void RefreshTerrainSurface();
    void UpdateFogTexture();
    void AddBuildingPad(const cinder::Entity& Entity);
    bool ValidateModel(UStaticMesh* Mesh, cinder::Kind Kind, FString& Reason) const;
    void AddEntity(const cinder::Entity& Entity);
    void FlushBatches();
    void UpdateCompletionAudio();
    cinder::Simulation Simulation;
    FCinderTutorial Training;
    cinder::Stats AudioStatsSnapshot;
    FCinderCombatFeedbackStats CombatFeedback;
    std::vector<cinder::Entity> ResourceMemory;
    bool bMenu = true;
    bool bPaused = false;
    bool bOnlineMatch = false;
    uint64 OnlineSnapshotSerial = 0, OnlinePoseSerial = 0;
    double OnlineSnapshotAt = 0;
    float OnlineSnapshotInterval = 0.1f;
    TMap<cinder::Id, cinder::Vec2> PreviousOnlinePositions;
    int CurrentMap = 0;
    float RenderTimer = 0;
    TArray<FBatch> Batches;
    FInstanceUploadCounters InstanceUploads;
    TArray<int32> ModelBatchIndices;
    TArray<int32> MotionPartBatchIndices;
    TArray<uint8> MotionKindAvailable;
    TArray<FString> ModelFallbackReasons;
    FCinderEntityMotion EntityMotion;
    int32 ModelBatchStart = 0;
    int32 ModelBatchCount = 0;
    int32 FogPlaneBatch = INDEX_NONE;
    int32 PadBatch = INDEX_NONE;
    TArray<int32> RockBatchIndices;
    TArray<uint8> LastFogCells;
    TArray<uint8> LastObstacleReveal;
    uint32 ObstacleGeometryHash = 0;
    uint64 FogTextureUploads = 0;
    uint32 TerrainSurfaceHash = 0;
    uint64 TerrainSurfaceUploads = 0;
    bool bTerrainSurfaceInvalid = true;
    bool bEnvironmentInvalid = true;
    int32 LastModelEntities = 0;
    int32 LastFallbackEntities = 0;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> MeshComponents;
    UPROPERTY() TArray<TObjectPtr<UStaticMesh>> ModelMeshes;
    UPROPERTY() TArray<TObjectPtr<UStaticMesh>> MotionMeshes;
    UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> ModelMaterials;
    UPROPERTY() TObjectPtr<UCinderScenery> Scenery;
    UPROPERTY() TObjectPtr<UCinderWorldEffects> WorldEffects;
    UPROPERTY() TObjectPtr<UStaticMesh> Cube;
    UPROPERTY() TObjectPtr<UStaticMesh> Cylinder;
    UPROPERTY() TObjectPtr<UStaticMesh> Cone;
    UPROPERTY() TObjectPtr<UStaticMesh> Sphere;
    UPROPERTY() TObjectPtr<UStaticMesh> Plane;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> FogTexture;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FogMaterial;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> GroundSurfaceTexture;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GroundSurfaceMaterial;
    UPROPERTY() TObjectPtr<UTextureCube> AmbientCubemap;
    UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
};
