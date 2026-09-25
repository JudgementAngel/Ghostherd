// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/MCPChatCommands.h"
#include "Core/MCPChatTokenEstimator.h"
#include "MCPChatAttachments.h"
#include "UI/SChatTriggerPopup.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FMCPChatController;
class SChatAttachmentStrip;
class SChatModelPicker;
class SMenuAnchor;
class SMultiLineEditableTextBox;

DECLARE_DELEGATE_OneParam(FOnComposerCommandResult, const FChatCommandResult&);

/**
 * Phase 6 — the composer (docs/03_UIUX_SPEC.md §5).
 *
 * The part of the panel people touch every minute, so it carries most of the
 * interaction weight: attachments, three trigger palettes, the model picker, the
 * cost readout, draft persistence and history recall.
 *
 * ── One constraint worth stating up front ──────────────────────────────────
 * Trigger detection is anchored at the END of the text, not at the caret.
 * SMultiLineEditableTextBox does not expose a caret position we can rely on
 * across engine versions, and guessing wrong would put a chip in the wrong place
 * — a silent corruption of the user's message, which is far worse than the
 * limitation. Typing `@` in the middle of an already-written sentence therefore
 * inserts a literal `@` and opens nothing. Every insertion path uses the same
 * end-anchored rule, so what you see is always what gets replaced.
 */
class SChatComposer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatComposer)
		: _NarrowMode(false)
	{}
		SLATE_ARGUMENT(TSharedPtr<FMCPChatController>, Controller)
		/** Narrow breakpoint: icon-only toolbar, badge-only model picker. */
		SLATE_ATTRIBUTE(bool, NarrowMode)
		/** `/` command outcomes the panel has to act on or display. */
		SLATE_EVENT(FOnComposerCommandResult, OnCommandResult)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SChatComposer() override;

	//~ SWidget — drag and drop from the Content Browser, the World Outliner and the OS
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	//~ End SWidget

	/** The active session changed: swap the draft, drop the attachments. */
	void RefreshForSession();

	/** Put the caret back in the text box. */
	void FocusTextBox();

	/** Open the model dropdown — `/model` with no argument. */
	void OpenModelPicker();

	/** Drop the current draft into the box, replacing whatever is there. Used by
	 *  `#workflow` template expansion. */
	void SetComposerText(const FString& Text);

private:
	// ---- Layout ----
	TSharedRef<SWidget> BuildTextRow();
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildTriggerButton(const FString& Glyph, EChatTriggerMode Mode, const FText& Tooltip);
	TSharedRef<SWidget> BuildAttachButtons();
	TSharedRef<SWidget> BuildEffortPicker();
	TSharedRef<SWidget> BuildReadout();
	TSharedRef<SWidget> BuildSendButton();

	// ---- Text and triggers ----
	void OnComposerTextChanged(const FText& NewText);
	FReply HandleKeyDown(const FGeometry& Geometry, const FKeyEvent& Event);

	/** Re-derive the trigger state from the text's trailing token. */
	void UpdateTriggerState();
	void CloseTrigger();
	void HandleTriggerChosen(const FChatTriggerEntry& Entry);

	/** Replace the active trigger token (from its `@`/`#`/`/` to the end) with
	 *  Replacement, then put the caret at the end. */
	void ReplaceTriggerToken(const FString& Replacement);

	/** Start a trigger from a toolbar button rather than a keystroke. */
	void BeginTriggerFromButton(EChatTriggerMode Mode);

	// ---- Sending ----
	void SendCurrentText();
	FReply OnSendClicked();

	// ---- Attachments ----
	void AddAttachment(const FChatAttachmentPtr& Attachment);
	void RemoveAttachment(const FGuid& Id);
	void TogglePin(const FGuid& Id);
	FReply OnAttachFileClicked(bool bImagesOnly);
	FReply OnCaptureViewportClicked();
	/** @return true when the clipboard held an image and it was attached. */
	bool TryAttachClipboardImage();

	// ---- Prompts and tools ----
	void ShowArgumentForm(const FChatTriggerEntry& Entry);
	void RunToolWithArgs(const FString& ToolName, const TSharedPtr<FJsonObject>& Args);
	void ExpandPromptTemplate(const FString& PromptName, const TSharedPtr<FJsonObject>& Args);

	// ---- Readout ----
	void InvalidateEstimate();
	const FMCPChatTokenEstimator::FEstimate& GetEstimate() const;

	FText       GetReadoutText() const;
	FSlateColor GetReadoutColor() const;
	FText       GetReadoutTooltip() const;

	FText GetSendGlyph() const;
	FText GetSendTooltip() const;
	FText GetHintText() const;
	FText GetEffortText() const;
	EVisibility GetEffortVisibility() const;
	bool  IsNarrow() const { return NarrowMode.Get(false); }

	/** Enter sends unless the user reversed it in settings; the placeholder always
	 *  states the binding that is actually in force. */
	static bool EnterSends();

	TSharedPtr<FMCPChatController> Controller;
	TAttribute<bool>               NarrowMode;
	FOnComposerCommandResult       OnCommandResult;

	TSharedPtr<SMultiLineEditableTextBox> TextBox;
	TSharedPtr<SChatAttachmentStrip>      AttachmentStrip;
	TSharedPtr<SChatModelPicker>          ModelPicker;
	TSharedPtr<SMenuAnchor>               TriggerAnchor;
	TSharedPtr<SChatTriggerPopup>         TriggerPopup;

	TArray<FChatAttachmentPtr> Attachments;

	// ---- Trigger state ----
	bool             bTriggerActive = false;
	EChatTriggerMode TriggerMode = EChatTriggerMode::Context;
	/** Index of the trigger character in the composer text. */
	int32            TriggerStart = INDEX_NONE;

	// ---- History recall ----
	/** -1 = not recalling. */
	int32   RecallIndex = INDEX_NONE;
	/** What was in the box when recall started, so ↓ past the newest restores it. */
	FString RecallStash;

	/** Which session's draft is currently loaded, so a session switch saves the old
	 *  draft before loading the new one. */
	FGuid LoadedDraftSession;

	/** Recomputing the estimate walks the whole transcript; doing that per paint
	 *  would make typing measurably heavier in a long conversation. */
	mutable FMCPChatTokenEstimator::FEstimate CachedEstimate;
	mutable bool bEstimateValid = false;

	/** Highlight the composer while a compatible drag is over it. */
	bool bDragHighlight = false;
};
