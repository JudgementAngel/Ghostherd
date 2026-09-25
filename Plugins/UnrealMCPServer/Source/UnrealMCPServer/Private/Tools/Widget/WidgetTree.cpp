// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "UObject/SavePackage.h"

// UMG Widget Blueprint editing
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetToolsModule.h"
#include "Misc/PackageName.h"
#include "Engine/Texture2D.h"

// Blueprint event graph (for bind_widget_event)
#include "K2Node_ComponentBoundEvent.h"
#include "EdGraphSchema_K2.h"

// UMG Widget types
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/GridPanel.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/GridSlot.h"
#include "Components/UniformGridSlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/WrapBoxSlot.h"
#include "Components/SizeBoxSlot.h"
#include "Components/BorderSlot.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/StackBoxSlot.h"
#include "Components/NamedSlotInterface.h"
#include "WidgetBlueprintOperationUtils.h"
#include "WidgetBlueprintEditorUtils.h"
#include "Components/Border.h"
#include "Components/WrapBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/EditableTextBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Spacer.h"
#include "Components/RichTextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/PanelWidget.h"

#include "Tools/Widget/WidgetCommon.h"

namespace MCPWidgetTools::Tree
{

using namespace MCPWidgetTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// get_widget_tree - Read widget hierarchy
	// ================================================================
	MCP_TOOL(Registry, "get_widget_tree")
		.Description(TEXT("Read the widget hierarchy of a Widget Blueprint. Returns a JSON tree showing all widgets, their types, names, children, and optionally their properties and slot configuration."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.BoolArg(TEXT("include_properties"), TEXT("Include basic properties like text, color, anchors (default: false)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			if (!WBP->WidgetTree) return FMCPToolResult::Error(TEXT("Widget Blueprint has no WidgetTree"));

			bool bIncludeProperties = false;
			Args->TryGetBoolField(TEXT("include_properties"), bIncludeProperties);

			UWidget* Root = WBP->WidgetTree->RootWidget;
			if (!Root) return FMCPToolResult::Success(TEXT("Widget Blueprint has no root widget (empty tree)"));

			TSharedPtr<FJsonObject> Tree = SerializeWidget(Root, bIncludeProperties);
			return FMCPToolResult::SuccessStructured(JsonToString(Tree), Tree);
		});
	// ================================================================
	// add_widget - Add a widget to the widget tree
	// ================================================================
	MCP_TOOL(Registry, "add_widget")
		.Description(TEXT("Add a widget to a Widget Blueprint's widget tree. Supports all standard UMG widgets by type name (any concrete UWidget subclass resolves by reflection: ListView, WidgetSwitcher, NamedSlot, ...), or an instance of another Widget Blueprint via widget_class_path (composition, e.g. put WBP_HealthBar inside WBP_HUD). Returns the created widget's name."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_type"), TEXT("Native widget type to add (CanvasPanel, VerticalBox, HorizontalBox, GridPanel, Overlay, SizeBox, ScaleBox, Border, WrapBox, UniformGridPanel, ScrollBox, Button, TextBlock, Image, EditableTextBox, Slider, ProgressBar, CheckBox, ComboBoxString, Spacer, RichTextBlock, WidgetSwitcher, ListView, NamedSlot, ... any UWidget subclass name). Ignored when widget_class_path is set."))
		.StringArg(TEXT("widget_class_path"), TEXT("Widget Blueprint asset path to instance instead of a native type (e.g. '/Game/UI/WBP_HealthBar')"))
		.StringArg(TEXT("widget_name"), TEXT("Custom name for the widget (auto-generated if omitted)"))
		.StringArg(TEXT("parent_widget_name"), TEXT("Name of parent widget. If omitted, adds to root widget (or becomes the root if the tree is empty)."))
		.IntArg(TEXT("index"), TEXT("Insertion index among siblings (appends to end if omitted)"))
		.BoolArg(TEXT("is_variable"), TEXT("Expose the widget as a Blueprint variable ('Is Variable' checkbox in the designer). Engine default: true for leaf widgets, false for layout panels."))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true). Pass false when batching many edits, then call compile_widget_blueprint."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetType, WidgetClassPath;
			Args->TryGetStringField(TEXT("widget_type"), WidgetType);
			Args->TryGetStringField(TEXT("widget_class_path"), WidgetClassPath);
			if (WidgetType.IsEmpty() && WidgetClassPath.IsEmpty())
				return FMCPToolResult::Error(TEXT("Either widget_type or widget_class_path is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			if (!WBP->WidgetTree) return FMCPToolResult::Error(TEXT("Widget Blueprint has no WidgetTree"));

			// Find parent (nullptr = root; if there is no root the new widget becomes it)
			UWidget* ParentWidget = nullptr;
			FString ParentName;
			if (Args->TryGetStringField(TEXT("parent_widget_name"), ParentName) && !ParentName.IsEmpty())
			{
				ParentWidget = FindWidgetByName(WBP, ParentName);
				if (!ParentWidget)
					return FMCPToolResult::Error(FString::Printf(TEXT("Parent widget not found: %s"), *ParentName));
				if (!Cast<UPanelWidget>(ParentWidget))
					return FMCPToolResult::Error(FString::Printf(TEXT("Parent widget '%s' is not a panel (cannot have children). To fill a named slot exposed by a sub-widget instance use set_named_slot_content."), *ParentName));
			}
			else
			{
				ParentWidget = WBP->WidgetTree->RootWidget;
				if (ParentWidget && !Cast<UPanelWidget>(ParentWidget))
					return FMCPToolResult::Error(TEXT("Root widget is not a panel. Pass parent_widget_name or wrap the root with wrap_widget."));
			}

			FString WidgetName;
			Args->TryGetStringField(TEXT("widget_name"), WidgetName);

			int32 Index = -1;
			if (Args->HasField(TEXT("index"))) Index = (int32)Args->GetNumberField(TEXT("index"));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Widget")));
			WBP->Modify();

			FString ConstructErr;
			UWidget* NewWidget = ConstructWidgetForTree(WBP, WidgetType, WidgetClassPath, WidgetName, ConstructErr);
			if (!NewWidget)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(ConstructErr);
			}

			// Optional: override the "Is Variable" flag (bIsVariable is not editable via Python/set_editor_property)
			bool bIsVariable = false;
			if (Args->TryGetBoolField(TEXT("is_variable"), bIsVariable))
			{
				NewWidget->bIsVariable = bIsVariable;
			}

			// Engine-canonical insertion: honours index, handles the empty-tree/root case,
			// fires OnVariableAdded and marks the Blueprint structurally modified.
			FText AddErr;
			if (!FWidgetBlueprintOperationUtils::AddWidget(WBP, NewWidget, ParentWidget, Index, AddErr))
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to add widget: %s"), *AddErr.ToString()));
			}

			GEditor->EndTransaction();

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			int32 FinalIndex = -1;
			if (UPanelWidget* P = NewWidget->GetParent()) FinalIndex = P->GetChildIndex(NewWidget);

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("widget_name"), NewWidget->GetName());
			Info->SetStringField(TEXT("type"), NewWidget->GetClass()->GetName());
			Info->SetStringField(TEXT("parent"), NewWidget->GetParent() ? NewWidget->GetParent()->GetName() : TEXT("(root)"));
			Info->SetNumberField(TEXT("index"), FinalIndex);
			Info->SetBoolField(TEXT("is_variable"), NewWidget->bIsVariable);

			return FMCPToolResult::SuccessStructured(FString::Printf(
				TEXT("Added %s widget '%s' to parent '%s' at index %d in %s (is_variable=%s)"),
				*NewWidget->GetClass()->GetName(), *NewWidget->GetName(),
				NewWidget->GetParent() ? *NewWidget->GetParent()->GetName() : TEXT("(root)"),
				FinalIndex, *AssetPath, NewWidget->bIsVariable ? TEXT("true") : TEXT("false")), Info);
		});
	// ================================================================
	// remove_widget - Remove a widget from the tree
	// ================================================================
	MCP_TOOL(Registry, "remove_widget")
		.Description(TEXT("Remove a widget from a Widget Blueprint's widget tree by name. Also removes all children of that widget."))
		.Destructive()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget to remove"), true)
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetName;
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName))
				return FMCPToolResult::Error(TEXT("widget_name is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			if (Widget == WBP->WidgetTree->RootWidget)
				return FMCPToolResult::Error(TEXT("Cannot remove the root widget. Use create_widget_blueprint to create a new one."));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Remove Widget")));
			WBP->Modify();

			// Remove from parent
			UPanelWidget* Parent = Widget->GetParent();
			if (Parent)
			{
				Parent->RemoveChild(Widget);
			}
			FText RemoveErr; // v5: engine path retires the widget variable GUID; WidgetTree->RemoveWidget alone left a stale GUID and tripped compiler ensures.
			if (!FWidgetBlueprintOperationUtils::RemoveWidget(WBP, Widget, RemoveErr)) { GEditor->EndTransaction(); return FMCPToolResult::Error(RemoveErr.IsEmpty() ? TEXT("Engine refused the removal") : RemoveErr.ToString()); }

			GEditor->EndTransaction();
			SaveWidgetBlueprint(WBP, WantsSave(Args));

			return FMCPToolResult::Success(FString::Printf(TEXT("Removed widget '%s' from %s"), *WidgetName, *AssetPath));
		});
	// ================================================================
	// set_widget_slot - Configure slot/layout properties
	// ================================================================
	MCP_TOOL(Registry, "set_widget_slot")
		.Description(TEXT("Configure a widget's slot properties (positioning within its parent container). CanvasPanel: anchors, offsets, alignment, z-order, auto_size. VerticalBox/HorizontalBox: size rule (Auto/Fill), fill weight, alignment, padding. Overlay/ScrollBox/SizeBox/Border/ScaleBox/WidgetSwitcher/StackBox: alignment, padding. GridPanel: row/column/spans/layer + alignment/padding. UniformGrid: row/column + alignment. WrapBox: alignment, padding, fill_empty_space. Only provided fields are modified. For anything else use set_widget_slot_property."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget to configure slot for"), true)
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		// Grid / UniformGrid slot
		.IntArg(TEXT("row"), TEXT("Row (GridPanel / UniformGridPanel slot)"))
		.IntArg(TEXT("column"), TEXT("Column (GridPanel / UniformGridPanel slot)"))
		.IntArg(TEXT("row_span"), TEXT("Row span (GridPanel slot)"))
		.IntArg(TEXT("column_span"), TEXT("Column span (GridPanel slot)"))
		.IntArg(TEXT("layer"), TEXT("Layer (GridPanel slot)"))
		// WrapBox slot
		.BoolArg(TEXT("fill_empty_space"), TEXT("Fill remaining line space (WrapBox slot)"))
		.NumberArg(TEXT("fill_span_when_less_than"), TEXT("Fill the line when remaining space is less than this (WrapBox slot)"))
		// CanvasPanel slot
		.NumberArg(TEXT("anchor_min_x"), TEXT("Anchor minimum X (0-1). CanvasPanel slot."))
		.NumberArg(TEXT("anchor_min_y"), TEXT("Anchor minimum Y (0-1). CanvasPanel slot."))
		.NumberArg(TEXT("anchor_max_x"), TEXT("Anchor maximum X (0-1). CanvasPanel slot."))
		.NumberArg(TEXT("anchor_max_y"), TEXT("Anchor maximum Y (0-1). CanvasPanel slot."))
		.NumberArg(TEXT("offset_left"), TEXT("Left offset in pixels. CanvasPanel slot."))
		.NumberArg(TEXT("offset_top"), TEXT("Top offset in pixels. CanvasPanel slot."))
		.NumberArg(TEXT("offset_right"), TEXT("Right offset/width. CanvasPanel slot."))
		.NumberArg(TEXT("offset_bottom"), TEXT("Bottom offset/height. CanvasPanel slot."))
		.NumberArg(TEXT("alignment_x"), TEXT("Alignment X (0-1). CanvasPanel slot."))
		.NumberArg(TEXT("alignment_y"), TEXT("Alignment Y (0-1). CanvasPanel slot."))
		.BoolArg(TEXT("auto_size"), TEXT("Auto size to content. CanvasPanel slot."))
		.IntArg(TEXT("z_order"), TEXT("Z-order for rendering. CanvasPanel slot."))
		// Box slot (Vertical/Horizontal)
		.EnumArg(TEXT("size_rule"), TEXT("Size rule for box slots"),
			{ TEXT("Auto"), TEXT("Fill") })
		.NumberArg(TEXT("fill_weight"), TEXT("Fill weight when size_rule is Fill (default: 1.0)"))
		.EnumArg(TEXT("halign"), TEXT("Horizontal alignment"),
			{ TEXT("Fill"), TEXT("Left"), TEXT("Center"), TEXT("Right") })
		.EnumArg(TEXT("valign"), TEXT("Vertical alignment"),
			{ TEXT("Fill"), TEXT("Top"), TEXT("Center"), TEXT("Bottom") })
		// Padding (for box/overlay slots)
		.NumberArg(TEXT("padding_left"), TEXT("Left padding"))
		.NumberArg(TEXT("padding_top"), TEXT("Top padding"))
		.NumberArg(TEXT("padding_right"), TEXT("Right padding"))
		.NumberArg(TEXT("padding_bottom"), TEXT("Bottom padding"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetName;
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName))
				return FMCPToolResult::Error(TEXT("widget_name is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			if (!Widget->Slot)
				return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' has no slot (it may be the root widget)"), *WidgetName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Slot")));
			WBP->Modify();

			TArray<FString> Changed;

			// Helper lambda for alignment enum parsing
			auto ParseHAlign = [](const FString& S) -> EHorizontalAlignment
			{
				if (S == TEXT("Left")) return EHorizontalAlignment::HAlign_Left;
				if (S == TEXT("Center")) return EHorizontalAlignment::HAlign_Center;
				if (S == TEXT("Right")) return EHorizontalAlignment::HAlign_Right;
				return EHorizontalAlignment::HAlign_Fill;
			};
			auto ParseVAlign = [](const FString& S) -> EVerticalAlignment
			{
				if (S == TEXT("Top")) return EVerticalAlignment::VAlign_Top;
				if (S == TEXT("Center")) return EVerticalAlignment::VAlign_Center;
				if (S == TEXT("Bottom")) return EVerticalAlignment::VAlign_Bottom;
				return EVerticalAlignment::VAlign_Fill;
			};

			if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				// Anchors
				bool bHasAnchor = Args->HasField(TEXT("anchor_min_x")) || Args->HasField(TEXT("anchor_min_y"))
					|| Args->HasField(TEXT("anchor_max_x")) || Args->HasField(TEXT("anchor_max_y"));
				if (bHasAnchor)
				{
					FAnchors Anchors = CS->GetAnchors();
					if (Args->HasField(TEXT("anchor_min_x"))) Anchors.Minimum.X = (float)Args->GetNumberField(TEXT("anchor_min_x"));
					if (Args->HasField(TEXT("anchor_min_y"))) Anchors.Minimum.Y = (float)Args->GetNumberField(TEXT("anchor_min_y"));
					if (Args->HasField(TEXT("anchor_max_x"))) Anchors.Maximum.X = (float)Args->GetNumberField(TEXT("anchor_max_x"));
					if (Args->HasField(TEXT("anchor_max_y"))) Anchors.Maximum.Y = (float)Args->GetNumberField(TEXT("anchor_max_y"));
					CS->SetAnchors(Anchors);
					Changed.Add(FString::Printf(TEXT("Anchors=(%.2f,%.2f)-(%.2f,%.2f)"),
						Anchors.Minimum.X, Anchors.Minimum.Y, Anchors.Maximum.X, Anchors.Maximum.Y));
				}

				// Offsets
				bool bHasOffset = Args->HasField(TEXT("offset_left")) || Args->HasField(TEXT("offset_top"))
					|| Args->HasField(TEXT("offset_right")) || Args->HasField(TEXT("offset_bottom"));
				if (bHasOffset)
				{
					FMargin Offsets = CS->GetOffsets();
					if (Args->HasField(TEXT("offset_left"))) Offsets.Left = (float)Args->GetNumberField(TEXT("offset_left"));
					if (Args->HasField(TEXT("offset_top"))) Offsets.Top = (float)Args->GetNumberField(TEXT("offset_top"));
					if (Args->HasField(TEXT("offset_right"))) Offsets.Right = (float)Args->GetNumberField(TEXT("offset_right"));
					if (Args->HasField(TEXT("offset_bottom"))) Offsets.Bottom = (float)Args->GetNumberField(TEXT("offset_bottom"));
					CS->SetOffsets(Offsets);
					Changed.Add(FString::Printf(TEXT("Offsets=(L=%.0f T=%.0f R=%.0f B=%.0f)"),
						Offsets.Left, Offsets.Top, Offsets.Right, Offsets.Bottom));
				}

				// Alignment
				bool bHasAlignX = Args->HasField(TEXT("alignment_x"));
				bool bHasAlignY = Args->HasField(TEXT("alignment_y"));
				if (bHasAlignX || bHasAlignY)
				{
					FVector2D Alignment = CS->GetAlignment();
					if (bHasAlignX) Alignment.X = (float)Args->GetNumberField(TEXT("alignment_x"));
					if (bHasAlignY) Alignment.Y = (float)Args->GetNumberField(TEXT("alignment_y"));
					CS->SetAlignment(Alignment);
					Changed.Add(FString::Printf(TEXT("Alignment=(%.2f, %.2f)"), Alignment.X, Alignment.Y));
				}

				// Auto size
				bool bAutoSize;
				if (Args->TryGetBoolField(TEXT("auto_size"), bAutoSize))
				{
					CS->SetAutoSize(bAutoSize);
					Changed.Add(FString::Printf(TEXT("AutoSize=%s"), bAutoSize ? TEXT("true") : TEXT("false")));
				}

				// Z-order
				if (Args->HasField(TEXT("z_order")))
				{
					int32 ZOrder = (int32)Args->GetNumberField(TEXT("z_order"));
					CS->SetZOrder(ZOrder);
					Changed.Add(FString::Printf(TEXT("ZOrder=%d"), ZOrder));
				}
			}
			else if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Widget->Slot))
			{
				FString SizeRule;
				if (Args->TryGetStringField(TEXT("size_rule"), SizeRule))
				{
					FSlateChildSize Size;
					Size.SizeRule = (SizeRule == TEXT("Fill")) ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic;
					if (Args->HasField(TEXT("fill_weight"))) Size.Value = (float)Args->GetNumberField(TEXT("fill_weight"));
					VS->SetSize(Size);
					Changed.Add(FString::Printf(TEXT("SizeRule=%s"), *SizeRule));
				}

				FString HAlignStr;
				if (Args->TryGetStringField(TEXT("halign"), HAlignStr))
				{
					VS->SetHorizontalAlignment(ParseHAlign(HAlignStr));
					Changed.Add(FString::Printf(TEXT("HAlign=%s"), *HAlignStr));
				}

				FString VAlignStr;
				if (Args->TryGetStringField(TEXT("valign"), VAlignStr))
				{
					VS->SetVerticalAlignment(ParseVAlign(VAlignStr));
					Changed.Add(FString::Printf(TEXT("VAlign=%s"), *VAlignStr));
				}

				bool bHasPad = Args->HasField(TEXT("padding_left")) || Args->HasField(TEXT("padding_top"))
					|| Args->HasField(TEXT("padding_right")) || Args->HasField(TEXT("padding_bottom"));
				if (bHasPad)
				{
					FMargin Pad;
					Pad.Left = Args->HasField(TEXT("padding_left")) ? (float)Args->GetNumberField(TEXT("padding_left")) : 0.0f;
					Pad.Top = Args->HasField(TEXT("padding_top")) ? (float)Args->GetNumberField(TEXT("padding_top")) : 0.0f;
					Pad.Right = Args->HasField(TEXT("padding_right")) ? (float)Args->GetNumberField(TEXT("padding_right")) : 0.0f;
					Pad.Bottom = Args->HasField(TEXT("padding_bottom")) ? (float)Args->GetNumberField(TEXT("padding_bottom")) : 0.0f;
					VS->SetPadding(Pad);
					Changed.Add(TEXT("Padding updated"));
				}
			}
			else if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Widget->Slot))
			{
				FString SizeRule;
				if (Args->TryGetStringField(TEXT("size_rule"), SizeRule))
				{
					FSlateChildSize Size;
					Size.SizeRule = (SizeRule == TEXT("Fill")) ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic;
					if (Args->HasField(TEXT("fill_weight"))) Size.Value = (float)Args->GetNumberField(TEXT("fill_weight"));
					HS->SetSize(Size);
					Changed.Add(FString::Printf(TEXT("SizeRule=%s"), *SizeRule));
				}

				FString HAlignStr;
				if (Args->TryGetStringField(TEXT("halign"), HAlignStr))
				{
					HS->SetHorizontalAlignment(ParseHAlign(HAlignStr));
					Changed.Add(FString::Printf(TEXT("HAlign=%s"), *HAlignStr));
				}

				FString VAlignStr;
				if (Args->TryGetStringField(TEXT("valign"), VAlignStr))
				{
					HS->SetVerticalAlignment(ParseVAlign(VAlignStr));
					Changed.Add(FString::Printf(TEXT("VAlign=%s"), *VAlignStr));
				}

				bool bHasPad = Args->HasField(TEXT("padding_left")) || Args->HasField(TEXT("padding_top"))
					|| Args->HasField(TEXT("padding_right")) || Args->HasField(TEXT("padding_bottom"));
				if (bHasPad)
				{
					FMargin Pad;
					Pad.Left = Args->HasField(TEXT("padding_left")) ? (float)Args->GetNumberField(TEXT("padding_left")) : 0.0f;
					Pad.Top = Args->HasField(TEXT("padding_top")) ? (float)Args->GetNumberField(TEXT("padding_top")) : 0.0f;
					Pad.Right = Args->HasField(TEXT("padding_right")) ? (float)Args->GetNumberField(TEXT("padding_right")) : 0.0f;
					Pad.Bottom = Args->HasField(TEXT("padding_bottom")) ? (float)Args->GetNumberField(TEXT("padding_bottom")) : 0.0f;
					HS->SetPadding(Pad);
					Changed.Add(TEXT("Padding updated"));
				}
			}
			else if (UOverlaySlot* OS = Cast<UOverlaySlot>(Widget->Slot))
			{
				FString HAlignStr;
				if (Args->TryGetStringField(TEXT("halign"), HAlignStr))
				{
					OS->SetHorizontalAlignment(ParseHAlign(HAlignStr));
					Changed.Add(FString::Printf(TEXT("HAlign=%s"), *HAlignStr));
				}

				FString VAlignStr;
				if (Args->TryGetStringField(TEXT("valign"), VAlignStr))
				{
					OS->SetVerticalAlignment(ParseVAlign(VAlignStr));
					Changed.Add(FString::Printf(TEXT("VAlign=%s"), *VAlignStr));
				}

				bool bHasPad = Args->HasField(TEXT("padding_left")) || Args->HasField(TEXT("padding_top"))
					|| Args->HasField(TEXT("padding_right")) || Args->HasField(TEXT("padding_bottom"));
				if (bHasPad)
				{
					FMargin Pad;
					Pad.Left = Args->HasField(TEXT("padding_left")) ? (float)Args->GetNumberField(TEXT("padding_left")) : 0.0f;
					Pad.Top = Args->HasField(TEXT("padding_top")) ? (float)Args->GetNumberField(TEXT("padding_top")) : 0.0f;
					Pad.Right = Args->HasField(TEXT("padding_right")) ? (float)Args->GetNumberField(TEXT("padding_right")) : 0.0f;
					Pad.Bottom = Args->HasField(TEXT("padding_bottom")) ? (float)Args->GetNumberField(TEXT("padding_bottom")) : 0.0f;
					OS->SetPadding(Pad);
					Changed.Add(TEXT("Padding updated"));
				}
			}
			else if (UGridSlot* GS = Cast<UGridSlot>(Widget->Slot))
			{
				if (Args->HasField(TEXT("row")))         { GS->SetRow((int32)Args->GetNumberField(TEXT("row"))); Changed.Add(TEXT("Row")); }
				if (Args->HasField(TEXT("column")))      { GS->SetColumn((int32)Args->GetNumberField(TEXT("column"))); Changed.Add(TEXT("Column")); }
				if (Args->HasField(TEXT("row_span")))    { GS->SetRowSpan((int32)Args->GetNumberField(TEXT("row_span"))); Changed.Add(TEXT("RowSpan")); }
				if (Args->HasField(TEXT("column_span"))) { GS->SetColumnSpan((int32)Args->GetNumberField(TEXT("column_span"))); Changed.Add(TEXT("ColumnSpan")); }
				if (Args->HasField(TEXT("layer")))       { GS->SetLayer((int32)Args->GetNumberField(TEXT("layer"))); Changed.Add(TEXT("Layer")); }
				FString HAlignStr, VAlignStr;
				if (Args->TryGetStringField(TEXT("halign"), HAlignStr)) { GS->SetHorizontalAlignment(ParseHAlign(HAlignStr)); Changed.Add(TEXT("HAlign")); }
				if (Args->TryGetStringField(TEXT("valign"), VAlignStr)) { GS->SetVerticalAlignment(ParseVAlign(VAlignStr)); Changed.Add(TEXT("VAlign")); }
				if (Args->HasField(TEXT("padding_left")) || Args->HasField(TEXT("padding_top")) || Args->HasField(TEXT("padding_right")) || Args->HasField(TEXT("padding_bottom")))
				{
					FMargin Pad = GS->GetPadding();
					if (Args->HasField(TEXT("padding_left")))   Pad.Left   = (float)Args->GetNumberField(TEXT("padding_left"));
					if (Args->HasField(TEXT("padding_top")))    Pad.Top    = (float)Args->GetNumberField(TEXT("padding_top"));
					if (Args->HasField(TEXT("padding_right")))  Pad.Right  = (float)Args->GetNumberField(TEXT("padding_right"));
					if (Args->HasField(TEXT("padding_bottom"))) Pad.Bottom = (float)Args->GetNumberField(TEXT("padding_bottom"));
					GS->SetPadding(Pad); Changed.Add(TEXT("Padding"));
				}
			}
			else if (UUniformGridSlot* UGS = Cast<UUniformGridSlot>(Widget->Slot))
			{
				if (Args->HasField(TEXT("row")))    { UGS->SetRow((int32)Args->GetNumberField(TEXT("row"))); Changed.Add(TEXT("Row")); }
				if (Args->HasField(TEXT("column"))) { UGS->SetColumn((int32)Args->GetNumberField(TEXT("column"))); Changed.Add(TEXT("Column")); }
				FString HAlignStr, VAlignStr;
				if (Args->TryGetStringField(TEXT("halign"), HAlignStr)) { UGS->SetHorizontalAlignment(ParseHAlign(HAlignStr)); Changed.Add(TEXT("HAlign")); }
				if (Args->TryGetStringField(TEXT("valign"), VAlignStr)) { UGS->SetVerticalAlignment(ParseVAlign(VAlignStr)); Changed.Add(TEXT("VAlign")); }
			}
			else if (UWrapBoxSlot* WS = Cast<UWrapBoxSlot>(Widget->Slot))
			{
				FString HAlignStr, VAlignStr;
				if (Args->TryGetStringField(TEXT("halign"), HAlignStr)) { WS->SetHorizontalAlignment(ParseHAlign(HAlignStr)); Changed.Add(TEXT("HAlign")); }
				if (Args->TryGetStringField(TEXT("valign"), VAlignStr)) { WS->SetVerticalAlignment(ParseVAlign(VAlignStr)); Changed.Add(TEXT("VAlign")); }
				bool bFill;
				if (Args->TryGetBoolField(TEXT("fill_empty_space"), bFill)) { WS->SetFillEmptySpace(bFill); Changed.Add(TEXT("FillEmptySpace")); }
				if (Args->HasField(TEXT("fill_span_when_less_than"))) { WS->SetFillSpanWhenLessThan((float)Args->GetNumberField(TEXT("fill_span_when_less_than"))); Changed.Add(TEXT("FillSpanWhenLessThan")); }
				if (Args->HasField(TEXT("padding_left")) || Args->HasField(TEXT("padding_top")) || Args->HasField(TEXT("padding_right")) || Args->HasField(TEXT("padding_bottom")))
				{
					FMargin Pad(
						Args->HasField(TEXT("padding_left"))   ? (float)Args->GetNumberField(TEXT("padding_left"))   : 0.0f,
						Args->HasField(TEXT("padding_top"))    ? (float)Args->GetNumberField(TEXT("padding_top"))    : 0.0f,
						Args->HasField(TEXT("padding_right"))  ? (float)Args->GetNumberField(TEXT("padding_right"))  : 0.0f,
						Args->HasField(TEXT("padding_bottom")) ? (float)Args->GetNumberField(TEXT("padding_bottom")) : 0.0f);
					WS->SetPadding(Pad); Changed.Add(TEXT("Padding"));
				}
			}
			else if (Cast<UScrollBoxSlot>(Widget->Slot) || Cast<USizeBoxSlot>(Widget->Slot) || Cast<UBorderSlot>(Widget->Slot)
				|| Cast<UScaleBoxSlot>(Widget->Slot) || Cast<UWidgetSwitcherSlot>(Widget->Slot) || Cast<UStackBoxSlot>(Widget->Slot))
			{
				// Single-child / simple slots: alignment + padding via reflection-free setters.
				FString HAlignStr, VAlignStr;
				const bool bHasH = Args->TryGetStringField(TEXT("halign"), HAlignStr);
				const bool bHasV = Args->TryGetStringField(TEXT("valign"), VAlignStr);
				const bool bHasPad = Args->HasField(TEXT("padding_left")) || Args->HasField(TEXT("padding_top")) || Args->HasField(TEXT("padding_right")) || Args->HasField(TEXT("padding_bottom"));
				FMargin Pad(
					Args->HasField(TEXT("padding_left"))   ? (float)Args->GetNumberField(TEXT("padding_left"))   : 0.0f,
					Args->HasField(TEXT("padding_top"))    ? (float)Args->GetNumberField(TEXT("padding_top"))    : 0.0f,
					Args->HasField(TEXT("padding_right"))  ? (float)Args->GetNumberField(TEXT("padding_right"))  : 0.0f,
					Args->HasField(TEXT("padding_bottom")) ? (float)Args->GetNumberField(TEXT("padding_bottom")) : 0.0f);

				if (UScrollBoxSlot* SBS = Cast<UScrollBoxSlot>(Widget->Slot))
				{
					if (bHasH) SBS->SetHorizontalAlignment(ParseHAlign(HAlignStr));
					if (bHasV) SBS->SetVerticalAlignment(ParseVAlign(VAlignStr));
					if (bHasPad) SBS->SetPadding(Pad);
				}
				else if (USizeBoxSlot* SZS = Cast<USizeBoxSlot>(Widget->Slot))
				{
					if (bHasH) SZS->SetHorizontalAlignment(ParseHAlign(HAlignStr));
					if (bHasV) SZS->SetVerticalAlignment(ParseVAlign(VAlignStr));
					if (bHasPad) SZS->SetPadding(Pad);
				}
				else if (UBorderSlot* BS = Cast<UBorderSlot>(Widget->Slot))
				{
					if (bHasH) BS->SetHorizontalAlignment(ParseHAlign(HAlignStr));
					if (bHasV) BS->SetVerticalAlignment(ParseVAlign(VAlignStr));
					if (bHasPad) BS->SetPadding(Pad);
				}
				else if (UScaleBoxSlot* SCS = Cast<UScaleBoxSlot>(Widget->Slot))
				{
					if (bHasH) SCS->SetHorizontalAlignment(ParseHAlign(HAlignStr));
					if (bHasV) SCS->SetVerticalAlignment(ParseVAlign(VAlignStr));
					// ScaleBoxSlot padding is deprecated since 5.1 - ignored.
				}
				else if (UWidgetSwitcherSlot* WSS = Cast<UWidgetSwitcherSlot>(Widget->Slot))
				{
					if (bHasH) WSS->SetHorizontalAlignment(ParseHAlign(HAlignStr));
					if (bHasV) WSS->SetVerticalAlignment(ParseVAlign(VAlignStr));
					if (bHasPad) WSS->SetPadding(Pad);
				}
				else if (UStackBoxSlot* STS = Cast<UStackBoxSlot>(Widget->Slot))
				{
					if (bHasH) STS->SetHorizontalAlignment(ParseHAlign(HAlignStr));
					if (bHasV) STS->SetVerticalAlignment(ParseVAlign(VAlignStr));
					if (bHasPad) STS->SetPadding(Pad);
				}
				if (bHasH) Changed.Add(TEXT("HAlign"));
				if (bHasV) Changed.Add(TEXT("VAlign"));
				if (bHasPad) Changed.Add(TEXT("Padding"));
			}
			else
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Unsupported slot type for widget '%s'. Slot type: %s. Use set_widget_slot_property for any slot property by name."),
					*WidgetName, *Widget->Slot->GetClass()->GetName()));
			}

			GEditor->EndTransaction();

			if (Changed.Num() == 0)
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("No slot properties changed on widget '%s'. Check that parameters match the parent container type."),
					*WidgetName));
			}

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Updated slot for widget '%s': %s"),
				*WidgetName, *FString::Join(Changed, TEXT(", "))));
		});
	// ================================================================
	// move_widget - Move/reparent a widget
	// ================================================================
	MCP_TOOL(Registry, "move_widget")
		.Description(TEXT("Move a widget to a different parent in the widget tree, or reorder within the same parent (index). Uses the designer's move logic, so cycles are refused and slot data is rebuilt for the new parent type."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget to move"), true)
		.StringArg(TEXT("new_parent_name"), TEXT("Name of the new parent widget (must be a panel). Omit to reorder within the current parent."))
		.IntArg(TEXT("index"), TEXT("Position among the parent's children (appends to end if omitted)"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetName, NewParentName;
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName))
				return FMCPToolResult::Error(TEXT("widget_name is required"));
			Args->TryGetStringField(TEXT("new_parent_name"), NewParentName);

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			UPanelWidget* NewParent = nullptr;
			if (!NewParentName.IsEmpty())
			{
				UWidget* NewParentWidget = FindWidgetByName(WBP, NewParentName);
				if (!NewParentWidget) return FMCPToolResult::Error(FString::Printf(TEXT("New parent not found: %s"), *NewParentName));
				NewParent = Cast<UPanelWidget>(NewParentWidget);
				if (!NewParent)
					return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a panel widget (cannot have children)"), *NewParentName));
			}
			else
			{
				NewParent = Widget->GetParent();
				if (!NewParent) return FMCPToolResult::Error(TEXT("Widget has no parent (root widget); pass new_parent_name"));
				if (!Args->HasField(TEXT("index"))) return FMCPToolResult::Error(TEXT("Pass new_parent_name and/or index"));
			}

			int32 Index = -1;
			if (Args->HasField(TEXT("index"))) Index = (int32)Args->GetNumberField(TEXT("index"));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Move Widget")));
			WBP->Modify();

			FText Err;
			const bool bOk = FWidgetBlueprintOperationUtils::MoveWidget(WBP, Widget, NewParent, Index, Err);

			GEditor->EndTransaction();
			if (!bOk)
				return FMCPToolResult::Error(FString::Printf(TEXT("Move failed: %s"), *Err.ToString()));

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			const int32 FinalIndex = Widget->GetParent() ? Widget->GetParent()->GetChildIndex(Widget) : -1;
			return FMCPToolResult::Success(FString::Printf(
				TEXT("Moved widget '%s' to parent '%s' at index %d"), *WidgetName, *NewParent->GetName(), FinalIndex));
		});

	// ================================================================
	// rename_widget - Rename a widget safely (fixes up graph refs, bindings, animations)
	// ================================================================
	MCP_TOOL(Registry, "rename_widget")
		.Description(TEXT("Rename a widget in a Widget Blueprint the way the designer does: the member variable, event nodes, variable get/set nodes, property bindings, animation bindings and navigation rules are all retargeted. (Renaming the UObject from Python breaks those references.)"))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Current name of the widget"), true)
		.StringArg(TEXT("new_name"), TEXT("New name (must be unique in the Blueprint and a valid identifier)"), true)
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName, NewName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			if (!Args->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty()) return FMCPToolResult::Error(TEXT("new_name is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			if (WidgetName == NewName)
				return FMCPToolResult::Success(FString::Printf(TEXT("Widget '%s' already has that name"), *WidgetName));

			FText VerifyErr;
			if (!FWidgetBlueprintOperationUtils::VerifyWidgetRename(WBP, Widget, FText::FromString(NewName), VerifyErr))
				return FMCPToolResult::ErrorStructured(EMCPError::InvalidName,
					FString::Printf(TEXT("Cannot rename '%s' to '%s': %s"), *WidgetName, *NewName, *VerifyErr.ToString()));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Rename Widget")));
			const bool bOk = FWidgetBlueprintOperationUtils::RenameWidget(WBP, Widget, NewName);
			GEditor->EndTransaction();

			if (!bOk) return FMCPToolResult::Error(FString::Printf(TEXT("Rename of '%s' failed (name may collide with a variable or function)"), *WidgetName));

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			return FMCPToolResult::Success(FString::Printf(TEXT("Renamed widget '%s' to '%s' in %s"), *WidgetName, *Widget->GetName(), *AssetPath));
		});

	// ================================================================
	// wrap_widget - Wrap a widget in a new parent (designer "Wrap With...")
	// ================================================================
	MCP_TOOL(Registry, "wrap_widget")
		.Description(TEXT("Wrap an existing widget in a new panel widget (designer 'Wrap With...'): e.g. put a TextBlock inside a new SizeBox or Border after the fact. The wrapper takes the widget's place in its parent; works on the root widget too."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget to wrap"), true)
		.StringArg(TEXT("wrapper_type"), TEXT("Panel widget type for the wrapper (SizeBox, Border, Overlay, VerticalBox, HorizontalBox, CanvasPanel, ScaleBox, ScrollBox, ...)"), true)
		.StringArg(TEXT("wrapper_name"), TEXT("Optional name for the new wrapper widget"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName, WrapperType, WrapperName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			if (!Args->TryGetStringField(TEXT("wrapper_type"), WrapperType)) return FMCPToolResult::Error(TEXT("wrapper_type is required"));
			Args->TryGetStringField(TEXT("wrapper_name"), WrapperName);

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			UClass* WrapperClass = WidgetClassFromTypeName(WrapperType);
			if (!WrapperClass) return FMCPToolResult::Error(FString::Printf(TEXT("Unknown widget type: %s"), *WrapperType));
			if (!WrapperClass->IsChildOf(UPanelWidget::StaticClass()))
				return FMCPToolResult::Error(FString::Printf(TEXT("%s is not a panel widget and cannot wrap other widgets"), *WrapperType));

			if (!WrapperName.IsEmpty() && FindWidgetByName(WBP, WrapperName))
				return FMCPToolResult::Error(FString::Printf(TEXT("A widget named '%s' already exists"), *WrapperName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Wrap Widget")));
			WBP->Modify();
			TArray<UWidget*> Wrappers = FWidgetBlueprintOperationUtils::WrapWidgets(WBP, { Widget }, WrapperClass);
			UWidget* Wrapper = Wrappers.Num() > 0 ? Wrappers[0] : nullptr;
			if (Wrapper && !WrapperName.IsEmpty())
			{
				FWidgetBlueprintOperationUtils::RenameWidget(WBP, Wrapper, WrapperName);
			}
			GEditor->EndTransaction();

			if (!Wrapper) return FMCPToolResult::Error(TEXT("Wrap failed (the engine returned no wrapper widget)"));

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Wrapped '%s' in new %s '%s' (parent: %s)"),
				*WidgetName, *WrapperClass->GetName(), *Wrapper->GetName(),
				Wrapper->GetParent() ? *Wrapper->GetParent()->GetName() : TEXT("(root)")));
		});

	// ================================================================
	// replace_widget - Replace a widget with another type (designer "Replace With...")
	// ================================================================
	MCP_TOOL(Registry, "replace_widget")
		.Description(TEXT("Replace a widget with a new widget of a different type, keeping its name, position and (where the classes are compatible) its children and property values. Graph references to compatible members are retargeted. Pass replace_with_child=true instead to remove a single-child panel and promote its child."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget to replace"), true)
		.StringArg(TEXT("new_type"), TEXT("Replacement widget type (e.g. 'Border' for a SizeBox, 'RichTextBlock' for a TextBlock)"))
		.BoolArg(TEXT("replace_with_child"), TEXT("Remove this single-child panel and put its child in its place"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName, NewType;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			Args->TryGetStringField(TEXT("new_type"), NewType);
			bool bWithChild = false;
			Args->TryGetBoolField(TEXT("replace_with_child"), bWithChild);
			if (NewType.IsEmpty() && !bWithChild) return FMCPToolResult::Error(TEXT("Pass new_type or replace_with_child=true"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			FText Err;
			bool bOk = false;
			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Replace Widget")));
			WBP->Modify();
			if (bWithChild)
			{
				bOk = FWidgetBlueprintOperationUtils::ReplaceWidgetWithChild(WBP, Widget, Err);
			}
			else
			{
				UClass* NewClass = WidgetClassFromTypeName(NewType);
				if (!NewClass)
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(FString::Printf(TEXT("Unknown widget type: %s"), *NewType));
				}
				bOk = FWidgetBlueprintOperationUtils::ReplaceWidgetWithTemplate(WBP, Widget, NewClass, Err);
			}
			GEditor->EndTransaction();

			if (!bOk) return FMCPToolResult::Error(FString::Printf(TEXT("Replace failed: %s"), *Err.ToString()));

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			UWidget* Replacement = FindWidgetByName(WBP, WidgetName);
			return FMCPToolResult::Success(FString::Printf(
				TEXT("Replaced '%s'%s. Widget now: %s"),
				*WidgetName, bWithChild ? TEXT(" with its child") : *FString::Printf(TEXT(" with %s"), *NewType),
				Replacement ? *FString::Printf(TEXT("%s (%s)"), *Replacement->GetName(), *Replacement->GetClass()->GetName()) : TEXT("(removed; see get_widget_tree)")));
		});

	// ================================================================
	// duplicate_widget - Deep-copy a widget subtree
	// ================================================================
	MCP_TOOL(Registry, "duplicate_widget")
		.Description(TEXT("Duplicate a widget and all of its children (designer copy/paste). The copy is inserted next to the original (or under target_parent_name) with unique names; property values and slot layout are preserved."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget to duplicate"), true)
		.StringArg(TEXT("new_name"), TEXT("Optional name for the top-level copy (children get auto-unique names)"))
		.StringArg(TEXT("target_parent_name"), TEXT("Panel to receive the copy (default: the original's parent)"))
		.IntArg(TEXT("index"), TEXT("Insertion index in the target parent (default: right after the original, or at the end)"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName, NewName, TargetParentName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			Args->TryGetStringField(TEXT("new_name"), NewName);
			Args->TryGetStringField(TEXT("target_parent_name"), TargetParentName);

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			UPanelWidget* TargetParent = nullptr;
			if (!TargetParentName.IsEmpty())
			{
				TargetParent = Cast<UPanelWidget>(FindWidgetByName(WBP, TargetParentName));
				if (!TargetParent) return FMCPToolResult::Error(FString::Printf(TEXT("Target parent '%s' not found or not a panel"), *TargetParentName));
			}
			else
			{
				TargetParent = Widget->GetParent();
				if (!TargetParent) return FMCPToolResult::Error(TEXT("Cannot duplicate the root widget without target_parent_name"));
			}
			if (!NewName.IsEmpty() && FindWidgetByName(WBP, NewName))
				return FMCPToolResult::Error(FString::Printf(TEXT("A widget named '%s' already exists"), *NewName));

			int32 Index = -1;
			if (Args->HasField(TEXT("index"))) Index = (int32)Args->GetNumberField(TEXT("index"));
			else if (TargetParent == Widget->GetParent()) Index = TargetParent->GetChildIndex(Widget) + 1;

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Duplicate Widget")));
			WBP->Modify();

			// Designer copy/paste path: export the subtree to text and import it back with unique names.
			FString Exported;
			FWidgetBlueprintEditorUtils::ExportWidgetsToText({ Widget }, Exported);

			TSet<UWidget*> Imported;
			TMap<FName, UWidgetSlotPair*> ExtraSlotData;
			FWidgetBlueprintEditorUtils::ImportWidgetsFromText(WBP, Exported, Imported, ExtraSlotData);

			// The top-level copy is the imported widget whose parent is not part of the import.
			UWidget* Copy = nullptr;
			for (UWidget* W : Imported)
			{
				if (!W) continue;
				UPanelWidget* P = W->GetParent();
				if (!P || !Imported.Contains(P)) { Copy = W; break; }
			}
			if (!Copy)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Duplicate failed: import produced no root widget"));
			}

			if (!NewName.IsEmpty())
			{
				Copy->Rename(*NewName, WBP->WidgetTree, REN_DontCreateRedirectors);
				Copy->SetDisplayLabel(NewName);
			}

			FText AddErr;
			if (!FWidgetBlueprintOperationUtils::AddWidget(WBP, Copy, TargetParent, Index, AddErr))
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(TEXT("Duplicate failed while attaching the copy: %s"), *AddErr.ToString()));
			}

			// Re-apply the original slot layout onto the new slot (same parent type only).
			if (Widget->Slot && Copy->Slot && Widget->Slot->GetClass() == Copy->Slot->GetClass())
			{
				TMap<FName, FString> SlotProps;
				FWidgetBlueprintEditorUtils::ExportPropertiesToText(Widget->Slot, SlotProps);
				SlotProps.Remove(TEXT("Parent"));
				SlotProps.Remove(TEXT("Content"));
				FWidgetBlueprintEditorUtils::ImportPropertiesFromText(Copy->Slot, SlotProps);
			}

			GEditor->EndTransaction();

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			TArray<FString> Names;
			for (UWidget* W : Imported) if (W) Names.Add(W->GetName());

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Duplicated '%s' as '%s' under '%s' (%d widget(s): %s)"),
				*WidgetName, *Copy->GetName(), *TargetParent->GetName(), Names.Num(), *FString::Join(Names, TEXT(", "))));
		});

	// ================================================================
	// set_named_slot_content - Fill a NamedSlot on a host widget
	// ================================================================
	MCP_TOOL(Registry, "set_named_slot_content")
		.Description(TEXT("Put a widget into a named slot exposed by a Widget Blueprint instance placed in this tree (a child WBP that contains NamedSlot widgets). A plain NamedSlot widget in this tree is a panel: use add_widget with parent_widget_name instead. Either create the content (widget_type / widget_class_path) or move an existing widget (content_widget_name). Call with no content arguments to list the host's slots."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("host_widget_name"), TEXT("Widget that owns the named slot(s)"), true)
		.StringArg(TEXT("slot_name"), TEXT("Named slot to fill (omit to just list slots; for a NamedSlot widget this is the widget's own name)"))
		.StringArg(TEXT("widget_type"), TEXT("Native widget type to create as content"))
		.StringArg(TEXT("widget_class_path"), TEXT("Widget Blueprint asset to instance as content"))
		.StringArg(TEXT("widget_name"), TEXT("Name for newly created content"))
		.StringArg(TEXT("content_widget_name"), TEXT("Existing widget to move into the slot"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, HostName, SlotName, WidgetType, WidgetClassPath, WidgetName, ContentName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("host_widget_name"), HostName)) return FMCPToolResult::Error(TEXT("host_widget_name is required"));
			Args->TryGetStringField(TEXT("slot_name"), SlotName);
			Args->TryGetStringField(TEXT("widget_type"), WidgetType);
			Args->TryGetStringField(TEXT("widget_class_path"), WidgetClassPath);
			Args->TryGetStringField(TEXT("widget_name"), WidgetName);
			Args->TryGetStringField(TEXT("content_widget_name"), ContentName);

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidget* Host = FindWidgetByName(WBP, HostName);
			if (!Host) return FMCPToolResult::Error(FString::Printf(TEXT("Host widget not found: %s"), *HostName));

			INamedSlotInterface* NamedSlots = Cast<INamedSlotInterface>(Host);
			if (!NamedSlots)
				return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' (%s) has no named slots"), *HostName, *Host->GetClass()->GetName()));

			TArray<FName> SlotNames;
			NamedSlots->GetSlotNames(SlotNames);

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> SlotArr;
			for (const FName& N : SlotNames)
			{
				TSharedPtr<FJsonObject> SO = MakeShared<FJsonObject>();
				SO->SetStringField(TEXT("slot"), N.ToString());
				UWidget* Existing = NamedSlots->GetContentForSlot(N);
				SO->SetStringField(TEXT("content"), Existing ? Existing->GetName() : TEXT(""));
				SlotArr.Add(MakeShared<FJsonValueObject>(SO));
			}
			Info->SetArrayField(TEXT("slots"), SlotArr);

			const bool bWantsContent = !WidgetType.IsEmpty() || !WidgetClassPath.IsEmpty() || !ContentName.IsEmpty();
			if (!bWantsContent)
			{
				return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("'%s' has %d named slot(s)"), *HostName, SlotNames.Num()), Info);
			}

			if (SlotName.IsEmpty())
			{
				if (SlotNames.Num() == 1) SlotName = SlotNames[0].ToString();
				else return FMCPToolResult::Error(TEXT("slot_name is required (host exposes several slots)"));
			}
			if (!SlotNames.Contains(FName(*SlotName)))
				return FMCPToolResult::Error(FString::Printf(TEXT("'%s' has no named slot '%s'"), *HostName, *SlotName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Named Slot Content")));
			WBP->Modify();
			Host->Modify();

			UWidget* Content = nullptr;
			if (!ContentName.IsEmpty())
			{
				Content = FindWidgetByName(WBP, ContentName);
				if (!Content) { GEditor->EndTransaction(); return FMCPToolResult::Error(FString::Printf(TEXT("Content widget not found: %s"), *ContentName)); }
				if (Content == Host || !FWidgetBlueprintOperationUtils::IsParentChildCycleFree(WBP, Content, Host))
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(TEXT("Cannot place a widget inside itself or one of its descendants"));
				}
				if (UPanelWidget* OldParent = Content->GetParent())
				{
					OldParent->Modify();
					OldParent->RemoveChild(Content);
				}
				else if (WBP->WidgetTree->RootWidget == Content)
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(TEXT("Cannot move the root widget into a named slot"));
				}
			}
			else
			{
				FString Err;
				Content = ConstructWidgetForTree(WBP, WidgetType, WidgetClassPath, WidgetName, Err);
				if (!Content) { GEditor->EndTransaction(); return FMCPToolResult::Error(Err); }
			}

			NamedSlots->SetContentForSlot(FName(*SlotName), Content);
			WBP->OnVariableAdded(Content->GetFName());
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

			GEditor->EndTransaction();

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Placed '%s' (%s) into named slot '%s' of '%s'"),
				*Content->GetName(), *Content->GetClass()->GetName(), *SlotName, *HostName));
		});
}

} // namespace MCPWidgetTools::Tree
