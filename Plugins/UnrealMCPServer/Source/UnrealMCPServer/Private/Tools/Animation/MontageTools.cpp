// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.6 Phase 1 — montage authoring.
//
// v4.5 could create a montage and read it back, and nothing else. A montage's
// entire usefulness is in its structure: sections that chain into each other
// (combos, loops), slot tracks that map onto AnimGraph slot nodes, and blend
// in/out settings that decide how it enters and leaves the pose. This file
// makes all of that reachable.
//
// Section time model (worth stating once, it is the source of most confusion):
// a section has a start time only. Its END is the start of the next section by
// time, or the montage length. So moving one section's time silently resizes
// its neighbour — montage_set_section_time reports both.

#include "Tools/Animation/AnimCommon.h"
#include "Common/MCPAssetResolver.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"
#include "AlphaBlend.h"
#include "Factories/AnimMontageFactory.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Common/MCPAssetCreate.h"

namespace MCPAnimTools::Montage
{

using namespace MCPAnimTools::Common;

// ---------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------

/** Sort CompositeSections by start time.
 *
 *  This has to live here: UAnimMontage::SortAnimCompositeSectionByPos() is
 *  private, and AddAnimCompositeSection() only appends — it never sorts. That
 *  matters because GetSectionStartAndEndTime() derives a section's END from
 *  CompositeSections[Index + 1], i.e. it assumes the array is in time order.
 *  Leave it unsorted and every section length past the insertion point is
 *  wrong. */
static void SortSectionsByTime(UAnimMontage* Montage)
{
	Montage->CompositeSections.Sort(
		[](const FCompositeSection& A, const FCompositeSection& B)
		{
			return A.GetTime() < B.GetTime();
		});
}

/** Recompute derived montage state after a structural edit: length from the slot
 *  tracks, then relink every linkable element (sections and notifies hold
 *  segment-relative positions, which go stale the moment a segment moves).
 *
 *  UAnimMontage::UpdateCommonTargetFrameRate() would belong here too — the
 *  factory calls it — but it is private. PostEditChange() invokes it, and
 *  RefreshCacheData() below covers the caches we actually depend on. */
static void RefreshMontage(UAnimMontage* Montage)
{
	Montage->SetCompositeLength(Montage->CalculateSequenceLength());
	Montage->UpdateLinkableElements();
	SortSectionsByTime(Montage);
	Montage->RefreshCacheData();
	Montage->MarkPackageDirty();
}

/** A montage must always own a section starting at 0 or playback has no entry
 *  point. Same invariant UAnimMontageFactory::EnsureStartingSection keeps. */
static void EnsureStartingSection(UAnimMontage* Montage)
{
	if (Montage->CompositeSections.Num() == 0)
	{
		FCompositeSection NewSection;
		NewSection.SetTime(0.0f);
		NewSection.SectionName = FName(TEXT("Default"));
		Montage->CompositeSections.Add(NewSection);
	}
	if (Montage->CompositeSections[0].GetTime() > 0.0f)
	{
		Montage->CompositeSections[0].SetTime(0.0f);
	}
}

/** Load a montage by path with a structured NotFound. */
static UAnimMontage* LoadMontage(const TSharedPtr<FJsonObject>& Args, FString& OutPath, FMCPToolResult& OutError)
{
	FMCPValidateResult Check = FMCPValidate::RequiredString(Args, TEXT("asset_path"), OutPath);
	if (!Check.bOk)
	{
		OutError = Check.Error;
		return nullptr;
	}
	return MCPCommon::LoadAssetChecked<UAnimMontage>(OutPath, OutError);
}

/** Parse an EAlphaBlendOption by name. */
static bool ParseBlendOption(const FString& Name, EAlphaBlendOption& Out, FMCPToolResult& OutError)
{
	const UEnum* Enum = StaticEnum<EAlphaBlendOption>();
	const int64 Value = Enum->GetValueByNameString(Name);
	if (Value == INDEX_NONE)
	{
		TArray<FString> Valid;
		for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
		{
			Valid.Add(Enum->GetNameStringByIndex(i));
		}
		OutError = FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
			FString::Printf(TEXT("Unknown blend option '%s'."), *Name), FString(), Valid);
		return false;
	}
	Out = static_cast<EAlphaBlendOption>(Value);
	return true;
}

// ---------------------------------------------------------------------

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// montage_get_sections
	// ================================================================
	MCP_TOOL(Registry, "montage_get_sections")
		.Description(TEXT(
			"Read a montage's full structure: sections (start/end time, length, and the next section each "
			"one chains to), slot tracks with their animation segments, blend in/out settings, and sync group. "
			"This is the read-back tool for montage authoring — call it after any montage_* edit to verify the "
			"result. A section's end time is implicit: it is the start of the next section by time, or the "
			"montage length."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage (e.g. '/Game/Characters/AM_HeroAttack')"), true)
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/AM_HeroAttack\"}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ true);
			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// montage_add_section
	// ================================================================
	MCP_TOOL(Registry, "montage_add_section")
		.Description(TEXT(
			"Add a named composite section to a montage at a given time. Sections are the addressable entry "
			"points for Montage_JumpToSection() at runtime — a combo is built as several sections on one "
			"montage. Adding a section splits the timeline: the previous section now ends where this one "
			"begins. Use montage_link_sections afterwards to control what plays next."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage"), true)
		.StringArg(TEXT("section_name"), TEXT("Name for the new section (e.g. 'Combo2'). Must be unique within the montage."), true)
		.NumberArg(TEXT("start_time"), TEXT("Section start time in seconds, within [0, montage duration]"), true)
		.StringArg(TEXT("next_section"), TEXT("Optional: name of the section to chain to when this one ends. Pass this section's own name to loop it."))
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/AM_HeroAttack\", \"section_name\": \"Combo2\", \"start_time\": 0.85}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			FString SectionName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("section_name"), SectionName));

			double StartTime = 0.0;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("start_time"), StartTime));
			if (!RequireTimeInRange(Montage, StartTime, TEXT("start_time"), Err)) return Err;

			if (Montage->IsValidSectionName(FName(*SectionName)))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::AlreadyExists,
					FString::Printf(TEXT("Montage '%s' already has a section named '%s'."), *Montage->GetName(), *SectionName),
					TEXT("Use montage_set_section_time to move the existing section instead."));
			}

			FString NextSection;
			Args->TryGetStringField(TEXT("next_section"), NextSection);
			if (!NextSection.IsEmpty() &&
				!NextSection.Equals(SectionName) &&
				!Montage->IsValidSectionName(FName(*NextSection)))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("next_section '%s' does not exist on this montage."), *NextSection),
					TEXT("Add that section first, or omit next_section and call montage_link_sections once both exist."));
			}

			// Note which section (if any) the engine will auto-chain to the new
			// one: AddAnimCompositeSection links the previously-appended section
			// to this one when its NextSectionName was unset. Capture it before
			// the call so the result can report the side effect.
			const FString AutoLinkedFrom = Montage->CompositeSections.Num() > 0 &&
				Montage->CompositeSections.Last().NextSectionName.IsNone()
					? Montage->CompositeSections.Last().SectionName.ToString()
					: FString();

			Montage->Modify();
			const int32 NewIndex = Montage->AddAnimCompositeSection(FName(*SectionName), (float)StartTime);
			if (NewIndex == INDEX_NONE)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("AddAnimCompositeSection refused the section."),
					TEXT("Section names must be unique within a montage."));
			}

			// RefreshMontage re-sorts by time, which invalidates NewIndex — so
			// sort first, then resolve by name to apply the explicit link.
			RefreshMontage(Montage);

			if (!NextSection.IsEmpty())
			{
				int32 Index = INDEX_NONE;
				if (ResolveSection(Montage, SectionName, Index, Err))
				{
					Montage->CompositeSections[Index].NextSectionName = FName(*NextSection);
				}
			}

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ false);
			Result->SetStringField(TEXT("added_section"), SectionName);
			if (!AutoLinkedFrom.IsEmpty())
			{
				Result->SetStringField(TEXT("auto_linked_from"), AutoLinkedFrom);
			}

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Added section '%s' at %.3fs to montage '%s' (%d sections total).%s"),
					*SectionName, StartTime, *Montage->GetName(), Montage->CompositeSections.Num(),
					AutoLinkedFrom.IsEmpty()
						? TEXT("")
						: *FString::Printf(TEXT(" The engine auto-chained '%s' to it (it had no next section); change that with montage_link_sections."), *AutoLinkedFrom)),
				Result);
		});

	// ================================================================
	// montage_remove_section
	// ================================================================
	MCP_TOOL(Registry, "montage_remove_section")
		.Description(TEXT(
			"Remove a named composite section from a montage. Refuses to remove the last remaining section — "
			"a montage with no section starting at 0 has no playback entry point. Any other section that "
			"chained to the removed one has its next_section cleared, and those are reported."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage"), true)
		.StringArg(TEXT("section_name"), TEXT("Name of the section to remove"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			FString SectionName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("section_name"), SectionName));

			int32 Index = INDEX_NONE;
			if (!ResolveSection(Montage, SectionName, Index, Err)) return Err;

			if (Montage->CompositeSections.Num() <= 1)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("Cannot remove the montage's only section."),
					TEXT("A montage needs at least one section starting at 0.0 to be playable. Add a replacement section first."));
			}

			Montage->Modify();

			// Clear inbound links before the indices shift.
			TArray<FString> Relinked;
			const FName Removed(*SectionName);
			for (FCompositeSection& Section : Montage->CompositeSections)
			{
				if (Section.NextSectionName == Removed && Section.SectionName != Removed)
				{
					Section.NextSectionName = NAME_None;
					Relinked.Add(Section.SectionName.ToString());
				}
			}

			if (!Montage->DeleteAnimCompositeSection(Index))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("DeleteAnimCompositeSection failed for '%s'."), *SectionName));
			}

			EnsureStartingSection(Montage);
			RefreshMontage(Montage);

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ false);
			TArray<TSharedPtr<FJsonValue>> RelinkedJson;
			for (const FString& Name : Relinked) { RelinkedJson.Add(MakeShared<FJsonValueString>(Name)); }
			Result->SetArrayField(TEXT("cleared_next_section_on"), RelinkedJson);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Removed section '%s' from '%s'.%s"),
					*SectionName, *Montage->GetName(),
					Relinked.Num() > 0
						? *FString::Printf(TEXT(" Cleared next_section on: %s."), *FString::Join(Relinked, TEXT(", ")))
						: TEXT("")),
				Result);
		});

	// ================================================================
	// montage_set_section_time
	// ================================================================
	MCP_TOOL(Registry, "montage_set_section_time")
		.Description(TEXT(
			"Move a montage section to a new start time. Because a section's end is implicit (the next "
			"section's start), moving one section resizes its neighbours; the result reports every section's "
			"new start/end so the effect is visible. The section starting at 0.0 cannot be moved off 0."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage"), true)
		.StringArg(TEXT("section_name"), TEXT("Name of the section to move"), true)
		.NumberArg(TEXT("start_time"), TEXT("New start time in seconds, within [0, montage duration]"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			FString SectionName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("section_name"), SectionName));

			double StartTime = 0.0;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("start_time"), StartTime));
			if (!RequireTimeInRange(Montage, StartTime, TEXT("start_time"), Err)) return Err;

			int32 Index = INDEX_NONE;
			if (!ResolveSection(Montage, SectionName, Index, Err)) return Err;

			if (Index == 0 && StartTime > 0.0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("Section '%s' is the montage's first section and must start at 0.0."), *SectionName),
					TEXT("Add a new section before it instead, then move this one."));
			}

			Montage->Modify();
			// Link() rather than SetTime(): a section stores its position as a
			// segment index plus an offset within that segment, so it has to be
			// re-bound to whichever segment now contains the new time.
			Montage->CompositeSections[Index].Link(Montage, (float)StartTime);
			RefreshMontage(Montage);

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ false);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Moved section '%s' to %.3fs on '%s'."), *SectionName, StartTime, *Montage->GetName()),
				Result);
		});

	// ================================================================
	// montage_link_sections
	// ================================================================
	MCP_TOOL(Registry, "montage_link_sections")
		.Description(TEXT(
			"Set which section plays after a given section finishes (FCompositeSection::NextSectionName). "
			"This is how montage flow is authored: point a section at itself to loop it (an idle-hold or a "
			"charge-up), point it at the next combo step to chain, or clear it so the montage blends out. "
			"Clear by passing an empty next_section."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage"), true)
		.StringArg(TEXT("section_name"), TEXT("Section whose successor is being set"), true)
		.StringArg(TEXT("next_section"), TEXT("Section to play next. Pass the same name as section_name to loop. Pass an empty string to clear the link (montage blends out)."), true)
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/AM_HeroAttack\", \"section_name\": \"Combo1\", \"next_section\": \"Combo2\"}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			FString SectionName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("section_name"), SectionName));

			// next_section is required but may legitimately be empty (= clear).
			FString NextSection;
			if (!Args->HasField(TEXT("next_section")))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("next_section is required (pass an empty string to clear the link)."));
			}
			Args->TryGetStringField(TEXT("next_section"), NextSection);

			int32 Index = INDEX_NONE;
			if (!ResolveSection(Montage, SectionName, Index, Err)) return Err;

			if (!NextSection.IsEmpty())
			{
				int32 NextIndex = INDEX_NONE;
				if (!ResolveSection(Montage, NextSection, NextIndex, Err)) return Err;
			}

			Montage->Modify();
			Montage->CompositeSections[Index].NextSectionName = NextSection.IsEmpty() ? NAME_None : FName(*NextSection);
			Montage->MarkPackageDirty();

			const bool bLoops = !NextSection.IsEmpty() && NextSection.Equals(SectionName);

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ false);
			return FMCPToolResult::SuccessStructured(
				NextSection.IsEmpty()
					? FString::Printf(TEXT("Cleared next_section on '%s'; the montage will blend out after it."), *SectionName)
					: FString::Printf(TEXT("Section '%s' now chains to '%s'%s."), *SectionName, *NextSection,
						bLoops ? TEXT(" (loops on itself)") : TEXT("")),
				Result);
		});

	// ================================================================
	// montage_add_slot
	// ================================================================
	MCP_TOOL(Registry, "montage_add_slot")
		.Description(TEXT(
			"Add a slot track to a montage and register the slot name on the target skeleton. Registration is "
			"the part that is easy to miss: an unregistered slot never appears in the AnimGraph's Slot node "
			"dropdown, so the montage plays into nothing. Slots let one montage drive different parts of the "
			"AnimGraph (e.g. 'DefaultSlot' for full body, 'UpperBody' for an additive layer)."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage"), true)
		.StringArg(TEXT("slot_name"), TEXT("Slot name (e.g. 'UpperBody'). Registered on the skeleton if not already present."), true)
		.StringArg(TEXT("slot_group"), TEXT("Skeleton slot group to file the slot under (default: 'DefaultGroup')"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			FString SlotName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("slot_name"), SlotName));

			FString SlotGroup = FAnimSlotGroup::DefaultGroupName.ToString();
			Args->TryGetStringField(TEXT("slot_group"), SlotGroup);

			int32 Existing = INDEX_NONE;
			FMCPToolResult Ignored;
			if (ResolveSlot(Montage, SlotName, Existing, Ignored))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::AlreadyExists,
					FString::Printf(TEXT("Montage '%s' already has a slot track named '%s'."), *Montage->GetName(), *SlotName));
			}

			USkeleton* Skeleton = Montage->GetSkeleton();
			if (!IsValid(Skeleton))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Montage '%s' has no target skeleton; the slot cannot be registered."), *Montage->GetName()));
			}

			Montage->Modify();
			Skeleton->Modify();

			// Register on the skeleton first so the slot is visible to AnimGraph
			// Slot nodes, then file it under the requested group.
			const FName SlotFName(*SlotName);
			const bool bNewlyRegistered = Skeleton->RegisterSlotNode(SlotFName);

			const FName GroupFName(*SlotGroup);
			if (!Skeleton->FindAnimSlotGroup(GroupFName))
			{
				Skeleton->AddSlotGroupName(GroupFName);
			}
			Skeleton->SetSlotGroupName(SlotFName, GroupFName);

			Montage->AddSlot(SlotFName);
			RefreshMontage(Montage);

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ false);
			Result->SetStringField(TEXT("slot_group"), SlotGroup);
			Result->SetBoolField(TEXT("registered_on_skeleton"), bNewlyRegistered);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Added slot '%s' (group '%s') to montage '%s'; %s on skeleton '%s'. Add an AnimGraph Slot node for this slot with animgraph_add_node."),
					*SlotName, *SlotGroup, *Montage->GetName(),
					bNewlyRegistered ? TEXT("registered") : TEXT("already registered"),
					*Skeleton->GetName()),
				Result);
		});

	// ================================================================
	// montage_add_segment
	// ================================================================
	MCP_TOOL(Registry, "montage_add_segment")
		.Description(TEXT(
			"Append an animation segment to one of a montage's slot tracks. Multi-segment slot tracks are how "
			"a montage stitches several sequences into one timeline (windup / strike / recovery as three "
			"clips). The source animation must share the montage's skeleton. Segments are appended at the end "
			"of the track unless start_time is given, and the montage length grows to fit."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage"), true)
		.StringArg(TEXT("animation_path"), TEXT("Content path to the UAnimSequence to append"), true)
		.StringArg(TEXT("slot_name"), TEXT("Slot track to append to (default: the montage's first slot)"))
		.NumberArg(TEXT("start_time"), TEXT("Position of the segment on the montage timeline in seconds (default: end of the track)"))
		.NumberArg(TEXT("anim_start_time"), TEXT("Trim: where in the source animation the segment starts (default: 0)"))
		.NumberArg(TEXT("anim_end_time"), TEXT("Trim: where in the source animation the segment ends (default: source play length)"))
		.NumberArg(TEXT("play_rate"), TEXT("Segment play rate multiplier (default: 1.0)"))
		.IntArg(TEXT("loop_count"), TEXT("How many times the segment repeats (default: 1)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			FString AnimPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("animation_path"), AnimPath));
			UAnimSequence* Anim = MCPCommon::LoadAssetChecked<UAnimSequence>(AnimPath, Err);
			if (!Anim) return Err;

			if (!RequireSameSkeleton(Montage->GetSkeleton(), Anim, TEXT("animation_path"), Err)) return Err;

			// Resolve the target slot track.
			int32 SlotIndex = 0;
			FString SlotName;
			if (Args->TryGetStringField(TEXT("slot_name"), SlotName) && !SlotName.IsEmpty())
			{
				if (!ResolveSlot(Montage, SlotName, SlotIndex, Err)) return Err;
			}
			else if (Montage->SlotAnimTracks.Num() == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Montage '%s' has no slot tracks."), *Montage->GetName()),
					TEXT("Add one with montage_add_slot first."));
			}

			FAnimTrack& Track = Montage->SlotAnimTracks[SlotIndex].AnimTrack;

			const float SourceLength = Anim->GetPlayLength();

			float AnimStart = 0.0f;
			float AnimEnd = SourceLength;
			if (Args->HasField(TEXT("anim_start_time"))) AnimStart = (float)Args->GetNumberField(TEXT("anim_start_time"));
			if (Args->HasField(TEXT("anim_end_time")))   AnimEnd   = (float)Args->GetNumberField(TEXT("anim_end_time"));

			if (AnimStart < 0.0f || AnimStart > SourceLength)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("anim_start_time %.4f is outside '%s' range [0, %.4f]."), AnimStart, *Anim->GetName(), SourceLength));
			}
			if (AnimEnd <= AnimStart || AnimEnd > SourceLength)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("anim_end_time %.4f must be greater than anim_start_time (%.4f) and at most %.4f."),
						AnimEnd, AnimStart, SourceLength));
			}

			float PlayRate = 1.0f;
			if (Args->HasField(TEXT("play_rate"))) PlayRate = (float)Args->GetNumberField(TEXT("play_rate"));
			if (FMath::IsNearlyZero(PlayRate))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("play_rate cannot be zero (segment length divides by it)."));
			}

			int32 LoopCount = 1;
			if (Args->HasField(TEXT("loop_count"))) LoopCount = (int32)Args->GetNumberField(TEXT("loop_count"));
			BAIL_IF_INVALID(FMCPValidate::InRangeI(LoopCount, 1, 1000, TEXT("loop_count")));

			const float StartPos = Args->HasField(TEXT("start_time"))
				? (float)Args->GetNumberField(TEXT("start_time"))
				: Track.GetLength();
			if (StartPos < 0.0f)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange, TEXT("start_time cannot be negative."));
			}

			Montage->Modify();

			FAnimSegment Segment;
			Segment.SetAnimReference(Anim, /*bInitialize*/ true);
			Segment.StartPos = StartPos;
			Segment.AnimStartTime = AnimStart;
			Segment.AnimEndTime = AnimEnd;
			Segment.AnimPlayRate = PlayRate;
			Segment.LoopingCount = LoopCount;

			const int32 SegmentIndex = Track.AnimSegments.Add(Segment);
			Track.SortAnimSegments();
			RefreshMontage(Montage);

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ false);
			Result->SetNumberField(TEXT("added_segment_index"), SegmentIndex);
			Result->SetStringField(TEXT("slot_name"), Montage->SlotAnimTracks[SlotIndex].SlotName.ToString());

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Appended '%s' to slot '%s' of montage '%s' at %.3fs; montage is now %.3fs long."),
					*Anim->GetName(), *Montage->SlotAnimTracks[SlotIndex].SlotName.ToString(),
					*Montage->GetName(), StartPos, Montage->GetPlayLength()),
				Result);
		});

	// ================================================================
	// montage_remove_segment
	// ================================================================
	MCP_TOOL(Registry, "montage_remove_segment")
		.Description(TEXT(
			"Remove an animation segment from a montage slot track by index (see montage_get_sections for "
			"segment indices). The montage length is recomputed and sections are relinked, so removing a "
			"segment can shorten the montage and pull later sections past its end."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage"), true)
		.IntArg(TEXT("segment_index"), TEXT("Index of the segment within the slot track"), true)
		.StringArg(TEXT("slot_name"), TEXT("Slot track to remove from (default: the montage's first slot)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			double SegmentIndexRaw = 0.0;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("segment_index"), SegmentIndexRaw));
			const int32 SegmentIndex = (int32)SegmentIndexRaw;

			int32 SlotIndex = 0;
			FString SlotName;
			if (Args->TryGetStringField(TEXT("slot_name"), SlotName) && !SlotName.IsEmpty())
			{
				if (!ResolveSlot(Montage, SlotName, SlotIndex, Err)) return Err;
			}
			else if (Montage->SlotAnimTracks.Num() == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Montage '%s' has no slot tracks."), *Montage->GetName()));
			}

			FAnimTrack& Track = Montage->SlotAnimTracks[SlotIndex].AnimTrack;
			if (!Track.AnimSegments.IsValidIndex(SegmentIndex))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("segment_index %d is out of range; slot '%s' has %d segment(s)."),
						SegmentIndex, *Montage->SlotAnimTracks[SlotIndex].SlotName.ToString(), Track.AnimSegments.Num()));
			}

			UAnimSequenceBase* Removed = Track.AnimSegments[SegmentIndex].GetAnimReference();
			const FString RemovedName = IsValid(Removed) ? Removed->GetName() : TEXT("(none)");

			Montage->Modify();
			Track.AnimSegments.RemoveAt(SegmentIndex);
			RefreshMontage(Montage);

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ false);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Removed segment %d ('%s') from slot '%s'; montage is now %.3fs long."),
					SegmentIndex, *RemovedName,
					*Montage->SlotAnimTracks[SlotIndex].SlotName.ToString(), Montage->GetPlayLength()),
				Result);
		});

	// ================================================================
	// montage_set_blend_settings
	// ================================================================
	MCP_TOOL(Registry, "montage_set_blend_settings")
		.Description(TEXT(
			"Set a montage's blend in/out timing and curve. blend_out_trigger_time is the subtle one: a "
			"negative value (the default) means the blend out finishes exactly as the montage ends, so the "
			"blend eats the tail of the animation; a value >= 0 starts the blend that many seconds before the "
			"end instead. Set enable_auto_blend_out=false for montages you intend to end explicitly with "
			"Montage_Stop."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage"), true)
		.NumberArg(TEXT("blend_in_time"), TEXT("Blend in duration in seconds (e.g. 0.25)"))
		.NumberArg(TEXT("blend_out_time"), TEXT("Blend out duration in seconds (e.g. 0.25)"))
		.NumberArg(TEXT("blend_out_trigger_time"), TEXT("When to start blending out. Negative (default) = blend finishes as the montage ends. >= 0 = start blending that many seconds before the end."))
		.EnumArg(TEXT("blend_in_option"), TEXT("Interpolation curve for the blend in"),
			{ TEXT("Linear"), TEXT("Cubic"), TEXT("HermiteCubic"), TEXT("Sinusoidal"), TEXT("QuadraticInOut"),
			  TEXT("CubicInOut"), TEXT("QuarticInOut"), TEXT("QuinticInOut"), TEXT("CircularIn"),
			  TEXT("CircularOut"), TEXT("CircularInOut"), TEXT("ExpIn"), TEXT("ExpOut"), TEXT("ExpInOut"), TEXT("Custom") })
		.EnumArg(TEXT("blend_out_option"), TEXT("Interpolation curve for the blend out"),
			{ TEXT("Linear"), TEXT("Cubic"), TEXT("HermiteCubic"), TEXT("Sinusoidal"), TEXT("QuadraticInOut"),
			  TEXT("CubicInOut"), TEXT("QuarticInOut"), TEXT("QuinticInOut"), TEXT("CircularIn"),
			  TEXT("CircularOut"), TEXT("CircularInOut"), TEXT("ExpIn"), TEXT("ExpOut"), TEXT("ExpInOut"), TEXT("Custom") })
		.BoolArg(TEXT("enable_auto_blend_out"), TEXT("Whether the montage blends out automatically when it reaches the end (default: true)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/AM_HeroAttack\", \"blend_in_time\": 0.15, \"blend_out_time\": 0.25, \"blend_out_trigger_time\": 0.2}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			bool bChanged = false;
			Montage->Modify();

			if (Args->HasField(TEXT("blend_in_time")))
			{
				const double Value = Args->GetNumberField(TEXT("blend_in_time"));
				BAIL_IF_INVALID(FMCPValidate::InRangeF(Value, 0.0, 60.0, TEXT("blend_in_time")));
				Montage->BlendIn.SetBlendTime((float)Value);
				bChanged = true;
			}
			if (Args->HasField(TEXT("blend_out_time")))
			{
				const double Value = Args->GetNumberField(TEXT("blend_out_time"));
				BAIL_IF_INVALID(FMCPValidate::InRangeF(Value, 0.0, 60.0, TEXT("blend_out_time")));
				Montage->BlendOut.SetBlendTime((float)Value);
				bChanged = true;
			}
			if (Args->HasField(TEXT("blend_out_trigger_time")))
			{
				Montage->BlendOutTriggerTime = (float)Args->GetNumberField(TEXT("blend_out_trigger_time"));
				bChanged = true;
			}

			FString OptionName;
			if (Args->TryGetStringField(TEXT("blend_in_option"), OptionName) && !OptionName.IsEmpty())
			{
				EAlphaBlendOption Option;
				if (!ParseBlendOption(OptionName, Option, Err)) return Err;
				Montage->BlendIn.SetBlendOption(Option);
				bChanged = true;
			}
			if (Args->TryGetStringField(TEXT("blend_out_option"), OptionName) && !OptionName.IsEmpty())
			{
				EAlphaBlendOption Option;
				if (!ParseBlendOption(OptionName, Option, Err)) return Err;
				Montage->BlendOut.SetBlendOption(Option);
				bChanged = true;
			}

			bool bAutoBlendOut = false;
			if (Args->TryGetBoolField(TEXT("enable_auto_blend_out"), bAutoBlendOut))
			{
				Montage->bEnableAutoBlendOut = bAutoBlendOut;
				bChanged = true;
			}

			if (!bChanged)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("No blend settings were supplied."),
					TEXT("Pass at least one of blend_in_time, blend_out_time, blend_out_trigger_time, blend_in_option, blend_out_option, enable_auto_blend_out."));
			}

			Montage->MarkPackageDirty();

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ false);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Updated blend settings on '%s' (in %.3fs, out %.3fs, trigger %.3f)."),
					*Montage->GetName(), Montage->BlendIn.GetBlendTime(),
					Montage->BlendOut.GetBlendTime(), Montage->BlendOutTriggerTime),
				Result);
		});

	// ================================================================
	// montage_set_blend_profile
	// ================================================================
	MCP_TOOL(Registry, "montage_set_blend_profile")
		.Description(TEXT(
			"Assign per-bone blend profiles to a montage's blend in and/or blend out. A blend profile scales "
			"blend time per bone, so an upper-body attack can snap on at the hands while the spine eases in. "
			"Profiles are authored in the Skeleton editor and cannot be created from script; this tool "
			"selects an existing one by name and lists the available profiles if the name is wrong. Pass an "
			"empty string to clear."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage"), true)
		.StringArg(TEXT("blend_in_profile"), TEXT("Blend profile name for the blend in, or empty to clear"))
		.StringArg(TEXT("blend_out_profile"), TEXT("Blend profile name for the blend out, or empty to clear"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			const bool bHasIn = Args->HasField(TEXT("blend_in_profile"));
			const bool bHasOut = Args->HasField(TEXT("blend_out_profile"));
			if (!bHasIn && !bHasOut)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("Pass blend_in_profile and/or blend_out_profile."));
			}

			USkeleton* Skeleton = Montage->GetSkeleton();
			Montage->Modify();

			FString ProfileName;
			if (bHasIn)
			{
				Args->TryGetStringField(TEXT("blend_in_profile"), ProfileName);
				if (ProfileName.IsEmpty())
				{
					Montage->BlendProfileIn = nullptr;
				}
				else
				{
					UBlendProfile* Profile = ResolveBlendProfile(Skeleton, ProfileName, Err);
					if (!Profile) return Err;
					Montage->BlendProfileIn = Profile;
				}
			}
			if (bHasOut)
			{
				Args->TryGetStringField(TEXT("blend_out_profile"), ProfileName);
				if (ProfileName.IsEmpty())
				{
					Montage->BlendProfileOut = nullptr;
				}
				else
				{
					UBlendProfile* Profile = ResolveBlendProfile(Skeleton, ProfileName, Err);
					if (!Profile) return Err;
					Montage->BlendProfileOut = Profile;
				}
			}

			Montage->MarkPackageDirty();

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ false);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Updated blend profiles on '%s' (in: %s, out: %s)."),
					*Montage->GetName(),
					Montage->BlendProfileIn ? *Montage->BlendProfileIn->GetName() : TEXT("none"),
					Montage->BlendProfileOut ? *Montage->BlendProfileOut->GetName() : TEXT("none")),
				Result);
		});

	// ================================================================
	// montage_set_sync_group
	// ================================================================
	MCP_TOOL(Registry, "montage_set_sync_group")
		.Description(TEXT(
			"Put a montage into a sync group so its playback position follows the group leader — the "
			"mechanism that keeps an upper-body montage in step with the locomotion underneath it "
			"(e.g. a reload that must stay on the walk cycle's footfalls). sync_slot_index selects which of "
			"the montage's slot tracks provides the sync position. Pass an empty sync_group to clear."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage"), true)
		.StringArg(TEXT("sync_group"), TEXT("Sync group name, or empty to clear"), true)
		.IntArg(TEXT("sync_slot_index"), TEXT("Index of the slot track that drives sync (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			if (!Args->HasField(TEXT("sync_group")))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("sync_group is required (pass an empty string to clear)."));
			}
			FString SyncGroup;
			Args->TryGetStringField(TEXT("sync_group"), SyncGroup);

			int32 SyncSlotIndex = 0;
			if (Args->HasField(TEXT("sync_slot_index")))
			{
				SyncSlotIndex = (int32)Args->GetNumberField(TEXT("sync_slot_index"));
			}
			if (!Montage->SlotAnimTracks.IsValidIndex(SyncSlotIndex))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("sync_slot_index %d is out of range; montage '%s' has %d slot track(s)."),
						SyncSlotIndex, *Montage->GetName(), Montage->SlotAnimTracks.Num()));
			}

			Montage->Modify();
			Montage->SyncGroup = SyncGroup.IsEmpty() ? NAME_None : FName(*SyncGroup);
			Montage->SyncSlotIndex = SyncSlotIndex;
			Montage->MarkPackageDirty();

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ false);
			return FMCPToolResult::SuccessStructured(
				SyncGroup.IsEmpty()
					? FString::Printf(TEXT("Cleared the sync group on '%s'."), *Montage->GetName())
					: FString::Printf(TEXT("Montage '%s' now syncs with group '%s' from slot index %d."),
						*Montage->GetName(), *SyncGroup, SyncSlotIndex),
				Result);
		});

	// ================================================================
	// montage_set_rate_scale
	// ================================================================
	MCP_TOOL(Registry, "montage_set_rate_scale")
		.Description(TEXT(
			"Set a montage's global RateScale — a multiplier applied on top of the play rate passed to "
			"Montage_Play at runtime. Use it to retime an entire montage (and everything anchored to it: "
			"sections, notifies) without touching the source animations."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage"), true)
		.NumberArg(TEXT("rate_scale"), TEXT("Playback rate multiplier; 1.0 = authored speed. Must be non-zero."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			double RateScale = 1.0;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("rate_scale"), RateScale));
			if (FMath::IsNearlyZero(RateScale))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("rate_scale cannot be zero — the montage would never advance."));
			}

			Montage->Modify();
			Montage->RateScale = (float)RateScale;
			Montage->MarkPackageDirty();

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ false);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Set rate_scale %.3f on '%s' (effective duration %.3fs)."),
					RateScale, *Montage->GetName(), Montage->GetPlayLength() / FMath::Abs((float)RateScale)),
				Result);
		});

	// ================================================================
	// montage_create_from_sections
	// ================================================================
	MCP_TOOL(Registry, "montage_create_from_sections")
		.Description(TEXT(
			"Build a complete multi-section montage in one call: creates the asset, appends each animation "
			"as a segment on one slot track, names a section at each segment boundary, and chains the "
			"sections in order. This is the fast path for combos — three attack sequences become a three-"
			"section montage the gameplay code drives with Montage_JumpToSection. All animations must share "
			"one skeleton. Set loop_last=true to make the final section loop on itself (charge-and-hold)."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new montage (e.g. '/Game/Characters/AM_HeroCombo')"), true)
		.StringArrayArg(TEXT("animation_paths"), TEXT("Ordered list of UAnimSequence paths, one per section"), true)
		.StringArrayArg(TEXT("section_names"), TEXT("Optional section names, one per animation. Defaults to 'Section1', 'Section2', ..."))
		.StringArg(TEXT("slot_name"), TEXT("Slot track name (default: 'DefaultSlot')"))
		.NumberArg(TEXT("blend_in_time"), TEXT("Blend in duration in seconds (default: 0.25)"))
		.NumberArg(TEXT("blend_out_time"), TEXT("Blend out duration in seconds (default: 0.25)"))
		.BoolArg(TEXT("chain_sections"), TEXT("Chain each section to the next so the montage plays straight through (default: true). Set false so each section must be jumped to explicitly — the usual choice for input-driven combos."))
		.BoolArg(TEXT("loop_last"), TEXT("Point the final section at itself so it loops (default: false)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/AM_HeroCombo\", \"animation_paths\": [\"/Game/Anims/AS_Attack01\", \"/Game/Anims/AS_Attack02\", \"/Game/Anims/AS_Attack03\"], \"section_names\": [\"Combo1\", \"Combo2\", \"Combo3\"], \"chain_sections\": false}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString AssetPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));

			TArray<FString> AnimPaths;
			BAIL_IF_INVALID(FMCPValidate::RequiredStringArray(Args, TEXT("animation_paths"), AnimPaths));

			FString PackagePath, AssetName;
			if (!SplitAssetPath(AssetPath, PackagePath, AssetName))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::InvalidPath,
					FString::Printf(TEXT("Invalid asset path: %s"), *AssetPath));
			}
			BAIL_IF_INVALID(FMCPValidate::PackagePath(PackagePath));
			BAIL_IF_INVALID(FMCPValidate::AssetName(AssetName));
			BAIL_IF_INVALID(FMCPValidate::AssetDoesNotExist(PackagePath));

			// Resolve every animation up front — all-or-nothing, so a typo in the
			// third path does not leave a half-built asset on disk.
			FMCPToolResult Err;
			TArray<UAnimSequence*> Anims;
			for (const FString& AnimPath : AnimPaths)
			{
				UAnimSequence* Anim = MCPCommon::LoadAssetChecked<UAnimSequence>(AnimPath, Err);
				if (!Anim) return Err;
				Anims.Add(Anim);
			}

			USkeleton* Skeleton = Anims[0]->GetSkeleton();
			if (!IsValid(Skeleton))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Animation '%s' has no skeleton."), *Anims[0]->GetName()));
			}
			for (int32 i = 1; i < Anims.Num(); ++i)
			{
				if (!RequireSameSkeleton(Skeleton, Anims[i], TEXT("animation_paths entry"), Err)) return Err;
			}

			// Section names: supplied, or generated.
			TArray<FString> SectionNames;
			Args->TryGetStringArrayField(TEXT("section_names"), SectionNames);
			if (SectionNames.Num() > 0 && SectionNames.Num() != Anims.Num())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("section_names has %d entries but animation_paths has %d; they must match."),
						SectionNames.Num(), Anims.Num()));
			}
			if (SectionNames.Num() == 0)
			{
				for (int32 i = 0; i < Anims.Num(); ++i)
				{
					SectionNames.Add(FString::Printf(TEXT("Section%d"), i + 1));
				}
			}
			TSet<FString> Unique(SectionNames);
			if (Unique.Num() != SectionNames.Num())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::AlreadyExists,
					TEXT("section_names contains duplicates; section names must be unique within a montage."));
			}

			FString SlotName = TEXT("DefaultSlot");
			Args->TryGetStringField(TEXT("slot_name"), SlotName);

			double BlendIn = 0.25, BlendOut = 0.25;
			if (Args->HasField(TEXT("blend_in_time")))  BlendIn = Args->GetNumberField(TEXT("blend_in_time"));
			if (Args->HasField(TEXT("blend_out_time"))) BlendOut = Args->GetNumberField(TEXT("blend_out_time"));

			bool bChain = true;
			Args->TryGetBoolField(TEXT("chain_sections"), bChain);
			bool bLoopLast = false;
			Args->TryGetBoolField(TEXT("loop_last"), bLoopLast);

			// --- build ---
			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Failed to create the package for the new montage."));
			}

			UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
			Factory->TargetSkeleton = Skeleton;
			Factory->SourceAnimation = Anims[0];

			UAnimMontage* Montage = Cast<UAnimMontage>(Factory->FactoryCreateNew(
				UAnimMontage::StaticClass(), Package, FName(*AssetName),
				RF_Public | RF_Standalone, nullptr, GWarn));

			if (!IsValid(Montage))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("UAnimMontageFactory::FactoryCreateNew returned null. Check the Output Log."));
			}

			// Journal the creation so run_tool_script can report truthfully that this
			// asset survives a rollback (UE package creation is not transactional).
			MCPCommon::NoteAssetCreated(PackagePath);

			// The factory seeds slot 0 with Anims[0]; name that slot and register it.
			const FName SlotFName(*SlotName);
			if (Montage->SlotAnimTracks.Num() == 0)
			{
				Montage->AddSlot(SlotFName);
			}
			else
			{
				Montage->SlotAnimTracks[0].SlotName = SlotFName;
			}
			Skeleton->Modify();
			Skeleton->RegisterSlotNode(SlotFName);

			FAnimTrack& Track = Montage->SlotAnimTracks[0].AnimTrack;

			// Append the remaining animations after the seeded first one.
			for (int32 i = 1; i < Anims.Num(); ++i)
			{
				FAnimSegment Segment;
				Segment.SetAnimReference(Anims[i], /*bInitialize*/ true);
				Segment.StartPos = Track.GetLength();
				Track.AnimSegments.Add(Segment);
			}

			Montage->SetCompositeLength(Montage->CalculateSequenceLength());

			// One section per segment boundary.
			Montage->CompositeSections.Empty();
			float Cursor = 0.0f;
			for (int32 i = 0; i < Anims.Num(); ++i)
			{
				FCompositeSection Section;
				Section.SectionName = FName(*SectionNames[i]);
				// Link binds the section to the segment containing Cursor; the
				// montage length is already set above so the lookup resolves.
				Section.Link(Montage, Cursor);

				if (bChain && i + 1 < Anims.Num())
				{
					Section.NextSectionName = FName(*SectionNames[i + 1]);
				}
				else if (bLoopLast && i == Anims.Num() - 1)
				{
					Section.NextSectionName = FName(*SectionNames[i]);
				}

				Montage->CompositeSections.Add(Section);
				Cursor += Track.AnimSegments.IsValidIndex(i) ? Track.AnimSegments[i].GetLength() : 0.0f;
			}

			Montage->BlendIn.SetBlendTime((float)BlendIn);
			Montage->BlendOut.SetBlendTime((float)BlendOut);

			RefreshMontage(Montage);

			FAssetRegistryModule::AssetCreated(Montage);
			Package->MarkPackageDirty();

			const FString Filename = FPackageName::LongPackageNameToFilename(
				PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			if (!UPackage::SavePackage(Package, Montage, *Filename, SaveArgs))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Locked,
					FString::Printf(TEXT("Montage was built but the package could not be saved at '%s'."), *PackagePath));
			}

			TSharedPtr<FJsonObject> Result = MontageToJson(Montage, /*bIncludeNotifies*/ false);
			Result->SetStringField(TEXT("slot_name"), SlotName);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Created montage '%s' with %d section(s) on slot '%s', %.3fs long.%s"),
					*AssetPath, Montage->CompositeSections.Num(), *SlotName, Montage->GetPlayLength(),
					bChain ? TEXT(" Sections chain in order.") : TEXT(" Sections are independent — jump to them explicitly.")),
				Result);
		});

	// ================================================================
	// montage_validate
	// ================================================================
	MCP_TOOL(Registry, "montage_validate")
		.Description(TEXT(
			"Check a montage for the structural problems that make it fail silently at runtime: sections "
			"chaining to names that do not exist, no section at time 0, slot names not registered on the "
			"skeleton (so no AnimGraph Slot node can play them), empty slot tracks, segments whose source "
			"animation is missing or targets a different skeleton, notifies past the montage end, and "
			"unreachable sections. Read-only — reports, never repairs."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimMontage"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Path; FMCPToolResult Err;
			UAnimMontage* Montage = LoadMontage(Args, Path, Err);
			if (!Montage) return Err;

			TArray<TSharedPtr<FJsonValue>> Issues;
			auto AddIssue = [&Issues](const TCHAR* Severity, const TCHAR* Code, const FString& Message, const FString& Fix)
			{
				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("severity"), Severity);
				Obj->SetStringField(TEXT("code"), Code);
				Obj->SetStringField(TEXT("message"), Message);
				Obj->SetStringField(TEXT("fix"), Fix);
				Issues.Add(MakeShared<FJsonValueObject>(Obj));
			};

			USkeleton* Skeleton = Montage->GetSkeleton();
			if (!IsValid(Skeleton))
			{
				AddIssue(TEXT("error"), TEXT("no_skeleton"),
					TEXT("The montage has no target skeleton."),
					TEXT("Recreate the montage from a source animation with create_anim_montage."));
			}

			// --- sections ---
			if (Montage->CompositeSections.Num() == 0)
			{
				AddIssue(TEXT("error"), TEXT("no_sections"),
					TEXT("The montage has no composite sections, so it has no playback entry point."),
					TEXT("Add one with montage_add_section at time 0."));
			}
			else
			{
				bool bHasZero = false;
				for (const FCompositeSection& Section : Montage->CompositeSections)
				{
					if (FMath::IsNearlyZero(Section.GetTime())) { bHasZero = true; break; }
				}
				if (!bHasZero)
				{
					AddIssue(TEXT("error"), TEXT("no_section_at_zero"),
						TEXT("No section starts at 0.0; playback from the beginning is undefined."),
						TEXT("Move the earliest section to 0 with montage_set_section_time."));
				}
			}

			TSet<FName> SectionNames;
			for (const FCompositeSection& Section : Montage->CompositeSections)
			{
				SectionNames.Add(Section.SectionName);
			}

			TSet<FName> Reachable;
			for (const FCompositeSection& Section : Montage->CompositeSections)
			{
				if (Section.NextSectionName.IsNone()) continue;

				if (!SectionNames.Contains(Section.NextSectionName))
				{
					AddIssue(TEXT("error"), TEXT("dangling_next_section"),
						FString::Printf(TEXT("Section '%s' chains to '%s', which does not exist."),
							*Section.SectionName.ToString(), *Section.NextSectionName.ToString()),
						TEXT("Fix or clear the link with montage_link_sections."));
				}
				else
				{
					Reachable.Add(Section.NextSectionName);
				}
			}

			// A section nobody chains to is fine only if gameplay jumps to it.
			for (int32 i = 1; i < Montage->CompositeSections.Num(); ++i)
			{
				const FCompositeSection& Section = Montage->CompositeSections[i];
				if (!Reachable.Contains(Section.SectionName))
				{
					AddIssue(TEXT("info"), TEXT("section_not_chained"),
						FString::Printf(TEXT("Section '%s' is not chained to from any other section."),
							*Section.SectionName.ToString()),
						TEXT("Expected for input-driven combos (gameplay calls Montage_JumpToSection). Otherwise chain it with montage_link_sections."));
				}
			}

			// --- slots and segments ---
			if (Montage->SlotAnimTracks.Num() == 0)
			{
				AddIssue(TEXT("error"), TEXT("no_slots"),
					TEXT("The montage has no slot tracks, so it drives no animation."),
					TEXT("Add one with montage_add_slot, then montage_add_segment."));
			}

			for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
			{
				const FString SlotName = SlotTrack.SlotName.ToString();

				if (IsValid(Skeleton))
				{
					const FName Group = Skeleton->GetSlotGroupName(SlotTrack.SlotName);
					const bool bRegistered = Skeleton->FindAnimSlotGroup(Group) != nullptr &&
						Skeleton->FindAnimSlotGroup(Group)->SlotNames.Contains(SlotTrack.SlotName);
					if (!bRegistered)
					{
						AddIssue(TEXT("warning"), TEXT("slot_not_registered"),
							FString::Printf(TEXT("Slot '%s' is not registered on skeleton '%s'; no AnimGraph Slot node can select it."),
								*SlotName, *Skeleton->GetName()),
							TEXT("Re-add the slot with montage_add_slot, which registers it."));
					}
				}

				if (SlotTrack.AnimTrack.AnimSegments.Num() == 0)
				{
					AddIssue(TEXT("warning"), TEXT("empty_slot"),
						FString::Printf(TEXT("Slot '%s' has no animation segments."), *SlotName),
						TEXT("Add one with montage_add_segment, or the slot contributes nothing."));
					continue;
				}

				for (int32 SegIdx = 0; SegIdx < SlotTrack.AnimTrack.AnimSegments.Num(); ++SegIdx)
				{
					const FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments[SegIdx];
					UAnimSequenceBase* Ref = Segment.GetAnimReference();

					if (!IsValid(Ref))
					{
						AddIssue(TEXT("error"), TEXT("missing_segment_animation"),
							FString::Printf(TEXT("Slot '%s' segment %d has no animation reference."), *SlotName, SegIdx),
							TEXT("Remove it with montage_remove_segment and re-add with montage_add_segment."));
						continue;
					}
					if (IsValid(Skeleton) && Ref->GetSkeleton() != Skeleton)
					{
						AddIssue(TEXT("error"), TEXT("segment_skeleton_mismatch"),
							FString::Printf(TEXT("Slot '%s' segment %d ('%s') targets a different skeleton."),
								*SlotName, SegIdx, *Ref->GetName()),
							TEXT("Retarget the animation, or replace the segment."));
					}
				}
			}

			// --- notifies ---
			const float Length = Montage->GetPlayLength();
			for (const FAnimNotifyEvent& Event : Montage->Notifies)
			{
				const float Trigger = Event.GetTriggerTime();
				if (Trigger > Length + KINDA_SMALL_NUMBER)
				{
					AddIssue(TEXT("warning"), TEXT("notify_past_end"),
						FString::Printf(TEXT("Notify '%s' triggers at %.3fs, past the montage end (%.3fs); it will never fire."),
							*Event.NotifyName.ToString(), Trigger, Length),
						TEXT("Move it with anim_move_notify or remove it with anim_remove_notify."));
				}
				if (Event.NotifyStateClass && Trigger + Event.GetDuration() > Length + KINDA_SMALL_NUMBER)
				{
					AddIssue(TEXT("warning"), TEXT("notify_state_overruns_end"),
						FString::Printf(TEXT("Notify state '%s' ends at %.3fs, past the montage end (%.3fs); its End event may not fire."),
							*Event.NotifyName.ToString(), Trigger + Event.GetDuration(), Length),
						TEXT("Shorten its duration with anim_move_notify."));
				}
			}

			// --- blend sanity ---
			if (Montage->BlendOut.GetBlendTime() > Length)
			{
				AddIssue(TEXT("warning"), TEXT("blend_out_longer_than_montage"),
					FString::Printf(TEXT("Blend out (%.3fs) is longer than the montage (%.3fs)."),
						Montage->BlendOut.GetBlendTime(), Length),
					TEXT("Shorten it with montage_set_blend_settings."));
			}

			int32 ErrorCount = 0, WarningCount = 0;
			for (const TSharedPtr<FJsonValue>& Value : Issues)
			{
				const FString Severity = Value->AsObject()->GetStringField(TEXT("severity"));
				if (Severity == TEXT("error")) ++ErrorCount;
				else if (Severity == TEXT("warning")) ++WarningCount;
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("montage"), Montage->GetName());
			Result->SetStringField(TEXT("path"), Montage->GetPathName());
			Result->SetBoolField(TEXT("valid"), ErrorCount == 0);
			Result->SetNumberField(TEXT("error_count"), ErrorCount);
			Result->SetNumberField(TEXT("warning_count"), WarningCount);
			Result->SetArrayField(TEXT("issues"), Issues);

			return FMCPToolResult::SuccessStructured(
				ErrorCount == 0 && WarningCount == 0
					? FString::Printf(TEXT("Montage '%s' is structurally sound."), *Montage->GetName())
					: FString::Printf(TEXT("Montage '%s': %d error(s), %d warning(s)."),
						*Montage->GetName(), ErrorCount, WarningCount),
				Result);
		});
}

} // namespace MCPAnimTools::Montage
