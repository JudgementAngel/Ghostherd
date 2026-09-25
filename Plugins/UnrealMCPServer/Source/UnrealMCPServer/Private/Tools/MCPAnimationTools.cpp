// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPAnimationTools.h"
#include "Common/MCPActorResolver.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "AssetRegistry/AssetRegistryModule.h"

namespace MCPAnimationTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

static AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	// v4 Phase 1: cached resolver (O(1) amortized) replaces the per-call actor scan.
	return MCPCommon::FindActorByLabel(World, Label);
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// set_skeletal_mesh - Set the skeletal mesh on an actor
	// ================================================================
	MCP_TOOL(Registry, "set_skeletal_mesh")
		.Description(TEXT("Set the skeletal mesh asset on an actor's SkeletalMeshComponent. Works on any actor that has a SkeletalMeshComponent (e.g., SkeletalMeshActor, Character). Wrap in an undo transaction."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor containing a SkeletalMeshComponent"), true)
		.StringArg(TEXT("mesh_path"), TEXT("Content path to the USkeletalMesh asset (e.g., '/Game/Characters/SK_Mannequin')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName, MeshPath;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));
			if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath))
				return FMCPToolResult::Error(TEXT("mesh_path is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!IsValid(Actor))
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			USkeletalMeshComponent* SkelComp = Actor->FindComponentByClass<USkeletalMeshComponent>();
			if (!IsValid(SkelComp))
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' has no SkeletalMeshComponent"), *ActorName));

			USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
			if (!IsValid(SkelMesh))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load skeletal mesh at path: %s"), *MeshPath));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Skeletal Mesh")));
			SkelComp->Modify();
			SkelComp->SetSkeletalMeshAsset(SkelMesh);
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Set skeletal mesh '%s' on actor '%s'"),
				*SkelMesh->GetName(), *ActorName));
		});

	// ================================================================
	// set_animation_blueprint - Set the anim BP class on a skeletal mesh component
	// ================================================================
	MCP_TOOL(Registry, "set_animation_blueprint")
		.Description(TEXT("Assign an Animation Blueprint to a SkeletalMeshComponent by loading the UAnimBlueprint asset, extracting its generated class, and calling SetAnimInstanceClass(). This determines how the skeleton is driven at runtime."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor containing a SkeletalMeshComponent"), true)
		.StringArg(TEXT("anim_bp_path"), TEXT("Content path to the UAnimBlueprint asset (e.g., '/Game/Characters/ABP_Mannequin')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName, AnimBPPath;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));
			if (!Args->TryGetStringField(TEXT("anim_bp_path"), AnimBPPath))
				return FMCPToolResult::Error(TEXT("anim_bp_path is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!IsValid(Actor))
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			USkeletalMeshComponent* SkelComp = Actor->FindComponentByClass<USkeletalMeshComponent>();
			if (!IsValid(SkelComp))
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' has no SkeletalMeshComponent"), *ActorName));

			UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
			if (!IsValid(AnimBP))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load AnimBlueprint at path: %s"), *AnimBPPath));

			UClass* AnimClass = AnimBP->GetAnimBlueprintGeneratedClass();
			if (!IsValid(AnimClass))
				return FMCPToolResult::Error(FString::Printf(TEXT("AnimBlueprint '%s' has no generated class. Ensure it has been compiled."), *AnimBP->GetName()));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Animation Blueprint")));
			SkelComp->Modify();
			SkelComp->SetAnimInstanceClass(AnimClass);
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Set AnimBlueprint '%s' (class: '%s') on actor '%s'"),
				*AnimBP->GetName(), *AnimClass->GetName(), *ActorName));
		});

	// ================================================================
	// play_animation - Play a single animation asset on a skeletal mesh
	// ================================================================
	MCP_TOOL(Registry, "play_animation")
		.Description(TEXT("Play a single UAnimSequence on an actor's SkeletalMeshComponent using AnimationSingleNode mode. This overrides any AnimBlueprint currently assigned. Useful for previewing animations directly in the editor viewport."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor containing a SkeletalMeshComponent"), true)
		.StringArg(TEXT("animation_path"), TEXT("Content path to the UAnimSequence asset (e.g., '/Game/Animations/AM_Run')"), true)
		.BoolArg(TEXT("looping"), TEXT("Whether the animation should loop (default: false)"))
		.NumberArg(TEXT("play_rate"), TEXT("Playback rate multiplier; 1.0 = normal speed, 2.0 = double speed (default: 1.0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName, AnimPath;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));
			if (!Args->TryGetStringField(TEXT("animation_path"), AnimPath))
				return FMCPToolResult::Error(TEXT("animation_path is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!IsValid(Actor))
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			USkeletalMeshComponent* SkelComp = Actor->FindComponentByClass<USkeletalMeshComponent>();
			if (!IsValid(SkelComp))
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' has no SkeletalMeshComponent"), *ActorName));

			UAnimSequence* AnimSequence = LoadObject<UAnimSequence>(nullptr, *AnimPath);
			if (!IsValid(AnimSequence))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load AnimSequence at path: %s"), *AnimPath));

			bool bLooping = false;
			Args->TryGetBoolField(TEXT("looping"), bLooping);

			float PlayRate = 1.0f;
			if (Args->HasField(TEXT("play_rate")))
			{
				PlayRate = static_cast<float>(Args->GetNumberField(TEXT("play_rate")));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Play Animation")));
			SkelComp->Modify();
			SkelComp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			SkelComp->PlayAnimation(AnimSequence, bLooping);
			SkelComp->SetPlayRate(PlayRate);
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Playing animation '%s' on actor '%s' (looping: %s, play_rate: %.2f)"),
				*AnimSequence->GetName(), *ActorName,
				bLooping ? TEXT("true") : TEXT("false"),
				PlayRate));
		});

	// ================================================================
	// get_skeleton_info - Get bone hierarchy and socket info for a skeletal mesh
	// ================================================================
	MCP_TOOL(Registry, "get_skeleton_info")
		.Description(TEXT("Retrieve structural information about a skeletal mesh asset: total bone count, the first 50 bone names in hierarchy order, socket count, socket names, and the mesh bounding box. Useful for animation rigging and attachment workflows."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("mesh_path"), TEXT("Content path to the USkeletalMesh asset (e.g., '/Game/Characters/SK_Mannequin')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MeshPath;
			if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath))
				return FMCPToolResult::Error(TEXT("mesh_path is required"));

			USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
			if (!IsValid(SkelMesh))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load skeletal mesh at path: %s"), *MeshPath));

			USkeleton* Skeleton = SkelMesh->GetSkeleton();
			if (!IsValid(Skeleton))
				return FMCPToolResult::Error(FString::Printf(TEXT("Skeletal mesh '%s' has no Skeleton asset"), *SkelMesh->GetName()));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("mesh_name"), SkelMesh->GetName());
			Result->SetStringField(TEXT("mesh_path"), SkelMesh->GetPathName());
			Result->SetStringField(TEXT("skeleton_name"), Skeleton->GetName());
			Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());

			// Bone information from the reference skeleton
			const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
			int32 BoneCount = RefSkeleton.GetNum();
			Result->SetNumberField(TEXT("bone_count"), BoneCount);

			// Return first 50 bones with their parent index
			int32 BoneLimit = FMath::Min(BoneCount, 50);
			TArray<TSharedPtr<FJsonValue>> BoneArray;
			for (int32 i = 0; i < BoneLimit; i++)
			{
				TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
				BoneObj->SetStringField(TEXT("name"), RefSkeleton.GetBoneName(i).ToString());
				BoneObj->SetNumberField(TEXT("index"), i);
				BoneObj->SetNumberField(TEXT("parent_index"), RefSkeleton.GetParentIndex(i));
				BoneArray.Add(MakeShared<FJsonValueObject>(BoneObj));
			}
			Result->SetArrayField(TEXT("bones"), BoneArray);

			if (BoneCount > 50)
			{
				Result->SetStringField(TEXT("bones_note"), FString::Printf(TEXT("Showing first 50 of %d bones"), BoneCount));
			}

			// Socket information
			const TArray<USkeletalMeshSocket*>& Sockets = SkelMesh->GetMeshOnlySocketList();
			int32 SocketCount = Sockets.Num();

			// Also count skeleton-level sockets
			const TArray<USkeletalMeshSocket*>& SkeletonSockets = Skeleton->Sockets;
			int32 TotalSocketCount = SocketCount + SkeletonSockets.Num();
			Result->SetNumberField(TEXT("socket_count"), TotalSocketCount);

			TArray<TSharedPtr<FJsonValue>> SocketArray;
			for (const USkeletalMeshSocket* Socket : Sockets)
			{
				if (IsValid(Socket))
				{
					TSharedPtr<FJsonObject> SocketObj = MakeShared<FJsonObject>();
					SocketObj->SetStringField(TEXT("name"), Socket->SocketName.ToString());
					SocketObj->SetStringField(TEXT("bone"), Socket->BoneName.ToString());
					SocketObj->SetStringField(TEXT("source"), TEXT("mesh"));
					SocketArray.Add(MakeShared<FJsonValueObject>(SocketObj));
				}
			}
			for (const USkeletalMeshSocket* Socket : SkeletonSockets)
			{
				if (IsValid(Socket))
				{
					TSharedPtr<FJsonObject> SocketObj = MakeShared<FJsonObject>();
					SocketObj->SetStringField(TEXT("name"), Socket->SocketName.ToString());
					SocketObj->SetStringField(TEXT("bone"), Socket->BoneName.ToString());
					SocketObj->SetStringField(TEXT("source"), TEXT("skeleton"));
					SocketArray.Add(MakeShared<FJsonValueObject>(SocketObj));
				}
			}
			Result->SetArrayField(TEXT("sockets"), SocketArray);

			// Bounds
			FBoxSphereBounds Bounds = SkelMesh->GetBounds();
			TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
			BoundsObj->SetNumberField(TEXT("origin_x"), Bounds.Origin.X);
			BoundsObj->SetNumberField(TEXT("origin_y"), Bounds.Origin.Y);
			BoundsObj->SetNumberField(TEXT("origin_z"), Bounds.Origin.Z);
			BoundsObj->SetNumberField(TEXT("extent_x"), Bounds.BoxExtent.X);
			BoundsObj->SetNumberField(TEXT("extent_y"), Bounds.BoxExtent.Y);
			BoundsObj->SetNumberField(TEXT("extent_z"), Bounds.BoxExtent.Z);
			BoundsObj->SetNumberField(TEXT("sphere_radius"), Bounds.SphereRadius);
			Result->SetObjectField(TEXT("bounds"), BoundsObj);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// list_animation_assets - List AnimSequence and AnimMontage assets
	// ================================================================
	MCP_TOOL(Registry, "list_animation_assets")
		.Description(TEXT("List UAnimSequence and UAnimMontage assets found in the Asset Registry under a given content path. Optionally filter by skeleton asset or name substring. Returns asset name, path, type (Sequence or Montage), duration in seconds, and frame count."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("skeleton_path"), TEXT("Optional content path to a USkeleton asset to filter results by skeleton (e.g., '/Game/Characters/SK_Mannequin_Skeleton')"))
		.StringArg(TEXT("path"), TEXT("Content path prefix to search under (default: '/Game/')"))
		.StringArg(TEXT("name_filter"), TEXT("Optional substring filter applied to asset names (case-insensitive)"))
		.IntArg(TEXT("limit"), TEXT("Maximum number of results to return (default: 100)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SearchPath = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), SearchPath);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = static_cast<int32>(Args->GetNumberField(TEXT("limit")));
				Limit = FMath::Clamp(Limit, 1, 1000);
			}

			FString SkeletonPath;
			bool bFilterBySkeleton = Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath) && !SkeletonPath.IsEmpty();

			// Load optional skeleton for filtering
			USkeleton* FilterSkeleton = nullptr;
			if (bFilterBySkeleton)
			{
				FilterSkeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
				if (!IsValid(FilterSkeleton))
					return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load Skeleton at path: %s"), *SkeletonPath));
			}

			IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

			// Gather both AnimSequence and AnimMontage assets
			TArray<FAssetData> AllAssets;

			FARFilter Filter;
			Filter.bRecursivePaths = true;
			Filter.PackagePaths.Add(FName(*SearchPath));
			Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
			Filter.ClassPaths.Add(UAnimMontage::StaticClass()->GetClassPathName());

			// v4.6: filter by skeleton through the Asset Registry's "Skeleton" tag
			// rather than by loading every candidate. Animation assets publish the
			// tag as the skeleton's export-text name (the same query Persona's own
			// asset pickers use), so the registry can reject non-matching assets
			// before any package is touched. Previously this tool loaded EVERY
			// AnimSequence and AnimMontage under the search path just to read a
			// duration, which does not scale past a few hundred assets.
			if (bFilterBySkeleton)
			{
				Filter.TagsAndValues.Add(TEXT("Skeleton"), FAssetData(FilterSkeleton).GetExportTextName());
			}

			AssetRegistry.GetAssets(Filter, AllAssets);

			TArray<TSharedPtr<FJsonValue>> AssetArray;
			int32 ProcessedCount = 0;

			for (const FAssetData& AssetData : AllAssets)
			{
				if (ProcessedCount >= Limit) break;

				// Apply name substring filter
				FString AssetName = AssetData.AssetName.ToString();
				if (!NameFilter.IsEmpty() && !AssetName.Contains(NameFilter, ESearchCase::IgnoreCase))
				{
					continue;
				}

				// Determine type string from class
				FString TypeString;
				FName ClassName = AssetData.AssetClassPath.GetAssetName();
				if (ClassName == TEXT("AnimSequence"))
				{
					TypeString = TEXT("Sequence");
				}
				else if (ClassName == TEXT("AnimMontage"))
				{
					TypeString = TEXT("Montage");
				}
				else
				{
					TypeString = ClassName.ToString();
				}

				// Duration and frame count are not registry tags, so the surviving
				// assets (at most `limit` of them) are loaded for those two fields.
				float Duration = 0.0f;
				int32 NumFrames = 0;
				if (UAnimSequenceBase* SeqBase = Cast<UAnimSequenceBase>(AssetData.GetAsset()))
				{
					Duration = SeqBase->GetPlayLength();
					NumFrames = SeqBase->GetNumberOfSampledKeys();
				}
				else
				{
					continue;
				}

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), AssetName);
				Entry->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
				Entry->SetStringField(TEXT("type"), TypeString);
				Entry->SetNumberField(TEXT("duration"), Duration);
				Entry->SetNumberField(TEXT("num_frames"), NumFrames);

				AssetArray.Add(MakeShared<FJsonValueObject>(Entry));
				ProcessedCount++;
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("search_path"), SearchPath);
			Result->SetNumberField(TEXT("count"), AssetArray.Num());
			Result->SetArrayField(TEXT("assets"), AssetArray);

			if (bFilterBySkeleton)
			{
				Result->SetStringField(TEXT("skeleton_filter"), FilterSkeleton->GetName());
			}
			if (!NameFilter.IsEmpty())
			{
				Result->SetStringField(TEXT("name_filter"), NameFilter);
			}

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPAnimationTools
