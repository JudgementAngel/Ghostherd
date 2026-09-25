// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Phase 6 — unsent composer text, and shell-style history recall.
 *
 * Two small features that are only noticed when they are missing: closing the tab
 * mid-sentence and coming back to an empty box, and having to retype a message
 * you sent two minutes ago.
 *
 * Kept out of FMCPChatStore deliberately. A draft is scratch state that should
 * never end up inside a transcript people export, diff or commit — different
 * lifetime, different file.
 */
class FMCPChatDrafts
{
public:
	static FMCPChatDrafts& Get();

	void Load();
	/** Debounced; the disk write happens on Flush or at shutdown. */
	void Save();
	void Flush();

	const FString& GetDraft(const FGuid& SessionId) const;
	void SetDraft(const FGuid& SessionId, const FString& Text);
	void ClearDraft(const FGuid& SessionId);

	/** Called on send. Deduplicates against the previous entry so holding Enter on
	 *  the same text does not fill the recall buffer with copies. */
	void PushSent(const FGuid& SessionId, const FString& Text);

	/**
	 * Walk backwards through sent messages. Index 0 is the most recent.
	 * @return false when Index is past the end, so the caller can stop at the top
	 *         rather than clamping (clamping makes ↑ feel stuck).
	 */
	bool GetRecalled(const FGuid& SessionId, int32 Index, FString& OutText) const;

	int32 GetRecallCount(const FGuid& SessionId) const;

	void ForgetSession(const FGuid& SessionId);

	static FString GetFilePath();

private:
	FMCPChatDrafts() = default;

	struct FEntry
	{
		FString Draft;
		/** Newest last. */
		TArray<FString> Sent;
	};

	TMap<FGuid, FEntry> Entries;
	bool bLoaded = false;
	bool bDirty = false;

	/** Enough to cover "what did I ask before that?" without turning the file into
	 *  a second transcript. */
	static constexpr int32 MaxRecallPerSession = 50;
};
