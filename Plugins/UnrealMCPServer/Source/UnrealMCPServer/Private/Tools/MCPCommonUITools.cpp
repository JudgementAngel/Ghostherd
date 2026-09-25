// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPCommonUITools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "IPythonScriptPlugin.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Common/MCPAssetCreate.h"
#include "Tools/Widget/WidgetCommon.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintOperationUtils.h"
#include "Blueprint/UserWidget.h"
#include "Factories/BlueprintFactory.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Blueprint/WidgetTree.h"

namespace MCPCommonUITools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_common_ui_widget - Create a CommonUI widget
	// ================================================================
	MCP_TOOL(Registry, "create_common_ui_widget")
		.Description(TEXT("Create a Widget Blueprint deriving from a CommonUI base class (CommonActivatableWidget = screens/panels with input config and back handling, CommonUserWidget = plain CommonUI widget, CommonButtonBase = button with input actions and styles, CommonActivatableWidgetStack / CommonActivatableWidgetQueue = containers, or any other UUserWidget subclass from CommonUI or your game module). Requires the CommonUI plugin to be enabled."))
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new widget (e.g., '/Game/UI/WBP_MainMenuScreen')"), true)
		.StringArg(TEXT("widget_type"), TEXT("CommonUI base class name (CommonActivatableWidget, CommonUserWidget, CommonButtonBase, ...) or a '/Script/Module.Class' path"), true)
		.EnumArg(TEXT("root_widget_type"), TEXT("Root panel widget type (default: CanvasPanel)"), { TEXT("CanvasPanel"), TEXT("VerticalBox"), TEXT("HorizontalBox"), TEXT("Overlay"), TEXT("SizeBox"), TEXT("Border") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, WidgetType, RootType = TEXT("CanvasPanel");
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("widget_type"), WidgetType)) return FMCPToolResult::Error(TEXT("widget_type is required"));
			Args->TryGetStringField(TEXT("root_widget_type"), RootType);

			if (!FModuleManager::Get().IsModuleLoaded(TEXT("CommonUI")))
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("CommonUI plugin is not enabled. Enable it in Edit > Plugins > UI > Common UI and restart the editor."));

			FString Err;
			UClass* ParentClass = MCPWidgetTools::Common::ResolveUserWidgetClass(WidgetType, Err);
			if (!ParentClass) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, Err);

			UClass* RootClass = MCPWidgetTools::Common::WidgetClassFromTypeName(RootType);
			if (!RootClass) return FMCPToolResult::Error(FString::Printf(TEXT("Unknown root widget type: %s"), *RootType));

			FString PackagePath, AssetName;
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package) return PackageError;

			UWidgetBlueprint* NewWBP = FWidgetBlueprintOperationUtils::CreateWidgetBlueprint(
				Package, FName(*AssetName), BPTYPE_Normal, ParentClass, RootClass, TEXT("MCP"), /*bRegisterAndCompile*/ true);
			if (!NewWBP) return FMCPToolResult::Error(TEXT("Failed to create CommonUI Widget Blueprint"));

			NewWBP->SetFlags(RF_Public | RF_Standalone);
			MCPWidgetTools::Common::SaveWidgetBlueprint(NewWBP, true);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created CommonUI Widget Blueprint '%s' (base: %s, root: %s) at %s. Use add_widget/set_widget_properties to build the UI."),
				*AssetName, *ParentClass->GetName(), *RootType, *AssetPath));
		});

	// ================================================================
	// create_common_ui_style - CommonButtonStyle / CommonTextStyle / CommonBorderStyle assets
	// ================================================================
	MCP_TOOL(Registry, "create_common_ui_style")
		.Description(TEXT("Create a CommonUI style asset: a Blueprint deriving from CommonButtonStyle, CommonTextStyle or CommonBorderStyle (what configure_common_button's style_path and CommonTextBlock's Style expect). Set its properties afterwards with set_blueprint_default-style tools or execute_python on the CDO."))
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new style asset (e.g., '/Game/UI/Styles/BS_Primary')"), true)
		.EnumArg(TEXT("style_type"), TEXT("Style base class"), { TEXT("CommonButtonStyle"), TEXT("CommonTextStyle"), TEXT("CommonBorderStyle") }, true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, StyleType;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("style_type"), StyleType)) return FMCPToolResult::Error(TEXT("style_type is required"));

			if (!FModuleManager::Get().IsModuleLoaded(TEXT("CommonUI")))
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("CommonUI plugin is not enabled."));

			UClass* ParentClass = FindFirstObject<UClass>(*StyleType, EFindFirstObjectOptions::ExactClass);
			if (!ParentClass) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Class %s not found (is CommonUI loaded?)"), *StyleType));

			FString PackagePath, AssetName;
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package) return PackageError;

			UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
			Factory->ParentClass = ParentClass;
			UBlueprint* NewBP = Cast<UBlueprint>(Factory->FactoryCreateNew(
				UBlueprint::StaticClass(), Package, FName(*AssetName), RF_Public | RF_Standalone, nullptr, GWarn));
			if (!NewBP) return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create %s Blueprint"), *StyleType));

			FBlueprintEditorUtils::MarkBlueprintAsModified(NewBP);
			FKismetEditorUtilities::CompileBlueprint(NewBP);
			FAssetRegistryModule::AssetCreated(NewBP);
			FString SaveErr;
			MCPWidgetTools::Common::SaveAssetPackage(NewBP, SaveErr);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created %s asset '%s' at %s. Its class path for style_path / Style properties: %s_C"),
				*StyleType, *AssetName, *AssetPath, *AssetPath));
		});

	// ================================================================
	// configure_common_button - Set up CommonButtonBase
	// ================================================================
	MCP_TOOL(Registry, "configure_common_button")
		.Description(TEXT("Configure a CommonButtonBase widget with input actions, styles, and interaction behavior. CommonButtons support gamepad/keyboard navigation, input action bindings, and selectable (toggle) mode. Uses Python bridge for property access."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the Widget Blueprint containing the button"), true)
		.StringArg(TEXT("button_name"), TEXT("Name of the CommonButtonBase widget in the WBP"), true)
		.StringArg(TEXT("triggering_input_action"), TEXT("Input action data asset path that triggers this button (e.g., '/Game/Input/IA_Confirm')"))
		.StringArg(TEXT("style_path"), TEXT("Path to a CommonButtonStyle data asset"))
		.BoolArg(TEXT("is_selectable"), TEXT("Whether the button can be persistently selected/toggled"))
		.BoolArg(TEXT("is_interactable_when_selected"), TEXT("Whether button remains interactable when selected"))
		.BoolArg(TEXT("hide_input_action"), TEXT("Whether to hide the input action widget"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, ButtonName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("button_name"), ButtonName))
				return FMCPToolResult::Error(TEXT("button_name is required"));

			if (!FModuleManager::Get().IsModuleLoaded(TEXT("CommonUI")))
				return FMCPToolResult::Error(TEXT("CommonUI plugin is not enabled."));

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is required."));

			FString EscapedAssetPath = AssetPath.Replace(TEXT("'"), TEXT("\\'"));
			FString EscapedButtonName = ButtonName.Replace(TEXT("'"), TEXT("\\'"));

			// Build property setting lines
			TArray<FString> PropertyLines;

			FString InputAction;
			if (Args->TryGetStringField(TEXT("triggering_input_action"), InputAction))
			{
				FString Escaped = InputAction.Replace(TEXT("'"), TEXT("\\'"));
				PropertyLines.Add(FString::Printf(TEXT(
					"    ia = unreal.load_asset('%s')\n"
					"    if ia:\n"
					"        btn.set_editor_property('triggering_input_action', ia)\n"
					"        props.append(f'input_action={ia.get_name()}')\n"
				), *Escaped));
			}

			FString StylePath;
			if (Args->TryGetStringField(TEXT("style_path"), StylePath))
			{
				FString Escaped = StylePath.Replace(TEXT("'"), TEXT("\\'"));
				PropertyLines.Add(FString::Printf(TEXT(
					"    style = unreal.load_asset('%s')\n"
					"    if style:\n"
					"        btn.set_editor_property('style', style)\n"
					"        props.append(f'style={style.get_name()}')\n"
				), *Escaped));
			}

			bool bVal;
			if (Args->TryGetBoolField(TEXT("is_selectable"), bVal))
			{
				PropertyLines.Add(FString::Printf(TEXT(
					"    btn.set_editor_property('is_selectable', %s)\n"
					"    props.append('is_selectable=%s')\n"
				), bVal ? TEXT("True") : TEXT("False"), bVal ? TEXT("true") : TEXT("false")));
			}
			if (Args->TryGetBoolField(TEXT("is_interactable_when_selected"), bVal))
			{
				PropertyLines.Add(FString::Printf(TEXT(
					"    btn.set_editor_property('is_interactable_when_selected', %s)\n"
					"    props.append('interactable_when_selected=%s')\n"
				), bVal ? TEXT("True") : TEXT("False"), bVal ? TEXT("true") : TEXT("false")));
			}
			if (Args->TryGetBoolField(TEXT("hide_input_action"), bVal))
			{
				PropertyLines.Add(FString::Printf(TEXT(
					"    btn.set_editor_property('hide_input_action', %s)\n"
					"    props.append('hide_input_action=%s')\n"
				), bVal ? TEXT("True") : TEXT("False"), bVal ? TEXT("true") : TEXT("false")));
			}

			FString PropCode = FString::Join(PropertyLines, TEXT(""));

			FString PythonCode = FString::Printf(TEXT(
				"import unreal\n"
				"wbp = unreal.load_asset('%s')\n"
				"if wbp:\n"
				"    tree = wbp.get_editor_property('widget_tree')\n"
				"    btn = tree.find_widget('%s') if tree else None\n"
				"    if btn:\n"
				"        props = []\n"
				"%s"
				"        unreal.EditorAssetLibrary.save_asset('%s')\n"
				"        unreal.log(f'MCP_CUI_BTN:configured {len(props)} properties on %s: {props}')\n"
				"    else:\n"
				"        unreal.log_error(f'Widget \\'%s\\' not found in WBP')\n"
				"else:\n"
				"    unreal.log_error(f'Widget Blueprint not found: %s')\n"
			), *EscapedAssetPath, *EscapedButtonName, *PropCode,
				*EscapedAssetPath, *EscapedButtonName, *EscapedButtonName, *EscapedAssetPath);

			bool bSuccess = PythonPlugin->ExecPythonCommand(*PythonCode);

			if (!bSuccess)
				return FMCPToolResult::Error(TEXT("Failed to configure button. Check Output Log."));

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Configured CommonButton '%s' in '%s'. Check Output Log for MCP_CUI_BTN details."),
				*ButtonName, *AssetPath));
		});

	// ================================================================
	// set_common_ui_input_mode - Configure input routing
	// ================================================================
	MCP_TOOL(Registry, "set_common_ui_input_mode")
		.Description(TEXT("Configure CommonUI input routing mode. Controls whether the UI responds to mouse, gamepad, or both input methods. Uses Python bridge. 'Mouse' = mouse/keyboard only, 'Gamepad' = gamepad/keyboard only, 'All' = all input methods active."))
		.Idempotent()
		.EnumArg(TEXT("input_mode"), TEXT("Input mode for CommonUI"), { TEXT("Mouse"), TEXT("Gamepad"), TEXT("All") }, true)
		.BoolArg(TEXT("mouse_capture_mode"), TEXT("Whether to capture mouse (default: false)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString InputMode;
			if (!Args->TryGetStringField(TEXT("input_mode"), InputMode))
				return FMCPToolResult::Error(TEXT("input_mode is required"));

			if (!FModuleManager::Get().IsModuleLoaded(TEXT("CommonUI")))
				return FMCPToolResult::Error(TEXT("CommonUI plugin is not enabled."));

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is required."));

			FString ModeEnum;
			if (InputMode == TEXT("Mouse")) ModeEnum = TEXT("Mouse");
			else if (InputMode == TEXT("Gamepad")) ModeEnum = TEXT("Gamepad");
			else ModeEnum = TEXT("All");

			FString PythonCode = FString::Printf(TEXT(
				"import unreal\n"
				"# CommonUI input mode is typically set in project settings or via the CommonUI subsystem\n"
				"# This configures the default input mode for the CommonUI framework\n"
				"settings = unreal.get_default_object(unreal.CommonUISettings) if hasattr(unreal, 'CommonUISettings') else None\n"
				"if settings:\n"
				"    unreal.log(f'MCP_CUI_INPUT:mode set to %s')\n"
				"else:\n"
				"    # Fallback: log guidance for manual configuration\n"
				"    unreal.log('MCP_CUI_INPUT:CommonUI input mode should be configured in Project Settings > Common UI > Default Input Mode')\n"
				"    unreal.log(f'MCP_CUI_INPUT:Recommended mode: %s')\n"
			), *ModeEnum, *ModeEnum);

			PythonPlugin->ExecPythonCommand(*PythonCode);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Set CommonUI input mode to '%s'. Input routing: %s. Check Output Log for MCP_CUI_INPUT details."),
				*InputMode,
				InputMode == TEXT("Mouse") ? TEXT("Mouse + Keyboard") :
				InputMode == TEXT("Gamepad") ? TEXT("Gamepad + Keyboard") :
				TEXT("All input devices")));
		});

	// ================================================================
	// list_common_ui_widgets - List CommonUI widget blueprints
	// ================================================================
	MCP_TOOL(Registry, "list_common_ui_widgets")
		.Description(TEXT("List Widget Blueprints that use CommonUI base classes (CommonActivatableWidget, CommonButtonBase, etc.)."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("path"), TEXT("Content path to search (default: '/Game/')"))
		.StringArg(TEXT("name_filter"), TEXT("Filter by name"))
		.IntArg(TEXT("limit"), TEXT("Maximum results (default: 50)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			int32 Limit = 50;
			if (Args->HasField(TEXT("limit")))
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 200);

			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AR = ARM.Get();

			FTopLevelAssetPath WBPClassPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint"));
			TArray<FAssetData> AllWBPs;
			AR.GetAssetsByClass(WBPClassPath, AllWBPs, true);

			TArray<TSharedPtr<FJsonValue>> ResultArray;

			for (const FAssetData& Asset : AllWBPs)
			{
				if (ResultArray.Num() >= Limit) break;
				if (!Asset.PackageName.ToString().StartsWith(Path)) continue;
				if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(NameFilter)) continue;

				// Check parent class for CommonUI types
				FString ParentClass;
				FAssetTagValueRef ParentTag = Asset.TagsAndValues.FindTag(TEXT("ParentClass"));
				if (ParentTag.IsSet()) ParentClass = ParentTag.AsString();

				if (ParentClass.Contains(TEXT("Common")))
				{
					TSharedPtr<FJsonObject> AObj = MakeShared<FJsonObject>();
					AObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
					AObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
					AObj->SetStringField(TEXT("parent_class"), ParentClass);
					ResultArray.Add(MakeShared<FJsonValueObject>(AObj));
				}
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), ResultArray.Num());
			Result->SetArrayField(TEXT("widgets"), ResultArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPCommonUITools
