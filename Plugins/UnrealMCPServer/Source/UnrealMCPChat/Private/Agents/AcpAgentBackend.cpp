// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Agents/AcpAgentBackend.h"
#include "Agents/MCPChatProcessRunner.h"
#include "Agents/MCPJsonRpcStdio.h"
#include "MCPChatSecretStore.h"
#include "MCPChatStore.h"
#include "MCPChatToolBridge.h"
#include "UnrealMCPChatModule.h"

#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#define LOCTEXT_NAMESPACE "AcpAgentBackend"

namespace
{
	/** The protocol version this client implements. */
	constexpr int32 AcpProtocolVersion = 1;

	/** Named for this file, not `JsonToString`: MCPProtocol.h declares a global with
	 *  that name and the same signature, and an unqualified call is ambiguous. */
	FString ToCompactJson(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid()) { return FString(); }
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Out;
	}

	/** ACP content blocks are `{type:"text", text:"…"}` and friends. Only the text
	 *  variant carries something we can render inline; the rest are summarised so
	 *  they are visible rather than silently dropped. */
	FString ContentBlockToText(const TSharedPtr<FJsonObject>& Block)
	{
		if (!Block.IsValid()) { return FString(); }

		FString Type;
		Block->TryGetStringField(TEXT("type"), Type);

		if (Type == TEXT("text"))
		{
			FString Text;
			Block->TryGetStringField(TEXT("text"), Text);
			return Text;
		}
		if (Type == TEXT("image"))    { return TEXT("[image]"); }
		if (Type == TEXT("audio"))    { return TEXT("[audio]"); }
		if (Type == TEXT("resource") || Type == TEXT("resource_link"))
		{
			FString Uri;
			Block->TryGetStringField(TEXT("uri"), Uri);
			return FString::Printf(TEXT("[%s]"), Uri.IsEmpty() ? TEXT("resource") : *Uri);
		}
		return FString();
	}

	/** `content` in ACP is sometimes an object and sometimes an array of them. */
	FString ExtractContent(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field)
	{
		if (!Params.IsValid()) { return FString(); }

		const TSharedPtr<FJsonObject>* SingleObject = nullptr;
		if (Params->TryGetObjectField(Field, SingleObject) && SingleObject)
		{
			return ContentBlockToText(*SingleObject);
		}

		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Params->TryGetArrayField(Field, Array) && Array)
		{
			FString Combined;
			for (const TSharedPtr<FJsonValue>& Value : *Array)
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (Value.IsValid() && Value->TryGetObject(Obj) && Obj)
				{
					Combined.Append(ContentBlockToText(*Obj));
				}
			}
			return Combined;
		}

		return FString();
	}
}

// ============================================================================
// Construction
// ============================================================================

FAcpAgentBackend::FAcpAgentBackend(const FMCPChatAgentInfo& InAgent)
	: Agent(InAgent)
{
}

FAcpAgentBackend::~FAcpAgentBackend()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	Shutdown();
}

bool FAcpAgentBackend::IsAvailable(FText& OutReason) const
{
	if (Agent.Command.IsEmpty())
	{
		OutReason = LOCTEXT("NoCommand", "This agent has no command configured.");
		return false;
	}

	// Cached: ResolveExecutable walks every directory on PATH, and the model picker
	// asks once per row on every repaint.
	const double Now = FPlatformTime::Seconds();
	if (Now - CachedResolveTime > AvailabilityCacheSeconds)
	{
		CachedResolvedPath = FMCPChatProcessRunner::ResolveExecutable(Agent.Command);
		CachedResolveTime = Now;
	}

	if (CachedResolvedPath.IsEmpty())
	{
		OutReason = Agent.InstallHint.IsEmpty()
			? FText::Format(LOCTEXT("NotFound", "'{0}' is not on PATH."), FText::FromString(Agent.Command))
			: FText::FromString(Agent.InstallHint);
		return false;
	}

	return true;
}

TArray<FChatModelInfo> FAcpAgentBackend::GetModels() const
{
	// Presented as models so the picker groups them under the agent exactly like a
	// provider's. They cost nothing per token — the agent bills through the sign-in
	// the user already has — so price stays zero and the picker renders "local".
	TArray<FChatModelInfo> Out;
	Out.Reserve(Agent.Models.Num());

	for (const TPair<FString, FString>& Model : Agent.Models)
	{
		FChatModelInfo Info;
		Info.ModelId     = Model.Key;
		Info.DisplayName = Model.Value;
		Info.ProviderId  = Agent.AgentId;
		Info.bTools      = true;
		Info.bSampling   = false;   // we do not shape an agent's request at all
		Out.Add(MoveTemp(Info));
	}
	return Out;
}

// ============================================================================
// Process lifecycle
// ============================================================================

bool FAcpAgentBackend::EnsureProcess(const FString& ModelId, FText& OutError)
{
	// A model change only reaches an agent through its command line or environment,
	// both of which are fixed at spawn. So switching model restarts the process —
	// and the user is told, because it also discards the agent's conversation state.
	if (Runner.IsValid() && Runner->IsRunning() && ModelId != LaunchedModelId)
	{
		UE_LOG(LogUnrealMCPChat, Log,
			TEXT("%s: model changed to '%s'; restarting the agent."), *Agent.DisplayName, *ModelId);
		Shutdown();
	}

	if (Runner.IsValid() && Runner->IsRunning()) { return true; }

	if (!IsAvailable(OutError)) { return false; }

	// The single highest-leverage line in the agent integration: this is what hands
	// the agent all 450 editor tools. An empty path is not fatal — the agent still
	// runs, just without them, and the user is told.
	const FString McpConfigPath = FMCPChatAgentCatalog::WriteMcpConfig(Agent.AgentId);

	FMCPChatProcessRunner::FLaunchParams Params;
	Params.Executable       = Agent.Command;
	Params.Arguments        = FMCPChatAgentCatalog::BuildArgumentString(Agent, McpConfigPath, ModelId);
	Params.WorkingDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	Params.Label            = Agent.DisplayName;
	Params.Environment      = Agent.Environment;

	if (Agent.McpInjection == EMCPAgentMcpInjection::Environment
		&& !Agent.McpConfigFlag.IsEmpty() && !McpConfigPath.IsEmpty())
	{
		Params.Environment.Add(Agent.McpConfigFlag, McpConfigPath);
	}

	if (!ModelId.IsEmpty() && !Agent.ModelEnvVar.IsEmpty())
	{
		Params.Environment.Add(Agent.ModelEnvVar, ModelId);
	}
	LaunchedModelId = ModelId;

	Runner = MakeShared<FMCPChatProcessRunner>();
	Rpc    = MakeShared<FMCPJsonRpcStdio>(Runner);

	TWeakPtr<FAcpAgentBackend> WeakSelf = AsShared();

	Runner->OnLine.BindLambda([WeakSelf](const FString& Line)
	{
		if (const TSharedPtr<FAcpAgentBackend> Self = WeakSelf.Pin())
		{
			if (Self->Rpc.IsValid()) { Self->Rpc->HandleLine(Line); }
		}
	});

	Runner->OnExited.BindLambda([WeakSelf](int32 ReturnCode, bool bWasRequested)
	{
		if (const TSharedPtr<FAcpAgentBackend> Self = WeakSelf.Pin())
		{
			Self->HandleProcessExited(ReturnCode, bWasRequested);
		}
	});

	Rpc->OnNotification.BindSP(this, &FAcpAgentBackend::HandleNotification);
	Rpc->OnRequest.BindSP(this, &FAcpAgentBackend::HandleRequest);
	Rpc->OnDiagnostic.BindSP(this, &FAcpAgentBackend::HandleDiagnostic);

	if (!Runner->Launch(Params, OutError))
	{
		Runner.Reset();
		Rpc.Reset();
		State = EState::Failed;
		return false;
	}

	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateSP(this, &FAcpAgentBackend::Tick));
	}

	State = EState::Starting;
	AcpSessionId.Reset();
	return true;
}

void FAcpAgentBackend::BeginInitialize()
{
	if (!Rpc.IsValid()) { return; }

	State = EState::Initializing;

	// Declare only what we actually implement. Over-declaring is worse than
	// under-declaring: an agent that believes we support `terminal/*` and finds we
	// do not gets an error mid-task instead of choosing a different approach.
	TSharedPtr<FJsonObject> FsCaps = MakeShared<FJsonObject>();
	FsCaps->SetBoolField(TEXT("readTextFile"), true);
	FsCaps->SetBoolField(TEXT("writeTextFile"), true);

	TSharedPtr<FJsonObject> ClientCaps = MakeShared<FJsonObject>();
	ClientCaps->SetObjectField(TEXT("fs"), FsCaps);
	ClientCaps->SetBoolField(TEXT("terminal"), true);

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetNumberField(TEXT("protocolVersion"), AcpProtocolVersion);
	Params->SetObjectField(TEXT("clientCapabilities"), ClientCaps);

	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
	Info->SetStringField(TEXT("name"), TEXT("Unreal MCP Chat"));
	Info->SetStringField(TEXT("version"), TEXT("1.0.0"));
	Params->SetObjectField(TEXT("clientInfo"), Info);

	Rpc->SendRequest(TEXT("initialize"), Params,
		FMCPJsonRpcStdio::FOnResponse::CreateSP(this, &FAcpAgentBackend::HandleInitializeResult),
		/*TimeoutSeconds*/ 60.f);
}

void FAcpAgentBackend::HandleInitializeResult(const TSharedPtr<FJsonObject>& Result,
                                              const TSharedPtr<FJsonObject>& Error)
{
	if (Error.IsValid() || !Result.IsValid())
	{
		FString Message = TEXT("The agent refused to initialise.");
		if (Error.IsValid()) { Error->TryGetStringField(TEXT("message"), Message); }
		State = EState::Failed;
		EmitError(FText::Format(LOCTEXT("AgentSaid", "{0}: {1}"),
			FText::FromString(Agent.DisplayName), FText::FromString(Message)));
		FinishTurn();
		return;
	}

	const TSharedPtr<FJsonObject>* AgentCaps = nullptr;
	if (Result->TryGetObjectField(TEXT("agentCapabilities"), AgentCaps) && AgentCaps)
	{
		(*AgentCaps)->TryGetBoolField(TEXT("loadSession"), bAgentSupportsLoadSession);
	}

	// Auth methods are recorded, NOT treated as a failure.
	//
	// I had this backwards. `authMethods` advertises the sign-in methods an agent
	// SUPPORTS — it says nothing about whether you are currently signed in. Claude
	// Code lists them on every initialize, so a signed-in user got told to go and
	// sign in, and the turn was killed before `session/new` was ever tried.
	//
	// The only reliable signal is a session actually failing. So these are kept and
	// used to make THAT error more useful, if it comes.
	AgentAuthMethods.Reset();
	const TArray<TSharedPtr<FJsonValue>>* AuthMethods = nullptr;
	if (Result->TryGetArrayField(TEXT("authMethods"), AuthMethods) && AuthMethods)
	{
		for (const TSharedPtr<FJsonValue>& Value : *AuthMethods)
		{
			const TSharedPtr<FJsonObject>* Method = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Method) || !Method) { continue; }

			FString Name;
			if (!(*Method)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
			{
				(*Method)->TryGetStringField(TEXT("id"), Name);
			}
			if (!Name.IsEmpty()) { AgentAuthMethods.Add(Name); }
		}
	}

	EnsureAcpSession();
}

void FAcpAgentBackend::EnsureAcpSession()
{
	if (!Rpc.IsValid()) { return; }

	State = EState::CreatingSession;

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	// ACP mandates absolute paths everywhere.
	Params->SetStringField(TEXT("cwd"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Params->SetArrayField(TEXT("mcpServers"), TArray<TSharedPtr<FJsonValue>>());

	TWeakPtr<FAcpAgentBackend> WeakSelf = AsShared();
	Rpc->SendRequest(TEXT("session/new"), Params,
		FMCPJsonRpcStdio::FOnResponse::CreateLambda(
			[WeakSelf](const TSharedPtr<FJsonObject>& Result, const TSharedPtr<FJsonObject>& Error)
	{
		const TSharedPtr<FAcpAgentBackend> Self = WeakSelf.Pin();
		if (!Self.IsValid()) { return; }

		if (Error.IsValid() || !Result.IsValid())
		{
			FString Message = TEXT("could not start a session");
			if (Error.IsValid()) { Error->TryGetStringField(TEXT("message"), Message); }
			Self->State = EState::Failed;
			Self->EmitError(FText::Format(LOCTEXT("AgentSaid2", "{0}: {1}"),
				FText::FromString(Self->Agent.DisplayName), FText::FromString(Message)));
			Self->FinishTurn();
			return;
		}

		Result->TryGetStringField(TEXT("sessionId"), Self->AcpSessionId);
		Self->State = EState::Ready;
		Self->SendPendingPrompt();
	}), /*TimeoutSeconds*/ 60.f);
}

void FAcpAgentBackend::SendPendingPrompt()
{
	if (!Rpc.IsValid() || State != EState::Ready || !PendingPromptParams.IsValid()) { return; }

	PendingPromptParams->SetStringField(TEXT("sessionId"), AcpSessionId);

	TSharedPtr<FJsonObject> Params = PendingPromptParams;
	PendingPromptParams.Reset();

	TWeakPtr<FAcpAgentBackend> WeakSelf = AsShared();
	Rpc->SendRequest(TEXT("session/prompt"), Params,
		FMCPJsonRpcStdio::FOnResponse::CreateLambda(
			[WeakSelf](const TSharedPtr<FJsonObject>& Result, const TSharedPtr<FJsonObject>& Error)
	{
		const TSharedPtr<FAcpAgentBackend> Self = WeakSelf.Pin();
		if (!Self.IsValid()) { return; }

		if (Error.IsValid())
		{
			FString Message = TEXT("the turn failed");
			Error->TryGetStringField(TEXT("message"), Message);
			Self->EmitError(FText::Format(LOCTEXT("AgentSaid2", "{0}: {1}"),
				FText::FromString(Self->Agent.DisplayName), FText::FromString(Message)));
		}
		else if (Result.IsValid())
		{
			// stopReason is how ACP reports refusals and limits; "end_turn" is the
			// ordinary case and needs no card.
			FString StopReason;
			Result->TryGetStringField(TEXT("stopReason"), StopReason);
			if (!StopReason.IsEmpty() && StopReason != TEXT("end_turn") && StopReason != TEXT("cancelled"))
			{
				Self->EmitText(FString::Printf(TEXT("\n\n_(stopped: %s)_"), *StopReason));
			}
		}

		Self->FinishTurn();
	}), /*TimeoutSeconds*/ static_cast<float>(MaxTurnSeconds));
}

// ============================================================================
// IMCPChatBackend
// ============================================================================

void FAcpAgentBackend::StartSession(const FGuid& SessionId)
{
	// Nothing eager. Spawning a CLI agent costs seconds and a subscription slot;
	// doing it when a conversation is merely *selected* would start six processes
	// for someone browsing their history.
	ActiveSessionId = SessionId;
}

void FAcpAgentBackend::SendTurn(const FChatTurnRequest& Request)
{
	if (bTurnActive)
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("%s: a turn is already in flight."), *Agent.DisplayName);
		return;
	}

	ActiveSessionId  = Request.SessionId;
	ActiveMessageId  = FGuid::NewGuid();
	CancelFlag       = Request.CancelFlag;
	bTurnActive      = true;
	TurnStartSeconds = FPlatformTime::Seconds();

	{
		FChatStreamEvent Started;
		Started.Type      = FChatStreamEvent::EType::TurnStarted;
		Started.MessageId = ActiveMessageId;
		Emit(Started);
	}

	// Build the prompt content. The agent keeps its OWN history — we do not replay
	// ours. Sending it again would duplicate every earlier turn inside the agent's
	// context, which is both expensive and confusing to the agent.
	TArray<TSharedPtr<FJsonValue>> Content;

	if (Request.UserMessage.IsValid())
	{
		for (const FChatContentBlock& Block : Request.UserMessage->Blocks)
		{
			FString Text;
			switch (Block.Type)
			{
			case FChatContentBlock::EType::Text:
				Text = Block.Text;
				break;

			case FChatContentBlock::EType::ContextRef:
				Text = FString::Printf(TEXT("<context ref=\"%s\">\n%s\n</context>"),
					*(Block.DisplayName.IsEmpty() ? Block.ContextKind : Block.DisplayName), *Block.Text);
				break;

			case FChatContentBlock::EType::File:
				Text = Block.Text.IsEmpty()
					? FString::Printf(TEXT("[attached file: %s]"), *Block.DisplayName)
					: FString::Printf(TEXT("<file name=\"%s\">\n%s\n</file>"), *Block.DisplayName, *Block.Text);
				break;

			case FChatContentBlock::EType::Image:
				// ACP has an image content block, but the agent's own model may not
				// accept one and we cannot tell from here. Naming it is honest and
				// cannot fail the turn.
				Text = FString::Printf(TEXT("[attached image: %s]"), *Block.DisplayName);
				break;

			default:
				break;
			}

			if (Text.IsEmpty()) { continue; }

			TSharedPtr<FJsonObject> ContentBlock = MakeShared<FJsonObject>();
			ContentBlock->SetStringField(TEXT("type"), TEXT("text"));
			ContentBlock->SetStringField(TEXT("text"), Text);
			Content.Add(MakeShared<FJsonValueObject>(ContentBlock));
		}
	}

	if (Content.Num() == 0)
	{
		EmitError(LOCTEXT("NothingToSend", "Nothing to send."));
		FinishTurn();
		return;
	}

	PendingPromptParams = MakeShared<FJsonObject>();
	PendingPromptParams->SetArrayField(TEXT("prompt"), Content);

	if (State == EState::Ready)
	{
		SendPendingPrompt();
		return;
	}

	FText Error;
	if (!EnsureProcess(Request.ModelId, Error))
	{
		EmitError(Error.ToString());
		FinishTurn();
		return;
	}

	// Tell the user what is happening: starting a CLI agent takes seconds, and
	// silence for five seconds after pressing Enter reads as a hang.
	EmitText(FString::Printf(TEXT("_Starting %s…_\n\n"), *Agent.DisplayName));
	BeginInitialize();
}

void FAcpAgentBackend::CancelTurn(const FGuid& /*SessionId*/)
{
	if (CancelFlag.IsValid()) { CancelFlag->Store(true, EMemoryOrder::Relaxed); }

	if (Rpc.IsValid() && !AcpSessionId.IsEmpty())
	{
		// A notification, not a request: ACP defines no response for it, and waiting
		// for one would hang the Stop button.
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("sessionId"), AcpSessionId);
		Rpc->SendNotification(TEXT("session/cancel"), Params);
	}

	// The process is NOT killed here. A cancel ends the turn, not the agent —
	// killing it would throw away its context and make the next message a cold
	// start. The grace-period kill belongs to Shutdown.
	FinishTurn();
}

void FAcpAgentBackend::EndSession(const FGuid& /*SessionId*/)
{
	Shutdown();
}

void FAcpAgentBackend::Shutdown()
{
	for (const TPair<FString, TSharedPtr<FAgentTerminal>>& Pair : Terminals)
	{
		if (Pair.Value.IsValid() && Pair.Value->Process.IsValid())
		{
			Pair.Value->Process->Stop(0.f);
		}
	}
	Terminals.Reset();

	if (Rpc.IsValid())
	{
		Rpc->FailAllPending(TEXT("The agent was stopped."));
		Rpc.Reset();
	}

	if (Runner.IsValid())
	{
		Runner->Stop(3.f);
		Runner.Reset();
	}

	AcpSessionId.Reset();
	LaunchedModelId.Reset();
	PendingPromptParams.Reset();
	State = EState::Idle;
}

void FAcpAgentBackend::RespondToPermission(const FGuid& RequestId, EChatPermissionResult Result)
{
	FPendingPermission Entry;
	if (!PendingPermissions.RemoveAndCopyValue(RequestId, Entry)) { return; }
	if (!Rpc.IsValid()) { return; }

	// "Always allow" is recorded on OUR side too. The agent's own memory of the
	// decision is its business; ours is what the Permissions settings page lists and
	// what the user can revoke.
	if (Result == EChatPermissionResult::AllowAlways && !Entry.ToolName.IsEmpty())
	{
		FMCPChatToolBridge::Get().GrantAlways(Entry.ToolName);
	}
	else if (Result == EChatPermissionResult::AllowForSession && !Entry.ToolName.IsEmpty())
	{
		FMCPChatToolBridge::Get().GrantForSession(Entry.ToolName, ActiveSessionId);
	}

	FString OptionId;
	switch (Result)
	{
	case EChatPermissionResult::AllowAlways:
		OptionId = Entry.AllowAlwaysOptionId.IsEmpty() ? Entry.AllowOptionId : Entry.AllowAlwaysOptionId;
		break;
	case EChatPermissionResult::AllowOnce:
	case EChatPermissionResult::AllowForSession:
		OptionId = Entry.AllowOptionId;
		break;
	case EChatPermissionResult::Deny:
	default:
		OptionId = Entry.RejectOptionId;
		break;
	}

	TSharedPtr<FJsonObject> Outcome = MakeShared<FJsonObject>();
	if (OptionId.IsEmpty())
	{
		// The agent offered no matching option. Cancelling is the safe reading of an
		// ambiguous answer — never the permissive one.
		Outcome->SetStringField(TEXT("outcome"), TEXT("cancelled"));
	}
	else
	{
		Outcome->SetStringField(TEXT("outcome"), TEXT("selected"));
		Outcome->SetStringField(TEXT("optionId"), OptionId);
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetObjectField(TEXT("outcome"), Outcome);
	Rpc->Respond(Entry.RpcId, ResultObj);
}

// ============================================================================
// Inbound: notifications
// ============================================================================

void FAcpAgentBackend::HandleNotification(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	if (Method == TEXT("session/update"))
	{
		HandleSessionUpdate(Params);
		return;
	}

	UE_LOG(LogUnrealMCPChat, Verbose, TEXT("%s: unhandled notification '%s'."),
		*Agent.DisplayName, *Method);
}

void FAcpAgentBackend::HandleSessionUpdate(const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid()) { return; }

	const TSharedPtr<FJsonObject>* UpdateObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("update"), UpdateObj) || !UpdateObj) { return; }
	const TSharedPtr<FJsonObject>& Update = *UpdateObj;

	FString Kind;
	Update->TryGetStringField(TEXT("sessionUpdate"), Kind);

	// ---- Assistant text ----
	if (Kind == TEXT("agent_message_chunk"))
	{
		const FString Text = ExtractContent(Update, TEXT("content"));
		if (!Text.IsEmpty()) { EmitText(Text); }
		return;
	}

	// ---- Reasoning ----
	if (Kind == TEXT("agent_thought_chunk"))
	{
		const FString Text = ExtractContent(Update, TEXT("content"));
		if (Text.IsEmpty()) { return; }

		FChatStreamEvent Event;
		Event.Type      = FChatStreamEvent::EType::ThinkingDelta;
		Event.MessageId = ActiveMessageId;
		Event.Text      = Text;
		Emit(Event);
		return;
	}

	// ---- Our own message, echoed back ----
	// Some agents replay the user turn. Rendering it would duplicate the message the
	// user is already looking at.
	if (Kind == TEXT("user_message_chunk")) { return; }

	// ---- Tool calls ----
	if (Kind == TEXT("tool_call") || Kind == TEXT("tool_call_update"))
	{
		FString ToolCallId;
		Update->TryGetStringField(TEXT("toolCallId"), ToolCallId);

		FString Title;
		Update->TryGetStringField(TEXT("title"), Title);

		FString Status;
		Update->TryGetStringField(TEXT("status"), Status);

		// The agent's ids are strings; ours are GUIDs. The map is what keeps a card
		// and its later `tool_call_update` pointing at the SAME block — without it,
		// every update would create a new card and a six-step task would render as
		// eighteen.
		FGuid StableId;
		if (ToolCallId.IsEmpty())
		{
			StableId = FGuid::NewGuid();
		}
		else if (const FGuid* Existing = ToolCallIds.Find(ToolCallId))
		{
			StableId = *Existing;
		}
		else
		{
			StableId = FGuid::NewGuid();
			ToolCallIds.Add(ToolCallId, StableId);
		}

		FChatStreamEvent Event;
		Event.MessageId      = ActiveMessageId;
		Event.ToolCallId     = StableId;
		Event.ProviderCallId = ToolCallId;
		Event.ToolName       = Title.IsEmpty() ? TEXT("tool") : Title;

		const TSharedPtr<FJsonObject>* RawInput = nullptr;
		if (Update->TryGetObjectField(TEXT("rawInput"), RawInput) && RawInput)
		{
			Event.ToolArgs = *RawInput;
		}

		if (Status == TEXT("completed") || Status == TEXT("failed"))
		{
			Event.Type           = FChatStreamEvent::EType::ToolCallResult;
			Event.bToolIsError   = (Status == TEXT("failed"));
			Event.ToolResultText = ExtractContent(Update, TEXT("content"));

			if (Event.ToolResultText.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* RawOutput = nullptr;
				if (Update->TryGetObjectField(TEXT("rawOutput"), RawOutput) && RawOutput)
				{
					Event.ToolResultText = ToCompactJson(*RawOutput);
					Event.ToolResult     = *RawOutput;
				}
			}
			if (Event.ToolResultText.IsEmpty()) { Event.ToolResultText = TEXT("(no output)"); }
		}
		else if (Kind == TEXT("tool_call"))
		{
			Event.Type = FChatStreamEvent::EType::ToolCallStarted;
		}
		else
		{
			// An in-progress update with no terminal status: route it through the
			// args-delta channel, which the card already renders as a status line.
			Event.Type = FChatStreamEvent::EType::ToolArgsDelta;
			Event.Text = Status.IsEmpty() ? TEXT("working…") : Status;
		}

		Emit(Event);
		return;
	}

	// ---- Plan ----
	if (Kind == TEXT("plan"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Update->TryGetArrayField(TEXT("entries"), Entries) || !Entries) { return; }

		TStringBuilder<1024> SB;
		for (const TSharedPtr<FJsonValue>& Value : *Entries)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Entry) || !Entry) { continue; }

			FString Content, Status;
			(*Entry)->TryGetStringField(TEXT("content"), Content);
			(*Entry)->TryGetStringField(TEXT("status"), Status);

			const TCHAR* Marker =
				Status == TEXT("completed")   ? TEXT("[x]") :
				Status == TEXT("in_progress") ? TEXT("[~]") : TEXT("[ ]");

			SB.Appendf(TEXT("%s %s\n"), Marker, *Content);
		}

		FChatStreamEvent Event;
		Event.Type      = FChatStreamEvent::EType::PlanUpdate;
		Event.MessageId = ActiveMessageId;
		Event.Text      = SB.ToString();
		Emit(Event);
		return;
	}

	// ---- Mode ----
	if (Kind == TEXT("current_mode_update"))
	{
		FString ModeId;
		Update->TryGetStringField(TEXT("currentModeId"), ModeId);
		if (ModeId.IsEmpty()) { return; }

		FChatStreamEvent Event;
		Event.Type      = FChatStreamEvent::EType::ModeChanged;
		Event.MessageId = ActiveMessageId;
		Event.Text      = ModeId;
		Emit(Event);
		return;
	}

	// available_commands_update and anything newer: ignored rather than logged as an
	// error. The protocol grows, and an agent using a variant we do not know about
	// is not misbehaving.
}

void FAcpAgentBackend::HandleDiagnostic(const FString& Line)
{
	// stdout and stderr share one pipe, so this is where npm warnings and progress
	// spinners land. Verbose, not Warning: an agent that prints a banner is fine.
	UE_LOG(LogUnrealMCPChat, Verbose, TEXT("%s: %s"), *Agent.DisplayName, *Line);
}

void FAcpAgentBackend::HandleProcessExited(int32 ReturnCode, bool bWasRequested)
{
	if (Rpc.IsValid())
	{
		Rpc->FailAllPending(TEXT("The agent process exited."));
	}

	State = EState::Idle;
	AcpSessionId.Reset();

	if (!bWasRequested)
	{
		EmitError(FText::Format(
			LOCTEXT("AgentExited",
				"{0} exited unexpectedly (code {1}). Check the Output Log for what it printed on the way out."),
			FText::FromString(Agent.DisplayName), FText::AsNumber(ReturnCode)));
	}

	FinishTurn();
}

// ============================================================================
// Inbound: requests from the agent
// ============================================================================

void FAcpAgentBackend::HandleRequest(const FString& Method, const TSharedPtr<FJsonObject>& Params,
                                     const TSharedPtr<FJsonValue>& Id)
{
	if (Method == TEXT("fs/read_text_file"))          { HandleFsRead(Params, Id); return; }
	if (Method == TEXT("fs/write_text_file"))         { HandleFsWrite(Params, Id); return; }
	if (Method == TEXT("session/request_permission")) { HandlePermissionRequest(Params, Id); return; }
	if (Method == TEXT("terminal/create"))            { HandleTerminalCreate(Params, Id); return; }
	if (Method == TEXT("terminal/output"))            { HandleTerminalOutput(Params, Id); return; }
	if (Method == TEXT("terminal/wait_for_exit"))     { HandleTerminalWait(Params, Id); return; }
	if (Method == TEXT("terminal/kill"))              { HandleTerminalKill(Params, Id); return; }
	if (Method == TEXT("terminal/release"))           { HandleTerminalRelease(Params, Id); return; }

	Rpc->RespondError(Id, FMCPJsonRpcStdio::ErrorMethodNotFound,
		FString::Printf(TEXT("'%s' is not implemented by this client."), *Method));
}

bool FAcpAgentBackend::IsPathAllowed(const FString& InPath, FString& OutAbsolute, FString& OutReason) const
{
	if (InPath.IsEmpty())
	{
		OutReason = TEXT("empty path");
		return false;
	}

	// Resolve FIRST, then test. A prefix test on the raw string is defeated by
	// "<project>/../../etc/passwd", which is the whole point of the check.
	// CollapseRelativeDirectories is called explicitly rather than relied upon: this
	// is the one check where being wrong hands a third-party binary the filesystem.
	OutAbsolute = FPaths::ConvertRelativePathToFull(InPath);
	FPaths::NormalizeFilename(OutAbsolute);
	FPaths::CollapseRelativeDirectories(OutAbsolute);

	FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(ProjectRoot);
	ProjectRoot += TEXT("/");

	if (!OutAbsolute.StartsWith(ProjectRoot, ESearchCase::IgnoreCase))
	{
		OutReason = TEXT("outside the project directory");
		return false;
	}

	// The chat panel's own data is not the agent's business — transcripts, drafts,
	// pid records and, when auth is on, the mcp.json holding a bearer token.
	FString ChatRoot = FPaths::ConvertRelativePathToFull(FMCPChatStore::GetRootDirectory());
	FPaths::NormalizeDirectoryName(ChatRoot);
	if (OutAbsolute.StartsWith(ChatRoot + TEXT("/"), ESearchCase::IgnoreCase))
	{
		OutReason = TEXT("inside the chat panel's private data");
		return false;
	}

	return true;
}

void FAcpAgentBackend::HandleFsRead(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& Id)
{
	FString Path;
	Params->TryGetStringField(TEXT("path"), Path);

	FString Absolute, Reason;
	if (!IsPathAllowed(Path, Absolute, Reason))
	{
		Rpc->RespondError(Id, FMCPJsonRpcStdio::ErrorInvalidParams,
			FString::Printf(TEXT("Refused: %s is %s."), *Path, *Reason));
		return;
	}

	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *Absolute))
	{
		Rpc->RespondError(Id, FMCPJsonRpcStdio::ErrorInternal,
			FString::Printf(TEXT("Could not read %s."), *Path));
		return;
	}

	// ACP line numbers are 1-based, and `limit` counts lines.
	double LineStart = 0.0, LineLimit = 0.0;
	const bool bHasLine  = Params->TryGetNumberField(TEXT("line"), LineStart);
	const bool bHasLimit = Params->TryGetNumberField(TEXT("limit"), LineLimit);

	if (bHasLine || bHasLimit)
	{
		TArray<FString> Lines;
		Contents.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

		const int32 First = bHasLine ? FMath::Max(0, static_cast<int32>(LineStart) - 1) : 0;
		const int32 Count = bHasLimit ? static_cast<int32>(LineLimit) : Lines.Num() - First;

		TArray<FString> Slice;
		for (int32 i = First; i < Lines.Num() && i < First + Count; ++i)
		{
			Slice.Add(Lines[i]);
		}
		Contents = FString::Join(Slice, TEXT("\n"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("content"), Contents);
	Rpc->Respond(Id, Result);
}

void FAcpAgentBackend::HandleFsWrite(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& Id)
{
	FString Path, Content;
	Params->TryGetStringField(TEXT("path"), Path);
	Params->TryGetStringField(TEXT("content"), Content);

	FString Absolute, Reason;
	if (!IsPathAllowed(Path, Absolute, Reason))
	{
		Rpc->RespondError(Id, FMCPJsonRpcStdio::ErrorInvalidParams,
			FString::Printf(TEXT("Refused: %s is %s."), *Path, *Reason));
		return;
	}

	// A write is a change to the user's project. Read-only mode means read-only, and
	// it must hold for a path the agent reached through us as much as for a tool we
	// ran ourselves.
	if (FMCPChatToolBridge::Get().GetEffectiveMode(ActiveSessionId) == EMCPChatApprovalMode::ReadOnly)
	{
		Rpc->RespondError(Id, FMCPJsonRpcStdio::ErrorInvalidParams,
			TEXT("Refused: this conversation is read-only."));
		return;
	}

	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	const FString Directory = FPaths::GetPath(Absolute);
	if (!PF.DirectoryExists(*Directory)) { PF.CreateDirectoryTree(*Directory); }

	if (!FFileHelper::SaveStringToFile(Content, *Absolute))
	{
		Rpc->RespondError(Id, FMCPJsonRpcStdio::ErrorInternal,
			FString::Printf(TEXT("Could not write %s."), *Path));
		return;
	}

	UE_LOG(LogUnrealMCPChat, Log, TEXT("%s wrote %s"), *Agent.DisplayName, *Path);
	Rpc->Respond(Id, MakeShared<FJsonObject>());
}

void FAcpAgentBackend::HandlePermissionRequest(const TSharedPtr<FJsonObject>& Params,
                                               const TSharedPtr<FJsonValue>& Id)
{
	// Pull the tool name out of the call the agent is asking about, so our own
	// policy can be applied to it.
	FString ToolName;
	FString Title;
	const TSharedPtr<FJsonObject>* ToolCall = nullptr;
	if (Params->TryGetObjectField(TEXT("toolCall"), ToolCall) && ToolCall)
	{
		(*ToolCall)->TryGetStringField(TEXT("title"), Title);
		const TSharedPtr<FJsonObject>* RawInput = nullptr;
		if ((*ToolCall)->TryGetObjectField(TEXT("rawInput"), RawInput) && RawInput)
		{
			(*RawInput)->TryGetStringField(TEXT("name"), ToolName);
		}
	}
	if (ToolName.IsEmpty()) { ToolName = Title; }

	// Sort the agent's options into the three answers our UI offers. Option ids are
	// agent-defined, so they are matched on `kind` first and only then on id text.
	FPendingPermission Entry;
	Entry.RpcId    = Id;
	Entry.ToolName = ToolName;

	const TArray<TSharedPtr<FJsonValue>>* Options = nullptr;
	if (Params->TryGetArrayField(TEXT("options"), Options) && Options)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Options)
		{
			const TSharedPtr<FJsonObject>* Option = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Option) || !Option) { continue; }

			FString OptionId, OptionKind;
			(*Option)->TryGetStringField(TEXT("optionId"), OptionId);
			(*Option)->TryGetStringField(TEXT("kind"), OptionKind);
			if (OptionId.IsEmpty()) { continue; }

			if (OptionKind == TEXT("allow_always"))       { Entry.AllowAlwaysOptionId = OptionId; }
			else if (OptionKind == TEXT("allow_once"))    { Entry.AllowOptionId = OptionId; }
			else if (OptionKind == TEXT("reject_once")
				  || OptionKind == TEXT("reject_always")) { if (Entry.RejectOptionId.IsEmpty()) { Entry.RejectOptionId = OptionId; } }
		}
	}

	// If the agent offered nothing recognisable, we cannot answer meaningfully.
	// Cancelling is correct; guessing an option id would be worse than declining.
	if (Entry.AllowOptionId.IsEmpty() && Entry.AllowAlwaysOptionId.IsEmpty())
	{
		TSharedPtr<FJsonObject> Outcome = MakeShared<FJsonObject>();
		Outcome->SetStringField(TEXT("outcome"), TEXT("cancelled"));
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("outcome"), Outcome);
		Rpc->Respond(Id, Result);
		return;
	}

	// Apply OUR policy on top of the agent's ask. This is the one place where the
	// panel's approval mode has real authority over an agent, so it is used: a
	// blocked tool is refused without troubling the user, and an already-granted one
	// is allowed without another prompt.
	const FGuid LocalId = FGuid::NewGuid();
	if (!ToolName.IsEmpty())
	{
		FText Reason;
		const EMCPChatGateResult Gate =
			FMCPChatToolBridge::Get().EvaluateGate(ToolName, ActiveSessionId, Reason);

		if (Gate == EMCPChatGateResult::Blocked)
		{
			PendingPermissions.Add(LocalId, Entry);
			RespondToPermission(LocalId, EChatPermissionResult::Deny);

			// A card, then its result. The transcript's result handler updates an
			// EXISTING block, so emitting the result alone would show nothing at all
			// and the refusal would be invisible.
			const FGuid CardId = FGuid::NewGuid();

			FChatStreamEvent Started;
			Started.Type       = FChatStreamEvent::EType::ToolCallStarted;
			Started.MessageId  = ActiveMessageId;
			Started.ToolCallId = CardId;
			Started.ToolName   = ToolName;
			Emit(Started);

			FChatStreamEvent Finished;
			Finished.Type           = FChatStreamEvent::EType::ToolCallResult;
			Finished.MessageId      = ActiveMessageId;
			Finished.ToolCallId     = CardId;
			Finished.ToolName       = ToolName;
			Finished.bToolIsError   = true;
			Finished.ToolResultText = Reason.ToString();
			Emit(Finished);
			return;
		}

		if (Gate == EMCPChatGateResult::Allow)
		{
			PendingPermissions.Add(LocalId, Entry);
			RespondToPermission(LocalId, EChatPermissionResult::AllowOnce);
			return;
		}
	}

	PendingPermissions.Add(LocalId, Entry);

	FChatStreamEvent Event;
	Event.Type           = FChatStreamEvent::EType::PermissionRequest;
	Event.MessageId      = ActiveMessageId;
	Event.ToolCallId     = LocalId;
	Event.ToolName       = ToolName.IsEmpty() ? Title : ToolName;
	Event.ApprovalReason = Title.IsEmpty()
		? FText::Format(LOCTEXT("AgentAsks", "{0} is asking to run this."), FText::FromString(Agent.DisplayName))
		: FText::Format(LOCTEXT("AgentAsksFor", "{0} wants to: {1}"),
			FText::FromString(Agent.DisplayName), FText::FromString(Title));
	Emit(Event);
}

// ============================================================================
// Terminals
// ============================================================================

void FAcpAgentBackend::HandleTerminalCreate(const TSharedPtr<FJsonObject>& Params,
                                            const TSharedPtr<FJsonValue>& Id)
{
	FString Command;
	Params->TryGetStringField(TEXT("command"), Command);
	if (Command.IsEmpty())
	{
		Rpc->RespondError(Id, FMCPJsonRpcStdio::ErrorInvalidParams, TEXT("No command."));
		return;
	}

	TArray<FString> Args;
	const TArray<TSharedPtr<FJsonValue>>* ArgArray = nullptr;
	if (Params->TryGetArrayField(TEXT("args"), ArgArray) && ArgArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ArgArray)
		{
			FString S;
			if (Value.IsValid() && Value->TryGetString(S)) { Args.Add(S); }
		}
	}

	FString Cwd;
	Params->TryGetStringField(TEXT("cwd"), Cwd);
	FString Absolute, Reason;
	if (!Cwd.IsEmpty() && !IsPathAllowed(Cwd, Absolute, Reason))
	{
		Rpc->RespondError(Id, FMCPJsonRpcStdio::ErrorInvalidParams,
			FString::Printf(TEXT("Refused: working directory is %s."), *Reason));
		return;
	}

	TSharedPtr<FAgentTerminal> Terminal = MakeShared<FAgentTerminal>();
	Terminal->Process = MakeShared<FMCPChatProcessRunner>();

	const FString Handle = FString::Printf(TEXT("term-%d"), NextTerminalId++);
	TWeakPtr<FAcpAgentBackend> WeakSelf = AsShared();

	Terminal->Process->OnLine.BindLambda([WeakSelf, Handle](const FString& Line)
	{
		const TSharedPtr<FAcpAgentBackend> Self = WeakSelf.Pin();
		if (!Self.IsValid()) { return; }
		if (const TSharedPtr<FAgentTerminal>* Found = Self->Terminals.Find(Handle))
		{
			// Bounded: a `while true; do echo; done` must not grow until the editor
			// runs out of memory. The tail is what matters to an agent reading output.
			(*Found)->Output.Append(Line);
			(*Found)->Output.AppendChar(TEXT('\n'));
			constexpr int32 MaxOutputChars = 1024 * 1024;
			if ((*Found)->Output.Len() > MaxOutputChars)
			{
				(*Found)->Output.RightChopInline((*Found)->Output.Len() - MaxOutputChars, EAllowShrinking::No);
			}
		}
	});

	Terminal->Process->OnExited.BindLambda([WeakSelf, Handle](int32 ReturnCode, bool /*bRequested*/)
	{
		const TSharedPtr<FAcpAgentBackend> Self = WeakSelf.Pin();
		if (!Self.IsValid() || !Self->Rpc.IsValid()) { return; }

		const TSharedPtr<FAgentTerminal>* Found = Self->Terminals.Find(Handle);
		if (!Found) { return; }

		(*Found)->bExited  = true;
		(*Found)->ExitCode = ReturnCode;

		// Release everyone blocked in terminal/wait_for_exit. Leaving one waiting is
		// an indefinite hang inside the agent with nothing to point at.
		for (const TSharedPtr<FJsonValue>& WaiterId : (*Found)->Waiters)
		{
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("exitCode"), ReturnCode);
			Self->Rpc->Respond(WaiterId, Result);
		}
		(*Found)->Waiters.Reset();
	});

	FMCPChatProcessRunner::FLaunchParams Launch;
	Launch.Executable       = Command;
	Launch.Arguments        = FString::Join(Args, TEXT(" "));
	Launch.WorkingDirectory = Absolute.IsEmpty()
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) : Absolute;
	Launch.Label            = FString::Printf(TEXT("%s terminal"), *Agent.DisplayName);

	FText Error;
	if (!Terminal->Process->Launch(Launch, Error))
	{
		Rpc->RespondError(Id, FMCPJsonRpcStdio::ErrorInternal, Error.ToString());
		return;
	}

	Terminals.Add(Handle, Terminal);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("terminalId"), Handle);
	Rpc->Respond(Id, Result);
}

void FAcpAgentBackend::HandleTerminalOutput(const TSharedPtr<FJsonObject>& Params,
                                            const TSharedPtr<FJsonValue>& Id)
{
	FString Handle;
	Params->TryGetStringField(TEXT("terminalId"), Handle);

	const TSharedPtr<FAgentTerminal>* Found = Terminals.Find(Handle);
	if (!Found)
	{
		Rpc->RespondError(Id, FMCPJsonRpcStdio::ErrorInvalidParams, TEXT("No such terminal."));
		return;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("output"), (*Found)->Output);
	Result->SetBoolField(TEXT("truncated"), false);
	if ((*Found)->bExited)
	{
		Result->SetNumberField(TEXT("exitCode"), (*Found)->ExitCode);
	}
	Rpc->Respond(Id, Result);
}

void FAcpAgentBackend::HandleTerminalWait(const TSharedPtr<FJsonObject>& Params,
                                          const TSharedPtr<FJsonValue>& Id)
{
	FString Handle;
	Params->TryGetStringField(TEXT("terminalId"), Handle);

	const TSharedPtr<FAgentTerminal>* Found = Terminals.Find(Handle);
	if (!Found)
	{
		Rpc->RespondError(Id, FMCPJsonRpcStdio::ErrorInvalidParams, TEXT("No such terminal."));
		return;
	}

	if ((*Found)->bExited)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("exitCode"), (*Found)->ExitCode);
		Rpc->Respond(Id, Result);
		return;
	}

	// Held, not polled: the response goes out from the process's OnExited. This is
	// the one place the client legitimately does not answer immediately.
	(*Found)->Waiters.Add(Id);
}

void FAcpAgentBackend::HandleTerminalKill(const TSharedPtr<FJsonObject>& Params,
                                          const TSharedPtr<FJsonValue>& Id)
{
	FString Handle;
	Params->TryGetStringField(TEXT("terminalId"), Handle);

	if (const TSharedPtr<FAgentTerminal>* Found = Terminals.Find(Handle))
	{
		if ((*Found)->Process.IsValid()) { (*Found)->Process->Stop(0.f); }
	}
	Rpc->Respond(Id, MakeShared<FJsonObject>());
}

void FAcpAgentBackend::HandleTerminalRelease(const TSharedPtr<FJsonObject>& Params,
                                             const TSharedPtr<FJsonValue>& Id)
{
	FString Handle;
	Params->TryGetStringField(TEXT("terminalId"), Handle);

	if (TSharedPtr<FAgentTerminal> Terminal;
		Terminals.RemoveAndCopyValue(Handle, Terminal) && Terminal.IsValid())
	{
		// Answer any waiters before dropping the terminal, or they hang forever.
		for (const TSharedPtr<FJsonValue>& WaiterId : Terminal->Waiters)
		{
			Rpc->RespondError(WaiterId, FMCPJsonRpcStdio::ErrorInternal,
				TEXT("The terminal was released."));
		}
		if (Terminal->Process.IsValid()) { Terminal->Process->Stop(0.f); }
	}

	Rpc->Respond(Id, MakeShared<FJsonObject>());
}

// ============================================================================
// Emission
// ============================================================================

void FAcpAgentBackend::Emit(const FChatStreamEvent& Event)
{
	check(IsInGameThread());
	OnStreamEvent.Broadcast(Event);
}

void FAcpAgentBackend::EmitText(const FString& Text)
{
	FChatStreamEvent Event;
	Event.Type      = FChatStreamEvent::EType::TextDelta;
	Event.MessageId = ActiveMessageId;
	Event.Text      = Text;
	Emit(Event);
}

void FAcpAgentBackend::EmitError(const FText& Message)
{
	EmitError(Message.ToString());
}

void FAcpAgentBackend::EmitError(const FString& Message)
{
	FChatStreamEvent Event;
	Event.Type      = FChatStreamEvent::EType::Error;
	Event.MessageId = ActiveMessageId;
	Event.Text      = FMCPChatSecretStore::Get().RedactSecrets(Message);
	Emit(Event);
}

void FAcpAgentBackend::FinishTurn()
{
	if (!bTurnActive) { return; }
	bTurnActive = false;

	// Any permission still on screen belongs to a turn that is over. Answering
	// "cancelled" keeps the agent from blocking on a card the user can no longer
	// meaningfully act on.
	if (Rpc.IsValid())
	{
		for (const TPair<FGuid, FPendingPermission>& Pair : PendingPermissions)
		{
			TSharedPtr<FJsonObject> Outcome = MakeShared<FJsonObject>();
			Outcome->SetStringField(TEXT("outcome"), TEXT("cancelled"));
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetObjectField(TEXT("outcome"), Outcome);
			Rpc->Respond(Pair.Value.RpcId, Result);
		}
	}
	PendingPermissions.Reset();

	FChatStreamEvent Event;
	Event.Type      = FChatStreamEvent::EType::TurnFinished;
	Event.MessageId = ActiveMessageId;
	Emit(Event);
}

bool FAcpAgentBackend::Tick(float /*DeltaTime*/)
{
	if (Rpc.IsValid()) { Rpc->TickTimeouts(); }

	if (bTurnActive && FPlatformTime::Seconds() - TurnStartSeconds > MaxTurnSeconds)
	{
		EmitError(FText::Format(
			LOCTEXT("AgentWedged",
				"{0} has been running for {1} minutes with no result. Stopping the turn; the agent is still alive."),
			FText::FromString(Agent.DisplayName),
			FText::AsNumber(FMath::RoundToInt(MaxTurnSeconds / 60.0))));
		CancelTurn(ActiveSessionId);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
