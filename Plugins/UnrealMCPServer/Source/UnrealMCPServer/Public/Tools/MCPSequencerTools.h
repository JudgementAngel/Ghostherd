// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMCPToolRegistry;

namespace MCPSequencerTools
{
	/** Lifecycle, tracks and special tracks (registered under the Sequencer category). */
	void RegisterAll(FMCPToolRegistry& Registry);

	/** v4.6 — skeletal animation tracks, sections and the bake/link round trip.
	 *  Registered separately under its own category so it can be toggled apart
	 *  from the rest of Sequencer. */
	namespace AnimationTracks { void RegisterAll(FMCPToolRegistry& Registry); }
}
