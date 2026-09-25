// Copyright StraySpark Studio 2026. All Rights Reserved.

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
				"PERFORMANCE TRIAGE. Target: %s FPS = %.1fms frame budget.\n"
				"Rule of thumb at 60 FPS: game thread <= 8ms, render thread <= 8ms, GPU <= 14ms.\n\n"
				"STEP 1 - BASELINE (record numbers BEFORE touching anything):\n"
				"- Read unreal://editor/performance and get_render_stats; note FPS, draw calls, triangles, memory.\n"
				"- run_console_command 'stat unit' - identifies WHICH thread is over budget (Game/Draw/GPU).\n\n"
				"STEP 2 - TRIAGE BY THREAD:\n"
				"GPU-bound ('GPU' highest):\n"
				"- 'stat gpu' for pass breakdown. Shadows > 3ms: cut shadow-casting lights (set_light_properties cast_shadows=false on fillers). Lumen/translucency heavy: profile_actors_in_view to find offenders.\n"
				"Render-thread-bound ('Draw' highest):\n"
				"- get_render_stats: draw calls > 3000 (desktop) is a red flag. Fix: enable_nanite on dense static meshes, merge/instance repeated actors, get_mesh_complexity_report for the worst meshes.\n"
				"Game-thread-bound ('Game' highest):\n"
				"- Tick storms: find_actors + batch audit; disable tick on decorative Blueprints.\n"
				"- 'stat game' to find the heavy category before changing anything.\n\n"
				"STEP 3 - BUDGETS (desktop / 60 FPS):\n"
				"- Draw calls <= 3000, visible triangles <= 5M (non-Nanite), shadow-casting movable lights <= 4 per view, texture memory: get_memory_report and keep streaming pool under its cap.\n\n"
				"STEP 4 - FIX HIGHEST-IMPACT FIRST, ONE CHANGE AT A TIME:\n"
				"- After EACH change: re-read unreal://editor/performance and compare against the step-1 baseline. Revert (undo) anything that didn't measurably help.\n\n"
				"STEP 5 - REPORT:\n"
				"- Summarize: baseline -> final numbers per metric, changes kept, changes reverted.\n"
				"- take_screenshot to confirm visual quality held.\n"
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
	// design_ui_layout - UMG UI design patterns (enhanced)
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("design_ui_layout");
		Def.Description = TEXT("Comprehensive guide for designing and building UMG widget layouts with full widget tree manipulation, styling, and optional AI image generation.");

		FMCPPromptArgument ScreenArg;
		ScreenArg.Name = TEXT("screen_type");
		ScreenArg.Description = TEXT("Screen type: hud, main_menu, inventory, dialogue, settings, shop, character_select, loading_screen");
		ScreenArg.bRequired = false;
		Def.Arguments.Add(ScreenArg);

		FMCPPromptArgument StyleArg;
		StyleArg.Name = TEXT("style");
		StyleArg.Description = TEXT("Visual style: sci-fi, fantasy, medieval, modern, minimal, dark, neon, cyberpunk");
		StyleArg.bRequired = false;
		Def.Arguments.Add(StyleArg);

		FMCPPromptArgument GenImagesArg;
		GenImagesArg.Name = TEXT("generate_images");
		GenImagesArg.Description = TEXT("Whether to generate AI images for UI elements (true/false). Requires fal.ai API key.");
		GenImagesArg.bRequired = false;
		Def.Arguments.Add(GenImagesArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Screen = TEXT("hud");
			if (const FString* V = Args.Find(TEXT("screen_type"))) Screen = *V;

			FString Style = TEXT("modern");
			if (const FString* V = Args.Find(TEXT("style"))) Style = *V;

			bool bGenImages = false;
			if (const FString* V = Args.Find(TEXT("generate_images")))
				bGenImages = (*V == TEXT("true") || *V == TEXT("1"));

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"Design and build a %s UI screen in UMG with %s style.\n\n"
				"AVAILABLE TOOLS (use these in order):\n"
				"1. create_widget_blueprint - Create the Widget Blueprint asset\n"
				"2. add_widget - Add widgets: CanvasPanel, VerticalBox, HorizontalBox, GridPanel, Overlay, SizeBox, ScaleBox, Border, WrapBox, UniformGridPanel, ScrollBox, Button, TextBlock, Image, EditableTextBox, Slider, ProgressBar, CheckBox, ComboBoxString, Spacer, RichTextBlock\n"
				"3. set_widget_properties - Set text, colors, fonts, sizes, visibility, opacity\n"
				"4. set_widget_slot - Position with anchors, offsets, alignment, size rules, padding\n"
				"5. set_widget_image - Apply textures to Image widgets\n"
				"6. get_widget_tree - Inspect/verify the widget hierarchy\n"
				"7. get_widget_properties - Read widget property values\n"
				"8. move_widget - Reparent widgets between containers\n"
				"9. remove_widget - Remove unwanted widgets\n"
				"%s"
				"\n"
				"DESIGN WORKFLOW:\n"
				"Step 1 - PLAN: Describe the full widget tree hierarchy first\n"
				"Step 2 - CREATE: create_widget_blueprint('/Game/UI/WBP_%s')\n"
				"%s"
				"Step %s - BUILD: add_widget for each element top-down\n"
				"Step %s - POSITION: set_widget_slot for anchors and layout\n"
				"Step %s - STYLE: set_widget_properties for colors, text, fonts\n"
				"%s"
				"Step %s - VERIFY: get_widget_tree to confirm structure\n\n"
				"ANCHORING RULES:\n"
				"- HUD elements: anchor to nearest screen edge\n"
				"- Health bar: top-left or bottom-left (anchor 0,0)\n"
				"- Minimap: top-right (anchor 1,0)\n"
				"- Crosshair: center (anchor 0.5,0.5, alignment 0.5,0.5)\n"
				"- Menus: center, stretch with margins\n"
				"- Full screen: anchor_min 0,0 anchor_max 1,1 offsets 0,0,0,0\n\n"
				"LAYOUT PATTERNS:\n"
				"- HUD: CanvasPanel root, elements anchored to edges\n"
				"- Menu: CanvasPanel > Overlay(center) > VerticalBox\n"
				"- Inventory: CanvasPanel > VerticalBox > [Header, UniformGridPanel]\n"
				"- Dialogue: CanvasPanel > Border(bottom-anchored) > VerticalBox\n"
				"- Settings: CanvasPanel > VerticalBox > [ScrollBox with rows]\n\n"
				"NAMING: WBP_<ScreenName>, widgets as <Type>_<Purpose> (e.g., Txt_PlayerName, Btn_Start, Img_Background)\n"
			),
				*Screen, *Style,
				bGenImages ? TEXT("10. generate_ui_image - AI-generate unique textures (icons, backgrounds, frames) via fal.ai\n11. remove_background - Remove backgrounds from generated images\n") : TEXT(""),
				*Screen,
				bGenImages ? TEXT("Step 3 - IMAGES: generate_ui_image for backgrounds, icons, frames with style presets\n") : TEXT(""),
				bGenImages ? TEXT("4") : TEXT("3"),
				bGenImages ? TEXT("5") : TEXT("4"),
				bGenImages ? TEXT("6") : TEXT("5"),
				bGenImages ? TEXT("Step 7 - TEXTURES: set_widget_image to apply generated textures\n") : TEXT(""),
				bGenImages ? TEXT("8") : TEXT("6")
			));

			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// design_full_ui - Complete AI-driven UI design workflow
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("design_full_ui");
		Def.Description = TEXT("Complete AI-driven UI design workflow. Generates a cohesive UI layout with optional AI-generated images, styled widgets, and proper UMG structure. Orchestrates the full pipeline from concept to built widget tree.");

		FMCPPromptArgument ScreenArg;
		ScreenArg.Name = TEXT("screen_type");
		ScreenArg.Description = TEXT("Screen type: hud, main_menu, inventory, dialogue, settings, shop, character_select, loading_screen");
		ScreenArg.bRequired = true;
		Def.Arguments.Add(ScreenArg);

		FMCPPromptArgument GenreArg;
		GenreArg.Name = TEXT("game_genre");
		GenreArg.Description = TEXT("Game genre: rpg, fps, tps, strategy, puzzle, horror, platformer, racing");
		GenreArg.bRequired = false;
		Def.Arguments.Add(GenreArg);

		FMCPPromptArgument StyleArg;
		StyleArg.Name = TEXT("style");
		StyleArg.Description = TEXT("Visual style: sci-fi, fantasy, medieval, modern, minimal, dark, neon, cyberpunk");
		StyleArg.bRequired = false;
		Def.Arguments.Add(StyleArg);

		FMCPPromptArgument ColorArg;
		ColorArg.Name = TEXT("color_scheme");
		ColorArg.Description = TEXT("Custom color description, e.g., 'dark blue with gold accents'");
		ColorArg.bRequired = false;
		Def.Arguments.Add(ColorArg);

		FMCPPromptArgument GenImagesArg;
		GenImagesArg.Name = TEXT("generate_images");
		GenImagesArg.Description = TEXT("Generate AI images for UI elements (true/false, default: true). Requires fal.ai API key.");
		GenImagesArg.bRequired = false;
		Def.Arguments.Add(GenImagesArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Screen = TEXT("hud");
			if (const FString* V = Args.Find(TEXT("screen_type"))) Screen = *V;

			FString Genre = TEXT("rpg");
			if (const FString* V = Args.Find(TEXT("game_genre"))) Genre = *V;

			FString Style = TEXT("modern");
			if (const FString* V = Args.Find(TEXT("style"))) Style = *V;

			FString Colors = TEXT("");
			if (const FString* V = Args.Find(TEXT("color_scheme"))) Colors = *V;

			bool bGenImages = true;
			if (const FString* V = Args.Find(TEXT("generate_images")))
				bGenImages = !(*V == TEXT("false") || *V == TEXT("0"));

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"BUILD A COMPLETE %s UI SCREEN for a %s %s game.\n"
				"Style: %s%s\n\n"
				"Execute this FULL PIPELINE step by step using MCP tools:\n\n"
				"=== STEP 1: PLAN THE WIDGET TREE ===\n"
				"Describe the complete hierarchy before building. Example:\n"
				"  CanvasPanel (root)\n"
				"    |- Image (background)\n"
				"    |- VerticalBox (main layout)\n"
				"    |   |- HorizontalBox (header)\n"
				"    |   |   |- TextBlock (title)\n"
				"    |   |   |- Spacer\n"
				"    |   |   |- Button (close)\n"
				"    |   |- ScrollBox (content)\n"
				"    |       |- ...\n\n"
				"%s"
				"=== STEP %s: CREATE WIDGET BLUEPRINT ===\n"
				"Call: create_widget_blueprint(asset_path='/Game/UI/WBP_%s', root_widget_type='CanvasPanel')\n\n"
				"=== STEP %s: BUILD THE WIDGET TREE ===\n"
				"Call add_widget for each element, top-down. Specify parent_widget_name to place correctly.\n"
				"Name widgets descriptively: Img_Background, Txt_Title, Btn_Close, VBox_Content, etc.\n\n"
				"=== STEP %s: POSITION EVERYTHING ===\n"
				"Call set_widget_slot for EVERY widget to set anchors, offsets, alignment.\n"
				"Common patterns:\n"
				"- Full screen background: anchor_min_x=0, anchor_min_y=0, anchor_max_x=1, anchor_max_y=1\n"
				"- Top-left HUD: anchor 0,0 with pixel offsets\n"
				"- Centered: anchor 0.5,0.5 with alignment 0.5,0.5\n"
				"- Fill parent: size_rule='Fill' in box slots\n\n"
				"=== STEP %s: STYLE THE WIDGETS ===\n"
				"Call set_widget_properties to set text, fonts, colors, opacity.\n"
				"Be consistent with the %s style and %s color scheme.\n\n"
				"%s"
				"=== STEP %s: VERIFY ===\n"
				"Call get_widget_tree(include_properties=true) to confirm the final structure.\n"
				"Fix any issues found.\n"
			),
				*Screen.ToUpper(), *Genre, *Style,
				*Style,
				Colors.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", Colors: %s"), *Colors),
				bGenImages ? TEXT(
					"=== STEP 2: GENERATE AI IMAGES ===\n"
					"Call generate_ui_image for each visual element needed:\n"
					"- Background: style_preset='ui_background'\n"
					"- Icons: style_preset='ui_icon', remove_background=true\n"
					"- Buttons: style_preset='ui_button'\n"
					"- Frames/borders: style_preset='ui_frame', remove_background=true\n"
					"Include the game style in each prompt. Save to /Game/UI/Generated/.\n\n"
				) : TEXT(""),
				bGenImages ? TEXT("3") : TEXT("2"),
				*Screen,
				bGenImages ? TEXT("4") : TEXT("3"),
				bGenImages ? TEXT("5") : TEXT("4"),
				bGenImages ? TEXT("6") : TEXT("5"),
				*Style, Colors.IsEmpty() ? TEXT("chosen") : *Colors,
				bGenImages ? TEXT(
					"=== STEP 7: APPLY TEXTURES ===\n"
					"Call set_widget_image for each Image widget, using the generated texture paths.\n\n"
				) : TEXT(""),
				bGenImages ? TEXT("8") : TEXT("6")
			));

			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}


	// ================================================================
	// pcg_workflow - Procedural content generation pipeline (v4 Phase 5)
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("pcg_workflow");
		Def.Description = TEXT("Build a procedural content pipeline with PCG: graph creation, node wiring, mesh spawning, execution, and iteration.");

		FMCPPromptArgument ContentType;
		ContentType.Name = TEXT("content_type");
		ContentType.Description = TEXT("What to scatter: forest, rocks, debris, props, grass");
		ContentType.bRequired = false;
		Def.Arguments.Add(ContentType);

		FMCPPromptArgument AreaSize;
		AreaSize.Name = TEXT("area_size");
		AreaSize.Description = TEXT("Coverage area in meters (default: 50)");
		AreaSize.bRequired = false;
		Def.Arguments.Add(AreaSize);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Content = TEXT("forest");
			if (const FString* V = Args.Find(TEXT("content_type"))) Content = *V;
			FString Area = TEXT("50");
			if (const FString* V = Args.Find(TEXT("area_size"))) Area = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"BUILD A PCG PIPELINE scattering %s over a %sm area.\n\n"
				"STEP 1 - DISCOVER ASSETS:\n"
				"- list_assets class_filter='StaticMesh' to find meshes that fit '%s'.\n"
				"- Pick 3-6 meshes with size variety; note their paths.\n\n"
				"STEP 2 - CREATE THE GRAPH:\n"
				"- create_pcg_graph at /Game/PCG/PCG_%s.\n"
				"- get_pcg_graph_nodes to inspect the default Input/Output nodes.\n"
				"- add_pcg_node type 'SurfaceSampler' (density ~0.1-1.0 per m2 depending on content).\n"
				"- add_pcg_node type 'TransformPoints' for random yaw (0-360) and scale jitter (0.8-1.3).\n"
				"- add_pcg_node type 'StaticMeshSpawner'; set_pcg_static_mesh_spawner_meshes with your mesh list and weights.\n"
				"- connect_pcg_nodes: Input -> SurfaceSampler -> TransformPoints -> StaticMeshSpawner.\n\n"
				"STEP 3 - PLACE & EXECUTE:\n"
				"- spawn_pcg_actor with the graph at the target location.\n"
				"- execute_pcg on that actor (this can take a moment on large areas).\n\n"
				"STEP 4 - VERIFY & ITERATE:\n"
				"- take_screenshot to inspect distribution.\n"
				"- get_render_stats: watch triangle count; if too dense, lower sampler density and re-execute.\n"
				"- Natural look: cluster rather than uniform-spread; avoid intersecting large meshes.\n\n"
				"RULES:\n"
				"- UE units are cm; %sm = %s00 units.\n"
				"- Keep total spawned instances under ~50K for editor responsiveness; use get_foliage_stats/get_render_stats to check.\n"
			), *Content, *Area, *Content, *Content, *Area, *Area));
			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// statetree_design - StateTree AI/logic authoring (v4 Phase 5)
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("statetree_design");
		Def.Description = TEXT("Design a StateTree for AI or gameplay logic: state hierarchy, transitions, evaluators, and verification.");

		FMCPPromptArgument Archetype;
		Archetype.Name = TEXT("archetype");
		Archetype.Description = TEXT("Behavior archetype: patrol_guard, ambient_npc, turret, companion, door_logic");
		Archetype.bRequired = false;
		Def.Arguments.Add(Archetype);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Arch = TEXT("patrol_guard");
			if (const FString* V = Args.Find(TEXT("archetype"))) Arch = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"DESIGN A STATETREE for a '%s' behavior.\n\n"
				"STEP 0 - CAPABILITY CHECK:\n"
				"- Read unreal://project/capabilities; StateTree tools need the StateTree plugin.\n"
				"- If missing, fall back to a Behavior Tree (create_behavior_tree + create_blackboard).\n\n"
				"STEP 1 - SKETCH THE HIERARCHY (before any tool call):\n"
				"- Root states should be the major modes (e.g. Patrol / Investigate / Combat / Return).\n"
				"- Child states are concrete actions (MoveToWaypoint, Wait, Scan).\n"
				"- Every state needs an exit: define which transition leaves it and on what condition.\n\n"
				"STEP 2 - BUILD:\n"
				"- create_state_tree at /Game/AI/ST_%s.\n"
				"- add_state_tree_state per state, parenting children correctly.\n"
				"- set_state_tree_evaluator for shared data (e.g. distance-to-player).\n\n"
				"STEP 3 - VERIFY:\n"
				"- get_state_tree_info and confirm the hierarchy matches your sketch exactly.\n"
				"- Common bug: orphaned states with no entry transition - list them and fix.\n\n"
				"STEP 4 - WIRE TO A PAWN:\n"
				"- The pawn needs a StateTreeComponent; add via add_component on its Blueprint, then compile_blueprint and verify with describe_graph/list_actor_components.\n"
			), *Arch, *Arch));
			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// chaos_destruction - Destruction authoring pipeline (v4 Phase 5)
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("chaos_destruction");
		Def.Description = TEXT("Author a destructible object with Chaos: geometry collection creation, fracturing, and PIE verification.");

		FMCPPromptArgument ObjectType;
		ObjectType.Name = TEXT("object_type");
		ObjectType.Description = TEXT("What to make destructible: wall, crate, pillar, window, statue");
		ObjectType.bRequired = false;
		Def.Arguments.Add(ObjectType);

		FMCPPromptArgument Pieces;
		Pieces.Name = TEXT("pieces");
		Pieces.Description = TEXT("Approximate fragment count (default: 25)");
		Pieces.bRequired = false;
		Def.Arguments.Add(Pieces);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Obj = TEXT("crate");
			if (const FString* V = Args.Find(TEXT("object_type"))) Obj = *V;
			FString NumPieces = TEXT("25");
			if (const FString* V = Args.Find(TEXT("pieces"))) NumPieces = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"MAKE A DESTRUCTIBLE %s (~%s fragments).\n\n"
				"STEP 1 - SOURCE MESH:\n"
				"- Pick the source static mesh (list_assets class_filter='StaticMesh').\n"
				"- NOTE: fracturing happens on a geometry collection ASSET, not the source mesh - the source stays intact.\n\n"
				"STEP 2 - CREATE THE COLLECTION:\n"
				"- chaos_create_geometry_collection asset_path='/Game/Destruction/GC_%s' source_meshes=[your mesh].\n\n"
				"STEP 3 - FRACTURE:\n"
				"- chaos_fracture collection_path=... method='voronoi' num_pieces=%s seed=42 (long-running; may return a task - poll get_task_status).\n"
				"- Method guide: voronoi = natural shatter (concrete/stone); cluster = chunky breaks (masonry); planar = clean slices (glass); uniform = even debris.\n"
				"- Verify via the structured result: new_pieces should be near your target.\n\n"
				"STEP 4 - PLACE & SIMULATE:\n"
				"- Spawn a GeometryCollectionActor with the asset (spawn_blueprint or create_actor with class 'GeometryCollectionActor', then set its RestCollection via set_actor_property).\n"
				"- pie_start, then chaos_apply_force on it (e.g. force_z=-50000) to trigger the break; pie_screenshot to verify; pie_stop.\n\n"
				"BUDGETS:\n"
				"- Keep fragments <= 200 for gameplay objects, <= 1000 for hero moments.\n"
				"- Each fragment is a draw call until it sleeps - check get_render_stats after simulation.\n"
			), *Obj, *NumPieces, *Obj, *NumPieces));
			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// metasound_graph - MetaSound graph authoring (v4.5, UE 5.8)
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("metasound_graph");
		Def.Description = TEXT("Author a MetaSound Source graph node-by-node: discover node classes, add nodes, wire pins, set defaults, and compile. Uses the 5.8 graph mutators.");

		FMCPPromptArgument SoundArg;
		SoundArg.Name = TEXT("sound_type");
		SoundArg.Description = TEXT("What to build: sine_tone, sfx, music_loop, ambient_bed, ui_blip");
		SoundArg.bRequired = false;
		Def.Arguments.Add(SoundArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Sound = TEXT("sine_tone");
			if (const FString* V = Args.Find(TEXT("sound_type"))) Sound = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"AUTHOR A METASOUND graph for a '%s'. In UE 5.8 the graph mutators edit the live document.\n\n"
				"STEP 0 - CAPABILITY:\n"
				"- metasound_list_node_classes (optional name_filter) — discover valid node classes. A node_class_path is a Frontend class name 'Namespace.Name[.Variant]' (e.g. 'UE.Sine.Audio').\n"
				"- If the MetaSound plugin isn't loaded the tools return 'unsupported'.\n\n"
				"STEP 1 - CREATE THE ASSET:\n"
				"- Create a MetaSound Source asset (see the MetaSound tools). Note its /Game path; pass it as metasound_path to every graph call.\n\n"
				"STEP 2 - BUILD THE GRAPH (each add_node returns the new node's GUID — keep it):\n"
				"- metasound_add_node(metasound_path, node_class_path) for each node (oscillators, math, envelopes, the output).\n"
				"- metasound_connect_pins(metasound_path, from_node, from_pin, to_node, to_pin) — pins are addressed by vertex NAME (the label in the editor). Types must match; an input takes one connection.\n"
				"- metasound_set_node_property(metasound_path, node_id, property_name, value) — set literal defaults on input pins (e.g. a frequency of 440).\n\n"
				"STEP 3 - VERIFY & COMPILE:\n"
				"- metasound_get_graph(metasound_path) to inspect; metasound_compile(metasound_path) to rebuild + save. Fix any reported errors and recompile.\n\n"
				"TIPS:\n"
				"- Drive the graph to a single audio output. Use set_metasound_parameter for runtime-tweakable defaults.\n"
				"- For a '%s', keep it simple first (source -> gain -> output), verify it compiles, then add modulation.\n"
			), *Sound, *Sound));
			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// animation_mixer - Sequencer Animation Mixer layering (v4.5, UE 5.8)
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("animation_mixer");
		Def.Description = TEXT("Layer and mask skeletal animation directly in Sequencer with the 5.8 Animation Mixer — no separate Anim Blueprint or slots.");

		FMCPPromptArgument GoalArg;
		GoalArg.Name = TEXT("goal");
		GoalArg.Description = TEXT("What to build: locomotion_plus_gesture, additive_lean, face_over_body, blend_two_clips");
		GoalArg.bRequired = false;
		Def.Arguments.Add(GoalArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Goal = TEXT("locomotion_plus_gesture");
			if (const FString* V = Args.Find(TEXT("goal"))) Goal = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"LAYER ANIMATION IN SEQUENCER with the Animation Mixer. Goal: '%s'.\n\n"
				"PREREQUISITES:\n"
				"- A Level Sequence with a skeletal-mesh actor possessed (an object BINDING). You need the binding GUID — the sequencer tools return it when you add the actor/possessable.\n\n"
				"WORKFLOW:\n"
				"1. animmixer_add_track(sequence_path, binding_id) — adds the Animation Mixer track to that binding (idempotent).\n"
				"2. animmixer_add_layer(sequence_path, binding_id) — one layer per input you want to blend/mask. Returns the layer index. The base locomotion goes on the lowest layer; gestures/additives on higher layers.\n"
				"3. animmixer_add_animation(sequence_path, binding_id, layer_index, anim_sequence_path, start_seconds) — place clips on each layer.\n"
				"4. animmixer_get_info(sequence_path, binding_id) — confirm layer count and sections per layer match your plan.\n\n"
				"NOTES:\n"
				"- Higher layers composite over lower ones; use them for masked/additive passes (e.g. a wave over a walk).\n"
				"- The mixer supports the Unreal Animation Framework and can be auto-baked to an anim sequence later.\n"
				"- Verify visually with the sequencer/PIE and take_screenshot.\n"
			), *Goal));
			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// substrate_material - Substrate material authoring (v4.5, UE 5.8)
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("substrate_material");
		Def.Description = TEXT("Author a material with the 5.8 Substrate framework: confirm Substrate is enabled, build with Substrate expression nodes, and assign.");

		FMCPPromptArgument KindArg;
		KindArg.Name = TEXT("material_kind");
		KindArg.Description = TEXT("Material kind: metal, glass, clearcoat_paint, cloth, skin, toon");
		KindArg.bRequired = false;
		Def.Arguments.Add(KindArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Kind = TEXT("metal");
			if (const FString* V = Args.Find(TEXT("material_kind"))) Kind = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"AUTHOR A SUBSTRATE MATERIAL for '%s' (UE 5.8 Substrate framework).\n\n"
				"STEP 0 - CONFIRM SUBSTRATE:\n"
				"- substrate_get_status — Substrate is a project setting (requires an editor restart to change). If it's OFF, the legacy shading models apply and you should author those instead (or enable Substrate in Project Settings > Rendering and restart).\n\n"
				"STEP 1 - CREATE THE MATERIAL:\n"
				"- create_material at /Game/Materials/M_%s.\n\n"
				"STEP 2 - BUILD THE GRAPH WITH SUBSTRATE NODES (when Substrate is ON):\n"
				"- Use the material graph tools to add Substrate expression nodes (e.g. a Substrate Slab BSDF) and wire base color / roughness / metallic / normal into it, then into the material's front material output.\n"
				"- For '%s': metal => high metallic, low roughness; glass => Substrate transmittance + thin/translucent; clearcoat => a coat layer over the base; cloth/skin => the matching Substrate BSDF; toon => the experimental Substrate Toon shader.\n"
				"- 5.8 also supports importing measured materials (X-Rite AxF) via the Substrate import path.\n\n"
				"STEP 3 - INSTANCE, ASSIGN, VERIFY:\n"
				"- create_material_instance for variations; assign to an actor's mesh; take_screenshot to confirm look.\n"
			), *Kind, *Kind, *Kind));
			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// lighting_setup - MegaLights / Lumen lighting (v4.5, UE 5.8)
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("lighting_setup");
		Def.Description = TEXT("Configure 5.8 lighting: MegaLights for many dynamic lights and Lumen GI, then verify against a frame budget.");

		FMCPPromptArgument TargetArg;
		TargetArg.Name = TEXT("target");
		TargetArg.Description = TEXT("Performance target: quality_desktop, 60fps_console, handheld");
		TargetArg.bRequired = false;
		Def.Arguments.Add(TargetArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Target = TEXT("quality_desktop");
			if (const FString* V = Args.Find(TEXT("target"))) Target = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"CONFIGURE 5.8 LIGHTING for the '%s' target.\n\n"
				"STEP 1 - BASELINE:\n"
				"- lighting_get_settings — record current MegaLights / Lumen / virtual-shadow-map state.\n"
				"- get_render_stats + run_console_command 'stat unit' for the starting frame breakdown.\n\n"
				"STEP 2 - APPLY:\n"
				"- Many dynamic, shadow-casting lights => lighting_set_megalights(enabled=true) (production-ready in 5.8; needs SM6 / a compatible platform).\n"
				"- Dynamic GI => lighting_set_lumen(diffuse_indirect=true). For a tighter budget, lower screen_probe_gather quality.\n"
				"- For handheld / strict-60fps targets, prefer the lighter Lumen mode and keep MegaLights sample counts modest.\n\n"
				"STEP 3 - SCENE LIGHTS:\n"
				"- DirectionalLight (sun) + SkyLight (ambient) + SkyAtmosphere for exteriors; PointLights/SpotLights for interiors (set_light_properties).\n"
				"- With MegaLights you can afford many more local shadow-casting lights than before — but still profile.\n\n"
				"STEP 4 - VERIFY (one change at a time):\n"
				"- After each change re-check lighting_get_settings + get_render_stats and compare to baseline; take_screenshot to confirm the look held.\n"
			), *Target));
			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// montage_authoring - build a playable montage end to end (v4.6)
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("montage_authoring");
		Def.Description = TEXT("Author an AnimMontage end to end: slot, sections and their chaining, notify states for gameplay windows, blend settings, then validate.");

		FMCPPromptArgument KindArg;
		KindArg.Name = TEXT("montage_kind");
		KindArg.Description = TEXT("What to build: combo, single_attack, reload, emote, traversal");
		KindArg.bRequired = false;
		Def.Arguments.Add(KindArg);

		FMCPPromptArgument SkeletonArg;
		SkeletonArg.Name = TEXT("skeleton_path");
		SkeletonArg.Description = TEXT("Target skeleton content path, if already known");
		SkeletonArg.bRequired = false;
		Def.Arguments.Add(SkeletonArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Kind = TEXT("combo");
			if (const FString* V = Args.Find(TEXT("montage_kind"))) Kind = *V;
			FString SkeletonPath = TEXT("(discover it)");
			if (const FString* V = Args.Find(TEXT("skeleton_path"))) SkeletonPath = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"AUTHOR AN ANIM MONTAGE. Kind: '%s'. Skeleton: %s.\n\n"
				"STEP 1 - FIND THE SOURCE ANIMATIONS:\n"
				"- list_anim_assets_by_skeleton(skeleton_path, asset_type='Sequence') to see what is available on this skeleton.\n"
				"- Every animation in one montage MUST share the montage's skeleton. The montage_* tools reject a mismatch and name both skeletons.\n\n"
				"STEP 2 - CREATE:\n"
				"- For a multi-section combo, montage_create_from_sections(asset_path, animation_paths[], section_names[], chain_sections=false) does the whole build in ONE call: segments, one section per clip, and the chaining. Use chain_sections=false for input-driven combos (gameplay jumps between sections) and true for a montage that plays straight through.\n"
				"- For a single clip, create_anim_montage(asset_path, animation_path, slot_name). It registers the slot on the skeleton, which is what makes the slot selectable from an AnimGraph Slot node.\n\n"
				"STEP 3 - SLOTS (only if layering):\n"
				"- A montage that should play over locomotion rather than replace it needs its own slot: montage_add_slot(asset_path, slot_name='UpperBody').\n"
				"- Then the AnimGraph needs a matching Slot node: animgraph_add_node(node_class='AnimGraphNode_Slot'), animgraph_set_node_property(property_name='SlotName', value='UpperBody'), animgraph_connect_pose into the pose chain, compile_anim_blueprint. Without that node the montage plays into nothing.\n\n"
				"STEP 4 - SECTIONS AND FLOW:\n"
				"- montage_get_sections(asset_path) first — it reports each section's start AND end, and a section's end is implicit (the next section's start), so moving one resizes its neighbour.\n"
				"- montage_link_sections(asset_path, section_name, next_section) sets what plays next. Point a section at ITSELF to loop it (charge-and-hold). Pass an empty next_section so the montage blends out there.\n"
				"- For an input-driven combo: leave each section unchained, and have gameplay call Montage_JumpToSection during the combo window.\n\n"
				"STEP 5 - GAMEPLAY WINDOWS (this is what makes a montage playable):\n"
				"- Instant events (hit frames, footsteps, VFX): anim_add_notify(asset_path, time_seconds, notify_name='AttackHit'). The AnimBP implements 'AnimNotify_AttackHit'.\n"
				"- Anything with a DURATION (combo input windows, i-frames, weapon traces): anim_add_notify_state(asset_path, notify_state_class, start_time, duration). Notify states on one track cannot overlap — put concurrent windows on separate tracks with anim_add_notify_track.\n"
				"- Alternatively a float curve: anim_add_float_curve_keys(asset_path, curve_name='ComboWindow', times, values) and read it with GetCurveValue.\n\n"
				"STEP 6 - FEEL:\n"
				"- montage_set_blend_settings(asset_path, blend_in_time, blend_out_time, blend_out_trigger_time). A NEGATIVE blend_out_trigger_time (the default) means the blend out finishes exactly as the montage ends, eating the tail; >= 0 starts the blend that many seconds before the end.\n"
				"- montage_set_blend_profile for per-bone blends (snap at the hands, ease in the spine).\n"
				"- montage_set_sync_group if the montage must stay in step with the locomotion underneath it.\n\n"
				"STEP 7 - VERIFY (do not skip):\n"
				"- montage_validate(asset_path) — catches dangling next_section links, unregistered slots, empty slot tracks, segments on the wrong skeleton, and notifies past the montage end. Fix every 'error'; 'info' about unchained sections is expected for input-driven combos.\n"
				"- montage_get_sections(asset_path) to read the final structure back.\n"
				"- Then play it for real: pie_start, and trigger the montage from gameplay.\n"
			), *Kind, *SkeletonPath));
			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// locomotion_state_machine - a state machine that actually moves (v4.6)
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("locomotion_state_machine");
		Def.Description = TEXT("Build a working Animation Blueprint state machine: states with real animations, transition rules that fire, an entry state, wired to the Output Pose, and compiled clean.");

		FMCPPromptArgument StatesArg;
		StatesArg.Name = TEXT("states");
		StatesArg.Description = TEXT("Comma-separated state names (default: Idle,Walk,Run,Jump)");
		StatesArg.bRequired = false;
		Def.Arguments.Add(StatesArg);

		FMCPPromptArgument SkeletonArg;
		SkeletonArg.Name = TEXT("skeleton_path");
		SkeletonArg.Description = TEXT("Target skeleton content path, if already known");
		SkeletonArg.bRequired = false;
		Def.Arguments.Add(SkeletonArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString States = TEXT("Idle,Walk,Run,Jump");
			if (const FString* V = Args.Find(TEXT("states"))) States = *V;
			FString SkeletonPath = TEXT("(discover it)");
			if (const FString* V = Args.Find(TEXT("skeleton_path"))) SkeletonPath = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"BUILD A LOCOMOTION STATE MACHINE. States: %s. Skeleton: %s.\n\n"
				"READ THIS FIRST — the two ways this goes wrong:\n"
				"- A state you create with add_anim_state is EMPTY. Its graph holds only a Result node, so the character T-poses in that state until set_anim_state_animation gives it a pose.\n"
				"- A transition you create with add_anim_transition has NO RULE, so bCanEnterTransition is false and it can never be taken. set_anim_transition_rule is what makes it fire.\n"
				"Neither shows up as a compile error. Follow every step.\n\n"
				"STEP 1 - ASSETS:\n"
				"- list_anim_assets_by_skeleton(skeleton_path, asset_type='Sequence') to find the clips (and 'BlendSpace' for a speed-driven Walk/Run blend).\n"
				"- create_anim_blueprint(asset_path, skeleton_path) if the AnimBP does not exist.\n\n"
				"STEP 2 - VARIABLES (the state machine reads these):\n"
				"- add_variable on the AnimBP for each condition: 'Speed' (float), 'IsMoving' (bool), 'IsFalling' (bool).\n"
				"- These are driven from the AnimBP's update event at runtime; the transition rules only READ them.\n\n"
				"STEP 3 - MACHINE AND STATES:\n"
				"- create_anim_state_machine(asset_path, machine_name='Locomotion').\n"
				"- add_anim_state for each state.\n"
				"- set_anim_state_animation(asset_path, machine_name, state_name, animation_path, loop=true) for EVERY state. Pass a BlendSpace here for a speed-driven ground state; pass loop=false for one-shot states like a landing.\n\n"
				"STEP 4 - TRANSITIONS AND RULES:\n"
				"- add_anim_transition(asset_path, machine_name, from_state, to_state, transition_duration) for each edge.\n"
				"- set_anim_transition_rule for EVERY transition. Pick the rule type by what the transition means:\n"
				"    BoolVariable  — the everyday case. Idle->Walk on 'IsMoving'; Walk->Idle on the same variable with invert=true.\n"
				"    Automatic     — 'play this state out, then move on'. Fires as the state's sequence player nears its end; the right answer for Jump->Land or a one-shot attack. The state must contain a sequence player.\n"
				"    CurveValue    — let the animation itself decide when it may be interrupted (a curve authored with anim_add_float_curve_keys).\n"
				"    Always/Never  — unconditional flow, or disabling an edge without deleting it.\n"
				"- set_anim_transition_settings for blend duration and per-bone blend profile. Duration is the biggest lever on feel: ~0.1s reads as a snap, ~0.3s as a settle. Use priority_order when two transitions out of one state can be true in the same frame.\n\n"
				"STEP 5 - ENTRY AND OUTPUT (both are easy to forget, and both break compilation):\n"
				"- set_anim_state_machine_entry(asset_path, machine_name, state_name='Idle') — a machine with no entry state does not compile.\n"
				"- animgraph_describe(asset_path) to get the state machine node's id and the graph topology.\n"
				"- animgraph_connect_pose(asset_path, from_node=<state machine node id>, to_node='root') — connects the machine to the Output Pose. Without this the AnimGraph evaluates nothing.\n\n"
				"STEP 6 - COMPILE AND VERIFY:\n"
				"- compile_anim_blueprint(asset_path, save=true). A failed compile comes back as an error WITH the compiler messages — read them.\n"
				"- get_anim_state_machine_info(asset_path) to confirm the states and transitions match your plan.\n"
				"- Then prove it moves: set_animation_blueprint on a character, pie_start, drive the variables, and take_screenshot / pie_screenshot.\n"
			), *States, *SkeletonPath));
			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

	// ================================================================
	// sequencer_animation - animate a character in a cinematic (v4.6)
	// ================================================================
	{
		FMCPPromptDefinition Def;
		Def.Name = TEXT("sequencer_animation");
		Def.Description = TEXT("Animate a skeletal character in a Level Sequence: animation track, clips, trimming and blending, then bake back to a reusable AnimSequence.");

		FMCPPromptArgument GoalArg;
		GoalArg.Name = TEXT("goal");
		GoalArg.Description = TEXT("What to build: cutscene, mocap_cleanup, blend_two_clips, bake_for_gameplay");
		GoalArg.bRequired = false;
		Def.Arguments.Add(GoalArg);

		FMCPPromptGenerator Generator;
		Generator.BindLambda([](const TMap<FString, FString>& Args) -> TArray<FMCPPromptMessage>
		{
			FString Goal = TEXT("cutscene");
			if (const FString* V = Args.Find(TEXT("goal"))) Goal = *V;

			FMCPPromptMessage Msg;
			Msg.Role = TEXT("user");
			Msg.Content = FMCPContentBlock::MakeText(FString::Printf(TEXT(
				"ANIMATE A CHARACTER IN SEQUENCER. Goal: '%s'.\n\n"
				"PREREQUISITES:\n"
				"- A Level Sequence (create_level_sequence) and a skeletal-mesh actor in the open level. The actor's level must be loaded — the bake and link tools resolve the binding against the live world.\n\n"
				"STEP 1 - TRACK:\n"
				"- add_animation_track(sequence_path, actor_name) binds the actor if needed and adds the skeletal animation track. This is the track that ANIMATES the character; the transform track only moves it.\n"
				"- Note the binding_id it returns — the other tools take either actor_name or binding_id.\n\n"
				"STEP 2 - CLIPS:\n"
				"- add_animation_section(sequence_path, actor_name, animation_path, start_seconds) per clip. The animation must target the bound actor's skeleton; a mismatch is rejected with both skeleton paths named.\n"
				"- Sections on the SAME row cannot overlap. To crossfade two clips, put the second on its own row with row_index, overlapping the first — Sequencer blends between rows.\n\n"
				"STEP 3 - SHAPE EACH CLIP:\n"
				"- list_animation_sections(sequence_path, actor_name) for the section indices and current values.\n"
				"- set_animation_section_params: start_frame_offset_seconds to trim the front, start_seconds/end_seconds to move or shorten it on the timeline, play_rate to retime, mirror_data_table to flip it left-to-right, slot_name to play THROUGH a montage slot so the clip layers over the AnimBP instead of replacing it.\n\n"
				"STEP 4 - VERIFY:\n"
				"- set_sequence_range so the playback range covers the clips.\n"
				"- play_sequence / take_screenshot, or pie_start for the in-game view.\n\n"
				"STEP 5 - BAKE BACK TO GAMEPLAY (when the shot should become a reusable clip):\n"
				"- bake_sequence_to_anim_sequence(sequence_path, actor_name, output_path) evaluates everything driving that mesh — animation sections, Control Rig, transform tracks — into a new AnimSequence on the actor's skeleton. It is long-running on a big shot; poll with get_task_status if it returns a task handle.\n"
				"- With create_link=true (the default) the two assets stay associated, so the bake can be re-run after the sequence changes. link_anim_sequence_to_sequence sets that association up for an animation baked earlier.\n"
				"- The baked clip is a normal AnimSequence: add notifies with anim_add_notify, build a montage from it with create_anim_montage.\n"
			), *Goal));
			return { Msg };
		});

		Provider.RegisterPrompt(Def, Generator);
	}

} // RegisterAll()

} // namespace MCPBuiltInPrompts
