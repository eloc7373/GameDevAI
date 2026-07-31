using UnrealBuildTool;
using System.Collections.Generic;

public class MillhavenEditorTarget : TargetRules
{
	public MillhavenEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Millhaven");
	}
}
