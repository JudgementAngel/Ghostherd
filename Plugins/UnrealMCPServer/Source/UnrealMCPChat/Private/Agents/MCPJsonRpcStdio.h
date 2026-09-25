// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FMCPChatProcessRunner;

/**
 * Phase 7 — JSON-RPC 2.0 over a newline-framed stdio pipe.
 *
 * ACP is bidirectional: we call the agent (`initialize`, `session/prompt`) and the
 * agent calls us (`fs/read_text_file`, `session/request_permission`). Both halves
 * share one id space and one pipe, so this owns:
 *
 *   - outgoing request → response correlation, with a per-request timeout
 *   - incoming notifications  → OnNotification
 *   - incoming REQUESTS       → OnRequest, which MUST produce a response
 *   - non-JSON lines          → OnDiagnostic
 *
 * That last one is not a nicety. stdout and stderr arrive on the same pipe (see
 * FMCPChatProcessRunner), so a `node` deprecation warning lands in the middle of
 * the protocol stream. Treating it as a parse failure would take down a working
 * agent over a warning.
 *
 * Everything here runs on the game thread; the runner has already marshalled.
 */
class FMCPJsonRpcStdio : public TSharedFromThis<FMCPJsonRpcStdio>
{
public:
	/** Success path: Result may be null for a `{}` result. */
	DECLARE_DELEGATE_TwoParams(FOnResponse,
		const TSharedPtr<FJsonObject>& /*Result*/, const TSharedPtr<FJsonObject>& /*Error*/);

	DECLARE_DELEGATE_TwoParams(FOnNotification,
		const FString& /*Method*/, const TSharedPtr<FJsonObject>& /*Params*/);

	/**
	 * An incoming request. The handler must call Respond() or RespondError() with
	 * the supplied id — exactly once. An agent blocked on a request we never answer
	 * hangs forever with no error anywhere, which is the worst failure shape here,
	 * so unanswered ids are swept and auto-failed.
	 */
	DECLARE_DELEGATE_ThreeParams(FOnRequest,
		const FString& /*Method*/, const TSharedPtr<FJsonObject>& /*Params*/,
		const TSharedPtr<FJsonValue>& /*Id*/);

	DECLARE_DELEGATE_OneParam(FOnDiagnostic, const FString& /*Line*/);

	explicit FMCPJsonRpcStdio(const TSharedPtr<FMCPChatProcessRunner>& InRunner);

	/** Feed one line from the process runner. */
	void HandleLine(const FString& Line);

	// ---- Outgoing ----

	/** @return the request id, or 0 when the pipe is gone. */
	int32 SendRequest(const FString& Method, const TSharedPtr<FJsonObject>& Params,
	                  FOnResponse OnResponse, float TimeoutSeconds = 120.f);

	void SendNotification(const FString& Method, const TSharedPtr<FJsonObject>& Params);

	// ---- Answering the agent ----

	void Respond(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result);
	void RespondError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message);

	/** Fail every in-flight request. Called when the process dies so callers get an
	 *  error instead of a callback that never fires. */
	void FailAllPending(const FString& Reason);

	/** Time out requests older than their deadline. Driven by the backend's ticker. */
	void TickTimeouts();

	FOnNotification OnNotification;
	FOnRequest      OnRequest;
	FOnDiagnostic   OnDiagnostic;

	/** Standard JSON-RPC error codes, plus the ACP-relevant ones. */
	static constexpr int32 ErrorParse          = -32700;
	static constexpr int32 ErrorInvalidRequest = -32600;
	static constexpr int32 ErrorMethodNotFound = -32601;
	static constexpr int32 ErrorInvalidParams  = -32602;
	static constexpr int32 ErrorInternal       = -32603;

private:
	struct FPending
	{
		FOnResponse Callback;
		double      DeadlineSeconds = 0.0;
		FString     Method;
	};

	void SendRaw(const TSharedPtr<FJsonObject>& Message);

	TWeakPtr<FMCPChatProcessRunner> Runner;
	TMap<int32, FPending>           Pending;
	int32                           NextId = 1;
};
