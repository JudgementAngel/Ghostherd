// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/Blueprint/BlueprintCommon.h"

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

namespace MCPBlueprintTools::Common
{

UBlueprint* FindBlueprint(const FString& AssetPath)
{
	return LoadObject<UBlueprint>(nullptr, *AssetPath);
}

UEdGraph* FindGraphInBlueprint(UBlueprint* BP, const FString& GraphName)
{
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		if (Graph && Graph->GetName() == GraphName) return Graph;
	}
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == GraphName) return Graph;
	}
	for (FBPInterfaceDescription& Interface : BP->ImplementedInterfaces)
	{
		for (UEdGraph* Graph : Interface.Graphs)
		{
			if (Graph && Graph->GetName() == GraphName) return Graph;
		}
	}
	return nullptr;
}

UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& NodeId)
{
	FGuid Guid;
	if (!FGuid::Parse(NodeId, Guid)) return nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == Guid) return Node;
	}
	return nullptr;
}

UClass* FindClassByName(const FString& ClassName)
{
	// 1. Try as content path first (e.g., "/Game/Blueprints/BP_Door",
	//    "/Game/Blueprints/BP_Door.BP_Door", or a generated-class path
	//    "/Game/Blueprints/BP_Door.BP_Door_C").
	if (ClassName.StartsWith(TEXT("/")))
	{
		// 1a. Direct UClass load — handles generated-class object paths
		//     ending in "_C" (e.g. ".WBP_HealthBar_C").
		if (UClass* DirectClass = LoadObject<UClass>(nullptr, *ClassName))
			return DirectClass;

		// 1b. Load as the UBlueprint asset and return its generated class.
		if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ClassName))
		{
			if (BP->GeneratedClass)
				return BP->GeneratedClass;
		}

		// 1c. Package-only path (e.g. "/Game/UI/WBP_HealthBar" with no
		//     ".Object" suffix): rebuild the object path as "Package.AssetName".
		{
			FString PackagePath = ClassName;
			int32 DotIndex;
			if (PackagePath.FindChar(TEXT('.'), DotIndex))
				PackagePath = PackagePath.Left(DotIndex);

			FString AssetName;
			if (PackagePath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd)
				&& !AssetName.IsEmpty())
			{
				const FString ObjectPath = PackagePath + TEXT(".") + AssetName;
				if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ObjectPath))
				{
					if (BP->GeneratedClass)
						return BP->GeneratedClass;
				}
				if (UClass* GenClass = LoadObject<UClass>(nullptr, *(ObjectPath + TEXT("_C"))))
					return GenClass;
			}
		}

		// A leading-slash path that didn't resolve must NOT fall through to the
		// "/Game/%s" search paths below: prepending "/Game/" to an already-
		// rooted path builds "/Game//Game/..." (double slash), which makes
		// CreatePackage assert and HARD-CRASH the editor. Bail out cleanly.
		return nullptr;
	}

	// 2. Try C++ class lookup with various naming conventions
	UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	if (!FoundClass)
		FoundClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *ClassName), EFindFirstObjectOptions::ExactClass);
	if (!FoundClass)
		FoundClass = FindFirstObject<UClass>(*FString::Printf(TEXT("A%s"), *ClassName), EFindFirstObjectOptions::ExactClass);
	if (FoundClass) return FoundClass;

	// 3. Try loading as a Blueprint asset by searching common paths
	//    This handles cases like "BP_Door" without a full content path
	TArray<FString> SearchPaths = {
		FString::Printf(TEXT("/Game/%s"), *ClassName),
		FString::Printf(TEXT("/Game/Blueprints/%s"), *ClassName),
		FString::Printf(TEXT("/Game/BP/%s"), *ClassName),
	};

	for (const FString& Path : SearchPaths)
	{
		UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Path);
		if (BP && BP->GeneratedClass)
			return BP->GeneratedClass;
	}

	// 4. Search the asset registry for any Blueprint with this name
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString() == ClassName)
		{
			UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
			if (BP && BP->GeneratedClass)
				return BP->GeneratedClass;
		}
	}

	return nullptr;
}

UScriptStruct* FindStructByName(const FString& StructName)
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

FEdGraphPinType StringToPinType(const FString& TypeStr)
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

UBlueprint* LoadStandardMacrosBlueprint()
{
	// Cache: weak so a GC'd asset re-resolves instead of dangling.
	static TWeakObjectPtr<UBlueprint> CachedMacroBP;
	if (CachedMacroBP.IsValid())
	{
		return CachedMacroBP.Get();
	}

	// Canonical path for UE 5.x (verified against 5.7's BaseEditorPerProjectUserSettings.ini).
	UBlueprint* MacroBP = LoadObject<UBlueprint>(nullptr,
		TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));

	// Fallback: ask the asset registry instead of guessing more hardcoded paths,
	// so the tools survive the asset moving in a future engine version.
	if (!MacroBP)
	{
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(TEXT("/Engine"));
		Filter.bRecursivePaths = true;

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName == FName(TEXT("StandardMacros")))
			{
				MacroBP = Cast<UBlueprint>(Asset.GetAsset());
				if (MacroBP) break;
			}
		}
	}

	CachedMacroBP = MacroBP;
	return MacroBP;
}

TSharedPtr<FJsonObject> PinToJson(UEdGraphPin* Pin)
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

}
