// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWorld;
class AActor;
class ULevelSequence;
class UMovieScene;
class UPackage;
class UObject;

namespace MCPSequencerTools::Common
{
	UWorld*          GetEditorWorld();
	ULevelSequence*  LoadSequence(const FString& AssetPath);
	FGuid            FindPossessableGuid(UMovieScene* MovieScene, const FString& ActorName);
	AActor*          FindActorByLabel(UWorld* World, const FString& Label);
	void             SaveSequencePackage(UPackage* Package, UObject* Asset, const FString& PackagePath);
}
