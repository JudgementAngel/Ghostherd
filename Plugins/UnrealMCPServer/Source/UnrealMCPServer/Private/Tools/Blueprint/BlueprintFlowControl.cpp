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

namespace MCPBlueprintTools::FlowControl
{

using namespace MCPBlueprintTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// add_for_each_loop_node - Add a ForEachLoop macro node
	// ================================================================
	MCP_TOOL(Registry, "add_for_each_loop_node")
		.Description(TEXT("Add a ForEachLoop macro instance node. Iterates over an array with 'Array Element' and 'Array Index' outputs, plus 'Loop Body' and 'Completed' exec outputs. Set with_break=true for the variant with a Break input. Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.BoolArg(TEXT("with_break"), TEXT("If true, use ForEachLoopWithBreak instead of ForEachLoop (default: false)"))
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			bool bWithBreak = false;
			if (Args->HasField(TEXT("with_break")))
				bWithBreak = Args->GetBoolField(TEXT("with_break"));

			FString MacroName = bWithBreak ? TEXT("ForEachLoopWithBreak") : TEXT("ForEachLoop");

			// Load the StandardMacros blueprint containing ForEachLoop
			UBlueprint* MacroBP = LoadStandardMacrosBlueprint();
			if (!MacroBP) return FMCPToolResult::Error(TEXT("Could not locate the engine's StandardMacros blueprint (canonical path + asset-registry search both failed)."));

			UEdGraph* MacroGraph = nullptr;
			for (UEdGraph* G : MacroBP->MacroGraphs)
			{
				if (G && G->GetName() == MacroName)
				{
					MacroGraph = G;
					break;
				}
			}
			if (!MacroGraph) return FMCPToolResult::Error(FString::Printf(TEXT("Could not find macro: %s"), *MacroName));

			UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
			Node->SetMacroGraph(MacroGraph);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;
			Node->NodePosX = PosX;
			Node->NodePosY = PosY;

			Graph->AddNode(Node, false, false);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			Result->SetStringField(TEXT("macroName"), MacroName);
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_while_loop_node - Add a WhileLoop macro node
	// ================================================================
	MCP_TOOL(Registry, "add_while_loop_node")
		.Description(TEXT("Add a WhileLoop macro instance node. Has a 'Condition' boolean input, 'Loop Body' exec output (runs while condition is true), and 'Completed' exec output (runs when condition becomes false). Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			// Load the StandardMacros blueprint containing WhileLoop
			UBlueprint* MacroBP = LoadStandardMacrosBlueprint();
			if (!MacroBP) return FMCPToolResult::Error(TEXT("Could not locate the engine's StandardMacros blueprint (canonical path + asset-registry search both failed)."));

			UEdGraph* MacroGraph = nullptr;
			for (UEdGraph* G : MacroBP->MacroGraphs)
			{
				if (G && G->GetName() == TEXT("WhileLoop"))
				{
					MacroGraph = G;
					break;
				}
			}
			if (!MacroGraph) return FMCPToolResult::Error(TEXT("Could not find WhileLoop macro"));

			UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
			Node->SetMacroGraph(MacroGraph);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;
			Node->NodePosX = PosX;
			Node->NodePosY = PosY;

			Graph->AddNode(Node, false, false);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			Result->SetStringField(TEXT("macroName"), TEXT("WhileLoop"));
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_flow_control_node - Add flow control macro nodes (DoOnce, FlipFlop, Gate, MultiGate, DoN)
	// ================================================================
	MCP_TOOL(Registry, "add_flow_control_node")
		.Description(TEXT("Add a flow control macro node (DoOnce, FlipFlop, Gate, MultiGate, DoN). These are standard macro instances from the engine's StandardMacros library. Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.EnumArg(TEXT("flow_type"), TEXT("Type of flow control node"),
			{ TEXT("DoOnce"), TEXT("FlipFlop"), TEXT("Gate"), TEXT("MultiGate"), TEXT("DoN") }, true)
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, FlowType;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("flow_type"), FlowType)) return FMCPToolResult::Error(TEXT("flow_type required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			// Map flow_type to macro name in StandardMacros
			FString MacroName = FlowType;
			if (FlowType == TEXT("DoN"))
				MacroName = TEXT("Do N");

			// Load the StandardMacros blueprint
			UBlueprint* MacroBP = LoadStandardMacrosBlueprint();
			if (!MacroBP) return FMCPToolResult::Error(TEXT("Could not locate the engine's StandardMacros blueprint (canonical path + asset-registry search both failed)."));

			UEdGraph* MacroGraph = nullptr;
			for (UEdGraph* G : MacroBP->MacroGraphs)
			{
				if (G && G->GetName() == MacroName)
				{
					MacroGraph = G;
					break;
				}
			}
			if (!MacroGraph) return FMCPToolResult::Error(FString::Printf(TEXT("Could not find macro: %s"), *MacroName));

			UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
			Node->SetMacroGraph(MacroGraph);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;
			Node->NodePosX = PosX;
			Node->NodePosY = PosY;

			Graph->AddNode(Node, false, false);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			Result->SetStringField(TEXT("flowType"), FlowType);
			Result->SetStringField(TEXT("macroName"), MacroName);
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_select_node - Add a Select node
	// ================================================================
	MCP_TOOL(Registry, "add_select_node")
		.Description(TEXT("Add a Select node that picks one of several values based on an index or boolean input. Similar to a ternary operator. Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			UK2Node_Select* Node = NewObject<UK2Node_Select>(Graph);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;
			Node->NodePosX = PosX;
			Node->NodePosY = PosY;

			Graph->AddNode(Node, false, false);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_switch_on_string_node - Add a Switch on String node
	// ================================================================
	MCP_TOOL(Registry, "add_switch_on_string_node")
		.Description(TEXT("Add a Switch on String node. Has an exec input, a string 'Selection' input, a 'Default' exec output, and named case exec outputs. Returns node ID and all pin names."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph"), true)
		.StringArrayArg(TEXT("cases"), TEXT("JSON array of case string values (e.g., [\"Idle\",\"Walking\",\"Running\"])"))
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, GraphName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("graph_name"), GraphName)) return FMCPToolResult::Error(TEXT("graph_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;

			UK2Node_SwitchString* SwitchNode = NewObject<UK2Node_SwitchString>(Graph);

			// Populate PinNames before allocating pins so they are created with the node
			const TArray<TSharedPtr<FJsonValue>>* CasesArray = nullptr;
			if (Args->TryGetArrayField(TEXT("cases"), CasesArray) && CasesArray)
			{
				for (const TSharedPtr<FJsonValue>& CaseVal : *CasesArray)
				{
					FString CaseStr = CaseVal->AsString();
					if (!CaseStr.IsEmpty())
					{
						SwitchNode->PinNames.Add(FName(*CaseStr));
					}
				}
			}

			SwitchNode->CreateNewGuid();
			SwitchNode->PostPlacedNewNode();
			SwitchNode->AllocateDefaultPins();

			SwitchNode->NodePosX = PosX;
			SwitchNode->NodePosY = PosY;
			Graph->AddNode(SwitchNode, false, false);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), SwitchNode->NodeGuid.ToString());

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : SwitchNode->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_switch_on_enum_node - Add a Switch on Enum node
	// ================================================================
	MCP_TOOL(Registry, "add_switch_on_enum_node")
		.Description(TEXT("Add a Switch on Enum node. Creates exec output pins for each enum value plus a Default pin. Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph"), true)
		.StringArg(TEXT("enum_path"), TEXT("Content path to the enum asset (e.g., '/Game/Enums/E_Elements')"), true)
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, GraphName, EnumPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("graph_name"), GraphName)) return FMCPToolResult::Error(TEXT("graph_name required"));
			if (!Args->TryGetStringField(TEXT("enum_path"), EnumPath)) return FMCPToolResult::Error(TEXT("enum_path required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			// Load the enum
			UEnum* EnumAsset = LoadObject<UEnum>(nullptr, *EnumPath);
			if (!EnumAsset) return FMCPToolResult::Error(FString::Printf(TEXT("Enum not found: %s"), *EnumPath));

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;

			UK2Node_SwitchEnum* SwitchNode = NewObject<UK2Node_SwitchEnum>(Graph);
			SwitchNode->SetEnum(EnumAsset);
			SwitchNode->CreateNewGuid();
			SwitchNode->PostPlacedNewNode();
			SwitchNode->AllocateDefaultPins();

			SwitchNode->NodePosX = PosX;
			SwitchNode->NodePosY = PosY;
			Graph->AddNode(SwitchNode, false, false);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), SwitchNode->NodeGuid.ToString());
			Result->SetStringField(TEXT("enumName"), EnumAsset->GetName());

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : SwitchNode->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPBlueprintTools::FlowControl
