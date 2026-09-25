

using UnrealBuildTool;
using System.Collections.Generic;

public class GhostherdEditorTarget : TargetRules
{
	public GhostherdEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;

		ExtraModuleNames.AddRange( new string[] { "Ghostherd" } );

		Name = "GhostherdEditor";
	}
}
