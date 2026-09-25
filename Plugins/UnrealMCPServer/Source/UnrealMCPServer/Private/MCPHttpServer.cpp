// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPHttpServer.h"
#include "MCPAuth.h"
#include "MCPTaskManager.h"
#include "MCPActorChangePlans.h"
#include "MCPEditorSurfaces.h"
#include "MCPSnapshots.h"
#include "MCPScenarios.h"
#include "MCPDiagnostics.h"
#include "MCPResultStore.h"
#include "MCPRequestContext.h"
#include "MCPToolRegistry.h"
#include "MCPResourceProvider.h"
#include "MCPPromptProvider.h"
#include "MCPSettings.h"
#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "HttpServerRequest.h"
#include "Misc/Guid.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	/** Helper to avoid ambiguous FHttpServerResponse::Create overloads in UE 5.7+ */
	TUniquePtr<FHttpServerResponse> CreateHttpResponse(const FString& Body, const FString& ContentType)
	{
		FTCHARToUTF8 Converter(*Body);
		TArray<uint8> Payload;
		Payload.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
		return FHttpServerResponse::Create(MoveTemp(Payload), ContentType);
	}
}

FMCPHttpServer& FMCPHttpServer::Get()
{
	static FMCPHttpServer Instance;
	return Instance;
}

bool FMCPHttpServer::Start(int32 Port)
{
	if (bIsRunning)
	{
		UE_LOG(LogUnrealMCP, Warning, TEXT("MCP server already running on port %d"), CurrentPort);
		return true;
	}

	// v4: real bind-address control (replaces the no-op bAllowRemoteConnections).
	// UE's listener reads [HTTPServer.Listeners] DefaultBindAddress from GEngineIni
	// at creation time; we set it in-memory just before creating ours. Non-loopback
	// binding is refused without bearer-token auth — falling back to loopback —
	// because plaintext HTTP with no token on a LAN is an open editor console.
	{
		const UMCPSettings* Settings = UMCPSettings::Get();
		FString BindAddress = Settings->BindAddress.TrimStartAndEnd();
		if (BindAddress.IsEmpty())
		{
			BindAddress = TEXT("127.0.0.1");
		}
		const bool bLoopback = BindAddress == TEXT("127.0.0.1")
			|| BindAddress.Equals(TEXT("localhost"), ESearchCase::IgnoreCase) || BindAddress == TEXT("::1");
		if (!bLoopback && (!Settings->bRequireAuthToken || Settings->AuthTokens.Num() == 0))
		{
			UE_LOG(LogUnrealMCP, Error,
				TEXT("BindAddress '%s' refused: non-loopback binding requires bRequireAuthToken with at least one token. Falling back to 127.0.0.1."),
				*BindAddress);
			BindAddress = TEXT("127.0.0.1");
		}
		if (BindAddress != TEXT("127.0.0.1") && BindAddress != TEXT("::1") && !BindAddress.Equals(TEXT("localhost"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogUnrealMCP, Warning,
				TEXT("MCP server binding to '%s' — reachable beyond this machine. Bearer-token auth is enforced."),
				*BindAddress);
		}
		GConfig->SetString(TEXT("HTTPServer.Listeners"), TEXT("DefaultBindAddress"), *BindAddress, GEngineIni);
	}

	FHttpServerModule& HttpModule = FHttpServerModule::Get();
	TSharedPtr<IHttpRouter> Router = HttpModule.GetHttpRouter(Port);
	if (!Router.IsValid())
	{
		UE_LOG(LogUnrealMCP, Error, TEXT("Failed to create HTTP router on port %d"), Port);
		return false;
	}

	// ---- Streamable HTTP transport (MCP spec 2025-03-26) ----

	// POST /mcp - Client sends JSON-RPC requests
	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FMCPHttpServer::HandleMCPPost)));

	// GET /mcp - Server-to-client notifications (SSE stream)
	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FMCPHttpServer::HandleMCPGet)));

	// DELETE /mcp - Session termination
	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_DELETE,
		FHttpRequestHandler::CreateRaw(this, &FMCPHttpServer::HandleMCPDelete)));

	// OPTIONS /mcp - CORS preflight
	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateRaw(this, &FMCPHttpServer::HandleMCPOptions)));

	// ---- Legacy SSE transport (MCP spec 2024-11-05) ----

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/sse")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FMCPHttpServer::HandleSSEConnect)));

	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/message")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FMCPHttpServer::HandleSSEMessage)));

	// OPTIONS for legacy endpoint
	RouteHandles.Add(Router->BindRoute(
		FHttpPath(TEXT("/message")),
		EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateRaw(this, &FMCPHttpServer::HandleMCPOptions)));

	HttpModule.StartAllListeners();

	// Idle-session sweep (v4 Phase 0): once a minute, reap sessions that have gone
	// quiet, releasing their working set and cancelling abandoned transactions.
	SessionSweepTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FMCPHttpServer::SweepStaleSessions), 60.0f);

	bIsRunning = true;
	CurrentPort = Port;
	ServerStartTime = FPlatformTime::Seconds();
	StatTotalRequests.Store(0, EMemoryOrder::Relaxed);
	StatTotalToolCalls.Store(0, EMemoryOrder::Relaxed);

	UE_LOG(LogUnrealMCP, Log, TEXT("MCP server started on port %d"), Port);
	UE_LOG(LogUnrealMCP, Log, TEXT("  Streamable HTTP: POST/GET/DELETE http://localhost:%d/mcp"), Port);
	UE_LOG(LogUnrealMCP, Log, TEXT("  Legacy SSE:      GET http://localhost:%d/sse"), Port);

	OnServerStarted.Broadcast();
	return true;
}

void FMCPHttpServer::Stop()
{
	if (!bIsRunning)
	{
		return;
	}

	FHttpServerModule& HttpModule = FHttpServerModule::Get();
	for (const FHttpRouteHandle& Handle : RouteHandles)
	{
		HttpModule.GetHttpRouter(CurrentPort)->UnbindRoute(Handle);
	}
	RouteHandles.Empty();

	if (SessionSweepTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SessionSweepTickerHandle);
		SessionSweepTickerHandle.Reset();
	}

	// v4.5 Phase 0 / R1 — drop every session's state before clearing the session
	// map, otherwise a client that stops the server mid-transaction leaves an open
	// GEditor transaction with no owner left to commit or roll it back.
	{
		TArray<FString> Sids;
		{
			FScopeLock Lock(&SessionLock);
			SessionLastActivity.GenerateKeyArray(Sids);
		}
		for (const FString& Sid : Sids)
		{
			DropSession(Sid);
		}
	}

	{
		FScopeLock Lock(&SessionLock);
		SessionLastActivity.Empty();
		SessionPrincipals.Empty();
	}

	// v4.5 — complete and drop all open SSE streams so their connections finish.
	{
		FScopeLock Lock(&SSELock);
		for (TPair<FString, TSharedPtr<FMCPSSEStream>>& Pair : SSEStreamsBySession)
		{
			if (Pair.Value.IsValid() && Pair.Value->Complete.IsValid())
			{
				Pair.Value->Complete->Store(true);
			}
		}
		SSEStreamsBySession.Empty();
	}

	bIsRunning = false;
	UE_LOG(LogUnrealMCP, Log, TEXT("MCP server stopped"));

	OnServerStopped.Broadcast();
}

// ============================================================================
// Streamable HTTP Transport
// ============================================================================

bool FMCPHttpServer::HandleMCPPost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const UMCPSettings* Settings = UMCPSettings::Get();

	// Request body size limit (v4 Phase 0): reject oversized payloads before any
	// parsing so a hostile/buggy client can't exhaust editor memory.
	const int64 MaxBodyBytes = (int64)FMath::Clamp(Settings->MaxRequestSizeMB, 1, 256) * 1024 * 1024;
	if ((int64)Request.Body.Num() > MaxBodyBytes)
	{
		UE_LOG(LogUnrealMCP, Warning, TEXT("Rejected oversized MCP request: %d bytes (limit %lld)"),
			Request.Body.Num(), MaxBodyBytes);
		auto Response = CreateHttpResponse(
			FString::Printf(TEXT("{\"error\":\"Request body exceeds %d MB limit\"}"), Settings->MaxRequestSizeMB),
			TEXT("application/json"));
		Response->Code = (EHttpServerResponseCodes)413; // Payload Too Large
		AddCorsHeaders(Response);
		OnComplete(MoveTemp(Response));
		return true;
	}

	// Rate limiting
	if (!CheckRateLimit(TEXT("default")))
	{
		auto Response = CreateHttpResponse(TEXT("{\"error\":\"Rate limit exceeded\"}"), TEXT("application/json"));
		Response->Code = EHttpServerResponseCodes::TooManyRequests;
		AddCorsHeaders(Response);
		OnComplete(MoveTemp(Response));
		return true;
	}

	StatTotalRequests.IncrementExchange();

	// Authenticate (origin allow-list + bearer token if required)
	FMCPAuthResult Auth = FMCPAuth::Authenticate(Request);
	if (Auth.Outcome == FMCPAuthResult::OriginRejected)
	{
		auto Response = CreateHttpResponse(
			FString::Printf(TEXT("{\"error\":\"Origin rejected: %s\"}"), *Auth.ErrorMessage),
			TEXT("application/json"));
		Response->Code = (EHttpServerResponseCodes)403;
		AddCorsHeaders(Response, Auth.ResolvedOrigin);
		OnComplete(MoveTemp(Response));
		return true;
	}
	if (Auth.Outcome == FMCPAuthResult::Unauthorized)
	{
		auto Response = CreateHttpResponse(
			FString::Printf(TEXT("{\"error\":\"Unauthorized: %s\"}"), *Auth.ErrorMessage),
			TEXT("application/json"));
		Response->Code = EHttpServerResponseCodes::Denied;  // 401
		Response->Headers.Add(TEXT("WWW-Authenticate"), { TEXT("Bearer realm=\"unreal-mcp\"") });
		AddCorsHeaders(Response, Auth.ResolvedOrigin);
		OnComplete(MoveTemp(Response));
		return true;
	}

	FMCPRequestContext Context;
	Context.Scope = Auth.Scope;
	Context.PrincipalId = Auth.PrincipalId;

	// Extract session id (lowercase header per UE 5.7 convention) so transactions
	// and working-set methods can key by it.
	if (const TArray<FString>* SidHeader = Request.Headers.Find(TEXT("mcp-session-id")))
	{
		if (SidHeader->Num() != 1 || (*SidHeader)[0].IsEmpty()) { RejectInvalidSession(TEXT(""), Auth, OnComplete); return true; }
        Context.SessionId = (*SidHeader)[0];
	}
	if (!Context.SessionId.IsEmpty() && RejectInvalidSession(Context.SessionId, Auth, OnComplete)) return true;

	// Parse body - use explicit length to avoid null-terminator issues
	FString Body;
	if (Request.Body.Num() > 0)
	{
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		Body = FString(Converter.Length(), Converter.Get());
	}

	if (Settings->bVerboseLogging)
	{
		UE_LOG(LogUnrealMCP, Log, TEXT("MCP POST body (%d bytes): %s"), Body.Len(), *Body);
	}

	if (Body.IsEmpty())
	{
		auto ErrorResponse = FJsonRpcResponse::MakeError(nullptr, FJsonRpcResponse::ParseError, TEXT("Empty request body"));
		auto Response = CreateHttpResponse(JsonToString(ErrorResponse), TEXT("application/json"));
		AddCorsHeaders(Response, Auth.ResolvedOrigin);
		OnComplete(MoveTemp(Response));
		return true;
	}

	TSharedPtr<FJsonObject> JsonBody = StringToJson(Body);
	if (!JsonBody.IsValid())
	{
		UE_LOG(LogUnrealMCP, Error, TEXT("Failed to parse JSON body: %s"), *Body);
		auto ErrorResponse = FJsonRpcResponse::MakeError(nullptr, FJsonRpcResponse::ParseError, TEXT("Invalid JSON"));
		auto Response = CreateHttpResponse(JsonToString(ErrorResponse), TEXT("application/json"));
		AddCorsHeaders(Response, Auth.ResolvedOrigin);
		OnComplete(MoveTemp(Response));
		return true;
	}

	FJsonRpcRequest RpcRequest;
	if (!RpcRequest.ParseFromJson(JsonBody))
	{
		auto ErrorResponse = FJsonRpcResponse::MakeError(nullptr, FJsonRpcResponse::InvalidRequest, TEXT("Invalid JSON-RPC request"));
		auto Response = CreateHttpResponse(JsonToString(ErrorResponse), TEXT("application/json"));
		AddCorsHeaders(Response, Auth.ResolvedOrigin);
		OnComplete(MoveTemp(Response));
		return true;
	}

	if (RpcRequest.Method != MCPProtocol::Methods::Initialize && Context.SessionId.IsEmpty())
    {
        RejectInvalidSession(Context.SessionId, Auth, OnComplete);
        return true;
    }

    // Handle notification (no response needed, but we still send 202)
	if (RpcRequest.IsNotification())
	{
		HandleJsonRpcRequest(RpcRequest, Context); // Process but ignore result
		auto Response = CreateHttpResponse(TEXT(""), TEXT("application/json"));
		Response->Code = EHttpServerResponseCodes::Accepted;
		AddCorsHeaders(Response, Auth.ResolvedOrigin);
		OnComplete(MoveTemp(Response));
		return true;
	}

	// Handle request
	TSharedPtr<FJsonObject> RpcResponse = HandleJsonRpcRequest(RpcRequest, Context);
	FString ResponseStr = JsonToString(RpcResponse);

	if (Settings->bVerboseLogging)
	{
		UE_LOG(LogUnrealMCP, Verbose, TEXT("MCP response: %s"), *ResponseStr);
	}

	auto Response = CreateHttpResponse(ResponseStr, TEXT("application/json"));
	Response->Code = EHttpServerResponseCodes::Ok;

	// Set session header only on initialize response, and store the session
	if (RpcRequest.Method == MCPProtocol::Methods::Initialize && RpcResponse.IsValid() && !RpcResponse->HasField(TEXT("error")))
	{
		FString SessionId = Context.SessionId.IsEmpty() ? GenerateSessionId() : Context.SessionId;
		Response->Headers.Add(TEXT("Mcp-Session-Id"), { SessionId });
		{
			FScopeLock Lock(&SessionLock);
			SessionLastActivity.Add(SessionId, FPlatformTime::Seconds());
			SessionPrincipals.Add(SessionId, Auth.PrincipalId);
		}
		UE_LOG(LogUnrealMCP, Log, TEXT("MCP session created: %s"), *SessionId);
	}

	AddCorsHeaders(Response, Auth.ResolvedOrigin);
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMCPHttpServer::HandleMCPGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// SSE carries session results and requires the same authenticated owner.
	FMCPAuthResult Auth = FMCPAuth::Authenticate(Request);
	if (Auth.Outcome == FMCPAuthResult::OriginRejected)
	{
		auto Response = CreateHttpResponse(TEXT("{\"error\":\"Origin rejected\"}"), TEXT("application/json"));
		Response->Code = (EHttpServerResponseCodes)403;
		AddCorsHeaders(Response, Auth.ResolvedOrigin);
		OnComplete(MoveTemp(Response));
		return true;
	}

	if (Auth.Outcome == FMCPAuthResult::Unauthorized)
	{
		auto Response = CreateHttpResponse(TEXT("{\"error\":\"Unauthorized\"}"), TEXT("application/json"));
		Response->Code = (EHttpServerResponseCodes)401;
		Response->Headers.Add(TEXT("WWW-Authenticate"), { TEXT("Bearer realm=\"unreal-mcp\"") });
		AddCorsHeaders(Response, Auth.ResolvedOrigin);
		OnComplete(MoveTemp(Response));
		return true;
	}

	// v4.5 — real SSE. The GET stream is the server->client channel of Streamable
	// HTTP (MCP spec): we hold it open as text/event-stream and push
	// notifications/progress for in-flight tool calls. Requires a session id so we
	// can correlate the stream with this client's POSTs.
	FString SessionId;
	if (const TArray<FString>* SidHeader = Request.Headers.Find(TEXT("mcp-session-id")))
	{
		if (SidHeader->Num() != 1 || (*SidHeader)[0].IsEmpty()) { RejectInvalidSession(TEXT(""), Auth, OnComplete); return true; }
        SessionId = (*SidHeader)[0];
	}
	if (SessionId.IsEmpty())
	{
		// No session to correlate against — clients without one poll get_task_status.
		auto Response = CreateHttpResponse(
			TEXT("Open an SSE stream by sending Mcp-Session-Id (from initialize). Otherwise poll get_task_status."),
			TEXT("text/plain"));
		Response->Code = (EHttpServerResponseCodes)405;
		AddCorsHeaders(Response, Auth.ResolvedOrigin);
		OnComplete(MoveTemp(Response));
		return true;
	}

	if (RejectInvalidSession(SessionId, Auth, OnComplete)) return true;

	// One stream per session: replace any previous stream (client reconnected).
	CloseSessionStream(SessionId);

	TSharedPtr<FMCPSSEStream> Stream = MakeShared<FMCPSSEStream>();
	Stream->Queue = MakeShared<TQueue<TArray<uint8>, EQueueMode::Spsc>>();
	Stream->Complete = MakeShared<TAtomic<bool>>(false);
	{
		FScopeLock Lock(&SSELock);
		SSEStreamsBySession.Add(SessionId, Stream);
	}

	auto Response = MakeUnique<FHttpServerResponse>();
	Response->Code = EHttpServerResponseCodes::Ok;
	Response->Headers.Add(TEXT("Content-Type"), { TEXT("text/event-stream") });
	Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache") });
	Response->Headers.Add(TEXT("Connection"), { TEXT("keep-alive") });
	AddCorsHeaders(Response, Auth.ResolvedOrigin);
	Response->Flags = EHttpServerResponseFlags::MultipleWriteStream | EHttpServerResponseFlags::HasAdditionalWrites;
	Response->StreamingBodyQueue = Stream->Queue;
	Response->StreamingBodyComplete = Stream->Complete;

	// Opening comment so the client sees the stream is live immediately.
	PushToSession(SessionId, FString(), TEXT(": unreal-mcp stream open"));

	UE_LOG(LogUnrealMCP, Log, TEXT("MCP SSE stream opened for session %s"), *SessionId);
	OnComplete(MoveTemp(Response));
	return true;
}

void FMCPHttpServer::PushToSession(const FString& SessionId, const FString& EventName, const FString& Data)
{
	if (SessionId.IsEmpty()) { return; }

	TSharedPtr<FMCPSSEStream> Stream;
	{
		FScopeLock Lock(&SSELock);
		if (TSharedPtr<FMCPSSEStream>* Found = SSEStreamsBySession.Find(SessionId))
		{
			Stream = *Found;
		}
	}
	if (!Stream.IsValid() || !Stream->Queue.IsValid()) { return; }

	FString Frame;
	if (EventName.IsEmpty() && Data.StartsWith(TEXT(":")))
	{
		Frame = Data + TEXT("\n\n");            // raw SSE comment (also a keep-alive)
	}
	else
	{
		if (!EventName.IsEmpty()) { Frame += FString::Printf(TEXT("event: %s\n"), *EventName); }
		Frame += FString::Printf(TEXT("data: %s\n\n"), *Data);
	}

	FTCHARToUTF8 Conv(*Frame);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());
	Stream->Queue->Enqueue(MoveTemp(Bytes));
}

void FMCPHttpServer::CloseSessionStream(const FString& SessionId)
{
	TSharedPtr<FMCPSSEStream> Stream;
	{
		FScopeLock Lock(&SSELock);
		if (SSEStreamsBySession.RemoveAndCopyValue(SessionId, Stream))
		{
			// fall through to signal completion outside the lock
		}
	}
	if (Stream.IsValid() && Stream->Complete.IsValid())
	{
		Stream->Complete->Store(true);  // lets the HTTP server finish & close the stream
	}
}

bool FMCPHttpServer::HandleMCPDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FMCPAuthResult Auth = FMCPAuth::Authenticate(Request);
	if (Auth.Outcome == FMCPAuthResult::OriginRejected)
	{
		auto Response = CreateHttpResponse(TEXT("{\"error\":\"Origin rejected\"}"), TEXT("application/json"));
		Response->Code = (EHttpServerResponseCodes)403;
		AddCorsHeaders(Response, Auth.ResolvedOrigin);
		OnComplete(MoveTemp(Response));
		return true;
	}

	if (Auth.Outcome == FMCPAuthResult::Unauthorized)
    {
        auto Response = CreateHttpResponse(TEXT("{\"error\":\"Unauthorized\"}"), TEXT("application/json"));
        Response->Code = (EHttpServerResponseCodes)401;
        Response->Headers.Add(TEXT("WWW-Authenticate"), { TEXT("Bearer realm=\"unreal-mcp\"") });
        AddCorsHeaders(Response, Auth.ResolvedOrigin);
        OnComplete(MoveTemp(Response));
        return true;
    }
    const TArray<FString>* Header = Request.Headers.Find(TEXT("mcp-session-id"));
    const FString SessionId = Header && Header->Num() == 1 ? (*Header)[0] : FString();
    if (RejectInvalidSession(SessionId, Auth, OnComplete)) return true;
    DropSession(SessionId);

	auto Response = CreateHttpResponse(TEXT(""), TEXT("application/json"));
	Response->Code = EHttpServerResponseCodes::Ok;
	AddCorsHeaders(Response, Auth.ResolvedOrigin);
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMCPHttpServer::HandleMCPOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FMCPAuthResult Auth = FMCPAuth::Authenticate(Request);
	if (Auth.Outcome == FMCPAuthResult::OriginRejected)
	{
		auto Response = CreateHttpResponse(TEXT("{\"error\":\"Origin rejected\"}"), TEXT("application/json"));
		Response->Code = (EHttpServerResponseCodes)403;
		AddCorsHeaders(Response, Auth.ResolvedOrigin);
		OnComplete(MoveTemp(Response));
		return true;
	}

	auto Response = CreateHttpResponse(TEXT(""), TEXT("text/plain"));
	Response->Code = EHttpServerResponseCodes::Ok;
	AddCorsHeaders(Response, Auth.ResolvedOrigin);
	OnComplete(MoveTemp(Response));
	return true;
}

// ============================================================================
// Legacy SSE Transport
// ============================================================================

bool FMCPHttpServer::HandleSSEConnect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FMCPAuthResult Auth = FMCPAuth::Authenticate(Request);
	if (Auth.Outcome == FMCPAuthResult::OriginRejected)
	{
		auto Response = CreateHttpResponse(TEXT("{\"error\":\"Origin rejected\"}"), TEXT("application/json"));
		Response->Code = (EHttpServerResponseCodes)403;
		AddCorsHeaders(Response, Auth.ResolvedOrigin);
		OnComplete(MoveTemp(Response));
		return true;
	}

	if (Auth.Outcome == FMCPAuthResult::Unauthorized)
    {
        auto Response = CreateHttpResponse(TEXT("Unauthorized"), TEXT("text/plain"));
        Response->Code = (EHttpServerResponseCodes)401;
        AddCorsHeaders(Response, Auth.ResolvedOrigin);
        OnComplete(MoveTemp(Response));
        return true;
    }
	UE_LOG(LogUnrealMCP, Log, TEXT("Legacy SSE connect request received on /sse"));
	// Legacy SSE: return endpoint URL for message posting
	FString SessionId = GenerateSessionId();
	{
        FScopeLock Lock(&SessionLock);
        SessionLastActivity.Add(SessionId, FPlatformTime::Seconds());
        SessionPrincipals.Add(SessionId, Auth.PrincipalId);
    }
    FString MessageUrl = FString::Printf(TEXT("http://localhost:%d/message?sessionId=%s"), CurrentPort, *SessionId);

	FString SseEvent = FString::Printf(TEXT("event: endpoint\ndata: %s\n\n"), *MessageUrl);

	auto Response = CreateHttpResponse(SseEvent, TEXT("text/event-stream"));
	Response->Code = EHttpServerResponseCodes::Ok;
	Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache") });
	Response->Headers.Add(TEXT("Connection"), { TEXT("keep-alive") });
	AddCorsHeaders(Response, Auth.ResolvedOrigin);
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMCPHttpServer::HandleSSEMessage(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Same handling as Streamable HTTP POST
	FHttpServerRequest Forwarded = Request;
    if (const FString* Session = Request.QueryParams.Find(TEXT("sessionId")))
    {
        if (const TArray<FString>* Header = Request.Headers.Find(TEXT("mcp-session-id")))
            if (Header->Num() != 1 || (*Header)[0] != *Session)
            {
                auto Response = CreateHttpResponse(TEXT("Conflicting session IDs"), TEXT("text/plain"));
                Response->Code = (EHttpServerResponseCodes)400;
                OnComplete(MoveTemp(Response));
                return true;
            }
        Forwarded.Headers.Add(TEXT("mcp-session-id"), { *Session });
    }
    return HandleMCPPost(Forwarded, OnComplete);
}

// ============================================================================
// JSON-RPC Dispatch
// ============================================================================

TSharedPtr<FJsonObject> FMCPHttpServer::HandleJsonRpcRequest(const FJsonRpcRequest& Request, const FMCPRequestContext& Context)
{
	const FString& Method = Request.Method;
    if ((Method == MCPProtocol::Methods::TransactionsBegin || Method == MCPProtocol::Methods::TransactionsCommit
        || Method == MCPProtocol::Methods::TransactionsRollback) && !Context.HasScope(EMCPScope::Scene))
        return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidRequest, TEXT("Transaction methods require Scene scope"));

	if (Method == MCPProtocol::Methods::Initialize)
	{
		return HandleInitialize(Request);
	}
	else if (Method == MCPProtocol::Methods::Initialized)
	{
		// Notification, no response
		return nullptr;
	}
	else if (Method == MCPProtocol::Methods::Cancelled)
	{
		// Client-initiated cancellation; flip the in-flight cancel flag and return no response.
		HandleCancelledNotification(Request, Context);
		return nullptr;
	}
	else if (Method == MCPProtocol::Methods::TransactionsBegin)    { return HandleTransactionsBegin(Request, Context); }
	else if (Method == MCPProtocol::Methods::TransactionsCommit)   { return HandleTransactionsCommit(Request, Context); }
	else if (Method == MCPProtocol::Methods::TransactionsRollback) { return HandleTransactionsRollback(Request, Context); }
	else if (Method == MCPProtocol::Methods::WorkingSetGet)        { return HandleWorkingSetGet(Request, Context); }
	else if (Method == MCPProtocol::Methods::WorkingSetSet)        { return HandleWorkingSetSet(Request, Context); }
	else if (Method == MCPProtocol::Methods::WorkingSetClear)      { return HandleWorkingSetClear(Request, Context); }
	else if (Method == MCPProtocol::Methods::TasksList || Method == MCPProtocol::Methods::TasksGet || Method == MCPProtocol::Methods::TasksCancel)
	{
		return HandleTasks(Request, Context);
	}
	else if (Method == MCPProtocol::Methods::Ping)
	{
		return HandlePing(Request);
	}
	else if (Method == MCPProtocol::Methods::ToolsList)
	{
		return HandleToolsList(Request);
	}
	else if (Method == MCPProtocol::Methods::ToolsCall)
	{
		return HandleToolsCall(Request, Context);
	}
	else if (Method == MCPProtocol::Methods::ToolsGetSchema)
	{
		return HandleToolsGetSchema(Request);
	}
	else if (Method == MCPProtocol::Methods::ResourcesList)
	{
		return HandleResourcesList(Request);
	}
	else if (Method == MCPProtocol::Methods::ResourcesRead)
	{
		return HandleResourcesRead(Request, Context);
	}
	else if (Method == MCPProtocol::Methods::ResourcesTemplatesList)
	{
		return HandleResourcesTemplatesList(Request);
	}
	else if (Method == MCPProtocol::Methods::PromptsList)
	{
		return HandlePromptsList(Request);
	}
	else if (Method == MCPProtocol::Methods::PromptsGet)
	{
		return HandlePromptsGet(Request);
	}

	return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::MethodNotFound,
		FString::Printf(TEXT("Method not found: %s"), *Method));
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandleInitialize(const FJsonRpcRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("protocolVersion"), MCPProtocol::Version);

	// Server capabilities
	TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();

	// Tools capability
	TSharedPtr<FJsonObject> ToolsCap = MakeShared<FJsonObject>();
	Capabilities->SetObjectField(TEXT("tools"), ToolsCap);

	// Resources capability
	TSharedPtr<FJsonObject> ResourcesCap = MakeShared<FJsonObject>();
	Capabilities->SetObjectField(TEXT("resources"), ResourcesCap);

	// Prompts capability
	TSharedPtr<FJsonObject> PromptsCap = MakeShared<FJsonObject>();
	Capabilities->SetObjectField(TEXT("prompts"), PromptsCap);

	// v5 increment 24: honest capability flags plus the experimental extensions this server actually serves.
	ToolsCap->SetBoolField(TEXT("listChanged"), false);
	ResourcesCap->SetBoolField(TEXT("subscribe"), false); ResourcesCap->SetBoolField(TEXT("listChanged"), false);
	PromptsCap->SetBoolField(TEXT("listChanged"), false);
	TSharedPtr<FJsonObject> Experimental = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> TasksExt = MakeShared<FJsonObject>();
	TasksExt->SetBoolField(TEXT("list"), true); TasksExt->SetBoolField(TEXT("get"), true); TasksExt->SetBoolField(TEXT("cancel"), true);
	TasksExt->SetStringField(TEXT("note"), TEXT("tasks/list, tasks/get {taskId}, tasks/cancel {taskId} map onto the owned operation store (scenarios, measurements, generation jobs, legacy tasks)."));
	Experimental->SetObjectField(TEXT("unrealmcp/tasks"), TasksExt);
	TSharedPtr<FJsonObject> ResultsExt = MakeShared<FJsonObject>();
	ResultsExt->SetBoolField(TEXT("paged_results"), true); ResultsExt->SetNumberField(TEXT("max_inline_kb"), UMCPSettings::Get() ? UMCPSettings::Get()->MaxToolResultKB : 1024);
	Experimental->SetObjectField(TEXT("unrealmcp/results"), ResultsExt);
	Capabilities->SetObjectField(TEXT("experimental"), Experimental);

	Result->SetObjectField(TEXT("capabilities"), Capabilities);

	// Server info
	TSharedPtr<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
	ServerInfo->SetStringField(TEXT("name"), MCPProtocol::ServerName);
	ServerInfo->SetStringField(TEXT("version"), MCPProtocol::ServerVersion);
	Result->SetObjectField(TEXT("serverInfo"), ServerInfo);

	return FJsonRpcResponse::MakeResult(Request.Id, Result);
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandlePing(const FJsonRpcRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	return FJsonRpcResponse::MakeResult(Request.Id, Result);
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandleToolsList(const FJsonRpcRequest& Request)
{
	const UMCPSettings* Settings = UMCPSettings::Get();

	// Check if client requests full schemas (default: slim for context optimization)
	bool bFullSchemas = false;
	if (Request.Params.IsValid())
	{
		Request.Params->TryGetBoolField(TEXT("includeSchemas"), bFullSchemas);
	}

	// v4 Phase 1 — exposure mode. Catalog returns the meta-tools plus a small
	// high-frequency core; agents fetch everything else via search_tools /
	// get_tool_schemas. Per-request override: params.exposure = "full"|"catalog".
	bool bCatalog = (Settings->ToolExposureMode == EMCPToolExposureMode::Catalog);
	if (Request.Params.IsValid())
	{
		FString ExposureOverride;
		if (Request.Params->TryGetStringField(TEXT("exposure"), ExposureOverride))
		{
			if (ExposureOverride == TEXT("full"))    { bCatalog = false; }
			if (ExposureOverride == TEXT("catalog")) { bCatalog = true; }
		}
	}

	// v4.5 Phase 0 / R3 — the catalog-core name set now lives on the registry
	// (FMCPToolRegistry::GetCatalogCoreToolNames) so in-process clients such as the
	// embedded chat panel expose exactly the same catalog as this endpoint.
	const int32 TotalRegistered = FMCPToolRegistry::Get().GetToolCount();

	const TArray<TSharedPtr<FJsonValue>> Selected = FMCPToolRegistry::Get().GetToolsForExposure(bCatalog, bFullSchemas);

	// Cursor pagination (MCP spec): params.cursor is an opaque index token.
	int32 StartIdx = 0;
	if (Request.Params.IsValid())
	{
		FString Cursor;
		if (Request.Params->TryGetStringField(TEXT("cursor"), Cursor))
		{
			StartIdx = FMath::Max(0, FCString::Atoi(*Cursor));
		}
	}
	int32 Limit = Selected.Num(); // default: everything remaining
	if (Request.Params.IsValid() && Request.Params->HasField(TEXT("limit")))
	{
		Limit = FMath::Clamp((int32)Request.Params->GetNumberField(TEXT("limit")), 1, 1000);
	}

	TArray<TSharedPtr<FJsonValue>> Page;
	for (int32 i = StartIdx; i < Selected.Num() && Page.Num() < Limit; ++i)
	{
		Page.Add(Selected[i]);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tools"), Page);
	if (StartIdx + Page.Num() < Selected.Num())
	{
		Result->SetStringField(TEXT("nextCursor"), FString::FromInt(StartIdx + Page.Num()));
	}
	if (bCatalog)
	{
		// Non-standard but harmless: tell the agent the catalog is bigger than
		// this page so it reaches for search_tools instead of assuming 25 tools.
		TSharedPtr<FJsonObject> CatalogInfo = MakeShared<FJsonObject>();
		CatalogInfo->SetNumberField(TEXT("totalRegisteredTools"), TotalRegistered);
		CatalogInfo->SetNumberField(TEXT("exposed"), Page.Num());
		CatalogInfo->SetStringField(TEXT("hint"),
			TEXT("Catalog mode: this is a curated core. Use search_tools / list_tool_categories / get_tool_schemas to discover and load any of the other registered tools. All of them are callable via tools/call right now."));
		Result->SetObjectField(TEXT("catalogInfo"), CatalogInfo);
	}

	return FJsonRpcResponse::MakeResult(Request.Id, Result);
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandleToolsCall(const FJsonRpcRequest& Request, const FMCPRequestContext& Context)
{
	if (!Request.Params.IsValid())
	{
		return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidParams, TEXT("Missing params"));
	}

	FString ToolName;
	if (!Request.Params->TryGetStringField(TEXT("name"), ToolName))
	{
		return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidParams, TEXT("Missing tool name"));
	}

	TSharedPtr<FJsonObject> Arguments;
	if (Request.Params->HasField(TEXT("arguments")))
	{
		if (!Request.Params->HasTypedField<EJson::Object>(TEXT("arguments")))
            return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidParams, TEXT("arguments must be an object"));
        Arguments = Request.Params->GetObjectField(TEXT("arguments"));
	}
	else
	{
		Arguments = MakeShared<FJsonObject>();
	}

	UE_LOG(LogUnrealMCP, Log, TEXT("Executing tool: %s"), *ToolName);

	StatTotalToolCalls.IncrementExchange();
	{
		FScopeLock StatsScope(&StatsLock);
		StatLastToolName = ToolName;
		StatLastToolCallTime = FPlatformTime::Seconds();
	}

	// Register a cancel flag for this in-flight call so notifications/cancelled can interrupt it.
	const FString RequestIdStr = CancellationKey(Context, Request.Id);
	FMCPRequestContext CallContext = Context;
	CallContext.bAllowAsyncTask = true;  // v4: top-level HTTP calls may become pollable tasks
	if (!RequestIdStr.IsEmpty())
	{
		CallContext.CancelFlag = RegisterCancelFlag(RequestIdStr, Context.SessionId);
        if (!CallContext.CancelFlag.IsValid())
            return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidRequest, TEXT("Duplicate in-flight request ID for this session"));
	}

	// v4.5 — if this client has an open SSE stream (GET /mcp with its session id),
	// stream notifications/progress live. The registry chains this sink with its
	// task-manager sink, so progress reaches BOTH the SSE stream and get_task_status.
	if (!CallContext.SessionId.IsEmpty())
	{
		const FString Sid = CallContext.SessionId;
		TSharedPtr<FJsonValue> ProgressToken;
        const TSharedPtr<FJsonObject>* Meta = nullptr;
        if (Request.Params.IsValid() && Request.Params->TryGetObjectField(TEXT("_meta"), Meta))
            ProgressToken = (*Meta)->TryGetField(TEXT("progressToken"));
		CallContext.ProgressSink = [this, Sid, ProgressToken](float Fraction, const FString& Message)
		{
			TSharedPtr<FJsonObject> Note = MakeShared<FJsonObject>();
			Note->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
			Note->SetStringField(TEXT("method"), TEXT("notifications/progress"));
			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			if (!ProgressToken.IsValid() || (ProgressToken->Type != EJson::String && ProgressToken->Type != EJson::Number)) return;
            Params->SetField(TEXT("progressToken"), ProgressToken);
			Params->SetNumberField(TEXT("progress"), Fraction);
			if (!Message.IsEmpty()) { Params->SetStringField(TEXT("message"), Message); }
			Note->SetObjectField(TEXT("params"), Params);
			PushToSession(Sid, TEXT("message"), JsonToString(Note));
		};
	}

	FMCPToolResult ToolResult = FMCPToolRegistry::Get().ExecuteTool(ToolName, Arguments, CallContext);

	if (!RequestIdStr.IsEmpty())
	{
		UnregisterCancelFlag(RequestIdStr);
	}

	// v5 increment 24 (V5-11): bounded response. Oversized results are retained for the owner and paged.
	TSharedPtr<FJsonObject> ResultJson = ToolResult.ToJson();
	const UMCPSettings* Settings = UMCPSettings::Get();
	const int64 CapBytes = (int64)FMath::Clamp(Settings ? Settings->MaxToolResultKB : 1024, 16, 65536) * 1024;
	FString Serialized = JsonToString(ResultJson);
	if ((int64)Serialized.Len() > CapBytes)
	{
		const int64 TotalBytes = Serialized.Len();
		const FString ResultId = MCPResultStore::Store(Context, ToolName, MoveTemp(Serialized));
		TSharedPtr<FJsonObject> Bounded = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Content;
		TSharedPtr<FJsonObject> Text = MakeShared<FJsonObject>(); Text->SetStringField(TEXT("type"), TEXT("text"));
		Text->SetStringField(TEXT("text"), ResultId.IsEmpty()
			? FString::Printf(TEXT("Result of %s is %lld bytes, above the %lld byte response cap, and too large to retain. Narrow the request (limit, offset, filters)."), *ToolName, (long long)TotalBytes, (long long)CapBytes)
			: FString::Printf(TEXT("Result of %s is %lld bytes, above the %lld byte response cap. Stored for 10 minutes as %s: read it with get_result_page (offset/max_bytes) or unreal://results/%s."), *ToolName, (long long)TotalBytes, (long long)CapBytes, *ResultId, *ResultId));
		Content.Add(MakeShared<FJsonValueObject>(Text));
		Bounded->SetArrayField(TEXT("content"), Content);
		Bounded->SetBoolField(TEXT("isError"), ToolResult.bIsError);
		TSharedPtr<FJsonObject> Structured = MakeShared<FJsonObject>();
		Structured->SetBoolField(TEXT("result_truncated"), true); Structured->SetNumberField(TEXT("total_bytes"), (double)TotalBytes); Structured->SetNumberField(TEXT("cap_bytes"), (double)CapBytes);
		Structured->SetBoolField(TEXT("retained"), !ResultId.IsEmpty());
		if (!ResultId.IsEmpty()) { Structured->SetStringField(TEXT("result_id"), ResultId); Structured->SetStringField(TEXT("resource_uri"), TEXT("unreal://results/") + ResultId); }
		Bounded->SetObjectField(TEXT("structuredContent"), Structured);
		return FJsonRpcResponse::MakeResult(Request.Id, Bounded);
	}

	return FJsonRpcResponse::MakeResult(Request.Id, ResultJson);
}

// v5 increment 24 (V5-13): task extension over the owned operation store.
TSharedPtr<FJsonObject> FMCPHttpServer::HandleTasks(const FJsonRpcRequest& Request, const FMCPRequestContext& Context)
{
	const FString& Method = Request.Method;
	if (Method == MCPProtocol::Methods::TasksList)
	{
		const FMCPToolResult R = FMCPToolRegistry::Get().ExecuteTool(TEXT("list_editor_operations"), MakeShared<FJsonObject>(), Context);
		if (R.bIsError || !R.StructuredContent.IsValid()) return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InternalError, R.Content.Num() ? R.Content[0].Text : TEXT("tasks/list failed"));
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("tasks"), R.StructuredContent->GetArrayField(TEXT("operations")));
		return FJsonRpcResponse::MakeResult(Request.Id, Result);
	}
	FString TaskId;
	if (!Request.Params.IsValid() || !Request.Params->TryGetStringField(TEXT("taskId"), TaskId) || TaskId.IsEmpty())
		return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidParams, TEXT("Missing taskId"));
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>(); Args->SetStringField(TEXT("operation_id"), TaskId);
	if (Method == MCPProtocol::Methods::TasksGet) Args->SetBoolField(TEXT("include_events"), Request.Params->HasField(TEXT("includeEvents")) && Request.Params->GetBoolField(TEXT("includeEvents")));
	const FMCPToolResult R = FMCPToolRegistry::Get().ExecuteTool(Method == MCPProtocol::Methods::TasksGet ? TEXT("get_editor_operation") : TEXT("cancel_editor_operation"), Args, Context);
	if (R.bIsError)
	{
		const FString Code = R.StructuredContent.IsValid() ? R.StructuredContent->GetStringField(TEXT("code")) : FString();
		return FJsonRpcResponse::MakeError(Request.Id, Code == TEXT("not_found") ? -32002 : FJsonRpcResponse::InternalError, R.Content.Num() ? R.Content[0].Text : TEXT("task request failed"));
	}
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("task"), R.StructuredContent);
	return FJsonRpcResponse::MakeResult(Request.Id, Result);
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandleToolsGetSchema(const FJsonRpcRequest& Request)
{
	if (!Request.Params.IsValid())
	{
		return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidParams, TEXT("Missing params"));
	}

	FString ToolName;
	if (!Request.Params->TryGetStringField(TEXT("name"), ToolName))
	{
		return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidParams, TEXT("Missing tool name"));
	}

	const FMCPToolDefinition* Tool = FMCPToolRegistry::Get().FindTool(ToolName);
	if (!Tool)
	{
		return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidParams,
			FString::Printf(TEXT("Tool not found: %s"), *ToolName));
	}

	// Return full tool definition with complete schema (including descriptions)
	return FJsonRpcResponse::MakeResult(Request.Id, Tool->ToJson());
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandleResourcesList(const FJsonRpcRequest& Request)
{
	TArray<FMCPResourceDefinition> AllResources = FMCPResourceProvider::Get().GetAllResources();

	TArray<TSharedPtr<FJsonValue>> ResourcesArray;
	for (const FMCPResourceDefinition& Res : AllResources)
	{
		ResourcesArray.Add(MakeShared<FJsonValueObject>(Res.ToJson()));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("resources"), ResourcesArray);

	return FJsonRpcResponse::MakeResult(Request.Id, Result);
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandleResourcesTemplatesList(const FJsonRpcRequest& Request)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FMCPResourceDefinition& Res : FMCPResourceProvider::Get().GetAllTemplates())
		Arr.Add(MakeShared<FJsonValueObject>(Res.ToJson()));
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("resourceTemplates"), Arr);
	return FJsonRpcResponse::MakeResult(Request.Id, Result);
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandleResourcesRead(const FJsonRpcRequest& Request, const FMCPRequestContext& Context)
{
	if (!Request.Params.IsValid())
	{
		return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidParams, TEXT("Missing params"));
	}

	FString Uri;
	if (!Request.Params->TryGetStringField(TEXT("uri"), Uri))
	{
		return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidParams, TEXT("Missing resource URI"));
	}

	FMCPResourceContent Content = FMCPResourceProvider::Get().ReadResource(Uri, Context);
	if (Content.bIsError)
	{
		// MCP: -32002 Resource not found. Owned resources of another caller are indistinguishable from missing ones.
		return FJsonRpcResponse::MakeError(Request.Id, -32002, Content.Error.IsEmpty() ? TEXT("Resource not found") : Content.Error);
	}

	TArray<TSharedPtr<FJsonValue>> ContentsArray;
	ContentsArray.Add(MakeShared<FJsonValueObject>(Content.ToJson()));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("contents"), ContentsArray);

	return FJsonRpcResponse::MakeResult(Request.Id, Result);
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandlePromptsList(const FJsonRpcRequest& Request)
{
	TArray<FMCPPromptDefinition> AllPrompts = FMCPPromptProvider::Get().GetAllPrompts();

	TArray<TSharedPtr<FJsonValue>> PromptsArray;
	for (const FMCPPromptDefinition& Prompt : AllPrompts)
	{
		PromptsArray.Add(MakeShared<FJsonValueObject>(Prompt.ToJson()));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("prompts"), PromptsArray);

	return FJsonRpcResponse::MakeResult(Request.Id, Result);
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandlePromptsGet(const FJsonRpcRequest& Request)
{
	if (!Request.Params.IsValid())
	{
		return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidParams, TEXT("Missing params"));
	}

	FString PromptName;
	if (!Request.Params->TryGetStringField(TEXT("name"), PromptName))
	{
		return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidParams, TEXT("Missing prompt name"));
	}

	// Parse arguments
	TMap<FString, FString> Arguments;
	if (Request.Params->HasField(TEXT("arguments")))
	{
		TSharedPtr<FJsonObject> ArgsObj = Request.Params->GetObjectField(TEXT("arguments"));
		for (const auto& Pair : ArgsObj->Values)
		{
			FString Value;
			if (Pair.Value->TryGetString(Value))
			{
				// v4.5 (5.8): FJsonObject keys are UE::FSharedString; deref to TCHAR*.
				Arguments.Add(FString(*Pair.Key), Value);
			}
		}
	}

	TArray<FMCPPromptMessage> Messages = FMCPPromptProvider::Get().GetPrompt(PromptName, Arguments);

	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	for (const FMCPPromptMessage& Msg : Messages)
	{
		MessagesArray.Add(MakeShared<FJsonValueObject>(Msg.ToJson()));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("messages"), MessagesArray);

	return FJsonRpcResponse::MakeResult(Request.Id, Result);
}

// ============================================================================
// Utility
// ============================================================================

void FMCPHttpServer::AddCorsHeaders(TUniquePtr<FHttpServerResponse>& Response, const FString& AllowedOrigin)
{
	// Echo back the validated Origin only. If AllowedOrigin is empty (no Origin
	// header on the request, or rejected), omit Access-Control-Allow-Origin so
	// browsers block the response.
	if (!AllowedOrigin.IsEmpty())
	{
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { AllowedOrigin });
	}
	Response->Headers.Add(TEXT("Vary"), { TEXT("Origin") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, DELETE, OPTIONS") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type, Mcp-Session-Id, Mcp-Protocol-Version, Authorization") });
	Response->Headers.Add(TEXT("Access-Control-Expose-Headers"), { TEXT("Mcp-Session-Id") });
}

FString FMCPHttpServer::GenerateSessionId()
{
	return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
}

FMCPHttpServer::FServerStats FMCPHttpServer::GetStats() const
{
	FServerStats Stats;
	Stats.TotalRequests = StatTotalRequests.Load(EMemoryOrder::Relaxed);
	Stats.TotalToolCalls = StatTotalToolCalls.Load(EMemoryOrder::Relaxed);
	Stats.UptimeSeconds = bIsRunning ? (FPlatformTime::Seconds() - ServerStartTime) : 0.0;
	{
		FScopeLock StatsScope(&StatsLock);
		Stats.LastToolName = StatLastToolName;
		Stats.SecondsSinceLastToolCall = (StatLastToolCallTime > 0.0)
			? (FPlatformTime::Seconds() - StatLastToolCallTime) : -1.0;
	}
	{
		FScopeLock Lock(&SessionLock);
		Stats.ActiveSessions = SessionLastActivity.Num();
	}
	return Stats;
}

void FMCPHttpServer::TouchSession(const FString& SessionId)
{
	if (SessionId.IsEmpty())
	{
		return;
	}
	FScopeLock Lock(&SessionLock);
	if (double* LastActivity = SessionLastActivity.Find(SessionId))
	{
		*LastActivity = FPlatformTime::Seconds();
	}
}

bool FMCPHttpServer::SweepStaleSessions(float /*DeltaTime*/)
{
	const UMCPSettings* Settings = UMCPSettings::Get();
	const double IdleLimitSeconds = (double)FMath::Clamp(Settings->SessionIdleTimeoutMinutes, 5, 1440) * 60.0;
	const double Now = FPlatformTime::Seconds();

	TArray<FString> StaleSessions;
	{
		FScopeLock Lock(&SessionLock);
		for (const auto& Pair : SessionLastActivity)
		{
			if (Now - Pair.Value > IdleLimitSeconds)
			{
				StaleSessions.Add(Pair.Key);
			}
		}

	}

	for (const FString& Sid : StaleSessions)
	{
		DropSession(Sid);

		UE_LOG(LogUnrealMCP, Log, TEXT("MCP session expired after %d min idle: %s"),
			Settings->SessionIdleTimeoutMinutes, *Sid);
	}

	return true; // keep ticking
}

bool FMCPHttpServer::CheckRateLimit(const FString& ClientId)
{
	FScopeLock Lock(&RateLimitLock);

	const UMCPSettings* Settings = UMCPSettings::Get();
	double Now = FPlatformTime::Seconds();
	double WindowStart = Now - 60.0; // 1 minute window

	TArray<double>& Timestamps = RequestTimestamps.FindOrAdd(ClientId);

	// Remove old timestamps
	Timestamps.RemoveAll([WindowStart](double T) { return T < WindowStart; });

	if (Timestamps.Num() >= Settings->MaxRequestsPerMinute)
	{
		UE_LOG(LogUnrealMCP, Warning, TEXT("Rate limit exceeded for client: %s"), *ClientId);
		return false;
	}

	Timestamps.Add(Now);
	return true;
}

// ============================================================================
// Cancellation registry (Phase B.2 / B5-min)
// ============================================================================

TSharedPtr<TAtomic<bool>> FMCPHttpServer::RegisterCancelFlag(const FString& RequestId, const FString& SessionId)
{
	TSharedPtr<TAtomic<bool>> Flag = MakeShared<TAtomic<bool>>(false);
	FScopeLock Lock(&CancelFlagsLock);
	if (InFlightCancelFlags.Contains(RequestId)) return nullptr;
	InFlightCancelFlags.Add(RequestId, Flag);
	CancelSessionIds.Add(RequestId, SessionId);
	return Flag;
}

void FMCPHttpServer::UnregisterCancelFlag(const FString& RequestId)
{
	FScopeLock Lock(&CancelFlagsLock);
	InFlightCancelFlags.Remove(RequestId);
	CancelSessionIds.Remove(RequestId);
}

bool FMCPHttpServer::MarkCancelled(const FString& RequestId)
{
	FScopeLock Lock(&CancelFlagsLock);
	if (TSharedPtr<TAtomic<bool>>* Found = InFlightCancelFlags.Find(RequestId))
	{
		(*Found)->Store(true, EMemoryOrder::Relaxed);
		UE_LOG(LogUnrealMCP, Log, TEXT("Cancellation flagged for request: %s"), *RequestId);
		return true;
	}
	return false;
}

void FMCPHttpServer::HandleCancelledNotification(const FJsonRpcRequest& Request, const FMCPRequestContext& Context)
{
	if (!Request.Params.IsValid())
	{
		UE_LOG(LogUnrealMCP, Warning, TEXT("notifications/cancelled missing params"));
		return;
	}

	// Per spec: params.requestId can be a string or number. We compare as serialized JSON
	// against what was stored at register-time, which keeps both shapes round-trip safe.
	TSharedPtr<FJsonValue> RequestIdValue;
	if (Request.Params->HasField(TEXT("requestId")))
	{
		RequestIdValue = Request.Params->TryGetField(TEXT("requestId"));
	}
	if (!RequestIdValue.IsValid())
	{
		UE_LOG(LogUnrealMCP, Warning, TEXT("notifications/cancelled missing requestId"));
		return;
	}

	const FString RequestIdStr = CancellationKey(Context, RequestIdValue);
	if (!MarkCancelled(RequestIdStr))
	{
		UE_LOG(LogUnrealMCP, Verbose, TEXT("notifications/cancelled for unknown/finished request: %s"), *RequestIdStr);
	}
}

// ============================================================================
// Transactions (Phase C / C4)
// ============================================================================

TSharedPtr<FJsonObject> FMCPHttpServer::HandleTransactionsBegin(const FJsonRpcRequest& Request, const FMCPRequestContext& Context)
{
    return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidRequest,
        TEXT("Multi-request editor transactions are disabled pending verified ownership and recovery. Use bounded tool scripts; they do not promise automatic rollback."));
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandleTransactionsCommit(const FJsonRpcRequest& Request, const FMCPRequestContext& Context)
{
    return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidRequest,
        TEXT("Multi-request editor transactions are disabled pending verified ownership and recovery. Use bounded tool scripts; they do not promise automatic rollback."));
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandleTransactionsRollback(const FJsonRpcRequest& Request, const FMCPRequestContext& Context)
{
    return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidRequest,
        TEXT("Automatic transaction rollback is unsupported. No undo was attempted."));
}

// ============================================================================
// Working Set (Phase C / C5)
// ============================================================================

TSharedPtr<FJsonObject> FMCPHttpServer::HandleWorkingSetGet(const FJsonRpcRequest& Request, const FMCPRequestContext& Context)
{
	const FMCPWorkingSet WS = FMCPWorkingSetStore::Get().GetCopy(Context.SessionId);
	return FJsonRpcResponse::MakeResult(Request.Id, WS.ToJson());
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandleWorkingSetSet(const FJsonRpcRequest& Request, const FMCPRequestContext& Context)
{
	if (!Request.Params.IsValid())
	{
		return FJsonRpcResponse::MakeError(Request.Id, FJsonRpcResponse::InvalidParams, TEXT("Missing params"));
	}
	const FString& Sid = Context.SessionId;
	const FMCPWorkingSet WS = FMCPWorkingSetStore::Get().ApplyJson(Sid, Request.Params);
	UE_LOG(LogUnrealMCP, Log, TEXT("WorkingSet set (session=%s, selection=%d, bp='%s')"),
		*Sid, WS.Selection.Num(), *WS.CurrentBlueprintPath);
	return FJsonRpcResponse::MakeResult(Request.Id, WS.ToJson());
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandleWorkingSetClear(const FJsonRpcRequest& Request, const FMCPRequestContext& Context)
{
	FMCPWorkingSetStore::Get().Clear(Context.SessionId);
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("cleared"), true);
	return FJsonRpcResponse::MakeResult(Request.Id, Result);
}

FString FMCPHttpServer::CancellationKey(const FMCPRequestContext& Context, const TSharedPtr<FJsonValue>& Id)
{
    if (!Id.IsValid() || (Id->Type != EJson::String && Id->Type != EJson::Number)) return FString();
    auto Key = MakeShared<FJsonObject>();
    Key->SetStringField(TEXT("principal"), Context.PrincipalId);
    Key->SetStringField(TEXT("session"), Context.SessionId);
    Key->SetField(TEXT("id"), Id);
    return JsonToString(Key);
}

bool FMCPHttpServer::RejectInvalidSession(const FString& SessionId, const FMCPAuthResult& Auth, const FHttpResultCallback& OnComplete)
{
    int32 Status = 0;
    bool bExpired = false;
    {
        FScopeLock Lock(&SessionLock);
        const FString* Owner = SessionPrincipals.Find(SessionId);
        const double* Last = SessionLastActivity.Find(SessionId);
        if (SessionId.IsEmpty()) Status = 400;
        else if (!Owner || !Last) Status = 404;
        else if (*Owner != Auth.PrincipalId) Status = 403;
        else if (FPlatformTime::Seconds() - *Last > FMath::Clamp(UMCPSettings::Get()->SessionIdleTimeoutMinutes, 5, 1440) * 60.0)
        { Status = 404; bExpired = true; }
        else SessionLastActivity[SessionId] = FPlatformTime::Seconds();
    }
    if (bExpired) DropSession(SessionId);
    if (!Status) return false;
    auto Response = CreateHttpResponse(TEXT("{\"error\":\"Missing, unknown, expired or unowned MCP session\"}"), TEXT("application/json"));
    Response->Code = (EHttpServerResponseCodes)Status;
    AddCorsHeaders(Response, Auth.ResolvedOrigin);
    OnComplete(MoveTemp(Response));
    return true;
}

void FMCPHttpServer::DropSession(const FString& SessionId)
{
    {
        FScopeLock Lock(&SessionLock);
        SessionLastActivity.Remove(SessionId);
        SessionPrincipals.Remove(SessionId);
    }
    {
        FScopeLock Lock(&CancelFlagsLock);
        for (auto It = CancelSessionIds.CreateIterator(); It; ++It)
            if (It.Value() == SessionId)
            {
                if (auto* Flag = InFlightCancelFlags.Find(It.Key())) (*Flag)->Store(true);
                InFlightCancelFlags.Remove(It.Key());
                It.RemoveCurrent();
            }
    }
    CloseSessionStream(SessionId);
    FMCPTaskManager::Get().CancelSessionTasks(SessionId);
    FMCPWorkingSetStore::Get().Clear(SessionId);
    MCPActorChangePlans::ClearSession(SessionId);
    MCPEditorSurfaces::ClearSession(SessionId);
    MCPSnapshots::ClearSession(SessionId);
    MCPScenarios::ClearSession(SessionId);
    MCPDiagnostics::ClearSession(SessionId);
    MCPResultStore::ClearSession(SessionId);
    FMCPTransactionManager::Get().AbandonIfOpen(SessionId);
}
