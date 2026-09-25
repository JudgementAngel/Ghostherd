// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMCPToolRegistry;

// v4.5 (UE 5.8) — interop with Epic's first-party experimental MCP plugin.
// Discovers tools registered with the engine's ModelContextProtocol module
// (IModelContextProtocolModule::GetTools) and surfaces them through THIS server's
// registry as `epic_<name>` tools, so users keep their custom UToolsetDefinition /
// AICallable tools while gaining our catalog mode, scopes, tasks, transactions,
// resources and prompts. Soft dependency: a no-op when Epic's plugin is absent.
namespace MCPToolsetAdapter
{
	/** Import Epic MCP toolset tools into Registry under the active category.
	 *  Returns the number imported (0 if the Epic plugin isn't available). */
	int32 ImportEpicToolsets(FMCPToolRegistry& Registry);
}
