#include "MCPPromptProvider.h"

FMCPPromptProvider& FMCPPromptProvider::Get()
{
	static FMCPPromptProvider Instance;
	return Instance;
}

void FMCPPromptProvider::RegisterPrompt(const FMCPPromptDefinition& Definition, FMCPPromptGenerator Generator)
{
	FScopeLock Lock(&PromptsLock);

	FMCPPromptRegistration Reg;
	Reg.Definition = Definition;
	Reg.Generator = Generator;
	Prompts.Add(Definition.Name, Reg);

	UE_LOG(LogUnrealMCP, Log, TEXT("Registered prompt: %s"), *Definition.Name);
}

void FMCPPromptProvider::UnregisterPrompt(const FString& Name)
{
	FScopeLock Lock(&PromptsLock);
	Prompts.Remove(Name);
}

void FMCPPromptProvider::UnregisterAllPrompts()
{
	FScopeLock Lock(&PromptsLock);
	Prompts.Empty();
}

TArray<FMCPPromptDefinition> FMCPPromptProvider::GetAllPrompts() const
{
	FScopeLock Lock(&PromptsLock);
	TArray<FMCPPromptDefinition> Result;
	for (const auto& Pair : Prompts)
	{
		Result.Add(Pair.Value.Definition);
	}
	return Result;
}

TArray<FMCPPromptMessage> FMCPPromptProvider::GetPrompt(const FString& Name, const TMap<FString, FString>& Arguments) const
{
	FScopeLock Lock(&PromptsLock);

	const FMCPPromptRegistration* Reg = Prompts.Find(Name);
	if (!Reg || !Reg->Generator.IsBound())
	{
		return {};
	}

	return Reg->Generator.Execute(Arguments);
}

bool FMCPPromptProvider::HasPrompt(const FString& Name) const
{
	FScopeLock Lock(&PromptsLock);
	return Prompts.Contains(Name);
}
