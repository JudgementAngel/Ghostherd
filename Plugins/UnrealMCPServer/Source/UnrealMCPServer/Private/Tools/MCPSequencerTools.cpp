#include "Tools/MCPSequencerTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

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

// Sections
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneBoolSection.h"

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

// LevelSequenceEditor factory - in Private, so we use the asset tools route instead
// (ULevelSequenceFactoryNew is a private editor class; we create via IAssetTools::CreateAsset)

namespace MCPSequencerTools
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

/** Load a ULevelSequence by content path (e.g. "/Game/Sequences/MySeq"). */
static ULevelSequence* LoadSequence(const FString& AssetPath)
{
	return LoadObject<ULevelSequence>(nullptr, *AssetPath);
}

/** Find a possessable GUID in the MovieScene by its display name. */
static FGuid FindPossessableGuid(UMovieScene* MovieScene, const FString& ActorName)
{
	if (!MovieScene) return FGuid();

	const int32 Count = MovieScene->GetPossessableCount();
	for (int32 i = 0; i < Count; ++i)
	{
		const FMovieScenePossessable& Poss = MovieScene->GetPossessable(i);
		if (Poss.GetName() == ActorName)
		{
			return Poss.GetGuid();
		}
	}
	return FGuid();
}

/** Find an actor in the editor world by label. */
static AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Label)
		{
			return *It;
		}
	}
	return nullptr;
}

/**
 * Save a package to disk using the established pattern used across the codebase.
 */
static void SaveSequencePackage(UPackage* Package, UObject* Asset, const FString& PackagePath)
{
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);

	FString PackageFilename = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
}

// ---------------------------------------------------------------------------
// Tool registration
// ---------------------------------------------------------------------------

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_level_sequence - Create a new LevelSequence asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new Level Sequence (e.g., '/Game/Sequences/MySequence')"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("frame_rate"), TEXT("Display frame rate (frames per second, default: 30)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_level_sequence");
		Def.Description = TEXT("Create a new LevelSequence asset at the specified content path. Sets the display frame rate and saves the asset immediately.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			int32 FrameRate = 30;
			if (Args->HasField(TEXT("frame_rate")))
			{
				FrameRate = FMath::Clamp((int32)Args->GetNumberField(TEXT("frame_rate")), 1, 120);
			}

			// Split into package path and asset name
			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			FString AssetName   = FPackageName::GetShortName(AssetPath);

			// Use IAssetTools::CreateAsset to properly invoke the factory
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

			// We cannot include the private LevelSequenceFactoryNew header, so we create
			// a package directly and use NewObject<ULevelSequence> + Initialize(), which
			// mirrors what ULevelSequenceFactoryNew::FactoryCreateNew does internally.
			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Level Sequence")));

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to create package"));
			}

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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// open_sequence - Open a LevelSequence in the Sequencer editor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the LevelSequence to open (e.g., '/Game/Sequences/MySequence.MySequence')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("open_sequence");
		Def.Description = TEXT("Open a LevelSequence asset in the Sequencer editor. The sequence will be focused and ready for editing.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_actor_to_sequence - Bind an actor as a possessable
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset (e.g., '/Game/Sequences/MySequence.MySequence')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor in the current level to bind as a possessable"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_actor_to_sequence");
		Def.Description = TEXT("Bind an actor from the current level as a Possessable in a LevelSequence. The actor can then have tracks and keyframes added to it.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_sequence_track - Add a track to an existing binding
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the already-bound actor to add the track to"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("track_type"), TEXT("Type of track to add"),
			{ TEXT("Transform"), TEXT("Visibility") }, true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_sequence_track");
		Def.Description = TEXT(
			"Add a track to an actor binding in a LevelSequence. "
			"Transform adds a 3D transform track; Visibility adds a bool visibility track. "
			"The actor must already be bound via add_actor_to_sequence.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_keyframe - Add a keyframe on a track at a given time
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the bound actor"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("track_type"), TEXT("Track type to key"),
			{ TEXT("Transform"), TEXT("Visibility") }, true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("time_seconds"), TEXT("Time in seconds at which to place the keyframe"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("value"), TEXT(
			"Value to key. "
			"Transform: space-separated floats: 'X Y Z' (location only) or 'X Y Z Pitch Yaw Roll' (location + rotation). "
			"Visibility: 'true' or 'false' (visible = true means actor is shown)."), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_keyframe");
		Def.Description = TEXT(
			"Add a keyframe at a specified time on a track belonging to a bound actor. "
			"For Transform tracks, the value is parsed as 'X Y Z' or 'X Y Z Pitch Yaw Roll' in world space. "
			"For Visibility tracks, the value is 'true' (visible) or 'false' (hidden).");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_sequence_range - Set the playback range of a sequence
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("start_seconds"), TEXT("Playback start time in seconds (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("end_seconds"), TEXT("Playback end time in seconds (default: 5)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_sequence_range");
		Def.Description = TEXT("Set the playback range (start and end times in seconds) of a LevelSequence. Times are converted to frame numbers using the sequence's tick resolution.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// play_sequence - Preview-play the sequence in the editor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset to open and play"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("play_sequence");
		Def.Description = TEXT(
			"Open a LevelSequence in the Sequencer editor (if not already open) so it can be previewed. "
			"Playback must be triggered manually in the Sequencer UI after opening. "
			"This tool ensures the sequence is loaded and focused in the editor.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}
	// ================================================================
	// get_sequence_info - Read-only info about a LevelSequence
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("sequence_path"), TEXT("Content path of the LevelSequence asset"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_sequence_info");
		Def.Description = TEXT("Get detailed information about a LevelSequence: display rate, playback range, bound actors, track types, and section counts. Useful for inspecting existing sequences before modifying them.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPSequencerTools
