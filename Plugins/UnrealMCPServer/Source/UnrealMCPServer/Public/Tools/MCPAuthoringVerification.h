// Copyright StraySpark Studio 2026. All Rights Reserved.
// v5 increment 19: non-mutating authoring verification tools (PCG graph, import preflight,
// material / Niagara / sound setup, shared-asset instance impact).

#pragma once

#include "CoreMinimal.h"

class FMCPToolRegistry;

namespace MCPAuthoringVerification
{
	void RegisterAll(FMCPToolRegistry& Registry);
}
