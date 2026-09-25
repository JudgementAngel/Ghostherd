// Copyright StraySpark Studio 2026. All Rights Reserved.
#include "Tools/MCPWidgetTools.h"
#include "MCPToolRegistry.h"

namespace MCPWidgetTools
{
	namespace Lifecycle  { void RegisterAll(FMCPToolRegistry&); }
	namespace Tree       { void RegisterAll(FMCPToolRegistry&); }
	namespace Properties { void RegisterAll(FMCPToolRegistry&); }
	namespace Events     { void RegisterAll(FMCPToolRegistry&); }
	namespace Batch      { void RegisterAll(FMCPToolRegistry&); }
	namespace Animation  { void RegisterAll(FMCPToolRegistry&); }
	namespace PIE        { void RegisterAll(FMCPToolRegistry&); }
	namespace Layout     { void RegisterAll(FMCPToolRegistry&); }
	namespace Patch      { void RegisterAll(FMCPToolRegistry&); }

	void RegisterAll(FMCPToolRegistry& Registry)
	{
		Lifecycle::RegisterAll(Registry);
		Tree::RegisterAll(Registry);
		Layout::RegisterAll(Registry);
		Patch::RegisterAll(Registry);
		Properties::RegisterAll(Registry);
		Events::RegisterAll(Registry);
		Batch::RegisterAll(Registry);
		Animation::RegisterAll(Registry);
		PIE::RegisterAll(Registry);
	}
}
