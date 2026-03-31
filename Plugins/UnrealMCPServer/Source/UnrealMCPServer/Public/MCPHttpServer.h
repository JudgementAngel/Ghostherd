#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpRouteHandle.h"

class UNREALMCPSERVER_API FMCPHttpServer
{
public:
	static FMCPHttpServer& Get();

	bool Start(int32 Port);
	void Stop();
	bool IsRunning() const { return bIsRunning; }
	int32 GetPort() const { return CurrentPort; }

	DECLARE_MULTICAST_DELEGATE(FOnServerStarted);
	DECLARE_MULTICAST_DELEGATE(FOnServerStopped);
	FOnServerStarted OnServerStarted;
	FOnServerStopped OnServerStopped;

private:
	FMCPHttpServer() = default;

	// Streamable HTTP transport (MCP spec 2025-03-26)
	bool HandleMCPPost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMCPGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMCPDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMCPOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// Legacy SSE transport (MCP spec 2024-11-05)
	bool HandleSSEConnect(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSSEMessage(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// JSON-RPC dispatch
	TSharedPtr<FJsonObject> HandleJsonRpcRequest(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandleInitialize(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandleToolsList(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandleToolsCall(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandleResourcesList(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandleResourcesRead(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandlePromptsList(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandlePromptsGet(const FJsonRpcRequest& Request);
	TSharedPtr<FJsonObject> HandlePing(const FJsonRpcRequest& Request);

	// CORS headers
	void AddCorsHeaders(TUniquePtr<FHttpServerResponse>& Response);

	// Session management
	FString GenerateSessionId();

	// Rate limiting
	bool CheckRateLimit(const FString& ClientId);

	bool bIsRunning = false;
	int32 CurrentPort = 0;
	TArray<FHttpRouteHandle> RouteHandles;

	// Session tracking
	TMap<FString, double> SessionLastActivity;
	mutable FCriticalSection SessionLock;

	// Rate limiting
	TMap<FString, TArray<double>> RequestTimestamps;
	mutable FCriticalSection RateLimitLock;
};
