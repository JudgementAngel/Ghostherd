// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMCPToolRegistry;

// v4.5 (UE 5.8) — Iris replication system status. Iris is production-ready in 5.8.
// Whether a net driver uses Iris is decided at connection time from project/cmdline
// config; these tools report the current Iris configuration for verification.
namespace MCPIrisTools
{
	void RegisterAll(FMCPToolRegistry& Registry);
}
