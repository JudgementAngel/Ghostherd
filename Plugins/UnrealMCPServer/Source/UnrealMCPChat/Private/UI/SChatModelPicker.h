// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMCPChatBackend.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FMCPChatController;
class SComboButton;
class SEditableTextBox;
class SVerticalBox;

/**
 * Phase 6 — the model dropdown (docs/03_UIUX_SPEC.md §5.2).
 *
 * Grouped by provider, searchable, with an availability dot per row. The dot is
 * the important part: "why is nothing happening" is almost always "no key" or
 * "binary not on PATH", and answering that at the point of choice is far cheaper
 * than answering it after a failed turn.
 */
class SChatModelPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatModelPicker)
		: _CompactMode(false)
	{}
		SLATE_ARGUMENT(TSharedPtr<FMCPChatController>, Controller)
		/** Narrow breakpoint: collapse to a two-letter badge. */
		SLATE_ATTRIBUTE(bool, CompactMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Open the menu from elsewhere — `/model` with no argument does this. */
	void OpenMenu();

private:
	TSharedRef<SWidget> BuildMenuContent();
	void RebuildRows();

	TSharedRef<SWidget> BuildProviderHeading(const FString& ProviderId, const FText& Label) const;
	TSharedRef<SWidget> BuildModelRow(const FChatModelInfo& Model, const FString& BackendId);
	TSharedRef<SWidget> BuildAgentRow(const FChatBackendPtr& Backend);

	void SelectModel(const FString& BackendId, const FString& ModelId);

	FText GetButtonText() const;
	FText GetButtonTooltip() const;

	/** Right-hand column: "1M · $5/$25", "subscription", "local, free". */
	static FString DescribeModel(const FChatModelInfo& Model);

	TSharedPtr<FMCPChatController> Controller;
	TAttribute<bool>               CompactMode;

	TSharedPtr<SComboButton>   ComboButton;
	TSharedPtr<SEditableTextBox> SearchBox;
	TSharedPtr<SVerticalBox>   RowContainer;
	FString                    SearchText;
};
