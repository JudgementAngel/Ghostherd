// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SChatTriggerPopup.h"
#include "Core/MCPChatCommands.h"
#include "MCPChatStyle.h"
#include "MCPChatToolBridge.h"

#include "MCPPromptProvider.h"
#include "MCPToolRegistry.h"

#include "Styling/AppStyle.h"
#include "Styling/StyleDefaults.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SChatTriggerPopup"

namespace
{
	/** A curated allowlist, not "every console command". Exposing the whole console
	 *  through a chat palette would put `quit` and `RestartLevel` one Tab away from a
	 *  half-typed word. */
	struct FEditorCommandEntry
	{
		const TCHAR* Label;
		const TCHAR* Command;
		bool         bDestructive;
	};

	const FEditorCommandEntry EditorCommands[] =
	{
		{ TEXT("stat fps"),        TEXT("stat fps"),        false },
		{ TEXT("stat unit"),       TEXT("stat unit"),       false },
		{ TEXT("stat game"),       TEXT("stat game"),       false },
		{ TEXT("stat none"),       TEXT("stat none"),       false },
		{ TEXT("Save All"),        TEXT("SaveAll"),         false },
		{ TEXT("Build Lighting"),  TEXT("BuildLighting"),   true  },
		{ TEXT("Build Paths"),     TEXT("BuildPaths"),      true  },
		{ TEXT("Toggle Wireframe"),TEXT("viewmode wireframe"), false },
		{ TEXT("Toggle Lit"),      TEXT("viewmode lit"),    false },
	};

	int32 ScoreMatch(const FString& Query, const FString& Candidate)
	{
		if (Query.IsEmpty()) { return 100; }

		const FString Q = Query.ToLower();
		const FString C = Candidate.ToLower();

		if (C == Q)                { return 1000; }
		if (C.StartsWith(Q))       { return 800 - C.Len(); }

		const int32 Found = C.Find(Q);
		if (Found != INDEX_NONE)   { return 500 - Found; }

		// Subsequence: "sa" matches "spawn_actor". Ranked last because it produces
		// the most surprising hits.
		int32 Ci = 0;
		for (int32 Qi = 0; Qi < Q.Len(); ++Qi)
		{
			bool bHit = false;
			while (Ci < C.Len())
			{
				if (C[Ci++] == Q[Qi]) { bHit = true; break; }
			}
			if (!bHit) { return INDEX_NONE; }
		}
		return 200 - C.Len();
	}
}

void SChatTriggerPopup::Construct(const FArguments& InArgs)
{
	OnChosen    = InArgs._OnChosen;
	OnDismissed = InArgs._OnDismissed;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("popupBg")))
		.Padding(FMCPChatStyle::Padding(TEXT("xs")))
		[
			SNew(SBox)
			.MinDesiredWidth(FMCPChatStyle::Space(TEXT("popupMinW"), 340.f))
			.MaxDesiredHeight(FMCPChatStyle::Space(TEXT("popupMaxH"), 420.f))
			[
				SAssignNew(ScrollBox, SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(RowContainer, SVerticalBox)
				]
			]
		]
	];
}

void SChatTriggerPopup::SetQuery(EChatTriggerMode InMode, const FString& InQuery)
{
	// Reset the selection whenever the query changes. Preserving it across a
	// keystroke means the highlighted row silently becomes a different command,
	// which is how people run the wrong thing.
	Mode  = InMode;
	Query = InQuery;
	SelectedIndex = 0;

	Entries.Reset();
	switch (Mode)
	{
	case EChatTriggerMode::Context: GatherContext(Query);  break;
	case EChatTriggerMode::Action:  GatherActions(Query);  break;
	case EChatTriggerMode::Command: GatherCommands(Query); break;
	}

	RebuildRows();
}

// ============================================================================
// Sources
// ============================================================================

void SChatTriggerPopup::GatherContext(const FString& InQuery)
{
	for (const FChatContextCandidate& C : FMCPChatContextResolver::Get().Suggest(InQuery, MaxRows))
	{
		FChatTriggerEntry E;
		E.Kind          = FChatTriggerEntry::EKind::Context;
		E.ContextKind   = C.Kind;
		E.ContextTarget = C.Target;
		E.Id            = FString::Printf(TEXT("%s:%s"),
			FMCPChatContextResolver::KindToString(C.Kind), *C.Target);
		E.Label         = C.Label;
		E.Detail        = C.Detail;
		E.Category      = LOCTEXT("CatContext", "EDITOR CONTEXT").ToString();
		E.bIsTerminal   = C.bIsTerminal;
		E.bReadOnly     = true;
		Entries.Add(MoveTemp(E));
	}
}

void SChatTriggerPopup::GatherActions(const FString& InQuery)
{
	// ---- Workflow prompts first ----
	// There are 18 of them against 450 tools, and they are the higher-leverage
	// choice: "#world_builder" is a whole task, "#spawn_actor" is one step.
	struct FScored { FChatTriggerEntry Entry; int32 Score; };
	TArray<FScored> Scored;

	for (const FMCPPromptDefinition& P : FMCPPromptProvider::Get().GetAllPrompts())
	{
		const int32 Score = ScoreMatch(InQuery, P.Name);
		if (Score == INDEX_NONE) { continue; }

		FChatTriggerEntry E;
		E.Kind     = FChatTriggerEntry::EKind::Prompt;
		E.Id       = P.Name;
		E.Label    = P.Name;
		E.Detail   = P.Description;
		E.Category = LOCTEXT("CatWorkflows", "WORKFLOWS").ToString();
		E.bReadOnly = true;
		for (const FMCPPromptArgument& Arg : P.Arguments)
		{
			if (Arg.bRequired) { E.bNeedsArguments = true; break; }
		}
		// Even an all-optional prompt opens the form: its arguments ARE the
		// customisation, and hiding them makes every run identical.
		if (P.Arguments.Num() > 0) { E.bNeedsArguments = true; }

		Scored.Add({ MoveTemp(E), Score + 50 });
	}

	// ---- MCP tools ----
	for (const FMCPToolDefinition& T : FMCPToolRegistry::Get().GetAllTools())
	{
		const int32 Score = ScoreMatch(InQuery, T.Name);
		if (Score == INDEX_NONE) { continue; }

		FChatTriggerEntry E;
		E.Kind         = FChatTriggerEntry::EKind::Tool;
		E.Id           = T.Name;
		E.Label        = T.Name;
		E.Detail       = T.Description;
		E.Category     = LOCTEXT("CatTools", "EDITOR TOOLS").ToString();
		E.bDestructive = T.bDestructiveHint;
		E.bReadOnly    = T.bReadOnlyHint;

		// Any tool with a schema gets the form. Running a tool with no arguments when
		// it expected some produces a validation error the user cannot act on.
		if (T.InputSchema.IsValid())
		{
			const TSharedPtr<FJsonObject>* Props = nullptr;
			if (T.InputSchema->TryGetObjectField(TEXT("properties"), Props) && Props && (*Props)->Values.Num() > 0)
			{
				E.bNeedsArguments = true;
			}
		}

		Scored.Add({ MoveTemp(E), Score });
	}

	// ---- Editor commands ----
	for (const FEditorCommandEntry& C : EditorCommands)
	{
		const int32 Score = ScoreMatch(InQuery, C.Label);
		if (Score == INDEX_NONE) { continue; }

		FChatTriggerEntry E;
		E.Kind         = FChatTriggerEntry::EKind::EditorCommand;
		E.Id           = C.Command;
		E.Label        = C.Label;
		E.Detail       = C.Command;
		E.Category     = LOCTEXT("CatCommands", "EDITOR COMMANDS").ToString();
		E.bDestructive = C.bDestructive;
		Scored.Add({ MoveTemp(E), Score });
	}

	Scored.Sort([](const FScored& A, const FScored& B) { return A.Score > B.Score; });

	const int32 Count = FMath::Min(Scored.Num(), MaxRows);
	for (int32 i = 0; i < Count; ++i) { Entries.Add(MoveTemp(Scored[i].Entry)); }

	// Group visually without disturbing rank: rows keep their score order, but rows
	// of the same category end up adjacent so the headings read correctly.
	Entries.StableSort([](const FChatTriggerEntry& A, const FChatTriggerEntry& B)
	{
		return A.Category < B.Category;
	});
}

void SChatTriggerPopup::GatherCommands(const FString& InQuery)
{
	for (const FChatSlashCommand& C : FMCPChatSlashCommands::Filter(InQuery, MaxRows))
	{
		FChatTriggerEntry E;
		E.Kind     = FChatTriggerEntry::EKind::SlashCommand;
		E.Id       = C.Name;
		E.Label    = C.ArgHint.IsEmpty()
			? FString::Printf(TEXT("/%s"), *C.Name)
			: FString::Printf(TEXT("/%s %s"), *C.Name, *C.ArgHint);
		E.Detail   = C.Description.ToString();
		E.Category = LOCTEXT("CatLocal", "LOCAL COMMANDS  ·  never sent to a model").ToString();
		E.bReadOnly = true;
		Entries.Add(MoveTemp(E));
	}
}

// ============================================================================
// Rows
// ============================================================================

void SChatTriggerPopup::RebuildRows()
{
	if (!RowContainer.IsValid()) { return; }

	RowContainer->ClearChildren();

	FString LastCategory;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (Entries[i].Category != LastCategory)
		{
			LastCategory = Entries[i].Category;
			RowContainer->AddSlot().AutoHeight()[ BuildHeadingRow(LastCategory) ];
		}
		RowContainer->AddSlot().AutoHeight()[ BuildEntryRow(i) ];
	}

	if (Entries.Num() == 0)
	{
		RowContainer->AddSlot().AutoHeight()
		.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("sm")))
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(LOCTEXT("NoMatches", "No matches — keep typing, or press Esc to go back to the message."))
		];
	}
}

TSharedRef<SWidget> SChatTriggerPopup::BuildHeadingRow(const FString& Category) const
{
	return SNew(SBox)
		.Padding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(FText::FromString(Category))
		];
}

TSharedRef<SWidget> SChatTriggerPopup::BuildEntryRow(int32 EntryIndex)
{
	const FChatTriggerEntry& Entry = Entries[EntryIndex];

	// Destructive tools get a coloured dot at the point of choice. Finding out at the
	// approval prompt is too late to have avoided typing it.
	FName DotColour = TEXT("fgSubdued");
	FText DotTip = FText::GetEmpty();
	if (Entry.bDestructive)
	{
		DotColour = TEXT("readoutOver");
		DotTip = LOCTEXT("DestructiveTip", "This one cannot be undone with Ctrl+Z alone.");
	}
	else if (Entry.bReadOnly)
	{
		DotColour = TEXT("success");
		DotTip = LOCTEXT("ReadOnlyTip", "Reads only; changes nothing.");
	}

	FText RowTip = Entry.Detail.IsEmpty() ? DotTip : FText::FromString(Entry.Detail);
	if (Entry.bDestructive && !Entry.Detail.IsEmpty())
	{
		RowTip = FText::Format(LOCTEXT("DestructiveRowTip", "{0}\n\n⚠ {1}"),
			FText::FromString(Entry.Detail), DotTip);
	}

	return SNew(SBorder)
		.BorderImage_Lambda([this, EntryIndex]() -> const FSlateBrush*
		{
			return (EntryIndex == SelectedIndex)
				? FMCPChatStyle::Brush(TEXT("popupRowActive"))
				: FStyleDefaults::GetNoBrush();
		})
		.Padding(0.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMCPChatStyle::Padding(TEXT("md"), TEXT("xs")))
			.ToolTipText(RowTip)
			.OnClicked_Lambda([this, EntryIndex]()
			{
				SelectedIndex = EntryIndex;
				CommitSelection();
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(0.f, 0.f, FMCPChatStyle::Space(TEXT("sm")), 0.f)
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.ColorAndOpacity(FMCPChatStyle::Color(DotColour))
					.Text(FText::FromString(TEXT("●")))
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
					.Text(FText::FromString(Entry.Label))
				]

				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				.Padding(FMCPChatStyle::Space(TEXT("md")), 0.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
					.Text(FText::FromString(Entry.Detail))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
			]
		];
}

// ============================================================================
// Keyboard
// ============================================================================

void SChatTriggerPopup::MoveSelection(int32 Delta)
{
	if (Entries.Num() == 0) { return; }

	// Wrap. A palette of eight rows is short enough that wrapping is faster than
	// clamping, and nobody has to think about which end they are at.
	SelectedIndex = (SelectedIndex + Delta + Entries.Num()) % Entries.Num();
	RebuildRows();
}

bool SChatTriggerPopup::CommitSelection()
{
	const FChatTriggerEntry* Entry = GetSelectedEntry();
	if (!Entry) { return false; }

	// Copy before invoking: the delegate re-queries this widget, which rebuilds
	// Entries out from under the reference.
	const FChatTriggerEntry Copy = *Entry;
	OnChosen.ExecuteIfBound(Copy);
	return true;
}

const FChatTriggerEntry* SChatTriggerPopup::GetSelectedEntry() const
{
	return Entries.IsValidIndex(SelectedIndex) ? &Entries[SelectedIndex] : nullptr;
}

#undef LOCTEXT_NAMESPACE
