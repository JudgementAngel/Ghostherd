// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMCPToolRegistry;

// v4.5 (UE 5.8) — Substrate material system status/introspection. Substrate
// enablement is a project rendering setting (r.Substrate, requires restart); these
// tools report the active configuration so agents author materials accordingly.
namespace MCPSubstrateTools
{
	void RegisterAll(FMCPToolRegistry& Registry);
}
