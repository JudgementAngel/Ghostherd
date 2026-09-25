// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// Phase D.6 (Material Layers) — Layer stack manipulation on material instances.
//
// Backed by UMaterialInstance::GetMaterialLayers / SetMaterialLayers (both
// public ENGINE_API) over FMaterialLayersFunctions. The Layers[] array holds
// UMaterialFunctionInterface assets; Blends[] holds matching blend functions
// (one per layer except the base).
//
// Set operations are editor-only — gated behind WITH_EDITOR — so the tools
// fail cleanly when the plugin runs in a no-editor context (which it never
// does today, but the guard keeps us forward-compatible).

#include "Tools/MCPMaterialLayerTools.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialLayersFunctions.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

namespace MCPMaterialLayerTools
{

static UMaterialInstance* LoadMaterialInstance(const FString& Path)
{
	UMaterialInstance* MI = LoadObject<UMaterialInstance>(nullptr, *Path);
	if (MI) return MI;
	FString S = Path; int32 Dot;
	if (S.FindLastChar(TEXT('.'), Dot)) S = S.Left(Dot);
	return LoadObject<UMaterialInstance>(nullptr, *S);
}

static UMaterialFunctionInterface* LoadLayerFunction(const FString& Path)
{
	if (Path.IsEmpty()) return nullptr;
	UMaterialFunctionInterface* F = LoadObject<UMaterialFunctionInterface>(nullptr, *Path);
	if (F) return F;
	FString S = Path; int32 Dot;
	if (S.FindLastChar(TEXT('.'), Dot)) S = S.Left(Dot);
	return LoadObject<UMaterialFunctionInterface>(nullptr, *S);
}

static void SaveAsset(UObject* Asset)
{
	if (!Asset) return;
	UPackage* Pkg = Asset->GetOutermost();
	Pkg->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(
		Pkg->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Pkg, Asset, *Filename, Args);
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// mat_layer_get_stack
	// ================================================================
	MCP_TOOL(Registry, "mat_layer_get_stack")
		.Description(TEXT(
			"Return the material layer stack of a material instance: arrays of layer / blend "
			"function asset paths. Index 0 of layers is the base layer (no matching blend)."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("material_instance"), TEXT("Material Instance asset path."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString MIPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("material_instance"), MIPath));

			UMaterialInstance* MI = LoadMaterialInstance(MIPath);
			if (!MI) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Material instance not found: %s"), *MIPath));

			FMaterialLayersFunctions Stack;
			const bool bHave = MI->GetMaterialLayers(Stack);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("material_instance"), MIPath);
			R->SetBoolField(TEXT("has_layers"), bHave);
			TArray<TSharedPtr<FJsonValue>> Layers, Blends;
			for (const TObjectPtr<UMaterialFunctionInterface>& L : Stack.Layers)
			{
				Layers.Add(MakeShared<FJsonValueString>(L ? L->GetPathName() : FString()));
			}
			for (const TObjectPtr<UMaterialFunctionInterface>& B : Stack.Blends)
			{
				Blends.Add(MakeShared<FJsonValueString>(B ? B->GetPathName() : FString()));
			}
			R->SetArrayField(TEXT("layers"), Layers);
			R->SetArrayField(TEXT("blends"), Blends);
			R->SetNumberField(TEXT("layer_count"), Layers.Num());
			R->SetNumberField(TEXT("blend_count"), Blends.Num());
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d layer(s), %d blend(s)"), Layers.Num(), Blends.Num()), R);
		});

#if WITH_EDITOR
	// ================================================================
	// mat_layer_add
	// ================================================================
	MCP_TOOL(Registry, "mat_layer_add")
		.Description(TEXT(
			"Append a layer (and optional matching blend) onto a material instance's layer stack. "
			"layer_path is a UMaterialFunction asset; blend_path is required for non-base layers."))
		.StringArg(TEXT("material_instance"), TEXT("Material Instance asset path."), true)
		.StringArg(TEXT("layer_path"), TEXT("UMaterialFunctionInterface asset path for the new layer."), true)
		.StringArg(TEXT("blend_path"), TEXT("Optional matching blend function path."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString MIPath, LayerPath, BlendPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("material_instance"), MIPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("layer_path"), LayerPath));
			Args->TryGetStringField(TEXT("blend_path"), BlendPath);

			UMaterialInstance* MI = LoadMaterialInstance(MIPath);
			if (!MI) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Material instance not found: %s"), *MIPath));

			UMaterialFunctionInterface* LayerFn = LoadLayerFunction(LayerPath);
			if (!LayerFn) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Layer function not found: %s"), *LayerPath));

			UMaterialFunctionInterface* BlendFn = LoadLayerFunction(BlendPath);

			FMaterialLayersFunctions Stack;
			MI->GetMaterialLayers(Stack);
			Stack.Layers.Add(LayerFn);
			if (Stack.Layers.Num() > 1)
			{
				Stack.Blends.Add(BlendFn); // may be nullptr; UE editor handles defaults
			}

			MI->Modify();
			const bool bOk = MI->SetMaterialLayers(Stack);
			SaveAsset(MI);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("ok"), bOk);
			R->SetNumberField(TEXT("layer_count"), Stack.Layers.Num());
			R->SetNumberField(TEXT("blend_count"), Stack.Blends.Num());
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Layer appended (%d total)"), Stack.Layers.Num()), R);
		});

	// ================================================================
	// mat_layer_remove
	// ================================================================
	MCP_TOOL(Registry, "mat_layer_remove")
		.Description(TEXT(
			"Remove a layer (and its matching blend) from a material instance's layer stack at an index. "
			"Index 0 is the base layer; removing it shifts the next layer down."))
		.Destructive()
		.StringArg(TEXT("material_instance"), TEXT("Material Instance asset path."), true)
		.IntArg(TEXT("index"), TEXT("Layer index to remove (0-based)."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString MIPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("material_instance"), MIPath));
			int32 Index = (int32)Args->GetNumberField(TEXT("index"));

			UMaterialInstance* MI = LoadMaterialInstance(MIPath);
			if (!MI) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Material instance not found: %s"), *MIPath));

			FMaterialLayersFunctions Stack;
			MI->GetMaterialLayers(Stack);
			BAIL_IF_INVALID(FMCPValidate::InRangeI(Index, 0, Stack.Layers.Num() - 1, TEXT("index")));

			MI->Modify();
			Stack.Layers.RemoveAt(Index);
			// Blends array is offset: Blends[i] sits between Layers[i] and Layers[i+1].
			// Remove the corresponding blend if it exists.
			const int32 BlendIdx = FMath::Max(Index - 1, 0);
			if (Stack.Blends.IsValidIndex(BlendIdx)) Stack.Blends.RemoveAt(BlendIdx);

			const bool bOk = MI->SetMaterialLayers(Stack);
			SaveAsset(MI);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("ok"), bOk);
			R->SetNumberField(TEXT("layer_count"), Stack.Layers.Num());
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Layer removed (now %d)"), Stack.Layers.Num()), R);
		});

	// ================================================================
	// mat_layer_set_blend
	// ================================================================
	MCP_TOOL(Registry, "mat_layer_set_blend")
		.Description(TEXT(
			"Replace the blend function at a given blend-index. Pass blend_path='' to clear the slot."))
		.StringArg(TEXT("material_instance"), TEXT("Material Instance asset path."), true)
		.IntArg(TEXT("blend_index"), TEXT("Blend index (0-based)."), true)
		.StringArg(TEXT("blend_path"), TEXT("UMaterialFunctionInterface asset path or '' to clear."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString MIPath, BlendPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("material_instance"), MIPath));
			Args->TryGetStringField(TEXT("blend_path"), BlendPath);
			int32 Idx = (int32)Args->GetNumberField(TEXT("blend_index"));

			UMaterialInstance* MI = LoadMaterialInstance(MIPath);
			if (!MI) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Material instance not found: %s"), *MIPath));

			FMaterialLayersFunctions Stack;
			MI->GetMaterialLayers(Stack);
			BAIL_IF_INVALID(FMCPValidate::InRangeI(Idx, 0, FMath::Max(0, Stack.Blends.Num() - 1), TEXT("blend_index")));

			UMaterialFunctionInterface* BlendFn = LoadLayerFunction(BlendPath);
			MI->Modify();
			Stack.Blends[Idx] = BlendFn;

			const bool bOk = MI->SetMaterialLayers(Stack);
			SaveAsset(MI);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("ok"), bOk);
			R->SetNumberField(TEXT("blend_index"), Idx);
			R->SetStringField(TEXT("blend_path"), BlendFn ? BlendFn->GetPathName() : FString());
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Blend %d updated"), Idx), R);
		});
#else
	// In non-editor builds, register a stub so tools/list is consistent.
	auto Stub = [&Registry](const FString& Name, const FString& Desc, TFunction<void(FMCPToolBuilder&)> Schema)
	{
		FMCPToolBuilder B(Registry, Name);
		B.Description(Desc);
		Schema(B);
		B.Handle([](const TSharedPtr<FJsonObject>&) -> FMCPToolResult
		{
			return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
				TEXT("Material layer mutation is editor-only"));
		});
	};
	Stub(TEXT("mat_layer_add"), TEXT("Append a material layer."),
		[](FMCPToolBuilder& B){ B.StringArg(TEXT("material_instance"), TEXT(""), true);
			B.StringArg(TEXT("layer_path"), TEXT(""), true); B.StringArg(TEXT("blend_path"), TEXT("")); });
	Stub(TEXT("mat_layer_remove"), TEXT("Remove a material layer."),
		[](FMCPToolBuilder& B){ B.StringArg(TEXT("material_instance"), TEXT(""), true);
			B.IntArg(TEXT("index"), TEXT(""), true); });
	Stub(TEXT("mat_layer_set_blend"), TEXT("Set a layer blend function."),
		[](FMCPToolBuilder& B){ B.StringArg(TEXT("material_instance"), TEXT(""), true);
			B.IntArg(TEXT("blend_index"), TEXT(""), true); B.StringArg(TEXT("blend_path"), TEXT(""), true); });
#endif
}

} // namespace MCPMaterialLayerTools
