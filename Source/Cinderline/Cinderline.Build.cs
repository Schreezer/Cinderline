using UnrealBuildTool;

public class Cinderline : ModuleRules
{
    public Cinderline(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // File-local helpers must stay local regardless of Git/adaptive unity
        // grouping. The portable simulation also compiles each source separately.
        bUseUnity = false;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
        PrivateDependencyModuleNames.AddRange(new string[] { "ApplicationCore", "Landscape", "MeshDescription", "StaticMeshDescription", "RenderCore", "RHI", "Slate", "SlateCore", "WebSockets", "Json", "Sockets", "CinderVoiceAudio" });
        if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.IOS)
        {
            PrivateDependencyModuleNames.Add("CinderMetalFX");
            PublicFrameworks.Add("Foundation");
        }
    }
}
