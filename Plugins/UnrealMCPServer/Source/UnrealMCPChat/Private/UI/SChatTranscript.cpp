// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SChatTranscript.h"
#include "UI/SChatMessageRow.h"
#include "MCPChatController.h"
#include "MCPChatStyle.h"
#include "UnrealMCPChatModule.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SChatTranscript"

void SChatTranscript::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;

	if (Controller.IsValid())
	{
		TranscriptChangedHandle = Controller->OnTranscriptChanged.AddSP(this, &SChatTranscript::RebuildMessageList);
		StreamingUpdatedHandle  = Controller->OnStreamingMessageUpdated.AddSP(this, &SChatTranscript::OnStreamingUpdated);
		TurnStateHandle         = Controller->OnTurnStateChanged.AddSP(this, &SChatTranscript::OnTurnStateChanged);
	}

	// A theme reload invalidates every brush this subtree captured.
	StyleReloadHandle = FMCPChatStyle::OnStyleReloaded().AddSP(this, &SChatTranscript::RebuildMessageList);

	ChildSlot
	[
		SNew(SOverlay)

		+ SOverlay::Slot()
		[
			SNew(SVerticalBox)

			// ---- Committed messages: virtualised, stable heights ----
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SAssignNew(MessageList, SListView<FChatMessagePtr>)
				.ListItemsSource(&ListItems)
				.OnGenerateRow(this, &SChatTranscript::OnGenerateRow)
				.OnListViewScrolled(this, &SChatTranscript::OnListScrolled)
				.SelectionMode(ESelectionMode::None)
				.ScrollbarVisibility(EVisibility::Visible)
			]

			// ---- Live tail: the single in-flight message ----
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(LiveTailContainer, SBox)
				.Visibility(this, &SChatTranscript::GetLiveTailVisibility)
				.MaxDesiredHeight(this, &SChatTranscript::GetLiveTailMaxHeight)
				[
					SAssignNew(LiveTailScrollBox, SScrollBox)
				]
			]
		]

		// ---- Empty state ----
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			.Visibility(this, &SChatTranscript::GetEmptyStateVisibility)

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.H2")))
				.Text(LOCTEXT("EmptyTitle", "Start a conversation"))
			]

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			.Padding(0.f, FMCPChatStyle::Space(TEXT("sm")), 0.f, 0.f)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.Text(LOCTEXT("EmptyBody",
					"The mock backend is active — try \"tool\", \"think\", \"long\", \"error\" or \"refuse\"."))
				.AutoWrapText(true)
				.Justification(ETextJustify::Center)
			]
		]

		// ---- "New messages" pill ----
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(0.f, 0.f, 0.f, FMCPChatStyle::Space(TEXT("md")))
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.Visibility(this, &SChatTranscript::GetNewMessagesPillVisibility)
			.OnClicked(this, &SChatTranscript::OnJumpToBottom)
			.ToolTipText(LOCTEXT("JumpToBottomTip", "Scroll to the newest message"))
			[
				SNew(SBorder)
				.BorderImage(FMCPChatStyle::Brush(TEXT("chip")))
				.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("xs")))
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.Text(LOCTEXT("NewMessages", "↓  New messages"))
				]
			]
		]
	];

	RebuildMessageList();
}

SChatTranscript::~SChatTranscript()
{
	if (Controller.IsValid())
	{
		Controller->OnTranscriptChanged.Remove(TranscriptChangedHandle);
		Controller->OnStreamingMessageUpdated.Remove(StreamingUpdatedHandle);
		Controller->OnTurnStateChanged.Remove(TurnStateHandle);
	}
	FMCPChatStyle::OnStyleReloaded().Remove(StyleReloadHandle);
}

// ============================================================================
// List
// ============================================================================

TSharedRef<ITableRow> SChatTranscript::OnGenerateRow(FChatMessagePtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<FChatMessagePtr>, OwnerTable)
		.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"))
		.Padding(0.f)
		[
			SNew(SChatMessageRow)
			.Message(Item)
			.Controller(Controller)
			.IsLiveTail(false)
		];
}

void SChatTranscript::RebuildMessageList()
{
	ListItems.Reset();

	if (Controller.IsValid())
	{
		if (const FChatSessionPtr Session = Controller->GetActiveSession())
		{
			ListItems = Session->BuildActivePath();

			// Cap rendered history. Phase 3 adds a "Load earlier messages" header;
			// until then the cap simply protects frame time on a huge transcript.
			if (ListItems.Num() > MaxRenderedMessages)
			{
				ListItems.RemoveAt(0, ListItems.Num() - MaxRenderedMessages, EAllowShrinking::No);
			}
		}
	}

	if (MessageList.IsValid())
	{
		MessageList->RequestListRefresh();

		if (bStickToBottom && ListItems.Num() > 0)
		{
			bSuppressScrollHandling = true;
			MessageList->RequestScrollIntoView(ListItems.Last());
		}
	}
}

void SChatTranscript::OnListScrolled(double /*ScrollOffset*/)
{
	if (bSuppressScrollHandling)
	{
		bSuppressScrollHandling = false;
		return;
	}
	if (!MessageList.IsValid()) { return; }

	// Re-arm when the user returns to the bottom, disarm when they leave it. No
	// separate "follow" toggle to get out of sync with what they actually did.
	const float Remaining = MessageList->GetScrollDistanceRemaining().Y;
	const bool bAtBottom = Remaining <= KINDA_SMALL_NUMBER;

	bStickToBottom = bAtBottom;
	if (bAtBottom)
	{
		bHasUnseenContent = false;
	}
}

FReply SChatTranscript::OnJumpToBottom()
{
	bStickToBottom = true;
	bHasUnseenContent = false;

	if (MessageList.IsValid() && ListItems.Num() > 0)
	{
		bSuppressScrollHandling = true;
		MessageList->RequestScrollIntoView(ListItems.Last());
	}
	return FReply::Handled();
}

// ============================================================================
// Live tail
// ============================================================================

void SChatTranscript::OnStreamingUpdated()
{
	if (!Controller.IsValid() || !LiveTailScrollBox.IsValid()) { return; }

	const FChatMessagePtr Streaming = Controller->GetStreamingMessage();

	if (!Streaming.IsValid())
	{
		LiveTailScrollBox->ClearChildren();
		LiveTailRow.Reset();
		LiveTailMessage.Reset();
		LiveTailBlockCount = 0;
		return;
	}

	// Rebuild only when the message identity or the BLOCK STRUCTURE changes. Text
	// growth inside an existing block flows through the row's text attributes and
	// costs a repaint, not a rebuild — this is the whole point of the split.
	const bool bNewMessage = (LiveTailMessage != Streaming);
	const bool bStructureChanged = (Streaming->Blocks.Num() != LiveTailBlockCount);

	if (bNewMessage)
	{
		LiveTailMessage = Streaming;
		LiveTailScrollBox->ClearChildren();
		LiveTailScrollBox->AddSlot()
		[
			SAssignNew(LiveTailRow, SChatMessageRow)
			.Message(Streaming)
			.Controller(Controller)
			.IsLiveTail(true)
		];
		LiveTailBlockCount = Streaming->Blocks.Num();
	}
	else if (bStructureChanged && LiveTailRow.IsValid())
	{
		LiveTailRow->RefreshContent();
		LiveTailBlockCount = Streaming->Blocks.Num();
	}

	// Keep the newest text visible inside the tail's own scroll box.
	if (LiveTailScrollBox.IsValid())
	{
		LiveTailScrollBox->ScrollToEnd();
	}
}

void SChatTranscript::OnTurnStateChanged(bool bInFlight)
{
	if (bInFlight)
	{
		// A new turn is a deliberate act, so follow it even if the user had scrolled
		// up to read history earlier.
		bStickToBottom = true;
		bHasUnseenContent = false;
	}
	else
	{
		// The committed message arrives via OnTranscriptChanged; clear the tail here
		// so it cannot briefly render twice.
		if (LiveTailScrollBox.IsValid())
		{
			LiveTailScrollBox->ClearChildren();
		}
		LiveTailRow.Reset();
		LiveTailMessage.Reset();
		LiveTailBlockCount = 0;
	}
}

void SChatTranscript::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bStickToBottom && MessageList.IsValid() && ListItems.Num() > 0)
	{
		if (MessageList->GetScrollDistanceRemaining().Y > KINDA_SMALL_NUMBER)
		{
			bSuppressScrollHandling = true;
			MessageList->ScrollToBottom();
		}
	}
	else if (!bStickToBottom && Controller.IsValid() && Controller->IsTurnInFlight())
	{
		bHasUnseenContent = true;
	}
}

// ============================================================================
// Visibility
// ============================================================================

EVisibility SChatTranscript::GetLiveTailVisibility() const
{
	return (Controller.IsValid() && Controller->GetStreamingMessage().IsValid())
		? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SChatTranscript::GetEmptyStateVisibility() const
{
	const bool bStreaming = Controller.IsValid() && Controller->GetStreamingMessage().IsValid();
	return (ListItems.Num() == 0 && !bStreaming) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SChatTranscript::GetNewMessagesPillVisibility() const
{
	return (!bStickToBottom && bHasUnseenContent) ? EVisibility::Visible : EVisibility::Collapsed;
}

FOptionalSize SChatTranscript::GetLiveTailMaxHeight() const
{
	// Cap the tail so a very long reply cannot swallow the whole panel; past the
	// cap it scrolls internally and history stays reachable above it.
	const FVector2D PanelSize = GetCachedGeometry().GetLocalSize();
	const float Cap = PanelSize.Y > 0.f ? PanelSize.Y * 0.6f : 400.f;
	return FOptionalSize(Cap);
}

#undef LOCTEXT_NAMESPACE
