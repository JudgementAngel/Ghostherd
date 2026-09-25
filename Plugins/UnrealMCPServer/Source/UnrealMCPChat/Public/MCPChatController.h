// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPChatTypes.h"
#include "IMCPChatBackend.h"

/**
 * Phase 2 — turn orchestration.
 *
 * Owns the active session, routes backend events into it, and tells the UI what
 * changed. The UI never talks to a backend directly, and a backend never touches
 * a session — that separation is what makes swapping Claude Code for Ollama a
 * pointer change.
 *
 * Game thread only.
 */
class UNREALMCPCHAT_API FMCPChatController : public TSharedFromThis<FMCPChatController>
{
public:
	FMCPChatController();
	~FMCPChatController();

	// ---- Backends ----

	void RegisterBackend(const FChatBackendPtr& Backend);
	FChatBackendPtr FindBackend(const FString& BackendId) const;
	TArray<FChatBackendPtr> GetBackends() const;

	// ---- Sessions ----

	FChatSessionPtr GetActiveSession() const { return ActiveSession; }
	void            SetActiveSession(const FChatSessionPtr& Session);
	FChatSessionPtr NewSession();
	void            OpenSession(const FGuid& SessionId);

	// ---- Session mutation (Phase 6 — driven by `/` commands and the model picker) ----

	/**
	 * Point the session at a different backend/model.
	 *
	 * Inserts a divider into the transcript. That divider is not decoration: a
	 * conversation whose second half was answered by a different model reads very
	 * differently, and there is otherwise no way to tell where the switch happened.
	 */
	void SwitchModel(const FString& BackendId, const FString& ModelId);

	/** Drop every message but keep the session, its id, and its settings. */
	void ClearActiveTranscript();

	/**
	 * Replace older messages with a locally-built digest.
	 *
	 * DELIBERATELY NOT a model summarisation. Asking a model to summarise costs a
	 * turn, takes seconds, and can silently drop the one detail that mattered. This
	 * keeps the recent messages verbatim and reduces the rest to a factual index of
	 * what happened — deterministic, instant, and inspectable.
	 *
	 * @return a human-readable account of what was compacted.
	 */
	FText CompactActiveSession(int32 KeepRecentMessages = 6);

	void SetSessionTitle(const FString& Title);
	void SetSessionPinned(bool bPinned);

	/** Pin/unpin an @context so it is re-resolved and re-sent every turn. */
	void AddPinnedContext(const FString& KindAndTarget);
	void RemovePinnedContext(const FString& KindAndTarget);

	// ---- Turns ----

	/** Append the user's message and ask the backend for a reply. */
	void SendMessage(const FString& Text, const TArray<FChatContentBlock>& Attachments = {});

	/**
	 * Run one tool directly, with no model round-trip — the `#tool` fast path.
	 *
	 * Still goes through the same gate as a model-issued call. "The user typed it"
	 * is not a reason to skip approval: they typed a tool name, not necessarily an
	 * understanding of what it deletes.
	 */
	void RunToolDirectly(const FString& ToolName, const TSharedPtr<FJsonObject>& Args);

	/** Cooperative cancel of the in-flight turn. */
	void CancelTurn();

	/** Re-run the last assistant turn as a SIBLING, so the previous answer is kept
	 *  and reachable through the ‹ 2/3 › stepper rather than destroyed. */
	void RetryLastTurn();

	bool IsTurnInFlight() const { return bTurnInFlight; }

	/** Answer an inline approval card. Routed to the backend that asked. */
	void RespondToPermission(const FGuid& ToolCallId, EChatPermissionResult Result);

	/** True while a turn is paused on an approval — the composer shows it. */
	bool IsAwaitingApproval() const { return bAwaitingApproval; }

	/** The message currently being streamed, or null. The transcript renders this
	 *  in its live tail rather than in the virtualised list. */
	FChatMessagePtr GetStreamingMessage() const { return StreamingMessage; }

	// ---- Notifications (all on the game thread) ----

	/** A message was appended, or the active session/branch changed — the list
	 *  source needs rebuilding. */
	DECLARE_MULTICAST_DELEGATE(FOnTranscriptChanged);
	FOnTranscriptChanged OnTranscriptChanged;

	/** The streaming message's content grew. Cheap: only the tail repaints. */
	DECLARE_MULTICAST_DELEGATE(FOnStreamingMessageUpdated);
	FOnStreamingMessageUpdated OnStreamingMessageUpdated;

	/** A turn started or ended. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnTurnStateChanged, bool /*bInFlight*/);
	FOnTurnStateChanged OnTurnStateChanged;

private:
	void HandleStreamEvent(const FChatStreamEvent& Event);
	void BeginStreamingMessage(const FGuid& MessageId);
	void FinishStreamingMessage();

	/** Append to the trailing block of MatchType, or start a new one. Keeps a
	 *  streamed reply as one text block instead of one block per token. */
	FChatContentBlock& GetOrAddTrailingBlock(FChatContentBlock::EType MatchType);

	FChatContentBlock* FindToolBlock(const FGuid& ToolCallId);

	TMap<FString, FChatBackendPtr> Backends;
	TArray<FString>                BackendOrder;

	FChatSessionPtr ActiveSession;
	FChatMessagePtr StreamingMessage;

	bool bTurnInFlight = false;
	bool bAwaitingApproval = false;
	TSharedPtr<TAtomic<bool>> CancelFlag;

	/** Delegate handles so re-registering a backend cannot double-subscribe. */
	TMap<FString, FDelegateHandle> BackendHandles;
};
