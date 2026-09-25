// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.5 (UE 5.8) — Morph Target (blendshape) tool family.
//
// 5.8 expanded morph-target authoring in the Skeletal Editor (element selection,
// soft selection). These tools cover the programmatic surface: enumerate a
// skeletal mesh's morph targets, and read/set/clear blendshape weights on a
// skeletal-mesh actor's component (USkeletalMeshComponent::SetMorphTarget).

#include "Tools/MCPMorphTargetTools.h"
#include "Common/MCPActorResolver.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/MorphTarget.h"

namespace MCPMorphTargetTools
{

static USkeletalMeshComponent* ResolveSkelComp(const FString& Label, FMCPToolResult& OutErr)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		OutErr = FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("No editor world"));
		return nullptr;
	}
	AActor* Actor = MCPCommon::FindActorByLabel(World, Label);
	if (!Actor)
	{
		OutErr = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
			FString::Printf(TEXT("Actor '%s' not found"), *Label));
		return nullptr;
	}
	USkeletalMeshComponent* Comp = Actor->FindComponentByClass<USkeletalMeshComponent>();
	if (!Comp)
	{
		OutErr = FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
			FString::Printf(TEXT("Actor '%s' has no skeletal mesh component"), *Label));
		return nullptr;
	}
	return Comp;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// morph_list_targets — from a skeletal mesh asset OR an actor's mesh
	// ================================================================
	MCP_TOOL(Registry, "morph_list_targets")
		.Description(TEXT(
			"List the morph targets (blendshapes) on a skeletal mesh. Provide either skeletal_mesh_path "
			"(asset) or actor_label (uses the actor's skeletal mesh component). Returns morph target names."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("skeletal_mesh_path"), TEXT("USkeletalMesh asset path (optional)."))
		.StringArg(TEXT("actor_label"), TEXT("Actor whose skeletal mesh to inspect (optional)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			USkeletalMesh* Mesh = nullptr;
			FString MeshPath, Label;
			if (Args->TryGetStringField(TEXT("skeletal_mesh_path"), MeshPath) && !MeshPath.IsEmpty())
			{
				Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
				if (!Mesh)
				{
					FString Stripped = MeshPath; int32 D;
					if (Stripped.FindLastChar(TEXT('.'), D)) Stripped = Stripped.Left(D);
					Mesh = LoadObject<USkeletalMesh>(nullptr, *Stripped);
				}
				if (!Mesh) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Skeletal mesh not found: %s"), *MeshPath));
			}
			else if (Args->TryGetStringField(TEXT("actor_label"), Label) && !Label.IsEmpty())
			{
				FMCPToolResult Err;
				USkeletalMeshComponent* Comp = ResolveSkelComp(Label, Err);
				if (!Comp) return Err;
				Mesh = Comp->GetSkeletalMeshAsset();
				if (!Mesh) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Actor '%s' has no skeletal mesh assigned"), *Label));
			}
			else
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("Provide either skeletal_mesh_path or actor_label."));
			}

			TArray<TSharedPtr<FJsonValue>> Names;
			for (const TObjectPtr<UMorphTarget>& MT : Mesh->GetMorphTargets())
			{
				if (MT) Names.Add(MakeShared<FJsonValueString>(MT->GetName()));
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("skeletal_mesh"), Mesh->GetPathName());
			R->SetNumberField(TEXT("count"), Names.Num());
			R->SetArrayField(TEXT("morph_targets"), Names);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d morph target(s) on '%s'"), Names.Num(), *Mesh->GetName()), R);
		});

	// ================================================================
	// morph_set_weight — set a blendshape weight on an actor's component
	// ================================================================
	MCP_TOOL(Registry, "morph_set_weight")
		.Description(TEXT(
			"Set a morph target (blendshape) weight on an actor's skeletal mesh component. Weight is "
			"typically 0..1 (values outside are allowed for over/under-driving). Use morph_list_targets "
			"to find valid names."))
		.StringArg(TEXT("actor_label"), TEXT("Target actor."), true)
		.StringArg(TEXT("morph_target"), TEXT("Morph target name."), true)
		.NumberArg(TEXT("weight"), TEXT("Weight (0..1 nominal)."), true)
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Label, Morph;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("actor_label"), Label));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("morph_target"), Morph));
			double Weight;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("weight"), Weight));

			FMCPToolResult Err;
			USkeletalMeshComponent* Comp = ResolveSkelComp(Label, Err);
			if (!Comp) return Err;

			// Warn (don't fail) if the name isn't on the mesh — SetMorphTarget is lenient.
			bool bKnown = false;
			if (USkeletalMesh* Mesh = Comp->GetSkeletalMeshAsset())
			{
				for (const TObjectPtr<UMorphTarget>& MT : Mesh->GetMorphTargets())
				{
					if (MT && MT->GetFName() == FName(*Morph)) { bKnown = true; break; }
				}
			}

			Comp->Modify();
			Comp->SetMorphTarget(FName(*Morph), (float)Weight);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("actor"), Label);
			R->SetStringField(TEXT("morph_target"), Morph);
			R->SetNumberField(TEXT("weight"), Weight);
			R->SetBoolField(TEXT("name_found_on_mesh"), bKnown);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Set morph '%s' = %.3f on '%s'%s"), *Morph, Weight, *Label,
					bKnown ? TEXT("") : TEXT(" (warning: name not found on mesh)")), R);
		});

	// ================================================================
	// morph_get_weights — read active blendshape weights on an actor
	// ================================================================
	MCP_TOOL(Registry, "morph_get_weights")
		.Description(TEXT(
			"Read the currently-applied morph target weights (set via SetMorphTarget) on an actor's "
			"skeletal mesh component."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("actor_label"), TEXT("Target actor."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Label;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("actor_label"), Label));

			FMCPToolResult Err;
			USkeletalMeshComponent* Comp = ResolveSkelComp(Label, Err);
			if (!Comp) return Err;

			TSharedPtr<FJsonObject> Weights = MakeShared<FJsonObject>();
			for (const TPair<FName, float>& Pair : Comp->GetMorphTargetCurves())
			{
				Weights->SetNumberField(Pair.Key.ToString(), Pair.Value);
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("actor"), Label);
			R->SetObjectField(TEXT("weights"), Weights);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d active morph weight(s) on '%s'"), Weights->Values.Num(), *Label), R);
		});

	// ================================================================
	// morph_clear — clear all applied blendshape weights on an actor
	// ================================================================
	MCP_TOOL(Registry, "morph_clear")
		.Description(TEXT("Clear all morph target weights set via SetMorphTarget on an actor's component."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("actor_label"), TEXT("Target actor."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Label;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("actor_label"), Label));

			FMCPToolResult Err;
			USkeletalMeshComponent* Comp = ResolveSkelComp(Label, Err);
			if (!Comp) return Err;

			Comp->Modify();
			Comp->ClearMorphTargets();

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("actor"), Label);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Cleared morph targets on '%s'"), *Label), R);
		});
}

} // namespace MCPMorphTargetTools
