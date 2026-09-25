// Copyright StraySpark Studio 2026. All Rights Reserved.
#include "Tools/MCPBlueprintTools.h"
#include "MCPToolRegistry.h"

namespace MCPBlueprintTools
{
	namespace Lifecycle    { void RegisterAll(FMCPToolRegistry&); }
	namespace Variables    { void RegisterAll(FMCPToolRegistry&); }
	namespace Functions    { void RegisterAll(FMCPToolRegistry&); }
	namespace Graph        { void RegisterAll(FMCPToolRegistry&); }
	namespace Nodes        { void RegisterAll(FMCPToolRegistry&); }
	namespace FlowControl  { void RegisterAll(FMCPToolRegistry&); }
	namespace Dispatchers  { void RegisterAll(FMCPToolRegistry&); }
	namespace Input        { void RegisterAll(FMCPToolRegistry&); }
	namespace Introspection { void RegisterAll(FMCPToolRegistry&); }   // v4 Phase 2
	namespace Validation   { void RegisterAll(FMCPToolRegistry&); }   // v5 increment 12
	namespace Patch        { void RegisterAll(FMCPToolRegistry&); }   // v5 increment 13

	void RegisterAll(FMCPToolRegistry& Registry)
	{
		Lifecycle::RegisterAll(Registry);
		Validation::RegisterAll(Registry);
		Patch::RegisterAll(Registry);
		Variables::RegisterAll(Registry);
		Functions::RegisterAll(Registry);
		Graph::RegisterAll(Registry);
		Nodes::RegisterAll(Registry);
		FlowControl::RegisterAll(Registry);
		Dispatchers::RegisterAll(Registry);
		Input::RegisterAll(Registry);
		Introspection::RegisterAll(Registry);
	}
}
