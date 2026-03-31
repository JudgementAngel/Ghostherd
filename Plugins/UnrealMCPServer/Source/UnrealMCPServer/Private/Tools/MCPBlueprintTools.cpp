#include "Tools/MCPBlueprintTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

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

namespace MCPBlueprintTools
{

// ============================================================================
// Helper functions
// ============================================================================

static UBlueprint* FindBlueprint(const FString& AssetPath)
{
	return LoadObject<UBlueprint>(nullptr, *AssetPath);
}

static UEdGraph* FindGraphInBlueprint(UBlueprint* BP, const FString& GraphName)
{
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		if (Graph && Graph->GetName() == GraphName) return Graph;
	}
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == GraphName) return Graph;
	}
	return nullptr;
}

static UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& NodeId)
{
	FGuid Guid;
	if (!FGuid::Parse(NodeId, Guid)) return nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == Guid) return Node;
	}
	return nullptr;
}

static UClass* FindClassByName(const FString& ClassName)
{
	UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	if (!FoundClass)
		FoundClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *ClassName), EFindFirstObjectOptions::ExactClass);
	if (!FoundClass)
		FoundClass = FindFirstObject<UClass>(*FString::Printf(TEXT("A%s"), *ClassName), EFindFirstObjectOptions::ExactClass);
	return FoundClass;
}

static UScriptStruct* FindStructByName(const FString& StructName)
{
	// Try exact match first
	UScriptStruct* Found = FindFirstObject<UScriptStruct>(*StructName, EFindFirstObjectOptions::ExactClass);
	if (Found) return Found;

	// Try with F prefix (standard UE naming: FVector, FRotator, FPostProcessSettings, etc.)
	Found = FindFirstObject<UScriptStruct>(*FString::Printf(TEXT("F%s"), *StructName), EFindFirstObjectOptions::ExactClass);
	if (Found) return Found;

	// Try known common structs by short name
	static const TMap<FString, UScriptStruct*> CommonStructs = {
		{ TEXT("Vector"), TBaseStructure<FVector>::Get() },
		{ TEXT("Rotator"), TBaseStructure<FRotator>::Get() },
		{ TEXT("Transform"), TBaseStructure<FTransform>::Get() },
		{ TEXT("LinearColor"), TBaseStructure<FLinearColor>::Get() },
		{ TEXT("Color"), TBaseStructure<FColor>::Get() },
		{ TEXT("Vector2D"), TBaseStructure<FVector2D>::Get() },
	};

	if (const UScriptStruct* const* CommonMatch = CommonStructs.Find(StructName))
		return const_cast<UScriptStruct*>(*CommonMatch);

	return nullptr;
}

static FEdGraphPinType StringToPinType(const FString& TypeStr)
{
	FEdGraphPinType PinType;

	if (TypeStr == TEXT("Boolean") || TypeStr == TEXT("bool"))
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	else if (TypeStr == TEXT("Integer") || TypeStr == TEXT("Int") || TypeStr == TEXT("int"))
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	else if (TypeStr == TEXT("Int64"))
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	else if (TypeStr == TEXT("Float") || TypeStr == TEXT("Double") || TypeStr == TEXT("float") || TypeStr == TEXT("double"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (TypeStr == TEXT("String") || TypeStr == TEXT("string"))
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	else if (TypeStr == TEXT("Name"))
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	else if (TypeStr == TEXT("Text"))
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	else if (TypeStr == TEXT("Vector"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (TypeStr == TEXT("Rotator"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (TypeStr == TEXT("Transform"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else if (TypeStr == TEXT("Object"))
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	else if (TypeStr == TEXT("Class"))
		PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
	else if (TypeStr == TEXT("Byte"))
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	else if (TypeStr == TEXT("Exec"))
		PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
	else
		PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;

	return PinType;
}

static TSharedPtr<FJsonObject> PinToJson(UEdGraphPin* Pin)
{
	TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
	PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
	PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
	PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());

	if (Pin->PinType.PinSubCategoryObject.IsValid())
	{
		PinObj->SetStringField(TEXT("subType"), Pin->PinType.PinSubCategoryObject->GetName());
	}
	if (!Pin->PinType.PinSubCategory.IsNone())
	{
		PinObj->SetStringField(TEXT("subCategory"), Pin->PinType.PinSubCategory.ToString());
	}

	if (!Pin->DefaultValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
	}
	if (!Pin->DefaultTextValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("defaultTextValue"), Pin->DefaultTextValue.ToString());
	}

	PinObj->SetBoolField(TEXT("isHidden"), Pin->bHidden);
	PinObj->SetBoolField(TEXT("isConnected"), Pin->LinkedTo.Num() > 0);

	TArray<TSharedPtr<FJsonValue>> LinksArray;
	for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
	{
		if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
		TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
		LinkObj->SetStringField(TEXT("nodeId"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
		LinkObj->SetStringField(TEXT("pinName"), LinkedPin->PinName.ToString());
		LinksArray.Add(MakeShared<FJsonValueObject>(LinkObj));
	}
	PinObj->SetArrayField(TEXT("connections"), LinksArray);

	return PinObj;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_blueprint - Create a new Blueprint class
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new blueprint (e.g., '/Game/Blueprints/BP_MyActor')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("parent_class"), TEXT("Parent class name (e.g., 'Actor', 'Pawn', 'Character', 'PlayerController'). Default: 'Actor'"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_blueprint");
		Def.Description = TEXT("Create a new Blueprint class asset. Specify a content path and parent class. The Blueprint is saved and ready for editing.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString ParentClassName = TEXT("Actor");
			Args->TryGetStringField(TEXT("parent_class"), ParentClassName);

			UClass* ParentClass = FindClassByName(ParentClassName);
			if (!ParentClass)
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Parent class not found: %s"), *ParentClassName));
			}

			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			FString AssetName = FPackageName::GetShortName(AssetPath);

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package: %s"), *PackagePath));
			}

			UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
			Factory->ParentClass = ParentClass;

			UBlueprint* NewBP = Cast<UBlueprint>(Factory->FactoryCreateNew(
				UBlueprint::StaticClass(), Package, FName(*AssetName),
				RF_Public | RF_Standalone, nullptr, GWarn));

			if (!NewBP)
			{
				return FMCPToolResult::Error(TEXT("Failed to create Blueprint"));
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(NewBP);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			FAssetRegistryModule::AssetCreated(NewBP);
			Package->MarkPackageDirty();

			FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, NewBP, *PackageFilename, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Created Blueprint '%s' (parent: %s) at %s"),
				*AssetName, *ParentClassName, *AssetPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_blueprint_info - Read Blueprint structure
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint (e.g., '/Game/Blueprints/BP_MyActor')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_blueprint_info");
		Def.Description = TEXT("Get comprehensive information about a Blueprint: parent class, components, variables, functions, and event graph structure.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("name"), BP->GetName());
			Info->SetStringField(TEXT("path"), AssetPath);
			Info->SetStringField(TEXT("parentClass"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT("None"));
			Info->SetStringField(TEXT("blueprintType"), StaticEnum<EBlueprintType>()->GetNameStringByValue((int64)BP->BlueprintType));

			// Components from SCS
			TArray<TSharedPtr<FJsonValue>> ComponentsArray;
			if (BP->SimpleConstructionScript)
			{
				const TArray<USCS_Node*>& AllNodes = BP->SimpleConstructionScript->GetAllNodes();
				for (USCS_Node* Node : AllNodes)
				{
					if (!Node) continue;
					TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
					CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
					CompObj->SetStringField(TEXT("class"), Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("Unknown"));
					CompObj->SetBoolField(TEXT("isRoot"), Node == BP->SimpleConstructionScript->GetDefaultSceneRootNode());
					ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
				}
			}
			Info->SetArrayField(TEXT("components"), ComponentsArray);

			// Variables
			TArray<TSharedPtr<FJsonValue>> VarsArray;
			for (FBPVariableDescription& Var : BP->NewVariables)
			{
				TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
				VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
				VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
				VarObj->SetBoolField(TEXT("instanceEditable"), Var.PropertyFlags & CPF_Edit ? true : false);
				VarObj->SetBoolField(TEXT("blueprintReadOnly"), Var.PropertyFlags & CPF_BlueprintReadOnly ? true : false);
				VarsArray.Add(MakeShared<FJsonValueObject>(VarObj));
			}
			Info->SetArrayField(TEXT("variables"), VarsArray);

			// Event Graphs
			TArray<TSharedPtr<FJsonValue>> GraphsArray;
			for (UEdGraph* Graph : BP->UbergraphPages)
			{
				TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
				GraphObj->SetStringField(TEXT("name"), Graph->GetName());
				GraphObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());

				TArray<TSharedPtr<FJsonValue>> NodesArray;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
					NodeObj->SetStringField(TEXT("name"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
					NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
					NodeObj->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
					NodeObj->SetNumberField(TEXT("posX"), Node->NodePosX);
					NodeObj->SetNumberField(TEXT("posY"), Node->NodePosY);
					NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
				}
				GraphObj->SetArrayField(TEXT("nodes"), NodesArray);
				GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
			}
			Info->SetArrayField(TEXT("eventGraphs"), GraphsArray);

			// Function Graphs
			TArray<TSharedPtr<FJsonValue>> FuncsArray;
			for (UEdGraph* Graph : BP->FunctionGraphs)
			{
				TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
				FuncObj->SetStringField(TEXT("name"), Graph->GetName());
				FuncObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());

				TArray<TSharedPtr<FJsonValue>> NodesArray;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
					NodeObj->SetStringField(TEXT("name"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
					NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
					NodeObj->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
					NodeObj->SetNumberField(TEXT("posX"), Node->NodePosX);
					NodeObj->SetNumberField(TEXT("posY"), Node->NodePosY);
					NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
				}
				FuncObj->SetArrayField(TEXT("nodes"), NodesArray);
				FuncsArray.Add(MakeShared<FJsonValueObject>(FuncObj));
			}
			Info->SetArrayField(TEXT("functions"), FuncsArray);

			return FMCPToolResult::Success(JsonToString(Info));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_component - Add a component to a Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("component_class"), TEXT("Component class name (e.g., 'StaticMeshComponent', 'PointLightComponent', 'BoxCollisionComponent')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("component_name"), TEXT("Name for the new component"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("parent_component"), TEXT("Name of parent component to attach to. If empty, attaches to root."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_component");
		Def.Description = TEXT("Add a component to a Blueprint's Simple Construction Script (SCS). The component will appear in the Blueprint's component hierarchy.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, CompClassName, CompName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("component_class"), CompClassName)) return FMCPToolResult::Error(TEXT("component_class required"));
			if (!Args->TryGetStringField(TEXT("component_name"), CompName)) return FMCPToolResult::Error(TEXT("component_name required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
			if (!BP->SimpleConstructionScript) return FMCPToolResult::Error(TEXT("Blueprint has no SCS"));

			UClass* CompClass = FindClassByName(CompClassName);
			if (!CompClass || !CompClass->IsChildOf(UActorComponent::StaticClass()))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Component class not found: %s"), *CompClassName));
			}

			USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(CompClass, FName(*CompName));
			if (!NewNode)
			{
				return FMCPToolResult::Error(TEXT("Failed to create SCS node"));
			}

			FString ParentName;
			if (Args->TryGetStringField(TEXT("parent_component"), ParentName) && !ParentName.IsEmpty())
			{
				const TArray<USCS_Node*>& AllNodes = BP->SimpleConstructionScript->GetAllNodes();
				for (USCS_Node* Node : AllNodes)
				{
					if (Node && Node->GetVariableName().ToString() == ParentName)
					{
						Node->AddChildNode(NewNode);
						break;
					}
				}
			}
			else
			{
				BP->SimpleConstructionScript->AddNode(NewNode);
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Added component '%s' (%s) to Blueprint '%s'"),
				*CompName, *CompClassName, *BP->GetName()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_component_property - Set default value on a BP component
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("component_name"), TEXT("Name of the component"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("property_name"), TEXT("Property to set"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("property_value"), TEXT("Value as string"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_component_property");
		Def.Description = TEXT("Set a default property value on a component in a Blueprint. Use get_blueprint_info first to discover component names.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, CompName, PropName, PropValue;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("component_name"), CompName)) return FMCPToolResult::Error(TEXT("component_name required"));
			if (!Args->TryGetStringField(TEXT("property_name"), PropName)) return FMCPToolResult::Error(TEXT("property_name required"));
			if (!Args->TryGetStringField(TEXT("property_value"), PropValue)) return FMCPToolResult::Error(TEXT("property_value required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
			if (!BP->SimpleConstructionScript) return FMCPToolResult::Error(TEXT("Blueprint has no SCS"));

			USCS_Node* TargetNode = nullptr;
			for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->GetVariableName().ToString() == CompName)
				{
					TargetNode = Node;
					break;
				}
			}
			if (!TargetNode) return FMCPToolResult::Error(FString::Printf(TEXT("Component not found: %s"), *CompName));

			UActorComponent* Template = TargetNode->ComponentTemplate;
			if (!Template) return FMCPToolResult::Error(TEXT("Component template is null"));

			FProperty* Prop = Template->GetClass()->FindPropertyByName(FName(*PropName));
			if (!Prop) return FMCPToolResult::Error(FString::Printf(TEXT("Property not found: %s"), *PropName));

			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Template);
			if (!Prop->ImportText_Direct(*PropValue, ValuePtr, Template, PPF_None))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to set '%s' = '%s'"), *PropName, *PropValue));
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Set '%s.%s' = '%s'"), *CompName, *PropName, *PropValue));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// compile_blueprint - Compile a Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint to compile"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("compile_blueprint");
		Def.Description = TEXT("Compile a Blueprint and report any errors or warnings.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FKismetEditorUtilities::CompileBlueprint(BP);

			FString StatusStr;
			switch (BP->Status)
			{
			case BS_Unknown: StatusStr = TEXT("Unknown"); break;
			case BS_Dirty: StatusStr = TEXT("Dirty (needs recompile)"); break;
			case BS_Error: StatusStr = TEXT("Error"); break;
			case BS_UpToDate: StatusStr = TEXT("Success"); break;
			case BS_BeingCreated: StatusStr = TEXT("Being created"); break;
			case BS_UpToDateWithWarnings: StatusStr = TEXT("Success (with warnings)"); break;
			default: StatusStr = TEXT("Unknown"); break;
			}

			bool bHasErrors = (BP->Status == BS_Error);
			FString Result = FString::Printf(TEXT("Compilation of '%s': %s"), *BP->GetName(), *StatusStr);

			if (bHasErrors)
			{
				return FMCPToolResult::Error(Result);
			}

			return FMCPToolResult::Success(Result);
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_variable - Add a variable to a Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("variable_name"), TEXT("Name of the new variable"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("variable_type"), TEXT("Type of the variable"),
			{ TEXT("Boolean"), TEXT("Integer"), TEXT("Float"), TEXT("String"), TEXT("Vector"), TEXT("Rotator"), TEXT("Transform"), TEXT("Object"), TEXT("Class"), TEXT("Name"), TEXT("Text") }, true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("instance_editable"), TEXT("Whether the variable is editable per-instance (default: true)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("category"), TEXT("Category for grouping in the details panel"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("default_value"), TEXT("Default value as a string"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_variable");
		Def.Description = TEXT("Add a new variable to a Blueprint with specified type and properties.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);
			if (!bSuccess)
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to add variable '%s'"), *VarName));
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// spawn_blueprint - Place a Blueprint actor in the level
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint to spawn"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"), TEXT("X position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"), TEXT("Y position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("z"), TEXT("Z position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("yaw"), TEXT("Yaw rotation (default: 0)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("label"), TEXT("Actor label in scene outliner"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("spawn_blueprint");
		Def.Description = TEXT("Spawn an instance of a Blueprint class in the current level at the specified location.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = nullptr;
			if (GEditor) World = GEditor->GetEditorWorldContext().World();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
			if (!BP->GeneratedClass) return FMCPToolResult::Error(TEXT("Blueprint has no generated class - compile it first"));

			FVector Location(
				Args->HasField(TEXT("x")) ? Args->GetNumberField(TEXT("x")) : 0.0,
				Args->HasField(TEXT("y")) ? Args->GetNumberField(TEXT("y")) : 0.0,
				Args->HasField(TEXT("z")) ? Args->GetNumberField(TEXT("z")) : 0.0
			);
			FRotator Rotation(0, Args->HasField(TEXT("yaw")) ? Args->GetNumberField(TEXT("yaw")) : 0.0, 0);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Spawn Blueprint")));

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AActor* NewActor = World->SpawnActor(BP->GeneratedClass, &Location, &Rotation, SpawnParams);
			if (!NewActor)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to spawn Blueprint actor"));
			}

			FString Label;
			if (Args->TryGetStringField(TEXT("label"), Label))
			{
				NewActor->SetActorLabel(Label);
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Spawned '%s' as '%s' at (%.1f, %.1f, %.1f)"),
				*BP->GetName(), *NewActor->GetActorLabel(), Location.X, Location.Y, Location.Z));
		});
		Registry.RegisterTool(Def);
	}


	// ################################################################
	// ##                                                            ##
	// ##  NEW TOOLS: Blueprint Graph / Node / Wiring Operations     ##
	// ##                                                            ##
	// ################################################################


	// ================================================================
	// add_function_graph - Create a new function in the Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("function_name"), TEXT("Name for the new function"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("access"), TEXT("Access specifier (default: Public)"),
			{ TEXT("Public"), TEXT("Protected"), TEXT("Private") });
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("pure"), TEXT("Whether the function is pure (no exec pins). Default: false"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_function_graph");
		Def.Description = TEXT("Create a new function graph in a Blueprint. Returns the function entry node ID. Use add_function_pin to add input/output parameters, and add nodes to build the function body.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_function_pin - Add an input or output parameter to a function
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("function_name"), TEXT("Name of the function to modify"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("pin_name"), TEXT("Name for the new parameter"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("pin_type"), TEXT("Type of the parameter"),
			{ TEXT("Boolean"), TEXT("Integer"), TEXT("Float"), TEXT("String"), TEXT("Vector"), TEXT("Rotator"), TEXT("Transform"), TEXT("Object"), TEXT("Name"), TEXT("Text"), TEXT("Byte") }, true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("direction"), TEXT("Input = function parameter, Output = return value"),
			{ TEXT("Input"), TEXT("Output") }, true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_function_pin");
		Def.Description = TEXT("Add an input parameter or output return value to a Blueprint function. Input pins become function parameters; Output pins become return values. A FunctionResult node is automatically created if needed for outputs.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_function_return_node - Add a return node to a function
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("function_name"), TEXT("Name of the function graph"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 600)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_function_return_node");
		Def.Description = TEXT("Add a FunctionResult (return) node to a function graph. Required for functions that return values. Use add_function_pin with direction 'Output' to add return value pins.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_event_node - Add a built-in event (BeginPlay, Tick, etc.)
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("event_name"), TEXT("Built-in event to add"),
			{ TEXT("BeginPlay"), TEXT("Tick"), TEXT("ActorBeginOverlap"), TEXT("ActorEndOverlap") }, true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_event_node");
		Def.Description = TEXT("Add a built-in event node (BeginPlay, Tick, ActorBeginOverlap, ActorEndOverlap) to the event graph. Also accepts any UFunction name directly (e.g., 'ReceiveBeginPlay'). Returns the node ID for wiring.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_custom_event - Create a custom event node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("event_name"), TEXT("Name for the custom event"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_custom_event");
		Def.Description = TEXT("Add a custom event node to the event graph. Custom events can be called from other parts of the Blueprint. Returns the node ID.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			UK2Node_CustomEvent* CustomEventNode = NewObject<UK2Node_CustomEvent>(EventGraph);
			CustomEventNode->CustomFunctionName = FName(*EventName);
			CustomEventNode->CreateNewGuid();
			CustomEventNode->PostPlacedNewNode();
			CustomEventNode->AllocateDefaultPins();
			CustomEventNode->NodePosX = PosX;
			CustomEventNode->NodePosY = PosY;
			EventGraph->AddNode(CustomEventNode, false, false);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Added custom event '%s' (id: %s)"),
				*EventName, *CustomEventNode->NodeGuid.ToString()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_function_call_node - Add a function call node to a graph
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph (e.g., 'EventGraph' or function name)"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("function_name"), TEXT("Name of the function to call (e.g., 'PrintString', 'K2_SetActorLocation', 'Delay')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("target"), TEXT("Class owning the function (e.g., 'KismetSystemLibrary', 'Actor', 'GameplayStatics'). Use 'Self' for functions on this Blueprint. Required."), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_function_call_node");
		Def.Description = TEXT("Add a function call node to a Blueprint graph. Specify the target class and function name. Common classes: KismetSystemLibrary (PrintString, Delay), KismetMathLibrary (math ops), GameplayStatics (GetPlayerController, SpawnActor), Actor (SetActorLocation). Use 'Self' for Blueprint's own functions. Returns node ID and pin names for wiring.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			// Find the function
			UFunction* Function = TargetClass->FindFunctionByName(FName(*FunctionName));
			if (!Function)
			{
				// Search up the class hierarchy
				for (UClass* SearchClass = TargetClass->GetSuperClass(); SearchClass; SearchClass = SearchClass->GetSuperClass())
				{
					Function = SearchClass->FindFunctionByName(FName(*FunctionName));
					if (Function) break;
				}
			}
			if (!Function)
				return FMCPToolResult::Error(FString::Printf(TEXT("Function '%s' not found on class '%s'"), *FunctionName, *TargetStr));

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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_variable_get_node - Add a variable getter node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph (e.g., 'EventGraph' or function name)"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("variable_name"), TEXT("Name of the variable to get"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_variable_get_node");
		Def.Description = TEXT("Add a variable getter (Get) node to a Blueprint graph. The variable must exist on this Blueprint (use add_variable first). Returns node ID and output pin name for wiring.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(Graph);
			GetNode->VariableReference.SetSelfMember(FName(*VarName));
			GetNode->CreateNewGuid();
			GetNode->PostPlacedNewNode();
			GetNode->AllocateDefaultPins();
			GetNode->NodePosX = PosX;
			GetNode->NodePosY = PosY;
			Graph->AddNode(GetNode, false, false);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), GetNode->NodeGuid.ToString());

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : GetNode->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_variable_set_node - Add a variable setter node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph (e.g., 'EventGraph' or function name)"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("variable_name"), TEXT("Name of the variable to set"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_variable_set_node");
		Def.Description = TEXT("Add a variable setter (Set) node to a Blueprint graph. Has exec input/output pins and a value input pin. Returns node ID and pin names for wiring.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(Graph);
			SetNode->VariableReference.SetSelfMember(FName(*VarName));
			SetNode->CreateNewGuid();
			SetNode->PostPlacedNewNode();
			SetNode->AllocateDefaultPins();
			SetNode->NodePosX = PosX;
			SetNode->NodePosY = PosY;
			Graph->AddNode(SetNode, false, false);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("nodeId"), SetNode->NodeGuid.ToString());

			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : SetNode->Pins)
			{
				if (Pin && !Pin->bHidden)
					PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_branch_node - Add a Branch (if/else) node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_branch_node");
		Def.Description = TEXT("Add a Branch (if/else) node. Has an exec input, a boolean 'Condition' input, and 'True'/'False' exec outputs. Returns node ID and pin names.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_switch_on_int_node - Add a Switch on Integer node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("num_cases"), TEXT("Number of integer cases to create (default: 2)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("start_index"), TEXT("Starting integer value for cases (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_switch_on_int_node");
		Def.Description = TEXT("Add a Switch on Integer node. Has an exec input, an integer 'Selection' input, a 'Default' exec output, and numbered case exec outputs. Returns node ID and all pin names.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// connect_pins - Wire two node pins together
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph containing both nodes"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("source_node_id"), TEXT("GUID of the source node (from node creation or get_blueprint_info)"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("source_pin_name"), TEXT("Name of the output pin on the source node"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("target_node_id"), TEXT("GUID of the target node"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("target_pin_name"), TEXT("Name of the input pin on the target node"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("connect_pins");
		Def.Description = TEXT("Wire two Blueprint node pins together. Connect an output pin on one node to an input pin on another. The schema handles type checking and will report errors for incompatible types. Use get_node_pins to discover available pin names.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			// Use the schema to validate and create the connection
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
			bool bSuccess = K2Schema->TryCreateConnection(SourcePin, TargetPin);

			if (!bSuccess)
			{
				// Try the reverse direction in case source/target were swapped
				bSuccess = K2Schema->TryCreateConnection(TargetPin, SourcePin);
			}

			if (!bSuccess)
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to connect '%s.%s' -> '%s.%s' (incompatible types or invalid connection)"),
					*SourceNodeId, *SourcePinName, *TargetNodeId, *TargetPinName));
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Connected '%s.%s' -> '%s.%s'"),
				*SourcePinName, *SourceNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
				*TargetPinName, *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// disconnect_pin - Break all connections on a pin
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("node_id"), TEXT("GUID of the node"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("pin_name"), TEXT("Name of the pin to disconnect"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("disconnect_pin");
		Def.Description = TEXT("Break all connections on a specific pin of a node. Use get_node_pins to see current connections before disconnecting.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_pin_default_value - Set a default/literal value on a pin
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("node_id"), TEXT("GUID of the node"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("pin_name"), TEXT("Name of the pin to set the default value on"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("default_value"), TEXT("The default value as a string (e.g., 'Hello', '42', 'true', '1.0,2.0,3.0' for Vector)"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_pin_default_value");
		Def.Description = TEXT("Set the default (literal) value on an unconnected input pin. Use this to set parameter values like strings, numbers, booleans, vectors, etc. The value is parsed by the UE property system. For Vectors use 'X,Y,Z' format, for Rotators use 'P,Y,R' format.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_node_pins - Get detailed pin info for nodes in a graph
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("node_id"), TEXT("GUID of a specific node (optional - if omitted, returns all nodes with pins)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_node_pins");
		Def.Description = TEXT("Get detailed pin information for nodes in a Blueprint graph. Returns pin names, directions (Input/Output), types, default values, and current connections. Essential for discovering pin names before using connect_pins. If node_id is omitted, returns all nodes with their pins.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// remove_node - Remove a node from a graph
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("node_id"), TEXT("GUID of the node to remove"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("remove_node");
		Def.Description = TEXT("Remove a node from a Blueprint graph. All connections to/from the node will be broken. Cannot remove the function entry node of a function graph.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_make_struct_node - Create a "Make [Struct]" node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("struct_type"), TEXT("Struct name (e.g., 'FVector', 'FPostProcessSettings', 'FLinearColor', 'FHitResult')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Graph to add to (default: EventGraph)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_make_struct_node");
		Def.Description = TEXT("Add a 'Make [Struct]' node that constructs a struct from individual member values. "
			"Works with any UScriptStruct: FVector, FRotator, FTransform, FLinearColor, FPostProcessSettings, FHitResult, etc. "
			"Returns node ID and all pins (one input per struct member + one struct output).");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_break_struct_node - Create a "Break [Struct]" node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("struct_type"), TEXT("Struct name (e.g., 'FVector', 'FPostProcessSettings', 'FLinearColor', 'FHitResult')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Graph to add to (default: EventGraph)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_break_struct_node");
		Def.Description = TEXT("Add a 'Break [Struct]' node that decomposes a struct into individual member output pins. "
			"Works with any UScriptStruct: FVector, FRotator, FTransform, FLinearColor, FPostProcessSettings, FHitResult, etc. "
			"Returns node ID and all pins (one struct input + one output per struct member).");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_set_struct_fields_node - Create a "Set Members in [Struct]" node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("struct_type"), TEXT("Struct name (e.g., 'FPostProcessSettings', 'FVector', 'FLinearColor')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Graph to add to (default: EventGraph)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_set_struct_fields_node");
		Def.Description = TEXT("Add a 'Set Members in [Struct]' node that sets individual fields of a struct. "
			"Unlike Make, this takes an existing struct as input and selectively modifies fields. Has exec pins (not pure). "
			"Use this for FPostProcessSettings, FHitResult, and other complex structs. "
			"Returns node ID and all pins (exec in/out, struct in/out, one input per struct member).");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_for_each_loop_node - Add a ForEachLoop macro node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("with_break"), TEXT("If true, use ForEachLoopWithBreak instead of ForEachLoop (default: false)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_for_each_loop_node");
		Def.Description = TEXT("Add a ForEachLoop macro instance node. Iterates over an array with 'Array Element' and 'Array Index' outputs, plus 'Loop Body' and 'Completed' exec outputs. Set with_break=true for the variant with a Break input. Returns node ID and all pins.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
			UBlueprint* MacroBP = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorResources/Macros/StandardMacros.StandardMacros"));
			if (!MacroBP) return FMCPToolResult::Error(TEXT("Could not load StandardMacros blueprint"));

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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_while_loop_node - Add a WhileLoop macro node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_while_loop_node");
		Def.Description = TEXT("Add a WhileLoop macro instance node. Has a 'Condition' boolean input, 'Loop Body' exec output (runs while condition is true), and 'Completed' exec output (runs when condition becomes false). Returns node ID and all pins.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
			UBlueprint* MacroBP = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorResources/Macros/StandardMacros.StandardMacros"));
			if (!MacroBP) return FMCPToolResult::Error(TEXT("Could not load StandardMacros blueprint"));

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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_delay_node - Add a Delay node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("duration"), TEXT("Delay duration in seconds (default: 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_delay_node");
		Def.Description = TEXT("Add a Delay node (latent action). Has exec input/output and a 'Duration' float input. The output exec fires after the specified duration. Returns node ID and all pins.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_timeline_node - Add a Timeline node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("timeline_name"), TEXT("Name for the timeline"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("length"), TEXT("Timeline length in seconds (default: 5.0)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("auto_play"), TEXT("Whether the timeline auto-plays on BeginPlay (default: false)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("loop"), TEXT("Whether the timeline loops (default: false)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_timeline_node");
		Def.Description = TEXT("Add a Timeline node to a Blueprint graph. Timelines allow you to animate float/vector/color values over time with keyframes. "
			"Has Play, PlayFromStart, Stop, Reverse exec inputs, Update/Finished/Direction exec outputs, and a float output for each track. "
			"Returns node ID and all pins.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_cast_node - Add a Cast (dynamic cast) node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("target_class"), TEXT("Class name to cast to (e.g., 'Character', 'Pawn', 'MyBlueprintClass')"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_cast_node");
		Def.Description = TEXT("Add a Cast To node (dynamic cast). Has an Object input, exec input, success/fail exec outputs, and a typed output pin for the cast result. "
			"Returns node ID and all pins.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_spawn_actor_node - Add a SpawnActorFromClass node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_class"), TEXT("Optional: Actor class name to pre-fill (e.g., 'StaticMeshActor', 'PointLight')"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_spawn_actor_node");
		Def.Description = TEXT("Add a SpawnActorFromClass node. Has a Class input, SpawnTransform input, exec input/output, and a return value pin for the spawned actor. "
			"Optionally pre-fills the actor class. Returns node ID and all pins.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);
			UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
			if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph not found: %s"), *GraphName));

			UK2Node_SpawnActorFromClass* Node = NewObject<UK2Node_SpawnActorFromClass>(Graph);
			Node->CreateNewGuid();
			Node->PostPlacedNewNode();
			Node->AllocateDefaultPins();

			// If actor_class is provided, find the class and set the class pin
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
			Result->SetStringField(TEXT("nodeName"), TEXT("SpawnActorFromClass"));
			Result->SetArrayField(TEXT("pins"), PinsArray);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_get_all_actors_of_class_node - Add GetAllActorsOfClass node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_class"), TEXT("Optional: Actor class name to pre-fill on the class pin (e.g., 'StaticMeshActor')"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_get_all_actors_of_class_node");
		Def.Description = TEXT("Add a GetAllActorsOfClass function call node. Returns an array of all actors of the specified class in the world. "
			"Has a WorldContextObject input, ActorClass input, and OutActors array output. Returns node ID and all pins.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_sequence_node - Add an Execution Sequence node
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("graph_name"), TEXT("Name of the graph (default: EventGraph)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("num_outputs"), TEXT("Number of 'Then' output exec pins (default: 2, max: 64)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_x"), TEXT("X position in graph (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("node_y"), TEXT("Y position in graph (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_sequence_node");
		Def.Description = TEXT("Add a Sequence node that executes multiple output pins in order. Has an exec input and numbered 'Then' exec outputs (Then_0, Then_1, ...). "
			"Useful for organizing sequential logic. Returns node ID and all pins.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPBlueprintTools
