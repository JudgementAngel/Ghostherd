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

namespace MCPBlueprintTools::Nodes
{

using namespace MCPBlueprintTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// add_event_node - Add a built-in event (BeginPlay, Tick, etc.)
	// ================================================================
	MCP_TOOL(Registry, "add_event_node")
		.Description(TEXT("Add a built-in event node (BeginPlay, Tick, ActorBeginOverlap, ActorEndOverlap) to the event graph. Also accepts any UFunction name directly (e.g., 'ReceiveBeginPlay'). Returns the node ID for wiring."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.EnumArg(TEXT("event_name"), TEXT("Built-in event to add"),
			{ TEXT("BeginPlay"), TEXT("Tick"), TEXT("ActorBeginOverlap"), TEXT("ActorEndOverlap") }, true)
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, EventName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("event_name"), EventName)) return FMCPToolResult::Error(TEXT("event_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			if (BP->UbergraphPages.Num() == 0)
				return FMCPToolResult::Error(TEXT("Blueprint has no event graph"));

			UEdGraph* EventGraph = BP->UbergraphPages[0];

			// Map friendly names to internal UFunction names
			FName InternalName;
			UClass* EventClass = AActor::StaticClass();

			if (EventName == TEXT("BeginPlay")) InternalName = FName("ReceiveBeginPlay");
			else if (EventName == TEXT("Tick")) InternalName = FName("ReceiveTick");
			else if (EventName == TEXT("ActorBeginOverlap")) InternalName = FName("ReceiveActorBeginOverlap");
			else if (EventName == TEXT("ActorEndOverlap")) InternalName = FName("ReceiveActorEndOverlap");
			else if (EventName == TEXT("AnyDamage")) InternalName = FName("ReceiveAnyDamage");
			else if (EventName == TEXT("Hit")) InternalName = FName("ReceiveHit");
			else if (EventName == TEXT("Destroyed")) InternalName = FName("ReceiveDestroyed");
			else InternalName = FName(*EventName); // Accept raw function name

			// Verify the function exists
			UFunction* EventFunc = EventClass->FindFunctionByName(InternalName);
			if (!EventFunc)
			{
				// Try parent class
				if (BP->ParentClass)
					EventFunc = BP->ParentClass->FindFunctionByName(InternalName);
				if (EventFunc)
					EventClass = BP->ParentClass;
			}
			if (!EventFunc)
				return FMCPToolResult::Error(FString::Printf(TEXT("Event function not found: %s"), *InternalName.ToString()));

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;

			// v5: a new Blueprint already carries disabled template overrides (BeginPlay, Tick...).
			// Adding a second one compiles with "more than one function with the same name";
			// reuse and enable the existing override instead.
			if (UK2Node_Event* Existing = FBlueprintEditorUtils::FindOverrideForFunction(BP, EventClass, InternalName))
			{
				Existing->Modify();
				if (!Existing->IsNodeEnabled()) Existing->SetEnabledState(ENodeEnabledState::Enabled);
				Existing->NodePosX = PosX; Existing->NodePosY = PosY;
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
				return FMCPToolResult::Success(FString::Printf(TEXT("Reused existing event '%s' (id: %s)"),
					*EventName, *Existing->NodeGuid.ToString()));
			}
			// AddDefaultEventNode signature: (BP, Graph, FName, UClass*, int32& InOutNodePosY)
			UK2Node_Event* EventNode = FKismetEditorUtilities::AddDefaultEventNode(
				BP, EventGraph, InternalName, EventClass, PosY);

			if (!EventNode)
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to add event node '%s' (may already exist)"), *EventName));

			// Set X position manually (AddDefaultEventNode only handles Y)
			EventNode->NodePosX = PosX;

			return FMCPToolResult::Success(FString::Printf(TEXT("Added event '%s' (id: %s)"),
				*EventName, *EventNode->NodeGuid.ToString()));
		});

	// ================================================================
	// add_custom_event - Create a custom event node
	// ================================================================
	MCP_TOOL(Registry, "add_custom_event")
		.Description(TEXT("Add a custom event node to the event graph. Custom events can be called from other parts of the Blueprint. Returns the node ID."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("event_name"), TEXT("Name for the custom event"), true)
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, EventName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("event_name"), EventName)) return FMCPToolResult::Error(TEXT("event_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
			if (BP->UbergraphPages.Num() == 0) return FMCPToolResult::Error(TEXT("Blueprint has no event graph"));

			UEdGraph* EventGraph = BP->UbergraphPages[0];

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;

			// Reject duplicates up front — two custom events with the same
			// CustomFunctionName put the Blueprint into a compile-error state.
			for (UEdGraphNode* ExistingNode : EventGraph->Nodes)
			{
				if (UK2Node_CustomEvent* Existing = Cast<UK2Node_CustomEvent>(ExistingNode))
				{
					if (Existing->CustomFunctionName == FName(*EventName))
					{
						return FMCPToolResult::Error(FString::Printf(
							TEXT("A custom event named '%s' already exists in this graph (id: %s)"),
							*EventName, *Existing->NodeGuid.ToString()));
					}
				}
			}

			// Engine-canonical creation order (UBlueprintNodeSpawner::Invoke):
			// pins BEFORE PostPlacedNewNode — some K2 node overrides touch pins
			// via FindPinChecked and assert if they don't exist yet (UE 5.7
			// SpawnActorFromClass did exactly that).
			UK2Node_CustomEvent* CustomEventNode = NewObject<UK2Node_CustomEvent>(EventGraph);
			CustomEventNode->CustomFunctionName = FName(*EventName);
			CustomEventNode->CreateNewGuid();
			CustomEventNode->SetFlags(RF_Transactional);
			CustomEventNode->AllocateDefaultPins();
			CustomEventNode->PostPlacedNewNode();
			CustomEventNode->NodePosX = PosX;
			CustomEventNode->NodePosY = PosY;
			EventGraph->Modify(); // record into the active transaction so rollback restores the node list
			EventGraph->AddNode(CustomEventNode, false, false);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Added custom event '%s' (id: %s)"),
				*EventName, *CustomEventNode->NodeGuid.ToString()));
		});

	// ================================================================
	// add_function_call_node - Add a function call node to a graph
	// ================================================================
	MCP_TOOL(Registry, "add_function_call_node")
		.Description(TEXT("Add a function call node to a Blueprint graph. Specify the target class and function name. Common classes: KismetSystemLibrary (PrintString, Delay), KismetMathLibrary (math ops), GameplayStatics (GetPlayerController, SpawnActor), Actor (SetActorLocation). Use 'Self' for Blueprint's own functions. Returns node ID and pin names for wiring."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (e.g., 'EventGraph' or function name)"), true)
		.StringArg(TEXT("function_name"), TEXT("Name of the function to call (e.g., 'PrintString', 'K2_SetActorLocation', 'Delay')"), true)
		.StringArg(TEXT("target"), TEXT("Class owning the function (e.g., 'KismetSystemLibrary', 'Actor', 'GameplayStatics'). Use 'Self' for functions on this Blueprint. Required."), true)
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, GraphName, FunctionName, TargetStr;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("graph_name"), GraphName)) return FMCPToolResult::Error(TEXT("graph_name required"));
			if (!Args->TryGetStringField(TEXT("function_name"), FunctionName)) return FMCPToolResult::Error(TEXT("function_name required"));
			if (!Args->TryGetStringField(TEXT("target"), TargetStr)) return FMCPToolResult::Error(TEXT("target required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			// Find the target class
			UClass* TargetClass = nullptr;
			if (TargetStr.Equals(TEXT("Self"), ESearchCase::IgnoreCase) || TargetStr.IsEmpty())
			{
				TargetClass = BP->SkeletonGeneratedClass ? BP->SkeletonGeneratedClass : BP->ParentClass;
			}
			else
			{
				TargetClass = FindClassByName(TargetStr);
			}
			if (!TargetClass)
				return FMCPToolResult::Error(FString::Printf(TEXT("Target class not found: %s"), *TargetStr));

			// Find the function — try exact name, K2_ prefix, and hierarchy
			UFunction* Function = TargetClass->FindFunctionByName(FName(*FunctionName));
			if (!Function)
			{
				// Try with K2_ prefix (many Blueprint-exposed functions use this: GetActorTransform → K2_GetActorTransform)
				Function = TargetClass->FindFunctionByName(FName(*FString::Printf(TEXT("K2_%s"), *FunctionName)));
			}
			if (!Function)
			{
				// Search up the class hierarchy
				for (UClass* SearchClass = TargetClass->GetSuperClass(); SearchClass; SearchClass = SearchClass->GetSuperClass())
				{
					Function = SearchClass->FindFunctionByName(FName(*FunctionName));
					if (Function) break;
					// Also try K2_ prefix in hierarchy
					Function = SearchClass->FindFunctionByName(FName(*FString::Printf(TEXT("K2_%s"), *FunctionName)));
					if (Function) break;
				}
			}
			if (!Function)
			{
				// Last resort: iterate all functions looking for case-insensitive match
				FString LowerName = FunctionName.ToLower();
				for (TFieldIterator<UFunction> It(TargetClass); It; ++It)
				{
					FString ItName = It->GetName();
					if (ItName.ToLower() == LowerName || ItName.ToLower() == (TEXT("k2_") + LowerName))
					{
						Function = *It;
						break;
					}
				}
			}
			if (!Function)
				return FMCPToolResult::Error(FString::Printf(TEXT("Function '%s' not found on class '%s'. Try list_class_functions to discover available names. Many UE functions use K2_ prefix (e.g., 'K2_GetActorTransform' instead of 'GetActorTransform')."), *FunctionName, *TargetStr));

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;

			UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(Graph);
			CallNode->CreateNewGuid();
			CallNode->SetFromFunction(Function);
			CallNode->PostPlacedNewNode();
			CallNode->AllocateDefaultPins();
			CallNode->NodePosX = PosX;
			CallNode->NodePosY = PosY;
			Graph->AddNode(CallNode, false, false);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			// Build response with pin info
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), CallNode->NodeGuid.ToString());
			Result->SetStringField(TEXT("nodeName"), CallNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : CallNode->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_branch_node - Add a Branch (if/else) node
	// ================================================================
	MCP_TOOL(Registry, "add_branch_node")
		.Description(TEXT("Add a Branch (if/else) node. Has an exec input, a boolean 'Condition' input, and 'True'/'False' exec outputs. Returns node ID and pin names."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph"), true)
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

			UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(Graph);
			BranchNode->CreateNewGuid();
			BranchNode->PostPlacedNewNode();
			BranchNode->AllocateDefaultPins();
			BranchNode->NodePosX = PosX;
			BranchNode->NodePosY = PosY;
			Graph->AddNode(BranchNode, false, false);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), BranchNode->NodeGuid.ToString());

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : BranchNode->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_switch_on_int_node - Add a Switch on Integer node
	// ================================================================
	MCP_TOOL(Registry, "add_switch_on_int_node")
		.Description(TEXT("Add a Switch on Integer node. Has an exec input, an integer 'Selection' input, a 'Default' exec output, and numbered case exec outputs. Returns node ID and all pin names."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph"), true)
		.IntArg(TEXT("num_cases"), TEXT("Number of integer cases to create (default: 2)"))
		.IntArg(TEXT("start_index"), TEXT("Starting integer value for cases (default: 0)"))
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

			int32 NumCases = 2;
			if (Args->HasField(TEXT("num_cases")))
				NumCases = (int32)Args->GetNumberField(TEXT("num_cases"));
			NumCases = FMath::Clamp(NumCases, 1, 64);

			int32 StartIndex = 0;
			if (Args->HasField(TEXT("start_index")))
				StartIndex = (int32)Args->GetNumberField(TEXT("start_index"));

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;

			UK2Node_SwitchInteger* SwitchNode = NewObject<UK2Node_SwitchInteger>(Graph);
			SwitchNode->StartIndex = StartIndex;
			SwitchNode->CreateNewGuid();
			SwitchNode->PostPlacedNewNode();
			SwitchNode->AllocateDefaultPins();

			// Add case pins
			for (int32 i = 0; i < NumCases; ++i)
			{
				SwitchNode->AddPinToSwitchNode();
			}

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
	// add_make_struct_node - Create a "Make [Struct]" node
	// ================================================================
	MCP_TOOL(Registry, "add_make_struct_node")
		.Description(TEXT("Add a 'Make [Struct]' node that constructs a struct from individual member values. " "Works with any UScriptStruct: FVector, FRotator, FTransform, FLinearColor, FPostProcessSettings, FHitResult, etc. " "Returns node ID and all pins (one input per struct member + one struct output)."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("struct_type"), TEXT("Struct name (e.g., 'FVector', 'FPostProcessSettings', 'FLinearColor', 'FHitResult')"), true)
		.StringArg(TEXT("graph_name"), TEXT("Graph to add to (default: EventGraph)"))
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, StructType;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("struct_type"), StructType)) return FMCPToolResult::Error(TEXT("struct_type required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			UScriptStruct* Struct = FindStructByName(StructType);
			if (!Struct) return FMCPToolResult::Error(FString::Printf(TEXT("Struct not found: %s. Use full name with F prefix (e.g., 'FPostProcessSettings')."), *StructType));

			if (!UK2Node_MakeStruct::CanBeMade(Struct))
				return FMCPToolResult::Error(FString::Printf(TEXT("Struct '%s' cannot be used with Make node"), *Struct->GetName()));

			UK2Node_MakeStruct* Node = NewObject<UK2Node_MakeStruct>(Graph);
			Node->StructType = Struct;
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;
			Node->NodePosX = PosX;
			Node->NodePosY = PosY;

			Graph->AddNode(Node, false, false);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			// Build pin list for response
			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin->bHidden) PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			Result->SetStringField(TEXT("nodeTitle"), FString::Printf(TEXT("Make %s"), *Struct->GetName()));
			Result->SetStringField(TEXT("structType"), Struct->GetName());
			Result->SetArrayField(TEXT("pins"), PinsArray);

			FString ResultStr;
			auto Writer = TJsonWriterFactory<>::Create(&ResultStr);
			FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
			return FMCPToolResult::Success(ResultStr);
		});

	// ================================================================
	// add_break_struct_node - Create a "Break [Struct]" node
	// ================================================================
	MCP_TOOL(Registry, "add_break_struct_node")
		.Description(TEXT("Add a 'Break [Struct]' node that decomposes a struct into individual member output pins. " "Works with any UScriptStruct: FVector, FRotator, FTransform, FLinearColor, FPostProcessSettings, FHitResult, etc. " "Returns node ID and all pins (one struct input + one output per struct member)."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("struct_type"), TEXT("Struct name (e.g., 'FVector', 'FPostProcessSettings', 'FLinearColor', 'FHitResult')"), true)
		.StringArg(TEXT("graph_name"), TEXT("Graph to add to (default: EventGraph)"))
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, StructType;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("struct_type"), StructType)) return FMCPToolResult::Error(TEXT("struct_type required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			UScriptStruct* Struct = FindStructByName(StructType);
			if (!Struct) return FMCPToolResult::Error(FString::Printf(TEXT("Struct not found: %s. Use full name with F prefix (e.g., 'FPostProcessSettings')."), *StructType));

			UK2Node_BreakStruct* Node = NewObject<UK2Node_BreakStruct>(Graph);
			Node->StructType = Struct;
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
				if (!Pin->bHidden) PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			Result->SetStringField(TEXT("nodeTitle"), FString::Printf(TEXT("Break %s"), *Struct->GetName()));
			Result->SetStringField(TEXT("structType"), Struct->GetName());
			Result->SetArrayField(TEXT("pins"), PinsArray);

			FString ResultStr;
			auto Writer = TJsonWriterFactory<>::Create(&ResultStr);
			FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
			return FMCPToolResult::Success(ResultStr);
		});

	// ================================================================
	// add_set_struct_fields_node - Create a "Set Members in [Struct]" node
	// ================================================================
	MCP_TOOL(Registry, "add_set_struct_fields_node")
		.Description(TEXT("Add a 'Set Members in [Struct]' node that sets individual fields of a struct. " "Unlike Make, this takes an existing struct as input and selectively modifies fields. Has exec pins (not pure). " "Use this for FPostProcessSettings, FHitResult, and other complex structs. " "Returns node ID and all pins (exec in/out, struct in/out, one input per struct member)."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("struct_type"), TEXT("Struct name (e.g., 'FPostProcessSettings', 'FVector', 'FLinearColor')"), true)
		.StringArg(TEXT("graph_name"), TEXT("Graph to add to (default: EventGraph)"))
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, StructType;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("struct_type"), StructType)) return FMCPToolResult::Error(TEXT("struct_type required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			UScriptStruct* Struct = FindStructByName(StructType);
			if (!Struct) return FMCPToolResult::Error(FString::Printf(TEXT("Struct not found: %s. Use full name with F prefix (e.g., 'FPostProcessSettings')."), *StructType));

			UK2Node_SetFieldsInStruct* Node = NewObject<UK2Node_SetFieldsInStruct>(Graph);
			Node->StructType = Struct;
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
				if (!Pin->bHidden) PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			Result->SetStringField(TEXT("nodeTitle"), FString::Printf(TEXT("Set Members in %s"), *Struct->GetName()));
			Result->SetStringField(TEXT("structType"), Struct->GetName());
			Result->SetArrayField(TEXT("pins"), PinsArray);

			FString ResultStr;
			auto Writer = TJsonWriterFactory<>::Create(&ResultStr);
			FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
			return FMCPToolResult::Success(ResultStr);
		});

	// ================================================================
	// add_delay_node - Add a Delay node
	// ================================================================
	MCP_TOOL(Registry, "add_delay_node")
		.Description(TEXT("Add a Delay node (latent action). Has exec input/output and a 'Duration' float input. The output exec fires after the specified duration. Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.NumberArg(TEXT("duration"), TEXT("Delay duration in seconds (default: 1.0)"))
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

			// Find Delay function on UKismetSystemLibrary
			UClass* KismetClass = UKismetSystemLibrary::StaticClass();
			UFunction* DelayFunc = KismetClass->FindFunctionByName(FName(TEXT("Delay")));
			if (!DelayFunc) return FMCPToolResult::Error(TEXT("Could not find Delay function on KismetSystemLibrary"));

			UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
			Node->CreateNewGuid();
			Node->SetFromFunction(DelayFunc);
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();

			// Set Duration default value if provided
			double Duration = 1.0;
			if (Args->HasField(TEXT("duration")))
				Duration = Args->GetNumberField(TEXT("duration"));

			UEdGraphPin* DurationPin = Node->FindPin(TEXT("Duration"));
			if (DurationPin)
			{
				const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
				K2Schema->TrySetDefaultValue(*DurationPin, FString::SanitizeFloat(Duration));
			}

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
			Result->SetStringField(TEXT("nodeName"), TEXT("Delay"));
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_timeline_node - Add a Timeline node
	// ================================================================
	MCP_TOOL(Registry, "add_timeline_node")
		.Description(TEXT("Add a Timeline node to a Blueprint graph. Timelines allow you to animate float/vector/color values over time with keyframes. " "Has Play, PlayFromStart, Stop, Reverse exec inputs, Update/Finished/Direction exec outputs, and a float output for each track. " "Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.StringArg(TEXT("timeline_name"), TEXT("Name for the timeline"), true)
		.NumberArg(TEXT("length"), TEXT("Timeline length in seconds (default: 5.0)"))
		.BoolArg(TEXT("auto_play"), TEXT("Whether the timeline auto-plays on BeginPlay (default: false)"))
		.BoolArg(TEXT("loop"), TEXT("Whether the timeline loops (default: false)"))
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));

			FString TimelineName;
			if (!Args->TryGetStringField(TEXT("timeline_name"), TimelineName)) return FMCPToolResult::Error(TEXT("timeline_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			double Length = 5.0;
			if (Args->HasField(TEXT("length")))
				Length = Args->GetNumberField(TEXT("length"));

			bool bAutoPlay = false;
			if (Args->HasField(TEXT("auto_play")))
				bAutoPlay = Args->GetBoolField(TEXT("auto_play"));

			bool bLoop = false;
			if (Args->HasField(TEXT("loop")))
				bLoop = Args->GetBoolField(TEXT("loop"));

			UK2Node_Timeline* Node = NewObject<UK2Node_Timeline>(Graph);
			Node->TimelineName = FName(*TimelineName);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;
			Node->NodePosX = PosX;
			Node->NodePosY = PosY;

			Graph->AddNode(Node, false, false);

			// Add the timeline template to the Blueprint and configure it
			UTimelineTemplate* NewTimeline = NewObject<UTimelineTemplate>(BP, FName(*TimelineName));
			NewTimeline->TimelineLength = Length;
			NewTimeline->bAutoPlay = bAutoPlay;
			NewTimeline->bLoop = bLoop;
			BP->Timelines.Add(NewTimeline);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			Result->SetStringField(TEXT("timelineName"), TimelineName);
			Result->SetNumberField(TEXT("length"), Length);
			Result->SetBoolField(TEXT("autoPlay"), bAutoPlay);
			Result->SetBoolField(TEXT("loop"), bLoop);
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_cast_node - Add a Cast (dynamic cast) node
	// ================================================================
	MCP_TOOL(Registry, "add_cast_node")
		.Description(TEXT("Add a Cast To node (dynamic cast). Has an Object input, exec input, success/fail exec outputs, and a typed output pin for the cast result. " "Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.StringArg(TEXT("target_class"), TEXT("Class name to cast to (e.g., 'Character', 'Pawn', 'MyBlueprintClass')"), true)
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));

			FString TargetClassName;
			if (!Args->TryGetStringField(TEXT("target_class"), TargetClassName)) return FMCPToolResult::Error(TEXT("target_class required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			UClass* TargetClass = FindClassByName(TargetClassName);
			if (!TargetClass) return FMCPToolResult::Error(FString::Printf(TEXT("Target class not found: %s"), *TargetClassName));

			UK2Node_DynamicCast* Node = NewObject<UK2Node_DynamicCast>(Graph);
			Node->TargetType = TargetClass;
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
			Result->SetStringField(TEXT("targetClass"), TargetClass->GetName());
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_spawn_actor_node - Add a SpawnActorFromClass node
	// ================================================================
	MCP_TOOL(Registry, "add_spawn_actor_node")
		.Description(TEXT("Add a SpawnActorFromClass node. Has a Class input, SpawnTransform input, exec input/output, and a return value pin for the spawned actor. " "Optionally pre-fills the actor class. Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.StringArg(TEXT("actor_class"), TEXT("Optional: Actor class name to pre-fill (e.g., 'StaticMeshActor', 'PointLight')"))
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

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;

			// Do NOT use FGraphNodeCreator here. In UE 5.7,
			// UK2Node_SpawnActorFromClass::PostPlacedNewNode() reads the ScaleMethod
			// pin via FindPinChecked(), and FGraphNodeCreator::Finalize() runs
			// PostPlacedNewNode() BEFORE AllocateDefaultPins() — zero pins at that
			// point → check(Result) assert (EdGraphNode.h:586) → editor crash.
			// Mirror UBlueprintNodeSpawner::Invoke() instead: pins first, then
			// PostPlacedNewNode, then add to graph.
			UK2Node_SpawnActorFromClass* Node = NewObject<UK2Node_SpawnActorFromClass>(Graph);
			Node->CreateNewGuid();
			Node->SetFlags(RF_Transactional);
			Node->AllocateDefaultPins();
			Node->PostPlacedNewNode();
			Node->NodePosX = PosX;
			Node->NodePosY = PosY;
			Graph->Modify(); // record into the active transaction so rollback restores the node list
			Graph->AddNode(Node, false, false);

			// If actor_class is provided, set it now that pins exist, then reconstruct
			FString ActorClassName;
			if (Args->TryGetStringField(TEXT("actor_class"), ActorClassName) && !ActorClassName.IsEmpty())
			{
				UClass* ActorClass = FindClassByName(ActorClassName);
				if (ActorClass && ActorClass->IsChildOf(AActor::StaticClass()))
				{
					UEdGraphPin* ClassPin = Node->GetClassPin();
					if (ClassPin)
					{
						ClassPin->DefaultObject = ActorClass;
						Node->ReconstructNode();
					}
				}
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			Result->SetStringField(TEXT("nodeName"), TEXT("SpawnActorFromClass"));
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_get_all_actors_of_class_node - Add GetAllActorsOfClass node
	// ================================================================
	MCP_TOOL(Registry, "add_get_all_actors_of_class_node")
		.Description(TEXT("Add a GetAllActorsOfClass function call node. Returns an array of all actors of the specified class in the world. " "Has a WorldContextObject input, ActorClass input, and OutActors array output. Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.StringArg(TEXT("actor_class"), TEXT("Optional: Actor class name to pre-fill on the class pin (e.g., 'StaticMeshActor')"))
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

			// Find GetAllActorsOfClass function on UGameplayStatics
			UClass* GameplayStaticsClass = UGameplayStatics::StaticClass();
			UFunction* GetAllActorsFunc = GameplayStaticsClass->FindFunctionByName(FName(TEXT("GetAllActorsOfClass")));
			if (!GetAllActorsFunc) return FMCPToolResult::Error(TEXT("Could not find GetAllActorsOfClass function on GameplayStatics"));

			UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
			Node->CreateNewGuid();
			Node->SetFromFunction(GetAllActorsFunc);
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();

			// If actor_class provided, set the class pin default
			FString ActorClassName;
			if (Args->TryGetStringField(TEXT("actor_class"), ActorClassName) && !ActorClassName.IsEmpty())
			{
				UClass* ActorClass = FindClassByName(ActorClassName);
				if (ActorClass && ActorClass->IsChildOf(AActor::StaticClass()))
				{
					UEdGraphPin* ClassPin = Node->FindPin(TEXT("ActorClass"));
					if (ClassPin)
					{
						ClassPin->DefaultObject = ActorClass;
					}
				}
			}

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
			Result->SetStringField(TEXT("nodeName"), TEXT("GetAllActorsOfClass"));
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_sequence_node - Add an Execution Sequence node
	// ================================================================
	MCP_TOOL(Registry, "add_sequence_node")
		.Description(TEXT("Add a Sequence node that executes multiple output pins in order. Has an exec input and numbered 'Then' exec outputs (Then_0, Then_1, ...). " "Useful for organizing sequential logic. Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.IntArg(TEXT("num_outputs"), TEXT("Number of 'Then' output exec pins (default: 2, max: 64)"))
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

			int32 NumOutputs = 2;
			if (Args->HasField(TEXT("num_outputs")))
				NumOutputs = (int32)Args->GetNumberField(TEXT("num_outputs"));
			NumOutputs = FMath::Clamp(NumOutputs, 2, 64);

			UK2Node_ExecutionSequence* Node = NewObject<UK2Node_ExecutionSequence>(Graph);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();

			// Default allocation gives 2 output pins; add more if needed
			for (int32 i = 2; i < NumOutputs; ++i)
			{
				Node->AddInputPin();
			}

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
			Result->SetNumberField(TEXT("numOutputs"), NumOutputs);
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_make_array_node - Add a Make Array node
	// ================================================================
	MCP_TOOL(Registry, "add_make_array_node")
		.Description(TEXT("Add a Make Array node that constructs an array from individual elements. Connect inputs to define array contents. Returns node ID and all pins."))
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

			UK2Node_MakeArray* Node = NewObject<UK2Node_MakeArray>(Graph);
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
	// add_create_widget_node - Add a CreateWidget node (K2Node_CreateWidget)
	// ================================================================
	MCP_TOOL(Registry, "add_create_widget_node")
		.Description(TEXT("Add a CreateWidget node to a Blueprint graph. Creates a UMG widget instance at runtime. " "Has a Class input (set via widget_class), Owning Player input, exec input/output, and a Return Value pin (the created widget). " "Use with AddToViewport to display the widget. Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.StringArg(TEXT("widget_class"), TEXT("Content path of the Widget Blueprint to create (e.g., '/Game/UI/WBP_HUD')"))
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

			int32 PosX = Args->HasField(TEXT("node_x")) ? (int32)Args->GetNumberField(TEXT("node_x")) : 0;
			int32 PosY = Args->HasField(TEXT("node_y")) ? (int32)Args->GetNumberField(TEXT("node_y")) : 0;

			// Use FGraphNodeCreator for safe node creation (same pattern as SpawnActorFromClass)
			FGraphNodeCreator<UK2Node_CreateWidget> NodeCreator(*Graph);
			UK2Node_CreateWidget* Node = NodeCreator.CreateNode();
			Node->NodePosX = PosX;
			Node->NodePosY = PosY;
			NodeCreator.Finalize();

			// If widget_class is provided, set it after finalize then reconstruct
			FString WidgetClassName;
			if (Args->TryGetStringField(TEXT("widget_class"), WidgetClassName) && !WidgetClassName.IsEmpty())
			{
				UClass* WidgetClass = FindClassByName(WidgetClassName);
				if (WidgetClass && WidgetClass->IsChildOf(UUserWidget::StaticClass()))
				{
					UEdGraphPin* ClassPin = Node->GetClassPin();
					if (ClassPin)
					{
						ClassPin->DefaultObject = WidgetClass;
						Node->ReconstructNode();
					}
				}
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			Result->SetStringField(TEXT("nodeName"), TEXT("CreateWidget"));
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPBlueprintTools::Nodes
