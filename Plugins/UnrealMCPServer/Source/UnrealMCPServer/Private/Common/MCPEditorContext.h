// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"

class UWorld;

/**
 * v4 Phase 1 — single source of truth for editor-world access.
 *
 * Replaces the GetEditorWorld() helper that was redefined identically in 15+
 * tool namespaces. Also classifies the world state so tools can give agents a
 * precise reason ("PIE is active") instead of a generic "no world".
 */
namespace MCPCommon
{
	enum class EWorldState : uint8
	{
		EditorReady,  // Editor world available, no PIE
		PIEActive,    // A play session is in progress
		Invalid,      // No GEditor / no world (startup, shutdown, commandlet)
	};

	/** The editor world, or nullptr. Same contract as the old per-file helpers. */
	UWorld* GetEditorWorld();

	/** Classify the current editor state. */
	EWorldState GetWorldState();

	/** Editor world + ready-made structured error when unavailable.
	 *  Usage:
	 *    FMCPToolResult Err;
	 *    UWorld* World = MCPCommon::GetEditorWorldChecked(Err);
	 *    if (!World) return Err;
	 */
	UWorld* GetEditorWorldChecked(FMCPToolResult& OutError);
}
