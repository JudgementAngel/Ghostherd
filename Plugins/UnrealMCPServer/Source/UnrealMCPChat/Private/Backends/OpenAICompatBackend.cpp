// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Backends/OpenAICompatBackend.h"
#include "MCPChatAttachments.h"
#include "MCPChatSecretStore.h"
#include "MCPChatSettings.h"
#include "MCPChatStore.h"
#include "MCPChatToolBridge.h"
#include "UnrealMCPChatModule.h"

#include "MCPTransactionScope.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#define LOCTEXT_NAMESPACE "OpenAICompatBackend"

namespace
{
	/** Providers that reject `stream_options`. Ollama and LM Studio implement a
	 *  subset of the API and 400 on fields they do not know, so the flag that turns
	 *  usage reporting on is opt-in per provider rather than always sent. */
	bool ProviderSupportsStreamOptions(const FString& ProviderId)
	{
		return ProviderId == TEXT("openai")
			|| ProviderId == TEXT("openrouter")
			|| ProviderId == TEXT("groq")
			|| ProviderId == TEXT("deepseek")
			|| ProviderId == TEXT("together");
	}

	/** Local servers need no key and are the fastest path to a working conversation
	 *  on a fresh install. */
	bool ProviderIsLocal(const FString& ProviderId)
	{
		return ProviderId == TEXT("ollama") || ProviderId == TEXT("lmstudio") || ProviderId == TEXT("vllm");
	}
}

// ============================================================================
// Construction
// ============================================================================

FOpenAICompatBackend::FOpenAICompatBackend(const FString& InProviderId)
	: ProviderId(InProviderId)
{
}

FOpenAICompatBackend::~FOpenAICompatBackend()
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
	}
}

FText FOpenAICompatBackend::GetDisplayName() const
{
	if (const FMCPChatProviderInfo* Provider = FMCPChatModelCatalog::Get().FindProvider(ProviderId))
	{
		return FText::FromString(Provider->DisplayName);
	}
	return FText::FromString(ProviderId);
}

bool FOpenAICompatBackend::IsAvailable(FText& OutReason) const
{
	const FMCPChatProviderInfo* Provider = FMCPChatModelCatalog::Get().FindProvider(ProviderId);
	if (!Provider)
	{
		OutReason = FText::Format(
			LOCTEXT("NoProvider", "'{0}' is not in Config/DefaultChatModels.json."),
			FText::FromString(ProviderId));
		return false;
	}

	if (!Provider->bRequiresKey)
	{
		// A local server is "available" if it is configured. Whether it is actually
		// listening is not knowable without a request, and blocking the picker on a
		// network probe would make opening a menu hang.
		return true;
	}

	FString Key;
	if (!FMCPChatSecretStore::Get().GetSecret(ProviderId, Key) || Key.IsEmpty())
	{
		OutReason = FText::Format(
			LOCTEXT("NoKey", "No API key for {0}. Set {1} or add one in Settings."),
			FText::FromString(Provider->DisplayName),
			FText::FromString(FMCPChatSecretStore::GetEnvironmentVariableName(ProviderId)));
		return false;
	}

	return true;
}

TArray<FChatModelInfo> FOpenAICompatBackend::GetModels() const
{
	return FMCPChatModelCatalog::Get().GetModelsForProvider(ProviderId);
}

// ============================================================================
// Request construction
// ============================================================================

TArray<TSharedPtr<FJsonValue>> FOpenAICompatBackend::BuildMessagesArray(const FChatTurnRequest& Request) const
{
	TArray<TSharedPtr<FJsonValue>> Out;

	const FChatModelInfo* Model = FMCPChatModelCatalog::Get().FindModel(Request.ModelId);
	const bool bModelHasVision = Model && Model->bVision;
	const FGuid SessionId = Request.SessionId;

	auto MakeSimpleMessage = [](const TCHAR* Role, const FString& Content)
	{
		TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("role"), Role);
		Message->SetStringField(TEXT("content"), Content);
		return Message;
	};

	/** An image part, base64 as a data: URI. Null on any failure — a malformed part
	 *  fails the whole request, so a missing picture must stay a missing picture. */
	auto MakeImagePart = [SessionId](const FChatContentBlock& Block) -> TSharedPtr<FJsonObject>
	{
		if (Block.StoredPath.IsEmpty() || !FMCPChatAttachments::IsImageMime(Block.MimeType))
		{
			return nullptr;
		}

		const FString Absolute = FMCPChatStore::GetAttachmentsDirectory(SessionId) / Block.StoredPath;
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Absolute) || Bytes.Num() == 0) { return nullptr; }

		TSharedPtr<FJsonObject> Url = MakeShared<FJsonObject>();
		Url->SetStringField(TEXT("url"),
			FString::Printf(TEXT("data:%s;base64,%s"), *Block.MimeType, *FBase64::Encode(Bytes)));

		TSharedPtr<FJsonObject> Part = MakeShared<FJsonObject>();
		Part->SetStringField(TEXT("type"), TEXT("image_url"));
		Part->SetObjectField(TEXT("image_url"), Url);
		return Part;
	};

	for (const FChatMessagePtr& M : Request.History)
	{
		if (!M.IsValid()) { continue; }

		// ---- Tool-role turns from the `#tool` fast path ----
		// The user ran something themselves. There is no provider call id to answer,
		// so it is narrated as a user turn rather than faked as a tool result — an
		// invented tool_call_id is rejected outright.
		if (M->Role == EChatRole::Tool)
		{
			FString Narration;
			for (const FChatContentBlock& B : M->Blocks)
			{
				if (B.Type != FChatContentBlock::EType::ToolCall) { continue; }
				Narration.Append(FString::Printf(TEXT("[I ran `%s` myself. Result%s: %s]\n"),
					*B.ToolName,
					B.bToolIsError ? TEXT(" (error)") : TEXT(""),
					B.ToolResultText.IsEmpty() ? TEXT("(no output)") : *B.ToolResultText));
			}
			if (!Narration.IsEmpty())
			{
				Out.Add(MakeShared<FJsonValueObject>(MakeSimpleMessage(TEXT("user"), Narration)));
			}
			continue;
		}

		if (M->Role != EChatRole::User && M->Role != EChatRole::Assistant) { continue; }
		const bool bIsUser = (M->Role == EChatRole::User);

		// ---- Gather this turn's parts ----
		FString TextAccumulator;
		TArray<TSharedPtr<FJsonValue>> Parts;          // user turns with images
		TArray<TSharedPtr<FJsonValue>> ToolCalls;      // assistant turns
		TArray<TSharedPtr<FJsonObject>> ToolResults;   // emitted AFTER the assistant turn

		for (const FChatContentBlock& B : M->Blocks)
		{
			switch (B.Type)
			{
			case FChatContentBlock::EType::Text:
				TextAccumulator.Append(B.Text);
				break;

			case FChatContentBlock::EType::ContextRef:
				if (!B.Text.IsEmpty())
				{
					TextAccumulator.Append(FString::Printf(TEXT("<context ref=\"%s\">\n%s\n</context>\n"),
						*(B.DisplayName.IsEmpty() ? B.ContextKind : B.DisplayName), *B.Text));
				}
				break;

			case FChatContentBlock::EType::File:
				if (!B.Text.IsEmpty())
				{
					TextAccumulator.Append(FString::Printf(TEXT("<file name=\"%s\">\n%s\n</file>\n"),
						*B.DisplayName, *B.Text));
				}
				break;

			case FChatContentBlock::EType::Image:
				if (!bModelHasVision)
				{
					TextAccumulator.Append(FString::Printf(
						TEXT("[image '%s' attached, but the current model cannot see images]\n"), *B.DisplayName));
				}
				else if (const TSharedPtr<FJsonObject> Part = MakeImagePart(B))
				{
					Parts.Add(MakeShared<FJsonValueObject>(Part));
				}
				break;

			case FChatContentBlock::EType::ToolCall:
			{
				// A call with no provider id came from a different backend; skipping it
				// on BOTH sides is what keeps calls and results paired.
				if (B.ProviderCallId.IsEmpty()) { break; }

				FString ArgsText = TEXT("{}");
				if (B.ToolArgs.IsValid())
				{
					const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
						TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ArgsText);
					FJsonSerializer::Serialize(B.ToolArgs.ToSharedRef(), Writer);
				}

				TSharedPtr<FJsonObject> Function = MakeShared<FJsonObject>();
				Function->SetStringField(TEXT("name"), B.ToolName);
				// `arguments` is a STRING containing JSON, not a JSON object. Sending an
				// object is a 400 that reads like a schema problem.
				Function->SetStringField(TEXT("arguments"), ArgsText);

				TSharedPtr<FJsonObject> Call = MakeShared<FJsonObject>();
				Call->SetStringField(TEXT("id"), B.ProviderCallId);
				Call->SetStringField(TEXT("type"), TEXT("function"));
				Call->SetObjectField(TEXT("function"), Function);
				ToolCalls.Add(MakeShared<FJsonValueObject>(Call));

				TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("role"), TEXT("tool"));
				Result->SetStringField(TEXT("tool_call_id"), B.ProviderCallId);
				Result->SetStringField(TEXT("content"),
					B.ToolResultText.IsEmpty() ? TEXT("(no output)") : B.ToolResultText);
				ToolResults.Add(Result);
				break;
			}

			default:
				break;
			}
		}

		// ---- Emit the turn ----
		if (bIsUser)
		{
			if (Parts.Num() > 0)
			{
				// Multimodal form: text first so the model reads the request before the
				// picture, which measurably improves grounding.
				if (!TextAccumulator.IsEmpty())
				{
					TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
					TextPart->SetStringField(TEXT("type"), TEXT("text"));
					TextPart->SetStringField(TEXT("text"), TextAccumulator);
					Parts.Insert(MakeShared<FJsonValueObject>(TextPart), 0);
				}

				TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
				Message->SetStringField(TEXT("role"), TEXT("user"));
				Message->SetArrayField(TEXT("content"), Parts);
				Out.Add(MakeShared<FJsonValueObject>(Message));
			}
			else if (!TextAccumulator.IsEmpty())
			{
				Out.Add(MakeShared<FJsonValueObject>(MakeSimpleMessage(TEXT("user"), TextAccumulator)));
			}
			continue;
		}

		// Assistant: content may legitimately be empty when the turn was only tool
		// calls, but the message must still exist or its results are orphaned.
		if (TextAccumulator.IsEmpty() && ToolCalls.Num() == 0) { continue; }

		TSharedPtr<FJsonObject> Assistant = MakeShared<FJsonObject>();
		Assistant->SetStringField(TEXT("role"), TEXT("assistant"));
		Assistant->SetStringField(TEXT("content"), TextAccumulator);
		if (ToolCalls.Num() > 0)
		{
			Assistant->SetArrayField(TEXT("tool_calls"), ToolCalls);
		}
		Out.Add(MakeShared<FJsonValueObject>(Assistant));

		// Every tool_call needs its tool message immediately after, or the request is
		// rejected with "messages with role 'tool' must be a response to a tool call".
		for (const TSharedPtr<FJsonObject>& Result : ToolResults)
		{
			Out.Add(MakeShared<FJsonValueObject>(Result));
		}
	}

	return Out;
}

TSharedPtr<FJsonObject> FOpenAICompatBackend::BuildRequestBody(const FChatTurnRequest& Request) const
{
	const FChatModelInfo* Model = FMCPChatModelCatalog::Get().FindModel(Request.ModelId);

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Request.ModelId);
	Body->SetBoolField(TEXT("stream"), true);
	Body->SetArrayField(TEXT("messages"), BuildMessagesArray(Request));

	int32 MaxTokens = Request.Params.MaxOutputTokens > 0 ? Request.Params.MaxOutputTokens : 4096;
	if (Model && Model->MaxOutputTokens > 0)
	{
		MaxTokens = FMath::Min(MaxTokens, Model->MaxOutputTokens);
	}
	// `max_tokens` is deprecated in favour of `max_completion_tokens` on OpenAI, but
	// every compatible server still accepts the old name and many only accept it.
	// Sending the widely-supported one is the choice that works everywhere.
	Body->SetNumberField(TEXT("max_tokens"), MaxTokens);

	// ---- Usage ----
	// Without this the final chunk carries no `usage` and the cost readout reads
	// zero forever. Only sent where the provider accepts the field.
	if (ProviderSupportsStreamOptions(ProviderId))
	{
		TSharedPtr<FJsonObject> StreamOptions = MakeShared<FJsonObject>();
		StreamOptions->SetBoolField(TEXT("include_usage"), true);
		Body->SetObjectField(TEXT("stream_options"), StreamOptions);
	}

	// ---- Sampling ----
	// Same rule as Anthropic: only when the catalogue says the model accepts it.
	// OpenAI's own reasoning models reject `temperature` with a 400.
	if (Model && Model->bSampling && Request.Params.bHasSampling)
	{
		Body->SetNumberField(TEXT("temperature"), Request.Params.Temperature);
	}

	// ---- Effort ----
	if (Model && Model->EffortLevels.Num() > 0 && !Request.Params.Effort.IsEmpty()
		&& Model->EffortLevels.Contains(Request.Params.Effort))
	{
		Body->SetStringField(TEXT("reasoning_effort"), Request.Params.Effort);
	}

	// ---- Tools ----
	if (!Model || Model->bTools)
	{
		const TArray<TSharedPtr<FJsonValue>> ToolSchemas =
			FMCPChatToolBridge::Get().BuildOpenAIToolSchemas(Request.Params.bCatalogToolExposure);
		if (ToolSchemas.Num() > 0)
		{
			Body->SetArrayField(TEXT("tools"), ToolSchemas);
			Body->SetStringField(TEXT("tool_choice"), TEXT("auto"));
		}
	}

	return Body;
}

// ============================================================================
// Send
// ============================================================================

void FOpenAICompatBackend::SendTurn(const FChatTurnRequest& Request)
{
	// SendToolResults re-enters here for the next tool round, keeping bTurnActive
	// true so the whole exchange stays one message. The flag is what distinguishes
	// that continuation from a second user turn arriving mid-flight.
	if (bTurnActive && !bContinuingTurn)
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("%s: a turn is already in flight."), *ProviderId);
		return;
	}
	bContinuingTurn = false;

	const FMCPChatProviderInfo* Provider = FMCPChatModelCatalog::Get().FindProvider(ProviderId);

	FString ApiKey;
	const bool bNeedsKey = !Provider || Provider->bRequiresKey;
	if (bNeedsKey && (!FMCPChatSecretStore::Get().GetSecret(ProviderId, ApiKey) || ApiKey.IsEmpty()))
	{
		ActiveMessageId = FGuid::NewGuid();
		bTurnActive = true;
		EmitError(FText::Format(
			LOCTEXT("NoProviderKey", "No API key for {0}. Set {1} or add one in Settings ▸ Providers."),
			FText::FromString(Provider ? Provider->DisplayName : ProviderId),
			FText::FromString(FMCPChatSecretStore::GetEnvironmentVariableName(ProviderId))));
		FinishTurn();
		return;
	}

	ActiveSessionId = Request.SessionId;
	// The same assistant message spans every tool round: one message in the
	// transcript, not one per round-trip.
	if (!bTurnActive || !ActiveMessageId.IsValid())
	{
		ActiveMessageId  = FGuid::NewGuid();
		ToolIteration    = 0;
		TurnStartSeconds = FPlatformTime::Seconds();
	}

	CancelFlag          = Request.CancelFlag;
	CurrentRequest      = Request;
	PendingRetryRequest = Request;

	Parser.Reset();
	ToolCallsByIndex.Reset();
	PendingToolCalls.Reset();
	RoundText.Reset();
	bStopReasonToolCalls = false;
	bAwaitingApproval    = false;
	bTurnActive          = true;

	SendRequest(Request);
}

void FOpenAICompatBackend::SendRequest(const FChatTurnRequest& Request)
{
	const FMCPChatProviderInfo* Provider = FMCPChatModelCatalog::Get().FindProvider(ProviderId);

	const FString BaseUrl = (Provider && !Provider->BaseUrl.IsEmpty())
		? Provider->BaseUrl
		: TEXT("https://api.openai.com/v1");

	ByteQueue = MakeShared<FMCPHttpStreamSink::FByteQueue>();
	Sink      = MakeShared<FMCPHttpStreamSink>(ByteQueue);

	FString BodyText;
	{
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&BodyText);
		FJsonSerializer::Serialize(BuildRequestBody(Request).ToSharedRef(), Writer);
	}

	HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(BaseUrl / TEXT("chat/completions"));
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("content-type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("accept"), TEXT("text/event-stream"));

	if (Provider && Provider->bRequiresKey)
	{
		FString ApiKey;
		FMCPChatSecretStore::Get().GetSecret(ProviderId, ApiKey);
		HttpRequest->SetHeader(
			Provider->AuthHeader.IsEmpty() ? TEXT("Authorization") : Provider->AuthHeader,
			Provider->AuthPrefix + ApiKey);
	}
	if (Provider)
	{
		for (const TPair<FString, FString>& Header : Provider->ExtraHeaders)
		{
			HttpRequest->SetHeader(Header.Key, Header.Value);
		}
	}

	HttpRequest->SetContentAsString(BodyText);
	HttpRequest->SetResponseBodyReceiveStream(Sink.ToSharedRef());

	TWeakPtr<FOpenAICompatBackend> WeakSelf = AsShared();
	HttpRequest->OnProcessRequestComplete().BindLambda(
		[WeakSelf](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
	{
		if (const TSharedPtr<FOpenAICompatBackend> Self = WeakSelf.Pin())
		{
			Self->HandleHttpComplete(Req, Res, bOk);
		}
	});

	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateSP(this, &FOpenAICompatBackend::TickStream));
	}

	// Nothing about the key, the body or the headers is logged. A 400 body echoes
	// the request, and the request carries the key.
	UE_LOG(LogUnrealMCPChat, Verbose, TEXT("%s: POST %s (model %s)"),
		*ProviderId, *BaseUrl, *Request.ModelId);

	HttpRequest->ProcessRequest();

	FChatStreamEvent Started;
	Started.Type      = FChatStreamEvent::EType::TurnStarted;
	Started.MessageId = ActiveMessageId;
	OnStreamEvent.Broadcast(Started);
}

// ============================================================================
// Stream
// ============================================================================

bool FOpenAICompatBackend::TickStream(float /*DeltaTime*/)
{
	// Retry timer.
	if (RetryAtSeconds > 0.0 && FPlatformTime::Seconds() >= RetryAtSeconds)
	{
		RetryAtSeconds = 0.0;
		Parser.Reset();
		SendRequest(PendingRetryRequest);
		return true;
	}

	if (!ByteQueue.IsValid()) { return true; }

	// Frame budget, matching the Anthropic path: bytes arrive on the HTTP thread and
	// are drained here, and a fast provider must not be able to stall a frame.
	const double Start = FPlatformTime::Seconds();

	TArray<uint8> Chunk;
	TArray<FMCPSseParser::FEvent> Events;
	while (ByteQueue->Dequeue(Chunk))
	{
		Parser.Append(Chunk, Events);
		if (FPlatformTime::Seconds() - Start > 0.004) { break; }
	}

	for (const FMCPSseParser::FEvent& Event : Events)
	{
		HandleSseEvent(Event);
	}

	return true;
}

void FOpenAICompatBackend::HandleSseEvent(const FMCPSseParser::FEvent& Event)
{
	const FString Data = Event.Data.TrimStartAndEnd();
	if (Data.IsEmpty()) { return; }

	// The stream terminator is a literal, not JSON. Parsing it produces an error
	// that looks like a protocol failure at exactly the moment everything worked.
	if (Data == TEXT("[DONE]"))
	{
		if (bStopReasonToolCalls)
		{
			// Order matters: the map is index-keyed for accumulation, and the model
			// expects results in the order it asked.
			ToolCallsByIndex.KeySort([](int32 A, int32 B) { return A < B; });
			for (const TPair<int32, FPendingToolCall>& Pair : ToolCallsByIndex)
			{
				PendingToolCalls.Add(Pair.Value);
			}
			ToolCallsByIndex.Reset();
			BeginToolPhase();
		}
		else
		{
			FinishTurn();
		}
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Data);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogUnrealMCPChat, Verbose, TEXT("%s: unparseable stream chunk, skipped."), *ProviderId);
		return;
	}

	// An error can arrive INSIDE a 200 stream — that is how several providers report
	// a mid-stream failure, and treating it as content would show the user JSON.
	const TSharedPtr<FJsonObject>* ErrorObj = nullptr;
	if (Root->TryGetObjectField(TEXT("error"), ErrorObj) && ErrorObj)
	{
		FString Message;
		(*ErrorObj)->TryGetStringField(TEXT("message"), Message);
		EmitError(Message.IsEmpty()
			? LOCTEXT("MidStreamError", "The provider reported an error mid-stream.").ToString()
			: Message);
		FinishTurn();
		return;
	}

	// Usage arrives on its own final chunk, whose `choices` array is empty.
	const TSharedPtr<FJsonObject>* UsageObj = nullptr;
	if (Root->TryGetObjectField(TEXT("usage"), UsageObj) && UsageObj)
	{
		double Prompt = 0.0, Completion = 0.0;
		(*UsageObj)->TryGetNumberField(TEXT("prompt_tokens"), Prompt);
		(*UsageObj)->TryGetNumberField(TEXT("completion_tokens"), Completion);
		PendingUsage.InputTokens  = static_cast<int32>(Prompt);
		PendingUsage.OutputTokens = static_cast<int32>(Completion);

		// Cached input is reported nested and under different names per provider.
		const TSharedPtr<FJsonObject>* Details = nullptr;
		if ((*UsageObj)->TryGetObjectField(TEXT("prompt_tokens_details"), Details) && Details)
		{
			double Cached = 0.0;
			if ((*Details)->TryGetNumberField(TEXT("cached_tokens"), Cached))
			{
				PendingUsage.CacheReadTokens = static_cast<int32>(Cached);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!Root->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->Num() == 0)
	{
		return;
	}

	const TSharedPtr<FJsonObject>* Choice = nullptr;
	if (!(*Choices)[0].IsValid() || !(*Choices)[0]->TryGetObject(Choice) || !Choice) { return; }

	FString FinishReason;
	if ((*Choice)->TryGetStringField(TEXT("finish_reason"), FinishReason) && !FinishReason.IsEmpty())
	{
		if (FinishReason == TEXT("tool_calls"))
		{
			bStopReasonToolCalls = true;
		}
		else if (FinishReason == TEXT("length"))
		{
			// Not an error card: the answer is real, just cut short, and saying so is
			// more useful than letting it end mid-sentence unexplained.
			FChatStreamEvent Note;
			Note.Type      = FChatStreamEvent::EType::TextDelta;
			Note.MessageId = ActiveMessageId;
			Note.Text      = TEXT("\n\n_(cut off at the output token limit — raise it in Settings ▸ AI)_");
			OnStreamEvent.Broadcast(Note);
		}
		else if (FinishReason == TEXT("content_filter"))
		{
			FChatStreamEvent Refusal;
			Refusal.Type      = FChatStreamEvent::EType::Refusal;
			Refusal.MessageId = ActiveMessageId;
			Refusal.Text      = TEXT("The provider's content filter stopped this response.");
			OnStreamEvent.Broadcast(Refusal);
		}
	}

	const TSharedPtr<FJsonObject>* Delta = nullptr;
	if ((*Choice)->TryGetObjectField(TEXT("delta"), Delta) && Delta)
	{
		HandleChoiceDelta(*Delta);
	}
}

void FOpenAICompatBackend::HandleChoiceDelta(const TSharedPtr<FJsonObject>& Delta)
{
	// ---- Reasoning ----
	// Neither field is in the OpenAI spec. DeepSeek uses `reasoning_content`,
	// OpenRouter `reasoning`; reading both costs two lines and covers the field.
	FString Reasoning;
	if (Delta->TryGetStringField(TEXT("reasoning_content"), Reasoning)
		|| Delta->TryGetStringField(TEXT("reasoning"), Reasoning))
	{
		if (!Reasoning.IsEmpty())
		{
			FChatStreamEvent Event;
			Event.Type      = FChatStreamEvent::EType::ThinkingDelta;
			Event.MessageId = ActiveMessageId;
			Event.Text      = Reasoning;
			OnStreamEvent.Broadcast(Event);
		}
	}

	// ---- Text ----
	FString Content;
	if (Delta->TryGetStringField(TEXT("content"), Content) && !Content.IsEmpty())
	{
		bSawAnyContent = true;
		RoundText.Append(Content);

		FChatStreamEvent Event;
		Event.Type      = FChatStreamEvent::EType::TextDelta;
		Event.MessageId = ActiveMessageId;
		Event.Text      = Content;
		OnStreamEvent.Broadcast(Event);
	}

	// ---- Tool calls ----
	const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
	if (!Delta->TryGetArrayField(TEXT("tool_calls"), ToolCalls) || !ToolCalls) { return; }

	for (const TSharedPtr<FJsonValue>& Value : *ToolCalls)
	{
		const TSharedPtr<FJsonObject>* CallObj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(CallObj) || !CallObj) { continue; }

		// The index is the identity. The id and the name arrive on the first fragment
		// only; every later fragment carries nothing but `index` and an argument
		// substring. Keying on the id would silently drop all of them.
		double IndexValue = 0.0;
		(*CallObj)->TryGetNumberField(TEXT("index"), IndexValue);
		const int32 Index = static_cast<int32>(IndexValue);

		FPendingToolCall& Call = ToolCallsByIndex.FindOrAdd(Index);
		if (!Call.LocalId.IsValid())
		{
			Call.LocalId = FGuid::NewGuid();
		}

		FString CallId;
		if ((*CallObj)->TryGetStringField(TEXT("id"), CallId) && !CallId.IsEmpty())
		{
			Call.ProviderId = CallId;
		}

		const TSharedPtr<FJsonObject>* Function = nullptr;
		if (!(*CallObj)->TryGetObjectField(TEXT("function"), Function) || !Function) { continue; }

		FString Name;
		if ((*Function)->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
		{
			// The card can only be announced once the tool has a name.
			const bool bFirstTime = Call.ToolName.IsEmpty();
			Call.ToolName = Name;

			if (bFirstTime)
			{
				FChatStreamEvent Event;
				Event.Type           = FChatStreamEvent::EType::ToolCallStarted;
				Event.MessageId      = ActiveMessageId;
				Event.ToolCallId     = Call.LocalId;
				Event.ProviderCallId = Call.ProviderId;
				Event.ToolName       = Name;
				OnStreamEvent.Broadcast(Event);
			}
		}

		FString ArgumentFragment;
		if ((*Function)->TryGetStringField(TEXT("arguments"), ArgumentFragment)
			&& !ArgumentFragment.IsEmpty())
		{
			Call.ArgsJson.Append(ArgumentFragment);

			FChatStreamEvent Event;
			Event.Type       = FChatStreamEvent::EType::ToolArgsDelta;
			Event.MessageId  = ActiveMessageId;
			Event.ToolCallId = Call.LocalId;
			Event.Text       = ArgumentFragment;
			OnStreamEvent.Broadcast(Event);
		}
	}
}

// ============================================================================
// Tool loop
//
// Deliberately a copy of the Anthropic loop's SHAPE rather than a shared base
// class. The two differ in wire format at every step (index-keyed vs id-keyed
// accumulation, tool messages vs tool_result blocks), and factoring a validated
// state machine into a template while neither backend has been run once would
// risk both to save neither. Phase 9 unifies them once both are exercised.
// ============================================================================

void FOpenAICompatBackend::OpenTurnTransactionIfNeeded()
{
	if (TurnTransaction.IsValid()) { return; }

	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	if (Settings && !Settings->bSingleUndoPerTurn) { return; }

	TurnTransaction = MakeUnique<FMCPScopedOwnerTransaction>(
		ActiveSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		LOCTEXT("ChatTurnTransaction", "MCP Chat Turn"));
}

bool FOpenAICompatBackend::AllToolCallsDecided() const
{
	for (const FPendingToolCall& Call : PendingToolCalls)
	{
		if (!Call.bDecided) { return false; }
	}
	return true;
}

void FOpenAICompatBackend::BeginToolPhase()
{
	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	const int32 MaxIterations = Settings ? Settings->MaxToolIterationsPerTurn : 25;

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

	for (FPendingToolCall& Call : PendingToolCalls)
	{
		if (Call.ArgsJson.IsEmpty())
		{
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
			// Malformed arguments fail THIS call, not the turn. The model is told why
			// and can re-issue it.
			Call.bDecided   = true;
			Call.bAllowed   = false;
			Call.DenyReason = LOCTEXT("BadArgs",
				"The arguments for this call were not valid JSON. Re-issue the call with well-formed arguments.");
		}
	}

	ProcessToolQueue();
}

void FOpenAICompatBackend::ProcessToolQueue()
{
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
			return;   // one prompt at a time
		}
	}

	bAwaitingApproval = false;
	if (AllToolCallsDecided())
	{
		ExecuteApprovedTools();
	}
}

void FOpenAICompatBackend::RespondToPermission(const FGuid& RequestId, EChatPermissionResult Result)
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
		Call->DenyReason = LOCTEXT("UserDenied", "The user denied permission for this tool call.");
		break;
	}

	ProcessToolQueue();
}

void FOpenAICompatBackend::ExecuteApprovedTools()
{
	FMCPChatToolBridge& Bridge = FMCPChatToolBridge::Get();
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
			if (!Bridge.IsToolReadOnly(Call.ToolName))
			{
				OpenTurnTransactionIfNeeded();
			}

			const FGuid LocalId   = Call.LocalId;
			const FGuid MessageId = ActiveMessageId;
			TWeakPtr<FOpenAICompatBackend> WeakSelf = AsShared();

			auto ProgressSink = [WeakSelf, LocalId, MessageId](float Fraction, const FString& Message)
			{
				if (const TSharedPtr<FOpenAICompatBackend> Self = WeakSelf.Pin())
				{
					FChatStreamEvent Progress;
					Progress.Type       = FChatStreamEvent::EType::ToolArgsDelta;
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

		if (CancelFlag.IsValid() && CancelFlag->Load(EMemoryOrder::Relaxed))
		{
			break;
		}
	}

	SendToolResults();
}

void FOpenAICompatBackend::SendToolResults()
{
	if (CancelFlag.IsValid() && CancelFlag->Load(EMemoryOrder::Relaxed))
	{
		// Same deliberate choice as the Anthropic path: work already done is
		// COMMITTED, not rolled back. Stop is not Undo, and Ctrl+Z is one keystroke
		// away if that is what was meant.
		FinishTurn();
		return;
	}

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

void FOpenAICompatBackend::CancelTurn(const FGuid& /*SessionId*/)
{
	if (CancelFlag.IsValid()) { CancelFlag->Store(true, EMemoryOrder::Relaxed); }

	if (HttpRequest.IsValid())
	{
		HttpRequest->OnProcessRequestComplete().Unbind();
		HttpRequest->CancelRequest();
		HttpRequest.Reset();
	}

	FinishTurn();
}

void FOpenAICompatBackend::EndSession(const FGuid& SessionId)
{
	CancelTurn(SessionId);
}

void FOpenAICompatBackend::HandleHttpComplete(FHttpRequestPtr /*Request*/, FHttpResponsePtr Response, bool bSucceeded)
{
	// Drain first: a fast error response can complete before the ticker runs once.
	if (ByteQueue.IsValid())
	{
		TArray<uint8> Chunk;
		TArray<FMCPSseParser::FEvent> Events;
		while (ByteQueue->Dequeue(Chunk)) { Parser.Append(Chunk, Events); }
		for (const FMCPSseParser::FEvent& Event : Events) { HandleSseEvent(Event); }
	}

	if (!bTurnActive) { return; }   // already finished via [DONE]

	const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
	const FMCPChatProviderInfo* Provider = FMCPChatModelCatalog::Get().FindProvider(ProviderId);
	const FString ProviderDisplay = Provider ? Provider->DisplayName : ProviderId;

	if (!bSucceeded || StatusCode == 0)
	{
		// A local server that is not running is by far the most common case here, and
		// "check your network" would be exactly the wrong advice.
		EmitError(ProviderIsLocal(ProviderId)
			? FString::Printf(
				TEXT("Could not reach %s at %s. Is it running? For Ollama, start it with 'ollama serve'."),
				*ProviderDisplay, *(Provider ? Provider->BaseUrl : FString()))
			: FString::Printf(TEXT("Could not reach %s. Check your network connection."), *ProviderDisplay));
		FinishTurn();
		return;
	}

	if (StatusCode >= 200 && StatusCode < 300)
	{
		// Stream ended without [DONE] — take what arrived as complete.
		if (bStopReasonToolCalls && (ToolCallsByIndex.Num() > 0 || PendingToolCalls.Num() > 0))
		{
			if (PendingToolCalls.Num() == 0)
			{
				ToolCallsByIndex.KeySort([](int32 A, int32 B) { return A < B; });
				for (const TPair<int32, FPendingToolCall>& Pair : ToolCallsByIndex)
				{
					PendingToolCalls.Add(Pair.Value);
				}
				ToolCallsByIndex.Reset();
			}
			BeginToolPhase();
		}
		else
		{
			FinishTurn();
		}
		return;
	}

	const FString Body = Response.IsValid() ? Response->GetContentAsString() : FString();

	const bool bRetryable = (StatusCode == 429 || StatusCode >= 500);
	if (bRetryable && RetryCount < MaxRetries)
	{
		++RetryCount;

		double DelaySeconds = FMath::Pow(2.0, static_cast<double>(RetryCount));
		if (Response.IsValid())
		{
			const FString RetryAfter = Response->GetHeader(TEXT("retry-after"));
			if (!RetryAfter.IsEmpty())
			{
				DelaySeconds = FMath::Clamp(FCString::Atod(*RetryAfter), 1.0, 60.0);
			}
		}

		UE_LOG(LogUnrealMCPChat, Warning, TEXT("%s %d — retry %d/%d in %.0fs"),
			*ProviderId, StatusCode, RetryCount, MaxRetries, DelaySeconds);

		RetryAtSeconds = FPlatformTime::Seconds() + DelaySeconds;
		HttpRequest.Reset();
		return;   // TickStream re-sends
	}

	EmitError(DescribeHttpError(StatusCode, Body, ProviderDisplay));
	FinishTurn();
}

FString FOpenAICompatBackend::DescribeHttpError(int32 StatusCode, const FString& Body,
                                                const FString& ProviderDisplay)
{
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
			else
			{
				// Ollama and several small servers return a bare {"error":"…"} string.
				Obj->TryGetStringField(TEXT("error"), ProviderMessage);
			}
		}
	}

	FString Advice;
	switch (StatusCode)
	{
	case 400:
		Advice = TEXT("The request was rejected. If this model rejects temperature, set ")
		         TEXT("\"sampling\": false for it in Config/DefaultChatModels.json.");
		break;
	case 401:
	case 403:
		Advice = FString::Printf(
			TEXT("%s rejected the API key. Check it in Settings ▸ Providers."), *ProviderDisplay);
		break;
	case 404:
		Advice = TEXT("Unknown model, or the base URL is wrong. For a local server, check the ")
		         TEXT("model has been pulled and the port matches.");
		break;
	case 413:
		Advice = TEXT("The conversation is too large. Start a new chat, run /compact, or remove attachments.");
		break;
	case 422:
		Advice = TEXT("The provider rejected part of the request — often tool schemas it does not support. ")
		         TEXT("Try Catalog tool exposure in Settings ▸ AI.");
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

void FOpenAICompatBackend::EmitError(const FText& Message)
{
	EmitError(Message.ToString());
}

void FOpenAICompatBackend::EmitError(const FString& Message)
{
	const FString Safe = FMCPChatSecretStore::Get().RedactSecrets(Message);

	UE_LOG(LogUnrealMCPChat, Error, TEXT("%s: %s"), *ProviderId, *Safe);

	FChatStreamEvent Out;
	Out.Type      = FChatStreamEvent::EType::Error;
	Out.MessageId = ActiveMessageId;
	Out.Text      = Safe;
	OnStreamEvent.Broadcast(Out);
}

void FOpenAICompatBackend::FinishTurn()
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

	// Commit before announcing, so a listener that inspects the undo stack on
	// TurnFinished sees a settled state.
	if (TurnTransaction.IsValid())
	{
		TurnTransaction->Commit();
		TurnTransaction.Reset();
	}

	bTurnActive          = false;
	bStopReasonToolCalls = false;
	bAwaitingApproval    = false;
	bSawAnyContent       = false;
	PendingToolCalls.Reset();
	ToolCallsByIndex.Reset();
	RoundText.Reset();
	PendingUsage = FChatUsage();
	ToolIteration  = 0;
	RetryCount     = 0;
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
