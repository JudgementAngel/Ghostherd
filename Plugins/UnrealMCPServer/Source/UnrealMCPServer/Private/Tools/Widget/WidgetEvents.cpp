// Copyright StraySpark Studio 2026. All Rights Reserved.
#include "Tools/Widget/WidgetCommon.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Blueprint/UserWidget.h"

// UMG Widget Blueprint editing
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprintOperationUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

// Blueprint event graph
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"

#include "Components/Widget.h"

namespace MCPWidgetTools::Events
{

using namespace MCPWidgetTools::Common;

static UEdGraph* FindEventGraph(UWidgetBlueprint* WBP)
{
	for (UEdGraph* Graph : WBP->UbergraphPages)
	{
		if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_EventGraph) return Graph;
	}
	return WBP->UbergraphPages.Num() > 0 ? WBP->UbergraphPages[0] : nullptr;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// bind_widget_event - Bind widget event to a Blueprint function
	// ================================================================
	MCP_TOOL(Registry, "bind_widget_event")
		.Description(TEXT("Create an event binding for a widget in a Widget Blueprint's event graph (the details-panel '+' button): adds a ComponentBoundEvent node for the widget's delegate (Button OnClicked, Slider OnValueChanged, ListView OnItemClicked, ...). The widget is promoted to a Blueprint variable if needed. If function_name is given, the event's exec pin is wired to a call of that function; if no such function exists a custom event with that name is created and called. Idempotent: an existing binding is reused."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget to bind event on (e.g., 'Btn_Start')"), true)
		.StringArg(TEXT("event_name"), TEXT("Multicast delegate on the widget class: OnClicked, OnPressed, OnReleased, OnHovered, OnUnhovered, OnValueChanged, OnCheckStateChanged, OnTextChanged, OnTextCommitted, OnSelectionChanged, OnItemClicked, ... (any delegate property name; the error lists what is available)"), true)
		.StringArg(TEXT("function_name"), TEXT("Blueprint function or custom event to call when the event fires. Created as a custom event if it does not exist."))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName, EventName, FunctionName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			if (!Args->TryGetStringField(TEXT("event_name"), EventName)) return FMCPToolResult::Error(TEXT("event_name is required"));
			Args->TryGetStringField(TEXT("function_name"), FunctionName);

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			// Resolve the delegate ("Clicked" -> "OnClicked" too)
			FName DelegateName = FName(*EventName);
			FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(Widget->GetClass(), DelegateName);
			if (!DelegateProp)
			{
				const FString AltName = FString::Printf(TEXT("On%s"), *EventName);
				DelegateProp = FindFProperty<FMulticastDelegateProperty>(Widget->GetClass(), FName(*AltName));
				if (DelegateProp) DelegateName = FName(*AltName);
			}
			if (!DelegateProp)
			{
				TArray<FString> Available;
				for (TFieldIterator<FMulticastDelegateProperty> It(Widget->GetClass()); It; ++It) Available.Add(It->GetName());
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(
					TEXT("Event '%s' not found on widget '%s' (type: %s). Available events: %s"),
					*EventName, *WidgetName, *Widget->GetClass()->GetName(),
					Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("none")), FString(), Available);
			}

			UEdGraph* EventGraph = FindEventGraph(WBP);
			if (!EventGraph) return FMCPToolResult::Error(TEXT("Could not find EventGraph in Widget Blueprint"));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Bind Widget Event")));
			WBP->Modify();
			EventGraph->Modify();

			// A bound event needs the widget to be a member variable (the designer forces this too).
			bool bPromoted = false;
			if (!Widget->bIsVariable)
			{
				Widget->Modify();
				FWidgetBlueprintOperationUtils::ToggleWidgetAsVariable(WBP, Widget, true, /*bMarkBlueprintModified*/ true);
				bPromoted = true;
			}
			else if (!FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Widget->GetFName()))
			{
				// Flag was set but the skeleton class is stale: regenerate so the variable exists.
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
			}

			// Engine path (same as the details panel): creates the K2Node_ComponentBoundEvent in the last edited ubergraph.
			bool bCreated = false;
			const UK2Node_ComponentBoundEvent* Existing = FKismetEditorUtilities::FindBoundEventForComponent(WBP, DelegateName, Widget->GetFName());
			if (!Existing)
			{
				FText Err;
				bCreated = FWidgetBlueprintOperationUtils::BindToEventProperty(WBP, DelegateName, Widget->GetFName(), Widget->GetClass(), /*bShouldJumpToNode*/ false, Err);
				Existing = FKismetEditorUtilities::FindBoundEventForComponent(WBP, DelegateName, Widget->GetFName());
				if (!Existing)
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create event binding: %s"), Err.IsEmpty() ? TEXT("unknown error") : *Err.ToString()));
				}
			}
			UK2Node_ComponentBoundEvent* EventNode = const_cast<UK2Node_ComponentBoundEvent*>(Existing);
			UEdGraph* Graph = EventNode->GetGraph();

			// Optional: wire the event to a function / custom event.
			FString WiredTo;
			bool bCreatedCustomEvent = false;
			if (!FunctionName.IsEmpty())
			{
				UFunction* Function = WBP->SkeletonGeneratedClass ? WBP->SkeletonGeneratedClass->FindFunctionByName(FName(*FunctionName)) : nullptr;
				if (!Function)
				{
					// Create a custom event with that name, then regenerate the skeleton so it is callable.
					UK2Node_CustomEvent* CustomEventNode = NewObject<UK2Node_CustomEvent>(Graph);
					CustomEventNode->CustomFunctionName = FName(*FunctionName);
					CustomEventNode->CreateNewGuid();
					CustomEventNode->SetFlags(RF_Transactional);
					CustomEventNode->AllocateDefaultPins();
					CustomEventNode->PostPlacedNewNode();
					CustomEventNode->NodePosX = EventNode->NodePosX;
					CustomEventNode->NodePosY = EventNode->NodePosY + 250;
					Graph->Modify();
					Graph->AddNode(CustomEventNode, false, false);
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
					bCreatedCustomEvent = true;
					Function = WBP->SkeletonGeneratedClass ? WBP->SkeletonGeneratedClass->FindFunctionByName(FName(*FunctionName)) : nullptr;
				}

				if (!Function)
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(FString::Printf(TEXT("Function '%s' could not be resolved on the Blueprint even after creating a custom event"), *FunctionName));
				}

				// Already wired? (idempotent re-runs)
				UEdGraphPin* ThenPin = EventNode->GetThenPin();
				bool bAlreadyWired = false;
				if (ThenPin)
				{
					for (UEdGraphPin* Linked : ThenPin->LinkedTo)
					{
						if (UK2Node_CallFunction* ExistingCall = Linked ? Cast<UK2Node_CallFunction>(Linked->GetOwningNode()) : nullptr)
						{
							if (ExistingCall->FunctionReference.GetMemberName() == Function->GetFName()) { bAlreadyWired = true; break; }
						}
					}
				}

				if (!bAlreadyWired)
				{
					UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(Graph);
					CallNode->CreateNewGuid();
					CallNode->SetFlags(RF_Transactional);
					CallNode->SetFromFunction(Function);
					CallNode->PostPlacedNewNode();
					CallNode->AllocateDefaultPins();
					CallNode->NodePosX = EventNode->NodePosX + 400;
					CallNode->NodePosY = EventNode->NodePosY;
					Graph->Modify();
					Graph->AddNode(CallNode, false, false);

					const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
					UEdGraphPin* ExecPin = CallNode->GetExecPin();
					if (ThenPin && ExecPin && !Schema->TryCreateConnection(ThenPin, ExecPin))
					{
						GEditor->EndTransaction();
						return FMCPToolResult::Error(FString::Printf(TEXT("Created nodes but could not connect the event to '%s'"), *FunctionName));
					}
				}
				WiredTo = FunctionName;
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
			GEditor->EndTransaction();

			TSharedPtr<FJsonObject> Compile = CompileWidgetBlueprint(WBP, WantsSave(Args));

			TArray<FString> OutputPins;
			for (UEdGraphPin* Pin : EventNode->Pins)
			{
				if (Pin->Direction == EGPD_Output)
					OutputPins.Add(FString::Printf(TEXT("%s (%s)"), *Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString()));
			}

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("node_id"), EventNode->NodeGuid.ToString());
			Info->SetStringField(TEXT("graph"), Graph ? Graph->GetName() : TEXT(""));
			Info->SetStringField(TEXT("event"), DelegateName.ToString());
			Info->SetBoolField(TEXT("created"), bCreated);
			Info->SetBoolField(TEXT("widget_promoted_to_variable"), bPromoted);
			Info->SetStringField(TEXT("wired_to"), WiredTo);
			Info->SetBoolField(TEXT("created_custom_event"), bCreatedCustomEvent);
			Info->SetObjectField(TEXT("compile"), Compile);

			return FMCPToolResult::SuccessStructured(FString::Printf(
				TEXT("%s event binding for '%s.%s' in %s.\nNode ID: %s\nOutput pins: %s%s%s\n\nUse Blueprint tools (connect_pins, add_function_call_node) to wire further logic."),
				bCreated ? TEXT("Created") : TEXT("Reused"), *WidgetName, *DelegateName.ToString(), *AssetPath,
				*EventNode->NodeGuid.ToString(), *FString::Join(OutputPins, TEXT(", ")),
				WiredTo.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("\nWired to: %s%s"), *WiredTo, bCreatedCustomEvent ? TEXT(" (new custom event)") : TEXT("")),
				bPromoted ? TEXT("\nWidget promoted to a Blueprint variable.") : TEXT("")), Info);
		});

	// ================================================================
	// bind_widget_property - Designer "Bind" dropdown (function / variable binding)
	// ================================================================
	MCP_TOOL(Registry, "bind_widget_property")
		.Description(TEXT("Create a property binding (the designer's 'Bind' dropdown) so a widget property such as Text, Percent, Visibility, ColorAndOpacity or Brush is driven every frame by a Blueprint function (function_name) or a member variable (variable_name) of matching type. Editor-only data with no Python surface. Note: polled bindings cost game-thread time; prefer direct setters for shipping UI."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget"), true)
		.StringArg(TEXT("property_name"), TEXT("Bindable property (one that has a matching '<Name>Delegate' on the widget class): Text, Percent, Visibility, ColorAndOpacity, Brush, IsEnabled, ToolTipText, ..."), true)
		.StringArg(TEXT("function_name"), TEXT("Pure Blueprint function on this Widget Blueprint returning the property type (create with add_function_graph + add_function_return_node first)"))
		.StringArg(TEXT("variable_name"), TEXT("Member variable on this Widget Blueprint of the property type (alternative to function_name)"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName, PropertyName, FunctionName, VariableName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			if (!Args->TryGetStringField(TEXT("property_name"), PropertyName)) return FMCPToolResult::Error(TEXT("property_name is required"));
			Args->TryGetStringField(TEXT("function_name"), FunctionName);
			Args->TryGetStringField(TEXT("variable_name"), VariableName);
			if (FunctionName.IsEmpty() == VariableName.IsEmpty())
				return FMCPToolResult::Error(TEXT("Pass exactly one of function_name or variable_name"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			if (!WBP->ArePropertyBindingsAllowed())
				return FMCPToolResult::Error(TEXT("Property bindings are disabled for this Widget Blueprint by the project's UMG editor settings"));
			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			// The property must have a "<Name>Delegate" sibling on the widget class.
			const FName DelegateName(*FString::Printf(TEXT("%sDelegate"), *PropertyName));
			FDelegateProperty* DelegateProp = FindFProperty<FDelegateProperty>(Widget->GetClass(), DelegateName);
			if (!DelegateProp)
			{
				TArray<FString> Bindable;
				for (TFieldIterator<FDelegateProperty> It(Widget->GetClass()); It; ++It)
				{
					FString N = It->GetName();
					if (N.RemoveFromEnd(TEXT("Delegate"))) Bindable.Add(N);
				}
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(
					TEXT("'%s' is not a bindable property on %s. Bindable: %s"),
					*PropertyName, *Widget->GetClass()->GetName(), *FString::Join(Bindable, TEXT(", "))), FString(), Bindable);
			}

			UClass* Skeleton = WBP->SkeletonGeneratedClass;
			if (!Skeleton) { FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP); Skeleton = WBP->SkeletonGeneratedClass; }
			if (!Skeleton) return FMCPToolResult::Error(TEXT("Widget Blueprint has no skeleton class (compile errors?)"));

			FDelegateEditorBinding Binding;
			Binding.ObjectName = Widget->GetName();
			Binding.PropertyName = FName(*PropertyName);

			if (!FunctionName.IsEmpty())
			{
				UFunction* Function = Skeleton->FindFunctionByName(FName(*FunctionName));
				if (!Function)
					return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
						FString::Printf(TEXT("Function '%s' not found on %s"), *FunctionName, *WBP->GetName()),
						TEXT("Create it with add_function_graph (mark it pure) and add a return value of the property type, then compile."));
				if (!Function->GetReturnProperty())
					return FMCPToolResult::Error(FString::Printf(TEXT("Function '%s' has no return value; a binding function must return the property type"), *FunctionName));

				Binding.FunctionName = Function->GetFName();
				UBlueprint::GetGuidFromClassByFieldName<UFunction>(Function->GetOwnerClass(), Function->GetFName(), Binding.MemberGuid);
				Binding.SourcePath = FEditorPropertyPath({ FFieldVariant(Function) });
				Binding.Kind = EBindingKind::Function;
			}
			else
			{
				FProperty* Property = Skeleton->FindPropertyByName(FName(*VariableName));
				if (!Property)
					return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
						FString::Printf(TEXT("Variable '%s' not found on %s"), *VariableName, *WBP->GetName()),
						TEXT("Create it with add_variable first."));
				Binding.SourceProperty = Property->GetFName();
				UBlueprint::GetGuidFromClassByFieldName<FProperty>(Skeleton, Property->GetFName(), Binding.MemberGuid);
				Binding.SourcePath = FEditorPropertyPath({ FFieldVariant(Property) });
				Binding.Kind = EBindingKind::Property;
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Bind Widget Property")));
			WBP->Modify();
			WBP->Bindings.Remove(Binding);
			WBP->Bindings.AddUnique(Binding);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
			GEditor->EndTransaction();

			// Compile so a type mismatch surfaces immediately.
			TSharedPtr<FJsonObject> Compile = CompileWidgetBlueprint(WBP, WantsSave(Args));
			bool bCompiled = false;
			Compile->TryGetBoolField(TEXT("compiled"), bCompiled);

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("widget"), WidgetName);
			Info->SetStringField(TEXT("property"), PropertyName);
			Info->SetStringField(TEXT("kind"), FunctionName.IsEmpty() ? TEXT("Property") : TEXT("Function"));
			Info->SetStringField(TEXT("source"), FunctionName.IsEmpty() ? VariableName : FunctionName);
			Info->SetObjectField(TEXT("compile"), Compile);

			if (!bCompiled)
			{
				return FMCPToolResult::SuccessStructured(FString::Printf(
					TEXT("Binding %s.%s -> %s was added but the Blueprint no longer compiles (type mismatch?). See compile.messages."),
					*WidgetName, *PropertyName, FunctionName.IsEmpty() ? *VariableName : *FunctionName), Info);
			}
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Bound %s.%s to %s '%s'"),
				*WidgetName, *PropertyName, FunctionName.IsEmpty() ? TEXT("variable") : TEXT("function"),
				FunctionName.IsEmpty() ? *VariableName : *FunctionName), Info);
		});

	// ================================================================
	// list_widget_bindings
	// ================================================================
	MCP_TOOL(Registry, "list_widget_bindings")
		.Description(TEXT("List the property bindings (designer 'Bind' dropdown) and bound event nodes in a Widget Blueprint."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Optional: only bindings on this widget"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			Args->TryGetStringField(TEXT("widget_name"), WidgetName);

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			TArray<TSharedPtr<FJsonValue>> PropertyBindings;
			for (const FDelegateEditorBinding& B : WBP->Bindings)
			{
				if (!WidgetName.IsEmpty() && B.ObjectName != WidgetName) continue;
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("widget"), B.ObjectName);
				O->SetStringField(TEXT("property"), B.PropertyName.ToString());
				O->SetStringField(TEXT("kind"), B.Kind == EBindingKind::Function ? TEXT("Function") : TEXT("Property"));
				O->SetStringField(TEXT("source"), B.Kind == EBindingKind::Function ? B.FunctionName.ToString() : B.SourceProperty.ToString());
				O->SetBoolField(TEXT("target_exists"), B.DoesBindingTargetExist(WBP));
				PropertyBindings.Add(MakeShared<FJsonValueObject>(O));
			}

			TArray<TSharedPtr<FJsonValue>> EventBindings;
			TArray<UK2Node_ComponentBoundEvent*> EventNodes;
			FBlueprintEditorUtils::GetAllNodesOfClass(WBP, EventNodes);
			for (UK2Node_ComponentBoundEvent* Node : EventNodes)
			{
				if (!Node) continue;
				if (!WidgetName.IsEmpty() && Node->ComponentPropertyName.ToString() != WidgetName) continue;
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("widget"), Node->ComponentPropertyName.ToString());
				O->SetStringField(TEXT("event"), Node->DelegatePropertyName.ToString());
				O->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
				O->SetStringField(TEXT("graph"), Node->GetGraph() ? Node->GetGraph()->GetName() : TEXT(""));
				UEdGraphPin* Then = Node->GetThenPin();
				O->SetNumberField(TEXT("then_links"), Then ? Then->LinkedTo.Num() : 0);
				EventBindings.Add(MakeShared<FJsonValueObject>(O));
			}

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetArrayField(TEXT("property_bindings"), PropertyBindings);
			Info->SetArrayField(TEXT("event_bindings"), EventBindings);
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%d property binding(s), %d event binding(s) in %s"),
				PropertyBindings.Num(), EventBindings.Num(), *WBP->GetName()), Info);
		});

	// ================================================================
	// remove_widget_binding
	// ================================================================
	MCP_TOOL(Registry, "remove_widget_binding")
		.Description(TEXT("Remove a property binding from a widget (property_name), or a bound event node (event_name) from the graph."))
		.Destructive()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget"), true)
		.StringArg(TEXT("property_name"), TEXT("Bound property to unbind (e.g. Text)"))
		.StringArg(TEXT("event_name"), TEXT("Bound event to remove (e.g. OnClicked)"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName, PropertyName, EventName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			Args->TryGetStringField(TEXT("property_name"), PropertyName);
			Args->TryGetStringField(TEXT("event_name"), EventName);
			if (PropertyName.IsEmpty() && EventName.IsEmpty()) return FMCPToolResult::Error(TEXT("Pass property_name or event_name"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Remove Widget Binding")));
			WBP->Modify();
			TArray<FString> Removed;

			if (!PropertyName.IsEmpty())
			{
				const int32 Before = WBP->Bindings.Num();
				WBP->Bindings.RemoveAll([&](const FDelegateEditorBinding& B)
				{
					return B.ObjectName == WidgetName && B.PropertyName == FName(*PropertyName);
				});
				if (WBP->Bindings.Num() != Before) Removed.Add(FString::Printf(TEXT("property binding %s"), *PropertyName));
			}

			if (!EventName.IsEmpty())
			{
				FName DelegateName(*EventName);
				const UK2Node_ComponentBoundEvent* Node = FKismetEditorUtilities::FindBoundEventForComponent(WBP, DelegateName, FName(*WidgetName));
				if (!Node)
				{
					DelegateName = FName(*FString::Printf(TEXT("On%s"), *EventName));
					Node = FKismetEditorUtilities::FindBoundEventForComponent(WBP, DelegateName, FName(*WidgetName));
				}
				if (Node)
				{
					UEdGraph* Graph = Node->GetGraph();
					if (Graph) { Graph->Modify(); }
					FBlueprintEditorUtils::RemoveNode(WBP, const_cast<UK2Node_ComponentBoundEvent*>(Node), true);
					Removed.Add(FString::Printf(TEXT("event %s"), *DelegateName.ToString()));
				}
			}

			GEditor->EndTransaction();
			if (Removed.Num() == 0)
				return FMCPToolResult::Success(FString::Printf(TEXT("Nothing to remove on '%s'"), *WidgetName));

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
			SaveWidgetBlueprint(WBP, WantsSave(Args));
			return FMCPToolResult::Success(FString::Printf(TEXT("Removed from '%s': %s"), *WidgetName, *FString::Join(Removed, TEXT(", "))));
		});
}

} // namespace MCPWidgetTools::Events
