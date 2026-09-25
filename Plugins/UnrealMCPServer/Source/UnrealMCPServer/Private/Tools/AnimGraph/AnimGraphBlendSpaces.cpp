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

namespace MCPAnimGraphTools::BlendSpaces
{

using namespace MCPAnimGraphTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	MCP_TOOL(Registry, "create_blend_space")
		.Description(TEXT(
			"Create a 2D UBlendSpace asset targeting a specific skeleton. "
			"Configure the X and Y blend axes with display names and value ranges. "
			"After creation, use add_blend_space_sample to populate the grid with animation samples."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new BlendSpace asset (e.g., '/Game/Characters/BS_LocomotionGrid')"), true)
		.StringArg(TEXT("skeleton_path"), TEXT("Content path to the USkeleton asset (e.g., '/Game/Characters/SK_Hero_Skeleton')"), true)
		.StringArg(TEXT("axis_x_name"), TEXT("Display name for the horizontal axis (default: 'Speed')"))
		.StringArg(TEXT("axis_y_name"), TEXT("Display name for the vertical axis (default: 'Direction')"))
		.NumberArg(TEXT("axis_x_range_min"), TEXT("Minimum value of the X axis (default: 0)"))
		.NumberArg(TEXT("axis_x_range_max"), TEXT("Maximum value of the X axis (default: 500)"))
		.NumberArg(TEXT("axis_y_range_min"), TEXT("Minimum value of the Y axis (default: -180)"))
		.NumberArg(TEXT("axis_y_range_max"), TEXT("Maximum value of the Y axis (default: 180)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package)
				return PackageError;

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

			// v4.6: rebuild the blend grid after moving the axis ranges. The
			// BlendParameters write above changes the coordinate space the grid
			// samples live in; without ResampleData the asset keeps the grid built
			// for the factory's default ranges, and samples added later land in the
			// wrong cells.
			NewBS->ResampleData();
			NewBS->ValidateSampleData();

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

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
	MCP_TOOL(Registry, "add_blend_space_sample")
		.Description(TEXT(
			"Add an animation sequence as a sample point to an existing BlendSpace at the specified (X, Y) coordinates. "
			"The coordinates must fall within the axis ranges defined when the BlendSpace was created. "
			"Multiple calls can be used to populate the blend grid with different animations."))
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UBlendSpace asset to modify"), true)
		.StringArg(TEXT("animation_path"), TEXT("Content path to the UAnimSequence to add as a sample"), true)
		.NumberArg(TEXT("x"), TEXT("X axis coordinate where this sample is placed in the blend space grid"), true)
		.NumberArg(TEXT("y"), TEXT("Y axis coordinate where this sample is placed in the blend space grid"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			// v4.6: ValidateSampleData reconciles the new sample against the grid
			// and flags degenerate cases (coincident samples, samples the
			// triangulation cannot reach) that AddSample itself accepts silently.
			BlendSpace->ValidateSampleData();
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

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
	MCP_TOOL(Registry, "create_aim_offset")
		.Description(TEXT(
			"Create a UAimOffsetBlendSpace asset targeting a specific skeleton. "
			"Aim offsets are specialised 2D blend spaces designed for additive aiming poses. "
			"The X axis defaults to horizontal aim angle [-90, 90] and Y axis to vertical aim angle [-90, 90]. "
			"Use add_blend_space_sample to add additive AnimSequence poses to the grid."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new AimOffset asset (e.g., '/Game/Characters/AO_Hero')"), true)
		.StringArg(TEXT("skeleton_path"), TEXT("Content path to the USkeleton asset (e.g., '/Game/Characters/SK_Hero_Skeleton')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package)
				return PackageError;

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

			// v4.6: see create_blend_space — the grid must be rebuilt for the new
			// axis ranges or later samples land in the wrong cells.
			NewAO->ResampleData();
			NewAO->ValidateSampleData();

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

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPAnimGraphTools::BlendSpaces
