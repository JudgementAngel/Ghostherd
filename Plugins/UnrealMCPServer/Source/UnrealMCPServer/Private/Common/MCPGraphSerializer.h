// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class FJsonObject;

/**
 * v4 Phase 1 — single source of truth for graph -> JSON serialization.
 *
 * Works on any UEdGraph (Blueprint event/function graphs, AnimGraphs,
 * Material graphs use their own expression model and are handled elsewhere).
 * Powers describe_graph (Phase 2) and replaces the per-tool ad-hoc pin JSON.
 */
namespace MCPCommon
{
	/** Pin -> {name, direction, type, subType?, defaultValue?, isHidden, isConnected, connections[]} */
	TSharedPtr<FJsonObject> PinToJson(const UEdGraphPin* Pin);

	/** Node -> {nodeId, type, title, x, y, enabled, isPure?, comment?, pins[]?} */
	TSharedPtr<FJsonObject> NodeToJson(const UEdGraphNode* Node, bool bIncludePins = true,
		bool bIncludeHiddenPins = false);

	/** Whole graph -> {name, nodeCount, nodes[], connections[]}.
	 *  connections[] is the deduplicated edge list {fromNode, fromPin, toNode, toPin}
	 *  (output->input direction), so agents don't have to reconstruct topology
	 *  from per-pin link lists. */
	TSharedPtr<FJsonObject> GraphToJson(const UEdGraph* Graph, bool bIncludePins = true,
		bool bIncludeHiddenPins = false);
}
