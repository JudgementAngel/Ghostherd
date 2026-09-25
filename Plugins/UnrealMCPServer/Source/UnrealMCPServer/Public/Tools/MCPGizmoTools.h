// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMCPToolRegistry;

// v4.5 (UE 5.8) — Viewport transform gizmo control. 5.8 unified the editor gizmos
// on the Interactive Tools Framework; the level-editor transform widget is driven
// through FEditorModeTools (GLevelEditorModeTools()).
namespace MCPGizmoTools
{
	void RegisterAll(FMCPToolRegistry& Registry);
}
