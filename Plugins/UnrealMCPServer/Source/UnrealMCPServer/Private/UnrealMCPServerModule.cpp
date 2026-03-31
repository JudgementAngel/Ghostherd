#include "UnrealMCPServerModule.h"
#include "MCPHttpServer.h"
#include "MCPToolRegistry.h"
#include "MCPResourceProvider.h"
#include "MCPPromptProvider.h"
#include "MCPSettings.h"
#include "MCPProtocol.h"

// Tools
#include "Tools/MCPActorTools.h"
#include "Tools/MCPBlueprintTools.h"
#include "Tools/MCPEditorTools.h"
#include "Tools/MCPAssetTools.h"
#include "Tools/MCPMaterialTools.h"
#include "Tools/MCPLevelTools.h"
#include "Tools/MCPPythonBridge.h"
#include "Tools/MCPStaticMeshTools.h"
#include "Tools/MCPBatchTools.h"
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
#include "Tools/MCPPCGTools.h"
#include "Tools/MCPWorldPartitionTools.h"
#include "Tools/MCPSplineTools.h"
#include "Tools/MCPGASTools.h"
#include "Tools/MCPEnhancedInputTools.h"
#include "Tools/MCPAITools.h"
#include "Tools/MCPGameFrameworkTools.h"
#include "Tools/MCPMacroTools.h"
#include "Tools/MCPBuildTools.h"
#include "Tools/MCPAnimGraphTools.h"
#include "Tools/MCPMaterialGraphTools.h"
#include "Tools/MCPMetaSoundTools.h"
#include "Tools/MCPNetworkingTools.h"
#include "Tools/MCPEngineAPITools.h"

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

	// Register status bar widget
	RegisterStatusBarWidget();

	UE_LOG(LogUnrealMCP, Log, TEXT("=== Unreal MCP Server startup complete ==="));
}

void FUnrealMCPServerModule::ShutdownModule()
{
	UE_LOG(LogUnrealMCP, Log, TEXT("Unreal MCP Server shutting down"));

	// Stop server
	FMCPHttpServer::Get().Stop();

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
		MCPActorTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("Editor")))
		MCPEditorTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("Asset")))
		MCPAssetTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("Level")))
		MCPLevelTools::RegisterAll(Registry);

	// ---- Scene Building ----
	if (Settings->IsCategoryEnabled(FName("Material")))
		MCPMaterialTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("StaticMesh")))
		MCPStaticMeshTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("Batch")))
		MCPBatchTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("Environment")))
		MCPEnvironmentTools::RegisterAll(Registry);

	// ---- Scripting ----
	if (Settings->IsCategoryEnabled(FName("Blueprint")))
		MCPBlueprintTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("Python")))
		MCPPythonBridge::RegisterAll(Registry);

	// ---- Cinematic ----
	if (Settings->IsCategoryEnabled(FName("Sequencer")))
		MCPSequencerTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("Animation")))
		MCPAnimationTools::RegisterAll(Registry);

	// ---- World Building ----
	if (Settings->IsCategoryEnabled(FName("Landscape")))
		MCPLandscapeTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("Foliage")))
		MCPFoliageTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("WorldPartition")))
		MCPWorldPartitionTools::RegisterAll(Registry);

	// ---- VFX & Audio ----
	if (Settings->IsCategoryEnabled(FName("Niagara")))
		MCPNiagaraTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("Audio")))
		MCPAudioTools::RegisterAll(Registry);

	// ---- Simulation ----
	if (Settings->IsCategoryEnabled(FName("Physics")))
		MCPPhysicsTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("Navigation")))
		MCPNavigationTools::RegisterAll(Registry);

	// ---- Data ----
	if (Settings->IsCategoryEnabled(FName("Data")))
		MCPDataTools::RegisterAll(Registry);

	// ---- UI ----
	if (Settings->IsCategoryEnabled(FName("Widget")))
		MCPWidgetTools::RegisterAll(Registry);

	// ---- Procedural ----
	if (Settings->IsCategoryEnabled(FName("PCG")))
		MCPPCGTools::RegisterAll(Registry);

	// ---- World Building (extended) ----
	if (Settings->IsCategoryEnabled(FName("Spline")))
		MCPSplineTools::RegisterAll(Registry);

	// ---- Gameplay ----
	if (Settings->IsCategoryEnabled(FName("GAS")))
		MCPGASTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("EnhancedInput")))
		MCPEnhancedInputTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("GameFramework")))
		MCPGameFrameworkTools::RegisterAll(Registry);

	// ---- AI ----
	if (Settings->IsCategoryEnabled(FName("AI")))
		MCPAITools::RegisterAll(Registry);

	// ---- Workflow ----
	if (Settings->IsCategoryEnabled(FName("Macro")))
		MCPMacroTools::RegisterAll(Registry);

	if (Settings->IsCategoryEnabled(FName("Build")))
		MCPBuildTools::RegisterAll(Registry);

	// ---- Cinematic (extended) ----
	if (Settings->IsCategoryEnabled(FName("AnimGraph")))
		MCPAnimGraphTools::RegisterAll(Registry);

	// ---- Scene Building (extended) ----
	if (Settings->IsCategoryEnabled(FName("MaterialGraph")))
		MCPMaterialGraphTools::RegisterAll(Registry);

	// ---- VFX & Audio (extended) ----
	if (Settings->IsCategoryEnabled(FName("MetaSound")))
		MCPMetaSoundTools::RegisterAll(Registry);

	// ---- Networking ----
	if (Settings->IsCategoryEnabled(FName("Networking")))
		MCPNetworkingTools::RegisterAll(Registry);

	// ---- Workflow (extended) ----
	if (Settings->IsCategoryEnabled(FName("EngineAPI")))
		MCPEngineAPITools::RegisterAll(Registry);

	UE_LOG(LogUnrealMCP, Log, TEXT("Registered %d tools (preset: %s)"), Registry.GetToolCount(),
		Settings->ToolPreset == EMCPToolPreset::Full ? TEXT("Full") :
		Settings->ToolPreset == EMCPToolPreset::SceneBuilding ? TEXT("Scene Building") :
		Settings->ToolPreset == EMCPToolPreset::Gameplay ? TEXT("Gameplay") :
		Settings->ToolPreset == EMCPToolPreset::Minimal ? TEXT("Minimal") : TEXT("Custom"));
}

void FUnrealMCPServerModule::RegisterAllResources()
{
	MCPBuiltInResources::RegisterAll(FMCPResourceProvider::Get());
}

void FUnrealMCPServerModule::RegisterAllPrompts()
{
	MCPBuiltInPrompts::RegisterAll(FMCPPromptProvider::Get());
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
