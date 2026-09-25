// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.6 — the animation authoring tool families.
//
//   Montage        montage_* : sections, slots, segments, blend settings, validation
//   AnimData       anim_*    : notifies, notify states, notify tracks, curves, sync markers
//   AnimGraphNodes animgraph_* + state machine completion : node authoring, state
//                  animations, transition rules, compilation
//
// These are registered as separate categories so each can be toggled (and
// costed) independently in the plugin settings.

#pragma once

#include "CoreMinimal.h"

class FMCPToolRegistry;

namespace MCPAnimTools
{
	namespace Montage        { void RegisterAll(FMCPToolRegistry& Registry); }
	namespace AnimData       { void RegisterAll(FMCPToolRegistry& Registry); }
	namespace AnimGraphNodes { void RegisterAll(FMCPToolRegistry& Registry); }
	namespace Validation     { void RegisterAll(FMCPToolRegistry& Registry); }   // v5 increment 17
}
