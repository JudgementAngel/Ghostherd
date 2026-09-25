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

namespace MCPSequencerTools::SpecialTracks
{

using namespace MCPSequencerTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// add_audio_track - Add an audio track with a sound to a bound actor
	// ================================================================
	MCP_TOOL(Registry, "add_audio_track")
		.Description(TEXT(
			"Add an audio track with a sound asset to an actor binding in a LevelSequence. "
			"The actor is automatically bound if not already present. The sound is placed at the start of the playback range."))
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset"), true)
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor to bind (will be auto-bound if not already)"), true)
		.StringArg(TEXT("sound_path"), TEXT("Content path to a USoundBase asset (e.g., '/Game/Audio/MySound')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SequencePath, ActorName, SoundPath;
			if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath))
				return FMCPToolResult::Error(TEXT("sequence_path is required"));
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));
			if (!Args->TryGetStringField(TEXT("sound_path"), SoundPath))
				return FMCPToolResult::Error(TEXT("sound_path is required"));

			ULevelSequence* Sequence = LoadSequence(SequencePath);
			if (!IsValid(Sequence))
				return FMCPToolResult::Error(FString::Printf(TEXT("LevelSequence not found: %s"), *SequencePath));

			UMovieScene* MovieScene = Sequence->GetMovieScene();
			if (!MovieScene)
				return FMCPToolResult::Error(TEXT("Sequence has no MovieScene"));

			UWorld* World = GetEditorWorld();
			if (!World)
				return FMCPToolResult::Error(TEXT("No editor world available"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found in current level: %s"), *ActorName));

			// Load the sound asset
			USoundBase* Sound = LoadObject<USoundBase>(nullptr, *SoundPath);
			if (!IsValid(Sound))
				return FMCPToolResult::Error(FString::Printf(TEXT("USoundBase not found: %s"), *SoundPath));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Audio Track")));
			MovieScene->Modify();

			// Auto-bind the actor if not already bound
			FGuid BindingGuid = FindPossessableGuid(MovieScene, ActorName);
			if (!BindingGuid.IsValid())
			{
				BindingGuid = MovieScene->AddPossessable(ActorName, Actor->GetClass());
				if (!BindingGuid.IsValid())
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(TEXT("Failed to add possessable to MovieScene"));
				}
				Sequence->BindPossessableObject(BindingGuid, *Actor, World);
			}

			// Add the audio track to this binding
			UMovieSceneAudioTrack* AudioTrack = MovieScene->AddTrack<UMovieSceneAudioTrack>(BindingGuid);
			if (!AudioTrack)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to add audio track"));
			}

			// Add the sound at the start of the playback range
			const TRange<FFrameNumber> PlayRange = MovieScene->GetPlaybackRange();
			FFrameNumber StartFrame = PlayRange.HasLowerBound() ? PlayRange.GetLowerBoundValue() : FFrameNumber(0);

			UMovieSceneSection* NewSection = AudioTrack->AddNewSound(Sound, StartFrame);
			if (!NewSection)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to add audio section with sound"));
			}

			Sequence->MarkPackageDirty();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added audio track with sound '%s' to actor '%s' in sequence '%s'"),
				*Sound->GetName(), *ActorName, *Sequence->GetName()));
		});

	// ================================================================
	// add_camera_cut_track - Add a camera cut track pointing to a camera actor
	// ================================================================
	MCP_TOOL(Registry, "add_camera_cut_track")
		.Description(TEXT(
			"Add a camera cut track to a LevelSequence pointing to a bound camera actor. "
			"Creates a CameraCut section spanning the specified time range. "
			"The camera actor must already be bound via add_actor_to_sequence."))
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset"), true)
		.StringArg(TEXT("camera_actor_name"), TEXT("Label of the camera actor in the current level (must already be bound in the sequence)"), true)
		.NumberArg(TEXT("start_seconds"), TEXT("Start time of the camera cut in seconds (default: 0)"))
		.NumberArg(TEXT("end_seconds"), TEXT("End time of the camera cut in seconds (default: end of playback range)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SequencePath, CameraActorName;
			if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath))
				return FMCPToolResult::Error(TEXT("sequence_path is required"));
			if (!Args->TryGetStringField(TEXT("camera_actor_name"), CameraActorName))
				return FMCPToolResult::Error(TEXT("camera_actor_name is required"));

			ULevelSequence* Sequence = LoadSequence(SequencePath);
			if (!IsValid(Sequence))
				return FMCPToolResult::Error(FString::Printf(TEXT("LevelSequence not found: %s"), *SequencePath));

			UMovieScene* MovieScene = Sequence->GetMovieScene();
			if (!MovieScene)
				return FMCPToolResult::Error(TEXT("Sequence has no MovieScene"));

			// Find the camera actor's binding GUID
			const FGuid CameraGuid = FindPossessableGuid(MovieScene, CameraActorName);
			if (!CameraGuid.IsValid())
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Camera actor '%s' is not bound in this sequence. Use add_actor_to_sequence first."),
					*CameraActorName));
			}

			// Compute time range
			const FFrameRate TickRes    = MovieScene->GetTickResolution();
			const FFrameRate DisplayRes = MovieScene->GetDisplayRate();
			const TRange<FFrameNumber> PlayRange = MovieScene->GetPlaybackRange();

			const double StartSeconds = Args->HasField(TEXT("start_seconds")) ? Args->GetNumberField(TEXT("start_seconds")) : 0.0;
			const FFrameNumber StartFrame = ConvertFrameTime(
				DisplayRes.AsFrameTime(StartSeconds), DisplayRes, TickRes).RoundToFrame();

			FFrameNumber EndFrame;
			if (Args->HasField(TEXT("end_seconds")))
			{
				const double EndSeconds = Args->GetNumberField(TEXT("end_seconds"));
				EndFrame = ConvertFrameTime(
					DisplayRes.AsFrameTime(EndSeconds), DisplayRes, TickRes).RoundToFrame();
			}
			else
			{
				EndFrame = PlayRange.HasUpperBound() ? PlayRange.GetUpperBoundValue() : StartFrame + 150000; // ~5s at 30000 tick res
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Camera Cut Track")));
			MovieScene->Modify();

			// Get or create the camera cut track (there can be only one per sequence)
			UMovieSceneCameraCutTrack* CameraCutTrack = MovieScene->FindTrack<UMovieSceneCameraCutTrack>();
			if (!CameraCutTrack)
			{
				CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(MovieScene->AddTrack(UMovieSceneCameraCutTrack::StaticClass()));
			}
			if (!CameraCutTrack)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to create or find CameraCut track"));
			}

			// Create the camera binding ID referencing the possessable
			UE::MovieScene::FRelativeObjectBindingID RelativeID(CameraGuid);
			FMovieSceneObjectBindingID CameraBindingID(RelativeID);

			// Add the camera cut section
			UMovieSceneCameraCutSection* CutSection = CameraCutTrack->AddNewCameraCut(CameraBindingID, StartFrame);
			if (!CutSection)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to create camera cut section"));
			}

			// Set the section range
			CutSection->SetRange(TRange<FFrameNumber>(StartFrame, TRangeBound<FFrameNumber>::Exclusive(EndFrame)));

			Sequence->MarkPackageDirty();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added camera cut track pointing to '%s' in sequence '%s' (%.3fs - %.3fs)"),
				*CameraActorName, *Sequence->GetName(),
				StartSeconds,
				TickRes.AsSeconds(EndFrame)));
		});

	// ================================================================
	// add_sub_sequence - Embed a sub-sequence inside a master sequence
	// ================================================================
	MCP_TOOL(Registry, "add_sub_sequence")
		.Description(TEXT(
			"Embed a child LevelSequence inside a master LevelSequence using a Sub Track. "
			"The sub-sequence is placed at the specified time range on a new or existing Sub Track."))
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the master LevelSequence"), true)
		.StringArg(TEXT("sub_sequence_path"), TEXT("Content path of the child LevelSequence to embed"), true)
		.NumberArg(TEXT("start_seconds"), TEXT("Start time in seconds for the sub-sequence (default: 0)"))
		.NumberArg(TEXT("end_seconds"), TEXT("End time in seconds for the sub-sequence (default: end of sub-sequence playback range)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SequencePath, SubSequencePath;
			if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath))
				return FMCPToolResult::Error(TEXT("sequence_path is required"));
			if (!Args->TryGetStringField(TEXT("sub_sequence_path"), SubSequencePath))
				return FMCPToolResult::Error(TEXT("sub_sequence_path is required"));

			ULevelSequence* MasterSequence = LoadSequence(SequencePath);
			if (!IsValid(MasterSequence))
				return FMCPToolResult::Error(FString::Printf(TEXT("Master LevelSequence not found: %s"), *SequencePath));

			ULevelSequence* SubSequence = LoadSequence(SubSequencePath);
			if (!IsValid(SubSequence))
				return FMCPToolResult::Error(FString::Printf(TEXT("Sub LevelSequence not found: %s"), *SubSequencePath));

			UMovieScene* MasterMovieScene = MasterSequence->GetMovieScene();
			if (!MasterMovieScene)
				return FMCPToolResult::Error(TEXT("Master sequence has no MovieScene"));

			// Compute time range
			const FFrameRate TickRes    = MasterMovieScene->GetTickResolution();
			const FFrameRate DisplayRes = MasterMovieScene->GetDisplayRate();

			const double StartSeconds = Args->HasField(TEXT("start_seconds")) ? Args->GetNumberField(TEXT("start_seconds")) : 0.0;
			const FFrameNumber StartFrame = ConvertFrameTime(
				DisplayRes.AsFrameTime(StartSeconds), DisplayRes, TickRes).RoundToFrame();

			// Determine duration
			int32 DurationInTicks;
			if (Args->HasField(TEXT("end_seconds")))
			{
				const double EndSeconds = Args->GetNumberField(TEXT("end_seconds"));
				const FFrameNumber EndFrame = ConvertFrameTime(
					DisplayRes.AsFrameTime(EndSeconds), DisplayRes, TickRes).RoundToFrame();
				DurationInTicks = (EndFrame - StartFrame).Value;
			}
			else
			{
				// Use sub-sequence's own playback range duration
				UMovieScene* SubMovieScene = SubSequence->GetMovieScene();
				if (SubMovieScene)
				{
					const TRange<FFrameNumber> SubRange = SubMovieScene->GetPlaybackRange();
					if (SubRange.HasLowerBound() && SubRange.HasUpperBound())
					{
						const FFrameRate SubTickRes = SubMovieScene->GetTickResolution();
						const double SubDurationSec = SubTickRes.AsSeconds(SubRange.GetUpperBoundValue()) - SubTickRes.AsSeconds(SubRange.GetLowerBoundValue());
						const FFrameNumber DurFrame = ConvertFrameTime(
							DisplayRes.AsFrameTime(SubDurationSec), DisplayRes, TickRes).RoundToFrame();
						DurationInTicks = DurFrame.Value;
					}
					else
					{
						// Default to 5 seconds
						DurationInTicks = ConvertFrameTime(
							DisplayRes.AsFrameTime(5.0), DisplayRes, TickRes).RoundToFrame().Value;
					}
				}
				else
				{
					DurationInTicks = ConvertFrameTime(
						DisplayRes.AsFrameTime(5.0), DisplayRes, TickRes).RoundToFrame().Value;
				}
			}

			if (DurationInTicks <= 0)
				return FMCPToolResult::Error(TEXT("Duration must be positive"));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Sub Sequence")));
			MasterMovieScene->Modify();

			// Find or create a sub track (master track, not bound to any actor)
			UMovieSceneSubTrack* SubTrack = MasterMovieScene->FindTrack<UMovieSceneSubTrack>();
			if (!SubTrack)
			{
				SubTrack = Cast<UMovieSceneSubTrack>(MasterMovieScene->AddTrack(UMovieSceneSubTrack::StaticClass()));
			}
			if (!SubTrack)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to create or find Sub Track"));
			}

			// Add the sub-sequence section
			UMovieSceneSubSection* SubSection = SubTrack->AddSequence(SubSequence, StartFrame, DurationInTicks);
			if (!SubSection)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to add sub-sequence section"));
			}

			MasterSequence->MarkPackageDirty();
			GEditor->EndTransaction();

			const double EndSeconds = TickRes.AsSeconds(FFrameNumber(StartFrame.Value + DurationInTicks));
			return FMCPToolResult::Success(FString::Printf(
				TEXT("Embedded sub-sequence '%s' into '%s' (%.3fs - %.3fs)"),
				*SubSequence->GetName(), *MasterSequence->GetName(),
				StartSeconds, EndSeconds));
		});

	// ================================================================
	// add_fade_track - Add a cinematic fade track to a sequence
	// ================================================================
	MCP_TOOL(Registry, "add_fade_track")
		.Description(TEXT(
			"Add a cinematic fade track to a LevelSequence. Creates a Fade track (master track) with keyframes "
			"for start and end fade values. 0 = no fade (clear), 1 = fully black. "
			"Useful for fade-in/fade-out transitions in cinematics."))
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset"), true)
		.NumberArg(TEXT("start_seconds"), TEXT("Fade start time in seconds (default: 0)"))
		.NumberArg(TEXT("end_seconds"), TEXT("Fade end time in seconds (default: 2)"))
		.NumberArg(TEXT("start_value"), TEXT("Fade value at start (0 = no fade/clear, 1 = fully black). Default: 0"))
		.NumberArg(TEXT("end_value"), TEXT("Fade value at end (0 = no fade/clear, 1 = fully black). Default: 1"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SequencePath;
			if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath))
				return FMCPToolResult::Error(TEXT("sequence_path is required"));

			const double StartSeconds = Args->HasField(TEXT("start_seconds")) ? Args->GetNumberField(TEXT("start_seconds")) : 0.0;
			const double EndSeconds   = Args->HasField(TEXT("end_seconds"))   ? Args->GetNumberField(TEXT("end_seconds"))   : 2.0;
			const float  StartValue   = Args->HasField(TEXT("start_value"))   ? (float)Args->GetNumberField(TEXT("start_value")) : 0.0f;
			const float  EndValue     = Args->HasField(TEXT("end_value"))     ? (float)Args->GetNumberField(TEXT("end_value"))   : 1.0f;

			if (EndSeconds <= StartSeconds)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("end_seconds (%.3f) must be greater than start_seconds (%.3f)"),
					EndSeconds, StartSeconds));
			}

			ULevelSequence* Sequence = LoadSequence(SequencePath);
			if (!IsValid(Sequence))
				return FMCPToolResult::Error(FString::Printf(TEXT("LevelSequence not found: %s"), *SequencePath));

			UMovieScene* MovieScene = Sequence->GetMovieScene();
			if (!MovieScene)
				return FMCPToolResult::Error(TEXT("Sequence has no MovieScene"));

			const FFrameRate TickRes    = MovieScene->GetTickResolution();
			const FFrameRate DisplayRes = MovieScene->GetDisplayRate();

			const FFrameNumber StartFrame = ConvertFrameTime(
				DisplayRes.AsFrameTime(StartSeconds), DisplayRes, TickRes).RoundToFrame();
			const FFrameNumber EndFrame = ConvertFrameTime(
				DisplayRes.AsFrameTime(EndSeconds), DisplayRes, TickRes).RoundToFrame();

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Fade Track")));
			MovieScene->Modify();

			// Find or create the fade track (master track)
			UMovieSceneFadeTrack* FadeTrack = MovieScene->FindTrack<UMovieSceneFadeTrack>();
			if (!FadeTrack)
			{
				FadeTrack = Cast<UMovieSceneFadeTrack>(MovieScene->AddTrack(UMovieSceneFadeTrack::StaticClass()));
			}
			if (!FadeTrack)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to create or find Fade track"));
			}

			// Create a new section for this fade
			UMovieSceneSection* RawSection = FadeTrack->CreateNewSection();
			UMovieSceneFadeSection* FadeSection = Cast<UMovieSceneFadeSection>(RawSection);
			if (!FadeSection)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to create fade section"));
			}

			// Set the section range
			FadeSection->SetRange(TRange<FFrameNumber>(StartFrame, TRangeBound<FFrameNumber>::Exclusive(EndFrame)));
			FadeTrack->AddSection(*FadeSection);

			// Add keyframes on the float curve for the fade values
			FadeSection->FloatCurve.AddLinearKey(StartFrame, StartValue);
			FadeSection->FloatCurve.AddLinearKey(EndFrame - 1, EndValue);

			// Default fade color is black
			FadeSection->FadeColor = FLinearColor::Black;

			Sequence->MarkPackageDirty();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added fade track to '%s': %.3fs (value %.2f) -> %.3fs (value %.2f)"),
				*Sequence->GetName(), StartSeconds, StartValue, EndSeconds, EndValue));
		});

}

} // namespace MCPSequencerTools::SpecialTracks
