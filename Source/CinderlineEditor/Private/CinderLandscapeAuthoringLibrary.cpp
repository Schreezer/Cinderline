#include "CinderLandscapeAuthoringLibrary.h"

#include "AssetCompilingManager.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "MaterialShared.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/App.h"
#include "Presentation/CinderLandscapeTerrain.h"
#include "ShaderCompiler.h"

namespace
{
constexpr TCHAR FrontierMapPath[] = TEXT("/Game/Maps/Frontier");
constexpr TCHAR CanyonMaterialPath[] =
    TEXT("/Game/Art/Canyon/Materials/M_CinderCanyonGround.M_CinderCanyonGround");

bool ValidateMaterialResource(UMaterialInterface* Material, const FString& Label, FString& OutError)
{
    if (!Material)
    {
        OutError = Label + TEXT(" has no material interface.");
        return false;
    }
    const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
    if (!Resource)
    {
        OutError = Label + TEXT(" has no material resource for the active shader platform.");
        return false;
    }
    if (!Resource->IsCompilationFinished())
    {
        OutError = Label + TEXT(" material compilation did not finish.");
        return false;
    }
    if (!Resource->GetCompileErrors().IsEmpty())
    {
        OutError = FString::Printf(TEXT("%s has %d material compile error(s): %s"), *Label,
            Resource->GetCompileErrors().Num(), *FString::Join(Resource->GetCompileErrors(), TEXT(" | ")));
        return false;
    }
    const FMaterialShaderMap* ShaderMap = Resource->GetGameThreadShaderMap();
    if (!ShaderMap || !ShaderMap->IsValidForRendering())
    {
        OutError = Label + TEXT(" has no valid renderable shader map.");
        return false;
    }
    return true;
}
}

bool UCinderLandscapeAuthoringLibrary::AuthorFrontierLandscapes(FString& OutReport)
{
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(FrontierMapPath);
    if (!World)
    {
        OutReport = TEXT("Could not load /Game/Maps/Frontier.");
        return false;
    }
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
        CanyonMaterialPath);
    if (!Material)
    {
        OutReport = TEXT("Missing /Game/Art/Canyon/Materials/M_CinderCanyonGround.");
        return false;
    }

    TArray<ALandscape*> Previous;
    for (TActorIterator<ALandscape> It(World); It; ++It)
        for (int32 Map = 0; Map < CinderLandscapeTerrain::MapCount; ++Map)
            if (It->ActorHasTag(CinderLandscapeTerrain::MapTag(Map))) { Previous.Add(*It); break; }
    for (ALandscape* Landscape : Previous) World->DestroyActor(Landscape);

    for (int32 Map = 0; Map < CinderLandscapeTerrain::MapCount; ++Map)
    {
        TArray<uint16> Heights;
        CinderLandscapeTerrain::BuildCanonicalHeightData(Map, Heights);

        ALandscape* Landscape = World->SpawnActor<ALandscape>(
            FVector(0, 0, CinderLandscapeTerrain::BaselineZ), FRotator::ZeroRotator);
        if (!Landscape)
        {
            OutReport = FString::Printf(TEXT("Could not spawn map %d Landscape."), Map);
            return false;
        }
        Landscape->SetActorLabel(FString::Printf(TEXT("CinderLandscapeMap%d"), Map));
        Landscape->Tags = {
            CinderLandscapeTerrain::MapTag(Map),
            CinderLandscapeTerrain::GeometryTag(CinderLandscapeTerrain::CanonicalGeometrySignature(Map))
        };
        Landscape->SetActorScale3D(FVector(CinderLandscapeTerrain::VertexSpacing(),
            CinderLandscapeTerrain::VertexSpacing(), 1.0f));
        Landscape->LandscapeMaterial = Material;
        Landscape->bUseDynamicMaterialInstance = true;
        Landscape->bUsedForNavigation = false;
        Landscape->SetActorEnableCollision(false);

        const FGuid ImportLayer;
        TMap<FGuid, TArray<uint16>> HeightMaps;
        HeightMaps.Add(ImportLayer, MoveTemp(Heights));
        TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayers;
        MaterialLayers.Add(ImportLayer, {});
        TArray<FLandscapeLayer> EditLayers;
        Landscape->Import(FGuid::NewGuid(), 0, 0,
            CinderLandscapeTerrain::QuadsPerAxis, CinderLandscapeTerrain::QuadsPerAxis,
            CinderLandscapeTerrain::SubsectionsPerComponent, CinderLandscapeTerrain::SubsectionSizeQuads,
            HeightMaps, nullptr, MaterialLayers, ELandscapeImportAlphamapType::Additive, EditLayers);

        TInlineComponentArray<UPrimitiveComponent*> Components(Landscape);
        for (UPrimitiveComponent* Component : Components)
        {
            // This small fixed grid keeps its flat guard at every camera zoom.
            // Coarse height mips can interpolate relief into playable lanes.
            if (auto* TerrainComponent = Cast<ULandscapeComponent>(Component))
                TerrainComponent->SetForcedLOD(0);
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Component->SetCanEverAffectNavigation(false);
            Component->SetCastShadow(false);
            Component->SetAffectDistanceFieldLighting(false);
            Component->SetVisibleInRayTracing(false);
        }
        Landscape->SetActorHiddenInGame(true);
    }
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, FrontierMapPath))
    {
        OutReport = TEXT("Landscape actors were authored but /Game/Maps/Frontier could not be saved.");
        return false;
    }
    OutReport = TEXT("Authored three canonical collisionless 127x127 Cinderline Landscapes in /Game/Maps/Frontier.");
    return true;
}

bool UCinderLandscapeAuthoringLibrary::FinalizeCanyonAssets(FString& OutReport)
{
    if (!FApp::CanEverRender())
    {
        OutReport = TEXT("Canyon finalization requires a render-capable editor process.");
        return false;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(FrontierMapPath);
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, CanyonMaterialPath);
    if (!World || !Material)
    {
        OutReport = TEXT("Could not load the Frontier map and owned canyon ground material.");
        return false;
    }

    TArray<ALandscape*> Landscapes;
    for (TActorIterator<ALandscape> It(World); It; ++It)
    {
        bool bOwned = false;
        for (int32 Map = 0; Map < CinderLandscapeTerrain::MapCount; ++Map)
            bOwned |= It->ActorHasTag(CinderLandscapeTerrain::MapTag(Map));
        if (bOwned) Landscapes.Add(*It);
    }
    if (Landscapes.Num() != CinderLandscapeTerrain::MapCount)
    {
        OutReport = FString::Printf(TEXT("Expected %d owned Landscapes, found %d."),
            CinderLandscapeTerrain::MapCount, Landscapes.Num());
        return false;
    }

    for (ALandscape* Landscape : Landscapes)
        Landscape->UpdateAllComponentMaterialInstances();
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    FAssetCompilingManager::Get().FinishAllCompilation();

    FString Error;
    if (!ValidateMaterialResource(Material, TEXT("M_CinderCanyonGround"), Error))
    {
        OutReport = MoveTemp(Error);
        return false;
    }
    int32 ComponentCount = 0;
    int32 ResourceCount = 0;
    for (ALandscape* Landscape : Landscapes)
    {
        TInlineComponentArray<ULandscapeComponent*> Components(Landscape);
        ComponentCount += Components.Num();
        for (ULandscapeComponent* Component : Components)
        {
            if (!Component || Component->MaterialInstances.IsEmpty())
            {
                OutReport = FString::Printf(TEXT("%s has a Landscape component without material instances."),
                    *Landscape->GetActorNameOrLabel());
                return false;
            }
            for (UMaterialInterface* ComponentMaterial : Component->MaterialInstances)
            {
                const FString Label = FString::Printf(TEXT("%s component material %d"),
                    *Landscape->GetActorNameOrLabel(), ResourceCount);
                if (!ValidateMaterialResource(ComponentMaterial, Label, Error))
                {
                    OutReport = MoveTemp(Error);
                    return false;
                }
                ++ResourceCount;
            }
        }
    }

    TArray<UPackage*> OwnedPackages{World->GetOutermost(), Material->GetOutermost()};
    if (!UEditorLoadingAndSavingUtils::SavePackages(OwnedPackages, false))
    {
        OutReport = TEXT("Validated canyon shaders but could not save the owned map and material packages.");
        return false;
    }
    OutReport = FString::Printf(TEXT("Finalized %d owned Landscapes, %d components and %d renderable component materials."),
        Landscapes.Num(), ComponentCount, ResourceCount);
    return true;
}
