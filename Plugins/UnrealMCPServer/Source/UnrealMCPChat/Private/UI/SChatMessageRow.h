// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPChatTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FMCPChatController;

/**
 * Phase 2 — one message.
 *
 * Renders plain text; Phase 3 swaps the paragraph builder for the markdown
 * renderer without touching this widget's structure.
 *
 * Used in two places: as a virtualised row inside the transcript list, and as the
 * live tail during streaming. The only difference is bIsLiveTail, which suppresses
 * hover actions (there is nothing to copy or retry yet) and keeps the widget
 * rebuilding cheaply as content grows.
 */
class SChatMessageRow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatMessageRow)
		: _IsLiveTail(false)
	{}
		SLATE_ARGUMENT(FChatMessagePtr, Message)
		SLATE_ARGUMENT(TSharedPtr<FMCPChatController>, Controller)
		SLATE_ARGUMENT(bool, IsLiveTail)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Re-read the message and rebuild the block widgets. Called by the live tail
	 *  when the block STRUCTURE changes (a new block, a tool call appearing).
	 *  Pure text growth does not need this — those bind through attributes. */
	void RefreshContent();

private:
	TSharedRef<SWidget> BuildHeader();
	TSharedRef<SWidget> BuildTextBlock(int32 BlockIndex);
	TSharedRef<SWidget> BuildThinkingBlock(int32 BlockIndex);
	TSharedRef<SWidget> BuildToolCard(int32 BlockIndex);
	/** Inline approval choices, rendered inside the card where the call is. */
	TSharedRef<SWidget> BuildApprovalPrompt(int32 BlockIndex);
	/** One row standing in for a run of successful read-only calls. */
	TSharedRef<SWidget> BuildCollapsedToolSummary(const TArray<int32>& BlockIndices);
	TSharedRef<SWidget> BuildNoticeBlock(int32 BlockIndex, bool bIsError);
	TSharedRef<SWidget> BuildAttachmentBlock(int32 BlockIndex);
	/** Phase 6 — an @mention that rode along with this message. */
	TSharedRef<SWidget> BuildContextChip(int32 BlockIndex);
	/** Phase 7 — an ACP agent's running checklist. */
	TSharedRef<SWidget> BuildPlanBlock(int32 BlockIndex);
	TSharedRef<SWidget> BuildDivider(int32 BlockIndex);

	FText GetRoleLabel() const;
	FText GetTimestampText() const;
	FText GetUsageText() const;
	FSlateColor GetRuleColor() const;
	EVisibility GetHoverActionsVisibility() const;
	EVisibility GetUsageVisibility() const;
	EVisibility GetSiblingStepperVisibility() const;
	FText GetSiblingText() const;

	FReply OnCopyClicked();
	FReply OnRetryClicked();
	FReply OnPreviousSibling();
	FReply OnNextSibling();

	/** Text is bound through an attribute so a streamed delta repaints without
	 *  rebuilding any widget — the single most important perf decision in the
	 *  transcript (docs/02 §6). */
	FText GetBlockText(int32 BlockIndex) const;

	FChatMessagePtr Message;
	TSharedPtr<FMCPChatController> Controller;
	bool bIsLiveTail = false;

	TSharedPtr<SVerticalBox> BlockContainer;
	TArray<bool> ThinkingExpanded;
	TArray<bool> ToolExpanded;
	/** Expansion state for each collapsed read-only run, keyed by its first index. */
	TSet<int32>  ExpandedToolRuns;

	/** Block count at the last rebuild — lets the live tail detect structural
	 *  change without diffing content. */
	int32 LastBlockCount = 0;
};
