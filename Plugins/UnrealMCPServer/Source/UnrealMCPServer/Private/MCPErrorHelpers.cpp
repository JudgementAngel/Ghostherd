// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPErrorHelpers.h"
#include "MCPSearchIndex.h"

namespace MCPErrorHelpers
{

FString AssetNotFoundError(const FString& AssetPath)
{
	FString BaseName = FPaths::GetBaseFilename(AssetPath);

	TArray<FMCPSearchResult> Results = FMCPSearchIndex::Get().Search(BaseName, TEXT("Asset"), 3);

	FString Msg = FString::Printf(TEXT("Asset not found: '%s'"), *AssetPath);

	if (Results.Num() > 0)
	{
		TArray<FString> SuggestionPaths;
		for (const FMCPSearchResult& R : Results)
		{
			SuggestionPaths.Add(FString::Printf(TEXT("'%s' (%s)"), *R.Path, *R.Category));
		}
		Msg += FString::Printf(TEXT(". Did you mean: %s"), *FString::Join(SuggestionPaths, TEXT(", ")));
	}

	return Msg;
}

FString ActorNotFoundError(const FString& ActorLabel)
{
	TArray<FString> Suggestions = FMCPSearchIndex::Get().SuggestSimilar(ActorLabel, TEXT("Actor"), 3);

	FString Msg = FString::Printf(TEXT("Actor not found: '%s'"), *ActorLabel);

	if (Suggestions.Num() > 0)
	{
		Msg += FString::Printf(TEXT(". Did you mean: %s"), *FString::Join(Suggestions, TEXT(", ")));
	}

	return Msg;
}

FString FunctionNotFoundError(const FString& FuncName, UClass* Class)
{
	FString Msg = FString::Printf(TEXT("Function '%s' not found on class '%s'"),
		*FuncName, Class ? *Class->GetName() : TEXT("Unknown"));

	if (Class)
	{
		TArray<FString> Similar;
		for (TFieldIterator<UFunction> It(Class); It; ++It)
		{
			if (It->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
			{
				FString Name = It->GetName();
				if (Name.Contains(FuncName) || FuncName.Contains(Name))
				{
					Similar.Add(Name);
				}
				else
				{
					int32 Dist = FMCPSearchIndex::LevenshteinDistance(FuncName, Name);
					if (Dist <= FMath::Max(3, FuncName.Len() / 3))
					{
						Similar.Add(Name);
					}
				}
			}
			if (Similar.Num() >= 5) break;
		}

		if (Similar.Num() > 0)
		{
			Msg += FString::Printf(TEXT(". Similar functions: %s"), *FString::Join(Similar, TEXT(", ")));
		}

		Msg += TEXT(". Use list_class_functions to discover available functions.");
	}

	return Msg;
}

} // namespace MCPErrorHelpers
