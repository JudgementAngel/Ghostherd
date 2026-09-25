// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/Skeleton.h"
#include "AnimationStateMachineGraph.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/AimOffsetBlendSpaceFactoryNew.h"
#include "Factories/AnimMontageFactory.h"
#include "AnimationStateMachineSchema.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationGraph.h"
#include "AnimGraphNode_Root.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Tools/AnimGraph/AnimGraphCommon.h"
#include "Common/MCPAssetCreate.h"

namespace MCPAnimGraphTools::Lifecycle
{

using namespace MCPAnimGraphTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	MCP_TOOL(Registry, "create_anim_blueprint")
		.Description(TEXT(
			"Create a new Animation Blueprint (UAnimBlueprint) asset targeting a specific skeleton. "
			"Optionally specify a parent class derived from UAnimInstance. "
			"The created asset is saved to disk immediately and ready for graph editing."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new AnimBlueprint asset (e.g., '/Game/Characters/ABP_Hero')"), true)
		.StringArg(TEXT("skeleton_path"), TEXT("Content path to the USkeleton asset to target (e.g., '/Game/Characters/SK_Hero_Skeleton')"), true)
		.StringArg(TEXT("parent_class"), TEXT("Short class name of the parent AnimInstance class (default: 'AnimInstance')"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, SkeletonPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
				return FMCPToolResult::Error(TEXT("skeleton_path is required"));

			FString ParentClassName = TEXT("AnimInstance");
			Args->TryGetStringField(TEXT("parent_class"), ParentClassName);

			// Load the skeleton
			USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
			if (!IsValid(Skeleton))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load Skeleton at path: %s"), *SkeletonPath));

			// Resolve parent class
			UClass* ParentClass = FindClassByShortName(ParentClassName);
			if (!ParentClass)
				return FMCPToolResult::Error(FString::Printf(TEXT("Parent class '%s' not found. Ensure the class exists and is loaded."), *ParentClassName));

			if (!ParentClass->IsChildOf(UAnimInstance::StaticClass()))
				return FMCPToolResult::Error(FString::Printf(TEXT("Parent class '%s' is not derived from UAnimInstance"), *ParentClassName));

			// Validates the path and refuses a name that is already taken (on disk or
			// in memory) — a loaded duplicate makes the Blueprint factory assert.
			FString PackagePath, AssetName;
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package)
				return PackageError;

			// Use UAnimBlueprintFactory to create the asset
			UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
			Factory->TargetSkeleton = Skeleton;
			Factory->ParentClass = ParentClass;

			UAnimBlueprint* NewAnimBP = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(
				UAnimBlueprint::StaticClass(),
				Package,
				FName(*AssetName),
				RF_Public | RF_Standalone,
				nullptr,
				GWarn));

			if (!IsValid(NewAnimBP))
				return FMCPToolResult::Error(TEXT("UAnimBlueprintFactory::FactoryCreateNew returned null. Check log for details."));

			if (!SaveNewAsset(Package, NewAnimBP, PackagePath))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to save AnimBlueprint package at: %s"), *PackagePath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), AssetName);
			Result->SetStringField(TEXT("path"), AssetPath);
			Result->SetStringField(TEXT("skeleton"), Skeleton->GetName());
			Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
			Result->SetStringField(TEXT("parent_class"), ParentClass->GetName());

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
	MCP_TOOL(Registry, "get_anim_blueprint_info")
		.Description(TEXT(
			"Retrieve detailed information about an existing Animation Blueprint: parent class, target skeleton, "
			"anim graph names, state machine names, montage slot groups referenced, and sync groups. "
			"Useful for understanding an AnimBP structure before modifying it."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint asset (e.g., '/Game/Characters/ABP_Hero')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
			if (!IsValid(AnimBP))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load AnimBlueprint at path: %s"), *AssetPath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), AnimBP->GetName());
			Result->SetStringField(TEXT("path"), AnimBP->GetPathName());

			// Parent class
			UClass* ParentClass = AnimBP->ParentClass;
			Result->SetStringField(TEXT("parent_class"), IsValid(ParentClass) ? ParentClass->GetName() : TEXT("(none)"));

			// Target skeleton
			USkeleton* Skeleton = AnimBP->TargetSkeleton;
			if (IsValid(Skeleton))
			{
				Result->SetStringField(TEXT("target_skeleton"), Skeleton->GetName());
				Result->SetStringField(TEXT("target_skeleton_path"), Skeleton->GetPathName());
			}
			else
			{
				Result->SetStringField(TEXT("target_skeleton"), TEXT("(none)"));
				Result->SetStringField(TEXT("target_skeleton_path"), TEXT(""));
			}

			// Anim graphs - iterate UbergraphPages for AnimationGraph types
			TArray<TSharedPtr<FJsonValue>> AnimGraphNames;
			for (UEdGraph* Graph : AnimBP->UbergraphPages)
			{
				if (IsValid(Graph))
				{
					// UAnimationGraph is the anim graph type
					AnimGraphNames.Add(MakeShared<FJsonValueString>(Graph->GetName()));
				}
			}
			// Also check FunctionGraphs
			for (UEdGraph* Graph : AnimBP->FunctionGraphs)
			{
				if (IsValid(Graph))
				{
					AnimGraphNames.Add(MakeShared<FJsonValueString>(Graph->GetName()));
				}
			}
			Result->SetArrayField(TEXT("anim_graphs"), AnimGraphNames);

			// State machines - search graph nodes for state machine graphs
			TArray<TSharedPtr<FJsonValue>> StateMachineNames;
			TSet<FString> FoundStateMachines;

			auto CollectStateMachines = [&](const TArray<UEdGraph*>& Graphs)
			{
				for (UEdGraph* Graph : Graphs)
				{
					if (!IsValid(Graph)) continue;
					for (UEdGraphNode* Node : Graph->Nodes)
					{
						if (!IsValid(Node)) continue;
						// State machine nodes embed a sub-graph of type UAnimationStateMachineGraph
						for (UEdGraph* SubGraph : Node->GetSubGraphs())
						{
							if (!IsValid(SubGraph)) continue;
							if (SubGraph->IsA<UAnimationStateMachineGraph>())
							{
								const FString SMName = SubGraph->GetName();
								if (!FoundStateMachines.Contains(SMName))
								{
									FoundStateMachines.Add(SMName);
									StateMachineNames.Add(MakeShared<FJsonValueString>(SMName));
								}
							}
						}
					}
				}
			};

			CollectStateMachines(AnimBP->UbergraphPages);
			CollectStateMachines(AnimBP->FunctionGraphs);
			Result->SetArrayField(TEXT("state_machines"), StateMachineNames);

			// Montage slot groups and sync groups - inspect the generated class if compiled
			TArray<TSharedPtr<FJsonValue>> SlotGroupNames;
			TArray<TSharedPtr<FJsonValue>> SyncGroupNames;

			UAnimBlueprintGeneratedClass* GenClass = AnimBP->GetAnimBlueprintGeneratedClass();
			if (IsValid(GenClass))
			{
				// Pull unique slot group names from the skeleton's slot groups
				if (IsValid(Skeleton))
				{
					for (const FAnimSlotGroup& SlotGroup : Skeleton->GetSlotGroups())
					{
						SlotGroupNames.Add(MakeShared<FJsonValueString>(SlotGroup.GroupName.ToString()));

						for (const FName& SlotName : SlotGroup.SlotNames)
						{
							// sync group names are separate; slot names under each group:
							(void)SlotName;
						}
					}

					// Sync groups are stored on the skeleton as marker names
					for (const FName& MarkerName : Skeleton->GetExistingMarkerNames())
					{
						SyncGroupNames.Add(MakeShared<FJsonValueString>(MarkerName.ToString()));
					}
				}
			}

			Result->SetArrayField(TEXT("montage_slot_groups"), SlotGroupNames);
			Result->SetArrayField(TEXT("sync_groups"), SyncGroupNames);
			Result->SetBoolField(TEXT("is_compiled"), IsValid(GenClass));

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
	MCP_TOOL(Registry, "list_anim_assets_by_skeleton")
		.Description(TEXT(
			"List all animation assets (AnimSequence, AnimMontage, BlendSpace) that use a specific skeleton. "
			"Uses the Asset Registry to enumerate assets efficiently without loading them all. "
			"Returns asset name, path, type, and for sequences their duration and frame count."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("skeleton_path"), TEXT("Content path to the USkeleton asset to filter by (e.g., '/Game/Characters/SK_Hero_Skeleton')"), true)
		.EnumArg(TEXT("asset_type"), TEXT("Filter by animation asset type (default: 'All')"),
			{ TEXT("All"), TEXT("Sequence"), TEXT("Montage"), TEXT("BlendSpace") })
		.IntArg(TEXT("limit"), TEXT("Maximum number of assets to return (default: 100, max: 1000)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SkeletonPath;
			if (!Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
				return FMCPToolResult::Error(TEXT("skeleton_path is required"));

			FString AssetTypeFilter = TEXT("All");
			Args->TryGetStringField(TEXT("asset_type"), AssetTypeFilter);

			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("limit"))), 1, 1000);
			}

			// Load and validate the skeleton
			USkeleton* FilterSkeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
			if (!IsValid(FilterSkeleton))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load Skeleton at path: %s"), *SkeletonPath));

			IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

			// Determine which class paths to query
			FARFilter Filter;
			Filter.bRecursivePaths = true;
			Filter.PackagePaths.Add(FName(TEXT("/Game")));

			const bool bIncludeSequences  = (AssetTypeFilter == TEXT("All") || AssetTypeFilter == TEXT("Sequence"));
			const bool bIncludeMontages   = (AssetTypeFilter == TEXT("All") || AssetTypeFilter == TEXT("Montage"));
			const bool bIncludeBlendSpaces = (AssetTypeFilter == TEXT("All") || AssetTypeFilter == TEXT("BlendSpace"));

			if (bIncludeSequences)
				Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
			if (bIncludeMontages)
				Filter.ClassPaths.Add(UAnimMontage::StaticClass()->GetClassPathName());
			if (bIncludeBlendSpaces)
			{
				Filter.ClassPaths.Add(UBlendSpace::StaticClass()->GetClassPathName());
				Filter.ClassPaths.Add(UAimOffsetBlendSpace::StaticClass()->GetClassPathName());
				Filter.ClassPaths.Add(UBlendSpace1D::StaticClass()->GetClassPathName());
				Filter.ClassPaths.Add(UAimOffsetBlendSpace1D::StaticClass()->GetClassPathName());
			}

			if (Filter.ClassPaths.Num() == 0)
				return FMCPToolResult::Error(FString::Printf(TEXT("Unrecognised asset_type: '%s'. Valid values: All, Sequence, Montage, BlendSpace"), *AssetTypeFilter));

			// v4.6: reject non-matching skeletons in the Asset Registry rather than
			// by loading every candidate. Animation assets publish their skeleton as
			// the "Skeleton" tag (export-text name), which is the same query Persona's
			// asset pickers use. The tool description promised registry-only
			// enumeration; before this it loaded every asset in /Game.
			Filter.TagsAndValues.Add(TEXT("Skeleton"), FAssetData(FilterSkeleton).GetExportTextName());

			TArray<FAssetData> AllAssets;
			AssetRegistry.GetAssets(Filter, AllAssets);

			TArray<TSharedPtr<FJsonValue>> AssetArray;
			const int32 TotalMatching = AllAssets.Num();

			for (const FAssetData& AssetData : AllAssets)
			{
				if (AssetArray.Num() >= Limit) break;

				const FName ClassName = AssetData.AssetClassPath.GetAssetName();
				FString TypeString;
				if (ClassName == TEXT("AnimSequence"))
				{
					TypeString = TEXT("Sequence");
				}
				else if (ClassName == TEXT("AnimMontage"))
				{
					TypeString = TEXT("Montage");
				}
				else if (ClassName == TEXT("BlendSpace") || ClassName == TEXT("AimOffsetBlendSpace"))
				{
					TypeString = TEXT("BlendSpace");
				}
				else if (ClassName == TEXT("BlendSpace1D") || ClassName == TEXT("AimOffsetBlendSpace1D"))
				{
					TypeString = TEXT("BlendSpace1D");
				}
				else
				{
					TypeString = ClassName.ToString();
				}

				// Duration/frame count are not registry tags, so only the assets
				// that survive the filter (at most `limit`) get loaded. Blend
				// spaces are not UAnimSequenceBase and report 0 for both.
				float Duration = 0.0f;
				int32 NumFrames = 0;

				if (UAnimSequenceBase* SeqBase = Cast<UAnimSequenceBase>(AssetData.GetAsset()))
				{
					Duration = SeqBase->GetPlayLength();
					NumFrames = SeqBase->GetNumberOfSampledKeys();
				}

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
				Entry->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
				Entry->SetStringField(TEXT("type"), TypeString);
				Entry->SetNumberField(TEXT("duration"), Duration);
				Entry->SetNumberField(TEXT("num_frames"), NumFrames);

				AssetArray.Add(MakeShared<FJsonValueObject>(Entry));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("skeleton"), FilterSkeleton->GetName());
			Result->SetStringField(TEXT("skeleton_path"), FilterSkeleton->GetPathName());
			Result->SetStringField(TEXT("asset_type_filter"), AssetTypeFilter);
			Result->SetNumberField(TEXT("count"), AssetArray.Num());
			Result->SetNumberField(TEXT("total_matching"), TotalMatching);
			Result->SetArrayField(TEXT("assets"), AssetArray);

			if (TotalMatching > AssetArray.Num())
			{
				Result->SetStringField(TEXT("note"), FString::Printf(
					TEXT("Showing %d of %d matching assets. Increase limit to see more."),
					AssetArray.Num(), TotalMatching));
			}

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPAnimGraphTools::Lifecycle
