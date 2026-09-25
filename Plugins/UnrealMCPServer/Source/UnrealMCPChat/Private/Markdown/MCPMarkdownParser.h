// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Phase 3 — markdown → block tree.
 *
 * Scope is fixed by docs/03_UIUX_SPEC.md §4.2 and deliberately excludes footnotes,
 * nested tables and HTML passthrough. Model output is CommonMark-ish, not a
 * document format; chasing full CommonMark here would cost days and buy nothing a
 * chat transcript needs.
 *
 * The parser is pure: no Slate, no engine subsystems. That is what makes it
 * testable from an automation spec against a fixture corpus.
 */

/** An inline span within a block. */
struct FMarkdownInline
{
	enum class EType : uint8
	{
		Text,
		Bold,
		Italic,
		BoldItalic,
		Code,        // `inline code`
		Strike,      // ~~struck~~
		Link,        // [text](url)
		UnrealRef,   // /Game/... , unreal://... , BP_Foo — linkified post-parse
	};

	EType   Type = EType::Text;
	FString Text;
	FString Url;    // Link / UnrealRef target
};

/** A block-level element. */
struct FMarkdownBlock
{
	enum class EType : uint8
	{
		Paragraph,
		Heading,
		CodeFence,
		ListItem,
		Quote,
		Rule,
		TableRow,
		Image,
	};

	EType Type = EType::Paragraph;

	/** Paragraph / Heading / ListItem / Quote. */
	TArray<FMarkdownInline> Inlines;

	/** Heading. 1–6, clamped to 3 at render time. */
	int32 HeadingLevel = 0;

	/** CodeFence. Language is the info string after the opening fence. */
	FString CodeText;
	FString CodeLanguage;
	/** True when the fence language is `diff`, or the body looks like a unified
	 *  diff. Renders through the diff path instead of the highlighter. */
	bool bIsDiff = false;

	/** ListItem. */
	int32 ListDepth = 0;
	bool  bOrdered = false;
	int32 OrderedIndex = 0;
	/** -1 = not a task item, 0 = unchecked, 1 = checked. */
	int32 TaskState = -1;

	/** TableRow. */
	TArray<TArray<FMarkdownInline>> Cells;
	bool bTableHeader = false;

	/** Image. */
	FString ImageUrl;
	FString ImageAlt;
};

struct FMarkdownDocument
{
	TArray<FMarkdownBlock> Blocks;
};

class FMCPMarkdownParser
{
public:
	/** Parse a whole message body. Never fails: unrecognised syntax degrades to
	 *  literal text rather than being dropped. */
	static FMarkdownDocument Parse(const FString& Source);

	/** Inline-only parse — exposed for tests and for re-parsing a single run. */
	static TArray<FMarkdownInline> ParseInlines(const FString& Line);

	/**
	 * Turn Unreal-looking strings inside Text runs into UnrealRef links:
	 *   /Game/... /Engine/... /Script/...      asset paths
	 *   unreal://...                           resource URIs
	 * Applied automatically by Parse(); separate so tests can target it.
	 *
	 * Bare identifiers like BP_Player are deliberately NOT linkified — too many
	 * false positives in prose, and a dead link is worse than plain text.
	 */
	static TArray<FMarkdownInline> LinkifyUnrealReferences(const TArray<FMarkdownInline>& In);

	/** True when the text looks like a unified diff (has @@ hunks or ---/+++ headers). */
	static bool LooksLikeUnifiedDiff(const FString& Text);

private:
	static bool TryParseFenceOpen(const FString& Line, FString& OutLanguage, int32& OutFenceLen, TCHAR& OutFenceChar);
	static bool IsFenceClose(const FString& Line, int32 FenceLen, TCHAR FenceChar);
	static bool IsHorizontalRule(const FString& Line);
	static bool TryParseHeading(const FString& Line, int32& OutLevel, FString& OutText);
	static bool TryParseListItem(const FString& Line, int32& OutDepth, bool& bOutOrdered,
	                             int32& OutIndex, int32& OutTaskState, FString& OutText);
	static bool TryParseTableDelimiterRow(const FString& Line);
	static TArray<FString> SplitTableRow(const FString& Line);
};
