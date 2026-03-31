#include "Tools/MCPMaterialTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

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

namespace MCPMaterialTools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_material - Create a new Material asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new material (e.g., '/Game/Materials/M_MyMaterial')"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("shading_model"), TEXT("Shading model"),
			{ TEXT("DefaultLit"), TEXT("Unlit"), TEXT("Subsurface"), TEXT("ClearCoat"), TEXT("TwoSidedFoliage") });
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("blend_mode"), TEXT("Blend mode"),
			{ TEXT("Opaque"), TEXT("Masked"), TEXT("Translucent"), TEXT("Additive") });
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("two_sided"), TEXT("Enable two-sided rendering (default: false)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_material");
		Def.Description = TEXT("Create a new Material asset with specified shading model and blend mode. The material is saved and ready for parameter editing.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			FString AssetName = FPackageName::GetShortName(AssetPath);

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package) return FMCPToolResult::Error(TEXT("Failed to create package"));

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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_material_instance - Create a Material Instance Constant
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new MI"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("parent_path"), TEXT("Content path of the parent material"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_material_instance");
		Def.Description = TEXT("Create a Material Instance Constant from a parent material. Parameters can then be set using set_material_scalar/set_material_vector.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, ParentPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("parent_path"), ParentPath)) return FMCPToolResult::Error(TEXT("parent_path required"));

			UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *ParentPath);
			if (!Parent) return FMCPToolResult::Error(FString::Printf(TEXT("Parent material not found: %s"), *ParentPath));

			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			FString AssetName = FPackageName::GetShortName(AssetPath);

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package) return FMCPToolResult::Error(TEXT("Failed to create package"));

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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_material_scalar - Set scalar parameter on MI
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Material Instance path"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("parameter_name"), TEXT("Scalar parameter name"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("value"), TEXT("Scalar value"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_material_scalar");
		Def.Description = TEXT("Set a scalar parameter value on a Material Instance Constant.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_material_vector - Set vector parameter on MI
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Material Instance path"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("parameter_name"), TEXT("Vector parameter name"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("r"), TEXT("Red channel (0-1)"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("g"), TEXT("Green channel (0-1)"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("b"), TEXT("Blue channel (0-1)"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("a"), TEXT("Alpha channel (0-1, default: 1)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_material_vector");
		Def.Description = TEXT("Set a vector (color) parameter value on a Material Instance Constant.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// assign_material - Apply material to a mesh actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the target actor"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"), TEXT("Content path of the material to assign"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("slot_index"), TEXT("Material slot index (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("assign_material");
		Def.Description = TEXT("Assign a material to a static mesh actor's material slot. Works on any actor with a mesh component.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPMaterialTools
