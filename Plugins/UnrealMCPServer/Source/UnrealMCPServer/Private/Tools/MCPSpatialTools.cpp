// Copyright StraySpark Studio 2026. All Rights Reserved.
#include "Tools/MCPSpatialTools.h"
#include "MCPToolRegistry.h"

namespace MCPSpatialTools
{
	namespace Bounds    { void RegisterAll(FMCPToolRegistry&); }
	namespace Trace     { void RegisterAll(FMCPToolRegistry&); }
	namespace Placement { void RegisterAll(FMCPToolRegistry&); }
	namespace Alignment { void RegisterAll(FMCPToolRegistry&); }

	void RegisterAll(FMCPToolRegistry& Registry)
	{
		Bounds::RegisterAll(Registry);
		Trace::RegisterAll(Registry);
		Placement::RegisterAll(Registry);
		Alignment::RegisterAll(Registry);
	}
}
