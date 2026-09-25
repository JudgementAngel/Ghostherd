// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Common/MCPEditorContext.h"

#include "Editor.h"
#include "Engine/World.h"

namespace MCPCommon
{

UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

EWorldState GetWorldState()
{
	if (!GEditor)
	{
		return EWorldState::Invalid;
	}
	if (GEditor->IsPlaySessionInProgress())
	{
		return EWorldState::PIEActive;
	}
	return GEditor->GetEditorWorldContext().World() ? EWorldState::EditorReady : EWorldState::Invalid;
}

UWorld* GetEditorWorldChecked(FMCPToolResult& OutError)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal,
			TEXT("No editor world available."),
			TEXT("The editor may still be starting up or shutting down. Retry shortly."));
	}
	return World;
}

}
