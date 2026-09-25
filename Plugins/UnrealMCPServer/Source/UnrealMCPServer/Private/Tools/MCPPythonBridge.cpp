// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPPythonBridge.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"
#include "MCPSettings.h"
#include "Common/MCPPythonCall.h"
#include "Misc/Base64.h"

#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "IPythonScriptPlugin.h"

namespace MCPPythonBridge
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// execute_python - Run Python code in UE's Python environment
	// ================================================================
	MCP_TOOL(Registry, "execute_python")
        .Destructive() // General code execution can exceed Scene effects.
		.Description(TEXT("Execute Python code in Unreal Engine's embedded Python environment. The 'unreal' module is available for accessing the engine API. Output is captured from the log. This is a powerful escape hatch for operations not covered by other tools. v5: runs as a script file (no literal splicing), returns ok, bounded output/errors lines and output_truncated; print MCP_RESULT:<json> to return a value; optional data object arrives as MCP_DATA."))
		.ObjectArg(TEXT("data"), TEXT("Optional JSON object delivered to the script as MCP_DATA without any quoting"), StringToJson(TEXT(R"({"type":"object"})")))
		.StringArg(TEXT("code"), TEXT("Python code to execute. Has access to the full UE Python API (unreal module)."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			const UMCPSettings* Settings = UMCPSettings::Get();
			if (!Settings->bEnablePythonBridge)
			{
				return FMCPToolResult::Error(TEXT("Python bridge is disabled in settings. Enable it in Project Settings > Plugins > Unreal MCP Server."));
			}

			FString Code;
			if (!Args->TryGetStringField(TEXT("code"), Code))
				return FMCPToolResult::Error(TEXT("code is required"));

			// Check if Python plugin is available
			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
			{
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is not loaded. Enable it in the plugin manager."));
			}

			// v5 increment 18: the script runs as a file through ExecPythonCommandEx, so user code is
			// never spliced into a quoted literal; optional data reaches the script as MCP_DATA via
			// base64 JSON (no quoting hazards); output and errors come back bounded and structured.
			FString Prelude;
			if (Args->HasTypedField<EJson::Object>(TEXT("data")))
			{
				const FString DataJson = JsonToString(Args->GetObjectField(TEXT("data")));
				FTCHARToUTF8 Utf8(*DataJson);
				Prelude = FString::Printf(TEXT("import json as _mcp_json, base64 as _mcp_b64\nMCP_DATA = _mcp_json.loads(_mcp_b64.b64decode('%s').decode('utf-8'))\n"), *FBase64::Encode((const uint8*)Utf8.Get(), Utf8.Length()));
			}
			const MCPCommon::FPythonRun Run = MCPCommon::RunPythonCode(Prelude + Code, TEXT("MCP_RESULT"));
			if (!Run.bAvailable)
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("Python is not available in this editor"), TEXT("Enable PythonScriptPlugin."));
			auto Out = MakeShared<FJsonObject>();
			Out->SetBoolField(TEXT("ok"), Run.bOk);
			TArray<TSharedPtr<FJsonValue>> O, E;
			for (const FString& L : Run.Output) O.Add(MakeShared<FJsonValueString>(L));
			for (const FString& L : Run.Errors) E.Add(MakeShared<FJsonValueString>(L));
			Out->SetArrayField(TEXT("output"), O); Out->SetArrayField(TEXT("errors"), E);
			Out->SetBoolField(TEXT("output_truncated"), Run.bTruncated);
			if (!Run.Result.IsEmpty()) Out->SetStringField(TEXT("result"), Run.Result);
			FMCPToolResult R = FMCPToolResult::SuccessStructured(Run.bOk
				? FString::Printf(TEXT("Python executed: %d output line(s), %d warning/error line(s)%s."), Run.Output.Num(), Run.Errors.Num(), Run.bTruncated ? TEXT(", truncated") : TEXT(""))
				: FString::Printf(TEXT("Python failed: %s"), Run.Errors.Num() ? *Run.Errors.Last().Left(400) : TEXT("see errors")), Out);
			R.bIsError = !Run.bOk;
			return R;
		});
}

} // namespace MCPPythonBridge
