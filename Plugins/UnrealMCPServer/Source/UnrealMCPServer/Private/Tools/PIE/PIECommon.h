// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWorld;
class AActor;
class APlayerController;

namespace MCPPIETools::Common
{
	/** Editor (non-PIE) world. */
	UWorld* GetEditorWorld();

	/** Active PIE world or nullptr if no PIE session is running. */
	UWorld* GetActivePIEWorld();

	/** Lookup PIE player controller by index. Returns nullptr if PIE not active. */
	APlayerController* GetPIEPlayerController(int32 ControllerIndex);

	/** Find an actor by its editor label inside the given world. */
	AActor* FindActorByLabel(UWorld* World, const FString& Label);

	/** True iff GEditor is non-null and a play session is in progress. */
	bool IsPIEActive();
}
