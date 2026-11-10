// Copyright JTW Studio 2026. All Rights Reserved.

using UnrealBuildTool;

public class RootMotionSourcePlus : ModuleRules
{
	public RootMotionSourcePlus(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings"
		});
	}
}
