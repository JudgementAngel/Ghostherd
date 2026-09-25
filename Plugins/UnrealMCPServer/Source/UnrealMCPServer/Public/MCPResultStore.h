// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class FMCPToolRegistry;
class FMCPResourceProvider;
struct FMCPRequestContext;

/** v5 increment 24 (V5-11): bounded tool results. A tools/call whose serialized result exceeds the
 *  configured cap is stored for the owner and answered with a short summary; the full text is read
 *  in pages through get_result_page or as unreal://results/{result_id}. Nothing is streamed
 *  unbounded and no producer queue grows without a cap. */
namespace MCPResultStore
{
    UNREALMCPSERVER_API FString Store(const FMCPRequestContext& Context, const FString& ToolName, FString&& FullText);
    UNREALMCPSERVER_API bool Page(const FString& ResultId, const FMCPRequestContext& Context, int64 Offset, int32 MaxBytes, FString& OutText, int64& OutTotal);
    UNREALMCPSERVER_API void RegisterAll(FMCPToolRegistry& Registry);
    UNREALMCPSERVER_API void RegisterResources(FMCPResourceProvider& Provider);
    UNREALMCPSERVER_API void ClearSession(const FString& SessionId);
    UNREALMCPSERVER_API void Reset();
    UNREALMCPSERVER_API TSharedPtr<FJsonObject> DiagnosticsJson();
}
