// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SMCPChatPanel.h"
#include "Core/MCPChatCommands.h"
#include "Core/MCPChatDrafts.h"
#include "UI/SChatComposer.h"
#include "UI/SChatEmptyState.h"
#include "UI/SChatSettingsPanel.h"
#include "UI/SChatHistoryRail.h"
#include "UI/SChatTranscript.h"
#include "Agents/AcpAgentBackend.h"
#include "Backends/AnthropicBackend.h"
#include "Backends/MockChatBackend.h"
#include "Backends/OpenAICompatBackend.h"
#include "MCPChatAgentCatalog.h"
#include "MCPChatController.h"
#include "MCPChatSettings.h"
#include "MCPChatModelCatalog.h"
#include "MCPChatStore.h"
#include "MCPChatToolBridge.h"
#include "MCPChatStyle.h"
#include "UnrealMCPChatModule.h"

#include "MCPHttpServer.h"
#include "MCPToolRegistry.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMCPChatPanel"

void SMCPChatPanel::Construct(const FArguments& InArgs)
{
	Controller = MakeShared<FMCPChatController>();

	// The mock stays registered as an offline demo and as the test harness — it is
	// how a user checks the panel works before spending anything.
	Controller->RegisterBackend(MakeShared<FMockChatBackend>());
	Controller->RegisterBackend(MakeShared<FAnthropicBackend>());
	FMCPChatModelCatalog::Get().Load();

	// Phase 8 — one backend instance per OpenAI-compatible provider. The wire format
	// is shared; the base URL, the key and the model list are data. That is what
	// makes adding Groq or a self-hosted vLLM a JSON edit instead of a class.
	for (const FMCPChatProviderInfo& Provider : FMCPChatModelCatalog::Get().GetProviders())
	{
		if (Provider.WireFormat != TEXT("openai")) { continue; }
		Controller->RegisterBackend(MakeShared<FOpenAICompatBackend>(Provider.ProviderId));
	}

	// Phase 7 — local agents. Every preset is registered whether or not its binary
	// is installed: the picker shows a hollow dot and the install hint, which is far
	// more useful than an agent that silently is not in the list.
	FMCPChatAgentCatalog::Get().Load();
	for (const FMCPChatAgentInfo& AgentInfo : FMCPChatAgentCatalog::Get().GetAgents())
	{
		if (!AgentInfo.bEnabled) { continue; }   // the "custom agent" template row
		Controller->RegisterBackend(MakeShared<FAcpAgentBackend>(AgentInfo));
	}

	// Resume the most recent conversation rather than always starting empty —
	// reopening the tab mid-task and losing your place is a small, constant tax.
	const TArray<FChatSessionSummaryPtr>& Index = FMCPChatStore::Get().GetIndex();
	FChatSessionSummaryPtr MostRecent;
	for (const FChatSessionSummaryPtr& Row : Index)
	{
		if (Row.IsValid() && (!MostRecent.IsValid() || Row->UpdatedAt > MostRecent->UpdatedAt))
		{
			MostRecent = Row;
		}
	}
	if (MostRecent.IsValid())
	{
		Controller->OpenSession(MostRecent->Id);
	}
	if (!Controller->GetActiveSession().IsValid())
	{
		Controller->NewSession();
	}

	StyleReloadHandle = FMCPChatStyle::OnStyleReloaded().AddSP(this, &SMCPChatPanel::RebuildLayout);

	// A session switch in the rail has to reach the composer: its draft, its
	// attachments and its model badge are all per-conversation.
	TranscriptChangedHandle = Controller->OnTranscriptChanged.AddLambda([this]()
	{
		if (Composer.IsValid()) { Composer->RefreshForSession(); }
	});

	RebuildLayout();
}

SMCPChatPanel::~SMCPChatPanel()
{
	FMCPChatStyle::OnStyleReloaded().Remove(StyleReloadHandle);
	if (Controller.IsValid())
	{
		Controller->OnTranscriptChanged.Remove(TranscriptChangedHandle);
	}
	FMCPChatDrafts::Get().Flush();

	// The tab is closing; don't leave the current transcript only in memory.
	if (Controller.IsValid())
	{
		if (const FChatSessionPtr Session = Controller->GetActiveSession())
		{
			FMCPChatStore::Get().SaveNow(Session->Id);
		}
	}
}

void SMCPChatPanel::RebuildLayout()
{
	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	const float RailWidth = Settings ? static_cast<float>(Settings->HistoryRailWidth) : 240.f;

	SAssignNew(RailContainer, SBox)
		.WidthOverride(RailWidth)
		.Visibility(this, &SMCPChatPanel::GetHistoryRailVisibility)
		[
			SNew(SBorder)
			.BorderImage(FMCPChatStyle::Brush(TEXT("railBg")))
			.Padding(0.f)
			[
				SAssignNew(HistoryRail, SChatHistoryRail)
				.Controller(Controller)
			]
		];

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("transcriptBg")))
		.Padding(0.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildHeader()
			]

			// A 2 px indeterminate line, not a spinner: it says "working" without
			// taking a row of transcript, and it sits where the eye already is after
			// pressing Enter.
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(2.f)
				.Visibility_Lambda([this]()
				{
					return (Controller.IsValid() && Controller->IsTurnInFlight())
						? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
				[
					SNew(SProgressBar)
					.Percent(TOptional<float>())   // indeterminate
				]
			]

			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth()
				[
					RailContainer.ToSharedRef()
				]

				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SBorder)
					.BorderImage(FMCPChatStyle::Brush(TEXT("transcriptBg")))
					.Padding(this, &SMCPChatPanel::GetTranscriptPadding)
					.HAlign(HAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(this, &SMCPChatPanel::GetTranscriptWidth)
						.HAlign(HAlign_Fill)
						[
							SNew(SOverlay)

							// Both are built once and swapped by visibility rather than
							// rebuilt on every message — an empty conversation becomes a
							// non-empty one on the first keystroke of the first reply, and
							// a layout rebuild at that moment would be visible.
							+ SOverlay::Slot()
							[
								SNew(SBox)
								.Visibility(this, &SMCPChatPanel::GetTranscriptVisibility)
								[
									SAssignNew(Transcript, SChatTranscript)
									.Controller(Controller)
								]
							]

							+ SOverlay::Slot()
							[
								SNew(SBox)
								.Visibility(this, &SMCPChatPanel::GetEmptyStateVisibility)
								[
									SAssignNew(EmptyState, SChatEmptyState)
									.Controller(Controller)
									.OnSuggestionChosen(FOnSuggestionChosen::CreateSP(
										this, &SMCPChatPanel::HandleSuggestionChosen))
									.OnOpenSettings(FOnOpenSettingsRequested::CreateSP(
										this, &SMCPChatPanel::OpenSettingsTab))
								]
							]
						]
					]
				]
			]

			// Offline banner. Local agents and Ollama keep working without the MCP
			// server, so this is a warning about tools, not about the panel.
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FMCPChatStyle::Brush(TEXT("toolCardWarn")))
				.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("xs")))
				.Visibility_Lambda([]()
				{
					return FMCPHttpServer::Get().IsRunning() ? EVisibility::Collapsed : EVisibility::Visible;
				})
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.ColorAndOpacity(FMCPChatStyle::Color(TEXT("readoutWarn")))
					.Text(LOCTEXT("OfflineBanner",
						"The MCP server is not running. Models can still answer, but nothing can change "
						"the project until it is back."))
					.AutoWrapText(true)
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(Composer, SChatComposer)
				.Controller(Controller)
				.NarrowMode_Lambda([this]() { return CurrentBreakpoint == EMCPChatBreakpoint::Narrow; })
				.OnCommandResult(FOnComposerCommandResult::CreateSP(this, &SMCPChatPanel::HandleCommandResult))
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildStatusBar()
			]
		]
	];
}

// ============================================================================
// Header
// ============================================================================

TSharedRef<SWidget> SMCPChatPanel::BuildHeader()
{
	return SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("headerBg")))
		.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("sm")))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			.Padding(0.f, 0.f, FMCPChatStyle::Space(TEXT("sm")), 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("ToggleHistory", "Show or hide conversation history"))
				.OnClicked(this, &SMCPChatPanel::OnToggleHistoryRail)
				.ContentPadding(FMCPChatStyle::Padding(TEXT("xs")))
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Menu"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]

			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Bold")))
				.Text(this, &SMCPChatPanel::GetHeaderTitle)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]

			// The model used to be named here. It now lives in the composer, next to
			// the message it applies to and next to the cost of sending it — which is
			// where the decision is actually made. Naming it twice would only invite
			// the two to disagree.

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			.Padding(FMCPChatStyle::Space(TEXT("sm")), 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("OpenSettingsTip",
					"Chat settings — API keys, local agents, permissions"))
				.ContentPadding(FMCPChatStyle::Padding(TEXT("xs")))
				.OnClicked_Lambda([this]() { OpenSettingsTab(); return FReply::Handled(); })
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Settings"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
				.ToolTipText(LOCTEXT("PinnedTip",
					"Context pinned to this conversation. It is re-read and re-sent on every message."))
				.Visibility_Lambda([this]()
				{
					const FChatSessionPtr S = Controller.IsValid() ? Controller->GetActiveSession() : nullptr;
					return (S.IsValid() && S->PinnedContext.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text_Lambda([this]() -> FText
				{
					const FChatSessionPtr S = Controller.IsValid() ? Controller->GetActiveSession() : nullptr;
					const int32 Count = S.IsValid() ? S->PinnedContext.Num() : 0;
					return FText::Format(LOCTEXT("PinnedCount", "◎ {0} pinned"), FText::AsNumber(Count));
				})
			]
		];
}

// ============================================================================
// States (Phase 8)
// ============================================================================

bool SMCPChatPanel::IsSessionEmpty() const
{
	if (!Controller.IsValid()) { return true; }

	const FChatSessionPtr Session = Controller->GetActiveSession();
	if (!Session.IsValid()) { return true; }

	// A streaming reply counts as content the moment it starts, so the empty state
	// disappears on the first token rather than sitting behind the first sentence.
	return Session->Messages.Num() == 0 && !Controller->GetStreamingMessage().IsValid();
}

EVisibility SMCPChatPanel::GetTranscriptVisibility() const
{
	return IsSessionEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SMCPChatPanel::GetEmptyStateVisibility() const
{
	return IsSessionEmpty() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

void SMCPChatPanel::HandleSuggestionChosen(const FString& Prompt)
{
	// Put it in the composer rather than sending it. A suggestion is a starting
	// point, and sending on a single click removes the chance to adjust it — or to
	// notice it was clicked by accident.
	if (Composer.IsValid())
	{
		Composer->SetComposerText(Prompt);
		Composer->FocusTextBox();
	}
}

void SMCPChatPanel::OpenSettingsTab()
{
	FUnrealMCPChatModule::OpenSettingsTab();
}

// ============================================================================
// Local command results
// ============================================================================

void SMCPChatPanel::HandleCommandResult(const FChatCommandResult& Result)
{
	// A `/` command produced no transcript entry by design — it never reached a
	// model — so its outcome is reported as an editor notification. Writing it into
	// the transcript would put text in the conversation that was never sent, which
	// is exactly the confusion an exported transcript must not contain.
	if (!Result.Message.IsEmpty())
	{
		FNotificationInfo Info(Result.Message);
		Info.ExpireDuration = Result.bIsError ? 6.f : 3.5f;
		Info.bFireAndForget = true;

		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(Result.bIsError ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
		}
	}

	switch (Result.UiAction)
	{
	case FChatCommandResult::EUiAction::OpenModelPicker:
		if (Composer.IsValid()) { Composer->OpenModelPicker(); }
		break;

	case FChatCommandResult::EUiAction::OpenSettings:
		OpenSettingsTab();
		break;

	case FChatCommandResult::EUiAction::OpenToolBrowser:
		// No dedicated catalogue browser yet. The AI tab is where tool exposure is
		// decided and shows the token cost of each option, which is the question
		// `/tools` is usually standing in for.
		OpenSettingsTab();
		break;

	case FChatCommandResult::EUiAction::RevealExport:
		if (!Result.Payload.IsEmpty())
		{
			FPlatformProcess::ExploreFolder(*Result.Payload);
		}
		break;

	case FChatCommandResult::EUiAction::ShowTokenBreakdown:
		// The composer's readout already carries the breakdown in its tooltip; point
		// at it rather than building a second surface that says the same thing.
		if (Composer.IsValid()) { Composer->FocusTextBox(); }
		break;

	default:
		break;
	}
}

// ============================================================================
// Status bar
// ============================================================================

TSharedRef<SWidget> SMCPChatPanel::BuildStatusBar()
{
	return SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("railBg")))
		.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("xs")))
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.Text(this, &SMCPChatPanel::GetStatusText)
		];
}

FText SMCPChatPanel::GetStatusText() const
{
	const FMCPHttpServer& Server = FMCPHttpServer::Get();
	if (!Server.IsRunning())
	{
		return LOCTEXT("ServerOffline", "MCP server offline — local agents will not see editor tools");
	}

	// The approval mode is shown permanently: "Never ask" is exactly the setting a
	// user forgets they turned on, and the consequence is silent project changes.
	// The EFFECTIVE mode, not the global one, so a `/scope read` conversation says
	// read-only even when the setting is looser.
	const FChatSessionPtr Session = Controller.IsValid() ? Controller->GetActiveSession() : nullptr;
	const EMCPChatApprovalMode Mode = Session.IsValid()
		? FMCPChatToolBridge::Get().GetEffectiveMode(Session->Id)
		: (UMCPChatSettings::Get() ? UMCPChatSettings::Get()->ApprovalMode : EMCPChatApprovalMode::AskDestructive);

	FText ModeText = LOCTEXT("ModeAskDestructive", "asks before destructive tools");
	switch (Mode)
	{
	case EMCPChatApprovalMode::AskWrites: ModeText = LOCTEXT("ModeAskWrites", "asks before every change"); break;
	case EMCPChatApprovalMode::AllowAll:  ModeText = LOCTEXT("ModeAllowAll",  "⚠ never asks"); break;
	case EMCPChatApprovalMode::ReadOnly:  ModeText = LOCTEXT("ModeReadOnly",  "read-only"); break;
	default: break;
	}

	// A LOCAL AGENT runs its own tool loop against the MCP endpoint we gave it. Our
	// approval mode governs what it ASKS us about, not what it does on its own — so
	// showing the same string for both cases would be a quiet lie about who is in
	// control. Phase 7's seam, stated where it can be seen (docs/05 §11).
	if (Session.IsValid() && Controller.IsValid())
	{
		if (const FChatBackendPtr Backend = Controller->FindBackend(Session->BackendId))
		{
			if (Backend->GetKind() == EChatBackendKind::Agent)
			{
				return FText::Format(
					LOCTEXT("ServerOnlineAgent",
						"Connected  ·  localhost:{0}  ·  {1} tools  ·  {2} runs its own tools — server scope applies"),
					FText::AsNumber(Server.GetPort(), &FNumberFormattingOptions::DefaultNoGrouping()),
					FText::AsNumber(FMCPToolRegistry::Get().GetToolCount()),
					Backend->GetDisplayName());
			}
		}
	}

	return FText::Format(
		LOCTEXT("ServerOnline", "Connected  ·  localhost:{0}  ·  {1} tools  ·  {2}"),
		FText::AsNumber(Server.GetPort(), &FNumberFormattingOptions::DefaultNoGrouping()),
		FText::AsNumber(FMCPToolRegistry::Get().GetToolCount()),
		ModeText);
}

FText SMCPChatPanel::GetHeaderTitle() const
{
	if (Controller.IsValid())
	{
		if (const FChatSessionPtr Session = Controller->GetActiveSession())
		{
			if (!Session->Title.IsEmpty())
			{
				return FText::FromString(Session->Title);
			}
		}
	}
	return LOCTEXT("NewConversation", "New conversation");
}

// ============================================================================
// Responsive layout
// ============================================================================

void SMCPChatPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	LastPanelWidth = AllottedGeometry.GetLocalSize().X;
	UpdateBreakpoint(LastPanelWidth, InCurrentTime);
}

void SMCPChatPanel::UpdateBreakpoint(float AvailableWidth, double CurrentTime)
{
	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	const float WideAt   = Settings ? static_cast<float>(Settings->WideBreakpoint)   : 900.f;
	const float NarrowAt = Settings ? static_cast<float>(Settings->NarrowBreakpoint) : 480.f;

	EMCPChatBreakpoint Target = EMCPChatBreakpoint::Compact;
	if (AvailableWidth >= WideAt)       { Target = EMCPChatBreakpoint::Wide; }
	else if (AvailableWidth < NarrowAt) { Target = EMCPChatBreakpoint::Narrow; }

	if (Target == CurrentBreakpoint)
	{
		PendingBreakpoint = Target;
		return;
	}

	// Hysteresis: dragging a splitter across a boundary would otherwise flip the
	// layout every frame.
	if (Target != PendingBreakpoint)
	{
		PendingBreakpoint = Target;
		PendingSinceSeconds = CurrentTime;
		return;
	}

	if (CurrentTime - PendingSinceSeconds >= BreakpointHysteresisSeconds)
	{
		CurrentBreakpoint = Target;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

FReply SMCPChatPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Esc: cancel a running turn, else hand back to the editor. A turn paused on an
	// approval is cancelled the same way — the safe outcome is "nothing ran".
	if (InKeyEvent.GetKey() == EKeys::Escape && Controller.IsValid() && Controller->IsTurnInFlight())
	{
		Controller->CancelTurn();
		return FReply::Handled();
	}

	if (InKeyEvent.GetKey() == EKeys::N && InKeyEvent.IsControlDown() && Controller.IsValid())
	{
		Controller->NewSession();
		return FReply::Handled();
	}

	if (InKeyEvent.GetKey() == EKeys::R && InKeyEvent.IsControlDown() && Controller.IsValid())
	{
		Controller->RetryLastTurn();
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

EVisibility SMCPChatPanel::GetHistoryRailVisibility() const
{
	if (bUserCollapsedRail)
	{
		return EVisibility::Collapsed;
	}
	return (CurrentBreakpoint == EMCPChatBreakpoint::Wide) ? EVisibility::Visible : EVisibility::Collapsed;
}

FMargin SMCPChatPanel::GetTranscriptPadding() const
{
	switch (CurrentBreakpoint)
	{
	case EMCPChatBreakpoint::Wide:    return FMCPChatStyle::Padding(TEXT("lg"));
	case EMCPChatBreakpoint::Compact: return FMCPChatStyle::Padding(TEXT("md"));
	case EMCPChatBreakpoint::Narrow:
	default:                          return FMCPChatStyle::Padding(TEXT("sm"));
	}
}

FOptionalSize SMCPChatPanel::GetTranscriptWidth() const
{
	// Measured in Tick. Before the first tick there is nothing to divide up, so
	// leave the box unconstrained rather than guessing a width and flashing.
	if (LastPanelWidth <= 0.f)
	{
		return FOptionalSize();
	}

	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	const float RailWidth = (GetHistoryRailVisibility() == EVisibility::Visible)
		? (Settings ? static_cast<float>(Settings->HistoryRailWidth) : 240.f)
		: 0.f;

	// Both sides of the transcript's own padding.
	const FMargin Padding = GetTranscriptPadding();
	const float Available = LastPanelWidth - RailWidth - Padding.Left - Padding.Right;
	if (Available <= 0.f)
	{
		return FOptionalSize();
	}

	// Fill the space, but stop at the readable-line-length limit. Below that limit
	// the transcript simply takes what it has — a narrow panel should not leave a
	// margin it cannot afford.
	const float Preferred = Settings ? static_cast<float>(Settings->TranscriptMaxContentWidth) : 820.f;
	return FOptionalSize(FMath::Min(Available, Preferred));
}

FReply SMCPChatPanel::OnToggleHistoryRail()
{
	bUserCollapsedRail = !bUserCollapsedRail;
	Invalidate(EInvalidateWidgetReason::Layout);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
