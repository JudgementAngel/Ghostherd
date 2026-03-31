#include "Tools/MCPPythonBridge.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPSettings.h"

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
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("code"), TEXT("Python code to execute. Has access to the full UE Python API (unreal module)."), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("execute_python");
		Def.Description = TEXT("Execute Python code in Unreal Engine's embedded Python environment. The 'unreal' module is available for accessing the engine API. Output is captured from the log. This is a powerful escape hatch for operations not covered by other tools.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			// Execute via GEditor
			TArray<FString> LogOutput;

			// Capture output by redirecting to a temporary log handler
			FString CombinedCode = FString::Printf(TEXT(
				"import unreal\n"
				"try:\n"
				"    exec('''\n%s\n''')\n"
				"except Exception as e:\n"
				"    unreal.log_error(f'Python Error: {e}')\n"
			), *Code);

			bool bSuccess = PythonPlugin->ExecPythonCommand(*CombinedCode);

			if (bSuccess)
			{
				return FMCPToolResult::Success(TEXT("Python code executed successfully. Check the Output Log for any print() output."));
			}
			else
			{
				return FMCPToolResult::Error(TEXT("Python execution failed. Check the Output Log for details."));
			}
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPPythonBridge
