// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.6 Phase 5 — skeletal animation in Sequencer.
//
// v4.5's add_sequence_track handled Transform and Visibility only, so a level
// sequence could move a character but never animate one — the single largest
// gap for cinematics work. This file adds the skeletal animation track, its
// sections, and the bake/link round trip back to an AnimSequence.
//
// 5.8 note: FMovieSceneSkeletalAnimationParams::PlayRate is no longer a float.
// It is an FMovieSceneTimeWarpVariant, which can hold either a constant rate or
// a full time-warp curve. Setting it goes through Set(double), not assignment.

#include "Tools/Sequencer/SequencerCommon.h"
#include "Tools/Animation/AnimCommon.h"
#include "Common/MCPAssetResolver.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/MirrorDataTable.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Exporters/AnimSeqExportOption.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneBindingProxy.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneSection.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Variants/MovieSceneTimeWarpVariant.h"
#include "SequencerTools.h"
#include "Common/MCPAssetCreate.h"

namespace MCPSequencerTools::AnimationTracks
{

using namespace MCPSequencerTools::Common;

// ---------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------

/** Resolve sequence + movie scene from the sequence_path arg. */
static ULevelSequence* ResolveSequence(const TSharedPtr<FJsonObject>& Args,
	UMovieScene*& OutMovieScene, FMCPToolResult& OutError)
{
	FString SequencePath;
	FMCPValidateResult Check = FMCPValidate::RequiredString(Args, TEXT("sequence_path"), SequencePath);
	if (!Check.bOk)
	{
		OutError = Check.Error;
		return nullptr;
	}

	ULevelSequence* Sequence = LoadSequence(SequencePath);
	if (!IsValid(Sequence))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
			FString::Printf(TEXT("LevelSequence not found: %s"), *SequencePath),
			TEXT("Create one with create_level_sequence, or list them with list_assets."));
		return nullptr;
	}

	OutMovieScene = Sequence->GetMovieScene();
	if (!OutMovieScene)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal,
			FString::Printf(TEXT("LevelSequence '%s' has no MovieScene."), *Sequence->GetName()));
		return nullptr;
	}
	return Sequence;
}

/** Resolve the binding for this tool call: either an explicit binding_id GUID,
 *  or an actor_name that is looked up and auto-bound. Returns an invalid FGuid
 *  and fills OutError on failure. */
static FGuid ResolveBinding(const TSharedPtr<FJsonObject>& Args, ULevelSequence* Sequence,
	UMovieScene* MovieScene, bool bAutoBind, FMCPToolResult& OutError)
{
	FString BindingId;
	if (Args->TryGetStringField(TEXT("binding_id"), BindingId) && !BindingId.IsEmpty())
	{
		FGuid Guid;
		if (!FGuid::Parse(BindingId, Guid))
		{
			OutError = FMCPToolResult::ErrorStructured(EMCPError::InvalidName,
				FString::Printf(TEXT("'%s' is not a valid binding GUID."), *BindingId));
			return FGuid();
		}
		if (!MovieScene->FindPossessable(Guid) && !MovieScene->FindSpawnable(Guid))
		{
			OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Sequence '%s' has no binding with id %s."), *Sequence->GetName(), *BindingId),
				TEXT("Use get_sequence_info to list bindings, or pass actor_name instead to bind by label."));
			return FGuid();
		}
		return Guid;
	}

	FString ActorName;
	if (!Args->TryGetStringField(TEXT("actor_name"), ActorName) || ActorName.IsEmpty())
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
			TEXT("Pass actor_name (bound automatically) or binding_id (an existing binding GUID)."));
		return FGuid();
	}

	FGuid Existing = FindPossessableGuid(MovieScene, ActorName);
	if (Existing.IsValid())
	{
		return Existing;
	}

	if (!bAutoBind)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
			FString::Printf(TEXT("Actor '%s' is not bound in sequence '%s'."), *ActorName, *Sequence->GetName()),
			TEXT("Bind it first with add_actor_to_sequence or add_animation_track."));
		return FGuid();
	}

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("No editor world available."));
		return FGuid();
	}

	AActor* Actor = FindActorByLabel(World, ActorName);
	if (!Actor)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
			FString::Printf(TEXT("Actor not found in the current level: %s"), *ActorName),
			TEXT("Use list_actors to see what is in the level."));
		return FGuid();
	}

	// An animation track is only meaningful on something with a skeletal mesh.
	if (!Actor->FindComponentByClass<USkeletalMeshComponent>())
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
			FString::Printf(TEXT("Actor '%s' has no SkeletalMeshComponent, so it cannot take an animation track."), *ActorName),
			TEXT("Animation tracks bind to skeletal-mesh actors (SkeletalMeshActor, Character). Use add_sequence_track for transform/visibility on other actors."));
		return FGuid();
	}

	const FGuid NewGuid = MovieScene->AddPossessable(ActorName, Actor->GetClass());
	if (!NewGuid.IsValid())
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal,
			FString::Printf(TEXT("Failed to add '%s' as a possessable."), *ActorName));
		return FGuid();
	}
	Sequence->BindPossessableObject(NewGuid, *Actor, World);
	return NewGuid;
}

/** Find the skeletal animation track on a binding. */
static UMovieSceneSkeletalAnimationTrack* FindAnimTrack(UMovieScene* MovieScene, const FGuid& Binding)
{
	return MovieScene->FindTrack<UMovieSceneSkeletalAnimationTrack>(Binding);
}

/** The skeleton a binding's actor drives, for compatibility checks.
 *
 *  Resolved through the binding's NAME rather than
 *  UMovieSceneSequence::LocateBoundObjects: the FResolveParams overload of that
 *  is deprecated in 5.8 in favour of one taking an FSharedPlaybackState, which
 *  only exists while a sequence is being played back. Possessables are created
 *  from the actor label (see add_actor_to_sequence and ResolveBinding below), so
 *  the label round-trips; spawnables carry their object template directly.
 *  Returns null when the actor is not in the open level, and callers treat that
 *  as "cannot check" rather than as a failure. */
static USkeleton* GetBindingSkeleton(ULevelSequence* Sequence, const FGuid& Binding)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return nullptr;
	}

	auto SkeletonFromObject = [](UObject* Object) -> USkeleton*
	{
		AActor* Actor = Cast<AActor>(Object);
		USkeletalMeshComponent* Component = Actor
			? Actor->FindComponentByClass<USkeletalMeshComponent>()
			: Cast<USkeletalMeshComponent>(Object);

		if (Component && Component->GetSkeletalMeshAsset())
		{
			return Component->GetSkeletalMeshAsset()->GetSkeleton();
		}
		return nullptr;
	};

	// Spawnable: the template object is right there, no world lookup needed.
	if (FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(Binding))
	{
		if (USkeleton* Skeleton = SkeletonFromObject(Spawnable->GetObjectTemplate()))
		{
			return Skeleton;
		}
	}

	// Possessable: find the actor in the open level by the label it was bound under.
	if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Binding))
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			return nullptr;
		}
		if (AActor* Actor = FindActorByLabel(World, Possessable->GetName()))
		{
			return SkeletonFromObject(Actor);
		}
	}

	return nullptr;
}

/** Serialize one animation section. */
static TSharedPtr<FJsonObject> SectionToJson(UMovieSceneSkeletalAnimationSection* Section,
	int32 Index, FFrameRate TickResolution)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("index"), Index);
	Obj->SetNumberField(TEXT("row_index"), Section->GetRowIndex());

	const TRange<FFrameNumber> Range = Section->GetRange();
	if (Range.HasLowerBound())
	{
		const FFrameNumber Start = Range.GetLowerBoundValue();
		Obj->SetNumberField(TEXT("start_frame"), Start.Value);
		Obj->SetNumberField(TEXT("start_seconds"), TickResolution.AsSeconds(FFrameTime(Start)));
	}
	if (Range.HasUpperBound())
	{
		const FFrameNumber End = Range.GetUpperBoundValue();
		Obj->SetNumberField(TEXT("end_frame"), End.Value);
		Obj->SetNumberField(TEXT("end_seconds"), TickResolution.AsSeconds(FFrameTime(End)));
	}

	const FMovieSceneSkeletalAnimationParams& Params = Section->Params;
	Obj->SetStringField(TEXT("animation"), Params.Animation ? Params.Animation->GetName() : TEXT("(none)"));
	Obj->SetStringField(TEXT("animation_path"), Params.Animation ? Params.Animation->GetPathName() : TEXT(""));
	Obj->SetNumberField(TEXT("start_frame_offset"), Params.StartFrameOffset.Value);
	Obj->SetNumberField(TEXT("first_loop_start_frame_offset"), Params.FirstLoopStartFrameOffset.Value);
	Obj->SetStringField(TEXT("slot_name"), Params.SlotName.ToString());
	Obj->SetStringField(TEXT("mirror_data_table"),
		Params.MirrorDataTable ? Params.MirrorDataTable->GetPathName() : TEXT(""));

	// 5.8: PlayRate is a time-warp variant, which may hold a constant rate or a
	// whole curve. AsFixedPlayRate() is only valid for the FixedPlayRate type.
	if (Params.PlayRate.GetType() == EMovieSceneTimeWarpType::FixedPlayRate)
	{
		Obj->SetNumberField(TEXT("play_rate"), Params.PlayRate.AsFixedPlayRate());
	}
	else
	{
		Obj->SetStringField(TEXT("play_rate"), FString::Printf(TEXT("(time-warp: %s)"),
			*StaticEnum<EMovieSceneTimeWarpType>()->GetNameStringByValue((int64)Params.PlayRate.GetType())));
	}

	return Obj;
}

// ---------------------------------------------------------------------

void RegisterAll(FMCPToolRegistry& Registry)
{
	// =================================================================
	// add_animation_track
	// =================================================================
	MCP_TOOL(Registry, "add_animation_track")
		.Description(TEXT(
			"Add a skeletal animation track to an actor in a level sequence — the track that makes a "
			"character actually animate in a cinematic, as opposed to the transform track that only moves "
			"it. The actor is bound automatically if it is not already, and must have a SkeletalMeshComponent. "
			"Idempotent: returns the existing track if the binding already has one. Add clips to it with "
			"add_animation_section."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the ULevelSequence"), true)
		.StringArg(TEXT("actor_name"), TEXT("Label of the skeletal-mesh actor to animate (auto-bound if needed)"))
		.StringArg(TEXT("binding_id"), TEXT("Existing binding GUID, as an alternative to actor_name"))
		.Example(TEXT("{\"sequence_path\": \"/Game/Cinematics/LS_Intro\", \"actor_name\": \"Hero\"}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			UMovieScene* MovieScene = nullptr;
			FMCPToolResult Err;
			ULevelSequence* Sequence = ResolveSequence(Args, MovieScene, Err);
			if (!Sequence) return Err;

			const FGuid Binding = ResolveBinding(Args, Sequence, MovieScene, /*bAutoBind*/ true, Err);
			if (!Binding.IsValid()) return Err;

			UMovieSceneSkeletalAnimationTrack* Track = FindAnimTrack(MovieScene, Binding);
			bool bCreated = false;
			if (!Track)
			{
				MovieScene->Modify();
				Track = MovieScene->AddTrack<UMovieSceneSkeletalAnimationTrack>(Binding);
				bCreated = true;
			}

			if (!Track)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Failed to add a skeletal animation track to that binding."));
			}

			Sequence->MarkPackageDirty();

			USkeleton* Skeleton = GetBindingSkeleton(Sequence, Binding);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
			Result->SetStringField(TEXT("binding_id"), Binding.ToString());
			Result->SetBoolField(TEXT("created"), bCreated);
			Result->SetNumberField(TEXT("section_count"), Track->GetAllSections().Num());
			if (Skeleton)
			{
				Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
			}

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%s animation track on binding %s of '%s'. Add clips with add_animation_section.%s"),
					bCreated ? TEXT("Added a skeletal") : TEXT("Found an existing skeletal"),
					*Binding.ToString(), *Sequence->GetName(),
					Skeleton ? *FString::Printf(TEXT(" Bound skeleton: %s."), *Skeleton->GetName()) : TEXT("")),
				Result);
		});

	// =================================================================
	// add_animation_section
	// =================================================================
	MCP_TOOL(Registry, "add_animation_section")
		.Description(TEXT(
			"Place an animation clip on a binding's skeletal animation track, creating the track if needed. "
			"The section starts at start_seconds and, by default, runs for the animation's full length. "
			"Sections on the same row cannot overlap — pass row_index to stack a second clip on its own row "
			"so Sequencer blends between them. The animation must target the bound actor's skeleton."))
		.SupportsDryRun()
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the ULevelSequence"), true)
		.StringArg(TEXT("animation_path"), TEXT("Content path to the UAnimSequence to place"), true)
		.StringArg(TEXT("actor_name"), TEXT("Label of the skeletal-mesh actor (auto-bound if needed)"))
		.StringArg(TEXT("binding_id"), TEXT("Existing binding GUID, as an alternative to actor_name"))
		.NumberArg(TEXT("start_seconds"), TEXT("Where the clip starts on the sequence timeline (default: 0)"))
		.IntArg(TEXT("row_index"), TEXT("Track row to place the section on. Use a different row to overlap and blend two clips (default: the first free row)."))
		.Example(TEXT("{\"sequence_path\": \"/Game/Cinematics/LS_Intro\", \"actor_name\": \"Hero\", \"animation_path\": \"/Game/Anims/AS_Wave\", \"start_seconds\": 1.5}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			UMovieScene* MovieScene = nullptr;
			FMCPToolResult Err;
			ULevelSequence* Sequence = ResolveSequence(Args, MovieScene, Err);
			if (!Sequence) return Err;

			FString AnimPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("animation_path"), AnimPath));
			UAnimSequenceBase* Anim = MCPCommon::LoadAssetChecked<UAnimSequenceBase>(AnimPath, Err);
			if (!Anim) return Err;

			const FGuid Binding = ResolveBinding(Args, Sequence, MovieScene, /*bAutoBind*/ true, Err);
			if (!Binding.IsValid()) return Err;

			// Skeleton compatibility: a mismatched clip binds but evaluates to garbage.
			if (USkeleton* BoundSkeleton = GetBindingSkeleton(Sequence, Binding))
			{
				if (!MCPAnimTools::Common::RequireSameSkeleton(BoundSkeleton, Anim, TEXT("animation_path"), Err))
				{
					return Err;
				}
			}

			MovieScene->Modify();

			UMovieSceneSkeletalAnimationTrack* Track = FindAnimTrack(MovieScene, Binding);
			bool bCreatedTrack = false;
			if (!Track)
			{
				Track = MovieScene->AddTrack<UMovieSceneSkeletalAnimationTrack>(Binding);
				bCreatedTrack = true;
			}
			if (!Track)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Failed to add a skeletal animation track to that binding."));
			}

			const FFrameRate TickResolution = MovieScene->GetTickResolution();
			const double StartSeconds = Args->HasField(TEXT("start_seconds"))
				? Args->GetNumberField(TEXT("start_seconds"))
				: 0.0;
			if (StartSeconds < 0.0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("start_seconds cannot be negative."));
			}
			const FFrameNumber StartFrame = (StartSeconds * TickResolution).RoundToFrame();

			const int32 RowIndex = Args->HasField(TEXT("row_index"))
				? (int32)Args->GetNumberField(TEXT("row_index"))
				: INDEX_NONE;

			Track->Modify();
			UMovieSceneSection* NewSection = Track->AddNewAnimationOnRow(StartFrame, Anim, RowIndex);
			if (!NewSection)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Failed to add '%s' to the animation track."), *Anim->GetName()),
					TEXT("A section may already occupy that time on that row — pass a different row_index or start_seconds."));
			}

			Sequence->MarkPackageDirty();

			UMovieSceneSkeletalAnimationSection* AnimSection = CastChecked<UMovieSceneSkeletalAnimationSection>(NewSection);
			const TArray<UMovieSceneSection*>& AllSections = Track->GetAllSections();

			TSharedPtr<FJsonObject> Result = SectionToJson(AnimSection, AllSections.Find(NewSection), TickResolution);
			Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
			Result->SetStringField(TEXT("binding_id"), Binding.ToString());
			Result->SetBoolField(TEXT("created_track"), bCreatedTrack);
			Result->SetNumberField(TEXT("section_count"), AllSections.Num());

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Placed '%s' (%.3fs) at %.3fs on row %d of '%s' (%d section(s) on the track)."),
					*Anim->GetName(), Anim->GetPlayLength(), StartSeconds,
					AnimSection->GetRowIndex(), *Sequence->GetName(), AllSections.Num()),
				Result);
		});

	// =================================================================
	// list_animation_sections
	// =================================================================
	MCP_TOOL(Registry, "list_animation_sections")
		.Description(TEXT(
			"List the animation clips on a binding's skeletal animation track: each section's animation, "
			"row, start/end in both frames and seconds, trim offsets, play rate, slot and mirror table. "
			"The section indices it returns are what set_animation_section_params and "
			"remove_animation_section take."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the ULevelSequence"), true)
		.StringArg(TEXT("actor_name"), TEXT("Label of the bound actor"))
		.StringArg(TEXT("binding_id"), TEXT("Binding GUID, as an alternative to actor_name"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			UMovieScene* MovieScene = nullptr;
			FMCPToolResult Err;
			ULevelSequence* Sequence = ResolveSequence(Args, MovieScene, Err);
			if (!Sequence) return Err;

			const FGuid Binding = ResolveBinding(Args, Sequence, MovieScene, /*bAutoBind*/ false, Err);
			if (!Binding.IsValid()) return Err;

			UMovieSceneSkeletalAnimationTrack* Track = FindAnimTrack(MovieScene, Binding);
			if (!Track)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Binding %s in '%s' has no skeletal animation track."),
						*Binding.ToString(), *Sequence->GetName()),
					TEXT("Add one with add_animation_track or add_animation_section."));
			}

			const FFrameRate TickResolution = MovieScene->GetTickResolution();
			const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();

			TArray<TSharedPtr<FJsonValue>> SectionJson;
			for (int32 i = 0; i < Sections.Num(); ++i)
			{
				if (UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Sections[i]))
				{
					SectionJson.Add(MakeShared<FJsonValueObject>(SectionToJson(AnimSection, i, TickResolution)));
				}
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
			Result->SetStringField(TEXT("binding_id"), Binding.ToString());
			Result->SetNumberField(TEXT("section_count"), SectionJson.Num());
			Result->SetArrayField(TEXT("sections"), SectionJson);
			Result->SetNumberField(TEXT("tick_resolution_numerator"), TickResolution.Numerator);
			Result->SetNumberField(TEXT("tick_resolution_denominator"), TickResolution.Denominator);

			if (USkeleton* Skeleton = GetBindingSkeleton(Sequence, Binding))
			{
				Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
			}

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// =================================================================
	// set_animation_section_params
	// =================================================================
	MCP_TOOL(Registry, "set_animation_section_params")
		.Description(TEXT(
			"Tune one animation section: trim its start (start_frame_offset), retime it (play_rate), route "
			"it through a montage slot (slot_name), mirror it left-to-right (mirror_data_table), or move it "
			"on the timeline (start_seconds / end_seconds). Section indices come from "
			"list_animation_sections.\n"
			"5.8 note: play_rate is stored as a time-warp variant. Setting a number here makes it a constant "
			"rate; a section already carrying a time-warp curve is overwritten by that constant."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the ULevelSequence"), true)
		.IntArg(TEXT("section_index"), TEXT("Index of the section, from list_animation_sections"), true)
		.StringArg(TEXT("actor_name"), TEXT("Label of the bound actor"))
		.StringArg(TEXT("binding_id"), TEXT("Binding GUID, as an alternative to actor_name"))
		.NumberArg(TEXT("start_seconds"), TEXT("Move the section's start to this time on the sequence timeline"))
		.NumberArg(TEXT("end_seconds"), TEXT("Move the section's end to this time (shortens or extends the clip)"))
		.NumberArg(TEXT("play_rate"), TEXT("Constant playback rate; 1.0 = authored speed"))
		.NumberArg(TEXT("start_frame_offset_seconds"), TEXT("Trim this many seconds off the front of the source animation"))
		.StringArg(TEXT("slot_name"), TEXT("Montage slot to play through, so the clip layers over the AnimBP rather than replacing it"))
		.StringArg(TEXT("mirror_data_table"), TEXT("Content path to a UMirrorDataTable to mirror the clip, or empty to clear"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			UMovieScene* MovieScene = nullptr;
			FMCPToolResult Err;
			ULevelSequence* Sequence = ResolveSequence(Args, MovieScene, Err);
			if (!Sequence) return Err;

			const FGuid Binding = ResolveBinding(Args, Sequence, MovieScene, /*bAutoBind*/ false, Err);
			if (!Binding.IsValid()) return Err;

			UMovieSceneSkeletalAnimationTrack* Track = FindAnimTrack(MovieScene, Binding);
			if (!Track)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Binding %s has no skeletal animation track."), *Binding.ToString()));
			}

			double SectionIndexRaw = 0.0;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("section_index"), SectionIndexRaw));
			const int32 SectionIndex = (int32)SectionIndexRaw;

			const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
			if (!Sections.IsValidIndex(SectionIndex))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("section_index %d is out of range; the track has %d section(s)."),
						SectionIndex, Sections.Num()),
					TEXT("Use list_animation_sections to see the valid indices."));
			}

			UMovieSceneSkeletalAnimationSection* Section = Cast<UMovieSceneSkeletalAnimationSection>(Sections[SectionIndex]);
			if (!Section)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Section %d is not a skeletal animation section."), SectionIndex));
			}

			const FFrameRate TickResolution = MovieScene->GetTickResolution();
			bool bChanged = false;

			Section->Modify();

			// --- timeline range ---
			TRange<FFrameNumber> Range = Section->GetRange();
			bool bRangeChanged = false;

			if (Args->HasField(TEXT("start_seconds")))
			{
				const double StartSeconds = Args->GetNumberField(TEXT("start_seconds"));
				Range.SetLowerBound(TRangeBound<FFrameNumber>::Inclusive((StartSeconds * TickResolution).RoundToFrame()));
				bRangeChanged = true;
			}
			if (Args->HasField(TEXT("end_seconds")))
			{
				const double EndSeconds = Args->GetNumberField(TEXT("end_seconds"));
				Range.SetUpperBound(TRangeBound<FFrameNumber>::Exclusive((EndSeconds * TickResolution).RoundToFrame()));
				bRangeChanged = true;
			}
			if (bRangeChanged)
			{
				if (Range.HasLowerBound() && Range.HasUpperBound() &&
					Range.GetLowerBoundValue() >= Range.GetUpperBoundValue())
				{
					return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
						TEXT("The section's start would land at or after its end."),
						TEXT("Check start_seconds and end_seconds."));
				}
				Section->SetRange(Range);
				bChanged = true;
			}

			// --- params ---
			FMovieSceneSkeletalAnimationParams& Params = Section->Params;

			if (Args->HasField(TEXT("play_rate")))
			{
				const double PlayRate = Args->GetNumberField(TEXT("play_rate"));
				if (FMath::IsNearlyZero(PlayRate))
				{
					return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
						TEXT("play_rate cannot be zero — the clip would never advance."));
				}
				// 5.8: FMovieSceneTimeWarpVariant, not a float.
				Params.PlayRate.Set(PlayRate);
				bChanged = true;
			}

			if (Args->HasField(TEXT("start_frame_offset_seconds")))
			{
				const double OffsetSeconds = Args->GetNumberField(TEXT("start_frame_offset_seconds"));
				if (OffsetSeconds < 0.0)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
						TEXT("start_frame_offset_seconds cannot be negative."));
				}
				if (Params.Animation && OffsetSeconds >= Params.Animation->GetPlayLength())
				{
					return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
						FString::Printf(TEXT("start_frame_offset_seconds %.4f is at or past '%s' length (%.4f) — nothing would play."),
							OffsetSeconds, *Params.Animation->GetName(), Params.Animation->GetPlayLength()));
				}
				Params.StartFrameOffset = (OffsetSeconds * TickResolution).RoundToFrame();
				bChanged = true;
			}

			FString SlotName;
			if (Args->TryGetStringField(TEXT("slot_name"), SlotName))
			{
				Params.SlotName = SlotName.IsEmpty() ? FName(TEXT("DefaultSlot")) : FName(*SlotName);
				bChanged = true;
			}

			FString MirrorPath;
			if (Args->HasField(TEXT("mirror_data_table")))
			{
				Args->TryGetStringField(TEXT("mirror_data_table"), MirrorPath);
				if (MirrorPath.IsEmpty())
				{
					Params.MirrorDataTable = nullptr;
				}
				else
				{
					UMirrorDataTable* MirrorTable = MCPCommon::LoadAssetChecked<UMirrorDataTable>(MirrorPath, Err);
					if (!MirrorTable) return Err;
					Params.MirrorDataTable = MirrorTable;
				}
				bChanged = true;
			}

			if (!bChanged)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("No parameters were supplied."),
					TEXT("Pass at least one of start_seconds, end_seconds, play_rate, start_frame_offset_seconds, slot_name, mirror_data_table."));
			}

			Sequence->MarkPackageDirty();

			TSharedPtr<FJsonObject> Result = SectionToJson(Section, SectionIndex, TickResolution);
			Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
			Result->SetStringField(TEXT("binding_id"), Binding.ToString());

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Updated section %d ('%s') on binding %s of '%s'."),
					SectionIndex,
					Params.Animation ? *Params.Animation->GetName() : TEXT("none"),
					*Binding.ToString(), *Sequence->GetName()),
				Result);
		});

	// =================================================================
	// remove_animation_section
	// =================================================================
	MCP_TOOL(Registry, "remove_animation_section")
		.Description(TEXT(
			"Remove an animation clip from a binding's skeletal animation track. Section indices come from "
			"list_animation_sections and shift after a removal, so re-list between removals rather than "
			"removing several by index in one pass. The track itself is kept even when its last section goes."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the ULevelSequence"), true)
		.IntArg(TEXT("section_index"), TEXT("Index of the section to remove, from list_animation_sections"), true)
		.StringArg(TEXT("actor_name"), TEXT("Label of the bound actor"))
		.StringArg(TEXT("binding_id"), TEXT("Binding GUID, as an alternative to actor_name"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			UMovieScene* MovieScene = nullptr;
			FMCPToolResult Err;
			ULevelSequence* Sequence = ResolveSequence(Args, MovieScene, Err);
			if (!Sequence) return Err;

			const FGuid Binding = ResolveBinding(Args, Sequence, MovieScene, /*bAutoBind*/ false, Err);
			if (!Binding.IsValid()) return Err;

			UMovieSceneSkeletalAnimationTrack* Track = FindAnimTrack(MovieScene, Binding);
			if (!Track)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Binding %s has no skeletal animation track."), *Binding.ToString()));
			}

			double SectionIndexRaw = 0.0;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("section_index"), SectionIndexRaw));
			const int32 SectionIndex = (int32)SectionIndexRaw;

			const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
			if (!Sections.IsValidIndex(SectionIndex))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("section_index %d is out of range; the track has %d section(s)."),
						SectionIndex, Sections.Num()));
			}

			UMovieSceneSection* Section = Sections[SectionIndex];
			FString AnimName = TEXT("(none)");
			if (UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section))
			{
				if (AnimSection->Params.Animation)
				{
					AnimName = AnimSection->Params.Animation->GetName();
				}
			}

			Track->Modify();
			Track->RemoveSection(*Section);
			Sequence->MarkPackageDirty();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
			Result->SetStringField(TEXT("binding_id"), Binding.ToString());
			Result->SetNumberField(TEXT("removed_section_index"), SectionIndex);
			Result->SetStringField(TEXT("removed_animation"), AnimName);
			Result->SetNumberField(TEXT("remaining_section_count"), Track->GetAllSections().Num());

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Removed section %d ('%s') from '%s'; %d section(s) remain."),
					SectionIndex, *AnimName, *Sequence->GetName(), Track->GetAllSections().Num()),
				Result);
		});

	// =================================================================
	// bake_sequence_to_anim_sequence
	// =================================================================
	MCP_TOOL(Registry, "bake_sequence_to_anim_sequence")
		.Description(TEXT(
			"Bake one binding's animation in a level sequence down into a reusable UAnimSequence asset. "
			"This is how cinematic or Control Rig work becomes a gameplay-usable clip: everything driving "
			"that skeletal mesh in the sequence — animation sections, Control Rig, transform tracks — is "
			"evaluated per frame and written into a new animation on the actor's skeleton.\n"
			"Baking evaluates the whole sequence and can take a while on long shots. With create_link=true "
			"the two assets stay associated so the bake can be re-run after the sequence changes."))
		.LongRunning()
		.SupportsDryRun()
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the ULevelSequence to bake"), true)
		.StringArg(TEXT("output_path"), TEXT("Content path for the new UAnimSequence (e.g. '/Game/Anims/AS_BakedIntro')"), true)
		.StringArg(TEXT("actor_name"), TEXT("Label of the bound actor to bake"))
		.StringArg(TEXT("binding_id"), TEXT("Binding GUID, as an alternative to actor_name"))
		.BoolArg(TEXT("export_morph_targets"), TEXT("Bake morph target curves (default: true)"))
		.BoolArg(TEXT("export_material_curves"), TEXT("Bake material parameter curves (default: true)"))
		.BoolArg(TEXT("record_in_world_space"), TEXT("Bake in world space rather than component space (default: false)"))
		.BoolArg(TEXT("create_link"), TEXT("Link the level sequence and the baked animation so the bake can be repeated (default: true)"))
		.Example(TEXT("{\"sequence_path\": \"/Game/Cinematics/LS_Intro\", \"actor_name\": \"Hero\", \"output_path\": \"/Game/Anims/AS_BakedIntro\"}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			UMovieScene* MovieScene = nullptr;
			FMCPToolResult Err;
			ULevelSequence* Sequence = ResolveSequence(Args, MovieScene, Err);
			if (!Sequence) return Err;

			const FGuid Binding = ResolveBinding(Args, Sequence, MovieScene, /*bAutoBind*/ false, Err);
			if (!Binding.IsValid()) return Err;

			USkeleton* Skeleton = GetBindingSkeleton(Sequence, Binding);
			if (!Skeleton)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Could not resolve a skeleton for binding %s."), *Binding.ToString()),
					TEXT("The binding must resolve to an actor with a SkeletalMeshComponent whose mesh has a skeleton. The level containing that actor must be open."));
			}

			FString OutputPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("output_path"), OutputPath));

			FString PackagePath, AssetName;
			if (!MCPAnimTools::Common::SplitAssetPath(OutputPath, PackagePath, AssetName))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::InvalidPath,
					FString::Printf(TEXT("Invalid output_path: %s"), *OutputPath));
			}
			BAIL_IF_INVALID(FMCPValidate::PackagePath(PackagePath));
			BAIL_IF_INVALID(FMCPValidate::AssetName(AssetName));
			BAIL_IF_INVALID(FMCPValidate::AssetDoesNotExist(PackagePath));

			UWorld* World = GetEditorWorld();
			if (!World)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("No editor world available."));
			}

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Failed to create the package for the baked animation."));
			}

			UAnimSequence* AnimSequence = NewObject<UAnimSequence>(
				Package, FName(*AssetName), RF_Public | RF_Standalone);
			if (!AnimSequence)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Failed to create the UAnimSequence object."));
			}
			AnimSequence->SetSkeleton(Skeleton);

			// Journal the creation so run_tool_script can report truthfully that this
			// asset survives a rollback (UE package creation is not transactional).
			MCPCommon::NoteAssetCreated(PackagePath);

			UAnimSeqExportOption* Options = NewObject<UAnimSeqExportOption>();
			Options->bExportTransforms = true;

			// Each of these keeps the engine default unless the caller overrode it.
			bool bValue = false;
			if (Args->TryGetBoolField(TEXT("export_morph_targets"), bValue))   { Options->bExportMorphTargets = bValue; }
			if (Args->TryGetBoolField(TEXT("export_material_curves"), bValue)) { Options->bExportMaterialCurves = bValue; }
			if (Args->TryGetBoolField(TEXT("record_in_world_space"), bValue))  { Options->bRecordInWorldSpace = bValue; }

			bool bCreateLink = true;
			Args->TryGetBoolField(TEXT("create_link"), bCreateLink);

			const FMovieSceneBindingProxy BindingProxy(Binding, Sequence);

			const bool bExported = USequencerToolsFunctionLibrary::ExportAnimSequence(
				World, Sequence, AnimSequence, Options, BindingProxy, bCreateLink);

			if (!bExported)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Baking '%s' to an animation failed."), *Sequence->GetName()),
					TEXT("The binding must resolve to a live skeletal-mesh actor in the open level, and the sequence must have a non-empty playback range. See the Output Log for the exporter's diagnostics."));
			}

			FMCPToolResult SaveError;
			if (!MCPAnimTools::Common::SaveExistingAsset(AnimSequence, SaveError))
			{
				return SaveError;
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
			Result->SetStringField(TEXT("binding_id"), Binding.ToString());
			Result->SetStringField(TEXT("anim_sequence"), AnimSequence->GetPathName());
			Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
			Result->SetNumberField(TEXT("duration"), AnimSequence->GetPlayLength());
			Result->SetNumberField(TEXT("frame_count"), AnimSequence->GetNumberOfSampledKeys());
			Result->SetBoolField(TEXT("linked"), bCreateLink);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Baked binding %s of '%s' into '%s' (%.3fs, %d keys) on skeleton '%s'.%s"),
					*Binding.ToString(), *Sequence->GetName(), *OutputPath,
					AnimSequence->GetPlayLength(), AnimSequence->GetNumberOfSampledKeys(), *Skeleton->GetName(),
					bCreateLink ? TEXT(" The two assets are linked, so the bake can be repeated.") : TEXT("")),
				Result);
		});

	// =================================================================
	// link_anim_sequence_to_sequence
	// =================================================================
	MCP_TOOL(Registry, "link_anim_sequence_to_sequence")
		.Description(TEXT(
			"Link an existing UAnimSequence to a level sequence binding without baking now. The link records "
			"which binding and export settings produced the animation, so the bake can be re-run later "
			"(bake_sequence_to_anim_sequence) after the sequence is edited — the round trip that keeps a "
			"hand-animated Sequencer shot and its gameplay clip in step. The animation must target the bound "
			"actor's skeleton."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the ULevelSequence"), true)
		.StringArg(TEXT("anim_sequence_path"), TEXT("Content path of the UAnimSequence to link"), true)
		.StringArg(TEXT("actor_name"), TEXT("Label of the bound actor"))
		.StringArg(TEXT("binding_id"), TEXT("Binding GUID, as an alternative to actor_name"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			UMovieScene* MovieScene = nullptr;
			FMCPToolResult Err;
			ULevelSequence* Sequence = ResolveSequence(Args, MovieScene, Err);
			if (!Sequence) return Err;

			FString AnimPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("anim_sequence_path"), AnimPath));
			UAnimSequence* AnimSequence = MCPCommon::LoadAssetChecked<UAnimSequence>(AnimPath, Err);
			if (!AnimSequence) return Err;

			const FGuid Binding = ResolveBinding(Args, Sequence, MovieScene, /*bAutoBind*/ false, Err);
			if (!Binding.IsValid()) return Err;

			if (USkeleton* BoundSkeleton = GetBindingSkeleton(Sequence, Binding))
			{
				if (!MCPAnimTools::Common::RequireSameSkeleton(BoundSkeleton, AnimSequence, TEXT("anim_sequence_path"), Err))
				{
					return Err;
				}
			}

			UAnimSeqExportOption* Options = NewObject<UAnimSeqExportOption>();
			const FMovieSceneBindingProxy BindingProxy(Binding, Sequence);

			const bool bLinked = USequencerToolsFunctionLibrary::LinkAnimSequence(
				Sequence, AnimSequence, Options, BindingProxy);

			if (!bLinked)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Failed to link '%s' to binding %s of '%s'."),
						*AnimSequence->GetName(), *Binding.ToString(), *Sequence->GetName()),
					TEXT("The binding must resolve to a skeletal-mesh actor in the open level. See the Output Log."));
			}

			Sequence->MarkPackageDirty();
			AnimSequence->MarkPackageDirty();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
			Result->SetStringField(TEXT("binding_id"), Binding.ToString());
			Result->SetStringField(TEXT("anim_sequence"), AnimSequence->GetPathName());

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Linked '%s' to binding %s of '%s'. Re-bake with bake_sequence_to_anim_sequence after editing the sequence."),
					*AnimSequence->GetName(), *Binding.ToString(), *Sequence->GetName()),
				Result);
		});
}

} // namespace MCPSequencerTools::AnimationTracks
