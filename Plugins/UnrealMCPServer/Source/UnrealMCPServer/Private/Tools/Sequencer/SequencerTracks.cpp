// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// LevelSequence & MovieScene
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneSequence.h"
#include "MovieSceneTimeHelpers.h"

// Tracks
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneVisibilityTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Tracks/MovieSceneFadeTrack.h"

// Sections
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneAudioSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneSubSection.h"
#include "Sections/MovieSceneFadeSection.h"

// Sound
#include "Sound/SoundBase.h"

// Channels
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneBoolChannel.h"

// Asset creation & management
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/Factory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

// Editor subsystems
#include "Subsystems/AssetEditorSubsystem.h"
#include "EditorSubsystem.h"

#include "Tools/Sequencer/SequencerCommon.h"

namespace MCPSequencerTools::Tracks
{

using namespace MCPSequencerTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// add_sequence_track - Add a track to an existing binding
	// ================================================================
	MCP_TOOL(Registry, "add_sequence_track")
		.Description(TEXT(
			"Add a track to an actor binding in a LevelSequence. "
			"Transform adds a 3D transform track; Visibility adds a bool visibility track. "
			"The actor must already be bound via add_actor_to_sequence."))
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset"), true)
		.StringArg(TEXT("actor_name"), TEXT("Label of the already-bound actor to add the track to"), true)
		.EnumArg(TEXT("track_type"), TEXT("Type of track to add"),
			{ TEXT("Transform"), TEXT("Visibility") }, true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SequencePath, ActorName, TrackTypeStr;
			if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath))
				return FMCPToolResult::Error(TEXT("sequence_path is required"));
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));
			if (!Args->TryGetStringField(TEXT("track_type"), TrackTypeStr))
				return FMCPToolResult::Error(TEXT("track_type is required"));

			ULevelSequence* Sequence = LoadSequence(SequencePath);
			if (!IsValid(Sequence))
				return FMCPToolResult::Error(FString::Printf(TEXT("LevelSequence not found: %s"), *SequencePath));

			UMovieScene* MovieScene = Sequence->GetMovieScene();
			if (!MovieScene)
				return FMCPToolResult::Error(TEXT("Sequence has no MovieScene"));

			const FGuid BindingGuid = FindPossessableGuid(MovieScene, ActorName);
			if (!BindingGuid.IsValid())
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Actor '%s' is not bound in this sequence. Use add_actor_to_sequence first."),
					*ActorName));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Sequence Track")));
			MovieScene->Modify();

			FString AddedTrackName;

			if (TrackTypeStr == TEXT("Transform"))
			{
				// Guard against double-add
				if (MovieScene->FindTrack<UMovieScene3DTransformTrack>(BindingGuid))
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(FString::Printf(
						TEXT("Transform track already exists for '%s'"), *ActorName));
				}

				UMovieScene3DTransformTrack* Track = MovieScene->AddTrack<UMovieScene3DTransformTrack>(BindingGuid);
				if (!Track)
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(TEXT("Failed to add Transform track"));
				}

				Track->SetPropertyNameAndPath(
					FName(TEXT("Transform")),
					TEXT("Transform"));

				// Create the default section spanning the playback range
				UMovieSceneSection* Section = Track->CreateNewSection();
				if (Section)
				{
					Section->SetRange(MovieScene->GetPlaybackRange());
					Track->AddSection(*Section);
				}

				AddedTrackName = TEXT("Transform");
			}
			else if (TrackTypeStr == TEXT("Visibility"))
			{
				if (MovieScene->FindTrack<UMovieSceneVisibilityTrack>(BindingGuid))
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(FString::Printf(
						TEXT("Visibility track already exists for '%s'"), *ActorName));
				}

				UMovieSceneVisibilityTrack* Track = MovieScene->AddTrack<UMovieSceneVisibilityTrack>(BindingGuid);
				if (!Track)
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(TEXT("Failed to add Visibility track"));
				}

				// The property that drives actor visibility in Sequencer
				Track->SetPropertyNameAndPath(
					FName(TEXT("bHidden")),
					TEXT("bHidden"));

				UMovieSceneSection* Section = Track->CreateNewSection();
				if (Section)
				{
					Section->SetRange(MovieScene->GetPlaybackRange());
					Track->AddSection(*Section);
				}

				AddedTrackName = TEXT("Visibility");
			}
			else
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Unknown track_type '%s'. Valid values: Transform, Visibility"), *TrackTypeStr));
			}

			Sequence->MarkPackageDirty();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added %s track to '%s' in sequence '%s'"),
				*AddedTrackName, *ActorName, *Sequence->GetName()));
		});

	// ================================================================
	// add_keyframe - Add a keyframe on a track at a given time
	// ================================================================
	MCP_TOOL(Registry, "add_keyframe")
		.Description(TEXT(
			"Add a keyframe at a specified time on a track belonging to a bound actor. "
			"For Transform tracks, the value is parsed as 'X Y Z' or 'X Y Z Pitch Yaw Roll' in world space. "
			"For Visibility tracks, the value is 'true' (visible) or 'false' (hidden)."))
		.Idempotent()
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset"), true)
		.StringArg(TEXT("actor_name"), TEXT("Label of the bound actor"), true)
		.EnumArg(TEXT("track_type"), TEXT("Track type to key"),
			{ TEXT("Transform"), TEXT("Visibility") }, true)
		.NumberArg(TEXT("time_seconds"), TEXT("Time in seconds at which to place the keyframe"), true)
		.StringArg(TEXT("value"), TEXT(
			"Value to key. "
			"Transform: space-separated floats: 'X Y Z' (location only) or 'X Y Z Pitch Yaw Roll' (location + rotation). "
			"Visibility: 'true' or 'false' (visible = true means actor is shown)."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SequencePath, ActorName, TrackTypeStr, ValueStr;
			double TimeSeconds = 0.0;

			if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath))
				return FMCPToolResult::Error(TEXT("sequence_path is required"));
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));
			if (!Args->TryGetStringField(TEXT("track_type"), TrackTypeStr))
				return FMCPToolResult::Error(TEXT("track_type is required"));
			if (!Args->HasField(TEXT("time_seconds")))
				return FMCPToolResult::Error(TEXT("time_seconds is required"));
			TimeSeconds = Args->GetNumberField(TEXT("time_seconds"));
			if (!Args->TryGetStringField(TEXT("value"), ValueStr))
				return FMCPToolResult::Error(TEXT("value is required"));

			ULevelSequence* Sequence = LoadSequence(SequencePath);
			if (!IsValid(Sequence))
				return FMCPToolResult::Error(FString::Printf(TEXT("LevelSequence not found: %s"), *SequencePath));

			UMovieScene* MovieScene = Sequence->GetMovieScene();
			if (!MovieScene)
				return FMCPToolResult::Error(TEXT("Sequence has no MovieScene"));

			const FGuid BindingGuid = FindPossessableGuid(MovieScene, ActorName);
			if (!BindingGuid.IsValid())
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Actor '%s' not bound in sequence. Use add_actor_to_sequence first."),
					*ActorName));
			}

			// Convert time in seconds to a frame number in tick resolution
			const FFrameRate TickRes    = MovieScene->GetTickResolution();
			const FFrameRate DisplayRes = MovieScene->GetDisplayRate();

			// Convert seconds -> display frame -> tick resolution frame number
			const FFrameTime DisplayFrameTime = DisplayRes.AsFrameTime(TimeSeconds);
			const FFrameNumber TickFrame = ConvertFrameTime(DisplayFrameTime, DisplayRes, TickRes).RoundToFrame();

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Keyframe")));
			MovieScene->Modify();

			FString ResultMsg;

			if (TrackTypeStr == TEXT("Transform"))
			{
				UMovieScene3DTransformTrack* Track = MovieScene->FindTrack<UMovieScene3DTransformTrack>(BindingGuid);
				if (!Track)
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(FString::Printf(
						TEXT("No Transform track found for '%s'. Use add_sequence_track first."), *ActorName));
				}

				// Parse value: "X Y Z" or "X Y Z Pitch Yaw Roll"
				TArray<FString> Parts;
				ValueStr.ParseIntoArrayWS(Parts);

				FVector Location = FVector::ZeroVector;
				FRotator Rotation = FRotator::ZeroRotator;

				if (Parts.Num() >= 3)
				{
					Location.X = FCString::Atof(*Parts[0]);
					Location.Y = FCString::Atof(*Parts[1]);
					Location.Z = FCString::Atof(*Parts[2]);
				}
				if (Parts.Num() >= 6)
				{
					Rotation.Pitch = FCString::Atof(*Parts[3]);
					Rotation.Yaw   = FCString::Atof(*Parts[4]);
					Rotation.Roll  = FCString::Atof(*Parts[5]);
				}

				// Get or create a section that covers this frame
				UMovieScene3DTransformSection* Section = nullptr;
				if (Track->GetAllSections().Num() > 0)
				{
					Section = Cast<UMovieScene3DTransformSection>(Track->GetAllSections()[0]);
				}
				if (!Section)
				{
					Section = Cast<UMovieScene3DTransformSection>(Track->CreateNewSection());
					if (Section)
					{
						Section->SetRange(TRange<FFrameNumber>::All());
						Track->AddSection(*Section);
					}
				}

				if (!Section)
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(TEXT("Failed to get or create transform section"));
				}

				Section->Modify();

				// Extend section range to include this frame if needed
				TRange<FFrameNumber> SectionRange = Section->GetRange();
				if (SectionRange.GetLowerBound().IsClosed() && TickFrame < SectionRange.GetLowerBoundValue())
				{
					Section->SetRange(TRange<FFrameNumber>(TickFrame, SectionRange.GetUpperBound()));
				}
				if (SectionRange.GetUpperBound().IsClosed() && TickFrame >= SectionRange.GetUpperBoundValue())
				{
					Section->SetRange(TRange<FFrameNumber>(Section->GetRange().GetLowerBound(),
						TRangeBound<FFrameNumber>::Exclusive(TickFrame + 1)));
				}

				// Get the double channels from the transform section proxy
				// Channel layout: Translation X(0), Y(1), Z(2), Rotation X(3), Y(4), Z(5), Scale X(6), Y(7), Z(8)
				FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
				TArrayView<FMovieSceneDoubleChannel*> Channels = ChannelProxy.GetChannels<FMovieSceneDoubleChannel>();

				// Translation channels: indices 0, 1, 2
				if (Channels.Num() >= 3)
				{
					Channels[0]->AddLinearKey(TickFrame, Location.X);
					Channels[1]->AddLinearKey(TickFrame, Location.Y);
					Channels[2]->AddLinearKey(TickFrame, Location.Z);
				}

				// Rotation channels: indices 3, 4, 5 (only if 6 values were provided)
				if (Parts.Num() >= 6 && Channels.Num() >= 6)
				{
					Channels[3]->AddLinearKey(TickFrame, Rotation.Pitch);
					Channels[4]->AddLinearKey(TickFrame, Rotation.Yaw);
					Channels[5]->AddLinearKey(TickFrame, Rotation.Roll);
				}

				ResultMsg = FString::Printf(
					TEXT("Added Transform keyframe at %.3fs (frame %d tick) for '%s': Loc(%.1f, %.1f, %.1f)%s"),
					TimeSeconds, TickFrame.Value, *ActorName,
					Location.X, Location.Y, Location.Z,
					Parts.Num() >= 6
						? *FString::Printf(TEXT(" Rot(%.1f, %.1f, %.1f)"), Rotation.Pitch, Rotation.Yaw, Rotation.Roll)
						: TEXT(""));
			}
			else if (TrackTypeStr == TEXT("Visibility"))
			{
				UMovieSceneVisibilityTrack* Track = MovieScene->FindTrack<UMovieSceneVisibilityTrack>(BindingGuid);
				if (!Track)
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(FString::Printf(
						TEXT("No Visibility track found for '%s'. Use add_sequence_track first."), *ActorName));
				}

				// Parse bool value - "true"/"false" maps to visible/hidden
				const bool bVisible = (ValueStr.TrimStartAndEnd().ToLower() == TEXT("true"));

				// Get or create a section
				UMovieSceneBoolSection* Section = nullptr;
				if (Track->GetAllSections().Num() > 0)
				{
					Section = Cast<UMovieSceneBoolSection>(Track->GetAllSections()[0]);
				}
				if (!Section)
				{
					Section = Cast<UMovieSceneBoolSection>(Track->CreateNewSection());
					if (Section)
					{
						Section->SetRange(TRange<FFrameNumber>::All());
						Track->AddSection(*Section);
					}
				}

				if (!Section)
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(TEXT("Failed to get or create visibility section"));
				}

				Section->Modify();

				// Extend range to include frame if needed
				TRange<FFrameNumber> SectionRange = Section->GetRange();
				if (SectionRange.GetLowerBound().IsClosed() && TickFrame < SectionRange.GetLowerBoundValue())
				{
					Section->SetRange(TRange<FFrameNumber>(TickFrame, SectionRange.GetUpperBound()));
				}
				if (SectionRange.GetUpperBound().IsClosed() && TickFrame >= SectionRange.GetUpperBoundValue())
				{
					Section->SetRange(TRange<FFrameNumber>(Section->GetRange().GetLowerBound(),
						TRangeBound<FFrameNumber>::Exclusive(TickFrame + 1)));
				}

				// The Visibility track drives bHidden; bHidden=true means actor is hidden,
				// so to show the actor we write bHidden=false (i.e. !bVisible).
				// Sequencer visibility track stores the raw bHidden value; the section marks
				// itself as externally inverted so that the UI shows "Visible" for true.
				// We store the raw value the user intends (visible = true on channel).
				Section->GetChannel().GetData().AddKey(TickFrame, bVisible);

				ResultMsg = FString::Printf(
					TEXT("Added Visibility keyframe at %.3fs for '%s': %s"),
					TimeSeconds, *ActorName, bVisible ? TEXT("visible") : TEXT("hidden"));
			}
			else
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Unknown track_type '%s'. Valid values: Transform, Visibility"), *TrackTypeStr));
			}

			Sequence->MarkPackageDirty();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(ResultMsg);
		});

}

} // namespace MCPSequencerTools::Tracks
