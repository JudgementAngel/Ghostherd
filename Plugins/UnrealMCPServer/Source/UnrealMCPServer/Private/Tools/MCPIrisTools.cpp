// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.5 (UE 5.8) — Iris replication status.
//
// Iris (the next-gen replication system) is production-ready in 5.8 with Scene
// Graph integration and RemoteObjects RPC routing. Whether a given net driver
// uses Iris is resolved at connection time from project settings / command line,
// so the meaningful editor-time surface is reporting the configuration. We read it
// from the net.Iris.* console variables (no hard IrisCore module dependency) plus
// whether the IrisCore module is loaded.

#include "Tools/MCPIrisTools.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

namespace MCPIrisTools
{

static FString CVarStr(const TCHAR* Name)
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
	return CVar ? CVar->GetString() : FString(TEXT("<not found>"));
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// iris_get_status
	// ================================================================
	MCP_TOOL(Registry, "iris_get_status")
		.Description(TEXT(
			"Report the Iris replication system configuration: whether the IrisCore module is loaded and the "
			"values of key net.Iris.* console variables. Note: whether a net driver actually uses Iris is "
			"decided at connection time from project settings / command line (-UseIrisReplication)."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			const bool bIrisCoreLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("IrisCore"));

			static const TCHAR* Names[] = {
				TEXT("net.Iris.UseIrisReplication"),
				TEXT("net.Iris.DeltaCompressInitialState"),
				TEXT("net.Iris.Attachments.AllowSendPolicyFlags"),
			};
			TSharedPtr<FJsonObject> CVars = MakeShared<FJsonObject>();
			for (const TCHAR* N : Names) { CVars->SetStringField(N, CVarStr(N)); }

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("iris_core_module_loaded"), bIrisCoreLoaded);
			R->SetObjectField(TEXT("cvars"), CVars);
			R->SetStringField(TEXT("hint"),
				TEXT("Enable Iris via Project Settings (Iris is production-ready in 5.8) or -UseIrisReplication. "
				     "Configure replicated properties as usual; Iris supports Scene Graph + RemoteObjects RPC routing."));
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("IrisCore module %s."), bIrisCoreLoaded ? TEXT("loaded") : TEXT("not loaded")), R);
		});
}

} // namespace MCPIrisTools
