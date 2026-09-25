// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPMaterialTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/Texture2D.h"
#include "Common/MCPAssetCreate.h"

namespace MCPMaterialTools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_material - Create a new Material asset
	// ================================================================
	MCP_TOOL(Registry, "create_material")
		.Description(TEXT("Create a new Material asset with specified shading model and blend mode. The material is saved and ready for parameter editing."))
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new material (e.g., '/Game/Materials/M_MyMaterial')"), true)
		.EnumArg(TEXT("shading_model"), TEXT("Shading model"), { TEXT("DefaultLit"), TEXT("Unlit"), TEXT("Subsurface"), TEXT("ClearCoat"), TEXT("TwoSidedFoliage") })
		.EnumArg(TEXT("blend_mode"), TEXT("Blend mode"), { TEXT("Opaque"), TEXT("Masked"), TEXT("Translucent"), TEXT("Additive") })
		.BoolArg(TEXT("two_sided"), TEXT("Enable two-sided rendering (default: false)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString PackagePath, AssetName;
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package) return PackageError;

			UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
			UMaterial* NewMaterial = Cast<UMaterial>(Factory->FactoryCreateNew(
				UMaterial::StaticClass(), Package, FName(*AssetName),
				RF_Public | RF_Standalone, nullptr, GWarn));

			if (!NewMaterial) return FMCPToolResult::Error(TEXT("Failed to create material"));

			// Shading model
			FString ShadingModel;
			if (Args->TryGetStringField(TEXT("shading_model"), ShadingModel))
			{
				if (ShadingModel == TEXT("Unlit")) NewMaterial->SetShadingModel(MSM_Unlit);
				else if (ShadingModel == TEXT("Subsurface")) NewMaterial->SetShadingModel(MSM_Subsurface);
				else if (ShadingModel == TEXT("ClearCoat")) NewMaterial->SetShadingModel(MSM_ClearCoat);
				else if (ShadingModel == TEXT("TwoSidedFoliage")) NewMaterial->SetShadingModel(MSM_TwoSidedFoliage);
				// DefaultLit is the default
			}

			// Blend mode
			FString BlendMode;
			if (Args->TryGetStringField(TEXT("blend_mode"), BlendMode))
			{
				if (BlendMode == TEXT("Masked")) NewMaterial->BlendMode = BLEND_Masked;
				else if (BlendMode == TEXT("Translucent")) NewMaterial->BlendMode = BLEND_Translucent;
				else if (BlendMode == TEXT("Additive")) NewMaterial->BlendMode = BLEND_Additive;
			}

			// Two sided
			bool bTwoSided = false;
			if (Args->TryGetBoolField(TEXT("two_sided"), bTwoSided))
			{
				NewMaterial->TwoSided = bTwoSided;
			}

			NewMaterial->PreEditChange(nullptr);
			NewMaterial->PostEditChange();

			FAssetRegistryModule::AssetCreated(NewMaterial);
			Package->MarkPackageDirty();

			FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, NewMaterial, *PackageFilename, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Created material '%s' at %s"), *AssetName, *AssetPath));
		});

	// ================================================================
	// create_material_instance - Create a Material Instance Constant
	// ================================================================
	MCP_TOOL(Registry, "create_material_instance")
		.Description(TEXT("Create a Material Instance Constant from a parent material. Parameters can then be set using set_material_scalar/set_material_vector."))
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new MI"), true)
		.StringArg(TEXT("parent_path"), TEXT("Content path of the parent material"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, ParentPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("parent_path"), ParentPath)) return FMCPToolResult::Error(TEXT("parent_path required"));

			UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *ParentPath);
			if (!Parent) return FMCPToolResult::Error(FString::Printf(TEXT("Parent material not found: %s"), *ParentPath));

			FString PackagePath, AssetName;
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package) return PackageError;

			UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
			Factory->InitialParent = Parent;

			UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Factory->FactoryCreateNew(
				UMaterialInstanceConstant::StaticClass(), Package, FName(*AssetName),
				RF_Public | RF_Standalone, nullptr, GWarn));

			if (!MIC) return FMCPToolResult::Error(TEXT("Failed to create material instance"));

			FAssetRegistryModule::AssetCreated(MIC);
			Package->MarkPackageDirty();

			FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, MIC, *PackageFilename, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Created material instance '%s' (parent: %s)"), *AssetName, *Parent->GetName()));
		});

	// ================================================================
	// set_material_scalar - Set scalar parameter on MI
	// ================================================================
	MCP_TOOL(Registry, "set_material_scalar")
		.Description(TEXT("Set a scalar parameter value on a Material Instance Constant."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Material Instance path"), true)
		.StringArg(TEXT("parameter_name"), TEXT("Scalar parameter name"), true)
		.NumberArg(TEXT("value"), TEXT("Scalar value"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, ParamName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("parameter_name"), ParamName)) return FMCPToolResult::Error(TEXT("parameter_name required"));

			UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *AssetPath);
			if (!MIC) return FMCPToolResult::Error(FString::Printf(TEXT("Material instance not found: %s"), *AssetPath));

			float Value = (float)Args->GetNumberField(TEXT("value"));

			MIC->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(FName(*ParamName)), Value);
			MIC->MarkPackageDirty();

			return FMCPToolResult::Success(FString::Printf(TEXT("Set '%s' = %f on '%s'"), *ParamName, Value, *MIC->GetName()));
		});

	// ================================================================
	// set_material_vector - Set vector parameter on MI
	// ================================================================
	MCP_TOOL(Registry, "set_material_vector")
		.Description(TEXT("Set a vector (color) parameter value on a Material Instance Constant."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Material Instance path"), true)
		.StringArg(TEXT("parameter_name"), TEXT("Vector parameter name"), true)
		.NumberArg(TEXT("r"), TEXT("Red channel (0-1)"), true)
		.NumberArg(TEXT("g"), TEXT("Green channel (0-1)"), true)
		.NumberArg(TEXT("b"), TEXT("Blue channel (0-1)"), true)
		.NumberArg(TEXT("a"), TEXT("Alpha channel (0-1, default: 1)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, ParamName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("parameter_name"), ParamName)) return FMCPToolResult::Error(TEXT("parameter_name required"));

			UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *AssetPath);
			if (!MIC) return FMCPToolResult::Error(FString::Printf(TEXT("Material instance not found: %s"), *AssetPath));

			FLinearColor Color(
				(float)Args->GetNumberField(TEXT("r")),
				(float)Args->GetNumberField(TEXT("g")),
				(float)Args->GetNumberField(TEXT("b")),
				Args->HasField(TEXT("a")) ? (float)Args->GetNumberField(TEXT("a")) : 1.0f
			);

			MIC->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(FName(*ParamName)), Color);
			MIC->MarkPackageDirty();

			return FMCPToolResult::Success(FString::Printf(TEXT("Set '%s' = (%.2f, %.2f, %.2f, %.2f) on '%s'"),
				*ParamName, Color.R, Color.G, Color.B, Color.A, *MIC->GetName()));
		});

	// ================================================================
	// assign_material - Apply material to a mesh actor
	// ================================================================
	MCP_TOOL(Registry, "assign_material")
		.Description(TEXT("Assign a material to a static mesh actor's material slot. Works on any actor with a mesh component."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the target actor"), true)
		.StringArg(TEXT("material_path"), TEXT("Content path of the material to assign"), true)
		.IntArg(TEXT("slot_index"), TEXT("Material slot index (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ActorName, MaterialPath;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));
			if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath)) return FMCPToolResult::Error(TEXT("material_path required"));

			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
			if (!Material) return FMCPToolResult::Error(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

			int32 SlotIndex = 0;
			if (Args->HasField(TEXT("slot_index")))
			{
				SlotIndex = (int32)Args->GetNumberField(TEXT("slot_index"));
			}

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

			AActor* Actor = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if ((*It)->GetActorLabel() == ActorName) { Actor = *It; break; }
			}
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			// Find mesh component
			UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
			if (!MeshComp) return FMCPToolResult::Error(TEXT("Actor has no StaticMeshComponent"));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Assign Material")));
			MeshComp->Modify();
			MeshComp->SetMaterial(SlotIndex, Material);
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Assigned '%s' to '%s' slot %d"),
				*Material->GetName(), *ActorName, SlotIndex));
		});
}

} // namespace MCPMaterialTools
