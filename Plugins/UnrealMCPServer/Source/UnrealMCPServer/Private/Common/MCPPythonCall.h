// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * v4 Phase 3 — shared helpers for invoking the plugin's bundled Python scripts
 * (fal.ai image/3D generation). Replaces near-identical statics that were
 * duplicated between MCPUIImageTools.cpp and MCP3DModelTools.cpp.
 */
namespace MCPCommon
{
	/** Escape a string for safe embedding in a Python single-quoted literal. */
	UNREALMCPSERVER_API FString PyEscape(const FString& S);

	/** Locate a script shipped under <plugin>/Content/Python, trying project
	 *  plugins, engine marketplace, and project marketplace layouts. Returns the
	 *  first existing path, or the default project-plugins path (Python reports
	 *  the missing file cleanly). */
	UNREALMCPSERVER_API FString FindPluginPythonScript(const FString& ScriptFileName);

	/** Build runpy-based invocation code: loads the script as a module, binds
	 *  each requested function as `_fn_<name>`, evaluates FunctionCall (which
	 *  references those), and logs `<ResultMarker>:{json}` for the caller to
	 *  parse from the log. runpy avoids the nested-exec scope issues that break
	 *  function visibility. */
	/** v5 increment 18: safe transport. Script path and keyword arguments travel as base64 JSON, so
	 *  quotes, backslashes, newlines and Unicode in user data can never alter the generated code. */
	UNREALMCPSERVER_API FString BuildPythonScriptCallJson(const FString& ScriptPath, const FString& FunctionName,
		const TSharedPtr<FJsonObject>& Kwargs, const FString& ResultMarker);

	/** v5 increment 18: bounded, structured execution through IPythonScriptPlugin::ExecPythonCommandEx. */
	struct FPythonRun
	{
		bool bAvailable = false;   // plugin loaded
		bool bOk = false;          // command executed without error
		FString Result;            // text after "<marker>:" on the last matching output line, if a marker was given
		TArray<FString> Output;    // info lines (bounded)
		TArray<FString> Errors;    // warning/error lines (bounded)
		bool bTruncated = false;
	};
	UNREALMCPSERVER_API FPythonRun RunPythonCode(const FString& Code, const FString& ResultMarker = FString(), int32 MaxLines = 200, int32 MaxBytes = 65536);

	FString BuildPythonScriptCall(const FString& ScriptPath, const TArray<FString>& FunctionNames,
		const FString& FunctionCall, const FString& ResultMarker);
}
