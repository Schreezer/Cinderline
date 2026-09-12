#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderAudioSubsystem.h"
#include "Presentation/CinderTerrainSurface.h"
#include "Presentation/CinderScenery.h"
#include "Presentation/CinderWorldEffects.h"
#include "Presentation/CinderOnlineSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/GameInstance.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "RHITypes.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "UObject/ConstructorHelpers.h"
#include <algorithm>

DEFINE_LOG_CATEGORY_STATIC(LogCinderModels, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCinderCombat, Log, All);

namespace
{
constexpr int32 ModelCount = 15;
constexpr int32 FogTextureSize = 256;
constexpr int32 FogPixelsPerCell = FogTextureSize / cinder::Simulation::FogSize;
static_assert(FogPixelsPerCell == 4, "Fog edge guards assume four pixels per simulation cell");
struct FFogTextureUpload
{
    FUpdateTextureRegion2D Region{0, 0, 0, 0, FogTextureSize, FogTextureSize};
    TArray<uint8> Pixels;
};
const TCHAR* ModelAssetNames[ModelCount] = {
    TEXT("SM_Drudge"), TEXT("SM_Ember"), TEXT("SM_Needle"), TEXT("SM_Skim"),
    TEXT("SM_Anvil"), TEXT("SM_Cinderthrow"), TEXT("SM_Mend"), TEXT("SM_Veil"),
    TEXT("SM_Anchor"), TEXT("SM_Siphon"), TEXT("SM_Kiln"), TEXT("SM_Crucible"),
    TEXT("SM_Resonator"), TEXT("SM_Ward"), TEXT("SM_Ore")
};
const TCHAR* ModelSlotNames[] = { TEXT("HullDark"), TEXT("HullLight"), TEXT("Metal"), TEXT("TeamPanel"), TEXT("CoreGlow") };
FAutoConsoleCommandWithWorld ModelStatusCommand(
    TEXT("cinder.models"),
    TEXT("Report imported models, primitive fallbacks and current rendered entity counts."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (!World) return;
        for (TActorIterator<ACinderBattlefield> It(World); It; ++It) It->LogModelStatus();
    }));
FAutoConsoleCommandWithWorld CombatStatusCommand(
    TEXT("cinder.combat"),
    TEXT("Report consumed combat IDs, adapter cue requests, and actual audio submissions."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (!World) return;
        for (TActorIterator<ACinderBattlefield> It(World); It; ++It) It->LogCombatStatus();
    }));
}

ACinderBattlefield::ACinderBattlefield()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("BattlefieldRoot"));
    Scenery = CreateDefaultSubobject<UCinderScenery>(TEXT("Scenery"));
    WorldEffects = CreateDefaultSubobject<UCinderWorldEffects>(TEXT("WorldEffects"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeAsset(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneAsset(TEXT("/Engine/BasicShapes/Plane.Plane"));
    static ConstructorHelpers::FObjectFinder<UTextureCube> AmbientAsset(TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"));
    Cube = CubeAsset.Object; Cylinder = CylinderAsset.Object; Cone = ConeAsset.Object; Sphere = SphereAsset.Object;
    Plane = PlaneAsset.Object;
    AmbientCubemap = AmbientAsset.Object;
}

ACinderBattlefield::FBatch& ACinderBattlefield::AddBatch(UStaticMesh* Mesh, FLinearColor Color, bool bCastShadow, bool bDynamic)
{
    auto* Component = NewObject<UInstancedStaticMeshComponent>(this);
    Component->SetupAttachment(RootComponent);
    Component->SetStaticMesh(Mesh);
    Component->SetMobility(EComponentMobility::Movable);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCanEverAffectNavigation(false);
    Component->SetCastShadow(bCastShadow);
    Component->SetCastContactShadow(bCastShadow);
    Component->RegisterComponent();
    if (BaseMaterial)
    {
        auto* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
        Material->SetVectorParameterValue(TEXT("Tint"), Color);
        Component->SetMaterial(0, Material);
    }
    MeshComponents.Add(Component);
    FBatch Batch; Batch.Mesh = Component; Batch.bDynamic = bDynamic;
    Batches.Add(MoveTemp(Batch));
    return Batches.Last();
}

bool ACinderBattlefield::ValidateModel(UStaticMesh* Mesh, cinder::Kind Kind, FString& Reason) const
{
    if (!Mesh) { Reason = TEXT("asset missing"); return false; }
    const auto& Slots = Mesh->GetStaticMaterials();
    if (Slots.Num() != UE_ARRAY_COUNT(ModelSlotNames)) { Reason = TEXT("expected five material slots"); return false; }
    for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(ModelSlotNames); ++Slot)
        if (Slots[Slot].MaterialSlotName != FName(ModelSlotNames[Slot]))
        {
            Reason = FString::Printf(TEXT("slot %d is %s, expected %s"), Slot, *Slots[Slot].MaterialSlotName.ToString(), ModelSlotNames[Slot]);
            return false;
        }
    const FBox Bounds = Mesh->GetBoundingBox();
    const FVector Size = Bounds.GetSize(), Center = Bounds.GetCenter();
    const double Diameter = cinder::definition(Kind).radius * 2;
    const double Tolerance = FMath::Max(0.2, Diameter * 0.01);
    if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || !FMath::IsFinite(Size.Z)
        || Size.X <= 0 || Size.Y <= 0 || Size.Z <= 0
        || FMath::Abs(FMath::Max(Size.X, Size.Y) - Diameter) > Tolerance)
    {
        Reason = FString::Printf(TEXT("footprint %.2fx%.2f does not match %.2f cm diameter"), Size.X, Size.Y, Diameter);
        return false;
    }
    if (FMath::Abs(Center.X) > Tolerance || FMath::Abs(Center.Y) > Tolerance || FMath::Abs(Bounds.Min.Z) > Tolerance)
    {
        Reason = TEXT("mesh origin is not bottom-centered"); return false;
    }
    if (Mesh->GetNumTriangles(0) <= 0 || Mesh->GetNumSections(0) != UE_ARRAY_COUNT(ModelSlotNames))
    {
        Reason = TEXT("mesh must have geometry in five material sections"); return false;
    }
    return true;
}

void ACinderBattlefield::LoadModelBatches()
{
    ModelBatchStart = Batches.Num();
    ModelBatchIndices.Init(INDEX_NONE, ModelCount * 2);
    MotionPartBatchIndices.Init(INDEX_NONE, ModelCount * 2 * static_cast<int32>(ECinderMotionPart::Count));
    MotionKindAvailable.Init(0, ModelCount);
    MotionMeshes.Reset();
    ModelFallbackReasons.Init(TEXT("asset missing"), ModelCount);
    ModelMeshes.SetNum(ModelCount);
    ModelMaterials.Reset();
    for (const TCHAR* Slot : ModelSlotNames)
    {
        const FString Name = FString::Printf(TEXT("/Game/Art/Materials/MI_Cinder%s.MI_Cinder%s"), Slot, Slot);
        const FString TargetName = FString::Printf(TEXT("/Game/Art/VisualTarget/Materials/MI_VT_%s.MI_VT_%s"), Slot, Slot);
        auto* Material = LoadObject<UMaterialInterface>(nullptr, *TargetName, nullptr, LOAD_NoWarn);
        if (!Material) Material = LoadObject<UMaterialInterface>(nullptr, *Name, nullptr, LOAD_NoWarn);
        if (!Material)
        {
            ModelFallbackReasons.Init(TEXT("model material pack missing"), ModelCount);
            UE_LOG(LogCinderModels, Verbose, TEXT("Model material pack is absent; using primitive entity silhouettes."));
            return;
        }
        ModelMaterials.Add(Material);
    }
    // Share these six dynamic instances across every kind; only the team panels and cores change.
    const FLinearColor PanelColors[] = { FLinearColor(0.04f, 0.82f, 0.72f), FLinearColor(0.96f, 0.24f, 0.17f), FLinearColor(0.85f, 0.35f, 0.08f) };
    const FLinearColor CoreColors[] = { FLinearColor(0.50f, 1.0f, 0.88f), FLinearColor(1.0f, 0.62f, 0.20f), FLinearColor(1.0f, 0.58f, 0.12f) };
    for (int32 Palette = 0; Palette < 3; ++Palette)
    {
        auto* Panel = UMaterialInstanceDynamic::Create(ModelMaterials[3], this);
        auto* Core = UMaterialInstanceDynamic::Create(ModelMaterials[4], this);
        Panel->SetVectorParameterValue(TEXT("Tint"), PanelColors[Palette]);
        Core->SetVectorParameterValue(TEXT("Tint"), CoreColors[Palette]);
        ModelMaterials.Add(Panel); ModelMaterials.Add(Core);
    }
    // Warm rock faces keep ore recognizable at compact gameplay zoom.
    const int32 OreMaterialStart = ModelMaterials.Num();
    const FLinearColor OreColors[] = { FLinearColor(0.24f, 0.11f, 0.035f), FLinearColor(0.58f, 0.29f, 0.07f) };
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        auto* OreMaterial = UMaterialInstanceDynamic::Create(ModelMaterials[Slot], this);
        OreMaterial->SetVectorParameterValue(TEXT("Tint"), OreColors[Slot]);
        ModelMaterials.Add(OreMaterial);
    }
    int32 Loaded = 0;
    for (int32 Index = 0; Index < ModelCount; ++Index)
    {
        const FString Path = FString::Printf(TEXT("/Game/Art/Models/%s.%s"), ModelAssetNames[Index], ModelAssetNames[Index]);
        const FString TargetPath = FString::Printf(TEXT("/Game/Art/VisualTarget/Models/%s.%s"), ModelAssetNames[Index], ModelAssetNames[Index]);
        auto* Mesh = LoadObject<UStaticMesh>(nullptr, *TargetPath, nullptr, LOAD_NoWarn);
        if (!Mesh) Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn);
        if (!ValidateModel(Mesh, static_cast<cinder::Kind>(Index), ModelFallbackReasons[Index]))
        {
            UE_LOG(LogCinderModels, Verbose, TEXT("%s fallback: %s"), ModelAssetNames[Index], *ModelFallbackReasons[Index]);
            continue;
        }
        ModelMeshes[Index] = Mesh;
        ModelFallbackReasons[Index].Empty();
        const bool Resource = Index == static_cast<int32>(cinder::Kind::Resource);
        for (int32 Team = 0; Team < (Resource ? 1 : 2); ++Team)
        {
            const int32 BatchIndex = Batches.Num();
            FBatch& Batch = AddBatch(Mesh, FLinearColor::White, true);
            for (int32 Slot = 0; Slot < 3; ++Slot)
                Batch.Mesh->SetMaterial(Slot, ModelMaterials[Resource && Slot < 2 ? OreMaterialStart + Slot : Slot]);
            const int32 Palette = Resource ? 2 : Team;
            Batch.Mesh->SetMaterial(3, ModelMaterials[5 + Palette * 2]);
            Batch.Mesh->SetMaterial(4, ModelMaterials[6 + Palette * 2]);
            ModelBatchIndices[Index * 2 + Team] = BatchIndex;
        }
        ++Loaded;
    }

    auto MotionKey = [](int32 Kind, int32 Team, ECinderMotionPart Part)
    {
        return (Kind * 2 + Team) * static_cast<int32>(ECinderMotionPart::Count) + static_cast<int32>(Part);
    };
    auto MaterialForName = [&](FName Name, int32 Team) -> UMaterialInterface*
    {
        if (Name == FName(ModelSlotNames[0])) return ModelMaterials[0];
        if (Name == FName(ModelSlotNames[1])) return ModelMaterials[1];
        if (Name == FName(ModelSlotNames[2])) return ModelMaterials[2];
        if (Name == FName(ModelSlotNames[3])) return ModelMaterials[5 + Team * 2];
        if (Name == FName(ModelSlotNames[4])) return ModelMaterials[6 + Team * 2];
        return nullptr;
    };
    auto ApplyMotionMaterials = [&](UInstancedStaticMeshComponent* Component, UStaticMesh* Mesh, int32 Team)
    {
        const auto& Slots = Mesh->GetStaticMaterials();
        for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
            Component->SetMaterial(Slot, MaterialForName(Slots[Slot].MaterialSlotName, Team));
    };
    for (int32 KindIndex = 0; KindIndex < ModelCount; ++KindIndex)
    {
        const auto Parts = CinderMotionAssetParts(static_cast<cinder::Kind>(KindIndex));
        if (Parts.IsEmpty() || !ModelMeshes.IsValidIndex(KindIndex) || !ModelMeshes[KindIndex]) continue;
        TArray<UStaticMesh*> LoadedParts;
        bool bComplete = false;
        for (const TCHAR* MotionRoot : { TEXT("/Game/Art/VisualTarget/Motion"), TEXT("/Game/Art/Motion") })
        {
            LoadedParts.Reset();
            bComplete = true;
            for (const FCinderMotionAssetPart& Part : Parts)
            {
                const FString Path = FString::Printf(TEXT("%s/%s.%s"), MotionRoot, Part.AssetName, Part.AssetName);
                UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn);
                LoadedParts.Add(Mesh);
                if (!Mesh || Mesh->GetNumTriangles(0) <= 0 || Mesh->GetNumSections(0) != Mesh->GetStaticMaterials().Num())
                {
                    bComplete = false;
                    break;
                }
                int32 PriorSlot = -1;
                for (const FStaticMaterial& Material : Mesh->GetStaticMaterials())
                {
                    int32 SlotIndex = INDEX_NONE;
                    for (int32 Candidate = 0; Candidate < UE_ARRAY_COUNT(ModelSlotNames); ++Candidate)
                        if (Material.MaterialSlotName == FName(ModelSlotNames[Candidate])) { SlotIndex = Candidate; break; }
                    if (SlotIndex <= PriorSlot) { bComplete = false; break; }
                    PriorSlot = SlotIndex;
                }
                if (!bComplete) break;
            }
            if (bComplete && LoadedParts.Num() == Parts.Num()) break;
        }
        if (!bComplete || LoadedParts.Num() != Parts.Num())
        {
            UE_LOG(LogCinderModels, Verbose, TEXT("%s motion pack incomplete; keeping the complete static model."), ModelAssetNames[KindIndex]);
            continue;
        }

        for (UStaticMesh* Mesh : LoadedParts) MotionMeshes.Add(Mesh);
        for (int32 Team = 0; Team < 2; ++Team)
        {
            for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
            {
                const FCinderMotionAssetPart& Part = Parts[PartIndex];
                UStaticMesh* Mesh = LoadedParts[PartIndex];
                int32 BatchIndex = INDEX_NONE;
                if (Part.Part == ECinderMotionPart::Body)
                {
                    BatchIndex = ModelBatchIndices[KindIndex * 2 + Team];
                    Batches[BatchIndex].Mesh->SetStaticMesh(Mesh);
                }
                else
                {
                    BatchIndex = Batches.Num();
                    AddBatch(Mesh, FLinearColor::White, true);
                }
                ApplyMotionMaterials(Batches[BatchIndex].Mesh, Mesh, Team);
                MotionPartBatchIndices[MotionKey(KindIndex, Team, Part.Part)] = BatchIndex;
            }
        }
        MotionKindAvailable[KindIndex] = 1;
    }
    ModelBatchCount = Batches.Num() - ModelBatchStart;
    UE_LOG(LogCinderModels, Verbose, TEXT("Loaded %d/%d models in %d ISM batches; cinder.models reports live usage."), Loaded, ModelCount, ModelBatchCount);
}

void ACinderBattlefield::LogModelStatus() const
{
    int32 Loaded = 0;
    for (const auto& Mesh : ModelMeshes) if (Mesh) ++Loaded;
    UE_LOG(LogCinderModels, Display, TEXT("CINDERLINE_RENDER_MODELS loaded=%d/%d model_batches=%d rendered_model_entities=%d rendered_fallback_entities=%d"),
        Loaded, ModelCount, ModelBatchCount, LastModelEntities, LastFallbackEntities);
    int32 RockKinds = 0, RockInstances = 0;
    for (int32 Index : RockBatchIndices) if (Index != INDEX_NONE)
    {
        ++RockKinds;
        RockInstances += Batches[Index].Mesh->GetInstanceCount();
    }
    UE_LOG(LogCinderModels, Display, TEXT("CINDERLINE_RENDER_WORLD rock_kinds=%d/4 rock_instances=%d fog=%s fog_uploads=%llu pads=%d"),
        RockKinds, RockInstances, FogPlaneBatch == INDEX_NONE ? TEXT("grid_fallback") : TEXT("256_texture_plane"),
        FogTextureUploads, PadBatch == INDEX_NONE ? 0 : Batches[PadBatch].Mesh->GetInstanceCount());
    UE_LOG(LogCinderModels, Display, TEXT("CINDERLINE_TERRAIN_SURFACE material=%s mask=%s uploads=%llu; cached explored cliffs and observed mineral positions, no terrain collision changes"),
        GroundSurfaceMaterial ? TEXT("layered_basalt") : TEXT("legacy"),
        GroundSurfaceTexture ? TEXT("256_linear") : TEXT("none"), TerrainSurfaceUploads);
    UE_LOG(LogCinderModels, Display,
        TEXT("CINDERLINE_INSTANCE_SUBMISSIONS passes=%llu delta_calls=%llu transforms=%llu added=%llu removed=%llu full_rebuilds=%llu unchanged_skips=%llu static_skips=%llu; actor-lifetime accepted adapter submissions, not GPU timings"),
        InstanceUploads.Passes, InstanceUploads.DeltaCalls, InstanceUploads.Transforms, InstanceUploads.Added,
        InstanceUploads.Removed, InstanceUploads.FullRebuilds, InstanceUploads.UnchangedSkips, InstanceUploads.StaticSkips);
    if (Scenery)
    {
        const auto Details = Scenery->Diagnostics();
        UE_LOG(LogCinderModels, Display, TEXT("CINDERLINE_SCENERY initialized=%d batches=%d instances=%d rocks=%d industrial=%d debris=%d out_of_bounds=%d rebuilds=%llu"),
            Details.bInitialized, Details.BatchCount, Details.TotalInstances, Details.TallRockInstances,
            Details.IndustrialInstances, Details.DebrisInstances, Details.OutOfBoundsTallInstances, Details.Rebuilds);
    }
    if (WorldEffects)
    {
        const auto& Effects = WorldEffects->Stats();
        UE_LOG(LogCinderModels, Display, TEXT("CINDERLINE_WORLD_EFFECTS initialized=%d instances=%d peak=%d uploads=%llu hidden=%llu budget_drops=%llu"),
            WorldEffects->IsInitialized(), Effects.RenderedInstances, Effects.PeakInstances, Effects.Uploads,
            Effects.HiddenEvents, Effects.DroppedForBudget);
    }
    for (int32 Index = 0; Index < ModelCount; ++Index)
    {
        if (ModelMeshes.IsValidIndex(Index) && ModelMeshes[Index])
        {
            const int32 First = ModelBatchIndices[Index * 2], Second = ModelBatchIndices[Index * 2 + 1];
            const int32 Count = (First != INDEX_NONE ? Batches[First].Mesh->GetInstanceCount() : 0) + (Second != INDEX_NONE ? Batches[Second].Mesh->GetInstanceCount() : 0);
            UE_LOG(LogCinderModels, Display, TEXT("  %s: imported, instances=%d, triangles=%d"), ModelAssetNames[Index], Count, ModelMeshes[Index]->GetNumTriangles(0));
        }
        else UE_LOG(LogCinderModels, Display, TEXT("  %s: primitive fallback (%s)"), ModelAssetNames[Index], ModelFallbackReasons.IsValidIndex(Index) ? *ModelFallbackReasons[Index] : TEXT("not initialized"));
    }
}

void ACinderBattlefield::InitializeEnvironment()
{
    RockBatchIndices.Init(INDEX_NONE, 4);
    auto* RockMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_CinderBasaltV2.M_CinderBasaltV2"), nullptr, LOAD_NoWarn);
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FString Name = FString::Printf(TEXT("SM_BasaltCliff_%c"), TCHAR('A' + Index));
        const FString Path = FString::Printf(TEXT("/Game/Art/Environment/%s.%s"), *Name, *Name);
        auto* Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn);
        if (!Mesh) continue;
        // Environment imports use a separate one-slot contract, not the unit-model validator.
        const FBox Bounds = Mesh->GetBoundingBox();
        if (!Bounds.GetSize().Equals(FVector(100), 1.0f) || !Bounds.GetCenter().Equals(FVector(0, 0, 50), 1.0f)) continue;
        RockBatchIndices[Index] = Batches.Num();
        FBatch& Batch = AddBatch(Mesh, FLinearColor(0.15f, 0.18f, 0.19f), true, false);
        if (RockMaterial) Batch.Mesh->SetMaterial(0, RockMaterial);
        else if (Mesh->GetMaterial(0)) Batch.Mesh->SetMaterial(0, Mesh->GetMaterial(0));
    }
    PadBatch = Batches.Num();
    FBatch& Pad = AddBatch(Cube, FLinearColor(0.10f, 0.14f, 0.15f), false);
    if (auto* PadMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/MI_CinderMetal.MI_CinderMetal"), nullptr, LOAD_NoWarn))
        Pad.Mesh->SetMaterial(0, PadMaterial);

    auto* FogBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_CinderFogV2.M_CinderFogV2"), nullptr, LOAD_NoWarn);
    if (FogBase && Plane && FApp::CanEverRender())
    {
        // An opaque initial texture prevents a reveal while the first region upload is queued.
        TArray64<uint8> InitialPixels;
        InitialPixels.SetNumZeroed(FogTextureSize * FogTextureSize * 4);
        for (int32 Pixel = 0; Pixel < FogTextureSize * FogTextureSize; ++Pixel)
        {
            InitialPixels[Pixel * 4 + 2] = 255; // BGRA storage; material samples R for opacity.
            InitialPixels[Pixel * 4 + 3] = 255;
        }
        FogTexture = UTexture2D::CreateTransient(FogTextureSize, FogTextureSize, PF_B8G8R8A8, NAME_None, InitialPixels);
        if (FogTexture)
        {
            FogTexture->SRGB = false;
            FogTexture->NeverStream = true;
            FogTexture->Filter = TF_Bilinear;
            FogTexture->AddressX = TA_Clamp;
            FogTexture->AddressY = TA_Clamp;
            FogTexture->UpdateResource();
            FogMaterial = UMaterialInstanceDynamic::Create(FogBase, this);
            FogMaterial->SetTextureParameterValue(TEXT("FogMask"), FogTexture);
            FogMaterial->SetVectorParameterValue(TEXT("FogColor"), FLinearColor(0.010f, 0.017f, 0.027f));
            FogMaterial->SetVectorParameterValue(TEXT("FogExploredColor"), FLinearColor(0.027f, 0.041f, 0.049f));
            FogPlaneBatch = Batches.Num();
            FBatch& Fog = AddBatch(Plane, FLinearColor::Black, false, false);
            Fog.Mesh->SetMaterial(0, FogMaterial);
            Fog.Mesh->bVisibleInReflectionCaptures = false;
            Fog.Mesh->bVisibleInRealTimeSkyCaptures = false;
            Fog.Mesh->bVisibleInReflections = false;
            Fog.Mesh->SetVisibleInRayTracing(false);
            Fog.Mesh->SetAffectDistanceFieldLighting(false);
            Fog.Mesh->SetAffectDynamicIndirectLighting(false);
            Fog.Mesh->SetReceivesDecals(false);
            Fog.Mesh->SetRenderInDepthPass(false);
            Fog.Mesh->SetTranslucentSortPriority(10);
        }
    }
    InvalidateEnvironment();
}

void ACinderBattlefield::InvalidateEnvironment()
{
    bEnvironmentInvalid = true;
    bTerrainSurfaceInvalid = true;
    LastFogCells.Reset();
    LastObstacleReveal.Reset();
}

void ACinderBattlefield::RefreshEnvironment()
{
    // Static terrain is submitted only after reset/load or a newly explored obstacle.
    uint32 GeometryHash = GetTypeHash(Simulation.config().map);
    TArray<uint8> Revealed;
    Revealed.Reserve(static_cast<int32>(Simulation.obstacles().size()));
    for (const auto& Obstacle : Simulation.obstacles())
    {
        GeometryHash = HashCombineFast(GeometryHash, GetTypeHash(Obstacle.center.x));
        GeometryHash = HashCombineFast(GeometryHash, GetTypeHash(Obstacle.center.y));
        GeometryHash = HashCombineFast(GeometryHash, GetTypeHash(Obstacle.half.x));
        GeometryHash = HashCombineFast(GeometryHash, GetTypeHash(Obstacle.half.y));
        Revealed.Add(Simulation.explored(0, Obstacle.center) ? 1 : 0);
    }
    if (!bEnvironmentInvalid && GeometryHash == ObstacleGeometryHash && Revealed == LastObstacleReveal) return;
    if (bEnvironmentInvalid)
    {
        Batches[0].Transforms.Reset();
        Batches[0].Transforms.Add(FTransform(FQuat::Identity, FVector(2400, 2400, -16), FVector(49, 49, 0.3f)));
        Batches[0].bDirty = true;
        if (FogPlaneBatch != INDEX_NONE)
        {
            FBatch& Fog = Batches[FogPlaneBatch];
            Fog.Transforms.Reset();
            Fog.Transforms.Add(FTransform(FQuat::Identity, FVector(2400, 2400, 2.5f), FVector(48, 48, 1)));
            Fog.bDirty = true;
        }
    }
    Batches[1].Transforms.Reset(); Batches[1].bDirty = true;
    for (int32 Index : RockBatchIndices) if (Index != INDEX_NONE)
    {
        Batches[Index].Transforms.Reset(); Batches[Index].bDirty = true;
    }
    int32 ObstacleIndex = 0;
    for (const auto& Obstacle : Simulation.obstacles())
    {
        const int32 Seed = ObstacleIndex++;
        if (!Revealed[Seed] || (Scenery && Scenery->IsInitialized())) continue;
        const bool bLongX = Obstacle.half.x >= Obstacle.half.y;
        const float LongSize = 2 * (bLongX ? Obstacle.half.x : Obstacle.half.y);
        const int32 Segments = FMath::Clamp(FMath::CeilToInt(LongSize / 430.0f), 1, 4);
        for (int32 Segment = 0; Segment < Segments; ++Segment)
        {
            const int32 Variant = (Seed * 3 + Segment) % 4;
            const int32 BatchIndex = RockBatchIndices.IsValidIndex(Variant) ? RockBatchIndices[Variant] : INDEX_NONE;
            const float Width = Obstacle.half.x * 2 / (bLongX ? Segments : 1);
            const float Depth = Obstacle.half.y * 2 / (bLongX ? 1 : Segments);
            const float Offset = (Segment + 0.5f) * LongSize / Segments - LongSize * 0.5f;
            const FVector Position(Obstacle.center.x + (bLongX ? Offset : 0), Obstacle.center.y + (bLongX ? 0 : Offset), -0.5f);
            const float Height = FMath::Clamp(FMath::Min(Width, Depth) * (0.48f + 0.06f * ((Seed + Segment) % 4)), 70.0f, 195.0f);
            if (BatchIndex == INDEX_NONE)
            {
                Batches[1].Transforms.Add(FTransform(FQuat::Identity, Position + FVector(0, 0, Height * 0.5f), FVector(Width / 100, Depth / 100, Height / 100)));
                continue;
            }
            // Quarter-turn variation preserves the exact authored blocked rectangle.
            const int32 QuarterTurn = (Seed + Segment * 3) % 4;
            const FVector Scale(QuarterTurn % 2 ? Depth / 100 : Width / 100,
                                QuarterTurn % 2 ? Width / 100 : Depth / 100, Height / 100);
            Batches[BatchIndex].Transforms.Add(FTransform(FRotator(0, QuarterTurn * 90.0f, 0), Position, Scale));
        }
    }
    LastObstacleReveal = MoveTemp(Revealed);
    ObstacleGeometryHash = GeometryHash;
    bEnvironmentInvalid = false;
}

void ACinderBattlefield::RefreshTerrainSurface()
{
    if (!GroundSurfaceMaterial || !FApp::CanEverRender()) return;
    CinderTerrainSurface::FFeatures Features;
    Features.Map = Simulation.config().map;
    uint32 Hash = GetTypeHash(Features.Map);
    for (const auto& Cliff : Simulation.obstacles())
    {
        if (!Simulation.explored(0, Cliff.center)) continue;
        Features.Cliffs.Add(Cliff);
        Hash = HashCombineFast(Hash, GetTypeHash(Cliff.center.x));
        Hash = HashCombineFast(Hash, GetTypeHash(Cliff.center.y));
        Hash = HashCombineFast(Hash, GetTypeHash(Cliff.half.x));
        Hash = HashCombineFast(Hash, GetTypeHash(Cliff.half.y));
    }
    for (const auto& Ore : ResourceMemory)
    {
        Features.Minerals.Add(Ore.pos);
        Hash = HashCombineFast(Hash, GetTypeHash(Ore.id));
        Hash = HashCombineFast(Hash, GetTypeHash(Ore.pos.x));
        Hash = HashCombineFast(Hash, GetTypeHash(Ore.pos.y));
    }
    if (!bTerrainSurfaceInvalid && GroundSurfaceTexture && Hash == TerrainSurfaceHash) return;
    constexpr int32 Size = CinderTerrainSurface::TextureSize;
    auto Upload = MakeShared<FFogTextureUpload, ESPMode::ThreadSafe>();
    static_assert(Size == FogTextureSize, "shared upload region must match terrain dimensions");
    CinderTerrainSurface::BuildPixels(Features, Upload->Pixels);
    if (!GroundSurfaceTexture)
    {
        GroundSurfaceTexture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, NAME_None, Upload->Pixels);
        if (!GroundSurfaceTexture) return;
        GroundSurfaceTexture->SRGB = false;
        GroundSurfaceTexture->NeverStream = true;
        GroundSurfaceTexture->Filter = TF_Bilinear;
        GroundSurfaceTexture->AddressX = GroundSurfaceTexture->AddressY = TA_Clamp;
        GroundSurfaceTexture->UpdateResource();
        GroundSurfaceMaterial->SetTextureParameterValue(TEXT("TerrainLayers"), GroundSurfaceTexture);
    }
    else
    {
        if (!GroundSurfaceTexture->GetResource()) return;
        GroundSurfaceTexture->UpdateTextureRegions(0, 1, &Upload->Region, Size * 4, 4, Upload->Pixels.GetData(),
            [Upload](uint8*, const FUpdateTextureRegion2D*) { (void)Upload; });
    }
    TerrainSurfaceHash = Hash;
    bTerrainSurfaceInvalid = false;
    ++TerrainSurfaceUploads;
}

void ACinderBattlefield::UpdateFogTexture()
{
    constexpr int32 Cells = cinder::Simulation::FogSize;
    constexpr float CellSize = cinder::Simulation::WorldSize / Cells;
    TArray<uint8> Current;
    Current.SetNumUninitialized(Cells * Cells);
    for (int32 Y = 0; Y < Cells; ++Y) for (int32 X = 0; X < Cells; ++X)
    {
        const cinder::Vec2 Point{(X + 0.5f) * CellSize, (Y + 0.5f) * CellSize};
        Current[Y * Cells + X] = Simulation.visible(0, Point) ? 2 : Simulation.explored(0, Point) ? 1 : 0;
    }
    if (FogPlaneBatch == INDEX_NONE)
    {
        // Missing optional material and headless tests retain the original grid adapter.
        for (int32 Y = 0; Y < Cells; ++Y) for (int32 X = 0; X < Cells; ++X)
            if (Current[Y * Cells + X] != 2)
                Batches[Current[Y * Cells + X] ? 4 : 3].Transforms.Add(FTransform(FQuat::Identity,
                    FVector((X + 0.5f) * CellSize, (Y + 0.5f) * CellSize, 1), FVector(CellSize / 100 + 0.001f, CellSize / 100 + 0.001f, 0.02f)));
        return;
    }
    if (Current == LastFogCells || !FogTexture || !FogTexture->GetResource()) return;
    auto Upload = MakeShared<FFogTextureUpload, ESPMode::ThreadSafe>();
    Upload->Pixels.SetNumUninitialized(FogTextureSize * FogTextureSize * 4);
    for (int32 Y = 0; Y < FogTextureSize; ++Y) for (int32 X = 0; X < FogTextureSize; ++X)
    {
        const int32 CX = X / FogPixelsPerCell, CY = Y / FogPixelsPerCell;
        const uint8 State = Current[CY * Cells + CX];
        float Opacity = 1;
        if (State == 2)
        {
            float Distance = 4;
            for (int32 NY = CY - 1; NY <= CY + 1; ++NY) for (int32 NX = CX - 1; NX <= CX + 1; ++NX)
            {
                if (NX >= 0 && NY >= 0 && NX < Cells && NY < Cells && Current[NY * Cells + NX] == 2) continue;
                const float DX = FMath::Max(0.0f, FMath::Max(NX * FogPixelsPerCell - (X + 0.5f), (X + 0.5f) - (NX + 1) * FogPixelsPerCell));
                const float DY = FMath::Max(0.0f, FMath::Max(NY * FogPixelsPerCell - (Y + 0.5f), (Y + 0.5f) - (NY + 1) * FogPixelsPerCell));
                Distance = FMath::Min(Distance, FMath::Sqrt(DX * DX + DY * DY));
            }
            // Guard texels alongside hidden cells remain fully opaque, including corners.
            // Bilinear filtering therefore cannot uncover ground outside observed cells.
            const float T = FMath::Clamp((Distance - 0.75f) / 2.25f, 0.0f, 1.0f);
            Opacity = 1 - T * T * (3 - 2 * T);
        }
        const int32 Pixel = (Y * FogTextureSize + X) * 4;
        Upload->Pixels[Pixel] = 0;
        Upload->Pixels[Pixel + 1] = State ? 255 : 0;
        Upload->Pixels[Pixel + 2] = static_cast<uint8>(FMath::RoundToInt(Opacity * 255));
        Upload->Pixels[Pixel + 3] = 255;
    }
    // Shared ownership survives both render/RHI queues. If UE rejects the update,
    // destruction of its cleanup function still releases the region and pixel buffer.
    FogTexture->UpdateTextureRegions(0, 1, &Upload->Region, FogTextureSize * 4, 4, Upload->Pixels.GetData(),
        [Upload](uint8*, const FUpdateTextureRegion2D*) { (void)Upload; });
    ++FogTextureUploads;
    LastFogCells = MoveTemp(Current);
}

void ACinderBattlefield::AddBuildingPad(const cinder::Entity& Entity)
{
    if (PadBatch == INDEX_NONE || (Entity.team == 0 && Entity.progress >= 1 && Scenery && Scenery->IsInitialized())) return;
    const float Diameter = cinder::definition(Entity.kind).radius * 2;
    // A single shallow rectangular service slab grounds the silhouette without a second ring.
    Batches[PadBatch].Transforms.Add(FTransform(FRotator(0, FMath::RadiansToDegrees(Entity.facing), 0),
        FVector(Entity.pos.x, Entity.pos.y, -0.5f), FVector(Diameter * 1.08f / 100, Diameter * 1.08f / 100, 0.03f)));
}

void ACinderBattlefield::BeginPlay()
{
    Super::BeginPlay();
    BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Generated/M_CinderTint.M_CinderTint"));
    if (!BaseMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cinderline generated material missing. Run scripts/unreal.sh bootstrap before playing."));
        BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    }
    AddBatch(Cube, FLinearColor(0.065f, 0.12f, 0.13f), false, false);
    auto* GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/VisualTarget/Materials/M_CinderGroundV4.M_CinderGroundV4"), nullptr, LOAD_NoWarn);
    if (!GroundMaterial) GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_CinderGroundV3.M_CinderGroundV3"), nullptr, LOAD_NoWarn);
    if (GroundMaterial)
    {
        GroundSurfaceMaterial = UMaterialInstanceDynamic::Create(GroundMaterial, this);
        GroundMaterial = GroundSurfaceMaterial;
    }
    if (!GroundMaterial) GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_CinderGroundV2.M_CinderGroundV2"), nullptr, LOAD_NoWarn);
    if (!GroundMaterial) GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Generated/M_CinderGround.M_CinderGround"), nullptr, LOAD_NoWarn);
    if (GroundMaterial)
        Batches[0].Mesh->SetMaterial(0, GroundMaterial);
    AddBatch(Cube, FLinearColor(0.14f, 0.21f, 0.22f), true, false);
    AddBatch(Cone, FLinearColor(1.0f, 0.52f, 0.12f), true);
    AddBatch(Cube, FLinearColor(0.012f, 0.022f, 0.035f));
    AddBatch(Cube, FLinearColor(0.034f, 0.067f, 0.080f));
    for (int Team = 0; Team < 2; ++Team)
    {
        const FLinearColor Color = Team == 0 ? FLinearColor(0.04f, 0.82f, 0.72f) : FLinearColor(0.96f, 0.24f, 0.17f);
        for (UStaticMesh* Shape : { Cube.Get(), Cylinder.Get(), Cone.Get(), Sphere.Get() }) AddBatch(Shape, Color, true);
    }
    for (int Team = 0; Team < 2; ++Team)
        for (UStaticMesh* Shape : { Cube.Get(), Cylinder.Get(), Cone.Get(), Sphere.Get() })
            AddBatch(Shape, Team == 0 ? FLinearColor(0.68f, 1.0f, 0.92f) : FLinearColor(1.0f, 0.68f, 0.28f), true);
    LoadModelBatches();
    InitializeEnvironment();
    Scenery->Initialize(RootComponent, GroundSurfaceMaterial);
    WorldEffects->Initialize(RootComponent, Sphere, Cylinder, Cone, Plane, BaseMaterial);

    auto* Sun = GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-42, -38, 0));
    auto* SunComponent = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
    SunComponent->SetMobility(EComponentMobility::Movable);
    SunComponent->SetIntensity(4.5f);
    SunComponent->SetLightColor(FLinearColor(1.0f, 0.84f, 0.66f));
    SunComponent->SetCastShadows(true);
    SunComponent->SetDynamicShadowDistanceMovableLight(6000);
    SunComponent->SetDynamicShadowCascades(PLATFORM_IOS ? 2 : 4);
    SunComponent->SetCascadeDistributionExponent(2.0f);
    SunComponent->SetShadowBias(0.25f);
    SunComponent->SetShadowSlopeBias(0.35f);
    auto* Sky = GetWorld()->SpawnActor<ASkyLight>();
    auto* SkyComponent = Sky->GetLightComponent();
    SkyComponent->SetMobility(EComponentMobility::Movable);
    SkyComponent->SetCastShadows(false);
    SkyComponent->bRealTimeCapture = false;
    if (AmbientCubemap)
    {
        // A fixed environment lights shaded faces without capturing the empty battlefield sky.
        SkyComponent->SourceType = SLS_SpecifiedCubemap;
        SkyComponent->SetCubemap(AmbientCubemap);
    }
    SkyComponent->SetIntensity(1.10f);
    SkyComponent->SetLightColor(FLinearColor(0.70f, 0.83f, 1.0f));
    Simulation.reset();
    ResetFeedback();
    RenderState();
}

void ACinderBattlefield::StartMatch(int MapIndex)
{
    if (bOnlineMatch) return;
    SetActorTickEnabled(true);
    CurrentMap = FMath::Clamp(MapIndex, 0, 2);
    cinder::Config Config; Config.map = CurrentMap;
    Simulation.reset(Config);
    ResetPresentation();
    bMenu = false; bPaused = false;
    RenderState();
}

void ACinderBattlefield::StartTutorial()
{
    if (bOnlineMatch) return;
    SetActorTickEnabled(true);
    CurrentMap = 0;
    cinder::Config Config;
    Config.map = CurrentMap; Config.seed = FCinderTutorial::Seed; Config.ai = false;
    Simulation.reset(Config);
    ResetPresentation();
    // A stationary opponent gives the last lesson a real target with ordinary
    // health and combat rules. It cannot leave its position to raid the learner.
    const cinder::Id Target = Simulation.debugSpawn(cinder::Kind::Worker, 1, FCinderTutorial::CombatPoint());
    cinder::Command Hold;
    Hold.type = cinder::CommandType::Hold; Hold.team = 1; Hold.units = {Target};
    Simulation.command(Hold);
    Training.Start(Simulation, Target);
    bMenu = false; bPaused = false;
    RenderState();
}

bool ACinderBattlefield::StartOnlineMatch(const cinder::net::Snapshot& Snapshot)
{
    if (!Simulation.applySnapshot(Snapshot)) return false;
    SetActorTickEnabled(true);
    CurrentMap = Snapshot.config.map;
    ResetPresentation();
    bOnlineMatch = true; bMenu = bPaused = false;
    OnlineSnapshotSerial = OnlinePoseSerial = 0;
    OnlineSnapshotAt = FPlatformTime::Seconds(); OnlineSnapshotInterval = 0.1f;
    PreviousOnlinePositions.Reset();
    // The server includes only remembered neutral resources, safe on reconnect.
    for (const auto& Entity : Simulation.entities())
        if (Entity.kind == cinder::Kind::Resource) ResourceMemory.push_back(Entity);
    RenderState();
    return true;
}

cinder::CommandResult ACinderBattlefield::SubmitCommand(const cinder::Command& Command)
{
    if (!bOnlineMatch) return Simulation.command(Command);
    auto* Online = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCinderOnlineSubsystem>() : nullptr;
    const bool bSent = Online && Online->SendCommand(Command);
    return {bSent, bSent ? "Order sent. Waiting for the server." : "Connection unavailable. Wait for the match to reconnect."};
}

cinder::Vec2 ACinderBattlefield::RenderPosition(const cinder::Entity& Entity) const
{
    if (!bOnlineMatch || cinder::definition(Entity.kind).building || Entity.kind == cinder::Kind::Resource) return Entity.pos;
    const cinder::Vec2* Previous = PreviousOnlinePositions.Find(Entity.id);
    if (!Previous || (Entity.team != 0 && !Simulation.visible(0, *Previous))) return Entity.pos;
    const float Alpha = FMath::Clamp(static_cast<float>((FPlatformTime::Seconds() - OnlineSnapshotAt) / OnlineSnapshotInterval), 0.0f, 1.0f);
    return {FMath::Lerp(Previous->x, Entity.pos.x, Alpha), FMath::Lerp(Previous->y, Entity.pos.y, Alpha)};
}

void ACinderBattlefield::ReturnToMenu()
{
    SetActorTickEnabled(true);
    bMenu = true; bPaused = false;
    Training.Reset();
    bOnlineMatch = false;
    PreviousOnlinePositions.Reset();
    // Keep the completed match counters available for diagnostics while skipping stale effects.
    ResetFeedback(false);
    WorldEffects->Reset(Simulation.lastEffectId());
}

bool ACinderBattlefield::HasWorldEffects() const
{
    return WorldEffects && WorldEffects->IsInitialized();
}

void ACinderBattlefield::SetPaused(bool Value)
{
    if (bPaused == Value) return;
    bPaused = Value;
    if (bPaused && !bMenu)
    {
        // Simulation steps and render submissions have independent accumulators.
        // Freeze the latest pose/fog, even if its render interval was not due yet.
        RenderTimer = 0;
        RenderState();
    }
}

void ACinderBattlefield::ResetPresentation()
{
    Training.Reset();
    InvalidateEnvironment();
    ResourceMemory.clear();
    Scenery->Reset();
    WorldEffects->Reset(Simulation.lastEffectId());
    ResetFeedback();
}

void ACinderBattlefield::ResetFeedback(bool bClearCombatCounters)
{
    AudioStatsSnapshot = Simulation.players()[0].stats;
    EntityMotion.Reset();
    if (bClearCombatCounters) CombatFeedback = FCinderCombatFeedbackStats{};
    CombatFeedback.ProcessedHighWater = Simulation.lastEffectId();
}

void ACinderBattlefield::UpdateCombatFeedback()
{
    const uint64 LastEffectId = Simulation.lastEffectId();
    if (LastEffectId < CombatFeedback.ProcessedHighWater)
    {
        // Public Sim() permits development fixtures to replace a match directly.
        // An explicit ResetFeedback remains necessary when a replacement has a higher ID.
        ResetFeedback(false);
        return;
    }
    if (LastEffectId == CombatFeedback.ProcessedHighWater) return;
    const uint64 PreviousHighWater = CombatFeedback.ProcessedHighWater;

    int32 ViewportWidth = 0, ViewportHeight = 0;
    APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (Player && Player->IsLocalController()) Player->GetViewportSize(ViewportWidth, ViewportHeight);
    const bool bHasViewport = Player && ViewportWidth > 0 && ViewportHeight > 0;
    auto InViewport = [&](const cinder::Effect& Effect, bool bSource)
    {
        // Transient world tests have no local viewport. Fog visibility still applies there.
        if (!bHasViewport) return true;
        const cinder::Vec2 Point = bSource ? Effect.from : Effect.to;
        const cinder::Kind Kind = bSource ? Effect.sourceKind : Effect.targetKind;
        FVector2D Screen;
        return Player->ProjectWorldLocationToScreen(FVector(Point.x, Point.y, cinder::definition(Kind).air ? 125 : 25), Screen)
            && Screen.X >= 0 && Screen.Y >= 0 && Screen.X < ViewportWidth && Screen.Y < ViewportHeight;
    };

    bool bDeath = false, bWeapon = false, bImpact = false;
    uint64 AudibleEvents = 0;
    for (const cinder::Effect& Effect : Simulation.effects())
    {
        if (Effect.id <= PreviousHighWater || Effect.id > LastEffectId) continue;
        ++CombatFeedback.ConsumedEvents;
        // Healing has its own visual treatment, but there is no suitable healing audio asset.
        if (Effect.type == cinder::EffectType::Heal) continue;
        const bool bSource = Effect.type == cinder::EffectType::Weapon;
        if (!Simulation.effectVisible(Effect, 0, bSource))
        {
            ++CombatFeedback.HiddenEvents;
            continue;
        }
        if (!InViewport(Effect, bSource))
        {
            ++CombatFeedback.OffscreenEvents;
            continue;
        }
        bool* Pending = nullptr;
        switch (Effect.type)
        {
        case cinder::EffectType::Weapon: Pending = &bWeapon; break;
        case cinder::EffectType::Impact: Pending = &bImpact; break;
        case cinder::EffectType::Death: Pending = &bDeath; break;
        default: break;
        }
        if (!Pending) continue;
        ++AudibleEvents;
        *Pending = true;
    }
    // Consume hidden, offscreen, expired and coalesced IDs too. A later reveal,
    // camera move or audio-device recovery must never replay an old event.
    CombatFeedback.ProcessedHighWater = LastEffectId;
    int32 Requested = 0;
    auto Submit = [&](bool bPending, ECinderCue Cue, uint64& Counter)
    {
        if (!bPending) return;
        ++Requested;
        ++Counter;
        UCinderAudioSubsystem::Play(this, Cue);
    };
    // Global 2D cues need one request per type, at most three per update. Mass fire
    // cannot starve impacts or deaths; the subsystem retains its per-cue time throttle.
    Submit(bDeath, ECinderCue::Explosion, CombatFeedback.DeathRequests);
    Submit(bWeapon, ECinderCue::Weapon_Pulse, CombatFeedback.WeaponRequests);
    Submit(bImpact, ECinderCue::Impact, CombatFeedback.ImpactRequests);
    CombatFeedback.CoalescedEvents += AudibleEvents - static_cast<uint64>(Requested);
}

void ACinderBattlefield::LogCombatStatus() const
{
    UE_LOG(LogCinderCombat, Display,
        TEXT("CINDERLINE_COMBAT_FEEDBACK highwater=%llu consumed=%llu weapon_requests=%llu impact_requests=%llu death_requests=%llu hidden=%llu offscreen=%llu coalesced=%llu; requests are adapter intents, not playback."),
        CombatFeedback.ProcessedHighWater, CombatFeedback.ConsumedEvents, CombatFeedback.WeaponRequests,
        CombatFeedback.ImpactRequests, CombatFeedback.DeathRequests, CombatFeedback.HiddenEvents,
        CombatFeedback.OffscreenEvents, CombatFeedback.CoalescedEvents);
    UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
    const UCinderAudioSubsystem* Audio = Instance ? Instance->GetSubsystem<UCinderAudioSubsystem>() : nullptr;
    if (!Audio)
    {
        UE_LOG(LogCinderCombat, Display, TEXT("CINDERLINE_COMBAT_AUDIO no game-instance subsystem; adapter requests above still count."));
        return;
    }
    const FCinderCueDiagnostics Weapon = Audio->CueDiagnostics(ECinderCue::Weapon_Pulse);
    const FCinderCueDiagnostics Impact = Audio->CueDiagnostics(ECinderCue::Impact);
    const FCinderCueDiagnostics Death = Audio->CueDiagnostics(ECinderCue::Explosion);
    UE_LOG(LogCinderCombat, Display,
        TEXT("CINDERLINE_COMBAT_AUDIO weapon_submitted=%llu impact_submitted=%llu explosion_submitted=%llu weapon_throttled=%llu impact_throttled=%llu explosion_throttled=%llu; submitted counts lifetime PlaySound2D calls, not listening proof."),
        Weapon.Submitted, Impact.Submitted, Death.Submitted, Weapon.Throttled, Impact.Throttled, Death.Throttled);
}

void ACinderBattlefield::UpdateCompletionAudio()
{
    const auto& Stats = Simulation.players()[0].stats;
    if (Stats.produced > AudioStatsSnapshot.produced)
        UCinderAudioSubsystem::Play(this, ECinderCue::Unit_Ready);
    if (Stats.built > AudioStatsSnapshot.built)
        UCinderAudioSubsystem::Play(this, ECinderCue::Building_Ready);
    AudioStatsSnapshot = Stats;
}

void ACinderBattlefield::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bOnlineMatch)
    {
        auto* Online = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCinderOnlineSubsystem>() : nullptr;
        bool bNewSnapshot = false;
        if (Online && Online->LatestSnapshot() && Online->SnapshotSerial() != OnlineSnapshotSerial)
        {
            const auto& Snapshot = *Online->LatestSnapshot();
            TMap<cinder::Id, cinder::Vec2> OldPositions;
            for (const auto& Entity : Simulation.entities()) OldPositions.Add(Entity.id, RenderPosition(Entity));
            const uint64 PreviousTick = Simulation.tick();
            if (Simulation.applySnapshot(Snapshot))
            {
                PreviousOnlinePositions = MoveTemp(OldPositions);
                OnlineSnapshotInterval = FMath::Clamp(static_cast<float>(Snapshot.tick > PreviousTick ? Snapshot.tick - PreviousTick : 2) * cinder::Simulation::Step, 0.05f, 0.25f);
                OnlineSnapshotAt = FPlatformTime::Seconds();
                OnlineSnapshotSerial = Online->SnapshotSerial(); bNewSnapshot = true;
                ResourceMemory.clear();
                for (const auto& Entity : Simulation.entities())
                    if (Entity.kind == cinder::Kind::Resource) ResourceMemory.push_back(Entity);
                if (bPaused)
                {
                    // Reading menus does not pause authority or accumulate old combat cues.
                    AudioStatsSnapshot = Simulation.players()[0].stats;
                    ResetFeedback(false);
                }
                else { UpdateCompletionAudio(); UpdateCombatFeedback(); }
            }
        }
        if (bPaused && Simulation.winner() < 0) return;
        RenderTimer += DeltaSeconds;
        const bool bInterpolating = FPlatformTime::Seconds() - OnlineSnapshotAt < OnlineSnapshotInterval;
        if (bNewSnapshot || (bInterpolating && RenderTimer >= cinder::Simulation::Step))
        { RenderTimer = 0; RenderState(); }
        return; // A network replica must never run the authoritative simulation loop.
    }
    // Start/load explicitly populate the scene. Frozen matches need neither new
    // simulation poses nor repeated fog scans and instance comparisons.
    if (bMenu || bPaused || Simulation.winner() >= 0) return;
    Simulation.update(FMath::Min(DeltaSeconds, 0.2f));
    UpdateCompletionAudio();
    UpdateCombatFeedback();
    RenderTimer += DeltaSeconds;
    // Always submit the terminal state before subsequent ticks become idle.
    if (RenderTimer >= cinder::Simulation::Step || Simulation.winner() >= 0)
    { RenderTimer = 0; RenderState(); }
}

void ACinderBattlefield::AddEntity(const cinder::Entity& AuthoritativeEntity)
{
    using namespace cinder;
    const cinder::Entity& Entity = AuthoritativeEntity;
    const cinder::Vec2 RenderPoint = RenderPosition(AuthoritativeEntity);
    const Definition& Def = definition(Entity.kind);
    const bool Resource = Entity.kind == Kind::Resource;
    if (Resource)
    {
        if (!Simulation.explored(0, RenderPoint) || Entity.resource <= 0) return;
    }
    else if (Entity.team != 0 && !Simulation.visible(0, RenderPoint)) return;
    FCinderEntityPose Pose;
    if (!Resource && !Def.building)
    {
        FCinderMotionObservation Observation;
        Observation.Id = Entity.id; Observation.Kind = Entity.kind; Observation.Position = RenderPoint;
        Observation.Order = Entity.order; Observation.Facing = Entity.facing;
        Observation.Cooldown = Entity.cooldown; Observation.HarvestTimer = Entity.harvestTimer;
        Observation.bReturning = Entity.returning;
        Observation.bConstructionActive = Entity.order == Order::Construct && Simulation.constructionActive(Entity.target);
        Observation.Tick = bOnlineMatch ? OnlinePoseSerial : Simulation.tick();
        Observation.Time = bOnlineMatch ? Simulation.time() - OnlineSnapshotInterval + FMath::Clamp(static_cast<float>(FPlatformTime::Seconds() - OnlineSnapshotAt), 0.0f, OnlineSnapshotInterval) : Simulation.time();
        Pose = EntityMotion.Observe(Observation);
    }
    // Cosmetic transforms follow simulation time; they cannot move the authoritative XY root.
    const bool bGroundHover = Entity.kind == Kind::Scout || Entity.kind == Kind::Mender;
    const float Elevation = (Def.air ? 125.0f : bGroundHover ? 8.0f : 0.0f) + Pose.BodyZ;
    if (Def.building && Simulation.visible(0, RenderPoint)) AddBuildingPad(Entity);
    const float BuildScale = Def.building ? FMath::Max(0.08f, Entity.progress) : 1;
    const FQuat Facing = FRotator(0, FMath::RadiansToDegrees(Entity.facing), 0).Quaternion();
    const int32 ModelKey = static_cast<int32>(Entity.kind) * 2 + (Resource ? 0 : Entity.team);
    if (ModelBatchIndices.IsValidIndex(ModelKey) && ModelBatchIndices[ModelKey] != INDEX_NONE)
    {
        const int32 KindIndex = static_cast<int32>(Entity.kind);
        if (MotionKindAvailable.IsValidIndex(KindIndex) && MotionKindAvailable[KindIndex])
        {
            const FVector Root(RenderPoint.x, RenderPoint.y, Elevation);
            const auto Parts = CinderMotionAssetParts(Entity.kind);
            for (const FCinderMotionAssetPart& Part : Parts)
            {
                const int32 Key = ModelKey * static_cast<int32>(ECinderMotionPart::Count) + static_cast<int32>(Part.Part);
                if (!MotionPartBatchIndices.IsValidIndex(Key) || MotionPartBatchIndices[Key] == INDEX_NONE) continue;
                Batches[MotionPartBatchIndices[Key]].Transforms.Add(CinderPartWorldTransform(
                    Root, Facing, Part.Pivot, Pose.Rotation(Part.Part), Pose.Offset(Part.Part)));
            }
            ++LastModelEntities;
            return;
        }
        // The imported centimeter mesh is already sized to the definition and has a bottom pivot.
        Batches[ModelBatchIndices[ModelKey]].Transforms.Add(FTransform(Facing * Pose.BodyRotation.Quaternion(),
            FVector(RenderPoint.x, RenderPoint.y, Elevation), FVector(1, 1, BuildScale)));
        ++LastModelEntities;
        return;
    }
    ++LastFallbackEntities;
    if (Entity.kind == Kind::Resource)
    {
        for (int I = 0; I < 3; ++I)
        {
            const float A = I * 2.0944f;
            Batches[2].Transforms.Add(FTransform(FRotator(0, I * 120, I * 8), FVector(RenderPoint.x + FMath::Cos(A) * 15, RenderPoint.y + FMath::Sin(A) * 15, 34), FVector(0.25f, 0.3f, 0.65f + I * 0.1f)));
        }
        return;
    }
    const float R = Def.radius / 50.0f;
    const FQuat PosedFacing = Facing * Pose.BodyRotation.Quaternion();
    auto Part = [&](int Shape, FVector Offset, FVector Scale, bool Accent = false, FRotator Rotation = FRotator::ZeroRotator)
    {
        if (Def.building) { Offset.X *= 0.5; Offset.Y *= 0.5; Scale.X *= 0.5; Scale.Y *= 0.5; }
        Offset.Z *= BuildScale; Scale.Z *= BuildScale;
        const FVector Position(RenderPoint.x, RenderPoint.y, Elevation);
        Batches[5 + Entity.team * 4 + Shape + (Accent ? 8 : 0)].Transforms.Add(FTransform(PosedFacing * Rotation.Quaternion(), Position + PosedFacing.RotateVector(Offset), Scale));
    };
    switch (Entity.kind)
    {
    case Kind::Worker:
        Part(3, FVector(0, 0, 20), FVector(R, R, 0.35f));
        Part(0, FVector(12, 0, 34), FVector(0.2f, 0.28f, 0.15f), true);
        Part(1, FVector(-10, -15, 9), FVector(0.14f, 0.14f, 0.18f));
        Part(1, FVector(-10, 15, 9), FVector(0.14f, 0.14f, 0.18f)); break;
    case Kind::Striker:
        Part(1, FVector(0, 0, 24), FVector(R * 0.7f, R * 0.7f, 0.44f));
        Part(3, FVector(0, 0, 48), FVector(0.24f), true);
        Part(0, FVector(20, 0, 34), FVector(0.4f, 0.10f, 0.12f), true); break;
    case Kind::Lancer:
        Part(0, FVector(0, 0, 25), FVector(R, R * 0.85f, 0.48f));
        Part(2, FVector(0, 0, 59), FVector(0.34f, 0.34f, 0.38f), true);
        Part(0, FVector(30, 0, 39), FVector(0.62f, 0.12f, 0.13f), true); break;
    case Kind::Scout:
        Part(2, FVector(0, 0, 22), FVector(0.36f, 0.44f, 0.8f), false, FRotator(90, 0, 0));
        Part(3, FVector(-12, 0, 34), FVector(0.23f), true); break;
    case Kind::Bastion:
        Part(0, FVector(0, 0, 25), FVector(R * 1.6f, R, 0.45f));
        Part(1, FVector(0, 0, 55), FVector(R * 0.8f, R * 0.8f, 0.32f), true);
        Part(0, FVector(40, 0, 64), FVector(0.9f, 0.14f, 0.16f), true); break;
    case Kind::Mortar:
        Part(0, FVector(0, 0, 20), FVector(R * 1.4f, R * 1.25f, 0.36f));
        Part(1, FVector(7, 0, 53), FVector(0.24f, 0.24f, 0.9f), true, FRotator(35, 0, 0)); break;
    case Kind::Mender:
        Part(1, FVector(0, 0, 25), FVector(R, R, 0.28f));
        Part(3, FVector(0, 0, 54), FVector(0.3f), true);
        Part(0, FVector(0, 0, 54), FVector(0.65f, 0.12f, 0.1f), true);
        Part(0, FVector(0, 0, 54), FVector(0.12f, 0.65f, 0.1f), true); break;
    case Kind::Kite:
        Part(2, FVector::ZeroVector, FVector(R * 1.4f, R * 2.2f, 0.25f));
        Part(0, FVector(12, 0, 8), FVector(0.8f, 0.14f, 0.16f), true);
        Part(3, FVector(-18, -32, -6), FVector(0.22f), true);
        Part(3, FVector(-18, 32, -6), FVector(0.22f), true); break;
    case Kind::Headquarters:
        Part(1, FVector(0, 0, 22), FVector(R * 2, R * 2, 0.4f));
        Part(0, FVector(0, 0, 69), FVector(R * 1.25f, R * 1.25f, 0.9f));
        Part(2, FVector(0, 0, 146), FVector(R * 0.8f, R * 0.8f, 0.9f), true);
        Part(0, FVector(0, 0, 108), FVector(R * 1.65f, 0.2f, 0.2f), true); break;
    case Kind::Processor:
        Part(1, FVector(0, 0, 24), FVector(R * 1.8f, R * 1.8f, 0.45f));
        Part(2, FVector(0, 0, 87), FVector(R, R, 1.0f), true);
        Part(0, FVector(0, 0, 42), FVector(R * 2, 0.3f, 0.2f), true); break;
    case Kind::Foundry:
        Part(0, FVector(0, 0, 40), FVector(R * 1.7f, R * 1.6f, 0.8f));
        Part(0, FVector(0, -30, 92), FVector(R * 1.5f, 0.18f, 0.25f), true);
        Part(0, FVector(0, 30, 92), FVector(R * 1.5f, 0.18f, 0.25f), true);
        Part(1, FVector(-35, 0, 100), FVector(0.3f, 0.3f, 0.7f), true); break;
    case Kind::MotorPool:
        Part(0, FVector(0, 0, 32), FVector(R * 1.9f, R * 1.6f, 0.6f));
        Part(1, FVector(0, -38, 75), FVector(0.4f, 0.4f, 1.35f), true, FRotator(90, 0, 0));
        Part(1, FVector(0, 38, 75), FVector(0.4f, 0.4f, 1.35f), true, FRotator(90, 0, 0)); break;
    case Kind::Laboratory:
        Part(1, FVector(0, 0, 28), FVector(R * 1.8f, R * 1.8f, 0.5f));
        Part(0, FVector(0, 0, 95), FVector(0.28f, 0.28f, 1.4f));
        Part(3, FVector(0, 0, 165), FVector(0.85f), true);
        Part(0, FVector(0, 0, 125), FVector(R * 1.55f, 0.13f, 0.14f), true); break;
    case Kind::Turret:
        Part(1, FVector(0, 0, 23), FVector(R * 1.7f, R * 1.7f, 0.45f));
        Part(0, FVector(0, 0, 65), FVector(0.3f, 0.3f, 0.8f));
        Part(0, FVector(15, -13, 110), FVector(0.75f, 0.13f, 0.16f), true);
        Part(0, FVector(15, 13, 110), FVector(0.75f, 0.13f, 0.16f), true); break;
    default: break;
    }
}

void ACinderBattlefield::RenderState()
{
    if (bOnlineMatch) ++OnlinePoseSerial;
    LastModelEntities = LastFallbackEntities = 0;
    for (FBatch& Batch : Batches) if (Batch.bDynamic) Batch.Transforms.Reset();
    if (Batches.IsEmpty()) return;
    RefreshEnvironment();
    RefreshTerrainSurface();
    UpdateFogTexture();
    EntityMotion.BeginFrame();
    for (const auto& Entity : Simulation.entities())
    {
        if (Entity.kind == cinder::Kind::Resource)
        {
            // Cache only observed resource state: unseen harvesting must not leak through visuals.
            if (Simulation.visible(0, Entity.pos))
            {
                const auto It = std::find_if(ResourceMemory.begin(), ResourceMemory.end(), [&](const cinder::Entity& E) { return E.id == Entity.id; });
                if (It == ResourceMemory.end()) ResourceMemory.push_back(Entity); else *It = Entity;
            }
        }
        else if (Entity.alive()) AddEntity(Entity);
    }
    for (const auto& Resource : ResourceMemory) AddEntity(Resource);
    EntityMotion.EndFrame();
    Scenery->Update(Simulation, ResourceMemory);
    WorldEffects->Update(Simulation, 0);
    FlushBatches();
}

void ACinderBattlefield::FlushBatches()
{
    ++InstanceUploads.Passes;
    for (FBatch& Batch : Batches)
    {
        if (!Batch.bDynamic && !Batch.bDirty)
        {
            ++InstanceUploads.StaticSkips;
            continue;
        }
        const int32 OldCount = Batch.Mesh->GetInstanceCount();
        const int32 NewCount = Batch.Transforms.Num();
        const bool bSnapshotMatchesCount = Batch.SubmittedTransforms.Num() == OldCount;
        auto SameTransform = [&](int32 Index)
        {
            // Zero tolerance skips only identical presentation poses; slow movement is retained.
            return Batch.Transforms[Index].Equals(Batch.SubmittedTransforms[Index], 0.0);
        };
        bool bUnchanged = bSnapshotMatchesCount && OldCount == NewCount;
        for (int32 Index = 0; bUnchanged && Index < NewCount; ++Index) bUnchanged = SameTransform(Index);
        if (bUnchanged)
        {
            ++InstanceUploads.UnchangedSkips;
            Batch.bDirty = false;
            continue;
        }

        // Dense visible ordering stays exactly as generated by RenderState. On a removal,
        // trim only the tail, then overwrite changed retained indices with the new prefix.
        // A hidden/dead entity never survives in an old slot, even when the count is unchanged.
        bool bAccepted = bSnapshotMatchesCount;
        if (bAccepted && OldCount > NewCount)
        {
            TArray<int32> Tail;
            Tail.Reserve(OldCount - NewCount);
            for (int32 Index = OldCount - 1; Index >= NewCount; --Index) Tail.Add(Index);
            bAccepted = Batch.Mesh->RemoveInstances(Tail, true);
            if (bAccepted) InstanceUploads.Removed += Tail.Num();
        }
        if (bAccepted && NewCount > OldCount)
        {
            TArray<FTransform> Tail;
            Tail.Append(Batch.Transforms.GetData() + OldCount, NewCount - OldCount);
            Batch.Mesh->AddInstances(Tail, false, false, false);
            bAccepted = Batch.Mesh->GetInstanceCount() == NewCount;
            if (bAccepted) InstanceUploads.Added += Tail.Num();
        }
        const int32 CommonCount = FMath::Min(OldCount, NewCount);
        for (int32 Index = 0; bAccepted && Index < CommonCount;)
        {
            if (SameTransform(Index)) { ++Index; continue; }
            const int32 Start = Index++;
            while (Index < CommonCount && !SameTransform(Index)) ++Index;
            const int32 Count = Index - Start;
            // UE 5.8 TransformChanged already schedules MarkRenderInstancesDirty. Setting
            // bMarkRenderStateDirty would instead recreate the component's scene proxy.
            bAccepted = Batch.Mesh->BatchUpdateInstancesTransforms(Start,
                TArrayView<const FTransform>(Batch.Transforms.GetData() + Start, Count), false, false, true);
            if (bAccepted)
            {
                ++InstanceUploads.DeltaCalls;
                InstanceUploads.Transforms += Count;
            }
        }
        if (!bAccepted || Batch.Mesh->GetInstanceCount() != NewCount)
        {
            // Recovery for an externally altered component or a rejected engine operation.
            // Ordinary training, death, reveal, reset and load use the incremental path above.
            ++InstanceUploads.FullRebuilds;
            Batch.Mesh->ClearInstances();
            if (NewCount > 0) Batch.Mesh->AddInstances(Batch.Transforms, false, false, false);
        }
        if (Batch.Mesh->GetInstanceCount() == NewCount)
        {
            Batch.SubmittedTransforms = Batch.Transforms;
            Batch.bDirty = false;
        }
        else Batch.bDirty = true;
    }
}

bool ACinderBattlefield::SaveMatch() const
{
    // The menu can retain the last simulation for diagnostics, including practice.
    // Only a current skirmish may reach the persistent save slot.
    if (bMenu || bOnlineMatch || Training.IsActive()) return false;
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Matches");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*(Directory / TEXT("skirmish.cinder")));
    return Simulation.save(TCHAR_TO_UTF8(*Filename));
}

bool ACinderBattlefield::LoadMatch()
{
    if (bOnlineMatch) return false;
    const FString Filename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*(FPaths::ProjectSavedDir() / TEXT("Matches/skirmish.cinder")));
    // A completed online view is still a replica. Load offline state separately,
    // so a failed load preserves the current view and success restores local rules.
    cinder::Simulation Loaded;
    if (!Loaded.load(TCHAR_TO_UTF8(*Filename))) return false;
    Simulation = MoveTemp(Loaded);
    SetActorTickEnabled(true);
    ResetPresentation();
    CurrentMap = Simulation.config().map;
    bMenu = false; bPaused = false; RenderState(); return true;
}
