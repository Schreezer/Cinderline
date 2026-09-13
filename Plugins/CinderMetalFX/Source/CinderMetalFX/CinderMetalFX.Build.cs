using System.IO;
using UnrealBuildTool;

public class CinderMetalFX : ModuleRules
{
    public CinderMetalFX(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[] { "Core" });
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
            "Engine",
            "MetalRHI",
            "RenderCore",
            "Renderer",
            "RHI"
        });

        AddEngineThirdPartyPrivateStaticDependencies(Target, "MetalCPP");
        PublicWeakFrameworks.Add("MetalFX");

        // ISpatialUpscaler remains a Renderer-private extension point in UE 5.8.
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private"));
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Internal"));

        string BridgeHeader = Path.Combine(
            EngineDirectory,
            "Source/Runtime/Apple/MetalRHI/Public/MetalRHIContext.h");
        bool bHasCinderBridge = File.Exists(BridgeHeader) &&
            File.ReadAllText(BridgeHeader).Contains("CinderMetalRHIRunOnCurrentCommandBuffer");
        PublicDefinitions.Add("CINDER_METALFX_ENGINE_BRIDGE=" + (bHasCinderBridge ? "1" : "0"));
    }
}
