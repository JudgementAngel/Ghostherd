// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/PIE/PIECommon.h"
#include "Common/MCPActorResolver.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

namespace MCPPIETools::Common
{

UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

UWorld* GetActivePIEWorld()
{
	if (!GEditor) return nullptr;
	return GEditor->PlayWorld;
}

bool IsPIEActive()
{
	return GEditor && GEditor->IsPlaySessionInProgress() && GEditor->PlayWorld != nullptr;
}

APlayerController* GetPIEPlayerController(int32 ControllerIndex)
{
	UWorld* World = GetActivePIEWorld();
	if (!World) return nullptr;

	int32 Idx = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (Idx == ControllerIndex)
			{
				return PC;
			}
			++Idx;
		}
	}
	return nullptr;
}

AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	// v4 Phase 1: cached resolver (O(1) amortized) replaces the per-call actor scan.
	return MCPCommon::FindActorByLabel(World, Label);
}

} // namespace MCPPIETools::Common
