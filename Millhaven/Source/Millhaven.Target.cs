using UnrealBuildTool;
using System.Collections.Generic;

public class MillhavenTarget : TargetRules
{
	public MillhavenTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Millhaven");
	}
}
