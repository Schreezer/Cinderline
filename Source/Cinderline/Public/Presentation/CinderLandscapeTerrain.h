#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sim/Simulation.h"
#include "CinderLandscapeTerrain.generated.h"

class ALandscape;
class UTexture2D;

namespace CinderLandscapeTerrain
{
constexpr int32 MapCount = 3;
constexpr int32 ComponentCountPerAxis = 2;
constexpr int32 SubsectionsPerComponent = 1;
constexpr int32 SubsectionSizeQuads = 63;
constexpr int32 QuadsPerAxis = ComponentCountPerAxis * SubsectionsPerComponent * SubsectionSizeQuads;
constexpr int32 SamplesPerAxis = QuadsPerAxis + 1;
constexpr uint16 FlatHeight = 32768;
constexpr float BaselineZ = -1.0f;

CINDERLINE_API float VertexSpacing(float WorldSize = cinder::Simulation::WorldSize);
CINDERLINE_API uint64 GeometrySignature(const cinder::Simulation& Simulation);
CINDERLINE_API uint64 CanonicalGeometrySignature(int32 Map);
CINDERLINE_API bool IsCanonicalGeometry(const cinder::Simulation& Simulation);
CINDERLINE_API FName MapTag(int32 Map);
CINDERLINE_API FName GeometryTag(uint64 Signature);
CINDERLINE_API void BuildCanonicalHeightData(int32 Map, TArray<uint16>& OutHeights);
CINDERLINE_API void BuildHeightData(const cinder::Simulation& Simulation, TArray<uint16>& OutHeights);
}

/** Selects a compatible pre-authored Standard Landscape; other sizes use the generated ground plane. */
UCLASS(ClassGroup=(Cinderline), meta=(BlueprintSpawnableComponent))
class CINDERLINE_API UCinderLandscapeTerrain : public UActorComponent
{
    GENERATED_BODY()
public:
    UCinderLandscapeTerrain();
    void Initialize();
    bool Update(const cinder::Simulation& Simulation, UTexture2D* FogMask, UTexture2D* TerrainLayers);

private:
    bool BindMaterialTextures(ALandscape* Landscape, UTexture2D* FogMask, UTexture2D* TerrainLayers,
        float WorldSizeInverse);
    void HideAll();
    UPROPERTY(Transient) TArray<TObjectPtr<ALandscape>> Landscapes;
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> BoundFogMasks;
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> BoundTerrainLayers;
    bool bInitialized = false;
    int32 ActiveMap = INDEX_NONE;
};
