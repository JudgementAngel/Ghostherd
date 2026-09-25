// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.5 (UE 5.8) — Substrate material system status.
//
// Substrate replaces the fixed shading-model/blend-mode set with a modular
// framework. It's enabled per-project (r.Substrate, read-only at runtime, needs a
// restart) and 5.8 adds AxF measured-material import and an experimental Toon
// shader on top of it. Authoring happens through Substrate material-expression
// nodes in the material graph (UMaterialExpressionSubstrate*). These tools report
// the active Substrate configuration so an agent knows which material path to use.

#include "Tools/MCPSubstrateTools.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "HAL/IConsoleManager.h"

namespace MCPSubstrateTools
{

static FString CVarStr(const TCHAR* Name)
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
	return CVar ? CVar->GetString() : FString(TEXT("<not found>"));
}

static bool CVarEnabled(const TCHAR* Name)
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
	return CVar && CVar->GetInt() != 0;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// substrate_get_status
	// ================================================================
	MCP_TOOL(Registry, "substrate_get_status")
		.Description(TEXT(
			"Report whether the Substrate material system is enabled for the project and the key Substrate "
			"rendering settings (bytes/closures per pixel, layer support, experimental). Substrate is a "
			"project setting requiring an editor restart to change. When enabled, author materials with "
			"Substrate expression nodes (Substrate Slab/BSDF), not the legacy shading models."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			const bool bEnabled = CVarEnabled(TEXT("r.Substrate"));

			static const TCHAR* Names[] = {
				TEXT("r.Substrate"),
				TEXT("r.Substrate.BytesPerPixel"),
				TEXT("r.Substrate.ClosuresPerPixel"),
				TEXT("r.Substrate.EnableLayerSupport"),
				TEXT("r.Substrate.AllocationMode"),
			};
			TSharedPtr<FJsonObject> CVars = MakeShared<FJsonObject>();
			for (const TCHAR* N : Names) { CVars->SetStringField(N, CVarStr(N)); }

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("enabled"), bEnabled);
			R->SetObjectField(TEXT("cvars"), CVars);
			R->SetStringField(TEXT("authoring_hint"), bEnabled
				? TEXT("Substrate is ON — use Substrate material expression nodes (Slab/BSDF). 5.8 adds AxF import + experimental Toon shader.")
				: TEXT("Substrate is OFF — legacy shading models apply. Enable via Project Settings > Rendering > Substrate (requires restart)."));
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Substrate is %s."), bEnabled ? TEXT("enabled") : TEXT("disabled")), R);
		});
}

} // namespace MCPSubstrateTools
