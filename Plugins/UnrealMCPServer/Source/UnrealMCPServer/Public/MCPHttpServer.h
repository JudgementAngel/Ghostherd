// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"
#include "MCPRequestContext.h"
#include "MCPTransactionScope.h"
#include "MCPWorkingSet.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpRouteHandle.h"
#include "Containers/Ticker.h"
#include "Containers/Queue.h"

struct FMCPAuthResult;

class UNREALMCPSERVER_API FMCPHttpServer
{
public:
	static FMCPHttpServer& Get();

	bool Start(int32 Port);
	void Stop();
	bool IsRunning() const { return bIsRunning; }
	int32 GetPort() const { return CurrentPort; }

	/** v4 — live server stats for the status-bar UI. Cheap, thread-safe snapshot. */
	struct FServerStats
	{
		uint64 TotalRequests = 0;
		uint64 TotalToolCalls = 0;
		double UptimeSeconds = 0.0;
		double SecondsSinceLastToolCall = -1.0;  // -1 = never
		FString LastToolName;
		int32 ActiveSessions = 0;
	};
	FServerStats GetStats() const;

	DECLARE_MULTICAST_DELEGATE(FOnServerStarted);
	DECLARE_MULTICAST_DELEGATE(FOnServerStopped);
	FOnServerStarted OnServerStarted;
	FOnServerStopped OnServerStopped;

private:
	FMCPHttpServer() = default;
	friend class FMCPHttpIsolationTest; // Fixture exercises the actual route handlers without opening a port.
	friend class FMCPProtocolConformanceTest; // v5 increment 24: conformance fixture
	friend class FMCPPolicyMatrixTest; // v5 increment 26: policy matrix fixture

	// Streamable HTTP transport (MCP spec 2025-03-26)
	bool HandleMCPPost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMCPGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMCPDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMCPOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Legacy SSE transport (MCP spec 2024-11-05)
	bool HandleSSEConnect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSSEMessage(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// JSON-RPC dispatch
	TSharedPtr<FJsonObject> HandleJsonRpcRequest(const FJsonRpcRequest& Request, const FMCPRequestContext& Context);
	TSharedPtr<FJsonObject> HandleInitialize(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandleToolsList(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandleToolsCall(const FJsonRpcRequest& Request, const FMCPRequestContext& Context);
	TSharedPtr<FJsonObject> HandleToolsGetSchema(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandleResourcesList(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandleResourcesRead(const FJsonRpcRequest& Request, const FMCPRequestContext& Context);
	TSharedPtr<FJsonObject> HandleResourcesTemplatesList(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandlePromptsList(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandlePromptsGet(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandlePing(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandleTasks(const FJsonRpcRequest& Request, const FMCPRequestContext& Context); // v5 increment 24

	// Cancellation: map JSON-RPC request id → atomic cancel flag for in-flight tool calls.
	// Network thread flips the flag when notifications/cancelled arrives; the game-thread
	// handler polls Context.IsCancelled() and bails out cooperatively.
	TSharedPtr<TAtomic<bool>> RegisterCancelFlag(const FString& RequestId, const FString& SessionId);
	void                      UnregisterCancelFlag(const FString& RequestId);
	bool                      MarkCancelled(const FString& RequestId);  // returns true if id was tracked
	void                      HandleCancelledNotification(const FJsonRpcRequest& Request, const FMCPRequestContext& Context);
	static FString CancellationKey(const FMCPRequestContext& Context, const TSharedPtr<FJsonValue>& Id);

	TMap<FString, TSharedPtr<TAtomic<bool>>> InFlightCancelFlags;
	TMap<FString, FString> CancelSessionIds;
	mutable FCriticalSection                  CancelFlagsLock;

	// Phase C / C4 — multi-call transactions. Each session may have at most one
	// active transaction at a time. The transaction wraps GEditor->BeginTransaction
	// so tools' inner FScopedTransactions nest into one undo step on commit.
	//
	// v4.5 Phase 0 / R1: the bookkeeping moved to the shared FMCPTransactionManager
	// (Public/MCPTransactionScope.h) so the chat panel gets the same one-undo-step
	// semantics and stale owners from either front-end are swept identically.
	// Owner id == Mcp-Session-Id for this transport.
	TSharedPtr<FJsonObject> HandleTransactionsBegin(const FJsonRpcRequest& Request, const FMCPRequestContext& Context);
	TSharedPtr<FJsonObject> HandleTransactionsCommit(const FJsonRpcRequest& Request, const FMCPRequestContext& Context);
	TSharedPtr<FJsonObject> HandleTransactionsRollback(const FJsonRpcRequest& Request, const FMCPRequestContext& Context);

	// Phase C / C5 — per-session working set (selection, focus pointers).
	// v4.5 Phase 0 / R2: storage moved to the shared FMCPWorkingSetStore
	// (Public/MCPWorkingSet.h).
	TSharedPtr<FJsonObject> HandleWorkingSetGet(const FJsonRpcRequest& Request, const FMCPRequestContext& Context);
	TSharedPtr<FJsonObject> HandleWorkingSetSet(const FJsonRpcRequest& Request, const FMCPRequestContext& Context);
	TSharedPtr<FJsonObject> HandleWorkingSetClear(const FJsonRpcRequest& Request, const FMCPRequestContext& Context);

	// ----------------------------------------------------------------
	// v4.5 — Server-Sent Events (real streaming). 5.8's FHttpServerResponse
	// supports a streaming body queue (EHttpServerResponseFlags::MultipleWriteStream
	// + StreamingBodyQueue/StreamingBodyComplete), so GET /mcp can hold an open
	// text/event-stream and push notifications/progress to the client live.
	// ----------------------------------------------------------------
	struct FMCPSSEStream
	{
		// SPSC: produced on the game thread (tool progress), drained by the HTTP
		// server's WriteStream (also game thread).
		TSharedPtr<TQueue<TArray<uint8>, EQueueMode::Spsc>> Queue;
		TSharedPtr<TAtomic<bool>> Complete;
	};
	TMap<FString, TSharedPtr<FMCPSSEStream>> SSEStreamsBySession;
	mutable FCriticalSection                 SSELock;

	/** Enqueue one SSE frame onto a session's open GET stream (no-op if none).
	 *  EventName empty + Data starting with ':' is sent as a raw comment line. */
	void PushToSession(const FString& SessionId, const FString& EventName, const FString& Data);
	/** Mark a session's SSE stream complete and drop it (DELETE / sweep / Stop). */
	void CloseSessionStream(const FString& SessionId);

	// CORS headers — echoes the validated origin, omits ACAO when empty.
	void AddCorsHeaders(TUniquePtr<FHttpServerResponse>& Response, const FString& AllowedOrigin = TEXT(""));

	// Session management
	FString GenerateSessionId();
	bool RejectInvalidSession(const FString& SessionId, const FMCPAuthResult& Auth, const FHttpResultCallback& OnComplete);
	void DropSession(const FString& SessionId);

	/** Refresh a session's last-activity timestamp (no-op for unknown/empty ids). */
	void TouchSession(const FString& SessionId);

	/** Ticker callback (game thread, ~1/min): drops sessions idle beyond
	 *  SessionIdleTimeoutMinutes, clearing their working set and cancelling any
	 *  transaction they left open so the undo stack stays healthy. (v4 Phase 0 —
	 *  v3 leaked sessions until explicit DELETE.) */
	bool SweepStaleSessions(float DeltaTime);

	// Rate limiting
	bool CheckRateLimit(const FString& ClientId);

	bool bIsRunning = false;
	int32 CurrentPort = 0;
	TArray<FHttpRouteHandle> RouteHandles;
	FTSTicker::FDelegateHandle SessionSweepTickerHandle;

	// v4 — live stats (status-bar UI)
	TAtomic<uint64> StatTotalRequests{ 0 };
	TAtomic<uint64> StatTotalToolCalls{ 0 };
	double ServerStartTime = 0.0;
	mutable FCriticalSection StatsLock;        // guards the two fields below
	FString StatLastToolName;
	double StatLastToolCallTime = 0.0;

	// Session tracking
	TMap<FString, double> SessionLastActivity;
	TMap<FString, FString> SessionPrincipals;
	mutable FCriticalSection SessionLock;

	// Rate limiting
	TMap<FString, TArray<double>> RequestTimestamps;
	mutable FCriticalSection RateLimitLock;
};
