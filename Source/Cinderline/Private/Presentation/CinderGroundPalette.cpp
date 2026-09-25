#include "Presentation/CinderGroundPalette.h"

#include "Landscape.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
const CinderGroundPalette::FPalette GActive;

struct FVectorEntry { const TCHAR* Name; FLinearColor Value; };

TArray<FVectorEntry> VectorEntries(const CinderGroundPalette::FPalette& P)
{
    return {
        {TEXT("SandTint"), P.SandTint},
        {TEXT("Windblown dust tint"), P.WindblownDust},
        {TEXT("Exposed sandstone tint"), P.ExposedSandstone},
        {TEXT("Mineral staining tint"), P.MineralStaining},
        {TEXT("Packed service ground tint"), P.PackedServiceGround},
        {TEXT("ValleyShade"), P.ValleyShade},
        {TEXT("CrestShade"), P.CrestShade},
        {TEXT("CliffFaceShade"), P.CliffFaceShade},
        {TEXT("CanyonShadow"), P.CanyonShadow},
        {TEXT("CanyonSandstone"), P.CanyonSandstone},
    };
}
}

namespace CinderGroundPalette
{
const FPalette& Active() { return GActive; }

void Apply(UMaterialInstanceDynamic* GroundSurface)
{
    if (!GroundSurface) return;
    for (const FVectorEntry& Entry : VectorEntries(GActive))
        GroundSurface->SetVectorParameterValue(Entry.Name, Entry.Value);
    GroundSurface->SetScalarParameterValue(TEXT("Desaturation"), GActive.Desaturation);
}

void Apply(ALandscape* Landscape)
{
    if (!Landscape) return;
    // SetLandscapeMaterial*ParameterValue reaches the landscape's own dynamic
    // instances on both the desktop and mobile paths, so unlike texture binding
    // this needs no separate ES3_1 branch.
    for (const FVectorEntry& Entry : VectorEntries(GActive))
        Landscape->SetLandscapeMaterialVectorParameterValue(Entry.Name, Entry.Value);
    Landscape->SetLandscapeMaterialScalarParameterValue(TEXT("Desaturation"), GActive.Desaturation);
}
}
