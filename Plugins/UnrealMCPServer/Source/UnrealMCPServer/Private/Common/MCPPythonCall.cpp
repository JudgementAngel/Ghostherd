// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Common/MCPPythonCall.h"

#include "Misc/Paths.h"
#include "Misc/Base64.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "IPythonScriptPlugin.h"
#include "MCPProtocol.h"

namespace MCPCommon
{

FString PyEscape(const FString& S)
{
	FString Out = S;
	Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Out.ReplaceInline(TEXT("'"), TEXT("\\'"));
	Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	Out.ReplaceInline(TEXT("\t"), TEXT("\\t"));
	return Out;
}

FString FindPluginPythonScript(const FString& ScriptFileName)
{
	// v5: locate content by plugin identity first (survives folder renames and Marketplace layouts).
	if (const TSharedPtr<IPlugin> Self = IPluginManager::Get().FindPlugin(TEXT("UnrealMCPServer")))
	{
		const FString ByIdentity = Self->GetContentDir() / TEXT("Python") / ScriptFileName;
		if (FPaths::FileExists(ByIdentity)) return ByIdentity;
	}
	const FString Candidates[] = {
		FPaths::ProjectPluginsDir() / TEXT("UnrealMCPServer/Content/Python") / ScriptFileName,
		FPaths::EnginePluginsDir() / TEXT("Marketplace/UnrealMCPServer/Content/Python") / ScriptFileName,
		FPaths::ProjectPluginsDir() / TEXT("Marketplace/UnrealMCPServer/Content/Python") / ScriptFileName,
	};
	for (const FString& Path : Candidates)
	{
		if (FPaths::FileExists(Path))
		{
			return Path;
		}
	}
	// Default path — Python reports the missing file cleanly.
	return Candidates[0];
}

FString BuildPythonScriptCall(const FString& ScriptPath, const TArray<FString>& FunctionNames,
	const FString& FunctionCall, const FString& ResultMarker)
{
	FString EscapedPath = ScriptPath;
	EscapedPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	FString Bindings;
	for (const FString& Fn : FunctionNames)
	{
		Bindings += FString::Printf(TEXT("    _fn_%s = _mod.get('%s')\n"), *Fn, *Fn);
	}

	return FString::Printf(TEXT(
		"import runpy, os, json, unreal\n"
		"_script = '%s'\n"
		"if not os.path.exists(_script):\n"
		"    unreal.log_error(f'script not found at: {_script}')\n"
		"else:\n"
		"    _mod = runpy.run_path(_script)\n"
		"%s"
		"    _result = %s\n"
		"    unreal.log(f'%s:{json.dumps(_result)}')\n"
	), *EscapedPath, *Bindings, *FunctionCall, *ResultMarker);
}


static FString B64(const FString& Text)
{
	FTCHARToUTF8 Utf8(*Text);
	return FBase64::Encode((const uint8*)Utf8.Get(), Utf8.Length());
}

FString BuildPythonScriptCallJson(const FString& ScriptPath, const FString& FunctionName,
	const TSharedPtr<FJsonObject>& Kwargs, const FString& ResultMarker)
{
	FString Fn = FunctionName;
	for (TCHAR C : Fn) if (!(FChar::IsAlnum(C) || C == TEXT('_'))) { Fn = TEXT("__invalid_function_name__"); break; }
	FString Marker = ResultMarker;
	for (TCHAR C : Marker) if (!(FChar::IsAlnum(C) || C == TEXT('_'))) { Marker = TEXT("MCP_RESULT"); break; }
	FString Path = ScriptPath; Path.ReplaceInline(TEXT("\\"), TEXT("/"));
	const FString KwargsJson = Kwargs.IsValid() ? JsonToString(Kwargs) : TEXT("{}");
	return FString::Printf(TEXT(
		"import runpy, os, json, base64, unreal\n"
		"_script = base64.b64decode('%s').decode('utf-8')\n"
		"_kwargs = json.loads(base64.b64decode('%s').decode('utf-8'))\n"
		"if not os.path.exists(_script):\n"
		"    unreal.log_error('script not found at: ' + _script)\n"
		"else:\n"
		"    _mod = runpy.run_path(_script)\n"
		"    _fn = _mod.get('%s')\n"
		"    if _fn is None:\n"
		"        unreal.log_error('function not found: %s')\n"
		"    else:\n"
		"        _result = _fn(**_kwargs)\n"
		"        unreal.log('%s:' + json.dumps(_result))\n"
	), *B64(Path), *B64(KwargsJson), *Fn, *Fn, *Marker);
}

FPythonRun RunPythonCode(const FString& Code, const FString& ResultMarker, int32 MaxLines, int32 MaxBytes)
{
	FPythonRun Run;
	IPythonScriptPlugin* Python = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
	if (!Python || !Python->IsPythonAvailable()) return Run;
	Run.bAvailable = true;
	FPythonCommandEx Cmd;
	Cmd.Command = Code;
	Cmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	Cmd.FileExecutionScope = EPythonFileExecutionScope::Private;
	Run.bOk = Python->ExecPythonCommandEx(Cmd);
	int32 Bytes = 0;
	for (const FPythonLogOutputEntry& Entry : Cmd.LogOutput)
	{
		if (!ResultMarker.IsEmpty() && Entry.Output.StartsWith(ResultMarker + TEXT(":"))) Run.Result = Entry.Output.Mid(ResultMarker.Len() + 1);
		if (Run.Output.Num() + Run.Errors.Num() >= MaxLines || Bytes >= MaxBytes) { Run.bTruncated = true; continue; }
		Bytes += Entry.Output.Len();
		if (Entry.Type == EPythonLogOutputType::Info) Run.Output.Add(Entry.Output); else Run.Errors.Add(Entry.Output);
	}
	if (!Cmd.CommandResult.IsEmpty() && Run.Result.IsEmpty() && ResultMarker.IsEmpty()) Run.Result = Cmd.CommandResult;
	if (!Run.bOk && !Cmd.CommandResult.IsEmpty())
	{
		// On failure the interpreter reports the traceback through CommandResult; surface it as error lines.
		TArray<FString> Lines; Cmd.CommandResult.ParseIntoArrayLines(Lines);
		for (const FString& L : Lines) { if (!Run.Errors.Contains(L)) Run.Errors.Add(L); }
	}
	return Run;
}

}
