#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/Simulation.h"
#include "CinderBattlefield.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

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

private:
    struct FBatch
    {
        UInstancedStaticMeshComponent* Mesh = nullptr;
        TArray<FTransform> Transforms;
    };
    FBatch& AddBatch(UStaticMesh* Mesh, FLinearColor Color);
    void AddEntity(const cinder::Entity& Entity);
    void FlushBatches();
    cinder::Simulation Simulation;
    std::vector<cinder::Entity> ResourceMemory;
    bool bMenu = true;
    bool bPaused = false;
    int CurrentMap = 0;
    float RenderTimer = 0;
    TArray<FBatch> Batches;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> MeshComponents;
    UPROPERTY() TObjectPtr<UStaticMesh> Cube;
    UPROPERTY() TObjectPtr<UStaticMesh> Cylinder;
    UPROPERTY() TObjectPtr<UStaticMesh> Cone;
    UPROPERTY() TObjectPtr<UStaticMesh> Sphere;
    UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
};
