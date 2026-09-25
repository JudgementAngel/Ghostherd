// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "MCPProtocol.h"
class FMCPToolRegistry;
struct FMCPRequestContext;

/** v5 increment 25 (V5-06/07/08): general actor change plans with an effect journal and verified recovery.
 *  plan_actor_changes captures before-state for typed operations (set_property, set_transform, rename, create,
 *  delete) without editing; apply_change_plan / get_change_plan / unreal://plans/{plan_id} delegate here for
 *  these plans; revert_change_plan applies the recorded inverse and verifies the restore. */
namespace MCPGeneralPlans
{
    UNREALMCPSERVER_API void RegisterAll(FMCPToolRegistry& Registry);
    /** True when the id names one of these plans (owned or not); the out parameters carry the answer. */
    UNREALMCPSERVER_API bool Describe(const FString& PlanId, const FMCPRequestContext& Context, FMCPToolResult& Out);
    UNREALMCPSERVER_API bool Apply(const FString& PlanId, const FString& ExpectedHash, const FString& IdempotencyKey, const FMCPRequestContext& Context, FMCPToolResult& Out);
    UNREALMCPSERVER_API bool ResourceText(const FString& PlanId, const FMCPRequestContext& Context, FString& OutJson);
    UNREALMCPSERVER_API void ClearSession(const FString& SessionId);
    UNREALMCPSERVER_API void Reset();
    UNREALMCPSERVER_API TSharedPtr<FJsonObject> DiagnosticsJson();
}
