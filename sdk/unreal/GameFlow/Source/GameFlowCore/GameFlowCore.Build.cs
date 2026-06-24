using UnrealBuildTool;

public class GameFlowCore : ModuleRules
{
    public GameFlowCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "HTTP", "Json", "Sockets", "Networking" });
        bEnableExceptions = false;
    }
}
