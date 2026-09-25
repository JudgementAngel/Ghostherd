// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"  // for FEdGraphPinType (it's a struct, not forward-declarable cleanly)

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UClass;
class UScriptStruct;
class FJsonObject;

namespace MCPBlueprintTools::Common
{
	UBlueprint*       FindBlueprint(const FString& AssetPath);
	UEdGraph*         FindGraphInBlueprint(UBlueprint* BP, const FString& GraphName);
	UEdGraphNode*     FindNodeByGuid(UEdGraph* Graph, const FString& NodeId);
	UClass*           FindClassByName(const FString& ClassName);
	UScriptStruct*    FindStructByName(const FString& StructName);
	FEdGraphPinType   StringToPinType(const FString& TypeStr);
	TSharedPtr<FJsonObject> PinToJson(UEdGraphPin* Pin);

	/** Locate the engine's StandardMacros macro library (ForEachLoop, WhileLoop, Gate, ...).
	 *  Tries the canonical 5.x path first, then falls back to an asset-registry search so
	 *  the tools survive the asset moving between engine versions. Result is cached.
	 *  (v4 Phase 0 — replaces three hardcoded path guesses.) */
	UBlueprint*       LoadStandardMacrosBlueprint();
}
