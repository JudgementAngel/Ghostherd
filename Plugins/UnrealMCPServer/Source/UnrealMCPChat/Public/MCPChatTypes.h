// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPWorkingSet.h"
#include "Dom/JsonObject.h"

/**
 * Phase 2 — the conversation data model.
 *
 * Messages are held by TSharedPtr because SListView requires shared items and
 * because branching (see FChatSession::BuildActivePath) needs stable identity
 * across list refreshes.
 *
 * Serialisation lives here rather than in FChatStore so the shape and its
 * on-disk form stay next to each other; the store owns files and scheduling only.
 */

/** Plain enum, not UENUM — this header has no .generated.h and the chat data model
 *  is intentionally non-UObject (it is serialised to JSON, never to a UAsset). */
enum class EChatRole : uint8
{
	User,
	Assistant,
	System,
	Tool,
};

/** One renderable unit inside a message. A message is an ordered list of these. */
struct UNREALMCPCHAT_API FChatContentBlock
{
	enum class EType : uint8
	{
		Text,
		Thinking,
		Image,
		File,
		ToolCall,
		ToolResult,
		Refusal,
		Error,
		Divider,     // "— switched to GPT-5 —"
		ContextRef,  // Phase 6: an @mention — a chip in the UI, resolved text on the wire
		Plan,        // Phase 7: an agent's running checklist, rewritten in place
	};

	EType   Type = EType::Text;

	/** Text / Thinking / Refusal / Error / Divider payload. */
	FString Text;

	/** Image / File. StoredPath is relative to the session's attachments folder so
	 *  a session directory can be moved or archived without rewriting paths. */
	FString MimeType;
	FString StoredPath;
	FString DisplayName;
	int64   SizeBytes = 0;

	/** Tool call / result. */
	FGuid                   ToolCallId;
	/** The provider's own id for this call (Anthropic "toolu_…"). Must be echoed
	 *  back verbatim on the tool_result or the follow-up request is rejected. */
	FString                 ProviderCallId;
	FString                 ToolName;
	TSharedPtr<FJsonObject> ToolArgs;
	TSharedPtr<FJsonObject> ToolResult;
	FString                 ToolResultText;
	bool                    bToolIsError = false;
	bool                    bToolAwaitingApproval = false;
	double                  DurationSeconds = 0.0;

	/** ContextRef (Phase 6). Kind is a lowercase tag — "asset", "actor", "selection",
	 *  "viewport", "file", … — kept as a string rather than an enum so this header
	 *  stays independent of the resolver, and so an unknown kind in an old transcript
	 *  degrades to a chip with no data instead of failing to parse.
	 *
	 *  `Text` holds the RESOLVED payload. It is filled in at send time, not at type
	 *  time, which is the whole point: @Selection means what was selected when you
	 *  pressed Enter, not what was selected when you typed the '@'. */
	FString ContextKind;
	FString ContextTarget;
	/** Pinned chips are re-resolved and re-sent on every subsequent turn of the
	 *  session — "always tell the model about my current level". */
	bool    bContextPinned = false;

	/** Streaming state: tool arguments arrive as partial JSON, so the card renders a
	 *  "receiving arguments" state until the block closes. */
	bool    bIsComplete = true;
	FString PartialArgsBuffer;

	TSharedPtr<FJsonObject> ToJson() const;
	static FChatContentBlock FromJson(const TSharedPtr<FJsonObject>& Json);

	static FChatContentBlock MakeText(const FString& InText);
	static FChatContentBlock MakeThinking(const FString& InText);
	static FChatContentBlock MakeError(const FString& InText);
	static FChatContentBlock MakeDivider(const FString& InText);
};

/** Token accounting for one assistant turn. */
struct UNREALMCPCHAT_API FChatUsage
{
	int32  InputTokens = 0;
	int32  OutputTokens = 0;
	int32  CacheReadTokens = 0;
	int32  CacheWriteTokens = 0;
	double EstimatedCostUsd = 0.0;

	bool IsEmpty() const { return InputTokens == 0 && OutputTokens == 0; }
	void Accumulate(const FChatUsage& Other);

	TSharedPtr<FJsonObject> ToJson() const;
	static FChatUsage FromJson(const TSharedPtr<FJsonObject>& Json);
};

/** Per-session request shaping. Which fields are legal depends on the model —
 *  the catalogue's capability flags decide what the settings UI even shows. */
struct UNREALMCPCHAT_API FChatModelParams
{
	FString Effort = TEXT("high");         // low|medium|high|xhigh|max — "" = omit
	int32   MaxOutputTokens = 64000;
	bool    bThinkingEnabled = true;
	bool    bThinkingVisible = true;       // maps to display:"summarized"
	bool    bPromptCaching = true;
	bool    bCatalogToolExposure = true;
	int32   MaxToolIterations = 25;

	/** Only sent for models whose catalogue entry has "sampling": true. */
	bool    bHasSampling = false;
	float   Temperature = 1.0f;

	TSharedPtr<FJsonObject> ToJson() const;
	static FChatModelParams FromJson(const TSharedPtr<FJsonObject>& Json);
};

/** One turn from one participant. */
struct UNREALMCPCHAT_API FChatMessage
{
	FGuid                     Id;
	EChatRole                 Role = EChatRole::User;
	TArray<FChatContentBlock> Blocks;
	FDateTime                 Timestamp;
	FString                   ModelId;
	FString                   BackendId;
	FChatUsage                Usage;

	/** Branching: siblings share a ParentId. "Retry" makes a sibling of an assistant
	 *  message; "Edit & resend" makes a sibling of a user message. Invalid = root. */
	FGuid                     ParentId;

	bool                      bIsStreaming = false;

	/** Concatenated Text/Thinking blocks — used for search and for Copy. */
	FString GetPlainText(bool bIncludeThinking = false) const;

	/** First N characters of the first text block, for list previews. */
	FString GetPreview(int32 MaxChars = 80) const;

	TSharedPtr<FJsonObject> ToJson() const;
	static TSharedPtr<FChatMessage> FromJson(const TSharedPtr<FJsonObject>& Json);
};

using FChatMessagePtr = TSharedPtr<FChatMessage>;

/** A conversation. */
struct UNREALMCPCHAT_API FChatSession
{
	static constexpr int32 CurrentSchemaVersion = 1;

	FGuid     Id;
	FString   Title;
	FDateTime CreatedAt;
	FDateTime UpdatedAt;
	FString   BackendId;
	FString   ModelId;

	FChatModelParams        Params;
	TArray<FChatMessagePtr> Messages;
	FMCPWorkingSet          WorkingSet;
	TArray<FString>         PinnedContext;
	TSet<FString>           Tags;
	bool                    bPinned = false;
	int32                   SchemaVersion = CurrentSchemaVersion;

	/** Which branch to follow at each fork. Absent = follow the newest child. */
	TMap<FGuid, FGuid> ActiveChildByParent;

	static TSharedPtr<FChatSession> CreateNew(const FString& InBackendId, const FString& InModelId);

	/** Messages on the currently-selected branch, root → leaf. This is what the
	 *  transcript renders and what a model backend sends as history. */
	TArray<FChatMessagePtr> BuildActivePath() const;

	/** Siblings of a message (including itself), oldest first. Drives the ‹ 2/3 › stepper. */
	TArray<FChatMessagePtr> GetSiblings(const FGuid& MessageId) const;

	/** Select a branch at the fork that owns MessageId. */
	void SetActiveBranch(const FGuid& MessageId);

	FChatMessagePtr FindMessage(const FGuid& MessageId) const;

	/** Append to the end of the active path (ParentId is filled in automatically). */
	void AppendMessage(const FChatMessagePtr& Message);

	/** Derive a title from the first user message. No-op once a title exists. */
	void EnsureTitle();

	TSharedPtr<FJsonObject> ToJson() const;
	static TSharedPtr<FChatSession> FromJson(const TSharedPtr<FJsonObject>& Json);
};

using FChatSessionPtr = TSharedPtr<FChatSession>;

/** Lightweight row in the history index — enough to render the rail without
 *  loading every session file. */
struct UNREALMCPCHAT_API FChatSessionSummary
{
	FGuid     Id;
	FString   Title;
	FString   Preview;
	FDateTime UpdatedAt;
	FString   BackendId;
	FString   ModelId;
	int32     MessageCount = 0;
	bool      bPinned = false;
	TArray<FString> Tags;

	TSharedPtr<FJsonObject> ToJson() const;
	static FChatSessionSummary FromJson(const TSharedPtr<FJsonObject>& Json);
	static FChatSessionSummary FromSession(const FChatSession& Session);
};

using FChatSessionSummaryPtr = TSharedPtr<FChatSessionSummary>;
