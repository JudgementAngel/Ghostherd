// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"
#include "MCPRequestContext.h"

/**
 * Phase 5 — the bridge from a model's tool calls to the editor.
 *
 * This is the point where the chat panel stops being a viewer and starts driving
 * Unreal, so the safety story lives here rather than being spread across backends:
 *
 *   - Tool exposure uses the SAME catalog-mode core the HTTP endpoint uses
 *     (FMCPToolRegistry::GetCatalogCoreToolNames, extracted in Phase 0). Sending
 *     all 450 schemas would cost ~62K tokens on the first message — more than most
 *     whole conversations.
 *   - Execution is IN-PROCESS. A model backend calls FMCPToolRegistry directly;
 *     there is no HTTP round-trip to our own server and no second transaction stack.
 *   - Approval is decided here, once, from one policy — not per backend.
 *
 * Everything runs on the game thread: FMCPToolRegistry::ExecuteTool touches GEditor
 * and UObjects.
 */

/** What the user must confirm before a tool runs. */
UENUM()
enum class EMCPChatApprovalMode : uint8
{
	/** Destructive tools ask; everything else runs. The default. */
	AskDestructive UMETA(DisplayName = "Ask for destructive tools"),
	/** Anything that mutates asks. */
	AskWrites      UMETA(DisplayName = "Ask for every change"),
	/** Nothing asks. */
	AllowAll       UMETA(DisplayName = "Never ask (use with care)"),
	/** Only read-only tools run at all; writes are refused outright. */
	ReadOnly       UMETA(DisplayName = "Read-only (block all changes)"),
};

/** Outcome of the approval gate for one call. */
enum class EMCPChatGateResult : uint8
{
	Allow,          // run it now
	NeedsApproval,  // ask the user
	Blocked,        // refuse without asking (policy or scope forbids it)
};

class UNREALMCPCHAT_API FMCPChatToolBridge
{
public:
	static FMCPChatToolBridge& Get();

	// ---- Schema conversion ----

	/** Registry → Anthropic `tools[]`: {name, description, input_schema}. */
	TArray<TSharedPtr<FJsonValue>> BuildAnthropicToolSchemas(bool bCatalogMode) const;

	/** Registry → OpenAI `tools[]`: {type:"function", function:{name, description, parameters}}. */
	TArray<TSharedPtr<FJsonValue>> BuildOpenAIToolSchemas(bool bCatalogMode) const;

	/** Rough token cost of the exposed tool set, so the settings UI can show the
	 *  price of Catalog vs Full instead of asking the user to guess. */
	int32 EstimateSchemaTokens(bool bCatalogMode) const;

	// ---- Approval ----

	EMCPChatGateResult EvaluateGate(const FString& ToolName, const FGuid& SessionId,
	                                FText& OutReason) const;

	/**
	 * Phase 6 — a per-session ceiling, set by `/scope`.
	 *
	 * A ceiling can only make a session STRICTER than the global setting, never
	 * looser. `/scope read` in one conversation must not be undone by the global
	 * mode, and `/scope destructive` must not quietly widen a project whose owner
	 * chose read-only globally — a chat command is not the place to escalate
	 * permissions.
	 */
	void SetSessionCeiling(const FGuid& SessionId, EMCPChatApprovalMode Ceiling);
	void ClearSessionCeiling(const FGuid& SessionId);
	bool GetSessionCeiling(const FGuid& SessionId, EMCPChatApprovalMode& OutCeiling) const;

	/** The stricter of the global mode and this session's ceiling. Everything that
	 *  decides whether a tool runs goes through here. */
	EMCPChatApprovalMode GetEffectiveMode(const FGuid& SessionId) const;

	/** Lower is stricter. Exposed so the UI can describe a ceiling honestly. */
	static int32 StrictnessRank(EMCPChatApprovalMode Mode);

	/** Remember an allow decision. Session grants die with the session; Always
	 *  grants are written to settings and are visible/removable there. */
	void GrantForSession(const FString& ToolName, const FGuid& SessionId);
	void GrantAlways(const FString& ToolName);
	void RevokeAlways(const FString& ToolName);
	void ClearSessionGrants(const FGuid& SessionId);

	// ---- Execution ----

	struct FExecutionResult
	{
		bool    bIsError = false;
		FString Text;                       // what goes back to the model
		TSharedPtr<FJsonObject> Structured; // structuredContent, when present
		double  DurationSeconds = 0.0;
		/** Set when a .LongRunning() tool was converted to a pollable task. */
		FString TaskId;
	};

	/**
	 * Run a tool. Game thread only.
	 *
	 * Scope is derived from the approval mode, so a ReadOnly session physically
	 * cannot execute a destructive tool even if a gate check were somehow bypassed
	 * — the registry refuses it too.
	 */
	FExecutionResult Execute(const FString& ToolName, const TSharedPtr<FJsonObject>& Args,
	                         const FGuid& SessionId, EMCPChatApprovalMode Mode,
	                         const TSharedPtr<TAtomic<bool>>& CancelFlag,
	                         TFunction<void(float, const FString&)> ProgressSink) const;

	// ---- Introspection for the UI ----

	bool IsToolReadOnly(const FString& ToolName) const;
	bool IsToolDestructive(const FString& ToolName) const;
	FText GetToolDescription(const FString& ToolName) const;

	static EMCPScope ScopeForMode(EMCPChatApprovalMode Mode);

private:
	FMCPChatToolBridge() = default;

	TArray<TSharedPtr<FJsonValue>> BuildSchemas(bool bCatalogMode, bool bOpenAIShape) const;

	/** Tool name → set of sessions that granted it for their lifetime. */
	TMap<FString, TSet<FGuid>> SessionGrants;

	/** Session → `/scope` ceiling. Not persisted: a ceiling is a deliberate act for
	 *  the work in front of you, and silently restoring one three days later would
	 *  look like the panel had broken. */
	TMap<FGuid, EMCPChatApprovalMode> SessionCeilings;
};
