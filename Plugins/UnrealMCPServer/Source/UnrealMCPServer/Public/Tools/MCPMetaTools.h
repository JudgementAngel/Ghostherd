// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class FMCPToolRegistry;

/**
 * v4 Phase 1 — progressive-disclosure meta-tools.
 *
 * With ~360 tools, a full tools/list costs ~60K tokens before the agent has
 * read the user's request. Catalog mode (see UMCPSettings::ToolExposureMode)
 * exposes only these meta-tools plus a small high-frequency core; agents
 * discover everything else on demand:
 *
 *   search_tools          keyword/category search over the registry
 *   get_tool_schemas      full definitions for chosen tools
 *   list_tool_categories  the 49 categories with counts and names
 *   run_tool_script       restricted multi-step program: sequential tool calls
 *                         with saved-result references and foreach, executed
 *                         in one transaction (all-or-nothing)
 */
namespace MCPMetaTools
{
	void RegisterAll(FMCPToolRegistry& Registry);
}
