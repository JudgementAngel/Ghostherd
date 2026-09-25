// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Common/MCPGraphSerializer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"

namespace MCPCommon
{

TSharedPtr<FJsonObject> PinToJson(const UEdGraphPin* Pin)
{
	TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
	if (!Pin)
	{
		return PinObj;
	}

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
	if (Pin->PinType.ContainerType == EPinContainerType::Array)  { PinObj->SetStringField(TEXT("container"), TEXT("array")); }
	if (Pin->PinType.ContainerType == EPinContainerType::Set)    { PinObj->SetStringField(TEXT("container"), TEXT("set")); }
	if (Pin->PinType.ContainerType == EPinContainerType::Map)    { PinObj->SetStringField(TEXT("container"), TEXT("map")); }

	if (!Pin->DefaultValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
	}
	if (!Pin->DefaultTextValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("defaultTextValue"), Pin->DefaultTextValue.ToString());
	}
	if (Pin->DefaultObject)
	{
		PinObj->SetStringField(TEXT("defaultObject"), Pin->DefaultObject->GetPathName());
	}

	PinObj->SetBoolField(TEXT("isHidden"), Pin->bHidden);
	PinObj->SetBoolField(TEXT("isConnected"), Pin->LinkedTo.Num() > 0);

	TArray<TSharedPtr<FJsonValue>> LinksArray;
	for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
	{
		if (!LinkedPin || !LinkedPin->GetOwningNode())
		{
			continue;
		}
		TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
		LinkObj->SetStringField(TEXT("nodeId"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
		LinkObj->SetStringField(TEXT("pinName"), LinkedPin->PinName.ToString());
		LinksArray.Add(MakeShared<FJsonValueObject>(LinkObj));
	}
	PinObj->SetArrayField(TEXT("connections"), LinksArray);

	return PinObj;
}

TSharedPtr<FJsonObject> NodeToJson(const UEdGraphNode* Node, bool bIncludePins, bool bIncludeHiddenPins)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Node)
	{
		return Obj;
	}

	Obj->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
	Obj->SetStringField(TEXT("type"), Node->GetClass()->GetName());
	Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
	Obj->SetNumberField(TEXT("x"), Node->NodePosX);
	Obj->SetNumberField(TEXT("y"), Node->NodePosY);
	Obj->SetBoolField(TEXT("enabled"), Node->GetDesiredEnabledState() == ENodeEnabledState::Enabled);

	if (const UK2Node* K2Node = Cast<UK2Node>(Node))
	{
		Obj->SetBoolField(TEXT("isPure"), K2Node->IsNodePure());
	}
	if (!Node->NodeComment.IsEmpty())
	{
		Obj->SetStringField(TEXT("comment"), Node->NodeComment);
	}

	if (bIncludePins)
	{
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && (bIncludeHiddenPins || !Pin->bHidden))
			{
				PinsArray.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
			}
		}
		Obj->SetArrayField(TEXT("pins"), PinsArray);
	}

	return Obj;
}

TSharedPtr<FJsonObject> GraphToJson(const UEdGraph* Graph, bool bIncludePins, bool bIncludeHiddenPins)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Graph)
	{
		return Obj;
	}

	Obj->SetStringField(TEXT("name"), Graph->GetName());

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> EdgesArray;

	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		NodesArray.Add(MakeShared<FJsonValueObject>(NodeToJson(Node, bIncludePins, bIncludeHiddenPins)));

		// Emit each edge exactly once, from the OUTPUT side, so the edge list
		// is a clean directed topology instead of doubled per-pin link lists.
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output)
			{
				continue;
			}
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNode())
				{
					continue;
				}
				TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
				Edge->SetStringField(TEXT("fromNode"), Node->NodeGuid.ToString());
				Edge->SetStringField(TEXT("fromPin"), Pin->PinName.ToString());
				Edge->SetStringField(TEXT("toNode"), Linked->GetOwningNode()->NodeGuid.ToString());
				Edge->SetStringField(TEXT("toPin"), Linked->PinName.ToString());
				EdgesArray.Add(MakeShared<FJsonValueObject>(Edge));
			}
		}
	}

	Obj->SetNumberField(TEXT("nodeCount"), NodesArray.Num());
	Obj->SetArrayField(TEXT("nodes"), NodesArray);
	Obj->SetArrayField(TEXT("connections"), EdgesArray);

	return Obj;
}

}
