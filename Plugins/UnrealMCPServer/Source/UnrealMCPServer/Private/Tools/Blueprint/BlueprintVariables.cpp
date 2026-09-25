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

namespace MCPBlueprintTools::Variables
{

using namespace MCPBlueprintTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// add_variable - Add a variable to a Blueprint
	// ================================================================
	MCP_TOOL(Registry, "add_variable")
		.Description(TEXT("Add a new variable to a Blueprint with specified type and properties."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("variable_name"), TEXT("Name of the new variable"), true)
		.EnumArg(TEXT("variable_type"), TEXT("Type of the variable"),
			{ TEXT("Boolean"), TEXT("Integer"), TEXT("Float"), TEXT("String"), TEXT("Vector"), TEXT("Rotator"), TEXT("Transform"), TEXT("Object"), TEXT("Class"), TEXT("Name"), TEXT("Text") }, true)
		.BoolArg(TEXT("instance_editable"), TEXT("Whether the variable is editable per-instance (default: true)"))
		.StringArg(TEXT("category"), TEXT("Category for grouping in the details panel"))
		.StringArg(TEXT("default_value"), TEXT("Default value as a string"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, VarName, VarTypeStr;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("variable_name"), VarName)) return FMCPToolResult::Error(TEXT("variable_name required"));
			if (!Args->TryGetStringField(TEXT("variable_type"), VarTypeStr)) return FMCPToolResult::Error(TEXT("variable_type required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FEdGraphPinType PinType = StringToPinType(VarTypeStr);
			if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
				return FMCPToolResult::Error(FString::Printf(TEXT("Unknown variable type: %s"), *VarTypeStr));

			// v4 Phase 0 (bug fix): distinguish duplicate-name from other failures —
			// AddMemberVariable returns a bare false either way, leaving agents guessing.
			if (FBlueprintEditorUtils::FindNewVariableIndex(BP, FName(*VarName)) != INDEX_NONE)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::AlreadyExists,
					FString::Printf(TEXT("Variable '%s' already exists on Blueprint '%s'."), *VarName, *BP->GetName()),
					TEXT("Pick a different name, or use the existing variable (get_blueprint_info lists variables)."));
			}

			bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);
			if (!bSuccess)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Failed to add variable '%s' (engine rejected the name or type)."), *VarName),
					TEXT("Variable names must be valid identifiers and not collide with inherited members or functions."));
			}

			bool bEditable = true;
			Args->TryGetBoolField(TEXT("instance_editable"), bEditable);
			if (bEditable)
			{
				FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(BP, FName(*VarName), false);
			}

			FString Category;
			if (Args->TryGetStringField(TEXT("category"), Category))
			{
				FBlueprintEditorUtils::SetBlueprintVariableCategory(BP, FName(*VarName), nullptr, FText::FromString(Category));
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Added variable '%s' (%s) to Blueprint '%s'"),
				*VarName, *VarTypeStr, *BP->GetName()));
		});

	// ================================================================
	// add_variable_get_node - Add a variable getter node (self or external class)
	// ================================================================
	MCP_TOOL(Registry, "add_variable_get_node")
		.Description(TEXT("Add a variable getter (Get) node to a Blueprint graph. For self variables, omit target_class. For EXTERNAL class variables (e.g., getting a variable from BP_GameInstance or another Blueprint), provide the target_class name or content path. Returns node ID and output pin name for wiring."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (e.g., 'EventGraph' or function name)"), true)
		.StringArg(TEXT("variable_name"), TEXT("Name of the variable to get"), true)
		.StringArg(TEXT("target_class"), TEXT("Optional: Class that owns the variable. Use 'Self' or omit for Blueprint's own variables. For external variables, provide class name (e.g., 'BP_GameInstance', 'GameplayStatics', 'CharacterMovementComponent'). Also accepts content paths like '/Game/BP/BP_GameInstance'."))
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, GraphName, VarName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("graph_name"), GraphName)) return FMCPToolResult::Error(TEXT("graph_name required"));
			if (!Args->TryGetStringField(TEXT("variable_name"), VarName)) return FMCPToolResult::Error(TEXT("variable_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;

			// Determine if self or external variable
			FString TargetClassStr;
			Args->TryGetStringField(TEXT("target_class"), TargetClassStr);
			bool bIsSelf = TargetClassStr.IsEmpty() || TargetClassStr.Equals(TEXT("Self"), ESearchCase::IgnoreCase);

			UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(Graph);

			if (bIsSelf)
			{
				GetNode->VariableReference.SetSelfMember(FName(*VarName));
			}
			else
			{
				// External class variable — try loading as Blueprint first (content path), then as C++ class
				UClass* ExternalClass = nullptr;

				// Try as Blueprint content path (e.g., /Game/BP/BP_GameInstance)
				if (TargetClassStr.StartsWith(TEXT("/")))
				{
					UBlueprint* ExtBP = FindBlueprint(TargetClassStr);
					if (ExtBP)
					{
						ExternalClass = ExtBP->SkeletonGeneratedClass ? ExtBP->SkeletonGeneratedClass : ExtBP->GeneratedClass;
					}
				}

				// Try as class name
				if (!ExternalClass)
				{
					ExternalClass = FindClassByName(TargetClassStr);
				}

				if (!ExternalClass)
				{
					return FMCPToolResult::Error(FString::Printf(
						TEXT("Target class not found: %s. Provide a class name (e.g., 'GameplayStatics') or Blueprint content path (e.g., '/Game/BP/BP_MyBlueprint')."),
						*TargetClassStr));
				}

				// Verify the property exists on the external class
				FProperty* Prop = ExternalClass->FindPropertyByName(FName(*VarName));
				if (!Prop)
				{
					// List available properties for helpful error
					TArray<FString> PropNames;
					for (TFieldIterator<FProperty> It(ExternalClass); It; ++It)
					{
						if (!(It->HasAnyPropertyFlags(CPF_Deprecated)))
							PropNames.Add(It->GetName());
					}
					FString Suggestions = PropNames.Num() > 0
						? FString::Printf(TEXT(" Available properties: %s"), *FString::Join(PropNames, TEXT(", ")))
						: TEXT("");
					return FMCPToolResult::Error(FString::Printf(
						TEXT("Variable '%s' not found on class '%s'.%s"),
						*VarName, *ExternalClass->GetName(), *Suggestions));
				}

				GetNode->VariableReference.SetExternalMember(FName(*VarName), ExternalClass);
			}

			GetNode->CreateNewGuid();
			GetNode->PostPlacedNewNode();
			GetNode->AllocateDefaultPins();
			GetNode->NodePosX = PosX;
			GetNode->NodePosY = PosY;
			Graph->AddNode(GetNode, false, false);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), GetNode->NodeGuid.ToString());
			Result->SetBoolField(TEXT("is_external"), !bIsSelf);

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : GetNode->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_variable_set_node - Add a variable setter node (self or external class)
	// ================================================================
	MCP_TOOL(Registry, "add_variable_set_node")
		.Description(TEXT("Add a variable setter (Set) node to a Blueprint graph. Has exec input/output pins and a value input pin. For EXTERNAL class variables, provide target_class. Returns node ID and pin names for wiring."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (e.g., 'EventGraph' or function name)"), true)
		.StringArg(TEXT("variable_name"), TEXT("Name of the variable to set"), true)
		.StringArg(TEXT("target_class"), TEXT("Optional: Class that owns the variable. Use 'Self' or omit for Blueprint's own variables. For external variables, provide class name or content path (e.g., '/Game/BP/BP_GameInstance')."))
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, GraphName, VarName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("graph_name"), GraphName)) return FMCPToolResult::Error(TEXT("graph_name required"));
			if (!Args->TryGetStringField(TEXT("variable_name"), VarName)) return FMCPToolResult::Error(TEXT("variable_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;

			// Determine if self or external variable
			FString TargetClassStr;
			Args->TryGetStringField(TEXT("target_class"), TargetClassStr);
			bool bIsSelf = TargetClassStr.IsEmpty() || TargetClassStr.Equals(TEXT("Self"), ESearchCase::IgnoreCase);

			UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(Graph);

			if (bIsSelf)
			{
				SetNode->VariableReference.SetSelfMember(FName(*VarName));
			}
			else
			{
				// External class variable
				UClass* ExternalClass = nullptr;

				if (TargetClassStr.StartsWith(TEXT("/")))
				{
					UBlueprint* ExtBP = FindBlueprint(TargetClassStr);
					if (ExtBP)
						ExternalClass = ExtBP->SkeletonGeneratedClass ? ExtBP->SkeletonGeneratedClass : ExtBP->GeneratedClass;
				}

				if (!ExternalClass)
					ExternalClass = FindClassByName(TargetClassStr);

				if (!ExternalClass)
				{
					return FMCPToolResult::Error(FString::Printf(
						TEXT("Target class not found: %s. Provide a class name or Blueprint content path."),
						*TargetClassStr));
				}

				FProperty* Prop = ExternalClass->FindPropertyByName(FName(*VarName));
				if (!Prop)
				{
					TArray<FString> PropNames;
					for (TFieldIterator<FProperty> It(ExternalClass); It; ++It)
					{
						if (!(It->HasAnyPropertyFlags(CPF_Deprecated)))
							PropNames.Add(It->GetName());
					}
					FString Suggestions = PropNames.Num() > 0
						? FString::Printf(TEXT(" Available properties: %s"), *FString::Join(PropNames, TEXT(", ")))
						: TEXT("");
					return FMCPToolResult::Error(FString::Printf(
						TEXT("Variable '%s' not found on class '%s'.%s"),
						*VarName, *ExternalClass->GetName(), *Suggestions));
				}

				SetNode->VariableReference.SetExternalMember(FName(*VarName), ExternalClass);
			}

			SetNode->CreateNewGuid();
			SetNode->PostPlacedNewNode();
			SetNode->AllocateDefaultPins();
			SetNode->NodePosX = PosX;
			SetNode->NodePosY = PosY;
			Graph->AddNode(SetNode, false, false);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), SetNode->NodeGuid.ToString());
			Result->SetBoolField(TEXT("is_external"), !bIsSelf);

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : SetNode->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_local_variable - Add a local variable to a function graph
	// ================================================================
	MCP_TOOL(Registry, "add_local_variable")
		.Description(TEXT("Add a local variable to a function graph. Local variables are scoped to the function and not visible outside of it."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("function_name"), TEXT("Name of the function to add the local variable to"), true)
		.StringArg(TEXT("variable_name"), TEXT("Name of the new local variable"), true)
		.EnumArg(TEXT("variable_type"), TEXT("Type of the variable"),
			{ TEXT("Boolean"), TEXT("Integer"), TEXT("Float"), TEXT("String"), TEXT("Vector"), TEXT("Rotator"), TEXT("Transform"), TEXT("Object"), TEXT("Name"), TEXT("Text") }, true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, FunctionName, VarName, VarTypeStr;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("function_name"), FunctionName)) return FMCPToolResult::Error(TEXT("function_name required"));
			if (!Args->TryGetStringField(TEXT("variable_name"), VarName)) return FMCPToolResult::Error(TEXT("variable_name required"));
			if (!Args->TryGetStringField(TEXT("variable_type"), VarTypeStr)) return FMCPToolResult::Error(TEXT("variable_type required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			// Find the function graph
			UEdGraph* FuncGraph = nullptr;
			for (UEdGraph* Graph : BP->FunctionGraphs)
			{
				if (Graph && Graph->GetName() == FunctionName)
				{
					FuncGraph = Graph;
					break;
				}
			}
			if (!FuncGraph) return FMCPToolResult::Error(FString::Printf(TEXT("Function graph not found: %s"), *FunctionName));

			FEdGraphPinType PinType = StringToPinType(VarTypeStr);
			if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
				return FMCPToolResult::Error(FString::Printf(TEXT("Unknown variable type: %s"), *VarTypeStr));

			bool bAdded = FBlueprintEditorUtils::AddLocalVariable(BP, FuncGraph, FName(*VarName), PinType);
			if (!bAdded)
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to add local variable '%s' to function '%s'"), *VarName, *FunctionName));

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Added local variable '%s' (%s) to function '%s'"),
				*VarName, *VarTypeStr, *FunctionName));
		});
}

} // namespace MCPBlueprintTools::Variables
