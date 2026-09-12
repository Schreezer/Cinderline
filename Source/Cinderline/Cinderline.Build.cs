using UnrealBuildTool;

public class Cinderline : ModuleRules
{
    public Cinderline(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
        PrivateDependencyModuleNames.AddRange(new string[] { "ApplicationCore", "RenderCore", "RHI", "Slate", "SlateCore", "WebSockets", "Json" });
    }
}
