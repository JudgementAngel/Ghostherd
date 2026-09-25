// Copyright StraySpark Studio 2026. All Rights Reserved.
#include "Tools/MCPSequencerTools.h"
#include "MCPToolRegistry.h"

namespace MCPSequencerTools
{
	namespace Lifecycle      { void RegisterAll(FMCPToolRegistry&); }
	namespace Tracks         { void RegisterAll(FMCPToolRegistry&); }
	namespace SpecialTracks  { void RegisterAll(FMCPToolRegistry&); }

	void RegisterAll(FMCPToolRegistry& Registry)
	{
		Lifecycle::RegisterAll(Registry);
		Tracks::RegisterAll(Registry);
		SpecialTracks::RegisterAll(Registry);
	}
}
