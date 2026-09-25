// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.5 (UE 5.8) — Lighting tool family.
//
// MegaLights moved out of experimental in 5.8 (production-ready many-light
// rendering); Lumen gained quality tiers (incl. the new "Lumen Lite" medium
// mode based on Irradiance Fields). Both are controlled at runtime through
// rendering console variables, which is what these tools drive — robustly via
// IConsoleManager::FindConsoleVariable (so a cvar that doesn't exist in a given
// config is reported, not crashed through).

#include "Tools/MCPLightingTools.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "HAL/IConsoleManager.h"

namespace MCPLightingTools
{

/** Set an int cvar by name. Returns false if the cvar doesn't exist; writes the
 *  prior value to OutOld on success. */
static bool SetIntCVar(const TCHAR* Name, int32 Value, FString& OutOld)
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
	if (!CVar) { return false; }
	OutOld = FString::FromInt(CVar->GetInt());
	CVar->Set(Value, ECVF_SetByConsole);
	return true;
}

/** Read a cvar's current value as a string. Returns false if not found. */
static bool GetCVarString(const TCHAR* Name, FString& Out)
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
	if (!CVar) { return false; }
	Out = CVar->GetString();
	return true;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// lighting_set_megalights
	// ================================================================
	MCP_TOOL(Registry, "lighting_set_megalights")
		.Description(TEXT(
			"Toggle MegaLights (production-ready in UE 5.8): GPU-efficient rendering of many dynamic, "
			"shadow-casting lights. Sets the r.MegaLights.EnableForProject console variable. Note: MegaLights also "
			"requires the project/scene to allow it (r.MegaLights.Allowed) and a compatible shadowing setup."))
		.BoolArg(TEXT("enabled"), TEXT("True to enable MegaLights, false to disable."), true)
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			bool bEnabled = false;
			if (!Args->TryGetBoolField(TEXT("enabled"), bEnabled))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("'enabled' (bool) is required."));
			}

			FString OldVal;
			if (!SetIntCVar(TEXT("r.MegaLights.EnableForProject"), bEnabled ? 1 : 0, OldVal))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					TEXT("r.MegaLights.EnableForProject not found in this build/config."),
					TEXT("Ensure the rendering features are available; MegaLights requires SM6 / a compatible platform."));
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("cvar"), TEXT("r.MegaLights.EnableForProject"));
			R->SetStringField(TEXT("previous"), OldVal);
			R->SetNumberField(TEXT("value"), bEnabled ? 1 : 0);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("MegaLights %s (r.MegaLights.EnableForProject %s -> %d)."),
					bEnabled ? TEXT("enabled") : TEXT("disabled"), *OldVal, bEnabled ? 1 : 0), R);
		});

	// ================================================================
	// lighting_set_lumen
	// ================================================================
	MCP_TOOL(Registry, "lighting_set_lumen")
		.Description(TEXT(
			"Enable/disable Lumen dynamic global illumination (diffuse indirect) at runtime via "
			"r.Lumen.DiffuseIndirect.Allow, and optionally tune the screen-probe gather quality. "
			"Use for quick lighting iteration; the project's Dynamic GI Method is the persistent setting."))
		.BoolArg(TEXT("diffuse_indirect"), TEXT("Allow Lumen diffuse GI (true) or disable (false)."), true)
		.IntArg(TEXT("screen_probe_gather"), TEXT("Optional: r.Lumen.ScreenProbeGather (0/1) quality toggle."))
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			bool bDiffuse = false;
			if (!Args->TryGetBoolField(TEXT("diffuse_indirect"), bDiffuse))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("'diffuse_indirect' (bool) is required."));
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			FString OldVal;
			if (!SetIntCVar(TEXT("r.Lumen.DiffuseIndirect.Allow"), bDiffuse ? 1 : 0, OldVal))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					TEXT("r.Lumen.DiffuseIndirect.Allow not found."));
			}
			R->SetStringField(TEXT("diffuse_indirect_previous"), OldVal);
			R->SetNumberField(TEXT("diffuse_indirect"), bDiffuse ? 1 : 0);

			if (Args->HasField(TEXT("screen_probe_gather")))
			{
				const int32 SPG = (int32)Args->GetNumberField(TEXT("screen_probe_gather"));
				FString OldSPG;
				if (SetIntCVar(TEXT("r.Lumen.ScreenProbeGather"), SPG != 0 ? 1 : 0, OldSPG))
				{
					R->SetStringField(TEXT("screen_probe_gather_previous"), OldSPG);
					R->SetNumberField(TEXT("screen_probe_gather"), SPG != 0 ? 1 : 0);
				}
			}

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Lumen diffuse GI %s."), bDiffuse ? TEXT("enabled") : TEXT("disabled")), R);
		});

	// ================================================================
	// lighting_get_settings
	// ================================================================
	MCP_TOOL(Registry, "lighting_get_settings")
		.Description(TEXT(
			"Read the current values of the key global-illumination / lighting console variables "
			"(MegaLights, Lumen, shadows) so an agent can verify the active lighting configuration."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			static const TCHAR* Names[] = {
				TEXT("r.MegaLights.EnableForProject"),
				TEXT("r.MegaLights.Allowed"),
				TEXT("r.Lumen.Supported"),
				TEXT("r.Lumen.DiffuseIndirect.Allow"),
				TEXT("r.Lumen.ScreenProbeGather"),
				TEXT("r.DynamicGlobalIlluminationMethod"),
				TEXT("r.ReflectionMethod"),
				TEXT("r.Shadow.Virtual.Enable"),
			};

			TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
			for (const TCHAR* Name : Names)
			{
				FString Val;
				Values->SetStringField(Name, GetCVarString(Name, Val) ? Val : TEXT("<not found>"));
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetObjectField(TEXT("cvars"), Values);
			return FMCPToolResult::SuccessStructured(TEXT("Current lighting cvar values"), R);
		});
}

} // namespace MCPLightingTools
