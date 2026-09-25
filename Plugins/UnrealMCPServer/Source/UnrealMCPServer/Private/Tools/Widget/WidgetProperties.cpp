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
#include "Components/PanelSlot.h"
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

namespace MCPWidgetTools::Properties
{

using namespace MCPWidgetTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// set_widget_component_property - Set properties on a WidgetComponent
	// ================================================================
	MCP_TOOL(Registry, "set_widget_component_property")
		.Description(TEXT("Set one or more properties on the first WidgetComponent found on an actor. Only provided fields are modified. Supports draw size, widget class, render space, tint color, interaction distance, and two-sided rendering."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor that owns the WidgetComponent"), true)
		.NumberArg(TEXT("draw_size_x"), TEXT("New draw width in world units"))
		.NumberArg(TEXT("draw_size_y"), TEXT("New draw height in world units"))
		.StringArg(TEXT("widget_class_path"), TEXT("Content path to a Widget Blueprint to assign as the widget class (e.g., '/Game/UI/WBP_HUD.WBP_HUD')"))
		.EnumArg(TEXT("space"), TEXT("Render space: 'World' for 3D world space, 'Screen' for screen-space overlay"),
			{ TEXT("World"), TEXT("Screen") })
		.NumberArg(TEXT("tint_r"), TEXT("Tint color red channel (0.0 - 1.0)"))
		.NumberArg(TEXT("tint_g"), TEXT("Tint color green channel (0.0 - 1.0)"))
		.NumberArg(TEXT("tint_b"), TEXT("Tint color blue channel (0.0 - 1.0)"))
		.NumberArg(TEXT("tint_a"), TEXT("Tint color alpha channel (0.0 - 1.0)"))
		.NumberArg(TEXT("max_interaction_distance"), TEXT("Maximum distance at which the player can interact with this widget (in world units)"))
		.BoolArg(TEXT("is_two_sided"), TEXT("Whether the widget geometry is rendered two-sided"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UWidgetComponent* WidgetComp = Actor->FindComponentByClass<UWidgetComponent>();
			if (!WidgetComp)
				return FMCPToolResult::Error(FString::Printf(
					TEXT("No WidgetComponent found on actor '%s'. Use spawn_widget_component to add one first."), *ActorName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Component Property")));
			WidgetComp->Modify();

			TArray<FString> ChangedProps;

			bool bHasDrawX = Args->HasField(TEXT("draw_size_x"));
			bool bHasDrawY = Args->HasField(TEXT("draw_size_y"));
			if (bHasDrawX || bHasDrawY)
			{
				FVector2D CurrentSize = WidgetComp->GetDrawSize();
				float NewX = bHasDrawX ? (float)Args->GetNumberField(TEXT("draw_size_x")) : CurrentSize.X;
				float NewY = bHasDrawY ? (float)Args->GetNumberField(TEXT("draw_size_y")) : CurrentSize.Y;
				WidgetComp->SetDrawSize(FVector2D(NewX, NewY));
				ChangedProps.Add(FString::Printf(TEXT("DrawSize=(%.0f x %.0f)"), NewX, NewY));
			}

			FString WidgetClassPath;
			if (Args->TryGetStringField(TEXT("widget_class_path"), WidgetClassPath) && !WidgetClassPath.IsEmpty())
			{
				UBlueprint* WidgetBP = LoadObject<UBlueprint>(nullptr, *WidgetClassPath);
				UClass* GenClass = WidgetBP ? WidgetBP->GeneratedClass.Get() : nullptr;
				if (GenClass && GenClass->IsChildOf(UUserWidget::StaticClass()))
				{
					WidgetComp->SetWidgetClass(TSubclassOf<UUserWidget>(GenClass));
					ChangedProps.Add(FString::Printf(TEXT("WidgetClass=%s"), *WidgetBP->GetName()));
				}
				else
				{
					FString GeneratedClassPath = WidgetClassPath + TEXT("_C");
					UClass* LoadedClass = LoadObject<UClass>(nullptr, *GeneratedClassPath);
					if (LoadedClass && LoadedClass->IsChildOf(UUserWidget::StaticClass()))
					{
						TSubclassOf<UUserWidget> WC = LoadedClass;
						WidgetComp->SetWidgetClass(WC);
						ChangedProps.Add(FString::Printf(TEXT("WidgetClass=%s"), *LoadedClass->GetName()));
					}
					else
					{
						GEditor->EndTransaction();
						return FMCPToolResult::Error(FString::Printf(
							TEXT("Could not load Widget Blueprint class from: %s"), *WidgetClassPath));
					}
				}
			}

			FString SpaceStr;
			if (Args->TryGetStringField(TEXT("space"), SpaceStr))
			{
				EWidgetSpace NewSpace = (SpaceStr == TEXT("Screen")) ? EWidgetSpace::Screen : EWidgetSpace::World;
				WidgetComp->SetWidgetSpace(NewSpace);
				ChangedProps.Add(FString::Printf(TEXT("Space=%s"), *SpaceStr));
			}

			bool bHasR = Args->HasField(TEXT("tint_r"));
			bool bHasG = Args->HasField(TEXT("tint_g"));
			bool bHasB = Args->HasField(TEXT("tint_b"));
			bool bHasA = Args->HasField(TEXT("tint_a"));
			if (bHasR || bHasG || bHasB || bHasA)
			{
				FLinearColor NewTint(
					bHasR ? (float)Args->GetNumberField(TEXT("tint_r")) : 1.0f,
					bHasG ? (float)Args->GetNumberField(TEXT("tint_g")) : 1.0f,
					bHasB ? (float)Args->GetNumberField(TEXT("tint_b")) : 1.0f,
					bHasA ? (float)Args->GetNumberField(TEXT("tint_a")) : 1.0f
				);
				WidgetComp->SetTintColorAndOpacity(NewTint);
				ChangedProps.Add(FString::Printf(TEXT("Tint=(R=%.2f G=%.2f B=%.2f A=%.2f)"),
					NewTint.R, NewTint.G, NewTint.B, NewTint.A));
			}

			bool bTwoSided = false;
			if (Args->TryGetBoolField(TEXT("is_two_sided"), bTwoSided))
			{
				WidgetComp->SetTwoSided(bTwoSided);
				ChangedProps.Add(FString::Printf(TEXT("TwoSided=%s"), bTwoSided ? TEXT("true") : TEXT("false")));
			}

			GEditor->EndTransaction();

			if (ChangedProps.Num() == 0)
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("No properties changed on WidgetComponent of '%s'. Provide at least one optional property to modify."),
					*ActorName));
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Updated WidgetComponent on '%s': %s"),
				*ActorName,
				*FString::Join(ChangedProps, TEXT(", "))));
		});
	// ================================================================
	// set_widget_properties - Set properties on any widget
	// ================================================================
	MCP_TOOL(Registry, "set_widget_properties")
		.Description(TEXT("Set properties on any widget in a Widget Blueprint. Supports common properties (visibility, opacity, tooltip) and type-specific properties (text/font for TextBlock, color for TextBlock/Button/Image, percent for ProgressBar, size overrides for SizeBox, padding for Border). Only provided fields are modified."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget to modify"), true)
		// Common properties
		.EnumArg(TEXT("visibility"), TEXT("Widget visibility"),
			{ TEXT("Visible"), TEXT("Collapsed"), TEXT("Hidden"), TEXT("HitTestInvisible"), TEXT("SelfHitTestInvisible") })
		.BoolArg(TEXT("is_enabled"), TEXT("Whether the widget is interactive"))
		.BoolArg(TEXT("is_variable"), TEXT("Expose the widget as a Blueprint variable ('Is Variable' checkbox). Required for BindWidget / graph access. Triggers a structural recompile."))
		.StringArg(TEXT("tooltip_text"), TEXT("Tooltip text"))
		.StringArg(TEXT("text_namespace"), TEXT("Localization namespace for text/tooltip_text (with text_key, makes the text localizable instead of a culture-invariant literal)"))
		.StringArg(TEXT("text_key"), TEXT("Localization key for text/tooltip_text"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.NumberArg(TEXT("render_opacity"), TEXT("Render opacity (0.0 - 1.0)"))
		// TextBlock
		.StringArg(TEXT("text"), TEXT("Text content (for TextBlock/RichTextBlock)"))
		.IntArg(TEXT("font_size"), TEXT("Font size in points (for TextBlock)"))
		.EnumArg(TEXT("justification"), TEXT("Text justification (for TextBlock)"),
			{ TEXT("Left"), TEXT("Center"), TEXT("Right") })
		// Color (applies to TextBlock color, Image tint, Button bg color)
		.NumberArg(TEXT("color_r"), TEXT("Color red channel (0.0 - 1.0)"))
		.NumberArg(TEXT("color_g"), TEXT("Color green channel (0.0 - 1.0)"))
		.NumberArg(TEXT("color_b"), TEXT("Color blue channel (0.0 - 1.0)"))
		.NumberArg(TEXT("color_a"), TEXT("Color alpha channel (0.0 - 1.0)"))
		// ProgressBar
		.NumberArg(TEXT("percent"), TEXT("Progress bar fill percent (0.0 - 1.0)"))
		// SizeBox
		.NumberArg(TEXT("width_override"), TEXT("Width override for SizeBox (0 to clear)"))
		.NumberArg(TEXT("height_override"), TEXT("Height override for SizeBox (0 to clear)"))
		// Border/SizeBox padding
		.NumberArg(TEXT("padding_left"), TEXT("Left padding"))
		.NumberArg(TEXT("padding_top"), TEXT("Top padding"))
		.NumberArg(TEXT("padding_right"), TEXT("Right padding"))
		.NumberArg(TEXT("padding_bottom"), TEXT("Bottom padding"))
		// Image brush size
		.NumberArg(TEXT("brush_size_x"), TEXT("Image brush width"))
		.NumberArg(TEXT("brush_size_y"), TEXT("Image brush height"))
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

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Properties")));
			WBP->Modify();

			TArray<FString> Changed;

			// Common: Visibility
			FString VisStr;
			if (Args->TryGetStringField(TEXT("visibility"), VisStr))
			{
				if (VisStr == TEXT("Visible")) Widget->SetVisibility(ESlateVisibility::Visible);
				else if (VisStr == TEXT("Collapsed")) Widget->SetVisibility(ESlateVisibility::Collapsed);
				else if (VisStr == TEXT("Hidden")) Widget->SetVisibility(ESlateVisibility::Hidden);
				else if (VisStr == TEXT("HitTestInvisible")) Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
				else if (VisStr == TEXT("SelfHitTestInvisible")) Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				Changed.Add(FString::Printf(TEXT("Visibility=%s"), *VisStr));
			}

			// Common: IsEnabled
			bool bEnabled;
			if (Args->TryGetBoolField(TEXT("is_enabled"), bEnabled))
			{
				Widget->SetIsEnabled(bEnabled);
				Changed.Add(FString::Printf(TEXT("IsEnabled=%s"), bEnabled ? TEXT("true") : TEXT("false")));
			}

			// Common: RenderOpacity
			if (Args->HasField(TEXT("render_opacity")))
			{
				float Opacity = (float)Args->GetNumberField(TEXT("render_opacity"));
				Widget->SetRenderOpacity(Opacity);
				Changed.Add(FString::Printf(TEXT("RenderOpacity=%.2f"), Opacity));
			}

			// Common: IsVariable (adds/removes a member variable on the generated class)
			bool bStructuralChange = false;
			bool bIsVariable = false;
			if (Args->TryGetBoolField(TEXT("is_variable"), bIsVariable))
			{
				if (Widget->bIsVariable != bIsVariable)
				{
					Widget->Modify();
					// Engine path (designer checkbox): sets the flag and marks the Blueprint structurally modified.
					FWidgetBlueprintOperationUtils::ToggleWidgetAsVariable(WBP, Widget, bIsVariable, /*bMarkBlueprintModified*/ false);
					bStructuralChange = true;
				}
				Changed.Add(FString::Printf(TEXT("IsVariable=%s"), bIsVariable ? TEXT("true") : TEXT("false")));
			}

			// Common: ToolTipText
			FString TooltipStr;
			if (Args->TryGetStringField(TEXT("tooltip_text"), TooltipStr))
			{
				Widget->SetToolTipText(MakeTextArg(Args, TooltipStr));
				Changed.Add(FString::Printf(TEXT("ToolTip=%s"), *TooltipStr));
			}

			// Build color from provided channels
			bool bHasColorR = Args->HasField(TEXT("color_r"));
			bool bHasColorG = Args->HasField(TEXT("color_g"));
			bool bHasColorB = Args->HasField(TEXT("color_b"));
			bool bHasColorA = Args->HasField(TEXT("color_a"));
			bool bHasColor = bHasColorR || bHasColorG || bHasColorB || bHasColorA;
			FLinearColor Color(
				bHasColorR ? (float)Args->GetNumberField(TEXT("color_r")) : 1.0f,
				bHasColorG ? (float)Args->GetNumberField(TEXT("color_g")) : 1.0f,
				bHasColorB ? (float)Args->GetNumberField(TEXT("color_b")) : 1.0f,
				bHasColorA ? (float)Args->GetNumberField(TEXT("color_a")) : 1.0f
			);

			// TextBlock specific
			if (UTextBlock* TB = Cast<UTextBlock>(Widget))
			{
				FString Text;
				if (Args->TryGetStringField(TEXT("text"), Text))
				{
					TB->SetText(MakeTextArg(Args, Text));
					Changed.Add(FString::Printf(TEXT("Text=%s"), *Text));
				}

				if (Args->HasField(TEXT("font_size")))
				{
					int32 FontSize = (int32)Args->GetNumberField(TEXT("font_size"));
					FSlateFontInfo FontInfo = TB->GetFont();
					FontInfo.Size = FontSize;
					TB->SetFont(FontInfo);
					Changed.Add(FString::Printf(TEXT("FontSize=%d"), FontSize));
				}

				FString JustStr;
				if (Args->TryGetStringField(TEXT("justification"), JustStr))
				{
					if (JustStr == TEXT("Left")) TB->SetJustification(ETextJustify::Left);
					else if (JustStr == TEXT("Center")) TB->SetJustification(ETextJustify::Center);
					else if (JustStr == TEXT("Right")) TB->SetJustification(ETextJustify::Right);
					Changed.Add(FString::Printf(TEXT("Justification=%s"), *JustStr));
				}

				if (bHasColor)
				{
					TB->SetColorAndOpacity(FSlateColor(Color));
					Changed.Add(TEXT("TextColor updated"));
				}
			}
			// Button specific
			else if (UButton* Btn = Cast<UButton>(Widget))
			{
				if (bHasColor)
				{
					Btn->SetBackgroundColor(Color);
					Changed.Add(TEXT("BackgroundColor updated"));
				}
			}
			// Image specific
			else if (UImage* Img = Cast<UImage>(Widget))
			{
				if (bHasColor)
				{
					Img->SetColorAndOpacity(Color);
					Changed.Add(TEXT("ImageTint updated"));
				}

				bool bHasBrushX = Args->HasField(TEXT("brush_size_x"));
				bool bHasBrushY = Args->HasField(TEXT("brush_size_y"));
				if (bHasBrushX || bHasBrushY)
				{
					FVector2D CurrentSize = Img->GetBrush().ImageSize;
					float BX = bHasBrushX ? (float)Args->GetNumberField(TEXT("brush_size_x")) : CurrentSize.X;
					float BY = bHasBrushY ? (float)Args->GetNumberField(TEXT("brush_size_y")) : CurrentSize.Y;
					Img->SetDesiredSizeOverride(FVector2D(BX, BY));
					Changed.Add(FString::Printf(TEXT("BrushSize=(%.0f, %.0f)"), BX, BY));
				}
			}
			// ProgressBar specific
			else if (UProgressBar* PB = Cast<UProgressBar>(Widget))
			{
				if (Args->HasField(TEXT("percent")))
				{
					float Pct = (float)Args->GetNumberField(TEXT("percent"));
					PB->SetPercent(Pct);
					Changed.Add(FString::Printf(TEXT("Percent=%.2f"), Pct));
				}
				if (bHasColor)
				{
					PB->SetFillColorAndOpacity(Color);
					Changed.Add(TEXT("FillColor updated"));
				}
			}
			// SizeBox specific
			else if (USizeBox* SB = Cast<USizeBox>(Widget))
			{
				if (Args->HasField(TEXT("width_override")))
				{
					float W = (float)Args->GetNumberField(TEXT("width_override"));
					SB->SetWidthOverride(W);
					Changed.Add(FString::Printf(TEXT("WidthOverride=%.0f"), W));
				}
				if (Args->HasField(TEXT("height_override")))
				{
					float H = (float)Args->GetNumberField(TEXT("height_override"));
					SB->SetHeightOverride(H);
					Changed.Add(FString::Printf(TEXT("HeightOverride=%.0f"), H));
				}
			}
			// Border specific
			else if (UBorder* Brd = Cast<UBorder>(Widget))
			{
				if (bHasColor)
				{
					Brd->SetBrushColor(Color);
					Changed.Add(TEXT("BorderColor updated"));
				}
				bool bHasPadding = Args->HasField(TEXT("padding_left")) || Args->HasField(TEXT("padding_top"))
					|| Args->HasField(TEXT("padding_right")) || Args->HasField(TEXT("padding_bottom"));
				if (bHasPadding)
				{
					FMargin Padding;
					Padding.Left = Args->HasField(TEXT("padding_left")) ? (float)Args->GetNumberField(TEXT("padding_left")) : 0.0f;
					Padding.Top = Args->HasField(TEXT("padding_top")) ? (float)Args->GetNumberField(TEXT("padding_top")) : 0.0f;
					Padding.Right = Args->HasField(TEXT("padding_right")) ? (float)Args->GetNumberField(TEXT("padding_right")) : 0.0f;
					Padding.Bottom = Args->HasField(TEXT("padding_bottom")) ? (float)Args->GetNumberField(TEXT("padding_bottom")) : 0.0f;
					Brd->SetPadding(Padding);
					Changed.Add(TEXT("Padding updated"));
				}
			}

			GEditor->EndTransaction();

			if (Changed.Num() == 0)
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("No applicable properties changed on widget '%s' (type: %s). Check parameter compatibility with widget type."),
					*WidgetName, *Widget->GetClass()->GetName()));
			}

			if (bStructuralChange)
			{
				// Mirrors what the UMG designer does when the "Is Variable" checkbox is toggled
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
			}

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Updated widget '%s': %s"),
				*WidgetName, *FString::Join(Changed, TEXT(", "))));
		});
	// ================================================================
	// get_widget_properties - Inspect widget properties
	// ================================================================
	MCP_TOOL(Registry, "get_widget_properties")
		.Description(TEXT("Inspect a widget's current properties including type, visibility, opacity, and type-specific properties (text, colors, sizes). Also reports slot/layout information from the parent container."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget to inspect"), true)
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

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("name"), Widget->GetName());
			Info->SetStringField(TEXT("type"), Widget->GetClass()->GetName());
			Info->SetStringField(TEXT("visibility"), UEnum::GetValueAsString(Widget->GetVisibility()));
			Info->SetBoolField(TEXT("is_enabled"), Widget->GetIsEnabled());
			Info->SetBoolField(TEXT("is_variable"), Widget->bIsVariable);
			Info->SetBoolField(TEXT("is_user_widget"), Widget->IsA<UUserWidget>());
			Info->SetBoolField(TEXT("hidden_in_designer"), Widget->bHiddenInDesigner);
			Info->SetBoolField(TEXT("locked_in_designer"), Widget->bLockedInDesigner);
			Info->SetNumberField(TEXT("render_opacity"), Widget->GetRenderOpacity());
			if (Widget->Slot) Info->SetStringField(TEXT("slot_type"), Widget->Slot->GetClass()->GetName());

			UPanelWidget* Parent = Widget->GetParent();
			if (Parent)
			{
				Info->SetStringField(TEXT("parent"), Parent->GetName());
			}

			bool bIsPanel = Cast<UPanelWidget>(Widget) != nullptr;
			Info->SetBoolField(TEXT("is_panel"), bIsPanel);
			if (bIsPanel)
			{
				Info->SetNumberField(TEXT("child_count"), Cast<UPanelWidget>(Widget)->GetChildrenCount());
			}

			// Type-specific properties
			if (UTextBlock* TB = Cast<UTextBlock>(Widget))
			{
				Info->SetStringField(TEXT("text"), TB->GetText().ToString());
				Info->SetNumberField(TEXT("font_size"), TB->GetFont().Size);
			}
			else if (UProgressBar* PB = Cast<UProgressBar>(Widget))
			{
				Info->SetNumberField(TEXT("percent"), PB->GetPercent());
			}
			else if (USizeBox* SB = Cast<USizeBox>(Widget))
			{
				Info->SetBoolField(TEXT("has_width_override"), SB->GetWidthOverride() > 0);
				Info->SetBoolField(TEXT("has_height_override"), SB->GetHeightOverride() > 0);
			}

			// Slot info
			if (Widget->Slot)
			{
				TSharedPtr<FJsonObject> SlotInfo = MakeShared<FJsonObject>();

				if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Widget->Slot))
				{
					SlotInfo->SetStringField(TEXT("slot_type"), TEXT("CanvasPanelSlot"));
					FAnchors A = CS->GetAnchors();
					SlotInfo->SetNumberField(TEXT("anchor_min_x"), A.Minimum.X);
					SlotInfo->SetNumberField(TEXT("anchor_min_y"), A.Minimum.Y);
					SlotInfo->SetNumberField(TEXT("anchor_max_x"), A.Maximum.X);
					SlotInfo->SetNumberField(TEXT("anchor_max_y"), A.Maximum.Y);
					FMargin O = CS->GetOffsets();
					SlotInfo->SetNumberField(TEXT("offset_left"), O.Left);
					SlotInfo->SetNumberField(TEXT("offset_top"), O.Top);
					SlotInfo->SetNumberField(TEXT("offset_right"), O.Right);
					SlotInfo->SetNumberField(TEXT("offset_bottom"), O.Bottom);
					FVector2D Alignment = CS->GetAlignment();
					SlotInfo->SetNumberField(TEXT("alignment_x"), Alignment.X);
					SlotInfo->SetNumberField(TEXT("alignment_y"), Alignment.Y);
					SlotInfo->SetNumberField(TEXT("z_order"), CS->GetZOrder());
					SlotInfo->SetBoolField(TEXT("auto_size"), CS->GetAutoSize());
				}
				else if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Widget->Slot))
				{
					SlotInfo->SetStringField(TEXT("slot_type"), TEXT("VerticalBoxSlot"));
					FSlateChildSize SizeRule = VS->GetSize();
					SlotInfo->SetStringField(TEXT("size_rule"), SizeRule.SizeRule == ESlateSizeRule::Automatic ? TEXT("Auto") : TEXT("Fill"));
					SlotInfo->SetStringField(TEXT("halign"), UEnum::GetValueAsString(VS->GetHorizontalAlignment()));
					SlotInfo->SetStringField(TEXT("valign"), UEnum::GetValueAsString(VS->GetVerticalAlignment()));
				}
				else if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Widget->Slot))
				{
					SlotInfo->SetStringField(TEXT("slot_type"), TEXT("HorizontalBoxSlot"));
					FSlateChildSize SizeRule = HS->GetSize();
					SlotInfo->SetStringField(TEXT("size_rule"), SizeRule.SizeRule == ESlateSizeRule::Automatic ? TEXT("Auto") : TEXT("Fill"));
					SlotInfo->SetStringField(TEXT("halign"), UEnum::GetValueAsString(HS->GetHorizontalAlignment()));
					SlotInfo->SetStringField(TEXT("valign"), UEnum::GetValueAsString(HS->GetVerticalAlignment()));
				}
				else if (UOverlaySlot* OS = Cast<UOverlaySlot>(Widget->Slot))
				{
					SlotInfo->SetStringField(TEXT("slot_type"), TEXT("OverlaySlot"));
					SlotInfo->SetStringField(TEXT("halign"), UEnum::GetValueAsString(OS->GetHorizontalAlignment()));
					SlotInfo->SetStringField(TEXT("valign"), UEnum::GetValueAsString(OS->GetVerticalAlignment()));
				}

				Info->SetObjectField(TEXT("slot"), SlotInfo);
			}

			return FMCPToolResult::SuccessStructured(JsonToString(Info), Info);
		});
	// ================================================================
	// set_widget_image - Set texture on an Image widget
	// ================================================================
	MCP_TOOL(Registry, "set_widget_image")
		.Description(TEXT("Set a texture on a UImage widget in a Widget Blueprint. Loads a Texture2D asset and assigns it as the image brush. Optionally set tint color and display size."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the UImage widget"), true)
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.StringArg(TEXT("texture_path"), TEXT("Content path to a Texture2D asset (e.g., '/Game/Textures/T_MyIcon')"), true)
		.NumberArg(TEXT("tint_r"), TEXT("Tint red (0-1)"))
		.NumberArg(TEXT("tint_g"), TEXT("Tint green (0-1)"))
		.NumberArg(TEXT("tint_b"), TEXT("Tint blue (0-1)"))
		.NumberArg(TEXT("tint_a"), TEXT("Tint alpha (0-1)"))
		.NumberArg(TEXT("size_x"), TEXT("Override image display width"))
		.NumberArg(TEXT("size_y"), TEXT("Override image display height"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetName;
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName))
				return FMCPToolResult::Error(TEXT("widget_name is required"));

			FString TexturePath;
			if (!Args->TryGetStringField(TEXT("texture_path"), TexturePath))
				return FMCPToolResult::Error(TEXT("texture_path is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			UImage* ImageWidget = Cast<UImage>(Widget);
			if (!ImageWidget)
				return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' is not an Image widget (type: %s)"),
					*WidgetName, *Widget->GetClass()->GetName()));

			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *TexturePath);
			if (!Texture)
				return FMCPToolResult::Error(FString::Printf(TEXT("Texture not found: %s"), *TexturePath));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Image")));
			WBP->Modify();

			ImageWidget->SetBrushFromTexture(Texture);

			// Tint
			bool bHasR = Args->HasField(TEXT("tint_r"));
			bool bHasG = Args->HasField(TEXT("tint_g"));
			bool bHasB = Args->HasField(TEXT("tint_b"));
			bool bHasA = Args->HasField(TEXT("tint_a"));
			if (bHasR || bHasG || bHasB || bHasA)
			{
				FLinearColor Tint(
					bHasR ? (float)Args->GetNumberField(TEXT("tint_r")) : 1.0f,
					bHasG ? (float)Args->GetNumberField(TEXT("tint_g")) : 1.0f,
					bHasB ? (float)Args->GetNumberField(TEXT("tint_b")) : 1.0f,
					bHasA ? (float)Args->GetNumberField(TEXT("tint_a")) : 1.0f
				);
				ImageWidget->SetColorAndOpacity(Tint);
			}

			// Size override
			bool bHasSizeX = Args->HasField(TEXT("size_x"));
			bool bHasSizeY = Args->HasField(TEXT("size_y"));
			if (bHasSizeX || bHasSizeY)
			{
				float SX = bHasSizeX ? (float)Args->GetNumberField(TEXT("size_x")) : 0.0f;
				float SY = bHasSizeY ? (float)Args->GetNumberField(TEXT("size_y")) : 0.0f;
				ImageWidget->SetDesiredSizeOverride(FVector2D(SX, SY));
			}

			GEditor->EndTransaction();
			SaveWidgetBlueprint(WBP, WantsSave(Args));

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Set texture '%s' on Image widget '%s' in %s"),
				*Texture->GetName(), *WidgetName, *AssetPath));
		});

	// ================================================================
	// set_widget_property - Generic reflection setter (any property, nested paths)
	// ================================================================
	MCP_TOOL(Registry, "set_widget_property")
		.Description(TEXT("Set ANY property on a widget by name using reflection, including nested paths ('Font.Size', 'WidgetStyle.Normal.TintColor', 'ColorAndOpacity.SpecifiedColor') and properties the Python API refuses (bIsVariable, designer flags). Values use Unreal text syntax: numbers, true/false, enum names ('Collapsed'), structs '(R=1,G=0,B=0,A=1)' / '(X=10,Y=20)', asset paths for object refs. Replaces most execute_python fallbacks for UMG."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget"), true)
		.StringArg(TEXT("property_name"), TEXT("Property name or dotted path (case-insensitive; 'IsVariable' resolves to 'bIsVariable')"), true)
		.StringArg(TEXT("value"), TEXT("Value in Unreal text-import syntax"), true)
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName, PropertyName, Value;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			if (!Args->TryGetStringField(TEXT("property_name"), PropertyName)) return FMCPToolResult::Error(TEXT("property_name is required"));
			if (!Args->TryGetStringField(TEXT("value"), Value)) return FMCPToolResult::Error(TEXT("value is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			FString Before, Err;
			GetPropertyAsString(Widget, PropertyName, Before, Err);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Property")));
			WBP->Modify();
			const bool bOk = SetPropertyFromString(Widget, PropertyName, Value, Err);
			GEditor->EndTransaction();
			if (!bOk) return FMCPToolResult::Error(Err);

			// bIsVariable (and anything else that changes the generated class) needs a structural recompile.
			FResolvedProperty R; FString Dummy;
			const bool bStructural = ResolvePropertyPath(Widget, PropertyName, R, Dummy)
				&& R.Property && R.Property->GetFName() == FName(TEXT("bIsVariable"));
			if (bStructural) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

			FString After;
			GetPropertyAsString(Widget, PropertyName, After, Err);

			SaveWidgetBlueprint(WBP, WantsSave(Args));

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("widget"), WidgetName);
			Info->SetStringField(TEXT("property"), PropertyName);
			Info->SetStringField(TEXT("previous_value"), Before);
			Info->SetStringField(TEXT("value"), After);
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%s.%s: '%s' -> '%s'"), *WidgetName, *PropertyName, *Before, *After), Info);
		});

	// ================================================================
	// get_widget_property - Generic reflection getter
	// ================================================================
	MCP_TOOL(Registry, "get_widget_property")
		.Description(TEXT("Read any property on a widget (or on its slot with target='slot') by name or dotted path, exported in Unreal text syntax."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget"), true)
		.StringArg(TEXT("property_name"), TEXT("Property name or dotted path"), true)
		.EnumArg(TEXT("target"), TEXT("Read from the widget (default) or its slot"), { TEXT("widget"), TEXT("slot") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName, PropertyName, Target;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			if (!Args->TryGetStringField(TEXT("property_name"), PropertyName)) return FMCPToolResult::Error(TEXT("property_name is required"));
			Args->TryGetStringField(TEXT("target"), Target);

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			UObject* Object = Widget;
			if (Target == TEXT("slot"))
			{
				if (!Widget->Slot) return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' has no slot"), *WidgetName));
				Object = Widget->Slot;
			}

			FString Value, Err;
			if (!GetPropertyAsString(Object, PropertyName, Value, Err)) return FMCPToolResult::Error(Err);

			FResolvedProperty R; FString Dummy;
			ResolvePropertyPath(Object, PropertyName, R, Dummy);
			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("widget"), WidgetName);
			Info->SetStringField(TEXT("property"), PropertyName);
			Info->SetStringField(TEXT("value"), Value);
			if (R.Property) Info->SetStringField(TEXT("type"), R.Property->GetCPPType());
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%s.%s = %s"), *WidgetName, *PropertyName, *Value), Info);
		});

	// ================================================================
	// list_widget_properties - Discover property names/types/values
	// ================================================================
	MCP_TOOL(Registry, "list_widget_properties")
		.Description(TEXT("List the reflected properties of a widget (or its slot) with type, category, editability and current value, so property names can be discovered without engine headers. Use with set_widget_property."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget"), true)
		.EnumArg(TEXT("target"), TEXT("List the widget's properties (default) or its slot's"), { TEXT("widget"), TEXT("slot") })
		.StringArg(TEXT("filter"), TEXT("Optional substring filter on property name / category"))
		.BoolArg(TEXT("include_inherited"), TEXT("Include properties from parent classes (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName, Target, Filter;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			Args->TryGetStringField(TEXT("target"), Target);
			Args->TryGetStringField(TEXT("filter"), Filter);
			bool bInherited = true;
			Args->TryGetBoolField(TEXT("include_inherited"), bInherited);

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			UObject* Object = Widget;
			if (Target == TEXT("slot"))
			{
				if (!Widget->Slot) return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' has no slot"), *WidgetName));
				Object = Widget->Slot;
			}

			TArray<TSharedPtr<FJsonValue>> Props;
			for (TFieldIterator<FProperty> It(Object->GetClass(), bInherited ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				FProperty* P = *It;
				if (P->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient)) continue;
				if (!Filter.IsEmpty())
				{
					const FString Cat = P->GetMetaData(TEXT("Category"));
					if (!P->GetName().Contains(Filter) && !Cat.Contains(Filter)) continue;
				}
				Props.Add(MakeShared<FJsonValueObject>(DescribeProperty(P, Object, Object)));
				if (Props.Num() >= 300) break;
			}

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("object"), Object->GetName());
			Info->SetStringField(TEXT("class"), Object->GetClass()->GetName());
			Info->SetArrayField(TEXT("properties"), Props);
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%d properties on %s (%s)"), Props.Num(), *Object->GetName(), *Object->GetClass()->GetName()), Info);
		});

	// ================================================================
	// set_widget_slot_property - Generic setter on the widget's slot
	// ================================================================
	MCP_TOOL(Registry, "set_widget_slot_property")
		.Description(TEXT("Set any property on a widget's slot by name (fallback for slot types / fields not covered by set_widget_slot), e.g. 'LayoutData.Offsets' = '(Left=0,Top=0,Right=200,Bottom=50)', 'Size.SizeRule' = 'Fill', 'ZOrder' = '3'."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget whose slot to modify"), true)
		.StringArg(TEXT("property_name"), TEXT("Slot property name or dotted path"), true)
		.StringArg(TEXT("value"), TEXT("Value in Unreal text-import syntax"), true)
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName, PropertyName, Value;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));
			if (!Args->TryGetStringField(TEXT("property_name"), PropertyName)) return FMCPToolResult::Error(TEXT("property_name is required"));
			if (!Args->TryGetStringField(TEXT("value"), Value)) return FMCPToolResult::Error(TEXT("value is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));
			if (!Widget->Slot) return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' has no slot (root widget?)"), *WidgetName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Slot Property")));
			WBP->Modify();
			FString Err;
			const bool bOk = SetPropertyFromString(Widget->Slot, PropertyName, Value, Err);
			if (bOk) Widget->Slot->SynchronizeProperties();
			GEditor->EndTransaction();
			if (!bOk) return FMCPToolResult::Error(Err);

			FString After;
			GetPropertyAsString(Widget->Slot, PropertyName, After, Err);
			SaveWidgetBlueprint(WBP, WantsSave(Args));
			return FMCPToolResult::Success(FString::Printf(TEXT("%s.Slot(%s).%s = %s"), *WidgetName, *Widget->Slot->GetClass()->GetName(), *PropertyName, *After));
		});

	// ================================================================
	// set_widget_designer_flags - Hidden / locked / expanded in designer
	// ================================================================
	MCP_TOOL(Registry, "set_widget_designer_flags")
		.Description(TEXT("Set the designer-only flags of a widget (the eye / lock icons and tree expansion in the UMG hierarchy). These are plain UPROPERTY() fields that Python cannot set. Only provided fields change."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("widget_name"), TEXT("Name of the widget"), true)
		.BoolArg(TEXT("hidden_in_designer"), TEXT("Hide in the designer preview (eye icon)"))
		.BoolArg(TEXT("locked_in_designer"), TEXT("Lock against designer selection/drag (lock icon)"))
		.BoolArg(TEXT("expanded_in_designer"), TEXT("Expanded in the hierarchy tree"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName)) return FMCPToolResult::Error(TEXT("widget_name is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Designer Flags")));
			WBP->Modify();
			Widget->Modify();
			TArray<FString> Changed;
			bool bVal;
			if (Args->TryGetBoolField(TEXT("hidden_in_designer"), bVal))   { Widget->bHiddenInDesigner = bVal;   Changed.Add(FString::Printf(TEXT("hidden=%d"), bVal)); }
			if (Args->TryGetBoolField(TEXT("locked_in_designer"), bVal))   { Widget->SetLockedInDesigner(bVal);  Changed.Add(FString::Printf(TEXT("locked=%d"), bVal)); }
			if (Args->TryGetBoolField(TEXT("expanded_in_designer"), bVal)) { Widget->bExpandedInDesigner = bVal; Changed.Add(FString::Printf(TEXT("expanded=%d"), bVal)); }
			GEditor->EndTransaction();

			if (Changed.Num() == 0) return FMCPToolResult::Success(TEXT("No designer flags provided"));
			SaveWidgetBlueprint(WBP, WantsSave(Args));
			return FMCPToolResult::Success(FString::Printf(TEXT("Designer flags on '%s': %s"), *WidgetName, *FString::Join(Changed, TEXT(", "))));
		});

	// ================================================================
	// set_widget_blueprint_defaults - Class defaults (CDO) + design-time size
	// ================================================================
	MCP_TOOL(Registry, "set_widget_blueprint_defaults")
		.Description(TEXT("Set class-default properties of the Widget Blueprint itself (its UserWidget CDO): e.g. 'bIsFocusable'='true', 'Priority'='10', 'TickFrequency'='Never', plus the designer preview size (design_size_mode / design_width / design_height). Only provided fields change."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("property_name"), TEXT("CDO property name or dotted path"))
		.StringArg(TEXT("value"), TEXT("Value in Unreal text-import syntax"))
		.EnumArg(TEXT("design_size_mode"), TEXT("Designer preview size mode"), { TEXT("FillScreen"), TEXT("Custom"), TEXT("CustomOnScreen"), TEXT("Desired"), TEXT("DesiredOnScreen") })
		.NumberArg(TEXT("design_width"), TEXT("Designer preview width (Custom modes)"))
		.NumberArg(TEXT("design_height"), TEXT("Designer preview height (Custom modes)"))
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, PropertyName, Value, SizeMode;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			Args->TryGetStringField(TEXT("property_name"), PropertyName);
			Args->TryGetStringField(TEXT("value"), Value);
			Args->TryGetStringField(TEXT("design_size_mode"), SizeMode);

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			if (!WBP->GeneratedClass) FKismetEditorUtilities::CompileBlueprint(WBP);
			UUserWidget* CDO = WBP->GeneratedClass ? WBP->GeneratedClass->GetDefaultObject<UUserWidget>() : nullptr;
			if (!CDO) return FMCPToolResult::Error(TEXT("Widget Blueprint has no generated class (compile errors?)"));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Blueprint Defaults")));
			WBP->Modify();
			CDO->Modify();
			TArray<FString> Changed;

			if (!PropertyName.IsEmpty())
			{
				FString Err;
				if (!SetPropertyFromString(CDO, PropertyName, Value, Err))
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(Err);
				}
				Changed.Add(FString::Printf(TEXT("%s=%s"), *PropertyName, *Value));
			}

			if (!SizeMode.IsEmpty())
			{
				EDesignPreviewSizeMode Mode = EDesignPreviewSizeMode::FillScreen;
				if (SizeMode == TEXT("Custom")) Mode = EDesignPreviewSizeMode::Custom;
				else if (SizeMode == TEXT("CustomOnScreen")) Mode = EDesignPreviewSizeMode::CustomOnScreen;
				else if (SizeMode == TEXT("Desired")) Mode = EDesignPreviewSizeMode::Desired;
				else if (SizeMode == TEXT("DesiredOnScreen")) Mode = EDesignPreviewSizeMode::DesiredOnScreen;
				CDO->DesignSizeMode = Mode;
				Changed.Add(FString::Printf(TEXT("DesignSizeMode=%s"), *SizeMode));
			}
			if (Args->HasField(TEXT("design_width")) || Args->HasField(TEXT("design_height")))
			{
				FVector2D Size = CDO->DesignTimeSize;
				if (Args->HasField(TEXT("design_width")))  Size.X = Args->GetNumberField(TEXT("design_width"));
				if (Args->HasField(TEXT("design_height"))) Size.Y = Args->GetNumberField(TEXT("design_height"));
				CDO->DesignTimeSize = Size;
				Changed.Add(FString::Printf(TEXT("DesignTimeSize=(%.0f,%.0f)"), Size.X, Size.Y));
			}
			GEditor->EndTransaction();

			if (Changed.Num() == 0) return FMCPToolResult::Success(TEXT("Nothing to change (pass property_name/value or design_* fields)"));

			FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
			SaveWidgetBlueprint(WBP, WantsSave(Args));
			return FMCPToolResult::Success(FString::Printf(TEXT("Widget Blueprint defaults updated: %s"), *FString::Join(Changed, TEXT(", "))));
		});
}

} // namespace MCPWidgetTools::Properties
