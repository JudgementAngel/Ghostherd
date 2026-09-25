// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPSearchIndex.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectIterator.h"
#include "Misc/Char.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMCPSearch, Log, All);
DEFINE_LOG_CATEGORY(LogMCPSearch);

FMCPSearchIndex& FMCPSearchIndex::Get()
{
	static FMCPSearchIndex Instance;
	return Instance;
}

TArray<FString> FMCPSearchIndex::Tokenize(const FString& Name)
{
	TArray<FString> Tokens;
	FString Current;

	for (int32 i = 0; i < Name.Len(); i++)
	{
		TCHAR C = Name[i];

		if (C == '_' || C == '-' || C == '/' || C == '.' || C == ' ')
		{
			if (Current.Len() > 0)
			{
				Tokens.Add(Current.ToLower());
				Current.Empty();
			}
			continue;
		}

		// Split on CamelCase: uppercase letter after lowercase
		if (FChar::IsUpper(C) && Current.Len() > 0 && FChar::IsLower(Name[i - 1]))
		{
			Tokens.Add(Current.ToLower());
			Current.Empty();
		}
		// Split on CamelCase: uppercase followed by lowercase after uppercase sequence (e.g., "HTMLParser" → "HTML", "Parser")
		if (FChar::IsUpper(C) && Current.Len() > 1 && FChar::IsUpper(Name[i - 1]) && i + 1 < Name.Len() && FChar::IsLower(Name[i + 1]))
		{
			Tokens.Add(Current.ToLower());
			Current.Empty();
		}

		Current.AppendChar(C);
	}

	if (Current.Len() > 0)
	{
		Tokens.Add(Current.ToLower());
	}

	return Tokens;
}

int32 FMCPSearchIndex::LevenshteinDistance(const FString& A, const FString& B)
{
	int32 LenA = A.Len();
	int32 LenB = B.Len();

	if (LenA == 0) return LenB;
	if (LenB == 0) return LenA;

	// Use single-row optimization
	TArray<int32> Row;
	Row.SetNum(LenB + 1);
	for (int32 j = 0; j <= LenB; j++) Row[j] = j;

	for (int32 i = 1; i <= LenA; i++)
	{
		int32 Prev = i - 1;
		Row[0] = i;

		for (int32 j = 1; j <= LenB; j++)
		{
			int32 Temp = Row[j];
			if (FChar::ToLower(A[i - 1]) == FChar::ToLower(B[j - 1]))
			{
				Row[j] = Prev;
			}
			else
			{
				Row[j] = FMath::Min3(Row[j] + 1, Row[j - 1] + 1, Prev + 1);
			}
			Prev = Temp;
		}
	}

	return Row[LenB];
}

float FMCPSearchIndex::ScoreMatch(const TArray<FString>& QueryTokens, const FIndexEntry& Entry)
{
	if (QueryTokens.Num() == 0) return 100.0f;

	float TotalScore = 0.0f;
	int32 MatchedTokens = 0;

	for (const FString& QToken : QueryTokens)
	{
		float BestTokenScore = 100.0f;

		// Check against entry tokens
		for (const FString& EToken : Entry.Tokens)
		{
			// Exact match
			if (EToken == QToken)
			{
				BestTokenScore = 0.0f;
				break;
			}

			// Prefix match (e.g., "print" matches "printstring")
			if (EToken.StartsWith(QToken) || QToken.StartsWith(EToken))
			{
				float PrefixScore = 0.5f;
				BestTokenScore = FMath::Min(BestTokenScore, PrefixScore);
				continue;
			}

			// Contains match
			if (EToken.Contains(QToken) || QToken.Contains(EToken))
			{
				float ContainsScore = 1.0f;
				BestTokenScore = FMath::Min(BestTokenScore, ContainsScore);
				continue;
			}

			// Levenshtein fuzzy match (only for tokens of similar length)
			if (FMath::Abs(EToken.Len() - QToken.Len()) <= 3)
			{
				int32 Dist = LevenshteinDistance(QToken, EToken);
				int32 MaxLen = FMath::Max(QToken.Len(), EToken.Len());
				if (MaxLen > 0 && Dist <= MaxLen / 2) // Only consider if within 50% edit distance
				{
					float FuzzyScore = 2.0f + (float)Dist / (float)MaxLen;
					BestTokenScore = FMath::Min(BestTokenScore, FuzzyScore);
				}
			}
		}

		// Also check against the full name (case-insensitive substring)
		FString LowerName = Entry.Name.ToLower();
		FString LowerQuery = QToken.ToLower();
		if (LowerName.Contains(LowerQuery))
		{
			BestTokenScore = FMath::Min(BestTokenScore, 0.3f);
		}

		if (BestTokenScore < 50.0f)
		{
			MatchedTokens++;
		}
		TotalScore += BestTokenScore;
	}

	// Penalty for unmatched query tokens
	float MatchRatio = (float)MatchedTokens / (float)QueryTokens.Num();
	if (MatchRatio < 1.0f)
	{
		TotalScore += (1.0f - MatchRatio) * 50.0f;
	}

	return TotalScore;
}

void FMCPSearchIndex::IndexAssets()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AllAssets;
	AssetRegistry.GetAssetsByPath(FName(TEXT("/Game")), AllAssets, true);

	for (const FAssetData& Asset : AllAssets)
	{
		FIndexEntry Entry;
		Entry.Name = Asset.AssetName.ToString();
		Entry.Path = Asset.GetObjectPathString();
		Entry.Type = TEXT("Asset");
		Entry.Category = Asset.AssetClassPath.GetAssetName().ToString();
		Entry.Description = FString::Printf(TEXT("%s (%s)"), *Entry.Name, *Entry.Category);
		Entry.Tokens = Tokenize(Entry.Name);

		// Add category tokens too
		TArray<FString> CategoryTokens = Tokenize(Entry.Category);
		Entry.Tokens.Append(CategoryTokens);

		Entries.Add(MoveTemp(Entry));
	}
}

void FMCPSearchIndex::IndexBlueprintContents()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Find all Blueprint assets
	TArray<FAssetData> BlueprintAssets;
	FTopLevelAssetPath BPClassPath(TEXT("/Script/Engine"), TEXT("Blueprint"));
	AssetRegistry.GetAssetsByClass(BPClassPath, BlueprintAssets, true);

	// Also get Widget Blueprints
	FTopLevelAssetPath WBPClassPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint"));
	TArray<FAssetData> WidgetBPAssets;
	AssetRegistry.GetAssetsByClass(WBPClassPath, WidgetBPAssets, true);
	BlueprintAssets.Append(WidgetBPAssets);

	for (const FAssetData& BPAsset : BlueprintAssets)
	{
		if (!BPAsset.PackageName.ToString().StartsWith(TEXT("/Game"))) continue;

		// Try to load the Blueprint to extract functions and variables
		UBlueprint* BP = Cast<UBlueprint>(BPAsset.GetAsset());
		if (!BP) continue;

		FString BPName = BP->GetName();
		FString BPPath = BPAsset.GetObjectPathString();

		// Index functions
		for (UEdGraph* Graph : BP->FunctionGraphs)
		{
			if (!Graph) continue;
			FIndexEntry Entry;
			Entry.Name = Graph->GetName();
			Entry.Path = BPPath;
			Entry.Type = TEXT("Function");
			Entry.Category = BPName;
			Entry.Description = FString::Printf(TEXT("Function '%s' in %s"), *Entry.Name, *BPName);
			Entry.Tokens = Tokenize(Entry.Name);
			Entries.Add(MoveTemp(Entry));
		}

		// Index variables
		for (const FBPVariableDescription& Var : BP->NewVariables)
		{
			FIndexEntry Entry;
			Entry.Name = Var.VarName.ToString();
			Entry.Path = BPPath;
			Entry.Type = TEXT("Variable");
			Entry.Category = BPName;
			Entry.Description = FString::Printf(TEXT("Variable '%s' (%s) in %s"),
				*Entry.Name, *Var.VarType.PinCategory.ToString(), *BPName);
			Entry.Tokens = Tokenize(Entry.Name);
			Entries.Add(MoveTemp(Entry));
		}
	}
}

void FMCPSearchIndex::IndexActors()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor)) continue;

		FIndexEntry Entry;
		Entry.Name = Actor->GetActorLabel();
		Entry.Path = Actor->GetPathName();
		Entry.Type = TEXT("Actor");
		Entry.Category = Actor->GetClass()->GetName();
		Entry.Description = FString::Printf(TEXT("%s (%s)"), *Entry.Name, *Entry.Category);
		Entry.Tokens = Tokenize(Entry.Name);

		// Add class tokens
		TArray<FString> ClassTokens = Tokenize(Entry.Category);
		Entry.Tokens.Append(ClassTokens);

		Entries.Add(MoveTemp(Entry));
	}
}

void FMCPSearchIndex::Build()
{
	FScopeLock Lock(&IndexLock);

	Entries.Empty();
	Entries.Reserve(5000);

	IndexAssets();
	IndexBlueprintContents();
	IndexActors();

	LastBuildTime = FDateTime::Now();

	UE_LOG(LogMCPSearch, Log, TEXT("Search index built: %d entries"), Entries.Num());
}

TArray<FMCPSearchResult> FMCPSearchIndex::Search(const FString& Query, const FString& CategoryFilter, int32 Limit) const
{
	FScopeLock Lock(&IndexLock);

	TArray<FString> QueryTokens = Tokenize(Query);

	// Also add the full query as a token for exact substring matching
	FString LowerQuery = Query.ToLower();

	TArray<TPair<float, int32>> ScoredResults;

	for (int32 i = 0; i < Entries.Num(); i++)
	{
		const FIndexEntry& Entry = Entries[i];

		// Apply category filter
		if (!CategoryFilter.IsEmpty() && CategoryFilter != TEXT("all"))
		{
			if (!Entry.Type.Equals(CategoryFilter, ESearchCase::IgnoreCase))
				continue;
		}

		float Score = ScoreMatch(QueryTokens, Entry);

		// Only include reasonable matches
		if (Score < 50.0f)
		{
			ScoredResults.Add(TPair<float, int32>(Score, i));
		}
	}

	// Sort by score (lower = better)
	ScoredResults.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B)
	{
		return A.Key < B.Key;
	});

	// Build results
	TArray<FMCPSearchResult> Results;
	int32 Count = FMath::Min(ScoredResults.Num(), Limit);
	for (int32 i = 0; i < Count; i++)
	{
		const FIndexEntry& Entry = Entries[ScoredResults[i].Value];
		FMCPSearchResult Result;
		Result.Name = Entry.Name;
		Result.Path = Entry.Path;
		Result.Type = Entry.Type;
		Result.Category = Entry.Category;
		Result.Score = ScoredResults[i].Key;
		Result.Description = Entry.Description;
		Results.Add(MoveTemp(Result));
	}

	return Results;
}

TArray<FString> FMCPSearchIndex::SuggestSimilar(const FString& Name, const FString& CategoryFilter, int32 Limit) const
{
	FScopeLock Lock(&IndexLock);

	TArray<TPair<int32, FString>> Candidates;

	for (const FIndexEntry& Entry : Entries)
	{
		if (!CategoryFilter.IsEmpty() && CategoryFilter != TEXT("all"))
		{
			if (!Entry.Type.Equals(CategoryFilter, ESearchCase::IgnoreCase))
				continue;
		}

		int32 Dist = LevenshteinDistance(Name, Entry.Name);
		// Only suggest if within reasonable edit distance
		if (Dist <= FMath::Max(3, Name.Len() / 2))
		{
			Candidates.Add(TPair<int32, FString>(Dist, Entry.Name));
		}
	}

	Candidates.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B)
	{
		return A.Key < B.Key;
	});

	TArray<FString> Suggestions;
	int32 Count = FMath::Min(Candidates.Num(), Limit);
	for (int32 i = 0; i < Count; i++)
	{
		Suggestions.Add(Candidates[i].Value);
	}

	return Suggestions;
}
