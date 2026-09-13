#include "Presentation/CinderScenery.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

namespace
{
constexpr int32 BatchCount = 12;
constexpr float FogPlaneZ = 2.5f;

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

bool IsFriendlyCompleteBuilding(const cinder::Entity& Entity)
{
    return Entity.team == 0 && Entity.alive() && Entity.progress >= 1.0f
        && cinder::definition(Entity.kind).building;
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

void UCinderScenery::Initialize(USceneComponent* AttachParent, UMaterialInterface* TerrainMaterial)
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
            const FString CanyonPath = FString::Printf(TEXT("/Game/Art/Canyon/Meshes/%s.%s"),
                CanyonAssetNames[Index], CanyonAssetNames[Index]);
            Mesh = LoadObject<UStaticMesh>(nullptr, *CanyonPath, nullptr, LOAD_NoWarn);
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
            bRock, bRock ? 5200 : 3900);
    }
}

bool UCinderScenery::AddInstance(EBatch Batch, const FTransform& Transform)
{
    const int32 Index = static_cast<int32>(Batch);
    if (!Batches.IsValidIndex(Index) || !Batches[Index] || !Batches[Index]->GetStaticMesh()) return false;
    int32 InstanceCap = 12;
    switch (Batch)
    {
    case EBatch::CliffMass: InstanceCap = 48; break;
    case EBatch::Pad:
    case EBatch::Crate: InstanceCap = 24; break;
    case EBatch::Road:
    case EBatch::Pipe: InstanceCap = 16; break;
    case EBatch::Debris: InstanceCap = 72; break;
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
    for (const cinder::Obstacle& Obstacle : Simulation.obstacles())
    {
        Hash = HashFloat(Hash, Obstacle.center.x);
        Hash = HashFloat(Hash, Obstacle.center.y);
        Hash = HashFloat(Hash, Obstacle.half.x);
        Hash = HashFloat(Hash, Obstacle.half.y);
        Hash = HashCombineFast(Hash, Simulation.explored(0, Obstacle.center) ? 1u : 0u);
    }
    for (const cinder::Entity& Entity : Simulation.entities())
        if (IsFriendlyCompleteBuilding(Entity)) Hash = HashEntity(Hash, Entity);
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
    if (!bCanyonGroundMaterialLoaded)
    {
        TArray<const cinder::Entity*> Buildings;
        for (const cinder::Entity& Entity : Simulation.entities())
            if (IsFriendlyCompleteBuilding(Entity)) Buildings.Add(&Entity);
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

void UCinderScenery::Update(const cinder::Simulation& Simulation,
    const std::vector<cinder::Entity>& KnownResources)
{
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
        const int32 EndCull = FMath::RoundToInt((bRock ? 5200.0f : 3900.0f) * CullScale);
        Batch->SetCullDistances(FMath::Max(0, EndCull - FMath::RoundToInt(1400.0f * CullScale)), EndCull);
    }
    for (TArray<FTransform>& Pending : PendingTransforms) Pending.Reset();
    LastOutOfBoundsTallInstances = 0;
    LastFogRejectedResourceInstances = 0;
    LastFogRejectedRoads = 0;
    LastMultiRowMesaInstances = 0;
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
        for (int32 Segment = 0; Segment < Segments; ++Segment)
        {
            const float Along = (Segment + 0.5f) / Segments * LongSize - LongSize * 0.5f;
            const float CrossHalf = bLongX ? Obstacle.half.y : Obstacle.half.x;
            // A low, nearly continuous stratified shelf closes the blocked rectangle
            // behind the taller irregular silhouettes.
            const FVector MassPosition = Point(Obstacle.center.x + (bLongX ? Along : 0.0f),
                Obstacle.center.y + (bLongX ? 0.0f : Along), 0.0f);
            const float MassAlong = FMath::Max(20.0f, SegmentSize - 6.0f);
            const float MassAcross = FMath::Max(20.0f, CrossHalf * 2.0f - 8.0f);
            const FVector MassScale(bLongX ? MassAlong / 100.0f : MassAcross / 100.0f,
                bLongX ? MassAcross / 100.0f : MassAlong / 100.0f,
                FMath::Clamp(20.0f + CrossHalf * 0.035f, 24.0f, 38.0f) / 100.0f);
            if (FMath::Abs(MassPosition.X - Obstacle.center.x) + MassScale.X * 50.0f > Obstacle.half.x + 0.05f
                || FMath::Abs(MassPosition.Y - Obstacle.center.y) + MassScale.Y * 50.0f > Obstacle.half.y + 0.05f)
                ++LastOutOfBoundsTallInstances;
            AddInstance(EBatch::CliffMass, FTransform(FQuat::Identity, MassPosition, MassScale));

            // Broad obstacles use two authored rows instead of stretching one
            // boulder across the full blocked width. A shared central rise keeps
            // neighboring modules reading as one stratified mesa.
            const int32 Rows = CrossHalf >= 300.0f ? 2 : 1;
            const float RowSpan = (CrossHalf * 2.0f - 12.0f) / Rows;
            const float CenterDistance = FMath::Abs((Segment + 0.5f) / Segments - 0.5f) * 2.0f;
            const float ProfileHeight = FMath::Lerp(236.0f, 190.0f, CenterDistance);
            for (int32 Row = 0; Row < Rows; ++Row)
            {
                const float RowCenter = -CrossHalf + 6.0f + (Row + 0.5f) * RowSpan;
                const float Cross = RowCenter + Random.FRandRange(-0.035f, 0.035f) * RowSpan;
                const float YawOffset = Random.FRandRange(-4.0f, 4.0f);
                const float SinYaw = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(YawOffset)));
                const float CosYaw = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(YawOffset)));
                float Depth = RowSpan * Random.FRandRange(0.82f, 0.94f);
                float Width = SegmentSize * Random.FRandRange(0.86f, 0.97f);
                // Keep each module near its authored horizontal proportions; the
                // low cliff skirt, not a stretched rock, closes the remaining gap.
                Width = FMath::Min(Width, Depth * 1.35f);
                Depth = FMath::Min(Depth, Width * 1.35f);
                Width = FMath::Min(Width, FMath::Max(20.0f,
                    2.0f * (SegmentSize * 0.5f - Depth * 0.5f * SinYaw - 3.0f) / CosYaw));
                Depth = FMath::Min(Depth, FMath::Max(20.0f,
                    2.0f * (CrossHalf - FMath::Abs(Cross) - Width * 0.5f * SinYaw - 3.0f) / CosYaw));
                const float Height = FMath::Clamp(ProfileHeight + Random.FRandRange(-7.0f, 7.0f)
                    - Row * 4.0f, 180.0f, 240.0f);
                const FVector Position = Point(Obstacle.center.x + (bLongX ? Along : Cross),
                    Obstacle.center.y + (bLongX ? Cross : Along), 0.0f);
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
            AddInstance(EBatch::Debris, FTransform(FRotator(0, Yaw, 0), Point(X, Y, 0.0f),
                FVector(Width / 100.0f, Depth / 100.0f, Width * 0.55f / 100.0f)));
        }
    }

    TArray<const cinder::Entity*> Buildings;
    for (const cinder::Entity& Entity : Simulation.entities())
    {
        if (!IsFriendlyCompleteBuilding(Entity)) continue;
        Buildings.Add(&Entity);
        const float Radius = cinder::definition(Entity.kind).radius;
        const float Yaw = FMath::RadiansToDegrees(Entity.facing);
        AddInstance(EBatch::Pad, FTransform(FRotator(0, Yaw, 0), Point(Entity.pos.x, Entity.pos.y, FogPlaneZ + 0.2f),
            FVector((Radius * 2.45f) / 100.0f, (Radius * 2.45f) / 100.0f, 0.055f)));

        FRandomStream Random(static_cast<int32>(Entity.id * 11939u));
        const float Side = (Entity.id & 1u) ? 1.0f : -1.0f;
        const FVector CratePosition = Point(Entity.pos.x + Radius * 0.72f,
            Entity.pos.y + Side * Radius * 0.92f, FogPlaneZ + 0.15f);
        AddInstance(EBatch::Crate, FTransform(FRotator(0, Yaw + Random.FRandRange(-12, 12), 0), CratePosition,
            FVector(0.34f, 0.28f, 0.24f)));
        if (Entity.kind == cinder::Kind::Processor || Entity.kind == cinder::Kind::Foundry
            || Entity.kind == cinder::Kind::MotorPool)
        {
            const FVector PipePosition = Point(Entity.pos.x - Radius * 0.72f,
                Entity.pos.y - Side * Radius * 0.86f, FogPlaneZ + 8.0f);
            AddInstance(EBatch::Pipe, FTransform(FRotator(90, Yaw, 0), PipePosition,
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
            // Keep the entire legacy road above the fog sheet; intersecting its surface caused striping.
            const FVector From(A.pos.x, A.pos.y, FogPlaneZ + 0.2f), To(B.pos.x, B.pos.y, FogPlaneZ + 0.2f);
            const FVector Delta = To - From;
            const float Length = Delta.Size2D();
            if (Length < 180.0f || Length > 1050.0f) continue;
            if (!RoadVisible(Simulation, A.pos, B.pos))
            {
                ++LastFogRejectedRoads;
                continue;
            }
            AddInstance(EBatch::Road, FTransform(FRotator(0, FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)), 0),
                (From + To) * 0.5f, FVector(Length / 100.0f, 0.72f, 0.025f)));
        }
    }

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
                Point(Position.x, Position.y, FogPlaneZ + 0.1f), FVector(Size, Size * 0.7f, Size * 0.35f)));
        }
    }

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
        else if (Index == static_cast<int32>(EBatch::Debris)) Result.DebrisInstances += Count;
        else if (Index == static_cast<int32>(EBatch::Road))
        {
            Result.RoadInstances += Count;
            Result.IndustrialInstances += Count;
        }
        else Result.IndustrialInstances += Count;
    }
    return Result;
}
