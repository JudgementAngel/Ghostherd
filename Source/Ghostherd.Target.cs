

using UnrealBuildTool;
using System.Collections.Generic;

public class GhostherdTarget : TargetRules
{
	public GhostherdTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V2;

		ExtraModuleNames.AddRange( new string[] { "Ghostherd" } );
	}
}
