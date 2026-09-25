// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"  // ECollisionChannel

class UWorld;
class AActor;
class FJsonObject;

namespace MCPSpatialTools::Common
{
	UWorld*  GetEditorWorld();
	AActor*  FindActorByLabel(UWorld* World, const FString& Label);
	TSharedPtr<FJsonObject> MakeVectorJson(const FVector& V);
	ECollisionChannel ParseTraceChannel(const FString& ChannelName);
}
