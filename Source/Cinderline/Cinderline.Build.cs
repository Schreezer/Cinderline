using UnrealBuildTool;

public class Cinderline : ModuleRules
{
    public Cinderline(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
        PrivateDependencyModuleNames.AddRange(new string[] { "ApplicationCore", "Landscape", "RenderCore", "RHI", "Slate", "SlateCore", "WebSockets", "Json", "Sockets" });
        if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.IOS)
        {
            PrivateDependencyModuleNames.Add("CinderMetalFX");
            PublicFrameworks.Add("Foundation");
        }
    }
}
