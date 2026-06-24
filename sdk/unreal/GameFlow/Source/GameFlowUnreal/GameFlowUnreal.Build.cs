using UnrealBuildTool;

public class GameFlowUnreal : ModuleRules
{
    public GameFlowUnreal(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "GameFlowCore" });
        bEnableExceptions = false;
    }
}
