#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Presentation/CinderEntityMotion.h"
#include "Presentation/CinderTutorial.h"
#include "Presentation/CinderCampaign.h"
#include "Presentation/CinderCampaignSave.h"
#include "Sim/AIDifficulty.h"
#include "Sim/MatchLength.h"
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
class UCinderLandscapeTerrain;
class ACinderCamera;

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
    cinder::Simulation& Sim() { return *Simulation; }
    const cinder::Simulation& Sim() const { return *Simulation; }
    const std::vector<cinder::Entity>& KnownResources() const { return ResourceMemory; }
    void StartMatch(int MapIndex, cinder::AIDifficulty Difficulty = cinder::AIDifficulty::Normal,
        cinder::MatchLength Length = cinder::MatchLength::Standard);
    void StartTutorial();
    bool StartCampaignMission(int32 Mission);
    bool ResumeCampaignCheckpoint();
    void GuidanceCameraInput();
    void RequestCampaignHint();
    FCinderCampaign& Campaign() { return CampaignDirector; }
    const FCinderCampaign& Campaign() const { return CampaignDirector; }
    const FCinderCampaignProgress& CampaignProgress() const { return CampaignRecord; }
    bool HasCampaignCheckpoint() const { return bCampaignCheckpointAvailable; }
    const FString& CampaignSaveMessage() const { return CampaignStorageMessage; }
    bool IsMatchOver() const { return CampaignDirector.IsTerminal() || Simulation->winner() != -1; }
    bool StartOnlineMatch(const cinder::net::Snapshot& Snapshot);
    bool IsOnlineMatch() const { return bOnlineMatch; }
    cinder::CommandResult SubmitCommand(const cinder::Command& Command, uint32* OutOnlineSequence = nullptr);
    cinder::Vec2 RenderPosition(const cinder::Entity& Entity) const;
    /**
     * World Z of the presented ground at an XY, for anything that has to sit on
     * the terrain rather than on the old flat plane — selection rings, world
     * labels, ground markers. Evaluates the same closed-form generator the
     * landscape is baked from, so it agrees with the rendered surface without
     * reading back a render resource.
     */
    float GroundHeight(cinder::Vec2 Point) const;
    /** Ground height after the terrain material's explored-fog vertex flattening.
        Used for pointer rays; entity seating continues to use GroundHeight. */
    float PickingGroundHeight(cinder::Vec2 Point) const;
    /** Terrain/obstacle reference for an entity, before its hull or UI clearance is added.
        Aircraft clear the visible cliff body; ground entities use the actual terrain. */
    float EntityGroundHeight(cinder::Vec2 Point, cinder::Kind Kind) const;
    /** True while the authored Landscape is bound, so the presented ground carries relief. */
    bool HasTerrainRelief() const { return bTerrainRelief; }
    FCinderTutorial& Tutorial() { return Training; }
    const FCinderTutorial& Tutorial() const { return Training; }
    void ReturnToMenu();
    bool IsMenu() const { return bMenu; }
    bool IsPaused() const { return bPaused; }
    void SetPaused(bool Value);
    int MapIndex() const { return CurrentMap; }
    cinder::AIDifficulty MatchDifficulty() const
    {
        return cinder::aiDifficultyFromAggression(Simulation->config().aiAggression);
    }
    cinder::MatchLength MatchLength() const { return Simulation->config().matchLength; }
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
    /** Latest local-player fog snapshot, shared by world fog and HUD consumers. */
    const TArray<uint8>& FogCells() const { return LastFogCells; }
    int32 FogDimension() const { return cinder::Simulation::FogSize; }
    uint64 FogRevision() const { return FogSnapshotRevision; }
    /** Changes at every reset/load, even when entity IDs and simulation tick are reused. */
    uint64 MatchGeneration() const { return MatchGenerationSerial; }

private:
    friend class FCinderWorldLifecycleIntegration;
    friend class FCinderDifficultyIntegration;
    friend class FCinderArmyControlIntegration;
    friend class FCinderTacticalOrderIntegration;
    friend class FCinderPatrolEscortIntegration;
    friend class FCinderCameraBoundaryTest;
    friend class FCinderTerrainPickingTest;
    friend class FCinderCampaignLifecycleIntegration;
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
    /** GroundZ is AddEntity's single per-entity terrain sample, so slab and hull agree. */
    void AddBuildingPad(const cinder::Entity& Entity, float GroundZ);
    bool ValidateModel(UStaticMesh* Mesh, cinder::Kind Kind, FString& Reason) const;
    void AddEntity(const cinder::Entity& Entity);
    /** Terrain, fog, scenery and effects: everything that only changes on a simulation step. */
    void RenderSimState();
    /** Entity poses: re-evaluated every rendered frame so motion is continuous. */
    void RenderPoseState();
    /**
     * bPosePass selects the dynamic entity batches; the complement is the static
     * environment set that RefreshEnvironment owns. The split is exact because
     * FBatch::bDynamic is already true for precisely the batches AddEntity writes.
     */
    void FlushBatches(bool bPosePass);
    void UpdateCompletionAudio();
    bool LoadMatchFrom(const FString& Filename);
    TUniquePtr<cinder::Simulation> Simulation;
    FCinderTutorial Training;
    void LoadCampaignProgress();
    void ObserveCampaign();
    void SettleCampaignBoundary();
    bool CanPersistCampaign() const;
    FCinderCampaign CampaignDirector;
    TUniquePtr<FCinderCampaignSave> CampaignStorage;
    FCinderCampaignProgress CampaignRecord;
    FString CampaignStorageMessage;
    uint32 SavedCampaignBoundary = 0;
    bool bCampaignCheckpointAvailable = false;
    bool bCampaignVictoryRecorded = false;
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
    /**
     * Presentation clock. Poses are evaluated against this free-running local
     * time rather than Simulation::time(), which is quantized to the 0.05 s step
     * and therefore held an animation for one or two display frames in an uneven
     * 3:2 cadence. It accumulates only while a match is actually running, so
     * pausing still freezes every animation. It is never read back by, compared
     * against, or fed into the simulation.
     */
    float PresentationTime = 0;
    /** Monotonic per-pose counter standing in for Simulation::tick() in observations. */
    uint64 PoseSerial = 0;
    /** Set from UCinderLandscapeTerrain::Update; false means the ground is the flat fallback plane. */
    bool bTerrainRelief = false;
    /** Local selection, refreshed once per pose pass. Cosmetic view state only. */
    TSet<cinder::Id> SelectedIds;
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
    int32 WorldBorderBatch = INDEX_NONE;
    int32 PadBatch = INDEX_NONE;
    TArray<int32> RockBatchIndices;
    TArray<uint8> LastFogCells;
    uint64 FogSnapshotRevision = 0;
    uint64 MatchGenerationSerial = 1;
    TArray<uint8> LastObstacleReveal;
    uint32 ObstacleGeometryHash = 0;
    float PresentedWorldSize = 0.0f;
    uint64 FogTextureUploads = 0;
    uint64 FogTextureSubmittedRevision = 0;
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
    UPROPERTY() TObjectPtr<UCinderLandscapeTerrain> CanyonTerrain;
    UPROPERTY(Transient) TObjectPtr<ACinderCamera> CameraRig;
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
