#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class FMCPToolRegistry;

/** v5 increment 13: server health, capability and world listing tools (V5-16/17/19 initial slices). */
class FMCPResourceProvider;
namespace MCPDiagnostics
{
    UNREALMCPSERVER_API void RegisterAll(FMCPToolRegistry& Registry);
    /** v5 increment 20: bounded tool-call timing telemetry (ring of 256 calls plus per-tool aggregates). */
    UNREALMCPSERVER_API void RecordToolCall(const FString& ToolName, double Milliseconds, bool bOk);
    UNREALMCPSERVER_API TSharedPtr<FJsonObject> ToolTimingJson(int32 RecentLimit = 20, int32 PerToolLimit = 20);
    /** v5 increment 20: owned diagnostic bundles (`unreal://bundles/{bundle_id}`). */
    UNREALMCPSERVER_API void RegisterResources(FMCPResourceProvider& Provider);
    UNREALMCPSERVER_API void ClearSession(const FString& SessionId);
    UNREALMCPSERVER_API void Reset();
}
