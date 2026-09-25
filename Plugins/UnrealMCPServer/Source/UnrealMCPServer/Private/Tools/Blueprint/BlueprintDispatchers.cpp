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
#include "Common/MCPAssetCreate.h"

namespace MCPBlueprintTools::Dispatchers
{

using namespace MCPBlueprintTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// add_event_dispatcher - Add an event dispatcher to a Blueprint
	// ================================================================
	MCP_TOOL(Registry, "add_event_dispatcher")
		.Description(TEXT("Add an event dispatcher (multicast delegate) variable to a Blueprint. Event dispatchers allow Blueprints to broadcast events that other Blueprints can bind to."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("dispatcher_name"), TEXT("Name for the new event dispatcher"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, DispatcherName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("dispatcher_name"), DispatcherName)) return FMCPToolResult::Error(TEXT("dispatcher_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			// Create a multicast delegate pin type for the event dispatcher
			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
			PinType.PinSubCategory = NAME_None;
			PinType.PinSubCategoryMemberReference.MemberParent = BP->SkeletonGeneratedClass;
			PinType.PinSubCategoryMemberReference.MemberName = FName(*DispatcherName);

			bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(BP, FName(*DispatcherName), PinType);
			if (!bSuccess)
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to add event dispatcher '%s'"), *DispatcherName));
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Added event dispatcher '%s' to Blueprint '%s'"),
				*DispatcherName, *BP->GetName()));
		});

	// ================================================================
	// add_call_dispatcher_node - Add a Call node for an event dispatcher
	// ================================================================
	MCP_TOOL(Registry, "add_call_dispatcher_node")
		.Description(TEXT("Add a 'Call' node for an event dispatcher. This broadcasts the event to all bound listeners. Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.StringArg(TEXT("dispatcher_name"), TEXT("Name of the event dispatcher to call"), true)
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, DispatcherName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("dispatcher_name"), DispatcherName)) return FMCPToolResult::Error(TEXT("dispatcher_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			UK2Node_CallDelegate* Node = NewObject<UK2Node_CallDelegate>(Graph);
			Node->DelegateReference.SetSelfMember(FName(*DispatcherName));
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
			Result->SetStringField(TEXT("dispatcherName"), DispatcherName);
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_bind_dispatcher_node - Add a Bind node for an event dispatcher
	// ================================================================
	MCP_TOOL(Registry, "add_bind_dispatcher_node")
		.Description(TEXT("Add a 'Bind Event to Dispatcher' node. This binds a custom event to listen to the dispatcher. Returns node ID and all pins."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"))
		.StringArg(TEXT("dispatcher_name"), TEXT("Name of the event dispatcher to bind to"), true)
		.NumberArg(TEXT("node_x"), TEXT("X position in graph (default: 0)"))
		.NumberArg(TEXT("node_y"), TEXT("Y position in graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, DispatcherName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("dispatcher_name"), DispatcherName)) return FMCPToolResult::Error(TEXT("dispatcher_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			UK2Node_AddDelegate* Node = NewObject<UK2Node_AddDelegate>(Graph);
			Node->DelegateReference.SetSelfMember(FName(*DispatcherName));
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
			Result->SetStringField(TEXT("dispatcherName"), DispatcherName);
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// create_enum - Create a UserDefinedEnum asset
	// ================================================================
	MCP_TOOL(Registry, "create_enum")
		.Description(TEXT("Create a UserDefinedEnum asset with optional initial entries. Entries can be added as a JSON array of strings."))
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new enum (e.g., '/Game/Enums/E_Elements')"), true)
		.StringArrayArg(TEXT("entries_json"), TEXT("JSON array of enum entry display names (e.g., [\"Fire\",\"Ice\",\"Lightning\"])"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));

			FString PackagePath, AssetName;
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package) return PackageError;

			UUserDefinedEnum* NewEnum = NewObject<UUserDefinedEnum>(Package, FName(*AssetName), RF_Public | RF_Standalone);
			if (!NewEnum) return FMCPToolResult::Error(TEXT("Failed to create UserDefinedEnum"));

			// Initialize with empty enums and _MAX entry.
			// v4.5 (5.8): SetEnums gained an EUnderlyingType arg and the trailing
			// bAddMaxKeyIfMissing bool became UEnum::EAddMaxKeyIfMissing.
			TArray<TPair<FName, int64>> InitialNames;
			NewEnum->SetEnums(InitialNames, UEnum::ECppForm::Namespaced,
				UEnum::EUnderlyingType::uint8, EEnumFlags::None, UEnum::EAddMaxKeyIfMissing::Yes);

			// Add entries from the JSON array using the editor utility
			const TArray<TSharedPtr<FJsonValue>>* EntriesArray = nullptr;
			if (Args->TryGetArrayField(TEXT("entries_json"), EntriesArray) && EntriesArray)
			{
				for (const TSharedPtr<FJsonValue>& Entry : *EntriesArray)
				{
					FString EntryDisplayName = Entry->AsString();
					if (!EntryDisplayName.IsEmpty())
					{
						// AddNewEnumeratorForUserDefinedEnum adds a new entry with auto-generated name
						FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(NewEnum);

						// Set the display name for the newly added entry (before _MAX)
						int32 NewEntryIndex = NewEnum->NumEnums() - 2; // -1 for _MAX, -1 for 0-based
						if (NewEntryIndex >= 0)
						{
							FName InternalName = NewEnum->GetNameByIndex(NewEntryIndex);
							NewEnum->DisplayNameMap.Add(InternalName, FText::FromString(EntryDisplayName));
						}
					}
				}
			}

			FAssetRegistryModule::AssetCreated(NewEnum);
			Package->MarkPackageDirty();

			FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, NewEnum, *PackageFilename, SaveArgs);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), AssetName);
			Result->SetStringField(TEXT("path"), AssetPath);
			Result->SetNumberField(TEXT("numEntries"), NewEnum->NumEnums() - 1); // Exclude _MAX

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// create_blueprint_interface - Create a new Blueprint Interface asset
	// ================================================================
	MCP_TOOL(Registry, "create_blueprint_interface")
		.Description(TEXT("Create a new Blueprint Interface asset. Blueprint Interfaces define function signatures that multiple Blueprints can implement."))
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new interface (e.g., '/Game/Interfaces/BPI_Interactable')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));

			FString PackagePath, AssetName;
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package) return PackageError;

			UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
			Factory->ParentClass = UInterface::StaticClass();
			Factory->BlueprintType = BPTYPE_Interface;

			UBlueprint* NewBPI = Cast<UBlueprint>(Factory->FactoryCreateNew(
				UBlueprint::StaticClass(), Package, FName(*AssetName),
				RF_Public | RF_Standalone, nullptr, GWarn));

			if (!NewBPI)
			{
				return FMCPToolResult::Error(TEXT("Failed to create Blueprint Interface"));
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(NewBPI);
			FKismetEditorUtilities::CompileBlueprint(NewBPI);

			FAssetRegistryModule::AssetCreated(NewBPI);
			Package->MarkPackageDirty();

			FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, NewBPI, *PackageFilename, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Created Blueprint Interface '%s' at %s"), *AssetName, *AssetPath));
		});

	// ================================================================
	// add_interface_to_blueprint - Add an interface to a Blueprint
	// ================================================================
	MCP_TOOL(Registry, "add_interface_to_blueprint")
		.Description(TEXT("Add a Blueprint Interface to an existing Blueprint, making it implement the interface's functions."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("interface_path"), TEXT("Content path of the Blueprint Interface to implement"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, InterfacePath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("interface_path"), InterfacePath)) return FMCPToolResult::Error(TEXT("interface_path required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UBlueprint* InterfaceBP = FindBlueprint(InterfacePath);
			if (!InterfaceBP) return FMCPToolResult::Error(FString::Printf(TEXT("Interface not found: %s"), *InterfacePath));

			UClass* InterfaceClass = InterfaceBP->GeneratedClass;
			if (!InterfaceClass) return FMCPToolResult::Error(TEXT("Interface has no generated class - compile the interface first"));

			// Check if already implemented
			for (const FBPInterfaceDescription& Iface : BP->ImplementedInterfaces)
			{
				if (Iface.Interface == InterfaceClass)
				{
					return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint already implements interface '%s'"), *InterfaceBP->GetName()));
				}
			}

			FBlueprintEditorUtils::ImplementNewInterface(BP, InterfaceClass->GetClassPathName());
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Added interface '%s' to Blueprint '%s'"),
				*InterfaceBP->GetName(), *BP->GetName()));
		});

	// ================================================================
	// get_blueprint_interfaces - List interfaces implemented by a Blueprint
	// ================================================================
	MCP_TOOL(Registry, "get_blueprint_interfaces")
		.ReadOnly()
		.Description(TEXT("List all interfaces implemented by a Blueprint, including their names, paths, and function signatures."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			TArray<TSharedPtr<FJsonValue>> InterfacesArray;
			for (const FBPInterfaceDescription& Iface : BP->ImplementedInterfaces)
			{
				TSharedPtr<FJsonObject> IfaceObj = MakeShared<FJsonObject>();
				IfaceObj->SetStringField(TEXT("name"), Iface.Interface ? Iface.Interface->GetName() : TEXT("Unknown"));
				IfaceObj->SetStringField(TEXT("path"), Iface.Interface ? Iface.Interface->GetPathName() : TEXT(""));

				// List functions in this interface
				TArray<TSharedPtr<FJsonValue>> FuncsArray;
				for (UEdGraph* Graph : Iface.Graphs)
				{
					if (!Graph) continue;
					TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
					FuncObj->SetStringField(TEXT("name"), Graph->GetName());
					FuncObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
					FuncsArray.Add(MakeShared<FJsonValueObject>(FuncObj));
				}
				IfaceObj->SetArrayField(TEXT("functions"), FuncsArray);
				InterfacesArray.Add(MakeShared<FJsonValueObject>(IfaceObj));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("blueprint"), BP->GetName());
			Result->SetNumberField(TEXT("interfaceCount"), InterfacesArray.Num());
			Result->SetArrayField(TEXT("interfaces"), InterfacesArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPBlueprintTools::Dispatchers
