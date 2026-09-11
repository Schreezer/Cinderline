#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/Simulation.h"
#include "CinderBattlefield.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UTexture2D;
class UTextureCube;

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
    void ReturnToMenu();
    bool IsMenu() const { return bMenu; }
    bool IsPaused() const { return bPaused; }
    void SetPaused(bool Value) { bPaused = Value; }
    int MapIndex() const { return CurrentMap; }
    void RenderState();
    bool SaveMatch() const;
    bool LoadMatch();
    void LogModelStatus() const;
    const FCinderCombatFeedbackStats& CombatFeedbackStats() const { return CombatFeedback; }
    /** Call after a direct Sim().reset/load in a development fixture; existing effects are skipped. */
    void ResetFeedback(bool bClearCombatCounters = true);
    /** Consume current events once. Tick owns the menu/pause gate; fixtures can call this directly. */
    void UpdateCombatFeedback();
    void LogCombatStatus() const;

private:
    struct FBatch
    {
        UInstancedStaticMeshComponent* Mesh = nullptr;
        TArray<FTransform> Transforms;
        bool bDynamic = true;
        bool bDirty = true;
    };
    FBatch& AddBatch(UStaticMesh* Mesh, FLinearColor Color, bool bCastShadow = false, bool bDynamic = true);
    void LoadModelBatches();
    void InitializeEnvironment();
    void InvalidateEnvironment();
    void RefreshEnvironment();
    void UpdateFogTexture();
    void AddBuildingPad(const cinder::Entity& Entity);
    bool ValidateModel(UStaticMesh* Mesh, cinder::Kind Kind, FString& Reason) const;
    void AddEntity(const cinder::Entity& Entity);
    void FlushBatches();
    void UpdateCompletionAudio();
    cinder::Simulation Simulation;
    cinder::Stats AudioStatsSnapshot;
    FCinderCombatFeedbackStats CombatFeedback;
    std::vector<cinder::Entity> ResourceMemory;
    bool bMenu = true;
    bool bPaused = false;
    int CurrentMap = 0;
    float RenderTimer = 0;
    TArray<FBatch> Batches;
    TArray<int32> ModelBatchIndices;
    TArray<FString> ModelFallbackReasons;
    int32 ModelBatchStart = 0;
    int32 ModelBatchCount = 0;
    int32 FogPlaneBatch = INDEX_NONE;
    int32 PadBatch = INDEX_NONE;
    TArray<int32> RockBatchIndices;
    TArray<uint8> LastFogCells;
    TArray<uint8> LastObstacleReveal;
    uint32 ObstacleGeometryHash = 0;
    uint64 FogTextureUploads = 0;
    bool bEnvironmentInvalid = true;
    int32 LastModelEntities = 0;
    int32 LastFallbackEntities = 0;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> MeshComponents;
    UPROPERTY() TArray<TObjectPtr<UStaticMesh>> ModelMeshes;
    UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> ModelMaterials;
    UPROPERTY() TObjectPtr<UStaticMesh> Cube;
    UPROPERTY() TObjectPtr<UStaticMesh> Cylinder;
    UPROPERTY() TObjectPtr<UStaticMesh> Cone;
    UPROPERTY() TObjectPtr<UStaticMesh> Sphere;
    UPROPERTY() TObjectPtr<UStaticMesh> Plane;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> FogTexture;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FogMaterial;
    UPROPERTY() TObjectPtr<UTextureCube> AmbientCubemap;
    UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
};
