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
#include "WidgetBlueprintOperationUtils.h"
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

namespace MCPWidgetTools::Batch
{

using namespace MCPWidgetTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// batch_add_widgets - Add multiple widgets in one call
	// ================================================================
	MCP_TOOL(Registry, "batch_add_widgets")
		.Description(TEXT("Add multiple widgets to a Widget Blueprint in one call. Provide a JSON array of widget definitions, each with 'type' (required), 'name' (optional), 'parent' (optional, defaults to root), and 'is_variable' (optional bool, exposes the widget as a Blueprint variable). Much faster than individual add_widget calls for building complex UIs."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widgets_json"), TEXT("JSON array of widget definitions: [{\"type\":\"TextBlock\",\"name\":\"Txt_Title\",\"parent\":\"RootPanel\",\"is_variable\":true}, {\"class_path\":\"/Game/UI/WBP_HealthBar\",\"name\":\"HealthBar\",\"parent\":\"RootPanel\"} ...]"), true)
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetsJson;
			if (!Args->TryGetStringField(TEXT("widgets_json"), WidgetsJson))
				return FMCPToolResult::Error(TEXT("widgets_json is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			if (!WBP->WidgetTree) return FMCPToolResult::Error(TEXT("No WidgetTree"));

			// Parse the JSON array
			TArray<TSharedPtr<FJsonValue>> WidgetDefs;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WidgetsJson);
			if (!FJsonSerializer::Deserialize(Reader, WidgetDefs))
				return FMCPToolResult::Error(TEXT("Failed to parse widgets_json. Expected JSON array of objects."));

			// v5 preflight: every named entry must be unique within the batch and absent
			// from the tree; parents must resolve to an existing panel or an earlier batch
			// entry. Any violation refuses the whole batch before the asset is touched.
			{
				TSet<FString> BatchNames;
				TArray<FString> Problems;
				for (int32 I = 0; I < WidgetDefs.Num(); ++I)
				{
					const TSharedPtr<FJsonObject> Def = WidgetDefs[I].IsValid() ? WidgetDefs[I]->AsObject() : nullptr;
					if (!Def) { Problems.Add(FString::Printf(TEXT("[%d]: not an object"), I)); continue; }
					FString Name; Def->TryGetStringField(TEXT("name"), Name);
					if (!Name.IsEmpty())
					{
						if (BatchNames.Contains(Name)) Problems.Add(FString::Printf(TEXT("[%d]: name '%s' repeated in batch"), I, *Name));
						else if (WBP->WidgetTree->FindWidget(FName(*Name))) Problems.Add(FString::Printf(TEXT("[%d]: widget '%s' already exists"), I, *Name));
						BatchNames.Add(Name);
					}
					FString Parent;
					if (Def->TryGetStringField(TEXT("parent"), Parent) && !Parent.IsEmpty() && !BatchNames.Contains(Parent))
					{
						UWidget* PW = FindWidgetByName(WBP, Parent);
						if (!PW || !Cast<UPanelWidget>(PW)) Problems.Add(FString::Printf(TEXT("[%d]: parent '%s' not found or not a panel"), I, *Parent));
					}
				}
				if (Problems.Num() > 0)
					return FMCPToolResult::ErrorStructured(EMCPError::AlreadyExists,
						FString::Printf(TEXT("Batch refused before any change; %d problem(s): %s"), Problems.Num(), *FString::Join(Problems, TEXT("; "))),
						TEXT("Use unique widget names and existing panel parents; nothing was modified."));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Batch Add Widgets")));
			WBP->Modify();

			TArray<FString> Added;
			TArray<FString> Failed;

			for (const TSharedPtr<FJsonValue>& WidgetVal : WidgetDefs)
			{
				TSharedPtr<FJsonObject> WidgetDef = WidgetVal->AsObject();
				if (!WidgetDef) { Failed.Add(TEXT("invalid entry")); continue; }

				FString WidgetType;
				WidgetDef->TryGetStringField(TEXT("type"), WidgetType);
				if (WidgetType.IsEmpty() && !WidgetDef->HasField(TEXT("class_path"))) { Failed.Add(TEXT("missing type / class_path")); continue; }

				FString ClassPath;
				WidgetDef->TryGetStringField(TEXT("class_path"), ClassPath);

				FString WidgetName;
				WidgetDef->TryGetStringField(TEXT("name"), WidgetName);

				// Find parent
				UWidget* ParentWidget = WBP->WidgetTree->RootWidget;
				FString ParentName;
				if (WidgetDef->TryGetStringField(TEXT("parent"), ParentName) && !ParentName.IsEmpty())
				{
					UWidget* PW = FindWidgetByName(WBP, ParentName);
					if (PW && Cast<UPanelWidget>(PW)) ParentWidget = PW;
					else { Failed.Add(FString::Printf(TEXT("%s: parent '%s' not found or not a panel"), *WidgetName, *ParentName)); continue; }
				}
				if (ParentWidget && !Cast<UPanelWidget>(ParentWidget)) { Failed.Add(FString::Printf(TEXT("%s: root is not a panel"), *WidgetName)); continue; }

				FString ConstructErr;
				UWidget* NewWidget = ConstructWidgetForTree(WBP, WidgetType, ClassPath, WidgetName, ConstructErr);
				if (!NewWidget) { Failed.Add(FString::Printf(TEXT("%s: %s"), *WidgetName, *ConstructErr)); continue; }

				bool bIsVariable = false;
				if (WidgetDef->TryGetBoolField(TEXT("is_variable"), bIsVariable))
				{
					NewWidget->bIsVariable = bIsVariable;
				}

				int32 Index = -1;
				if (WidgetDef->HasField(TEXT("index"))) Index = (int32)WidgetDef->GetNumberField(TEXT("index"));

				FText AddErr;
				if (!FWidgetBlueprintOperationUtils::AddWidget(WBP, NewWidget, ParentWidget, Index, AddErr))
				{
					Failed.Add(FString::Printf(TEXT("%s: %s"), *WidgetName, *AddErr.ToString()));
					continue;
				}
				if (WidgetType.IsEmpty()) WidgetType = NewWidget->GetClass()->GetName();

				Added.Add(FString::Printf(TEXT("%s (%s)"), *NewWidget->GetName(), *WidgetType));
			}

			GEditor->EndTransaction();
			SaveWidgetBlueprint(WBP, WantsSave(Args));

			FString Result = FString::Printf(TEXT("Added %d widget(s)"), Added.Num());
			if (Added.Num() > 0) Result += FString::Printf(TEXT(": %s"), *FString::Join(Added, TEXT(", ")));
			if (Failed.Num() > 0) Result += FString::Printf(TEXT(". Failed %d: %s"), Failed.Num(), *FString::Join(Failed, TEXT(", ")));

			return FMCPToolResult::Success(Result);
		});
	// ================================================================
	// batch_set_widget_properties - Set properties on multiple widgets
	// ================================================================
	MCP_TOOL(Registry, "batch_set_widget_properties")
		.Description(TEXT("Set properties on multiple widgets in one call. Each operation specifies a widget name and the properties to set (same as set_widget_properties: text, font_size, visibility, render_opacity, is_enabled, is_variable, tooltip_text, color_r/g/b/a, percent, width_override, height_override, justification). Much faster than individual calls."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("operations_json"), TEXT("JSON array of operations: [{\"widget\":\"Txt_Title\",\"text\":\"Hello\",\"font_size\":24}, {\"widget\":\"Img_Icon\",\"render_opacity\":0.5}, {\"widget\":\"Btn\",\"property\":\"WidgetStyle.Normal.TintColor.SpecifiedColor\",\"value\":\"(R=1,G=0,B=0,A=1)\"}]"), true)
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString OpsJson;
			if (!Args->TryGetStringField(TEXT("operations_json"), OpsJson))
				return FMCPToolResult::Error(TEXT("operations_json is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			if (!WBP->WidgetTree) return FMCPToolResult::Error(TEXT("No WidgetTree"));

			TArray<TSharedPtr<FJsonValue>> Operations;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OpsJson);
			if (!FJsonSerializer::Deserialize(Reader, Operations))
				return FMCPToolResult::Error(TEXT("Failed to parse operations_json. Expected JSON array of objects."));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Batch Set Widget Properties")));
			WBP->Modify();

			TArray<FString> Succeeded;
			TArray<FString> Failed;
			bool bStructuralChange = false;

			for (const TSharedPtr<FJsonValue>& OpVal : Operations)
			{
				TSharedPtr<FJsonObject> Op = OpVal->AsObject();
				if (!Op) { Failed.Add(TEXT("invalid entry")); continue; }

				FString WidgetName;
				if (!Op->TryGetStringField(TEXT("widget"), WidgetName))
				{
					Failed.Add(TEXT("missing 'widget' field"));
					continue;
				}

				UWidget* Widget = FindWidgetByName(WBP, WidgetName);
				if (!Widget)
				{
					Failed.Add(FString::Printf(TEXT("%s: not found"), *WidgetName));
					continue;
				}

				TArray<FString> Changed;

				// Common: Visibility
				FString VisStr;
				if (Op->TryGetStringField(TEXT("visibility"), VisStr))
				{
					if (VisStr == TEXT("Visible")) Widget->SetVisibility(ESlateVisibility::Visible);
					else if (VisStr == TEXT("Collapsed")) Widget->SetVisibility(ESlateVisibility::Collapsed);
					else if (VisStr == TEXT("Hidden")) Widget->SetVisibility(ESlateVisibility::Hidden);
					else if (VisStr == TEXT("HitTestInvisible")) Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
					else if (VisStr == TEXT("SelfHitTestInvisible")) Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
					Changed.Add(TEXT("visibility"));
				}

				// Common: IsEnabled
				bool bEnabled;
				if (Op->TryGetBoolField(TEXT("is_enabled"), bEnabled))
				{
					Widget->SetIsEnabled(bEnabled);
					Changed.Add(TEXT("is_enabled"));
				}

				// Common: RenderOpacity
				if (Op->HasField(TEXT("render_opacity")))
				{
					Widget->SetRenderOpacity((float)Op->GetNumberField(TEXT("render_opacity")));
					Changed.Add(TEXT("render_opacity"));
				}

				// Common: IsVariable
				bool bIsVariable = false;
				if (Op->TryGetBoolField(TEXT("is_variable"), bIsVariable))
				{
					if (Widget->bIsVariable != bIsVariable)
					{
						Widget->Modify();
						FWidgetBlueprintOperationUtils::ToggleWidgetAsVariable(WBP, Widget, bIsVariable, /*bMarkBlueprintModified*/ false);
						bStructuralChange = true;
					}
					Changed.Add(TEXT("is_variable"));
				}

				// Generic: {"property": "...", "value": "..."} via reflection
				FString GenericProp, GenericValue;
				if (Op->TryGetStringField(TEXT("property"), GenericProp) && Op->TryGetStringField(TEXT("value"), GenericValue))
				{
					FString Err;
					if (SetPropertyFromString(Widget, GenericProp, GenericValue, Err)) Changed.Add(GenericProp);
					else Failed.Add(FString::Printf(TEXT("%s.%s: %s"), *WidgetName, *GenericProp, *Err));
				}

				// Common: ToolTipText
				FString TooltipStr;
				if (Op->TryGetStringField(TEXT("tooltip_text"), TooltipStr))
				{
					Widget->SetToolTipText(FText::FromString(TooltipStr));
					Changed.Add(TEXT("tooltip"));
				}

				// Color
				bool bHasColor = Op->HasField(TEXT("color_r")) || Op->HasField(TEXT("color_g"))
					|| Op->HasField(TEXT("color_b")) || Op->HasField(TEXT("color_a"));
				FLinearColor Color(
					Op->HasField(TEXT("color_r")) ? (float)Op->GetNumberField(TEXT("color_r")) : 1.0f,
					Op->HasField(TEXT("color_g")) ? (float)Op->GetNumberField(TEXT("color_g")) : 1.0f,
					Op->HasField(TEXT("color_b")) ? (float)Op->GetNumberField(TEXT("color_b")) : 1.0f,
					Op->HasField(TEXT("color_a")) ? (float)Op->GetNumberField(TEXT("color_a")) : 1.0f
				);

				// TextBlock
				if (UTextBlock* TB = Cast<UTextBlock>(Widget))
				{
					FString Text;
					if (Op->TryGetStringField(TEXT("text"), Text))
					{
						TB->SetText(FText::FromString(Text));
						Changed.Add(TEXT("text"));
					}
					if (Op->HasField(TEXT("font_size")))
					{
						FSlateFontInfo FontInfo = TB->GetFont();
						FontInfo.Size = (int32)Op->GetNumberField(TEXT("font_size"));
						TB->SetFont(FontInfo);
						Changed.Add(TEXT("font_size"));
					}
					FString JustStr;
					if (Op->TryGetStringField(TEXT("justification"), JustStr))
					{
						if (JustStr == TEXT("Left")) TB->SetJustification(ETextJustify::Left);
						else if (JustStr == TEXT("Center")) TB->SetJustification(ETextJustify::Center);
						else if (JustStr == TEXT("Right")) TB->SetJustification(ETextJustify::Right);
						Changed.Add(TEXT("justification"));
					}
					if (bHasColor) { TB->SetColorAndOpacity(FSlateColor(Color)); Changed.Add(TEXT("color")); }
				}
				else if (UButton* Btn = Cast<UButton>(Widget))
				{
					if (bHasColor) { Btn->SetBackgroundColor(Color); Changed.Add(TEXT("color")); }
				}
				else if (UImage* Img = Cast<UImage>(Widget))
				{
					if (bHasColor) { Img->SetColorAndOpacity(Color); Changed.Add(TEXT("color")); }
				}
				else if (UProgressBar* PB = Cast<UProgressBar>(Widget))
				{
					if (Op->HasField(TEXT("percent")))
					{
						PB->SetPercent((float)Op->GetNumberField(TEXT("percent")));
						Changed.Add(TEXT("percent"));
					}
					if (bHasColor) { PB->SetFillColorAndOpacity(Color); Changed.Add(TEXT("color")); }
				}
				else if (USizeBox* SB = Cast<USizeBox>(Widget))
				{
					if (Op->HasField(TEXT("width_override")))
					{
						SB->SetWidthOverride((float)Op->GetNumberField(TEXT("width_override")));
						Changed.Add(TEXT("width"));
					}
					if (Op->HasField(TEXT("height_override")))
					{
						SB->SetHeightOverride((float)Op->GetNumberField(TEXT("height_override")));
						Changed.Add(TEXT("height"));
					}
				}

				if (Changed.Num() > 0)
					Succeeded.Add(FString::Printf(TEXT("%s: %s"), *WidgetName, *FString::Join(Changed, TEXT(","))));
				else
					Succeeded.Add(FString::Printf(TEXT("%s: no applicable changes"), *WidgetName));
			}

			GEditor->EndTransaction();
			if (bStructuralChange)
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
			}
			SaveWidgetBlueprint(WBP, WantsSave(Args));

			FString Result = FString::Printf(TEXT("Updated %d widget(s)"), Succeeded.Num());
			if (Succeeded.Num() > 0) Result += FString::Printf(TEXT(": %s"), *FString::Join(Succeeded, TEXT("; ")));
			if (Failed.Num() > 0) Result += FString::Printf(TEXT(". Failed %d: %s"), Failed.Num(), *FString::Join(Failed, TEXT(", ")));

			return FMCPToolResult::Success(Result);
		});
}

} // namespace MCPWidgetTools::Batch
