// Copyright StraySpark Studio 2026. All Rights Reserved.


#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_SetFieldsInStruct.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Timeline.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_ExecutionSequence.h"
#include "Nodes/K2Node_CreateWidget.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/TimelineTemplate.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/BlueprintFactory.h"
#include "UObject/SavePackage.h"
#include "FileHelpers.h"
#include "EngineUtils.h"

// New includes for additional Blueprint graph tools
#include "K2Node_CallDelegate.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_Select.h"
#include "K2Node_MakeArray.h"
#include "K2Node_SwitchString.h"
#include "K2Node_SwitchEnum.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"
#include "K2Node_EnhancedInputAction.h"
#include "InputAction.h"
#include "InputTriggers.h"

#include "Tools/Blueprint/BlueprintCommon.h"

namespace MCPBlueprintTools::Graph
{

using namespace MCPBlueprintTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// connect_pins - Wire two node pins together
	// ================================================================
	MCP_TOOL(Registry, "connect_pins")
		.Description(TEXT("Wire two Blueprint node pins together. Connect an output pin on one node to an input pin on another. The schema handles type checking and will report errors for incompatible types. Use get_node_pins to discover available pin names."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph containing both nodes"), true)
		.StringArg(TEXT("source_node_id"), TEXT("GUID of the source node (from node creation or get_blueprint_info)"), true)
		.StringArg(TEXT("source_pin_name"), TEXT("Name of the output pin on the source node"), true)
		.StringArg(TEXT("target_node_id"), TEXT("GUID of the target node"), true)
		.StringArg(TEXT("target_pin_name"), TEXT("Name of the input pin on the target node"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, GraphName, SourceNodeId, SourcePinName, TargetNodeId, TargetPinName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("graph_name"), GraphName)) return FMCPToolResult::Error(TEXT("graph_name required"));
			if (!Args->TryGetStringField(TEXT("source_node_id"), SourceNodeId)) return FMCPToolResult::Error(TEXT("source_node_id required"));
			if (!Args->TryGetStringField(TEXT("source_pin_name"), SourcePinName)) return FMCPToolResult::Error(TEXT("source_pin_name required"));
			if (!Args->TryGetStringField(TEXT("target_node_id"), TargetNodeId)) return FMCPToolResult::Error(TEXT("target_node_id required"));
			if (!Args->TryGetStringField(TEXT("target_pin_name"), TargetPinName)) return FMCPToolResult::Error(TEXT("target_pin_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			UEdGraphNode* SourceNode = FindNodeByGuid(Graph, SourceNodeId);
			if (!SourceNode) return FMCPToolResult::Error(FString::Printf(TEXT("Source node not found: %s"), *SourceNodeId));

			UEdGraphNode* TargetNode = FindNodeByGuid(Graph, TargetNodeId);
			if (!TargetNode) return FMCPToolResult::Error(FString::Printf(TEXT("Target node not found: %s"), *TargetNodeId));

			// Find pins (search all pins, not just by direction, to be flexible)
			UEdGraphPin* SourcePin = nullptr;
			for (UEdGraphPin* Pin : SourceNode->Pins)
			{
				if (Pin && Pin->PinName.ToString() == SourcePinName)
				{
					SourcePin = Pin;
					break;
				}
			}
			if (!SourcePin) return FMCPToolResult::Error(FString::Printf(TEXT("Source pin not found: '%s' on node %s"), *SourcePinName, *SourceNodeId));

			UEdGraphPin* TargetPin = nullptr;
			for (UEdGraphPin* Pin : TargetNode->Pins)
			{
				if (Pin && Pin->PinName.ToString() == TargetPinName)
				{
					TargetPin = Pin;
					break;
				}
			}
			if (!TargetPin) return FMCPToolResult::Error(FString::Printf(TEXT("Target pin not found: '%s' on node %s"), *TargetPinName, *TargetNodeId));

			// v4 Phase 0 (bug fix): validate pin directions explicitly instead of
			// silently retrying with swapped arguments — the old fallback could
			// succeed in reverse and leave the agent believing A->B when the graph
			// got B->A. source must be the output pin, target the input; if the
			// caller swapped them we correct it and say so in the result.
			bool bSwappedToMatchDirections = false;
			if (SourcePin->Direction == EGPD_Input && TargetPin->Direction == EGPD_Output)
			{
				Swap(SourcePin, TargetPin);
				Swap(SourceNode, TargetNode);
				bSwappedToMatchDirections = true;
			}
			else if (SourcePin->Direction == TargetPin->Direction)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
					FString::Printf(TEXT("Cannot connect two %s pins ('%s' and '%s')."),
						SourcePin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"),
						*SourcePinName, *TargetPinName),
					TEXT("source_pin_name must name an output pin and target_pin_name an input pin. Use get_node_pins to inspect pin directions."));
			}

			// Ask the schema for its verdict first so failures carry the engine's
			// own reason (type mismatch, would-create-cycle, ...) instead of a
			// generic message.
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
			const FPinConnectionResponse Verdict = K2Schema->CanCreateConnection(SourcePin, TargetPin);
			if (Verdict.Response == CONNECT_RESPONSE_DISALLOW)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
					FString::Printf(TEXT("Cannot connect '%s' -> '%s': %s"),
						*SourcePin->PinName.ToString(), *TargetPin->PinName.ToString(),
						*Verdict.Message.ToString()),
					TEXT("Check pin types with get_node_pins; some type pairs need an explicit conversion or cast node."));
			}

			if (!K2Schema->TryCreateConnection(SourcePin, TargetPin))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to connect '%s.%s' -> '%s.%s' (schema rejected the connection)"),
					*SourceNodeId, *SourcePin->PinName.ToString(), *TargetNodeId, *TargetPin->PinName.ToString()));
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Connected '%s' on '%s' (output) -> '%s' on '%s' (input)%s"),
				*SourcePin->PinName.ToString(), *SourceNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
				*TargetPin->PinName.ToString(), *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
				bSwappedToMatchDirections
					? TEXT(" [note: source/target arguments were swapped to match pin directions]")
					: TEXT("")));
		});

	// ================================================================
	// disconnect_pin - Break all connections on a pin
	// ================================================================
	MCP_TOOL(Registry, "disconnect_pin")
		.Description(TEXT("Break all connections on a specific pin of a node. Use get_node_pins to see current connections before disconnecting."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph"), true)
		.StringArg(TEXT("node_id"), TEXT("GUID of the node"), true)
		.StringArg(TEXT("pin_name"), TEXT("Name of the pin to disconnect"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, GraphName, NodeId, PinName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("graph_name"), GraphName)) return FMCPToolResult::Error(TEXT("graph_name required"));
			if (!Args->TryGetStringField(TEXT("node_id"), NodeId)) return FMCPToolResult::Error(TEXT("node_id required"));
			if (!Args->TryGetStringField(TEXT("pin_name"), PinName)) return FMCPToolResult::Error(TEXT("pin_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			UEdGraphNode* Node = FindNodeByGuid(Graph, NodeId);
			if (!Node) return FMCPToolResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));

			UEdGraphPin* Pin = nullptr;
			for (UEdGraphPin* P : Node->Pins)
			{
				if (P && P->PinName.ToString() == PinName) { Pin = P; break; }
			}
			if (!Pin) return FMCPToolResult::Error(FString::Printf(TEXT("Pin not found: '%s'"), *PinName));

			int32 NumBroken = Pin->LinkedTo.Num();
			Pin->BreakAllPinLinks(true);

			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Disconnected pin '%s' (%d connections broken)"), *PinName, NumBroken));
		});

	// ================================================================
	// set_pin_default_value - Set a default/literal value on a pin
	// ================================================================
	MCP_TOOL(Registry, "set_pin_default_value")
		.Description(TEXT("Set the default (literal) value on an unconnected input pin. Use this to set parameter values like strings, numbers, booleans, vectors, etc. The value is parsed by the UE property system. For Vectors use 'X,Y,Z' format, for Rotators use 'P,Y,R' format."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph"), true)
		.StringArg(TEXT("node_id"), TEXT("GUID of the node"), true)
		.StringArg(TEXT("pin_name"), TEXT("Name of the pin to set the default value on"), true)
		.StringArg(TEXT("default_value"), TEXT("The default value as a string (e.g., 'Hello', '42', 'true', '1.0,2.0,3.0' for Vector)"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, GraphName, NodeId, PinName, DefaultValue;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("graph_name"), GraphName)) return FMCPToolResult::Error(TEXT("graph_name required"));
			if (!Args->TryGetStringField(TEXT("node_id"), NodeId)) return FMCPToolResult::Error(TEXT("node_id required"));
			if (!Args->TryGetStringField(TEXT("pin_name"), PinName)) return FMCPToolResult::Error(TEXT("pin_name required"));
			if (!Args->TryGetStringField(TEXT("default_value"), DefaultValue)) return FMCPToolResult::Error(TEXT("default_value required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			UEdGraphNode* Node = FindNodeByGuid(Graph, NodeId);
			if (!Node) return FMCPToolResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));

			UEdGraphPin* Pin = nullptr;
			for (UEdGraphPin* P : Node->Pins)
			{
				if (P && P->PinName.ToString() == PinName) { Pin = P; break; }
			}
			if (!Pin) return FMCPToolResult::Error(FString::Printf(TEXT("Pin not found: '%s'"), *PinName));

			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
			K2Schema->TrySetDefaultValue(*Pin, DefaultValue);

			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Set default value of '%s' = '%s'"), *PinName, *DefaultValue));
		});

	// ================================================================
	// get_node_pins - Get detailed pin info for nodes in a graph
	// ================================================================
	MCP_TOOL(Registry, "get_node_pins")
		.ReadOnly()
		.Description(TEXT("Get detailed pin information for nodes in a Blueprint graph. Returns pin names, directions (Input/Output), types, default values, and current connections. Essential for discovering pin names before using connect_pins. If node_id is omitted, returns all nodes with their pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph"), true)
		.StringArg(TEXT("node_id"), TEXT("GUID of a specific node (optional - if omitted, returns all nodes with pins)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, GraphName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("graph_name"), GraphName)) return FMCPToolResult::Error(TEXT("graph_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			FString NodeId;
			Args->TryGetStringField(TEXT("node_id"), NodeId);

			TArray<UEdGraphNode*> NodesToInspect;
			if (!NodeId.IsEmpty())
			{
				UEdGraphNode* Node = FindNodeByGuid(Graph, NodeId);
				if (!Node) return FMCPToolResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));
				NodesToInspect.Add(Node);
			}
			else
			{
				NodesToInspect = Graph->Nodes;
			}

			TArray<TSharedPtr<FJsonValue>> NodesArray;
			for (UEdGraphNode* Node : NodesToInspect)
			{
				if (!Node) continue;

				TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
				NodeObj->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
				NodeObj->SetStringField(TEXT("name"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
				NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
				NodeObj->SetNumberField(TEXT("posX"), Node->NodePosX);
				NodeObj->SetNumberField(TEXT("posY"), Node->NodePosY);

				TArray<TSharedPtr<FJsonValue>> PinsArray;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin) PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
				}
				NodeObj->SetArrayField(TEXT("pins"), PinsArray);

				NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("graphName"), GraphName);
			Result->SetArrayField(TEXT("nodes"), NodesArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// remove_node - Remove a node from a graph
	// ================================================================
	MCP_TOOL(Registry, "remove_node")
		.Description(TEXT("Remove a node from a Blueprint graph. All connections to/from the node will be broken. Cannot remove the function entry node of a function graph."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph"), true)
		.StringArg(TEXT("node_id"), TEXT("GUID of the node to remove"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, GraphName, NodeId;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("graph_name"), GraphName)) return FMCPToolResult::Error(TEXT("graph_name required"));
			if (!Args->TryGetStringField(TEXT("node_id"), NodeId)) return FMCPToolResult::Error(TEXT("node_id required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			UEdGraphNode* Node = FindNodeByGuid(Graph, NodeId);
			if (!Node) return FMCPToolResult::Error(FString::Printf(TEXT("Node not found: %s"), *NodeId));

			// Prevent removing function entry nodes
			if (Cast<UK2Node_FunctionEntry>(Node))
				return FMCPToolResult::Error(TEXT("Cannot remove the function entry node"));

			FString NodeName = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();

			// Break all pin connections
			Node->BreakAllNodeLinks();

			// Remove the node
			Graph->RemoveNode(Node);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Removed node '%s' from graph '%s'"), *NodeName, *GraphName));
		});
}

} // namespace MCPBlueprintTools::Graph
