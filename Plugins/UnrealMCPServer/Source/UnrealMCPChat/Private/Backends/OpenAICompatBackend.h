// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMCPChatBackend.h"
#include "MCPChatModelCatalog.h"
#include "Backends/MCPHttpStreamSink.h"
#include "Backends/MCPSseParser.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"

/**
 * Phase 8 — the OpenAI `/chat/completions` wire format.
 *
 * ONE class, MANY providers. OpenAI, OpenRouter, Groq, DeepSeek, Together,
 * Fireworks, Ollama, LM Studio and vLLM all speak this shape, so a provider is a
 * row in Config/DefaultChatModels.json — a base URL, an auth header, and a set of
 * models — not a class. That is what makes "+ Add provider" a text field rather
 * than a plugin update.
 *
 * One instance is registered per provider; ProviderId selects the base URL, the
 * key, and which models the picker groups under it.
 *
 * ── Where this differs from Anthropic, and why the differences bite ─────────
 *  1. Tool arguments stream as `function.arguments` FRAGMENTS keyed by an ARRAY
 *     INDEX, not by an id. The id often arrives only on the first fragment, so
 *     accumulation must be index-keyed or the arguments of two parallel calls
 *     interleave into one unparseable string.
 *  2. `usage` is omitted entirely unless `stream_options.include_usage` is set —
 *     the cost readout silently reads zero without it. Some providers reject the
 *     field, so it is sent only when the provider opts in.
 *  3. Reasoning models put their thinking in `reasoning_content` (DeepSeek) or
 *     `reasoning` (OpenRouter). Neither is in the OpenAI spec; both are read.
 *  4. Ollama and LM Studio need NO key. Requiring one would put a wall in front
 *     of the only setup that costs nothing, which is the fastest path to a
 *     working conversation on a fresh install.
 */
class FOpenAICompatBackend : public IMCPChatBackend, public TSharedFromThis<FOpenAICompatBackend>
{
public:
	explicit FOpenAICompatBackend(const FString& InProviderId);
	virtual ~FOpenAICompatBackend() override;

	//~ IMCPChatBackend
	virtual EChatBackendKind GetKind() const override { return EChatBackendKind::Model; }
	virtual FString GetId() const override { return ProviderId; }
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
	/** One tool the model asked for, tracked from arrival to result. */
	struct FPendingToolCall
	{
		FGuid   LocalId;
		FString ProviderId;      // the `id` echoed back on the tool message
		FString ToolName;
		FString ArgsJson;        // accumulated `function.arguments` fragments
		TSharedPtr<FJsonObject> ParsedArgs;

		bool  bDecided = false;
		bool  bAllowed = false;
		FText DenyReason;

		bool    bExecuted = false;
		bool    bIsError  = false;
		FString ResultText;
		double  DurationSeconds = 0.0;
	};

	// ---- Request ----
	TSharedPtr<FJsonObject> BuildRequestBody(const FChatTurnRequest& Request) const;
	TArray<TSharedPtr<FJsonValue>> BuildMessagesArray(const FChatTurnRequest& Request) const;
	void SendRequest(const FChatTurnRequest& Request);

	// ---- Stream ----
	bool TickStream(float DeltaTime);
	void HandleSseEvent(const FMCPSseParser::FEvent& Event);
	void HandleChoiceDelta(const TSharedPtr<FJsonObject>& Delta);
	void HandleHttpComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);

	// ---- Tool loop ----
	void BeginToolPhase();
	void ProcessToolQueue();
	void ExecuteApprovedTools();
	void SendToolResults();
	void OpenTurnTransactionIfNeeded();
	bool AllToolCallsDecided() const;

	void EmitError(const FString& Message);
	/** Localised overload. Everything the user reads should come through here; the
	 *  FString form remains for provider-supplied text, which is not ours to
	 *  translate and must be shown verbatim. */
	void EmitError(const FText& Message);
	void FinishTurn();

	static FString DescribeHttpError(int32 StatusCode, const FString& Body, const FString& ProviderDisplay);

	FString ProviderId;

	FGuid ActiveSessionId;
	FGuid ActiveMessageId;
	TSharedPtr<TAtomic<bool>> CancelFlag;

	FHttpRequestPtr                            HttpRequest;
	TSharedPtr<FMCPHttpStreamSink::FByteQueue> ByteQueue;
	TSharedPtr<FMCPHttpStreamSink>             Sink;
	FMCPSseParser                              Parser;
	FTSTicker::FDelegateHandle                 TickerHandle;

	bool bTurnActive = false;
	bool bSawAnyContent = false;
	bool bStopReasonToolCalls = false;
	/** True only while SendToolResults re-enters SendTurn for the next tool round. */
	bool bContinuingTurn = false;
	bool bAwaitingApproval = false;
	FChatUsage PendingUsage;

	/** Assistant text produced this round, echoed back with the tool results. */
	FString RoundText;

	/**
	 * Array index → pending call. Index-keyed, NOT id-keyed: the id is absent on
	 * every fragment after the first, and keying on it drops the arguments.
	 */
	TMap<int32, FPendingToolCall> ToolCallsByIndex;
	/** Ordered for emission; rebuilt from the map when the stream ends. */
	TArray<FPendingToolCall> PendingToolCalls;

	FChatTurnRequest CurrentRequest;
	int32  ToolIteration = 0;
	double TurnStartSeconds = 0.0;

	/** One undo step per turn, opened lazily on the first mutating tool. */
	TUniquePtr<class FMCPScopedOwnerTransaction> TurnTransaction;

	int32            RetryCount = 0;
	double           RetryAtSeconds = 0.0;
	FChatTurnRequest PendingRetryRequest;
	static constexpr int32  MaxRetries = 3;
	static constexpr double MaxTurnSeconds = 600.0;
};
