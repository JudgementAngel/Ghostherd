// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.6: rewritten onto the result-file pattern already used by MCPMoltTools.
//
// The v4.5 versions were fire-and-forget: they ran Python that logged
// "MCP_CR_INFO:{json}" to the Output Log and then returned the literal string
// "Control Rig info logged. Check Output Log" — so get_control_rig_info handed
// the agent no data at all, and create_control_rig trusted ExecPythonCommand's
// bool without ever confirming the asset existed. Both now write JSON to a file
// under Saved/ and read it back, which is the difference between a tool an
// agent can act on and one it can only hope worked.

#include "Tools/MCPControlRigTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"
#include "MCPSettings.h"
#include "MCPValidate.h"

#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Control Rig is an optional plugin — reached through the Python bridge so this
// module links without it.
#include "Modules/ModuleManager.h"
#include "IPythonScriptPlugin.h"
#include "Common/MCPAssetCreate.h"

namespace MCPControlRigTools
{

/** Where the Python side drops its JSON result. */
static FString ControlRigResultPath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("mcp_controlrig_result.json"))
		.Replace(TEXT("\\"), TEXT("/"));
}

/** Escape for a Python single-quoted literal. */
static FString PyEscape(const FString& In)
{
	return In.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));
}

/** Confirm the ControlRig plugin and the Python bridge are both usable. */
static bool RequireControlRigPython(IPythonScriptPlugin*& OutPython, FMCPToolResult& OutError)
{
	const UMCPSettings* Settings = UMCPSettings::Get();
	if (Settings && !Settings->bEnablePythonBridge)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
			TEXT("The Python bridge is disabled, and the Control Rig tools depend on it."),
			TEXT("Enable it in Project Settings > Plugins > Unreal MCP Server > Python."));
		return false;
	}

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("ControlRigEditor")))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
			TEXT("The Control Rig plugin is not enabled."),
			TEXT("Enable it in Edit > Plugins > Animation > Control Rig, then restart the editor."));
		return false;
	}

	OutPython = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
	if (!OutPython)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
			TEXT("PythonScriptPlugin is not loaded, and the Control Rig tools depend on it."),
			TEXT("Enable it in Edit > Plugins > Scripting > Python Editor Script Plugin."));
		return false;
	}
	return true;
}

/** Run a Python snippet that writes JSON to ResultFile, then read it back.
 *  Returns nullptr and fills OutError when the script produced nothing (which
 *  is what a Python-side syntax error or hard exception looks like from here). */
static TSharedPtr<FJsonObject> RunPythonForResult(IPythonScriptPlugin* Python,
	const FString& Code, const FString& ResultFile, FMCPToolResult& OutError)
{
	// Clear any stale result first — otherwise a script that dies before writing
	// leaves the previous call's answer behind and we report it as this one's.
	IFileManager::Get().Delete(*ResultFile, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);

	Python->ExecPythonCommand(*Code);

	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *ResultFile))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal,
			TEXT("The Control Rig script produced no result file."),
			TEXT("The Python side failed before it could write a result. Check the Output Log for the traceback."));
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Result) || !Result.IsValid())
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal,
			TEXT("The Control Rig script wrote a result that is not valid JSON."));
		return nullptr;
	}

	bool bOk = false;
	Result->TryGetBoolField(TEXT("ok"), bOk);
	if (!bOk)
	{
		FString ScriptError;
		Result->TryGetStringField(TEXT("error"), ScriptError);
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal,
			ScriptError.IsEmpty() ? TEXT("The Control Rig script reported failure.") : ScriptError);
		return nullptr;
	}

	return Result;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_control_rig
	// ================================================================
	MCP_TOOL(Registry, "create_control_rig")
		.Description(TEXT(
			"Create a Control Rig Blueprint for a skeleton. Control Rigs drive a skeleton procedurally — IK "
			"chains, bone constraints, and the animator-facing controls used in Sequencer. The rig is created "
			"with the skeleton's preview mesh assigned; its actual rig graph is authored in the Control Rig "
			"editor, which has no scripting surface for node authoring.\n"
			"Requires the ControlRig plugin and the Python bridge. To bake Control Rig work in a level "
			"sequence down to a reusable animation, use bake_sequence_to_anim_sequence."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new Control Rig Blueprint (e.g., '/Game/Characters/CR_Hero')"), true)
		.StringArg(TEXT("skeleton_path"), TEXT("Content path to the USkeleton asset (e.g., '/Game/Characters/SK_Hero_Skeleton')"), true)
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/CR_Hero\", \"skeleton_path\": \"/Game/Characters/SK_Hero_Skeleton\"}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString AssetPath, SkeletonPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("skeleton_path"), SkeletonPath));

			USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
			if (!IsValid(Skeleton))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));
			}

			const FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			const FString AssetName = FPackageName::GetShortName(AssetPath);
			BAIL_IF_INVALID(FMCPValidate::PackagePath(PackagePath));
			BAIL_IF_INVALID(FMCPValidate::AssetName(AssetName));
			BAIL_IF_INVALID(FMCPValidate::AssetDoesNotExist(PackagePath));

			IPythonScriptPlugin* Python = nullptr;
			FMCPToolResult Err;
			if (!RequireControlRigPython(Python, Err)) return Err;

			const FString ResultFile = ControlRigResultPath();
			const FString Code = FString::Printf(TEXT(
				"import unreal, json\n"
				"res = dict(ok=False)\n"
				"try:\n"
				"    at = unreal.AssetToolsHelpers.get_asset_tools()\n"
				"    rig = at.create_asset('%s', '%s', unreal.ControlRigBlueprint, unreal.ControlRigBlueprintFactory())\n"
				"    if rig is None:\n"
				"        raise Exception('create_asset returned None')\n"
				"    skel = unreal.load_asset('%s')\n"
				"    mesh_name = ''\n"
				"    if skel is not None:\n"
				"        mesh = skel.get_preview_mesh(True)\n"
				"        if mesh is not None:\n"
				"            rig.set_preview_mesh(mesh, True)\n"
				"            mesh_name = mesh.get_path_name()\n"
				"    unreal.EditorAssetLibrary.save_asset('%s', only_if_is_dirty=False)\n"
				"    res = dict(ok=True, path='%s', name=rig.get_name(), preview_mesh=mesh_name,\n"
				"               exists=unreal.EditorAssetLibrary.does_asset_exist('%s'))\n"
				"except Exception as e:\n"
				"    res = dict(ok=False, error=str(e)[:400])\n"
				"open('%s', 'w').write(json.dumps(res))\n"),
				*PyEscape(AssetName), *PyEscape(PackagePath), *PyEscape(SkeletonPath),
				*PyEscape(AssetPath), *PyEscape(AssetPath), *PyEscape(AssetPath),
				*PyEscape(ResultFile));

			TSharedPtr<FJsonObject> Result = RunPythonForResult(Python, Code, ResultFile, Err);
			if (!Result) return Err;

			// Verify on the C++ side rather than trusting the script's own report.
			bool bExists = false;
			Result->TryGetBoolField(TEXT("exists"), bExists);
			if (!bExists)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("The Control Rig script reported success but no asset exists at '%s'."), *AssetPath));
			}

			// Journal the creation so run_tool_script can report truthfully that this
			// asset survives a rollback (UE package creation is not transactional).
			MCPCommon::NoteAssetCreated(PackagePath);

			Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());

			FString PreviewMesh;
			Result->TryGetStringField(TEXT("preview_mesh"), PreviewMesh);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Created Control Rig Blueprint '%s' for skeleton '%s'%s. Author its rig graph in the Control Rig editor; node authoring has no scripting API."),
					*AssetPath, *Skeleton->GetName(),
					PreviewMesh.IsEmpty()
						? TEXT(" (the skeleton has no preview mesh, so none was assigned)")
						: *FString::Printf(TEXT(" with preview mesh '%s'"), *PreviewMesh)),
				Result);
		});

	// ================================================================
	// get_control_rig_info
	// ================================================================
	MCP_TOOL(Registry, "get_control_rig_info")
		.Description(TEXT(
			"Read a Control Rig Blueprint's structure: preview mesh, and the rig hierarchy broken down into "
			"bones, controls and nulls with their names. Controls are the animator-facing handles a Sequencer "
			"Control Rig track keys, so this is how you discover what a rig actually exposes before animating "
			"it. Requires the ControlRig plugin and the Python bridge."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the Control Rig Blueprint"), true)
		.IntArg(TEXT("limit"), TEXT("Maximum hierarchy elements to return per category (default: 100)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString AssetPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));

			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 1000);
			}

			IPythonScriptPlugin* Python = nullptr;
			FMCPToolResult Err;
			if (!RequireControlRigPython(Python, Err)) return Err;

			const FString ResultFile = ControlRigResultPath();
			const FString Code = FString::Printf(TEXT(
				"import unreal, json\n"
				"res = dict(ok=False)\n"
				"try:\n"
				"    rig = unreal.load_asset('%s')\n"
				"    if rig is None:\n"
				"        raise Exception(\"Control Rig not found: %s\")\n"
				"    info = dict(ok=True, name=rig.get_name(), path='%s', asset_class=rig.get_class().get_name())\n"
				"    mesh = rig.get_preview_mesh()\n"
				"    if mesh is not None:\n"
				"        info['preview_mesh'] = mesh.get_name()\n"
				"        info['preview_mesh_path'] = mesh.get_path_name()\n"
				"    bones, controls, nulls, curves = [], [], [], []\n"
				"    try:\n"
				"        h = rig.get_hierarchy()\n"
				"        for key in h.get_all_keys(True):\n"
				"            t = str(key.type).split('.')[-1].upper()\n"
				"            n = str(key.name)\n"
				"            if t == 'BONE' and len(bones) < %d: bones.append(n)\n"
				"            elif t == 'CONTROL' and len(controls) < %d: controls.append(n)\n"
				"            elif t == 'NULL' and len(nulls) < %d: nulls.append(n)\n"
				"            elif t == 'CURVE' and len(curves) < %d: curves.append(n)\n"
				"    except Exception as he:\n"
				"        info['hierarchy_error'] = str(he)[:200]\n"
				"    info['bones'] = bones\n"
				"    info['controls'] = controls\n"
				"    info['nulls'] = nulls\n"
				"    info['curves'] = curves\n"
				"    info['bone_count'] = len(bones)\n"
				"    info['control_count'] = len(controls)\n"
				"    info['null_count'] = len(nulls)\n"
				"    info['curve_count'] = len(curves)\n"
				"    res = info\n"
				"except Exception as e:\n"
				"    res = dict(ok=False, error=str(e)[:400])\n"
				"open('%s', 'w').write(json.dumps(res))\n"),
				*PyEscape(AssetPath), *PyEscape(AssetPath), *PyEscape(AssetPath),
				Limit, Limit, Limit, Limit,
				*PyEscape(ResultFile));

			TSharedPtr<FJsonObject> Result = RunPythonForResult(Python, Code, ResultFile, Err);
			if (!Result) return Err;

			int32 BoneCount = 0, ControlCount = 0;
			Result->TryGetNumberField(TEXT("bone_count"), BoneCount);
			Result->TryGetNumberField(TEXT("control_count"), ControlCount);

			FString HierarchyError;
			const bool bHierarchyFailed = Result->TryGetStringField(TEXT("hierarchy_error"), HierarchyError);

			return FMCPToolResult::SuccessStructured(
				bHierarchyFailed
					? FString::Printf(TEXT("Read Control Rig '%s', but its hierarchy could not be enumerated: %s"),
						*AssetPath, *HierarchyError)
					: FString::Printf(TEXT("Control Rig '%s': %d bone(s), %d control(s)."),
						*AssetPath, BoneCount, ControlCount),
				Result);
		});
}

} // namespace MCPControlRigTools
