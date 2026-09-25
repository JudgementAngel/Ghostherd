// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"

DECLARE_DELEGATE_RetVal_OneParam(FMCPResourceContent, FMCPResourceReader, const FString& /*Uri*/);
// v5 increment 15: context-aware reader so owned resources can check principal and session.
DECLARE_DELEGATE_RetVal_TwoParams(FMCPResourceContent, FMCPResourceReaderCtx, const FString& /*Uri*/, const FMCPRequestContext& /*Context*/);

struct FMCPResourceRegistration
{
	FMCPResourceDefinition Definition;
	FMCPResourceReader Reader;
	FMCPResourceReaderCtx ReaderCtx;
};

class UNREALMCPSERVER_API FMCPResourceProvider
{
public:
	static FMCPResourceProvider& Get();

	void RegisterResource(const FMCPResourceDefinition& Definition, FMCPResourceReader Reader);
	/** v5: register a concrete or template ({param}) resource whose reader receives the caller's context. */
	void RegisterResourceCtx(const FMCPResourceDefinition& Definition, FMCPResourceReaderCtx Reader);
	void UnregisterResource(const FString& Uri);
	void UnregisterAllResources();

	/** Concrete resources only (templates are listed by GetAllTemplates). */
	TArray<FMCPResourceDefinition> GetAllResources() const;
	TArray<FMCPResourceDefinition> GetAllTemplates() const;
	/** Legacy read with an internal Read-only context; owned resources refuse it. */
	FMCPResourceContent ReadResource(const FString& Uri) const;
	/** v5: exact match first, then the template whose prefix (before '{') matches; runs on the game thread. */
	FMCPResourceContent ReadResource(const FString& Uri, const FMCPRequestContext& Context) const;
	bool HasResource(const FString& Uri) const;

private:
	FMCPResourceProvider() = default;

	TMap<FString, FMCPResourceRegistration> Resources;
	mutable FCriticalSection ResourcesLock;
};
