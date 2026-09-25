// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatStore.h"
#include "UnrealMCPChatModule.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#define LOCTEXT_NAMESPACE "MCPChatStore"

FMCPChatStore& FMCPChatStore::Get()
{
	static FMCPChatStore Instance;
	return Instance;
}

// ============================================================================
// Paths
// ============================================================================

FString FMCPChatStore::GetRootDirectory()
{
	// Project-scoped: transcripts reference this project's assets and are close to
	// meaningless elsewhere. A user-directory option is a Phase 8 setting.
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UnrealMCPChat"));
}

FString FMCPChatStore::GetSessionDirectory(const FGuid& SessionId)
{
	return GetRootDirectory() / TEXT("sessions") / SessionId.ToString(EGuidFormats::DigitsWithHyphens);
}

FString FMCPChatStore::GetSessionFilePath(const FGuid& SessionId)
{
	return GetSessionDirectory(SessionId) / TEXT("session.json");
}

FString FMCPChatStore::GetAttachmentsDirectory(const FGuid& SessionId)
{
	return GetSessionDirectory(SessionId) / TEXT("attachments");
}

FString FMCPChatStore::GetIndexFilePath()
{
	return GetRootDirectory() / TEXT("index.json");
}

// ============================================================================
// Lifecycle
// ============================================================================

void FMCPChatStore::Initialize()
{
	if (bInitialized) { return; }
	bInitialized = true;

	IFileManager::Get().MakeDirectory(*(GetRootDirectory() / TEXT("sessions")), /*Tree*/ true);
	LoadIndex();

	AutosaveTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FMCPChatStore::TickAutosave), 0.5f);

	UE_LOG(LogUnrealMCPChat, Log, TEXT("Chat store ready: %d session(s) indexed at %s"),
		Index.Num(), *GetRootDirectory());
}

void FMCPChatStore::Shutdown()
{
	if (AutosaveTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(AutosaveTickerHandle);
		AutosaveTickerHandle.Reset();
	}

	// Synchronous on purpose: a background write racing editor teardown is how
	// transcripts get lost.
	SaveNow();

	LoadedSessions.Empty();
	Index.Empty();
	SearchIndex.Empty();
	bSearchIndexValid = false;
	bInitialized = false;
}

// ============================================================================
// Atomic write
// ============================================================================

bool FMCPChatStore::WriteStringAtomic(const FString& AbsolutePath, const FString& Contents)
{
	const FString TempPath = AbsolutePath + TEXT(".tmp");

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsolutePath), /*Tree*/ true);

	if (!FFileHelper::SaveStringToFile(Contents, *TempPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogUnrealMCPChat, Error, TEXT("Could not write '%s'."), *TempPath);
		return false;
	}

	// Move overwrites on all supported platforms. If it fails the original file is
	// still intact, which is the whole point.
	if (!IFileManager::Get().Move(*AbsolutePath, *TempPath, /*bReplace*/ true, /*bEvenIfReadOnly*/ true))
	{
		UE_LOG(LogUnrealMCPChat, Error, TEXT("Could not move '%s' into place."), *TempPath);
		IFileManager::Get().Delete(*TempPath, false, true, true);
		return false;
	}
	return true;
}

// ============================================================================
// Index
// ============================================================================

void FMCPChatStore::LoadIndex()
{
	Index.Reset();

	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *GetIndexFilePath()))
	{
		return;   // first run
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
	if (!FJsonSerializer::Deserialize(Reader, Rows))
	{
		UE_LOG(LogUnrealMCPChat, Warning,
			TEXT("index.json is corrupt; rebuilding it from the sessions folder."));

		// Recover rather than lose the history list: every session file is
		// self-describing, so the index is derived data.
		TArray<FString> Dirs;
		IFileManager::Get().FindFiles(Dirs, *(GetRootDirectory() / TEXT("sessions") / TEXT("*")), false, true);
		for (const FString& Dir : Dirs)
		{
			FGuid SessionId;
			if (FGuid::Parse(Dir, SessionId))
			{
				if (const FChatSessionPtr S = LoadSession(SessionId))
				{
					Index.Add(MakeShared<FChatSessionSummary>(FChatSessionSummary::FromSession(*S)));
				}
			}
		}
		SaveIndex();
		return;
	}

	for (const TSharedPtr<FJsonValue>& V : Rows)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (V.IsValid() && V->TryGetObject(Obj) && Obj)
		{
			FChatSessionSummary Sum = FChatSessionSummary::FromJson(*Obj);
			if (Sum.Id.IsValid())
			{
				Index.Add(MakeShared<FChatSessionSummary>(MoveTemp(Sum)));
			}
		}
	}
}

void FMCPChatStore::SaveIndex()
{
	TArray<TSharedPtr<FJsonValue>> Rows;
	Rows.Reserve(Index.Num());
	for (const FChatSessionSummaryPtr& S : Index)
	{
		if (S.IsValid()) { Rows.Add(MakeShared<FJsonValueObject>(S->ToJson())); }
	}

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Rows, Writer);

	WriteStringAtomic(GetIndexFilePath(), Out);
}

void FMCPChatStore::UpdateIndexEntry(const FChatSessionPtr& Session)
{
	if (!Session.IsValid()) { return; }

	const FChatSessionSummary Fresh = FChatSessionSummary::FromSession(*Session);
	for (FChatSessionSummaryPtr& Row : Index)
	{
		if (Row.IsValid() && Row->Id == Session->Id)
		{
			*Row = Fresh;
			return;
		}
	}
	Index.Add(MakeShared<FChatSessionSummary>(Fresh));
}

// ============================================================================
// Sessions
// ============================================================================

FChatSessionPtr FMCPChatStore::LoadSession(const FGuid& SessionId)
{
	if (const FChatSessionPtr* Cached = LoadedSessions.Find(SessionId))
	{
		return *Cached;
	}

	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *GetSessionFilePath(SessionId)))
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogUnrealMCPChat, Error, TEXT("Session %s is not valid JSON."),
			*SessionId.ToString(EGuidFormats::DigitsWithHyphens));
		return nullptr;
	}

	FChatSessionPtr Session = FChatSession::FromJson(Root);
	if (Session.IsValid())
	{
		LoadedSessions.Add(SessionId, Session);
	}
	return Session;
}

void FMCPChatStore::AddSession(const FChatSessionPtr& Session)
{
	if (!Session.IsValid()) { return; }

	LoadedSessions.Add(Session->Id, Session);
	UpdateIndexEntry(Session);
	WriteSessionFile(Session);
	SaveIndex();
	bSearchIndexValid = false;
	OnIndexChanged.Broadcast();
}

void FMCPChatStore::MarkDirty(const FGuid& SessionId)
{
	if (!SessionId.IsValid()) { return; }
	DirtySessions.Add(SessionId);
	LastDirtySeconds = FPlatformTime::Seconds();
}

bool FMCPChatStore::TickAutosave(float /*DeltaTime*/)
{
	if (DirtySessions.Num() == 0) { return true; }
	if (FPlatformTime::Seconds() - LastDirtySeconds < DebounceSeconds) { return true; }

	// Snapshot and clear first: a save that itself dirties the session (it should
	// not, but be defensive) must not loop.
	TArray<FGuid> ToSave = DirtySessions.Array();
	DirtySessions.Reset();

	for (const FGuid& Id : ToSave)
	{
		if (const FChatSessionPtr* Session = LoadedSessions.Find(Id))
		{
			UpdateIndexEntry(*Session);
			WriteSessionFile(*Session);
		}
	}
	SaveIndex();
	bSearchIndexValid = false;
	OnIndexChanged.Broadcast();
	return true;
}

void FMCPChatStore::WriteSessionFile(const FChatSessionPtr& Session)
{
	if (!Session.IsValid()) { return; }

	// Serialise on the game thread (the session is mutating there) and hand the
	// finished string to a background task. Copying the string is far cheaper than
	// blocking the editor on file IO for a large transcript.
	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Session->ToJson().ToSharedRef(), Writer);

	const FString Path = GetSessionFilePath(Session->Id);

	if (IsInGameThread())
	{
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Path, Out = MoveTemp(Out)]()
		{
			WriteStringAtomic(Path, Out);
		});
	}
	else
	{
		WriteStringAtomic(Path, Out);
	}
}

void FMCPChatStore::SaveNow(const FGuid& SessionId)
{
	auto SaveOne = [this](const FChatSessionPtr& Session)
	{
		if (!Session.IsValid()) { return; }
		UpdateIndexEntry(Session);

		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Session->ToJson().ToSharedRef(), Writer);
		WriteStringAtomic(GetSessionFilePath(Session->Id), Out);   // synchronous
	};

	if (SessionId.IsValid())
	{
		if (const FChatSessionPtr* Session = LoadedSessions.Find(SessionId))
		{
			SaveOne(*Session);
		}
		DirtySessions.Remove(SessionId);
	}
	else
	{
		for (const TPair<FGuid, FChatSessionPtr>& Pair : LoadedSessions)
		{
			SaveOne(Pair.Value);
		}
		DirtySessions.Reset();
	}

	SaveIndex();
	bSearchIndexValid = false;
}

bool FMCPChatStore::DeleteSession(const FGuid& SessionId)
{
	const FString Dir = GetSessionDirectory(SessionId);
	const bool bRemoved = IFileManager::Get().DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ true);

	LoadedSessions.Remove(SessionId);
	DirtySessions.Remove(SessionId);
	Index.RemoveAll([&SessionId](const FChatSessionSummaryPtr& Row)
	{
		return Row.IsValid() && Row->Id == SessionId;
	});

	SaveIndex();
	bSearchIndexValid = false;
	OnIndexChanged.Broadcast();

	if (!bRemoved)
	{
		UE_LOG(LogUnrealMCPChat, Warning,
			TEXT("Removed session %s from the index but could not delete '%s'."),
			*SessionId.ToString(EGuidFormats::DigitsWithHyphens), *Dir);
	}
	return bRemoved;
}

// ============================================================================
// Search
// ============================================================================

void FMCPChatStore::BuildSearchIndex()
{
	SearchIndex.Reset();

	for (const FChatSessionSummaryPtr& Row : Index)
	{
		if (!Row.IsValid()) { continue; }

		// Title and preview come free from the index. Message bodies need the full
		// session, so only load what is already cached — a cold search falls back to
		// title/preview matching rather than reading every file on disk.
		FString Corpus = Row->Title + TEXT(" ") + Row->Preview;
		if (const FChatSessionPtr* Session = LoadedSessions.Find(Row->Id))
		{
			for (const FChatMessagePtr& M : (*Session)->Messages)
			{
				if (M.IsValid()) { Corpus += TEXT(" ") + M->GetPlainText(false); }
			}
		}

		TArray<FString> Tokens;
		Corpus.ToLower().ParseIntoArray(Tokens, TEXT(" "), true);
		for (FString& Token : Tokens)
		{
			Token.TrimStartAndEndInline();
			if (Token.Len() >= 2)
			{
				SearchIndex.FindOrAdd(Token).Add(Row->Id);
			}
		}
	}
	bSearchIndexValid = true;
}

TArray<FChatSessionSummaryPtr> FMCPChatStore::Search(const FString& Query)
{
	if (Query.TrimStartAndEnd().IsEmpty())
	{
		return Index;
	}

	if (!bSearchIndexValid)
	{
		BuildSearchIndex();
	}

	const FString Needle = Query.ToLower().TrimStartAndEnd();

	// Prefix match on tokens, so typing "bluep" finds "blueprint". Intersecting
	// full-token lookups would be faster but would not match as the user types.
	TSet<FGuid> Hits;
	for (const TPair<FString, TSet<FGuid>>& Pair : SearchIndex)
	{
		if (Pair.Key.StartsWith(Needle))
		{
			Hits.Append(Pair.Value);
		}
	}

	TArray<FChatSessionSummaryPtr> Results;
	for (const FChatSessionSummaryPtr& Row : Index)
	{
		if (Row.IsValid() && Hits.Contains(Row->Id))
		{
			Results.Add(Row);
		}
	}
	return Results;
}

// ============================================================================
// Grouping
// ============================================================================

TArray<FMCPChatStore::FGroupedIndex> FMCPChatStore::GetGroupedIndex(const FString& SearchQuery)
{
	TArray<FChatSessionSummaryPtr> Rows = Search(SearchQuery);

	Rows.Sort([](const FChatSessionSummaryPtr& A, const FChatSessionSummaryPtr& B)
	{
		if (!A.IsValid() || !B.IsValid()) { return false; }
		return A->UpdatedAt > B->UpdatedAt;
	});

	const FDateTime Now = FDateTime::UtcNow();
	const FDateTime TodayStart = Now.GetDate();
	const FDateTime YesterdayStart = TodayStart - FTimespan::FromDays(1);
	const FDateTime WeekStart = TodayStart - FTimespan::FromDays(7);

	TArray<FChatSessionSummaryPtr> Pinned, Today, Yesterday, ThisWeek, Earlier;
	for (const FChatSessionSummaryPtr& Row : Rows)
	{
		if (!Row.IsValid()) { continue; }
		if (Row->bPinned)                    { Pinned.Add(Row); }
		else if (Row->UpdatedAt >= TodayStart)     { Today.Add(Row); }
		else if (Row->UpdatedAt >= YesterdayStart) { Yesterday.Add(Row); }
		else if (Row->UpdatedAt >= WeekStart)      { ThisWeek.Add(Row); }
		else                                        { Earlier.Add(Row); }
	}

	TArray<FGroupedIndex> Groups;
	auto AddGroup = [&Groups](const FText& Heading, TArray<FChatSessionSummaryPtr>& Src)
	{
		if (Src.Num() > 0)
		{
			Groups.Add(FGroupedIndex{ Heading, MoveTemp(Src) });
		}
	};

	AddGroup(LOCTEXT("GroupPinned",    "Pinned"),    Pinned);
	AddGroup(LOCTEXT("GroupToday",     "Today"),     Today);
	AddGroup(LOCTEXT("GroupYesterday", "Yesterday"), Yesterday);
	AddGroup(LOCTEXT("GroupThisWeek",  "This week"), ThisWeek);
	AddGroup(LOCTEXT("GroupEarlier",   "Earlier"),   Earlier);
	return Groups;
}

// ============================================================================
// Export
// ============================================================================

FString FMCPChatStore::ExportSessionToMarkdown(const FGuid& SessionId)
{
	const FChatSessionPtr Session = LoadSession(SessionId);
	if (!Session.IsValid()) { return FString(); }

	FString Md;
	Md += FString::Printf(TEXT("# %s\n\n"), *(Session->Title.IsEmpty() ? TEXT("Untitled conversation") : Session->Title));
	Md += FString::Printf(TEXT("- Session: `%s`\n"), *Session->Id.ToString(EGuidFormats::DigitsWithHyphens));
	Md += FString::Printf(TEXT("- Backend: %s  ·  Model: %s\n"), *Session->BackendId, *Session->ModelId);
	Md += FString::Printf(TEXT("- Created: %s  ·  Updated: %s\n\n---\n\n"),
		*Session->CreatedAt.ToIso8601(), *Session->UpdatedAt.ToIso8601());

	// Export the active branch only — exporting every abandoned retry would make
	// the document unreadable.
	for (const FChatMessagePtr& M : Session->BuildActivePath())
	{
		if (!M.IsValid()) { continue; }

		const TCHAR* RoleLabel =
			M->Role == EChatRole::User      ? TEXT("You") :
			M->Role == EChatRole::Assistant ? TEXT("Assistant") :
			M->Role == EChatRole::System    ? TEXT("System") : TEXT("Tool");

		Md += FString::Printf(TEXT("## %s"), RoleLabel);
		if (!M->ModelId.IsEmpty()) { Md += FString::Printf(TEXT(" · %s"), *M->ModelId); }
		Md += TEXT("\n\n");

		for (const FChatContentBlock& B : M->Blocks)
		{
			switch (B.Type)
			{
			case FChatContentBlock::EType::Text:
			case FChatContentBlock::EType::Refusal:
				Md += B.Text + TEXT("\n\n");
				break;

			case FChatContentBlock::EType::Thinking:
				Md += FString::Printf(TEXT("<details><summary>Thinking</summary>\n\n%s\n\n</details>\n\n"), *B.Text);
				break;

			case FChatContentBlock::EType::Error:
				Md += FString::Printf(TEXT("> **Error:** %s\n\n"), *B.Text);
				break;

			case FChatContentBlock::EType::Divider:
				Md += FString::Printf(TEXT("---\n\n*%s*\n\n"), *B.Text);
				break;

			case FChatContentBlock::EType::ToolCall:
			{
				FString ArgsText;
				if (B.ToolArgs.IsValid())
				{
					const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W =
						TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&ArgsText);
					FJsonSerializer::Serialize(B.ToolArgs.ToSharedRef(), W);
				}
				Md += FString::Printf(TEXT("**Tool call: `%s`**%s\n\n```json\n%s\n```\n\n"),
					*B.ToolName, B.bToolIsError ? TEXT(" *(failed)*") : TEXT(""), *ArgsText);
				break;
			}

			case FChatContentBlock::EType::ToolResult:
				Md += FString::Printf(TEXT("```\n%s\n```\n\n"), *B.ToolResultText);
				break;

			case FChatContentBlock::EType::Image:
			case FChatContentBlock::EType::File:
				Md += FString::Printf(TEXT("📎 `%s`\n\n"),
					*(B.DisplayName.IsEmpty() ? B.StoredPath : B.DisplayName));
				break;
			}
		}
	}

	const FString OutPath = GetRootDirectory() / TEXT("exports")
		/ FString::Printf(TEXT("%s.md"), *Session->Id.ToString(EGuidFormats::DigitsWithHyphens));

	return WriteStringAtomic(OutPath, Md) ? OutPath : FString();
}

#undef LOCTEXT_NAMESPACE
