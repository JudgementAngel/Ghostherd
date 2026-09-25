// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPChatStore.h"
#include "MCPChatTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class FMCPChatController;
class SSearchBox;

/**
 * Phase 2 — conversation history.
 *
 * Renders from the index only; a full session is loaded when the user picks one.
 * Grouping (Pinned / Today / Yesterday / This week / Earlier) comes from the store
 * so the rail has no date logic of its own.
 */
class SChatHistoryRail : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatHistoryRail) {}
		SLATE_ARGUMENT(TSharedPtr<FMCPChatController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SChatHistoryRail() override;

private:
	/** One rail entry: either a group heading or a session. Using a single flat
	 *  list keeps SListView's virtualisation working across groups. */
	struct FRailEntry
	{
		bool  bIsHeading = false;
		FText Heading;
		FChatSessionSummaryPtr Summary;
	};
	using FRailEntryPtr = TSharedPtr<FRailEntry>;

	TSharedRef<ITableRow> OnGenerateRow(FRailEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnSelectionChanged(FRailEntryPtr Item, ESelectInfo::Type SelectInfo);
	void OnSearchTextChanged(const FText& NewText);
	void Rebuild();

	FReply OnNewChatClicked();
	TSharedPtr<SWidget> OnContextMenu();

	bool IsActiveSession(const FGuid& SessionId) const;

	TSharedPtr<FMCPChatController> Controller;

	TArray<FRailEntryPtr>              Entries;
	TSharedPtr<SListView<FRailEntryPtr>> EntryList;
	TSharedPtr<SSearchBox>             SearchBox;
	FString                            SearchQuery;

	FDelegateHandle IndexChangedHandle;
	FDelegateHandle TranscriptChangedHandle;
};
