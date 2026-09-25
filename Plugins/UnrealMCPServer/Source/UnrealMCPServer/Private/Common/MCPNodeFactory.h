// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"

class UBlueprint;

/**
 * v4 Phase 1 — single implementation of the K2-node creation sequence that was
 * copy-pasted 55+ times across the Blueprint tool files:
 *
 *   NewObject -> [configure] -> CreateNewGuid -> PostPlacedNewNode ->
 *   AllocateDefaultPins -> position -> AddNode -> MarkStructurallyModified
 *
 * The PreInit hook runs BEFORE PostPlacedNewNode/AllocateDefaultPins — that's
 * where node identity must be configured (SetMacroGraph, SetFromFunction,
 * TargetType, etc.) so the right pins get allocated.
 */
namespace MCPCommon
{
	template <typename TNodeType>
	TNodeType* CreateK2Node(UBlueprint* BP, UEdGraph* Graph, int32 PosX, int32 PosY,
		TFunctionRef<void(TNodeType*)> PreInit)
	{
		TNodeType* Node = NewObject<TNodeType>(Graph);
		PreInit(Node);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Graph->AddNode(Node, /*bFromUI*/ false, /*bSelectNewNode*/ false);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		return Node;
	}

	/** Overload for nodes that need no pre-pin configuration. */
	template <typename TNodeType>
	TNodeType* CreateK2Node(UBlueprint* BP, UEdGraph* Graph, int32 PosX, int32 PosY)
	{
		return CreateK2Node<TNodeType>(BP, Graph, PosX, PosY, [](TNodeType*) {});
	}
}
