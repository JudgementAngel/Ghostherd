// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Backends/MockChatBackend.h"
#include "UnrealMCPChatModule.h"

#include "MCPToolRegistry.h"
#include "Dom/JsonObject.h"

#define LOCTEXT_NAMESPACE "MockChatBackend"

FMockChatBackend::FMockChatBackend()
{
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FMockChatBackend::TickScript), 0.f);
}

FMockChatBackend::~FMockChatBackend()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

FText FMockChatBackend::GetDisplayName() const
{
	return LOCTEXT("MockBackend", "Mock (offline demo)");
}

TArray<FChatModelInfo> FMockChatBackend::GetModels() const
{
	FChatModelInfo Info;
	Info.ModelId         = TEXT("mock-1");
	Info.DisplayName     = TEXT("Mock");
	Info.ProviderId      = TEXT("mock");
	Info.ContextTokens   = 200000;
	Info.MaxOutputTokens = 8192;
	Info.bVision         = true;
	Info.bTools          = true;
	Info.bSampling       = false;
	Info.EffortLevels    = { TEXT("low"), TEXT("medium"), TEXT("high") };
	return { Info };
}

void FMockChatBackend::SendTurn(const FChatTurnRequest& Request)
{
	ActiveSessionId = Request.SessionId;
	ActiveMessageId = FGuid::NewGuid();
	CancelFlag      = Request.CancelFlag;

	Script.Reset();
	ScriptCursor = 0;
	TimeUntilNextStep = 0.f;

	BuildScript(Request);
}

void FMockChatBackend::CancelTurn(const FGuid& SessionId)
{
	if (SessionId != ActiveSessionId) { return; }

	Script.Reset();
	ScriptCursor = 0;

	FChatStreamEvent Finished;
	Finished.Type = FChatStreamEvent::EType::TurnFinished;
	Finished.MessageId = ActiveMessageId;
	OnStreamEvent.Broadcast(Finished);
}

void FMockChatBackend::EndSession(const FGuid& SessionId)
{
	if (SessionId == ActiveSessionId)
	{
		Script.Reset();
		ScriptCursor = 0;
		ActiveSessionId.Invalidate();
	}
}

// ============================================================================
// Script construction
// ============================================================================

void FMockChatBackend::QueueTextAsDeltas(const FString& Text, const FGuid& MessageId)
{
	// Split on whitespace but keep it — chunking mid-word is what a real tokenizer
	// does and it is the case that breaks naive text accumulation.
	const float PerToken = 1.f / TokensPerSecond;

	int32 Start = 0;
	while (Start < Text.Len())
	{
		int32 End = Start + 1;
		while (End < Text.Len() && !FChar::IsWhitespace(Text[End]))
		{
			++End;
		}
		while (End < Text.Len() && FChar::IsWhitespace(Text[End]))
		{
			++End;
		}

		FScriptedStep Step;
		Step.Event.Type      = FChatStreamEvent::EType::TextDelta;
		Step.Event.MessageId = MessageId;
		Step.Event.Text      = Text.Mid(Start, End - Start);
		Step.DelaySeconds    = PerToken;
		Script.Add(MoveTemp(Step));

		Start = End;
	}
}

void FMockChatBackend::BuildScript(const FChatTurnRequest& Request)
{
	const FGuid MsgId = ActiveMessageId;
	const FString Prompt = Request.UserMessage.IsValid()
		? Request.UserMessage->GetPlainText(false).ToLower()
		: FString();

	auto Add = [this](FChatStreamEvent::EType Type, float Delay) -> FChatStreamEvent&
	{
		FScriptedStep Step;
		Step.Event.Type      = Type;
		Step.Event.MessageId = ActiveMessageId;
		Step.DelaySeconds    = Delay;
		return Script.Add_GetRef(MoveTemp(Step)).Event;
	};

	Add(FChatStreamEvent::EType::TurnStarted, 0.05f);

	// --- Keyword-driven scenarios, so every transcript state is reachable offline ---

	if (Prompt.Contains(TEXT("error")))
	{
		QueueTextAsDeltas(TEXT("Let me try that. "), MsgId);
		Add(FChatStreamEvent::EType::Error, 0.4f).Text =
			TEXT("Simulated provider error: 529 overloaded_error. This is the mock backend demonstrating the error card.");
		Add(FChatStreamEvent::EType::TurnFinished, 0.05f);
		return;
	}

	if (Prompt.Contains(TEXT("refus")))
	{
		FChatStreamEvent& Refusal = Add(FChatStreamEvent::EType::Refusal, 0.5f);
		Refusal.Text = TEXT("This request was declined by the provider's safety classifiers.");
		Refusal.RefusalCategory = TEXT("mock");
		Add(FChatStreamEvent::EType::TurnFinished, 0.05f);
		return;
	}

	if (Prompt.Contains(TEXT("think")))
	{
		const TCHAR* Thought =
			TEXT("The user wants to see a thinking block. I'll emit a few thinking deltas ")
			TEXT("first, then the visible answer, so the collapsed-by-default treatment can ")
			TEXT("be checked in both states.");
		for (const FString& Chunk : { FString(Thought).Left(60), FString(Thought).Mid(60, 60), FString(Thought).Mid(120) })
		{
			FScriptedStep Step;
			Step.Event.Type      = FChatStreamEvent::EType::ThinkingDelta;
			Step.Event.MessageId = MsgId;
			Step.Event.Text      = Chunk;
			Step.DelaySeconds    = 0.25f;
			Script.Add(MoveTemp(Step));
		}
	}

	if (Prompt.Contains(TEXT("tool")) || Prompt.Contains(TEXT("actor")) || Prompt.Contains(TEXT("level")))
	{
		QueueTextAsDeltas(TEXT("I'll check the current level first.\n\n"), MsgId);

		const FGuid CallId = FGuid::NewGuid();

		FChatStreamEvent& Started = Add(FChatStreamEvent::EType::ToolCallStarted, 0.3f);
		Started.ToolCallId = CallId;
		Started.ToolName   = TEXT("get_level_info");

		// Arguments arrive as partial JSON in the real thing, so exercise that path.
		for (const TCHAR* Fragment : { TEXT("{\"inc"), TEXT("lude_actor"), TEXT("s\": true}") })
		{
			FChatStreamEvent& Delta = Add(FChatStreamEvent::EType::ToolArgsDelta, 0.12f);
			Delta.ToolCallId = CallId;
			Delta.Text       = Fragment;
		}

		FChatStreamEvent& Result = Add(FChatStreamEvent::EType::ToolCallResult, 0.6f);
		Result.ToolCallId      = CallId;
		Result.ToolName        = TEXT("get_level_info");
		Result.DurationSeconds = 0.42;
		Result.ToolResultText  = FString::Printf(
			TEXT("Mock result — the real registry currently has %d tools available."),
			FMCPToolRegistry::Get().GetToolCount());

		QueueTextAsDeltas(
			TEXT("\nThe level looks fine. Nothing was modified — this is the mock backend, ")
			TEXT("so no editor state was touched.\n"), MsgId);

		FChatStreamEvent& Usage = Add(FChatStreamEvent::EType::UsageUpdate, 0.1f);
		Usage.Usage.InputTokens      = 1204;
		Usage.Usage.OutputTokens     = 96;
		Usage.Usage.CacheReadTokens  = 892;
		Usage.Usage.EstimatedCostUsd = 0.0043;

		Add(FChatStreamEvent::EType::TurnFinished, 0.05f);
		return;
	}

	if (Prompt.Contains(TEXT("long")))
	{
		// Stress case: enough text to force scrolling and to make the live-tail
		// height cap matter.
		FString Long;
		for (int32 i = 1; i <= 40; ++i)
		{
			Long += FString::Printf(
				TEXT("Line %d. This paragraph exists to push the transcript past one screen ")
				TEXT("so stick-to-bottom, the new-messages pill and the tail height cap can all ")
				TEXT("be verified.\n\n"), i);
		}
		QueueTextAsDeltas(Long, MsgId);
		Add(FChatStreamEvent::EType::TurnFinished, 0.05f);
		return;
	}

	// --- Default reply ---
	QueueTextAsDeltas(
		TEXT("This is the **mock backend**. It streams a scripted reply so the panel can be ")
		TEXT("built and tested without a network connection or an API key.\n\n")
		TEXT("Try one of these to see other states:\n\n")
		TEXT("- `tool` — a tool call with streaming arguments and a result card\n")
		TEXT("- `think` — a collapsible thinking block\n")
		TEXT("- `long` — enough output to exercise scrolling\n")
		TEXT("- `error` — the error card\n")
		TEXT("- `refuse` — the refusal card\n"), MsgId);

	FChatStreamEvent& Usage = Add(FChatStreamEvent::EType::UsageUpdate, 0.1f);
	Usage.Usage.InputTokens  = 42;
	Usage.Usage.OutputTokens = 84;

	Add(FChatStreamEvent::EType::TurnFinished, 0.05f);
}

// ============================================================================
// Playback
// ============================================================================

bool FMockChatBackend::TickScript(float DeltaTime)
{
	if (!Script.IsValidIndex(ScriptCursor))
	{
		return true;
	}

	if (CancelFlag.IsValid() && CancelFlag->Load(EMemoryOrder::Relaxed))
	{
		CancelTurn(ActiveSessionId);
		return true;
	}

	TimeUntilNextStep -= DeltaTime;

	// Emit every step whose time has come this frame, so a low frame rate doesn't
	// stretch the script out — same reason a real transport drains its whole queue.
	int32 Guard = 0;
	while (Script.IsValidIndex(ScriptCursor) && TimeUntilNextStep <= 0.f && Guard++ < 256)
	{
		const FScriptedStep& Step = Script[ScriptCursor];
		OnStreamEvent.Broadcast(Step.Event);
		++ScriptCursor;

		if (Script.IsValidIndex(ScriptCursor))
		{
			TimeUntilNextStep += Script[ScriptCursor].DelaySeconds;
		}
	}

	if (!Script.IsValidIndex(ScriptCursor))
	{
		Script.Reset();
		ScriptCursor = 0;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
