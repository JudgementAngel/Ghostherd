// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Shared error helper utilities for MCP tools.
 * Provides "Did you mean...?" suggestions using the search index.
 */
namespace MCPErrorHelpers
{
	/** "Asset not found: X. Did you mean: Y?" */
	FString AssetNotFoundError(const FString& AssetPath);

	/** "Actor not found: X. Did you mean: Y?" */
	FString ActorNotFoundError(const FString& ActorLabel);

	/** "Function X not found on class Y. Similar: Z" */
	FString FunctionNotFoundError(const FString& FuncName, UClass* Class);
}
