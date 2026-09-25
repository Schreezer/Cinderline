using UnrealBuildTool;

public class CinderVoiceAudio : ModuleRules
{
    public CinderVoiceAudio(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.Add("Core");
        if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.IOS)
        {
            bEnableObjCAutomaticReferenceCounting = true;
            PublicFrameworks.AddRange(new string[] { "Foundation", "AVFoundation", "AVFAudio", "AudioToolbox" });
        }
    }
}
