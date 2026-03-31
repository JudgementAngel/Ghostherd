#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"

DECLARE_DELEGATE_RetVal_OneParam(FMCPResourceContent, FMCPResourceReader, const FString& /*Uri*/);

struct FMCPResourceRegistration
{
	FMCPResourceDefinition Definition;
	FMCPResourceReader Reader;
};

class UNREALMCPSERVER_API FMCPResourceProvider
{
public:
	static FMCPResourceProvider& Get();

	void RegisterResource(const FMCPResourceDefinition& Definition, FMCPResourceReader Reader);
	void UnregisterResource(const FString& Uri);
	void UnregisterAllResources();

	TArray<FMCPResourceDefinition> GetAllResources() const;
	FMCPResourceContent ReadResource(const FString& Uri) const;
	bool HasResource(const FString& Uri) const;

private:
	FMCPResourceProvider() = default;

	TMap<FString, FMCPResourceRegistration> Resources;
	mutable FCriticalSection ResourcesLock;
};
