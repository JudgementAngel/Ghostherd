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

namespace MCPBlueprintTools::Input
{

using namespace MCPBlueprintTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// add_input_action_event - Add Enhanced Input Action event node
	// ================================================================
	MCP_TOOL(Registry, "add_input_action_event")
		.Description(TEXT("Add an Enhanced Input Action event node to a Blueprint graph. This creates an event that fires when the specified InputAction is triggered. Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.StringArg(TEXT("action_path"), TEXT("Content path to the InputAction asset (e.g., '/Game/Input/IA_Jump')"), true)
		.EnumArg(TEXT("trigger_event"), TEXT("Which trigger event to respond to (default: Triggered)"),
			{ TEXT("Started"), TEXT("Triggered"), TEXT("Completed"), TEXT("Canceled"), TEXT("Ongoing") })
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, ActionPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("action_path"), ActionPath)) return FMCPToolResult::Error(TEXT("action_path required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			// Load the InputAction asset
			UInputAction* InputAction = LoadObject<UInputAction>(nullptr, *ActionPath);
			if (!InputAction) return FMCPToolResult::Error(FString::Printf(TEXT("InputAction not found: %s"), *ActionPath));

			// Determine the trigger event type
			FString TriggerEventStr = TEXT("Triggered");
			Args->TryGetStringField(TEXT("trigger_event"), TriggerEventStr);

			ETriggerEvent TriggerEvent = ETriggerEvent::Triggered;
			if (TriggerEventStr == TEXT("Started")) TriggerEvent = ETriggerEvent::Started;
			else if (TriggerEventStr == TEXT("Completed")) TriggerEvent = ETriggerEvent::Completed;
			else if (TriggerEventStr == TEXT("Canceled")) TriggerEvent = ETriggerEvent::Canceled;
			else if (TriggerEventStr == TEXT("Ongoing")) TriggerEvent = ETriggerEvent::Ongoing;

			UK2Node_EnhancedInputAction* Node = NewObject<UK2Node_EnhancedInputAction>(Graph);
			Node->InputAction = InputAction;
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
			Result->SetStringField(TEXT("actionName"), InputAction->GetName());
			Result->SetStringField(TEXT("triggerEvent"), TriggerEventStr);
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPBlueprintTools::Input
