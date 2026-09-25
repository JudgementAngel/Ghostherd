// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatTypes.h"

namespace
{
	const TCHAR* RoleToString(EChatRole Role)
	{
		switch (Role)
		{
		case EChatRole::User:      return TEXT("user");
		case EChatRole::Assistant: return TEXT("assistant");
		case EChatRole::System:    return TEXT("system");
		case EChatRole::Tool:      return TEXT("tool");
		}
		return TEXT("user");
	}

	EChatRole RoleFromString(const FString& In)
	{
		if (In == TEXT("assistant")) return EChatRole::Assistant;
		if (In == TEXT("system"))    return EChatRole::System;
		if (In == TEXT("tool"))      return EChatRole::Tool;
		return EChatRole::User;
	}

	const TCHAR* BlockTypeToString(FChatContentBlock::EType Type)
	{
		using EType = FChatContentBlock::EType;
		switch (Type)
		{
		case EType::Text:       return TEXT("text");
		case EType::Thinking:   return TEXT("thinking");
		case EType::Image:      return TEXT("image");
		case EType::File:       return TEXT("file");
		case EType::ToolCall:   return TEXT("tool_call");
		case EType::ToolResult: return TEXT("tool_result");
		case EType::Refusal:    return TEXT("refusal");
		case EType::Error:      return TEXT("error");
		case EType::Divider:    return TEXT("divider");
		case EType::ContextRef: return TEXT("context_ref");
		case EType::Plan:       return TEXT("plan");
		}
		return TEXT("text");
	}

	FChatContentBlock::EType BlockTypeFromString(const FString& In)
	{
		using EType = FChatContentBlock::EType;
		if (In == TEXT("thinking"))    return EType::Thinking;
		if (In == TEXT("image"))       return EType::Image;
		if (In == TEXT("file"))        return EType::File;
		if (In == TEXT("tool_call"))   return EType::ToolCall;
		if (In == TEXT("tool_result")) return EType::ToolResult;
		if (In == TEXT("refusal"))     return EType::Refusal;
		if (In == TEXT("error"))       return EType::Error;
		if (In == TEXT("divider"))     return EType::Divider;
		if (In == TEXT("context_ref")) return EType::ContextRef;
		if (In == TEXT("plan"))        return EType::Plan;
		return EType::Text;
	}
}

// ============================================================================
// FChatContentBlock
// ============================================================================

FChatContentBlock FChatContentBlock::MakeText(const FString& InText)
{
	FChatContentBlock B; B.Type = EType::Text; B.Text = InText; return B;
}

FChatContentBlock FChatContentBlock::MakeThinking(const FString& InText)
{
	FChatContentBlock B; B.Type = EType::Thinking; B.Text = InText; return B;
}

FChatContentBlock FChatContentBlock::MakeError(const FString& InText)
{
	FChatContentBlock B; B.Type = EType::Error; B.Text = InText; return B;
}

FChatContentBlock FChatContentBlock::MakeDivider(const FString& InText)
{
	FChatContentBlock B; B.Type = EType::Divider; B.Text = InText; return B;
}

TSharedPtr<FJsonObject> FChatContentBlock::ToJson() const
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), BlockTypeToString(Type));

	// Only write fields that carry meaning for this block type — transcripts are
	// meant to be human-readable and greppable, not a struct dump.
	if (!Text.IsEmpty())            { O->SetStringField(TEXT("text"), Text); }
	if (!MimeType.IsEmpty())        { O->SetStringField(TEXT("mimeType"), MimeType); }
	if (!StoredPath.IsEmpty())      { O->SetStringField(TEXT("storedPath"), StoredPath); }
	if (!DisplayName.IsEmpty())     { O->SetStringField(TEXT("displayName"), DisplayName); }
	if (SizeBytes > 0)              { O->SetNumberField(TEXT("sizeBytes"), static_cast<double>(SizeBytes)); }

	if (Type == EType::ToolCall || Type == EType::ToolResult)
	{
		O->SetStringField(TEXT("toolCallId"), ToolCallId.ToString(EGuidFormats::DigitsWithHyphens));
		O->SetStringField(TEXT("toolName"), ToolName);
		if (!ProviderCallId.IsEmpty()) { O->SetStringField(TEXT("providerCallId"), ProviderCallId); }
		if (ToolArgs.IsValid())         { O->SetObjectField(TEXT("toolArgs"), ToolArgs); }
		if (ToolResult.IsValid())       { O->SetObjectField(TEXT("toolResult"), ToolResult); }
		if (!ToolResultText.IsEmpty())  { O->SetStringField(TEXT("toolResultText"), ToolResultText); }
		if (bToolIsError)               { O->SetBoolField(TEXT("toolIsError"), true); }
		if (DurationSeconds > 0.0)      { O->SetNumberField(TEXT("durationSeconds"), DurationSeconds); }
	}

	if (Type == EType::ContextRef)
	{
		O->SetStringField(TEXT("contextKind"), ContextKind);
		if (!ContextTarget.IsEmpty()) { O->SetStringField(TEXT("contextTarget"), ContextTarget); }
		if (bContextPinned)           { O->SetBoolField(TEXT("contextPinned"), true); }
	}
	return O;
}

FChatContentBlock FChatContentBlock::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FChatContentBlock B;
	if (!Json.IsValid()) { return B; }

	FString TypeStr;
	Json->TryGetStringField(TEXT("type"), TypeStr);
	B.Type = BlockTypeFromString(TypeStr);

	Json->TryGetStringField(TEXT("text"), B.Text);
	Json->TryGetStringField(TEXT("mimeType"), B.MimeType);
	Json->TryGetStringField(TEXT("storedPath"), B.StoredPath);
	Json->TryGetStringField(TEXT("displayName"), B.DisplayName);

	double Size = 0.0;
	if (Json->TryGetNumberField(TEXT("sizeBytes"), Size)) { B.SizeBytes = static_cast<int64>(Size); }

	FString IdStr;
	if (Json->TryGetStringField(TEXT("toolCallId"), IdStr)) { FGuid::Parse(IdStr, B.ToolCallId); }
	Json->TryGetStringField(TEXT("toolName"), B.ToolName);
	Json->TryGetStringField(TEXT("providerCallId"), B.ProviderCallId);
	Json->TryGetStringField(TEXT("toolResultText"), B.ToolResultText);
	Json->TryGetBoolField(TEXT("toolIsError"), B.bToolIsError);
	Json->TryGetNumberField(TEXT("durationSeconds"), B.DurationSeconds);

	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (Json->TryGetObjectField(TEXT("toolArgs"), Obj) && Obj)   { B.ToolArgs = *Obj; }
	if (Json->TryGetObjectField(TEXT("toolResult"), Obj) && Obj) { B.ToolResult = *Obj; }

	Json->TryGetStringField(TEXT("contextKind"), B.ContextKind);
	Json->TryGetStringField(TEXT("contextTarget"), B.ContextTarget);
	Json->TryGetBoolField(TEXT("contextPinned"), B.bContextPinned);

	// Anything loaded from disk is by definition finished.
	B.bIsComplete = true;
	return B;
}

// ============================================================================
// FChatUsage
// ============================================================================

void FChatUsage::Accumulate(const FChatUsage& Other)
{
	InputTokens      += Other.InputTokens;
	OutputTokens     += Other.OutputTokens;
	CacheReadTokens  += Other.CacheReadTokens;
	CacheWriteTokens += Other.CacheWriteTokens;
	EstimatedCostUsd += Other.EstimatedCostUsd;
}

TSharedPtr<FJsonObject> FChatUsage::ToJson() const
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("inputTokens"), InputTokens);
	O->SetNumberField(TEXT("outputTokens"), OutputTokens);
	if (CacheReadTokens > 0)  { O->SetNumberField(TEXT("cacheReadTokens"), CacheReadTokens); }
	if (CacheWriteTokens > 0) { O->SetNumberField(TEXT("cacheWriteTokens"), CacheWriteTokens); }
	if (EstimatedCostUsd > 0) { O->SetNumberField(TEXT("estimatedCostUsd"), EstimatedCostUsd); }
	return O;
}

FChatUsage FChatUsage::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FChatUsage U;
	if (!Json.IsValid()) { return U; }
	double N = 0.0;
	if (Json->TryGetNumberField(TEXT("inputTokens"), N))      { U.InputTokens = static_cast<int32>(N); }
	if (Json->TryGetNumberField(TEXT("outputTokens"), N))     { U.OutputTokens = static_cast<int32>(N); }
	if (Json->TryGetNumberField(TEXT("cacheReadTokens"), N))  { U.CacheReadTokens = static_cast<int32>(N); }
	if (Json->TryGetNumberField(TEXT("cacheWriteTokens"), N)) { U.CacheWriteTokens = static_cast<int32>(N); }
	Json->TryGetNumberField(TEXT("estimatedCostUsd"), U.EstimatedCostUsd);
	return U;
}

// ============================================================================
// FChatModelParams
// ============================================================================

TSharedPtr<FJsonObject> FChatModelParams::ToJson() const
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("effort"), Effort);
	O->SetNumberField(TEXT("maxOutputTokens"), MaxOutputTokens);
	O->SetBoolField(TEXT("thinkingEnabled"), bThinkingEnabled);
	O->SetBoolField(TEXT("thinkingVisible"), bThinkingVisible);
	O->SetBoolField(TEXT("promptCaching"), bPromptCaching);
	O->SetBoolField(TEXT("catalogToolExposure"), bCatalogToolExposure);
	O->SetNumberField(TEXT("maxToolIterations"), MaxToolIterations);
	if (bHasSampling)
	{
		O->SetNumberField(TEXT("temperature"), Temperature);
	}
	return O;
}

FChatModelParams FChatModelParams::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FChatModelParams P;
	if (!Json.IsValid()) { return P; }
	Json->TryGetStringField(TEXT("effort"), P.Effort);
	double N = 0.0;
	if (Json->TryGetNumberField(TEXT("maxOutputTokens"), N))   { P.MaxOutputTokens = static_cast<int32>(N); }
	if (Json->TryGetNumberField(TEXT("maxToolIterations"), N)) { P.MaxToolIterations = static_cast<int32>(N); }
	Json->TryGetBoolField(TEXT("thinkingEnabled"), P.bThinkingEnabled);
	Json->TryGetBoolField(TEXT("thinkingVisible"), P.bThinkingVisible);
	Json->TryGetBoolField(TEXT("promptCaching"), P.bPromptCaching);
	Json->TryGetBoolField(TEXT("catalogToolExposure"), P.bCatalogToolExposure);
	if (Json->TryGetNumberField(TEXT("temperature"), N))
	{
		P.bHasSampling = true;
		P.Temperature = static_cast<float>(N);
	}
	return P;
}

// ============================================================================
// FChatMessage
// ============================================================================

FString FChatMessage::GetPlainText(bool bIncludeThinking) const
{
	TArray<FString> Parts;
	for (const FChatContentBlock& B : Blocks)
	{
		if (B.Type == FChatContentBlock::EType::Text
			|| B.Type == FChatContentBlock::EType::Refusal
			|| B.Type == FChatContentBlock::EType::Error
			|| (bIncludeThinking && B.Type == FChatContentBlock::EType::Thinking))
		{
			if (!B.Text.IsEmpty()) { Parts.Add(B.Text); }
		}
	}
	return FString::Join(Parts, TEXT("\n\n"));
}

FString FChatMessage::GetPreview(int32 MaxChars) const
{
	FString Text = GetPlainText(false);
	Text.ReplaceInline(TEXT("\r"), TEXT(" "));
	Text.ReplaceInline(TEXT("\n"), TEXT(" "));
	Text.TrimStartAndEndInline();

	if (Text.IsEmpty())
	{
		// A turn can legitimately be nothing but tool calls; say so rather than
		// showing a blank row in the history rail.
		for (const FChatContentBlock& B : Blocks)
		{
			if (B.Type == FChatContentBlock::EType::ToolCall)
			{
				return FString::Printf(TEXT("[%s]"), *B.ToolName);
			}
		}
		return FString();
	}

	return Text.Len() <= MaxChars ? Text : (Text.Left(MaxChars - 1) + TEXT("…"));
}

TSharedPtr<FJsonObject> FChatMessage::ToJson() const
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("id"), Id.ToString(EGuidFormats::DigitsWithHyphens));
	O->SetStringField(TEXT("role"), RoleToString(Role));
	O->SetStringField(TEXT("timestamp"), Timestamp.ToIso8601());
	if (!ModelId.IsEmpty())    { O->SetStringField(TEXT("modelId"), ModelId); }
	if (!BackendId.IsEmpty())  { O->SetStringField(TEXT("backendId"), BackendId); }
	if (ParentId.IsValid())    { O->SetStringField(TEXT("parentId"), ParentId.ToString(EGuidFormats::DigitsWithHyphens)); }
	if (!Usage.IsEmpty())      { O->SetObjectField(TEXT("usage"), Usage.ToJson()); }

	TArray<TSharedPtr<FJsonValue>> BlockArray;
	for (const FChatContentBlock& B : Blocks)
	{
		BlockArray.Add(MakeShared<FJsonValueObject>(B.ToJson()));
	}
	O->SetArrayField(TEXT("blocks"), BlockArray);
	return O;
}

TSharedPtr<FChatMessage> FChatMessage::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid()) { return nullptr; }

	TSharedPtr<FChatMessage> M = MakeShared<FChatMessage>();

	FString Str;
	if (Json->TryGetStringField(TEXT("id"), Str)) { FGuid::Parse(Str, M->Id); }
	if (!M->Id.IsValid()) { M->Id = FGuid::NewGuid(); }   // tolerate a hand-edited file

	Json->TryGetStringField(TEXT("role"), Str);
	M->Role = RoleFromString(Str);

	if (Json->TryGetStringField(TEXT("timestamp"), Str)) { FDateTime::ParseIso8601(*Str, M->Timestamp); }
	Json->TryGetStringField(TEXT("modelId"), M->ModelId);
	Json->TryGetStringField(TEXT("backendId"), M->BackendId);
	if (Json->TryGetStringField(TEXT("parentId"), Str)) { FGuid::Parse(Str, M->ParentId); }

	const TSharedPtr<FJsonObject>* UsageObj = nullptr;
	if (Json->TryGetObjectField(TEXT("usage"), UsageObj) && UsageObj)
	{
		M->Usage = FChatUsage::FromJson(*UsageObj);
	}

	const TArray<TSharedPtr<FJsonValue>>* BlockArray = nullptr;
	if (Json->TryGetArrayField(TEXT("blocks"), BlockArray) && BlockArray)
	{
		for (const TSharedPtr<FJsonValue>& V : *BlockArray)
		{
			const TSharedPtr<FJsonObject>* BObj = nullptr;
			if (V.IsValid() && V->TryGetObject(BObj) && BObj)
			{
				M->Blocks.Add(FChatContentBlock::FromJson(*BObj));
			}
		}
	}
	return M;
}

// ============================================================================
// FChatSession
// ============================================================================

TSharedPtr<FChatSession> FChatSession::CreateNew(const FString& InBackendId, const FString& InModelId)
{
	TSharedPtr<FChatSession> S = MakeShared<FChatSession>();
	S->Id        = FGuid::NewGuid();
	S->CreatedAt = FDateTime::UtcNow();
	S->UpdatedAt = S->CreatedAt;
	S->BackendId = InBackendId;
	S->ModelId   = InModelId;
	return S;
}

FChatMessagePtr FChatSession::FindMessage(const FGuid& MessageId) const
{
	for (const FChatMessagePtr& M : Messages)
	{
		if (M.IsValid() && M->Id == MessageId) { return M; }
	}
	return nullptr;
}

TArray<FChatMessagePtr> FChatSession::GetSiblings(const FGuid& MessageId) const
{
	TArray<FChatMessagePtr> Siblings;
	const FChatMessagePtr Target = FindMessage(MessageId);
	if (!Target.IsValid()) { return Siblings; }

	for (const FChatMessagePtr& M : Messages)
	{
		if (M.IsValid() && M->ParentId == Target->ParentId)
		{
			Siblings.Add(M);
		}
	}
	// Messages is append-ordered, so this is already oldest-first.
	return Siblings;
}

void FChatSession::SetActiveBranch(const FGuid& MessageId)
{
	const FChatMessagePtr Target = FindMessage(MessageId);
	if (!Target.IsValid()) { return; }
	ActiveChildByParent.Add(Target->ParentId, Target->Id);
}

TArray<FChatMessagePtr> FChatSession::BuildActivePath() const
{
	// Index children by parent once — a linear scan per level would be O(n²) on a
	// long transcript, and this runs on every list refresh.
	TMap<FGuid, TArray<FChatMessagePtr>> ChildrenByParent;
	for (const FChatMessagePtr& M : Messages)
	{
		if (M.IsValid())
		{
			ChildrenByParent.FindOrAdd(M->ParentId).Add(M);
		}
	}

	TArray<FChatMessagePtr> Path;
	FGuid Cursor;   // invalid GUID == the root bucket

	// Guard against a malformed file producing a parent cycle.
	TSet<FGuid> Visited;

	while (true)
	{
		const TArray<FChatMessagePtr>* Children = ChildrenByParent.Find(Cursor);
		if (!Children || Children->Num() == 0) { break; }

		FChatMessagePtr Next;
		if (const FGuid* Preferred = ActiveChildByParent.Find(Cursor))
		{
			for (const FChatMessagePtr& C : *Children)
			{
				if (C->Id == *Preferred) { Next = C; break; }
			}
		}
		if (!Next.IsValid())
		{
			Next = Children->Last();   // default branch: the newest child
		}

		if (Visited.Contains(Next->Id))
		{
			break;   // cycle — stop rather than hang
		}
		Visited.Add(Next->Id);

		Path.Add(Next);
		Cursor = Next->Id;
	}

	return Path;
}

void FChatSession::AppendMessage(const FChatMessagePtr& Message)
{
	if (!Message.IsValid()) { return; }

	if (!Message->Id.IsValid())      { Message->Id = FGuid::NewGuid(); }
	if (Message->Timestamp == FDateTime()) { Message->Timestamp = FDateTime::UtcNow(); }

	// Attach to the tip of the active path unless the caller already chose a parent.
	if (!Message->ParentId.IsValid())
	{
		const TArray<FChatMessagePtr> Path = BuildActivePath();
		if (Path.Num() > 0)
		{
			Message->ParentId = Path.Last()->Id;
		}
	}

	Messages.Add(Message);
	// A new message becomes the active branch at its fork, so appending after a
	// retry follows the branch the user just created.
	ActiveChildByParent.Add(Message->ParentId, Message->Id);
	UpdatedAt = FDateTime::UtcNow();
}

void FChatSession::EnsureTitle()
{
	if (!Title.IsEmpty()) { return; }

	for (const FChatMessagePtr& M : Messages)
	{
		if (M.IsValid() && M->Role == EChatRole::User)
		{
			const FString Preview = M->GetPreview(48);
			if (!Preview.IsEmpty())
			{
				Title = Preview;
				return;
			}
		}
	}
}

TSharedPtr<FJsonObject> FChatSession::ToJson() const
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("schemaVersion"), SchemaVersion);
	O->SetStringField(TEXT("id"), Id.ToString(EGuidFormats::DigitsWithHyphens));
	O->SetStringField(TEXT("title"), Title);
	O->SetStringField(TEXT("createdAt"), CreatedAt.ToIso8601());
	O->SetStringField(TEXT("updatedAt"), UpdatedAt.ToIso8601());
	O->SetStringField(TEXT("backendId"), BackendId);
	O->SetStringField(TEXT("modelId"), ModelId);
	O->SetBoolField(TEXT("pinned"), bPinned);
	O->SetObjectField(TEXT("params"), Params.ToJson());
	O->SetObjectField(TEXT("workingSet"), WorkingSet.ToJson());

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FChatMessagePtr& M : Messages)
	{
		if (M.IsValid()) { Arr.Add(MakeShared<FJsonValueObject>(M->ToJson())); }
	}
	O->SetArrayField(TEXT("messages"), Arr);

	Arr.Reset();
	for (const FString& S : PinnedContext) { Arr.Add(MakeShared<FJsonValueString>(S)); }
	O->SetArrayField(TEXT("pinnedContext"), Arr);

	Arr.Reset();
	for (const FString& S : Tags) { Arr.Add(MakeShared<FJsonValueString>(S)); }
	O->SetArrayField(TEXT("tags"), Arr);

	if (ActiveChildByParent.Num() > 0)
	{
		TSharedPtr<FJsonObject> Branches = MakeShared<FJsonObject>();
		for (const TPair<FGuid, FGuid>& Pair : ActiveChildByParent)
		{
			Branches->SetStringField(
				Pair.Key.ToString(EGuidFormats::DigitsWithHyphens),
				Pair.Value.ToString(EGuidFormats::DigitsWithHyphens));
		}
		O->SetObjectField(TEXT("activeBranches"), Branches);
	}

	return O;
}

TSharedPtr<FChatSession> FChatSession::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid()) { return nullptr; }

	TSharedPtr<FChatSession> S = MakeShared<FChatSession>();

	double Version = 1.0;
	Json->TryGetNumberField(TEXT("schemaVersion"), Version);
	S->SchemaVersion = static_cast<int32>(Version);
	// Forward migration hook: when CurrentSchemaVersion moves past 1, branch here
	// per source version before reading fields that changed shape.

	FString Str;
	if (Json->TryGetStringField(TEXT("id"), Str)) { FGuid::Parse(Str, S->Id); }
	if (!S->Id.IsValid()) { return nullptr; }   // an id-less file is unusable

	Json->TryGetStringField(TEXT("title"), S->Title);
	if (Json->TryGetStringField(TEXT("createdAt"), Str)) { FDateTime::ParseIso8601(*Str, S->CreatedAt); }
	if (Json->TryGetStringField(TEXT("updatedAt"), Str)) { FDateTime::ParseIso8601(*Str, S->UpdatedAt); }
	Json->TryGetStringField(TEXT("backendId"), S->BackendId);
	Json->TryGetStringField(TEXT("modelId"), S->ModelId);
	Json->TryGetBoolField(TEXT("pinned"), S->bPinned);

	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (Json->TryGetObjectField(TEXT("params"), Obj) && Obj)     { S->Params = FChatModelParams::FromJson(*Obj); }
	if (Json->TryGetObjectField(TEXT("workingSet"), Obj) && Obj) { S->WorkingSet.FromJson(*Obj); }

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Json->TryGetArrayField(TEXT("messages"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject>* MObj = nullptr;
			if (V.IsValid() && V->TryGetObject(MObj) && MObj)
			{
				if (FChatMessagePtr M = FChatMessage::FromJson(*MObj))
				{
					S->Messages.Add(M);
				}
			}
		}
	}

	if (Json->TryGetArrayField(TEXT("pinnedContext"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr) { S->PinnedContext.Add(V->AsString()); }
	}
	if (Json->TryGetArrayField(TEXT("tags"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr) { S->Tags.Add(V->AsString()); }
	}

	if (Json->TryGetObjectField(TEXT("activeBranches"), Obj) && Obj)
	{
		for (const auto& Pair : (*Obj)->Values)
		{
			FGuid ParentGuid, ChildGuid;
			if (FGuid::Parse(Pair.Key, ParentGuid) && Pair.Value.IsValid()
				&& FGuid::Parse(Pair.Value->AsString(), ChildGuid))
			{
				S->ActiveChildByParent.Add(ParentGuid, ChildGuid);
			}
		}
	}
	// An invalid (all-zero) GUID key is legitimate here — it is the root fork.

	return S;
}

// ============================================================================
// FChatSessionSummary
// ============================================================================

FChatSessionSummary FChatSessionSummary::FromSession(const FChatSession& Session)
{
	FChatSessionSummary Sum;
	Sum.Id           = Session.Id;
	Sum.Title        = Session.Title;
	Sum.UpdatedAt    = Session.UpdatedAt;
	Sum.BackendId    = Session.BackendId;
	Sum.ModelId      = Session.ModelId;
	Sum.MessageCount = Session.Messages.Num();
	Sum.bPinned      = Session.bPinned;
	Sum.Tags         = Session.Tags.Array();

	// Preview is the LAST message on the active path — "where did I leave off"
	// is more useful in a history list than "how did this start".
	const TArray<FChatMessagePtr> Path = Session.BuildActivePath();
	if (Path.Num() > 0 && Path.Last().IsValid())
	{
		Sum.Preview = Path.Last()->GetPreview(80);
	}
	return Sum;
}

TSharedPtr<FJsonObject> FChatSessionSummary::ToJson() const
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("id"), Id.ToString(EGuidFormats::DigitsWithHyphens));
	O->SetStringField(TEXT("title"), Title);
	O->SetStringField(TEXT("preview"), Preview);
	O->SetStringField(TEXT("updatedAt"), UpdatedAt.ToIso8601());
	O->SetStringField(TEXT("backendId"), BackendId);
	O->SetStringField(TEXT("modelId"), ModelId);
	O->SetNumberField(TEXT("messageCount"), MessageCount);
	O->SetBoolField(TEXT("pinned"), bPinned);

	TArray<TSharedPtr<FJsonValue>> TagArr;
	for (const FString& T : Tags) { TagArr.Add(MakeShared<FJsonValueString>(T)); }
	O->SetArrayField(TEXT("tags"), TagArr);
	return O;
}

FChatSessionSummary FChatSessionSummary::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FChatSessionSummary S;
	if (!Json.IsValid()) { return S; }

	FString Str;
	if (Json->TryGetStringField(TEXT("id"), Str)) { FGuid::Parse(Str, S.Id); }
	Json->TryGetStringField(TEXT("title"), S.Title);
	Json->TryGetStringField(TEXT("preview"), S.Preview);
	if (Json->TryGetStringField(TEXT("updatedAt"), Str)) { FDateTime::ParseIso8601(*Str, S.UpdatedAt); }
	Json->TryGetStringField(TEXT("backendId"), S.BackendId);
	Json->TryGetStringField(TEXT("modelId"), S.ModelId);
	Json->TryGetBoolField(TEXT("pinned"), S.bPinned);

	double N = 0.0;
	if (Json->TryGetNumberField(TEXT("messageCount"), N)) { S.MessageCount = static_cast<int32>(N); }

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Json->TryGetArrayField(TEXT("tags"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr) { S.Tags.Add(V->AsString()); }
	}
	return S;
}
