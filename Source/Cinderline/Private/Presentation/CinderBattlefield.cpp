#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderFogMask.h"
#include "Presentation/CinderGroundPalette.h"
#include "Presentation/CinderAudioSubsystem.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderTerrainSurface.h"
#include "Presentation/CinderLandscapeTerrain.h"
#include "Presentation/CinderScenery.h"
#include "Presentation/CinderTeamColors.h"
#include "Presentation/CinderWorldEffects.h"
#include "Presentation/CinderOnlineSubsystem.h"
#include "Presentation/CinderPlayerController.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
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
constexpr int32 TeamCount = CinderTeamColors::Count;
constexpr int32 FogTextureSize = 256;
constexpr int32 FogPixelsPerCell = FogTextureSize / cinder::Simulation::FogSize;
constexpr float BasicShapeSize = 100.0f;
constexpr float WorldBorderWidth = 3000.0f;
// The flat fog sheet's Z, and the reference height of the out-of-bounds skirt just above
// it. Only the generated flat-ground fallback still draws the sheet: a single plane 2.5 cm
// up cannot cover a map that has real hills in it, so on the canonical Landscape path the
// ground material carries fog itself and RenderSimState hides this plane. See there.
constexpr float WorldSurfaceZ = 2.5f;
static_assert(FogTextureSize == CinderFogMask::TextureSize, "fog upload must match the shared mask");
static_assert(TeamCount == cinder::Simulation::MaxPlayers, "The presentation palette must cover every simulation player");
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
// Hull tinting by battlefield role. Team colour stays the identity signal and
// lives in the TeamPanel and CoreGlow slots; these only shift the hull, so the
// two channels never compete. The values form a deliberate value ladder —
// aircraft lightest, structures darkest — so small units read against warm
// ground while large structures sit back and let the army read in front.
enum class ECinderModelRole : uint8 { Infantry, Vehicle, Air, Structure, Neutral, Count };
const ECinderModelRole ModelRoles[ModelCount] = {
    ECinderModelRole::Infantry,  // Worker
    ECinderModelRole::Infantry,  // Striker
    ECinderModelRole::Infantry,  // Lancer
    ECinderModelRole::Infantry,  // Scout
    ECinderModelRole::Vehicle,   // Bastion
    ECinderModelRole::Vehicle,   // Mortar
    ECinderModelRole::Vehicle,   // Mender
    ECinderModelRole::Air,       // Kite
    ECinderModelRole::Structure, // Headquarters
    ECinderModelRole::Structure, // Processor
    ECinderModelRole::Structure, // Foundry
    ECinderModelRole::Structure, // MotorPool
    ECinderModelRole::Structure, // Laboratory
    ECinderModelRole::Structure, // Turret
    ECinderModelRole::Neutral    // Resource
};
// One dark and one light hull tint per role, in ECinderModelRole order. HullDark
// covers most of every mesh, so the spread there has to be wide — a few
// hundredths apart is invisible on a 40-pixel unit. Hue moves with value:
// infantry warm steel, vehicles blue steel, aircraft pale sky, structures deep
// navy. Ore never uses Neutral in practice; it keeps the warm override below.
//
// The dark slot previously spanned 0.044 to 0.190, which is the bottom of the
// ramp where the tonemapper has almost no range left — every role resolved to
// the same near-black on a phone. The ladder now sits where the key light
// actually reaches, so the four roles separate by value before they separate by
// hue, and they keep separating after the thermal ladder zeroes bloom.
const FLinearColor RoleHullTints[static_cast<int32>(ECinderModelRole::Count)][2] = {
    { FLinearColor(0.115f, 0.135f, 0.165f), FLinearColor(0.66f, 0.70f, 0.74f) }, // Infantry
    { FLinearColor(0.070f, 0.105f, 0.150f), FLinearColor(0.42f, 0.52f, 0.62f) }, // Vehicle
    { FLinearColor(0.240f, 0.300f, 0.380f), FLinearColor(0.82f, 0.88f, 0.96f) }, // Air
    { FLinearColor(0.050f, 0.072f, 0.100f), FLinearColor(0.34f, 0.42f, 0.50f) }, // Structure
    { FLinearColor(0.045f, 0.065f, 0.075f), FLinearColor(0.42f, 0.50f, 0.52f) }  // Neutral
};
// Surface response per role. M_CinderModelV3 already evaluates a Fresnel rim on
// every unit pixel and then multiplies it by RimLight, which nothing has ever
// written — so the cost was being paid and the rim discarded. Writing it here
// costs no extra instruction and is the only thing that separates a unit from
// the ground once shadows are off at Minimum quality.
//
// Rim is inversely proportional to screen size on purpose: the smallest
// silhouettes need the most edge to survive MetalFX at 80% plus FXAA, while a
// structure with a large footprint would look wrapped in neon at the same value.
struct FRoleFinish { float Roughness, Metallic, Rim; };
const FRoleFinish RoleFinishes[static_cast<int32>(ECinderModelRole::Count)] = {
    { 0.52f, 0.08f, 0.60f }, // Infantry — small, needs the most edge
    { 0.38f, 0.35f, 0.50f }, // Vehicle — polished plate catches the key
    { 0.30f, 0.20f, 0.70f }, // Air — read against sky-lit ground, highest rim
    { 0.62f, 0.05f, 0.34f }, // Structure — matte, sits back behind the army
    { 0.70f, 0.02f, 0.30f }  // Neutral
};
static_assert(UE_ARRAY_COUNT(ModelRoles) == ModelCount,
    "Every simulation kind needs a battlefield role");
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
    : Simulation(MakeUnique<cinder::Simulation>()), CampaignStorage(MakeUnique<FCinderCampaignSave>())
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("BattlefieldRoot"));
    Scenery = CreateDefaultSubobject<UCinderScenery>(TEXT("Scenery"));
    WorldEffects = CreateDefaultSubobject<UCinderWorldEffects>(TEXT("WorldEffects"));
    CanyonTerrain = CreateDefaultSubobject<UCinderLandscapeTerrain>(TEXT("CanyonTerrain"));
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
    ModelBatchIndices.Init(INDEX_NONE, ModelCount * TeamCount);
    MotionPartBatchIndices.Init(INDEX_NONE, ModelCount * TeamCount * static_cast<int32>(ECinderMotionPart::Count));
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
    // Share one panel/core pair per faction plus a separate warm resource palette.
    const int32 PaletteMaterialStart = ModelMaterials.Num();
    for (int32 Team = 0; Team < TeamCount; ++Team)
    {
        auto* Panel = UMaterialInstanceDynamic::Create(ModelMaterials[3], this);
        auto* Core = UMaterialInstanceDynamic::Create(ModelMaterials[4], this);
        Panel->SetVectorParameterValue(TEXT("Tint"), CinderTeamColors::Color(Team));
        Core->SetVectorParameterValue(TEXT("Tint"), CinderTeamColors::Accent(Team));
        ModelMaterials.Add(Panel); ModelMaterials.Add(Core);
    }
    const int32 ResourcePaletteStart = ModelMaterials.Num();
    for (int32 Slot = 3; Slot <= 4; ++Slot)
    {
        auto* Material = UMaterialInstanceDynamic::Create(ModelMaterials[Slot], this);
        // Ore reads as self-lit azure crystal. Two reasons beyond looks: the core
        // slot is unlit at GlowIntensity 2.60, so blue spines become the most
        // saturated thing in frame and a deposit is findable at a glance; and the
        // old amber (1.0, 0.58, 0.12) sat almost exactly on team 3's identity
        // orange (1.00, 0.68, 0.14), so a neutral deposit and a player's buildings
        // were the same colour on the minimap. Azure is the widest gap in the
        // palette - cyan would re-collide with team 0's teal.
        Material->SetVectorParameterValue(TEXT("Tint"), Slot == 3
            ? FLinearColor(0.13f, 0.40f, 0.90f) : FLinearColor(0.14f, 0.44f, 1.00f));
        ModelMaterials.Add(Material);
    }
    // Cool desaturated rock so the crystal spines above carry all the saturation.
    const int32 OreMaterialStart = ModelMaterials.Num();
    const FLinearColor OreColors[] = { FLinearColor(0.055f, 0.075f, 0.115f), FLinearColor(0.33f, 0.41f, 0.52f) };
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        auto* OreMaterial = UMaterialInstanceDynamic::Create(ModelMaterials[Slot], this);
        OreMaterial->SetVectorParameterValue(TEXT("Tint"), OreColors[Slot]);
        ModelMaterials.Add(OreMaterial);
    }
    // Per-role hull pairs. Ore keeps its own warm override below, so the Neutral
    // entry only ever backs a role that has no dedicated palette.
    const int32 RoleMaterialStart = ModelMaterials.Num();
    for (int32 Role = 0; Role < static_cast<int32>(ECinderModelRole::Count); ++Role)
    {
        const FRoleFinish& Finish = RoleFinishes[Role];
        for (int32 Slot = 0; Slot < 2; ++Slot)
        {
            auto* Hull = UMaterialInstanceDynamic::Create(ModelMaterials[Slot], this);
            Hull->SetVectorParameterValue(TEXT("Tint"), RoleHullTints[Role][Slot]);
            // Rim colour stays per-role rather than per-team: these hull instances
            // are shared across factions, and team identity already lives in the
            // TeamPanel and CoreGlow slots. Tinting the rim per team here would
            // need four times the material instances for no added information.
            Hull->SetScalarParameterValue(TEXT("Roughness"), Finish.Roughness);
            Hull->SetScalarParameterValue(TEXT("Metallic"), Finish.Metallic);
            Hull->SetScalarParameterValue(TEXT("RimLight"), Finish.Rim);
            ModelMaterials.Add(Hull);
        }
    }
    auto RoleHullMaterial = [&](int32 Kind, int32 Slot)
    {
        return RoleMaterialStart + static_cast<int32>(ModelRoles[Kind]) * 2 + Slot;
    };
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
        for (int32 Team = 0; Team < (Resource ? 1 : TeamCount); ++Team)
        {
            const int32 BatchIndex = Batches.Num();
            FBatch& Batch = AddBatch(Mesh, FLinearColor::White, true);
            for (int32 Slot = 0; Slot < 3; ++Slot)
            {
                // Slot 2 is mechanical Metal, shared by every role on purpose.
                const int32 Material = Slot >= 2 ? Slot
                    : Resource ? OreMaterialStart + Slot : RoleHullMaterial(Index, Slot);
                Batch.Mesh->SetMaterial(Slot, ModelMaterials[Material]);
            }
            Batch.Mesh->SetMaterial(3, ModelMaterials[Resource ? ResourcePaletteStart : PaletteMaterialStart + Team * 2]);
            Batch.Mesh->SetMaterial(4, ModelMaterials[Resource ? ResourcePaletteStart + 1 : PaletteMaterialStart + Team * 2 + 1]);
            ModelBatchIndices[Index * TeamCount + Team] = BatchIndex;
        }
        ++Loaded;
    }

    auto MotionKey = [](int32 Kind, int32 Team, ECinderMotionPart Part)
    {
        return (Kind * TeamCount + Team) * static_cast<int32>(ECinderMotionPart::Count) + static_cast<int32>(Part);
    };
    // Legs, weapons and other motion parts must carry their parent's role hull,
    // or an articulated unit would show two different greys on one body.
    auto MaterialForName = [&](FName Name, int32 Kind, int32 Team) -> UMaterialInterface*
    {
        if (Name == FName(ModelSlotNames[0])) return ModelMaterials[RoleHullMaterial(Kind, 0)];
        if (Name == FName(ModelSlotNames[1])) return ModelMaterials[RoleHullMaterial(Kind, 1)];
        if (Name == FName(ModelSlotNames[2])) return ModelMaterials[2];
        if (Name == FName(ModelSlotNames[3])) return ModelMaterials[PaletteMaterialStart + Team * 2];
        if (Name == FName(ModelSlotNames[4])) return ModelMaterials[PaletteMaterialStart + Team * 2 + 1];
        return nullptr;
    };
    auto ApplyMotionMaterials = [&](UInstancedStaticMeshComponent* Component, UStaticMesh* Mesh,
        int32 Kind, int32 Team)
    {
        const auto& Slots = Mesh->GetStaticMaterials();
        for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
            Component->SetMaterial(Slot, MaterialForName(Slots[Slot].MaterialSlotName, Kind, Team));
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
        for (int32 Team = 0; Team < TeamCount; ++Team)
        {
            for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
            {
                const FCinderMotionAssetPart& Part = Parts[PartIndex];
                UStaticMesh* Mesh = LoadedParts[PartIndex];
                int32 BatchIndex = INDEX_NONE;
                if (Part.Part == ECinderMotionPart::Body)
                {
                    BatchIndex = ModelBatchIndices[KindIndex * TeamCount + Team];
                    Batches[BatchIndex].Mesh->SetStaticMesh(Mesh);
                }
                else
                {
                    BatchIndex = Batches.Num();
                    AddBatch(Mesh, FLinearColor::White, true);
                }
                ApplyMotionMaterials(Batches[BatchIndex].Mesh, Mesh, KindIndex, Team);
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
            int32 Count = 0;
            for (int32 Team = 0; Team < TeamCount; ++Team)
            {
                const int32 Batch = ModelBatchIndices[Index * TeamCount + Team];
                if (Batch != INDEX_NONE) Count += Batches[Batch].Mesh->GetInstanceCount();
            }
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

    if (Plane)
    {
        bool bFogBorderMaterial = false;
        auto* BorderBase = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/Art/VisualTarget/Materials/M_VT_CoreSurface.M_VT_CoreSurface"), nullptr, LOAD_NoWarn);
        const bool bCoreBorderMaterial = BorderBase != nullptr;
        if (!BorderBase && FogBase)
        {
            BorderBase = FogBase;
            bFogBorderMaterial = true;
        }
        if (BorderBase)
        {
            WorldBorderBatch = Batches.Num();
            FBatch& Border = AddBatch(Plane, FLinearColor::Black, false, false);
            Border.Mesh->ComponentTags.Add(TEXT("CinderMapBorder"));
            auto* BorderMaterial = UMaterialInstanceDynamic::Create(BorderBase, this);
            if (bFogBorderMaterial)
            {
                // M_CinderFogV2 is unlit and ships with an opaque default mask.
                BorderMaterial->SetVectorParameterValue(TEXT("FogColor"), FLinearColor::Black);
                BorderMaterial->SetVectorParameterValue(TEXT("FogExploredColor"), FLinearColor::Black);
            }
            else
            {
                BorderMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor::Black);
                if (bCoreBorderMaterial) BorderMaterial->SetScalarParameterValue(TEXT("GlowIntensity"), 0.0f);
            }
            Border.Mesh->SetMaterial(0, BorderMaterial);
            Border.Mesh->bVisibleInReflectionCaptures = false;
            Border.Mesh->bVisibleInRealTimeSkyCaptures = false;
            Border.Mesh->bVisibleInReflections = false;
            Border.Mesh->SetVisibleInRayTracing(false);
            Border.Mesh->SetAffectDistanceFieldLighting(false);
            Border.Mesh->SetAffectDynamicIndirectLighting(false);
            Border.Mesh->SetReceivesDecals(false);

            Border.Transforms.Reserve(4);
        }
    }
    InvalidateEnvironment();
}

void ACinderBattlefield::InvalidateEnvironment()
{
    bEnvironmentInvalid = true;
    bTerrainSurfaceInvalid = true;
    if (!LastFogCells.IsEmpty())
    {
        LastFogCells.Reset();
        ++FogSnapshotRevision;
    }
    LastObstacleReveal.Reset();
}

void ACinderBattlefield::RefreshEnvironment()
{
    // Static terrain is submitted only after reset/load or a newly explored obstacle.
    const float World = Simulation->worldSize();
    uint32 GeometryHash = HashCombineFast(GetTypeHash(Simulation->config().map), GetTypeHash(World));
    TArray<uint8> Revealed;
    Revealed.Reserve(static_cast<int32>(Simulation->obstacles().size()));
    for (const auto& Obstacle : Simulation->obstacles())
    {
        GeometryHash = HashCombineFast(GeometryHash, GetTypeHash(Obstacle.center.x));
        GeometryHash = HashCombineFast(GeometryHash, GetTypeHash(Obstacle.center.y));
        GeometryHash = HashCombineFast(GeometryHash, GetTypeHash(Obstacle.half.x));
        GeometryHash = HashCombineFast(GeometryHash, GetTypeHash(Obstacle.half.y));
        Revealed.Add(Simulation->explored(0, Obstacle.center) ? 1 : 0);
    }
    if (!bEnvironmentInvalid && FMath::IsNearlyEqual(PresentedWorldSize, World)
        && GeometryHash == ObstacleGeometryHash && Revealed == LastObstacleReveal) return;
    if (bEnvironmentInvalid || !FMath::IsNearlyEqual(PresentedWorldSize, World))
    {
        const float WorldSizeInverse = 1.0f / FMath::Max(1.0f, World);
        if (FogMaterial)
            FogMaterial->SetScalarParameterValue(TEXT("CinderWorldSizeInverse"), WorldSizeInverse);
        if (GroundSurfaceMaterial)
            GroundSurfaceMaterial->SetScalarParameterValue(TEXT("CinderWorldSizeInverse"), WorldSizeInverse);
        const float HalfWorld = World * 0.5f;
        Batches[0].Transforms.Reset();
        Batches[0].Transforms.Add(FTransform(FQuat::Identity, FVector(HalfWorld, HalfWorld, -16),
            FVector(World / BasicShapeSize, World / BasicShapeSize, 0.3f)));
        Batches[0].bDirty = true;
        if (FogPlaneBatch != INDEX_NONE)
        {
            FBatch& Fog = Batches[FogPlaneBatch];
            Fog.Transforms.Reset();
            Fog.Transforms.Add(FTransform(FQuat::Identity, FVector(HalfWorld, HalfWorld, WorldSurfaceZ),
                FVector(World / BasicShapeSize, World / BasicShapeSize, 1)));
            Fog.bDirty = true;
        }
        if (WorldBorderBatch != INDEX_NONE)
        {
            constexpr float HalfBorder = WorldBorderWidth * 0.5f;
            // The out-of-bounds skirt, not a second fog sheet, so relief never pokes through
            // it and it stays drawn on both terrain paths even when the fog plane above is
            // hidden: every one of these four planes lies entirely OUTSIDE the playable
            // rectangle (the camera boundary regression asserts exactly that), and the
            // generator feathers relief to zero at the world boundary, so the map edge still
            // meets the skirt flush. Its Z must also stay a compile-time constant - that same
            // regression re-enters a Standard match on a different map and requires the four
            // border transforms to compare exactly equal, which a terrain-derived height on a
            // per-map heightfield could not do.
            constexpr float BorderZ = WorldSurfaceZ + 0.1f;
            const FVector VerticalScale(WorldBorderWidth / BasicShapeSize,
                (World + 2.0f * WorldBorderWidth) / BasicShapeSize, 1.0f);
            const FVector HorizontalScale(World / BasicShapeSize,
                WorldBorderWidth / BasicShapeSize, 1.0f);
            FBatch& Border = Batches[WorldBorderBatch];
            Border.Transforms.Reset();
            Border.Transforms.Add(FTransform(FQuat::Identity,
                FVector(-HalfBorder, HalfWorld, BorderZ), VerticalScale));
            Border.Transforms.Add(FTransform(FQuat::Identity,
                FVector(World + HalfBorder, HalfWorld, BorderZ), VerticalScale));
            Border.Transforms.Add(FTransform(FQuat::Identity,
                FVector(HalfWorld, -HalfBorder, BorderZ), HorizontalScale));
            Border.Transforms.Add(FTransform(FQuat::Identity,
                FVector(HalfWorld, World + HalfBorder, BorderZ), HorizontalScale));
            Border.bDirty = true;
        }
    }
    Batches[1].Transforms.Reset(); Batches[1].bDirty = true;
    for (int32 Index : RockBatchIndices) if (Index != INDEX_NONE)
    {
        Batches[Index].Transforms.Reset(); Batches[Index].bDirty = true;
    }
    int32 ObstacleIndex = 0;
    for (const auto& Obstacle : Simulation->obstacles())
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
            // These proxies deliberately stay at the flat base and are NOT seated on the
            // heightfield. They exist only while CinderScenery is unavailable, which is the
            // same situation in which no Landscape is bound and the ground really is the flat
            // plane: they stand IN FOR the obstacle cliff rather than on top of it, so
            // lifting them by the relief inside the rectangle would stack a second mesa on
            // the first. Every other artefact in this file is seated; this one must not be.
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
    PresentedWorldSize = World;
    bEnvironmentInvalid = false;
}

void ACinderBattlefield::RefreshTerrainSurface()
{
    if (!GroundSurfaceMaterial || !FApp::CanEverRender()) return;
    CinderTerrainSurface::FFeatures Features;
    Features.Map = Simulation->config().map;
    Features.WorldSize = Simulation->worldSize();
    if (Simulation->usesAuthoredTerrain()) Features.AuthoredDefinition = &cinder::mapDefinition(
        Simulation->config().map, Simulation->playerCount(), Simulation->config().matchLength,
        Simulation->config().mapRevision);
    GroundSurfaceMaterial->SetScalarParameterValue(TEXT("AuthoredMap"), Features.AuthoredDefinition ? 1.0f : 0.0f);
    uint32 Hash = HashCombineFast(GetTypeHash(Features.Map), GetTypeHash(Features.WorldSize));
    Hash = HashCombineFast(Hash, GetTypeHash(Simulation->playerCount()));
    Hash = HashCombineFast(Hash, GetTypeHash(CinderLandscapeTerrain::GeometrySignature(*Simulation)));
    for (const auto& Cliff : Simulation->obstacles())
    {
        if (!Simulation->explored(0, Cliff.center)) continue;
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
    TArray<const cinder::Entity*> ServiceBuildings;
    for (const auto& Entity : Simulation->entities())
    {
        if (Entity.team != 0 || !Entity.alive() || Entity.progress < 1
            || !cinder::definition(Entity.kind).building || ServiceBuildings.Num() >= 24) continue;
        ServiceBuildings.Add(&Entity);
        Hash = HashCombineFast(Hash, GetTypeHash(Entity.id));
        Hash = HashCombineFast(Hash, GetTypeHash(static_cast<int32>(Entity.kind)));
        Hash = HashCombineFast(Hash, GetTypeHash(cinder::definition(Entity.kind).radius));
        Hash = HashCombineFast(Hash, GetTypeHash(Entity.pos.x));
        Hash = HashCombineFast(Hash, GetTypeHash(Entity.pos.y));
    }
    // Pads and connecting roads are derived from these known inputs. Stable
    // scenery must return before any pair search, visibility walk or mask build.
    Hash = HashCombineFast(Hash, GetTypeHash(Features.Cliffs.Num()));
    Hash = HashCombineFast(Hash, GetTypeHash(Features.Minerals.Num()));
    Hash = HashCombineFast(Hash, GetTypeHash(ServiceBuildings.Num()));
    if (!bTerrainSurfaceInvalid && GroundSurfaceTexture && Hash == TerrainSurfaceHash) return;
    CinderLandscapeTerrain::GatherPathways(*Simulation, Features.Trails);
    for (const cinder::Entity* Building : ServiceBuildings)
    {
        const float Radius = cinder::definition(Building->kind).radius * 1.20f;
        Features.ServicePads.Add({Building->pos, {Radius, Radius}});
    }
    auto KnownRoadClearance = [&](cinder::Vec2 Point, cinder::Id Source)
    {
        constexpr float RoadRadius = 18;
        if (Point.x < RoadRadius || Point.y < RoadRadius
            || Point.x > Features.WorldSize - RoadRadius
            || Point.y > Features.WorldSize - RoadRadius) return false;
        for (const auto& Cliff : Features.Cliffs)
        {
            const float DX = FMath::Max(0.0f, FMath::Abs(Point.x - Cliff.center.x) - Cliff.half.x);
            const float DY = FMath::Max(0.0f, FMath::Abs(Point.y - Cliff.center.y) - Cliff.half.y);
            if (DX * DX + DY * DY < RoadRadius * RoadRadius) return false;
        }
        for (const auto& Mineral : Features.Minerals)
        {
            const float DX = Point.x - Mineral.x, DY = Point.y - Mineral.y;
            const float Radius = RoadRadius + cinder::definition(cinder::Kind::Resource).radius;
            if (DX * DX + DY * DY < Radius * Radius) return false;
        }
        for (const cinder::Entity* Building : ServiceBuildings)
        {
            if (Building->id == Source) continue;
            const float DX = Point.x - Building->pos.x, DY = Point.y - Building->pos.y;
            const float Radius = RoadRadius + cinder::definition(Building->kind).radius;
            if (DX * DX + DY * DY < Radius * Radius) return false;
        }
        return true;
    };
    for (int32 Index = 1; Index < ServiceBuildings.Num(); ++Index)
    {
        const auto& To = *ServiceBuildings[Index];
        const cinder::Entity* Closest = nullptr;
        float ClosestDistance = 1050;
        for (int32 Previous = 0; Previous < Index; ++Previous)
        {
            const auto& From = *ServiceBuildings[Previous];
            const float DX = To.pos.x - From.pos.x, DY = To.pos.y - From.pos.y;
            const float Distance = FMath::Sqrt(DX * DX + DY * DY);
            if (Distance < 180 || Distance >= ClosestDistance) continue;
            bool bClear = true;
            const float Start = cinder::definition(From.kind).radius + 45;
            const float End = Distance - cinder::definition(To.kind).radius - 45;
            for (float Along = Start; bClear && Along < End; Along += 30)
            {
                const cinder::Vec2 Point{From.pos.x + DX * Along / Distance, From.pos.y + DY * Along / Distance};
                // Only known, hashed geometry affects the surface. Consulting
                // all simulation blockers could disclose an unseen structure.
                bClear = Simulation->visible(0, Point) && KnownRoadClearance(Point, From.id);
            }
            if (bClear) { Closest = &From; ClosestDistance = Distance; }
        }
        if (Closest)
        {
            Features.Roads.Add({Closest->pos, To.pos});
        }
    }
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
    const float CellSize = Simulation->worldSize() / Cells;
    TArray<uint8> Current;
    Current.SetNumUninitialized(Cells * Cells);
    for (int32 Y = 0; Y < Cells; ++Y) for (int32 X = 0; X < Cells; ++X)
    {
        const cinder::Vec2 Point{(X + 0.5f) * CellSize, (Y + 0.5f) * CellSize};
        Current[Y * Cells + X] = Simulation->visible(0, Point) ? 2 : Simulation->explored(0, Point) ? 1 : 0;
    }
    const bool bFogChanged = Current != LastFogCells;
    if (bFogChanged)
    {
        LastFogCells = Current;
        ++FogSnapshotRevision;
    }
    if (FogPlaneBatch == INDEX_NONE)
    {
        // Missing optional material and headless tests retain the original grid adapter.
        // No fog material means no FogTexture, and UCinderLandscapeTerrain::Update refuses to
        // bind without one, so this branch always runs over the flat ground plane: these
        // quads at Z 1 have no relief to poke through, and the landscape-bound path above
        // that hides the fog sheet can never be reached from here.
        for (int32 Y = 0; Y < Cells; ++Y) for (int32 X = 0; X < Cells; ++X)
            if (Current[Y * Cells + X] != 2)
                Batches[Current[Y * Cells + X] ? 4 : 3].Transforms.Add(FTransform(FQuat::Identity,
                    FVector((X + 0.5f) * CellSize, (Y + 0.5f) * CellSize, 1), FVector(CellSize / 100 + 0.001f, CellSize / 100 + 0.001f, 0.02f)));
        return;
    }
    if (FogTextureSubmittedRevision == FogSnapshotRevision || !FogTexture || !FogTexture->GetResource()) return;
    auto Upload = MakeShared<FFogTextureUpload, ESPMode::ThreadSafe>();
    Upload->Pixels.SetNumUninitialized(FogTextureSize * FogTextureSize * 4);
    for (int32 Y = 0; Y < FogTextureSize; ++Y) for (int32 X = 0; X < FogTextureSize; ++X)
    {
        const CinderFogMask::FTexel Texel = CinderFogMask::SampleTexel(Current, X, Y);
        const int32 Pixel = (Y * FogTextureSize + X) * 4;
        Upload->Pixels[Pixel] = 0;
        Upload->Pixels[Pixel + 1] = static_cast<uint8>(FMath::RoundToInt(Texel.Explored * 255));
        Upload->Pixels[Pixel + 2] = static_cast<uint8>(FMath::RoundToInt(Texel.Opacity * 255));
        Upload->Pixels[Pixel + 3] = 255;
    }
    // Shared ownership survives both render/RHI queues. If UE rejects the update,
    // destruction of its cleanup function still releases the region and pixel buffer.
    FogTexture->UpdateTextureRegions(0, 1, &Upload->Region, FogTextureSize * 4, 4, Upload->Pixels.GetData(),
        [Upload](uint8*, const FUpdateTextureRegion2D*) { (void)Upload; });
    FogTextureSubmittedRevision = FogSnapshotRevision;
    ++FogTextureUploads;
}

void ACinderBattlefield::AddBuildingPad(const cinder::Entity& Entity, float GroundZ)
{
    if (PadBatch == INDEX_NONE || (Entity.team == 0 && Entity.progress >= 1 && Scenery && Scenery->IsInitialized())) return;
    const float Diameter = cinder::definition(Entity.kind).radius * 2;
    // A single shallow rectangular service slab grounds the silhouette without a second ring.
    // GroundZ is the caller's one sample for this entity, taken at the same XY the hull uses,
    // so the slab and the building it belongs to can never disagree about where the floor is.
    // The half-centimetre sink keeps the slab reading as poured into the surface rather than
    // laid on top of it, exactly as the old -0.5 did against the flat plane.
    //
    // THE SLAB STAYS FLAT WHILE THE GROUND UNDER IT MAY TILT. One rigid box cannot follow a
    // heightfield, so a corner would lift clear on a real gradient. That is acceptable only
    // because the terrain generator damps relief toward level along the worn pathways, which
    // is the ground players actually build on: the residual gradient under a footprint is a
    // small fraction of the walkable ceiling, so the lift stays inside the slab's thickness.
    // If pathway damping is ever removed, this slab has to become per-corner geometry.
    Batches[PadBatch].Transforms.Add(FTransform(FRotator(0, FMath::RadiansToDegrees(Entity.facing), 0),
        FVector(Entity.pos.x, Entity.pos.y, GroundZ - 0.5f), FVector(Diameter * 1.08f / 100, Diameter * 1.08f / 100, 0.03f)));
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
    auto* GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Canyon/Materials/M_CinderCanyonGround.M_CinderCanyonGround"), nullptr, LOAD_NoWarn);
    if (!GroundMaterial) GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/VisualTarget/Materials/M_CinderGroundV4.M_CinderGroundV4"), nullptr, LOAD_NoWarn);
    if (!GroundMaterial) GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_CinderGroundV3.M_CinderGroundV3"), nullptr, LOAD_NoWarn);
    if (GroundMaterial)
    {
        GroundSurfaceMaterial = UMaterialInstanceDynamic::Create(GroundMaterial, this);
        CinderGroundPalette::Apply(GroundSurfaceMaterial);
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
    for (int32 Team = 0; Team < TeamCount; ++Team)
    {
        for (UStaticMesh* Shape : { Cube.Get(), Cylinder.Get(), Cone.Get(), Sphere.Get() })
            AddBatch(Shape, CinderTeamColors::Color(Team), true);
    }
    for (int32 Team = 0; Team < TeamCount; ++Team)
        for (UStaticMesh* Shape : { Cube.Get(), Cylinder.Get(), Cone.Get(), Sphere.Get() })
            AddBatch(Shape, CinderTeamColors::Accent(Team), true);
    LoadModelBatches();
    InitializeEnvironment();
    if (GroundSurfaceMaterial && FogTexture) GroundSurfaceMaterial->SetTextureParameterValue(TEXT("FogMask"), FogTexture);
    CanyonTerrain->Initialize();
    Scenery->Initialize(RootComponent, GroundSurfaceMaterial, FogTexture);
    WorldEffects->Initialize(RootComponent, Sphere, Cylinder, Cone, Plane, BaseMaterial);

    // Raking light gives fractured cliffs depth. Keep enough headroom for pale
    // unit armor while the cool skylight makes shaded rock faces readable.
    auto* Sun = GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-46, 45, 0));
    auto* SunComponent = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
    SunComponent->SetMobility(EComponentMobility::Movable);
    SunComponent->SetIntensity(6.5f);
    SunComponent->SetLightColor(FLinearColor(1.0f, 0.91f, 0.78f));
    SunComponent->SetCastShadows(true);
    // The camera never pulls back past FarthestDistance, so 6000 cm of cascade
    // range was spending most of a fixed 1024 shadow map on ground nobody sees.
    // 4200 roughly doubles the texel density over the playable view.
    SunComponent->SetDynamicShadowDistanceMovableLight(4200);
    SunComponent->SetDynamicShadowCascades(PLATFORM_IOS ? 2 : 4);
    SunComponent->SetCascadeDistributionExponent(3.2f);
    // Steep landscape walls need more slope bias than flat ground; the lower
    // values produced dense self-shadow stripes that hid the rock's strata.
    SunComponent->SetShadowBias(0.70f);
    SunComponent->SetShadowSlopeBias(1.0f);
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
    SkyComponent->SetIntensity(1.40f);
    SkyComponent->SetLightColor(FLinearColor(0.50f, 0.62f, 0.82f));

    // Atmospheric depth. Mobile forward computes height fog per-vertex for opaque
    // geometry, so this is close to free and it is the only distance cue in the
    // project — without it the far half of the map sits at the same contrast as
    // the near half and the battlefield reads flat. Cool blue inscatter against
    // warm ground is what makes distance recede.
    //
    // Volumetric fog is explicitly off: it is a full volume pass this frame
    // budget cannot afford. FogMaxOpacity is capped well below 1 and StartDistance
    // keeps the near field clear, so unexplored ground still reads as unknown
    // rather than merely hazy — fog of war stays the darker, separate signal.
    auto* HeightFog = GetWorld()->SpawnActor<AExponentialHeightFog>();
    if (auto* FogComponent = HeightFog ? HeightFog->GetComponent() : nullptr)
    {
        FogComponent->SetMobility(EComponentMobility::Movable);
        FogComponent->SetFogDensity(0.012f);
        FogComponent->SetFogHeightFalloff(0.35f);
        FogComponent->SetFogInscatteringColor(FLinearColor(0.30f, 0.42f, 0.62f));
        FogComponent->SetStartDistance(900.0f);
        FogComponent->SetFogMaxOpacity(0.55f);
        FogComponent->SetVolumetricFog(false);
    }
    Simulation->reset();
    LoadCampaignProgress();
    ResetFeedback();
    RenderState();
}

void ACinderBattlefield::StartMatch(int MapIndex, cinder::AIDifficulty Difficulty,
    cinder::MatchLength Length)
{
    if (bOnlineMatch) return;
    SetActorTickEnabled(true);
    CurrentMap = FMath::Clamp(MapIndex, 0, 2);
    cinder::Config Config;
    Config.map = CurrentMap;
    Config.aiAggression = cinder::aiDifficultyAggression(Difficulty);
    Config.matchLength = Length;
    Simulation->reset(Config);
    ResetPresentation();
    bMenu = false; bPaused = false;
    RenderState();
}

void ACinderBattlefield::StartTutorial()
{
    if (bOnlineMatch) return;
    SetActorTickEnabled(true);
    CurrentMap = 0;
    FCinderTutorial InitializedTraining;
    if (!InitializedTraining.InitializeScenario(*Simulation)) return;
    ResetPresentation();
    Training = InitializedTraining;
    bMenu = false; bPaused = false;
    RenderState();
}

bool ACinderBattlefield::StartOnlineMatch(const cinder::net::Snapshot& Snapshot)
{
    if (!Simulation->applySnapshot(Snapshot)) return false;
    SetActorTickEnabled(true);
    CurrentMap = Snapshot.config.map;
    ResetPresentation();
    bOnlineMatch = true; bMenu = bPaused = false;
    OnlineSnapshotSerial = OnlinePoseSerial = 0;
    OnlineSnapshotAt = FPlatformTime::Seconds(); OnlineSnapshotInterval = 0.1f;
    PreviousOnlinePositions.Reset();
    // The server includes only remembered neutral resources, safe on reconnect.
    for (const auto& Entity : Simulation->entities())
        if (Entity.kind == cinder::Kind::Resource) ResourceMemory.push_back(Entity);
    RenderState();
    return true;
}

cinder::CommandResult ACinderBattlefield::SubmitCommand(const cinder::Command& Command, uint32* OutOnlineSequence)
{
    if (OutOnlineSequence) *OutOnlineSequence = 0;
    if (!bOnlineMatch)
    {
        if (CampaignDirector.IsActive() && (bMenu || bPaused || IsMatchOver()))
            return {false, "This campaign mission is not accepting orders."};
        const auto Result = Simulation->command(Command);
        if (Result.accepted && CampaignDirector.IsActive())
            CampaignDirector.AcceptedCommand(*Simulation, Command);
        return Result;
    }
    auto* Online = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCinderOnlineSubsystem>() : nullptr;
    const bool bSent = Online && Online->SendCommand(Command, OutOnlineSequence);
    return {bSent, bSent ? "Order sent. Waiting for the server." : "Connection unavailable. Wait for the match to reconnect."};
}

cinder::Vec2 ACinderBattlefield::RenderPosition(const cinder::Entity& Entity) const
{
    // Online is the one place the presented root may lag the authoritative one:
    // snapshots arrive on a network interval with nothing in between, so there is
    // no alternative to interpolating. A local match deliberately does NOT do
    // this. Lerping between two local steps would buy smoother translation at the
    // cost of rendering up to a full 0.05 s behind, which breaks the invariant
    // that a settled or paused frame shows exactly where things are — and on a
    // touch RTS the tap-to-move response is worth more than the last of the
    // translation smoothing. Everything riding the root (gait, recoil, turret,
    // banking, flinch) is evaluated per rendered frame regardless.
    if (!bOnlineMatch || cinder::definition(Entity.kind).building || Entity.kind == cinder::Kind::Resource) return Entity.pos;
    const cinder::Vec2* Previous = PreviousOnlinePositions.Find(Entity.id);
    if (!Previous || (Entity.team != 0 && !Simulation->visible(0, *Previous))) return Entity.pos;
    const float Alpha = FMath::Clamp(static_cast<float>((FPlatformTime::Seconds() - OnlineSnapshotAt) / OnlineSnapshotInterval), 0.0f, 1.0f);
    return {FMath::Lerp(Previous->x, Entity.pos.x, Alpha), FMath::Lerp(Previous->y, Entity.pos.y, Alpha)};
}

float ACinderBattlefield::GroundHeight(cinder::Vec2 Point) const
{
    // Where the ground IS, which is not the same question as what the generator
    // WOULD produce. Only a compatible world size gets a pre-authored Landscape;
    // every other match length falls back to a genuinely flat generated plane, and
    // seating against relief that nothing renders would float units, props and
    // overlays by up to the walkable ceiling over ground the player sees as level.
    // HeightAt stays the pure generator so BuildHeightData can still bake every
    // size; the presented surface is gated here, once, where the answer is known.
    if (!bTerrainRelief) return 0.0f;
    // The landscape actor sits at BaselineZ, so the rendered surface is the
    // baseline plus the generated relief.
    return CinderLandscapeTerrain::BaselineZ
        + CinderLandscapeTerrain::HeightAt(*Simulation, Point.x, Point.y);
}

float ACinderBattlefield::PickingGroundHeight(cinder::Vec2 Point) const
{
    if (!bTerrainRelief) return 0.0f;
    constexpr float Baseline = CinderLandscapeTerrain::BaselineZ;
    if (!FMath::IsFinite(Point.x) || !FMath::IsFinite(Point.y)
        || LastFogCells.Num() != CinderFogMask::Cells * CinderFogMask::Cells) return Baseline;

    // Match the uploaded linear BGRA8 FogMask.G at mip zero: normalized world
    // UVs, texel-center coordinates, clamp addressing, and quantization before
    // bilinear filtering. Sample the last presented fog snapshot rather than
    // newer simulation visibility that may not yet have reached the material.
    const float WorldSize = FMath::Max(1.0f, Simulation->worldSize());
    const float TexelX = FMath::Clamp(Point.x / WorldSize, 0.0f, 1.0f) * CinderFogMask::TextureSize - 0.5f;
    const float TexelY = FMath::Clamp(Point.y / WorldSize, 0.0f, 1.0f) * CinderFogMask::TextureSize - 0.5f;
    const int32 X = FMath::FloorToInt(TexelX), Y = FMath::FloorToInt(TexelY);
    const auto ExploredAt = [this](int32 SampleX, int32 SampleY)
    {
        const auto Texel = CinderFogMask::SampleTexel(LastFogCells,
            FMath::Clamp(SampleX, 0, CinderFogMask::TextureSize - 1),
            FMath::Clamp(SampleY, 0, CinderFogMask::TextureSize - 1));
        return static_cast<float>(FMath::RoundToInt(Texel.Explored * 255.0f)) / 255.0f;
    };
    const float FractionX = TexelX - X, FractionY = TexelY - Y;
    const float Explored = FMath::Lerp(
        FMath::Lerp(ExploredAt(X, Y), ExploredAt(X + 1, Y), FractionX),
        FMath::Lerp(ExploredAt(X, Y + 1), ExploredAt(X + 1, Y + 1), FractionX), FractionY);
    // The ground shader offsets -(WorldZ + 1) * (1 - FogMask.G).
    return Baseline + (GroundHeight(Point) - Baseline) * Explored;
}

float ACinderBattlefield::EntityGroundHeight(cinder::Vec2 Point, cinder::Kind Kind) const
{
    const float Ground = GroundHeight(Point);
    const cinder::Definition& Definition = cinder::definition(Kind);
    return Definition.air ? FMath::Max(Ground, CinderLandscapeTerrain::VisualObstacleTopAt(
        *Simulation, Point.x, Point.y, bTerrainRelief, Definition.radius * 2.0f)) : Ground;
}

void ACinderBattlefield::ReturnToMenu()
{
    SetActorTickEnabled(true);
    bMenu = true; bPaused = false;
    Training.Reset();
    CampaignDirector.Reset();
    bOnlineMatch = false;
    PreviousOnlinePositions.Reset();
    // Keep the completed match counters available for diagnostics while skipping stale effects.
    ResetFeedback(false);
    WorldEffects->Reset(Simulation->lastEffectId());
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
    ++MatchGenerationSerial;
    Training.Reset();
    CampaignDirector.Reset();
    SavedCampaignBoundary = 0;
    bCampaignVictoryRecorded = false;
    InvalidateEnvironment();
    ResourceMemory.clear();
    // The presentation clock belongs to the match that produced it: recoil,
    // flinch, birth and death stamps are all compared against it, and carrying it
    // across a reset would leave every stamp in the future.
    PresentationTime = 0;
    Scenery->Reset();
    WorldEffects->Reset(Simulation->lastEffectId());
    ResetFeedback();
}

void ACinderBattlefield::ResetFeedback(bool bClearCombatCounters)
{
    AudioStatsSnapshot = Simulation->players()[0].stats;
    EntityMotion.Reset();
    if (bClearCombatCounters) CombatFeedback = FCinderCombatFeedbackStats{};
    CombatFeedback.ProcessedHighWater = Simulation->lastEffectId();
}

void ACinderBattlefield::UpdateCombatFeedback()
{
    const uint64 LastEffectId = Simulation->lastEffectId();
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
        const float Z = EntityGroundHeight(Point, Kind) + (cinder::definition(Kind).air ? 125.0f : 25.0f);
        return Player->ProjectWorldLocationToScreen(FVector(Point.x, Point.y, Z), Screen)
            && Screen.X >= 0 && Screen.Y >= 0 && Screen.X < ViewportWidth && Screen.Y < ViewportHeight;
    };

    bool bDeath = false, bWeapon = false, bImpact = false;
    uint64 AudibleEvents = 0;
    for (const cinder::Effect& Effect : Simulation->effects())
    {
        if (Effect.id <= PreviousHighWater || Effect.id > LastEffectId) continue;
        ++CombatFeedback.ConsumedEvents;
        // Healing has its own visual treatment, but there is no suitable healing audio asset.
        if (Effect.type == cinder::EffectType::Heal) continue;
        const bool bSource = Effect.type == cinder::EffectType::Weapon;
        if (!Simulation->effectVisible(Effect, 0, bSource))
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
    const auto& Stats = Simulation->players()[0].stats;
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
            for (const auto& Entity : Simulation->entities()) OldPositions.Add(Entity.id, RenderPosition(Entity));
            const uint64 PreviousTick = Simulation->tick();
            if (Simulation->applySnapshot(Snapshot))
            {
                PreviousOnlinePositions = MoveTemp(OldPositions);
                OnlineSnapshotInterval = FMath::Clamp(static_cast<float>(Snapshot.tick > PreviousTick ? Snapshot.tick - PreviousTick : 2) * cinder::Simulation::Step, 0.05f, 0.25f);
                OnlineSnapshotAt = FPlatformTime::Seconds();
                OnlineSnapshotSerial = Online->SnapshotSerial(); bNewSnapshot = true;
                ResourceMemory.clear();
                for (const auto& Entity : Simulation->entities())
                    if (Entity.kind == cinder::Kind::Resource) ResourceMemory.push_back(Entity);
                if (bPaused)
                {
                    // Reading menus does not pause authority or accumulate old combat cues.
                    AudioStatsSnapshot = Simulation->players()[0].stats;
                    ResetFeedback(false);
                }
                else { UpdateCompletionAudio(); UpdateCombatFeedback(); }
            }
        }
        if (bPaused && Simulation->winner() == -1) return;
        PresentationTime += DeltaSeconds;
        RenderTimer += DeltaSeconds;
        const bool bInterpolating = FPlatformTime::Seconds() - OnlineSnapshotAt < OnlineSnapshotInterval;
        if (bNewSnapshot || (bInterpolating && RenderTimer >= cinder::Simulation::Step))
        { RenderTimer = 0; RenderSimState(); }
        // Online already interpolates the root between snapshots; running the pose
        // pass every frame is what makes the parts riding that root continuous too.
        RenderPoseState();
        return; // A network replica must never run the authoritative simulation loop.
    }
    // Start/load explicitly populate the scene. Frozen matches need neither new
    // simulation poses nor repeated fog scans and instance comparisons.
    if (bMenu || bPaused) return;
    if (IsMatchOver())
    {
        if (CampaignDirector.IsRunning()) ObserveCampaign();
        else if (CampaignDirector.IsActive()) SettleCampaignBoundary();
        return;
    }
    Simulation->update(FMath::Min(DeltaSeconds, 0.2f));
    if (Training.IsActive()) Training.TickOpponent(*Simulation);
    if (CampaignDirector.IsActive())
    {
        CampaignDirector.TickOpponent(*Simulation);
        ObserveCampaign();
    }
    UpdateCompletionAudio();
    UpdateCombatFeedback();
    PresentationTime += DeltaSeconds;
    RenderTimer += DeltaSeconds;
    // Always submit the terminal state before subsequent ticks become idle.
    if (RenderTimer >= cinder::Simulation::Step || IsMatchOver())
    { RenderTimer = 0; RenderSimState(); }
    RenderPoseState();
}

void ACinderBattlefield::AddEntity(const cinder::Entity& AuthoritativeEntity)
{
    using namespace cinder;
    const cinder::Entity& Entity = AuthoritativeEntity;
    const cinder::Vec2 RenderPoint = RenderPosition(AuthoritativeEntity);
    const Definition& Def = definition(Entity.kind);
    const bool Resource = Entity.kind == Kind::Resource;
    if (!Resource && (!CinderTeamColors::IsValid(Entity.team) || Entity.team >= Simulation->playerCount())) return;
    if (Resource)
    {
        if (!Simulation->explored(0, RenderPoint) || Entity.resource <= 0) return;
    }
    else if (Entity.team != 0 && !Simulation->visible(0, RenderPoint)) return;
    FCinderEntityPose Pose;
    if (!Resource && !Def.building)
    {
        FCinderMotionObservation Observation;
        Observation.Id = Entity.id; Observation.Kind = Entity.kind; Observation.Position = RenderPoint;
        Observation.Team = Entity.team;
        Observation.Order = Entity.order; Observation.Facing = Entity.facing;
        Observation.Cooldown = Entity.cooldown; Observation.HarvestTimer = Entity.harvestTimer;
        Observation.bReturning = Entity.returning;
        Observation.bConstructionActive = Entity.order == Order::Construct && Simulation->constructionActive(Entity.target);
        // Health is read only to edge-detect a drop for the flinch; the pose never
        // feeds any of it back, and Simulation::hash still owns every one of these.
        Observation.Hp = Entity.hp;
        Observation.MaxHp = Def.hp;
        if (const cinder::Entity* AimTarget = Entity.target ? Simulation->find(Entity.target) : nullptr)
        {
            // Fog privacy: a barrel may never track a target the local player cannot
            // see, or the turret itself becomes a pointer to an unseen enemy.
            if (AimTarget->alive() && Simulation->visible(0, AimTarget->pos))
            {
                Observation.bHasTarget = true;
                Observation.TargetBearing = FMath::Atan2(AimTarget->pos.y - RenderPoint.y,
                    AimTarget->pos.x - RenderPoint.x);
            }
        }
        // LOCAL VIEW STATE. This is the one pose input that legitimately differs
        // between two clients watching the same match, so it may only ever widen a
        // cosmetic channel. It must never reach a command, the state hash, a save,
        // a snapshot, or any observation hash added later.
        Observation.bSelected = SelectedIds.Contains(Entity.id);
        // One monotonic presentation clock for every stamp. Recoil, flinch, birth
        // and death all record against Observation.Time and are later compared
        // against the time handed to EndFrame, so the two must be the same clock.
        Observation.Tick = PoseSerial;
        Observation.SimulationTick = Simulation->tick();
        Observation.bInterpolatedPosition = bOnlineMatch;
        Observation.Time = PresentationTime;
        Pose = EntityMotion.Observe(Observation);
    }
    // Cosmetic transforms follow simulation time; they cannot move the authoritative XY root.
    const bool bGroundHover = Entity.kind == Kind::Scout || Entity.kind == Kind::Mender;
    // ONE heightfield evaluation per entity per frame, reused by the hull, every motion part,
    // the primitive fallback and the service slab. GroundHeight is the closed-form generator
    // rather than a trace, but it still walks the obstacle list per call, and the HUD already
    // samples the same function at the same XY for the selection ring - seating both from one
    // value is what keeps the ring welded to the feet instead of a centimetre out.
    //
    // Air units clear the full mesh cliff silhouette, which can be much taller
    // than the low Landscape foundation beneath it.
    // A unit is a point and can ride the exact surface under it. A building is a rigid
    // flat-bottomed box: seated at its centre height, the downhill corner of a footprint
    // sitting across a gradient lifts clear of the ground and the structure visibly
    // hovers. Seat a building at the LOWEST ground under its own footprint instead, so
    // the uphill side embeds into the slope — a structure cut into a rise reads as
    // built there, while one floating over a dip reads as a bug. Four rim samples plus
    // the centre is enough at this footprint size and only runs for buildings.
    float GroundZ = EntityGroundHeight(RenderPoint, Entity.kind);
    if (Def.building && bTerrainRelief)
    {
        const float Reach = Def.radius * 0.78f;
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            const float Angle = Corner * (PI * 0.5f) + Entity.facing;
            GroundZ = FMath::Min(GroundZ, GroundHeight({RenderPoint.x + FMath::Cos(Angle) * Reach,
                RenderPoint.y + FMath::Sin(Angle) * Reach}));
        }
    }
    const float Elevation = GroundZ + (Def.air ? 125.0f : bGroundHover ? 8.0f : 0.0f) + Pose.BodyZ;
    if (Def.building && Simulation->visible(0, RenderPoint)) AddBuildingPad(Entity, GroundZ);
    const float BuildScale = Def.building ? FMath::Max(0.08f, Entity.progress) : 1;
    const FQuat Facing = FRotator(0, FMath::RadiansToDegrees(Entity.facing), 0).Quaternion();
    const int32 ModelKey = static_cast<int32>(Entity.kind) * TeamCount + (Resource ? 0 : Entity.team);
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
                    Root, Facing, Part.Pivot, Pose.Rotation(Part.Part), Pose.Offset(Part.Part),
                    FVector(Pose.UniformScale)));
            }
            ++LastModelEntities;
            return;
        }
        // The imported centimeter mesh is already sized to the definition and has a bottom pivot.
        // UniformScale is 1 outside the 0.35 s birth window, so the construction
        // rise (BuildScale) keeps its exact meaning for every settled entity.
        Batches[ModelBatchIndices[ModelKey]].Transforms.Add(FTransform(Facing * Pose.BodyRotation.Quaternion(),
            FVector(RenderPoint.x, RenderPoint.y, Elevation),
            FVector(Pose.UniformScale, Pose.UniformScale, BuildScale * Pose.UniformScale)));
        ++LastModelEntities;
        return;
    }
    ++LastFallbackEntities;
    if (Entity.kind == Kind::Resource)
    {
        for (int I = 0; I < 3; ++I)
        {
            const float A = I * 2.0944f;
            // Ore shards are ground-standing geometry like everything else: the three
            // spikes ride the entity's single ground sample rather than an absolute 34 cm.
            Batches[2].Transforms.Add(FTransform(FRotator(0, I * 120, I * 8), FVector(RenderPoint.x + FMath::Cos(A) * 15, RenderPoint.y + FMath::Sin(A) * 15, GroundZ + 34), FVector(0.25f, 0.3f, 0.65f + I * 0.1f)));
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
        Batches[5 + Entity.team * 4 + Shape + (Accent ? TeamCount * 4 : 0)].Transforms.Add(FTransform(PosedFacing * Rotation.Quaternion(), Position + PosedFacing.RotateVector(Offset), Scale));
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
    // Explicit submissions (start, load, reset, the terminal frame) still want one
    // complete scene, so the whole pass runs. Tick calls the two halves separately.
    RenderSimState();
    RenderPoseState();
}

void ACinderBattlefield::RenderSimState()
{
    if (bOnlineMatch) ++OnlinePoseSerial;
    if (Batches.IsEmpty()) return;
    if (!IsValid(CameraRig))
    {
        if (UWorld* World = GetWorld())
        {
            if (APlayerController* Controller = World->GetFirstPlayerController())
                CameraRig = Cast<ACinderCamera>(Controller->GetPawn());
            if (!CameraRig)
            {
                TActorIterator<ACinderCamera> It(World);
                if (It) CameraRig = *It;
            }
        }
    }
    if (CameraRig)
    {
        CameraRig->SetTerrainSource(this);
        CameraRig->SetWorldSize(Simulation->worldSize());
    }
    RefreshEnvironment();
    RefreshTerrainSurface();
    UpdateFogTexture();
    // ONE signal, two artefacts. UCinderLandscapeTerrain::Update returns true only when it
    // has a canonical Landscape bound WITH this frame's fog mask already in its material, so
    // the return value means precisely "the authored terrain is on screen and it is carrying
    // fog itself". The generated flat ground plane hides behind it, and so does the flat fog
    // sheet: that sheet is a single plane at WorldSurfaceZ, and any relief taller than 2.5 cm
    // pokes straight through it. With real hills that is not an occasional artefact at an
    // obstacle, it is every hill on every map on every frame.
    //
    // M_CinderCanyonGround does the job properly in its place: it multiplies BASE_COLOR by
    // the visible fraction of this same FogTexture, writes the fog tint as EMISSIVE_COLOR so
    // it holds up independently of lighting, and flattens unexplored relief through
    // WORLD_POSITION_OFFSET so an unseen ridge cannot even silhouette against explored sky.
    //
    // FOG PRIVACY, which is correctness and not dressing: hiding the sheet reveals nothing.
    // The sheet only ever DIMMED geometry this class had already decided the local player may
    // see - obstacle rock batches gated on explored, service slabs gated on visible, scenery
    // gated on its own fog checks and now masked by the canyon materials. It was never the
    // gate. For the ground itself the gate moves from the plane to the landscape material,
    // and both read the identical FogTexture, so unknown, explored and visible fall on the
    // same texels either way. Update refuses to bind without that texture, so there is no
    // path on which the landscape shows while the mask is missing.
    //
    // On the fallback path (any non-canonical world size) the ground really is a flat plane,
    // so the sheet still fits it exactly and is left exactly as it is today.
    const bool bLandscapeBound = CanyonTerrain->Update(*Simulation, FogTexture, GroundSurfaceTexture);
    // One authority for "the presented ground has relief", read by GroundHeight and
    // handed to the scenery and effect components so all four systems seat against
    // the same surface rather than each deciding for itself.
    bTerrainRelief = bLandscapeBound;
    Batches[0].Mesh->SetVisibility(!bLandscapeBound);
    if (FogPlaneBatch != INDEX_NONE) Batches[FogPlaneBatch].Mesh->SetVisibility(!bLandscapeBound);
    for (const auto& Entity : Simulation->entities())
    {
        if (Entity.kind != cinder::Kind::Resource) continue;
        // Cache only observed resource state: unseen harvesting must not leak through visuals.
        if (!Simulation->visible(0, Entity.pos)) continue;
        const auto It = std::find_if(ResourceMemory.begin(), ResourceMemory.end(), [&](const cinder::Entity& E) { return E.id == Entity.id; });
        if (It == ResourceMemory.end()) ResourceMemory.push_back(Entity); else *It = Entity;
    }
    Scenery->Update(*Simulation, ResourceMemory, bTerrainRelief);
    WorldEffects->Update(*Simulation, 0, bTerrainRelief);
    FlushBatches(false);
}

void ACinderBattlefield::RenderPoseState()
{
    if (Batches.IsEmpty()) return;
    ++PoseSerial;
    LastModelEntities = LastFallbackEntities = 0;
    for (FBatch& Batch : Batches) if (Batch.bDynamic) Batch.Transforms.Reset();
    // Snapshot the local selection once per pass rather than querying the
    // controller per entity. Purely cosmetic view state; see AddEntity.
    SelectedIds.Reset();
    if (UWorld* World = GetWorld())
        if (const auto* Controller = Cast<ACinderPlayerController>(World->GetFirstPlayerController()))
            for (cinder::Id Id : Controller->Selection()) SelectedIds.Add(Id);
    EntityMotion.BeginFrame();
    for (const auto& Entity : Simulation->entities())
        if (Entity.kind != cinder::Kind::Resource && Entity.alive()) AddEntity(Entity);
    for (const auto& Resource : ResourceMemory) AddEntity(Resource);
    // Eviction discriminates death from fog: a sample that vanished while its cell
    // is still visible died, and one whose cell went dark merely left vision. Only
    // the first may play a death, or the scene would leak kills made in the dark.
    EntityMotion.EndFrame(PresentationTime,
        [this](const cinder::Vec2& Position) { return Simulation->visible(0, Position); });
    // Topples ride the batches their unit already used, so a death costs transforms
    // and nothing else: no component, no draw call, no asset. The ring is bounded
    // at MaxDyingEntities, so the worst case is 24 hulls of at most six parts.
    for (const FCinderDyingEntity& Wreck : EntityMotion.DyingEntities())
    {
        const int32 KindIndex = static_cast<int32>(Wreck.Kind);
        if (!MotionKindAvailable.IsValidIndex(KindIndex) || !MotionKindAvailable[KindIndex]) continue;
        if (!CinderTeamColors::IsValid(Wreck.Team) || Wreck.Team >= Simulation->playerCount()) continue;
        const int32 ModelKey = KindIndex * TeamCount + Wreck.Team;
        const FCinderEntityPose DeathPose = FCinderEntityMotion::CalculateDeathPose(Wreck, PresentationTime);
        const bool bWreckHover = Wreck.Kind == cinder::Kind::Scout || Wreck.Kind == cinder::Kind::Mender;
        // Seated exactly as the living hull was in AddEntity, from one sample at the wreck's
        // own XY. Any other formula would make the hull jump vertically on the single frame
        // the unit stops being an entity and becomes a topple.
        const cinder::Definition& WreckDef = cinder::definition(Wreck.Kind);
        const float WreckGroundZ = EntityGroundHeight(Wreck.Position, Wreck.Kind);
        const float WreckZ = WreckGroundZ + (WreckDef.air ? 125.0f : bWreckHover ? 8.0f : 0.0f)
            + DeathPose.BodyZ;
        const FVector Root(Wreck.Position.x, Wreck.Position.y, WreckZ);
        const FQuat WreckFacing = FRotator(0, FMath::RadiansToDegrees(Wreck.Facing), 0).Quaternion();
        for (const FCinderMotionAssetPart& Part : CinderMotionAssetParts(Wreck.Kind))
        {
            const int32 Key = ModelKey * static_cast<int32>(ECinderMotionPart::Count)
                + static_cast<int32>(Part.Part);
            if (!MotionPartBatchIndices.IsValidIndex(Key) || MotionPartBatchIndices[Key] == INDEX_NONE) continue;
            Batches[MotionPartBatchIndices[Key]].Transforms.Add(CinderPartWorldTransform(
                Root, WreckFacing, Part.Pivot, DeathPose.Rotation(Part.Part), DeathPose.Offset(Part.Part)));
        }
    }
    FlushBatches(true);
}

void ACinderBattlefield::FlushBatches(bool bPosePass)
{
    // One presentation pass is one submitted frame, not one call. The environment
    // half runs on the simulation step and the pose half every rendered frame, so
    // counting both would double a number that diagnostics and the lifecycle
    // regressions read as "frames submitted". The pose half is the per-frame one.
    if (bPosePass) ++InstanceUploads.Passes;
    for (FBatch& Batch : Batches)
    {
        if (Batch.bDynamic != bPosePass) continue;
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
            // The idle settle moves every unit every pass, so the unchanged fast
            // path above almost never fires for entity batches and this snapshot
            // is on the hot path. A dynamic batch's Transforms are cleared at the
            // top of the next pose pass, so swapping keeps both allocations and
            // drops an O(n) copy of 96-byte transforms. The static environment
            // batches are rebuilt in place by RefreshEnvironment and must copy.
            if (Batch.bDynamic) Swap(Batch.SubmittedTransforms, Batch.Transforms);
            else Batch.SubmittedTransforms = Batch.Transforms;
            Batch.bDirty = false;
        }
        else Batch.bDirty = true;
    }
}

bool ACinderBattlefield::SaveMatch() const
{
    // The menu can retain the last simulation for diagnostics, including practice.
    // Only a current skirmish may reach the persistent save slot.
    if (bMenu || bOnlineMatch || Training.IsActive() || CampaignDirector.IsActive()) return false;
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Matches");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*(Directory / TEXT("skirmish.cinder")));
    return Simulation->save(TCHAR_TO_UTF8(*Filename));
}

bool ACinderBattlefield::LoadMatch()
{
    if (bOnlineMatch) return false;
    const FString Filename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*(FPaths::ProjectSavedDir() / TEXT("Matches/skirmish.cinder")));
    return LoadMatchFrom(Filename);
}

bool ACinderBattlefield::LoadMatchFrom(const FString& Filename)
{
    if (bOnlineMatch) return false;
    // A completed online view is still a replica. Load offline state separately,
    // so a failed load preserves the current view and success restores local rules.
    cinder::Simulation Loaded;
    if (!Loaded.load(TCHAR_TO_UTF8(*Filename))) return false;
    *Simulation = MoveTemp(Loaded);
    SetActorTickEnabled(true);
    ResetPresentation();
    CurrentMap = Simulation->config().map;
    bMenu = false; bPaused = false; RenderState(); return true;
}
