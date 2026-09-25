// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FMCPChatController;
class SVerticalBox;

DECLARE_DELEGATE_OneParam(FOnSuggestionChosen, const FString& /*Prompt*/);
DECLARE_DELEGATE(FOnOpenSettingsRequested);

/**
 * Phase 8 — what the transcript shows when there is nothing in it (03 §8).
 *
 * Two different empty states, because they are two different problems:
 *
 *  **First run, nothing configured.** The user does not need a suggestion, they
 *  need a decision: a local agent (uses a sign-in they already have, costs
 *  nothing extra) or an API key. Both are offered, the free one first, and each
 *  goes straight to the place that configures it.
 *
 *  **Configured, empty conversation.** Now suggestions help — and they are
 *  derived from the editor's actual state, so "Review BP_Player" only appears if
 *  a Blueprint is open. A generic prompt list is decoration; a specific one is a
 *  demonstration that the panel can see the project.
 */
class SChatEmptyState : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatEmptyState) {}
		SLATE_ARGUMENT(TSharedPtr<FMCPChatController>, Controller)
		SLATE_EVENT(FOnSuggestionChosen, OnSuggestionChosen)
		SLATE_EVENT(FOnOpenSettingsRequested, OnOpenSettings)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Re-evaluate configuration and suggestions. Cheap; called when the transcript
	 *  becomes empty or the backend changes. */
	void Refresh();

private:
	/** True when no backend is usable — no key anywhere and no agent binary found. */
	bool IsAnythingConfigured() const;

	TSharedRef<SWidget> BuildFirstRun();
	TSharedRef<SWidget> BuildReadyState();
	TSharedRef<SWidget> BuildSuggestionChip(const FText& Label, const FString& Prompt);

	/** Prompts derived from what is actually open in the editor. */
	TArray<TPair<FText, FString>> GatherSuggestions() const;

	TSharedPtr<FMCPChatController> Controller;
	TSharedPtr<SVerticalBox>       Root;

	FOnSuggestionChosen      OnSuggestionChosen;
	FOnOpenSettingsRequested OnOpenSettings;
};
