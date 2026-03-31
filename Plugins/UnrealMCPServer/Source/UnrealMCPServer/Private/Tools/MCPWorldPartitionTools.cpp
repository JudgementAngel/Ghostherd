#include "Tools/MCPWorldPartitionTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// World Partition core
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

// Data Layer support
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"

namespace MCPWorldPartitionTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// get_world_partition_info - Query World Partition configuration
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_world_partition_info");
		Def.Description = TEXT("Get World Partition configuration and status for the current level. Reports whether World Partition is enabled, data layers, and world bounds. Works with both World Partition and non-World Partition levels.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World)
			{
				return FMCPToolResult::Error(TEXT("No editor world available"));
			}

			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();

			// Basic world info always reported
			Output->SetStringField(TEXT("world_name"), World->GetName());
			Output->SetStringField(TEXT("map_name"), World->GetMapName());

			// Package path
			if (World->GetOutermost())
			{
				Output->SetStringField(TEXT("package_path"), World->GetOutermost()->GetName());
			}

			// ----------------------------------------------------------
			// World Partition presence check
			// ----------------------------------------------------------
			UWorldPartition* WorldPartition = World->GetWorldPartition();

			if (!WorldPartition)
			{
				// No World Partition - report basic level info instead
				Output->SetBoolField(TEXT("is_world_partition_enabled"), false);
				Output->SetStringField(TEXT("info"), TEXT("This level does not use World Partition. Use standard level streaming or enable World Partition in World Settings."));

				// Still report streaming levels for context
				TArray<TSharedPtr<FJsonValue>> StreamingLevels;
				for (ULevelStreaming* Streaming : World->GetStreamingLevels())
				{
					if (!Streaming)
					{
						continue;
					}
					TSharedPtr<FJsonObject> LevelObj = MakeShared<FJsonObject>();
					LevelObj->SetStringField(TEXT("name"), Streaming->GetWorldAssetPackageName());
					LevelObj->SetBoolField(TEXT("is_loaded"), Streaming->IsLevelLoaded());
					LevelObj->SetBoolField(TEXT("is_visible"), Streaming->GetShouldBeVisibleInEditor());
					StreamingLevels.Add(MakeShared<FJsonValueObject>(LevelObj));
				}
				Output->SetArrayField(TEXT("streaming_levels"), StreamingLevels);
				Output->SetNumberField(TEXT("streaming_level_count"), StreamingLevels.Num());

				return FMCPToolResult::Success(JsonToString(Output));
			}

			// ----------------------------------------------------------
			// World Partition is active - report full details
			// ----------------------------------------------------------
			Output->SetBoolField(TEXT("is_world_partition_enabled"), true);

			// Streaming completed state via subsystem
			UWorldPartitionSubsystem* WPSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
			if (WPSubsystem)
			{
				Output->SetBoolField(TEXT("is_streaming_completed"), WPSubsystem->IsStreamingCompleted());
			}
			else
			{
				Output->SetBoolField(TEXT("is_streaming_completed"), false);
			}

			// ----------------------------------------------------------
			// Data Layers
			// ----------------------------------------------------------
			UDataLayerManager* DataLayerManager = WorldPartition->GetDataLayerManager();
			if (DataLayerManager)
			{
				TArray<TSharedPtr<FJsonValue>> DataLayerArray;

				DataLayerManager->ForEachDataLayerInstance([&DataLayerArray](UDataLayerInstance* Instance) -> bool
				{
					if (!Instance)
					{
						return true; // Continue iteration
					}

					TSharedPtr<FJsonObject> DLObj = MakeShared<FJsonObject>();
					DLObj->SetStringField(TEXT("name"), Instance->GetDataLayerShortName());
					DLObj->SetStringField(TEXT("full_name"), Instance->GetDataLayerFullName());

					// Runtime state as string
					EDataLayerRuntimeState RuntimeState = Instance->GetRuntimeState();
					FString StateStr;
					switch (RuntimeState)
					{
						case EDataLayerRuntimeState::Unloaded:  StateStr = TEXT("Unloaded");  break;
						case EDataLayerRuntimeState::Loaded:    StateStr = TEXT("Loaded");    break;
						case EDataLayerRuntimeState::Activated: StateStr = TEXT("Activated"); break;
						default:                                StateStr = TEXT("Unknown");   break;
					}
					DLObj->SetStringField(TEXT("runtime_state"), StateStr);

					// Initial runtime state
					EDataLayerRuntimeState InitialState = Instance->GetInitialRuntimeState();
					FString InitStateStr;
					switch (InitialState)
					{
						case EDataLayerRuntimeState::Unloaded:  InitStateStr = TEXT("Unloaded");  break;
						case EDataLayerRuntimeState::Loaded:    InitStateStr = TEXT("Loaded");    break;
						case EDataLayerRuntimeState::Activated: InitStateStr = TEXT("Activated"); break;
						default:                                InitStateStr = TEXT("Unknown");   break;
					}
					DLObj->SetStringField(TEXT("initial_runtime_state"), InitStateStr);

					DLObj->SetBoolField(TEXT("is_runtime_relevant"), Instance->IsRuntime());
					DLObj->SetBoolField(TEXT("is_visible_in_editor"), Instance->IsEffectiveLoadedInEditor());

					DataLayerArray.Add(MakeShared<FJsonValueObject>(DLObj));
					return true; // Continue iteration
				});

				Output->SetArrayField(TEXT("data_layers"), DataLayerArray);
				Output->SetNumberField(TEXT("data_layer_count"), DataLayerArray.Num());
			}
			else
			{
				Output->SetArrayField(TEXT("data_layers"), TArray<TSharedPtr<FJsonValue>>());
				Output->SetNumberField(TEXT("data_layer_count"), 0);
			}

			// ----------------------------------------------------------
			// World Bounds (derived from iterating actors)
			// ----------------------------------------------------------
			FBox WorldBounds(ForceInit);
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor))
				{
					continue;
				}
				FVector Origin, Extent;
				Actor->GetActorBounds(false, Origin, Extent);
				if (Extent.SizeSquared() > 0.0f)
				{
					WorldBounds += FBox(Origin - Extent, Origin + Extent);
				}
			}

			if (WorldBounds.IsValid)
			{
				TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
				BoundsObj->SetNumberField(TEXT("min_x"), WorldBounds.Min.X);
				BoundsObj->SetNumberField(TEXT("min_y"), WorldBounds.Min.Y);
				BoundsObj->SetNumberField(TEXT("min_z"), WorldBounds.Min.Z);
				BoundsObj->SetNumberField(TEXT("max_x"), WorldBounds.Max.X);
				BoundsObj->SetNumberField(TEXT("max_y"), WorldBounds.Max.Y);
				BoundsObj->SetNumberField(TEXT("max_z"), WorldBounds.Max.Z);

				FVector Size = WorldBounds.GetSize();
				BoundsObj->SetNumberField(TEXT("size_x"), Size.X);
				BoundsObj->SetNumberField(TEXT("size_y"), Size.Y);
				BoundsObj->SetNumberField(TEXT("size_z"), Size.Z);

				Output->SetObjectField(TEXT("world_bounds"), BoundsObj);
			}

			return FMCPToolResult::Success(JsonToString(Output));
		});
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// load_world_partition_region - Load editor cells by bounding box
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("min_x"), TEXT("Minimum X coordinate of the region to load (Unreal units / cm)"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("min_y"), TEXT("Minimum Y coordinate of the region to load (Unreal units / cm)"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("min_z"), TEXT("Minimum Z coordinate of the region to load (Unreal units / cm)"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("max_x"), TEXT("Maximum X coordinate of the region to load (Unreal units / cm)"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("max_y"), TEXT("Maximum Y coordinate of the region to load (Unreal units / cm)"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("max_z"), TEXT("Maximum Z coordinate of the region to load (Unreal units / cm)"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("load_world_partition_region");
		Def.Description = TEXT("Load World Partition editor cells within a bounding box region. Uses the console command 'wp.Editor.LoadRegion' to trigger cell loading. Requires World Partition to be enabled on the current level. Coordinates are in Unreal units (cm).");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World)
			{
				return FMCPToolResult::Error(TEXT("No editor world available"));
			}

			UWorldPartition* WorldPartition = World->GetWorldPartition();
			if (!WorldPartition)
			{
				return FMCPToolResult::Error(TEXT("World Partition is not enabled on the current level. Enable it in World Settings > World Partition."));
			}

			// Parse bounds from args
			double MinX = Args->GetNumberField(TEXT("min_x"));
			double MinY = Args->GetNumberField(TEXT("min_y"));
			double MinZ = Args->GetNumberField(TEXT("min_z"));
			double MaxX = Args->GetNumberField(TEXT("max_x"));
			double MaxY = Args->GetNumberField(TEXT("max_y"));
			double MaxZ = Args->GetNumberField(TEXT("max_z"));

			// Validate bounds ordering
			if (MinX >= MaxX || MinY >= MaxY || MinZ >= MaxZ)
			{
				return FMCPToolResult::Error(TEXT("Invalid bounds: min values must be less than max values for all axes."));
			}

			FBox Region(FVector(MinX, MinY, MinZ), FVector(MaxX, MaxY, MaxZ));

			// Use the console command approach which is stable across UE5 versions.
			// wp.Editor.LoadRegion MinX MinY MinZ MaxX MaxY MaxZ
			FString Cmd = FString::Printf(
				TEXT("wp.Editor.LoadRegion %f %f %f %f %f %f"),
				MinX, MinY, MinZ, MaxX, MaxY, MaxZ);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Load World Partition Region")));
			GEditor->Exec(World, *Cmd, *GLog);
			GEditor->EndTransaction();

			// Build result JSON
			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();
			Output->SetBoolField(TEXT("success"), true);

			TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
			BoundsObj->SetNumberField(TEXT("min_x"), MinX);
			BoundsObj->SetNumberField(TEXT("min_y"), MinY);
			BoundsObj->SetNumberField(TEXT("min_z"), MinZ);
			BoundsObj->SetNumberField(TEXT("max_x"), MaxX);
			BoundsObj->SetNumberField(TEXT("max_y"), MaxY);
			BoundsObj->SetNumberField(TEXT("max_z"), MaxZ);
			Output->SetObjectField(TEXT("region"), BoundsObj);

			FVector Size = Region.GetSize();
			Output->SetStringField(TEXT("message"), FString::Printf(
				TEXT("Requested editor cell load for region (%.0f x %.0f x %.0f cm). Cells overlapping this region will be loaded asynchronously by the editor."),
				Size.X, Size.Y, Size.Z
			));

			Output->SetStringField(TEXT("note"), TEXT("Cell loading is asynchronous. Use get_world_partition_info to check streaming completion status."));

			return FMCPToolResult::Success(JsonToString(Output));
		});
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

} // void RegisterAll

} // namespace MCPWorldPartitionTools
