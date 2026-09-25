// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"
#include "MCPSearchIndex.h"
#include "UObject/Object.h"

/**
 * v4 Phase 1 — unified asset loading with structured NotFound errors.
 *
 * Replaces the `LoadObject<T>() ... return Error("not found")` preamble that
 * each tool file hand-rolled with slightly different (and usually suggestion-
 * free) error messages. Failures carry EMCPError::NotFound plus did_you_mean
 * suggestions from the search index.
 */
namespace MCPCommon
{
	/** Load an asset by content path. On failure fills OutError (structured,
	 *  with fuzzy suggestions) and returns nullptr.
	 *
	 *  Usage:
	 *    FMCPToolResult Err;
	 *    UBlueprint* BP = MCPCommon::LoadAssetChecked<UBlueprint>(AssetPath, Err);
	 *    if (!BP) return Err;
	 */
	template <typename T>
	T* LoadAssetChecked(const FString& AssetPath, FMCPToolResult& OutError)
	{
		T* Asset = LoadObject<T>(nullptr, *AssetPath);
		if (Asset)
		{
			return Asset;
		}

		TArray<FString> Suggestions;
		const FString LeafName = FPackageName::GetShortName(AssetPath);
		if (!LeafName.IsEmpty())
		{
			Suggestions = FMCPSearchIndex::Get().SuggestSimilar(LeafName, TEXT(""), /*Limit*/ 3);
		}

		OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
			FString::Printf(TEXT("%s not found: %s"), *FString(T::StaticClass()->GetName()), *AssetPath),
			TEXT("Use list_assets or search_project to locate the asset; paths look like /Game/Folder/AssetName."),
			Suggestions);
		return nullptr;
	}
}
