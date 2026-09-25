// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MCPRequestContext.h"

struct FHttpServerRequest;

/** Outcome of authenticating a single HTTP request. */
struct FMCPAuthResult
{
	enum EOutcome { Allowed, Unauthorized, OriginRejected };

	EOutcome Outcome = Allowed;
	EMCPScope Scope = EMCPScope::Read;
	FString PrincipalId; // Opaque process-local identity; never the bearer credential.
	FString ResolvedOrigin;  // Validated Origin header value, or empty if no Origin header.
	FString ErrorMessage;    // Filled on Unauthorized / OriginRejected.
};

class UNREALMCPSERVER_API FMCPAuth
{
public:
	/** Inspect Authorization and Origin headers; consult settings; return outcome.
	 *  - When bRequireAuthToken=false: no auth required; Scope is Destructive iff
	 *    bAllowDestructiveScope=true, else Scene.
	 *  - When bRequireAuthToken=true: extract `Authorization: Bearer <token>`,
	 *    look up in settings.AuthTokens (verbatim string match). On match: Scope
	 *    is Destructive iff bAllowDestructiveScope=true, else Scene. On miss
	 *    or missing header: Outcome = Unauthorized.
	 *  - Origin: read Origin header. If empty: pass (typical for non-browser
	 *    clients). If non-empty: must be in settings.AllowedOrigins, else
	 *    Outcome = OriginRejected. */
	static FMCPAuthResult Authenticate(const FHttpServerRequest& Request);
};
