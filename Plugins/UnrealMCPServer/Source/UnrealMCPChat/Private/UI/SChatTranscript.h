// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPChatTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FMCPChatController;
class SChatMessageRow;
class SScrollBox;

/**
 * Phase 2 — the message list.
 *
 * THE DESIGN THAT MATTERS (docs/02_ARCHITECTURE.md §6):
 *
 *   SVerticalBox
 *   ├─ [Fill] SListView<FChatMessagePtr>   completed messages ONLY — heights are
 *   │                                      stable, so they cache and never re-measure
 *   └─ [Auto] live tail                    the single in-flight message, NOT virtualised,
 *                                          height-capped, with its own scroll box
 *
 * A naive implementation puts the streaming message in the list. SListView
 * re-measures a row whenever its content changes, so at 45 tokens/sec that is
 * ~45 layout passes per second over the visible set. Splitting it out means the
 * list re-measures exactly once per turn, at the moment the message is committed.
 *
 * Second-order benefit: the in-flight reply is pinned above the composer and does
 * not slide around while it grows, which is materially easier to read.
 */
class SChatTranscript : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatTranscript) {}
		SLATE_ARGUMENT(TSharedPtr<FMCPChatController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SChatTranscript() override;

	//~ SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End SWidget

private:
	TSharedRef<ITableRow> OnGenerateRow(FChatMessagePtr Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnListScrolled(double ScrollOffset);

	/** Rebuild the list source from the session's active branch. */
	void RebuildMessageList();

	/** The in-flight message changed. Rebuilds the tail widget only when the block
	 *  STRUCTURE changed; pure text growth repaints through attributes. */
	void OnStreamingUpdated();

	void OnTurnStateChanged(bool bInFlight);

	EVisibility GetLiveTailVisibility() const;
	EVisibility GetEmptyStateVisibility() const;
	EVisibility GetNewMessagesPillVisibility() const;
	FOptionalSize GetLiveTailMaxHeight() const;
	FReply OnJumpToBottom();

	TSharedPtr<FMCPChatController> Controller;

	TArray<FChatMessagePtr>              ListItems;
	TSharedPtr<SListView<FChatMessagePtr>> MessageList;

	TSharedPtr<SBox>             LiveTailContainer;
	TSharedPtr<SScrollBox>       LiveTailScrollBox;
	TSharedPtr<SChatMessageRow>  LiveTailRow;
	FChatMessagePtr              LiveTailMessage;
	int32                        LiveTailBlockCount = 0;

	/** Auto-scroll unless the user has deliberately scrolled up. Re-arms itself
	 *  when they scroll back to the bottom — no explicit "resume" affordance needed. */
	bool bStickToBottom = true;
	bool bHasUnseenContent = false;

	/** Set when we scroll programmatically, so our own scroll doesn't look like a
	 *  user scroll and disarm stick-to-bottom. */
	bool bSuppressScrollHandling = false;

	FDelegateHandle TranscriptChangedHandle;
	FDelegateHandle StreamingUpdatedHandle;
	FDelegateHandle TurnStateHandle;
	FDelegateHandle StyleReloadHandle;

	/** Cap on rendered history; older messages page in on demand (Phase 3+). */
	static constexpr int32 MaxRenderedMessages = 400;
};
