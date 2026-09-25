// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Markdown/MCPMarkdownParser.h"

class SWidget;
class SVerticalBox;

/**
 * Phase 3 — block tree → Slate.
 *
 * Paragraphs go through SRichTextBlock so a line containing mixed styles still
 * wraps as one flowing paragraph. (A SHorizontalBox of STextBlocks cannot wrap
 * mid-paragraph, which is why the obvious approach is wrong.)
 *
 * SRichTextBlock consumes UE's own markup, so inline runs are re-emitted as
 * <Chat.Md.Bold>text</> and the tag names are registered as FTextBlockStyles in
 * the chat style set. That also means literal user text has to be made safe —
 * see EscapeForRichText.
 */
class FMCPMarkdownRenderer
{
public:
	/** Build a widget tree for a whole document. */
	static TSharedRef<SWidget> BuildDocument(const FMarkdownDocument& Doc, float MaxImageWidth = 560.f);

	/** Convenience: parse and build in one step. */
	static TSharedRef<SWidget> BuildFromMarkdown(const FString& Source, float MaxImageWidth = 560.f);

	/** Inline runs → UE rich-text markup. Public for tests. */
	static FString InlinesToRichText(const TArray<FMarkdownInline>& Inlines);

	/**
	 * Make literal text safe for UE's rich-text parser.
	 *
	 * The parser looks for `<tag>content</>`; it does NOT recurse into content and
	 * has no entity escaping. So the only genuinely dangerous sequence inside a run
	 * is a premature `</>`, which would close our wrapper early. We neutralise that
	 * and leave everything else — including `<vector>` and `a < b` — untouched.
	 */
	static FString EscapeForRichText(const FString& In);

	/** Handle a click on a link emitted by this renderer. Routes:
	 *    unreal://…   → resource, focuses the matching editor surface
	 *    /Game/…      → sync the Content Browser to the asset
	 *    http(s)://…  → external browser
	 */
	static void HandleLinkClicked(const FString& Href);

private:
	static TSharedRef<SWidget> BuildParagraph(const FMarkdownBlock& Block, const FString& StyleName);
	static TSharedRef<SWidget> BuildHeading(const FMarkdownBlock& Block);
	static TSharedRef<SWidget> BuildListItem(const FMarkdownBlock& Block);
	static TSharedRef<SWidget> BuildQuote(const FMarkdownBlock& Block);
	static TSharedRef<SWidget> BuildRule();
	static TSharedRef<SWidget> BuildTable(const TArray<FMarkdownBlock>& Rows);
};
