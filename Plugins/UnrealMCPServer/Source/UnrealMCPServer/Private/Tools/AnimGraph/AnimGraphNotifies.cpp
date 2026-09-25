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
#include "AnimationBlueprintLibrary.h"

namespace MCPAnimGraphTools::Notifies
{

using namespace MCPAnimGraphTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// v4.6: rewritten. The v4.5 implementation appended a bare FAnimNotifyEvent
	// and force-saved the package, which produced notifies the editor and the
	// runtime disagreed about:
	//   - no Modify(), so the edit escaped the registry transaction and could
	//     not be rolled back by run_tool_script;
	//   - no Link(), so the notify was never bound to the sequence's frame rate
	//     (and, on a montage, to the segment containing that time);
	//   - no SortNotifies()/RefreshCacheData(), so AnimNotifyTracks and the
	//     compiled notify data drifted from Notifies[];
	//   - track_index was written unchecked — an out-of-range index corrupts
	//     the notify panel;
	//   - the forced UPackage::SavePackage defeated both the transaction and
	//     any attempt to batch edits.
	// It now delegates to UAnimationBlueprintLibrary, which does all of the
	// above correctly. anim_add_notify supersedes it (notify classes, notify
	// states, track-by-name); this stays for backward compatibility.
	MCP_TOOL(Registry, "add_anim_notify")
		.Description(TEXT(
			"Add a simple named notify to an AnimSequence or AnimMontage at a given time. Notifies fire "
			"gameplay events — footsteps, hit frames, VFX — as 'AnimNotify_<name>' in the Animation "
			"Blueprint.\n"
			"Superseded by anim_add_notify, which also supports UAnimNotify subclasses and addresses notify "
			"tracks by name; and by anim_add_notify_state for anything with a duration (combo windows, hit "
			"windows). This tool is kept for compatibility and takes a track INDEX."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the AnimSequence or AnimMontage"), true)
		.NumberArg(TEXT("time_seconds"), TEXT("Time in seconds to place the notify"), true)
		.StringArg(TEXT("notify_name"), TEXT("Name for the notify event (e.g., 'FootStep_L', 'AttackHit', 'ComboWindow')"), true)
		.IntArg(TEXT("track_index"), TEXT("Notify track index (default: 0). Validated against the animation's existing tracks."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			double TimeSeconds = 0.0;
			if (!Args->TryGetNumberField(TEXT("time_seconds"), TimeSeconds))
				return FMCPToolResult::Error(TEXT("time_seconds is required"));

			FString NotifyName;
			if (!Args->TryGetStringField(TEXT("notify_name"), NotifyName))
				return FMCPToolResult::Error(TEXT("notify_name is required"));

			UAnimSequenceBase* AnimAsset = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
			if (!IsValid(AnimAsset))
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Animation asset not found: %s"), *AssetPath));

			if (TimeSeconds < 0.0 || TimeSeconds > AnimAsset->GetPlayLength())
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("time_seconds %.4f is outside '%s' range [0, %.4f]."),
						TimeSeconds, *AnimAsset->GetName(), AnimAsset->GetPlayLength()));

			int32 TrackIndex = 0;
			if (Args->HasField(TEXT("track_index")))
				TrackIndex = (int32)Args->GetNumberField(TEXT("track_index"));

			// Resolve the index to a track NAME, which is what the engine's
			// notify API takes — and reject an index that does not exist rather
			// than writing it and corrupting the notify panel.
			TArray<FName> TrackNames;
			UAnimationBlueprintLibrary::GetAnimationNotifyTrackNames(AnimAsset, TrackNames);
			if (TrackNames.Num() == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("'%s' has no notify tracks."), *AnimAsset->GetName()),
					TEXT("Add one with anim_add_notify_track."));
			}
			if (!TrackNames.IsValidIndex(TrackIndex))
			{
				TArray<FString> Available;
				for (int32 i = 0; i < TrackNames.Num(); ++i)
				{
					Available.Add(FString::Printf(TEXT("%d = '%s'"), i, *TrackNames[i].ToString()));
				}
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("track_index %d is out of range; '%s' has %d notify track(s)."),
						TrackIndex, *AnimAsset->GetName(), TrackNames.Num()),
					TEXT("Use anim_list_notify_tracks, or anim_add_notify which addresses tracks by name."),
					Available);
			}

			AnimAsset->Modify();

			const int32 BeforeCount = AnimAsset->Notifies.Num();
			UAnimationBlueprintLibrary::AddAnimationNotifyEvent(
				AnimAsset, TrackNames[TrackIndex], (float)TimeSeconds, /*NotifyClass*/ nullptr);

			if (AnimAsset->Notifies.Num() != BeforeCount + 1)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("The engine did not add a notify to '%s'. See the Output Log."), *AnimAsset->GetName()));
			}

			// A class-less notify is created with NotifyName = NAME_None; the name
			// is what the AnimBP binds its event to.
			AnimAsset->Notifies.Last().NotifyName = FName(*NotifyName);

			AnimAsset->SortNotifies();
			AnimAsset->RefreshCacheData();
			AnimAsset->MarkPackageDirty();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added notify '%s' at %.3fs on '%s' (track %d, '%s'). Total notifies: %d. Implement 'AnimNotify_%s' in the Animation Blueprint to react to it."),
				*NotifyName, TimeSeconds, *AnimAsset->GetName(),
				TrackIndex, *TrackNames[TrackIndex].ToString(), AnimAsset->Notifies.Num(), *NotifyName));
		});
	MCP_TOOL(Registry, "list_anim_notifies")
		.Description(TEXT("List all animation notifies on an AnimSequence or AnimMontage with their trigger times, names, types, and track indices."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the AnimSequence or AnimMontage"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UAnimSequenceBase* AnimAsset = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
			if (!IsValid(AnimAsset))
				return FMCPToolResult::Error(FString::Printf(TEXT("Animation asset not found: %s"), *AssetPath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset"), AnimAsset->GetName());
			Result->SetStringField(TEXT("path"), AnimAsset->GetPathName());
			Result->SetNumberField(TEXT("duration"), AnimAsset->GetPlayLength());

			TArray<TSharedPtr<FJsonValue>> NotifyArray;
			for (const FAnimNotifyEvent& Notify : AnimAsset->Notifies)
			{
				TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
				NObj->SetStringField(TEXT("name"), Notify.NotifyName.ToString());
				NObj->SetNumberField(TEXT("time"), Notify.GetTriggerTime());
				NObj->SetNumberField(TEXT("duration"), Notify.GetDuration());
				NObj->SetNumberField(TEXT("track_index"), Notify.TrackIndex);

				if (Notify.Notify)
				{
					NObj->SetStringField(TEXT("class"), Notify.Notify->GetClass()->GetName());
				}
				else if (Notify.NotifyStateClass.Get())
				{
					NObj->SetStringField(TEXT("class"), Notify.NotifyStateClass.Get()->GetClass()->GetName());
					NObj->SetBoolField(TEXT("is_state"), true);
				}
				else
				{
					NObj->SetStringField(TEXT("class"), TEXT("Simple Notify"));
				}

				NotifyArray.Add(MakeShared<FJsonValueObject>(NObj));
			}

			Result->SetNumberField(TEXT("notify_count"), NotifyArray.Num());
			Result->SetArrayField(TEXT("notifies"), NotifyArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPAnimGraphTools::Notifies
