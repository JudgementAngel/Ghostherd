// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPChatContext.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SVerticalBox;
class SScrollBox;

/** Which trigger opened the popup. One widget, three sources — they share layout,
 *  keyboard handling and ranking, and only the row source differs. */
enum class EChatTriggerMode : uint8
{
	Context,   // '@' — editor state
	Action,    // '#' — MCP tools, workflow prompts, editor commands
	Command,   // '/' — local commands
};

/** One selectable row. */
struct FChatTriggerEntry
{
	enum class EKind : uint8
	{
		Context,
		Tool,
		Prompt,
		EditorCommand,
		SlashCommand,
	};

	EKind   Kind = EKind::Context;

	/** What the caller acts on: a tool name, a command name, "asset:/Game/Foo". */
	FString Id;
	FString Label;
	FString Detail;
	/** Group heading this row sits under. */
	FString Category;

	/** False when picking this only narrows the query (an `@Asset:` prefix). */
	bool bIsTerminal = true;
	bool bDestructive = false;
	bool bReadOnly = false;
	/** Tool/prompt has required arguments — picking it opens the inline form. */
	bool bNeedsArguments = false;

	EChatContextKind ContextKind = EChatContextKind::Unknown;
	FString          ContextTarget;
};

DECLARE_DELEGATE_OneParam(FOnTriggerEntryChosen, const FChatTriggerEntry&);
DECLARE_DELEGATE(FOnTriggerDismissed);

/**
 * Phase 6 — the `@` / `#` / `/` palette (docs/03_UIUX_SPEC.md §5.4–§5.6).
 *
 * Keyboard focus never leaves the composer while this is open. That is a
 * deliberate constraint, not an implementation shortcut: the user is mid-sentence,
 * and moving focus into the popup would break IME composition, break `Home`/`End`,
 * and make dismissing it feel like an interruption. The composer forwards the four
 * navigation keys and this widget owns nothing but its own selection index.
 */
class SChatTriggerPopup : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatTriggerPopup) {}
		SLATE_EVENT(FOnTriggerEntryChosen, OnChosen)
		SLATE_EVENT(FOnTriggerDismissed, OnDismissed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Re-source and re-rank. Cheap enough to call on every keystroke. */
	void SetQuery(EChatTriggerMode Mode, const FString& Query);

	/** True when at least one row is showing — the composer uses this to decide
	 *  whether the popup should be open at all. */
	bool HasResults() const { return Entries.Num() > 0; }

	// ---- Keyboard, forwarded from the composer ----
	void MoveSelection(int32 Delta);
	/** @return false when there was nothing to commit, so the composer can let the
	 *  keypress through instead of swallowing it. */
	bool CommitSelection();

	const FChatTriggerEntry* GetSelectedEntry() const;

private:
	void RebuildRows();

	void GatherContext(const FString& Query);
	void GatherActions(const FString& Query);
	void GatherCommands(const FString& Query);

	TSharedRef<SWidget> BuildHeadingRow(const FString& Category) const;
	TSharedRef<SWidget> BuildEntryRow(int32 EntryIndex);

	EChatTriggerMode Mode = EChatTriggerMode::Context;
	FString          Query;

	TArray<FChatTriggerEntry> Entries;
	int32                     SelectedIndex = 0;

	TSharedPtr<SVerticalBox> RowContainer;
	TSharedPtr<SScrollBox>   ScrollBox;

	FOnTriggerEntryChosen OnChosen;
	FOnTriggerDismissed   OnDismissed;

	/** Keeping this small is the point of a palette. Eight rows fit above a composer
	 *  without covering the last message, which is what people are reading while
	 *  they type. */
	static constexpr int32 MaxRows = 8;
};
