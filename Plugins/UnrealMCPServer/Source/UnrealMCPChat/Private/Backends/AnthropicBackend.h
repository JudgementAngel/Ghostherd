// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMCPChatBackend.h"
#include "Backends/MCPHttpStreamSink.h"
#include "Backends/MCPSseParser.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"

/**
 * Phase 4 — Anthropic Messages API, streaming.
 *
 * POST https://api.anthropic.com/v1/messages with "stream": true, consumed as SSE.
 *
 * Model-specific rules are NOT hardcoded here; they come from the catalogue
 * (Config/DefaultChatModels.json) because they differ per model and change over
 * time. The three that actually bite:
 *
 *   1. temperature / top_p / top_k are REJECTED with a 400 on Opus 5, Sonnet 5,
 *      Opus 4.8/4.7 and Fable 5. Sent only when the catalogue says sampling:true.
 *   2. `thinking.display` defaults to "omitted", which streams thinking blocks
 *      with EMPTY text. Since the UI has a thinking section, we must ask for
 *      "summarized" explicitly or it renders blank and looks broken.
 *   3. stop_reason "refusal" arrives as a normal HTTP 200 with empty or partial
 *      content. Code that reads content[0] unconditionally breaks on it.
 *
 * Threading: the HTTP layer writes response bytes from its own thread into
 * FMCPHttpStreamSink; a ticker drains them on the game thread. See gotcha G3.
 */
class FAnthropicBackend : public IMCPChatBackend, public TSharedFromThis<FAnthropicBackend>
{
public:
	FAnthropicBackend();
	virtual ~FAnthropicBackend() override;

	//~ IMCPChatBackend
	virtual EChatBackendKind GetKind() const override { return EChatBackendKind::Model; }
	virtual FString GetId() const override { return TEXT("anthropic"); }
	virtual FText   GetDisplayName() const override;
	virtual bool    IsAvailable(FText& OutReason) const override;
	virtual TArray<FChatModelInfo> GetModels() const override;

	virtual void StartSession(const FGuid& SessionId) override {}
	virtual void SendTurn(const FChatTurnRequest& Request) override;
	virtual void CancelTurn(const FGuid& SessionId) override;
	virtual void EndSession(const FGuid& SessionId) override;
	virtual void RespondToPermission(const FGuid& RequestId, EChatPermissionResult Result) override;
	//~ End IMCPChatBackend

private:
	/**
	 * Phase 5 — one tool the model asked for, tracked from arrival to result.
	 *
	 * The provider id (`toolu_…`) must be echoed back verbatim on the tool_result;
	 * a mismatch, or any tool_use left unanswered, makes the follow-up request fail.
	 */
	struct FPendingToolCall
	{
		FGuid   LocalId;         // what the UI knows it by
		FString ProviderId;      // toolu_… — echoed back
		FString ToolName;
		FString ArgsJson;        // accumulated input_json_delta
		TSharedPtr<FJsonObject> ParsedArgs;

		bool bDecided  = false;  // gate evaluated (or user answered)
		bool bAllowed  = false;
		FText DenyReason;

		bool    bExecuted = false;
		bool    bIsError  = false;
		FString ResultText;
		double  DurationSeconds = 0.0;
	};

	// ---- Tool loop ----
	void BeginToolPhase();
	void ProcessToolQueue();
	void ExecuteApprovedTools();
	void SendToolResults();
	void OpenTurnTransactionIfNeeded();

	/** True when every pending call has a decision. */
	bool AllToolCallsDecided() const;
	/** Build the JSON body. Separated out so it can be unit-tested without a network. */
	TSharedPtr<FJsonObject> BuildRequestBody(const FChatTurnRequest& Request) const;

	/** History → Anthropic `messages`. Drops empty turns and merges consecutive
	 *  same-role turns, both of which the API rejects. */
	TArray<TSharedPtr<FJsonValue>> BuildMessagesArray(const FChatTurnRequest& Request) const;

	/** Phase 6 — prompt caching. Both are no-ops when the prefix is below the
	 *  model's cache minimum, because an ignored marker still costs the write. */
	void ApplyToolCacheMarker(TArray<TSharedPtr<FJsonValue>>& Tools,
		const FChatTurnRequest& Request, const FChatModelInfo* Model) const;
	void ApplyHistoryCacheMarker(TArray<TSharedPtr<FJsonValue>>& Messages,
		const FChatTurnRequest& Request, const FChatModelInfo* Model) const;

	bool TickStream(float DeltaTime);
	void HandleSseEvent(const FMCPSseParser::FEvent& Event);
	void HandleHttpComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);

	void EmitError(const FString& Message);
	/** Localised overload. Everything the user reads should come through here; the
	 *  FString form remains for provider-supplied text, which is not ours to
	 *  translate and must be shown verbatim. */
	void EmitError(const FText& Message);
	void FinishTurn();

	/** Map an HTTP status to a message that tells the user what to actually do. */
	static FString DescribeHttpError(int32 StatusCode, const FString& Body);

	FGuid ActiveSessionId;
	FGuid ActiveMessageId;
	TSharedPtr<TAtomic<bool>> CancelFlag;

	FHttpRequestPtr                              HttpRequest;
	TSharedPtr<FMCPHttpStreamSink::FByteQueue>   ByteQueue;
	TSharedPtr<FMCPHttpStreamSink>               Sink;
	FMCPSseParser                                Parser;
	FTSTicker::FDelegateHandle                   TickerHandle;

	/** content_block index → what kind of block it is, so a delta can be routed
	 *  without re-reading the block-start event. */
	TMap<int32, FString> BlockTypeByIndex;
	TMap<int32, FGuid>   ToolCallIdByIndex;
	TMap<int32, FString> ToolNameByIndex;

	bool bTurnActive = false;
	bool bSawRefusal = false;
	bool bSawAnyContent = false;
	FChatUsage PendingUsage;

	// ---- Phase 5: tool state ----

	/** The request being iterated. History grows as tool rounds complete. */
	FChatTurnRequest CurrentRequest;

	/** Assistant content from THIS round, echoed back so the model sees its own
	 *  tool_use blocks alongside our tool_results. */
	FString                  RoundText;
	TArray<FPendingToolCall> PendingToolCalls;

	/** Set when message_delta reports stop_reason "tool_use". */
	bool bStopReasonToolUse = false;
	/** True only while SendToolResults re-enters SendTurn for the next tool round.
	 *  Without it the in-flight guard rejects the continuation. */
	bool bContinuingTurn = false;
	bool bAwaitingApproval = false;

	int32  ToolIteration = 0;
	double TurnStartSeconds = 0.0;

	/** One undo step per turn. Opened lazily on the first mutating tool so a
	 *  read-only turn doesn't pollute the undo stack with an empty entry. */
	TUniquePtr<class FMCPScopedOwnerTransaction> TurnTransaction;

	/** Wall-clock ceiling for a whole turn, tool rounds included. */
	static constexpr double MaxTurnSeconds = 600.0;

	/** Retry bookkeeping for 429 / 5xx. */
	int32   RetryCount = 0;
	double  RetryAtSeconds = 0.0;
	FChatTurnRequest PendingRetryRequest;
	static constexpr int32 MaxRetries = 3;
};
