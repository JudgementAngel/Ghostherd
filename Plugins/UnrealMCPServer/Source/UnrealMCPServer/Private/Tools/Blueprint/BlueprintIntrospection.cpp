// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4 Phase 2 — closed-loop graph editing: read back, verify, and edit what was
// created. Before these tools, agents could place nodes but never see the
// whole graph again (N x get_node_pins round-trips), couldn't move/delete
// nodes, and couldn't trace execution flow to verify their own work.

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Common/MCPGraphSerializer.h"

#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphNode_Comment.h"
#include "K2Node.h"
#include "K2Node_Knot.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "ScopedTransaction.h"

#include "Tools/Blueprint/BlueprintCommon.h"

namespace MCPBlueprintTools::Introspection
{

using namespace MCPBlueprintTools::Common;

namespace
{
	/** Shared preamble: load BP + graph or produce the structured error. */
	bool ResolveGraph(const TSharedPtr<FJsonObject>& Args, UBlueprint*& OutBP, UEdGraph*& OutGraph,
		FMCPToolResult& OutError)
	{
		FString AssetPath, GraphName = TEXT("EventGraph");
		if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
		{
			OutError = FMCPToolResult::Error(TEXT("asset_path required"));
			return false;
		}
		Args->TryGetStringField(TEXT("graph_name"), GraphName);

		OutBP = FindBlueprint(AssetPath);
		if (!OutBP)
		{
			OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
			return false;
		}
		OutGraph = FindGraphInBlueprint(OutBP, GraphName);
		if (!OutGraph)
		{
			TArray<FString> GraphNames;
			for (UEdGraph* G : OutBP->UbergraphPages)   { if (G) GraphNames.Add(G->GetName()); }
			for (UEdGraph* G : OutBP->FunctionGraphs)   { if (G) GraphNames.Add(G->GetName()); }
			OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Graph '%s' not found in %s"), *GraphName, *AssetPath),
				TEXT("Available graphs are listed in did_you_mean."), GraphNames);
			return false;
		}
		return true;
	}

	bool IsExecPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// describe_graph — the whole topology in one call
	// ================================================================
	MCP_TOOL(Registry, "describe_graph")
		.Description(TEXT("Dump a Blueprint graph's full topology in one call: every node (id, type, title, position, pins) plus a deduplicated directed connection list. Use this to verify your edits instead of calling get_node_pins per node."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Graph name (default: EventGraph)"))
		.BoolArg(TEXT("include_pins"), TEXT("Include per-node pin details (default true). Set false for a compact node+edges view."))
		.BoolArg(TEXT("include_hidden_pins"), TEXT("Include hidden pins (default false)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Blueprints/BP_Door\", \"graph_name\": \"EventGraph\"}"))
		.OutputSchema(TEXT(R"({
			"type": "object",
			"properties": {
				"name": {"type": "string"},
				"blueprint": {"type": "string"},
				"compile_status": {"type": "string", "enum": ["UpToDate", "UpToDateWithWarnings", "Error", "Dirty", "Unknown"]},
				"nodeCount": {"type": "integer"},
				"nodes": {"type": "array", "items": {"type": "object", "properties": {
					"nodeId": {"type": "string"}, "type": {"type": "string"}, "title": {"type": "string"},
					"x": {"type": "number"}, "y": {"type": "number"}, "enabled": {"type": "boolean"},
					"pins": {"type": "array"}}}},
				"connections": {"type": "array", "items": {"type": "object", "properties": {
					"fromNode": {"type": "string"}, "fromPin": {"type": "string"},
					"toNode": {"type": "string"}, "toPin": {"type": "string"}}}}
			},
			"required": ["name", "nodeCount", "nodes", "connections"]
		})"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UBlueprint* BP = nullptr; UEdGraph* Graph = nullptr; FMCPToolResult Err;
			if (!ResolveGraph(Args, BP, Graph, Err)) { return Err; }

			bool bIncludePins = true, bIncludeHidden = false;
			Args->TryGetBoolField(TEXT("include_pins"), bIncludePins);
			Args->TryGetBoolField(TEXT("include_hidden_pins"), bIncludeHidden);

			TSharedPtr<FJsonObject> GraphJson = MCPCommon::GraphToJson(Graph, bIncludePins, bIncludeHidden);
			GraphJson->SetStringField(TEXT("blueprint"), BP->GetPathName());
			GraphJson->SetStringField(TEXT("compile_status"),
				BP->Status == BS_UpToDate ? TEXT("UpToDate") :
				BP->Status == BS_UpToDateWithWarnings ? TEXT("UpToDateWithWarnings") :
				BP->Status == BS_Error ? TEXT("Error") :
				BP->Status == BS_Dirty ? TEXT("Dirty") : TEXT("Unknown"));

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Graph '%s': %d nodes."), *Graph->GetName(), Graph->Nodes.Num()),
				GraphJson);
		});

	// ================================================================
	// move_node — reposition without recreate
	// ================================================================
	MCP_TOOL(Registry, "move_node")
		.Description(TEXT("Move an existing node to a new graph position. Layout-only; no pins or connections change."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Graph name (default: EventGraph)"))
		.StringArg(TEXT("node_id"), TEXT("GUID of the node"), true)
		.NumberArg(TEXT("x"), TEXT("New X position"), true)
		.NumberArg(TEXT("y"), TEXT("New Y position"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UBlueprint* BP = nullptr; UEdGraph* Graph = nullptr; FMCPToolResult Err;
			if (!ResolveGraph(Args, BP, Graph, Err)) { return Err; }

			FString NodeId;
			Args->TryGetStringField(TEXT("node_id"), NodeId);
			UEdGraphNode* Node = FindNodeByGuid(Graph, NodeId);
			if (!Node)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Node not found: %s"), *NodeId));
			}

			const FScopedTransaction Txn(NSLOCTEXT("UnrealMCP", "MoveNode", "MCP: Move Node"));
			Node->Modify();
			Node->NodePosX = (int32)Args->GetNumberField(TEXT("x"));
			Node->NodePosY = (int32)Args->GetNumberField(TEXT("y"));
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Moved node '%s' to (%d, %d)"),
				*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), Node->NodePosX, Node->NodePosY));
		});

	// ================================================================
	// delete_nodes — batch removal with protection
	// ================================================================
	MCP_TOOL(Registry, "delete_nodes")
		.Description(TEXT("Delete one or more nodes from a graph (all-or-nothing). Function entry/result nodes and other protected nodes are refused with a reason. Connections to deleted nodes are broken cleanly."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Graph name (default: EventGraph)"))
		.StringArrayArg(TEXT("node_ids"), TEXT("GUIDs of the nodes to delete"), true)
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UBlueprint* BP = nullptr; UEdGraph* Graph = nullptr; FMCPToolResult Err;
			if (!ResolveGraph(Args, BP, Graph, Err)) { return Err; }

			const TArray<TSharedPtr<FJsonValue>>* Ids = nullptr;
			if (!Args->TryGetArrayField(TEXT("node_ids"), Ids) || Ids->Num() == 0)
			{
				return FMCPToolResult::Error(TEXT("node_ids array is required"));
			}

			// Pre-validate everything before mutating (all-or-nothing).
			TArray<UEdGraphNode*> ToDelete;
			TArray<FString> Problems;
			for (const TSharedPtr<FJsonValue>& IdVal : *Ids)
			{
				FString Id;
				if (!IdVal->TryGetString(Id)) { continue; }
				UEdGraphNode* Node = FindNodeByGuid(Graph, Id);
				if (!Node)
				{
					Problems.Add(FString::Printf(TEXT("%s: not found"), *Id));
				}
				else if (Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_FunctionResult>())
				{
					Problems.Add(FString::Printf(TEXT("%s: protected (function entry/result nodes cannot be deleted)"), *Id));
				}
				else if (!Node->CanUserDeleteNode())
				{
					Problems.Add(FString::Printf(TEXT("%s: protected ('%s' refuses deletion)"), *Id,
						*Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
				}
				else
				{
					ToDelete.Add(Node);
				}
			}
			if (Problems.Num() > 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
					FString::Printf(TEXT("Refusing to delete: %d problem(s). Nothing was deleted.\n%s"),
						Problems.Num(), *FString::Join(Problems, TEXT("\n"))));
			}

			const FScopedTransaction Txn(NSLOCTEXT("UnrealMCP", "DeleteNodes", "MCP: Delete Nodes"));
			for (UEdGraphNode* Node : ToDelete)
			{
				FBlueprintEditorUtils::RemoveNode(BP, Node, /*bDontRecompile*/ true);
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Deleted %d node(s) from '%s'."),
				ToDelete.Num(), *Graph->GetName()));
		});

	// ================================================================
	// add_comment_node / add_reroute_node — organizational nodes
	// ================================================================
	MCP_TOOL(Registry, "add_comment_node")
		.Description(TEXT("Add a comment box to a graph for organization. Position/size it to visually group related nodes."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Graph name (default: EventGraph)"))
		.StringArg(TEXT("text"), TEXT("Comment text"), true)
		.NumberArg(TEXT("x"), TEXT("X position (default 0)"))
		.NumberArg(TEXT("y"), TEXT("Y position (default 0)"))
		.NumberArg(TEXT("width"), TEXT("Box width (default 400)"))
		.NumberArg(TEXT("height"), TEXT("Box height (default 300)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UBlueprint* BP = nullptr; UEdGraph* Graph = nullptr; FMCPToolResult Err;
			if (!ResolveGraph(Args, BP, Graph, Err)) { return Err; }

			FString Text;
			Args->TryGetStringField(TEXT("text"), Text);

			const FScopedTransaction Txn(NSLOCTEXT("UnrealMCP", "AddComment", "MCP: Add Comment"));
			UEdGraphNode_Comment* Node = NewObject<UEdGraphNode_Comment>(Graph);
			Node->NodeComment = Text;
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->NodePosX = Args->HasField(TEXT("x")) ? (int32)Args->GetNumberField(TEXT("x")) : 0;
			Node->NodePosY = Args->HasField(TEXT("y")) ? (int32)Args->GetNumberField(TEXT("y")) : 0;
			Node->NodeWidth = Args->HasField(TEXT("width")) ? (int32)Args->GetNumberField(TEXT("width")) : 400;
			Node->NodeHeight = Args->HasField(TEXT("height")) ? (int32)Args->GetNumberField(TEXT("height")) : 300;
			Graph->AddNode(Node, false, false);
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Added comment box '%s'."), *Text), Out);
		});

	MCP_TOOL(Registry, "add_reroute_node")
		.Description(TEXT("Add a reroute (knot) node for tidying long wires. Connect it like any node: one input, one output, type adapts to what you wire in."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Graph name (default: EventGraph)"))
		.NumberArg(TEXT("x"), TEXT("X position (default 0)"))
		.NumberArg(TEXT("y"), TEXT("Y position (default 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UBlueprint* BP = nullptr; UEdGraph* Graph = nullptr; FMCPToolResult Err;
			if (!ResolveGraph(Args, BP, Graph, Err)) { return Err; }

			const FScopedTransaction Txn(NSLOCTEXT("UnrealMCP", "AddReroute", "MCP: Add Reroute"));
			UK2Node_Knot* Node = NewObject<UK2Node_Knot>(Graph);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();
			Node->NodePosX = Args->HasField(TEXT("x")) ? (int32)Args->GetNumberField(TEXT("x")) : 0;
			Node->NodePosY = Args->HasField(TEXT("y")) ? (int32)Args->GetNumberField(TEXT("y")) : 0;
			Graph->AddNode(Node, false, false);
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			TArray<TSharedPtr<FJsonValue>> Pins;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin) Pins.Add(MakeShared<FJsonValueObject>(MCPCommon::PinToJson(Pin)));
			}
			Out->SetArrayField(TEXT("pins"), Pins);
			return FMCPToolResult::SuccessStructured(TEXT("Added reroute node."), Out);
		});

	// ================================================================
	// set_node_comment — annotate existing nodes
	// ================================================================
	MCP_TOOL(Registry, "set_node_comment")
		.Description(TEXT("Set or clear the comment bubble on an existing node."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Graph name (default: EventGraph)"))
		.StringArg(TEXT("node_id"), TEXT("GUID of the node"), true)
		.StringArg(TEXT("comment"), TEXT("Comment text (empty clears)"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UBlueprint* BP = nullptr; UEdGraph* Graph = nullptr; FMCPToolResult Err;
			if (!ResolveGraph(Args, BP, Graph, Err)) { return Err; }

			FString NodeId, Comment;
			Args->TryGetStringField(TEXT("node_id"), NodeId);
			Args->TryGetStringField(TEXT("comment"), Comment);

			UEdGraphNode* Node = FindNodeByGuid(Graph, NodeId);
			if (!Node)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Node not found: %s"), *NodeId));
			}

			const FScopedTransaction Txn(NSLOCTEXT("UnrealMCP", "SetNodeComment", "MCP: Set Node Comment"));
			Node->Modify();
			Node->NodeComment = Comment;
			Node->bCommentBubbleVisible = !Comment.IsEmpty();
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

			return FMCPToolResult::Success(Comment.IsEmpty()
				? TEXT("Cleared node comment.")
				: FString::Printf(TEXT("Set node comment to '%s'."), *Comment));
		});

	// ================================================================
	// get_execution_paths — verify control flow
	// ================================================================
	MCP_TOOL(Registry, "get_execution_paths")
		.Description(TEXT("Trace execution flow from each event/entry node by following exec pins. Returns the chains of nodes each event reaches — use to verify wiring (e.g. 'does BeginPlay reach SetActorHidden?')."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Graph name (default: EventGraph)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UBlueprint* BP = nullptr; UEdGraph* Graph = nullptr; FMCPToolResult Err;
			if (!ResolveGraph(Args, BP, Graph, Err)) { return Err; }

			constexpr int32 MaxDepth = 100;
			constexpr int32 MaxPaths = 50;

			TArray<TSharedPtr<FJsonValue>> PathsArr;

			// DFS from each entry node along exec output pins; branches fork paths.
			TFunction<void(UEdGraphNode*, TArray<UEdGraphNode*>&)> Walk =
				[&](UEdGraphNode* Node, TArray<UEdGraphNode*>& Stack)
			{
				if (PathsArr.Num() >= MaxPaths || Stack.Num() >= MaxDepth || Stack.Contains(Node))
				{
					return;
				}
				Stack.Push(Node);

				bool bHasOutgoing = false;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (!IsExecPin(Pin) || Pin->Direction != EGPD_Output) { continue; }
					for (UEdGraphPin* Linked : Pin->LinkedTo)
					{
						if (Linked && Linked->GetOwningNode())
						{
							bHasOutgoing = true;
							Walk(Linked->GetOwningNode(), Stack);
						}
					}
				}

				if (!bHasOutgoing && Stack.Num() > 1 && PathsArr.Num() < MaxPaths)
				{
					// Leaf: record the full chain.
					TArray<TSharedPtr<FJsonValue>> Chain;
					for (UEdGraphNode* N : Stack)
					{
						TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
						Entry->SetStringField(TEXT("nodeId"), N->NodeGuid.ToString());
						Entry->SetStringField(TEXT("title"), N->GetNodeTitle(ENodeTitleType::ListView).ToString());
						Chain.Add(MakeShared<FJsonValueObject>(Entry));
					}
					TSharedPtr<FJsonObject> PathObj = MakeShared<FJsonObject>();
					PathObj->SetArrayField(TEXT("nodes"), Chain);
					PathsArr.Add(MakeShared<FJsonValueObject>(PathObj));
				}

				Stack.Pop();
			};

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && (Node->IsA<UK2Node_Event>() || Node->IsA<UK2Node_FunctionEntry>()))
				{
					TArray<UEdGraphNode*> Stack;
					Walk(Node, Stack);
				}
			}

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetNumberField(TEXT("path_count"), PathsArr.Num());
			Out->SetArrayField(TEXT("paths"), PathsArr);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d execution path(s) in '%s'."), PathsArr.Num(), *Graph->GetName()),
				Out);
		});

	// ================================================================
	// find_orphaned_nodes — cleanup helper
	// ================================================================
	MCP_TOOL(Registry, "find_orphaned_nodes")
		.Description(TEXT("Find nodes with no connections at all (excluding comments). Candidates for delete_nodes cleanup."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Graph name (default: EventGraph)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UBlueprint* BP = nullptr; UEdGraph* Graph = nullptr; FMCPToolResult Err;
			if (!ResolveGraph(Args, BP, Graph, Err)) { return Err; }

			TArray<TSharedPtr<FJsonValue>> Orphans;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node || Node->IsA<UEdGraphNode_Comment>()) { continue; }
				bool bConnected = false;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin && Pin->LinkedTo.Num() > 0) { bConnected = true; break; }
				}
				if (!bConnected)
				{
					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
					Entry->SetStringField(TEXT("type"), Node->GetClass()->GetName());
					Entry->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
					Orphans.Add(MakeShared<FJsonValueObject>(Entry));
				}
			}

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetNumberField(TEXT("count"), Orphans.Num());
			Out->SetArrayField(TEXT("orphans"), Orphans);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d orphaned node(s) in '%s'."), Orphans.Num(), *Graph->GetName()),
				Out);
		});

	// ================================================================
	// list_node_types — discoverability of what can be created
	// ================================================================
	MCP_TOOL(Registry, "list_node_types")
		.Description(TEXT("List the concrete K2 node classes available in this engine (via reflection), optionally filtered by name substring. Note: creation tools exist for the most common types (see search_tools query 'add node'); others can often be created via add_function_call_node or execute_python."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("filter"), TEXT("Optional name substring filter (e.g. 'Switch')"))
		.IntArg(TEXT("limit"), TEXT("Max results (default 100)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Filter;
			Args->TryGetStringField(TEXT("filter"), Filter);
			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 500);
			}

			TArray<TSharedPtr<FJsonValue>> Types;
			int32 Total = 0;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Class = *It;
				if (!Class->IsChildOf(UK2Node::StaticClass())
					|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
				{
					continue;
				}
				const FString ClassName = Class->GetName();
				if (!Filter.IsEmpty() && !ClassName.Contains(Filter))
				{
					continue;
				}
				Total++;
				if (Types.Num() < Limit)
				{
					Types.Add(MakeShared<FJsonValueString>(ClassName));
				}
			}

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetNumberField(TEXT("total"), Total);
			Out->SetArrayField(TEXT("types"), Types);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d K2 node type(s)%s."), Total,
					Filter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" matching '%s'"), *Filter)),
				Out);
		});
}

} // namespace MCPBlueprintTools::Introspection
