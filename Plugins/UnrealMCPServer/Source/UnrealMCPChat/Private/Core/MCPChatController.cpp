// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatController.h"
#include "MCPChatContext.h"
#include "MCPChatModelCatalog.h"
#include "MCPChatStore.h"
#include "MCPChatToolBridge.h"
#include "UnrealMCPChatModule.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "MCPChatController"

FMCPChatController::FMCPChatController()
{
}

FMCPChatController::~FMCPChatController()
{
	for (const TPair<FString, FChatBackendPtr>& Pair : Backends)
	{
		if (Pair.Value.IsValid())
		{
			if (const FDelegateHandle* Handle = BackendHandles.Find(Pair.Key))
			{
				Pair.Value->OnStreamEvent.Remove(*Handle);
			}
		}
	}
}

// ============================================================================
// Backends
// ============================================================================

void FMCPChatController::RegisterBackend(const FChatBackendPtr& Backend)
{
	if (!Backend.IsValid()) { return; }

	const FString Id = Backend->GetId();

	// Re-registering must not double-subscribe — that would duplicate every token.
	if (const FDelegateHandle* Existing = BackendHandles.Find(Id))
	{
		if (const FChatBackendPtr* Old = Backends.Find(Id))
		{
			if (Old->IsValid()) { (*Old)->OnStreamEvent.Remove(*Existing); }
		}
	}
	else
	{
		BackendOrder.Add(Id);
	}

	Backends.Add(Id, Backend);
	BackendHandles.Add(Id, Backend->OnStreamEvent.AddRaw(this, &FMCPChatController::HandleStreamEvent));

	UE_LOG(LogUnrealMCPChat, Log, TEXT("Registered chat backend '%s'"), *Id);
}

FChatBackendPtr FMCPChatController::FindBackend(const FString& BackendId) const
{
	if (const FChatBackendPtr* Found = Backends.Find(BackendId))
	{
		return *Found;
	}
	return nullptr;
}

TArray<FChatBackendPtr> FMCPChatController::GetBackends() const
{
	TArray<FChatBackendPtr> Result;
	Result.Reserve(BackendOrder.Num());
	for (const FString& Id : BackendOrder)
	{
		if (const FChatBackendPtr* Found = Backends.Find(Id))
		{
			Result.Add(*Found);
		}
	}
	return Result;
}

// ============================================================================
// Sessions
// ============================================================================

void FMCPChatController::SetActiveSession(const FChatSessionPtr& Session)
{
	if (ActiveSession == Session) { return; }

	// Leaving a session mid-turn would strand the backend, so stop cleanly first.
	if (bTurnInFlight)
	{
		CancelTurn();
	}

	// "Allow for this session" must not leak into the next conversation.
	if (ActiveSession.IsValid())
	{
		FMCPChatToolBridge::Get().ClearSessionGrants(ActiveSession->Id);
	}

	ActiveSession = Session;
	StreamingMessage.Reset();
	bAwaitingApproval = false;

	if (ActiveSession.IsValid())
	{
		if (const FChatBackendPtr Backend = FindBackend(ActiveSession->BackendId))
		{
			Backend->StartSession(ActiveSession->Id);
		}
	}

	OnTranscriptChanged.Broadcast();
}

FChatSessionPtr FMCPChatController::NewSession()
{
	// Inherit the current backend/model so "New chat" doesn't silently reset the
	// user's choice.
	FString BackendId = ActiveSession.IsValid() ? ActiveSession->BackendId : FString();
	FString ModelId   = ActiveSession.IsValid() ? ActiveSession->ModelId : FString();

	if (BackendId.IsEmpty())
	{
		// First run: pick the first backend that is actually usable, so a user with
		// ANTHROPIC_API_KEY exported lands on a real model instead of the mock.
		for (const FChatBackendPtr& Backend : GetBackends())
		{
			FText Reason;
			if (Backend.IsValid() && Backend->GetKind() != EChatBackendKind::Mock
				&& Backend->IsAvailable(Reason))
			{
				BackendId = Backend->GetId();
				const TArray<FChatModelInfo> Models = Backend->GetModels();
				if (Models.Num() > 0) { ModelId = Models[0].ModelId; }
				break;
			}
		}
	}
	if (BackendId.IsEmpty() && BackendOrder.Num() > 0)
	{
		BackendId = BackendOrder[0];
	}

	FChatSessionPtr Session = FChatSession::CreateNew(BackendId, ModelId);
	FMCPChatStore::Get().AddSession(Session);
	SetActiveSession(Session);
	return Session;
}

void FMCPChatController::OpenSession(const FGuid& SessionId)
{
	if (const FChatSessionPtr Session = FMCPChatStore::Get().LoadSession(SessionId))
	{
		SetActiveSession(Session);
	}
	else
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("Could not open session %s"),
			*SessionId.ToString(EGuidFormats::DigitsWithHyphens));
	}
}

// ============================================================================
// Turns
// ============================================================================

// ============================================================================
// Session mutation (Phase 6)
// ============================================================================

void FMCPChatController::SwitchModel(const FString& BackendId, const FString& ModelId)
{
	if (!ActiveSession.IsValid()) { return; }
	if (ActiveSession->BackendId == BackendId && ActiveSession->ModelId == ModelId) { return; }

	const FChatBackendPtr NewBackend = FindBackend(BackendId);
	if (!NewBackend.IsValid())
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("Cannot switch to unknown backend '%s'."), *BackendId);
		return;
	}

	// Only mark the transcript if there is a transcript to mark. A divider as the
	// first row of an empty conversation is noise.
	if (ActiveSession->Messages.Num() > 0)
	{
		FString Label;
		if (const FChatModelInfo* Info = FMCPChatModelCatalog::Get().FindModel(ModelId))
		{
			Label = Info->DisplayName;
		}
		if (Label.IsEmpty()) { Label = ModelId.IsEmpty() ? NewBackend->GetDisplayName().ToString() : ModelId; }

		FChatMessagePtr Marker = MakeShared<FChatMessage>();
		Marker->Id        = FGuid::NewGuid();
		Marker->Role      = EChatRole::System;
		Marker->Timestamp = FDateTime::UtcNow();
		Marker->Blocks.Add(FChatContentBlock::MakeDivider(
			FString::Printf(TEXT("switched to %s"), *Label)));
		ActiveSession->AppendMessage(Marker);
	}

	ActiveSession->BackendId = BackendId;
	ActiveSession->ModelId   = ModelId;

	// Capability flags differ per model. Carrying `bHasSampling` across a switch to a
	// model that rejects `temperature` produces a 400 the user cannot explain.
	if (const FChatModelInfo* Info = FMCPChatModelCatalog::Get().FindModel(ModelId))
	{
		ActiveSession->Params.bHasSampling = Info->bSampling && ActiveSession->Params.bHasSampling;
		if (Info->MaxOutputTokens > 0)
		{
			ActiveSession->Params.MaxOutputTokens =
				FMath::Min(ActiveSession->Params.MaxOutputTokens, Info->MaxOutputTokens);
		}
		if (Info->EffortLevels.Num() > 0 && !Info->EffortLevels.Contains(ActiveSession->Params.Effort))
		{
			ActiveSession->Params.Effort = Info->EffortLevels.Last();
		}
		else if (Info->EffortLevels.Num() == 0)
		{
			ActiveSession->Params.Effort.Reset();
		}
	}

	FMCPChatStore::Get().MarkDirty(ActiveSession->Id);
	OnTranscriptChanged.Broadcast();
}

void FMCPChatController::ClearActiveTranscript()
{
	if (!ActiveSession.IsValid()) { return; }

	ActiveSession->Messages.Reset();
	ActiveSession->ActiveChildByParent.Reset();
	StreamingMessage.Reset();

	FMCPChatStore::Get().MarkDirty(ActiveSession->Id);
	OnTranscriptChanged.Broadcast();
}

FText FMCPChatController::CompactActiveSession(int32 KeepRecentMessages)
{
	if (!ActiveSession.IsValid()) { return LOCTEXT("CompactNoSession", "No conversation to compact."); }

	const TArray<FChatMessagePtr> Path = ActiveSession->BuildActivePath();
	if (Path.Num() <= KeepRecentMessages)
	{
		return LOCTEXT("CompactNothing", "Nothing to compact yet.");
	}

	const int32 CutIndex = Path.Num() - KeepRecentMessages;

	int32 UserTurns = 0, AssistantTurns = 0;
	TArray<FString> ToolsUsed;
	TArray<FString> Asks;

	for (int32 i = 0; i < CutIndex; ++i)
	{
		const FChatMessagePtr& M = Path[i];
		if (!M.IsValid()) { continue; }

		if (M->Role == EChatRole::User)
		{
			++UserTurns;
			// Keep what was ASKED verbatim. The requests are the thread of the
			// conversation; the answers are recoverable, the intent is not.
			const FString Preview = M->GetPreview(160);
			if (!Preview.IsEmpty()) { Asks.Add(Preview); }
		}
		else if (M->Role == EChatRole::Assistant)
		{
			++AssistantTurns;
		}

		for (const FChatContentBlock& B : M->Blocks)
		{
			if (B.Type == FChatContentBlock::EType::ToolCall && !B.ToolName.IsEmpty())
			{
				ToolsUsed.AddUnique(B.ToolName);
			}
		}
	}

	TStringBuilder<4096> SB;
	SB.Append(TEXT("[Earlier in this conversation, compacted locally]\n\n"));
	SB.Appendf(TEXT("%d earlier messages (%d from me, %d from you) were replaced by this summary.\n\n"),
		CutIndex, UserTurns, AssistantTurns);

	if (Asks.Num() > 0)
	{
		SB.Append(TEXT("What I asked for, in order:\n"));
		for (const FString& Ask : Asks) { SB.Appendf(TEXT("  - %s\n"), *Ask); }
		SB.Append(TEXT("\n"));
	}

	if (ToolsUsed.Num() > 0)
	{
		SB.Appendf(TEXT("Editor tools already used: %s\n\n"), *FString::Join(ToolsUsed, TEXT(", ")));
	}

	SB.Append(TEXT("The full transcript is still on disk; nothing was deleted from the record. "
	               "If you need a detail from before this point, say so and re-read the relevant "
	               "state with a tool rather than guessing.\n"));

	// Build the replacement history: the digest as one user message, then the tail.
	TArray<FChatMessagePtr> Kept;
	FChatMessagePtr Digest = MakeShared<FChatMessage>();
	Digest->Id        = FGuid::NewGuid();
	Digest->Role      = EChatRole::User;
	Digest->Timestamp = Path[0].IsValid() ? Path[0]->Timestamp : FDateTime::UtcNow();
	Digest->Blocks.Add(FChatContentBlock::MakeText(SB.ToString()));
	Kept.Add(Digest);

	for (int32 i = CutIndex; i < Path.Num(); ++i)
	{
		if (Path[i].IsValid()) { Kept.Add(Path[i]); }
	}

	// Re-parent into a single straight line. Compaction discards branches by design:
	// keeping alternates whose parents no longer exist would leave the stepper
	// pointing at messages that cannot be reached.
	FGuid Previous;
	for (const FChatMessagePtr& M : Kept)
	{
		M->ParentId = Previous;
		Previous = M->Id;
	}

	ActiveSession->Messages = MoveTemp(Kept);
	ActiveSession->ActiveChildByParent.Reset();

	FMCPChatStore::Get().MarkDirty(ActiveSession->Id);
	OnTranscriptChanged.Broadcast();

	return FText::Format(
		LOCTEXT("CompactDone", "Compacted {0} earlier messages into a summary. The last {1} are unchanged."),
		FText::AsNumber(CutIndex), FText::AsNumber(KeepRecentMessages));
}

void FMCPChatController::SetSessionTitle(const FString& Title)
{
	if (!ActiveSession.IsValid() || Title.IsEmpty()) { return; }
	ActiveSession->Title = Title;
	FMCPChatStore::Get().MarkDirty(ActiveSession->Id);
	OnTranscriptChanged.Broadcast();
}

void FMCPChatController::SetSessionPinned(bool bPinned)
{
	if (!ActiveSession.IsValid()) { return; }
	ActiveSession->bPinned = bPinned;
	FMCPChatStore::Get().MarkDirty(ActiveSession->Id);
	OnTranscriptChanged.Broadcast();
}

void FMCPChatController::AddPinnedContext(const FString& KindAndTarget)
{
	if (!ActiveSession.IsValid() || KindAndTarget.IsEmpty()) { return; }
	ActiveSession->PinnedContext.AddUnique(KindAndTarget);
	FMCPChatStore::Get().MarkDirty(ActiveSession->Id);
}

void FMCPChatController::RemovePinnedContext(const FString& KindAndTarget)
{
	if (!ActiveSession.IsValid()) { return; }
	ActiveSession->PinnedContext.Remove(KindAndTarget);
	FMCPChatStore::Get().MarkDirty(ActiveSession->Id);
}

void FMCPChatController::RunToolDirectly(const FString& ToolName, const TSharedPtr<FJsonObject>& Args)
{
	if (!ActiveSession.IsValid()) { NewSession(); }
	if (!ActiveSession.IsValid()) { return; }

	FMCPChatToolBridge& Bridge = FMCPChatToolBridge::Get();

	// The user asked for it directly, so there is no model turn to pause — but the
	// gate still applies. A blocked or approval-needing tool is reported rather than
	// run, and the user re-issues it from the card with full context.
	FText Reason;
	const EMCPChatGateResult Gate = Bridge.EvaluateGate(ToolName, ActiveSession->Id, Reason);

	FChatMessagePtr Message = MakeShared<FChatMessage>();
	Message->Id        = FGuid::NewGuid();
	Message->Role      = EChatRole::Tool;
	Message->Timestamp = FDateTime::UtcNow();

	FChatContentBlock Block;
	Block.Type       = FChatContentBlock::EType::ToolCall;
	Block.ToolCallId = FGuid::NewGuid();
	Block.ToolName   = ToolName;
	Block.ToolArgs   = Args.IsValid() ? Args : MakeShared<FJsonObject>();

	if (Gate == EMCPChatGateResult::Blocked)
	{
		Block.bToolIsError   = true;
		Block.ToolResultText = Reason.ToString();
	}
	else if (Gate == EMCPChatGateResult::NeedsApproval)
	{
		Block.bToolAwaitingApproval = true;
		Block.ToolResultText        = Reason.ToString();
	}
	else
	{
		const FMCPChatToolBridge::FExecutionResult Result = Bridge.Execute(
			ToolName, Block.ToolArgs, ActiveSession->Id,
			Bridge.GetEffectiveMode(ActiveSession->Id), nullptr, nullptr);

		Block.bToolIsError    = Result.bIsError;
		Block.ToolResultText  = Result.Text;
		Block.ToolResult      = Result.Structured;
		Block.DurationSeconds = Result.DurationSeconds;
	}

	Message->Blocks.Add(MoveTemp(Block));
	ActiveSession->AppendMessage(Message);
	FMCPChatStore::Get().MarkDirty(ActiveSession->Id);
	OnTranscriptChanged.Broadcast();
}

void FMCPChatController::SendMessage(const FString& Text, const TArray<FChatContentBlock>& Attachments)
{
	if (bTurnInFlight)
	{
		UE_LOG(LogUnrealMCPChat, Verbose, TEXT("Ignoring send: a turn is already in flight."));
		return;
	}

	if (!ActiveSession.IsValid())
	{
		NewSession();
	}
	if (!ActiveSession.IsValid()) { return; }

	const FChatBackendPtr Backend = FindBackend(ActiveSession->BackendId);
	if (!Backend.IsValid())
	{
		UE_LOG(LogUnrealMCPChat, Warning,
			TEXT("Session has no usable backend ('%s')."), *ActiveSession->BackendId);
		return;
	}

	// --- User message ---
	FChatMessagePtr UserMessage = MakeShared<FChatMessage>();
	UserMessage->Id        = FGuid::NewGuid();
	UserMessage->Role      = EChatRole::User;
	UserMessage->Timestamp = FDateTime::UtcNow();
	UserMessage->Blocks    = Attachments;

	// Pinned context is re-resolved on EVERY turn, not captured once. "Always tell
	// the model about my current level" is worthless if it means the level as it was
	// nine turns ago. Pinned blocks go first so the model reads the situation before
	// the request.
	if (ActiveSession->PinnedContext.Num() > 0)
	{
		TArray<FChatContentBlock> PinnedBlocks;
		for (const FString& Entry : ActiveSession->PinnedContext)
		{
			FString KindStr = Entry;
			FString Target;
			Entry.Split(TEXT(":"), &KindStr, &Target);

			FChatContentBlock B;
			B.Type           = FChatContentBlock::EType::ContextRef;
			B.ContextKind    = KindStr;
			B.ContextTarget  = Target;
			B.bContextPinned = true;

			const EChatContextKind Kind = FMCPChatContextResolver::KindFromString(KindStr);
			B.DisplayName = FMCPChatContextResolver::MakeLabel(Kind, Target);

			FText Error;
			FString Resolved;
			if (FMCPChatContextResolver::Get().Resolve(Kind, Target, Resolved, Error))
			{
				B.Text = Resolved;
			}
			else
			{
				// A pinned context that stops resolving (the actor was deleted) must not
				// silently vanish — the model should know it was asked for and is gone.
				B.Text = FString::Printf(TEXT("[%s could not be resolved: %s]"),
					*B.DisplayName, *Error.ToString());
			}
			PinnedBlocks.Add(MoveTemp(B));
		}
		UserMessage->Blocks.Insert(PinnedBlocks, 0);
	}

	if (!Text.IsEmpty())
	{
		UserMessage->Blocks.Add(FChatContentBlock::MakeText(Text));
	}

	ActiveSession->AppendMessage(UserMessage);
	ActiveSession->EnsureTitle();
	FMCPChatStore::Get().MarkDirty(ActiveSession->Id);
	OnTranscriptChanged.Broadcast();

	// --- Dispatch ---
	CancelFlag = MakeShared<TAtomic<bool>>(false);

	FChatTurnRequest Request;
	Request.SessionId   = ActiveSession->Id;
	Request.History     = ActiveSession->BuildActivePath();
	Request.UserMessage = UserMessage;
	Request.ModelId     = ActiveSession->ModelId;
	Request.Params      = ActiveSession->Params;
	Request.CancelFlag  = CancelFlag;

	bTurnInFlight = true;
	OnTurnStateChanged.Broadcast(true);

	Backend->SendTurn(Request);
}

void FMCPChatController::RespondToPermission(const FGuid& ToolCallId, EChatPermissionResult Result)
{
	if (!ActiveSession.IsValid()) { return; }

	if (FChatContentBlock* Block = FindToolBlock(ToolCallId))
	{
		Block->bToolAwaitingApproval = false;
	}
	bAwaitingApproval = false;
	OnStreamingMessageUpdated.Broadcast();

	if (const FChatBackendPtr Backend = FindBackend(ActiveSession->BackendId))
	{
		Backend->RespondToPermission(ToolCallId, Result);
	}
}

void FMCPChatController::CancelTurn()
{
	if (!bTurnInFlight) { return; }

	if (CancelFlag.IsValid())
	{
		CancelFlag->Store(true, EMemoryOrder::Relaxed);
	}
	if (ActiveSession.IsValid())
	{
		if (const FChatBackendPtr Backend = FindBackend(ActiveSession->BackendId))
		{
			Backend->CancelTurn(ActiveSession->Id);
		}
	}
	// The backend is expected to answer with TurnFinished; FinishStreamingMessage
	// clears the in-flight flag there so a cancelled partial reply is still kept.
}

void FMCPChatController::RetryLastTurn()
{
	if (bTurnInFlight || !ActiveSession.IsValid()) { return; }

	const TArray<FChatMessagePtr> Path = ActiveSession->BuildActivePath();
	if (Path.Num() == 0) { return; }

	// Find the last assistant message and the user turn that produced it.
	int32 AssistantIdx = INDEX_NONE;
	for (int32 i = Path.Num() - 1; i >= 0; --i)
	{
		if (Path[i].IsValid() && Path[i]->Role == EChatRole::Assistant)
		{
			AssistantIdx = i;
			break;
		}
	}
	if (AssistantIdx == INDEX_NONE) { return; }

	const FChatMessagePtr OldAssistant = Path[AssistantIdx];

	const FChatBackendPtr Backend = FindBackend(ActiveSession->BackendId);
	if (!Backend.IsValid()) { return; }

	// The retry is a SIBLING of the old answer: same parent, new id. The old answer
	// stays reachable through the sibling stepper instead of being overwritten.
	CancelFlag = MakeShared<TAtomic<bool>>(false);

	FChatTurnRequest Request;
	Request.SessionId  = ActiveSession->Id;
	Request.ModelId    = ActiveSession->ModelId;
	Request.Params     = ActiveSession->Params;
	Request.CancelFlag = CancelFlag;

	// History is everything up to (but excluding) the answer being replaced.
	for (int32 i = 0; i < AssistantIdx; ++i)
	{
		Request.History.Add(Path[i]);
	}
	if (AssistantIdx > 0 && Path[AssistantIdx - 1]->Role == EChatRole::User)
	{
		Request.UserMessage = Path[AssistantIdx - 1];
	}

	// Pre-create the sibling so streaming has somewhere to land.
	StreamingMessage = MakeShared<FChatMessage>();
	StreamingMessage->Id          = FGuid::NewGuid();
	StreamingMessage->Role        = EChatRole::Assistant;
	StreamingMessage->Timestamp   = FDateTime::UtcNow();
	StreamingMessage->ParentId    = OldAssistant->ParentId;
	StreamingMessage->ModelId     = ActiveSession->ModelId;
	StreamingMessage->BackendId   = ActiveSession->BackendId;
	StreamingMessage->bIsStreaming = true;

	bTurnInFlight = true;
	OnTurnStateChanged.Broadcast(true);
	OnTranscriptChanged.Broadcast();

	Backend->SendTurn(Request);
}

// ============================================================================
// Stream handling
// ============================================================================

void FMCPChatController::BeginStreamingMessage(const FGuid& MessageId)
{
	if (StreamingMessage.IsValid()) { return; }   // retry pre-created it

	StreamingMessage = MakeShared<FChatMessage>();
	StreamingMessage->Id           = MessageId.IsValid() ? MessageId : FGuid::NewGuid();
	StreamingMessage->Role         = EChatRole::Assistant;
	StreamingMessage->Timestamp    = FDateTime::UtcNow();
	StreamingMessage->bIsStreaming = true;

	if (ActiveSession.IsValid())
	{
		StreamingMessage->ModelId   = ActiveSession->ModelId;
		StreamingMessage->BackendId = ActiveSession->BackendId;
	}
}

void FMCPChatController::FinishStreamingMessage()
{
	if (StreamingMessage.IsValid() && ActiveSession.IsValid())
	{
		StreamingMessage->bIsStreaming = false;

		// Close any tool block that never got a result (cancelled mid-call), so the
		// card renders as interrupted rather than spinning forever after reload.
		for (FChatContentBlock& B : StreamingMessage->Blocks)
		{
			if (B.Type == FChatContentBlock::EType::ToolCall && !B.bIsComplete)
			{
				B.bIsComplete = true;
				B.bToolIsError = true;
				B.ToolResultText = TEXT("Interrupted before the tool returned.");
			}
		}

		// Drop a turn that produced nothing at all rather than leaving an empty row.
		const bool bHasContent = StreamingMessage->Blocks.ContainsByPredicate(
			[](const FChatContentBlock& B) { return !B.Text.IsEmpty() || B.Type != FChatContentBlock::EType::Text; });

		if (bHasContent)
		{
			ActiveSession->AppendMessage(StreamingMessage);
			FMCPChatStore::Get().MarkDirty(ActiveSession->Id);
		}
	}

	StreamingMessage.Reset();
	bTurnInFlight = false;
	bAwaitingApproval = false;
	CancelFlag.Reset();

	OnTurnStateChanged.Broadcast(false);
	OnTranscriptChanged.Broadcast();
}

FChatContentBlock& FMCPChatController::GetOrAddTrailingBlock(FChatContentBlock::EType MatchType)
{
	check(StreamingMessage.IsValid());

	if (StreamingMessage->Blocks.Num() > 0)
	{
		FChatContentBlock& Last = StreamingMessage->Blocks.Last();
		if (Last.Type == MatchType && Last.bIsComplete)
		{
			return Last;
		}
	}

	FChatContentBlock New;
	New.Type = MatchType;
	return StreamingMessage->Blocks.Add_GetRef(MoveTemp(New));
}

FChatContentBlock* FMCPChatController::FindToolBlock(const FGuid& ToolCallId)
{
	if (!StreamingMessage.IsValid()) { return nullptr; }
	for (FChatContentBlock& B : StreamingMessage->Blocks)
	{
		if (B.Type == FChatContentBlock::EType::ToolCall && B.ToolCallId == ToolCallId)
		{
			return &B;
		}
	}
	return nullptr;
}

void FMCPChatController::HandleStreamEvent(const FChatStreamEvent& Event)
{
	// Backends guarantee game-thread delivery; assert it so a future transport
	// that forgets to marshal fails loudly here rather than corrupting Slate.
	check(IsInGameThread());

	using EType = FChatStreamEvent::EType;

	switch (Event.Type)
	{
	case EType::TurnStarted:
		BeginStreamingMessage(Event.MessageId);
		OnStreamingMessageUpdated.Broadcast();
		break;

	case EType::TextDelta:
	{
		BeginStreamingMessage(Event.MessageId);
		GetOrAddTrailingBlock(FChatContentBlock::EType::Text).Text += Event.Text;
		OnStreamingMessageUpdated.Broadcast();
		break;
	}

	case EType::ThinkingDelta:
	{
		BeginStreamingMessage(Event.MessageId);
		GetOrAddTrailingBlock(FChatContentBlock::EType::Thinking).Text += Event.Text;
		OnStreamingMessageUpdated.Broadcast();
		break;
	}

	case EType::ToolCallStarted:
	{
		BeginStreamingMessage(Event.MessageId);

		// Close the trailing text block so a later delta starts a new one after the
		// card, preserving the model's actual ordering.
		if (StreamingMessage->Blocks.Num() > 0)
		{
			StreamingMessage->Blocks.Last().bIsComplete = true;
		}

		FChatContentBlock Call;
		Call.Type           = FChatContentBlock::EType::ToolCall;
		Call.ToolCallId     = Event.ToolCallId;
		Call.ProviderCallId = Event.ProviderCallId;
		Call.ToolName       = Event.ToolName;
		Call.bIsComplete    = false;   // "receiving arguments…"
		StreamingMessage->Blocks.Add(MoveTemp(Call));

		OnStreamingMessageUpdated.Broadcast();
		break;
	}

	case EType::ToolArgsDelta:
	{
		// Tool arguments stream as partial JSON. Buffer and only parse when the call
		// completes — parsing a fragment produces noise, not information.
		if (FChatContentBlock* Block = FindToolBlock(Event.ToolCallId))
		{
			Block->PartialArgsBuffer += Event.Text;
			OnStreamingMessageUpdated.Broadcast();
		}
		break;
	}

	case EType::ToolCallResult:
	{
		if (FChatContentBlock* Block = FindToolBlock(Event.ToolCallId))
		{
			Block->bIsComplete            = true;
			Block->bToolAwaitingApproval  = false;
			Block->bToolIsError           = Event.bToolIsError;
			Block->DurationSeconds = Event.DurationSeconds;
			Block->ToolResult      = Event.ToolResult;
			Block->ToolResultText  = Event.ToolResultText;

			if (Event.ToolArgs.IsValid())
			{
				Block->ToolArgs = Event.ToolArgs;
			}
			else if (!Block->PartialArgsBuffer.IsEmpty())
			{
				TSharedPtr<FJsonObject> Parsed;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Block->PartialArgsBuffer);
				if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
				{
					Block->ToolArgs = Parsed;
				}
				// If it doesn't parse we keep the raw buffer; the card shows it verbatim
				// rather than pretending there were no arguments.
			}
			OnStreamingMessageUpdated.Broadcast();
		}
		break;
	}

	case EType::PlanUpdate:
	{
		BeginStreamingMessage(Event.MessageId);

		// A plan is REWRITTEN, not appended. An agent republishes the whole checklist
		// every time one item changes; appending would stack six copies of the same
		// list down the transcript.
		FChatContentBlock* Existing = nullptr;
		for (FChatContentBlock& Block : StreamingMessage->Blocks)
		{
			if (Block.Type == FChatContentBlock::EType::Plan) { Existing = &Block; break; }
		}

		if (Existing)
		{
			Existing->Text = Event.Text;
		}
		else
		{
			FChatContentBlock Plan;
			Plan.Type = FChatContentBlock::EType::Plan;
			Plan.Text = Event.Text;
			StreamingMessage->Blocks.Add(MoveTemp(Plan));
		}

		OnStreamingMessageUpdated.Broadcast();
		break;
	}

	case EType::ModeChanged:
	{
		BeginStreamingMessage(Event.MessageId);
		StreamingMessage->Blocks.Add(FChatContentBlock::MakeDivider(
			FString::Printf(TEXT("mode: %s"), *Event.Text)));
		OnStreamingMessageUpdated.Broadcast();
		break;
	}

	case EType::UsageUpdate:
		if (StreamingMessage.IsValid())
		{
			StreamingMessage->Usage.Accumulate(Event.Usage);
		}
		break;

	case EType::Refusal:
	{
		BeginStreamingMessage(Event.MessageId);
		FChatContentBlock Block;
		Block.Type = FChatContentBlock::EType::Refusal;
		Block.Text = Event.Text;
		StreamingMessage->Blocks.Add(MoveTemp(Block));
		OnStreamingMessageUpdated.Broadcast();
		break;
	}

	case EType::Error:
	{
		BeginStreamingMessage(Event.MessageId);
		StreamingMessage->Blocks.Add(FChatContentBlock::MakeError(Event.Text));
		OnStreamingMessageUpdated.Broadcast();
		break;
	}

	case EType::PermissionRequest:
	{
		// Mark the card as awaiting a decision and let the transcript render the
		// choice inline, where the call is — not as a modal that hides the context
		// the user needs in order to decide.
		if (FChatContentBlock* Block = FindToolBlock(Event.ToolCallId))
		{
			Block->bToolAwaitingApproval = true;
			if (Event.ToolArgs.IsValid()) { Block->ToolArgs = Event.ToolArgs; }
			if (!Event.ApprovalReason.IsEmpty())
			{
				Block->ToolResultText = Event.ApprovalReason.ToString();
			}
		}
		bAwaitingApproval = true;
		OnStreamingMessageUpdated.Broadcast();
		break;
	}

	case EType::TurnFinished:
		FinishStreamingMessage();
		break;
	}
}

#undef LOCTEXT_NAMESPACE
