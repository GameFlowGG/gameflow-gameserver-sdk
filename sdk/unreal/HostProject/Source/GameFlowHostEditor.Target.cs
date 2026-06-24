using UnrealBuildTool;

public class GameFlowHostEditorTarget : TargetRules
{
    public GameFlowHostEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("GameFlowHost");
    }
}
