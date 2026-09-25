// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMCPToolRegistry;

/**
 * Project gap-fill tools (see Plugins/UnrealMCPServer_v4/docs/MCP_TOOLS_GAP_PLAN.md).
 * Tier 1: component attach-socket, skeleton sockets, live PIE property inspection.
 * Registered under existing categories (Blueprint / Animation / PIE).
 */
namespace MCPMoltTools
{
	void RegisterAll(FMCPToolRegistry& Registry);
}
