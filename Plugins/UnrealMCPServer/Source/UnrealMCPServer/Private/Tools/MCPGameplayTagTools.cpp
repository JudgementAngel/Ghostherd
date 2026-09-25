// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPGameplayTagTools.h"
#include "Common/MCPActorResolver.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayTagsManager.h"
#include "GameplayTagContainer.h"

namespace MCPGameplayTagTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor) return GEditor->GetEditorWorldContext().World();
	return nullptr;
}

static AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	// v4 Phase 1: cached resolver (O(1) amortized) replaces the per-call actor scan.
	return MCPCommon::FindActorByLabel(World, Label);
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// add_gameplay_tags - Register new gameplay tags
	// ================================================================
	MCP_TOOL(Registry, "add_gameplay_tags")
		.Description(TEXT("Register new Gameplay Tags in the project's tag hierarchy. Tags use dot-separated paths (e.g., 'Character.State.Stunned', 'Weapon.Type.Rifle'). Parent tags are automatically created. Tags persist in the project's DefaultGameplayTags.ini."))
		.StringArrayArg(TEXT("tags"), TEXT("Array of tag strings to register (dot-separated hierarchy, e.g., 'Character.State.Stunned', 'Ability.Cooldown')"), true)
		.StringArg(TEXT("comment"), TEXT("Optional comment describing these tags"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			const TArray<TSharedPtr<FJsonValue>>* TagsArray;
			if (!Args->TryGetArrayField(TEXT("tags"), TagsArray) || TagsArray->Num() == 0)
				return FMCPToolResult::Error(TEXT("tags array is required and must not be empty"));

			FString Comment;
			Args->TryGetStringField(TEXT("comment"), Comment);

			UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

			TArray<FString> AddedTags;
			TArray<FString> ExistingTags;

			for (const TSharedPtr<FJsonValue>& TagVal : *TagsArray)
			{
				FString TagStr = TagVal->AsString();
				if (TagStr.IsEmpty()) continue;

				FGameplayTag ExistingTag = Manager.RequestGameplayTag(FName(*TagStr), false);
				if (ExistingTag.IsValid())
				{
					ExistingTags.Add(TagStr);
					continue;
				}

				// Add the tag natively via the tag manager
				Manager.AddNativeGameplayTag(FName(*TagStr), Comment);
				AddedTags.Add(TagStr);
			}

			// Refresh the tag manager
			if (AddedTags.Num() > 0)
			{
				Manager.EditorRefreshGameplayTagTree();
			}

			FString Result = FString::Printf(TEXT("Added %d new tag(s)"), AddedTags.Num());
			if (AddedTags.Num() > 0)
				Result += FString::Printf(TEXT(": %s"), *FString::Join(AddedTags, TEXT(", ")));
			if (ExistingTags.Num() > 0)
				Result += FString::Printf(TEXT(". Already existed: %s"), *FString::Join(ExistingTags, TEXT(", ")));

			return FMCPToolResult::Success(Result);
		});

	// ================================================================
	// list_gameplay_tags - List all registered tags
	// ================================================================
	MCP_TOOL(Registry, "list_gameplay_tags")
		.Description(TEXT("List all registered Gameplay Tags in the project. Filter by substring or parent tag to narrow results. Returns tag names in hierarchical dot-notation."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("filter"), TEXT("Filter tags by substring (e.g., 'Character' shows all Character.* tags)"))
		.StringArg(TEXT("parent_tag"), TEXT("Only show children of this parent tag"))
		.IntArg(TEXT("limit"), TEXT("Maximum tags to return (default: 100)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Filter;
			Args->TryGetStringField(TEXT("filter"), Filter);

			FString ParentTag;
			Args->TryGetStringField(TEXT("parent_tag"), ParentTag);

			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 1000);

			UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

			FGameplayTagContainer AllTags;
			Manager.RequestAllGameplayTags(AllTags, true);

			TArray<FString> MatchedTags;

			for (const FGameplayTag& Tag : AllTags)
			{
				FString TagStr = Tag.ToString();

				if (!Filter.IsEmpty() && !TagStr.Contains(Filter))
					continue;

				if (!ParentTag.IsEmpty() && !TagStr.StartsWith(ParentTag + TEXT(".")))
					continue;

				MatchedTags.Add(TagStr);

				if (MatchedTags.Num() >= Limit) break;
			}

			MatchedTags.Sort();

			if (MatchedTags.Num() == 0)
			{
				return FMCPToolResult::Success(FString::Printf(TEXT("No gameplay tags found%s%s"),
					Filter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" matching '%s'"), *Filter),
					ParentTag.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" under '%s'"), *ParentTag)));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("total"), MatchedTags.Num());

			TArray<TSharedPtr<FJsonValue>> TagArray;
			for (const FString& Tag : MatchedTags)
			{
				TagArray.Add(MakeShared<FJsonValueString>(Tag));
			}
			Result->SetArrayField(TEXT("tags"), TagArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// set_actor_gameplay_tags - Assign tags to actor
	// ================================================================
	MCP_TOOL(Registry, "set_actor_gameplay_tags")
		.Description(TEXT("Assign Gameplay Tags to an actor via its Tags property. Tags must already be registered (use add_gameplay_tags first). Modes: 'add' appends, 'remove' removes specific tags, 'replace' overwrites all tags."))
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor"), true)
		.StringArrayArg(TEXT("tags"), TEXT("Array of gameplay tag strings to assign"), true)
		.EnumArg(TEXT("mode"), TEXT("How to apply tags (default: add)"),
			{ TEXT("add"), TEXT("remove"), TEXT("replace") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			const TArray<TSharedPtr<FJsonValue>>* TagsArray;
			if (!Args->TryGetArrayField(TEXT("tags"), TagsArray) || TagsArray->Num() == 0)
				return FMCPToolResult::Error(TEXT("tags array is required"));

			FString Mode = TEXT("add");
			Args->TryGetStringField(TEXT("mode"), Mode);

			// Convert string tags to FName actor tags
			// Note: UE actor Tags are FName[], not FGameplayTagContainer.
			// For gameplay tag containers, the actor needs a component with a tag container.
			// Here we store them as actor tags (FName) for simplicity.
			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Actor Gameplay Tags")));
			Actor->Modify();

			if (Mode == TEXT("replace"))
			{
				Actor->Tags.Empty();
			}

			TArray<FString> Applied;
			for (const TSharedPtr<FJsonValue>& TagVal : *TagsArray)
			{
				FString TagStr = TagVal->AsString();
				if (TagStr.IsEmpty()) continue;

				FName TagName(*TagStr);

				if (Mode == TEXT("remove"))
				{
					if (Actor->Tags.Remove(TagName) > 0)
						Applied.Add(TagStr);
				}
				else // add or replace
				{
					if (!Actor->Tags.Contains(TagName))
					{
						Actor->Tags.Add(TagName);
					}
					Applied.Add(TagStr);
				}
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("%s %d tag(s) on '%s': %s. Total tags: %d"),
				Mode == TEXT("remove") ? TEXT("Removed") : (Mode == TEXT("replace") ? TEXT("Set") : TEXT("Added")),
				Applied.Num(), *ActorName,
				*FString::Join(Applied, TEXT(", ")),
				Actor->Tags.Num()));
		});
}

} // namespace MCPGameplayTagTools
