// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMCPToolRegistry;

// v4.5 (UE 5.8) — Animation Mixer (Sequencer) family. The Animation Mixer lets
// cinematic artists layer/mask skeletal animation directly in Sequencer without a
// separate Anim Blueprint. Driven via the engine's MovieSceneAnimMixerScripting
// extensions on a UMovieSceneAnimationMixerTrack.
namespace MCPAnimMixerTools
{
	void RegisterAll(FMCPToolRegistry& Registry);
}
