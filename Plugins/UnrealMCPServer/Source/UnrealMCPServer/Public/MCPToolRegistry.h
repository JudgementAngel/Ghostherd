#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"

class UNREALMCPSERVER_API FMCPToolRegistry
{
public:
	static FMCPToolRegistry& Get();

	void RegisterTool(const FMCPToolDefinition& Tool);
	void UnregisterTool(const FString& Name);
	void UnregisterAllTools();

	const FMCPToolDefinition* FindTool(const FString& Name) const;
	TArray<FMCPToolDefinition> GetAllTools() const;
	int32 GetToolCount() const;

	FMCPToolResult ExecuteTool(const FString& Name, const TSharedPtr<FJsonObject>& Arguments);

private:
	FMCPToolRegistry() = default;

	TMap<FString, FMCPToolDefinition> Tools;
	mutable FCriticalSection ToolsLock;
};
