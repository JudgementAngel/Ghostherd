// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Lightweight in-memory search index for project assets, Blueprint functions,
 * actor labels, and variables. Supports fuzzy/partial matching via tokenized
 * CamelCase splitting and Levenshtein distance scoring.
 *
 * Rebuilds automatically on AssetRegistry changes. Zero external dependencies.
 */

struct FMCPSearchResult
{
	FString Name;
	FString Path;
	FString Type;         // "Asset", "Function", "Variable", "Actor", "Widget"
	FString Category;     // Asset class, owning class, etc.
	float Score;          // 0.0 = perfect match, higher = worse
	FString Description;  // Optional extra context
};

class UNREALMCPSERVER_API FMCPSearchIndex
{
public:
	static FMCPSearchIndex& Get();

	/** Build/rebuild the entire index from current project state */
	void Build();

	/** Search the index with a query string */
	TArray<FMCPSearchResult> Search(const FString& Query, const FString& CategoryFilter = TEXT(""), int32 Limit = 20) const;

	/** Get index stats */
	int32 GetEntryCount() const { return Entries.Num(); }
	FDateTime GetLastBuildTime() const { return LastBuildTime; }

	/** Suggest closest matches for a given name (for "Did you mean...?" errors) */
	TArray<FString> SuggestSimilar(const FString& Name, const FString& CategoryFilter = TEXT(""), int32 Limit = 5) const;

	/** Levenshtein distance between two strings (public for use by MCPErrorHelpers) */
	static int32 LevenshteinDistance(const FString& A, const FString& B);

private:
	FMCPSearchIndex() = default;

	struct FIndexEntry
	{
		FString Name;
		FString Path;
		FString Type;
		FString Category;
		FString Description;
		TArray<FString> Tokens;  // Tokenized name for matching
	};

	void IndexAssets();
	void IndexBlueprintContents();
	void IndexActors();

	/** Tokenize a name: split CamelCase, underscores, dashes → lowercase tokens */
	static TArray<FString> Tokenize(const FString& Name);

	/** Score a query against an entry (lower = better match) */
	static float ScoreMatch(const TArray<FString>& QueryTokens, const FIndexEntry& Entry);

	TArray<FIndexEntry> Entries;
	FDateTime LastBuildTime;
	mutable FCriticalSection IndexLock;
};
