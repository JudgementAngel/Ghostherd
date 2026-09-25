// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPSearchTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"
#include "MCPSearchIndex.h"

namespace MCPSearchTools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// search_project - Fuzzy search across project
	// ================================================================
	MCP_TOOL(Registry, "search_project")
		.Description(TEXT("Fuzzy search across the entire project: assets, Blueprint functions, variables, and level actors. Supports partial names, CamelCase fragments, and approximate matching. Use this when you don't know the exact name or path of something. Results are ranked by relevance score (lower = better match)."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("query"), TEXT("Search query - supports partial names, CamelCase fragments, fuzzy matching (e.g., 'player character', 'health bar', 'BP_Enemy', 'print string')"), true)
		.EnumArg(TEXT("category"), TEXT("Filter results by category (default: all)"),
			{ TEXT("all"), TEXT("Asset"), TEXT("Function"), TEXT("Variable"), TEXT("Actor") })
		.IntArg(TEXT("limit"), TEXT("Maximum results to return (default: 20, max: 100)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Query;
			if (!Args->TryGetStringField(TEXT("query"), Query))
				return FMCPToolResult::Error(TEXT("query is required"));

			FString Category = TEXT("all");
			Args->TryGetStringField(TEXT("category"), Category);

			int32 Limit = 20;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 100);
			}

			FMCPSearchIndex& Index = FMCPSearchIndex::Get();

			// Auto-build if index is empty
			if (Index.GetEntryCount() == 0)
			{
				Index.Build();
			}

			TArray<FMCPSearchResult> Results = Index.Search(Query, Category, Limit);

			if (Results.Num() == 0)
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("No results found for '%s'%s. Try a different query or rebuild the index with rebuild_search_index."),
					*Query,
					Category != TEXT("all") ? *FString::Printf(TEXT(" (category: %s)"), *Category) : TEXT("")));
			}

			TArray<TSharedPtr<FJsonValue>> ResultArray;
			for (const FMCPSearchResult& Result : Results)
			{
				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("name"), Result.Name);
				Obj->SetStringField(TEXT("path"), Result.Path);
				Obj->SetStringField(TEXT("type"), Result.Type);
				Obj->SetStringField(TEXT("category"), Result.Category);
				Obj->SetNumberField(TEXT("score"), Result.Score);
				Obj->SetStringField(TEXT("description"), Result.Description);
				ResultArray.Add(MakeShared<FJsonValueObject>(Obj));
			}

			TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("query"), Query);
			Response->SetNumberField(TEXT("total_results"), Results.Num());
			Response->SetNumberField(TEXT("index_size"), Index.GetEntryCount());
			Response->SetArrayField(TEXT("results"), ResultArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Response), Response);
		});

	// ================================================================
	// rebuild_search_index - Force rebuild the search index
	// ================================================================
	MCP_TOOL(Registry, "rebuild_search_index")
		.Description(TEXT("Force rebuild the project search index. Call this after significant project changes (importing assets, creating Blueprints, adding actors) to ensure search results are up to date. The index auto-builds on first search if empty."))
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FMCPSearchIndex& Index = FMCPSearchIndex::Get();
			Index.Build();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Search index rebuilt: %d entries indexed at %s"),
				Index.GetEntryCount(),
				*Index.GetLastBuildTime().ToString()));
		});
}

} // namespace MCPSearchTools
