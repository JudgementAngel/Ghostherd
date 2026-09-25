// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"
#include "MCPToolRegistry.h"

/**
 * Fluent builder for tool definitions. Replaces hand-rolled FMCPToolDefinition setup.
 *
 * Usage:
 *   MCP_TOOL(Registry, "list_actors")
 *       .Description(TEXT("List actors in the current level."))
 *       .ReadOnly()
 *       .Idempotent()
 *       .StringArg(TEXT("class_filter"), TEXT("Filter by class."))
 *       .IntArg(TEXT("limit"), TEXT("Max actors (default 100)."))
 *       .Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult { ... });
 */
class UNREALMCPSERVER_API FMCPToolBuilder
{
public:
	FMCPToolBuilder(FMCPToolRegistry& InRegistry, const FString& InName);

	FMCPToolBuilder& Description(const FString& InDescription);

	// Annotation hints
	FMCPToolBuilder& ReadOnly();
	FMCPToolBuilder& Destructive();
	FMCPToolBuilder& Idempotent();
	FMCPToolBuilder& ClosedWorld();

	// Capability hints (Phase C / C6 + C3)
	/** Refuse to run while PIE is active. */
	FMCPToolBuilder& RequiresPieOff();
	/** Legacy hint; a dedicated Preview handler is also required to enable dry_run. */
	FMCPToolBuilder& SupportsDryRun();
	/** Bind a non-mutating preflight before final registration via Handle/HandleCtx. */
	FMCPToolBuilder& Preview(TFunction<FMCPToolResult(const TSharedPtr<FJsonObject>&, const FMCPRequestContext&)> InPreview);

	/** v4: this tool may run longer than an HTTP request should block. Over
	 *  HTTP the registry runs it without blocking the network thread, waits a
	 *  short grace period, and on overrun returns a pollable task handle
	 *  (get_task_status / cancel_task / list_tasks). MCP 2025-11-25 Tasks model. */
	FMCPToolBuilder& LongRunning();

	// Argument schema
	FMCPToolBuilder& StringArg(const FString& Name, const FString& Desc, bool bRequired = false);
	FMCPToolBuilder& NumberArg(const FString& Name, const FString& Desc, bool bRequired = false);
	FMCPToolBuilder& IntArg(const FString& Name, const FString& Desc, bool bRequired = false);
	FMCPToolBuilder& BoolArg(const FString& Name, const FString& Desc, bool bRequired = false);
	FMCPToolBuilder& EnumArg(const FString& Name, const FString& Desc, const TArray<FString>& Values, bool bRequired = false);
	FMCPToolBuilder& StringArrayArg(const FString& Name, const FString& Desc, bool bRequired = false);
	FMCPToolBuilder& ObjectArg(const FString& Name, const FString& Desc, TSharedPtr<FJsonObject> SubSchema, bool bRequired = false);

	/** v4 Phase 1: attach a concrete invocation example (JSON object string of
	 *  arguments). Stored under the input schema's "examples" keyword. */
	FMCPToolBuilder& Example(const FString& ExampleJson);

	/** v4: declare the tool's structured-output schema (JSON Schema object as a
	 *  string). Serialized as the MCP `outputSchema` field; pair it with
	 *  FMCPToolResult::SuccessStructured so clients can validate/unmarshal. */
	FMCPToolBuilder& OutputSchema(const FString& SchemaJson);

	/** Bind handler and register with the registry. Must be called last. */
	void Handle(TFunction<FMCPToolResult(const TSharedPtr<FJsonObject>&)> InHandler);

	/** v4 Phase 1: context-aware variant — handler also receives the caller's
	 *  FMCPRequestContext (scope, session, cancellation, progress sink). */
	void HandleCtx(TFunction<FMCPToolResult(const TSharedPtr<FJsonObject>&, const FMCPRequestContext&)> InHandler);

private:
	FMCPToolRegistry& Registry;
	FMCPToolDefinition Def;
};

#define MCP_TOOL(RegistryRef, ToolName) FMCPToolBuilder(RegistryRef, TEXT(ToolName))
