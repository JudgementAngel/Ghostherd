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

namespace MCPAnimGraphTools::Montages
{

using namespace MCPAnimGraphTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	MCP_TOOL(Registry, "create_anim_montage")
		.Description(TEXT(
			"Create a UAnimMontage from an existing animation sequence. The animation goes into a named slot "
			"track, and the slot is registered on the target skeleton so an AnimGraph Slot node can select it "
			"— without that registration the montage plays into nothing. Play it at runtime with "
			"PlayAnimMontage() or Montage_Play().\n"
			"For a multi-section combo built in one call, use montage_create_from_sections instead. After "
			"creating, shape the montage with montage_add_section, montage_link_sections and "
			"montage_set_blend_settings, then check it with montage_validate."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new AnimMontage asset (e.g., '/Game/Characters/AM_HeroAttack')"), true)
		.StringArg(TEXT("animation_path"), TEXT("Content path to the source UAnimSequence asset (e.g., '/Game/Characters/AS_Attack01')"), true)
		.StringArg(TEXT("slot_name"), TEXT("Name of the slot track in the montage (default: 'DefaultSlot'). Registered on the skeleton if new."))
		.NumberArg(TEXT("blend_in_time"), TEXT("Blend in duration in seconds (default: engine default, 0.25)"))
		.NumberArg(TEXT("blend_out_time"), TEXT("Blend out duration in seconds (default: engine default, 0.25)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Failed to load AnimSequence at path: %s"), *AnimPath));

			USkeleton* Skeleton = AnimSeq->GetSkeleton();
			if (!IsValid(Skeleton))
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("AnimSequence '%s' has no skeleton, so no montage can target it."), *AnimSeq->GetName()));

			FString PackagePath, AssetName;
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package)
				return PackageError;

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

			// Name the slot track the factory seeded (or add one if it seeded none).
			const FName SlotFName(*SlotName);
			if (NewMontage->SlotAnimTracks.Num() > 0)
			{
				NewMontage->SlotAnimTracks[0].SlotName = SlotFName;
			}
			else
			{
				NewMontage->AddSlot(SlotFName);
			}

			// v4.6: register the slot on the skeleton. Naming the track alone was
			// not enough — an unregistered slot never appears in the AnimGraph Slot
			// node's dropdown, so the montage had nowhere to play.
			Skeleton->Modify();
			const bool bNewlyRegisteredSlot = Skeleton->RegisterSlotNode(SlotFName);

			if (Args->HasField(TEXT("blend_in_time")))
			{
				NewMontage->BlendIn.SetBlendTime((float)Args->GetNumberField(TEXT("blend_in_time")));
			}
			if (Args->HasField(TEXT("blend_out_time")))
			{
				NewMontage->BlendOut.SetBlendTime((float)Args->GetNumberField(TEXT("blend_out_time")));
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
			Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
			Result->SetBoolField(TEXT("registered_slot_on_skeleton"), bNewlyRegisteredSlot);
			Result->SetNumberField(TEXT("duration"), NewMontage->GetPlayLength());
			Result->SetNumberField(TEXT("section_count"), NewMontage->CompositeSections.Num());
			Result->SetNumberField(TEXT("blend_in_time"), NewMontage->BlendIn.GetBlendTime());
			Result->SetNumberField(TEXT("blend_out_time"), NewMontage->BlendOut.GetBlendTime());

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Created montage '%s' (%.3fs) on slot '%s'; %s on skeleton '%s'."),
					*AssetPath, NewMontage->GetPlayLength(), *SlotName,
					bNewlyRegisteredSlot ? TEXT("registered the slot") : TEXT("the slot was already registered"),
					*Skeleton->GetName()),
				Result);
		});
	MCP_TOOL(Registry, "get_anim_montage_info")
		.Description(TEXT(
			"Retrieve detailed information about an AnimMontage asset: total duration, "
			"composite sections with their start times and next section links, "
			"slot track names with their animation segments, and anim notify events. "
			"Useful for understanding or debugging a montage before runtime playback."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage asset (e.g., '/Game/Characters/AM_HeroAttack')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPAnimGraphTools::Montages
