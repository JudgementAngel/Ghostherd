#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"

// Typedef to avoid macro comma issues with TMap<K,V> inside DECLARE_DELEGATE
typedef TMap<FString, FString> FMCPPromptArgMap;

DECLARE_DELEGATE_RetVal_OneParam(TArray<FMCPPromptMessage>, FMCPPromptGenerator, const FMCPPromptArgMap&);

struct FMCPPromptRegistration
{
	FMCPPromptDefinition Definition;
	FMCPPromptGenerator Generator;
};

class UNREALMCPSERVER_API FMCPPromptProvider
{
public:
	static FMCPPromptProvider& Get();

	void RegisterPrompt(const FMCPPromptDefinition& Definition, FMCPPromptGenerator Generator);
	void UnregisterPrompt(const FString& Name);
	void UnregisterAllPrompts();

	TArray<FMCPPromptDefinition> GetAllPrompts() const;
	TArray<FMCPPromptMessage> GetPrompt(const FString& Name, const TMap<FString, FString>& Arguments) const;
	bool HasPrompt(const FString& Name) const;

private:
	FMCPPromptProvider() = default;

	TMap<FString, FMCPPromptRegistration> Prompts;
	mutable FCriticalSection PromptsLock;
};
