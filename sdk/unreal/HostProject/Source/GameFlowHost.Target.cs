using UnrealBuildTool;

public class GameFlowHostTarget : TargetRules
{
    public GameFlowHostTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("GameFlowHost");
    }
}
