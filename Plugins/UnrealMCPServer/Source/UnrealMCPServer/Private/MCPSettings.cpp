// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPSettings.h"

UMCPSettings::UMCPSettings()
	: ServerPort(13579)
	, bAutoStartServer(true)
	, BindAddress(TEXT("127.0.0.1"))
	, MaxRequestsPerMinute(120)
	, MaxRequestSizeMB(8)
	, ToolCallTimeoutSeconds(60)
	, SessionIdleTimeoutMinutes(30)
	// Authentication / CORS / WebSocket (Phase B defaults)
	, bRequireAuthToken(false)
	, bEnableWebSocket(false)
	, bVerboseLogging(false)
	, ToolPreset(EMCPToolPreset::Full)
	, ToolExposureMode(EMCPToolExposureMode::Catalog)
	// Core (always on by default)
	, bEnableActorTools(true)
	, bEnableEditorTools(true)
	, bEnableAssetTools(true)
	, bEnableLevelTools(true)
	// Scene Building
	, bEnableMaterialTools(true)
	, bEnableStaticMeshTools(true)
	, bEnableBatchTools(true)
	, bEnableSpatialTools(true)
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
	, bEnableUIImageTools(true)
	, bEnable3DModelTools(true)
	, DefaultFalModel(TEXT("flux-2-flash"))
	, DefaultTextTo3DModel(TEXT("meshy-v6"))
	, DefaultImageTo3DModel(TEXT("trellis-2"))
	// Procedural
	, bEnablePCGTools(true)
	// World Building (continued)
	, bEnableWorldPartitionTools(true)
	, bEnableSplineTools(true)
	// Gameplay
	, bEnableGASTools(true)
	, bEnableEnhancedInputTools(true)
	// Gameplay Tags
	, bEnableGameplayTagTools(true)
	// AI (declared before GameFrameworkTools in header)
	, bEnableAITools(true)
	// Game Framework
	, bEnableGameFrameworkTools(true)
	// Workflow
	, bEnableMacroTools(true)
	, bEnableBuildTools(true)
	// Control Rig (header line 248)
	, bEnableControlRigTools(true)
	// Cinematic (continued, header line 253)
	, bEnableAnimGraphTools(true)
	, bEnableMontageTools(true)
	, bEnableAnimDataTools(true)
	, bEnableAnimGraphNodeTools(true)
	, bEnableSequencerAnimationTools(true)
	// Scene Building (continued, header line 258)
	, bEnableMaterialGraphTools(true)
	// VFX & Audio (continued)
	, bEnableMetaSoundTools(true)
	// Gameplay (continued)
	, bEnableNetworkingTools(true)
	, bEnablePIETools(true)
	// Phase D — new tool families
	, bEnableSourceControlTools(true)
	, bEnableTestAuthoringTools(true)
	, bEnableDebugTools(true)
	, bEnableMetaSoundGraphTools(true)
	, bEnableModelingTools(true)
	, bEnableMaterialLayerTools(true)
	, bEnableChaosTools(true)
	, bEnableMetaHumanTools(true)
	// State Trees
	, bEnableStateTreeTools(true)
	// Common UI
	, bEnableCommonUITools(true)
	// Performance
	, bEnablePerformanceTools(true)
	// Asset Management
	, bEnableAssetManagementTools(true)
	// Workflow (continued)
	, bEnableEngineAPITools(true)
	// v4.5 (UE 5.8) new families
	, bEnableLightingTools(true)
	, bEnableMorphTargetTools(true)
	, bEnableAnimMixerTools(true)
	, bEnableGizmoTools(true)
	, bEnableSubstrateTools(true)
	, bEnableIrisTools(true)
	, bEnableEpicToolsetInterop(true)
	// Safety
	, bEnableConsoleCommands(true)
	, bAllowDestructiveScope(false)
{
	// CORS allow-list defaults to localhost only. Edit in Project Settings or DefaultUnrealMCPServer.ini.
	AllowedOrigins.Add(TEXT("http://localhost"));
	AllowedOrigins.Add(TEXT("http://127.0.0.1"));
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
			FName("MaterialGraph"), FName("EngineAPI"),
			FName("SourceControl"), FName("TestAuthoring")
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
			FName("GameplayTags"),
			FName("AI"), FName("Macro"), FName("Build"),
			FName("Physics"), FName("Navigation"), FName("Data"),
			FName("Networking"), FName("PIE"), FName("EngineAPI"),
			FName("Debug"), FName("TestAuthoring"), FName("SourceControl")
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
		if (Category == FName("Spatial"))			return bEnableSpatialTools;
		if (Category == FName("Environment"))		return bEnableEnvironmentTools;
		if (Category == FName("Blueprint"))			return bEnableBlueprintTools;
		if (Category == FName("Python"))			return bEnablePythonBridge;
		if (Category == FName("Sequencer"))			return bEnableSequencerTools;
		if (Category == FName("Animation"))			return bEnableAnimationTools;
		if (Category == FName("Montage"))			return bEnableMontageTools;
		if (Category == FName("AnimData"))			return bEnableAnimDataTools;
		if (Category == FName("AnimGraphNodes"))	return bEnableAnimGraphNodeTools;
		if (Category == FName("SequencerAnimation"))return bEnableSequencerAnimationTools;
		if (Category == FName("Landscape"))			return bEnableLandscapeTools;
		if (Category == FName("Foliage"))			return bEnableFoliageTools;
		if (Category == FName("Niagara"))			return bEnableNiagaraTools;
		if (Category == FName("Audio"))				return bEnableAudioTools;
		if (Category == FName("Physics"))			return bEnablePhysicsTools;
		if (Category == FName("Navigation"))		return bEnableNavigationTools;
		if (Category == FName("Data"))				return bEnableDataTools;
		if (Category == FName("Widget"))			return bEnableWidgetTools;
		if (Category == FName("UIImage"))			return bEnableUIImageTools;
		if (Category == FName("3DModel"))			return bEnable3DModelTools;
		if (Category == FName("PCG"))				return bEnablePCGTools;
		if (Category == FName("WorldPartition"))	return bEnableWorldPartitionTools;
		if (Category == FName("Spline"))			return bEnableSplineTools;
		if (Category == FName("GAS"))				return bEnableGASTools;
		if (Category == FName("EnhancedInput"))		return bEnableEnhancedInputTools;
		if (Category == FName("GameFramework"))		return bEnableGameFrameworkTools;
		if (Category == FName("GameplayTags"))	return bEnableGameplayTagTools;
		if (Category == FName("AI"))				return bEnableAITools;
		if (Category == FName("Macro"))				return bEnableMacroTools;
		if (Category == FName("Build"))				return bEnableBuildTools;
		if (Category == FName("AnimGraph"))			return bEnableAnimGraphTools;
		if (Category == FName("ControlRig"))		return bEnableControlRigTools;
		if (Category == FName("MaterialGraph"))		return bEnableMaterialGraphTools;
		if (Category == FName("MetaSound"))			return bEnableMetaSoundTools;
		if (Category == FName("Networking"))			return bEnableNetworkingTools;
		if (Category == FName("PIE"))				return bEnablePIETools;
		if (Category == FName("StateTree"))			return bEnableStateTreeTools;
		if (Category == FName("CommonUI"))			return bEnableCommonUITools;
		if (Category == FName("Performance"))		return bEnablePerformanceTools;
		if (Category == FName("AssetManagement"))	return bEnableAssetManagementTools;
		if (Category == FName("EngineAPI"))			return bEnableEngineAPITools;
		if (Category == FName("SourceControl"))		return bEnableSourceControlTools;
		if (Category == FName("TestAuthoring"))		return bEnableTestAuthoringTools;
		if (Category == FName("Debug"))				return bEnableDebugTools;
		if (Category == FName("MetaSoundGraph"))	return bEnableMetaSoundGraphTools;
		if (Category == FName("Modeling"))			return bEnableModelingTools;
		if (Category == FName("MaterialLayer"))		return bEnableMaterialLayerTools;
		if (Category == FName("Chaos"))				return bEnableChaosTools;
		if (Category == FName("MetaHuman"))			return bEnableMetaHumanTools;
		// v4.5 (UE 5.8) new families
		if (Category == FName("Lighting"))			return bEnableLightingTools;
		if (Category == FName("MorphTarget"))		return bEnableMorphTargetTools;
		if (Category == FName("AnimMixer"))			return bEnableAnimMixerTools;
		if (Category == FName("Gizmo"))				return bEnableGizmoTools;
		if (Category == FName("Substrate"))			return bEnableSubstrateTools;
		if (Category == FName("Iris"))				return bEnableIrisTools;
		if (Category == FName("EpicToolsets"))		return bEnableEpicToolsetInterop;
		return true; // Unknown categories default to enabled
	}
	}

	return true;
}

const UMCPSettings* UMCPSettings::Get()
{
	return GetDefault<UMCPSettings>();
}
