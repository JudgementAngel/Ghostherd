// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Core/MCPChatDrafts.h"
#include "MCPChatStore.h"
#include "UnrealMCPChatModule.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FMCPChatDrafts& FMCPChatDrafts::Get()
{
	static FMCPChatDrafts Instance;
	return Instance;
}

FString FMCPChatDrafts::GetFilePath()
{
	return FMCPChatStore::GetRootDirectory() / TEXT("drafts.json");
}

void FMCPChatDrafts::Load()
{
	if (bLoaded) { return; }
	bLoaded = true;

	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *GetFilePath())) { return; }

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		// A corrupt drafts file is not worth telling the user about — losing unsent
		// scratch text is a much smaller cost than an error dialog at editor start.
		UE_LOG(LogUnrealMCPChat, Verbose, TEXT("drafts.json unreadable; starting empty."));
		return;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Root->Values)
	{
		FGuid Id;
		if (!FGuid::Parse(Pair.Key, Id)) { continue; }

		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Obj) || !Obj) { continue; }

		FEntry Entry;
		(*Obj)->TryGetStringField(TEXT("draft"), Entry.Draft);

		const TArray<TSharedPtr<FJsonValue>>* Sent = nullptr;
		if ((*Obj)->TryGetArrayField(TEXT("sent"), Sent) && Sent)
		{
			for (const TSharedPtr<FJsonValue>& V : *Sent)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S)) { Entry.Sent.Add(S); }
			}
		}

		Entries.Add(Id, MoveTemp(Entry));
	}
}

void FMCPChatDrafts::Save()
{
	// Marked only; Flush does the write. Typing must not touch the disk per keystroke.
	bDirty = true;
}

void FMCPChatDrafts::Flush()
{
	if (!bDirty) { return; }
	bDirty = false;

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	for (const TPair<FGuid, FEntry>& Pair : Entries)
	{
		if (Pair.Value.Draft.IsEmpty() && Pair.Value.Sent.Num() == 0) { continue; }

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!Pair.Value.Draft.IsEmpty()) { Obj->SetStringField(TEXT("draft"), Pair.Value.Draft); }

		TArray<TSharedPtr<FJsonValue>> Sent;
		for (const FString& S : Pair.Value.Sent) { Sent.Add(MakeShared<FJsonValueString>(S)); }
		if (Sent.Num() > 0) { Obj->SetArrayField(TEXT("sent"), Sent); }

		Root->SetObjectField(Pair.Key.ToString(EGuidFormats::DigitsWithHyphens), Obj);
	}

	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	FFileHelper::SaveStringToFile(Out, *GetFilePath());
}

const FString& FMCPChatDrafts::GetDraft(const FGuid& SessionId) const
{
	if (const FEntry* Entry = Entries.Find(SessionId)) { return Entry->Draft; }
	static const FString Empty;
	return Empty;
}

void FMCPChatDrafts::SetDraft(const FGuid& SessionId, const FString& Text)
{
	FEntry& Entry = Entries.FindOrAdd(SessionId);
	if (Entry.Draft == Text) { return; }
	Entry.Draft = Text;
	Save();
}

void FMCPChatDrafts::ClearDraft(const FGuid& SessionId)
{
	if (FEntry* Entry = Entries.Find(SessionId))
	{
		if (Entry->Draft.IsEmpty()) { return; }
		Entry->Draft.Reset();
		Save();
	}
}

void FMCPChatDrafts::PushSent(const FGuid& SessionId, const FString& Text)
{
	if (Text.IsEmpty()) { return; }

	FEntry& Entry = Entries.FindOrAdd(SessionId);
	if (Entry.Sent.Num() > 0 && Entry.Sent.Last() == Text) { return; }

	Entry.Sent.Add(Text);
	if (Entry.Sent.Num() > MaxRecallPerSession)
	{
		Entry.Sent.RemoveAt(0, Entry.Sent.Num() - MaxRecallPerSession, EAllowShrinking::No);
	}
	Save();
}

bool FMCPChatDrafts::GetRecalled(const FGuid& SessionId, int32 Index, FString& OutText) const
{
	const FEntry* Entry = Entries.Find(SessionId);
	if (!Entry || Index < 0 || Index >= Entry->Sent.Num()) { return false; }

	OutText = Entry->Sent[Entry->Sent.Num() - 1 - Index];
	return true;
}

int32 FMCPChatDrafts::GetRecallCount(const FGuid& SessionId) const
{
	const FEntry* Entry = Entries.Find(SessionId);
	return Entry ? Entry->Sent.Num() : 0;
}

void FMCPChatDrafts::ForgetSession(const FGuid& SessionId)
{
	if (Entries.Remove(SessionId) > 0) { Save(); }
}
