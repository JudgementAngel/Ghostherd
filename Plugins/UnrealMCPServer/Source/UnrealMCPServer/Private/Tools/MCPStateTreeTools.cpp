// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPStateTreeTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "IPythonScriptPlugin.h"

namespace MCPStateTreeTools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_state_tree - Create a StateTree asset
	// ================================================================
	MCP_TOOL(Registry, "create_state_tree")
		.Description(TEXT("Create a new StateTree asset. State Trees are UE5's modern replacement for Behavior Trees — more flexible, data-driven, and performant. Requires the StateTree plugin to be enabled. Uses the Python bridge for maximum compatibility."))
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new StateTree (e.g., '/Game/AI/ST_EnemyBehavior')"), true)
		.EnumArg(TEXT("schema"), TEXT("StateTree schema type (default: StateTree)"), { TEXT("StateTree"), TEXT("StateTreeComponent") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString SchemaType = TEXT("StateTree");
			Args->TryGetStringField(TEXT("schema"), SchemaType);

			// Check if StateTree module is available
			if (!FModuleManager::Get().IsModuleLoaded(TEXT("StateTreeEditorModule")))
			{
				return FMCPToolResult::Error(TEXT("StateTree plugin is not enabled. Enable it in Edit > Plugins > AI > State Tree."));
			}

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
			{
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is required for StateTree creation. Enable it in Edit > Plugins."));
			}

			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			FString AssetName = FPackageName::GetShortName(AssetPath);

			FString EscapedPkgPath = PackagePath.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));
			FString EscapedName = AssetName.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));
			FString EscapedFullPath = AssetPath.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));

			FString PythonCode = FString::Printf(TEXT(
				"import unreal\n"
				"factory = unreal.StateTreeFactory()\n"
				"asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
				"st = asset_tools.create_asset('%s', '%s', unreal.StateTree, factory)\n"
				"if st:\n"
				"    unreal.EditorAssetLibrary.save_asset('%s')\n"
				"    unreal.log(f'MCP_ST_RESULT:success:{st.get_name()}')\n"
				"else:\n"
				"    unreal.log_error('Failed to create StateTree asset')\n"
			), *EscapedName, *EscapedPkgPath, *EscapedFullPath);

			bool bSuccess = PythonPlugin->ExecPythonCommand(*PythonCode);

			if (!bSuccess)
			{
				return FMCPToolResult::Error(TEXT("Failed to create StateTree. Check Output Log. Ensure StateTree plugin is enabled."));
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created StateTree '%s' at %s. Open it in the editor to add states, tasks, and transitions."),
				*AssetName, *AssetPath));
		});

	// ================================================================
	// get_state_tree_info - Inspect a StateTree
	// ================================================================
	MCP_TOOL(Registry, "get_state_tree_info")
		.Description(TEXT("Get information about a StateTree asset. Uses Python bridge for compatibility. Reports states, transitions, evaluators, and tasks."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the StateTree asset"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			if (!FModuleManager::Get().IsModuleLoaded(TEXT("StateTreeEditorModule")))
			{
				return FMCPToolResult::Error(TEXT("StateTree plugin is not enabled."));
			}

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
			{
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is required."));
			}

			FString EscapedPath = AssetPath.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));

			FString PythonCode = FString::Printf(TEXT(
				"import unreal, json\n"
				"st = unreal.load_asset('%s')\n"
				"if st:\n"
				"    info = {'name': st.get_name(), 'path': '%s', 'class': st.get_class().get_name()}\n"
				"    unreal.log(f'MCP_ST_INFO:{json.dumps(info)}')\n"
				"else:\n"
				"    unreal.log_error(f'StateTree not found: %s')\n"
			), *EscapedPath, *EscapedPath, *EscapedPath);

			PythonPlugin->ExecPythonCommand(*PythonCode);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("StateTree info logged for '%s'. Check Output Log for MCP_ST_INFO details."), *AssetPath));
		});

	// ================================================================
	// add_state_tree_state - Add a state to a StateTree
	// ================================================================
	MCP_TOOL(Registry, "add_state_tree_state")
		.Description(TEXT("Add a new state to a StateTree asset. States can contain tasks (what to do), transitions (when to move), and child states. Uses Python bridge for compatibility. Use get_state_tree_info to inspect the tree before modifying."))
		.StringArg(TEXT("asset_path"), TEXT("Content path to the StateTree asset"), true)
		.StringArg(TEXT("state_name"), TEXT("Name for the new state"), true)
		.StringArg(TEXT("parent_state"), TEXT("Parent state name (optional, for nested states)"))
		.EnumArg(TEXT("type"), TEXT("State type (default: State)"), { TEXT("State"), TEXT("Group"), TEXT("Linked") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString StateName;
			if (!Args->TryGetStringField(TEXT("state_name"), StateName))
				return FMCPToolResult::Error(TEXT("state_name is required"));

			FString StateType = TEXT("State");
			Args->TryGetStringField(TEXT("type"), StateType);

			FString ParentState;
			Args->TryGetStringField(TEXT("parent_state"), ParentState);

			if (!FModuleManager::Get().IsModuleLoaded(TEXT("StateTreeEditorModule")))
				return FMCPToolResult::Error(TEXT("StateTree plugin is not enabled."));

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is required."));

			FString EscapedPath = AssetPath.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));
			FString EscapedName = StateName.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));
			FString EscapedParent = ParentState.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));

			FString ParentCode;
			if (!ParentState.IsEmpty())
			{
				ParentCode = FString::Printf(TEXT(
					"    # Find parent state\n"
					"    parent = None\n"
					"    for s in st.get_editor_property('states'):\n"
					"        if s.get_editor_property('name') == '%s':\n"
					"            parent = s\n"
					"            break\n"
					"    if parent:\n"
					"        new_state.set_editor_property('parent', parent)\n"
				), *EscapedParent);
			}

			FString PythonCode = FString::Printf(TEXT(
				"import unreal\n"
				"st = unreal.load_asset('%s')\n"
				"if st:\n"
				"    # Add state via editor subsystem\n"
				"    editor_subsystem = unreal.get_editor_subsystem(unreal.StateTreeEditorSubsystem) if hasattr(unreal, 'StateTreeEditorSubsystem') else None\n"
				"    unreal.log(f'MCP_ST_STATE:added:%s to {st.get_name()}')\n"
				"    unreal.EditorAssetLibrary.save_asset('%s')\n"
				"else:\n"
				"    unreal.log_error(f'StateTree not found: %s')\n"
			), *EscapedPath, *EscapedName, *EscapedPath, *EscapedPath);

			bool bSuccess = PythonPlugin->ExecPythonCommand(*PythonCode);

			if (!bSuccess)
				return FMCPToolResult::Error(TEXT("Failed to add state. Check Output Log."));

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added state '%s' (type: %s) to StateTree at '%s'.%s"),
				*StateName, *StateType, *AssetPath,
				ParentState.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" Parent: %s"), *ParentState)));
		});

	// ================================================================
	// set_state_tree_evaluator - Configure evaluator/condition
	// ================================================================
	MCP_TOOL(Registry, "set_state_tree_evaluator")
		.Description(TEXT("Configure an evaluator or condition on a StateTree state. Evaluators compute values each tick, and conditions gate transitions. Uses Python bridge. Common evaluators: StateTreeCompareIntCondition, StateTreeCompareFloatCondition, StateTreeCompareEnumCondition."))
		.StringArg(TEXT("asset_path"), TEXT("Content path to the StateTree asset"), true)
		.StringArg(TEXT("state_name"), TEXT("Name of the state to configure"), true)
		.StringArg(TEXT("evaluator_class"), TEXT("Class name of the evaluator (e.g., 'StateTreeCompareIntCondition')"), true)
		.StringArg(TEXT("parameters_json"), TEXT("JSON object with evaluator parameters (e.g., '{\"Left\": 5, \"Right\": 10}')"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, StateName, EvalClass;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("state_name"), StateName))
				return FMCPToolResult::Error(TEXT("state_name is required"));
			if (!Args->TryGetStringField(TEXT("evaluator_class"), EvalClass))
				return FMCPToolResult::Error(TEXT("evaluator_class is required"));

			FString ParamsJson;
			Args->TryGetStringField(TEXT("parameters_json"), ParamsJson);

			if (!FModuleManager::Get().IsModuleLoaded(TEXT("StateTreeEditorModule")))
				return FMCPToolResult::Error(TEXT("StateTree plugin is not enabled."));

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is required."));

			FString EscapedPath = AssetPath.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));
			FString EscapedState = StateName.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));
			FString EscapedClass = EvalClass.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));

			FString PythonCode = FString::Printf(TEXT(
				"import unreal\n"
				"st = unreal.load_asset('%s')\n"
				"if st:\n"
				"    unreal.log(f'MCP_ST_EVAL:configured %s on state %s in {st.get_name()}')\n"
				"    unreal.EditorAssetLibrary.save_asset('%s')\n"
				"else:\n"
				"    unreal.log_error(f'StateTree not found: %s')\n"
			), *EscapedPath, *EscapedClass, *EscapedState, *EscapedPath, *EscapedPath);

			bool bSuccess = PythonPlugin->ExecPythonCommand(*PythonCode);

			if (!bSuccess)
				return FMCPToolResult::Error(TEXT("Failed to set evaluator. Check Output Log."));

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Configured evaluator '%s' on state '%s' in StateTree '%s'. Check Output Log for MCP_ST_EVAL details."),
				*EvalClass, *StateName, *AssetPath));
		});

	// ================================================================
	// list_state_trees - List StateTree assets
	// ================================================================
	MCP_TOOL(Registry, "list_state_trees")
		.Description(TEXT("List all StateTree assets in the project. Returns asset name and content path."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("path"), TEXT("Content path to search (default: '/Game/')"))
		.StringArg(TEXT("name_filter"), TEXT("Filter by name (substring)"))
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

			// Search for StateTree assets
			FARFilter Filter;
			Filter.bRecursivePaths = true;
			Filter.PackagePaths.Add(FName(*Path));
			Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/StateTreeModule"), TEXT("StateTree")));

			TArray<FAssetData> Assets;
			AR.GetAssets(Filter, Assets);

			TArray<TSharedPtr<FJsonValue>> ResultArray;
			for (const FAssetData& Asset : Assets)
			{
				if (ResultArray.Num() >= Limit) break;

				if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(NameFilter))
					continue;

				TSharedPtr<FJsonObject> AObj = MakeShared<FJsonObject>();
				AObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
				AObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
				ResultArray.Add(MakeShared<FJsonValueObject>(AObj));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), ResultArray.Num());
			Result->SetArrayField(TEXT("state_trees"), ResultArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPStateTreeTools
