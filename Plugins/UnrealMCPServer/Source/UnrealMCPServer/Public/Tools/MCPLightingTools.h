// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMCPToolRegistry;

// v4.5 (UE 5.8) — Lighting feature family: MegaLights (production-ready in 5.8)
// and Lumen quality/mode control, driven through rendering console variables.
namespace MCPLightingTools
{
	void RegisterAll(FMCPToolRegistry& Registry);
}
