#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderAudioSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/GameInstance.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ConstructorHelpers.h"
#include <algorithm>

DEFINE_LOG_CATEGORY_STATIC(LogCinderModels, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogCinderCombat, Log, All);

namespace
{
constexpr int32 ModelCount = 15;
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
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeAsset(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UTextureCube> AmbientAsset(TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"));
    Cube = CubeAsset.Object; Cylinder = CylinderAsset.Object; Cone = ConeAsset.Object; Sphere = SphereAsset.Object;
    AmbientCubemap = AmbientAsset.Object;
}

ACinderBattlefield::FBatch& ACinderBattlefield::AddBatch(UStaticMesh* Mesh, FLinearColor Color)
{
    auto* Component = NewObject<UInstancedStaticMeshComponent>(this);
    Component->SetupAttachment(RootComponent);
    Component->SetStaticMesh(Mesh);
    Component->SetMobility(EComponentMobility::Movable);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCastShadow(false);
    Component->RegisterComponent();
    if (BaseMaterial)
    {
        auto* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
        Material->SetVectorParameterValue(TEXT("Tint"), Color);
        Component->SetMaterial(0, Material);
    }
    MeshComponents.Add(Component);
    FBatch Batch; Batch.Mesh = Component;
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
    ModelFallbackReasons.Init(TEXT("asset missing"), ModelCount);
    ModelMeshes.SetNum(ModelCount);
    ModelMaterials.Reset();
    for (const TCHAR* Slot : ModelSlotNames)
    {
        const FString Name = FString::Printf(TEXT("/Game/Art/Materials/MI_Cinder%s.MI_Cinder%s"), Slot, Slot);
        auto* Material = LoadObject<UMaterialInterface>(nullptr, *Name, nullptr, LOAD_NoWarn);
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
        auto* Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn);
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
            FBatch& Batch = AddBatch(Mesh, FLinearColor::White);
            for (int32 Slot = 0; Slot < 3; ++Slot)
                Batch.Mesh->SetMaterial(Slot, ModelMaterials[Resource && Slot < 2 ? OreMaterialStart + Slot : Slot]);
            const int32 Palette = Resource ? 2 : Team;
            Batch.Mesh->SetMaterial(3, ModelMaterials[5 + Palette * 2]);
            Batch.Mesh->SetMaterial(4, ModelMaterials[6 + Palette * 2]);
            ModelBatchIndices[Index * 2 + Team] = BatchIndex;
        }
        ++Loaded;
    }
    UE_LOG(LogCinderModels, Verbose, TEXT("Loaded %d/%d models in %d ISM batches; cinder.models reports live usage."), Loaded, ModelCount, Batches.Num() - ModelBatchStart);
}

void ACinderBattlefield::LogModelStatus() const
{
    int32 Loaded = 0;
    for (const auto& Mesh : ModelMeshes) if (Mesh) ++Loaded;
    UE_LOG(LogCinderModels, Display, TEXT("CINDERLINE_RENDER_MODELS loaded=%d/%d model_batches=%d rendered_model_entities=%d rendered_fallback_entities=%d"),
        Loaded, ModelCount, Batches.Num() - ModelBatchStart, LastModelEntities, LastFallbackEntities);
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

void ACinderBattlefield::BeginPlay()
{
    Super::BeginPlay();
    BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Generated/M_CinderTint.M_CinderTint"));
    if (!BaseMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cinderline generated material missing. Run scripts/unreal.sh bootstrap before playing."));
        BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    }
    AddBatch(Cube, FLinearColor(0.065f, 0.12f, 0.13f));
    if (auto* GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Generated/M_CinderGround.M_CinderGround")))
        Batches[0].Mesh->SetMaterial(0, GroundMaterial);
    AddBatch(Cube, FLinearColor(0.14f, 0.21f, 0.22f));
    AddBatch(Cone, FLinearColor(1.0f, 0.52f, 0.12f));
    AddBatch(Cube, FLinearColor(0.012f, 0.022f, 0.035f));
    AddBatch(Cube, FLinearColor(0.034f, 0.067f, 0.080f));
    for (int Team = 0; Team < 2; ++Team)
    {
        const FLinearColor Color = Team == 0 ? FLinearColor(0.04f, 0.82f, 0.72f) : FLinearColor(0.96f, 0.24f, 0.17f);
        for (UStaticMesh* Shape : { Cube.Get(), Cylinder.Get(), Cone.Get(), Sphere.Get() }) AddBatch(Shape, Color);
    }
    for (int Team = 0; Team < 2; ++Team)
        for (UStaticMesh* Shape : { Cube.Get(), Cylinder.Get(), Cone.Get(), Sphere.Get() })
            AddBatch(Shape, Team == 0 ? FLinearColor(0.68f, 1.0f, 0.92f) : FLinearColor(1.0f, 0.68f, 0.28f));
    LoadModelBatches();

    auto* Sun = GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-58, -32, 0));
    Sun->GetLightComponent()->SetIntensity(3.0f);
    Sun->GetLightComponent()->SetCastShadows(false);
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
    SkyComponent->SetIntensity(2.0f);
    Simulation.reset();
    ResetFeedback();
    RenderState();
}

void ACinderBattlefield::StartMatch(int MapIndex)
{
    SetActorTickEnabled(true);
    CurrentMap = FMath::Clamp(MapIndex, 0, 2);
    cinder::Config Config; Config.map = CurrentMap;
    Simulation.reset(Config);
    ResetFeedback();
    ResourceMemory.clear();
    bMenu = false; bPaused = false;
    RenderState();
}

void ACinderBattlefield::ReturnToMenu()
{
    SetActorTickEnabled(true);
    bMenu = true; bPaused = false;
    // Keep the completed match counters available for diagnostics while skipping stale effects.
    ResetFeedback(false);
}

void ACinderBattlefield::ResetFeedback(bool bClearCombatCounters)
{
    AudioStatsSnapshot = Simulation.players()[0].stats;
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
    if (!bMenu && !bPaused)
    {
        if (Simulation.winner() < 0) Simulation.update(FMath::Min(DeltaSeconds, 0.2f));
        UpdateCompletionAudio();
        UpdateCombatFeedback();
    }
    RenderTimer += DeltaSeconds;
    if (RenderTimer >= cinder::Simulation::Step) { RenderTimer = 0; RenderState(); }
}

void ACinderBattlefield::AddEntity(const cinder::Entity& Entity)
{
    using namespace cinder;
    const Definition& Def = definition(Entity.kind);
    const bool Resource = Entity.kind == Kind::Resource;
    if (Resource)
    {
        if (!Simulation.explored(0, Entity.pos) || Entity.resource <= 0) return;
    }
    else if (Entity.team != 0 && !Simulation.visible(0, Entity.pos)) return;
    const float Elevation = Def.air ? 125.0f : 0.0f;
    const float BuildScale = Def.building ? FMath::Max(0.08f, Entity.progress) : 1;
    const FQuat Facing = FRotator(0, FMath::RadiansToDegrees(Entity.facing), 0).Quaternion();
    const int32 ModelKey = static_cast<int32>(Entity.kind) * 2 + (Resource ? 0 : Entity.team);
    if (ModelBatchIndices.IsValidIndex(ModelKey) && ModelBatchIndices[ModelKey] != INDEX_NONE)
    {
        // The imported centimeter mesh is already sized to the definition and has a bottom pivot.
        Batches[ModelBatchIndices[ModelKey]].Transforms.Add(FTransform(Facing, FVector(Entity.pos.x, Entity.pos.y, Elevation), FVector(1, 1, BuildScale)));
        ++LastModelEntities;
        return;
    }
    ++LastFallbackEntities;
    if (Entity.kind == Kind::Resource)
    {
        for (int I = 0; I < 3; ++I)
        {
            const float A = I * 2.0944f;
            Batches[2].Transforms.Add(FTransform(FRotator(0, I * 120, I * 8), FVector(Entity.pos.x + FMath::Cos(A) * 15, Entity.pos.y + FMath::Sin(A) * 15, 34), FVector(0.25f, 0.3f, 0.65f + I * 0.1f)));
        }
        return;
    }
    const float R = Def.radius / 50.0f;
    auto Part = [&](int Shape, FVector Offset, FVector Scale, bool Accent = false, FRotator Rotation = FRotator::ZeroRotator)
    {
        if (Def.building) { Offset.X *= 0.5; Offset.Y *= 0.5; Scale.X *= 0.5; Scale.Y *= 0.5; }
        Offset.Z *= BuildScale; Scale.Z *= BuildScale;
        const FVector Position(Entity.pos.x, Entity.pos.y, Elevation);
        Batches[5 + Entity.team * 4 + Shape + (Accent ? 8 : 0)].Transforms.Add(FTransform(Facing * Rotation.Quaternion(), Position + Facing.RotateVector(Offset), Scale));
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
    LastModelEntities = LastFallbackEntities = 0;
    for (FBatch& Batch : Batches) Batch.Transforms.Reset();
    if (Batches.IsEmpty()) return;
    Batches[0].Transforms.Add(FTransform(FQuat::Identity, FVector(2400, 2400, -16), FVector(49, 49, 0.3f)));
    for (const auto& Obstacle : Simulation.obstacles())
        if (Simulation.explored(0, Obstacle.center))
            Batches[1].Transforms.Add(FTransform(FQuat::Identity, FVector(Obstacle.center.x, Obstacle.center.y, 38), FVector(Obstacle.half.x / 50, Obstacle.half.y / 50, 0.8f)));
    constexpr float Cell = cinder::Simulation::WorldSize / cinder::Simulation::FogSize;
    for (int Y = 0; Y < cinder::Simulation::FogSize; ++Y)
        for (int X = 0; X < cinder::Simulation::FogSize; ++X)
        {
            cinder::Vec2 P{(X + 0.5f) * Cell, (Y + 0.5f) * Cell};
            if (!Simulation.visible(0, P))
                Batches[Simulation.explored(0, P) ? 4 : 3].Transforms.Add(FTransform(FQuat::Identity, FVector(P.x, P.y, 1), FVector(Cell / 100 + 0.001f, Cell / 100 + 0.001f, 0.02f)));
        }
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
    FlushBatches();
}

void ACinderBattlefield::FlushBatches()
{
    for (FBatch& Batch : Batches)
    {
        if (Batch.Mesh->GetInstanceCount() == Batch.Transforms.Num())
        {
            if (!Batch.Transforms.IsEmpty()) Batch.Mesh->BatchUpdateInstancesTransforms(0, Batch.Transforms, false, true, true);
        }
        else
        {
            Batch.Mesh->ClearInstances();
            Batch.Mesh->AddInstances(Batch.Transforms, false, false);
        }
    }
}

bool ACinderBattlefield::SaveMatch() const
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Matches");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*(Directory / TEXT("skirmish.cinder")));
    return Simulation.save(TCHAR_TO_UTF8(*Filename));
}

bool ACinderBattlefield::LoadMatch()
{
    const FString Filename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*(FPaths::ProjectSavedDir() / TEXT("Matches/skirmish.cinder")));
    if (!Simulation.load(TCHAR_TO_UTF8(*Filename))) return false;
    SetActorTickEnabled(true);
    ResetFeedback();
    CurrentMap = Simulation.config().map;
    ResourceMemory.clear();
    bMenu = false; bPaused = false; RenderState(); return true;
}
