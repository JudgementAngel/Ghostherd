#include "MCPToolRegistry.h"

FMCPToolRegistry& FMCPToolRegistry::Get()
{
	static FMCPToolRegistry Instance;
	return Instance;
}

void FMCPToolRegistry::RegisterTool(const FMCPToolDefinition& Tool)
{
	FScopeLock Lock(&ToolsLock);

	if (Tools.Contains(Tool.Name))
	{
		UE_LOG(LogUnrealMCP, Warning, TEXT("Tool '%s' already registered, overwriting"), *Tool.Name);
	}

	Tools.Add(Tool.Name, Tool);
	UE_LOG(LogUnrealMCP, Log, TEXT("Registered tool: %s"), *Tool.Name);
}

void FMCPToolRegistry::UnregisterTool(const FString& Name)
{
	FScopeLock Lock(&ToolsLock);
	Tools.Remove(Name);
	UE_LOG(LogUnrealMCP, Log, TEXT("Unregistered tool: %s"), *Name);
}

void FMCPToolRegistry::UnregisterAllTools()
{
	FScopeLock Lock(&ToolsLock);
	Tools.Empty();
}

const FMCPToolDefinition* FMCPToolRegistry::FindTool(const FString& Name) const
{
	FScopeLock Lock(&ToolsLock);
	return Tools.Find(Name);
}

TArray<FMCPToolDefinition> FMCPToolRegistry::GetAllTools() const
{
	FScopeLock Lock(&ToolsLock);
	TArray<FMCPToolDefinition> Result;
	Tools.GenerateValueArray(Result);
	return Result;
}

int32 FMCPToolRegistry::GetToolCount() const
{
	FScopeLock Lock(&ToolsLock);
	return Tools.Num();
}

FMCPToolResult FMCPToolRegistry::ExecuteTool(const FString& Name, const TSharedPtr<FJsonObject>& Arguments)
{
	const FMCPToolDefinition* Tool = FindTool(Name);
	if (!Tool)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Unknown tool: %s"), *Name));
	}

	if (!Tool->Handler.IsBound())
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Tool '%s' has no handler"), *Name));
	}

	// Execute on game thread to safely access UE APIs
	FMCPToolResult Result;
	if (IsInGameThread())
	{
		Result = Tool->Handler.Execute(Arguments);
	}
	else
	{
		FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool();
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			Result = Tool->Handler.Execute(Arguments);
			CompletionEvent->Trigger();
		});
		CompletionEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
	}

	return Result;
}
