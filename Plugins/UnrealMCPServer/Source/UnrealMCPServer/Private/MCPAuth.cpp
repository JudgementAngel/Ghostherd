// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPAuth.h"
#include "MCPSettings.h"
#include "HttpServerRequest.h"
#include "Misc/ScopeLock.h"
#include "Misc/Guid.h"

FMCPAuthResult FMCPAuth::Authenticate(const FHttpServerRequest& Request)
{
	FMCPAuthResult Result;
	const UMCPSettings* Settings = UMCPSettings::Get();

	// --- Host validation (v4 Phase 1 — DNS-rebinding defense) ---
	// A malicious page can point attacker.example's DNS at 127.0.0.1 and have a
	// non-browser fetch hit us with Host: attacker.example. Browsers send Origin
	// (checked below), but plain HTTP clients only send Host — so require it to
	// be a loopback name. Port suffix is ignored. Skipped when the server is
	// deliberately bound non-loopback (remote clients legitimately send the
	// machine's address; mandatory bearer-token auth carries security there).
	const FString ConfiguredBind = Settings->BindAddress.TrimStartAndEnd();
	const bool bBoundToLoopback = ConfiguredBind.IsEmpty()
		|| ConfiguredBind == TEXT("127.0.0.1")
		|| ConfiguredBind.Equals(TEXT("localhost"), ESearchCase::IgnoreCase)
        || ConfiguredBind == TEXT("::1")
        || !Settings->bRequireAuthToken || Settings->AuthTokens.IsEmpty();
	const TArray<FString>* HostHeader = Request.Headers.Find(TEXT("host"));
	if (bBoundToLoopback)
	{
        bool bValidHost = HostHeader && HostHeader->Num() == 1;
        FString Host = bValidHost ? (*HostHeader)[0] : FString();
        FString Name, Port;
        if (Host.StartsWith(TEXT("[")))
        {
            int32 End = INDEX_NONE;
            if (Host.FindChar(TEXT(']'), End))
            {
                Name = Host.Left(End + 1);
                const FString Suffix = Host.Mid(End + 1);
                if (!Suffix.IsEmpty())
                {
                    bValidHost &= Suffix.StartsWith(TEXT(":"));
                    Port = Suffix.Mid(1);
                    bValidHost &= !Port.IsEmpty();
                }
            }
            else bValidHost = false;
        }
        else if (!Host.Split(TEXT(":"), &Name, &Port)) Name = Host;
        else bValidHost &= !Port.IsEmpty();
        for (TCHAR C : Port) bValidHost &= C >= TEXT('0') && C <= TEXT('9');
        if (!Port.IsEmpty()) bValidHost &= Port.Len() <= 5 && FCString::Atoi(*Port) > 0 && FCString::Atoi(*Port) <= 65535;
        bValidHost &= Name.Equals(TEXT("localhost"), ESearchCase::IgnoreCase)
            || Name == TEXT("127.0.0.1") || Name == TEXT("[::1]");
        if (!bValidHost)
        {
            Result.Outcome = FMCPAuthResult::OriginRejected;
            Result.ErrorMessage = TEXT("A single valid loopback Host header is required");
            return Result;
        }
	}

	// --- Origin validation (applies regardless of auth requirement) ---
	const TArray<FString>* OriginHeader = Request.Headers.Find(TEXT("origin"));
	if (OriginHeader && (OriginHeader->Num() != 1 || (*OriginHeader)[0].IsEmpty()))
    {
        Result.Outcome = FMCPAuthResult::OriginRejected;
        Result.ErrorMessage = TEXT("Invalid Origin header");
        return Result;
    }
    if (OriginHeader && OriginHeader->Num() == 1)
	{
		const FString& Origin = (*OriginHeader)[0];
		bool bMatched = false;
		for (const FString& Allowed : Settings->AllowedOrigins)
		{
			if (Origin.Equals(Allowed, ESearchCase::IgnoreCase))
			{
				bMatched = true;
				break;
			}
		}
		if (!bMatched)
		{
			Result.Outcome = FMCPAuthResult::OriginRejected;
			Result.ErrorMessage = FString::Printf(TEXT("Origin '%s' not in allow-list"), *Origin);
			// ResolvedOrigin stays empty so ACAO header is omitted on the rejection response.
			return Result;
		}
		Result.ResolvedOrigin = Origin;
	}
	// else: no Origin header; non-browser client, allowed to proceed.

	// --- Bearer-token authentication ---
	const EMCPScope GrantedScope = Settings->bAllowDestructiveScope ? EMCPScope::Destructive : EMCPScope::Scene;

	if (!Settings->bRequireAuthToken)
	{
		Result.Outcome = FMCPAuthResult::Allowed;
		Result.Scope = GrantedScope;
		Result.PrincipalId = TEXT("local-anonymous");
		return Result;
	}

	const TArray<FString>* AuthHeader = Request.Headers.Find(TEXT("authorization"));
	if (!AuthHeader || AuthHeader->Num() != 1 || (*AuthHeader)[0].IsEmpty())
	{
		Result.Outcome = FMCPAuthResult::Unauthorized;
		Result.ErrorMessage = TEXT("Missing Authorization header");
		return Result;
	}

	const FString& HeaderValue = (*AuthHeader)[0];
	static const TCHAR* BearerPrefix = TEXT("Bearer ");
	const int32 BearerPrefixLen = 7;
	if (HeaderValue.Len() <= BearerPrefixLen ||
		FCString::Strnicmp(*HeaderValue, BearerPrefix, BearerPrefixLen) != 0)
	{
		Result.Outcome = FMCPAuthResult::Unauthorized;
		Result.ErrorMessage = TEXT("Authorization header is not a Bearer token");
		return Result;
	}

	const FString Token = HeaderValue.Mid(BearerPrefixLen).TrimStartAndEnd();
	if (Token.IsEmpty())
	{
		Result.Outcome = FMCPAuthResult::Unauthorized;
		Result.ErrorMessage = TEXT("Empty bearer token");
		return Result;
	}

	bool bTokenMatched = false;
	for (const FString& Allowed : Settings->AuthTokens)
	{
		// Verbatim, case-sensitive match.
		if (Token.Equals(Allowed, ESearchCase::CaseSensitive))
		{
			bTokenMatched = true;
			break;
		}
	}

	if (!bTokenMatched)
	{
		Result.Outcome = FMCPAuthResult::Unauthorized;
		Result.ErrorMessage = TEXT("Bearer token not recognized");
		return Result;
	}

	// v4.5 — per-token scopes. Default to the global grant; if this token has an
	// explicit entry in AuthTokenScopes, use it. Destructive is always capped by
	// the global bAllowDestructiveScope kill-switch.
	EMCPScope TokenScope = GrantedScope;
	if (const FString* ScopeStr = Settings->AuthTokenScopes.Find(Token))
	{
		const FString S = ScopeStr->TrimStartAndEnd().ToLower();
		if (S == TEXT("read"))             TokenScope = EMCPScope::Read;
		else if (S == TEXT("scene"))       TokenScope = EMCPScope::Scene;
		else if (S == TEXT("destructive")) TokenScope = EMCPScope::Destructive;
		else
        {
            Result.Outcome = FMCPAuthResult::Unauthorized;
            Result.ErrorMessage = TEXT("Invalid configured token scope");
            return Result;
        }
	}
	if (TokenScope == EMCPScope::Destructive && !Settings->bAllowDestructiveScope)
	{
		TokenScope = EMCPScope::Scene; // global kill-switch always caps
	}

	Result.Outcome = FMCPAuthResult::Allowed;
	Result.Scope = TokenScope;
    // Keep opaque identities stable while credentials remain configured. Only
    // recognized credentials can allocate entries; stale configuration is pruned.
    static FCriticalSection PrincipalLock;
    static TMap<FString, FString> Principals;
    FScopeLock Lock(&PrincipalLock);
    for (auto It = Principals.CreateIterator(); It; ++It)
        if (!Settings->AuthTokens.Contains(It.Key())) It.RemoveCurrent();
    FString& Principal = Principals.FindOrAdd(Token);
    if (Principal.IsEmpty()) Principal = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Result.PrincipalId = Principal;
	return Result;
}
