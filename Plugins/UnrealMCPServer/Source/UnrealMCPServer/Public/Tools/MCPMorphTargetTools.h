// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMCPToolRegistry;

// v4.5 (UE 5.8) — Morph Target / blendshape family. Complements the expanded
// 5.8 Skeletal Editor morph-target tooling: list a mesh's morph targets and
// drive blendshape weights on a skeletal-mesh actor at edit time.
namespace MCPMorphTargetTools
{
	void RegisterAll(FMCPToolRegistry& Registry);
}
