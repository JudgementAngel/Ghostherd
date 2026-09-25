// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.5 (UE 5.8) — Animation Mixer tool family.
//
// New in 5.8: the Animation Mixer mixes/layers skeletal animation inputs directly
// in Sequencer (no separate Anim Blueprint / slots). The engine ships a dedicated
// scripting surface, MovieSceneAnimMixerScripting, with static extension libraries
// operating on a UMovieSceneAnimationMixerTrack bound to an object in a level
// sequence. These tools wrap: add the mixer track, add/inspect layers, and add an
// animation clip to a layer.

#include "Tools/MCPAnimMixerTools.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneAnimationMixerTrack.h"
#include "MovieSceneAnimationMixerLayer.h"
#include "MovieSceneAnimMixerTrackExtensions.h"
#include "MovieSceneAnimMixerLayerExtensions.h"
#include "Animation/AnimSequenceBase.h"

namespace MCPAnimMixerTools
{

/** Load a level sequence by path (with .obj suffix tolerance). */
static ULevelSequence* LoadSequence(const FString& Path)
{
	ULevelSequence* Seq = LoadObject<ULevelSequence>(nullptr, *Path);
	if (!Seq)
	{
		FString Stripped = Path; int32 D;
		if (Stripped.FindLastChar(TEXT('.'), D)) Stripped = Stripped.Left(D);
		Seq = LoadObject<ULevelSequence>(nullptr, *Stripped);
	}
	return Seq;
}

/** Resolve sequence + parse binding guid + (optionally) find the mixer track. */
static bool Resolve(const TSharedPtr<FJsonObject>& Args, ULevelSequence*& OutSeq, UMovieScene*& OutMS,
	FGuid& OutBinding, FMCPToolResult& OutErr)
{
	FString SeqPath, BindingStr;
	if (!Args->TryGetStringField(TEXT("sequence_path"), SeqPath) || SeqPath.IsEmpty())
	{
		OutErr = FMCPToolResult::ErrorStructured(EMCPError::OutOfRange, TEXT("'sequence_path' is required."));
		return false;
	}
	if (!Args->TryGetStringField(TEXT("binding_id"), BindingStr) || !FGuid::Parse(BindingStr, OutBinding))
	{
		OutErr = FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
			TEXT("'binding_id' (a GUID from the sequencer binding) is required."),
			TEXT("Use the binding GUID returned when you possessed the actor in the level sequence."));
		return false;
	}
	OutSeq = LoadSequence(SeqPath);
	if (!OutSeq)
	{
		OutErr = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
			FString::Printf(TEXT("Level sequence not found: %s"), *SeqPath));
		return false;
	}
	OutMS = OutSeq->GetMovieScene();
	if (!OutMS)
	{
		OutErr = FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("Level sequence has no MovieScene."));
		return false;
	}
	return true;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// animmixer_add_track
	// ================================================================
	MCP_TOOL(Registry, "animmixer_add_track")
		.Description(TEXT(
			"Add an Animation Mixer track to an object binding in a level sequence (the binding should be a "
			"skeletal-mesh actor). Idempotent: returns the existing track if one is already present. "
			"Marks the sequence dirty."))
		.StringArg(TEXT("sequence_path"), TEXT("Level sequence asset path."), true)
		.StringArg(TEXT("binding_id"), TEXT("Object binding GUID to attach the mixer track to."), true)
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			ULevelSequence* Seq; UMovieScene* MS; FGuid Binding; FMCPToolResult Err;
			if (!Resolve(Args, Seq, MS, Binding, Err)) return Err;

			UMovieSceneAnimationMixerTrack* Track = MS->FindTrack<UMovieSceneAnimationMixerTrack>(Binding);
			bool bCreated = false;
			if (!Track)
			{
				MS->Modify();
				Track = MS->AddTrack<UMovieSceneAnimationMixerTrack>(Binding);
				bCreated = true;
			}
			if (!Track) return FMCPToolResult::ErrorStructured(EMCPError::Internal,
				TEXT("Failed to add an Animation Mixer track to that binding."),
				TEXT("Verify the binding GUID exists in this sequence."));

			Seq->MarkPackageDirty();

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("sequence"), Seq->GetPathName());
			R->SetStringField(TEXT("binding_id"), Binding.ToString());
			R->SetBoolField(TEXT("created"), bCreated);
			R->SetNumberField(TEXT("layer_count"), UMovieSceneAnimMixerTrackExtensions::GetLayerCount(Track));
			return FMCPToolResult::SuccessStructured(
				bCreated ? TEXT("Added Animation Mixer track.") : TEXT("Animation Mixer track already present."), R);
		});

	// ================================================================
	// animmixer_add_layer
	// ================================================================
	MCP_TOOL(Registry, "animmixer_add_layer")
		.Description(TEXT(
			"Add a new layer to the Animation Mixer track on a binding (layers blend/mask over each other). "
			"Returns the new layer index. Requires animmixer_add_track first."))
		.StringArg(TEXT("sequence_path"), TEXT("Level sequence asset path."), true)
		.StringArg(TEXT("binding_id"), TEXT("Object binding GUID with a mixer track."), true)
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			ULevelSequence* Seq; UMovieScene* MS; FGuid Binding; FMCPToolResult Err;
			if (!Resolve(Args, Seq, MS, Binding, Err)) return Err;

			UMovieSceneAnimationMixerTrack* Track = MS->FindTrack<UMovieSceneAnimationMixerTrack>(Binding);
			if (!Track) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				TEXT("No Animation Mixer track on that binding."),
				TEXT("Call animmixer_add_track first."));

			MS->Modify();
			UMovieSceneAnimationMixerLayer* Layer = UMovieSceneAnimMixerTrackExtensions::AddLayer(Track);
			if (!Layer) return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("AddLayer failed."));
			Seq->MarkPackageDirty();

			const int32 LayerIndex = UMovieSceneAnimMixerLayerExtensions::GetLayerIndex(Layer);
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("sequence"), Seq->GetPathName());
			R->SetStringField(TEXT("binding_id"), Binding.ToString());
			R->SetNumberField(TEXT("layer_index"), LayerIndex);
			R->SetNumberField(TEXT("layer_count"), UMovieSceneAnimMixerTrackExtensions::GetLayerCount(Track));
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Added mixer layer (index %d)."), LayerIndex), R);
		});

	// ================================================================
	// animmixer_add_animation
	// ================================================================
	MCP_TOOL(Registry, "animmixer_add_animation")
		.Description(TEXT(
			"Add an animation sequence to a layer of the Animation Mixer track, starting at start_seconds. "
			"Use animmixer_add_layer to create layers first."))
		.StringArg(TEXT("sequence_path"), TEXT("Level sequence asset path."), true)
		.StringArg(TEXT("binding_id"), TEXT("Object binding GUID with a mixer track."), true)
		.IntArg(TEXT("layer_index"), TEXT("Target layer index (from animmixer_add_layer)."), true)
		.StringArg(TEXT("anim_sequence_path"), TEXT("UAnimSequenceBase asset path."), true)
		.NumberArg(TEXT("start_seconds"), TEXT("Start time in seconds (default 0)."))
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			ULevelSequence* Seq; UMovieScene* MS; FGuid Binding; FMCPToolResult Err;
			if (!Resolve(Args, Seq, MS, Binding, Err)) return Err;

			int32 LayerIndex = 0;
			BAIL_IF_INVALID(FMCPValidate::InRangeI(
				LayerIndex = (int32)Args->GetNumberField(TEXT("layer_index")), 0, 4096, TEXT("layer_index")));

			FString AnimPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("anim_sequence_path"), AnimPath));
			UAnimSequenceBase* Anim = LoadObject<UAnimSequenceBase>(nullptr, *AnimPath);
			if (!Anim)
			{
				FString Stripped = AnimPath; int32 D;
				if (Stripped.FindLastChar(TEXT('.'), D)) Stripped = Stripped.Left(D);
				Anim = LoadObject<UAnimSequenceBase>(nullptr, *Stripped);
			}
			if (!Anim) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Animation sequence not found: %s"), *AnimPath));

			UMovieSceneAnimationMixerTrack* Track = MS->FindTrack<UMovieSceneAnimationMixerTrack>(Binding);
			if (!Track) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				TEXT("No Animation Mixer track on that binding."), TEXT("Call animmixer_add_track first."));
			if (LayerIndex >= UMovieSceneAnimMixerTrackExtensions::GetLayerCount(Track))
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("layer_index %d out of range (track has %d layers)."),
						LayerIndex, UMovieSceneAnimMixerTrackExtensions::GetLayerCount(Track)));

			const double StartSeconds = Args->HasField(TEXT("start_seconds"))
				? Args->GetNumberField(TEXT("start_seconds")) : 0.0;
			const FFrameNumber StartFrame = (MS->GetTickResolution().AsFrameTime(StartSeconds)).FloorToFrame();

			MS->Modify();
			UMovieSceneSection* Section =
				UMovieSceneAnimMixerTrackExtensions::AddAnimation(Track, LayerIndex, StartFrame, Anim);
			if (!Section) return FMCPToolResult::ErrorStructured(EMCPError::Internal,
				TEXT("AddAnimation returned no section."));
			Seq->MarkPackageDirty();

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("sequence"), Seq->GetPathName());
			R->SetStringField(TEXT("binding_id"), Binding.ToString());
			R->SetNumberField(TEXT("layer_index"), LayerIndex);
			R->SetStringField(TEXT("animation"), Anim->GetPathName());
			R->SetNumberField(TEXT("start_seconds"), StartSeconds);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Added '%s' to mixer layer %d at %.2fs."),
					*Anim->GetName(), LayerIndex, StartSeconds), R);
		});

	// ================================================================
	// animmixer_get_info
	// ================================================================
	MCP_TOOL(Registry, "animmixer_get_info")
		.Description(TEXT(
			"Report the Animation Mixer track on a binding: layer count and the number of sections per layer."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("sequence_path"), TEXT("Level sequence asset path."), true)
		.StringArg(TEXT("binding_id"), TEXT("Object binding GUID."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			ULevelSequence* Seq; UMovieScene* MS; FGuid Binding; FMCPToolResult Err;
			if (!Resolve(Args, Seq, MS, Binding, Err)) return Err;

			UMovieSceneAnimationMixerTrack* Track = MS->FindTrack<UMovieSceneAnimationMixerTrack>(Binding);
			if (!Track) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				TEXT("No Animation Mixer track on that binding."));

			TArray<TSharedPtr<FJsonValue>> Layers;
			for (UMovieSceneAnimationMixerLayer* Layer : UMovieSceneAnimMixerTrackExtensions::GetLayers(Track))
			{
				if (!Layer) continue;
				TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
				L->SetNumberField(TEXT("index"), UMovieSceneAnimMixerLayerExtensions::GetLayerIndex(Layer));
				L->SetNumberField(TEXT("sections"), UMovieSceneAnimMixerLayerExtensions::GetSections(Layer).Num());
				L->SetBoolField(TEXT("empty"), UMovieSceneAnimMixerLayerExtensions::IsEmpty(Layer));
				Layers.Add(MakeShared<FJsonValueObject>(L));
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("sequence"), Seq->GetPathName());
			R->SetStringField(TEXT("binding_id"), Binding.ToString());
			R->SetNumberField(TEXT("layer_count"), Layers.Num());
			R->SetArrayField(TEXT("layers"), Layers);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Animation Mixer track has %d layer(s)."), Layers.Num()), R);
		});
}

} // namespace MCPAnimMixerTools
