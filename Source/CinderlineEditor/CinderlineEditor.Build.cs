using UnrealBuildTool;

public class CinderlineEditor : ModuleRules
{
    public CinderlineEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] { "Cinderline", "Landscape", "RHI", "RenderCore", "UnrealEd" });
    }
}
