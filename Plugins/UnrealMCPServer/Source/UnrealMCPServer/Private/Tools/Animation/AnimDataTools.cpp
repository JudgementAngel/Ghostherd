// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.6 Phase 2 — notifies, notify states, notify tracks, curves, sync markers.
//
// Everything here routes through UAnimationBlueprintLibrary (the engine's own
// editor-side animation editing surface) rather than poking UAnimSequenceBase
// fields directly. That matters for three reasons the v4.5 add_anim_notify got
// wrong: notifies must be Link()ed to bind them to the sequence's frame rate,
// RefreshCacheData() must run or the compiled notify tracks disagree with
// Notifies[], and a notify's TrackIndex must resolve from a track that actually
// exists.
//
// One wrinkle the library forces us to handle: its Add* functions are silent on
// bad input — an unknown notify track name logs a warning and adds nothing, and
// the caller gets no signal. So every tool here pre-validates the track and the
// time, and turns a failure into a structured error naming the valid options.

#include "Tools/Animation/AnimCommon.h"
#include "Common/MCPAssetResolver.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "AnimationBlueprintLibrary.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/Skeleton.h"

namespace MCPAnimTools::AnimData
{

using namespace MCPAnimTools::Common;

// ---------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------

/** Load an AnimSequence or AnimMontage (both are UAnimSequenceBase). */
static UAnimSequenceBase* LoadAnim(const TSharedPtr<FJsonObject>& Args, FMCPToolResult& OutError)
{
	FString Path;
	FMCPValidateResult Check = FMCPValidate::RequiredString(Args, TEXT("asset_path"), Path);
	if (!Check.bOk)
	{
		OutError = Check.Error;
		return nullptr;
	}
	return MCPCommon::LoadAssetChecked<UAnimSequenceBase>(Path, OutError);
}

/** Sync markers live on UAnimSequence only — a montage's markers are collected
 *  from the sequences in its slot tracks and cannot be authored on the montage. */
static UAnimSequence* LoadSequenceForMarkers(const TSharedPtr<FJsonObject>& Args, FMCPToolResult& OutError)
{
	UAnimSequenceBase* Base = LoadAnim(Args, OutError);
	if (!Base)
	{
		return nullptr;
	}

	UAnimSequence* Sequence = Cast<UAnimSequence>(Base);
	if (!Sequence)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
			FString::Printf(TEXT("'%s' is a %s; sync markers can only be authored on an AnimSequence."),
				*Base->GetName(), *Base->GetClass()->GetName()),
			TEXT("A montage inherits markers from the sequences in its slot tracks — add the marker to the source sequence instead."));
		return nullptr;
	}
	return Sequence;
}

/** Serialize one notify event. */
static TSharedPtr<FJsonObject> NotifyToJson(const FAnimNotifyEvent& Event, const UAnimSequenceBase* Anim)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("name"), Event.NotifyName.ToString());
	Obj->SetNumberField(TEXT("trigger_time"), Event.GetTriggerTime());
	Obj->SetNumberField(TEXT("duration"), Event.GetDuration());
	Obj->SetBoolField(TEXT("is_state"), Event.NotifyStateClass != nullptr);
	Obj->SetBoolField(TEXT("is_branching_point"), Event.IsBranchingPoint());
	Obj->SetNumberField(TEXT("track_index"), Event.TrackIndex);

	if (Anim && Anim->AnimNotifyTracks.IsValidIndex(Event.TrackIndex))
	{
		Obj->SetStringField(TEXT("track_name"), Anim->AnimNotifyTracks[Event.TrackIndex].TrackName.ToString());
	}

	if (Event.NotifyStateClass)
	{
		Obj->SetStringField(TEXT("class"), Event.NotifyStateClass->GetClass()->GetName());
		Obj->SetStringField(TEXT("class_path"), Event.NotifyStateClass->GetClass()->GetPathName());
	}
	else if (Event.Notify)
	{
		Obj->SetStringField(TEXT("class"), Event.Notify->GetClass()->GetName());
		Obj->SetStringField(TEXT("class_path"), Event.Notify->GetClass()->GetPathName());
	}
	else
	{
		// A notify with no class is a plain named event: the AnimBP receives it
		// as an "AnimNotify_<Name>" function call.
		Obj->SetStringField(TEXT("class"), TEXT("(simple notify)"));
		Obj->SetStringField(TEXT("anim_bp_event"), FString::Printf(TEXT("AnimNotify_%s"), *Event.NotifyName.ToString()));
	}

	return Obj;
}

/** Full notify listing for a result payload. */
static TSharedPtr<FJsonObject> NotifiesToJson(UAnimSequenceBase* Anim)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset"), Anim->GetName());
	Result->SetStringField(TEXT("path"), Anim->GetPathName());
	Result->SetNumberField(TEXT("duration"), Anim->GetPlayLength());

	TArray<TSharedPtr<FJsonValue>> Notifies;
	for (const FAnimNotifyEvent& Event : Anim->Notifies)
	{
		Notifies.Add(MakeShared<FJsonValueObject>(NotifyToJson(Event, Anim)));
	}
	Result->SetArrayField(TEXT("notifies"), Notifies);
	Result->SetNumberField(TEXT("notify_count"), Notifies.Num());

	TArray<FName> TrackNames;
	UAnimationBlueprintLibrary::GetAnimationNotifyTrackNames(Anim, TrackNames);
	TArray<TSharedPtr<FJsonValue>> Tracks;
	for (const FName& Name : TrackNames)
	{
		Tracks.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	Result->SetArrayField(TEXT("notify_tracks"), Tracks);

	return Result;
}

/** Parse ERawCurveTrackTypes from the tool's enum arg. */
static ERawCurveTrackTypes ParseCurveType(const FString& Name)
{
	if (Name.Equals(TEXT("Transform"), ESearchCase::IgnoreCase)) return ERawCurveTrackTypes::RCT_Transform;
	if (Name.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))    return ERawCurveTrackTypes::RCT_Vector;
	return ERawCurveTrackTypes::RCT_Float;
}

// ---------------------------------------------------------------------

void RegisterAll(FMCPToolRegistry& Registry)
{
	// =================================================================
	// Notify tracks
	// =================================================================

	MCP_TOOL(Registry, "anim_list_notify_tracks")
		.Description(TEXT(
			"List an animation's notify tracks and the notifies on each. Notify tracks are the horizontal "
			"lanes in the animation editor's notify panel; every notify belongs to exactly one. Tracks are "
			"addressed by NAME everywhere in the v4.6 notify tools — the default track an animation ships "
			"with is named '1', not '0'. Call this before adding notifies so you pass a track that exists."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence or AnimMontage"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequenceBase* Anim = LoadAnim(Args, Err);
			if (!Anim) return Err;

			TArray<FName> TrackNames;
			UAnimationBlueprintLibrary::GetAnimationNotifyTrackNames(Anim, TrackNames);

			TArray<TSharedPtr<FJsonValue>> Tracks;
			for (const FName& TrackName : TrackNames)
			{
				TArray<FAnimNotifyEvent> Events;
				UAnimationBlueprintLibrary::GetAnimationNotifyEventsForTrack(Anim, TrackName, Events);

				TArray<TSharedPtr<FJsonValue>> EventJson;
				for (const FAnimNotifyEvent& Event : Events)
				{
					EventJson.Add(MakeShared<FJsonValueObject>(NotifyToJson(Event, Anim)));
				}

				TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
				TrackObj->SetStringField(TEXT("name"), TrackName.ToString());
				TrackObj->SetNumberField(TEXT("notify_count"), EventJson.Num());
				TrackObj->SetArrayField(TEXT("notifies"), EventJson);
				Tracks.Add(MakeShared<FJsonValueObject>(TrackObj));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset"), Anim->GetName());
			Result->SetStringField(TEXT("path"), Anim->GetPathName());
			Result->SetNumberField(TEXT("duration"), Anim->GetPlayLength());
			Result->SetNumberField(TEXT("track_count"), Tracks.Num());
			Result->SetArrayField(TEXT("tracks"), Tracks);
			Result->SetNumberField(TEXT("total_notify_count"), Anim->Notifies.Num());

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	MCP_TOOL(Registry, "anim_add_notify_track")
		.Description(TEXT(
			"Add a named notify track to an animation. Separate tracks keep unrelated notify families apart "
			"(footsteps on one, combat windows on another) which matters because notify states on the same "
			"track cannot overlap. Idempotent: returns success if the track already exists."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence or AnimMontage"), true)
		.StringArg(TEXT("track_name"), TEXT("Name for the new notify track (e.g. 'Combat', 'Footsteps')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequenceBase* Anim = LoadAnim(Args, Err);
			if (!Anim) return Err;

			FString TrackName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("track_name"), TrackName));

			const FName TrackFName(*TrackName);
			const bool bExisted = UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(Anim, TrackFName);

			if (!bExisted)
			{
				BeginAnimEdit(Anim);
				UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Anim, TrackFName, FLinearColor::White);
				EndAnimEdit(Anim);

				if (!UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(Anim, TrackFName))
				{
					return FMCPToolResult::ErrorStructured(EMCPError::Internal,
						FString::Printf(TEXT("Failed to add notify track '%s' to '%s'."), *TrackName, *Anim->GetName()));
				}
			}

			TSharedPtr<FJsonObject> Result = NotifiesToJson(Anim);
			Result->SetBoolField(TEXT("created"), !bExisted);

			return FMCPToolResult::SuccessStructured(
				bExisted
					? FString::Printf(TEXT("Notify track '%s' already exists on '%s'."), *TrackName, *Anim->GetName())
					: FString::Printf(TEXT("Added notify track '%s' to '%s'."), *TrackName, *Anim->GetName()),
				Result);
		});

	MCP_TOOL(Registry, "anim_remove_notify_track")
		.Description(TEXT(
			"Remove a notify track from an animation. Every notify on that track is removed with it — the "
			"result reports how many. Refuses to remove the animation's last remaining track."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence or AnimMontage"), true)
		.StringArg(TEXT("track_name"), TEXT("Name of the notify track to remove"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequenceBase* Anim = LoadAnim(Args, Err);
			if (!Anim) return Err;

			FString TrackName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("track_name"), TrackName));

			FName Resolved;
			if (!ResolveNotifyTrack(Anim, TrackName, /*bCreateIfMissing*/ false, Resolved, Err)) return Err;

			TArray<FName> AllTracks;
			UAnimationBlueprintLibrary::GetAnimationNotifyTrackNames(Anim, AllTracks);
			if (AllTracks.Num() <= 1)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("Cannot remove the animation's only notify track."),
					TEXT("Add a replacement track first with anim_add_notify_track."));
			}

			TArray<FAnimNotifyEvent> Doomed;
			UAnimationBlueprintLibrary::GetAnimationNotifyEventsForTrack(Anim, Resolved, Doomed);
			const int32 RemovedNotifies = Doomed.Num();

			BeginAnimEdit(Anim);
			UAnimationBlueprintLibrary::RemoveAnimationNotifyTrack(Anim, Resolved);
			EndAnimEdit(Anim);

			TSharedPtr<FJsonObject> Result = NotifiesToJson(Anim);
			Result->SetNumberField(TEXT("removed_notify_count"), RemovedNotifies);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Removed notify track '%s' from '%s' along with %d notify/notifies."),
					*Resolved.ToString(), *Anim->GetName(), RemovedNotifies),
				Result);
		});

	// =================================================================
	// Notifies
	// =================================================================

	MCP_TOOL(Registry, "anim_add_notify")
		.Description(TEXT(
			"Add a notify to an AnimSequence or AnimMontage at a given time. Two forms:\n"
			"  - Simple named notify (pass notify_name only): fires as an 'AnimNotify_<name>' event the "
			"Animation Blueprint can implement — the usual choice for footsteps and hit frames.\n"
			"  - Class notify (pass notify_class): instantiates a UAnimNotify subclass such as "
			"AnimNotify_PlaySound or AnimNotify_PlayParticleEffect, which carries its own configurable "
			"properties (set them afterwards with set_object_property on the returned notify path).\n"
			"For anything with a DURATION — combo windows, invulnerability frames, hit boxes — use "
			"anim_add_notify_state instead."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence or AnimMontage"), true)
		.NumberArg(TEXT("time_seconds"), TEXT("Trigger time in seconds, within [0, animation duration]"), true)
		.StringArg(TEXT("notify_name"), TEXT("Name for a simple notify (e.g. 'FootStep_L'). The AnimBP implements 'AnimNotify_FootStep_L'. Ignored when notify_class is given, since the class supplies the name."))
		.StringArg(TEXT("notify_class"), TEXT("Optional UAnimNotify subclass, e.g. 'AnimNotify_PlaySound'. Blueprint notify classes need a full path ending in '_C'."))
		.StringArg(TEXT("track_name"), TEXT("Notify track to place it on (default: the animation's first track, usually named '1')"))
		.BoolArg(TEXT("create_track"), TEXT("Create the notify track if it does not exist (default: false)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Anims/AS_Run\", \"time_seconds\": 0.25, \"notify_name\": \"FootStep_L\"}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequenceBase* Anim = LoadAnim(Args, Err);
			if (!Anim) return Err;

			double TimeSeconds = 0.0;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("time_seconds"), TimeSeconds));
			if (!RequireTimeInRange(Anim, TimeSeconds, TEXT("time_seconds"), Err)) return Err;

			FString NotifyName, NotifyClassName;
			Args->TryGetStringField(TEXT("notify_name"), NotifyName);
			Args->TryGetStringField(TEXT("notify_class"), NotifyClassName);

			if (NotifyName.IsEmpty() && NotifyClassName.IsEmpty())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("Pass notify_name (for a simple named notify) or notify_class (for a UAnimNotify subclass)."));
			}

			UClass* NotifyClass = nullptr;
			if (!NotifyClassName.IsEmpty())
			{
				NotifyClass = ResolveClass(NotifyClassName, UAnimNotify::StaticClass(), Err);
				if (!NotifyClass) return Err;
			}

			FString TrackName;
			Args->TryGetStringField(TEXT("track_name"), TrackName);
			bool bCreateTrack = false;
			Args->TryGetBoolField(TEXT("create_track"), bCreateTrack);

			BeginAnimEdit(Anim);

			FName ResolvedTrack;
			if (!ResolveNotifyTrack(Anim, TrackName, bCreateTrack, ResolvedTrack, Err)) return Err;

			const int32 BeforeCount = Anim->Notifies.Num();
			UAnimNotify* Created = UAnimationBlueprintLibrary::AddAnimationNotifyEvent(
				Anim, ResolvedTrack, (float)TimeSeconds, NotifyClass);

			if (Anim->Notifies.Num() != BeforeCount + 1)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("The engine did not add a notify to '%s'."), *Anim->GetName()),
					TEXT("AddAnimationNotifyEvent rejects out-of-range times and unknown track names; both were pre-checked, so see the Output Log for the engine's warning."));
			}

			// A class-less notify is created with NotifyName = NAME_None; the
			// name is what the AnimBP binds its event to, so set it here.
			FAnimNotifyEvent& NewEvent = Anim->Notifies.Last();
			if (!NotifyClass && !NotifyName.IsEmpty())
			{
				NewEvent.NotifyName = FName(*NotifyName);
			}

			Anim->SortNotifies();
			EndAnimEdit(Anim);

			TSharedPtr<FJsonObject> Result = NotifiesToJson(Anim);
			Result->SetStringField(TEXT("added_notify"), NewEvent.NotifyName.ToString());
			Result->SetStringField(TEXT("track_name"), ResolvedTrack.ToString());
			if (Created)
			{
				Result->SetStringField(TEXT("notify_object_path"), Created->GetPathName());
			}

			return FMCPToolResult::SuccessStructured(
				NotifyClass
					? FString::Printf(TEXT("Added %s notify at %.3fs on track '%s' of '%s'. Configure it with set_object_property on '%s'."),
						*NotifyClass->GetName(), TimeSeconds, *ResolvedTrack.ToString(), *Anim->GetName(),
						Created ? *Created->GetPathName() : TEXT("(the new notify)"))
					: FString::Printf(TEXT("Added simple notify '%s' at %.3fs on track '%s' of '%s'. Implement 'AnimNotify_%s' in the Animation Blueprint to react to it."),
						*NotifyName, TimeSeconds, *ResolvedTrack.ToString(), *Anim->GetName(), *NotifyName),
				Result);
		});

	MCP_TOOL(Registry, "anim_add_notify_state")
		.Description(TEXT(
			"Add a notify STATE — a notify with a duration — to an AnimSequence or AnimMontage. Where a "
			"plain notify is an instant event, a notify state has Begin / Tick / End, which is what combo "
			"input windows, invulnerability frames, weapon hit-traces and root-motion warping windows are "
			"built from. Requires a UAnimNotifyState subclass; Blueprint ones need a full path ending in "
			"'_C'. Notify states on the same track cannot overlap — put concurrent states on separate tracks."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence or AnimMontage"), true)
		.StringArg(TEXT("notify_state_class"), TEXT("UAnimNotifyState subclass, e.g. 'AnimNotifyState_TimedParticleEffect' or '/Game/Anim/ANS_ComboWindow.ANS_ComboWindow_C'"), true)
		.NumberArg(TEXT("start_time"), TEXT("Start time in seconds"), true)
		.NumberArg(TEXT("duration"), TEXT("Duration in seconds. start_time + duration must not exceed the animation length."), true)
		.StringArg(TEXT("track_name"), TEXT("Notify track to place it on (default: the animation's first track)"))
		.BoolArg(TEXT("create_track"), TEXT("Create the notify track if it does not exist (default: false)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/AM_HeroCombo\", \"notify_state_class\": \"/Game/Anim/ANS_ComboWindow.ANS_ComboWindow_C\", \"start_time\": 0.45, \"duration\": 0.3, \"track_name\": \"Combat\", \"create_track\": true}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequenceBase* Anim = LoadAnim(Args, Err);
			if (!Anim) return Err;

			FString ClassName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("notify_state_class"), ClassName));

			double StartTime = 0.0, Duration = 0.0;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("start_time"), StartTime));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("duration"), Duration));

			if (!RequireTimeInRange(Anim, StartTime, TEXT("start_time"), Err)) return Err;
			if (Duration <= 0.0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("duration must be greater than 0. For an instant event use anim_add_notify instead."));
			}
			if (StartTime + Duration > Anim->GetPlayLength() + KINDA_SMALL_NUMBER)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("start_time + duration (%.4f) exceeds the animation length (%.4f); the state's End event would never fire."),
						StartTime + Duration, Anim->GetPlayLength()));
			}

			UClass* StateClass = ResolveClass(ClassName, UAnimNotifyState::StaticClass(), Err);
			if (!StateClass) return Err;

			FString TrackName;
			Args->TryGetStringField(TEXT("track_name"), TrackName);
			bool bCreateTrack = false;
			Args->TryGetBoolField(TEXT("create_track"), bCreateTrack);

			BeginAnimEdit(Anim);

			FName ResolvedTrack;
			if (!ResolveNotifyTrack(Anim, TrackName, bCreateTrack, ResolvedTrack, Err)) return Err;

			// Overlap check: the editor forbids overlapping states on one track,
			// and an overlap silently corrupts Begin/End pairing at runtime.
			TArray<FAnimNotifyEvent> OnTrack;
			UAnimationBlueprintLibrary::GetAnimationNotifyEventsForTrack(Anim, ResolvedTrack, OnTrack);
			for (const FAnimNotifyEvent& Existing : OnTrack)
			{
				if (!Existing.NotifyStateClass) continue;

				const double ExistingStart = Existing.GetTriggerTime();
				const double ExistingEnd = ExistingStart + Existing.GetDuration();
				if (StartTime < ExistingEnd - KINDA_SMALL_NUMBER &&
					ExistingStart < StartTime + Duration - KINDA_SMALL_NUMBER)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::AlreadyExists,
						FString::Printf(TEXT("Notify state '%s' already occupies [%.3f, %.3f] on track '%s'; the new state [%.3f, %.3f] would overlap it."),
							*Existing.NotifyName.ToString(), ExistingStart, ExistingEnd,
							*ResolvedTrack.ToString(), StartTime, StartTime + Duration),
						TEXT("Notify states cannot overlap on one track. Put this state on a different track (anim_add_notify_track), or move the existing one with anim_move_notify."));
				}
			}

			const int32 BeforeCount = Anim->Notifies.Num();
			UAnimNotifyState* Created = UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(
				Anim, ResolvedTrack, (float)StartTime, (float)Duration, StateClass);

			if (Anim->Notifies.Num() != BeforeCount + 1)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("The engine did not add a notify state to '%s'. See the Output Log."), *Anim->GetName()));
			}

			const FAnimNotifyEvent& NewEvent = Anim->Notifies.Last();
			const FString AddedName = NewEvent.NotifyName.ToString();

			Anim->SortNotifies();
			EndAnimEdit(Anim);

			TSharedPtr<FJsonObject> Result = NotifiesToJson(Anim);
			Result->SetStringField(TEXT("added_notify_state"), AddedName);
			Result->SetStringField(TEXT("track_name"), ResolvedTrack.ToString());
			if (Created)
			{
				Result->SetStringField(TEXT("notify_object_path"), Created->GetPathName());
			}

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Added %s over [%.3f, %.3f] on track '%s' of '%s'."),
					*StateClass->GetName(), StartTime, StartTime + Duration,
					*ResolvedTrack.ToString(), *Anim->GetName()),
				Result);
		});

	MCP_TOOL(Registry, "anim_remove_notify")
		.Description(TEXT(
			"Remove notifies from an animation, by name or by whole track. Removing by name removes EVERY "
			"notify with that name (a footstep notify placed four times goes away four times) — the result "
			"reports the count. Use anim_list_notify_tracks first to see what is there."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence or AnimMontage"), true)
		.StringArg(TEXT("notify_name"), TEXT("Remove every notify with this name. Mutually exclusive with track_name."))
		.StringArg(TEXT("track_name"), TEXT("Remove every notify on this track (the track itself is kept). Mutually exclusive with notify_name."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequenceBase* Anim = LoadAnim(Args, Err);
			if (!Anim) return Err;

			FString NotifyName, TrackName;
			Args->TryGetStringField(TEXT("notify_name"), NotifyName);
			Args->TryGetStringField(TEXT("track_name"), TrackName);

			if (NotifyName.IsEmpty() == TrackName.IsEmpty())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("Pass exactly one of notify_name or track_name."));
			}

			BeginAnimEdit(Anim);

			int32 Removed = 0;
			FString What;
			if (!NotifyName.IsEmpty())
			{
				Removed = UAnimationBlueprintLibrary::RemoveAnimationNotifyEventsByName(Anim, FName(*NotifyName));
				What = FString::Printf(TEXT("named '%s'"), *NotifyName);

				if (Removed == 0)
				{
					TArray<FName> Existing;
					UAnimationBlueprintLibrary::GetAnimationNotifyEventNames(Anim, Existing);
					TArray<FString> Names;
					for (const FName& Name : Existing) { Names.Add(Name.ToString()); }

					return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
						FString::Printf(TEXT("'%s' has no notify named '%s'."), *Anim->GetName(), *NotifyName),
						TEXT("Use anim_list_notify_tracks to see the notifies on this animation."),
						Names);
				}
			}
			else
			{
				FName ResolvedTrack;
				if (!ResolveNotifyTrack(Anim, TrackName, /*bCreateIfMissing*/ false, ResolvedTrack, Err)) return Err;

				Removed = UAnimationBlueprintLibrary::RemoveAnimationNotifyEventsByTrack(Anim, ResolvedTrack);
				What = FString::Printf(TEXT("on track '%s'"), *ResolvedTrack.ToString());
			}

			EndAnimEdit(Anim);

			TSharedPtr<FJsonObject> Result = NotifiesToJson(Anim);
			Result->SetNumberField(TEXT("removed_count"), Removed);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Removed %d notify/notifies %s from '%s'."), Removed, *What, *Anim->GetName()),
				Result);
		});

	MCP_TOOL(Registry, "anim_move_notify")
		.Description(TEXT(
			"Move a notify to a new time, and optionally change a notify state's duration or move it to a "
			"different track. Identifies the notify by name; when several share a name, occurrence_index "
			"selects which one in time order (0 = earliest). This is the retiming tool — use it when a hit "
			"frame lands early rather than deleting and re-adding the notify, which would lose the notify "
			"object's configured properties."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence or AnimMontage"), true)
		.StringArg(TEXT("notify_name"), TEXT("Name of the notify to move"), true)
		.NumberArg(TEXT("time_seconds"), TEXT("New trigger time in seconds"), true)
		.NumberArg(TEXT("duration"), TEXT("New duration in seconds (notify states only)"))
		.StringArg(TEXT("track_name"), TEXT("Move the notify to this track"))
		.IntArg(TEXT("occurrence_index"), TEXT("Which occurrence to move when several notifies share the name, in time order (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequenceBase* Anim = LoadAnim(Args, Err);
			if (!Anim) return Err;

			FString NotifyName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("notify_name"), NotifyName));

			double TimeSeconds = 0.0;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("time_seconds"), TimeSeconds));
			if (!RequireTimeInRange(Anim, TimeSeconds, TEXT("time_seconds"), Err)) return Err;

			int32 Occurrence = 0;
			if (Args->HasField(TEXT("occurrence_index")))
			{
				Occurrence = (int32)Args->GetNumberField(TEXT("occurrence_index"));
			}

			// Notifies are kept sorted by trigger time, so array order is time order.
			const FName Wanted(*NotifyName);
			int32 Found = INDEX_NONE;
			int32 Seen = 0;
			for (int32 i = 0; i < Anim->Notifies.Num(); ++i)
			{
				if (Anim->Notifies[i].NotifyName == Wanted)
				{
					if (Seen == Occurrence) { Found = i; break; }
					++Seen;
				}
			}

			if (Found == INDEX_NONE)
			{
				TArray<FName> Existing;
				UAnimationBlueprintLibrary::GetAnimationNotifyEventNames(Anim, Existing);
				TArray<FString> Names;
				for (const FName& Name : Existing) { Names.Add(Name.ToString()); }

				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					Seen > 0
						? FString::Printf(TEXT("'%s' has only %d notify/notifies named '%s'; occurrence_index %d is out of range."),
							*Anim->GetName(), Seen, *NotifyName, Occurrence)
						: FString::Printf(TEXT("'%s' has no notify named '%s'."), *Anim->GetName(), *NotifyName),
					TEXT("Use anim_list_notify_tracks to inspect the notifies on this animation."),
					Names);
			}

			FAnimNotifyEvent& Event = Anim->Notifies[Found];
			const bool bIsState = Event.NotifyStateClass != nullptr;

			double NewDuration = Event.GetDuration();
			if (Args->HasField(TEXT("duration")))
			{
				if (!bIsState)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
						FString::Printf(TEXT("Notify '%s' is an instant notify and has no duration."), *NotifyName),
						TEXT("Only notify states (added with anim_add_notify_state) have a duration."));
				}
				NewDuration = Args->GetNumberField(TEXT("duration"));
				if (NewDuration <= 0.0)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange, TEXT("duration must be greater than 0."));
				}
			}

			if (bIsState && TimeSeconds + NewDuration > Anim->GetPlayLength() + KINDA_SMALL_NUMBER)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("The state would end at %.4f, past the animation length (%.4f)."),
						TimeSeconds + NewDuration, Anim->GetPlayLength()));
			}

			BeginAnimEdit(Anim);

			FString TrackName;
			if (Args->TryGetStringField(TEXT("track_name"), TrackName) && !TrackName.IsEmpty())
			{
				FName ResolvedTrack;
				if (!ResolveNotifyTrack(Anim, TrackName, /*bCreateIfMissing*/ false, ResolvedTrack, Err)) return Err;
				Event.TrackIndex = UAnimationBlueprintLibrary::GetTrackIndexForAnimationNotifyTrackName(Anim, ResolvedTrack);
			}

			// Link() re-binds the notify to the montage segment / sequence frame
			// that now contains the new time; SetTime alone would leave the
			// segment binding stale.
			Event.Link(Anim, (float)TimeSeconds, Event.GetSlotIndex());
			Event.TriggerTimeOffset = GetTriggerTimeOffsetForType(Anim->CalculateOffsetForNotify((float)TimeSeconds));

			if (bIsState)
			{
				// SetDuration moves EndLink's TIME, but does not re-bind it to the
				// segment that now contains that time — which matters on a montage,
				// where a linkable element stores a segment index plus an offset.
				// Link() with the time SetDuration just wrote does the re-bind.
				Event.SetDuration((float)NewDuration);
				Event.EndLink.Link(Anim, Event.EndLink.GetTime(), Event.EndLink.GetSlotIndex());
			}

			Anim->SortNotifies();
			EndAnimEdit(Anim);

			TSharedPtr<FJsonObject> Result = NotifiesToJson(Anim);
			Result->SetStringField(TEXT("moved_notify"), NotifyName);

			return FMCPToolResult::SuccessStructured(
				bIsState
					? FString::Printf(TEXT("Moved notify state '%s' to [%.3f, %.3f] on '%s'."),
						*NotifyName, TimeSeconds, TimeSeconds + NewDuration, *Anim->GetName())
					: FString::Printf(TEXT("Moved notify '%s' to %.3fs on '%s'."),
						*NotifyName, TimeSeconds, *Anim->GetName()),
				Result);
		});

	MCP_TOOL(Registry, "anim_copy_notifies")
		.Description(TEXT(
			"Copy every notify from one animation to another, creating notify tracks on the destination as "
			"needed. The usual case is propagating a footstep or hit-frame pass from one variant of a move "
			"onto its retargeted or mirrored siblings. Notify TIMES are copied verbatim, so if the two "
			"animations differ in length some notifies may land past the end of the destination — the "
			"result reports how many, and montage_validate / anim_list_notify_tracks will show them."))
		.SupportsDryRun()
		.StringArg(TEXT("source_path"), TEXT("Animation to copy notifies from"), true)
		.StringArg(TEXT("destination_path"), TEXT("Animation to copy notifies to"), true)
		.BoolArg(TEXT("replace_existing"), TEXT("Delete the destination's existing notifies first (default: false, which appends)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString SourcePath, DestPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("source_path"), SourcePath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("destination_path"), DestPath));

			FMCPToolResult Err;
			UAnimSequenceBase* Source = MCPCommon::LoadAssetChecked<UAnimSequenceBase>(SourcePath, Err);
			if (!Source) return Err;
			UAnimSequenceBase* Dest = MCPCommon::LoadAssetChecked<UAnimSequenceBase>(DestPath, Err);
			if (!Dest) return Err;

			if (Source == Dest)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("source_path and destination_path are the same animation."));
			}

			bool bReplace = false;
			Args->TryGetBoolField(TEXT("replace_existing"), bReplace);

			const int32 SourceCount = Source->Notifies.Num();
			if (SourceCount == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Source animation '%s' has no notifies to copy."), *Source->GetName()));
			}

			BeginAnimEdit(Dest);
			UAnimationBlueprintLibrary::CopyAnimNotifiesFromSequence(Source, Dest, bReplace);
			EndAnimEdit(Dest);

			// Report notifies stranded past the destination's end.
			const float DestLength = Dest->GetPlayLength();
			int32 PastEnd = 0;
			for (const FAnimNotifyEvent& Event : Dest->Notifies)
			{
				if (Event.GetTriggerTime() > DestLength + KINDA_SMALL_NUMBER) { ++PastEnd; }
			}

			TSharedPtr<FJsonObject> Result = NotifiesToJson(Dest);
			Result->SetStringField(TEXT("source"), Source->GetPathName());
			Result->SetNumberField(TEXT("source_notify_count"), SourceCount);
			Result->SetNumberField(TEXT("notifies_past_end"), PastEnd);
			Result->SetNumberField(TEXT("source_duration"), Source->GetPlayLength());
			Result->SetNumberField(TEXT("destination_duration"), DestLength);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Copied notifies from '%s' (%.3fs) to '%s' (%.3fs); destination now has %d notify/notifies.%s"),
					*Source->GetName(), Source->GetPlayLength(), *Dest->GetName(), DestLength, Dest->Notifies.Num(),
					PastEnd > 0
						? *FString::Printf(TEXT(" WARNING: %d land past the destination's end and will never fire — retime them with anim_move_notify."), PastEnd)
						: TEXT("")),
				Result);
		});

	// =================================================================
	// Curves
	// =================================================================

	MCP_TOOL(Registry, "anim_add_curve")
		.Description(TEXT(
			"Add a named animation curve to an AnimSequence or AnimMontage. Float curves are the general "
			"mechanism for driving values from animation — an AnimBP reads them with GetCurveValue, and a "
			"curve whose metadata is flagged as a morph target or material parameter drives that directly. "
			"Adding the curve creates it empty; add keys with anim_add_float_curve_keys. Set "
			"is_metadata_curve=true for a curve that is a constant marker rather than an animated value."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence or AnimMontage"), true)
		.StringArg(TEXT("curve_name"), TEXT("Name of the curve (e.g. 'Jaw_Open', 'DisableFootIK')"), true)
		.EnumArg(TEXT("curve_type"), TEXT("Curve data type (default: 'Float')"), { TEXT("Float"), TEXT("Vector"), TEXT("Transform") })
		.BoolArg(TEXT("is_metadata_curve"), TEXT("Create as a metadata curve — a constant flag rather than an animated value (default: false)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequenceBase* Anim = LoadAnim(Args, Err);
			if (!Anim) return Err;

			FString CurveName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("curve_name"), CurveName));

			FString CurveTypeName = TEXT("Float");
			Args->TryGetStringField(TEXT("curve_type"), CurveTypeName);
			const ERawCurveTrackTypes CurveType = ParseCurveType(CurveTypeName);

			bool bMetaData = false;
			Args->TryGetBoolField(TEXT("is_metadata_curve"), bMetaData);

			const FName CurveFName(*CurveName);
			if (UAnimationBlueprintLibrary::DoesCurveExist(Anim, CurveFName, CurveType))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::AlreadyExists,
					FString::Printf(TEXT("'%s' already has a %s curve named '%s'."), *Anim->GetName(), *CurveTypeName, *CurveName),
					TEXT("Add keys to it with anim_add_float_curve_keys, or remove it first with anim_remove_curve."));
			}

			BeginAnimEdit(Anim);
			UAnimationBlueprintLibrary::AddCurve(Anim, CurveFName, CurveType, bMetaData);
			EndAnimEdit(Anim);

			if (!UAnimationBlueprintLibrary::DoesCurveExist(Anim, CurveFName, CurveType))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("The engine did not create curve '%s' on '%s'. See the Output Log."), *CurveName, *Anim->GetName()));
			}

			TArray<FName> AllCurves;
			UAnimationBlueprintLibrary::GetAnimationCurveNames(Anim, CurveType, AllCurves);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset"), Anim->GetName());
			Result->SetStringField(TEXT("path"), Anim->GetPathName());
			Result->SetStringField(TEXT("curve_name"), CurveName);
			Result->SetStringField(TEXT("curve_type"), CurveTypeName);
			Result->SetBoolField(TEXT("is_metadata_curve"), bMetaData);
			Result->SetNumberField(TEXT("curve_count"), AllCurves.Num());

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Added %s curve '%s' to '%s'. It is empty — add keys with anim_add_float_curve_keys."),
					*CurveTypeName, *CurveName, *Anim->GetName()),
				Result);
		});

	MCP_TOOL(Registry, "anim_remove_curve")
		.Description(TEXT(
			"Remove a named curve from an animation. By default the curve NAME is left registered on the "
			"skeleton (other animations may still use it); pass remove_name_from_skeleton=true to unregister "
			"it entirely, which affects every animation on that skeleton."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence or AnimMontage"), true)
		.StringArg(TEXT("curve_name"), TEXT("Name of the curve to remove"), true)
		.BoolArg(TEXT("remove_name_from_skeleton"), TEXT("Also unregister the curve name from the skeleton, affecting every animation on it (default: false)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequenceBase* Anim = LoadAnim(Args, Err);
			if (!Anim) return Err;

			FString CurveName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("curve_name"), CurveName));

			const FName CurveFName(*CurveName);
			if (!UAnimationBlueprintLibrary::DoesCurveExist(Anim, CurveFName, ERawCurveTrackTypes::RCT_Float) &&
				!UAnimationBlueprintLibrary::DoesCurveExist(Anim, CurveFName, ERawCurveTrackTypes::RCT_Transform))
			{
				TArray<FName> Existing;
				UAnimationBlueprintLibrary::GetAnimationCurveNames(Anim, ERawCurveTrackTypes::RCT_Float, Existing);
				TArray<FString> Names;
				for (const FName& Name : Existing) { Names.Add(Name.ToString()); }

				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("'%s' has no curve named '%s'."), *Anim->GetName(), *CurveName),
					FString(), Names);
			}

			bool bRemoveFromSkeleton = false;
			Args->TryGetBoolField(TEXT("remove_name_from_skeleton"), bRemoveFromSkeleton);

			BeginAnimEdit(Anim);
			UAnimationBlueprintLibrary::RemoveCurve(Anim, CurveFName, bRemoveFromSkeleton);
			EndAnimEdit(Anim);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset"), Anim->GetName());
			Result->SetStringField(TEXT("path"), Anim->GetPathName());
			Result->SetStringField(TEXT("removed_curve"), CurveName);
			Result->SetBoolField(TEXT("removed_from_skeleton"), bRemoveFromSkeleton);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Removed curve '%s' from '%s'%s."), *CurveName, *Anim->GetName(),
					bRemoveFromSkeleton ? TEXT(" and unregistered the name from the skeleton") : TEXT("")),
				Result);
		});

	MCP_TOOL(Registry, "anim_add_float_curve_keys")
		.Description(TEXT(
			"Add float keys to an animation curve, creating the curve if it does not exist. times and values "
			"are parallel arrays and must be the same length. This is how a curve gets its shape: two keys "
			"(0.0 -> 1.0 at the strike frame, 1.0 -> 0.0 just after) gives you a gate an AnimBP or gameplay "
			"code can read with GetCurveValue."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence or AnimMontage"), true)
		.StringArg(TEXT("curve_name"), TEXT("Name of the float curve"), true)
		.StringArg(TEXT("times"), TEXT("Comma-separated key times in seconds (e.g. '0.0,0.35,0.5')"), true)
		.StringArg(TEXT("values"), TEXT("Comma-separated key values, one per time (e.g. '0.0,1.0,0.0')"), true)
		.BoolArg(TEXT("create_if_missing"), TEXT("Create the curve if it does not exist (default: true)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/AM_HeroCombo\", \"curve_name\": \"ComboWindow\", \"times\": \"0.0,0.45,0.75,1.0\", \"values\": \"0.0,1.0,1.0,0.0\"}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequenceBase* Anim = LoadAnim(Args, Err);
			if (!Anim) return Err;

			FString CurveName, TimesRaw, ValuesRaw;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("curve_name"), CurveName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("times"), TimesRaw));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("values"), ValuesRaw));

			TArray<FString> TimeTokens, ValueTokens;
			TimesRaw.ParseIntoArray(TimeTokens, TEXT(","), /*bCullEmpty*/ true);
			ValuesRaw.ParseIntoArray(ValueTokens, TEXT(","), /*bCullEmpty*/ true);

			if (TimeTokens.Num() == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("times parsed to zero values."),
					TEXT("Pass a comma-separated list, e.g. '0.0,0.5,1.0'."));
			}
			if (TimeTokens.Num() != ValueTokens.Num())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("times has %d entries but values has %d; they must be parallel arrays."),
						TimeTokens.Num(), ValueTokens.Num()));
			}

			const float Length = Anim->GetPlayLength();
			TArray<float> Times, Values;
			for (int32 i = 0; i < TimeTokens.Num(); ++i)
			{
				const float Time = FCString::Atof(*TimeTokens[i].TrimStartAndEnd());
				if (Time < 0.0f || Time > Length + KINDA_SMALL_NUMBER)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
						FString::Printf(TEXT("Key time %.4f (entry %d) is outside '%s' range [0, %.4f]."),
							Time, i, *Anim->GetName(), Length));
				}
				Times.Add(Time);
				Values.Add(FCString::Atof(*ValueTokens[i].TrimStartAndEnd()));
			}

			bool bCreate = true;
			Args->TryGetBoolField(TEXT("create_if_missing"), bCreate);

			const FName CurveFName(*CurveName);
			const bool bExists = UAnimationBlueprintLibrary::DoesCurveExist(Anim, CurveFName, ERawCurveTrackTypes::RCT_Float);
			if (!bExists && !bCreate)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("'%s' has no float curve named '%s'."), *Anim->GetName(), *CurveName),
					TEXT("Pass create_if_missing=true, or create it with anim_add_curve."));
			}

			BeginAnimEdit(Anim);
			if (!bExists)
			{
				UAnimationBlueprintLibrary::AddCurve(Anim, CurveFName, ERawCurveTrackTypes::RCT_Float, /*bMetaDataCurve*/ false);
			}
			UAnimationBlueprintLibrary::AddFloatCurveKeys(Anim, CurveFName, Times, Values);
			EndAnimEdit(Anim);

			TArray<float> ReadTimes, ReadValues;
			UAnimationBlueprintLibrary::GetFloatKeys(Anim, CurveFName, ReadTimes, ReadValues);

			TArray<TSharedPtr<FJsonValue>> Keys;
			for (int32 i = 0; i < ReadTimes.Num(); ++i)
			{
				TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
				Key->SetNumberField(TEXT("time"), ReadTimes[i]);
				Key->SetNumberField(TEXT("value"), ReadValues.IsValidIndex(i) ? ReadValues[i] : 0.0f);
				Keys.Add(MakeShared<FJsonValueObject>(Key));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset"), Anim->GetName());
			Result->SetStringField(TEXT("path"), Anim->GetPathName());
			Result->SetStringField(TEXT("curve_name"), CurveName);
			Result->SetBoolField(TEXT("created_curve"), !bExists);
			Result->SetNumberField(TEXT("added_key_count"), Times.Num());
			Result->SetNumberField(TEXT("total_key_count"), Keys.Num());
			Result->SetArrayField(TEXT("keys"), Keys);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Added %d key(s) to curve '%s' on '%s' (%d total)."),
					Times.Num(), *CurveName, *Anim->GetName(), Keys.Num()),
				Result);
		});

	MCP_TOOL(Registry, "anim_get_curve_keys")
		.Description(TEXT(
			"Read an animation's curves. With no curve_name, lists every float and transform curve on the "
			"asset; with a curve_name, returns that curve's keys as time/value pairs. This is the read-back "
			"half of curve authoring — call it after anim_add_float_curve_keys to confirm the shape."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence or AnimMontage"), true)
		.StringArg(TEXT("curve_name"), TEXT("Optional: return this curve's keys instead of listing all curve names"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequenceBase* Anim = LoadAnim(Args, Err);
			if (!Anim) return Err;

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset"), Anim->GetName());
			Result->SetStringField(TEXT("path"), Anim->GetPathName());
			Result->SetNumberField(TEXT("duration"), Anim->GetPlayLength());

			TArray<FName> FloatCurves, TransformCurves;
			UAnimationBlueprintLibrary::GetAnimationCurveNames(Anim, ERawCurveTrackTypes::RCT_Float, FloatCurves);
			UAnimationBlueprintLibrary::GetAnimationCurveNames(Anim, ERawCurveTrackTypes::RCT_Transform, TransformCurves);

			FString CurveName;
			if (Args->TryGetStringField(TEXT("curve_name"), CurveName) && !CurveName.IsEmpty())
			{
				const FName CurveFName(*CurveName);
				if (!UAnimationBlueprintLibrary::DoesCurveExist(Anim, CurveFName, ERawCurveTrackTypes::RCT_Float))
				{
					TArray<FString> Names;
					for (const FName& Name : FloatCurves) { Names.Add(Name.ToString()); }
					return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
						FString::Printf(TEXT("'%s' has no float curve named '%s'."), *Anim->GetName(), *CurveName),
						TEXT("Call anim_get_curve_keys without curve_name to list every curve on this asset."),
						Names);
				}

				TArray<float> Times, Values;
				UAnimationBlueprintLibrary::GetFloatKeys(Anim, CurveFName, Times, Values);

				TArray<TSharedPtr<FJsonValue>> Keys;
				float MinValue = TNumericLimits<float>::Max();
				float MaxValue = TNumericLimits<float>::Lowest();
				for (int32 i = 0; i < Times.Num(); ++i)
				{
					const float Value = Values.IsValidIndex(i) ? Values[i] : 0.0f;
					MinValue = FMath::Min(MinValue, Value);
					MaxValue = FMath::Max(MaxValue, Value);

					TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
					Key->SetNumberField(TEXT("time"), Times[i]);
					Key->SetNumberField(TEXT("value"), Value);
					Keys.Add(MakeShared<FJsonValueObject>(Key));
				}

				Result->SetStringField(TEXT("curve_name"), CurveName);
				Result->SetNumberField(TEXT("key_count"), Keys.Num());
				Result->SetArrayField(TEXT("keys"), Keys);
				if (Keys.Num() > 0)
				{
					Result->SetNumberField(TEXT("min_value"), MinValue);
					Result->SetNumberField(TEXT("max_value"), MaxValue);
				}

				return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
			}

			TArray<TSharedPtr<FJsonValue>> FloatJson, TransformJson;
			for (const FName& Name : FloatCurves)     { FloatJson.Add(MakeShared<FJsonValueString>(Name.ToString())); }
			for (const FName& Name : TransformCurves) { TransformJson.Add(MakeShared<FJsonValueString>(Name.ToString())); }

			Result->SetArrayField(TEXT("float_curves"), FloatJson);
			Result->SetArrayField(TEXT("transform_curves"), TransformJson);
			Result->SetNumberField(TEXT("float_curve_count"), FloatJson.Num());
			Result->SetNumberField(TEXT("transform_curve_count"), TransformJson.Num());

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	MCP_TOOL(Registry, "anim_set_curve_metadata")
		.Description(TEXT(
			"Flag a curve name on a SKELETON as driving a morph target and/or a material parameter. This is "
			"what makes a float curve do something without any Blueprint wiring: a curve flagged as a morph "
			"target drives the morph of the same name on every skeletal mesh using that skeleton (the basis "
			"of facial animation), and one flagged as a material parameter drives the scalar parameter of "
			"that name. The flag lives on the skeleton, so it applies to every animation on it — this tool "
			"takes the skeleton path, not an animation path."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("skeleton_path"), TEXT("Content path to the USkeleton that owns the curve name"), true)
		.StringArg(TEXT("curve_name"), TEXT("Curve name to flag"), true)
		.BoolArg(TEXT("morph_target"), TEXT("Drive the morph target of the same name"))
		.BoolArg(TEXT("material"), TEXT("Drive the material scalar parameter of the same name"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString SkeletonPath, CurveName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("skeleton_path"), SkeletonPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("curve_name"), CurveName));

			FMCPToolResult Err;
			USkeleton* Skeleton = MCPCommon::LoadAssetChecked<USkeleton>(SkeletonPath, Err);
			if (!Skeleton) return Err;

			const bool bHasMorph = Args->HasField(TEXT("morph_target"));
			const bool bHasMaterial = Args->HasField(TEXT("material"));
			if (!bHasMorph && !bHasMaterial)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("Pass morph_target and/or material."));
			}

			const FName CurveFName(*CurveName);

			// The metadata entry must exist before its flags can be set.
			TArray<FName> MetaNames;
			UAnimationBlueprintLibrary::GetCurveMetaDataNames(Skeleton, MetaNames);
			if (!MetaNames.Contains(CurveFName))
			{
				UAnimationBlueprintLibrary::AddCurveMetaData(Skeleton, CurveFName, /*bTransact*/ true);
			}

			Skeleton->Modify();

			bool bMorph = false, bMaterial = false;
			if (bHasMorph)
			{
				Args->TryGetBoolField(TEXT("morph_target"), bMorph);
				UAnimationBlueprintLibrary::SetCurveMetaDataMorphTarget(Skeleton, CurveFName, bMorph);
			}
			if (bHasMaterial)
			{
				Args->TryGetBoolField(TEXT("material"), bMaterial);
				UAnimationBlueprintLibrary::SetCurveMetaDataMaterial(Skeleton, CurveFName, bMaterial);
			}

			Skeleton->MarkPackageDirty();

			const bool bReadMorph = UAnimationBlueprintLibrary::GetCurveMetaDataMorphTarget(Skeleton, CurveFName);
			const bool bReadMaterial = UAnimationBlueprintLibrary::GetCurveMetaDataMaterial(Skeleton, CurveFName);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("skeleton"), Skeleton->GetName());
			Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
			Result->SetStringField(TEXT("curve_name"), CurveName);
			Result->SetBoolField(TEXT("morph_target"), bReadMorph);
			Result->SetBoolField(TEXT("material"), bReadMaterial);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Curve '%s' on skeleton '%s': morph_target=%s, material=%s."),
					*CurveName, *Skeleton->GetName(),
					bReadMorph ? TEXT("true") : TEXT("false"),
					bReadMaterial ? TEXT("true") : TEXT("false")),
				Result);
		});

	// =================================================================
	// Sync markers
	// =================================================================

	MCP_TOOL(Registry, "anim_add_sync_marker")
		.Description(TEXT(
			"Add a sync marker to an AnimSequence. Sync markers are named points (conventionally the "
			"footfalls: 'LeftFootDown', 'RightFootDown') that let animations in a sync group align by "
			"PHASE rather than by normalised time — the reason a walk can blend into a run without the feet "
			"skating, even though the two clips have different lengths. Every animation in the group needs "
			"the same marker names in the same order. Markers live on AnimSequences only; a montage "
			"inherits them from the sequences in its slot tracks."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence"), true)
		.StringArg(TEXT("marker_name"), TEXT("Marker name (e.g. 'LeftFootDown'). Must match across every animation in the sync group."), true)
		.NumberArg(TEXT("time_seconds"), TEXT("Marker time in seconds"), true)
		.StringArg(TEXT("track_name"), TEXT("Notify track to place the marker on (default: the animation's first track)"))
		.BoolArg(TEXT("create_track"), TEXT("Create the notify track if it does not exist (default: false)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Anims/AS_Run\", \"marker_name\": \"LeftFootDown\", \"time_seconds\": 0.12}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequence* Sequence = LoadSequenceForMarkers(Args, Err);
			if (!Sequence) return Err;

			FString MarkerName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("marker_name"), MarkerName));

			double TimeSeconds = 0.0;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("time_seconds"), TimeSeconds));
			if (!RequireTimeInRange(Sequence, TimeSeconds, TEXT("time_seconds"), Err)) return Err;

			FString TrackName;
			Args->TryGetStringField(TEXT("track_name"), TrackName);
			bool bCreateTrack = false;
			Args->TryGetBoolField(TEXT("create_track"), bCreateTrack);

			BeginAnimEdit(Sequence);

			FName ResolvedTrack;
			if (!ResolveNotifyTrack(Sequence, TrackName, bCreateTrack, ResolvedTrack, Err)) return Err;

			const int32 Before = Sequence->AuthoredSyncMarkers.Num();
			UAnimationBlueprintLibrary::AddAnimationSyncMarker(
				Sequence, FName(*MarkerName), (float)TimeSeconds, ResolvedTrack);

			if (Sequence->AuthoredSyncMarkers.Num() != Before + 1)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("The engine did not add sync marker '%s' to '%s'. See the Output Log."),
						*MarkerName, *Sequence->GetName()));
			}

			EndAnimEdit(Sequence);

			TArray<FAnimSyncMarker> Markers;
			UAnimationBlueprintLibrary::GetAnimationSyncMarkers(Sequence, Markers);

			TArray<TSharedPtr<FJsonValue>> MarkerJson;
			for (const FAnimSyncMarker& Marker : Markers)
			{
				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("name"), Marker.MarkerName.ToString());
				Obj->SetNumberField(TEXT("time"), Marker.Time);
				MarkerJson.Add(MakeShared<FJsonValueObject>(Obj));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset"), Sequence->GetName());
			Result->SetStringField(TEXT("path"), Sequence->GetPathName());
			Result->SetStringField(TEXT("added_marker"), MarkerName);
			Result->SetNumberField(TEXT("marker_count"), MarkerJson.Num());
			Result->SetArrayField(TEXT("markers"), MarkerJson);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Added sync marker '%s' at %.3fs to '%s' (%d marker(s) total)."),
					*MarkerName, TimeSeconds, *Sequence->GetName(), MarkerJson.Num()),
				Result);
		});

	MCP_TOOL(Registry, "anim_list_sync_markers")
		.Description(TEXT(
			"List an AnimSequence's sync markers with their times, plus the set of unique marker names. "
			"Use it to check that every animation intended for one sync group carries the same marker names "
			"in the same order — a mismatch makes the group silently fall back to normalised-time sync."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequence* Sequence = LoadSequenceForMarkers(Args, Err);
			if (!Sequence) return Err;

			TArray<FAnimSyncMarker> Markers;
			UAnimationBlueprintLibrary::GetAnimationSyncMarkers(Sequence, Markers);

			TArray<FName> UniqueNames;
			UAnimationBlueprintLibrary::GetUniqueMarkerNames(Sequence, UniqueNames);

			TArray<TSharedPtr<FJsonValue>> MarkerJson;
			for (const FAnimSyncMarker& Marker : Markers)
			{
				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("name"), Marker.MarkerName.ToString());
				Obj->SetNumberField(TEXT("time"), Marker.Time);
				if (Sequence->AnimNotifyTracks.IsValidIndex(Marker.TrackIndex))
				{
					Obj->SetStringField(TEXT("track_name"), Sequence->AnimNotifyTracks[Marker.TrackIndex].TrackName.ToString());
				}
				MarkerJson.Add(MakeShared<FJsonValueObject>(Obj));
			}

			TArray<TSharedPtr<FJsonValue>> NameJson;
			for (const FName& Name : UniqueNames)
			{
				NameJson.Add(MakeShared<FJsonValueString>(Name.ToString()));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset"), Sequence->GetName());
			Result->SetStringField(TEXT("path"), Sequence->GetPathName());
			Result->SetNumberField(TEXT("duration"), Sequence->GetPlayLength());
			Result->SetNumberField(TEXT("marker_count"), MarkerJson.Num());
			Result->SetArrayField(TEXT("markers"), MarkerJson);
			Result->SetArrayField(TEXT("unique_marker_names"), NameJson);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	MCP_TOOL(Registry, "anim_remove_sync_markers")
		.Description(TEXT(
			"Remove sync markers from an AnimSequence — all of them, or only those with a given name. "
			"Removing markers from one animation in a sync group breaks phase sync for the whole group, so "
			"remove from every member or none."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to an AnimSequence"), true)
		.StringArg(TEXT("marker_name"), TEXT("Remove only markers with this name. Omit to remove every marker on the animation."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimSequence* Sequence = LoadSequenceForMarkers(Args, Err);
			if (!Sequence) return Err;

			const int32 Before = Sequence->AuthoredSyncMarkers.Num();
			if (Before == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("'%s' has no sync markers."), *Sequence->GetName()));
			}

			FString MarkerName;
			const bool bByName = Args->TryGetStringField(TEXT("marker_name"), MarkerName) && !MarkerName.IsEmpty();

			BeginAnimEdit(Sequence);

			int32 Removed = 0;
			if (bByName)
			{
				const FName Wanted(*MarkerName);
				Removed = Sequence->AuthoredSyncMarkers.RemoveAll(
					[Wanted](const FAnimSyncMarker& Marker) { return Marker.MarkerName == Wanted; });

				if (Removed == 0)
				{
					TArray<FName> UniqueNames;
					UAnimationBlueprintLibrary::GetUniqueMarkerNames(Sequence, UniqueNames);
					TArray<FString> Names;
					for (const FName& Name : UniqueNames) { Names.Add(Name.ToString()); }

					return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
						FString::Printf(TEXT("'%s' has no sync marker named '%s'."), *Sequence->GetName(), *MarkerName),
						FString(), Names);
				}
			}
			else
			{
				UAnimationBlueprintLibrary::RemoveAllAnimationSyncMarkers(Sequence);
				Removed = Before;
			}

			// UniqueMarkerNames is derived from AuthoredSyncMarkers, and
			// RefreshCacheData does NOT rebuild it (it only sorts the markers and
			// re-bins them onto notify tracks). Without this, GetUniqueMarkerNames
			// keeps reporting names that no longer have any marker.
			Sequence->RefreshSyncMarkerDataFromAuthored();
			EndAnimEdit(Sequence);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset"), Sequence->GetName());
			Result->SetStringField(TEXT("path"), Sequence->GetPathName());
			Result->SetNumberField(TEXT("removed_count"), Removed);
			Result->SetNumberField(TEXT("remaining_count"), Sequence->AuthoredSyncMarkers.Num());

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Removed %d sync marker(s) from '%s'; %d remain."),
					Removed, *Sequence->GetName(), Sequence->AuthoredSyncMarkers.Num()),
				Result);
		});
}

} // namespace MCPAnimTools::AnimData
