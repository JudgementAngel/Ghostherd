#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class FMCPToolRegistry;
class FMCPResourceProvider;

/** Initial bounded, editor-only transform plans for native movable StaticMeshActors. */
namespace MCPActorChangePlans
{
    UNREALMCPSERVER_API void RegisterAll(FMCPToolRegistry& Registry);
    UNREALMCPSERVER_API void RegisterResources(FMCPResourceProvider& Provider);
    UNREALMCPSERVER_API void ClearSession(const FString& SessionId);
    UNREALMCPSERVER_API void Reset();
    UNREALMCPSERVER_API TSharedPtr<FJsonObject> DiagnosticsJson();
}
