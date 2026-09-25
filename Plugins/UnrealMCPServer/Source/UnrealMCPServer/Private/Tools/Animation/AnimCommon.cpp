// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/Animation/AnimCommon.h"

#include "MCPProtocol.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"

#include "AnimGraphNode_StateMachine.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateMachineGraph.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"

namespace MCPAnimTools::Common
{

// =====================================================================
// Asset plumbing
// =====================================================================

bool SplitAssetPath(const FString& FullPath, FString& OutPackagePath, FString& OutAssetName)
{
	OutPackagePath = FPackageName::ObjectPathToPackageName(FullPath);
	OutAssetName = FPackageName::GetShortName(FullPath);
	return !OutPackagePath.IsEmpty() && !OutAssetName.IsEmpty();
}

bool SaveExistingAsset(UObject* Asset, FMCPToolResult& OutError)
{
	if (!IsValid(Asset))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("Cannot save a null asset."));
		return false;
	}

	UPackage* Package = Asset->GetPackage();
	if (!Package)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal,
			FString::Printf(TEXT("Asset '%s' has no package."), *Asset->GetName()));
		return false;
	}

	const FString PackagePath = Package->GetName();
	const FString Filename = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, Asset, *Filename, SaveArgs))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Locked,
			FString::Printf(TEXT("Failed to save package '%s'."), *PackagePath),
			TEXT("The file may be read-only or checked out by another user. Try sc_check_out first."));
		return false;
	}
	return true;
}

UClass* ResolveClass(const FString& ClassName, UClass* BaseClass, FMCPToolResult& OutError)
{
	if (ClassName.IsEmpty())
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::InvalidName, TEXT("Class name is empty."));
		return nullptr;
	}

	UClass* Found = nullptr;

	// Fully-qualified path first ("/Script/Engine.AnimNotify_PlaySound").
	if (ClassName.StartsWith(TEXT("/")))
	{
		Found = LoadClass<UObject>(nullptr, *ClassName);
	}

	if (!Found)
	{
		Found = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	}
	if (!Found)
	{
		// Engine notify classes are conventionally prefixed; try the common forms.
		const TArray<FString> Prefixes = { TEXT("U"), TEXT("AnimNotify_"), TEXT("AnimNotifyState_") };
		for (const FString& Prefix : Prefixes)
		{
			Found = FindFirstObject<UClass>(*(Prefix + ClassName), EFindFirstObjectOptions::ExactClass);
			if (Found) break;
		}
	}

	if (!Found)
	{
		// Collect loaded subclasses of BaseClass as did-you-mean candidates.
		TArray<FString> Candidates;
		if (BaseClass)
		{
			for (TObjectIterator<UClass> It; It && Candidates.Num() < 200; ++It)
			{
				UClass* Candidate = *It;
				if (Candidate->IsChildOf(BaseClass) &&
					!Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
				{
					Candidates.Add(Candidate->GetName());
				}
			}
			Candidates.Sort();
		}

		// Keep the hint short: only names sharing a prefix or containing the query.
		TArray<FString> Suggestions;
		for (const FString& Candidate : Candidates)
		{
			if (Candidate.Contains(ClassName, ESearchCase::IgnoreCase))
			{
				Suggestions.Add(Candidate);
				if (Suggestions.Num() >= 5) break;
			}
		}

		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
			FString::Printf(TEXT("Class '%s' not found."), *ClassName),
			BaseClass
				? FString::Printf(TEXT("Expected a subclass of %s. The class must be loaded — a Blueprint class needs its full path, e.g. '/Game/Anim/ANS_Combo.ANS_Combo_C'."), *BaseClass->GetName())
				: TEXT("The class must be loaded; Blueprint classes need a full object path."),
			Suggestions);
		return nullptr;
	}

	if (BaseClass && !Found->IsChildOf(BaseClass))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::InvalidName,
			FString::Printf(TEXT("Class '%s' is not a subclass of %s."), *Found->GetName(), *BaseClass->GetName()));
		return nullptr;
	}

	return Found;
}

// =====================================================================
// Skeleton compatibility
// =====================================================================

bool RequireSameSkeleton(const USkeleton* Expected, const UAnimationAsset* Asset,
	const TCHAR* AssetLabel, FMCPToolResult& OutError)
{
	if (!Expected)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
			TEXT("The target asset has no Skeleton, so compatibility cannot be checked."));
		return false;
	}
	if (!IsValid(Asset))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
			FString::Printf(TEXT("%s is not a valid animation asset."), AssetLabel));
		return false;
	}

	const USkeleton* AssetSkeleton = const_cast<UAnimationAsset*>(Asset)->GetSkeleton();
	if (AssetSkeleton == Expected)
	{
		return true;
	}

	OutError = FMCPToolResult::ErrorStructured(EMCPError::InvalidName,
		FString::Printf(TEXT("Skeleton mismatch: %s '%s' targets skeleton '%s', but the operation requires '%s'."),
			AssetLabel,
			*Asset->GetName(),
			AssetSkeleton ? *AssetSkeleton->GetPathName() : TEXT("(none)"),
			*Expected->GetPathName()),
		TEXT("Retarget the animation onto the target skeleton first (create_ik_retargeter + retarget_animations), or pass an asset that already shares this skeleton. list_anim_assets_by_skeleton lists compatible assets."));
	return false;
}

// =====================================================================
// Animation sequence / montage editing
// =====================================================================

bool RequireTimeInRange(const UAnimSequenceBase* Anim, double Time,
	const TCHAR* FieldName, FMCPToolResult& OutError)
{
	if (!IsValid(Anim))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("Animation asset is invalid."));
		return false;
	}

	const float Length = Anim->GetPlayLength();
	if (Time < 0.0 || Time > Length)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
			FString::Printf(TEXT("%s = %.4f is outside '%s' playable range [0, %.4f]."),
				FieldName, Time, *Anim->GetName(), Length));
		return false;
	}
	return true;
}

bool ResolveNotifyTrack(UAnimSequenceBase* Anim, const FString& TrackName,
	bool bCreateIfMissing, FName& OutTrackName, FMCPToolResult& OutError)
{
	if (!IsValid(Anim))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("Animation asset is invalid."));
		return false;
	}

	// An animation always has at least track 0; the engine names it "1".
	const FName Requested = TrackName.IsEmpty()
		? (Anim->AnimNotifyTracks.Num() > 0 ? Anim->AnimNotifyTracks[0].TrackName : FName(TEXT("1")))
		: FName(*TrackName);

	if (UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(Anim, Requested))
	{
		OutTrackName = Requested;
		return true;
	}

	if (bCreateIfMissing)
	{
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Anim, Requested, FLinearColor::White);
		if (UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(Anim, Requested))
		{
			OutTrackName = Requested;
			return true;
		}
	}

	TArray<FName> Existing;
	UAnimationBlueprintLibrary::GetAnimationNotifyTrackNames(Anim, Existing);
	TArray<FString> Names;
	for (const FName& Name : Existing) { Names.Add(Name.ToString()); }

	OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
		FString::Printf(TEXT("Notify track '%s' does not exist on '%s'."), *Requested.ToString(), *Anim->GetName()),
		TEXT("Create it with anim_add_notify_track, or pass create_track=true. Note tracks are addressed by NAME, not index — the default track is named '1'."),
		Names);
	return false;
}

void BeginAnimEdit(UAnimSequenceBase* Anim)
{
	if (IsValid(Anim))
	{
		Anim->Modify();
	}
}

void EndAnimEdit(UAnimSequenceBase* Anim)
{
	if (!IsValid(Anim))
	{
		return;
	}

	// RefreshCacheData rebuilds the notify-track arrays and (for montages) the
	// branching-point markers. Without it the editor UI and the compiled data
	// disagree with Notifies[].
	Anim->RefreshCacheData();
	Anim->MarkPackageDirty();
}

bool ResolveSection(UAnimMontage* Montage, const FString& SectionName,
	int32& OutIndex, FMCPToolResult& OutError)
{
	if (!IsValid(Montage))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("Montage is invalid."));
		return false;
	}

	const int32 Index = Montage->GetSectionIndex(FName(*SectionName));
	if (Index != INDEX_NONE)
	{
		OutIndex = Index;
		return true;
	}

	TArray<FString> Names;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		Names.Add(Section.SectionName.ToString());
	}

	OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
		FString::Printf(TEXT("Montage '%s' has no section named '%s'."), *Montage->GetName(), *SectionName),
		TEXT("Use montage_get_sections to list sections, or montage_add_section to create one."),
		Names);
	return false;
}

bool ResolveSlot(UAnimMontage* Montage, const FString& SlotName,
	int32& OutIndex, FMCPToolResult& OutError)
{
	if (!IsValid(Montage))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("Montage is invalid."));
		return false;
	}

	const FName Wanted(*SlotName);
	for (int32 i = 0; i < Montage->SlotAnimTracks.Num(); ++i)
	{
		if (Montage->SlotAnimTracks[i].SlotName == Wanted)
		{
			OutIndex = i;
			return true;
		}
	}

	TArray<FString> Names;
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		Names.Add(Track.SlotName.ToString());
	}

	OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
		FString::Printf(TEXT("Montage '%s' has no slot track named '%s'."), *Montage->GetName(), *SlotName),
		TEXT("Use montage_add_slot to add one, or get_anim_montage_info to list existing slots."),
		Names);
	return false;
}

TSharedPtr<FJsonObject> MontageToJson(UAnimMontage* Montage, bool bIncludeNotifies)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!IsValid(Montage))
	{
		return Result;
	}

	Result->SetStringField(TEXT("name"), Montage->GetName());
	Result->SetStringField(TEXT("path"), Montage->GetPathName());
	Result->SetNumberField(TEXT("duration"), Montage->GetPlayLength());
	Result->SetNumberField(TEXT("rate_scale"), Montage->RateScale);
	Result->SetBoolField(TEXT("enable_auto_blend_out"), Montage->bEnableAutoBlendOut);

	if (USkeleton* Skeleton = Montage->GetSkeleton())
	{
		Result->SetStringField(TEXT("skeleton"), Skeleton->GetName());
		Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
	}

	// Blend settings
	TSharedPtr<FJsonObject> Blend = MakeShared<FJsonObject>();
	Blend->SetNumberField(TEXT("blend_in_time"), Montage->BlendIn.GetBlendTime());
	Blend->SetNumberField(TEXT("blend_out_time"), Montage->BlendOut.GetBlendTime());
	Blend->SetNumberField(TEXT("blend_out_trigger_time"), Montage->BlendOutTriggerTime);
	Blend->SetStringField(TEXT("blend_in_option"),
		StaticEnum<EAlphaBlendOption>()->GetNameStringByValue((int64)Montage->BlendIn.GetBlendOption()));
	Blend->SetStringField(TEXT("blend_out_option"),
		StaticEnum<EAlphaBlendOption>()->GetNameStringByValue((int64)Montage->BlendOut.GetBlendOption()));
	Blend->SetStringField(TEXT("blend_profile_in"),
		Montage->BlendProfileIn ? Montage->BlendProfileIn->GetName() : TEXT(""));
	Blend->SetStringField(TEXT("blend_profile_out"),
		Montage->BlendProfileOut ? Montage->BlendProfileOut->GetName() : TEXT(""));
	Result->SetObjectField(TEXT("blend"), Blend);

	TSharedPtr<FJsonObject> Sync = MakeShared<FJsonObject>();
	Sync->SetStringField(TEXT("sync_group"), Montage->SyncGroup.ToString());
	Sync->SetNumberField(TEXT("sync_slot_index"), Montage->SyncSlotIndex);
	Result->SetObjectField(TEXT("sync"), Sync);

	// Sections, in time order, with the resolved next-section link.
	TArray<TSharedPtr<FJsonValue>> Sections;
	for (int32 i = 0; i < Montage->CompositeSections.Num(); ++i)
	{
		const FCompositeSection& Section = Montage->CompositeSections[i];

		float StartTime = 0.f, EndTime = 0.f;
		Montage->GetSectionStartAndEndTime(i, StartTime, EndTime);

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("index"), i);
		Obj->SetStringField(TEXT("name"), Section.SectionName.ToString());
		Obj->SetNumberField(TEXT("start_time"), StartTime);
		Obj->SetNumberField(TEXT("end_time"), EndTime);
		Obj->SetNumberField(TEXT("length"), Montage->GetSectionLength(i));
		Obj->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
		Obj->SetBoolField(TEXT("loops_to_self"), Section.NextSectionName == Section.SectionName);
		Sections.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Result->SetArrayField(TEXT("sections"), Sections);
	Result->SetNumberField(TEXT("section_count"), Sections.Num());

	// Slot tracks and their segments.
	TArray<TSharedPtr<FJsonValue>> Slots;
	for (int32 SlotIdx = 0; SlotIdx < Montage->SlotAnimTracks.Num(); ++SlotIdx)
	{
		const FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[SlotIdx];

		TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetNumberField(TEXT("index"), SlotIdx);
		SlotObj->SetStringField(TEXT("slot_name"), SlotTrack.SlotName.ToString());

		TArray<TSharedPtr<FJsonValue>> Segments;
		for (int32 SegIdx = 0; SegIdx < SlotTrack.AnimTrack.AnimSegments.Num(); ++SegIdx)
		{
			const FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments[SegIdx];
			UAnimSequenceBase* Ref = Segment.GetAnimReference();

			TSharedPtr<FJsonObject> SegObj = MakeShared<FJsonObject>();
			SegObj->SetNumberField(TEXT("index"), SegIdx);
			SegObj->SetStringField(TEXT("animation"), IsValid(Ref) ? Ref->GetName() : TEXT("(none)"));
			SegObj->SetStringField(TEXT("animation_path"), IsValid(Ref) ? Ref->GetPathName() : TEXT(""));
			SegObj->SetNumberField(TEXT("start_pos"), Segment.StartPos);
			SegObj->SetNumberField(TEXT("anim_start_time"), Segment.AnimStartTime);
			SegObj->SetNumberField(TEXT("anim_end_time"), Segment.AnimEndTime);
			SegObj->SetNumberField(TEXT("anim_play_rate"), Segment.AnimPlayRate);
			SegObj->SetNumberField(TEXT("loop_count"), Segment.LoopingCount);
			Segments.Add(MakeShared<FJsonValueObject>(SegObj));
		}
		SlotObj->SetArrayField(TEXT("segments"), Segments);
		SlotObj->SetNumberField(TEXT("segment_count"), Segments.Num());
		SlotObj->SetNumberField(TEXT("track_length"), SlotTrack.AnimTrack.GetLength());
		Slots.Add(MakeShared<FJsonValueObject>(SlotObj));
	}
	Result->SetArrayField(TEXT("slots"), Slots);
	Result->SetNumberField(TEXT("slot_count"), Slots.Num());

	if (bIncludeNotifies)
	{
		TArray<TSharedPtr<FJsonValue>> Notifies;
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), Event.NotifyName.ToString());
			Obj->SetNumberField(TEXT("trigger_time"), Event.GetTriggerTime());
			Obj->SetNumberField(TEXT("duration"), Event.GetDuration());
			Obj->SetBoolField(TEXT("is_state"), Event.NotifyStateClass != nullptr);
			Obj->SetBoolField(TEXT("is_branching_point"), Event.IsBranchingPoint());

			FString ClassName = TEXT("Simple Notify");
			if (Event.NotifyStateClass) { ClassName = Event.NotifyStateClass->GetClass()->GetName(); }
			else if (Event.Notify)      { ClassName = Event.Notify->GetClass()->GetName(); }
			Obj->SetStringField(TEXT("class"), ClassName);

			Notifies.Add(MakeShared<FJsonValueObject>(Obj));
		}
		Result->SetArrayField(TEXT("notifies"), Notifies);
		Result->SetNumberField(TEXT("notify_count"), Notifies.Num());
	}

	return Result;
}

// =====================================================================
// AnimBlueprint graph navigation
// =====================================================================

UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBP)
{
	if (!IsValid(AnimBP))
	{
		return nullptr;
	}

	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (IsValid(Graph) && Graph->GetFName() == TEXT("AnimGraph"))
		{
			return Graph;
		}
	}

	// Fall back to the first graph using the animation schema — covers AnimBPs
	// whose main graph was renamed.
	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (IsValid(Graph) && Graph->GetSchema() &&
			Graph->GetSchema()->IsA<UAnimationGraphSchema>())
		{
			return Graph;
		}
	}
	return nullptr;
}

/** Every graph in the AnimBP that could host a state machine node. */
static void GatherCandidateGraphs(UAnimBlueprint* AnimBP, TArray<UEdGraph*>& OutGraphs)
{
	OutGraphs.Append(AnimBP->FunctionGraphs);
	OutGraphs.Append(AnimBP->UbergraphPages);

	// State machines can be nested inside other state machines' state graphs.
	for (int32 i = 0; i < OutGraphs.Num(); ++i)
	{
		UEdGraph* Graph = OutGraphs[i];
		if (!IsValid(Graph)) continue;
		for (UEdGraph* Sub : Graph->SubGraphs)
		{
			if (IsValid(Sub) && !OutGraphs.Contains(Sub))
			{
				OutGraphs.Add(Sub);
			}
		}
	}
}

UAnimationStateMachineGraph* FindStateMachineGraph(UAnimBlueprint* AnimBP,
	const FString& MachineName, FMCPToolResult& OutError)
{
	if (!IsValid(AnimBP))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("AnimBlueprint is invalid."));
		return nullptr;
	}

	TArray<UEdGraph*> Graphs;
	GatherCandidateGraphs(AnimBP, Graphs);

	TArray<FString> Available;
	for (UEdGraph* Graph : Graphs)
	{
		if (!IsValid(Graph)) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
			if (!SMNode || !SMNode->EditorStateMachineGraph) continue;

			// The authoritative name is the bound graph's name; the node title
			// tracks it. Match on either so callers can use what they saw.
			const FString GraphName = SMNode->EditorStateMachineGraph->GetName();
			Available.AddUnique(GraphName);

			if (GraphName.Equals(MachineName, ESearchCase::IgnoreCase) ||
				SMNode->GetStateMachineName().Equals(MachineName, ESearchCase::IgnoreCase))
			{
				return SMNode->EditorStateMachineGraph;
			}
		}
	}

	OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
		FString::Printf(TEXT("AnimBlueprint '%s' has no state machine named '%s'."), *AnimBP->GetName(), *MachineName),
		TEXT("Use get_anim_state_machine_info to list state machines, or create_anim_state_machine to add one."),
		Available);
	return nullptr;
}

UAnimStateNode* FindState(UAnimationStateMachineGraph* SMGraph,
	const FString& StateName, FMCPToolResult& OutError)
{
	if (!SMGraph)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("State machine graph is invalid."));
		return nullptr;
	}

	TArray<FString> Available;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
		{
			const FString Name = State->GetStateName();
			Available.Add(Name);
			if (Name.Equals(StateName, ESearchCase::IgnoreCase))
			{
				return State;
			}
		}
	}

	OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
		FString::Printf(TEXT("State '%s' not found in state machine '%s'."), *StateName, *SMGraph->GetName()),
		TEXT("Use get_anim_state_machine_info to list states, or add_anim_state to create one."),
		Available);
	return nullptr;
}

UAnimStateTransitionNode* FindTransition(UAnimationStateMachineGraph* SMGraph,
	const FString& FromState, const FString& ToState, FMCPToolResult& OutError)
{
	if (!SMGraph)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("State machine graph is invalid."));
		return nullptr;
	}

	TArray<FString> Available;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node);
		if (!Transition) continue;

		UAnimStateNodeBase* Prev = Transition->GetPreviousState();
		UAnimStateNodeBase* Next = Transition->GetNextState();
		if (!Prev || !Next) continue;

		const FString PrevName = Prev->GetStateName();
		const FString NextName = Next->GetStateName();
		Available.Add(FString::Printf(TEXT("%s -> %s"), *PrevName, *NextName));

		if (PrevName.Equals(FromState, ESearchCase::IgnoreCase) &&
			NextName.Equals(ToState, ESearchCase::IgnoreCase))
		{
			return Transition;
		}
	}

	OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
		FString::Printf(TEXT("No transition '%s' -> '%s' in state machine '%s'."),
			*FromState, *ToState, *SMGraph->GetName()),
		TEXT("Create it with add_anim_transition first."),
		Available);
	return nullptr;
}

UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction, const FString& PreferredName)
{
	if (!IsValid(Node))
	{
		return nullptr;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != Direction || Pin->bHidden) continue;
		if (!UAnimationGraphSchema::IsPosePin(Pin->PinType)) continue;

		if (PreferredName.IsEmpty())
		{
			return Pin;
		}
		if (Pin->PinName.ToString().Equals(PreferredName, ESearchCase::IgnoreCase))
		{
			return Pin;
		}
	}
	return nullptr;
}

FString DescribePosePins(UEdGraphNode* Node)
{
	if (!IsValid(Node))
	{
		return FString();
	}

	TArray<FString> In, Out;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden) continue;
		if (!UAnimationGraphSchema::IsPosePin(Pin->PinType)) continue;
		(Pin->Direction == EGPD_Input ? In : Out).Add(Pin->PinName.ToString());
	}

	return FString::Printf(TEXT("input pose pins: [%s]; output pose pins: [%s]"),
		*FString::Join(In, TEXT(", ")), *FString::Join(Out, TEXT(", ")));
}

bool CompileAnimBlueprint(UAnimBlueprint* AnimBP, bool bSave, TSharedPtr<FJsonObject>& OutResult)
{
	OutResult = MakeShared<FJsonObject>();
	if (!IsValid(AnimBP))
	{
		OutResult->SetBoolField(TEXT("compiled"), false);
		OutResult->SetStringField(TEXT("error"), TEXT("AnimBlueprint is invalid."));
		return false;
	}

	OutResult->SetStringField(TEXT("anim_blueprint"), AnimBP->GetName());
	OutResult->SetStringField(TEXT("path"), AnimBP->GetPathName());

	FCompilerResultsLog Results;
	Results.bSilentMode = true;
	FKismetEditorUtilities::CompileBlueprint(AnimBP, EBlueprintCompileOptions::None, &Results);

	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("severity"),
			Message->GetSeverity() == EMessageSeverity::Error ? TEXT("error") : TEXT("warning"));
		Obj->SetStringField(TEXT("text"), Message->ToText().ToString());
		Messages.Add(MakeShared<FJsonValueObject>(Obj));

		// Cap the payload — a badly broken graph can emit hundreds.
		if (Messages.Num() >= 50) break;
	}

	const bool bOk = (Results.NumErrors == 0);
	OutResult->SetBoolField(TEXT("compiled"), bOk);
	OutResult->SetNumberField(TEXT("error_count"), Results.NumErrors);
	OutResult->SetNumberField(TEXT("warning_count"), Results.NumWarnings);
	OutResult->SetArrayField(TEXT("messages"), Messages);
	OutResult->SetBoolField(TEXT("has_generated_class"), AnimBP->GetAnimBlueprintGeneratedClass() != nullptr);

	if (bSave && bOk)
	{
		FMCPToolResult SaveError;
		const bool bSaved = SaveExistingAsset(AnimBP, SaveError);
		OutResult->SetBoolField(TEXT("saved"), bSaved);
	}
	else
	{
		OutResult->SetBoolField(TEXT("saved"), false);
	}

	return bOk;
}

UBlendProfile* ResolveBlendProfile(USkeleton* Skeleton, const FString& ProfileName, FMCPToolResult& OutError)
{
	if (!IsValid(Skeleton))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
			TEXT("The asset has no Skeleton, so its blend profiles cannot be resolved."));
		return nullptr;
	}

	if (UBlendProfile* Profile = Skeleton->GetBlendProfile(FName(*ProfileName)))
	{
		return Profile;
	}

	TArray<FString> Available;
	for (const TObjectPtr<UBlendProfile>& Profile : Skeleton->BlendProfiles)
	{
		if (Profile) { Available.Add(Profile->GetName()); }
	}

	OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
		FString::Printf(TEXT("Skeleton '%s' has no blend profile named '%s'."), *Skeleton->GetName(), *ProfileName),
		TEXT("Blend profiles are authored in the Skeleton editor (Blend Profiles panel); they cannot be created from script."),
		Available);
	return nullptr;
}

} // namespace MCPAnimTools::Common
