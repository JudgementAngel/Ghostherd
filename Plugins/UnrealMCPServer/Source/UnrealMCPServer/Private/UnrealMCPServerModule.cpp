// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UnrealMCPServerModule.h"
#include "MCPActorChangePlans.h"
#include "MCPEditorSurfaces.h"
#include "MCPDiagnostics.h"
#include "MCPSnapshots.h"
#include "MCPScenarios.h"
#include "MCPHttpServer.h"
#include "MCPToolRegistry.h"
#include "MCPResourceProvider.h"
#include "MCPPromptProvider.h"
#include "MCPSettings.h"
#include "MCPProtocol.h"

// Tools
#include "Tools/MCPActorTools.h"
#include "Tools/MCPMoltTools.h"
#include "Tools/MCPBlueprintTools.h"
#include "Tools/MCPEditorTools.h"
#include "Tools/MCPAssetTools.h"
#include "Tools/MCPMaterialTools.h"
#include "Tools/MCPLevelTools.h"
#include "Tools/MCPPythonBridge.h"
#include "Tools/MCPStaticMeshTools.h"
#include "Tools/MCPBatchTools.h"
#include "Tools/MCPSpatialTools.h"
#include "Tools/MCPEnvironmentTools.h"
#include "Tools/MCPLandscapeTools.h"
#include "Tools/MCPSequencerTools.h"
#include "Tools/MCPFoliageTools.h"
#include "Tools/MCPNiagaraTools.h"
#include "Tools/MCPDataTools.h"
#include "Tools/MCPNavigationTools.h"
#include "Tools/MCPAudioTools.h"
#include "Tools/MCPPhysicsTools.h"
#include "Tools/MCPAnimationTools.h"
#include "Tools/MCPWidgetTools.h"
#include "Tools/MCPUIImageTools.h"
#include "Tools/MCP3DModelTools.h"
#include "Tools/MCPPCGTools.h"
#include "Tools/MCPAuthoringVerification.h"
#include "MCPExternalGeneration.h"
#include "MCPResultStore.h"
#include "MCPGeneralPlans.h"
#include "Tools/MCPWorldPartitionTools.h"
#include "Tools/MCPSplineTools.h"
#include "Tools/MCPGASTools.h"
#include "Tools/MCPEnhancedInputTools.h"
#include "Tools/MCPAITools.h"
#include "Tools/MCPGameFrameworkTools.h"
#include "Tools/MCPMacroTools.h"
#include "Tools/MCPBuildTools.h"
#include "Tools/MCPAnimGraphTools.h"
#include "Tools/MCPAnimTools.h"   // v4.6: Montage / AnimData / AnimGraphNodes
#include "Tools/MCPMaterialGraphTools.h"
#include "Tools/MCPMetaSoundTools.h"
#include "Tools/MCPNetworkingTools.h"
#include "Tools/MCPPIETools.h"
#include "Tools/MCPEngineAPITools.h"
#include "Tools/MCPSearchTools.h"
#include "Tools/MCPMetaTools.h"
#include "Tools/MCPGameplayTagTools.h"
#include "Tools/MCPControlRigTools.h"
#include "Tools/MCPAssetManagementTools.h"
#include "Tools/MCPStateTreeTools.h"
#include "Tools/MCPCommonUITools.h"
#include "Tools/MCPPerformanceTools.h"
#include "Tools/MCPSourceControlTools.h"
#include "Tools/MCPTestAuthoringTools.h"
#include "Tools/MCPDebugTools.h"
#include "Tools/MCPMetaSoundGraphTools.h"
#include "Tools/MCPModelingTools.h"
#include "Tools/MCPMaterialLayerTools.h"
#include "Tools/MCPChaosTools.h"
#include "Tools/MCPMetaHumanTools.h"
// v4.5 (UE 5.8) — new feature families
#include "Tools/MCPLightingTools.h"
#include "Tools/MCPMorphTargetTools.h"
#include "Tools/MCPAnimMixerTools.h"
#include "Tools/MCPGizmoTools.h"
#include "Tools/MCPSubstrateTools.h"
#include "Tools/MCPIrisTools.h"
#include "Tools/MCPToolsetAdapter.h"
#include "MCPSearchIndex.h"

// UI
#include "UI/SMCPStatusBarWidget.h"
#include "ToolMenus.h"

// Resources & Prompts
#include "Resources/MCPBuiltInResources.h"
#include "Prompts/MCPBuiltInPrompts.h"

DEFINE_LOG_CATEGORY(LogUnrealMCP);

IMPLEMENT_MODULE(FUnrealMCPServerModule, UnrealMCPServer)

void FUnrealMCPServerModule::StartupModule()
{
	UE_LOG(LogUnrealMCP, Log, TEXT("=== Unreal MCP Server starting up ==="));

	// Register tools, resources, and prompts
	RegisterAllTools();
	RegisterAllResources();
	RegisterAllPrompts();

	// Start HTTP server if auto-start enabled
	const UMCPSettings* Settings = UMCPSettings::Get();
	if (Settings->bAutoStartServer)
	{
		FMCPHttpServer& Server = FMCPHttpServer::Get();
		if (Server.Start(Settings->ServerPort))
		{
			int32 ToolCount = FMCPToolRegistry::Get().GetToolCount();
			UE_LOG(LogUnrealMCP, Log, TEXT("MCP server ready with %d tools on port %d"), ToolCount, Settings->ServerPort);
			UE_LOG(LogUnrealMCP, Log, TEXT("Connect your AI client to: http://localhost:%d/mcp"), Settings->ServerPort);
		}
		else
		{
			UE_LOG(LogUnrealMCP, Error, TEXT("Failed to start MCP server on port %d"), Settings->ServerPort);
		}
	}
	else
	{
		UE_LOG(LogUnrealMCP, Log, TEXT("MCP server auto-start disabled. Enable in Project Settings > Plugins > Unreal MCP Server."));
	}

	// Build search index (deferred to allow assets to finish loading)
	FMCPSearchIndex::Get().Build();

	// Register status bar widget
	RegisterStatusBarWidget();

	UE_LOG(LogUnrealMCP, Log, TEXT("=== Unreal MCP Server startup complete ==="));
}

void FUnrealMCPServerModule::ShutdownModule()
{
	UE_LOG(LogUnrealMCP, Log, TEXT("Unreal MCP Server shutting down"));

	// Stop server
	FMCPHttpServer::Get().Stop();
    MCPActorChangePlans::Reset();
    MCPEditorSurfaces::Reset();
    MCPSnapshots::Reset();
    MCPScenarios::Reset();
    MCPDiagnostics::Reset();
    MCPResultStore::Reset();

	// Clear registries
	FMCPToolRegistry::Get().UnregisterAllTools();
	FMCPResourceProvider::Get().UnregisterAllResources();
	FMCPPromptProvider::Get().UnregisterAllPrompts();
}

void FUnrealMCPServerModule::RegisterAllTools()
{
	FMCPToolRegistry& Registry = FMCPToolRegistry::Get();
	const UMCPSettings* Settings = UMCPSettings::Get();

	UE_LOG(LogUnrealMCP, Log, TEXT("Tool preset: %s"),
		Settings->ToolPreset == EMCPToolPreset::Full ? TEXT("Full") :
		Settings->ToolPreset == EMCPToolPreset::SceneBuilding ? TEXT("Scene Building") :
		Settings->ToolPreset == EMCPToolPreset::Gameplay ? TEXT("Gameplay") :
		Settings->ToolPreset == EMCPToolPreset::Minimal ? TEXT("Minimal") : TEXT("Custom"));

	// ---- Core ----
	if (Settings->IsCategoryEnabled(FName("Actor")))
	{
		Registry.SetActiveCategory(FName("Actor"));
		MCPActorTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("Editor")))
	{
		Registry.SetActiveCategory(FName("Editor"));
		MCPEditorTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("Asset")))
	{
		Registry.SetActiveCategory(FName("Asset"));
		MCPAssetTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("Level")))
	{
		Registry.SetActiveCategory(FName("Level"));
		MCPLevelTools::RegisterAll(Registry);
	}

	// ---- Scene Building ----
	if (Settings->IsCategoryEnabled(FName("Material")))
	{
		Registry.SetActiveCategory(FName("Material"));
		MCPMaterialTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("StaticMesh")))
	{
		Registry.SetActiveCategory(FName("StaticMesh"));
		MCPStaticMeshTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("Batch")))
	{
		Registry.SetActiveCategory(FName("Batch"));
		MCPBatchTools::RegisterAll(Registry);
	}

	// ---- Spatial Awareness ----
	if (Settings->IsCategoryEnabled(FName("Spatial")))
	{
		Registry.SetActiveCategory(FName("Spatial"));
		MCPSpatialTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("Environment")))
	{
		Registry.SetActiveCategory(FName("Environment"));
		MCPEnvironmentTools::RegisterAll(Registry);
	}

	// ---- Scripting ----
	if (Settings->IsCategoryEnabled(FName("Blueprint")))
	{
		Registry.SetActiveCategory(FName("Blueprint"));
		MCPBlueprintTools::RegisterAll(Registry);
	}

	// ---- Project gap-fill tools (set_component_socket, add/list_skeleton_socket, pie_get_actor_property) ----
	// Registered unconditionally; each tool sets its own (existing) category internally.
	// See Plugins/UnrealMCPServer_v4/docs/MCP_TOOLS_GAP_PLAN.md
	MCPMoltTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("Python")))
	{
		Registry.SetActiveCategory(FName("Python"));
		MCPPythonBridge::RegisterAll(Registry);
	}

	// ---- Cinematic ----
	if (Settings->IsCategoryEnabled(FName("Sequencer")))
	{
		Registry.SetActiveCategory(FName("Sequencer"));
		MCPSequencerTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("Animation")))
	{
		Registry.SetActiveCategory(FName("Animation"));
		MCPAnimationTools::RegisterAll(Registry);
	}

	// ---- World Building ----
	if (Settings->IsCategoryEnabled(FName("Landscape")))
	{
		Registry.SetActiveCategory(FName("Landscape"));
		MCPLandscapeTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("Foliage")))
	{
		Registry.SetActiveCategory(FName("Foliage"));
		MCPFoliageTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("WorldPartition")))
	{
		Registry.SetActiveCategory(FName("WorldPartition"));
		MCPWorldPartitionTools::RegisterAll(Registry);
	}

	// ---- VFX & Audio ----
	if (Settings->IsCategoryEnabled(FName("Niagara")))
	{
		Registry.SetActiveCategory(FName("Niagara"));
		MCPNiagaraTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("Audio")))
	{
		Registry.SetActiveCategory(FName("Audio"));
		MCPAudioTools::RegisterAll(Registry);
	}

	// ---- Simulation ----
	if (Settings->IsCategoryEnabled(FName("Physics")))
	{
		Registry.SetActiveCategory(FName("Physics"));
		MCPPhysicsTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("Navigation")))
	{
		Registry.SetActiveCategory(FName("Navigation"));
		MCPNavigationTools::RegisterAll(Registry);
	}

	// ---- Data ----
	if (Settings->IsCategoryEnabled(FName("Data")))
	{
		Registry.SetActiveCategory(FName("Data"));
		MCPDataTools::RegisterAll(Registry);
	}

	// ---- UI ----
	if (Settings->IsCategoryEnabled(FName("Widget")))
	{
		Registry.SetActiveCategory(FName("Widget"));
		MCPWidgetTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("UIImage")))
	{
		Registry.SetActiveCategory(FName("UIImage"));
		MCPUIImageTools::RegisterAll(Registry);
	}

	// ---- AI 3D Generation ----
	if (Settings->IsCategoryEnabled(FName("3DModel")))
	{
		Registry.SetActiveCategory(FName("3DModel"));
		MCP3DModelTools::RegisterAll(Registry);
	}

	// ---- Procedural ----
	if (Settings->IsCategoryEnabled(FName("PCG")))
	{
		Registry.SetActiveCategory(FName("PCG"));
		MCPPCGTools::RegisterAll(Registry);
		MCPAuthoringVerification::RegisterAll(Registry); // v5 increment 19: PCG/import/material/Niagara/sound verification
	}

	// ---- World Building (extended) ----
	if (Settings->IsCategoryEnabled(FName("Spline")))
	{
		Registry.SetActiveCategory(FName("Spline"));
		MCPSplineTools::RegisterAll(Registry);
	}

	// ---- Gameplay ----
	if (Settings->IsCategoryEnabled(FName("GAS")))
	{
		Registry.SetActiveCategory(FName("GAS"));
		MCPGASTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("EnhancedInput")))
	{
		Registry.SetActiveCategory(FName("EnhancedInput"));
		MCPEnhancedInputTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("GameFramework")))
	{
		Registry.SetActiveCategory(FName("GameFramework"));
		MCPGameFrameworkTools::RegisterAll(Registry);
	}

	// ---- AI ----
	if (Settings->IsCategoryEnabled(FName("AI")))
	{
		Registry.SetActiveCategory(FName("AI"));
		MCPAITools::RegisterAll(Registry);
	}

	// ---- Workflow ----
	if (Settings->IsCategoryEnabled(FName("Macro")))
	{
		Registry.SetActiveCategory(FName("Macro"));
		MCPMacroTools::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("Build")))
	{
		Registry.SetActiveCategory(FName("Build"));
		MCPBuildTools::RegisterAll(Registry);
	}

	// ---- Cinematic (extended) ----
	if (Settings->IsCategoryEnabled(FName("AnimGraph")))
	{
		Registry.SetActiveCategory(FName("AnimGraph"));
		MCPAnimGraphTools::RegisterAll(Registry);
	}

	// ---- v4.6 Animation authoring ----
	if (Settings->IsCategoryEnabled(FName("Montage")))
	{
		Registry.SetActiveCategory(FName("Montage"));
		MCPAnimTools::Montage::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("AnimData")))
	{
		Registry.SetActiveCategory(FName("AnimData"));
		MCPAnimTools::AnimData::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("AnimGraphNodes")))
	{
		Registry.SetActiveCategory(FName("AnimGraphNodes"));
		MCPAnimTools::AnimGraphNodes::RegisterAll(Registry);
		MCPAnimTools::Validation::RegisterAll(Registry);
	}

	if (Settings->IsCategoryEnabled(FName("SequencerAnimation")))
	{
		Registry.SetActiveCategory(FName("SequencerAnimation"));
		MCPSequencerTools::AnimationTracks::RegisterAll(Registry);
	}

	// ---- Scene Building (extended) ----
	if (Settings->IsCategoryEnabled(FName("MaterialGraph")))
	{
		Registry.SetActiveCategory(FName("MaterialGraph"));
		MCPMaterialGraphTools::RegisterAll(Registry);
	}

	// ---- VFX & Audio (extended) ----
	if (Settings->IsCategoryEnabled(FName("MetaSound")))
	{
		Registry.SetActiveCategory(FName("MetaSound"));
		MCPMetaSoundTools::RegisterAll(Registry);
	}

	// ---- Networking ----
	if (Settings->IsCategoryEnabled(FName("Networking")))
	{
		Registry.SetActiveCategory(FName("Networking"));
		MCPNetworkingTools::RegisterAll(Registry);
	}

	// ---- PIE Control ----
	if (Settings->IsCategoryEnabled(FName("PIE")))
	{
		Registry.SetActiveCategory(FName("PIE"));
		MCPPIETools::RegisterAll(Registry);
	}

	// ---- Workflow (extended) ----
	if (Settings->IsCategoryEnabled(FName("EngineAPI")))
	{
		Registry.SetActiveCategory(FName("EngineAPI"));
		MCPEngineAPITools::RegisterAll(Registry);
	}

	// ---- Gameplay Tags ----
	if (Settings->IsCategoryEnabled(FName("GameplayTags")))
	{
		Registry.SetActiveCategory(FName("GameplayTags"));
		MCPGameplayTagTools::RegisterAll(Registry);
	}

	// ---- Control Rig ----
	if (Settings->IsCategoryEnabled(FName("ControlRig")))
	{
		Registry.SetActiveCategory(FName("ControlRig"));
		MCPControlRigTools::RegisterAll(Registry);
	}

	// ---- Asset Management ----
	if (Settings->IsCategoryEnabled(FName("AssetManagement")))
	{
		Registry.SetActiveCategory(FName("AssetManagement"));
		MCPAssetManagementTools::RegisterAll(Registry);
	}

	// ---- State Trees ----
	if (Settings->IsCategoryEnabled(FName("StateTree")))
	{
		Registry.SetActiveCategory(FName("StateTree"));
		MCPStateTreeTools::RegisterAll(Registry);
	}

	// ---- Common UI ----
	if (Settings->IsCategoryEnabled(FName("CommonUI")))
	{
		Registry.SetActiveCategory(FName("CommonUI"));
		MCPCommonUITools::RegisterAll(Registry);
	}

	// ---- Performance & Templates ----
	if (Settings->IsCategoryEnabled(FName("Performance")))
	{
		Registry.SetActiveCategory(FName("Performance"));
		MCPPerformanceTools::RegisterAll(Registry);
	}

	// ---- Phase D.2 — Source Control ----
	if (Settings->IsCategoryEnabled(FName("SourceControl")))
	{
		Registry.SetActiveCategory(FName("SourceControl"));
		MCPSourceControlTools::RegisterAll(Registry);
	}

	// ---- Phase D.3 — Test Authoring & Run ----
	if (Settings->IsCategoryEnabled(FName("TestAuthoring")))
	{
		Registry.SetActiveCategory(FName("TestAuthoring"));
		MCPTestAuthoringTools::RegisterAll(Registry);
	}

	// ---- Phase D.4 — Runtime Debug & Introspection ----
	if (Settings->IsCategoryEnabled(FName("Debug")))
	{
		Registry.SetActiveCategory(FName("Debug"));
		MCPDebugTools::RegisterAll(Registry);
	}

	// ---- Phase D.5 — MetaSound Graph parity ----
	if (Settings->IsCategoryEnabled(FName("MetaSoundGraph")))
	{
		Registry.SetActiveCategory(FName("MetaSoundGraph"));
		MCPMetaSoundGraphTools::RegisterAll(Registry);
	}

	// ---- Phase D.6 — Modeling Mode + Material Layers ----
	if (Settings->IsCategoryEnabled(FName("Modeling")))
	{
		Registry.SetActiveCategory(FName("Modeling"));
		MCPModelingTools::RegisterAll(Registry);
	}
	if (Settings->IsCategoryEnabled(FName("MaterialLayer")))
	{
		Registry.SetActiveCategory(FName("MaterialLayer"));
		MCPMaterialLayerTools::RegisterAll(Registry);
	}

	// ---- Phase D.7 — Chaos / Destruction ----
	if (Settings->IsCategoryEnabled(FName("Chaos")))
	{
		Registry.SetActiveCategory(FName("Chaos"));
		MCPChaosTools::RegisterAll(Registry);
	}

	// ---- Phase D.8 — MetaHuman ----
	if (Settings->IsCategoryEnabled(FName("MetaHuman")))
	{
		Registry.SetActiveCategory(FName("MetaHuman"));
		MCPMetaHumanTools::RegisterAll(Registry);
	}

	// ---- v4.5 (UE 5.8) — Lighting (MegaLights / Lumen) ----
	if (Settings->IsCategoryEnabled(FName("Lighting")))
	{
		Registry.SetActiveCategory(FName("Lighting"));
		MCPLightingTools::RegisterAll(Registry);
	}

	// ---- v4.5 (UE 5.8) — Morph Targets / blendshapes ----
	if (Settings->IsCategoryEnabled(FName("MorphTarget")))
	{
		Registry.SetActiveCategory(FName("MorphTarget"));
		MCPMorphTargetTools::RegisterAll(Registry);
	}

	// ---- v4.5 (UE 5.8) — Animation Mixer (Sequencer) ----
	if (Settings->IsCategoryEnabled(FName("AnimMixer")))
	{
		Registry.SetActiveCategory(FName("AnimMixer"));
		MCPAnimMixerTools::RegisterAll(Registry);
	}

	// ---- v4.5 (UE 5.8) — Viewport transform gizmo ----
	if (Settings->IsCategoryEnabled(FName("Gizmo")))
	{
		Registry.SetActiveCategory(FName("Gizmo"));
		MCPGizmoTools::RegisterAll(Registry);
	}

	// ---- v4.5 (UE 5.8) — Substrate material status ----
	if (Settings->IsCategoryEnabled(FName("Substrate")))
	{
		Registry.SetActiveCategory(FName("Substrate"));
		MCPSubstrateTools::RegisterAll(Registry);
	}

	// ---- v4.5 (UE 5.8) — Iris replication status ----
	if (Settings->IsCategoryEnabled(FName("Iris")))
	{
		Registry.SetActiveCategory(FName("Iris"));
		MCPIrisTools::RegisterAll(Registry);
	}

	// ---- v4.5 (UE 5.8) — interop: import Epic first-party MCP toolset tools ----
	if (Settings->IsCategoryEnabled(FName("EpicToolsets")))
	{
		Registry.SetActiveCategory(FName("EpicToolsets"));
		MCPToolsetAdapter::ImportEpicToolsets(Registry);
	}

	// ---- Search ----
	Registry.SetActiveCategory(FName("Search"));
	MCPSearchTools::RegisterAll(Registry);

	// ---- v4 Phase 1: progressive-disclosure meta-tools (always on) ----
	Registry.SetActiveCategory(FName("Meta"));
	MCPMetaTools::RegisterAll(Registry);
    Registry.SetActiveCategory(FName("ChangePlans"));
    MCPActorChangePlans::RegisterAll(Registry);
    MCPGeneralPlans::RegisterAll(Registry); // v5 increment 25
    Registry.SetActiveCategory(FName("Visual"));
    MCPEditorSurfaces::RegisterAll(Registry);
    Registry.SetActiveCategory(FName("Diagnostics"));
    MCPDiagnostics::RegisterAll(Registry);
    Registry.SetActiveCategory(FName("Snapshots"));
    MCPSnapshots::RegisterAll(Registry);
    Registry.SetActiveCategory(FName("Operations"));
    MCPScenarios::RegisterAll(Registry);
    MCPExternalGeneration::RegisterAll(Registry); // v5 increment 20
    MCPResultStore::RegisterAll(Registry); // v5 increment 24

	Registry.SetActiveCategory(NAME_None);

	UE_LOG(LogUnrealMCP, Log, TEXT("Registered %d tools (preset: %s)"), Registry.GetToolCount(),
		Settings->ToolPreset == EMCPToolPreset::Full ? TEXT("Full") :
		Settings->ToolPreset == EMCPToolPreset::SceneBuilding ? TEXT("Scene Building") :
		Settings->ToolPreset == EMCPToolPreset::Gameplay ? TEXT("Gameplay") :
		Settings->ToolPreset == EMCPToolPreset::Minimal ? TEXT("Minimal") : TEXT("Custom"));
}

void FUnrealMCPServerModule::RegisterAllResources()
{
	MCPBuiltInResources::RegisterAll(FMCPResourceProvider::Get());
    MCPEditorSurfaces::RegisterResources(FMCPResourceProvider::Get());
    MCPSnapshots::RegisterResources(FMCPResourceProvider::Get());
    MCPActorChangePlans::RegisterResources(FMCPResourceProvider::Get());
    MCPDiagnostics::RegisterResources(FMCPResourceProvider::Get()); // v5 increment 20
    MCPResultStore::RegisterResources(FMCPResourceProvider::Get()); // v5 increment 24
}

void FUnrealMCPServerModule::RegisterAllPrompts()
{
	MCPBuiltInPrompts::RegisterAll(FMCPPromptProvider::Get());
    MCPEditorSurfaces::RegisterPrompts(FMCPPromptProvider::Get());
}

void FUnrealMCPServerModule::RegisterStatusBarWidget()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.StatusBar.ToolBar");
		FToolMenuSection& Section = Menu->FindOrAddSection("MCPServer");

		Section.AddEntry(
			FToolMenuEntry::InitWidget(
				"MCPServerStatus",
				SNew(SMCPStatusBarWidget),
				FText::GetEmpty(),
				true,   // bNoIndent
				false   // bSearchable
			)
		);

		UE_LOG(LogUnrealMCP, Log, TEXT("MCP status bar widget registered"));
	}));
}
