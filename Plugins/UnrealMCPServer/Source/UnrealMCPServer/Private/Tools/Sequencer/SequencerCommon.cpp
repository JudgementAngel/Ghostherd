// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/Sequencer/SequencerCommon.h"
#include "Common/MCPActorResolver.h"

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

namespace MCPSequencerTools::Common
{

UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

/** Load a ULevelSequence by content path (e.g. "/Game/Sequences/MySeq"). */
ULevelSequence* LoadSequence(const FString& AssetPath)
{
	return LoadObject<ULevelSequence>(nullptr, *AssetPath);
}

/** Find a possessable GUID in the MovieScene by its display name. */
FGuid FindPossessableGuid(UMovieScene* MovieScene, const FString& ActorName)
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
AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	// v4 Phase 1: cached resolver (O(1) amortized) replaces the per-call actor scan.
	return MCPCommon::FindActorByLabel(World, Label);
}

/**
 * Save a package to disk using the established pattern used across the codebase.
 */
void SaveSequencePackage(UPackage* Package, UObject* Asset, const FString& PackagePath)
{
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);

	FString PackageFilename = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
}

} // namespace MCPSequencerTools::Common
