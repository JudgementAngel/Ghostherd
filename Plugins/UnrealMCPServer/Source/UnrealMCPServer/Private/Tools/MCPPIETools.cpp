// Copyright StraySpark Studio 2026. All Rights Reserved.
#include "Tools/MCPPIETools.h"
#include "MCPToolRegistry.h"

namespace MCPPIETools
{
	namespace Lifecycle { void RegisterAll(FMCPToolRegistry&); }
	namespace Input     { void RegisterAll(FMCPToolRegistry&); }
	namespace Capture   { void RegisterAll(FMCPToolRegistry&); }

	void RegisterAll(FMCPToolRegistry& Registry)
	{
		Lifecycle::RegisterAll(Registry);
		Input::RegisterAll(Registry);
		Capture::RegisterAll(Registry);
	}
}
