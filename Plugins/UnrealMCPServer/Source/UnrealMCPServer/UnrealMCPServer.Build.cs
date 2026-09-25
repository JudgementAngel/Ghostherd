// Copyright StraySpark Studio 2026. All Rights Reserved.

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

			// Rendering & viewport
			"RenderCore",
			"RHI",
			"ImageWrapper",

			// Platform
			"DesktopPlatform",
			"ApplicationCore",

			// Settings
			"EngineSettings",
			"Settings",          // v4 UI: ISettingsModule (status-bar "Plugin Settings..." action)

			// Niagara particle system (required plugin dependency)
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

			// Physics (PhysicalMaterial class)
			"PhysicsCore",

			// UMG / Widget tools
			"UMG",
			"UMGEditor",

			// PCG - Procedural Content Generation (required plugin dependency)
			"PCG",

			// Enhanced Input System (required plugin dependency)
			"EnhancedInput",
			"InputBlueprintNodes",

			// Gameplay Tags
			"GameplayTags",

			// AI / Behavior Trees
			"AIModule",
			"GameplayTasks",

			// AnimGraph tools (anim blueprint factories, blend spaces, state machine graphs)
			"AnimGraphRuntime",
			"AnimGraph",

			// v4.6 — animation authoring. AnimationBlueprintLibrary is the engine's
			// own editor-side animation editing surface (notifies, notify states,
			// notify tracks, curves, curve metadata, sync markers, root motion and
			// additive settings). Editor module, always present in an editor target.
			"AnimationBlueprintLibrary",
			"AnimationCore",

			// Phase D.2 — Source Control (provider-agnostic; works with whatever provider UE has loaded)
			"SourceControl",

			// Phase D.3 — Test authoring & run (FunctionalTesting for AFunctionalTest;
			// FAutomationTestFramework comes from Core's Misc/AutomationTest.h — no extra dep)
			"FunctionalTesting",

			// v4 Phase 3 — Modeling tools implemented via GeometryScript
			// (boolean/plane-cut/extrude/UV/remesh on UDynamicMesh; engine-shipped plugin)
			"GeometryScriptingCore",
			"GeometryFramework",

			// v4 Phase 3 — Chaos geometry-collection creation + fracture
			// (GeometryCollectionEngine/Chaos/DataflowCore are engine runtime modules;
			// FractureEngine comes from the engine-shipped 'Fracture' plugin)
			"GeometryCollectionEngine",
			"Chaos",
			"DataflowCore",
			"FractureEngine",

			// Note: World Partition classes are part of the Engine module.
			// Note: GameplayAbilities is optional — GAS tools use dynamic class lookup.
			// Note: Networking/Replication uses Engine module classes — no extra dep needed.
			// Note: Material Graph tools use Engine module material expression classes.
		});

		// ---------------------------------------------------------------------
		// v4.5 (UE 5.8) — module deps taken intentionally to close v4 stubs and
		// add 5.8 tool families. Kept here (not above) so the 5.7→5.8 delta is
		// obvious. MetaSound graph mutators are unblocked in 5.8 because
		// FClassInterface left experimental status (FNodeClassMetadata::DefaultInterface
		// now accepts it directly; the old FVertexInterface path is deprecated).
		// ---------------------------------------------------------------------
		if (Target.Version.MajorVersion > 5 || (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8))
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				// Phase 1 — MetaSound graph mutators via FMetaSoundDocumentBuilder
				"MetasoundFrontend",
				"MetasoundGraphCore",
				"MetasoundEngine",
				"MetasoundEditor",

				// Phase 1 — Chaos cloth asset creation (Dataflow-driven in 5.8)
				"ChaosClothAsset",
				"ChaosClothAssetEngine",
				"ChaosClothAssetTools",   // UChaosClothAssetFactory (editor)

				// Phase 1 — Chaos field actors (radial/uniform/noise field nodes)
				"FieldSystemEngine",

				// Phase 2 — unified editor gizmo / interactive tools
				"InteractiveToolsFramework",
				"EditorInteractiveToolsFramework",

				// Phase 2 — Animation Mixer (Sequencer) scripting surface
				"MovieSceneAnimMixer",
				"MovieSceneAnimMixerScripting",

				// v4.6 — Sequencer animation bake/link round trip
				// (USequencerToolsFunctionLibrary::ExportAnimSequence / LinkAnimSequence).
				// Engine-shipped SequencerScripting plugin; declared in the .uplugin
				// so UBT enables it for consumers of this plugin.
				"SequencerScriptingEditor",
			});

			// Phase 2 — Substrate, Mesh Terrain: these use Engine/Renderer-side
			// classes reached via dynamic lookup + console vars where possible.
			// Add explicit modules here once names are confirmed against the
			// installed 5.8 build (e.g. "MeshTerrain"). Left as a documented
			// extension point to avoid pinning an unverified module name.

			// Phase 4 — interop with Epic's first-party MCP plugin. SOFT dependency:
			// include path only (no link), accessed at runtime via FModuleManager so
			// this plugin still loads when the experimental ModelContextProtocol
			// plugin is disabled. (Same pattern as PythonScriptPlugin below.)
			PrivateIncludePathModuleNames.Add("ModelContextProtocol");
		}

		// Python scripting - soft dependency via interface header
		PrivateIncludePathModuleNames.Add("PythonScriptPlugin");

		// UMGEditor private headers (K2Node_CreateWidget is in private/Nodes/)
		string UMGEditorPrivate = System.IO.Path.Combine(EngineDirectory, "Source", "Editor", "UMGEditor", "Private");
		if (System.IO.Directory.Exists(UMGEditorPrivate))
		{
			PrivateIncludePaths.Add(UMGEditorPrivate);
		}

		// v4 Phase 0: exceptions enabled so the tool registry can wrap handler
		// execution in try/catch — a throwing tool returns a structured error
		// instead of taking down the editor. (Access violations/asserts are still
		// fatal; this guards std::exception-style failures from tool code and
		// third-party libs.)
		bEnableExceptions = true;
		bUseUnity = true;

		PublicDefinitions.Add("WITH_UNREAL_MCP_SERVER=1");
	}
}
