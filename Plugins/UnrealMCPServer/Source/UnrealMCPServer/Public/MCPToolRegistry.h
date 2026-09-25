// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"
#include "MCPRequestContext.h"

class UNREALMCPSERVER_API FMCPToolRegistry
{
public:
	static FMCPToolRegistry& Get();

	void RegisterTool(const FMCPToolDefinition& Tool);
	void UnregisterTool(const FString& Name);
	void UnregisterAllTools();

	const FMCPToolDefinition* FindTool(const FString& Name) const;
	TArray<FMCPToolDefinition> GetAllTools() const;
	int32 GetToolCount() const;

	// ---- v4 Phase 1: categories & catalog mode ----

	/** Category stamped on tools registered from now on (set by the module
	 *  before each tool family's RegisterAll). */
	void  SetActiveCategory(FName Category) { ActiveCategory = Category; }
	FName GetActiveCategory() const         { return ActiveCategory; }

	/** category -> sorted tool names. */
	TMap<FName, TArray<FString>> GetToolsByCategory() const;

	/** Cached tools/list payload (slim or full). Rebuilt lazily after any
	 *  register/unregister; avoids re-serializing ~360 schemas per request. */
	TArray<TSharedPtr<FJsonValue>> GetToolsListJson(bool bFullSchemas) const;

	// ---- Phase 0 / R3: shared catalog-mode exposure ----

	/** The names always exposed in catalog mode: the discovery meta-tools plus the
	 *  high-frequency core an agent needs for the first steps of most tasks.
	 *  Single source of truth for BOTH the HTTP tools/list handler and any
	 *  in-process client (the chat panel's model backends), so the two can never
	 *  drift into showing agents different catalogs. */
	static const TSet<FString>& GetCatalogCoreToolNames();

	/** tools/list payload filtered by exposure mode.
	 *  bCatalogMode=true  → meta-tools + core (~31 tools, ~3K tokens)
	 *  bCatalogMode=false → every registered tool (~62K tokens at Full preset)
	 *  Kept as a bool rather than EMCPToolExposureMode so this header stays free
	 *  of the UObject settings header. */
	TArray<TSharedPtr<FJsonValue>> GetToolsForExposure(bool bCatalogMode, bool bFullSchemas) const;

	/** Definitions (not JSON) filtered the same way — what an in-process client
	 *  needs in order to convert schemas to a provider's tool format. */
	TArray<FMCPToolDefinition> GetToolDefinitionsForExposure(bool bCatalogMode) const;

	// ---- Phase 0 / R4: execution observability ----

	/**
	 * Fired on the GAME THREAD immediately after any tool finishes, regardless of
	 * which front-end invoked it. Lets the chat panel surface activity triggered by
	 * *external* agents (a Claude Code terminal driving the editor over HTTP), not
	 * only its own calls.
	 *
	 * Handlers must be cheap and must not call back into ExecuteTool.
	 */
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnToolExecuted,
		const FString& /*ToolName*/, const FMCPRequestContext& /*Context*/, const FMCPToolResult& /*Result*/);
	FOnToolExecuted OnToolExecuted;

	/** Existing entry point (Phase A). Equivalent to ExecuteTool(Name, Args, FMCPRequestContext{}). */
	FMCPToolResult ExecuteTool(const FString& Name, const TSharedPtr<FJsonObject>& Arguments);

	/** Phase B: context-aware execution. Enforces the scope gates before invoking
	 *  the handler: Destructive tools need Destructive scope, and any tool not
	 *  annotated .ReadOnly() needs at least Scene, which is what makes a Read
	 *  scoped session actually read-only. */
	FMCPToolResult ExecuteTool(const FString& Name, const TSharedPtr<FJsonObject>& Arguments,
		const FMCPRequestContext& Context);

private:
	FMCPToolRegistry() = default;

	TMap<FString, FMCPToolDefinition> Tools;
	mutable FCriticalSection ToolsLock;

	FName ActiveCategory;

	// tools/list serialization cache (v4 Phase 1)
	mutable TArray<TSharedPtr<FJsonValue>> CachedListSlim;
	mutable TArray<TSharedPtr<FJsonValue>> CachedListFull;
	mutable bool bListCacheDirty = true;
};
