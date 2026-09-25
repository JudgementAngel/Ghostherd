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
#include "Common/MCPAssetCreate.h"

namespace MCPSequencerTools::Lifecycle
{

using namespace MCPSequencerTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_level_sequence - Create a new LevelSequence asset
	// ================================================================
	MCP_TOOL(Registry, "create_level_sequence")
		.Description(TEXT("Create a new LevelSequence asset at the specified content path. Sets the display frame rate and saves the asset immediately."))
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new Level Sequence (e.g., '/Game/Sequences/MySequence')"), true)
		.IntArg(TEXT("frame_rate"), TEXT("Display frame rate (frames per second, default: 30)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			int32 FrameRate = 30;
			if (Args->HasField(TEXT("frame_rate")))
			{
				FrameRate = FMath::Clamp((int32)Args->GetNumberField(TEXT("frame_rate")), 1, 120);
			}

			// Split into package path and asset name, and refuse a path that is
			// already taken. Done before BeginTransaction so a rejected path never
			// opens (and immediately cancels) an undo transaction.
			FString PackagePath, AssetName;
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package)
				return PackageError;

			// Use IAssetTools::CreateAsset to properly invoke the factory
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

			// We cannot include the private LevelSequenceFactoryNew header, so we create
			// a package directly and use NewObject<ULevelSequence> + Initialize(), which
			// mirrors what ULevelSequenceFactoryNew::FactoryCreateNew does internally.
			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Level Sequence")));

			ULevelSequence* NewSequence = NewObject<ULevelSequence>(
				Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);

			if (!NewSequence)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to create LevelSequence object"));
			}

			// Initialize calls PostInitProperties, sets up the MovieScene, tick/display rates
			NewSequence->Initialize();

			// Set the requested display frame rate
			UMovieScene* MovieScene = NewSequence->GetMovieScene();
			if (MovieScene)
			{
				MovieScene->SetDisplayRate(FFrameRate(FrameRate, 1));
			}

			GEditor->EndTransaction();

			SaveSequencePackage(Package, NewSequence, PackagePath);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created LevelSequence '%s' at '%s' (frame rate: %d fps)"),
				*AssetName, *AssetPath, FrameRate));
		});

	// ================================================================
	// open_sequence - Open a LevelSequence in the Sequencer editor
	// ================================================================
	MCP_TOOL(Registry, "open_sequence")
		.Description(TEXT("Open a LevelSequence asset in the Sequencer editor. The sequence will be focused and ready for editing."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the LevelSequence to open (e.g., '/Game/Sequences/MySequence.MySequence')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			ULevelSequence* Sequence = LoadSequence(AssetPath);
			if (!IsValid(Sequence))
				return FMCPToolResult::Error(FString::Printf(TEXT("LevelSequence not found: %s"), *AssetPath));

			UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (!EditorSubsystem)
				return FMCPToolResult::Error(TEXT("Could not get AssetEditorSubsystem"));

			const bool bOpened = EditorSubsystem->OpenEditorForAsset(Sequence);
			if (!bOpened)
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to open editor for: %s"), *AssetPath));

			return FMCPToolResult::Success(FString::Printf(TEXT("Opened sequence '%s' in Sequencer"), *Sequence->GetName()));
		});

	// ================================================================
	// add_actor_to_sequence - Bind an actor as a possessable
	// ================================================================
	MCP_TOOL(Registry, "add_actor_to_sequence")
		.Description(TEXT("Bind an actor from the current level as a Possessable in a LevelSequence. The actor can then have tracks and keyframes added to it."))
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset (e.g., '/Game/Sequences/MySequence.MySequence')"), true)
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor in the current level to bind as a possessable"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SequencePath, ActorName;
			if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath))
				return FMCPToolResult::Error(TEXT("sequence_path is required"));
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

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

			// Check if already bound
			FGuid ExistingGuid = FindPossessableGuid(MovieScene, ActorName);
			if (ExistingGuid.IsValid())
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' is already bound in this sequence"), *ActorName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Actor to Sequence")));
			MovieScene->Modify();

			// Create the possessable binding in the MovieScene
			const FGuid NewGuid = MovieScene->AddPossessable(ActorName, Actor->GetClass());
			if (!NewGuid.IsValid())
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to add possessable to MovieScene"));
			}

			// Complete the binding so LevelSequence tracks the actual world object
			Sequence->BindPossessableObject(NewGuid, *Actor, World);

			Sequence->MarkPackageDirty();

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Bound actor '%s' as possessable in sequence '%s' (Guid: %s)"),
				*ActorName, *Sequence->GetName(), *NewGuid.ToString()));
		});

	// ================================================================
	// set_sequence_range - Set the playback range of a sequence
	// ================================================================
	MCP_TOOL(Registry, "set_sequence_range")
		.Description(TEXT("Set the playback range (start and end times in seconds) of a LevelSequence. Times are converted to frame numbers using the sequence's tick resolution."))
		.Idempotent()
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset"), true)
		.NumberArg(TEXT("start_seconds"), TEXT("Playback start time in seconds (default: 0)"))
		.NumberArg(TEXT("end_seconds"), TEXT("Playback end time in seconds (default: 5)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SequencePath;
			if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath))
				return FMCPToolResult::Error(TEXT("sequence_path is required"));

			const double StartSeconds = Args->HasField(TEXT("start_seconds")) ? Args->GetNumberField(TEXT("start_seconds")) : 0.0;
			const double EndSeconds   = Args->HasField(TEXT("end_seconds"))   ? Args->GetNumberField(TEXT("end_seconds"))   : 5.0;

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

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Sequence Range")));
			MovieScene->Modify();

			// Duration = end - start frames
			const int32 Duration = (EndFrame - StartFrame).Value;
			MovieScene->SetPlaybackRange(StartFrame, Duration);

			Sequence->MarkPackageDirty();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Set playback range for '%s': %.3fs - %.3fs (frames %d - %d at %d fps display rate)"),
				*Sequence->GetName(), StartSeconds, EndSeconds,
				StartFrame.Value, EndFrame.Value,
				FMath::RoundToInt((float)DisplayRes.Numerator / (float)DisplayRes.Denominator)));
		});

	// ================================================================
	// play_sequence - Preview-play the sequence in the editor
	// ================================================================
	MCP_TOOL(Registry, "play_sequence")
		.Description(TEXT(
			"Open a LevelSequence in the Sequencer editor (if not already open) so it can be previewed. "
			"Playback must be triggered manually in the Sequencer UI after opening. "
			"This tool ensures the sequence is loaded and focused in the editor."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset to open and play"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SequencePath;
			if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath))
				return FMCPToolResult::Error(TEXT("sequence_path is required"));

			ULevelSequence* Sequence = LoadSequence(SequencePath);
			if (!IsValid(Sequence))
				return FMCPToolResult::Error(FString::Printf(TEXT("LevelSequence not found: %s"), *SequencePath));

			UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (!EditorSubsystem)
				return FMCPToolResult::Error(TEXT("Could not get AssetEditorSubsystem"));

			// Open or focus the Sequencer editor for this asset
			const bool bOpened = EditorSubsystem->OpenEditorForAsset(Sequence);
			if (!bOpened)
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to open sequence in editor: %s"), *SequencePath));

			// Log playback range for reference
			UMovieScene* MovieScene = Sequence->GetMovieScene();
			FString RangeInfo;
			if (MovieScene)
			{
				const FFrameRate TickRes    = MovieScene->GetTickResolution();
				const FFrameRate DisplayRes = MovieScene->GetDisplayRate();
				const TRange<FFrameNumber> PlayRange = MovieScene->GetPlaybackRange();

				if (PlayRange.HasLowerBound() && PlayRange.HasUpperBound())
				{
					const double StartSec = TickRes.AsSeconds(PlayRange.GetLowerBoundValue());
					const double EndSec   = TickRes.AsSeconds(PlayRange.GetUpperBoundValue());
					RangeInfo = FString::Printf(TEXT(" | Range: %.2fs - %.2fs"), StartSec, EndSec);
				}
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Opened '%s' in Sequencer. Press Play in the Sequencer UI to preview.%s"),
				*Sequence->GetName(), *RangeInfo));
		});
	// ================================================================
	// get_sequence_info - Read-only info about a LevelSequence
	// ================================================================
	MCP_TOOL(Registry, "get_sequence_info")
		.Description(TEXT("Get detailed information about a LevelSequence: display rate, playback range, bound actors, track types, and section counts. Useful for inspecting existing sequences before modifying them."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SequencePath;
			if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath))
				return FMCPToolResult::Error(TEXT("sequence_path is required"));

			ULevelSequence* Sequence = LoadSequence(SequencePath);
			if (!IsValid(Sequence))
				return FMCPToolResult::Error(FString::Printf(TEXT("LevelSequence not found: %s"), *SequencePath));

			UMovieScene* MovieScene = Sequence->GetMovieScene();
			if (!MovieScene)
				return FMCPToolResult::Error(TEXT("MovieScene is null"));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Sequence->GetName());
			Result->SetStringField(TEXT("path"), SequencePath);

			// Display rate
			FFrameRate DisplayRate = MovieScene->GetDisplayRate();
			Result->SetNumberField(TEXT("display_fps"), (double)DisplayRate.Numerator / (double)DisplayRate.Denominator);

			// Tick resolution
			FFrameRate TickRes = MovieScene->GetTickResolution();
			Result->SetNumberField(TEXT("tick_resolution"), (double)TickRes.Numerator);

			// Playback range
			TRange<FFrameNumber> PlayRange = MovieScene->GetPlaybackRange();
			if (PlayRange.HasLowerBound() && PlayRange.HasUpperBound())
			{
				Result->SetNumberField(TEXT("start_seconds"), TickRes.AsSeconds(PlayRange.GetLowerBoundValue()));
				Result->SetNumberField(TEXT("end_seconds"), TickRes.AsSeconds(PlayRange.GetUpperBoundValue()));
			}

			// Possessables (bound actors)
			TArray<TSharedPtr<FJsonValue>> BindingsArray;
			const int32 PossCount = MovieScene->GetPossessableCount();
			for (int32 i = 0; i < PossCount; ++i)
			{
				const FMovieScenePossessable& Poss = MovieScene->GetPossessable(i);
				TSharedPtr<FJsonObject> BindObj = MakeShared<FJsonObject>();
				BindObj->SetStringField(TEXT("name"), Poss.GetName());
				BindObj->SetStringField(TEXT("guid"), Poss.GetGuid().ToString());
				BindObj->SetStringField(TEXT("class"), Poss.GetPossessedObjectClass() ? Poss.GetPossessedObjectClass()->GetName() : TEXT("Unknown"));

				// List tracks for this binding
				FMovieSceneBinding* Binding = MovieScene->FindBinding(Poss.GetGuid());
				if (Binding)
				{
					TArray<TSharedPtr<FJsonValue>> TracksArray;
					for (UMovieSceneTrack* Track : Binding->GetTracks())
					{
						if (!Track) continue;
						TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
						TrackObj->SetStringField(TEXT("type"), Track->GetClass()->GetName());
						TrackObj->SetNumberField(TEXT("section_count"), Track->GetAllSections().Num());
						TracksArray.Add(MakeShared<FJsonValueObject>(TrackObj));
					}
					BindObj->SetArrayField(TEXT("tracks"), TracksArray);
				}

				BindingsArray.Add(MakeShared<FJsonValueObject>(BindObj));
			}
			Result->SetArrayField(TEXT("bindings"), BindingsArray);
			Result->SetNumberField(TEXT("binding_count"), PossCount);

			// Master tracks
			TArray<TSharedPtr<FJsonValue>> MasterArray;
			for (UMovieSceneTrack* Track : MovieScene->GetTracks())
			{
				if (!Track) continue;
				TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
				TrackObj->SetStringField(TEXT("type"), Track->GetClass()->GetName());
				TrackObj->SetNumberField(TEXT("section_count"), Track->GetAllSections().Num());
				MasterArray.Add(MakeShared<FJsonValueObject>(TrackObj));
			}
			Result->SetArrayField(TEXT("master_tracks"), MasterArray);

			FString ResultStr;
			auto Writer = TJsonWriterFactory<>::Create(&ResultStr);
			FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
			return FMCPToolResult::Success(ResultStr);
		});

}

} // namespace MCPSequencerTools::Lifecycle
