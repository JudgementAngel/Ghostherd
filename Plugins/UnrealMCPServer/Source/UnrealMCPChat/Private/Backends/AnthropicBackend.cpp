// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Backends/AnthropicBackend.h"
#include "MCPChatAttachments.h"
#include "MCPChatModelCatalog.h"
#include "MCPChatSettings.h"
#include "MCPChatStore.h"
#include "MCPChatToolBridge.h"
#include "MCPChatSecretStore.h"
#include "UnrealMCPChatModule.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "MCPTransactionScope.h"

#define LOCTEXT_NAMESPACE "AnthropicBackend"

namespace
{
	const TCHAR* AnthropicVersionHeader = TEXT("2023-06-01");
}

FAnthropicBackend::FAnthropicBackend()
{
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FAnthropicBackend::TickStream), 0.f);
}

FAnthropicBackend::~FAnthropicBackend()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (HttpRequest.IsValid())
	{
		HttpRequest->OnProcessRequestComplete().Unbind();
		HttpRequest->CancelRequest();
		HttpRequest.Reset();
	}
}

FText FAnthropicBackend::GetDisplayName() const
{
	return LOCTEXT("Anthropic", "Anthropic");
}

bool FAnthropicBackend::IsAvailable(FText& OutReason) const
{
	EMCPSecretSource Source = EMCPSecretSource::None;
	FString Key;
	if (!FMCPChatSecretStore::Get().GetSecret(TEXT("anthropic"), Key, &Source))
	{
		OutReason = FText::Format(
			LOCTEXT("NoKey", "No API key. Set {0} or add one in Settings."),
			FText::FromString(FMCPChatSecretStore::GetEnvironmentVariableName(TEXT("anthropic"))));
		return false;
	}

	OutReason = FText::Format(LOCTEXT("KeyFrom", "Key from {0}"),
		FMCPChatSecretStore::DescribeSource(Source));
	return true;
}

TArray<FChatModelInfo> FAnthropicBackend::GetModels() const
{
	return FMCPChatModelCatalog::Get().GetModelsForProvider(TEXT("anthropic"));
}

// ============================================================================
// Request construction
// ============================================================================

TArray<TSharedPtr<FJsonValue>> FAnthropicBackend::BuildMessagesArray(const FChatTurnRequest& Request) const
{
	TArray<TSharedPtr<FJsonValue>> Out;

	auto MakeTextContent = [](const FString& Text)
	{
		TSharedPtr<FJsonObject> Block = MakeShared<FJsonObject>();
		Block->SetStringField(TEXT("type"), TEXT("text"));
		Block->SetStringField(TEXT("text"), Text);
		return Block;
	};

	// Attachments are stored under the session folder, so the id is needed to find
	// them again when a resumed conversation is re-sent.
	const FGuid SessionIdForAttachments = Request.SessionId;

	const FChatModelInfo* ModelInfo = FMCPChatModelCatalog::Get().FindModel(Request.ModelId);
	// Unknown model → assume no vision. Sending an image to a model that cannot take
	// one fails the whole request; omitting one costs a sentence of explanation.
	const bool bModelHasVision = ModelInfo && ModelInfo->bVision;

	/**
	 * An image block, base64-inlined from the session's attachments folder.
	 *
	 * Returns null rather than an empty block on any failure — a malformed image
	 * block makes the API reject the ENTIRE request, so a missing picture must
	 * degrade to a missing picture, not to a failed message.
	 */
	auto MakeImageContent = [](const FChatContentBlock& B, const FGuid& SessionId) -> TSharedPtr<FJsonObject>
	{
		if (B.StoredPath.IsEmpty() || !FMCPChatAttachments::IsImageMime(B.MimeType)) { return nullptr; }

		const FString Absolute = FMCPChatStore::GetAttachmentsDirectory(SessionId) / B.StoredPath;
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Absolute) || Bytes.Num() == 0) { return nullptr; }

		TSharedPtr<FJsonObject> Source = MakeShared<FJsonObject>();
		Source->SetStringField(TEXT("type"), TEXT("base64"));
		Source->SetStringField(TEXT("media_type"), B.MimeType);
		Source->SetStringField(TEXT("data"), FBase64::Encode(Bytes));

		TSharedPtr<FJsonObject> Block = MakeShared<FJsonObject>();
		Block->SetStringField(TEXT("type"), TEXT("image"));
		Block->SetObjectField(TEXT("source"), Source);
		return Block;
	};

	FString PendingRole;
	TArray<TSharedPtr<FJsonValue>> PendingContent;

	auto FlushPending = [&]()
	{
		if (PendingRole.IsEmpty() || PendingContent.Num() == 0) { return; }
		TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
		Msg->SetStringField(TEXT("role"), PendingRole);
		Msg->SetArrayField(TEXT("content"), PendingContent);
		Out.Add(MakeShared<FJsonValueObject>(Msg));
		PendingRole.Reset();
		PendingContent.Reset();
	};

	for (const FChatMessagePtr& M : Request.History)
	{
		if (!M.IsValid()) { continue; }

		// System turns are not messages in this API; Phase 5 lifts them to the
		// top-level `system` field.
		//
		// Tool turns come from the `#tool` fast path (Phase 6): the user ran something
		// themselves, with no model round-trip. Those MUST still reach the model, or
		// "now scale it" after a manual #spawn_actor refers to something the model has
		// never heard of. They are reported as user-role narration rather than as a
		// tool_use/tool_result pair, because there is no provider call id to echo — no
		// provider ever asked for this call.
		if (M->Role == EChatRole::Tool)
		{
			FString Narration;
			for (const FChatContentBlock& B : M->Blocks)
			{
				if (B.Type != FChatContentBlock::EType::ToolCall) { continue; }

				FString ArgsText;
				if (B.ToolArgs.IsValid() && B.ToolArgs->Values.Num() > 0)
				{
					const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&ArgsText);
					FJsonSerializer::Serialize(B.ToolArgs.ToSharedRef(), W);
				}

				Narration.Append(FString::Printf(
					TEXT("[I ran the tool `%s`%s myself. Result%s: %s]\n"),
					*B.ToolName,
					ArgsText.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" with %s"), *ArgsText),
					B.bToolIsError ? TEXT(" (error)") : TEXT(""),
					B.ToolResultText.IsEmpty() ? TEXT("(no output)") : *B.ToolResultText));
			}

			if (!Narration.IsEmpty())
			{
				// Merge into an open user turn rather than flushing first — flushing
				// would emit two consecutive user messages, which the API rejects.
				if (PendingRole != TEXT("user"))
				{
					FlushPending();
					PendingRole = TEXT("user");
				}
				PendingContent.Add(MakeShared<FJsonValueObject>(MakeTextContent(Narration)));
			}
			continue;
		}

		if (M->Role != EChatRole::User && M->Role != EChatRole::Assistant) { continue; }

		const FString Role = (M->Role == EChatRole::User) ? TEXT("user") : TEXT("assistant");

		TArray<TSharedPtr<FJsonValue>> Content;
		TArray<TSharedPtr<FJsonValue>> ToolResults;   // must go in the NEXT user turn

		for (const FChatContentBlock& B : M->Blocks)
		{
			// Thinking blocks are NOT echoed back: replaying one requires preserving
			// its signature exactly, and we do not capture it. A reconstructed block
			// is rejected, so omitting is the correct conservative move.
			if (B.Type == FChatContentBlock::EType::Text && !B.Text.IsEmpty())
			{
				Content.Add(MakeShared<FJsonValueObject>(MakeTextContent(B.Text)));
			}
			else if (B.Type == FChatContentBlock::EType::ContextRef && !B.Text.IsEmpty())
			{
				// An @mention is a text block on the wire, fenced and labelled so the
				// model can tell attached context from something the user wrote. Without
				// the fence, a pasted level dump reads as an instruction.
				Content.Add(MakeShared<FJsonValueObject>(MakeTextContent(
					FString::Printf(TEXT("<context ref=\"%s\">\n%s\n</context>"),
						*(B.DisplayName.IsEmpty() ? B.ContextKind : B.DisplayName), *B.Text))));
			}
			else if (B.Type == FChatContentBlock::EType::Image)
			{
				// A model without vision returns a 400 for an image block, and a
				// conversation started on a vision model can be continued on one without
				// it. Name the picture instead of dropping it silently.
				if (!bModelHasVision)
				{
					Content.Add(MakeShared<FJsonValueObject>(MakeTextContent(
						FString::Printf(TEXT("[image '%s' attached, but the current model cannot see images]"),
							*B.DisplayName))));
				}
				else if (const TSharedPtr<FJsonObject> Image = MakeImageContent(B, SessionIdForAttachments))
				{
					Content.Add(MakeShared<FJsonValueObject>(Image));
				}
			}
			else if (B.Type == FChatContentBlock::EType::File && !B.Text.IsEmpty())
			{
				// Text files are inlined at attach time; binaries carry no Text and are
				// mentioned by name only, which is all the model can use anyway.
				Content.Add(MakeShared<FJsonValueObject>(MakeTextContent(
					FString::Printf(TEXT("<file name=\"%s\">\n%s\n</file>"), *B.DisplayName, *B.Text))));
			}
			else if (B.Type == FChatContentBlock::EType::ToolCall && !B.ProviderCallId.IsEmpty())
			{
				// Every tool_use in history needs its matching tool_result, or the API
				// rejects the whole request. Blocks without a provider id came from a
				// different backend and are skipped on both sides to stay consistent.
				TSharedPtr<FJsonObject> Use = MakeShared<FJsonObject>();
				Use->SetStringField(TEXT("type"), TEXT("tool_use"));
				Use->SetStringField(TEXT("id"), B.ProviderCallId);
				Use->SetStringField(TEXT("name"), B.ToolName);
				Use->SetObjectField(TEXT("input"), B.ToolArgs.IsValid() ? B.ToolArgs : MakeShared<FJsonObject>());
				Content.Add(MakeShared<FJsonValueObject>(Use));

				TSharedPtr<FJsonObject> ResultBlock = MakeShared<FJsonObject>();
				ResultBlock->SetStringField(TEXT("type"), TEXT("tool_result"));
				ResultBlock->SetStringField(TEXT("tool_use_id"), B.ProviderCallId);
				ResultBlock->SetStringField(TEXT("content"),
					B.ToolResultText.IsEmpty() ? TEXT("(no output)") : B.ToolResultText);
				if (B.bToolIsError) { ResultBlock->SetBoolField(TEXT("is_error"), true); }
				ToolResults.Add(MakeShared<FJsonValueObject>(ResultBlock));
			}
		}

		// The API rejects a message with empty content — drop the turn entirely.
		if (Content.Num() == 0) { continue; }

		// It also rejects consecutive same-role turns; merge instead.
		if (PendingRole == Role)
		{
			PendingContent.Append(Content);
		}
		else
		{
			FlushPending();
			PendingRole = Role;
			PendingContent = MoveTemp(Content);
		}

		// Tool results belong to the user turn that follows the assistant turn which
		// requested them — emit that turn immediately so ordering stays correct.
		if (ToolResults.Num() > 0)
		{
			FlushPending();
			PendingRole = TEXT("user");
			PendingContent = MoveTemp(ToolResults);
			FlushPending();
		}
	}
	FlushPending();

	return Out;
}

// ---------------------------------------------------------------------------
// Prompt caching
//
// Two breakpoints, both on genuinely stable prefixes:
//
//   1. The tool schemas. Identical on every request of every conversation — the
//      single best thing in this request to cache, and the reason Phase 5 had to
//      land before this was worth doing.
//   2. The conversation up to but NOT including the newest turn. Marking the tail
//      would write a new cache entry every message and read none of them; marking
//      one turn back means each request reads the previous request's write.
//
// A marker below the model's minimum is not free — it is billed at the
// cache-write premium and then ignored — so both are size-checked first.
// ---------------------------------------------------------------------------

namespace
{
	TSharedPtr<FJsonObject> MakeEphemeralCacheControl()
	{
		TSharedPtr<FJsonObject> CC = MakeShared<FJsonObject>();
		CC->SetStringField(TEXT("type"), TEXT("ephemeral"));
		return CC;
	}

	/** Very rough token count of a serialised JSON value — enough to compare against
	 *  a 1024-token threshold, which is all this decision needs. */
	int32 ApproxTokensOf(const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		int32 Chars = 0;
		for (const TSharedPtr<FJsonValue>& V : Values)
		{
			if (!V.IsValid()) { continue; }
			FString S;
			const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
			if (V->Type == EJson::Object && V->AsObject().IsValid())
			{
				FJsonSerializer::Serialize(V->AsObject().ToSharedRef(), W);
			}
			Chars += S.Len();
		}
		return FMath::CeilToInt(Chars / 2.2f);
	}
}

void FAnthropicBackend::ApplyToolCacheMarker(TArray<TSharedPtr<FJsonValue>>& Tools,
	const FChatTurnRequest& Request, const FChatModelInfo* Model) const
{
	if (!Request.Params.bPromptCaching || Tools.Num() == 0) { return; }

	const int32 MinTokens = Model ? Model->CacheMinTokens : 1024;
	if (ApproxTokensOf(Tools) < MinTokens) { return; }

	// One marker on the LAST tool caches the whole tools block — a marker per tool
	// would burn all four available breakpoints for no extra benefit.
	if (const TSharedPtr<FJsonObject> Last = Tools.Last().IsValid() ? Tools.Last()->AsObject() : nullptr)
	{
		Last->SetObjectField(TEXT("cache_control"), MakeEphemeralCacheControl());
	}
}

void FAnthropicBackend::ApplyHistoryCacheMarker(TArray<TSharedPtr<FJsonValue>>& Messages,
	const FChatTurnRequest& Request, const FChatModelInfo* Model) const
{
	// Below three turns there is no prefix worth caching: the whole conversation is
	// the new turn.
	if (!Request.Params.bPromptCaching || Messages.Num() < 3) { return; }

	const int32 MarkIndex = Messages.Num() - 2;

	TArray<TSharedPtr<FJsonValue>> Prefix;
	for (int32 i = 0; i <= MarkIndex; ++i) { Prefix.Add(Messages[i]); }

	const int32 MinTokens = Model ? Model->CacheMinTokens : 1024;
	if (ApproxTokensOf(Prefix) < MinTokens) { return; }

	const TSharedPtr<FJsonObject> Message = Messages[MarkIndex].IsValid()
		? Messages[MarkIndex]->AsObject() : nullptr;
	if (!Message.IsValid()) { return; }

	const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
	if (!Message->TryGetArrayField(TEXT("content"), Content) || !Content || Content->Num() == 0) { return; }

	// The marker goes on the last block of that message: cache_control means
	// "everything up to and including this", so anything earlier would leave the
	// rest of the same turn out of the cached prefix for no reason.
	if (const TSharedPtr<FJsonObject> LastBlock = Content->Last().IsValid() ? Content->Last()->AsObject() : nullptr)
	{
		LastBlock->SetObjectField(TEXT("cache_control"), MakeEphemeralCacheControl());
	}
}

TSharedPtr<FJsonObject> FAnthropicBackend::BuildRequestBody(const FChatTurnRequest& Request) const
{
	const FChatModelInfo* Model = FMCPChatModelCatalog::Get().FindModel(Request.ModelId);

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Request.ModelId);
	Body->SetBoolField(TEXT("stream"), true);

	// max_tokens caps thinking + response text TOGETHER on models where thinking is
	// on, so a value sized for the answer alone truncates mid-sentence. Default
	// high; we always stream, so there is no HTTP-timeout reason to keep it small.
	int32 MaxTokens = Request.Params.MaxOutputTokens > 0 ? Request.Params.MaxOutputTokens : 64000;
	if (Model && Model->MaxOutputTokens > 0)
	{
		MaxTokens = FMath::Min(MaxTokens, Model->MaxOutputTokens);
	}
	Body->SetNumberField(TEXT("max_tokens"), MaxTokens);

	TArray<TSharedPtr<FJsonValue>> Messages = BuildMessagesArray(Request);
	ApplyHistoryCacheMarker(Messages, Request, Model);
	Body->SetArrayField(TEXT("messages"), Messages);

	// ---- Thinking ----
	const bool bModelSupportsThinking = !Model || Model->EffortLevels.Num() > 0;
	if (Request.Params.bThinkingEnabled && bModelSupportsThinking)
	{
		TSharedPtr<FJsonObject> Thinking = MakeShared<FJsonObject>();
		Thinking->SetStringField(TEXT("type"), TEXT("adaptive"));

		// Without this the API streams thinking blocks whose text is EMPTY, and the
		// UI's thinking section renders blank — which reads as a bug, not a setting.
		if (Request.Params.bThinkingVisible)
		{
			Thinking->SetStringField(TEXT("display"), TEXT("summarized"));
		}
		Body->SetObjectField(TEXT("thinking"), Thinking);
	}

	// ---- Effort ----
	if (Model && Model->EffortLevels.Num() > 0 && !Request.Params.Effort.IsEmpty()
		&& Model->EffortLevels.Contains(Request.Params.Effort))
	{
		TSharedPtr<FJsonObject> OutputConfig = MakeShared<FJsonObject>();
		OutputConfig->SetStringField(TEXT("effort"), Request.Params.Effort);
		Body->SetObjectField(TEXT("output_config"), OutputConfig);
	}

	// ---- Tools ----
	// Catalog mode by default: all 450 schemas would be ~62K tokens on the first
	// message, more than most whole conversations. The model discovers the rest via
	// search_tools / get_tool_schemas, which are in the core set.
	if (!Model || Model->bTools)
	{
		TArray<TSharedPtr<FJsonValue>> ToolSchemas =
			FMCPChatToolBridge::Get().BuildAnthropicToolSchemas(Request.Params.bCatalogToolExposure);
		if (ToolSchemas.Num() > 0)
		{
			ApplyToolCacheMarker(ToolSchemas, Request, Model);
			Body->SetArrayField(TEXT("tools"), ToolSchemas);
		}
	}

	// ---- Sampling ----
	// Sent ONLY when the catalogue says the model accepts it. Opus 5, Sonnet 5,
	// Opus 4.8/4.7 and Fable 5 return a 400 for temperature/top_p/top_k, and the
	// catalogue defaults this to false so an unknown model is treated as strict.
	if (Model && Model->bSampling && Request.Params.bHasSampling)
	{
		Body->SetNumberField(TEXT("temperature"), Request.Params.Temperature);
	}

	return Body;
}

// ============================================================================
// Send
// ============================================================================

void FAnthropicBackend::SendTurn(const FChatTurnRequest& Request)
{
	// The guard has to let ONE caller through while a turn is active: SendToolResults
	// re-enters here for the next tool round, deliberately keeping bTurnActive true
	// so the whole exchange stays one message. A plain `if (bTurnActive) return;`
	// silently killed every round after the first — the tool loop appeared to run
	// once and stop, with nothing in the log to say why.
	if (bTurnActive && !bContinuingTurn)
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("Anthropic: a turn is already in flight."));
		return;
	}
	bContinuingTurn = false;

	FString ApiKey;
	if (!FMCPChatSecretStore::Get().GetSecret(TEXT("anthropic"), ApiKey) || ApiKey.IsEmpty())
	{
		ActiveMessageId = FGuid::NewGuid();
		EmitError(LOCTEXT("NoAnthropicKey",
			"No Anthropic API key. Set ANTHROPIC_API_KEY or add one in Settings \u25B8 Providers."));
		FinishTurn();
		return;
	}

	ActiveSessionId = Request.SessionId;
	// Keep the SAME assistant message across tool rounds: the whole turn — text,
	// tool cards, follow-up text — is one message in the transcript, not one per
	// round-trip.
	if (!bTurnActive || !ActiveMessageId.IsValid())
	{
		ActiveMessageId = FGuid::NewGuid();
		ToolIteration    = 0;
		TurnStartSeconds = FPlatformTime::Seconds();
	}
	CancelFlag      = Request.CancelFlag;
	PendingRetryRequest = Request;
	CurrentRequest      = Request;

	BlockTypeByIndex.Reset();
	ToolCallIdByIndex.Reset();
	ToolNameByIndex.Reset();
	Parser.Reset();
	bSawRefusal = false;
	bStopReasonToolUse = false;
	bAwaitingApproval  = false;
	RoundText.Reset();
	PendingToolCalls.Reset();
	bTurnActive = true;

	ByteQueue = MakeShared<FMCPHttpStreamSink::FByteQueue>();
	Sink      = MakeShared<FMCPHttpStreamSink>(ByteQueue);

	const FMCPChatProviderInfo* Provider = FMCPChatModelCatalog::Get().FindProvider(TEXT("anthropic"));
	const FString BaseUrl = (Provider && !Provider->BaseUrl.IsEmpty())
		? Provider->BaseUrl : TEXT("https://api.anthropic.com");

	FString BodyText;
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyText);
		FJsonSerializer::Serialize(BuildRequestBody(Request).ToSharedRef(), Writer);
	}

	HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(BaseUrl / TEXT("v1/messages"));
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("content-type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("accept"), TEXT("text/event-stream"));
	HttpRequest->SetHeader(TEXT("x-api-key"), ApiKey);
	HttpRequest->SetHeader(TEXT("anthropic-version"), AnthropicVersionHeader);
	if (Provider)
	{
		for (const TPair<FString, FString>& Header : Provider->ExtraHeaders)
		{
			HttpRequest->SetHeader(Header.Key, Header.Value);
		}
	}
	HttpRequest->SetContentAsString(BodyText);

	// Route the body through our sink so bytes arrive incrementally rather than
	// only at completion. Without this the whole "streaming" story collapses into
	// one big response at the end.
	HttpRequest->SetResponseBodyReceiveStream(Sink.ToSharedRef());

	TWeakPtr<FAnthropicBackend> WeakSelf = AsShared();
	HttpRequest->OnProcessRequestComplete().BindLambda(
		[WeakSelf](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
	{
		if (const TSharedPtr<FAnthropicBackend> Self = WeakSelf.Pin())
		{
			Self->HandleHttpComplete(Req, Resp, bOk);
		}
	});

	// Never log the body or headers: the key is in there.
	UE_LOG(LogUnrealMCPChat, Verbose, TEXT("Anthropic request: model=%s, max_tokens=%d, %d message(s)"),
		*Request.ModelId, Request.Params.MaxOutputTokens, Request.History.Num());

	FChatStreamEvent Started;
	Started.Type = FChatStreamEvent::EType::TurnStarted;
	Started.MessageId = ActiveMessageId;
	OnStreamEvent.Broadcast(Started);

	HttpRequest->ProcessRequest();
}

void FAnthropicBackend::CancelTurn(const FGuid& SessionId)
{
	if (!bTurnActive || SessionId != ActiveSessionId) { return; }

	if (HttpRequest.IsValid())
	{
		// Drop the completion binding first: cancelling fires it with bSucceeded
		// false, which would otherwise be reported to the user as a network error.
		HttpRequest->OnProcessRequestComplete().Unbind();
		HttpRequest->CancelRequest();
		HttpRequest.Reset();
	}
	RetryCount = MaxRetries;   // a cancelled turn must not auto-retry
	FinishTurn();
}

void FAnthropicBackend::EndSession(const FGuid& SessionId)
{
	if (SessionId == ActiveSessionId)
	{
		CancelTurn(SessionId);
		ActiveSessionId.Invalidate();
	}
}

// ============================================================================
// Stream draining
// ============================================================================

bool FAnthropicBackend::TickStream(float /*DeltaTime*/)
{
	// Deferred retry after a 429 / 5xx.
	if (RetryAtSeconds > 0.0 && FPlatformTime::Seconds() >= RetryAtSeconds)
	{
		RetryAtSeconds = 0.0;
		const FChatTurnRequest Retry = PendingRetryRequest;
		bTurnActive = false;
		SendTurn(Retry);
		return true;
	}

	if (!ByteQueue.IsValid()) { return true; }

	if (CancelFlag.IsValid() && CancelFlag->Load(EMemoryOrder::Relaxed) && bTurnActive)
	{
		CancelTurn(ActiveSessionId);
		return true;
	}

	// Drain with a frame budget. A fast model produces far more events per second
	// than a frame should spend on layout, so we cap the work and let the rest
	// arrive next tick rather than stalling the editor.
	const double Deadline = FPlatformTime::Seconds() + 0.004;   // 4 ms

	TArray<uint8> Chunk;
	TArray<FMCPSseParser::FEvent> Events;
	while (ByteQueue->Dequeue(Chunk))
	{
		Parser.Append(Chunk, Events);
		if (FPlatformTime::Seconds() > Deadline) { break; }
	}

	for (const FMCPSseParser::FEvent& Event : Events)
	{
		HandleSseEvent(Event);
	}
	return true;
}

void FAnthropicBackend::HandleSseEvent(const FMCPSseParser::FEvent& Event)
{
	if (Event.Data.IsEmpty()) { return; }

	TSharedPtr<FJsonObject> Obj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Event.Data);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("Anthropic: unparseable SSE payload for event '%s'."),
			*Event.EventName);
		return;
	}

	FString Type = Event.EventName;
	Obj->TryGetStringField(TEXT("type"), Type);   // the payload's own type is authoritative

	// ---- content_block_start: remember what this index is ----
	if (Type == TEXT("content_block_start"))
	{
		double IndexNum = 0.0;
		Obj->TryGetNumberField(TEXT("index"), IndexNum);
		const int32 Index = static_cast<int32>(IndexNum);

		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (Obj->TryGetObjectField(TEXT("content_block"), Block) && Block)
		{
			FString BlockType;
			(*Block)->TryGetStringField(TEXT("type"), BlockType);
			BlockTypeByIndex.Add(Index, BlockType);

			if (BlockType == TEXT("tool_use"))
			{
				FPendingToolCall Call;
				Call.LocalId = FGuid::NewGuid();
				(*Block)->TryGetStringField(TEXT("name"), Call.ToolName);
				// The provider id is what a tool_result must reference; losing it means
				// the follow-up request is rejected for an unanswered tool_use.
				(*Block)->TryGetStringField(TEXT("id"), Call.ProviderId);

				ToolCallIdByIndex.Add(Index, Call.LocalId);
				ToolNameByIndex.Add(Index, Call.ToolName);

				FChatStreamEvent Out;
				Out.Type           = FChatStreamEvent::EType::ToolCallStarted;
				Out.MessageId      = ActiveMessageId;
				Out.ToolCallId     = Call.LocalId;
				Out.ToolName       = Call.ToolName;
				Out.ProviderCallId = Call.ProviderId;
				OnStreamEvent.Broadcast(Out);
				bSawAnyContent = true;

				PendingToolCalls.Add(MoveTemp(Call));
			}
		}
		return;
	}

	// ---- content_block_delta: route by the remembered block type ----
	if (Type == TEXT("content_block_delta"))
	{
		double IndexNum = 0.0;
		Obj->TryGetNumberField(TEXT("index"), IndexNum);
		const int32 Index = static_cast<int32>(IndexNum);

		const TSharedPtr<FJsonObject>* Delta = nullptr;
		if (!Obj->TryGetObjectField(TEXT("delta"), Delta) || !Delta) { return; }

		FString DeltaType;
		(*Delta)->TryGetStringField(TEXT("type"), DeltaType);

		if (DeltaType == TEXT("text_delta"))
		{
			FString Text;
			(*Delta)->TryGetStringField(TEXT("text"), Text);
			if (!Text.IsEmpty())
			{
				RoundText += Text;   // echoed back with this round's tool_use blocks

				FChatStreamEvent Out;
				Out.Type      = FChatStreamEvent::EType::TextDelta;
				Out.MessageId = ActiveMessageId;
				Out.Text      = Text;
				OnStreamEvent.Broadcast(Out);
				bSawAnyContent = true;
			}
		}
		else if (DeltaType == TEXT("thinking_delta"))
		{
			FString Text;
			(*Delta)->TryGetStringField(TEXT("thinking"), Text);
			// Empty thinking text means display defaulted to "omitted" — see the
			// header. Nothing to render, so nothing is emitted.
			if (!Text.IsEmpty())
			{
				FChatStreamEvent Out;
				Out.Type      = FChatStreamEvent::EType::ThinkingDelta;
				Out.MessageId = ActiveMessageId;
				Out.Text      = Text;
				OnStreamEvent.Broadcast(Out);
			}
		}
		else if (DeltaType == TEXT("input_json_delta"))
		{
			FString Partial;
			(*Delta)->TryGetStringField(TEXT("partial_json"), Partial);
			if (const FGuid* CallId = ToolCallIdByIndex.Find(Index))
			{
				// GOTCHA G6: arguments arrive as PARTIAL JSON. Buffer and parse only
				// once the block closes — parsing a fragment yields noise.
				for (FPendingToolCall& Call : PendingToolCalls)
				{
					if (Call.LocalId == *CallId) { Call.ArgsJson += Partial; break; }
				}

				FChatStreamEvent Out;
				Out.Type       = FChatStreamEvent::EType::ToolArgsDelta;
				Out.MessageId  = ActiveMessageId;
				Out.ToolCallId = *CallId;
				Out.Text       = Partial;
				OnStreamEvent.Broadcast(Out);
			}
		}
		return;
	}

	// ---- message_delta: stop_reason + output token count ----
	if (Type == TEXT("message_delta"))
	{
		const TSharedPtr<FJsonObject>* Delta = nullptr;
		if (Obj->TryGetObjectField(TEXT("delta"), Delta) && Delta)
		{
			FString StopReason;
			if ((*Delta)->TryGetStringField(TEXT("stop_reason"), StopReason))
			{
				if (StopReason == TEXT("refusal"))
				{
					// HTTP 200 with empty or partial content. This is a content
					// outcome, not an error — it gets its own card.
					bSawRefusal = true;

					FString Category;
					const TSharedPtr<FJsonObject>* Details = nullptr;
					if ((*Delta)->TryGetObjectField(TEXT("stop_details"), Details) && Details)
					{
						(*Details)->TryGetStringField(TEXT("category"), Category);
					}

					FChatStreamEvent Out;
					Out.Type            = FChatStreamEvent::EType::Refusal;
					Out.MessageId       = ActiveMessageId;
					Out.RefusalCategory = Category;
					Out.Text = Category.IsEmpty()
						? TEXT("The provider declined this request.")
						: FString::Printf(TEXT("The provider declined this request (%s)."), *Category);
					OnStreamEvent.Broadcast(Out);
				}
				else if (StopReason == TEXT("tool_use"))
				{
					bStopReasonToolUse = true;
				}
				else if (StopReason == TEXT("max_tokens"))
				{
					FChatStreamEvent Out;
					Out.Type      = FChatStreamEvent::EType::Error;
					Out.MessageId = ActiveMessageId;
					Out.Text = TEXT("The reply hit max_tokens and was cut off. Raise 'Max output tokens' in AI settings, ")
					           TEXT("or lower the effort level so less of the budget goes to thinking.");
					OnStreamEvent.Broadcast(Out);
				}
			}
		}

		const TSharedPtr<FJsonObject>* Usage = nullptr;
		if (Obj->TryGetObjectField(TEXT("usage"), Usage) && Usage)
		{
			double N = 0.0;
			if ((*Usage)->TryGetNumberField(TEXT("output_tokens"), N))
			{
				PendingUsage.OutputTokens = static_cast<int32>(N);
			}
		}
		return;
	}

	// ---- message_start: input tokens + cache accounting ----
	if (Type == TEXT("message_start"))
	{
		const TSharedPtr<FJsonObject>* Message = nullptr;
		if (Obj->TryGetObjectField(TEXT("message"), Message) && Message)
		{
			const TSharedPtr<FJsonObject>* Usage = nullptr;
			if ((*Message)->TryGetObjectField(TEXT("usage"), Usage) && Usage)
			{
				double N = 0.0;
				if ((*Usage)->TryGetNumberField(TEXT("input_tokens"), N))              { PendingUsage.InputTokens = static_cast<int32>(N); }
				if ((*Usage)->TryGetNumberField(TEXT("cache_read_input_tokens"), N))   { PendingUsage.CacheReadTokens = static_cast<int32>(N); }
				if ((*Usage)->TryGetNumberField(TEXT("cache_creation_input_tokens"), N)){ PendingUsage.CacheWriteTokens = static_cast<int32>(N); }
			}
		}
		return;
	}

	// ---- error events arrive in-band on the stream ----
	if (Type == TEXT("error"))
	{
		const TSharedPtr<FJsonObject>* Error = nullptr;
		FString Message = TEXT("The provider reported an error.");
		if (Obj->TryGetObjectField(TEXT("error"), Error) && Error)
		{
			(*Error)->TryGetStringField(TEXT("message"), Message);
		}
		EmitError(Message);
		return;
	}

	if (Type == TEXT("message_stop"))
	{
		if (bStopReasonToolUse && PendingToolCalls.Num() > 0)
		{
			BeginToolPhase();
		}
		else
		{
			FinishTurn();
		}
	}
}

// ============================================================================
// Phase 5 — tool loop
// ============================================================================

bool FAnthropicBackend::AllToolCallsDecided() const
{
	for (const FPendingToolCall& Call : PendingToolCalls)
	{
		if (!Call.bDecided) { return false; }
	}
	return true;
}

void FAnthropicBackend::OpenTurnTransactionIfNeeded()
{
	if (TurnTransaction.IsValid()) { return; }

	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	if (Settings && !Settings->bSingleUndoPerTurn) { return; }

	// Opened lazily on the first mutating tool, so a read-only turn does not leave
	// an empty entry on the undo stack. Tools' own FScopedTransactions nest inside
	// this one, which is what turns "spawn 20 cubes" into a single Ctrl+Z.
	TurnTransaction = MakeUnique<FMCPScopedOwnerTransaction>(
		ActiveSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		LOCTEXT("ChatTurnTransaction", "MCP Chat Turn"));
}

void FAnthropicBackend::BeginToolPhase()
{
	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	const int32 MaxIterations = Settings ? Settings->MaxToolIterationsPerTurn : 25;

	// --- Guards, checked before anything executes ---
	if (++ToolIteration > MaxIterations)
	{
		EmitError(FText::Format(
			LOCTEXT("TooManyRounds",
				"Stopped after {0} tool rounds in one turn. Raise 'Max tool iterations per turn' in "
				"Settings if this task legitimately needs more, or ask for a smaller step."),
			FText::AsNumber(MaxIterations)));
		FinishTurn();
		return;
	}

	if (FPlatformTime::Seconds() - TurnStartSeconds > MaxTurnSeconds)
	{
		EmitError(LOCTEXT("TurnTimedOut",
			"This turn ran past its time limit and was stopped. Any completed tool calls have been kept."));
		FinishTurn();
		return;
	}

	if (CancelFlag.IsValid() && CancelFlag->Load(EMemoryOrder::Relaxed))
	{
		FinishTurn();
		return;
	}

	// --- Parse the buffered arguments now that every block has closed ---
	for (FPendingToolCall& Call : PendingToolCalls)
	{
		if (Call.ArgsJson.IsEmpty())
		{
			// A no-argument tool legitimately streams nothing.
			Call.ParsedArgs = MakeShared<FJsonObject>();
			continue;
		}

		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Call.ArgsJson);
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
		{
			Call.ParsedArgs = Parsed;
		}
		else
		{
			// Malformed arguments are the model's mistake, not a crash. Fail this one
			// call with an explanation the model can act on, and keep the turn alive.
			Call.bDecided = true;
			Call.bAllowed = false;
			Call.DenyReason = LOCTEXT("BadArgs",
				"The arguments for this call were not valid JSON. Re-issue the call with well-formed arguments.");
		}
	}

	ProcessToolQueue();
}

void FAnthropicBackend::ProcessToolQueue()
{
	// Decide anything still undecided; ask the user where the gate says so.
	for (FPendingToolCall& Call : PendingToolCalls)
	{
		if (Call.bDecided) { continue; }

		FText Reason;
		const EMCPChatGateResult Gate =
			FMCPChatToolBridge::Get().EvaluateGate(Call.ToolName, ActiveSessionId, Reason);

		if (Gate == EMCPChatGateResult::Allow)
		{
			Call.bDecided = true;
			Call.bAllowed = true;
		}
		else if (Gate == EMCPChatGateResult::Blocked)
		{
			Call.bDecided   = true;
			Call.bAllowed   = false;
			Call.DenyReason = Reason;
		}
		else
		{
			// Pause the turn here. The card renders the choice inline, the user
			// answers, and RespondToPermission resumes us.
			bAwaitingApproval = true;

			FChatStreamEvent Ask;
			Ask.Type           = FChatStreamEvent::EType::PermissionRequest;
			Ask.MessageId      = ActiveMessageId;
			Ask.ToolCallId     = Call.LocalId;
			Ask.ToolName       = Call.ToolName;
			Ask.ProviderCallId = Call.ProviderId;
			Ask.ApprovalReason = Reason;
			Ask.ToolArgs       = Call.ParsedArgs;
			OnStreamEvent.Broadcast(Ask);
			return;   // one prompt at a time — a wall of dialogs is not a decision
		}
	}

	bAwaitingApproval = false;
	if (AllToolCallsDecided())
	{
		ExecuteApprovedTools();
	}
}

void FAnthropicBackend::RespondToPermission(const FGuid& RequestId, EChatPermissionResult Result)
{
	FPendingToolCall* Call = PendingToolCalls.FindByPredicate(
		[&RequestId](const FPendingToolCall& C) { return C.LocalId == RequestId; });

	if (!Call)
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("Approval for an unknown tool call; ignoring."));
		return;
	}

	Call->bDecided = true;

	switch (Result)
	{
	case EChatPermissionResult::AllowAlways:
		FMCPChatToolBridge::Get().GrantAlways(Call->ToolName);
		Call->bAllowed = true;
		break;

	case EChatPermissionResult::AllowForSession:
		FMCPChatToolBridge::Get().GrantForSession(Call->ToolName, ActiveSessionId);
		Call->bAllowed = true;
		break;

	case EChatPermissionResult::AllowOnce:
		Call->bAllowed = true;
		break;

	case EChatPermissionResult::Deny:
	default:
		Call->bAllowed   = false;
		// Told to the MODEL, not just the user: a denial with a reason lets it try
		// a different approach instead of retrying the same call.
		Call->DenyReason = LOCTEXT("UserDenied", "The user denied permission for this tool call.");
		break;
	}

	// Continue with the next undecided call, or execute.
	ProcessToolQueue();
}

void FAnthropicBackend::ExecuteApprovedTools()
{
	FMCPChatToolBridge& Bridge = FMCPChatToolBridge::Get();

	// Effective, not global: a session with a `/scope read` ceiling must execute at
	// that scope even though the global setting is looser. Reading the setting
	// directly here would have let the ceiling gate the decision but not the
	// execution — the exact kind of half-applied policy that becomes a hole.
	const EMCPChatApprovalMode Mode = Bridge.GetEffectiveMode(ActiveSessionId);

	for (FPendingToolCall& Call : PendingToolCalls)
	{
		if (Call.bExecuted) { continue; }

		if (!Call.bAllowed)
		{
			Call.bExecuted  = true;
			Call.bIsError   = true;
			Call.ResultText = Call.DenyReason.IsEmpty()
				? TEXT("This tool call was not permitted.")
				: Call.DenyReason.ToString();
		}
		else
		{
			// Open the shared undo step just before the first real mutation.
			if (!Bridge.IsToolReadOnly(Call.ToolName))
			{
				OpenTurnTransactionIfNeeded();
			}

			const FGuid LocalId = Call.LocalId;
			const FGuid MessageId = ActiveMessageId;
			TWeakPtr<FAnthropicBackend> WeakSelf = AsShared();

			// Progress from a long tool reaches the card live rather than the card
			// looking frozen for ten seconds.
			auto ProgressSink = [WeakSelf, LocalId, MessageId](float Fraction, const FString& Message)
			{
				if (const TSharedPtr<FAnthropicBackend> Self = WeakSelf.Pin())
				{
					FChatStreamEvent Progress;
					Progress.Type       = FChatStreamEvent::EType::ToolArgsDelta;  // reused as a status channel
					Progress.MessageId  = MessageId;
					Progress.ToolCallId = LocalId;
					Progress.Text       = FString::Printf(TEXT("\n[%.0f%%] %s"), Fraction * 100.f, *Message);
					Self->OnStreamEvent.Broadcast(Progress);
				}
			};

			const FMCPChatToolBridge::FExecutionResult Exec = Bridge.Execute(
				Call.ToolName, Call.ParsedArgs, ActiveSessionId, Mode, CancelFlag, ProgressSink);

			Call.bExecuted       = true;
			Call.bIsError        = Exec.bIsError;
			Call.ResultText      = Exec.Text;
			Call.DurationSeconds = Exec.DurationSeconds;
		}

		FChatStreamEvent Done;
		Done.Type            = FChatStreamEvent::EType::ToolCallResult;
		Done.MessageId       = ActiveMessageId;
		Done.ToolCallId      = Call.LocalId;
		Done.ToolName        = Call.ToolName;
		Done.ProviderCallId  = Call.ProviderId;
		Done.ToolArgs        = Call.ParsedArgs;
		Done.ToolResultText  = Call.ResultText;
		Done.bToolIsError    = Call.bIsError;
		Done.DurationSeconds = Call.DurationSeconds;
		OnStreamEvent.Broadcast(Done);

		// A tool can take a while; honour a cancel between calls rather than only
		// between rounds.
		if (CancelFlag.IsValid() && CancelFlag->Load(EMemoryOrder::Relaxed))
		{
			break;
		}
	}

	SendToolResults();
}

void FAnthropicBackend::SendToolResults()
{
	if (CancelFlag.IsValid() && CancelFlag->Load(EMemoryOrder::Relaxed))
	{
		// DELIBERATE: work already done is COMMITTED, not rolled back. The user
		// pressed Stop, not Undo — silently reverting ten spawned actors would be
		// surprising, and Ctrl+Z is one keystroke away if that is what they meant.
		// FinishTurn commits the open transaction, so the partial result is a single
		// undo step exactly like a completed turn.
		FinishTurn();
		return;
	}

	// Fold this round into the history we send next time: one assistant turn
	// carrying the text + tool_use blocks, then one user turn carrying every
	// tool_result. Both are synthesised here rather than read back from the
	// session, because the session's message is still streaming.
	FChatMessagePtr AssistantTurn = MakeShared<FChatMessage>();
	AssistantTurn->Id   = FGuid::NewGuid();
	AssistantTurn->Role = EChatRole::Assistant;
	if (!RoundText.IsEmpty())
	{
		AssistantTurn->Blocks.Add(FChatContentBlock::MakeText(RoundText));
	}
	for (const FPendingToolCall& Call : PendingToolCalls)
	{
		FChatContentBlock Block;
		Block.Type            = FChatContentBlock::EType::ToolCall;
		Block.ToolCallId      = Call.LocalId;
		Block.ProviderCallId  = Call.ProviderId;
		Block.ToolName        = Call.ToolName;
		Block.ToolArgs        = Call.ParsedArgs;
		Block.ToolResultText  = Call.ResultText;
		Block.bToolIsError    = Call.bIsError;
		Block.DurationSeconds = Call.DurationSeconds;
		Block.bIsComplete     = true;
		AssistantTurn->Blocks.Add(MoveTemp(Block));
	}
	CurrentRequest.History.Add(AssistantTurn);

	// Next round. Keeping bTurnActive true preserves ActiveMessageId, so the whole
	// exchange stays one message in the transcript.
	FChatTurnRequest Next = CurrentRequest;

	ByteQueue.Reset();
	Sink.Reset();
	if (HttpRequest.IsValid())
	{
		HttpRequest->OnProcessRequestComplete().Unbind();
		HttpRequest.Reset();
	}

	bContinuingTurn = true;
	SendTurn(Next);
}

// ============================================================================
// Completion / errors
// ============================================================================

void FAnthropicBackend::HandleHttpComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
{
	// Drain anything the sink still holds before deciding what happened — a fast
	// error response can complete before the ticker has run once.
	if (ByteQueue.IsValid())
	{
		TArray<uint8> Chunk;
		TArray<FMCPSseParser::FEvent> Events;
		while (ByteQueue->Dequeue(Chunk)) { Parser.Append(Chunk, Events); }
		for (const FMCPSseParser::FEvent& Event : Events) { HandleSseEvent(Event); }
	}

	if (!bTurnActive) { return; }   // already finished via message_stop

	const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;

	if (!bSucceeded || StatusCode == 0)
	{
		EmitError(LOCTEXT("AnthropicUnreachable",
			"Could not reach api.anthropic.com. Check your network connection."));
		FinishTurn();
		return;
	}

	if (StatusCode >= 200 && StatusCode < 300)
	{
		// Stream ended without message_stop — treat what we have as complete.
		if (bStopReasonToolUse && PendingToolCalls.Num() > 0)
		{
			BeginToolPhase();
		}
		else
		{
			FinishTurn();
		}
		return;
	}

	// Body may hold the provider's own message; redact before it can reach a log.
	const FString Body = Response.IsValid() ? Response->GetContentAsString() : FString();

	const bool bRetryable = (StatusCode == 429 || StatusCode == 529 || StatusCode >= 500);
	if (bRetryable && RetryCount < MaxRetries)
	{
		++RetryCount;

		// Honour retry-after when present; otherwise exponential backoff.
		double DelaySeconds = FMath::Pow(2.0, static_cast<double>(RetryCount));
		if (Response.IsValid())
		{
			const FString RetryAfter = Response->GetHeader(TEXT("retry-after"));
			if (!RetryAfter.IsEmpty())
			{
				DelaySeconds = FMath::Clamp(FCString::Atod(*RetryAfter), 1.0, 60.0);
			}
		}

		UE_LOG(LogUnrealMCPChat, Warning, TEXT("Anthropic %d — retry %d/%d in %.0fs"),
			StatusCode, RetryCount, MaxRetries, DelaySeconds);

		RetryAtSeconds = FPlatformTime::Seconds() + DelaySeconds;
		HttpRequest.Reset();
		return;   // TickStream re-sends
	}

	EmitError(DescribeHttpError(StatusCode, Body));
	FinishTurn();
}

FString FAnthropicBackend::DescribeHttpError(int32 StatusCode, const FString& Body)
{
	// Extract the provider's own message where possible — it is usually more
	// specific than anything we could write.
	FString ProviderMessage;
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
		{
			const TSharedPtr<FJsonObject>* Error = nullptr;
			if (Obj->TryGetObjectField(TEXT("error"), Error) && Error)
			{
				(*Error)->TryGetStringField(TEXT("message"), ProviderMessage);
			}
		}
	}

	// Each case says what to DO, not just what went wrong.
	FString Advice;
	switch (StatusCode)
	{
	case 400:
		Advice = TEXT("The request was rejected. If this model rejects sampling parameters, ")
		         TEXT("set \"sampling\": false for it in Config/DefaultChatModels.json.");
		break;
	case 401:
		Advice = TEXT("The API key was rejected. Check it in Settings ▸ Providers.");
		break;
	case 403:
		Advice = TEXT("This key does not have access to that model.");
		break;
	case 404:
		Advice = TEXT("Unknown model id. Check the id in Config/DefaultChatModels.json.");
		break;
	case 413:
		Advice = TEXT("The conversation is too large. Start a new chat or remove attachments.");
		break;
	case 429:
		Advice = TEXT("Rate limited, and automatic retries were exhausted. Wait a moment and try again.");
		break;
	default:
		Advice = (StatusCode >= 500)
			? TEXT("The provider is having trouble. Retries were exhausted; try again shortly.")
			: TEXT("Unexpected response from the provider.");
		break;
	}

	return ProviderMessage.IsEmpty()
		? FString::Printf(TEXT("HTTP %d — %s"), StatusCode, *Advice)
		: FString::Printf(TEXT("HTTP %d — %s\n\n%s"), StatusCode, *Advice, *ProviderMessage);
}

void FAnthropicBackend::EmitError(const FText& Message)
{
	EmitError(Message.ToString());
}

void FAnthropicBackend::EmitError(const FString& Message)
{
	// Redaction is not optional: a 400 body can echo the request, and the request
	// carries the key.
	const FString Safe = FMCPChatSecretStore::Get().RedactSecrets(Message);

	UE_LOG(LogUnrealMCPChat, Error, TEXT("Anthropic: %s"), *Safe);

	FChatStreamEvent Out;
	Out.Type      = FChatStreamEvent::EType::Error;
	Out.MessageId = ActiveMessageId;
	Out.Text      = Safe;
	OnStreamEvent.Broadcast(Out);
}

void FAnthropicBackend::FinishTurn()
{
	if (!bTurnActive) { return; }

	if (!PendingUsage.IsEmpty())
	{
		PendingUsage.EstimatedCostUsd =
			FMCPChatModelCatalog::Get().EstimateCost(PendingRetryRequest.ModelId, PendingUsage);

		FChatStreamEvent Usage;
		Usage.Type      = FChatStreamEvent::EType::UsageUpdate;
		Usage.MessageId = ActiveMessageId;
		Usage.Usage     = PendingUsage;
		OnStreamEvent.Broadcast(Usage);
	}

	// A refusal before any output produces an empty message; say so rather than
	// committing a blank turn.
	if (bSawRefusal && !bSawAnyContent)
	{
		UE_LOG(LogUnrealMCPChat, Log, TEXT("Anthropic: request refused before any output."));
	}

	// Commit the turn's undo step BEFORE announcing completion, so a listener that
	// immediately inspects the undo stack sees a settled state.
	if (TurnTransaction.IsValid())
	{
		TurnTransaction->Commit();
		TurnTransaction.Reset();
	}

	bTurnActive = false;
	bStopReasonToolUse = false;
	bAwaitingApproval = false;
	PendingToolCalls.Reset();
	RoundText.Reset();
	ToolIteration = 0;
	RetryCount = 0;
	RetryAtSeconds = 0.0;
	ByteQueue.Reset();
	Sink.Reset();
	if (HttpRequest.IsValid())
	{
		HttpRequest->OnProcessRequestComplete().Unbind();
		HttpRequest.Reset();
	}

	FChatStreamEvent Finished;
	Finished.Type      = FChatStreamEvent::EType::TurnFinished;
	Finished.MessageId = ActiveMessageId;
	OnStreamEvent.Broadcast(Finished);
}

#undef LOCTEXT_NAMESPACE
