// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPChatTypes.h"
#include "Containers/Ticker.h"

/**
 * Phase 2 — conversation persistence.
 *
 * Layout (docs/02_ARCHITECTURE.md §7):
 *   <Project>/Saved/UnrealMCPChat/
 *     index.json                       summaries only — the rail never loads full sessions
 *     sessions/<guid>/session.json
 *     sessions/<guid>/attachments/*
 *
 * Deliberately plain JSON: people grep and diff their transcripts.
 *
 * Threading: the public API is game-thread only. Writes are serialised on the
 * game thread and dispatched to a background task, because a 10 MB transcript
 * write must not hitch the editor.
 */
class UNREALMCPCHAT_API FMCPChatStore
{
public:
	static FMCPChatStore& Get();

	/** Read index.json. Cheap; call once at module start. */
	void Initialize();

	/** Flush pending saves synchronously. Call before the editor exits. */
	void Shutdown();

	// ---- Sessions ----

	/** Loaded sessions are cached, so repeated selection in the rail is free. */
	FChatSessionPtr LoadSession(const FGuid& SessionId);

	/** Register a new session, write it, and add it to the index. */
	void AddSession(const FChatSessionPtr& Session);

	/** Mark dirty. The actual write happens after DebounceSeconds of quiet, or on
	 *  Shutdown / SaveNow. Cheap enough to call on every streamed token. */
	void MarkDirty(const FGuid& SessionId);

	/** Force an immediate synchronous write of one session (or all when invalid). */
	void SaveNow(const FGuid& SessionId = FGuid());

	/** Delete from disk and index. Returns false if the directory could not be removed. */
	bool DeleteSession(const FGuid& SessionId);

	// ---- Index ----

	const TArray<FChatSessionSummaryPtr>& GetIndex() const { return Index; }

	/** Full-text search over titles and message bodies. Empty query returns the
	 *  whole index. Builds a lazy inverted index on first use. */
	TArray<FChatSessionSummaryPtr> Search(const FString& Query);

	/** Index rows grouped for the history rail: Pinned / Today / Yesterday /
	 *  This week / Earlier, each already sorted newest-first. */
	struct FGroupedIndex
	{
		FText Heading;
		TArray<FChatSessionSummaryPtr> Rows;
	};
	TArray<FGroupedIndex> GetGroupedIndex(const FString& SearchQuery = FString());

	/** Fired after the index changes (add / delete / rename / save). */
	DECLARE_MULTICAST_DELEGATE(FOnIndexChanged);
	FOnIndexChanged OnIndexChanged;

	// ---- Paths ----

	static FString GetRootDirectory();
	static FString GetSessionDirectory(const FGuid& SessionId);
	static FString GetSessionFilePath(const FGuid& SessionId);
	static FString GetAttachmentsDirectory(const FGuid& SessionId);
	static FString GetIndexFilePath();

	/** Markdown export. Returns the written path, or empty on failure. */
	FString ExportSessionToMarkdown(const FGuid& SessionId);

private:
	FMCPChatStore() = default;

	void LoadIndex();
	void SaveIndex();
	void UpdateIndexEntry(const FChatSessionPtr& Session);
	bool TickAutosave(float DeltaTime);
	void WriteSessionFile(const FChatSessionPtr& Session);
	void BuildSearchIndex();

	/** Write via temp file + move so an editor crash mid-write cannot leave a
	 *  truncated session file where a valid one used to be. */
	static bool WriteStringAtomic(const FString& AbsolutePath, const FString& Contents);

	TMap<FGuid, FChatSessionPtr>   LoadedSessions;
	TArray<FChatSessionSummaryPtr> Index;

	TSet<FGuid> DirtySessions;
	double      LastDirtySeconds = 0.0;
	FTSTicker::FDelegateHandle AutosaveTickerHandle;

	/** token → session ids. Rebuilt lazily; invalidated by any save or delete. */
	TMap<FString, TSet<FGuid>> SearchIndex;
	bool bSearchIndexValid = false;

	bool bInitialized = false;

	static constexpr double DebounceSeconds = 2.0;
};
