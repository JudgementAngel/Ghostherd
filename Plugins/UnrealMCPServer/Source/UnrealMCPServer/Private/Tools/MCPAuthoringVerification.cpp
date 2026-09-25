// Copyright StraySpark Studio 2026. All Rights Reserved.
// v5 increment 19: authoring verification (V5-25 PCG slice, V5-26, V5-27 preflight slice).
// Every tool here is non-mutating: it loads or finds assets, reads structure and engine
// compile state, and reports structured issues. Nothing is compiled, imported or saved.

#include "Tools/MCPAuthoringVerification.h"
#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/Factory.h"
#include "UObject/UObjectIterator.h"

#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#include "PCGSettings.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "MaterialShared.h"
#include "RHI.h"

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"

#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeWavePlayer.h"

#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"

namespace MCPAuthoringVerification
{
namespace
{
void Issue(TArray<TSharedPtr<FJsonValue>>& Issues, TMap<FString, int32>& Counts, const FString& Rule, const FString& Severity, const FString& Where, const FString& Message)
{
	auto I = MakeShared<FJsonObject>();
	I->SetStringField(TEXT("rule"), Rule); I->SetStringField(TEXT("severity"), Severity);
	if (!Where.IsEmpty()) I->SetStringField(TEXT("where"), Where);
	I->SetStringField(TEXT("message"), Message);
	Issues.Add(MakeShared<FJsonValueObject>(I));
	Counts.FindOrAdd(Rule)++;
}

void FinishReport(const TSharedPtr<FJsonObject>& Out, TArray<TSharedPtr<FJsonValue>>& Issues, const TMap<FString, int32>& Counts, int32 Limit)
{
	int32 Errors = 0, Warnings = 0;
	for (const auto& V : Issues)
	{
		const FString S = V->AsObject()->GetStringField(TEXT("severity"));
		if (S == TEXT("error")) ++Errors; else if (S == TEXT("warning")) ++Warnings;
	}
	const int32 Total = Issues.Num();
	if (Issues.Num() > Limit) Issues.SetNum(Limit);
	Out->SetArrayField(TEXT("issues"), Issues);
	Out->SetNumberField(TEXT("issue_count"), Total);
	Out->SetBoolField(TEXT("issues_truncated"), Total > Limit);
	Out->SetNumberField(TEXT("error_count"), Errors);
	Out->SetNumberField(TEXT("warning_count"), Warnings);
	Out->SetBoolField(TEXT("ok"), Errors == 0);
	auto C = MakeShared<FJsonObject>(); for (const auto& P : Counts) C->SetNumberField(P.Key, P.Value);
	Out->SetObjectField(TEXT("counts"), C);
	Out->SetBoolField(TEXT("deterministic"), true);
	Out->SetBoolField(TEXT("mutated"), false);
}

/** Finds an in-memory object first (transient fixtures) and only then loads from disk. */
UObject* FindOrLoad(const FString& InPath)
{
	FString P = InPath.TrimStartAndEnd();
	if (P.IsEmpty()) return nullptr;
	if (UObject* Found = FindObject<UObject>(nullptr, *P)) return Found;
	if (!P.Contains(TEXT("."))) P = FString::Printf(TEXT("%s.%s"), *P, *FPackageName::GetShortName(P));
	if (UObject* Found = FindObject<UObject>(nullptr, *P)) return Found;
	if (!FPackageName::IsValidObjectPath(P)) return nullptr;
	return LoadObject<UObject>(nullptr, *P);
}

FMCPToolResult NotFoundResult(const FString& What, const FString& Path)
{
	return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("%s not found: %s"), *What, *Path), TEXT("Use search_assets or list tools to find the exact object path."));
}

int32 LimitArg(const TSharedPtr<FJsonObject>& Args, int32 Default = 200)
{
	return Args->HasField(TEXT("limit")) ? FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 1000) : Default;
}

UWorld* ResolveWorld(const TSharedPtr<FJsonObject>& Args, FString& OutError)
{
	FString WorldId; Args->TryGetStringField(TEXT("world_id"), WorldId);
	if (!WorldId.IsEmpty())
	{
		if (GEngine) for (const FWorldContext& Ctx : GEngine->GetWorldContexts()) if (Ctx.World() && Ctx.World()->GetPathName() == WorldId) return Ctx.World();
		OutError = TEXT("World not found: ") + WorldId; return nullptr;
	}
	UWorld* W = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!W) OutError = TEXT("No editor world available");
	return W;
}
} // namespace

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ------------------------------------------------------------------
	// validate_pcg_graph (V5-25)
	// ------------------------------------------------------------------
	MCP_TOOL(Registry, "validate_pcg_graph")
		.Description(TEXT("Validate a PCG Graph asset's structure without executing it. Reports per node: title, settings class, enabled state, input pins (label, required, connected) and output pins. Rules: missing_settings, disabled_node, required_input_unconnected, dead_end_node (a non-output node whose outputs are all unconnected), output_node_unconnected (graph output receives nothing), dangling_edge (edge whose pins or nodes are invalid), no_input_node/no_output_node. Issues carry rule, severity, where and message; counts per rule; ok=false when any error. Nothing is generated, compiled or saved. Use execute_pcg to generate afterwards."))
		.ReadOnly().Idempotent()
		.StringArg(TEXT("graph_path"), TEXT("PCG Graph asset path"), true)
		.BoolArg(TEXT("include_nodes"), TEXT("Include the per-node structure (default true)"))
		.IntArg(TEXT("limit"), TEXT("Maximum issues returned (default 200, max 1000)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path; if (!Args->TryGetStringField(TEXT("graph_path"), Path) || Path.IsEmpty()) return FMCPToolResult::Error(TEXT("graph_path is required"));
			UPCGGraph* Graph = Cast<UPCGGraph>(FindOrLoad(Path));
			if (!Graph) return NotFoundResult(TEXT("PCG graph"), Path);
			const bool bNodes = !Args->HasField(TEXT("include_nodes")) || Args->GetBoolField(TEXT("include_nodes"));
			TArray<TSharedPtr<FJsonValue>> Issues, NodeArr; TMap<FString, int32> Counts;
			const TArray<UPCGNode*>& Nodes = Graph->GetNodes();
			const UPCGNode* InputNode = Graph->GetInputNode();
			const UPCGNode* OutputNode = Graph->GetOutputNode();
			if (!InputNode) Issue(Issues, Counts, TEXT("no_input_node"), TEXT("error"), TEXT(""), TEXT("Graph has no input node"));
			if (!OutputNode) Issue(Issues, Counts, TEXT("no_output_node"), TEXT("error"), TEXT(""), TEXT("Graph has no output node"));
			TSet<const UPCGNode*> Known; for (UPCGNode* N : Nodes) if (N) Known.Add(N);
			if (InputNode) Known.Add(InputNode); if (OutputNode) Known.Add(OutputNode);
			int32 EdgeCount = 0;
			auto Describe = [&](const UPCGNode* Node, int32 Index, const FString& Role)
			{
				if (!Node) return;
				const FString Title = Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString();
				const FString Where = Index >= 0 ? FString::Printf(TEXT("node[%d] %s"), Index, *Title) : FString::Printf(TEXT("%s node"), *Role);
				const UPCGSettings* Settings = Node->GetSettings();
				auto NJ = MakeShared<FJsonObject>();
				if (Index >= 0) NJ->SetNumberField(TEXT("node_index"), Index);
				NJ->SetStringField(TEXT("role"), Role); NJ->SetStringField(TEXT("title"), Title);
				NJ->SetStringField(TEXT("settings_class"), Settings ? Settings->GetClass()->GetName() : TEXT("(none)"));
				const bool bEnabled = Settings ? Settings->bEnabled : true;
				NJ->SetBoolField(TEXT("enabled"), bEnabled);
				if (Role == TEXT("regular") && !Settings) Issue(Issues, Counts, TEXT("missing_settings"), TEXT("error"), Where, TEXT("Node has no settings object"));
				if (!bEnabled) Issue(Issues, Counts, TEXT("disabled_node"), TEXT("info"), Where, TEXT("Node is disabled and will be skipped"));
				TArray<TSharedPtr<FJsonValue>> Ins, Outs; int32 ConnectedOut = 0, ConnectedIn = 0;
				for (const UPCGPin* Pin : Node->GetInputPins())
				{
					if (!Pin) continue;
					auto PJ = MakeShared<FJsonObject>();
					PJ->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
					PJ->SetBoolField(TEXT("required"), Pin->Properties.IsRequiredPin());
					PJ->SetBoolField(TEXT("connected"), Pin->IsConnected());
					if (Pin->IsConnected()) ++ConnectedIn;
					for (const UPCGEdge* E : Pin->Edges)
					{
						++EdgeCount;
						if (!E || !E->InputPin || !E->OutputPin || !E->InputPin->Node || !E->OutputPin->Node || !Known.Contains(E->InputPin->Node) || !Known.Contains(E->OutputPin->Node))
							Issue(Issues, Counts, TEXT("dangling_edge"), TEXT("error"), Where, FString::Printf(TEXT("Edge into pin '%s' references an invalid pin or node"), *Pin->Properties.Label.ToString()));
					}
					if (Pin->Properties.IsRequiredPin() && !Pin->IsConnected() && bEnabled && Role == TEXT("regular"))
						Issue(Issues, Counts, TEXT("required_input_unconnected"), TEXT("error"), Where, FString::Printf(TEXT("Required input pin '%s' is not connected"), *Pin->Properties.Label.ToString()));
					Ins.Add(MakeShared<FJsonValueObject>(PJ));
				}
				for (const UPCGPin* Pin : Node->GetOutputPins())
				{
					if (!Pin) continue;
					auto PJ = MakeShared<FJsonObject>();
					PJ->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
					PJ->SetBoolField(TEXT("connected"), Pin->IsConnected());
					if (Pin->IsConnected()) ++ConnectedOut;
					Outs.Add(MakeShared<FJsonValueObject>(PJ));
				}
				if (Role == TEXT("regular") && bEnabled && Node->GetOutputPins().Num() > 0 && ConnectedOut == 0)
					Issue(Issues, Counts, TEXT("dead_end_node"), TEXT("warning"), Where, TEXT("No output pin is connected; the node's results are discarded"));
				if (Role == TEXT("output") && ConnectedIn == 0)
					Issue(Issues, Counts, TEXT("output_node_unconnected"), TEXT("warning"), Where, TEXT("Graph output receives no data"));
				NJ->SetArrayField(TEXT("input_pins"), Ins); NJ->SetArrayField(TEXT("output_pins"), Outs);
				if (bNodes) NodeArr.Add(MakeShared<FJsonValueObject>(NJ));
			};
			Describe(InputNode, -1, TEXT("input"));
			for (int32 i = 0; i < Nodes.Num(); ++i) if (Nodes[i] && Nodes[i] != InputNode && Nodes[i] != OutputNode) Describe(Nodes[i], i, TEXT("regular"));
			Describe(OutputNode, -1, TEXT("output"));
			auto Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("graph_path"), Graph->GetPathName());
			Out->SetNumberField(TEXT("node_count"), Nodes.Num());
			Out->SetNumberField(TEXT("edge_count"), EdgeCount);
			if (bNodes) Out->SetArrayField(TEXT("nodes"), NodeArr);
			FinishReport(Out, Issues, Counts, LimitArg(Args));
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("PCG graph %s: %d node(s), %d issue(s)"), *Graph->GetName(), Nodes.Num(), (int32)Out->GetNumberField(TEXT("issue_count"))), Out);
		});

	// ------------------------------------------------------------------
	// preview_asset_import (V5-27)
	// ------------------------------------------------------------------
	MCP_TOOL(Registry, "preview_asset_import")
		.Description(TEXT("Preflight an asset import without importing anything. Resolves the source file (absolute path required, existence, size), detects the asset type from the extension and lists the editor factories that accept the file, validates the destination package path (mounted root, no traversal, protected roots such as /Engine are refused), computes the expected asset name and object path, checks for a destination collision and evaluates collision_policy (error, skip, replace; replace reports existing referencers). Rules: source_relative_path, source_missing, source_empty, unsupported_extension, no_factory, invalid_destination, protected_destination, invalid_asset_name, destination_collision (severity depends on policy). importable=true when no error remains. Nothing is read beyond file metadata; nothing is created."))
		.ReadOnly().Idempotent()
		.StringArg(TEXT("source_path"), TEXT("Absolute filesystem path of the file to import"), true)
		.StringArg(TEXT("destination_path"), TEXT("Destination content folder, e.g. /Game/Imports"), true)
		.StringArg(TEXT("asset_name"), TEXT("Asset name override (default: sanitised file base name)"))
		.EnumArg(TEXT("collision_policy"), TEXT("What the real import should do if the destination exists: error (default), skip, replace"), { TEXT("error"), TEXT("skip"), TEXT("replace") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Source, Dest; Args->TryGetStringField(TEXT("source_path"), Source); Args->TryGetStringField(TEXT("destination_path"), Dest);
			if (Source.IsEmpty() || Dest.IsEmpty()) return FMCPToolResult::Error(TEXT("source_path and destination_path are required"));
			const FString Policy = Args->HasField(TEXT("collision_policy")) ? Args->GetStringField(TEXT("collision_policy")) : TEXT("error");
			TArray<TSharedPtr<FJsonValue>> Issues; TMap<FString, int32> Counts;
			auto Out = MakeShared<FJsonObject>();

			// Source
			FString Normalized = Source; FPaths::NormalizeFilename(Normalized);
			Out->SetStringField(TEXT("source_path"), Normalized);
			if (FPaths::IsRelative(Normalized)) Issue(Issues, Counts, TEXT("source_relative_path"), TEXT("error"), TEXT("source_path"), TEXT("Source must be an absolute path; relative paths are resolved against an unspecified working directory"));
			const bool bExists = !FPaths::IsRelative(Normalized) && IFileManager::Get().FileExists(*Normalized);
			Out->SetBoolField(TEXT("source_exists"), bExists);
			if (!bExists && !FPaths::IsRelative(Normalized)) Issue(Issues, Counts, TEXT("source_missing"), TEXT("error"), TEXT("source_path"), TEXT("Source file does not exist or is not readable by the editor process"));
			if (bExists)
			{
				const int64 Size = IFileManager::Get().FileSize(*Normalized);
				Out->SetNumberField(TEXT("source_size_bytes"), (double)Size);
				if (Size <= 0) Issue(Issues, Counts, TEXT("source_empty"), TEXT("error"), TEXT("source_path"), TEXT("Source file is empty"));
			}
			const FString Ext = FPaths::GetExtension(Normalized).ToLower();
			Out->SetStringField(TEXT("extension"), Ext);
			static const TMap<FString, FString> TypeByExt = {
				{TEXT("fbx"), TEXT("static_mesh_or_skeletal_mesh")}, {TEXT("obj"), TEXT("static_mesh")}, {TEXT("gltf"), TEXT("static_mesh_or_skeletal_mesh")}, {TEXT("glb"), TEXT("static_mesh_or_skeletal_mesh")},
				{TEXT("png"), TEXT("texture2d")}, {TEXT("jpg"), TEXT("texture2d")}, {TEXT("jpeg"), TEXT("texture2d")}, {TEXT("tga"), TEXT("texture2d")}, {TEXT("bmp"), TEXT("texture2d")}, {TEXT("exr"), TEXT("texture2d")}, {TEXT("hdr"), TEXT("texture_cube")}, {TEXT("psd"), TEXT("texture2d")}, {TEXT("dds"), TEXT("texture2d")},
				{TEXT("wav"), TEXT("sound_wave")}, {TEXT("ogg"), TEXT("sound_wave")}, {TEXT("flac"), TEXT("sound_wave")}, {TEXT("aif"), TEXT("sound_wave")}, {TEXT("aiff"), TEXT("sound_wave")},
				{TEXT("csv"), TEXT("data_table_or_curve")}, {TEXT("json"), TEXT("data_table_or_curve")}, {TEXT("abc"), TEXT("alembic_geometry_cache")}, {TEXT("usd"), TEXT("usd_stage")}, {TEXT("usda"), TEXT("usd_stage")}, {TEXT("usdc"), TEXT("usd_stage")}, {TEXT("ttf"), TEXT("font_face")}, {TEXT("otf"), TEXT("font_face")}, {TEXT("mp4"), TEXT("file_media_source")}, {TEXT("srt"), TEXT("unsupported")} };
			const FString* Detected = TypeByExt.Find(Ext);
			Out->SetStringField(TEXT("detected_type"), Detected ? *Detected : TEXT("unknown"));
			if (!Detected || *Detected == TEXT("unsupported")) Issue(Issues, Counts, TEXT("unsupported_extension"), TEXT("warning"), TEXT("source_path"), TEXT("Extension is not in the known import table; factory detection decides"));
			// Factories that accept the file (extension based; no file content is read).
			TArray<TSharedPtr<FJsonValue>> Factories;
			if (!Ext.IsEmpty())
			{
				const FString Probe = FPaths::IsRelative(Normalized) ? FString::Printf(TEXT("/probe.%s"), *Ext) : Normalized;
				for (TObjectIterator<UClass> It; It; ++It)
				{
					if (!It->IsChildOf(UFactory::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)) continue;
					UFactory* F = It->GetDefaultObject<UFactory>();
					if (!F || !F->bEditorImport) continue;
					TArray<FString> Exts; F->GetSupportedFileExtensions(Exts);
					if (Exts.Contains(Ext)) { auto FJ = MakeShared<FJsonObject>(); FJ->SetStringField(TEXT("factory"), It->GetName()); FJ->SetStringField(TEXT("creates"), F->GetSupportedClass() ? F->GetSupportedClass()->GetName() : TEXT("(unknown)")); Factories.Add(MakeShared<FJsonValueObject>(FJ)); }
				}
			}
			Out->SetArrayField(TEXT("factories"), Factories);
			if (Factories.Num() == 0) Issue(Issues, Counts, TEXT("no_factory"), TEXT("error"), TEXT("source_path"), TEXT("No enabled editor import factory accepts this extension; the required plugin may be disabled"));

			// Destination
			FString Folder = Dest.TrimStartAndEnd(); Folder.RemoveFromEnd(TEXT("/"));
			Folder.ReplaceInline(TEXT("\\"), TEXT("/"));
			bool bDestOk = true;
			if (Folder.Contains(TEXT("..")) || Folder.Contains(TEXT("//")) || Folder.Contains(TEXT(":"))) { bDestOk = false; Issue(Issues, Counts, TEXT("invalid_destination"), TEXT("error"), TEXT("destination_path"), TEXT("Destination contains path traversal or a filesystem path; use a content path like /Game/Folder")); }
			else if (!FMCPValidate::PackagePath(Folder).bOk) { bDestOk = false; Issue(Issues, Counts, TEXT("invalid_destination"), TEXT("error"), TEXT("destination_path"), TEXT("Destination is not a valid mounted long package path")); }
			if (bDestOk && (Folder.StartsWith(TEXT("/Engine")) || Folder.StartsWith(TEXT("/Script")) || Folder.StartsWith(TEXT("/Temp")))) Issue(Issues, Counts, TEXT("protected_destination"), TEXT("error"), TEXT("destination_path"), TEXT("Imports into engine, script or temporary roots are refused"));
			FString Name; Args->TryGetStringField(TEXT("asset_name"), Name);
			if (Name.IsEmpty()) { Name = FPaths::GetBaseFilename(Normalized); for (TCHAR& C : Name) if (!FChar::IsAlnum(C) && C != TEXT('_')) C = TEXT('_'); }
			if (!FMCPValidate::AssetName(Name).bOk) Issue(Issues, Counts, TEXT("invalid_asset_name"), TEXT("error"), TEXT("asset_name"), TEXT("Asset name has invalid characters"));
			const FString PackagePath = bDestOk ? FString::Printf(TEXT("%s/%s"), *Folder, *Name) : FString();
			const FString ObjectPath = bDestOk ? FString::Printf(TEXT("%s.%s"), *PackagePath, *Name) : FString();
			auto Expected = MakeShared<FJsonObject>();
			Expected->SetStringField(TEXT("asset_name"), Name); Expected->SetStringField(TEXT("package_path"), PackagePath); Expected->SetStringField(TEXT("object_path"), ObjectPath);
			Out->SetObjectField(TEXT("expected_output"), Expected);
			// Collision
			bool bCollision = false; FString ExistingClass; int32 Referencers = 0;
			if (bDestOk)
			{
				const FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				const FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
				if (AD.IsValid()) { bCollision = true; ExistingClass = AD.AssetClassPath.GetAssetName().ToString(); TArray<FName> Refs; ARM.Get().GetReferencers(FName(*PackagePath), Refs); Referencers = Refs.Num(); }
				else if (FPackageName::DoesPackageExist(PackagePath)) { bCollision = true; ExistingClass = TEXT("(on disk, not registered)"); }
				else if (FindObject<UObject>(nullptr, *ObjectPath)) { bCollision = true; ExistingClass = TEXT("(in memory)"); }
			}
			auto Col = MakeShared<FJsonObject>();
			Col->SetBoolField(TEXT("exists"), bCollision); Col->SetStringField(TEXT("policy"), Policy);
			if (bCollision)
			{
				Col->SetStringField(TEXT("existing_class"), ExistingClass); Col->SetNumberField(TEXT("existing_referencers"), Referencers);
				const FString Outcome = Policy == TEXT("replace") ? TEXT("would_replace") : Policy == TEXT("skip") ? TEXT("would_skip") : TEXT("would_fail");
				Col->SetStringField(TEXT("outcome"), Outcome);
				Issue(Issues, Counts, TEXT("destination_collision"), Policy == TEXT("error") ? TEXT("error") : TEXT("warning"), TEXT("destination_path"),
					FString::Printf(TEXT("%s already exists (%s, %d referencer package(s)); policy '%s' → %s"), *ObjectPath, *ExistingClass, Referencers, *Policy, *Outcome));
			}
			else Col->SetStringField(TEXT("outcome"), TEXT("would_create"));
			Out->SetObjectField(TEXT("collision"), Col);
			FinishReport(Out, Issues, Counts, LimitArg(Args));
			Out->SetBoolField(TEXT("importable"), Out->GetBoolField(TEXT("ok")));
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Import preflight: %s → %s (%s)"), *Normalized, *ObjectPath, Out->GetBoolField(TEXT("importable")) ? TEXT("importable") : TEXT("blocked")), Out);
		});

	// ------------------------------------------------------------------
	// validate_material_setup (V5-26)
	// ------------------------------------------------------------------
	MCP_TOOL(Registry, "validate_material_setup")
		.Description(TEXT("Validate a Material or Material Instance without compiling or saving. For a Material: domain, blend mode, shading models, substrate flag, expression count, connected material outputs, compile state for the current shader platform (compilation_finished, compile_errors[]), and rules texture_sample_without_texture, unused_expression (no output used and not wired to a material output), no_material_output_connected, compile_error. For a Material Instance: parent chain (missing_parent, parent_chain), override counts and the root material's compile errors. Issues carry rule, severity, where, message; ok=false on any error."))
		.ReadOnly().Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Material or Material Instance asset path"), true)
		.IntArg(TEXT("limit"), TEXT("Maximum issues returned (default 200)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path; Args->TryGetStringField(TEXT("asset_path"), Path);
			UObject* Obj = FindOrLoad(Path);
			UMaterialInterface* MI = Cast<UMaterialInterface>(Obj);
			if (!MI) return NotFoundResult(TEXT("Material or Material Instance"), Path);
			TArray<TSharedPtr<FJsonValue>> Issues; TMap<FString, int32> Counts;
			auto Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("asset_path"), MI->GetPathName());
			Out->SetStringField(TEXT("class"), MI->GetClass()->GetName());
			UMaterial* Root = nullptr;
			if (UMaterialInstance* Inst = Cast<UMaterialInstance>(MI))
			{
				TArray<TSharedPtr<FJsonValue>> Chain; UMaterialInterface* Cur = Inst->Parent; int32 Depth = 0;
				while (Cur && Depth++ < 32) { Chain.Add(MakeShared<FJsonValueString>(Cur->GetPathName())); if (UMaterialInstance* P = Cast<UMaterialInstance>(Cur)) Cur = P->Parent; else { Root = Cast<UMaterial>(Cur); break; } }
				Out->SetArrayField(TEXT("parent_chain"), Chain);
				if (!Inst->Parent) Issue(Issues, Counts, TEXT("missing_parent"), TEXT("error"), TEXT("Parent"), TEXT("Material Instance has no parent material"));
				else if (!Root) Issue(Issues, Counts, TEXT("missing_parent"), TEXT("error"), TEXT("Parent"), TEXT("Parent chain does not end in a Material"));
				auto Ov = MakeShared<FJsonObject>();
				Ov->SetNumberField(TEXT("scalar"), Inst->ScalarParameterValues.Num()); Ov->SetNumberField(TEXT("vector"), Inst->VectorParameterValues.Num());
				Ov->SetNumberField(TEXT("texture"), Inst->TextureParameterValues.Num()); Ov->SetNumberField(TEXT("static_switch"), Inst->GetStaticParameters().StaticSwitchParameters.Num());
				Out->SetObjectField(TEXT("overrides"), Ov);
				for (const FTextureParameterValue& T : Inst->TextureParameterValues) if (!T.ParameterValue) Issue(Issues, Counts, TEXT("texture_override_null"), TEXT("warning"), T.ParameterInfo.Name.ToString(), TEXT("Texture parameter override is null"));
			}
			else Root = Cast<UMaterial>(MI);
			if (Root)
			{
				Out->SetStringField(TEXT("root_material"), Root->GetPathName());
				Out->SetStringField(TEXT("domain"), StaticEnum<EMaterialDomain>()->GetNameStringByValue((int64)Root->MaterialDomain.GetValue()));
				Out->SetStringField(TEXT("blend_mode"), StaticEnum<EBlendMode>()->GetNameStringByValue((int64)Root->GetBlendMode()));
				TArray<TSharedPtr<FJsonValue>> Models; const FMaterialShadingModelField SM = Root->GetShadingModels();
				for (int32 i = 0; i < MSM_NUM; ++i) if (SM.HasShadingModel((EMaterialShadingModel)i)) Models.Add(MakeShared<FJsonValueString>(StaticEnum<EMaterialShadingModel>()->GetNameStringByValue(i)));
				Out->SetArrayField(TEXT("shading_models"), Models);
				const TConstArrayView<TObjectPtr<UMaterialExpression>> Exprs = Root->GetExpressions();
				Out->SetNumberField(TEXT("expression_count"), Exprs.Num());
				TSet<const UMaterialExpression*> Used;
				for (UMaterialExpression* E : Exprs)
				{
					if (!E) continue;
					for (FExpressionInput* In : E->GetInputsView()) if (In && In->Expression) Used.Add(In->Expression);
					if (const UMaterialExpressionTextureBase* T = Cast<UMaterialExpressionTextureBase>(E)) if (!T->Texture)
						Issue(Issues, Counts, TEXT("texture_sample_without_texture"), TEXT("error"), E->GetName(), TEXT("Texture expression has no texture assigned"));
				}
				TArray<FString> Connected; bool bSubstrate = false;
				if (UMaterialEditorOnlyData* ED = Root->GetEditorOnlyData())
				{
					auto Mark = [&](const TCHAR* Label, const UMaterialExpression* Ex) { if (Ex) { Connected.Add(Label); Used.Add(Ex); } };
					Mark(TEXT("BaseColor"), ED->BaseColor.Expression); Mark(TEXT("Metallic"), ED->Metallic.Expression); Mark(TEXT("Specular"), ED->Specular.Expression);
					Mark(TEXT("Roughness"), ED->Roughness.Expression); Mark(TEXT("Normal"), ED->Normal.Expression); Mark(TEXT("EmissiveColor"), ED->EmissiveColor.Expression);
					Mark(TEXT("Opacity"), ED->Opacity.Expression); Mark(TEXT("OpacityMask"), ED->OpacityMask.Expression); Mark(TEXT("WorldPositionOffset"), ED->WorldPositionOffset.Expression);
					Mark(TEXT("AmbientOcclusion"), ED->AmbientOcclusion.Expression); Mark(TEXT("Refraction"), ED->Refraction.Expression); Mark(TEXT("SubsurfaceColor"), ED->SubsurfaceColor.Expression);
					Mark(TEXT("Anisotropy"), ED->Anisotropy.Expression); Mark(TEXT("Tangent"), ED->Tangent.Expression);
					if (ED->FrontMaterial.Expression) { Connected.Add(TEXT("FrontMaterial")); Used.Add(ED->FrontMaterial.Expression); bSubstrate = true; }
				}
				TArray<TSharedPtr<FJsonValue>> CJ; for (const FString& C : Connected) CJ.Add(MakeShared<FJsonValueString>(C));
				Out->SetArrayField(TEXT("connected_outputs"), CJ);
				if (Connected.Num() == 0 && Exprs.Num() > 0) Issue(Issues, Counts, TEXT("no_material_output_connected"), TEXT("warning"), TEXT("Material outputs"), TEXT("No expression is wired to any material output; the material renders defaults"));
				for (UMaterialExpression* E : Exprs) if (E && !Used.Contains(E) && E->GetOutputs().Num() > 0)
					Issue(Issues, Counts, TEXT("unused_expression"), TEXT("info"), E->GetName(), FString::Printf(TEXT("%s output is not used"), *E->GetClass()->GetName()));
				bool bFinished = true; TArray<TSharedPtr<FJsonValue>> Errs; bool bResSubstrate = false;
				if (const FMaterialResource* Res = Root->GetMaterialResource(GMaxRHIShaderPlatform))
				{
					bFinished = Res->IsCompilationFinished(); bResSubstrate = Res->IsSubstrateMaterial();
					for (const FString& Er : Res->GetCompileErrors()) { Errs.Add(MakeShared<FJsonValueString>(Er)); Issue(Issues, Counts, TEXT("compile_error"), TEXT("error"), TEXT("Shader compile"), Er); }
				}
				Out->SetBoolField(TEXT("is_substrate"), bSubstrate || bResSubstrate);
				Out->SetBoolField(TEXT("compilation_finished"), bFinished);
				Out->SetArrayField(TEXT("compile_errors"), Errs);
				Out->SetStringField(TEXT("compile_state_note"), bFinished ? TEXT("Compile errors reflect the last compilation for the current shader platform; call compile_material after graph edits to refresh them.") : TEXT("Shader compilation is still running; compile errors may be incomplete."));
			}
			FinishReport(Out, Issues, Counts, LimitArg(Args));
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%s: %d issue(s), ok=%s"), *MI->GetName(), (int32)Out->GetNumberField(TEXT("issue_count")), Out->GetBoolField(TEXT("ok")) ? TEXT("true") : TEXT("false")), Out);
		});

	// ------------------------------------------------------------------
	// validate_niagara_system (V5-26)
	// ------------------------------------------------------------------
	MCP_TOOL(Registry, "validate_niagara_system")
		.Description(TEXT("Validate a Niagara System asset without compiling, spawning or saving. Reports system validity, emitters (name, enabled, sim_target, spawn/update script compile status), system script compile status, exposed user parameters and rules: system_invalid, no_emitters, emitter_disabled (info), missing_emitter (handle without emitter asset), script_not_compiled (status other than UpToDate). Unavailable Niagara plugin returns an unsupported error rather than fabricated output."))
		.ReadOnly().Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Niagara System asset path"), true)
		.IntArg(TEXT("limit"), TEXT("Maximum issues returned (default 200)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			if (!FModuleManager::Get().IsModuleLoaded(TEXT("Niagara"))) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("Niagara module is not loaded"), TEXT("Enable the Niagara plugin."));
			FString Path; Args->TryGetStringField(TEXT("asset_path"), Path);
			UNiagaraSystem* Sys = Cast<UNiagaraSystem>(FindOrLoad(Path));
			if (!Sys) return NotFoundResult(TEXT("Niagara System"), Path);
			TArray<TSharedPtr<FJsonValue>> Issues; TMap<FString, int32> Counts;
			auto Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("asset_path"), Sys->GetPathName());
			const bool bValid = Sys->IsValid();
			Out->SetBoolField(TEXT("system_valid"), bValid);
			if (!bValid) Issue(Issues, Counts, TEXT("system_invalid"), TEXT("error"), TEXT("System"), TEXT("UNiagaraSystem::IsValid() is false (scripts missing or not compiled)"));
			auto StatusName = [](const UNiagaraScript* S) -> FString { return S ? StaticEnum<ENiagaraScriptCompileStatus>()->GetNameStringByValue((int64)S->GetLastCompileStatus()) : TEXT("(no script)"); };
			auto CheckScript = [&](const TCHAR* Where, UNiagaraScript* S)
			{
				if (!S) { Issue(Issues, Counts, TEXT("script_missing"), TEXT("error"), Where, TEXT("Script is missing")); return; }
				if (S->GetLastCompileStatus() != ENiagaraScriptCompileStatus::NCS_UpToDate) Issue(Issues, Counts, TEXT("script_not_compiled"), TEXT("warning"), Where, FString::Printf(TEXT("Compile status is %s"), *StatusName(S)));
			};
			auto SysJ = MakeShared<FJsonObject>();
			SysJ->SetStringField(TEXT("spawn_script_status"), StatusName(Sys->GetSystemSpawnScript())); SysJ->SetStringField(TEXT("update_script_status"), StatusName(Sys->GetSystemUpdateScript()));
			CheckScript(TEXT("SystemSpawnScript"), Sys->GetSystemSpawnScript()); CheckScript(TEXT("SystemUpdateScript"), Sys->GetSystemUpdateScript());
			Out->SetObjectField(TEXT("system_scripts"), SysJ);
			TArray<TSharedPtr<FJsonValue>> Emitters; int32 Enabled = 0;
			for (const FNiagaraEmitterHandle& H : Sys->GetEmitterHandles())
			{
				auto EJ = MakeShared<FJsonObject>();
				const FString Name = H.GetName().ToString();
				EJ->SetStringField(TEXT("name"), Name); EJ->SetBoolField(TEXT("enabled"), H.GetIsEnabled());
				if (H.GetIsEnabled()) ++Enabled; else Issue(Issues, Counts, TEXT("emitter_disabled"), TEXT("info"), Name, TEXT("Emitter is disabled"));
				const FVersionedNiagaraEmitter Inst = H.GetInstance();
				const bool bStateless = H.GetStatelessEmitter() != nullptr;
				EJ->SetBoolField(TEXT("stateless"), bStateless);
				if (!Inst.Emitter && !bStateless) Issue(Issues, Counts, TEXT("missing_emitter"), TEXT("error"), Name, TEXT("Emitter handle has no emitter asset"));
				if (FVersionedNiagaraEmitterData* Data = H.GetEmitterData())
				{
					EJ->SetStringField(TEXT("sim_target"), StaticEnum<ENiagaraSimTarget>()->GetNameStringByValue((int64)Data->SimTarget));
					EJ->SetStringField(TEXT("spawn_script_status"), StatusName(Data->SpawnScriptProps.Script)); EJ->SetStringField(TEXT("update_script_status"), StatusName(Data->UpdateScriptProps.Script));
					if (H.GetIsEnabled()) { CheckScript(*FString::Printf(TEXT("%s.SpawnScript"), *Name), Data->SpawnScriptProps.Script); CheckScript(*FString::Printf(TEXT("%s.UpdateScript"), *Name), Data->UpdateScriptProps.Script); }
				}
				Emitters.Add(MakeShared<FJsonValueObject>(EJ));
			}
			Out->SetArrayField(TEXT("emitters"), Emitters);
			Out->SetNumberField(TEXT("emitter_count"), Emitters.Num()); Out->SetNumberField(TEXT("enabled_emitter_count"), Enabled);
			if (Emitters.Num() == 0) Issue(Issues, Counts, TEXT("no_emitters"), TEXT("warning"), TEXT("System"), TEXT("System has no emitters"));
			TArray<TSharedPtr<FJsonValue>> Params;
			for (const FNiagaraVariableWithOffset& V : Sys->GetExposedParameters().ReadParameterVariables())
			{ auto PJ = MakeShared<FJsonObject>(); PJ->SetStringField(TEXT("name"), V.GetName().ToString()); PJ->SetStringField(TEXT("type"), V.GetType().GetName()); Params.Add(MakeShared<FJsonValueObject>(PJ)); }
			Out->SetArrayField(TEXT("exposed_parameters"), Params);
			FinishReport(Out, Issues, Counts, LimitArg(Args));
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%s: %d emitter(s), %d issue(s)"), *Sys->GetName(), Emitters.Num(), (int32)Out->GetNumberField(TEXT("issue_count"))), Out);
		});

	// ------------------------------------------------------------------
	// validate_sound_setup (V5-26)
	// ------------------------------------------------------------------
	MCP_TOOL(Registry, "validate_sound_setup")
		.Description(TEXT("Validate a sound asset (SoundWave, SoundCue or MetaSound source) without playing or saving. SoundWave: duration, channels, sample rate, looping, streaming; rules zero_duration, no_channels. SoundCue: node count, first node, wave players; rules cue_without_first_node, wave_player_without_wave, cue_zero_duration. MetaSound: reported as metasound with class name (use metasound_get_graph for graph checks). Common: sound_class, attenuation, volume and pitch."))
		.ReadOnly().Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Sound asset path"), true)
		.IntArg(TEXT("limit"), TEXT("Maximum issues returned (default 200)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path; Args->TryGetStringField(TEXT("asset_path"), Path);
			USoundBase* Sound = Cast<USoundBase>(FindOrLoad(Path));
			if (!Sound) return NotFoundResult(TEXT("Sound asset"), Path);
			TArray<TSharedPtr<FJsonValue>> Issues; TMap<FString, int32> Counts;
			auto Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("asset_path"), Sound->GetPathName()); Out->SetStringField(TEXT("class"), Sound->GetClass()->GetName());
			Out->SetStringField(TEXT("sound_class"), Sound->GetSoundClass() ? Sound->GetSoundClass()->GetPathName() : TEXT(""));
			Out->SetBoolField(TEXT("has_attenuation"), Sound->AttenuationSettings != nullptr);
			Out->SetNumberField(TEXT("volume"), Sound->GetVolumeMultiplier()); Out->SetNumberField(TEXT("pitch"), Sound->GetPitchMultiplier());
			Out->SetNumberField(TEXT("duration"), Sound->GetDuration());
			if (USoundWave* W = Cast<USoundWave>(Sound))
			{
				Out->SetStringField(TEXT("kind"), TEXT("sound_wave"));
				Out->SetNumberField(TEXT("channels"), (double)W->NumChannels); Out->SetNumberField(TEXT("sample_rate"), W->GetSampleRateForCurrentPlatform());
				Out->SetBoolField(TEXT("looping"), W->bLooping != 0); Out->SetBoolField(TEXT("streaming"), W->bStreaming != 0);
				if (W->Duration <= 0.f) Issue(Issues, Counts, TEXT("zero_duration"), TEXT("error"), TEXT("Duration"), TEXT("Sound wave has zero duration (no imported audio data)"));
				if (W->NumChannels <= 0) Issue(Issues, Counts, TEXT("no_channels"), TEXT("error"), TEXT("NumChannels"), TEXT("Sound wave has no channels"));
			}
			else if (USoundCue* Cue = Cast<USoundCue>(Sound))
			{
				Out->SetStringField(TEXT("kind"), TEXT("sound_cue"));
				Out->SetNumberField(TEXT("node_count"), Cue->AllNodes.Num());
				Out->SetStringField(TEXT("first_node"), Cue->FirstNode ? Cue->FirstNode->GetClass()->GetName() : TEXT(""));
				if (!Cue->FirstNode) Issue(Issues, Counts, TEXT("cue_without_first_node"), TEXT("error"), TEXT("FirstNode"), TEXT("Sound cue output is not connected to any node"));
				int32 Players = 0;
				for (USoundNode* N : Cue->AllNodes) if (USoundNodeWavePlayer* P = Cast<USoundNodeWavePlayer>(N)) { ++Players; if (!P->GetSoundWave()) Issue(Issues, Counts, TEXT("wave_player_without_wave"), TEXT("error"), N->GetName(), TEXT("Wave Player node has no sound wave")); }
				Out->SetNumberField(TEXT("wave_player_count"), Players);
				if (Cue->FirstNode && Cue->GetDuration() <= 0.f) Issue(Issues, Counts, TEXT("cue_zero_duration"), TEXT("warning"), TEXT("Duration"), TEXT("Sound cue reports zero duration"));
			}
			else
			{
				const bool bMeta = Sound->GetClass()->GetName().Contains(TEXT("MetaSound"));
				Out->SetStringField(TEXT("kind"), bMeta ? TEXT("metasound") : TEXT("other"));
				if (bMeta) Out->SetStringField(TEXT("note"), TEXT("Graph-level checks are available through metasound_get_graph and metasound_compile."));
			}
			FinishReport(Out, Issues, Counts, LimitArg(Args));
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%s (%s): %d issue(s)"), *Sound->GetName(), *Out->GetStringField(TEXT("kind")), (int32)Out->GetNumberField(TEXT("issue_count"))), Out);
		});

	// ------------------------------------------------------------------
	// get_asset_instance_impact (V5-26: mesh-asset versus actor-instance effects)
	// ------------------------------------------------------------------
	MCP_TOOL(Registry, "get_asset_instance_impact")
		.Description(TEXT("Report every place a shared asset is used in a world before changing the asset itself. Supports StaticMesh (StaticMeshComponents), SkeletalMesh (SkeletalMeshComponents) and MaterialInterface (any mesh component slot; instances derived from a material are counted when include_derived is true). Returns per-instance actor label, path, component and slot, counts, and the asset registry's referencing packages. Use it to make the all-instance impact of an asset-level edit explicit."))
		.ReadOnly().Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("StaticMesh, SkeletalMesh or Material/Material Instance path"), true)
		.StringArg(TEXT("world_id"), TEXT("World path from list_worlds (default: editor world)"))
		.BoolArg(TEXT("include_derived"), TEXT("For materials, also count instances whose parent chain contains the asset (default true)"))
		.IntArg(TEXT("limit"), TEXT("Maximum instances listed (default 200, max 1000)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path; Args->TryGetStringField(TEXT("asset_path"), Path);
			UObject* Asset = FindOrLoad(Path);
			if (!Asset) return NotFoundResult(TEXT("Asset"), Path);
			FString Err; UWorld* World = ResolveWorld(Args, Err);
			if (!World) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, Err, TEXT("Use list_worlds."));
			const bool bDerived = !Args->HasField(TEXT("include_derived")) || Args->GetBoolField(TEXT("include_derived"));
			const int32 Limit = LimitArg(Args);
			UStaticMesh* SM = Cast<UStaticMesh>(Asset); USkeletalMesh* SK = Cast<USkeletalMesh>(Asset); UMaterialInterface* Mat = Cast<UMaterialInterface>(Asset);
			if (!SM && !SK && !Mat) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("Unsupported asset class: ") + Asset->GetClass()->GetName(), TEXT("Supported: StaticMesh, SkeletalMesh, Material, MaterialInstance."));
			auto MatchesMaterial = [&](UMaterialInterface* Slot) -> bool
			{
				if (!Slot) return false; if (Slot == Mat) return true; if (!bDerived) return false;
				UMaterialInterface* Cur = Slot; int32 Depth = 0;
				while (UMaterialInstance* I = Cast<UMaterialInstance>(Cur)) { if (I->Parent == Mat) return true; Cur = I->Parent; if (!Cur || ++Depth > 32) break; }
				return false;
			};
			TArray<TSharedPtr<FJsonValue>> Instances; int32 Total = 0; TSet<AActor*> Actors;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* A = *It; if (!A) continue;
				TInlineComponentArray<UMeshComponent*> Comps; A->GetComponents(Comps);
				for (UMeshComponent* C : Comps)
				{
					TArray<FString> Hits;
					if (SM) { if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(C)) if (SMC->GetStaticMesh() == SM) Hits.Add(TEXT("mesh")); }
					else if (SK) { if (USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(C)) if (SKC->GetSkeletalMeshAsset() == SK) Hits.Add(TEXT("mesh")); }
					else { const int32 N = C->GetNumMaterials(); for (int32 i = 0; i < N; ++i) if (MatchesMaterial(C->GetMaterial(i))) Hits.Add(FString::Printf(TEXT("material_slot[%d]"), i)); }
					for (const FString& H : Hits)
					{
						++Total; Actors.Add(A);
						if (Instances.Num() >= Limit) continue;
						auto IJ = MakeShared<FJsonObject>();
						IJ->SetStringField(TEXT("actor_label"), A->GetActorLabel()); IJ->SetStringField(TEXT("actor_path"), A->GetPathName());
						IJ->SetStringField(TEXT("component"), C->GetName()); IJ->SetStringField(TEXT("usage"), H);
						Instances.Add(MakeShared<FJsonValueObject>(IJ));
					}
				}
			}
			TArray<FName> Refs;
			const FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			ARM.Get().GetReferencers(Asset->GetOutermost()->GetFName(), Refs);
			TArray<TSharedPtr<FJsonValue>> RefJ; for (int32 i = 0; i < Refs.Num() && i < Limit; ++i) RefJ.Add(MakeShared<FJsonValueString>(Refs[i].ToString()));
			auto Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("asset_path"), Asset->GetPathName()); Out->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
			Out->SetStringField(TEXT("world_id"), World->GetPathName());
			Out->SetNumberField(TEXT("instance_count"), Total); Out->SetNumberField(TEXT("actor_count"), Actors.Num());
			Out->SetArrayField(TEXT("instances"), Instances); Out->SetBoolField(TEXT("instances_truncated"), Total > Instances.Num());
			Out->SetNumberField(TEXT("referencing_package_count"), Refs.Num()); Out->SetArrayField(TEXT("referencing_packages"), RefJ);
			Out->SetStringField(TEXT("impact_note"), Total > 0 ? FString::Printf(TEXT("Editing the asset changes all %d instance(s) on %d actor(s) in this world; edit a duplicate or an instance-level override to affect one actor only."), Total, Actors.Num()) : TEXT("No instance in this world uses the asset; other worlds and packages may still reference it (see referencing_packages)."));
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%s: %d instance(s) on %d actor(s), %d referencing package(s)"), *Asset->GetName(), Total, Actors.Num(), Refs.Num()), Out);
		});
}
} // namespace MCPAuthoringVerification
