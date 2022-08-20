// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ycExtraAnimNodeEditor : ModuleRules
{
	public ycExtraAnimNodeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory,"Public")
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory,"Private"),
				System.IO.Path.Combine("ycExtraAnimNode","Private"),
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
                "Core",
                "CoreUObject",
                "Engine",
                "Slate",
                "AnimGraphRuntime",
                "BlueprintGraph",
				"ycExtraAnimNode",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
                "InputCore",
                "SlateCore",
                "EditorFramework",
                "UnrealEd",
                "GraphEditor",
                "PropertyEditor",
                "EditorStyle",
                "ContentBrowser",
                "KismetWidgets",
                "ToolMenus",
                "KismetCompiler",
                "Kismet",
                "EditorWidgets",
                "AnimGraph",
                "Persona",
				// ... add private dependencies that you statically link with here ...	
			}
			);

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "Persona",
                "SkeletonEditor",
                "AdvancedPreviewScene",
                "AnimationBlueprintEditor",
            }
        );



		if (Target.bBuildEditor == true)
        {
            PublicDependencyModuleNames.AddRange(new string[]
            {
				"AssetTools"
			});
        }
	}
}
