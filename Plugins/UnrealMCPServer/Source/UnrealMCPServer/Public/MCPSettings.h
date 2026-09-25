// Copyright StraySpark Studio 2026. All Rights Reserved.

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
	/** All 54 tool categories enabled (378 tools as of 4.0.0). Best for general-purpose AI workflows. */
	Full		UMETA(DisplayName = "Full (All Tools)"),

	/** Scene-building: Actor, Editor, Asset, Level, Material(+Graph), StaticMesh, Batch, Environment, Blueprint, Python, Spline, Macro, Build, EngineAPI, SourceControl, TestAuthoring (~169 tools incl. always-on Meta/Search). */
	SceneBuilding	UMETA(DisplayName = "Scene Building"),

	/** Gameplay-focused: Core + Blueprint + GAS + EnhancedInput + GameFramework + GameplayTags + AI + Physics + Navigation + Data + Networking + PIE + Debug + workflow categories (~205 tools incl. always-on Meta/Search). */
	Gameplay	UMETA(DisplayName = "Gameplay"),

	/** Minimal set: Actor, Editor, Level only (~39 tools incl. always-on Meta/Search). Fastest registration, best for simple tasks. */
	Minimal		UMETA(DisplayName = "Minimal"),

	/** Per-category toggles below are used. Set individual bEnable* booleans. */
	Custom		UMETA(DisplayName = "Custom"),
};

/**
 * How tools/list presents the catalog (v4 Phase 1 — progressive disclosure).
 * Orthogonal to EMCPToolPreset (which controls what's REGISTERED): exposure
 * controls what's LISTED up front; everything registered stays callable.
 */
UENUM()
enum class EMCPToolExposureMode : uint8
{
	/** tools/list returns only the meta-tools (search_tools, get_tool_schemas,
	 *  list_tool_categories, run_tool_script) plus a small high-frequency core
	 *  (31 tools, ~3K tokens). Agents discover the rest on demand. Cuts a fresh
	 *  session's tool-definition cost by ~95% with all 378 tools still callable. */
	Catalog		UMETA(DisplayName = "Catalog (progressive disclosure)"),

	/** tools/list returns every registered tool (v3 behavior, ~62K tokens with the Full preset). */
	Full		UMETA(DisplayName = "Full (all tools up front)"),
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

	UPROPERTY(config, EditAnywhere, Category = "Server",
		meta = (ClampMin = "1024", ClampMax = "65535",
			ToolTip = "TCP port the MCP JSON-RPC endpoint listens on (default 13579). Restart the server (or editor) after changing."))
	int32 ServerPort;

	UPROPERTY(config, EditAnywhere, Category = "Server",
		meta = (ToolTip = "Start the MCP server automatically when the editor finishes loading. If off, start it manually from the status-bar menu or console."))
	bool bAutoStartServer;

	UPROPERTY(config, EditAnywhere, Category = "Server",
		meta = (ToolTip = "Address the HTTP listener binds to. Default 127.0.0.1 (loopback only — recommended). Set to a LAN IP or 'any' to accept remote connections; this is REFUSED unless bRequireAuthToken is on with at least one token, and falls back to loopback. v4 replacement for the old no-op bAllowRemoteConnections."))
	FString BindAddress;

	// NOTE (v4): bAllowRemoteConnections was removed in 3.2.0. It never changed the
	// bind address — UE's FHttpServerModule listens on the address configured via
	// [HTTPServer.Listeners] in Engine config — so the flag only created a false
	// sense of a security boundary. BindAddress (above) is its real replacement,
	// shipped in v4 Phase 1.

	UPROPERTY(config, EditAnywhere, Category = "Server",
		meta = (ClampMin = "10", ClampMax = "1000",
			ToolTip = "Rate limit: JSON-RPC requests accepted per minute per session. Requests over the limit are rejected with a structured rate-limit error."))
	int32 MaxRequestsPerMinute;

	UPROPERTY(config, EditAnywhere, Category = "Server",
		meta = (ClampMin = "1", ClampMax = "256",
			ToolTip = "Maximum accepted JSON-RPC request body size in megabytes. Oversized requests are rejected with HTTP 413 before parsing. Protects the editor from memory exhaustion."))
	int32 MaxRequestSizeMB;

	UPROPERTY(config, EditAnywhere, Category = "Server",
		meta = (ClampMin = "0", ClampMax = "600",
			ToolTip = "Maximum seconds a tools/call may hold a network thread while waiting on the game thread. 0 = wait forever (v3 behavior). On timeout the request fails with a structured 'timeout' error; the tool may still finish in the editor."))
	int32 ToolCallTimeoutSeconds;

	// v5 increment 24 (V5-11): bounded tools/call responses. Larger serialized results are stored for the
	// session and returned in pages through get_result_page / unreal://results/{result_id}.
	UPROPERTY(config, EditAnywhere, Category = "Server",
		meta = (ClampMin = "16", ClampMax = "65536",
			ToolTip = "Maximum serialized tools/call result size in KB returned inline. Larger results are retained for 10 minutes and paged through get_result_page."))
	int32 MaxToolResultKB = 1024;

	UPROPERTY(config, EditAnywhere, Category = "Server",
		meta = (ClampMin = "5", ClampMax = "1440",
			ToolTip = "Minutes of inactivity before an MCP session (and its working set / open transaction) is garbage-collected. Open transactions on stale sessions are cancelled to keep the undo stack healthy."))
	int32 SessionIdleTimeoutMinutes;

	// ================================================================
	// Authentication (Phase B / v3)
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Server|Authentication",
		meta = (ToolTip = "Require an 'Authorization: Bearer <token>' header on every JSON-RPC request. Strongly recommended whenever the listener is reachable beyond localhost."))
	bool bRequireAuthToken;

	UPROPERTY(config, EditAnywhere, Category = "Server|Authentication",
		meta = (ToolTip = "Allow-list of accepted bearer tokens. Tokens are matched verbatim. Generate a strong random value (e.g. 'openssl rand -hex 32') per machine. Empty list with bRequireAuthToken=true rejects all requests."))
	TArray<FString> AuthTokens;

	// v4.5 — per-token scopes. Optional map from a bearer token (must also be in
	// AuthTokens) to the scope it is granted: "read", "scene", or "destructive".
	// A token NOT listed here uses the global default (bAllowDestructiveScope ?
	// destructive : scene). A token granted "destructive" still requires
	// bAllowDestructiveScope to be on (the global kill-switch always caps).
	UPROPERTY(config, EditAnywhere, Category = "Server|Authentication",
		meta = (ToolTip = "Per-token scope grants. Key = a token from AuthTokens; Value = 'read' | 'scene' | 'destructive'. Lets you issue a read-only token and a build token separately. Tokens not listed get the global default scope."))
	TMap<FString, FString> AuthTokenScopes;

	// NOTE: the destructive-tools kill-switch (bAllowDestructiveScope) lives in
	// the Safety category below; per-token scopes granted here can widen it.

	// ================================================================
	// CORS / Origin allow-list (Phase B / v3)
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Server|CORS",
		meta = (ToolTip = "HTTP Origin values accepted by the server. Defaults to localhost only. Replaces v2's unsafe wildcard CORS."))
	TArray<FString> AllowedOrigins;

	// ================================================================
	// WebSocket transport (Phase B / v3)
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Server|WebSocket",
		meta = (ToolTip = "Run a WebSocket server on the same port alongside HTTP. Some clients prefer WS streaming. Off by default."))
	bool bEnableWebSocket;

	// ================================================================
	// Logging
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Logging",
		meta = (ToolTip = "Log every JSON-RPC request/response and tool execution detail to LogUnrealMCP. Useful for debugging agents; noisy for normal use."))
	bool bVerboseLogging;

	// ================================================================
	// Tool Preset (quick-select)
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Tool Preset",
		meta = (ToolTip = "Quick-select which tool categories get REGISTERED (callable). 'Custom' uses the per-category toggles below. Counts from the live 4.0.0 registry:\nFull = 378 tools (all 54 categories)\nScene Building = ~169 tools\nGameplay = ~205 tools\nMinimal = ~39 tools\nMeta/discovery and Search tools are always registered regardless of preset.\nNote: with Tool Exposure Mode = Catalog, the preset does NOT change the upfront context cost (tools/list stays at 31 tools / ~3K tokens) — it only changes what is discoverable and callable."))
	EMCPToolPreset ToolPreset;

	UPROPERTY(config, EditAnywhere, Category = "Tool Preset",
		meta = (ToolTip = "How tools/list presents the catalog (v4).\nCatalog = meta-tools + a high-frequency core (31 tools, ~3K tokens); agents discover the rest via search_tools/get_tool_schemas and can run anything via run_tool_script. All registered tools remain callable.\nFull = every registered tool up front (v3 behavior; ~62K tokens with the Full preset).\nA client can override per request with tools/list params {\"exposure\": \"full\"|\"catalog\"}."))
	EMCPToolExposureMode ToolExposureMode;

	// ================================================================
	// Tool Categories (used when ToolPreset == Custom)
	// ================================================================

	/** Core actor manipulation: spawn, transform, properties, hierarchy, tags, visibility (16 tools). Always-on in non-Custom presets. */
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

	/** Level management: info, new, open, save (6 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Core",
		meta = (ToolTip = "Level management tools: get info, create new, open, save. ~800 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableLevelTools;

	/** Material creation and editing: create material, instances, scalar/vector params, assign (5 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Material tools: create materials/instances, set parameters, assign to actors. ~1300 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableMaterialTools;

	/** Static mesh: set mesh, get info, batch material slots, create mesh actor (7 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Static mesh tools: set mesh on actor, get mesh info, material slots, convenience spawn. ~1200 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableStaticMeshTools;

	/** Batch operations: transform, set property, find actors (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Batch tools: multi-actor transform, property set, advanced actor search. ~900 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableBatchTools;

	/** Spatial Awareness: bounds, raycasting, overlap detection, placement helpers (10 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Spatial tools: actor/mesh bounds, line traces, overlap testing, ground placement, alignment, stacking, distance measurement, spatial context analysis, smart placement. Essential for AI-driven level design. ~2500 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableSpatialTools;

	/** Post-process, fog, sky atmosphere, light properties (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Environment tools: post-process, fog, sky atmosphere, light settings. ~1200 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableEnvironmentTools;

	/** Blueprint authoring: lifecycle, components, variables, functions, full graph/node editing, wiring, compile (60 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scripting",
		meta = (ToolTip = "Blueprint tools: create/inspect Blueprints, components, variables, functions, event/node graph authoring, pin wiring, compile, spawn. Largest category — 60 tools. ~10K context tokens when fully listed.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableBlueprintTools;

	/** Python script execution (1 tool). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scripting",
		meta = (ToolTip = "Python bridge: execute Python code in UE's embedded interpreter. ~200 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnablePythonBridge;

	/** Sequencer: create, open, bind actors, add tracks, keyframes, playback (12 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "Sequencer/cinematic tools: create sequences, add tracks, keyframes, playback control. ~2000 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableSequencerTools;

	/** Animation: set skeletal mesh, anim BP, play anim, skeleton info, list assets (5 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "Animation tools: skeletal mesh, animation blueprints, playback, skeleton info. ~1400 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableAnimationTools;

	/** Landscape terrain: info, create, set material (3 tools). */
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

	/** Physics: simulate, collision profiles, constraints, info (9 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Simulation",
		meta = (ToolTip = "Physics tools: simulate physics, collision profiles, physics constraints, info query. ~1100 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnablePhysicsTools;

	/** Navigation: build navmesh, query path, get info (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Simulation",
		meta = (ToolTip = "Navigation tools: build navmesh, pathfinding queries, nav info. ~700 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableNavigationTools;

	/** DataTable: list, read rows, add row (6 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Data",
		meta = (ToolTip = "Data tools: list DataTables, read/write rows. ~700 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableDataTools;

	/** Widget/UMG: create, inspect, and build Widget Blueprint layouts (15 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|UI",
		meta = (ToolTip = "Widget/UMG tools: create widget blueprints, build widget trees, set properties/slots, image assignment. ~3500 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableWidgetTools;

	/** AI Image Generation: generate UI textures via fal.ai, remove backgrounds (2 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|UI",
		meta = (ToolTip = "AI Image tools (fal.ai): generate UI textures with AI, remove backgrounds. Requires PythonScriptPlugin + fal.ai API key. ~500 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableUIImageTools;

	/** AI 3D Model Generation: text-to-3D and image-to-3D via fal.ai (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|AI Generation",
		meta = (ToolTip = "AI 3D Model tools (fal.ai): generate 3D models from text or images using Meshy, Hunyuan, Trellis, Rodin, Tripo. Requires PythonScriptPlugin + fal.ai API key. ~800 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnable3DModelTools;

	// ================================================================
	// AI Generation (fal.ai)
	// ================================================================

	/** fal.ai API key for AI image and 3D model generation. Get one at fal.ai/dashboard/keys */
	UPROPERTY(config, EditAnywhere, Category = "AI Generation",
		meta = (ToolTip = "fal.ai API key for AI image and 3D model generation. Get one at fal.ai/dashboard/keys.\nWARNING: stored in plain text in Config/UnrealMCPServer ini files — do not commit that file to source control with a key in it."))
	FString FalAIApiKey;

	/** Default fal.ai model for text-to-image generation. */
	UPROPERTY(config, EditAnywhere, Category = "AI Generation|Default Models",
		meta = (ToolTip = "Default text-to-image model. flux-2-flash = fastest/cheapest with transparency. nano-banana-2 = concept art. flux-pro = highest quality."))
	FString DefaultFalModel;

	/** Default fal.ai model for text-to-3D generation. */
	UPROPERTY(config, EditAnywhere, Category = "AI Generation|Default Models",
		meta = (ToolTip = "Default text-to-3D model. meshy-v6 = best all-round with PBR. hunyuan-pro = highest fidelity. meshy-v6-preview = fastest."))
	FString DefaultTextTo3DModel;

	/** Default fal.ai model for image-to-3D generation. */
	UPROPERTY(config, EditAnywhere, Category = "AI Generation|Default Models",
		meta = (ToolTip = "Default image-to-3D model. trellis-2 = best quality. meshy-v6-img = supports PBR/rigging. rodin-v2 = clean production geometry."))
	FString DefaultImageTo3DModel;

	/** PCG: list graphs, spawn actor, execute, get info (9 tools). */
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

	/** Gameplay Tags: create, list, assign gameplay tags (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Gameplay",
		meta = (ToolTip = "Gameplay Tag tools: register tags, list hierarchy, assign to actors. ~600 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableGameplayTagTools;

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

	/** Build & Automation: project info, build config, asset validation (7 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Workflow",
		meta = (ToolTip = "Build tools: project info, build configuration, asset validation, map check. ~1200 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableBuildTools;

	/** Control Rig: create and inspect Control Rig Blueprints (2 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "Control Rig tools: create rigs, inspect structure. Requires ControlRig + PythonScriptPlugin. ~400 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableControlRigTools;

	/** AnimGraph: create anim BPs, blend spaces, montages, aim offsets, state machines (14 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "AnimGraph tools: create anim blueprints, blend spaces, aim offsets, montages, state machines and states. ~2000 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableAnimGraphTools;

	/** v4.6 Montage authoring: sections, slots, segments, blends, validation (14 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "Montage authoring (4.6): composite sections and their chaining, slot tracks, multi-segment timelines, blend in/out and blend profiles, sync groups, structural validation. ~3000 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableMontageTools;

	/** v4.6 Animation data: notifies, notify states, notify tracks, curves, sync markers (16 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "Animation data authoring (4.6): notifies and notify states (combo/hit windows), notify tracks, float curves, curve metadata (morph target / material), and sync markers. Wraps UAnimationBlueprintLibrary. ~3200 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableAnimDataTools;

	/** v4.6 AnimGraph node authoring + state machine completion (12 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "AnimGraph node authoring (4.6): add/connect/inspect any AnimGraph node, give state machine states their animation, author transition rules, and compile. Required for state machines that actually evaluate — without these a generated state machine is a T-pose. ~2600 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableAnimGraphNodeTools;

	/** v4.6 Sequencer animation: skeletal animation tracks, sections, bake/link (7 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "Sequencer animation (4.6): add skeletal animation tracks and clips to a level sequence, trim/retime/mirror sections, and bake a binding down to a reusable AnimSequence. ~1600 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableSequencerAnimationTools;

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

	/** PIE Control: start/stop/pause PIE, send synthetic input, capture viewport, attach controllers (9 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Gameplay",
		meta = (ToolTip = "PIE Control tools: start/stop/pause/resume/step PIE, send synthetic input, capture PIE viewport screenshots, attach controllers. Editor-only. ~1500 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnablePIETools;

	/** Source Control: provider-agnostic check-out / revert / submit / history / diff (8 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Workflow",
		meta = (ToolTip = "Source Control tools (Phase D.2): wrap ISourceControlModule for provider-agnostic check-out, revert, submit, history, and conflict resolution. ~1800 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableSourceControlTools;

	/** Test Authoring & Run: scaffold automation specs, run them, fetch reports (5 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Workflow",
		meta = (ToolTip = "Test Authoring tools (Phase D.3): scaffold automation specs, list, run by tag, fetch the last run report, place functional test actors. ~1300 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableTestAuthoringTools;

	/** Runtime Debug & Introspection: blueprint breakpoints, watches, runtime errors (7 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scripting",
		meta = (ToolTip = "Debug tools (Phase D.4): set/clear/list Blueprint breakpoints, add/get pin watches, query last runtime error and call stack. Wraps FKismetDebugUtilities. ~1700 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableDebugTools;

	/** MetaSound Graph editing parity with MaterialGraph: nodes, pins, properties, compile (8 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|VFX & Audio",
		meta = (ToolTip = "MetaSound Graph tools (Phase D.5): node-level authoring inside MetaSound graphs (add/remove nodes, connect/disconnect pins, set node property, compile, list node classes, inspect graph). ~1900 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableMetaSoundGraphTools;

	/** Modeling Mode tools: poly-extrude, poly-cut, boolean, UV unwrap, remesh (5 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Modeling tools (Phase D.6): wraps the MeshModelingToolsExp plugin's editor-mode tools. Schemas registered; bodies pending a MeshModelingToolsExp + ModelingComponents dep. ~1200 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableModelingTools;

	/** Material Layers: layer stack inspection and editing on material instances (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Material Layer tools (Phase D.6): inspect / append / remove layers and configure blend functions on material instances via UMaterialInstance::Get/SetMaterialLayers. ~900 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableMaterialLayerTools;

	/** Chaos / Destruction tools: geometry collections, fracture, fields, cloth, runtime impulse (5 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Simulation",
		meta = (ToolTip = "Chaos tools (Phase D.7): geometry-collection / fracture / field / cloth schemas (pending GeometryCollectionEngine + FractureEngine + ChaosCloth deps) plus a runtime chaos_apply_force that wraps UPrimitiveComponent::AddImpulse. ~1100 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableChaosTools;

	/** MetaHuman tools: list, import, set LOD, attach to skeletal mesh component (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "MetaHuman tools (Phase D.8): list MetaHuman skeletal meshes via AssetRegistry, set forced LOD, attach a USkeletalMesh to a component. metahuman_import is registered but pending the MetaHuman + Quixel Bridge plugin path. ~900 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableMetaHumanTools;

	/** State Trees: create and inspect UE5 StateTree assets (5 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|AI",
		meta = (ToolTip = "StateTree tools: create, list, inspect State Trees. Requires StateTree plugin. ~500 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableStateTreeTools;

	/** Common UI: create cross-platform UI widgets (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|UI",
		meta = (ToolTip = "CommonUI tools: create activatable widgets, list CommonUI widgets. Requires CommonUI plugin. ~400 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableCommonUITools;

	/** Performance: render stats, memory report, scene templates (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Workflow",
		meta = (ToolTip = "Performance tools: render stats, memory reports, scene templates. ~800 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnablePerformanceTools;

	/** Asset Management: folders, references, unused assets, texture settings, size reports (7 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Workflow",
		meta = (ToolTip = "Asset Management tools: create folders, move assets, find unused, dependency graph, texture settings, size reports. ~1500 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableAssetManagementTools;

	/** Engine API search: search classes, grep headers, read header files (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Workflow",
		meta = (ToolTip = "Engine API tools: search engine classes/methods in installed headers, read header files. ~800 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableEngineAPITools;

	// ================================================================
	// v4.5 (UE 5.8) — new feature families
	// ================================================================

	/** Lighting: MegaLights / Lumen control via rendering cvars (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Lighting tools (5.8): toggle MegaLights (r.MegaLights.Enable), Lumen GI (r.Lumen.*), and read lighting cvars. ~500 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableLightingTools;

	/** Morph Targets: list and drive skeletal-mesh blendshape weights (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "Morph Target tools (5.8): list a skeletal mesh's morph targets, set/get/clear blendshape weights on an actor. ~600 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableMorphTargetTools;

	/** Animation Mixer: layer animation in Sequencer (4 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Cinematic",
		meta = (ToolTip = "Animation Mixer tools (5.8): add a mixer track to a binding, add/inspect layers, add animations. Wraps MovieSceneAnimMixerScripting. ~800 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableAnimMixerTools;

	/** Gizmo: viewport transform widget mode + coordinate space (3 tools). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Workflow",
		meta = (ToolTip = "Gizmo tools (5.8): set/get the viewport transform gizmo mode (translate/rotate/scale) and coordinate space. ~400 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableGizmoTools;

	/** Substrate: material-system status/introspection (1 tool). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Scene Building",
		meta = (ToolTip = "Substrate tools (5.8): report whether the Substrate material system is enabled and its key rendering settings. ~300 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableSubstrateTools;

	/** Iris: replication system status (1 tool). */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Gameplay",
		meta = (ToolTip = "Iris tools (5.8): report the Iris replication configuration (net.Iris.* cvars + module-loaded). ~300 context tokens.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableIrisTools;

	/** Epic interop: import the engine's first-party MCP toolset tools as epic_*. */
	UPROPERTY(config, EditAnywhere, Category = "Tool Categories|Workflow",
		meta = (ToolTip = "Interop (5.8): import tools registered with Epic's experimental ModelContextProtocol plugin (UToolsetDefinition / AICallable) into this registry as epic_<name>. No-op if that plugin is disabled.", EditCondition = "ToolPreset == EMCPToolPreset::Custom"))
	bool bEnableEpicToolsetInterop;

	// ================================================================
	// Safety
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Safety",
		meta = (ToolTip = "Allow the run_console_command tool to execute. Checked at execution time — toggling takes effect immediately, no restart needed. Console commands are arbitrary engine execution; consider disabling when the server is reachable beyond localhost."))
	bool bEnableConsoleCommands;

	UPROPERTY(config, EditAnywhere, Category = "Safety",
		meta = (ToolTip = "Grant the Destructive scope to sessions, allowing tools annotated as destructive (delete_asset, sc_revert, etc.) to run. Off by default. When auth tokens are enabled, a token mapped to the Destructive scope can also grant this per session."))
	bool bAllowDestructiveScope;

	// NOTE (v4): bEnableDestructiveOperations was removed — it was never read
	// anywhere, so unchecking it silently did nothing (the same false-security
	// trap as the old bAllowRemoteConnections, removed in 3.2.0). The enforced
	// gate is bAllowDestructiveScope + per-token scopes (see MCPAuth).

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
