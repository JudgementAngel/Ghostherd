#include "MCPSettings.h"

UMCPSettings::UMCPSettings()
	: ServerPort(13579)
	, bAutoStartServer(true)
	, bAllowRemoteConnections(false)
	, MaxRequestsPerMinute(120)
	, bVerboseLogging(false)
	, ToolPreset(EMCPToolPreset::Full)
	// Core (always on by default)
	, bEnableActorTools(true)
	, bEnableEditorTools(true)
	, bEnableAssetTools(true)
	, bEnableLevelTools(true)
	// Scene Building
	, bEnableMaterialTools(true)
	, bEnableStaticMeshTools(true)
	, bEnableBatchTools(true)
	, bEnableEnvironmentTools(true)
	// Scripting
	, bEnableBlueprintTools(true)
	, bEnablePythonBridge(true)
	// Cinematic
	, bEnableSequencerTools(true)
	, bEnableAnimationTools(true)
	// World Building
	, bEnableLandscapeTools(true)
	, bEnableFoliageTools(true)
	// VFX & Audio
	, bEnableNiagaraTools(true)
	, bEnableAudioTools(true)
	// Simulation
	, bEnablePhysicsTools(true)
	, bEnableNavigationTools(true)
	// Data
	, bEnableDataTools(true)
	// UI
	, bEnableWidgetTools(true)
	// Procedural
	, bEnablePCGTools(true)
	// World Building (continued)
	, bEnableWorldPartitionTools(true)
	, bEnableSplineTools(true)
	// Gameplay
	, bEnableGASTools(true)
	, bEnableEnhancedInputTools(true)
	// AI (declared before GameFrameworkTools in header)
	, bEnableAITools(true)
	// Game Framework
	, bEnableGameFrameworkTools(true)
	// Workflow
	, bEnableMacroTools(true)
	, bEnableBuildTools(true)
	// Cinematic (continued)
	, bEnableAnimGraphTools(true)
	// Scene Building (continued)
	, bEnableMaterialGraphTools(true)
	// VFX & Audio (continued)
	, bEnableMetaSoundTools(true)
	// Gameplay (continued)
	, bEnableNetworkingTools(true)
	// Workflow (continued)
	, bEnableEngineAPITools(true)
	// Safety
	, bEnableConsoleCommands(true)
	, bEnableDestructiveOperations(false)
{
}

bool UMCPSettings::IsCategoryEnabled(FName Category) const
{
	// Presets override individual toggles
	switch (ToolPreset)
	{
	case EMCPToolPreset::Full:
		return true;

	case EMCPToolPreset::SceneBuilding:
	{
		// Core + Scene Building + Scripting + Spline categories
		static const TSet<FName> SceneBuildingCategories = {
			FName("Actor"), FName("Editor"), FName("Asset"), FName("Level"),
			FName("Material"), FName("StaticMesh"), FName("Batch"), FName("Environment"),
			FName("Blueprint"), FName("Python"), FName("Spline"), FName("Macro"), FName("Build"),
			FName("MaterialGraph"), FName("EngineAPI")
		};
		return SceneBuildingCategories.Contains(Category);
	}

	case EMCPToolPreset::Gameplay:
	{
		// Core + Gameplay + AI + Scripting + Workflow categories
		static const TSet<FName> GameplayCategories = {
			FName("Actor"), FName("Editor"), FName("Asset"), FName("Level"),
			FName("Blueprint"), FName("Python"),
			FName("GAS"), FName("EnhancedInput"), FName("GameFramework"),
			FName("AI"), FName("Macro"), FName("Build"),
			FName("Physics"), FName("Navigation"), FName("Data"),
			FName("Networking"), FName("EngineAPI")
		};
		return GameplayCategories.Contains(Category);
	}

	case EMCPToolPreset::Minimal:
	{
		// Only core categories
		static const TSet<FName> MinimalCategories = {
			FName("Actor"), FName("Editor"), FName("Level")
		};
		return MinimalCategories.Contains(Category);
	}

	case EMCPToolPreset::Custom:
	{
		// Use per-category booleans
		if (Category == FName("Actor"))				return bEnableActorTools;
		if (Category == FName("Editor"))			return bEnableEditorTools;
		if (Category == FName("Asset"))				return bEnableAssetTools;
		if (Category == FName("Level"))				return bEnableLevelTools;
		if (Category == FName("Material"))			return bEnableMaterialTools;
		if (Category == FName("StaticMesh"))		return bEnableStaticMeshTools;
		if (Category == FName("Batch"))				return bEnableBatchTools;
		if (Category == FName("Environment"))		return bEnableEnvironmentTools;
		if (Category == FName("Blueprint"))			return bEnableBlueprintTools;
		if (Category == FName("Python"))			return bEnablePythonBridge;
		if (Category == FName("Sequencer"))			return bEnableSequencerTools;
		if (Category == FName("Animation"))			return bEnableAnimationTools;
		if (Category == FName("Landscape"))			return bEnableLandscapeTools;
		if (Category == FName("Foliage"))			return bEnableFoliageTools;
		if (Category == FName("Niagara"))			return bEnableNiagaraTools;
		if (Category == FName("Audio"))				return bEnableAudioTools;
		if (Category == FName("Physics"))			return bEnablePhysicsTools;
		if (Category == FName("Navigation"))		return bEnableNavigationTools;
		if (Category == FName("Data"))				return bEnableDataTools;
		if (Category == FName("Widget"))			return bEnableWidgetTools;
		if (Category == FName("PCG"))				return bEnablePCGTools;
		if (Category == FName("WorldPartition"))	return bEnableWorldPartitionTools;
		if (Category == FName("Spline"))			return bEnableSplineTools;
		if (Category == FName("GAS"))				return bEnableGASTools;
		if (Category == FName("EnhancedInput"))		return bEnableEnhancedInputTools;
		if (Category == FName("GameFramework"))		return bEnableGameFrameworkTools;
		if (Category == FName("AI"))				return bEnableAITools;
		if (Category == FName("Macro"))				return bEnableMacroTools;
		if (Category == FName("Build"))				return bEnableBuildTools;
		if (Category == FName("AnimGraph"))			return bEnableAnimGraphTools;
		if (Category == FName("MaterialGraph"))		return bEnableMaterialGraphTools;
		if (Category == FName("MetaSound"))			return bEnableMetaSoundTools;
		if (Category == FName("Networking"))			return bEnableNetworkingTools;
		if (Category == FName("EngineAPI"))			return bEnableEngineAPITools;
		return true; // Unknown categories default to enabled
	}
	}

	return true;
}

const UMCPSettings* UMCPSettings::Get()
{
	return GetDefault<UMCPSettings>();
}
