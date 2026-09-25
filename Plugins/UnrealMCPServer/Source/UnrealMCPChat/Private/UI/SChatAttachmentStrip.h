// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPChatAttachments.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SWrapBox;

DECLARE_DELEGATE_OneParam(FOnAttachmentRemoved, const FGuid& /*AttachmentId*/);
DECLARE_DELEGATE_OneParam(FOnAttachmentPinToggled, const FGuid& /*AttachmentId*/);

/**
 * Phase 6 — the chips above the text box.
 *
 * Auto-hides when empty rather than reserving a blank row: the composer is the
 * densest part of the panel and an always-present empty strip costs a line of
 * transcript on every screen for the majority of messages that have no
 * attachments.
 */
class SChatAttachmentStrip : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatAttachmentStrip) {}
		/** Not copied — the composer owns the array and this reads it live, so a
		 *  drag-drop during a rebuild cannot show a stale strip. */
		SLATE_ARGUMENT(TArray<FChatAttachmentPtr>*, Attachments)
		SLATE_EVENT(FOnAttachmentRemoved, OnRemoved)
		SLATE_EVENT(FOnAttachmentPinToggled, OnPinToggled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Called by the composer whenever the array changes. */
	void Refresh();

private:
	TSharedRef<SWidget> BuildChip(const FChatAttachmentPtr& Attachment);

	/** A glyph rather than an icon brush: the set of things that can be attached is
	 *  open-ended and shipping an icon per MIME type is not worth the resource. */
	static FText GlyphFor(const FChatAttachment& Attachment);

	EVisibility GetStripVisibility() const;

	TArray<FChatAttachmentPtr>* Attachments = nullptr;
	TSharedPtr<SWrapBox>        Box;

	FOnAttachmentRemoved    OnRemoved;
	FOnAttachmentPinToggled OnPinToggled;
};
