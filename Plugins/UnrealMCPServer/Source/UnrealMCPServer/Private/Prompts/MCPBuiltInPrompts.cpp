#include "Prompts/MCPBuiltInPrompts.h"
#include "MCPPromptProvider.h"
#include "MCPProtocol.h"

namespace MCPBuiltInPrompts
{

void RegisterAll(FMCPPromptProvider& Provider)
{
	// ================================================================
	// world_builder - System prompt for building 3D environments
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("world_builder");
		Def.Description = TEXT("System prompt with best practices for building 3D environments in Unreal Engine. Includes conventions for actor placement, scale, lighting, and organization.");

		FMCPPromptArgument StyleArg;
		StyleArg.Name = TEXT("style");
		StyleArg.Description = TEXT("Environment style: realistic, stylized, sci-fi, fantasy, horror");
		StyleArg.bRequired = false;
		Def.Arguments.Add(StyleArg);

		FMCPPromptArgument ScaleArg;
		ScaleArg.Name = TEXT("scale");
		ScaleArg.Description = TEXT("Scale of the environment: room, building, city_block, landscape");
		ScaleArg.bRequired = false;
		Def.Arguments.Add(ScaleArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Arguments) -> TArray<FMCPPromptMessage>
		{
			FString Style = TEXT("realistic");
			if (const FString* Val = Arguments.Find(TEXT("style"))) Style = *Val;

			FString Scale = TEXT("room");
			if (const FString* Val = Arguments.Find(TEXT("scale"))) Scale = *Val;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"You are a world builder for Unreal Engine 5.7. Follow these conventions:\n\n"
				"STYLE: %s\n"
				"SCALE: %s\n\n"
				"PLACEMENT RULES:\n"
				"- UE uses centimeters. 1 unit = 1 cm. A door is ~200cm tall, ~100cm wide.\n"
				"- Y-axis is right, X-axis is forward, Z-axis is up.\n"
				"- Place floors at Z=0. Standard floor height is 300-400 cm.\n"
				"- Use folders to organize actors (e.g., 'Geometry/Walls', 'Lighting/Main').\n"
				"- Tag actors for easy filtering (e.g., 'exterior', 'interactive').\n\n"
				"LIGHTING RULES:\n"
				"- Start with a DirectionalLight for sun/moon (yaw ~300, pitch ~-45).\n"
				"- Add a SkyLight for ambient. Set to 'Captured Scene' for reflections.\n"
				"- Use PointLights for interiors (Intensity ~5-20, Attenuation ~500-1000).\n"
				"- SpotLights for focused areas (Inner angle ~15, Outer ~30).\n\n"
				"WORKFLOW:\n"
				"1. First use list_actors/get_level_info to understand the current scene.\n"
				"2. Create a blockout with simple geometry before detailing.\n"
				"3. Set mobility to Static for baked lighting, Movable for dynamic.\n"
				"4. Group related actors in folders for clean hierarchy.\n"
				"5. Use take_screenshot to verify placement visually.\n"
			), *Style, *Scale));

			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// blueprint_architect - System prompt for Blueprint development
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("blueprint_architect");
		Def.Description = TEXT("System prompt with best practices for Blueprint development in Unreal Engine. Includes naming conventions, component patterns, and variable usage.");

		FMCPPromptArgument TypeArg;
		TypeArg.Name = TEXT("blueprint_type");
		TypeArg.Description = TEXT("Type of Blueprint: actor, character, game_mode, widget, component");
		TypeArg.bRequired = false;
		Def.Arguments.Add(TypeArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Arguments) -> TArray<FMCPPromptMessage>
		{
			FString BPType = TEXT("actor");
			if (const FString* Val = Arguments.Find(TEXT("blueprint_type"))) BPType = *Val;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"You are a Blueprint architect for Unreal Engine 5.7. Building: %s\n\n"
				"NAMING CONVENTIONS:\n"
				"- Blueprints: BP_<Name> (e.g., BP_HealthPickup)\n"
				"- Widgets: WBP_<Name> (e.g., WBP_MainMenu)\n"
				"- Materials: M_<Name>, Material Instances: MI_<Name>\n"
				"- Variables: PascalCase (e.g., MaxHealth, bIsActive)\n"
				"- Boolean variables: prefix with 'b' (e.g., bCanJump)\n\n"
				"COMPONENT HIERARCHY:\n"
				"- Root: DefaultSceneRoot (or the main mesh)\n"
				"- Organize logically: Mesh > Collision > Effects > Interaction\n"
				"- Set appropriate mobility per component.\n\n"
				"VARIABLE BEST PRACTICES:\n"
				"- Mark gameplay-tuning variables as Instance Editable.\n"
				"- Use categories to group variables logically.\n"
				"- Add tooltips for complex variables.\n"
				"- Use private variables for internal state.\n\n"
				"WORKFLOW:\n"
				"1. create_blueprint to create the asset.\n"
				"2. add_component to build the component hierarchy.\n"
				"3. set_component_property to configure defaults.\n"
				"4. add_variable for Blueprint variables.\n"
				"5. compile_blueprint to validate.\n"
				"6. spawn_blueprint to place in the level.\n"
			), *BPType));

			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// debug_scene - Template for diagnosing scene issues
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("debug_scene");
		Def.Description = TEXT("Step-by-step template for diagnosing common scene issues: missing actors, incorrect transforms, lighting problems, and performance.");

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Arguments) -> TArray<FMCPPromptMessage>
		{
			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(TEXT(
				"Debug the current scene systematically:\n\n"
				"1. SCENE OVERVIEW: Use get_level_info to understand the level state.\n"
				"2. ACTOR AUDIT: Use list_actors to find all actors and check for issues.\n"
				"3. VISUAL CHECK: Use take_screenshot to see the current viewport state.\n"
				"4. TRANSFORM VERIFY: Check actor positions are reasonable (not at origin, not overlapping).\n"
				"5. PROPERTY CHECK: Use get_actor_properties on suspicious actors.\n"
				"6. LOG REVIEW: Check unreal://editor/log resource for recent errors.\n\n"
				"COMMON ISSUES:\n"
				"- Actors at (0,0,0): Probably spawned without position.\n"
				"- Invisible actors: Check Hidden property, scale, or materials.\n"
				"- Dark scene: Missing lights or lights at wrong position.\n"
				"- Performance: Too many movable actors or overdraw.\n"
			));

			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// create_game_level - Level design workflow
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("create_game_level");
		Def.Description = TEXT("Step-by-step workflow for creating a complete game level from scratch with proper structure, lighting, and gameplay elements.");

		FMCPPromptArgument GenreArg;
		GenreArg.Name = TEXT("genre");
		GenreArg.Description = TEXT("Game genre: fps, tps, platformer, horror, puzzle, rpg");
		GenreArg.bRequired = false;
		Def.Arguments.Add(GenreArg);

		FMCPPromptArgument ThemeArg;
		ThemeArg.Name = TEXT("theme");
		ThemeArg.Description = TEXT("Visual theme: industrial, medieval, sci-fi, nature, urban, dungeon");
		ThemeArg.bRequired = false;
		Def.Arguments.Add(ThemeArg);

		FMCPPromptArgument SizeArg;
		SizeArg.Name = TEXT("size");
		SizeArg.Description = TEXT("Level size: small, medium, large");
		SizeArg.bRequired = false;
		Def.Arguments.Add(SizeArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Genre = TEXT("fps");
			if (const FString* V = Args.Find(TEXT("genre"))) Genre = *V;
			FString Theme = TEXT("industrial");
			if (const FString* V = Args.Find(TEXT("theme"))) Theme = *V;
			FString Size = TEXT("medium");
			if (const FString* V = Args.Find(TEXT("size"))) Size = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"Create a %s game level with %s theme at %s scale.\n\n"
				"STEP 1 - FOUNDATION:\n"
				"- new_level to start fresh, or get_level_info to assess existing.\n"
				"- create_landscape or place floor geometry (SM_Floor at Z=0).\n"
				"- Add PlayerStart at a good spawn point.\n\n"
				"STEP 2 - STRUCTURE:\n"
				"- Block out walls, corridors, rooms with simple geometry.\n"
				"- Use folders: Geometry/Floors, Geometry/Walls, Geometry/Props.\n"
				"- Standard door: 200cm tall, 100cm wide. Ceiling: 300-400cm.\n\n"
				"STEP 3 - LIGHTING:\n"
				"- Exterior: DirectionalLight (sun) + SkyLight + SkyAtmosphere.\n"
				"- Interior: PointLights (intensity 5-20cd, radius 500-1500cm).\n"
				"- Accent: SpotLights for focal points.\n\n"
				"STEP 4 - ENVIRONMENT:\n"
				"- PostProcessVolume (infinite extent) for color grading.\n"
				"- ExponentialHeightFog for atmosphere.\n"
				"- Foliage and props for detail.\n\n"
				"STEP 5 - GAMEPLAY:\n"
				"- NavMeshBoundsVolume + build_navigation for AI.\n"
				"- Trigger volumes for events.\n"
				"- Pickup items, interactive objects.\n\n"
				"STEP 6 - VERIFY:\n"
				"- take_screenshot from multiple angles.\n"
				"- Check unreal://editor/performance for frame budget.\n"
				"- save_level when satisfied.\n"
			), *Genre, *Theme, *Size));

			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// setup_character - Character creation guide
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("setup_character");
		Def.Description = TEXT("Guide for creating a playable character Blueprint with movement, camera, and input setup.");

		FMCPPromptArgument TypeArg;
		TypeArg.Name = TEXT("type");
		TypeArg.Description = TEXT("Camera perspective: fps, tps, top_down, side_scroller");
		TypeArg.bRequired = false;
		Def.Arguments.Add(TypeArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Type = TEXT("tps");
			if (const FString* V = Args.Find(TEXT("type"))) Type = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"Create a %s playable character Blueprint.\n\n"
				"1. create_blueprint with parent 'Character' -> BP_PlayerCharacter\n"
				"2. COMPONENTS:\n"
				"   - CapsuleComponent (root, height=96, radius=42)\n"
				"   - SkeletalMeshComponent for character model\n"
				"   - SpringArmComponent (length=300 for TPS, 0 for FPS)\n"
				"   - CameraComponent attached to spring arm\n"
				"   - CharacterMovementComponent (auto-included)\n\n"
				"3. VARIABLES:\n"
				"   - MoveSpeed (Float, default 600)\n"
				"   - LookSensitivity (Float, default 1.0)\n"
				"   - bCanSprint (Boolean, default true)\n"
				"   - MaxHealth (Float, default 100)\n"
				"   - CurrentHealth (Float, default 100)\n\n"
				"4. EVENT GRAPH:\n"
				"   - BeginPlay: Set defaults, enable input\n"
				"   - Tick: Handle movement input\n"
				"   - Custom events: TakeDamage, Heal, Die\n\n"
				"5. compile_blueprint then spawn_blueprint to test.\n"
			), *Type));

			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// debug_performance - Performance investigation
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("debug_performance");
		Def.Description = TEXT("Systematic performance investigation workflow for identifying and fixing common UE5 performance bottlenecks.");

		FMCPPromptArgument TargetFPS;
		TargetFPS.Name = TEXT("target_fps");
		TargetFPS.Description = TEXT("Target framerate: 30, 60, 120");
		TargetFPS.bRequired = false;
		Def.Arguments.Add(TargetFPS);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Target = TEXT("60");
			if (const FString* V = Args.Find(TEXT("target_fps"))) Target = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"Performance target: %s FPS (budget: %.1fms)\n\n"
				"STEP 1 - BASELINE:\n"
				"- Read unreal://editor/performance for current FPS/memory.\n"
				"- run_console_command 'stat fps' and 'stat unit' for detailed timing.\n\n"
				"STEP 2 - IDENTIFY BOTTLENECK:\n"
				"- 'stat scenerendering': Check draw calls, triangle count.\n"
				"- 'stat gpu': GPU thread timing breakdown.\n"
				"- 'stat game': Game thread timing.\n\n"
				"STEP 3 - COMMON FIXES:\n"
				"- Too many draw calls: Merge static actors, use instancing.\n"
				"- Too many triangles: Add LODs, use Nanite for static meshes.\n"
				"- Shadows expensive: Reduce shadow-casting lights, use distance cull.\n"
				"- Movable lights: Switch to Static/Stationary where possible.\n"
				"- Material overdraw: Simplify transparent materials.\n\n"
				"STEP 4 - ACTOR AUDIT:\n"
				"- list_actors to count total actors per class.\n"
				"- Check for excessive Movable actors.\n"
				"- Verify actor tick is disabled where not needed.\n\n"
				"STEP 5 - VERIFY FIX:\n"
				"- Re-check unreal://editor/performance after each change.\n"
				"- take_screenshot to confirm visual quality maintained.\n"
			), *Target, 1000.0f / FCString::Atof(*Target)));

			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// review_blueprint - Blueprint quality analysis
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("review_blueprint");
		Def.Description = TEXT("Blueprint code review checklist for identifying quality issues, naming violations, and performance problems.");

		FMCPPromptArgument PathArg;
		PathArg.Name = TEXT("blueprint_path");
		PathArg.Description = TEXT("Content path of the Blueprint to review (e.g., '/Game/Blueprints/BP_MyActor')");
		PathArg.bRequired = true;
		Def.Arguments.Add(PathArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Path = TEXT("/Game/Blueprints/BP_Unknown");
			if (const FString* V = Args.Find(TEXT("blueprint_path"))) Path = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"Review Blueprint: %s\n\n"
				"1. STRUCTURE: Use get_blueprint_info to inspect components, variables, functions.\n\n"
				"2. NAMING CHECK:\n"
				"   - Blueprint: BP_ prefix?\n"
				"   - Variables: PascalCase, booleans with 'b' prefix?\n"
				"   - Functions: verb-first (Get, Set, Calculate, Handle)?\n"
				"   - Components: descriptive names (not default)?\n\n"
				"3. COMPONENT REVIEW:\n"
				"   - Appropriate root component?\n"
				"   - Correct mobility settings?\n"
				"   - No unnecessary components?\n\n"
				"4. VARIABLE REVIEW:\n"
				"   - Instance editable variables have categories?\n"
				"   - Default values set correctly?\n"
				"   - No unused variables?\n\n"
				"5. GRAPH REVIEW (via get_node_pins):\n"
				"   - Tick event used only when necessary?\n"
				"   - No hard references to specific actors?\n"
				"   - Event-driven over polling?\n"
				"   - Error handling on casts?\n\n"
				"6. compile_blueprint to verify no errors/warnings.\n"
			), *Path));

			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// setup_multiplayer - Multiplayer configuration guide
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("setup_multiplayer");
		Def.Description = TEXT("Step-by-step guide for setting up multiplayer networking in an Unreal Engine project.");

		FMCPPromptArgument TopoArg;
		TopoArg.Name = TEXT("topology");
		TopoArg.Description = TEXT("Network topology: listen_server, dedicated_server, p2p");
		TopoArg.bRequired = false;
		Def.Arguments.Add(TopoArg);

		FMCPPromptArgument PlayerArg;
		PlayerArg.Name = TEXT("max_players");
		PlayerArg.Description = TEXT("Maximum number of players: 2, 4, 8, 16, 32, 64");
		PlayerArg.bRequired = false;
		Def.Arguments.Add(PlayerArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Topology = TEXT("listen_server");
			if (const FString* V = Args.Find(TEXT("topology"))) Topology = *V;
			FString Players = TEXT("4");
			if (const FString* V = Args.Find(TEXT("max_players"))) Players = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"Setup %s multiplayer for %s players.\n\n"
				"1. GAME MODE:\n"
				"   - create_blueprint parent=GameModeBase -> BP_GameMode\n"
				"   - Set DefaultPawnClass, PlayerControllerClass\n"
				"   - Configure in Project Settings > Maps & Modes\n\n"
				"2. PLAYER FRAMEWORK:\n"
				"   - create_blueprint parent=PlayerController -> BP_PlayerController\n"
				"   - create_blueprint parent=PlayerState -> BP_PlayerState\n"
				"   - create_blueprint parent=GameStateBase -> BP_GameState\n\n"
				"3. REPLICATION RULES:\n"
				"   - Replicate variables that ALL clients need to see.\n"
				"   - Use RepNotify for variables that trigger visual changes.\n"
				"   - Server RPCs for client requests (e.g., fire weapon).\n"
				"   - Client RPCs for server-to-specific-client (e.g., UI updates).\n"
				"   - NetMulticast for all-clients events (e.g., explosions).\n\n"
				"4. AUTHORITY:\n"
				"   - Server authoritative: validate all gameplay on server.\n"
				"   - Never trust client input directly.\n"
				"   - Use HasAuthority() checks in Blueprints.\n\n"
				"5. TESTING:\n"
				"   - PIE with Net Mode: Play As Listen Server + 1 Client.\n"
				"   - Check Relevant Actors in details for net relevancy.\n"
			), *Topology, *Players));

			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// optimize_materials - Material optimization workflow
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("optimize_materials");
		Def.Description = TEXT("Material optimization checklist for reducing shader complexity and improving rendering performance.");

		FMCPPromptArgument TargetArg;
		TargetArg.Name = TEXT("target");
		TargetArg.Description = TEXT("Target platform: mobile, desktop, console");
		TargetArg.bRequired = false;
		Def.Arguments.Add(TargetArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Target = TEXT("desktop");
			if (const FString* V = Args.Find(TEXT("target"))) Target = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"Optimize materials for %s platform.\n\n"
				"1. AUDIT: Use list_assets class_filter='Material' to find all materials.\n"
				"2. REDUCE INSTRUCTIONS:\n"
				"   - Use Material Instances instead of unique materials.\n"
				"   - Replace complex math with texture lookups.\n"
				"   - Use Fully Rough where specular isn't needed.\n"
				"   - Disable features not visible at distance.\n\n"
				"3. TEXTURE OPTIMIZATION:\n"
				"   - Power-of-2 textures for mipmaps.\n"
				"   - Compress: BC7 for color, BC5 for normals.\n"
				"   - Use texture atlases for small props.\n"
				"   - LOD bias for distant objects.\n\n"
				"4. MATERIAL INSTANCES:\n"
				"   - Create parent materials with parameters.\n"
				"   - Use create_material_instance for variations.\n"
				"   - Scalar/vector params are cheaper than texture switches.\n\n"
				"5. VERIFY:\n"
				"   - 'stat gpu' to check material shader cost.\n"
				"   - take_screenshot to verify visual quality.\n"
			), *Target));

			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// design_ui_layout - UMG UI design patterns
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("design_ui_layout");
		Def.Description = TEXT("Guide for designing UMG widget layouts with proper anchoring, scaling, and interaction patterns.");

		FMCPPromptArgument ScreenArg;
		ScreenArg.Name = TEXT("screen_type");
		ScreenArg.Description = TEXT("Screen type: hud, main_menu, inventory, dialogue, settings, shop");
		ScreenArg.bRequired = false;
		Def.Arguments.Add(ScreenArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Screen = TEXT("hud");
			if (const FString* V = Args.Find(TEXT("screen_type"))) Screen = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"Design a %s UI screen in UMG.\n\n"
				"NAMING: WBP_<ScreenName> (e.g., WBP_MainMenu)\n\n"
				"ANCHORING RULES:\n"
				"- HUD elements: anchor to nearest screen edge.\n"
				"- Health bar: top-left or bottom-left.\n"
				"- Minimap: top-right corner.\n"
				"- Crosshair: center of screen.\n"
				"- Menus: center, stretch to fill with margins.\n\n"
				"LAYOUT PATTERNS:\n"
				"- Use CanvasPanel for absolute positioning (HUD).\n"
				"- Use VerticalBox/HorizontalBox for lists.\n"
				"- Use GridPanel for inventory grids.\n"
				"- Use SizeBox to constrain element dimensions.\n\n"
				"INTERACTION:\n"
				"- Buttons need hover/press/disabled states.\n"
				"- Navigation support for gamepad (focus chain).\n"
				"- Use widget animations for transitions.\n\n"
				"TOOLS:\n"
				"- list_widget_blueprints to find existing widgets.\n"
				"- spawn_widget_component for in-world UI.\n"
			), *Screen));

			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

} // RegisterAll()

} // namespace MCPBuiltInPrompts
