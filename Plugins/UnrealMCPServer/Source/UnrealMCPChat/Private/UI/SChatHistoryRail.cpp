// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SChatHistoryRail.h"
#include "MCPChatController.h"
#include "MCPChatStyle.h"
#include "UnrealMCPChatModule.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SChatHistoryRail"

void SChatHistoryRail::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;

	IndexChangedHandle = FMCPChatStore::Get().OnIndexChanged.AddSP(this, &SChatHistoryRail::Rebuild);
	if (Controller.IsValid())
	{
		// Selecting a different session must move the highlight.
		TranscriptChangedHandle = Controller->OnTranscriptChanged.AddSP(this, &SChatHistoryRail::Rebuild);
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		// ---- Search + new ----
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMCPChatStyle::Padding(TEXT("sm")))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Search conversations..."))
				.OnTextChanged(this, &SChatHistoryRail::OnSearchTextChanged)
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			.Padding(FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("NewChat", "New conversation"))
				.OnClicked(this, &SChatHistoryRail::OnNewChatClicked)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Plus"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		]

		// ---- Entries ----
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SAssignNew(EntryList, SListView<FRailEntryPtr>)
			.ListItemsSource(&Entries)
			.OnGenerateRow(this, &SChatHistoryRail::OnGenerateRow)
			.OnSelectionChanged(this, &SChatHistoryRail::OnSelectionChanged)
			.OnContextMenuOpening(this, &SChatHistoryRail::OnContextMenu)
			.SelectionMode(ESelectionMode::Single)
		]
	];

	Rebuild();
}

SChatHistoryRail::~SChatHistoryRail()
{
	FMCPChatStore::Get().OnIndexChanged.Remove(IndexChangedHandle);
	if (Controller.IsValid())
	{
		Controller->OnTranscriptChanged.Remove(TranscriptChangedHandle);
	}
}

void SChatHistoryRail::Rebuild()
{
	Entries.Reset();

	for (const FMCPChatStore::FGroupedIndex& Group : FMCPChatStore::Get().GetGroupedIndex(SearchQuery))
	{
		FRailEntryPtr Heading = MakeShared<FRailEntry>();
		Heading->bIsHeading = true;
		Heading->Heading    = Group.Heading;
		Entries.Add(Heading);

		for (const FChatSessionSummaryPtr& Summary : Group.Rows)
		{
			FRailEntryPtr Row = MakeShared<FRailEntry>();
			Row->Summary = Summary;
			Entries.Add(Row);
		}
	}

	if (EntryList.IsValid())
	{
		EntryList->RequestListRefresh();
	}
}

bool SChatHistoryRail::IsActiveSession(const FGuid& SessionId) const
{
	if (!Controller.IsValid()) { return false; }
	const FChatSessionPtr Active = Controller->GetActiveSession();
	return Active.IsValid() && Active->Id == SessionId;
}

TSharedRef<ITableRow> SChatHistoryRail::OnGenerateRow(FRailEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!Item.IsValid())
	{
		return SNew(STableRow<FRailEntryPtr>, OwnerTable);
	}

	// --- Group heading ---
	if (Item->bIsHeading)
	{
		return SNew(STableRow<FRailEntryPtr>, OwnerTable)
			.Padding(0.f)
			// Headings are not selectable; clicking one should do nothing.
			.ShowSelection(false)
			[
				SNew(SBox)
				.Padding(FMCPChatStyle::Space(TEXT("sm")), FMCPChatStyle::Space(TEXT("sm")),
				         FMCPChatStyle::Space(TEXT("sm")), FMCPChatStyle::Space(TEXT("xs")))
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.Text(Item->Heading)
				]
			];
	}

	// --- Session row ---
	const FChatSessionSummaryPtr Summary = Item->Summary;
	const FGuid RowId = Summary.IsValid() ? Summary->Id : FGuid();

	return SNew(STableRow<FRailEntryPtr>, OwnerTable)
		.Padding(0.f)
		[
			SNew(SBorder)
			.BorderImage_Lambda([this, RowId]()
			{
				return IsActiveSession(RowId)
					? FMCPChatStyle::Brush(TEXT("sessionActive"))
					: FAppStyle::Get().GetBrush("NoBorder");
			})
			.Padding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					.Padding(0.f, 0.f, FMCPChatStyle::Space(TEXT("xs")), 0.f)
					[
						SNew(STextBlock)
						.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
						.Text(FText::FromString(TEXT("★")))
						.Visibility(Summary.IsValid() && Summary->bPinned ? EVisibility::Visible : EVisibility::Collapsed)
					]

					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(STextBlock)
						.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
						.Text(FText::FromString(
							Summary.IsValid() && !Summary->Title.IsEmpty()
								? Summary->Title
								: LOCTEXT("Untitled", "Untitled").ToString()))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
				]

				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.Text(FText::FromString(Summary.IsValid() ? Summary->Preview : FString()))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Visibility(Summary.IsValid() && !Summary->Preview.IsEmpty()
						? EVisibility::Visible : EVisibility::Collapsed)
				]
			]
		];
}

void SChatHistoryRail::OnSelectionChanged(FRailEntryPtr Item, ESelectInfo::Type SelectInfo)
{
	// Ignore programmatic selection (Rebuild re-selects), and ignore headings.
	if (SelectInfo == ESelectInfo::Direct) { return; }
	if (!Item.IsValid() || Item->bIsHeading || !Item->Summary.IsValid()) { return; }
	if (!Controller.IsValid()) { return; }

	Controller->OpenSession(Item->Summary->Id);
}

void SChatHistoryRail::OnSearchTextChanged(const FText& NewText)
{
	SearchQuery = NewText.ToString();
	Rebuild();
}

FReply SChatHistoryRail::OnNewChatClicked()
{
	if (Controller.IsValid())
	{
		Controller->NewSession();
	}
	return FReply::Handled();
}

TSharedPtr<SWidget> SChatHistoryRail::OnContextMenu()
{
	if (!EntryList.IsValid()) { return nullptr; }

	const TArray<FRailEntryPtr> Selected = EntryList->GetSelectedItems();
	if (Selected.Num() == 0 || !Selected[0].IsValid() || Selected[0]->bIsHeading
		|| !Selected[0]->Summary.IsValid())
	{
		return nullptr;
	}

	const FGuid SessionId = Selected[0]->Summary->Id;
	const bool bPinned = Selected[0]->Summary->bPinned;

	FMenuBuilder Menu(/*bShouldCloseAfterSelection*/ true, nullptr);

	Menu.AddMenuEntry(
		bPinned ? LOCTEXT("Unpin", "Unpin") : LOCTEXT("Pin", "Pin"),
		LOCTEXT("PinTip", "Keep this conversation at the top of the list"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([SessionId]()
		{
			if (const FChatSessionPtr Session = FMCPChatStore::Get().LoadSession(SessionId))
			{
				Session->bPinned = !Session->bPinned;
				FMCPChatStore::Get().SaveNow(SessionId);
				FMCPChatStore::Get().OnIndexChanged.Broadcast();
			}
		})));

	Menu.AddMenuEntry(
		LOCTEXT("ExportMd", "Export as Markdown"),
		LOCTEXT("ExportMdTip", "Write this conversation to Saved/UnrealMCPChat/exports"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([SessionId]()
		{
			const FString Path = FMCPChatStore::Get().ExportSessionToMarkdown(SessionId);

			FNotificationInfo Info(Path.IsEmpty()
				? LOCTEXT("ExportFailed", "Export failed — see the Output Log")
				: FText::Format(LOCTEXT("ExportOk", "Exported to {0}"), FText::FromString(Path)));
			Info.ExpireDuration = 6.f;
			if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
			{
				Item->SetCompletionState(Path.IsEmpty() ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
			}
		})));

	Menu.AddSeparator();

	Menu.AddMenuEntry(
		LOCTEXT("Delete", "Delete"),
		LOCTEXT("DeleteTip", "Permanently delete this conversation and its attachments"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
		FUIAction(FExecuteAction::CreateLambda([this, SessionId]()
		{
			// Deleting a transcript is destructive and not undoable, so confirm.
			// (Phase 8 replaces this with an undo toast, per the UI spec.)
			const EAppReturnType::Type Answer = FMessageDialog::Open(
				EAppMsgType::YesNo,
				LOCTEXT("ConfirmDelete", "Delete this conversation permanently?"));

			if (Answer == EAppReturnType::Yes)
			{
				const bool bWasActive = IsActiveSession(SessionId);
				FMCPChatStore::Get().DeleteSession(SessionId);
				if (bWasActive && Controller.IsValid())
				{
					Controller->NewSession();
				}
			}
		})));

	return Menu.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
