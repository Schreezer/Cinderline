#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CinderLandscapeAuthoringLibrary.generated.h"

/** Deterministic editor entry point used by the Unreal Python asset pipeline. */
UCLASS()
class CINDERLINEEDITOR_API UCinderLandscapeAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Cinderline|Editor")
    static bool AuthorFrontierLandscapes(FString& OutReport);

    /** Finalize and validate the render resources after render-capable asset import. */
    UFUNCTION(BlueprintCallable, Category="Cinderline|Editor")
    static bool FinalizeCanyonAssets(FString& OutReport);
};
