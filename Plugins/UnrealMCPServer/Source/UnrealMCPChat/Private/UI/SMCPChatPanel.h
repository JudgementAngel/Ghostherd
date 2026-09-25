// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

struct FChatCommandResult;
class FMCPChatController;
class SChatComposer;
class SChatEmptyState;
class SChatHistoryRail;
class SChatTranscript;

/** Responsive breakpoints from docs/03_UIUX_SPEC.md §2.3. */
enum class EMCPChatBreakpoint : uint8
{
	Narrow,   // < NarrowBreakpoint — icon-only composer, no rail
	Compact,  // NarrowBreakpoint..WideBreakpoint — rail as an overlay drawer
	Wide,     // >= WideBreakpoint — rail pinned, transcript centred
};

/**
 * The panel shell: history rail / transcript / composer, plus the responsive
 * layout. Owns the controller for the lifetime of the tab.
 *
 * Every visual value comes from FMCPChatStyle — no literals here by design
 * (docs/03_UIUX_SPEC.md §3.5).
 */
class SMCPChatPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMCPChatPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SMCPChatPanel() override;

	//~ SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	//~ End SWidget

	EMCPChatBreakpoint GetBreakpoint() const { return CurrentBreakpoint; }

private:
	TSharedRef<SWidget> BuildHeader();
	TSharedRef<SWidget> BuildStatusBar();

	void RebuildLayout();
	void UpdateBreakpoint(float AvailableWidth, double CurrentTime);

	FReply OnToggleHistoryRail();

	/** Phase 6 — act on a `/` command the composer executed locally. */
	void HandleCommandResult(const FChatCommandResult& Result);

	/** Phase 8 — states. */
	void HandleSuggestionChosen(const FString& Prompt);
	void OpenSettingsTab();

	EVisibility GetTranscriptVisibility() const;
	EVisibility GetEmptyStateVisibility() const;
	/** True when the active conversation has nothing in it yet. */
	bool IsSessionEmpty() const;

	EVisibility GetHistoryRailVisibility() const;
	FMargin     GetTranscriptPadding() const;
	/** The transcript's actual width, not just a ceiling on it.
	 *
	 *  MaxDesiredWidth alone was the bug: it caps the DESIRED width, and the
	 *  centring border then sizes the column to whatever the content happened to
	 *  want — so a short reply rendered as a 130 px ribbon floating off-centre.
	 *  An override forces min(available, preferred) instead. */
	FOptionalSize GetTranscriptWidth() const;
	FText       GetStatusText() const;
	FText       GetHeaderTitle() const;

	TSharedPtr<FMCPChatController> Controller;

	TSharedPtr<SWidget>          RailContainer;
	TSharedPtr<SChatHistoryRail> HistoryRail;
	TSharedPtr<SChatTranscript>  Transcript;
	TSharedPtr<SChatComposer>    Composer;
	TSharedPtr<SChatEmptyState>  EmptyState;

	/** So a session switch in the rail swaps the composer's draft too. */
	FDelegateHandle TranscriptChangedHandle;

	EMCPChatBreakpoint CurrentBreakpoint = EMCPChatBreakpoint::Wide;

	/** Panel width measured in Tick, so the transcript can be sized against the
	 *  space it actually has rather than against its own content. */
	float LastPanelWidth = 0.f;
	bool bUserCollapsedRail = false;

	EMCPChatBreakpoint PendingBreakpoint = EMCPChatBreakpoint::Wide;
	double PendingSinceSeconds = 0.0;
	static constexpr double BreakpointHysteresisSeconds = 0.15;

	FDelegateHandle StyleReloadHandle;
};
