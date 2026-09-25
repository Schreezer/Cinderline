#include "Presentation/CinderScenery.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Presentation/CinderLandscapeTerrain.h"
#include "Presentation/CinderTerrainSurface.h"
#include "UObject/UObjectGlobals.h"

namespace
{
constexpr int32 BatchCount = 12;
constexpr float FogPlaneZ = 2.5f;

// The tall wall dressing is only used over the flat fallback. Relief caps use their
// own shallow seat so the heightfield remains the main cliff silhouette.
constexpr float MesaSinkCm = 20.0f;

// Ambient scatter lattice. Thirty cells per axis regardless of world size keeps both
// the per-frame hash cost and the on-screen chip density constant across Short,
// Standard and Long instead of tripling the candidate count on the largest map.
constexpr int32 ScatterGridSize = 30;
// The debris component caps at 400. Obstacle rubble (<= 72 observed) and ore chips
// (<= 84 observed) are placed first because they carry gameplay meaning, so scatter
// claims the remainder and never starves them.
constexpr int32 ScatterInstanceBudget = 220;
const TCHAR* AssetNames[BatchCount] = {
    TEXT("SM_CinderScenery_Rock_A"), TEXT("SM_CinderScenery_Rock_B"),
    TEXT("SM_CinderScenery_Rock_C"), TEXT("SM_CinderScenery_Rock_D"),
    TEXT("SM_CinderScenery_Rock_E"), TEXT("SM_CinderScenery_Rock_F"),
    TEXT("SM_CinderScenery_CliffMass"),
    TEXT("SM_CinderScenery_Pad"), TEXT("SM_CinderScenery_Road"),
    TEXT("SM_CinderScenery_Pipe"), TEXT("SM_CinderScenery_Crate"),
    TEXT("SM_CinderScenery_Debris")
};

// These roles deliberately map one-for-one. Falling back per role keeps a
// partially imported canyon kit deterministic instead of shifting silhouettes.
const TCHAR* CanyonAssetNames[BatchCount] = {
    TEXT("SM_CinderCanyon_Rock_A"), TEXT("SM_CinderCanyon_Rock_B"),
    TEXT("SM_CinderCanyon_Rock_C"), TEXT("SM_CinderCanyon_Rock_D"),
    TEXT("SM_CinderCanyon_Rock_E"), TEXT("SM_CinderCanyon_Rock_F"),
    TEXT("SM_CinderCanyon_CliffMass"),
    nullptr, nullptr, nullptr, nullptr,
    TEXT("SM_CinderCanyon_Debris")
};

// A preference *inside* an existing role, never a new role and never a new component.
// Ambient scatter needs one 10-30 cm pebble; the authored debris field is sixteen
// chunks normalized across a 100 cm footprint, so at scatter scale each chunk resolves
// to under two centimetres and the whole instance reads as noise. When the chip is
// missing the role still falls through to the debris field, then the legacy mesh, then
// the primitive, so a partially imported kit degrades instead of leaving the batch
// empty-but-registered or moving debris onto some other component.
const TCHAR* ScatterAssetNames[BatchCount] = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr,
    TEXT("SM_CinderCanyon_Chip_A")
};

uint32 HashFloat(uint32 Hash, float Value)
{
    return HashCombineFast(Hash, GetTypeHash(Value));
}

uint32 HashEntity(uint32 Hash, const cinder::Entity& Entity)
{
    Hash = HashCombineFast(Hash, GetTypeHash(Entity.id));
    Hash = HashCombineFast(Hash, GetTypeHash(static_cast<int32>(Entity.kind)));
    Hash = HashFloat(Hash, Entity.pos.x);
    return HashFloat(Hash, Entity.pos.y);
}

// Own buildings are known by definition; anyone else's must be under live vision this
// frame. The old team == 0 test is why enemy and neutral ground carried no pads, crates
// or pipes at all; routing the relaxation through visible() is what keeps a building the
// local player has never seen from leaking its position through its dressing.
bool IsDressedBuilding(const cinder::Simulation& Simulation, const cinder::Entity& Entity)
{
    if (!Entity.alive() || Entity.progress < 1.0f || !cinder::definition(Entity.kind).building) return false;
    return Entity.team == 0 || Simulation.visible(0, Entity.pos);
}

FVector Point(float X, float Y, float Z = 0.0f)
{
    return FVector(X, Y, Z);
}

bool AreaVisible(const cinder::Simulation& Simulation, cinder::Vec2 Position, float Radius)
{
    const float WorldSize = Simulation.worldSize();
    const float Cell = WorldSize / cinder::Simulation::FogSize;
    if (Position.x - Radius < 0 || Position.y - Radius < 0
        || Position.x + Radius >= WorldSize
        || Position.y + Radius >= WorldSize) return false;
    const int32 MinX = FMath::FloorToInt((Position.x - Radius) / Cell);
    const int32 MaxX = FMath::FloorToInt((Position.x + Radius) / Cell);
    const int32 MinY = FMath::FloorToInt((Position.y - Radius) / Cell);
    const int32 MaxY = FMath::FloorToInt((Position.y + Radius) / Cell);
    for (int32 Y = MinY; Y <= MaxY; ++Y)
        for (int32 X = MinX; X <= MaxX; ++X)
            if (!Simulation.visible(0, {(X + 0.5f) * Cell, (Y + 0.5f) * Cell})) return false;
    return true;
}

bool RoadVisible(const cinder::Simulation& Simulation, cinder::Vec2 From, cinder::Vec2 To)
{
    const float Cell = Simulation.worldSize() / cinder::Simulation::FogSize;
    const float DX = To.x - From.x, DY = To.y - From.y;
    const float Length = FMath::Sqrt(DX * DX + DY * DY);
    const int32 Samples = FMath::Max(1, FMath::CeilToInt(Length / (Cell * 0.45f)));
    for (int32 Sample = 0; Sample <= Samples; ++Sample)
    {
        const float Alpha = static_cast<float>(Sample) / Samples;
        if (!AreaVisible(Simulation, {From.x + DX * Alpha, From.y + DY * Alpha}, 39.0f)) return false;
    }
    return true;
}

struct FScatterCandidate
{
    float X = 0.0f;
    float Y = 0.0f;
    float Yaw = 0.0f;
    float SizeCm = 0.0f;
    float Ratio = 0.0f;
    float HeightRatio = 0.0f;
    float Roll = 0.0f;
};

// One enumeration shared by the rebuild gate and the build. The lattice is a pure
// function of map and world size, so ObservedStateHash can hash the explored bit under
// every candidate without paying for the terrain mask, and the two passes can never
// disagree about which candidate points exist. Visitor returns false to stop early;
// only the build ever does, because stopping cannot change a hash it never affects.
template <typename VisitorType>
void ForEachScatterCandidate(const cinder::Simulation& Simulation, VisitorType&& Visitor)
{
    const float WorldSize = Simulation.worldSize();
    const float Cell = WorldSize / ScatterGridSize;
    FRandomStream Random(0x2c1d + Simulation.config().map * 101 + static_cast<int32>(WorldSize));
    for (int32 GridY = 0; GridY < ScatterGridSize; ++GridY)
    for (int32 GridX = 0; GridX < ScatterGridSize; ++GridX)
    {
        FScatterCandidate Candidate;
        // Jitter stays inside 0.42 of a cell so no two candidates can collapse onto each
        // other and leave a visible hole in the lattice they were meant to break up.
        Candidate.X = (GridX + 0.5f + Random.FRandRange(-0.42f, 0.42f)) * Cell;
        Candidate.Y = (GridY + 0.5f + Random.FRandRange(-0.42f, 0.42f)) * Cell;
        Candidate.Yaw = Random.FRandRange(0.0f, 360.0f);
        // Ten centimetres is the floor: the 80% MetalFX resolve plus FXAA smears anything
        // thinner than roughly three resolved pixels into the ground texture, so a smaller
        // chip would cost an instance and return nothing.
        Candidate.SizeCm = Random.FRandRange(10.0f, 30.0f);
        Candidate.Ratio = Random.FRandRange(0.55f, 0.95f);
        Candidate.HeightRatio = Random.FRandRange(0.34f, 0.62f);
        Candidate.Roll = Random.FRand();
        if (!Visitor(Candidate)) return;
    }
}
}

UCinderScenery::UCinderScenery()
{
    PrimaryComponentTick.bCanEverTick = false;
}

UInstancedStaticMeshComponent* UCinderScenery::AddBatch(EBatch Batch, UStaticMesh* Mesh,
    UMaterialInterface* Material, USceneComponent* AttachParent, bool bCastShadow, int32 EndCullDistance)
{
    if (!Mesh || !AttachParent || !GetOwner()) return nullptr;
    const int32 Index = static_cast<int32>(Batch);
    const FName Name(*FString::Printf(TEXT("CinderScenery_%s"), AssetNames[Index]));
    auto* Component = NewObject<UInstancedStaticMeshComponent>(GetOwner(), Name);
    Component->SetupAttachment(AttachParent);
    Component->SetStaticMesh(Mesh);
    Component->SetMobility(EComponentMobility::Movable);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCanEverAffectNavigation(false);
    Component->SetCastShadow(bCastShadow);
    Component->SetCastContactShadow(false);
    Component->SetCullDistances(FMath::Max(0, EndCullDistance - 1400), EndCullDistance);
    Component->SetVisibleInRayTracing(false);
    Component->SetAffectDistanceFieldLighting(false);
    Component->SetAffectDynamicIndirectLighting(false);
    if (Material) Component->SetMaterial(0, Material);
    Component->RegisterComponent();
    Batches[Index] = Component;
    return Component;
}

void UCinderScenery::Initialize(USceneComponent* AttachParent, UMaterialInterface* TerrainMaterial,
                                UTexture2D* FogMask)
{
    static_assert(BatchCount == static_cast<int32>(EBatch::Count));
    if (!AttachParent || (AttachedParent == AttachParent && IsInitialized())) return;
    Reset();
    for (UInstancedStaticMeshComponent* Batch : Batches)
        if (Batch) Batch->DestroyComponent();
    Batches.Init(nullptr, BatchCount);
    PendingTransforms.SetNum(BatchCount);
    SubmittedTransforms.SetNum(BatchCount);
    LoadedMeshes.Reset();
    LoadedMaterials.Reset();
    AttachedParent = AttachParent;
    CanyonMeshBatchCount = 0;
    LegacyCanyonFallbackBatchCount = 0;
    PrimitiveCanyonFallbackBatchCount = 0;
    bCanyonMaterialLoaded = false;
    bCanyonGroundMaterialLoaded = false;

    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
    UMaterialInterface* RockMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Art/VisualTarget/Materials/MI_CinderSceneryRock.MI_CinderSceneryRock"), nullptr, LOAD_NoWarn);
    UMaterialInterface* PhotoRockMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Art/VisualTarget/Scenery/Materials/M_CinderSceneryPhotogrammetry.M_CinderSceneryPhotogrammetry"),
        nullptr, LOAD_NoWarn);
    UMaterialInterface* CanyonMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Art/Canyon/Materials/M_CinderCanyonRock.M_CinderCanyonRock"), nullptr, LOAD_NoWarn);
    bCanyonMaterialLoaded = CanyonMaterial != nullptr;
    // The flat gameplay fog plane cannot darken geometry taller than itself, so
    // the cliffs sample the same mask the ground does and fog themselves. Without
    // a mask the material keeps its fully-explored default and renders as before.
    if (CanyonMaterial)
    {
        if (auto* Fogged = UMaterialInstanceDynamic::Create(CanyonMaterial, this))
        {
            if (FogMask) Fogged->SetTextureParameterValue(TEXT("FogMask"), FogMask);
            // Keep rock faces readable beside the dusty ground. These are existing
            // diffuse parameters; visibility and fog emission retain their authored gates.
            Fogged->SetVectorParameterValue(TEXT("CanyonShadow"), FLinearColor(0.24f, 0.20f, 0.16f));
            Fogged->SetVectorParameterValue(TEXT("CanyonSandstone"), FLinearColor(0.43f, 0.34f, 0.25f));
            Fogged->SetVectorParameterValue(TEXT("CanyonCap"), FLinearColor(0.28f, 0.25f, 0.20f));
            Fogged->SetVectorParameterValue(TEXT("IronOxide"), FLinearColor(0.29f, 0.20f, 0.14f));
            Fogged->SetScalarParameterValue(TEXT("NormalStrength"), 0.28f);
            CanyonFogMaterial = Fogged;
            CanyonMaterial = Fogged;
        }
    }
    UMaterialInterface* CanyonGroundMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Art/Canyon/Materials/M_CinderCanyonGround.M_CinderCanyonGround"), nullptr, LOAD_NoWarn);
    bCanyonGroundMaterialLoaded = CanyonGroundMaterial != nullptr;
    UMaterialInterface* MetalMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Art/VisualTarget/Materials/MI_CinderSceneryMetal.MI_CinderSceneryMetal"), nullptr, LOAD_NoWarn);
    UMaterialInterface* PaintMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Art/VisualTarget/Materials/MI_CinderSceneryPaint.MI_CinderSceneryPaint"), nullptr, LOAD_NoWarn);
    UMaterialInterface* RoadMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Art/VisualTarget/Materials/M_VT_DirtRoad.M_VT_DirtRoad"), nullptr, LOAD_NoWarn);
    if (!RockMaterial) RockMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Art/Materials/M_CinderBasaltV2.M_CinderBasaltV2"), nullptr, LOAD_NoWarn);
    if (!MetalMaterial) MetalMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Art/Materials/MI_CinderMetal.MI_CinderMetal"), nullptr, LOAD_NoWarn);
    if (!PaintMaterial) PaintMaterial = MetalMaterial;
    if (!RoadMaterial) RoadMaterial = PaintMaterial;
    if (!PhotoRockMaterial) PhotoRockMaterial = RockMaterial;
    if (RockMaterial) LoadedMaterials.Add(RockMaterial);
    if (PhotoRockMaterial && PhotoRockMaterial != RockMaterial) LoadedMaterials.Add(PhotoRockMaterial);
    if (CanyonMaterial) LoadedMaterials.Add(CanyonMaterial);
    if (CanyonGroundMaterial) LoadedMaterials.Add(CanyonGroundMaterial);
    if (MetalMaterial) LoadedMaterials.Add(MetalMaterial);
    if (PaintMaterial && PaintMaterial != MetalMaterial) LoadedMaterials.Add(PaintMaterial);
    if (RoadMaterial && RoadMaterial != PaintMaterial) LoadedMaterials.Add(RoadMaterial);
    if (TerrainMaterial) LoadedMaterials.Add(TerrainMaterial);

    for (int32 Index = 0; Index < BatchCount; ++Index)
    {
        const EBatch Batch = static_cast<EBatch>(Index);
        const bool bCanyonRole = CanyonAssetNames[Index] != nullptr;
        const bool bRock = Index <= static_cast<int32>(EBatch::CliffMass) || Batch == EBatch::Debris;
        UStaticMesh* Mesh = nullptr;
        bool bUsingCanyonMesh = false;
        if (bCanyonRole)
        {
            if (ScatterAssetNames[Index])
            {
                const FString ScatterPath = FString::Printf(TEXT("/Game/Art/Canyon/Meshes/%s.%s"),
                    ScatterAssetNames[Index], ScatterAssetNames[Index]);
                Mesh = LoadObject<UStaticMesh>(nullptr, *ScatterPath, nullptr, LOAD_NoWarn);
            }
            if (!Mesh)
            {
                const FString CanyonPath = FString::Printf(TEXT("/Game/Art/Canyon/Meshes/%s.%s"),
                    CanyonAssetNames[Index], CanyonAssetNames[Index]);
                Mesh = LoadObject<UStaticMesh>(nullptr, *CanyonPath, nullptr, LOAD_NoWarn);
            }
            if (Mesh)
            {
                bUsingCanyonMesh = true;
                ++CanyonMeshBatchCount;
            }
        }
        if (!Mesh)
        {
            const FString LegacyPath = FString::Printf(TEXT("/Game/Art/VisualTarget/Scenery/%s.%s"),
                AssetNames[Index], AssetNames[Index]);
            Mesh = LoadObject<UStaticMesh>(nullptr, *LegacyPath, nullptr, LOAD_NoWarn);
            if (bCanyonRole)
            {
                if (Mesh) ++LegacyCanyonFallbackBatchCount;
                else ++PrimitiveCanyonFallbackBatchCount;
            }
        }
        if (Mesh) LoadedMeshes.Add(Mesh);
        if (!Mesh)
            Mesh = Batch == EBatch::CliffMass ? Cube : (bRock ? Cone : (Batch == EBatch::Pipe ? Cylinder : Cube));
        UMaterialInterface* BatchMaterial = bRock
            ? (bUsingCanyonMesh && CanyonMaterial ? CanyonMaterial : PhotoRockMaterial)
            : (Batch == EBatch::Road ? RoadMaterial : (Batch == EBatch::Pad ? PaintMaterial : MetalMaterial));
        // Preserve the old ground-blended primitive/legacy skirt while the
        // authored canyon material or mesh is unavailable.
        if (Batch == EBatch::CliffMass && !bUsingCanyonMesh && TerrainMaterial) BatchMaterial = TerrainMaterial;
        AddBatch(Batch, Mesh, BatchMaterial, AttachParent,
            bRock && Batch != EBatch::Debris,
            Batch == EBatch::Debris ? 3600 : (bRock ? 5200 : 3900));
    }
}

bool UCinderScenery::AddInstance(EBatch Batch, const FTransform& Transform)
{
    const int32 Index = static_cast<int32>(Batch);
    if (!Batches.IsValidIndex(Index) || !Batches[Index] || !Batches[Index]->GetStaticMesh()) return false;
    // Batches are the scarce resource on this device, not instances: mesh draw commands
    // already run 500-1000 against roughly 400 affordable, while about 400 of 5,000
    // affordable instances were in use. These caps spend the slack inside the same
    // twelve components - roughly 1,230 instances at full saturation, a quarter of the
    // affordable pool - without adding a single batch.
    int32 InstanceCap = 96;
    switch (Batch)
    {
    case EBatch::CliffMass: InstanceCap = 96; break;
    case EBatch::Pad:
    case EBatch::Crate: InstanceCap = 48; break;
    case EBatch::Road:
    case EBatch::Pipe: InstanceCap = 32; break;
    case EBatch::Debris: InstanceCap = 400; break;
    default: break;
    }
    if (!PendingTransforms.IsValidIndex(Index) || PendingTransforms[Index].Num() >= InstanceCap) return false;
    // Call sites specify final dimensions against Unreal's 100 cm basic-shape convention.
    // Normalization keeps obstacle containment exact across authored and fallback meshes.
    const FVector BoundsSize = Batches[Index]->GetStaticMesh()->GetBoundingBox().GetSize();
    if (BoundsSize.X <= UE_SMALL_NUMBER || BoundsSize.Y <= UE_SMALL_NUMBER || BoundsSize.Z <= UE_SMALL_NUMBER) return false;
    FTransform Adjusted = Transform;
    FVector Scale = Adjusted.GetScale3D();
    Scale.X *= 100.0 / BoundsSize.X;
    Scale.Y *= 100.0 / BoundsSize.Y;
    Scale.Z *= 100.0 / BoundsSize.Z;
    Adjusted.SetScale3D(Scale);
    PendingTransforms[Index].Add(Adjusted);
    return true;
}

void UCinderScenery::SubmitPending()
{
    for (int32 BatchIndex = 0; BatchIndex < Batches.Num() && PendingTransforms.IsValidIndex(BatchIndex)
        && SubmittedTransforms.IsValidIndex(BatchIndex); ++BatchIndex)
    {
        UInstancedStaticMeshComponent* Component = Batches[BatchIndex];
        if (!Component) continue;
        const TArray<FTransform>& Pending = PendingTransforms[BatchIndex];
        TArray<FTransform>& Submitted = SubmittedTransforms[BatchIndex];
        const int32 OldCount = Component->GetInstanceCount();
        const int32 NewCount = Pending.Num();
        bool bUnchanged = Submitted.Num() == OldCount && OldCount == NewCount;
        for (int32 Index = 0; bUnchanged && Index < NewCount; ++Index)
            bUnchanged = Pending[Index].Equals(Submitted[Index], 0.0);
        if (bUnchanged)
        {
            ++UnchangedBatchSkipCount;
            continue;
        }

        bool bAccepted = true;
        if (OldCount > NewCount)
        {
            TArray<int32> Tail;
            Tail.Reserve(OldCount - NewCount);
            for (int32 Index = OldCount - 1; Index >= NewCount; --Index) Tail.Add(Index);
            bAccepted = Component->RemoveInstances(Tail, true);
        }
        if (bAccepted && OldCount < NewCount)
        {
            TArray<FTransform> Tail;
            Tail.Append(Pending.GetData() + OldCount, NewCount - OldCount);
            Component->AddInstances(Tail, false, false, false);
            bAccepted = Component->GetInstanceCount() == NewCount;
        }
        const int32 Common = FMath::Min(OldCount, NewCount);
        for (int32 Index = 0; bAccepted && Index < Common;)
        {
            const bool bSame = Submitted.Num() == OldCount && Pending[Index].Equals(Submitted[Index], 0.0);
            if (bSame) { ++Index; continue; }
            const int32 Start = Index++;
            while (Index < Common
                && !(Submitted.Num() == OldCount && Pending[Index].Equals(Submitted[Index], 0.0))) ++Index;
            bAccepted = Component->BatchUpdateInstancesTransforms(Start,
                TArrayView<const FTransform>(Pending.GetData() + Start, Index - Start), false, false, true);
        }
        if (!bAccepted || Component->GetInstanceCount() != NewCount)
        {
            Component->ClearInstances();
            if (NewCount > 0) Component->AddInstances(Pending, false, false, false);
        }
        if (Component->GetInstanceCount() == NewCount) Submitted = Pending;
        else Submitted.Reset();

        ++BatchUploadCount;
        if (BatchIndex <= static_cast<int32>(EBatch::CliffMass)) ++TallRockBatchUploadCount;
        else if (BatchIndex == static_cast<int32>(EBatch::Debris)) ++DebrisBatchUploadCount;
        else ++IndustrialBatchUploadCount;
    }
}

uint32 UCinderScenery::ObservedStateHash(const cinder::Simulation& Simulation,
    const std::vector<cinder::Entity>& KnownResources) const
{
    uint32 Hash = HashFloat(GetTypeHash(Simulation.config().map), Simulation.worldSize());
    Hash = HashCombineFast(Hash, bSurfaceRelief ? 1u : 0u);
    for (const cinder::Obstacle& Obstacle : Simulation.obstacles())
    {
        Hash = HashFloat(Hash, Obstacle.center.x);
        Hash = HashFloat(Hash, Obstacle.center.y);
        Hash = HashFloat(Hash, Obstacle.half.x);
        Hash = HashFloat(Hash, Obstacle.half.y);
        Hash = HashCombineFast(Hash, Simulation.explored(0, Obstacle.center) ? 1u : 0u);
    }
    for (const cinder::Entity& Entity : Simulation.entities())
        if (IsDressedBuilding(Simulation, Entity)) Hash = HashEntity(Hash, Entity);
    for (const cinder::Entity& Resource : KnownResources)
    {
        Hash = HashEntity(Hash, Resource);
        FRandomStream Random(static_cast<int32>(Resource.id * 7919u));
        for (int32 Piece = 0; Piece < 3; ++Piece)
        {
            const float Angle = Random.FRandRange(0.0f, 2.0f * PI);
            const float Distance = Random.FRandRange(70.0f, 118.0f);
            const float Size = Random.FRandRange(0.14f, 0.28f);
            const cinder::Vec2 Position{Resource.pos.x + FMath::Cos(Angle) * Distance,
                Resource.pos.y + FMath::Sin(Angle) * Distance};
            Hash = HashCombineFast(Hash, AreaVisible(Simulation, Position, Size * 55.0f) ? 1u : 0u);
        }
    }
    // Every ambient chip turns on exactly one input that is not already hashed above:
    // whether the local player has explored the point under it. Density comes from the
    // terrain mask, which is a pure function of the map, the world size, the explored
    // cliffs and the remembered ore - all hashed above - so hashing the explored bit per
    // candidate closes the rebuild gate exactly, with no mask evaluation per frame.
    ForEachScatterCandidate(Simulation, [&Hash, &Simulation](const FScatterCandidate& Candidate)
    {
        Hash = HashCombineFast(Hash,
            Simulation.explored(0, {Candidate.X, Candidate.Y}) ? 1u : 0u);
        return true;
    });
    if (!bCanyonGroundMaterialLoaded)
    {
        TArray<const cinder::Entity*> Buildings;
        for (const cinder::Entity& Entity : Simulation.entities())
            if (IsDressedBuilding(Simulation, Entity)) Buildings.Add(&Entity);
        Buildings.Sort([](const cinder::Entity& A, const cinder::Entity& B) { return A.id < B.id; });
        for (int32 Index = 1; Index < Buildings.Num(); ++Index)
        {
            const cinder::Entity& A = *Buildings[Index - 1];
            const cinder::Entity& B = *Buildings[Index];
            const float DX = B.pos.x - A.pos.x, DY = B.pos.y - A.pos.y;
            const float Length = FMath::Sqrt(DX * DX + DY * DY);
            if (Length >= 180.0f && Length <= 1050.0f)
                Hash = HashCombineFast(Hash, RoadVisible(Simulation, A.pos, B.pos) ? 1u : 0u);
        }
    }
    return Hash;
}

// Presentation may raise Z freely; it may never displace the authoritative XY root.
// Every ground-standing prop reads the heightfield so an obstacle plateau lifts its own
// mesas with it instead of swallowing them whole.
//
// A member rather than a free helper purely so it can consult bSurfaceRelief: only a
// compatible world size renders the authored Landscape, and on every other match length
// the ground really is a flat plane that props must sit on rather than hover over.
float UCinderScenery::SurfaceRelief(const cinder::Simulation& Simulation, float X, float Y) const
{
    return bSurfaceRelief ? CinderLandscapeTerrain::HeightAt(Simulation, X, Y) : 0.0f;
}

FVector UCinderScenery::GroundPoint(const cinder::Simulation& Simulation, float X, float Y, float Offset) const
{
    return FVector(X, Y, SurfaceRelief(Simulation, X, Y) + Offset);
}

void UCinderScenery::Update(const cinder::Simulation& Simulation,
    const std::vector<cinder::Entity>& KnownResources, bool bTerrainRelief)
{
    // Only a compatible world size renders the authored Landscape; every other
    // match length draws a flat plane, and seating props against relief nothing
    // renders would float them over ground the player sees as level.
    bSurfaceRelief = bTerrainRelief;
    if (Batches.Num() != BatchCount || !AttachedParent) return;
    const uint32 StateHash = ObservedStateHash(Simulation, KnownResources);
    if (bHasObservedHash && StateHash == LastObservedHash) return;
    const float CullScale = Simulation.worldSize() / cinder::Simulation::WorldSize;
    for (int32 Index = 0; Index < Batches.Num(); ++Index)
    {
        UInstancedStaticMeshComponent* Batch = Batches[Index];
        if (!Batch) continue;
        const EBatch Role = static_cast<EBatch>(Index);
        const bool bRock = Role <= EBatch::CliffMass || Role == EBatch::Debris;
        // Occlusion feedback is disabled project-wide, so cull distance is the only thing
        // bounding the per-frame cost of the scatter population. A 10-30 cm chip is under
        // a resolved pixel well before 3,600 cm, and the clamp keeps even a Long map's
        // 1.25x scale inside the 6,000 cm ceiling the mobile budget allows.
        const int32 EndCull = Role == EBatch::Debris
            ? FMath::Min(4600, FMath::RoundToInt(3600.0f * CullScale))
            : FMath::Min(6000, FMath::RoundToInt((bRock ? 5200.0f : 3900.0f) * CullScale));
        Batch->SetCullDistances(FMath::Max(0, EndCull - FMath::RoundToInt(1400.0f * CullScale)), EndCull);
    }
    // World size is per match, and the mask is addressed in normalized world UV.
    if (CanyonFogMaterial)
        CanyonFogMaterial->SetScalarParameterValue(TEXT("CinderWorldSizeInverse"),
            1.0f / FMath::Max(1.0f, Simulation.worldSize()));
    for (TArray<FTransform>& Pending : PendingTransforms) Pending.Reset();
    LastOutOfBoundsTallInstances = 0;
    LastFogRejectedResourceInstances = 0;
    LastFogRejectedRoads = 0;
    LastMultiRowMesaInstances = 0;
    LastScatterInstances = 0;
    LastForeignBuildingSites = 0;
    LastMinimumMesaHeightCm = 0.0f;
    LastMaximumMesaHeightCm = 0.0f;
    ++RebuildCount;

    int32 ObstacleIndex = 0;
    for (const cinder::Obstacle& Obstacle : Simulation.obstacles())
    {
        const int32 Seed = ObstacleIndex++;
        if (!Simulation.explored(0, Obstacle.center)) continue;
        FRandomStream Random(0x2c1d + Simulation.config().map * 101 + Seed * 977);
        const bool bLongX = Obstacle.half.x >= Obstacle.half.y;
        const float LongSize = 2.0f * (bLongX ? Obstacle.half.x : Obstacle.half.y);
        const int32 Segments = FMath::Clamp(FMath::CeilToInt(LongSize / 300.0f), 2, 7);
        const float SegmentSize = LongSize / Segments;
        if (bSurfaceRelief)
        {
            // The landscape already supplies the continuous 280-480 cm cliff body.
            // A few broad, shallow outcrops break its crest without stacking another
            // wall on top or hiding the plateau between repeated columns.
            const float CrossHalf = bLongX ? Obstacle.half.y : Obstacle.half.x;
            const float Inset = FMath::Min(CrossHalf * 0.60f,
                CinderLandscapeTerrain::VertexSpacing(Simulation.worldSize()) * 2.9f);
            const float CoreHalfAcross = FMath::Max(12.0f, CrossHalf - Inset);
            const float CoreLongSize = FMath::Max(24.0f, LongSize - Inset * 2.0f);
            const int32 Groups = FMath::Clamp(FMath::CeilToInt(LongSize / 530.0f), 1, 5);
            const float GroupSpan = CoreLongSize / Groups;
            for (int32 Group = 0; Group < Groups; ++Group)
            {
                const int32 Rows = CrossHalf >= 300.0f && (Group + Seed) % 2 == 0 ? 2 : 1;
                for (int32 Row = 0; Row < Rows; ++Row)
                {
                    const float Along = (Group + 0.5f) * GroupSpan - CoreLongSize * 0.5f
                        + Random.FRandRange(-0.08f, 0.08f) * GroupSpan;
                    const float Cross = (Rows > 1 ? (Row == 0 ? -0.36f : 0.36f) : 0.0f)
                        * CoreHalfAcross + Random.FRandRange(-0.08f, 0.08f) * CoreHalfAcross;
                    const float YawOffset = Random.FRandRange(-17.0f, 17.0f);
                    const float SinYaw = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(YawOffset)));
                    const float CosYaw = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(YawOffset)));
                    float Width = FMath::Min(330.0f, GroupSpan * Random.FRandRange(0.64f, 0.88f));
                    float Depth = FMath::Min(Width * Random.FRandRange(0.52f, 0.78f),
                        CoreHalfAcross * (Rows > 1 ? 0.72f : 1.35f));
                    const float Fit = FMath::Min(1.0f, FMath::Min(
                        (CoreLongSize * 0.5f - FMath::Abs(Along))
                            / FMath::Max(1.0f, (Width * CosYaw + Depth * SinYaw) * 0.5f),
                        (CoreHalfAcross - FMath::Abs(Cross))
                            / FMath::Max(1.0f, (Depth * CosYaw + Width * SinYaw) * 0.5f)));
                    Width *= Fit;
                    Depth *= Fit;
                    const float Height = Random.FRandRange(50.0f, Row == 0 ? 120.0f : 82.0f);
                    const float X = Obstacle.center.x + (bLongX ? Along : Cross);
                    const float Y = Obstacle.center.y + (bLongX ? Cross : Along);
                    float HalfX = 0.0f, HalfY = 0.0f, Seat = 0.0f;
                    for (int32 Attempt = 0; Attempt < 4; ++Attempt)
                    {
                        HalfX = ((bLongX ? Width : Depth) * CosYaw
                            + (bLongX ? Depth : Width) * SinYaw) * 0.5f;
                        HalfY = ((bLongX ? Depth : Width) * CosYaw
                            + (bLongX ? Width : Depth) * SinYaw) * 0.5f;
                        float Low = SurfaceRelief(Simulation, X, Y);
                        float High = Low;
                        // Seat into the lowest sampled support. Shrinking on steep
                        // shoulders keeps a short cap from hovering over the cliff face.
                        for (int32 CornerX : {-1, 1})
                        for (int32 CornerY : {-1, 1})
                        {
                            const float Support = SurfaceRelief(Simulation,
                                X + CornerX * HalfX, Y + CornerY * HalfY);
                            Low = FMath::Min(Low, Support);
                            High = FMath::Max(High, Support);
                        }
                        Seat = Low - 12.0f;
                        if (High - Low <= Height * 0.55f || Attempt == 3) break;
                        Width *= 0.78f;
                        Depth *= 0.78f;
                    }
                    if (FMath::Abs(X - Obstacle.center.x) + HalfX > Obstacle.half.x + 0.05f
                        || FMath::Abs(Y - Obstacle.center.y) + HalfY > Obstacle.half.y + 0.05f)
                        ++LastOutOfBoundsTallInstances;
                    const EBatch Variant = static_cast<EBatch>((Seed * 3 + Group * 5 + Row * 2) % 6);
                    if (AddInstance(Variant, FTransform(
                        FRotator(0, (bLongX ? 0.0f : 90.0f) + YawOffset, 0), FVector(X, Y, Seat),
                        FVector(Width / 100.0f, Depth / 100.0f, Height / 100.0f))))
                    {
                        LastMinimumMesaHeightCm = LastMinimumMesaHeightCm <= 0.0f
                            ? Height : FMath::Min(LastMinimumMesaHeightCm, Height);
                        LastMaximumMesaHeightCm = FMath::Max(LastMaximumMesaHeightCm, Height);
                        if (Rows > 1) ++LastMultiRowMesaInstances;
                    }
                }
            }
        }
        else for (int32 Segment = 0; Segment < Segments; ++Segment)
        {
            const float Along = (Segment + 0.5f) / Segments * LongSize - LongSize * 0.5f;
            const float CrossHalf = bLongX ? Obstacle.half.y : Obstacle.half.x;
            // Normalized 0 at the middle segment, exactly 1 at the two segments that end
            // the rectangle. Segments is clamped to at least 2, so the divisor is never
            // zero, and reaching exactly 1 is what lets the edge profile be a fixed number
            // rather than something that drifts with the segment count.
            const float EdgeBias = Segments > 1
                ? FMath::Abs(Segment * 2.0f - static_cast<float>(Segments - 1))
                    / static_cast<float>(Segments - 1)
                : 1.0f;
            // A low, nearly continuous stratified shelf closes the blocked rectangle
            // behind the taller irregular silhouettes. The shelf used to top out at 38 cm,
            // which vanished entirely once props started sinking 20 cm into the plateau;
            // 60-110 cm leaves a readable 40-90 cm band and thickens toward the ends so
            // the chokepoint face has a base as well as a crest.
            const FVector MassPosition = GroundPoint(Simulation,
                Obstacle.center.x + (bLongX ? Along : 0.0f),
                Obstacle.center.y + (bLongX ? 0.0f : Along), -MesaSinkCm);
            const float MassAlong = FMath::Max(20.0f, SegmentSize - 6.0f);
            const float MassAcross = FMath::Max(20.0f, CrossHalf * 2.0f - 8.0f);
            const float MassHeight = FMath::Clamp(48.0f + CrossHalf * 0.05f, 60.0f, 110.0f)
                * FMath::Lerp(1.0f, 1.28f, EdgeBias);
            const FVector MassScale(bLongX ? MassAlong / 100.0f : MassAcross / 100.0f,
                bLongX ? MassAcross / 100.0f : MassAlong / 100.0f,
                MassHeight / 100.0f);
            if (FMath::Abs(MassPosition.X - Obstacle.center.x) + MassScale.X * 50.0f > Obstacle.half.x + 0.05f
                || FMath::Abs(MassPosition.Y - Obstacle.center.y) + MassScale.Y * 50.0f > Obstacle.half.y + 0.05f)
                ++LastOutOfBoundsTallInstances;
            AddInstance(EBatch::CliffMass, FTransform(FQuat::Identity, MassPosition, MassScale));

            // Broad obstacles use two authored rows instead of stretching one
            // boulder across the full blocked width. A shared central rise keeps
            // neighboring modules reading as one stratified mesa.
            const int32 Rows = CrossHalf >= 300.0f ? 2 : 1;
            const float RowSpan = (CrossHalf * 2.0f - 12.0f) / Rows;
            // The old profile tapered downward toward the rectangle ends, which is exactly
            // backwards: the ends are the shoulders of a crossing, and a lane framed by the
            // *lowest* rock in the formation gives a player nothing to read. Inverted, the
            // face rises into every gap. 244 at the middle and 307 at the ends, rather than
            // a flat 240-300, because the +/-7 cm jitter below must not drop an edge mesa
            // under the 300 cm silhouette the chokepoint language depends on, nor push a
            // centre mesa into the 240 cm clamp where its variation would flatten out.
            const float ProfileHeight = FMath::Lerp(244.0f, 307.0f, EdgeBias);
            // Ends narrow as well as rise: a cliff face that pinches toward the crossing
            // funnels the eye into it, and shrinking a footprint can never push an instance
            // outside the authoritative rectangle it is already contained by.
            const float ProfileDepthScale = FMath::Lerp(1.0f, 0.80f, EdgeBias);
            for (int32 Row = 0; Row < Rows; ++Row)
            {
                const float RowCenter = -CrossHalf + 6.0f + (Row + 0.5f) * RowSpan;
                const float Cross = RowCenter + Random.FRandRange(-0.035f, 0.035f) * RowSpan;
                const float YawOffset = Random.FRandRange(-4.0f, 4.0f);
                const float SinYaw = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(YawOffset)));
                const float CosYaw = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(YawOffset)));
                float Depth = RowSpan * Random.FRandRange(0.82f, 0.94f) * ProfileDepthScale;
                float Width = SegmentSize * Random.FRandRange(0.86f, 0.97f);
                // Keep each module near its authored horizontal proportions; the
                // low cliff skirt, not a stretched rock, closes the remaining gap.
                Width = FMath::Min(Width, Depth * 1.35f);
                Depth = FMath::Min(Depth, Width * 1.35f);
                Width = FMath::Min(Width, FMath::Max(20.0f,
                    2.0f * (SegmentSize * 0.5f - Depth * 0.5f * SinYaw - 3.0f) / CosYaw));
                Depth = FMath::Min(Depth, FMath::Max(20.0f,
                    2.0f * (CrossHalf - FMath::Abs(Cross) - Width * 0.5f * SinYaw - 3.0f) / CosYaw));
                // Without landscape relief the meshes carry the blocked silhouette.
                const float Height = FMath::Clamp(ProfileHeight + Random.FRandRange(-7.0f, 7.0f)
                    - Row * 4.0f, 240.0f, 340.0f);
                const FVector Position = GroundPoint(Simulation,
                    Obstacle.center.x + (bLongX ? Along : Cross),
                    Obstacle.center.y + (bLongX ? Cross : Along), -MesaSinkCm);
                const float Yaw = (bLongX ? 0.0f : 90.0f) + YawOffset;
                const float HalfX = bLongX
                    ? Width * 0.5f * CosYaw + Depth * 0.5f * SinYaw
                    : Depth * 0.5f * CosYaw + Width * 0.5f * SinYaw;
                const float HalfY = bLongX
                    ? Depth * 0.5f * CosYaw + Width * 0.5f * SinYaw
                    : Width * 0.5f * CosYaw + Depth * 0.5f * SinYaw;
                if (FMath::Abs(Position.X - Obstacle.center.x) + HalfX > Obstacle.half.x + 0.05f
                    || FMath::Abs(Position.Y - Obstacle.center.y) + HalfY > Obstacle.half.y + 0.05f)
                    ++LastOutOfBoundsTallInstances;
                const EBatch Variant = static_cast<EBatch>((Seed * 3 + Segment * 5 + Row * 2) % 6);
                if (AddInstance(Variant, FTransform(FRotator(0, Yaw, 0), Position,
                    FVector(Width / 100.0f, Depth / 100.0f, Height / 100.0f))))
                {
                    LastMinimumMesaHeightCm = LastMinimumMesaHeightCm <= 0.0f
                        ? Height : FMath::Min(LastMinimumMesaHeightCm, Height);
                    LastMaximumMesaHeightCm = FMath::Max(LastMaximumMesaHeightCm, Height);
                    if (Rows > 1) ++LastMultiRowMesaInstances;
                }
            }
        }
        const int32 Fragments = FMath::Clamp(Segments * 2, 4, 12);
        for (int32 Fragment = 0; Fragment < Fragments; ++Fragment)
        {
            const float Width = Random.FRandRange(16.0f, 42.0f);
            const float Depth = Width * Random.FRandRange(0.60f, 1.0f);
            const float Yaw = Random.FRandRange(0.0f, 360.0f);
            const float SinYaw = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Yaw)));
            const float CosYaw = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Yaw)));
            const float HalfX = Width * 0.5f * CosYaw + Depth * 0.5f * SinYaw;
            const float HalfY = Depth * 0.5f * CosYaw + Width * 0.5f * SinYaw;
            const float X = Obstacle.center.x + Random.FRandRange(
                -FMath::Max(0.0f, Obstacle.half.x - HalfX - 2.0f),
                FMath::Max(0.0f, Obstacle.half.x - HalfX - 2.0f));
            const float Y = Obstacle.center.y + Random.FRandRange(
                -FMath::Max(0.0f, Obstacle.half.y - HalfY - 2.0f),
                FMath::Max(0.0f, Obstacle.half.y - HalfY - 2.0f));
            // Rubble beds a quarter of its own height into the plateau rather than the flat
            // 20 cm the mesas use: a 16 cm fragment sunk 20 cm would simply be gone.
            AddInstance(EBatch::Debris, FTransform(FRotator(0, Yaw, 0),
                GroundPoint(Simulation, X, Y, -Width * 0.55f * 0.25f),
                FVector(Width / 100.0f, Depth / 100.0f, Width * 0.55f / 100.0f)));
        }
    }

    TArray<const cinder::Entity*> Buildings;
    for (const cinder::Entity& Entity : Simulation.entities())
    {
        if (!IsDressedBuilding(Simulation, Entity)) continue;
        Buildings.Add(&Entity);
        if (Entity.team != 0) ++LastForeignBuildingSites;
        const float Radius = cinder::definition(Entity.kind).radius;
        const float Yaw = FMath::RadiansToDegrees(Entity.facing);
        AddInstance(EBatch::Pad, FTransform(FRotator(0, Yaw, 0),
            GroundPoint(Simulation, Entity.pos.x, Entity.pos.y, FogPlaneZ + 0.2f),
            FVector((Radius * 2.45f) / 100.0f, (Radius * 2.45f) / 100.0f, 0.055f)));

        FRandomStream Random(static_cast<int32>(Entity.id * 11939u));
        const float Side = (Entity.id & 1u) ? 1.0f : -1.0f;
        const float CrateX = Entity.pos.x + Radius * 0.72f;
        const float CrateY = Entity.pos.y + Side * Radius * 0.92f;
        AddInstance(EBatch::Crate, FTransform(FRotator(0, Yaw + Random.FRandRange(-12, 12), 0),
            GroundPoint(Simulation, CrateX, CrateY, FogPlaneZ + 0.15f),
            FVector(0.34f, 0.28f, 0.24f)));
        if (Entity.kind == cinder::Kind::Processor || Entity.kind == cinder::Kind::Foundry
            || Entity.kind == cinder::Kind::MotorPool)
        {
            const float PipeX = Entity.pos.x - Radius * 0.72f;
            const float PipeY = Entity.pos.y - Side * Radius * 0.86f;
            AddInstance(EBatch::Pipe, FTransform(FRotator(90, Yaw, 0),
                GroundPoint(Simulation, PipeX, PipeY, FogPlaneZ + 8.0f),
                FVector(0.16f, 0.16f, Radius * 1.15f / 100.0f)));
        }
    }

    Buildings.Sort([](const cinder::Entity& A, const cinder::Entity& B) { return A.id < B.id; });
    // The canyon ground material paints service routes through TerrainLayers.
    // Keep the raised rectangular road meshes only for the legacy ground path.
    if (!bCanyonGroundMaterialLoaded)
    {
        for (int32 Index = 1; Index < Buildings.Num(); ++Index)
        {
            const cinder::Entity& A = *Buildings[Index - 1];
            const cinder::Entity& B = *Buildings[Index];
            const FVector Delta(B.pos.x - A.pos.x, B.pos.y - A.pos.y, 0.0f);
            const float Length = Delta.Size2D();
            if (Length < 180.0f || Length > 1050.0f) continue;
            if (!RoadVisible(Simulation, A.pos, B.pos))
            {
                ++LastFogRejectedRoads;
                continue;
            }
            // One rigid box cannot follow a heightfield, so the whole span rides the
            // midpoint's terrain height. Keep the entire legacy road above the fog sheet;
            // intersecting its surface caused striping.
            const float SpanZ = SurfaceRelief(Simulation,
                (A.pos.x + B.pos.x) * 0.5f, (A.pos.y + B.pos.y) * 0.5f) + FogPlaneZ + 0.2f;
            AddInstance(EBatch::Road, FTransform(FRotator(0, FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)), 0),
                Point((A.pos.x + B.pos.x) * 0.5f, (A.pos.y + B.pos.y) * 0.5f, SpanZ),
                FVector(Length / 100.0f, 0.72f, 0.025f)));
        }
    }

    // Ore expansions are identified by the feathered Mineral channel in
    // CinderTerrainSurface. Keep resource dressing to small rubble here: reusing the
    // square industrial pad at cluster scale produced the conspicuous gold slabs.
    for (const cinder::Entity& Resource : KnownResources)
    {
        FRandomStream Random(static_cast<int32>(Resource.id * 7919u));
        for (int32 Piece = 0; Piece < 3; ++Piece)
        {
            const float Angle = Random.FRandRange(0.0f, 2.0f * PI);
            const float Radius = Random.FRandRange(70.0f, 118.0f);
            const float Size = Random.FRandRange(0.14f, 0.28f);
            const cinder::Vec2 Position{Resource.pos.x + FMath::Cos(Angle) * Radius,
                Resource.pos.y + FMath::Sin(Angle) * Radius};
            if (!AreaVisible(Simulation, Position, Size * 55.0f))
            {
                ++LastFogRejectedResourceInstances;
                continue;
            }
            AddInstance(EBatch::Debris, FTransform(FRotator(0, FMath::RadiansToDegrees(Angle), 0),
                GroundPoint(Simulation, Position.x, Position.y, FogPlaneZ + 0.1f),
                FVector(Size, Size * 0.7f, Size * 0.35f)));
        }
    }

    // Ambient scatter, placed last so it can only ever consume the debris capacity the
    // gameplay-meaningful dressing above did not want. Ninety percent of the playable
    // surface carried no props at all, which is the single largest reason the frame read
    // as a greybox; this is the cheapest fix available because it adds no batch, no
    // shadow caster, no material and no draw call.
    CinderTerrainSurface::FFeatures Features;
    Features.Map = Simulation.config().map;
    Features.WorldSize = Simulation.worldSize();
    for (const cinder::Obstacle& Cliff : Simulation.obstacles())
        if (Simulation.explored(0, Cliff.center)) Features.Cliffs.Add(Cliff);
    for (const cinder::Entity& Resource : KnownResources) Features.Minerals.Add(Resource.pos);
    CinderLandscapeTerrain::GatherPathways(Simulation, Features.Trails);
    int32 Scattered = 0;
    ForEachScatterCandidate(Simulation, [&](const FScatterCandidate& Candidate)
    {
        if (Scattered >= ScatterInstanceBudget) return false;
        // The same gate the obstacle loop uses. Nothing is drawn on ground the local
        // player has never explored, so scatter can never outline unseen terrain.
        if (!Simulation.explored(0, {Candidate.X, Candidate.Y})) return true;
        // Obstacle interiors already carry mesas, a skirt and rubble; a chip there would
        // be hidden geometry. Rectangles are hashed unconditionally, so this costs the
        // rebuild gate nothing.
        for (const cinder::Obstacle& Obstacle : Simulation.obstacles())
            if (FMath::Abs(Candidate.X - Obstacle.center.x) <= Obstacle.half.x + 24.0f
                && FMath::Abs(Candidate.Y - Obstacle.center.y) <= Obstacle.half.y + 24.0f) return true;
        // Same predicate the dressing loop used, so the building set this consults is
        // exactly the building set the hash saw.
        for (const cinder::Entity* Building : Buildings)
        {
            const float Clearance = cinder::definition(Building->kind).radius * 1.35f;
            const float DX = Candidate.X - Building->pos.x, DY = Candidate.Y - Building->pos.y;
            if (DX * DX + DY * DY <= Clearance * Clearance) return true;
        }
        const FColor Mask = CinderTerrainSurface::Sample(Features, Candidate.X, Candidate.Y);
        const float Stone = Mask.R / 255.0f;
        const float Ash = Mask.B / 255.0f;
        const float Worn = Mask.A / 255.0f;
        // Chips collect on exposed stone - which the mask already feathers outward from
        // every explored cliff, so obstacle skirts get their density for free - and blow
        // clear of the windblown ash drifts. The 0.09 floor keeps open lanes lightly
        // littered rather than sterile without crowding ground a player reads for pathing.
        const float Density = FMath::Clamp(0.09f + Stone * 0.74f - Ash * 0.24f, 0.0f, 1.0f)
            * (1.0f - Worn * 0.88f);
        if (Candidate.Roll > Density) return true;
        const float Size = FMath::Clamp(Candidate.SizeCm * FMath::Lerp(0.82f, 1.14f, Stone), 9.0f, 30.0f);
        if (!AddInstance(EBatch::Debris, FTransform(FRotator(0, Candidate.Yaw, 0),
            GroundPoint(Simulation, Candidate.X, Candidate.Y, -Size * Candidate.HeightRatio * 0.22f),
            FVector(Size / 100.0f, Size * Candidate.Ratio / 100.0f,
                Size * Candidate.HeightRatio / 100.0f)))) return false;
        ++Scattered;
        return true;
    });
    LastScatterInstances = Scattered;

    SubmitPending();
    LastObservedHash = StateHash;
    bHasObservedHash = true;
}

void UCinderScenery::Reset()
{
    for (UInstancedStaticMeshComponent* Batch : Batches) if (Batch) Batch->ClearInstances();
    for (TArray<FTransform>& Pending : PendingTransforms) Pending.Reset();
    for (TArray<FTransform>& Submitted : SubmittedTransforms) Submitted.Reset();
    LastObservedHash = 0;
    bHasObservedHash = false;
    LastOutOfBoundsTallInstances = 0;
    LastFogRejectedResourceInstances = 0;
    LastFogRejectedRoads = 0;
    LastMultiRowMesaInstances = 0;
    LastScatterInstances = 0;
    LastForeignBuildingSites = 0;
    LastMinimumMesaHeightCm = 0.0f;
    LastMaximumMesaHeightCm = 0.0f;
}

bool UCinderScenery::IsInitialized() const
{
    if (!AttachedParent || Batches.Num() != BatchCount) return false;
    for (const UInstancedStaticMeshComponent* Batch : Batches) if (!Batch) return false;
    return true;
}

FCinderSceneryDiagnostics UCinderScenery::Diagnostics() const
{
    FCinderSceneryDiagnostics Result;
    Result.bInitialized = IsInitialized();
    Result.Rebuilds = RebuildCount;
    Result.BatchUploads = BatchUploadCount;
    Result.TallRockBatchUploads = TallRockBatchUploadCount;
    Result.IndustrialBatchUploads = IndustrialBatchUploadCount;
    Result.DebrisBatchUploads = DebrisBatchUploadCount;
    Result.UnchangedBatchSkips = UnchangedBatchSkipCount;
    Result.BatchCount = Batches.Num();
    Result.CanyonMeshBatches = CanyonMeshBatchCount;
    Result.LegacyCanyonFallbackBatches = LegacyCanyonFallbackBatchCount;
    Result.PrimitiveCanyonFallbackBatches = PrimitiveCanyonFallbackBatchCount;
    Result.MultiRowMesaInstances = LastMultiRowMesaInstances;
    Result.ForeignBuildingSites = LastForeignBuildingSites;
    Result.MinimumMesaHeightCm = LastMinimumMesaHeightCm;
    Result.MaximumMesaHeightCm = LastMaximumMesaHeightCm;
    Result.bCanyonMaterialLoaded = bCanyonMaterialLoaded;
    Result.bCanyonGroundMaterialLoaded = bCanyonGroundMaterialLoaded;
    Result.OutOfBoundsTallInstances = LastOutOfBoundsTallInstances;
    Result.FogRejectedResourceInstances = LastFogRejectedResourceInstances;
    Result.FogRejectedRoads = LastFogRejectedRoads;
    for (int32 Index = 0; Index < Batches.Num(); ++Index)
    {
        const int32 Count = Batches[Index] ? Batches[Index]->GetInstanceCount() : 0;
        if (Batches[Index] && Batches[Index]->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
            ++Result.CollisionEnabledBatches;
        Result.TotalInstances += Count;
        if (Index <= static_cast<int32>(EBatch::RockF))
        {
            Result.RockVariantInstances += Count;
            Result.TallRockInstances += Count;
        }
        else if (Index == static_cast<int32>(EBatch::CliffMass))
        {
            Result.CliffMassInstances += Count;
            Result.TallRockInstances += Count;
        }
        else if (Index == static_cast<int32>(EBatch::Debris))
        {
            // Ambient scatter shares this component with obstacle rubble and ore chips but
            // is a separate population answering to a separate budget. Reporting them apart
            // is what keeps DebrisInstances meaning "authored dressing" for the bounds and
            // fog regressions, which would otherwise read a scatter pass as a fog leak.
            Result.AmbientScatterInstances = FMath::Min(Count, LastScatterInstances);
            Result.DebrisInstances += FMath::Max(0, Count - LastScatterInstances);
        }
        else if (Index == static_cast<int32>(EBatch::Road))
        {
            Result.RoadInstances += Count;
            Result.IndustrialInstances += Count;
        }
        else Result.IndustrialInstances += Count;
    }
    return Result;
}
