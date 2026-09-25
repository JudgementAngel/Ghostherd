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
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/TokenizedMessage.h"
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
#include "Common/MCPPropertyIO.h"
#include "Common/MCPAssetCreate.h"

namespace MCPBlueprintTools::Lifecycle
{

using namespace MCPBlueprintTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_blueprint - Create a new Blueprint class
	// ================================================================
	MCP_TOOL(Registry, "create_blueprint")
		.Description(TEXT("Create a new Blueprint class asset. Specify a content path and parent class. The Blueprint is saved and ready for editing."))
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new blueprint (e.g., '/Game/Blueprints/BP_MyActor')"), true)
		.StringArg(TEXT("parent_class"), TEXT("Parent class name (e.g., 'Actor', 'Pawn', 'Character', 'PlayerController'). Default: 'Actor'"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			FString PackagePath, AssetName;
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package)
			{
				return PackageError;
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

	// ================================================================
	// get_blueprint_info - Read Blueprint structure
	// ================================================================
	MCP_TOOL(Registry, "get_blueprint_info")
		.ReadOnly()
		.Description(TEXT("Get comprehensive information about a Blueprint: parent class, components, variables, functions, and event graph structure."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint (e.g., '/Game/Blueprints/BP_MyActor')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			// Implemented Interface Graphs
			TArray<TSharedPtr<FJsonValue>> InterfacesArray;
			for (FBPInterfaceDescription& Interface : BP->ImplementedInterfaces)
			{
				TSharedPtr<FJsonObject> InterfaceObj = MakeShared<FJsonObject>();
				InterfaceObj->SetStringField(TEXT("interfaceName"), Interface.Interface ? Interface.Interface->GetName() : TEXT("Unknown"));

				TArray<TSharedPtr<FJsonValue>> InterfaceFuncsArray;
				for (UEdGraph* Graph : Interface.Graphs)
				{
					if (!Graph) continue;
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
					InterfaceFuncsArray.Add(MakeShared<FJsonValueObject>(FuncObj));
				}
				InterfaceObj->SetArrayField(TEXT("functions"), InterfaceFuncsArray);
				InterfacesArray.Add(MakeShared<FJsonValueObject>(InterfaceObj));
			}
			Info->SetArrayField(TEXT("implementedInterfaces"), InterfacesArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Info), Info);
		});

	// ================================================================
	// add_component - Add a component to a Blueprint
	// ================================================================
	MCP_TOOL(Registry, "add_component")
		.Description(TEXT("Add a component to a Blueprint's Simple Construction Script (SCS). The component will appear in the Blueprint's component hierarchy."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("component_class"), TEXT("Component class name (e.g., 'StaticMeshComponent', 'PointLightComponent', 'BoxCollisionComponent')"), true)
		.StringArg(TEXT("component_name"), TEXT("Name for the new component"), true)
		.StringArg(TEXT("parent_component"), TEXT("Name of parent component to attach to. If empty, attaches to root."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

	// ================================================================
	// set_component_property - Set default value on a BP component
	// ================================================================
	MCP_TOOL(Registry, "set_component_property")
		.Description(TEXT("Set a default property value on a component in a Blueprint. Use get_blueprint_info first to discover component names."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
		.StringArg(TEXT("component_name"), TEXT("Name of the component"), true)
		.StringArg(TEXT("property_name"), TEXT("Property to set"), true)
		.StringArg(TEXT("property_value"), TEXT("Value as string"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			// Route through Pre/PostEditChange — raw ImportText_Direct on a
			// component template skips owner notifications and trips engine
			// state-tracking ensures (e.g. UStaticMeshComponent::KnownStaticMesh).
			FString SetError;
			if (!MCPCommon::SetObjectPropertyWithNotify(Prop, Template, PropValue, SetError))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to set '%s': %s"), *PropName, *SetError));
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Set '%s.%s' = '%s'"), *CompName, *PropName, *PropValue));
		});

	// ================================================================
	// compile_blueprint - Compile a Blueprint
	// ================================================================
	MCP_TOOL(Registry, "compile_blueprint")
		.Description(TEXT("Compile a Blueprint and report any errors or warnings."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint to compile"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP) return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			// v4 Phase 0: capture the compiler's message log so agents see *what*
			// failed (node, reason), not just a status word.
			FCompilerResultsLog ResultsLog;
			ResultsLog.SetSourcePath(BP->GetPathName());
			FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &ResultsLog);

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

			const bool bHasErrors = (BP->Status == BS_Error);
			FString Result = FString::Printf(TEXT("Compilation of '%s': %s (%d error(s), %d warning(s))"),
				*BP->GetName(), *StatusStr, ResultsLog.NumErrors, ResultsLog.NumWarnings);

			// Append individual compiler messages (errors + warnings) so the agent
			// can locate and fix the offending nodes.
			int32 Emitted = 0;
			for (const TSharedRef<FTokenizedMessage>& Msg : ResultsLog.Messages)
			{
				const EMessageSeverity::Type Severity = Msg->GetSeverity();
				if (Severity == EMessageSeverity::Error || Severity == EMessageSeverity::Warning)
				{
					Result += FString::Printf(TEXT("\n[%s] %s"),
						Severity == EMessageSeverity::Error ? TEXT("error") : TEXT("warning"),
						*Msg->ToText().ToString());
					if (++Emitted >= 25) { Result += TEXT("\n... (truncated)"); break; }
				}
			}

			if (bHasErrors)
			{
				return FMCPToolResult::Error(Result);
			}

			return FMCPToolResult::Success(Result);
		});

	// validate_blueprint moved to BlueprintValidation.cpp (v5 increment 12: structured, revisioned).

	// ================================================================
	// spawn_blueprint - Place a Blueprint actor in the level
	// ================================================================
	MCP_TOOL(Registry, "spawn_blueprint")
		.Description(TEXT("Spawn an instance of a Blueprint class in the current level at the specified location."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint to spawn"), true)
		.NumberArg(TEXT("x"), TEXT("X position (default: 0)"))
		.NumberArg(TEXT("y"), TEXT("Y position (default: 0)"))
		.NumberArg(TEXT("z"), TEXT("Z position (default: 0)"))
		.NumberArg(TEXT("yaw"), TEXT("Yaw rotation (default: 0)"))
		.StringArg(TEXT("label"), TEXT("Actor label in scene outliner"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
}

} // namespace MCPBlueprintTools::Lifecycle
