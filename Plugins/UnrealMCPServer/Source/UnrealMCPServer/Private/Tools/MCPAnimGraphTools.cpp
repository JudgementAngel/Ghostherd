#include "Tools/MCPAnimGraphTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

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
#include "Animation/Skeleton.h"
#include "AnimationStateMachineGraph.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/AimOffsetBlendSpaceFactoryNew.h"
#include "Factories/AnimMontageFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace MCPAnimGraphTools
{

// ============================================================================
// Helper: create package path and asset name from a full content path
// ============================================================================

static bool SplitAssetPath(const FString& FullPath, FString& OutPackagePath, FString& OutAssetName)
{
	OutPackagePath = FPackageName::ObjectPathToPackageName(FullPath);
	OutAssetName = FPackageName::GetShortName(FullPath);
	return !OutPackagePath.IsEmpty() && !OutAssetName.IsEmpty();
}

// ============================================================================
// Helper: save a newly created asset package
// ============================================================================

static bool SaveNewAsset(UPackage* Package, UObject* Asset, const FString& PackagePath)
{
	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();

	const FString PackageFilename = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
}

// ============================================================================
// Helper: find a UClass by short name, trying several lookup strategies
// ============================================================================

static UClass* FindClassByShortName(const FString& ClassName)
{
	// Exact match first
	UClass* Found = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	if (Found) return Found;

	// Try with U prefix
	Found = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *ClassName), EFindFirstObjectOptions::ExactClass);
	if (Found) return Found;

	// Try LoadClass from common modules
	const TArray<FString> Modules = { TEXT("Engine"), TEXT("AnimGraphRuntime"), TEXT("GameplayAbilities") };
	for (const FString& Module : Modules)
	{
		Found = LoadClass<UObject>(nullptr, *FString::Printf(TEXT("/Script/%s.%s"), *Module, *ClassName));
		if (Found) return Found;
	}

	return nullptr;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_anim_blueprint - Create a new UAnimBlueprint asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new AnimBlueprint asset (e.g., '/Game/Characters/ABP_Hero')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("skeleton_path"), TEXT("Content path to the USkeleton asset to target (e.g., '/Game/Characters/SK_Hero_Skeleton')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("parent_class"), TEXT("Short class name of the parent AnimInstance class (default: 'AnimInstance')"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_anim_blueprint");
		Def.Description = TEXT(
			"Create a new Animation Blueprint (UAnimBlueprint) asset targeting a specific skeleton. "
			"Optionally specify a parent class derived from UAnimInstance. "
			"The created asset is saved to disk immediately and ready for graph editing.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			FString PackagePath, AssetName;
			if (!SplitAssetPath(AssetPath, PackagePath, AssetName))
				return FMCPToolResult::Error(FString::Printf(TEXT("Invalid asset path format: %s"), *AssetPath));

			// Create the package
			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
				return FMCPToolResult::Error(TEXT("Failed to create UPackage for the new AnimBlueprint"));

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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_anim_blueprint_info - Get detailed info about an AnimBlueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint asset (e.g., '/Game/Characters/ABP_Hero')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_anim_blueprint_info");
		Def.Description = TEXT(
			"Retrieve detailed information about an existing Animation Blueprint: parent class, target skeleton, "
			"anim graph names, state machine names, montage slot groups referenced, and sync groups. "
			"Useful for understanding an AnimBP structure before modifying it.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_blend_space - Create a 2D UBlendSpace asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new BlendSpace asset (e.g., '/Game/Characters/BS_LocomotionGrid')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("skeleton_path"), TEXT("Content path to the USkeleton asset (e.g., '/Game/Characters/SK_Hero_Skeleton')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("axis_x_name"), TEXT("Display name for the horizontal axis (default: 'Speed')"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("axis_y_name"), TEXT("Display name for the vertical axis (default: 'Direction')"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("axis_x_range_min"), TEXT("Minimum value of the X axis (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("axis_x_range_max"), TEXT("Maximum value of the X axis (default: 500)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("axis_y_range_min"), TEXT("Minimum value of the Y axis (default: -180)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("axis_y_range_max"), TEXT("Maximum value of the Y axis (default: 180)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_blend_space");
		Def.Description = TEXT(
			"Create a 2D UBlendSpace asset targeting a specific skeleton. "
			"Configure the X and Y blend axes with display names and value ranges. "
			"After creation, use add_blend_space_sample to populate the grid with animation samples.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, SkeletonPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
				return FMCPToolResult::Error(TEXT("skeleton_path is required"));

			USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
			if (!IsValid(Skeleton))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load Skeleton at path: %s"), *SkeletonPath));

			FString AxisXName = TEXT("Speed");
			FString AxisYName = TEXT("Direction");
			Args->TryGetStringField(TEXT("axis_x_name"), AxisXName);
			Args->TryGetStringField(TEXT("axis_y_name"), AxisYName);

			float AxisXMin = 0.0f;
			float AxisXMax = 500.0f;
			float AxisYMin = -180.0f;
			float AxisYMax = 180.0f;

			if (Args->HasField(TEXT("axis_x_range_min"))) AxisXMin = static_cast<float>(Args->GetNumberField(TEXT("axis_x_range_min")));
			if (Args->HasField(TEXT("axis_x_range_max"))) AxisXMax = static_cast<float>(Args->GetNumberField(TEXT("axis_x_range_max")));
			if (Args->HasField(TEXT("axis_y_range_min"))) AxisYMin = static_cast<float>(Args->GetNumberField(TEXT("axis_y_range_min")));
			if (Args->HasField(TEXT("axis_y_range_max"))) AxisYMax = static_cast<float>(Args->GetNumberField(TEXT("axis_y_range_max")));

			if (AxisXMin >= AxisXMax)
				return FMCPToolResult::Error(FString::Printf(TEXT("axis_x_range_min (%.2f) must be less than axis_x_range_max (%.2f)"), AxisXMin, AxisXMax));
			if (AxisYMin >= AxisYMax)
				return FMCPToolResult::Error(FString::Printf(TEXT("axis_y_range_min (%.2f) must be less than axis_y_range_max (%.2f)"), AxisYMin, AxisYMax));

			FString PackagePath, AssetName;
			if (!SplitAssetPath(AssetPath, PackagePath, AssetName))
				return FMCPToolResult::Error(FString::Printf(TEXT("Invalid asset path format: %s"), *AssetPath));

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
				return FMCPToolResult::Error(TEXT("Failed to create UPackage for the new BlendSpace"));

			UBlendSpaceFactoryNew* Factory = NewObject<UBlendSpaceFactoryNew>();
			Factory->TargetSkeleton = Skeleton;

			UBlendSpace* NewBS = Cast<UBlendSpace>(Factory->FactoryCreateNew(
				UBlendSpace::StaticClass(),
				Package,
				FName(*AssetName),
				RF_Public | RF_Standalone,
				nullptr,
				GWarn));

			if (!IsValid(NewBS))
				return FMCPToolResult::Error(TEXT("UBlendSpaceFactoryNew::FactoryCreateNew returned null. Check log for details."));

			// Configure X axis via direct property access (BlendParameters is a protected UPROPERTY array)
			{
				FProperty* Prop = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters"));
				if (Prop)
				{
					FBlendParameter* Params = Prop->ContainerPtrToValuePtr<FBlendParameter>(NewBS);
					Params[0].DisplayName = AxisXName;
					Params[0].Min = AxisXMin;
					Params[0].Max = AxisXMax;
					Params[1].DisplayName = AxisYName;
					Params[1].Min = AxisYMin;
					Params[1].Max = AxisYMax;
				}
			}

			NewBS->PreEditChange(nullptr);
			NewBS->PostEditChange();

			if (!SaveNewAsset(Package, NewBS, PackagePath))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to save BlendSpace package at: %s"), *PackagePath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), AssetName);
			Result->SetStringField(TEXT("path"), AssetPath);
			Result->SetStringField(TEXT("skeleton"), Skeleton->GetName());
			Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
			Result->SetStringField(TEXT("axis_x_name"), AxisXName);
			Result->SetNumberField(TEXT("axis_x_min"), AxisXMin);
			Result->SetNumberField(TEXT("axis_x_max"), AxisXMax);
			Result->SetStringField(TEXT("axis_y_name"), AxisYName);
			Result->SetNumberField(TEXT("axis_y_min"), AxisYMin);
			Result->SetNumberField(TEXT("axis_y_max"), AxisYMax);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_blend_space_sample - Add an animation sample to a BlendSpace
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path to the UBlendSpace asset to modify"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("animation_path"), TEXT("Content path to the UAnimSequence to add as a sample"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"), TEXT("X axis coordinate where this sample is placed in the blend space grid"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"), TEXT("Y axis coordinate where this sample is placed in the blend space grid"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_blend_space_sample");
		Def.Description = TEXT(
			"Add an animation sequence as a sample point to an existing BlendSpace at the specified (X, Y) coordinates. "
			"The coordinates must fall within the axis ranges defined when the BlendSpace was created. "
			"Multiple calls can be used to populate the blend grid with different animations.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, AnimPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("animation_path"), AnimPath))
				return FMCPToolResult::Error(TEXT("animation_path is required"));

			if (!Args->HasField(TEXT("x")))
				return FMCPToolResult::Error(TEXT("x is required"));
			if (!Args->HasField(TEXT("y")))
				return FMCPToolResult::Error(TEXT("y is required"));

			const float SampleX = static_cast<float>(Args->GetNumberField(TEXT("x")));
			const float SampleY = static_cast<float>(Args->GetNumberField(TEXT("y")));

			UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, *AssetPath);
			if (!IsValid(BlendSpace))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load BlendSpace at path: %s"), *AssetPath));

			UAnimSequence* AnimSeq = LoadObject<UAnimSequence>(nullptr, *AnimPath);
			if (!IsValid(AnimSeq))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load AnimSequence at path: %s"), *AnimPath));

			// Validate sample coordinates against axis ranges (GetBlendParameter returns const ref)
			const FBlendParameter& BlendAxisX = BlendSpace->GetBlendParameter(0);
			const FBlendParameter& BlendAxisY = BlendSpace->GetBlendParameter(1);

			if (SampleX < BlendAxisX.Min || SampleX > BlendAxisX.Max)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("X value %.2f is outside BlendSpace X axis range [%.2f, %.2f]"),
					SampleX, BlendAxisX.Min, BlendAxisX.Max));
			}
			if (SampleY < BlendAxisY.Min || SampleY > BlendAxisY.Max)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Y value %.2f is outside BlendSpace Y axis range [%.2f, %.2f]"),
					SampleY, BlendAxisY.Min, BlendAxisY.Max));
			}

			BlendSpace->Modify();

			// AddSample(AnimSequence, SampleValue) returns the sample index, or INDEX_NONE on failure
			const FVector SamplePoint(SampleX, SampleY, 0.0f);
			const int32 SampleIndex = BlendSpace->AddSample(AnimSeq, SamplePoint);
			const bool bAdded = (SampleIndex != INDEX_NONE);

			if (!bAdded)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to add sample at (%.2f, %.2f). The position may already be occupied or out of range."),
					SampleX, SampleY));
			}

			BlendSpace->PostEditChange();
			BlendSpace->MarkPackageDirty();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("blend_space"), BlendSpace->GetName());
			Result->SetStringField(TEXT("blend_space_path"), BlendSpace->GetPathName());
			Result->SetStringField(TEXT("animation"), AnimSeq->GetName());
			Result->SetStringField(TEXT("animation_path"), AnimSeq->GetPathName());
			Result->SetNumberField(TEXT("x"), SampleX);
			Result->SetNumberField(TEXT("y"), SampleY);
			Result->SetNumberField(TEXT("total_samples"), BlendSpace->GetBlendSamples().Num());

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_aim_offset - Create a UAimOffsetBlendSpace asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new AimOffset asset (e.g., '/Game/Characters/AO_Hero')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("skeleton_path"), TEXT("Content path to the USkeleton asset (e.g., '/Game/Characters/SK_Hero_Skeleton')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_aim_offset");
		Def.Description = TEXT(
			"Create a UAimOffsetBlendSpace asset targeting a specific skeleton. "
			"Aim offsets are specialised 2D blend spaces designed for additive aiming poses. "
			"The X axis defaults to horizontal aim angle [-90, 90] and Y axis to vertical aim angle [-90, 90]. "
			"Use add_blend_space_sample to add additive AnimSequence poses to the grid.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, SkeletonPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
				return FMCPToolResult::Error(TEXT("skeleton_path is required"));

			USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
			if (!IsValid(Skeleton))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load Skeleton at path: %s"), *SkeletonPath));

			FString PackagePath, AssetName;
			if (!SplitAssetPath(AssetPath, PackagePath, AssetName))
				return FMCPToolResult::Error(FString::Printf(TEXT("Invalid asset path format: %s"), *AssetPath));

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
				return FMCPToolResult::Error(TEXT("Failed to create UPackage for the new AimOffset"));

			// UAimOffsetBlendSpace is created via the AimOffset-specific factory
			UAimOffsetBlendSpaceFactoryNew* Factory = NewObject<UAimOffsetBlendSpaceFactoryNew>();
			Factory->TargetSkeleton = Skeleton;

			UAimOffsetBlendSpace* NewAO = Cast<UAimOffsetBlendSpace>(Factory->FactoryCreateNew(
				UAimOffsetBlendSpace::StaticClass(),
				Package,
				FName(*AssetName),
				RF_Public | RF_Standalone,
				nullptr,
				GWarn));

			if (!IsValid(NewAO))
				return FMCPToolResult::Error(TEXT("AimOffsetBlendSpaceFactoryNew::FactoryCreateNew returned null. Check log for details."));

			// Configure sensible defaults for aiming: horizontal and vertical angles
			{
				FProperty* Prop = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters"));
				if (Prop)
				{
					FBlendParameter* Params = Prop->ContainerPtrToValuePtr<FBlendParameter>(NewAO);
					Params[0].DisplayName = TEXT("Yaw");
					Params[0].Min = -90.0f;
					Params[0].Max = 90.0f;
					Params[1].DisplayName = TEXT("Pitch");
					Params[1].Min = -90.0f;
					Params[1].Max = 90.0f;
				}
			}

			NewAO->PreEditChange(nullptr);
			NewAO->PostEditChange();

			if (!SaveNewAsset(Package, NewAO, PackagePath))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to save AimOffset package at: %s"), *PackagePath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), AssetName);
			Result->SetStringField(TEXT("path"), AssetPath);
			Result->SetStringField(TEXT("skeleton"), Skeleton->GetName());
			Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
			Result->SetStringField(TEXT("axis_x_name"), TEXT("Yaw"));
			Result->SetNumberField(TEXT("axis_x_min"), -90.0f);
			Result->SetNumberField(TEXT("axis_x_max"), 90.0f);
			Result->SetStringField(TEXT("axis_y_name"), TEXT("Pitch"));
			Result->SetNumberField(TEXT("axis_y_min"), -90.0f);
			Result->SetNumberField(TEXT("axis_y_max"), 90.0f);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_anim_montage - Create a UAnimMontage asset from an animation
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new AnimMontage asset (e.g., '/Game/Characters/AM_HeroAttack')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("animation_path"), TEXT("Content path to the source UAnimSequence asset (e.g., '/Game/Characters/AS_Attack01')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("slot_name"), TEXT("Name of the slot track in the montage (default: 'DefaultSlot')"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_anim_montage");
		Def.Description = TEXT(
			"Create a new UAnimMontage from an existing animation sequence. "
			"The animation is placed into a named slot track which determines which slot on the AnimGraph is driven. "
			"The created montage can be played on a character at runtime using PlayAnimMontage() or Montage_Play().");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, AnimPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("animation_path"), AnimPath))
				return FMCPToolResult::Error(TEXT("animation_path is required"));

			FString SlotName = TEXT("DefaultSlot");
			Args->TryGetStringField(TEXT("slot_name"), SlotName);

			UAnimSequence* AnimSeq = LoadObject<UAnimSequence>(nullptr, *AnimPath);
			if (!IsValid(AnimSeq))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load AnimSequence at path: %s"), *AnimPath));

			FString PackagePath, AssetName;
			if (!SplitAssetPath(AssetPath, PackagePath, AssetName))
				return FMCPToolResult::Error(FString::Printf(TEXT("Invalid asset path format: %s"), *AssetPath));

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
				return FMCPToolResult::Error(TEXT("Failed to create UPackage for the new AnimMontage"));

			UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
			Factory->SourceAnimation = AnimSeq;

			UAnimMontage* NewMontage = Cast<UAnimMontage>(Factory->FactoryCreateNew(
				UAnimMontage::StaticClass(),
				Package,
				FName(*AssetName),
				RF_Public | RF_Standalone,
				nullptr,
				GWarn));

			if (!IsValid(NewMontage))
				return FMCPToolResult::Error(TEXT("UAnimMontageFactory::FactoryCreateNew returned null. Check log for details."));

			// Update the slot name on the first slot track if it exists
			if (NewMontage->SlotAnimTracks.Num() > 0)
			{
				NewMontage->SlotAnimTracks[0].SlotName = FName(*SlotName);
			}
			else
			{
				// No tracks were created by the factory; create one manually
				FSlotAnimationTrack NewTrack;
				NewTrack.SlotName = FName(*SlotName);
				NewMontage->SlotAnimTracks.Add(NewTrack);
			}

			NewMontage->PreEditChange(nullptr);
			NewMontage->PostEditChange();

			if (!SaveNewAsset(Package, NewMontage, PackagePath))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to save AnimMontage package at: %s"), *PackagePath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), AssetName);
			Result->SetStringField(TEXT("path"), AssetPath);
			Result->SetStringField(TEXT("source_animation"), AnimSeq->GetName());
			Result->SetStringField(TEXT("source_animation_path"), AnimSeq->GetPathName());
			Result->SetStringField(TEXT("slot_name"), SlotName);
			Result->SetNumberField(TEXT("duration"), NewMontage->GetPlayLength());
			Result->SetNumberField(TEXT("section_count"), NewMontage->CompositeSections.Num());

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_anim_montage_info - Get detailed info about an AnimMontage
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path to the UAnimMontage asset (e.g., '/Game/Characters/AM_HeroAttack')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_anim_montage_info");
		Def.Description = TEXT(
			"Retrieve detailed information about an AnimMontage asset: total duration, "
			"composite sections with their start times and next section links, "
			"slot track names with their animation segments, and anim notify events. "
			"Useful for understanding or debugging a montage before runtime playback.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *AssetPath);
			if (!IsValid(Montage))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load AnimMontage at path: %s"), *AssetPath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Montage->GetName());
			Result->SetStringField(TEXT("path"), Montage->GetPathName());
			Result->SetNumberField(TEXT("duration"), Montage->GetPlayLength());
			Result->SetNumberField(TEXT("rate_scale"), Montage->RateScale);

			// Target skeleton info
			USkeleton* Skeleton = Montage->GetSkeleton();
			if (IsValid(Skeleton))
			{
				Result->SetStringField(TEXT("skeleton"), Skeleton->GetName());
				Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
			}

			// Composite sections
			TArray<TSharedPtr<FJsonValue>> SectionsArray;
			for (const FCompositeSection& Section : Montage->CompositeSections)
			{
				TSharedPtr<FJsonObject> SectionObj = MakeShared<FJsonObject>();
				SectionObj->SetStringField(TEXT("name"), Section.SectionName.ToString());
				SectionObj->SetNumberField(TEXT("start_time"), Section.GetTime());
				SectionObj->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
				SectionsArray.Add(MakeShared<FJsonValueObject>(SectionObj));
			}
			Result->SetArrayField(TEXT("sections"), SectionsArray);
			Result->SetNumberField(TEXT("section_count"), SectionsArray.Num());

			// Slot tracks
			TArray<TSharedPtr<FJsonValue>> SlotsArray;
			for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
			{
				TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
				SlotObj->SetStringField(TEXT("slot_name"), SlotTrack.SlotName.ToString());

				TArray<TSharedPtr<FJsonValue>> SegmentsArray;
				for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
				{
					TSharedPtr<FJsonObject> SegObj = MakeShared<FJsonObject>();
					SegObj->SetStringField(TEXT("animation"), IsValid(Segment.GetAnimReference()) ? Segment.GetAnimReference()->GetName() : TEXT("(none)"));
					SegObj->SetNumberField(TEXT("start_pos"), Segment.StartPos);
					SegObj->SetNumberField(TEXT("anim_start_time"), Segment.AnimStartTime);
					SegObj->SetNumberField(TEXT("anim_end_time"), Segment.AnimEndTime);
					SegObj->SetNumberField(TEXT("anim_play_rate"), Segment.AnimPlayRate);
					SegmentsArray.Add(MakeShared<FJsonValueObject>(SegObj));
				}
				SlotObj->SetArrayField(TEXT("segments"), SegmentsArray);
				SlotsArray.Add(MakeShared<FJsonValueObject>(SlotObj));
			}
			Result->SetArrayField(TEXT("slots"), SlotsArray);
			Result->SetNumberField(TEXT("slot_count"), SlotsArray.Num());

			// Anim notifies
			TArray<TSharedPtr<FJsonValue>> NotifiesArray;
			for (const FAnimNotifyEvent& NotifyEvent : Montage->Notifies)
			{
				TSharedPtr<FJsonObject> NotifyObj = MakeShared<FJsonObject>();
				NotifyObj->SetStringField(TEXT("name"), NotifyEvent.NotifyName.ToString());
				NotifyObj->SetNumberField(TEXT("trigger_time"), NotifyEvent.GetTriggerTime());
				NotifyObj->SetNumberField(TEXT("duration"), NotifyEvent.GetDuration());

				const FString NotifyClassName = IsValid(NotifyEvent.Notify)
					? NotifyEvent.Notify->GetClass()->GetName()
					: TEXT("(state-less notify)");
				NotifyObj->SetStringField(TEXT("class"), NotifyClassName);

				NotifiesArray.Add(MakeShared<FJsonValueObject>(NotifyObj));
			}
			Result->SetArrayField(TEXT("notifies"), NotifiesArray);
			Result->SetNumberField(TEXT("notify_count"), NotifiesArray.Num());

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// list_anim_assets_by_skeleton - List animation assets for a skeleton
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("skeleton_path"), TEXT("Content path to the USkeleton asset to filter by (e.g., '/Game/Characters/SK_Hero_Skeleton')"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("asset_type"), TEXT("Filter by animation asset type (default: 'All')"),
			{ TEXT("All"), TEXT("Sequence"), TEXT("Montage"), TEXT("BlendSpace") });
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("limit"), TEXT("Maximum number of assets to return (default: 100, max: 1000)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_anim_assets_by_skeleton");
		Def.Description = TEXT(
			"List all animation assets (AnimSequence, AnimMontage, BlendSpace) that use a specific skeleton. "
			"Uses the Asset Registry to enumerate assets efficiently without loading them all. "
			"Returns asset name, path, type, and for sequences their duration and frame count.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			TArray<FAssetData> AllAssets;
			AssetRegistry.GetAssets(Filter, AllAssets);

			TArray<TSharedPtr<FJsonValue>> AssetArray;
			int32 TotalMatching = 0;

			for (const FAssetData& AssetData : AllAssets)
			{
				if (AssetArray.Num() >= Limit) break;

				// Load the asset to check its skeleton and gather metadata
				UAnimationAsset* AnimAsset = Cast<UAnimationAsset>(AssetData.GetAsset());
				if (!IsValid(AnimAsset)) continue;

				USkeleton* AssetSkeleton = AnimAsset->GetSkeleton();
				if (AssetSkeleton != FilterSkeleton) continue;

				TotalMatching++;

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

				float Duration = 0.0f;
				int32 NumFrames = 0;

				if (UAnimSequenceBase* SeqBase = Cast<UAnimSequenceBase>(AnimAsset))
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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPAnimGraphTools
