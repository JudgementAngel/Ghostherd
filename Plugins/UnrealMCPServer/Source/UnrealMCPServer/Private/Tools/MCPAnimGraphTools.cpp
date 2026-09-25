// Copyright StraySpark Studio 2026. All Rights Reserved.
#include "Tools/MCPAnimGraphTools.h"
#include "MCPToolRegistry.h"

namespace MCPAnimGraphTools
{
	namespace Lifecycle    { void RegisterAll(FMCPToolRegistry&); }
	namespace BlendSpaces  { void RegisterAll(FMCPToolRegistry&); }
	namespace Montages     { void RegisterAll(FMCPToolRegistry&); }
	namespace Notifies     { void RegisterAll(FMCPToolRegistry&); }
	namespace StateMachine { void RegisterAll(FMCPToolRegistry&); }

	void RegisterAll(FMCPToolRegistry& Registry)
	{
		Lifecycle::RegisterAll(Registry);
		BlendSpaces::RegisterAll(Registry);
		Montages::RegisterAll(Registry);
		Notifies::RegisterAll(Registry);
		StateMachine::RegisterAll(Registry);
	}
}
