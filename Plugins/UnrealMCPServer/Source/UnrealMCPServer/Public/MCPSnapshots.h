#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class FMCPToolRegistry;
class FMCPResourceProvider;

/** v5 increment 14: stable object references (V5-19) and owned structural snapshots with diffs (V5-20). */
namespace MCPSnapshots
{
    UNREALMCPSERVER_API void RegisterAll(FMCPToolRegistry& Registry);
    UNREALMCPSERVER_API void RegisterResources(FMCPResourceProvider& Provider);
    UNREALMCPSERVER_API void ClearSession(const FString& SessionId);
    UNREALMCPSERVER_API void Reset();
    UNREALMCPSERVER_API TSharedPtr<FJsonObject> DiagnosticsJson();
}
