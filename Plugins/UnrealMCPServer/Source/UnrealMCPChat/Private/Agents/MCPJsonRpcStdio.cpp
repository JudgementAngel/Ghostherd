// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Agents/MCPJsonRpcStdio.h"
#include "Agents/MCPChatProcessRunner.h"
#include "UnrealMCPChatModule.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FMCPJsonRpcStdio::FMCPJsonRpcStdio(const TSharedPtr<FMCPChatProcessRunner>& InRunner)
	: Runner(InRunner)
{
}

// ============================================================================
// Inbound
// ============================================================================

void FMCPJsonRpcStdio::HandleLine(const FString& Line)
{
	const FString Trimmed = Line.TrimStartAndEnd();
	if (Trimmed.IsEmpty()) { return; }

	// Cheap pre-check before paying for a parse. Agents print banners, progress
	// spinners and npm warnings onto the same pipe; those are diagnostics, not
	// protocol errors, and must not be logged as failures.
	if (!Trimmed.StartsWith(TEXT("{")))
	{
		OnDiagnostic.ExecuteIfBound(Trimmed);
		return;
	}

	TSharedPtr<FJsonObject> Message;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
	if (!FJsonSerializer::Deserialize(Reader, Message) || !Message.IsValid())
	{
		OnDiagnostic.ExecuteIfBound(Trimmed);
		return;
	}

	const TSharedPtr<FJsonValue> IdValue = Message->TryGetField(TEXT("id"));
	FString Method;
	const bool bHasMethod = Message->TryGetStringField(TEXT("method"), Method);

	// ---- Response to something we sent ----
	// A message with an id and NO method is a response. Ordering matters: an
	// incoming request also has an id, and checking the method first would route
	// every agent request into the response table.
	if (IdValue.IsValid() && !bHasMethod)
	{
		int32 Id = 0;
		if (IdValue->Type == EJson::Number)
		{
			Id = static_cast<int32>(IdValue->AsNumber());
		}
		else
		{
			// Some agents echo the id back as a string. Accepting both costs one line
			// and avoids a class of "the agent replied but nothing happened" bugs.
			LexFromString(Id, *IdValue->AsString());
		}

		FPending Entry;
		if (!Pending.RemoveAndCopyValue(Id, Entry))
		{
			UE_LOG(LogUnrealMCPChat, Verbose, TEXT("ACP: response to unknown id %d, ignored."), Id);
			return;
		}

		const TSharedPtr<FJsonObject>* ResultObj = nullptr;
		const TSharedPtr<FJsonObject>* ErrorObj  = nullptr;
		Message->TryGetObjectField(TEXT("result"), ResultObj);
		Message->TryGetObjectField(TEXT("error"), ErrorObj);

		Entry.Callback.ExecuteIfBound(
			ResultObj ? *ResultObj : nullptr,
			ErrorObj  ? *ErrorObj  : nullptr);
		return;
	}

	if (!bHasMethod)
	{
		OnDiagnostic.ExecuteIfBound(Trimmed);
		return;
	}

	const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
	Message->TryGetObjectField(TEXT("params"), ParamsObj);
	const TSharedPtr<FJsonObject> Params = ParamsObj ? *ParamsObj : MakeShared<FJsonObject>();

	// ---- Request from the agent (has an id) ----
	if (IdValue.IsValid())
	{
		if (!OnRequest.IsBound())
		{
			// Never leave the agent blocked. An unanswered request is an indefinite
			// hang with nothing in any log to explain it.
			RespondError(IdValue, ErrorMethodNotFound,
				FString::Printf(TEXT("'%s' is not supported by this client."), *Method));
			return;
		}
		OnRequest.Execute(Method, Params, IdValue);
		return;
	}

	// ---- Notification ----
	OnNotification.ExecuteIfBound(Method, Params);
}

// ============================================================================
// Outbound
// ============================================================================

void FMCPJsonRpcStdio::SendRaw(const TSharedPtr<FJsonObject>& Message)
{
	const TSharedPtr<FMCPChatProcessRunner> Pinned = Runner.Pin();
	if (!Pinned.IsValid() || !Message.IsValid()) { return; }

	FString Serialised;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialised);
	FJsonSerializer::Serialize(Message.ToSharedRef(), Writer);

	// Condensed, deliberately: a pretty-printed message contains newlines, and this
	// protocol is newline-framed. Pretty-printing here would corrupt every message.
	Pinned->WriteLine(Serialised);
}

int32 FMCPJsonRpcStdio::SendRequest(const FString& Method, const TSharedPtr<FJsonObject>& Params,
                                    FOnResponse OnResponse, float TimeoutSeconds)
{
	const TSharedPtr<FMCPChatProcessRunner> Pinned = Runner.Pin();
	if (!Pinned.IsValid() || !Pinned->IsRunning())
	{
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("code"), ErrorInternal);
		Error->SetStringField(TEXT("message"), TEXT("The agent process is not running."));
		OnResponse.ExecuteIfBound(nullptr, Error);
		return 0;
	}

	const int32 Id = NextId++;

	TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Message->SetNumberField(TEXT("id"), Id);
	Message->SetStringField(TEXT("method"), Method);
	Message->SetObjectField(TEXT("params"), Params.IsValid() ? Params : MakeShared<FJsonObject>());

	FPending Entry;
	Entry.Callback        = OnResponse;
	Entry.Method          = Method;
	Entry.DeadlineSeconds = FPlatformTime::Seconds() + TimeoutSeconds;
	Pending.Add(Id, MoveTemp(Entry));

	SendRaw(Message);
	return Id;
}

void FMCPJsonRpcStdio::SendNotification(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Message->SetStringField(TEXT("method"), Method);
	Message->SetObjectField(TEXT("params"), Params.IsValid() ? Params : MakeShared<FJsonObject>());
	SendRaw(Message);
}

void FMCPJsonRpcStdio::Respond(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result)
{
	if (!Id.IsValid()) { return; }

	TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Message->SetField(TEXT("id"), Id);
	Message->SetObjectField(TEXT("result"), Result.IsValid() ? Result : MakeShared<FJsonObject>());
	SendRaw(Message);
}

void FMCPJsonRpcStdio::RespondError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& ErrorMessage)
{
	if (!Id.IsValid()) { return; }

	TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
	Error->SetNumberField(TEXT("code"), Code);
	Error->SetStringField(TEXT("message"), ErrorMessage);

	TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Message->SetField(TEXT("id"), Id);
	Message->SetObjectField(TEXT("error"), Error);
	SendRaw(Message);
}

// ============================================================================
// Failure paths
// ============================================================================

void FMCPJsonRpcStdio::FailAllPending(const FString& Reason)
{
	// Move first: a callback can start a new request, and mutating the map while
	// iterating it would be the kind of crash that only shows up under load.
	TMap<int32, FPending> Snapshot = MoveTemp(Pending);
	Pending.Reset();

	for (TPair<int32, FPending>& Pair : Snapshot)
	{
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("code"), ErrorInternal);
		Error->SetStringField(TEXT("message"), Reason);
		Pair.Value.Callback.ExecuteIfBound(nullptr, Error);
	}
}

void FMCPJsonRpcStdio::TickTimeouts()
{
	if (Pending.Num() == 0) { return; }

	const double Now = FPlatformTime::Seconds();

	TArray<int32> Expired;
	for (const TPair<int32, FPending>& Pair : Pending)
	{
		if (Now >= Pair.Value.DeadlineSeconds) { Expired.Add(Pair.Key); }
	}

	for (const int32 Id : Expired)
	{
		FPending Entry;
		if (!Pending.RemoveAndCopyValue(Id, Entry)) { continue; }

		UE_LOG(LogUnrealMCPChat, Warning, TEXT("ACP: '%s' timed out."), *Entry.Method);

		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("code"), ErrorInternal);
		Error->SetStringField(TEXT("message"),
			FString::Printf(TEXT("The agent did not answer '%s' in time."), *Entry.Method));
		Entry.Callback.ExecuteIfBound(nullptr, Error);
	}
}
