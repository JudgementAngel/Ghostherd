#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MCPSettings.generated.h"

/**
 * Tool preset profiles for quick configuration.
 * Controls which tool categories are enabled to optimize AI context usage.
 */
UENUM()
enum class EMCPToolPreset : uint8
{
	/** All tool categories enabled (~140+ tools, ~40K tokens). Best for general-purpose AI workflows. */
	Full		UMETA(DisplayName = "Full (All Tools)"),

	/** Core scene-building tools: Actor, Editor, Asset, Material, Level, StaticMesh, Batch, Environment, Blueprint, Spline (~75 tools, ~20K tokens). */
	SceneBuilding	UMETA(DisplayName = "Scene Building"),

	/** Gameplay-focused: Core + Blueprint + GAS + EnhancedInput + AI + GameFramework + Macro (~80 tools, ~22K tokens). */
	Gameplay	UMETA(DisplayName = "Gameplay"),

	/** Minimal set: Actor, Editor, Level only (~28 tools, ~7K tokens). Fastest context, best for simple tasks. */
	Minimal		UMETA(DisplayName = "Minimal"),

	/** Per-category toggles below are used. Set individual bEnable* booleans. */
	Custom		UMETA(DisplayName = "Custom"),
};

UCLASS(config = UnrealMCPServer, defaultconfig, meta = (DisplayName = "Unreal MCP Server"))
class UNREALMCPSERVER_API UMCPSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMCPSettings();

	// ================================================================
	// Server
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Server", meta = (ClampMin = "1024", ClampMax = "65535"))
	int32 ServerPort;

	UPROPERTY(config, EditAnywhere, Category = "Server")
	bool bAutoStartServer;

	UPROPERTY(config, EditAnywhere, Category = "Server",
		meta = (ToolTip = "Allow connections from non-localhost addresses. WARNING: Only enable on trusted networks."))
	bool bAllowRemoteConnections;

	UPROPERTY(config, EditAnywhere, Category = "Server", meta = (ClampMin = "10", ClampMax = "1000"))
	int32 MaxRequestsPerMinute;

	// ================================================================
	// Logging
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Logging")
	bool bVerboseLogging;

	// ================================================================
	// Tool Preset (quick-select)
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Tool Preset",
		meta = (ToolTip = "Quick-select a tool profile. 'Custom' uses the per-category toggles below.\nFull = ~93 tools (~27K context tokens)\nScene Building = ~62 tools (~17K tokens)\nMinimal = ~28 tools (~7K tokens)"))
	EMCPToolPreset ToolPreset;

	// ================================================================
	// Tool Categories (used when ToolPreset == Custom)
	// ================================================================

	/** Core actor manipulation: spawn, transform, properties, hierarchy, tags, visibility (14 tools). Always-on in non-Custom presets. */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Core",
		meta = (ToolTip = "Actor tools: create, destroy, transform, properties, attach/detach, tags, visibility. ~4200 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableActorTools;

	/** Editor & viewport: screenshot, selection, focus, undo/redo, console commands (7 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Core",
		meta = (ToolTip = "Editor/viewport tools: screenshot, selection, camera, undo/redo, console commands. ~1800 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableEditorTools;

	/** Asset management: list, info, import, delete, duplicate, rename (6 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Core",
		meta = (ToolTip = "Asset browser tools: list, get info, import, delete, duplicate, rename. ~1500 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableAssetTools;

	/** Level management: info, new, open, save (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Core",
		meta = (ToolTip = "Level management tools: get info, create new, open, save. ~800 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableLevelTools;

	/** Material creation and editing: create material, instances, scalar/vector params, assign (5 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Material tools: create materials/instances, set parameters, assign to actors. ~1300 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableMaterialTools;

	/** Static mesh: set mesh, get info, batch material slots, create mesh actor (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Static mesh tools: set mesh on actor, get mesh info, material slots, convenience spawn. ~1200 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableStaticMeshTools;

	/** Batch operations: transform, set property, find actors (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Batch tools: multi-actor transform, property set, advanced actor search. ~900 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableBatchTools;

	/** Post-process, fog, sky atmosphere, light properties (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Environment tools: post-process, fog, sky atmosphere, light settings. ~1200 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableEnvironmentTools;

	/** Blueprint graph: create, get info, add components/variables, set properties, compile (7 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scripting",
		meta = (ToolTip = "Blueprint tools: create, inspect, add components/variables, set defaults, compile, spawn. ~2100 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableBlueprintTools;

	/** Python script execution (1 tool). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scripting",
		meta = (ToolTip = "Python bridge: execute Python code in UE's embedded interpreter. ~200 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnablePythonBridge;

	/** Sequencer: create, open, bind actors, add tracks, keyframes, playback (7 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "Sequencer/cinematic tools: create sequences, add tracks, keyframes, playback control. ~2000 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableSequencerTools;

	/** Animation: set skeletal mesh, anim BP, play anim, skeleton info, list assets (5 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "Animation tools: skeletal mesh, animation blueprints, playback, skeleton info. ~1400 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableAnimationTools;

	/** Landscape terrain: info, create (2 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|World Building",
		meta = (ToolTip = "Landscape tools: get terrain info, create landscape. ~600 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableLandscapeTools;

	/** Foliage: add type, paint, erase, stats (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|World Building",
		meta = (ToolTip = "Foliage tools: add foliage types, paint/erase instances, get stats. ~1100 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableFoliageTools;

	/** Niagara VFX: spawn system, set/get parameters (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|VFX & Audio",
		meta = (ToolTip = "Niagara particle system tools: spawn systems, set/get user parameters. ~800 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableNiagaraTools;

	/** Audio: spawn sound, set properties, get info (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|VFX & Audio",
		meta = (ToolTip = "Audio tools: spawn ambient sounds, set audio properties, get sound info. ~800 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableAudioTools;

	/** Physics: simulate, collision profiles, constraints, info (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Simulation",
		meta = (ToolTip = "Physics tools: simulate physics, collision profiles, physics constraints, info query. ~1100 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnablePhysicsTools;

	/** Navigation: build navmesh, query path, get info (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Simulation",
		meta = (ToolTip = "Navigation tools: build navmesh, pathfinding queries, nav info. ~700 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableNavigationTools;

	/** DataTable: list, read rows, add row (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Data",
		meta = (ToolTip = "Data tools: list DataTables, read/write rows. ~700 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableDataTools;

	/** Widget/UMG: list widget BPs, spawn widget component, set properties (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|UI",
		meta = (ToolTip = "Widget/UMG tools: list widget blueprints, spawn widget components, set properties. ~700 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableWidgetTools;

	/** PCG: list graphs, spawn actor, execute, get info (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Procedural",
		meta = (ToolTip = "PCG tools: list graphs, spawn PCG actors, execute generation, query info. ~1000 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnablePCGTools;

	/** World Partition: info query, region loading (2 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|World Building",
		meta = (ToolTip = "World Partition tools: query streaming info, load editor cells by region. ~500 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableWorldPartitionTools;

	/** Spline: create, edit points, get info, set mesh (7 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|World Building",
		meta = (ToolTip = "Spline tools: create spline actors, add/edit/remove points, closed loops. ~1600 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableSplineTools;

	/** Gameplay Ability System: abilities, effects, attribute sets (8 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Gameplay",
		meta = (ToolTip = "GAS tools: create abilities, effects, attribute sets, manage ability system. ~2000 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableGASTools;

	/** Enhanced Input: input actions, mapping contexts, key bindings (6 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Gameplay",
		meta = (ToolTip = "Enhanced Input tools: create actions, mapping contexts, bind keys. ~1400 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableEnhancedInputTools;

	/** AI: behavior trees, blackboards, EQS queries (8 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|AI",
		meta = (ToolTip = "AI tools: behavior trees, blackboards, EQS queries, AI asset management. ~2000 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableAITools;

	/** Game Framework: game modes, controllers, states, HUD (6 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Gameplay",
		meta = (ToolTip = "Game Framework tools: create game modes, player controllers, states, HUD blueprints. ~1400 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableGameFrameworkTools;

	/** Macro/Composite: high-level workflow tools like create_basic_level (6 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Workflow",
		meta = (ToolTip = "Macro tools: high-level composite operations (basic level, light rig, grid layout). ~1800 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableMacroTools;

	/** Build & Automation: project info, build config, asset validation (5 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Workflow",
		meta = (ToolTip = "Build tools: project info, build configuration, asset validation, map check. ~1200 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableBuildTools;

	/** AnimGraph: create anim BPs, blend spaces, montages, aim offsets (8 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "AnimGraph tools: create anim blueprints, blend spaces, aim offsets, montages. ~2000 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableAnimGraphTools;

	/** Material Graph: add expressions, connect pins, parameters, compile (8 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Material Graph tools: add/connect/remove material expressions, parameters, compile. ~2000 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableMaterialGraphTools;

	/** MetaSound: create sources, list, duplicate, configure (6 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|VFX & Audio",
		meta = (ToolTip = "MetaSound tools: create/list/duplicate MetaSound sources, set parameters. ~1400 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableMetaSoundTools;

	/** Networking: replication settings, dormancy, component replication (5 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Gameplay",
		meta = (ToolTip = "Networking tools: configure actor/component replication, dormancy, net relevancy. ~1200 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableNetworkingTools;

	/** Engine API search: search classes, grep headers, read header files (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Workflow",
		meta = (ToolTip = "Engine API tools: search engine classes/methods in installed headers, read header files. ~800 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableEngineAPITools;

	// ================================================================
	// Safety
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Safety",
		meta = (ToolTip = "Enable console command execution tool"))
	bool bEnableConsoleCommands;

	UPROPERTY(config, EditAnywhere, Category = "Safety",
		meta = (ToolTip = "Enable asset deletion and destructive operations"))
	bool bEnableDestructiveOperations;

	// ================================================================
	// Helpers
	// ================================================================

	/** Returns true if the given category should be enabled based on current preset + custom toggles. */
	bool IsCategoryEnabled(FName Category) const;

	// UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Unreal MCP Server"); }

	static const UMCPSettings* Get();
};
