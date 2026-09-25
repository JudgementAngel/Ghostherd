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

namespace MCPBlueprintTools::Functions
{

using namespace MCPBlueprintTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// add_function_graph - Create a new function in the Blueprint
	// ================================================================
	MCP_TOOL(Registry, "add_function_graph")
		.Description(TEXT("Create a new function graph in a Blueprint. Returns the function entry node ID. Use add_function_pin to add input/output parameters, and add nodes to build the function body."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("function_name"), TEXT("Name for the new function"), true)
		.EnumArg(TEXT("access"), TEXT("Access specifier (default: Public)"),
			{ TEXT("Public"), TEXT("Protected"), TEXT("Private") })
		.BoolArg(TEXT("pure"), TEXT("Whether the function is pure (no exec pins). Default: false"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, FunctionName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("function_name"), FunctionName)) return FMCPToolResult::Error(TEXT("function_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			// Check if function already exists
			for (UEdGraph* Graph : BP->FunctionGraphs)
			{
				if (Graph && Graph->GetName() == FunctionName)
					return FMCPToolResult::Error(FString::Printf(TEXT("Function '%s' already exists"), *FunctionName));
			}

			UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
				BP,
				FName(*FunctionName),
				UEdGraph::StaticClass(),
				UEdGraphSchema_K2::StaticClass()
			);

			if (!NewGraph)
				return FMCPToolResult::Error(TEXT("Failed to create function graph"));

			FBlueprintEditorUtils::AddFunctionGraph(BP, NewGraph, /*bIsUserCreated=*/true, static_cast<UClass*>(nullptr));

			// Configure function entry node (access specifier + pure flag)
			FString Access = TEXT("Public");
			Args->TryGetStringField(TEXT("access"), Access);
			bool bPure = false;
			Args->TryGetBoolField(TEXT("pure"), bPure);

			for (UEdGraphNode* Node : NewGraph->Nodes)
			{
				UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node);
				if (EntryNode)
				{
					if (Access == TEXT("Protected"))
						EntryNode->AddExtraFlags(FUNC_Protected);
					else if (Access == TEXT("Private"))
						EntryNode->AddExtraFlags(FUNC_Private);

					if (bPure)
					{
						EntryNode->AddExtraFlags(FUNC_BlueprintPure);
						EntryNode->ReconstructNode();
					}
					break;
				}
			}

			// Find entry node ID for return value
			FString EntryNodeId;
			for (UEdGraphNode* Node : NewGraph->Nodes)
			{
				UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node);
				if (EntryNode)
				{
					EntryNodeId = EntryNode->NodeGuid.ToString();
					break;
				}
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("functionName"), FunctionName);
			Result->SetStringField(TEXT("graphName"), NewGraph->GetName());
			Result->SetStringField(TEXT("entryNodeId"), EntryNodeId);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_function_pin - Add an input or output parameter to a function
	// ================================================================
	MCP_TOOL(Registry, "add_function_pin")
		.Description(TEXT("Add an input parameter or output return value to a Blueprint function. Input pins become function parameters; Output pins become return values. A FunctionResult node is automatically created if needed for outputs."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("function_name"), TEXT("Name of the function to modify"), true)
		.StringArg(TEXT("pin_name"), TEXT("Name for the new parameter"), true)
		.EnumArg(TEXT("pin_type"), TEXT("Type of the parameter"),
			{ TEXT("Boolean"), TEXT("Integer"), TEXT("Float"), TEXT("String"), TEXT("Vector"), TEXT("Rotator"), TEXT("Transform"), TEXT("Object"), TEXT("Name"), TEXT("Text"), TEXT("Byte") }, true)
		.EnumArg(TEXT("direction"), TEXT("Input = function parameter, Output = return value"),
			{ TEXT("Input"), TEXT("Output") }, true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, FunctionName, PinName, PinTypeStr, Direction;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("function_name"), FunctionName)) return FMCPToolResult::Error(TEXT("function_name required"));
			if (!Args->TryGetStringField(TEXT("pin_name"), PinName)) return FMCPToolResult::Error(TEXT("pin_name required"));
			if (!Args->TryGetStringField(TEXT("pin_type"), PinTypeStr)) return FMCPToolResult::Error(TEXT("pin_type required"));
			if (!Args->TryGetStringField(TEXT("direction"), Direction)) return FMCPToolResult::Error(TEXT("direction required"));

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

			FEdGraphPinType PinType = StringToPinType(PinTypeStr);
			if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
				return FMCPToolResult::Error(FString::Printf(TEXT("Unknown pin type: %s"), *PinTypeStr));

			if (Direction == TEXT("Input"))
			{
				// Find the entry node
				UK2Node_FunctionEntry* EntryNode = nullptr;
				for (UEdGraphNode* Node : FuncGraph->Nodes)
				{
					EntryNode = Cast<UK2Node_FunctionEntry>(Node);
					if (EntryNode) break;
				}
				if (!EntryNode) return FMCPToolResult::Error(TEXT("Function entry node not found"));

				// Entry node outputs = function inputs (confusing but correct)
				UEdGraphPin* NewPin = EntryNode->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Output);
				if (!NewPin) return FMCPToolResult::Error(TEXT("Failed to create input pin"));
			}
			else // Output
			{
				// Find or create result node
				UK2Node_FunctionResult* ResultNode = nullptr;
				for (UEdGraphNode* Node : FuncGraph->Nodes)
				{
					ResultNode = Cast<UK2Node_FunctionResult>(Node);
					if (ResultNode) break;
				}
				if (!ResultNode)
				{
					// Create result node
					ResultNode = NewObject<UK2Node_FunctionResult>(FuncGraph);
					ResultNode->CreateNewGuid();
					ResultNode->PostPlacedNewNode();
					ResultNode->AllocateDefaultPins();
					ResultNode->NodePosX = 600;
					ResultNode->NodePosY = 0;
					FuncGraph->AddNode(ResultNode, false, false);
				}

				// Result node inputs = function outputs
				UEdGraphPin* NewPin = ResultNode->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Input);
				if (!NewPin) return FMCPToolResult::Error(TEXT("Failed to create output pin"));
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Added %s pin '%s' (%s) to function '%s'"),
				*Direction, *PinName, *PinTypeStr, *FunctionName));
		});

	// ================================================================
	// add_function_return_node - Add a return node to a function
	// ================================================================
	MCP_TOOL(Registry, "add_function_return_node")
		.Description(TEXT("Add a FunctionResult (return) node to a function graph. Required for functions that return values. Use add_function_pin with direction 'Output' to add return value pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("function_name"), TEXT("Name of the function graph"), true)
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 600)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, FunctionName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("function_name"), FunctionName)) return FMCPToolResult::Error(TEXT("function_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UEdGraph* FuncGraph = nullptr;
			for (UEdGraph* Graph : BP->FunctionGraphs)
			{
				if (Graph && Graph->GetName() == FunctionName) { FuncGraph = Graph; break; }
			}
			if (!FuncGraph) return FMCPToolResult::Error(FString::Printf(TEXT("Function graph not found: %s"), *FunctionName));

			// Check if result node already exists
			for (UEdGraphNode* Node : FuncGraph->Nodes)
			{
				if (Cast<UK2Node_FunctionResult>(Node))
					return FMCPToolResult::Error(TEXT("Function already has a return node"));
			}

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 600;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;

			UK2Node_FunctionResult* ResultNode = NewObject<UK2Node_FunctionResult>(FuncGraph);
			ResultNode->CreateNewGuid();
			ResultNode->PostPlacedNewNode();
			ResultNode->AllocateDefaultPins();
			ResultNode->NodePosX = PosX;
			ResultNode->NodePosY = PosY;
			FuncGraph->AddNode(ResultNode, false, false);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Added return node to function '%s' (id: %s)"),
				*FunctionName, *ResultNode->NodeGuid.ToString()));
		});

	// ================================================================
	// list_class_functions - Discover callable functions on a class
	// ================================================================
	MCP_TOOL(Registry, "list_class_functions")
		.ReadOnly()
		.Description(TEXT("List all callable Blueprint functions on a UClass. Essential for discovering exact function names before using add_function_call_node. Returns function name, whether it's pure/static, return type, and optionally full parameter details. Use name_filter to narrow results (e.g., name_filter='Print' on KismetSystemLibrary)."))
		.StringArg(TEXT("class_name"), TEXT("UClass name to inspect (e.g., 'Actor', 'KismetSystemLibrary', 'Character', 'GameplayStatics'). Searches with A/U prefix fallbacks."), true)
		.StringArg(TEXT("name_filter"), TEXT("Filter functions by name (substring match, case-insensitive)"))
		.BoolArg(TEXT("include_parent_classes"), TEXT("Include inherited functions from parent classes (default: true)"))
		.BoolArg(TEXT("include_parameters"), TEXT("Include full parameter details for each function (default: false — set true for specific functions)"))
		.IntArg(TEXT("limit"), TEXT("Maximum results (default: 50, max: 200)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ClassName;
			if (!Args->TryGetStringField(TEXT("class_name"), ClassName))
				return FMCPToolResult::Error(TEXT("class_name is required"));

			UClass* Class = FindClassByName(ClassName);
			if (!Class)
				return FMCPToolResult::Error(FString::Printf(TEXT("Class not found: '%s'. Try the full name (e.g., 'KismetSystemLibrary', 'Actor', 'Character')."), *ClassName));

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			bool bIncludeParents = true;
			Args->TryGetBoolField(TEXT("include_parent_classes"), bIncludeParents);

			bool bIncludeParams = false;
			Args->TryGetBoolField(TEXT("include_parameters"), bIncludeParams);

			int32 Limit = 50;
			if (Args->HasField(TEXT("limit")))
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 200);

			TArray<TSharedPtr<FJsonValue>> FunctionArray;
			int32 TotalFound = 0;

			for (TFieldIterator<UFunction> It(Class, bIncludeParents ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				UFunction* Func = *It;
				if (!Func) continue;

				// Only show BlueprintCallable functions
				if (!Func->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
					continue;

				// Skip internal/hidden functions
				if (Func->HasMetaData(TEXT("BlueprintInternalUseOnly")))
					continue;

				FString FuncName = Func->GetName();

				// Apply name filter
				if (!NameFilter.IsEmpty() && !FuncName.Contains(NameFilter))
					continue;

				TotalFound++;
				if (FunctionArray.Num() >= Limit) continue;

				TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
				FuncObj->SetStringField(TEXT("name"), FuncName);
				FuncObj->SetStringField(TEXT("owning_class"), Func->GetOwnerClass()->GetName());
				FuncObj->SetBoolField(TEXT("is_pure"), Func->HasAnyFunctionFlags(FUNC_BlueprintPure));
				FuncObj->SetBoolField(TEXT("is_static"), Func->HasAnyFunctionFlags(FUNC_Static));
				FuncObj->SetBoolField(TEXT("is_const"), Func->HasAnyFunctionFlags(FUNC_Const));

				// Display name if different
				FString DisplayName = Func->GetMetaData(TEXT("DisplayName"));
				if (!DisplayName.IsEmpty() && DisplayName != FuncName)
				{
					FuncObj->SetStringField(TEXT("display_name"), DisplayName);
				}

				// Category
				FString Category = Func->GetMetaData(TEXT("Category"));
				if (!Category.IsEmpty())
				{
					FuncObj->SetStringField(TEXT("category"), Category);
				}

				// Return type
				FProperty* ReturnProp = Func->GetReturnProperty();
				if (ReturnProp)
				{
					FuncObj->SetStringField(TEXT("return_type"), ReturnProp->GetCPPType());
				}

				// Parameter count
				int32 ParamCount = 0;
				for (TFieldIterator<FProperty> PIt(Func); PIt; ++PIt)
				{
					if (!PIt->HasAnyPropertyFlags(CPF_ReturnParm))
						ParamCount++;
				}
				FuncObj->SetNumberField(TEXT("param_count"), ParamCount);

				// Full parameter details if requested
				if (bIncludeParams)
				{
					TArray<TSharedPtr<FJsonValue>> ParamsArray;
					for (TFieldIterator<FProperty> PIt(Func); PIt; ++PIt)
					{
						FProperty* Param = *PIt;
						TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
						ParamObj->SetStringField(TEXT("name"), Param->GetName());
						ParamObj->SetStringField(TEXT("type"), Param->GetCPPType());
						ParamObj->SetBoolField(TEXT("is_output"), Param->HasAnyPropertyFlags(CPF_OutParm));
						ParamObj->SetBoolField(TEXT("is_return"), Param->HasAnyPropertyFlags(CPF_ReturnParm));

						// Default value from metadata
						FString DefaultValue = Func->GetMetaData(*FString::Printf(TEXT("CPP_Default_%s"), *Param->GetName()));
						if (!DefaultValue.IsEmpty())
						{
							ParamObj->SetStringField(TEXT("default_value"), DefaultValue);
						}

						ParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
					}
					FuncObj->SetArrayField(TEXT("parameters"), ParamsArray);
				}

				FunctionArray.Add(MakeShared<FJsonValueObject>(FuncObj));
			}

			if (FunctionArray.Num() == 0)
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("No Blueprint-callable functions found on class '%s'%s."),
					*Class->GetName(),
					NameFilter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" matching '%s'"), *NameFilter)));
			}

			TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("class"), Class->GetName());
			Response->SetNumberField(TEXT("total_found"), TotalFound);
			Response->SetNumberField(TEXT("showing"), FunctionArray.Num());
			Response->SetArrayField(TEXT("functions"), FunctionArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Response), Response);
		});

	// ================================================================
	// get_function_signature - Get exact signature for a function
	// ================================================================
	MCP_TOOL(Registry, "get_function_signature")
		.ReadOnly()
		.Description(TEXT("Get the exact Blueprint node pin signature for a function. Returns all input and output pin names and types exactly as they will appear when using add_function_call_node + connect_pins. Use list_class_functions first to find the correct function name."))
		.StringArg(TEXT("class_name"), TEXT("UClass owning the function"), true)
		.StringArg(TEXT("function_name"), TEXT("Exact function name"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ClassName, FuncName;
			if (!Args->TryGetStringField(TEXT("class_name"), ClassName))
				return FMCPToolResult::Error(TEXT("class_name is required"));
			if (!Args->TryGetStringField(TEXT("function_name"), FuncName))
				return FMCPToolResult::Error(TEXT("function_name is required"));

			UClass* Class = FindClassByName(ClassName);
			if (!Class)
				return FMCPToolResult::Error(FString::Printf(TEXT("Class not found: '%s'"), *ClassName));

			UFunction* Func = Class->FindFunctionByName(FName(*FuncName));
			if (!Func)
			{
				// Try with K2_ prefix
				Func = Class->FindFunctionByName(FName(*FString::Printf(TEXT("K2_%s"), *FuncName)));
			}
			if (!Func)
			{
				// Suggest alternatives
				TArray<FString> Suggestions;
				for (TFieldIterator<UFunction> It(Class); It; ++It)
				{
					if (It->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
					{
						if (It->GetName().Contains(FuncName))
						{
							Suggestions.Add(It->GetName());
						}
					}
				}

				FString SuggestStr = Suggestions.Num() > 0
					? FString::Printf(TEXT(" Similar functions: %s"), *FString::Join(Suggestions, TEXT(", ")))
					: TEXT("");

				return FMCPToolResult::Error(FString::Printf(
					TEXT("Function '%s' not found on class '%s'.%s"),
					*FuncName, *Class->GetName(), *SuggestStr));
			}

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("function_name"), Func->GetName());
			Info->SetStringField(TEXT("class"), Class->GetName());
			Info->SetBoolField(TEXT("is_pure"), Func->HasAnyFunctionFlags(FUNC_BlueprintPure));
			Info->SetBoolField(TEXT("is_static"), Func->HasAnyFunctionFlags(FUNC_Static));
			Info->SetBoolField(TEXT("has_exec_pins"), !Func->HasAnyFunctionFlags(FUNC_BlueprintPure));

			// Build expected pin list
			TArray<TSharedPtr<FJsonValue>> InputPins;
			TArray<TSharedPtr<FJsonValue>> OutputPins;

			// Exec pins (if not pure)
			if (!Func->HasAnyFunctionFlags(FUNC_BlueprintPure))
			{
				TSharedPtr<FJsonObject> ExecIn = MakeShared<FJsonObject>();
				ExecIn->SetStringField(TEXT("name"), TEXT("execute"));
				ExecIn->SetStringField(TEXT("type"), TEXT("exec"));
				InputPins.Add(MakeShared<FJsonValueObject>(ExecIn));

				TSharedPtr<FJsonObject> ExecOut = MakeShared<FJsonObject>();
				ExecOut->SetStringField(TEXT("name"), TEXT("then"));
				ExecOut->SetStringField(TEXT("type"), TEXT("exec"));
				OutputPins.Add(MakeShared<FJsonValueObject>(ExecOut));
			}

			// Self pin (if not static)
			if (!Func->HasAnyFunctionFlags(FUNC_Static))
			{
				TSharedPtr<FJsonObject> SelfPin = MakeShared<FJsonObject>();
				SelfPin->SetStringField(TEXT("name"), TEXT("self"));
				SelfPin->SetStringField(TEXT("type"), Class->GetName());
				InputPins.Add(MakeShared<FJsonValueObject>(SelfPin));
			}

			// Parameters
			for (TFieldIterator<FProperty> PIt(Func); PIt; ++PIt)
			{
				FProperty* Param = *PIt;
				TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
				PinObj->SetStringField(TEXT("name"), Param->GetName());
				PinObj->SetStringField(TEXT("type"), Param->GetCPPType());

				FString DefaultValue = Func->GetMetaData(*FString::Printf(TEXT("CPP_Default_%s"), *Param->GetName()));
				if (!DefaultValue.IsEmpty())
				{
					PinObj->SetStringField(TEXT("default_value"), DefaultValue);
				}

				if (Param->HasAnyPropertyFlags(CPF_ReturnParm) || Param->HasAnyPropertyFlags(CPF_OutParm))
				{
					PinObj->SetBoolField(TEXT("is_return"), Param->HasAnyPropertyFlags(CPF_ReturnParm));
					OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
				}
				else
				{
					InputPins.Add(MakeShared<FJsonValueObject>(PinObj));
				}
			}

			Info->SetArrayField(TEXT("input_pins"), InputPins);
			Info->SetArrayField(TEXT("output_pins"), OutputPins);

			return FMCPToolResult::SuccessStructured(JsonToString(Info), Info);
		});
}

} // namespace MCPBlueprintTools::Functions
