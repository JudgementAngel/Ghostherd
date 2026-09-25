// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// Phase D.8 — MetaHuman tool family.
//
// MetaHuman assets ship as a regular USkeletalMesh + USkeleton + matching
// AnimBlueprints, conventionally rooted at /Game/MetaHumans/<Name>/. The
// MetaHuman plugin itself is optional (frequently re-versioned), so we
// soft-detect it: tools that only need standard skeletal-mesh operations
// work whether or not the plugin is loaded; the import flow that wraps
// Quixel Bridge requires the plugin and is registered as Unsupported.

#include "Tools/MCPMetaHumanTools.h"
#include "Common/MCPActorResolver.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace MCPMetaHumanTools
{

static AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	// v4 Phase 1: cached resolver (O(1) amortized) replaces the per-call actor scan.
	return MCPCommon::FindActorByLabel(World, Label);
}

static bool IsLikelyMetaHumanPath(const FString& PackagePath)
{
	return PackagePath.Contains(TEXT("/MetaHumans/"), ESearchCase::IgnoreCase)
		|| PackagePath.Contains(TEXT("/MetaHuman/"), ESearchCase::IgnoreCase);
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// metahuman_list_assets — implementable via AssetRegistry filtering.
	// ================================================================
	MCP_TOOL(Registry, "metahuman_list_assets")
		.Description(TEXT(
			"List MetaHuman skeletal mesh assets in the project. Uses the AssetRegistry, filtering "
			"USkeletalMesh assets whose package path contains '/MetaHumans/'."))
		.ReadOnly()
		.Idempotent()
		.IntArg(TEXT("limit"), TEXT("Max results (default 100)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = (int32)Args->GetNumberField(TEXT("limit"));
				BAIL_IF_INVALID(FMCPValidate::InRangeI(Limit, 1, 1000, TEXT("limit")));
			}

			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray<FAssetData> Assets;
			ARM.Get().GetAssetsByClass(USkeletalMesh::StaticClass()->GetClassPathName(), Assets, true);

			TArray<TSharedPtr<FJsonValue>> Items;
			int32 Total = 0;
			for (const FAssetData& A : Assets)
			{
				const FString PP = A.PackagePath.ToString();
				if (!IsLikelyMetaHumanPath(PP)) continue;
				++Total;
				if (Items.Num() >= Limit) continue;

				TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("name"), A.AssetName.ToString());
				E->SetStringField(TEXT("path"), A.GetObjectPathString());
				E->SetStringField(TEXT("package"), PP);
				Items.Add(MakeShared<FJsonValueObject>(E));
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetNumberField(TEXT("total_matching"), Total);
			R->SetNumberField(TEXT("returned"), Items.Num());
			R->SetArrayField(TEXT("metahumans"), Items);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d MetaHuman skeletal mesh(es)"), Total), R);
		});

	// ================================================================
	// metahuman_import — gated on the MetaHuman plugin + Quixel Bridge.
	// ================================================================
	MCP_TOOL(Registry, "metahuman_import")
		.Description(TEXT(
			"Import a MetaHuman from Quixel Bridge into the project. Requires the MetaHuman + Quixel "
			"Bridge plugins. Currently registered surface only — implementation depends on the "
			"plugin's Python bridge entry points."))
		.StringArg(TEXT("metahuman_id"), TEXT("MetaHuman identifier as exposed by Quixel Bridge."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			// 5.8: importing a *pre-made* MetaHuman by id from Quixel Bridge is an
			// interactive plugin flow with no stable public/scripted entry point, so
			// we don't fake it. For programmatic creation, 5.8 ships the MetaHuman
			// Generator toolset (UMetaHumanCharacter) with a scripted create() —
			// drive it via the execute_python tool against the engine's
			// `metahuman_toolset` module. Use metahuman_list_assets to enumerate
			// MetaHumans already in the project.
			return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
				TEXT("metahuman_import (by Bridge id) is an interactive Quixel Bridge flow with no scriptable entry point in 5.8."),
				TEXT("Options: (1) create a MetaHuman programmatically via execute_python calling the engine's "
				     "MetaHumanGenerator 'metahuman_toolset' (UMetaHumanCharacter.create(asset_path)); "
				     "(2) import interactively via the Bridge window, then use metahuman_list_assets / "
				     "metahuman_attach_to_skeletal_mesh."));
		});

	// ================================================================
	// metahuman_set_lod — implementable on any USkeletalMeshComponent.
	// ================================================================
	MCP_TOOL(Registry, "metahuman_set_lod")
		.Description(TEXT(
			"Force a forced LOD index on an actor's skeletal mesh component. -1 disables forcing "
			"and lets streaming pick. Works for any skeletal mesh actor, not just MetaHumans."))
		.StringArg(TEXT("actor_label"), TEXT("Editor label of the actor."), true)
		.IntArg(TEXT("lod"), TEXT("Forced LOD index (>=0) or -1 to clear."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Label;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("actor_label"), Label));
			int32 Lod = (int32)Args->GetNumberField(TEXT("lod"));

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World) return FMCPToolResult::ErrorStructured(EMCPError::Internal,
				TEXT("No editor world"));
			AActor* Actor = FindActorByLabel(World, Label);
			if (!Actor) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Actor '%s' not found"), *Label));

			USkeletalMeshComponent* Comp = Actor->FindComponentByClass<USkeletalMeshComponent>();
			if (!Comp) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
				FString::Printf(TEXT("Actor '%s' has no skeletal mesh component"), *Label));

			Comp->SetForcedLOD(Lod < 0 ? 0 : Lod + 1); // UE semantics: 0 = auto, 1 = LOD0, etc.
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("actor"), Label);
			R->SetNumberField(TEXT("forced_lod_input"), Lod);
			R->SetNumberField(TEXT("forced_lod_internal"), Lod < 0 ? 0 : Lod + 1);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Forced LOD set on '%s'"), *Label), R);
		});

	// ================================================================
	// metahuman_attach_to_skeletal_mesh — assign a USkeletalMesh asset
	// to an actor's skeletal mesh component.
	// ================================================================
	MCP_TOOL(Registry, "metahuman_attach_to_skeletal_mesh")
		.Description(TEXT(
			"Assign a USkeletalMesh asset (typically a MetaHuman body or face mesh) onto an actor's "
			"skeletal mesh component. The component must already exist on the actor."))
		.StringArg(TEXT("actor_label"), TEXT("Target actor."), true)
		.StringArg(TEXT("skeletal_mesh_path"), TEXT("USkeletalMesh asset path."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Label, MeshPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("actor_label"), Label));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("skeletal_mesh_path"), MeshPath));

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World) return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("No editor world"));
			AActor* Actor = FindActorByLabel(World, Label);
			if (!Actor) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Actor '%s' not found"), *Label));
			USkeletalMeshComponent* Comp = Actor->FindComponentByClass<USkeletalMeshComponent>();
			if (!Comp) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
				FString::Printf(TEXT("Actor '%s' has no skeletal mesh component"), *Label));

			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
			if (!Mesh)
			{
				FString Stripped = MeshPath; int32 D;
				if (Stripped.FindLastChar(TEXT('.'), D)) Stripped = Stripped.Left(D);
				Mesh = LoadObject<USkeletalMesh>(nullptr, *Stripped);
			}
			if (!Mesh) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Skeletal mesh not found: %s"), *MeshPath));

			Comp->Modify();
			Comp->SetSkeletalMesh(Mesh);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("actor"), Label);
			R->SetStringField(TEXT("skeletal_mesh"), Mesh->GetPathName());
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Assigned '%s' to '%s'"), *Mesh->GetName(), *Label), R);
		});
}

} // namespace MCPMetaHumanTools
