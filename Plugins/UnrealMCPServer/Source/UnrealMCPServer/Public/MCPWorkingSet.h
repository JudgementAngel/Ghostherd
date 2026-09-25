// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Phase 0 / R2 — per-owner focus state, promoted out of FMCPHttpServer.
 *
 * Lets an agent (or the chat panel) scope work to "the current selection" /
 * "the Blueprint I'm editing" without re-passing paths on every call. The HTTP
 * server owns one store keyed by Mcp-Session-Id; the chat panel persists one of
 * these per chat session so a resumed conversation keeps its focus.
 */
struct UNREALMCPSERVER_API FMCPWorkingSet
{
	TArray<FString> Selection;
	FString CurrentBlueprintPath;
	FString CurrentWidgetPath;
	FString CurrentLevelSequencePath;

	TSharedPtr<FJsonObject> ToJson() const;
	void FromJson(const TSharedPtr<FJsonObject>& Json);

	bool IsEmpty() const
	{
		return Selection.Num() == 0
			&& CurrentBlueprintPath.IsEmpty()
			&& CurrentWidgetPath.IsEmpty()
			&& CurrentLevelSequencePath.IsEmpty();
	}

	void Reset()
	{
		Selection.Empty();
		CurrentBlueprintPath.Empty();
		CurrentWidgetPath.Empty();
		CurrentLevelSequencePath.Empty();
	}
};

/**
 * Thread-safe owner-keyed store. Owner ids are opaque: the HTTP transport uses
 * session ids, the chat panel uses chat-session GUIDs.
 */
class UNREALMCPSERVER_API FMCPWorkingSetStore
{
public:
	static FMCPWorkingSetStore& Get();

	/** Copy of the owner's set (default-constructed when absent). */
	FMCPWorkingSet GetCopy(const FString& OwnerId) const;

	/** Replace the owner's set wholesale. */
	void Set(const FString& OwnerId, const FMCPWorkingSet& InSet);

	/** Merge a partial JSON payload into the owner's set and return the result. */
	FMCPWorkingSet ApplyJson(const FString& OwnerId, const TSharedPtr<FJsonObject>& Json);

	void Clear(const FString& OwnerId);
	bool Contains(const FString& OwnerId) const;
	int32 Num() const;

private:
	FMCPWorkingSetStore() = default;

	TMap<FString, FMCPWorkingSet> SetsByOwner;
	mutable FCriticalSection SetsLock;
};
