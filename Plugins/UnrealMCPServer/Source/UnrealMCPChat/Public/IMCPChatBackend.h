// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPChatTypes.h"

/**
 * The seam between the UI and everything that can produce a conversation.
 *
 * Two families implement it (docs/02_ARCHITECTURE.md §3):
 *   Agent  — a local CLI agent driven over ACP/stdio; it owns its own tool loop
 *            and reaches our editor tools over the MCP HTTP endpoint.
 *   Model  — a provider API called directly with the user's key; WE own the tool
 *            loop and call FMCPToolRegistry in-process.
 *
 * Pulled forward from Phase 4 into Phase 2 so FMockChatBackend can drive the
 * transcript before any transport exists.
 */

enum class EChatBackendKind : uint8
{
	Agent,
	Model,
	Mock,
};

enum class EChatPermissionResult : uint8
{
	Deny,
	AllowOnce,
	AllowForSession,
	AllowAlways,
};

/** Everything the transcript can render, as one union so there is one code path. */
struct UNREALMCPCHAT_API FChatStreamEvent
{
	enum class EType : uint8
	{
		TurnStarted,
		TextDelta,
		ThinkingDelta,
		ToolCallStarted,
		ToolArgsDelta,
		ToolCallResult,
		PermissionRequest,
		UsageUpdate,
		Error,
		Refusal,
		TurnFinished,
		/** Phase 7 — ACP agents publish a running checklist and can switch operating
		 *  mode mid-turn (plan → build). Neither has an equivalent on the model
		 *  backends, which is why they arrive as their own event types rather than
		 *  being squeezed into TextDelta. */
		PlanUpdate,
		ModeChanged,
	};

	EType   Type = EType::TextDelta;
	FGuid   MessageId;       // the assistant message this event belongs to
	FGuid   ToolCallId;
	FString Text;            // delta payload / error text / tool name
	FString ToolName;
	/** Provider-side call id, echoed back on the tool_result. */
	FString ProviderCallId;
	/** PermissionRequest: why we are asking, in the user's words. */
	FText   ApprovalReason;

	TSharedPtr<FJsonObject> ToolArgs;
	TSharedPtr<FJsonObject> ToolResult;
	FString                 ToolResultText;
	bool                    bToolIsError = false;
	double                  DurationSeconds = 0.0;

	FChatUsage Usage;

	/** Set on Refusal — the provider's category, when it supplies one. */
	FString RefusalCategory;
};

/** What the UI needs to know about a model to render the picker. Loaded from
 *  Config/DefaultChatModels.json in Phase 4, not hardcoded. */
struct UNREALMCPCHAT_API FChatModelInfo
{
	FString ModelId;
	FString DisplayName;
	FString ProviderId;
	int32   ContextTokens = 0;
	int32   MaxOutputTokens = 0;
	double  PriceInPerMillion = 0.0;
	double  PriceOutPerMillion = 0.0;
	bool    bVision = false;
	bool    bTools = true;
	/** False for models that reject temperature/top_p/top_k — the AI settings panel
	 *  hides the sliders rather than sending a parameter that 400s. */
	bool    bSampling = false;
	/** Smallest prefix worth a cache_control marker. Below it the provider ignores
	 *  the marker, so it is checked before one is emitted rather than assumed —
	 *  paying the cache-write premium on a prefix too small to be cached is a pure
	 *  loss. */
	int32   CacheMinTokens = 1024;
	TArray<FString> EffortLevels;
};

struct UNREALMCPCHAT_API FChatTurnRequest
{
	FGuid                     SessionId;
	TArray<FChatMessagePtr>   History;      // active path; agents ignore it
	FChatMessagePtr           UserMessage;
	FString                   ModelId;
	FChatModelParams          Params;
	TSharedPtr<TAtomic<bool>> CancelFlag;
};

class UNREALMCPCHAT_API IMCPChatBackend
{
public:
	virtual ~IMCPChatBackend() = default;

	virtual EChatBackendKind GetKind() const = 0;

	/** Stable id stored on sessions and messages. */
	virtual FString GetId() const = 0;
	virtual FText   GetDisplayName() const = 0;

	/** Binary on PATH? key present? Drives the availability dot in the picker. */
	virtual bool IsAvailable(FText& OutReason) const = 0;

	/** Empty for most agents — they choose their own model. */
	virtual TArray<FChatModelInfo> GetModels() const { return {}; }

	virtual void StartSession(const FGuid& SessionId) = 0;
	virtual void SendTurn(const FChatTurnRequest& Request) = 0;
	virtual void CancelTurn(const FGuid& SessionId) = 0;
	virtual void EndSession(const FGuid& SessionId) = 0;

	virtual void RespondToPermission(const FGuid& /*RequestId*/, EChatPermissionResult /*Result*/) {}

	/** Always broadcast on the GAME THREAD. Backends marshal from their own
	 *  worker threads; the UI never has to check. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnStreamEvent, const FChatStreamEvent&);
	FOnStreamEvent OnStreamEvent;
};

using FChatBackendPtr = TSharedPtr<IMCPChatBackend>;
