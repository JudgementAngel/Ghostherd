// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPPCGTools.h"
#include "Common/MCPActorResolver.h"
#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

// PCG includes
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGSubsystem.h"
#include "PCGCommon.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "Common/MCPAssetCreate.h"
#include "MCPScenarios.h"
#include "MCPRequestContext.h"

namespace MCPPCGTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

static AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	// v4 Phase 1: cached resolver (O(1) amortized) replaces the per-call actor scan.
	return MCPCommon::FindActorByLabel(World, Label);

}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// list_pcg_graphs - List all PCG Graph assets in the project
	// ================================================================
	MCP_TOOL(Registry, "list_pcg_graphs")
		.Description(TEXT(
			"List all PCG Graph assets found in the project content browser. "
			"Optionally filter by path and/or name substring. Returns asset name and full content path for each graph."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("path"),
			TEXT("Content path to search under (e.g., '/Game/', '/Game/PCG/'). Default: '/Game/'"))
		.StringArg(TEXT("name_filter"),
			TEXT("Optional substring filter applied to asset names (case-insensitive)"))
		.IntArg(TEXT("limit"),
			TEXT("Maximum number of results to return (default: 100)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FAssetRegistryModule& AssetRegistryModule =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			FString SearchPath = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), SearchPath);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 5000);
			}

			// Query asset registry for UPCGGraph assets
			TArray<FAssetData> AllAssets;
			AssetRegistry.GetAssetsByPath(FName(*SearchPath), AllAssets, /*bRecursive=*/true);

			const FTopLevelAssetPath PCGGraphClassPath(TEXT("/Script/PCG"), TEXT("PCGGraph"));

			TArray<TSharedPtr<FJsonValue>> GraphArray;
			int32 TotalFound = 0;

			for (const FAssetData& Asset : AllAssets)
			{
				if (Asset.AssetClassPath != PCGGraphClassPath)
				{
					continue;
				}

				// Apply optional name filter
				if (!NameFilter.IsEmpty())
				{
					if (!Asset.AssetName.ToString().Contains(NameFilter, ESearchCase::IgnoreCase))
					{
						continue;
					}
				}

				TotalFound++;
				if (GraphArray.Num() < Limit)
				{
					TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
					GraphObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
					GraphObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
					GraphObj->SetStringField(TEXT("package"), Asset.PackageName.ToString());
					GraphArray.Add(MakeShared<FJsonValueObject>(GraphObj));
				}
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("graphs"), GraphArray);
			Result->SetNumberField(TEXT("count"), GraphArray.Num());
			Result->SetNumberField(TEXT("total_found"), TotalFound);
			Result->SetStringField(TEXT("search_path"), SearchPath);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// spawn_pcg_actor - Spawn an actor with a PCG component in the level
	// ================================================================
	MCP_TOOL(Registry, "spawn_pcg_actor")
		.Description(TEXT(
			"Spawn a new Actor with a UPCGComponent in the current level. "
			"Optionally assign a PCG Graph asset to the component. "
			"The actor's scale is set to the provided scale values, which determines the effective "
			"PCG volume bounds. Use execute_pcg after spawning to trigger graph generation."))
		.StringArg(TEXT("graph_path"),
			TEXT("Optional content path to a PCG Graph asset to assign (e.g., '/Game/PCG/MyGraph.MyGraph')"))
		.NumberArg(TEXT("x"),
			TEXT("World X position to spawn the actor (default: 0)"))
		.NumberArg(TEXT("y"),
			TEXT("World Y position to spawn the actor (default: 0)"))
		.NumberArg(TEXT("z"),
			TEXT("World Z position to spawn the actor (default: 0)"))
		.NumberArg(TEXT("scale_x"),
			TEXT("Scale X applied to the actor, effectively controlling PCG volume extent (default: 1.0)"))
		.NumberArg(TEXT("scale_y"),
			TEXT("Scale Y applied to the actor, effectively controlling PCG volume extent (default: 1.0)"))
		.NumberArg(TEXT("scale_z"),
			TEXT("Scale Z applied to the actor, effectively controlling PCG volume extent (default: 1.0)"))
		.StringArg(TEXT("label"),
			TEXT("Optional actor label shown in the scene outliner"))
		.IntArg(TEXT("seed"),
			TEXT("Optional integer seed to set on the PCG component for deterministic generation"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World)
			{
				return FMCPToolResult::Error(TEXT("No editor world available"));
			}

			const FVector SpawnLocation(
				Args->HasField(TEXT("x")) ? Args->GetNumberField(TEXT("x")) : 0.0,
				Args->HasField(TEXT("y")) ? Args->GetNumberField(TEXT("y")) : 0.0,
				Args->HasField(TEXT("z")) ? Args->GetNumberField(TEXT("z")) : 0.0
			);

			const FVector SpawnScale(
				Args->HasField(TEXT("scale_x")) ? Args->GetNumberField(TEXT("scale_x")) : 1.0,
				Args->HasField(TEXT("scale_y")) ? Args->GetNumberField(TEXT("scale_y")) : 1.0,
				Args->HasField(TEXT("scale_z")) ? Args->GetNumberField(TEXT("scale_z")) : 1.0
			);

			// Load graph asset if a path was provided
			UPCGGraphInterface* PCGGraph = nullptr;
			FString GraphPath;
			if (Args->TryGetStringField(TEXT("graph_path"), GraphPath) && !GraphPath.IsEmpty())
			{
				PCGGraph = LoadObject<UPCGGraphInterface>(nullptr, *GraphPath);
				if (!IsValid(PCGGraph))
				{
					return FMCPToolResult::Error(FString::Printf(
						TEXT("Failed to load PCG Graph asset at path: %s"), *GraphPath));
				}
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Spawn PCG Actor")));

			// Spawn a plain AActor and attach the PCG component manually
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AActor* PCGActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnLocation, FRotator::ZeroRotator, SpawnParams);
			if (!IsValid(PCGActor))
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to spawn actor in the level"));
			}

			// Add the PCG component
			UPCGComponent* PCGComponent = NewObject<UPCGComponent>(PCGActor, UPCGComponent::StaticClass(),
				FName(TEXT("PCGComponent")), RF_Transactional);
			if (!IsValid(PCGComponent))
			{
				PCGActor->Destroy();
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to create UPCGComponent on the spawned actor"));
			}

			PCGActor->AddInstanceComponent(PCGComponent);
			PCGComponent->RegisterComponent();
			// Note: UPCGComponent is not a USceneComponent, so it cannot be the root.
			// The actor already has a default scene root from spawning.

			// Apply scale
			PCGActor->SetActorScale3D(SpawnScale);

			// Assign graph if loaded
			if (IsValid(PCGGraph))
			{
				PCGComponent->SetGraph(PCGGraph);
			}

			// Apply optional seed
			if (Args->HasField(TEXT("seed")))
			{
				PCGComponent->Seed = (int32)Args->GetNumberField(TEXT("seed"));
			}

			// Apply label
			FString Label;
			if (Args->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
			{
				PCGActor->SetActorLabel(Label);
			}

			const FString ActorLabel = PCGActor->GetActorLabel();
			const FString GraphName = IsValid(PCGGraph) ? PCGGraph->GetName() : TEXT("(none)");

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Spawned PCG actor '%s' at (%.1f, %.1f, %.1f) with scale (%.2f, %.2f, %.2f). Graph: %s. Use execute_pcg to generate."),
				*ActorLabel,
				SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z,
				SpawnScale.X, SpawnScale.Y, SpawnScale.Z,
				*GraphName));
		});

	// ================================================================
	// execute_pcg - Trigger PCG generation on an actor's PCG component
	// ================================================================
	MCP_TOOL(Registry, "execute_pcg")
		.LongRunning()
		.Description(TEXT(
			"Trigger PCG graph generation on the UPCGComponent attached to the named actor. "
			"The component's assigned PCG Graph will be executed immediately with a forced regeneration. "
			"Use spawn_pcg_actor to create a PCG actor first, then call this tool to run the graph."))
		.Idempotent()
		.StringArg(TEXT("actor_name"),
			TEXT("Label of the actor containing the UPCGComponent to execute"),
			/*bRequired=*/true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World)
			{
				return FMCPToolResult::Error(TEXT("No editor world available"));
			}

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
			{
				return FMCPToolResult::Error(TEXT("actor_name is required"));
			}

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!IsValid(Actor))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Actor not found: %s"), *ActorName));
			}

			UPCGComponent* PCGComponent = Actor->FindComponentByClass<UPCGComponent>();
			if (!IsValid(PCGComponent))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Actor '%s' has no UPCGComponent"), *ActorName));
			}

			// Check that a graph is assigned before trying to generate
			UPCGGraphInterface* AssignedGraph = PCGComponent->GetGraph();
			if (!IsValid(AssignedGraph))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("UPCGComponent on actor '%s' has no PCG Graph assigned. "
					     "Use spawn_pcg_actor with graph_path, or manually assign a graph first."),
					*ActorName));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Execute PCG")));
			PCGComponent->Modify();

			// Force a clean regeneration
			PCGComponent->CleanupLocalImmediate(/*bRemoveComponents=*/true);
			PCGComponent->GenerateLocal(/*bForce=*/true);

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Executed PCG generation on actor '%s'. Graph: '%s'. Seed: %d."),
				*ActorName,
				*AssignedGraph->GetName(),
				PCGComponent->Seed));
		});

	// ================================================================
	// get_pcg_info - Get info about the PCG component on an actor
	// ================================================================
	MCP_TOOL(Registry, "get_pcg_info")
		.Description(TEXT(
			"Retrieve information about the UPCGComponent attached to the named actor. "
			"Reports the assigned graph name and path, seed, generation trigger type, "
			"actor location and scale (which defines the effective generation bounds)."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("actor_name"),
			TEXT("Label of the actor to inspect for a UPCGComponent"),
			/*bRequired=*/true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World)
			{
				return FMCPToolResult::Error(TEXT("No editor world available"));
			}

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
			{
				return FMCPToolResult::Error(TEXT("actor_name is required"));
			}

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!IsValid(Actor))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Actor not found: %s"), *ActorName));
			}

			UPCGComponent* PCGComponent = Actor->FindComponentByClass<UPCGComponent>();
			if (!IsValid(PCGComponent))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Actor '%s' has no UPCGComponent"), *ActorName));
			}

			// Graph info
			UPCGGraphInterface* AssignedGraph = PCGComponent->GetGraph();
			const FString GraphName = IsValid(AssignedGraph) ? AssignedGraph->GetName() : TEXT("(none)");
			const FString GraphPath = IsValid(AssignedGraph) ? AssignedGraph->GetPathName() : TEXT("");

			// Generation trigger type as a readable string
			FString TriggerTypeStr;
			switch (PCGComponent->GenerationTrigger)
			{
				case EPCGComponentGenerationTrigger::GenerateOnLoad:
					TriggerTypeStr = TEXT("GenerateOnLoad");
					break;
				case EPCGComponentGenerationTrigger::GenerateOnDemand:
					TriggerTypeStr = TEXT("GenerateOnDemand");
					break;
				default:
					TriggerTypeStr = TEXT("Unknown");
					break;
			}

			// Actor transform
			const FVector ActorLocation = Actor->GetActorLocation();
			const FVector ActorScale = Actor->GetActorScale3D();

			// Build result JSON
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"), ActorName);

			TSharedPtr<FJsonObject> GraphInfo = MakeShared<FJsonObject>();
			GraphInfo->SetStringField(TEXT("name"), GraphName);
			GraphInfo->SetStringField(TEXT("path"), GraphPath);
			Result->SetObjectField(TEXT("graph"), GraphInfo);

			Result->SetNumberField(TEXT("seed"), PCGComponent->Seed);
			Result->SetStringField(TEXT("generation_trigger"), TriggerTypeStr);

			TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
			LocationObj->SetNumberField(TEXT("x"), ActorLocation.X);
			LocationObj->SetNumberField(TEXT("y"), ActorLocation.Y);
			LocationObj->SetNumberField(TEXT("z"), ActorLocation.Z);
			Result->SetObjectField(TEXT("location"), LocationObj);

			TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
			ScaleObj->SetNumberField(TEXT("x"), ActorScale.X);
			ScaleObj->SetNumberField(TEXT("y"), ActorScale.Y);
			ScaleObj->SetNumberField(TEXT("z"), ActorScale.Z);
			Result->SetObjectField(TEXT("scale"), ScaleObj);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// create_pcg_graph - Create a new PCG Graph asset in the content browser
	// ================================================================
	MCP_TOOL(Registry, "create_pcg_graph")
		.Description(TEXT(
			"Create a new UPCGGraph asset in the content browser at the specified path. "
			"The graph is saved immediately and registered with the asset registry. "
			"Use add_pcg_node to populate the graph with nodes after creation."))
		.Idempotent()
		.StringArg(TEXT("asset_path"),
			TEXT("Content path for the new PCG Graph asset (e.g., '/Game/PCG/MyGraph'). "
			     "Do not include a file extension."),
			/*bRequired=*/true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
			{
				return FMCPToolResult::Error(TEXT("asset_path is required"));
			}

			// Derive package name and asset name from the provided path.
			// Strip trailing asset reference if present (e.g., "/Game/PCG/MyGraph.MyGraph").
			int32 DotIndex;
			if (AssetPath.FindChar(TEXT('.'), DotIndex))
			{
				AssetPath = AssetPath.Left(DotIndex);
			}

			const FString PackageName = AssetPath;
			const FString AssetName = FPackageName::GetShortName(PackageName);

			// Check whether the package already exists to avoid stomping existing assets.
			if (FindPackage(nullptr, *PackageName) != nullptr ||
			    FPackageName::DoesPackageExist(PackageName))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Asset already exists at path: %s"), *PackageName));
			}

			UPackage* Package = CreatePackage(*PackageName);
			if (!IsValid(Package))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to create package for path: %s"), *PackageName));
			}

			Package->FullyLoad();

			UPCGGraph* NewGraph = NewObject<UPCGGraph>(
				Package,
				*AssetName,
				RF_Public | RF_Standalone | RF_Transactional);

			if (!IsValid(NewGraph))
			{
				return FMCPToolResult::Error(TEXT("Failed to create UPCGGraph object"));
			}

			// Notify the asset registry so the graph appears in the content browser.
			FAssetRegistryModule& AssetRegistryModule =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			AssetRegistryModule.AssetCreated(NewGraph);
			// Journal the creation so run_tool_script can report truthfully that this
			// asset survives a rollback (UE package creation is not transactional).
			MCPCommon::NoteAssetCreated(PackageName);

			// Mark dirty and save the package to disk.
			Package->MarkPackageDirty();

			FString PackageFilename;
			if (FPackageName::TryConvertLongPackageNameToFilename(
				PackageName, PackageFilename, FPackageName::GetAssetPackageExtension()))
			{
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
				SaveArgs.Error = GError;
				SaveArgs.bForceByteSwapping = false;
				SaveArgs.bWarnOfLongFilename = false;
				SaveArgs.SaveFlags = SAVE_NoError;

				const bool bSaved = UPackage::SavePackage(Package, NewGraph, *PackageFilename, SaveArgs);

				if (!bSaved)
				{
					return FMCPToolResult::Error(FString::Printf(
						TEXT("PCG Graph object created but failed to save package to disk: %s"),
						*PackageFilename));
				}
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created PCG Graph asset '%s' at path '%s'. Use add_pcg_node to add nodes."),
				*AssetName, *PackageName));
		});

	// ================================================================
	// get_pcg_graph_nodes - List all nodes in a PCG Graph asset
	// ================================================================
	MCP_TOOL(Registry, "get_pcg_graph_nodes")
		.Description(TEXT(
			"List all nodes contained in a PCG Graph asset. "
			"Returns the node index, display title, settings class name, and editor position "
			"for each node. The node index can be used with add_pcg_node and connect_pcg_nodes."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("graph_path"),
			TEXT("Content path of the PCG Graph asset (e.g., '/Game/PCG/MyGraph.MyGraph')"),
			/*bRequired=*/true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString GraphPath;
			if (!Args->TryGetStringField(TEXT("graph_path"), GraphPath))
			{
				return FMCPToolResult::Error(TEXT("graph_path is required"));
			}

			UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
			if (!IsValid(Graph))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to load PCG Graph asset at path: %s"), *GraphPath));
			}

			TArray<TSharedPtr<FJsonValue>> NodeArray;
			const TArray<UPCGNode*>& Nodes = Graph->GetNodes();

			for (int32 i = 0; i < Nodes.Num(); ++i)
			{
				UPCGNode* Node = Nodes[i];
				if (!IsValid(Node)) continue;

				TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
				NodeObj->SetNumberField(TEXT("node_index"), i);

				// Node title / name (GetNodeTitle requires EPCGNodeTitleType in UE5.7)
				const FString NodeName = Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString();
				NodeObj->SetStringField(TEXT("node_title"), NodeName.IsEmpty() ? Node->GetName() : NodeName);

				// Settings class
				UPCGSettings* Settings = Node->GetSettings();
				if (IsValid(Settings))
				{
					NodeObj->SetStringField(TEXT("settings_class"),
						Settings->GetClass()->GetName());
				}
				else
				{
					NodeObj->SetStringField(TEXT("settings_class"), TEXT("(none)"));
				}

				// Editor position via reflection (NodePosX / NodePosY are UPROPERTY fields on UPCGNode)
				int32 PosX = 0;
				int32 PosY = 0;
				if (FIntProperty* PropX = FindFProperty<FIntProperty>(UPCGNode::StaticClass(), TEXT("PositionX")))
				{
					PosX = PropX->GetPropertyValue_InContainer(Node);
				}
				if (FIntProperty* PropY = FindFProperty<FIntProperty>(UPCGNode::StaticClass(), TEXT("PositionY")))
				{
					PosY = PropY->GetPropertyValue_InContainer(Node);
				}
				NodeObj->SetNumberField(TEXT("position_x"), PosX);
				NodeObj->SetNumberField(TEXT("position_y"), PosY);

				// Input / output pin counts
				NodeObj->SetNumberField(TEXT("input_pins"),  Node->GetInputPins().Num());
				NodeObj->SetNumberField(TEXT("output_pins"), Node->GetOutputPins().Num());

				NodeArray.Add(MakeShared<FJsonValueObject>(NodeObj));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("graph"), Graph->GetName());
			Result->SetStringField(TEXT("graph_path"), GraphPath);
			Result->SetNumberField(TEXT("node_count"), NodeArray.Num());
			Result->SetArrayField(TEXT("nodes"), NodeArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_pcg_node - Add a node to a PCG Graph
	// ================================================================
	MCP_TOOL(Registry, "add_pcg_node")
		.Description(TEXT(
			"Add a new node to a PCG Graph by specifying the UPCGSettings subclass to use. "
			"The node is appended to the graph and the package is marked dirty. "
			"Use get_pcg_graph_nodes to retrieve node indices for subsequent connect_pcg_nodes calls."))
		.StringArg(TEXT("graph_path"),
			TEXT("Content path of the PCG Graph asset (e.g., '/Game/PCG/MyGraph.MyGraph')"),
			/*bRequired=*/true)
		.StringArg(TEXT("settings_class"),
			TEXT("Name of the UPCGSettings subclass to instantiate. "
			     "Common values: 'PCGSurfaceSamplerSettings', 'PCGStaticMeshSpawnerSettings', "
			     "'PCGDensityFilterSettings', 'PCGPointFilterSettings', 'PCGSelfPruningSettings'. "
			     "The 'U' prefix is optional."),
			/*bRequired=*/true)
		.IntArg(TEXT("node_x"),
			TEXT("Horizontal position of the node in the graph editor (default: 0)"))
		.IntArg(TEXT("node_y"),
			TEXT("Vertical position of the node in the graph editor (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString GraphPath;
			if (!Args->TryGetStringField(TEXT("graph_path"), GraphPath))
			{
				return FMCPToolResult::Error(TEXT("graph_path is required"));
			}

			FString SettingsClassName;
			if (!Args->TryGetStringField(TEXT("settings_class"), SettingsClassName))
			{
				return FMCPToolResult::Error(TEXT("settings_class is required"));
			}

			UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
			if (!IsValid(Graph))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to load PCG Graph asset at path: %s"), *GraphPath));
			}

			// Resolve the settings class — try as-is, then with 'U' prefix.
			UClass* SettingsClass = FindFirstObject<UClass>(*SettingsClassName,
			                            EFindFirstObjectOptions::ExactClass);
			if (!SettingsClass)
			{
				SettingsClass = FindFirstObject<UClass>(
					*FString::Printf(TEXT("U%s"), *SettingsClassName),
					EFindFirstObjectOptions::ExactClass);
			}

			if (!SettingsClass)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Could not find UPCGSettings subclass named '%s'. "
					     "Verify the class name is correct (e.g., 'PCGSurfaceSamplerSettings')."),
					*SettingsClassName));
			}

			if (!SettingsClass->IsChildOf(UPCGSettings::StaticClass()))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Class '%s' is not a subclass of UPCGSettings."), *SettingsClassName));
			}

			const int32 NodeX = Args->HasField(TEXT("node_x"))
				? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			const int32 NodeY = Args->HasField(TEXT("node_y"))
				? (int32)Args->GetNumberField(TEXT("node_y")) : 0;

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add PCG Node")));
			Graph->Modify();

			// AddNodeOfType takes a TSubclassOf<UPCGSettings> and outputs the default settings instance
			UPCGSettings* DefaultSettings = nullptr;
			UPCGNode* NewNode = Graph->AddNodeOfType(SettingsClass, DefaultSettings);
			if (!IsValid(NewNode))
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(
					TEXT("UPCGGraph::AddNodeOfType failed for settings class '%s'."),
					*SettingsClassName));
			}

			// Set editor position via reflection.
			if (FIntProperty* PropX = FindFProperty<FIntProperty>(UPCGNode::StaticClass(), TEXT("PositionX")))
			{
				PropX->SetPropertyValue_InContainer(NewNode, NodeX);
			}
			if (FIntProperty* PropY = FindFProperty<FIntProperty>(UPCGNode::StaticClass(), TEXT("PositionY")))
			{
				PropY->SetPropertyValue_InContainer(NewNode, NodeY);
			}

			Graph->MarkPackageDirty();
			GEditor->EndTransaction();

			// Determine the new node's index.
			const TArray<UPCGNode*>& AllNodes = Graph->GetNodes();
			int32 NewNodeIndex = AllNodes.IndexOfByKey(NewNode);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added PCG node with settings class '%s' to graph '%s'. "
				     "New node index: %d. Position: (%d, %d)."),
				*SettingsClass->GetName(),
				*Graph->GetName(),
				NewNodeIndex,
				NodeX, NodeY));
		});

	// ================================================================
	// connect_pcg_nodes - Connect two nodes in a PCG Graph
	// ================================================================
	MCP_TOOL(Registry, "connect_pcg_nodes")
		.Description(TEXT(
			"Connect an output pin of one PCG node to an input pin of another PCG node in the same graph. "
			"Node indices correspond to the array order returned by get_pcg_graph_nodes. "
			"Default pin names 'Out' and 'In' are used when source_pin_label / target_pin_label are omitted. "
			"The graph package is marked dirty after a successful connection."))
		.StringArg(TEXT("graph_path"),
			TEXT("Content path of the PCG Graph asset (e.g., '/Game/PCG/MyGraph.MyGraph')"),
			/*bRequired=*/true)
		.IntArg(TEXT("source_node_index"),
			TEXT("Zero-based index of the source (output) node as returned by get_pcg_graph_nodes"),
			/*bRequired=*/true)
		.StringArg(TEXT("source_pin_label"),
			TEXT("Name of the output pin on the source node (default: 'Out')"))
		.IntArg(TEXT("target_node_index"),
			TEXT("Zero-based index of the target (input) node as returned by get_pcg_graph_nodes"),
			/*bRequired=*/true)
		.StringArg(TEXT("target_pin_label"),
			TEXT("Name of the input pin on the target node (default: 'In')"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString GraphPath;
			if (!Args->TryGetStringField(TEXT("graph_path"), GraphPath))
			{
				return FMCPToolResult::Error(TEXT("graph_path is required"));
			}

			if (!Args->HasField(TEXT("source_node_index")))
			{
				return FMCPToolResult::Error(TEXT("source_node_index is required"));
			}
			if (!Args->HasField(TEXT("target_node_index")))
			{
				return FMCPToolResult::Error(TEXT("target_node_index is required"));
			}

			const int32 SourceIdx = (int32)Args->GetNumberField(TEXT("source_node_index"));
			const int32 TargetIdx = (int32)Args->GetNumberField(TEXT("target_node_index"));

			FString SourcePinLabel = TEXT("Out");
			Args->TryGetStringField(TEXT("source_pin_label"), SourcePinLabel);

			FString TargetPinLabel = TEXT("In");
			Args->TryGetStringField(TEXT("target_pin_label"), TargetPinLabel);

			UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
			if (!IsValid(Graph))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to load PCG Graph asset at path: %s"), *GraphPath));
			}

			const TArray<UPCGNode*>& Nodes = Graph->GetNodes();

			if (SourceIdx < 0 || SourceIdx >= Nodes.Num())
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("source_node_index %d is out of range (graph has %d nodes)."),
					SourceIdx, Nodes.Num()));
			}
			if (TargetIdx < 0 || TargetIdx >= Nodes.Num())
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("target_node_index %d is out of range (graph has %d nodes)."),
					TargetIdx, Nodes.Num()));
			}

			UPCGNode* SourceNode = Nodes[SourceIdx];
			UPCGNode* TargetNode = Nodes[TargetIdx];

			if (!IsValid(SourceNode))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Source node at index %d is null or invalid."), SourceIdx));
			}
			if (!IsValid(TargetNode))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Target node at index %d is null or invalid."), TargetIdx));
			}

			// Locate the output pin on the source node.
			UPCGPin* SourcePin = nullptr;
			for (UPCGPin* Pin : SourceNode->GetOutputPins())
			{
				if (IsValid(Pin) && Pin->Properties.Label.ToString() == SourcePinLabel)
				{
					SourcePin = Pin;
					break;
				}
			}
			if (!SourcePin && SourceNode->GetOutputPins().Num() > 0)
			{
				// Fall back to first available output pin when label was not matched.
				SourcePin = SourceNode->GetOutputPins()[0];
			}

			if (!SourcePin)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("No output pin named '%s' found on source node %d ('%s'). "
					     "Check available pins with get_pcg_graph_nodes."),
					*SourcePinLabel, SourceIdx,
					IsValid(SourceNode->GetSettings())
						? *SourceNode->GetSettings()->GetClass()->GetName()
						: *SourceNode->GetName()));
			}

			// Locate the input pin on the target node.
			UPCGPin* TargetPin = nullptr;
			for (UPCGPin* Pin : TargetNode->GetInputPins())
			{
				if (IsValid(Pin) && Pin->Properties.Label.ToString() == TargetPinLabel)
				{
					TargetPin = Pin;
					break;
				}
			}
			if (!TargetPin && TargetNode->GetInputPins().Num() > 0)
			{
				// Fall back to first available input pin when label was not matched.
				TargetPin = TargetNode->GetInputPins()[0];
			}

			if (!TargetPin)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("No input pin named '%s' found on target node %d ('%s'). "
					     "Check available pins with get_pcg_graph_nodes."),
					*TargetPinLabel, TargetIdx,
					IsValid(TargetNode->GetSettings())
						? *TargetNode->GetSettings()->GetClass()->GetName()
						: *TargetNode->GetName()));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Connect PCG Nodes")));
			Graph->Modify();

			const bool bConnected = SourcePin->AddEdgeTo(TargetPin);

			if (!bConnected)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(
					TEXT("UPCGPin::AddEdgeTo failed connecting node %d pin '%s' -> node %d pin '%s'. "
					     "Pins may be incompatible or already connected."),
					SourceIdx, *SourcePinLabel,
					TargetIdx, *TargetPinLabel));
			}

			Graph->MarkPackageDirty();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Connected PCG node %d (pin '%s') -> node %d (pin '%s') in graph '%s'."),
				SourceIdx, *SourcePinLabel,
				TargetIdx, *TargetPinLabel,
				*Graph->GetName()));
		});

		// ================================================================
	// set_pcg_static_mesh_spawner_meshes - Set mesh entries on spawner
	// ================================================================
	MCP_TOOL(Registry, "set_pcg_static_mesh_spawner_meshes")
		.Description(TEXT(
			"Assign one or more static meshes to a PCG Static Mesh Spawner node using the weighted mesh selector. "
			"Each mesh is added with equal weight. This is useful for completing PCG graphs created via MCP."))
		.StringArg(TEXT("graph_path"),
			TEXT("Content path of the PCG Graph asset (e.g., '/Game/PCG/MyGraph.MyGraph')"),
			/*bRequired=*/true)
		.IntArg(TEXT("node_index"),
			TEXT("Zero-based node index of the Static Mesh Spawner node in the graph."),
			/*bRequired=*/true)
		.StringArrayArg(TEXT("mesh_paths"),
			TEXT("Array of static mesh asset paths to assign to the spawner."),
			/*bRequired=*/true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString GraphPath;
			if (!Args->TryGetStringField(TEXT("graph_path"), GraphPath))
			{
				return FMCPToolResult::Error(TEXT("graph_path is required"));
			}

			if (!Args->HasField(TEXT("node_index")))
			{
				return FMCPToolResult::Error(TEXT("node_index is required"));
			}
			const int32 NodeIndex = (int32)Args->GetNumberField(TEXT("node_index"));

			const TArray<TSharedPtr<FJsonValue>>* MeshPathsJson = nullptr;
			if (!Args->TryGetArrayField(TEXT("mesh_paths"), MeshPathsJson) || !MeshPathsJson)
			{
				return FMCPToolResult::Error(TEXT("mesh_paths is required"));
			}

			UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
			if (!IsValid(Graph))
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to load PCG Graph asset at path: %s"), *GraphPath));
			}

			const TArray<UPCGNode*>& Nodes = Graph->GetNodes();
			if (NodeIndex < 0 || NodeIndex >= Nodes.Num())
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("node_index %d is out of range (graph has %d nodes)."),
					NodeIndex, Nodes.Num()));
			}

			UPCGNode* Node = Nodes[NodeIndex];
			if (!IsValid(Node) || !IsValid(Node->GetSettings()))
			{
				return FMCPToolResult::Error(TEXT("Target PCG node or its settings are invalid."));
			}

			UPCGStaticMeshSpawnerSettings* SpawnerSettings =
				Cast<UPCGStaticMeshSpawnerSettings>(Node->GetSettings());
			if (!SpawnerSettings)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Node %d is not a UPCGStaticMeshSpawnerSettings node (actual: %s)."),
					NodeIndex, *Node->GetSettings()->GetClass()->GetName()));
			}

			TArray<FPCGMeshSelectorWeightedEntry> Entries;
			TArray<FString> LoadedMeshes;
			for (const TSharedPtr<FJsonValue>& Value : *MeshPathsJson)
			{
				const FString MeshPath = Value.IsValid() ? Value->AsString() : FString();
				if (MeshPath.IsEmpty())
				{
					continue;
				}

				UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
				if (!IsValid(Mesh))
				{
					return FMCPToolResult::Error(FString::Printf(
						TEXT("Failed to load static mesh at path: %s"), *MeshPath));
				}

				FPCGMeshSelectorWeightedEntry Entry;
				Entry.Descriptor.StaticMesh = Mesh;
				Entry.Weight = 1;
				Entries.Add(Entry);
				LoadedMeshes.Add(Mesh->GetName());
			}

			if (Entries.IsEmpty())
			{
				return FMCPToolResult::Error(TEXT("mesh_paths did not contain any valid mesh paths."));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set PCG Spawner Meshes")));
			Graph->Modify();
			Node->Modify();
			SpawnerSettings->Modify();

			SpawnerSettings->SetMeshSelectorType(UPCGMeshSelectorWeighted::StaticClass());
			UPCGMeshSelectorWeighted* WeightedSelector =
				Cast<UPCGMeshSelectorWeighted>(SpawnerSettings->MeshSelectorParameters);
			if (!WeightedSelector)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to initialize weighted mesh selector on spawner settings."));
			}

			WeightedSelector->Modify();
			WeightedSelector->MeshEntries = Entries;
			SpawnerSettings->bSynchronousLoad = true;

			Graph->MarkPackageDirty();
			GEditor->EndTransaction();

			const FString MeshList = FString::Join(LoadedMeshes, TEXT(", "));
			return FMCPToolResult::Success(FString::Printf(
				TEXT("Assigned %d mesh(es) to PCG Static Mesh Spawner node %d in graph '%s': %s"),
				Entries.Num(),
				NodeIndex,
				*Graph->GetName(),
				*MeshList));
		});
	// ------------------------------------------------------------------
	// run_pcg_generation (v5 increment 26, V5-25): cancellable generation as an owned operation.
	// ------------------------------------------------------------------
	MCP_TOOL(Registry, "run_pcg_generation")
		.Description(TEXT("Generate a PCG component as an owned operation instead of a blocking call. Finds the actor's PCGComponent, starts GenerateLocal(force) and polls IsGenerating on the editor ticker; poll with get_editor_operation, cancel with cancel_editor_operation (the component's CancelGeneration is called and pcg_cancellation reports requested). The result carries generated (component flag), last_generated_bounds, generated_output_count (data collection entries) and dirty (package dirty flag). Requires Scene scope; PIE must be stopped."))
		.RequiresPieOff()
		.StringArg(TEXT("actor_path"), TEXT("Exact loaded actor path with a PCGComponent"), true)
		.BoolArg(TEXT("force"), TEXT("Force regeneration even when already generated (default true)"))
		.IntArg(TEXT("deadline_seconds"), TEXT("Operation deadline 1..600 (default 300)"))
		.HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context) -> FMCPToolResult
		{
			if (!IsInGameThread()) return FMCPToolResult::ErrorStructured(EMCPError::RequiresGameThread, TEXT("PCG operations require the game thread"));
			if (!Context.HasScope(EMCPScope::Scene)) return FMCPToolResult::ErrorStructured(EMCPError::ScopeDenied, TEXT("Scene scope required"));
			AActor* Actor = FindObject<AActor>(nullptr, *Args->GetStringField(TEXT("actor_path")));
			if (!Actor || !IsValid(Actor)) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("Actor not found: ") + Args->GetStringField(TEXT("actor_path")));
			UPCGComponent* Comp = Actor->FindComponentByClass<UPCGComponent>();
			if (!Comp) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("Actor has no PCGComponent"));
			if (!Comp->GetGraph()) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("PCGComponent has no graph assigned"));
			const bool bForce = !Args->HasField(TEXT("force")) || Args->GetBoolField(TEXT("force"));
			const double Deadline = Args->HasField(TEXT("deadline_seconds")) ? Args->GetNumberField(TEXT("deadline_seconds")) : 300.0;
			TWeakObjectPtr<UPCGComponent> Weak = Comp;
			struct FState { bool bStarted = false; FString Cancellation = TEXT("not_requested"); };
			auto St = MakeShared<FState>();
			auto Details = MakeShared<FJsonObject>(); Details->SetStringField(TEXT("actor_path"), Actor->GetPathName()); Details->SetStringField(TEXT("graph"), Comp->GetGraph()->GetPathName()); Details->SetBoolField(TEXT("force"), bForce);
			FString Err;
			const FString Id = MCPScenarios::StartDrivenOperation(Context, TEXT("pcg_generation"), TEXT("PCG ") + Actor->GetActorLabel(), Deadline,
				[Weak, St, bForce](FMCPOperationTick& T)
				{
					UPCGComponent* C = Weak.Get();
					auto Result = [&]() { auto R = MakeShared<FJsonObject>(); R->SetStringField(TEXT("pcg_cancellation"), St->Cancellation); if (C) { R->SetBoolField(TEXT("generated"), C->bGenerated); R->SetBoolField(TEXT("is_generating"), C->IsGenerating()); const FBox B = C->GetLastGeneratedBounds(); auto BJ = MakeShared<FJsonObject>(); BJ->SetBoolField(TEXT("valid"), B.IsValid != 0); BJ->SetStringField(TEXT("min"), B.Min.ToString()); BJ->SetStringField(TEXT("max"), B.Max.ToString()); R->SetObjectField(TEXT("last_generated_bounds"), BJ); R->SetNumberField(TEXT("generated_output_count"), C->GetGeneratedGraphOutput().TaggedData.Num()); R->SetBoolField(TEXT("dirty"), C->GetOutermost() && C->GetOutermost()->IsDirty()); } return R; };
					if (!C || !IsValid(C)) { T.Finish(TEXT("failed"), TEXT("PCGComponent destroyed during generation"), Result()); return; }
					if (T.bCancelRequested) { C->CancelGeneration(); St->Cancellation = TEXT("requested"); T.Event(TEXT("cancel"), TEXT("CancelGeneration called on the component")); T.Finish(TEXT("cancelled"), TEXT("Cancelled by owner"), Result()); return; }
					if (!St->bStarted) { St->bStarted = true; C->GenerateLocal(bForce); T.Event(TEXT("generate_requested"), bForce ? TEXT("GenerateLocal(force)") : TEXT("GenerateLocal")); T.Progress(0.1, TEXT("generation requested")); return; }
					if (C->IsGenerating()) { T.Progress(0.5, TEXT("generating")); return; }
					T.Progress(1.0, TEXT("generation finished"));
					T.Finish(C->bGenerated ? TEXT("succeeded") : TEXT("failed"), C->bGenerated ? FString() : TEXT("Component reports not generated after the task finished"), Result());
				},
				[Weak, St](const FString&) { if (UPCGComponent* C = Weak.Get()) { C->CancelGeneration(); St->Cancellation = TEXT("requested"); } },
				Details, Err);
			if (Id.IsEmpty()) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, Err);
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("PCG generation queued as %s; poll get_editor_operation."), *Id), MCPScenarios::DescribeOperation(Id, Context, false));
		});

}

} // namespace MCPPCGTools
