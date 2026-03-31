using UnrealBuildTool;

public class UnrealMCPServer : ModuleRules
{
	public UnrealMCPServer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Json",
			"JsonUtilities",
			"HTTPServer",
			"HTTP",
			"Slate",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor core
			"UnrealEd",
			"EditorSubsystem",
			"LevelEditor",
			"EditorFramework",
			"ToolMenus",
			"DeveloperSettings",
			"Projects",

			// Asset management
			"AssetTools",
			"AssetRegistry",
			"ContentBrowser",
			"ContentBrowserData",

			// Blueprint & graph
			"Kismet",
			"KismetCompiler",
			"BlueprintGraph",
			"GraphEditor",

			// Scripting utilities
			"EditorScriptingUtilities",

			// Rendering & viewport
			"RenderCore",
			"RHI",
			"ImageWrapper",

			// Platform
			"DesktopPlatform",
			"ApplicationCore",

			// Settings
			"EngineSettings",

			// Niagara particle system
			"Niagara",

			// Landscape terrain system
			"Landscape",
			"LandscapeEditor",

			// Foliage system
			"Foliage",

			// Sequencer / MovieScene tools
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
			"SequencerCore",

			// Navigation system
			"NavigationSystem",

			// UMG / Widget tools
			"UMG",
			"UMGEditor",

			// PCG (Procedural Content Generation)
			"PCG",

			// Enhanced Input System
			"EnhancedInput",

			// AI / Behavior Trees
			"AIModule",
			"GameplayTasks",

			// AnimGraph tools (anim blueprint factories, blend spaces, state machine graphs)
			"AnimGraphRuntime",
			"AnimGraph",

			// Note: World Partition classes (UWorldPartition, UDataLayerManager, etc.)
			// are part of the Engine module — no separate dependency needed.
			// Note: GameplayAbilities is optional — GAS tools use dynamic class lookup.
			// Note: MetaSound tools use dynamic class loading — no compile-time dep needed.
			// Note: Networking/Replication uses Engine module classes — no extra dep needed.
			// Note: Material Graph tools use Engine module material expression classes.
		});

		// Python scripting - soft dependency via interface header
		PrivateIncludePathModuleNames.Add("PythonScriptPlugin");

		bEnableExceptions = false;
		bUseUnity = true;

		PublicDefinitions.Add("WITH_UNREAL_MCP_SERVER=1");
	}
}
