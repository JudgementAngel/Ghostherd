// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Authorization scope of a request. Tools are filtered against this by
 * FMCPToolRegistry::ExecuteTool:
 *   - a tool marked Destructive (e.g. delete_asset) requires Destructive;
 *   - any tool NOT marked ReadOnly requires at least Scene, so a Read-scoped
 *     session is genuinely read-only (fail-closed: unannotated == mutating).
 *
 * Scope is set from FMCPAuth::Authenticate. With bRequireAuthToken=false every
 * caller gets Scene, or Destructive when bAllowDestructiveScope is on; Read is
 * reached by mapping a token to "read" in AuthTokenScopes, or by the Chat
 * module's ReadOnly approval mode. The struct default below is Read purely so
 * a default-constructed context is the safe one.
 */
enum class EMCPScope : uint8
{
	Read = 0,        // List/inspect tools only
	Scene = 1,       // Read + non-destructive mutations (create, edit)
	Destructive = 2  // Scene + destructive ops (delete, overwrite)
};

/**
 * Per-request context carried alongside `tools/call` arguments. Lets the
 * registry, the auth layer, and tool handlers share scope, cancellation
 * state, and a progress sink without threading them through every signature.
 *
 * Designed to be cheap to construct (default = Read scope, no progress sink,
 * no cancel flag) so the existing `ExecuteTool(Name, Args)` overload stays
 * source-compatible.
 */
struct UNREALMCPSERVER_API FMCPRequestContext
{
	/** Scope this request is authorized for. Default = Read. */
	EMCPScope Scope = EMCPScope::Read;

	/** Mcp-Session-Id header value (if any). Empty when stateless. */
	FString SessionId;
	FString PrincipalId = TEXT("internal"); // HTTP authentication always overrides this.

	/** v4: true only for top-level HTTP tools/call — allows a .LongRunning()
	 *  tool to be converted into a pollable background task. Inner calls
	 *  (run_tool_script steps) keep this false so transaction semantics hold. */
	bool bAllowAsyncTask = false;

	/** Optional progress sink. Tools may call Progress(0..1, "msg") to push
	 *  notifications to the client over SSE/WS. Null when the transport doesn't
	 *  support streaming or the client didn't subscribe. Thread-safe per call. */
	TFunction<void(float /*Fraction*/, const FString& /*Message*/)> ProgressSink;

	/** Cooperative cancellation flag. Tools poll IsCancelled() between game-thread
	 *  waits and bail out cleanly. Atomic so it can be flipped from the network
	 *  thread by `notifications/cancelled`. */
	TSharedPtr<TAtomic<bool>> CancelFlag;

	bool IsCancelled() const
	{
		return CancelFlag.IsValid() && CancelFlag->Load(EMemoryOrder::Relaxed);
	}

	void Progress(float Fraction, const FString& Message) const
	{
		if (ProgressSink) ProgressSink(Fraction, Message);
	}

	/** Returns true if Scope is sufficient for the required minimum. */
	bool HasScope(EMCPScope Required) const
	{
		return static_cast<uint8>(Scope) >= static_cast<uint8>(Required);
	}
};
