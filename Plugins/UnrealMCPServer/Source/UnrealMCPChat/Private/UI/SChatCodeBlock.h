// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SMultiLineEditableText;

/**
 * Phase 3 — a fenced code block.
 *
 * Read-only SMultiLineEditableText (not STextBlock) so the text is selectable and
 * copyable per-line, which is the whole point of showing code. Highlighting runs
 * through FMCPCodeSyntaxHighlighter.
 *
 * Long blocks collapse behind a "Show all (N lines)" footer: a 400-line paste
 * should not push the actual answer off screen.
 */
class SChatCodeBlock : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatCodeBlock)
		: _Language()
		, _IsDiff(false)
	{}
		SLATE_ARGUMENT(FString, Code)
		SLATE_ARGUMENT(FString, Language)
		SLATE_ARGUMENT(bool, IsDiff)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> BuildHeader();
	TSharedRef<SWidget> BuildBody();
	TSharedRef<SWidget> BuildDiffBody();

	FReply OnCopyClicked();
	FReply OnSaveClicked();
	FReply OnToggleCollapsed();

	FText       GetToggleText() const;
	EVisibility GetToggleVisibility() const;
	FOptionalSize GetBodyMaxHeight() const;

	FString Code;
	FString Language;
	bool    bIsDiff = false;
	int32   LineCount = 0;
	bool    bCollapsed = false;

	/** Blocks longer than this start collapsed. */
	static constexpr int32 CollapseThresholdLines = 40;
	/** Approximate row height used to cap the collapsed body. */
	static constexpr float CollapsedPreviewLines = 20.f;
};
