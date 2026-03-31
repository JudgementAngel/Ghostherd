#include "MCPHttpServer.h"
#include "MCPToolRegistry.h"
#include "MCPResourceProvider.h"
#include "MCPPromptProvider.h"
#include "MCPSettings.h"
#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "HttpServerRequest.h"
#include "Misc/Guid.h"

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

	bIsRunning = true;
	CurrentPort = Port;

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

	{
		FScopeLock Lock(&SessionLock);
		SessionLastActivity.Empty();
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

	// Rate limiting
	if (!CheckRateLimit(TEXT("default")))
	{
		auto Response = FHttpServerResponse::Create(TEXT("{\"error\":\"Rate limit exceeded\"}"), TEXT("application/json"));
		Response->Code = EHttpServerResponseCodes::TooManyRequests;
		AddCorsHeaders(Response);
		OnComplete(MoveTemp(Response));
		return true;
	}

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
		auto Response = FHttpServerResponse::Create(JsonToString(ErrorResponse), TEXT("application/json"));
		AddCorsHeaders(Response);
		OnComplete(MoveTemp(Response));
		return true;
	}

	TSharedPtr<FJsonObject> JsonBody = StringToJson(Body);
	if (!JsonBody.IsValid())
	{
		UE_LOG(LogUnrealMCP, Error, TEXT("Failed to parse JSON body: %s"), *Body);
		auto ErrorResponse = FJsonRpcResponse::MakeError(nullptr, FJsonRpcResponse::ParseError, TEXT("Invalid JSON"));
		auto Response = FHttpServerResponse::Create(JsonToString(ErrorResponse), TEXT("application/json"));
		AddCorsHeaders(Response);
		OnComplete(MoveTemp(Response));
		return true;
	}

	FJsonRpcRequest RpcRequest;
	if (!RpcRequest.ParseFromJson(JsonBody))
	{
		auto ErrorResponse = FJsonRpcResponse::MakeError(nullptr, FJsonRpcResponse::InvalidRequest, TEXT("Invalid JSON-RPC request"));
		auto Response = FHttpServerResponse::Create(JsonToString(ErrorResponse), TEXT("application/json"));
		AddCorsHeaders(Response);
		OnComplete(MoveTemp(Response));
		return true;
	}

	// Handle notification (no response needed, but we still send 202)
	if (RpcRequest.IsNotification())
	{
		HandleJsonRpcRequest(RpcRequest); // Process but ignore result
		auto Response = FHttpServerResponse::Create(TEXT(""), TEXT("application/json"));
		Response->Code = EHttpServerResponseCodes::Accepted;
		AddCorsHeaders(Response);
		OnComplete(MoveTemp(Response));
		return true;
	}

	// Handle request
	TSharedPtr<FJsonObject> RpcResponse = HandleJsonRpcRequest(RpcRequest);
	FString ResponseStr = JsonToString(RpcResponse);

	if (Settings->bVerboseLogging)
	{
		UE_LOG(LogUnrealMCP, Verbose, TEXT("MCP response: %s"), *ResponseStr);
	}

	auto Response = FHttpServerResponse::Create(ResponseStr, TEXT("application/json"));
	Response->Code = EHttpServerResponseCodes::Ok;

	// Set session header
	FString SessionId = GenerateSessionId();
	Response->Headers.Add(TEXT("Mcp-Session-Id"), { SessionId });

	AddCorsHeaders(Response);
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMCPHttpServer::HandleMCPGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Streamable HTTP GET is for server-to-client SSE stream
	// For now, we respond with 405 since we handle everything synchronously via POST
	auto Response = FHttpServerResponse::Create(TEXT("Server-initiated notifications not supported yet"), TEXT("text/plain"));
	Response->Code = EHttpServerResponseCodes::Denied; // 405
	AddCorsHeaders(Response);
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMCPHttpServer::HandleMCPDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Session termination
	auto Response = FHttpServerResponse::Create(TEXT(""), TEXT("application/json"));
	Response->Code = EHttpServerResponseCodes::Ok;
	AddCorsHeaders(Response);
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMCPHttpServer::HandleMCPOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	auto Response = FHttpServerResponse::Create(TEXT(""), TEXT("text/plain"));
	Response->Code = EHttpServerResponseCodes::Ok;
	AddCorsHeaders(Response);
	OnComplete(MoveTemp(Response));
	return true;
}

// ============================================================================
// Legacy SSE Transport
// ============================================================================

bool FMCPHttpServer::HandleSSEConnect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Legacy SSE: return endpoint URL for message posting
	FString SessionId = GenerateSessionId();
	FString MessageUrl = FString::Printf(TEXT("http://localhost:%d/message?sessionId=%s"), CurrentPort, *SessionId);

	FString SseEvent = FString::Printf(TEXT("event: endpoint\ndata: %s\n\n"), *MessageUrl);

	auto Response = FHttpServerResponse::Create(SseEvent, TEXT("text/event-stream"));
	Response->Code = EHttpServerResponseCodes::Ok;
	Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache") });
	Response->Headers.Add(TEXT("Connection"), { TEXT("keep-alive") });
	AddCorsHeaders(Response);
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMCPHttpServer::HandleSSEMessage(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Same handling as Streamable HTTP POST
	return HandleMCPPost(Request, OnComplete);
}

// ============================================================================
// JSON-RPC Dispatch
// ============================================================================

TSharedPtr<FJsonObject> FMCPHttpServer::HandleJsonRpcRequest(const FJsonRpcRequest& Request)
{
	const FString& Method = Request.Method;

	if (Method == MCPProtocol::Methods::Initialize)
	{
		return HandleInitialize(Request);
	}
	else if (Method == MCPProtocol::Methods::Initialized)
	{
		// Notification, no response
		return nullptr;
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
		return HandleToolsCall(Request);
	}
	else if (Method == MCPProtocol::Methods::ResourcesList)
	{
		return HandleResourcesList(Request);
	}
	else if (Method == MCPProtocol::Methods::ResourcesRead)
	{
		return HandleResourcesRead(Request);
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
	TArray<FMCPToolDefinition> AllTools = FMCPToolRegistry::Get().GetAllTools();

	TArray<TSharedPtr<FJsonValue>> ToolsArray;
	for (const FMCPToolDefinition& Tool : AllTools)
	{
		ToolsArray.Add(MakeShared<FJsonValueObject>(Tool.ToJson()));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tools"), ToolsArray);

	return FJsonRpcResponse::MakeResult(Request.Id, Result);
}

TSharedPtr<FJsonObject> FMCPHttpServer::HandleToolsCall(const FJsonRpcRequest& Request)
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
		Arguments = Request.Params->GetObjectField(TEXT("arguments"));
	}
	else
	{
		Arguments = MakeShared<FJsonObject>();
	}

	UE_LOG(LogUnrealMCP, Log, TEXT("Executing tool: %s"), *ToolName);

	FMCPToolResult ToolResult = FMCPToolRegistry::Get().ExecuteTool(ToolName, Arguments);
	return FJsonRpcResponse::MakeResult(Request.Id, ToolResult.ToJson());
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

TSharedPtr<FJsonObject> FMCPHttpServer::HandleResourcesRead(const FJsonRpcRequest& Request)
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

	FMCPResourceContent Content = FMCPResourceProvider::Get().ReadResource(Uri);

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
				Arguments.Add(Pair.Key, Value);
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

void FMCPHttpServer::AddCorsHeaders(TUniquePtr<FHttpServerResponse>& Response)
{
	Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, DELETE, OPTIONS") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type, Mcp-Session-Id, Mcp-Protocol-Version") });
	Response->Headers.Add(TEXT("Access-Control-Expose-Headers"), { TEXT("Mcp-Session-Id") });
}

FString FMCPHttpServer::GenerateSessionId()
{
	return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
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
