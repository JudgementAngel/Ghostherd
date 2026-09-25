// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Markdown/MCPMarkdownParser.h"

namespace
{
	/** Leading spaces, with tabs counted as 4. Used for list nesting depth. */
	int32 CountIndent(const FString& Line, int32& OutFirstNonSpace)
	{
		int32 Indent = 0;
		int32 i = 0;
		for (; i < Line.Len(); ++i)
		{
			if (Line[i] == TEXT(' '))      { ++Indent; }
			else if (Line[i] == TEXT('\t')) { Indent += 4; }
			else                             { break; }
		}
		OutFirstNonSpace = i;
		return Indent;
	}

	bool IsBlank(const FString& Line)
	{
		for (TCHAR C : Line)
		{
			if (!FChar::IsWhitespace(C)) { return false; }
		}
		return true;
	}

	void AppendText(TArray<FMarkdownInline>& Out, const FString& Text)
	{
		if (Text.IsEmpty()) { return; }

		// Merge with a trailing plain-text run so consumers don't see the run
		// fragmentation the scanner produces.
		if (Out.Num() > 0 && Out.Last().Type == FMarkdownInline::EType::Text)
		{
			Out.Last().Text += Text;
			return;
		}
		FMarkdownInline Run;
		Run.Type = FMarkdownInline::EType::Text;
		Run.Text = Text;
		Out.Add(MoveTemp(Run));
	}
}

// ============================================================================
// Inline scanning
// ============================================================================

TArray<FMarkdownInline> FMCPMarkdownParser::ParseInlines(const FString& Line)
{
	TArray<FMarkdownInline> Out;
	FString Pending;

	const int32 Len = Line.Len();
	int32 i = 0;

	auto Flush = [&]()
	{
		AppendText(Out, Pending);
		Pending.Reset();
	};

	auto FindClose = [&Line, Len](int32 From, const TCHAR* Delim, int32 DelimLen) -> int32
	{
		for (int32 j = From; j + DelimLen <= Len; ++j)
		{
			bool bMatch = true;
			for (int32 k = 0; k < DelimLen; ++k)
			{
				if (Line[j + k] != Delim[k]) { bMatch = false; break; }
			}
			if (bMatch) { return j; }
		}
		return INDEX_NONE;
	};

	while (i < Len)
	{
		const TCHAR C = Line[i];

		// Backslash escape — the only way a user can write a literal * or `.
		if (C == TEXT('\\') && i + 1 < Len)
		{
			Pending.AppendChar(Line[i + 1]);
			i += 2;
			continue;
		}

		// Inline code first: its content is literal, so nothing inside is markup.
		if (C == TEXT('`'))
		{
			const int32 Close = FindClose(i + 1, TEXT("`"), 1);
			if (Close != INDEX_NONE)
			{
				Flush();
				FMarkdownInline Run;
				Run.Type = FMarkdownInline::EType::Code;
				Run.Text = Line.Mid(i + 1, Close - i - 1);
				Out.Add(MoveTemp(Run));
				i = Close + 1;
				continue;
			}
		}

		// [text](url)
		if (C == TEXT('['))
		{
			const int32 CloseBracket = FindClose(i + 1, TEXT("]"), 1);
			if (CloseBracket != INDEX_NONE
				&& CloseBracket + 1 < Len && Line[CloseBracket + 1] == TEXT('('))
			{
				const int32 CloseParen = FindClose(CloseBracket + 2, TEXT(")"), 1);
				if (CloseParen != INDEX_NONE)
				{
					Flush();
					FMarkdownInline Run;
					Run.Type = FMarkdownInline::EType::Link;
					Run.Text = Line.Mid(i + 1, CloseBracket - i - 1);
					Run.Url  = Line.Mid(CloseBracket + 2, CloseParen - CloseBracket - 2);
					Out.Add(MoveTemp(Run));
					i = CloseParen + 1;
					continue;
				}
			}
		}

		// ***bold italic*** / **bold** / *italic* / _italic_
		if (C == TEXT('*') || C == TEXT('_'))
		{
			const TCHAR Marker = C;
			int32 RunLen = 0;
			while (i + RunLen < Len && Line[i + RunLen] == Marker) { ++RunLen; }
			RunLen = FMath::Min(RunLen, 3);

			const FString Delim = FString::ChrN(RunLen, Marker);
			const int32 Close = FindClose(i + RunLen, *Delim, RunLen);
			if (Close != INDEX_NONE && Close > i + RunLen)
			{
				Flush();
				FMarkdownInline Run;
				Run.Type = (RunLen >= 3) ? FMarkdownInline::EType::BoldItalic
				         : (RunLen == 2) ? FMarkdownInline::EType::Bold
				                         : FMarkdownInline::EType::Italic;
				Run.Text = Line.Mid(i + RunLen, Close - i - RunLen);
				Out.Add(MoveTemp(Run));
				i = Close + RunLen;
				continue;
			}
		}

		// ~~strike~~
		if (C == TEXT('~') && i + 1 < Len && Line[i + 1] == TEXT('~'))
		{
			const int32 Close = FindClose(i + 2, TEXT("~~"), 2);
			if (Close != INDEX_NONE)
			{
				Flush();
				FMarkdownInline Run;
				Run.Type = FMarkdownInline::EType::Strike;
				Run.Text = Line.Mid(i + 2, Close - i - 2);
				Out.Add(MoveTemp(Run));
				i = Close + 2;
				continue;
			}
		}

		Pending.AppendChar(C);
		++i;
	}

	Flush();
	return Out;
}

// ============================================================================
// Unreal linkification
// ============================================================================

TArray<FMarkdownInline> FMCPMarkdownParser::LinkifyUnrealReferences(const TArray<FMarkdownInline>& In)
{
	TArray<FMarkdownInline> Out;
	Out.Reserve(In.Num());

	for (const FMarkdownInline& Run : In)
	{
		// Only plain text is scanned. Code spans stay literal (a path inside
		// backticks is being shown, not referenced) and links already have a target.
		if (Run.Type != FMarkdownInline::EType::Text)
		{
			Out.Add(Run);
			continue;
		}

		const FString& Text = Run.Text;
		int32 Cursor = 0;

		auto IsPathChar = [](TCHAR C)
		{
			return FChar::IsAlnum(C) || C == TEXT('_') || C == TEXT('/') || C == TEXT('.')
				|| C == TEXT('-') || C == TEXT(':');
		};

		while (Cursor < Text.Len())
		{
			// Find the next candidate start.
			int32 Start = INDEX_NONE;
			int32 MatchLen = 0;

			for (int32 i = Cursor; i < Text.Len(); ++i)
			{
				const bool bAtBoundary = (i == 0) || !FChar::IsAlnum(Text[i - 1]);
				if (!bAtBoundary) { continue; }

				// "unreal://" is 9 characters — Mid(i, 8) would never match.
				static const FString UnrealScheme = TEXT("unreal://");
				if (Text.Mid(i, UnrealScheme.Len()) == UnrealScheme)
				{
					Start = i; MatchLen = UnrealScheme.Len(); break;
				}
				if (Text[i] == TEXT('/'))
				{
					const FString Head = Text.Mid(i, 7);
					if (Head.StartsWith(TEXT("/Game/")) || Head.StartsWith(TEXT("/Engine/"))
						|| Head.StartsWith(TEXT("/Script/")))
					{
						Start = i; MatchLen = 1; break;
					}
				}
			}

			if (Start == INDEX_NONE)
			{
				AppendText(Out, Text.Mid(Cursor));
				break;
			}

			// Consume to the end of the reference.
			int32 End = Start + MatchLen;
			while (End < Text.Len() && IsPathChar(Text[End])) { ++End; }

			// Trailing punctuation belongs to the sentence, not the path.
			while (End > Start && (Text[End - 1] == TEXT('.') || Text[End - 1] == TEXT(',')
				|| Text[End - 1] == TEXT(':') || Text[End - 1] == TEXT(';')))
			{
				--End;
			}

			if (End - Start <= MatchLen)
			{
				// Nothing usable after the prefix — emit literally and move on.
				AppendText(Out, Text.Mid(Cursor, End - Cursor));
				Cursor = End;
				continue;
			}

			AppendText(Out, Text.Mid(Cursor, Start - Cursor));

			FMarkdownInline Ref;
			Ref.Type = FMarkdownInline::EType::UnrealRef;
			Ref.Text = Text.Mid(Start, End - Start);
			Ref.Url  = Ref.Text;
			Out.Add(MoveTemp(Ref));

			Cursor = End;
		}
	}

	return Out;
}

// ============================================================================
// Block-level helpers
// ============================================================================

bool FMCPMarkdownParser::TryParseFenceOpen(const FString& Line, FString& OutLanguage,
	int32& OutFenceLen, TCHAR& OutFenceChar)
{
	int32 First = 0;
	CountIndent(Line, First);
	if (First >= Line.Len()) { return false; }

	const TCHAR C = Line[First];
	if (C != TEXT('`') && C != TEXT('~')) { return false; }

	int32 Count = 0;
	while (First + Count < Line.Len() && Line[First + Count] == C) { ++Count; }
	if (Count < 3) { return false; }

	OutFenceChar = C;
	OutFenceLen  = Count;
	OutLanguage  = Line.Mid(First + Count).TrimStartAndEnd();

	// An info string can carry attributes ("cpp title=foo"); only the first word
	// is the language.
	int32 SpaceIdx = INDEX_NONE;
	if (OutLanguage.FindChar(TEXT(' '), SpaceIdx))
	{
		OutLanguage = OutLanguage.Left(SpaceIdx);
	}
	return true;
}

bool FMCPMarkdownParser::IsFenceClose(const FString& Line, int32 FenceLen, TCHAR FenceChar)
{
	int32 First = 0;
	CountIndent(Line, First);
	if (First >= Line.Len()) { return false; }
	if (Line[First] != FenceChar) { return false; }

	int32 Count = 0;
	while (First + Count < Line.Len() && Line[First + Count] == FenceChar) { ++Count; }
	if (Count < FenceLen) { return false; }

	return IsBlank(Line.Mid(First + Count));
}

bool FMCPMarkdownParser::IsHorizontalRule(const FString& Line)
{
	const FString T = Line.TrimStartAndEnd();
	if (T.Len() < 3) { return false; }

	const TCHAR C = T[0];
	if (C != TEXT('-') && C != TEXT('*') && C != TEXT('_')) { return false; }

	for (TCHAR Ch : T)
	{
		if (Ch != C && !FChar::IsWhitespace(Ch)) { return false; }
	}
	return true;
}

bool FMCPMarkdownParser::TryParseHeading(const FString& Line, int32& OutLevel, FString& OutText)
{
	int32 First = 0;
	CountIndent(Line, First);
	if (First >= Line.Len() || Line[First] != TEXT('#')) { return false; }

	int32 Level = 0;
	while (First + Level < Line.Len() && Line[First + Level] == TEXT('#')) { ++Level; }
	if (Level > 6) { return false; }

	// ATX headings require a space after the hashes; "#hashtag" is not a heading.
	if (First + Level >= Line.Len() || !FChar::IsWhitespace(Line[First + Level]))
	{
		return false;
	}

	OutLevel = Level;
	OutText  = Line.Mid(First + Level).TrimStartAndEnd();
	// Optional closing hashes.
	while (OutText.EndsWith(TEXT("#"))) { OutText.LeftChopInline(1); }
	OutText.TrimEndInline();
	return true;
}

bool FMCPMarkdownParser::TryParseListItem(const FString& Line, int32& OutDepth, bool& bOutOrdered,
	int32& OutIndex, int32& OutTaskState, FString& OutText)
{
	int32 First = 0;
	const int32 Indent = CountIndent(Line, First);
	if (First >= Line.Len()) { return false; }

	int32 ContentStart = INDEX_NONE;
	bOutOrdered = false;
	OutIndex = 0;

	const TCHAR C = Line[First];
	if ((C == TEXT('-') || C == TEXT('*') || C == TEXT('+'))
		&& First + 1 < Line.Len() && FChar::IsWhitespace(Line[First + 1]))
	{
		ContentStart = First + 2;
	}
	else if (FChar::IsDigit(C))
	{
		int32 j = First;
		while (j < Line.Len() && FChar::IsDigit(Line[j])) { ++j; }
		if (j < Line.Len() && (Line[j] == TEXT('.') || Line[j] == TEXT(')'))
			&& j + 1 < Line.Len() && FChar::IsWhitespace(Line[j + 1]))
		{
			bOutOrdered = true;
			OutIndex = FCString::Atoi(*Line.Mid(First, j - First));
			ContentStart = j + 2;
		}
	}

	if (ContentStart == INDEX_NONE) { return false; }

	OutDepth = Indent / 2;   // two spaces per level; matches what models emit
	OutText  = Line.Mid(ContentStart).TrimStartAndEnd();

	// Task list: "- [ ] thing" / "- [x] thing"
	OutTaskState = -1;
	if (OutText.Len() >= 3 && OutText[0] == TEXT('['))
	{
		if (OutText[2] == TEXT(']'))
		{
			const TCHAR Mark = OutText[1];
			if (Mark == TEXT(' '))                                    { OutTaskState = 0; }
			else if (Mark == TEXT('x') || Mark == TEXT('X'))          { OutTaskState = 1; }

			if (OutTaskState >= 0)
			{
				OutText = OutText.Mid(3).TrimStart();
			}
		}
	}
	return true;
}

bool FMCPMarkdownParser::TryParseTableDelimiterRow(const FString& Line)
{
	const FString T = Line.TrimStartAndEnd();
	if (!T.Contains(TEXT("|")) || !T.Contains(TEXT("-"))) { return false; }

	for (TCHAR C : T)
	{
		if (C != TEXT('|') && C != TEXT('-') && C != TEXT(':') && !FChar::IsWhitespace(C))
		{
			return false;
		}
	}
	return true;
}

TArray<FString> FMCPMarkdownParser::SplitTableRow(const FString& Line)
{
	FString T = Line.TrimStartAndEnd();
	if (T.StartsWith(TEXT("|"))) { T.RightChopInline(1); }
	if (T.EndsWith(TEXT("|")))   { T.LeftChopInline(1); }

	TArray<FString> Cells;
	T.ParseIntoArray(Cells, TEXT("|"), false);
	for (FString& Cell : Cells)
	{
		Cell.TrimStartAndEndInline();
	}
	return Cells;
}

bool FMCPMarkdownParser::LooksLikeUnifiedDiff(const FString& Text)
{
	// Require a hunk header — "---"/"+++" alone appear in plenty of non-diff text.
	return Text.Contains(TEXT("@@"))
		&& (Text.Contains(TEXT("\n+")) || Text.Contains(TEXT("\n-")));
}

// ============================================================================
// Block parse
// ============================================================================

FMarkdownDocument FMCPMarkdownParser::Parse(const FString& Source)
{
	FMarkdownDocument Doc;

	TArray<FString> Lines;
	Source.ParseIntoArray(Lines, TEXT("\n"), false);
	for (FString& Line : Lines)
	{
		Line.RemoveFromEnd(TEXT("\r"));
	}

	// Paragraph accumulator — consecutive non-blank lines join with a space so a
	// hard-wrapped paragraph reflows to the panel width instead of keeping the
	// model's arbitrary line breaks.
	TArray<FString> ParagraphLines;

	auto FlushParagraph = [&]()
	{
		if (ParagraphLines.Num() == 0) { return; }

		FMarkdownBlock Block;
		Block.Type = FMarkdownBlock::EType::Paragraph;
		Block.Inlines = LinkifyUnrealReferences(ParseInlines(FString::Join(ParagraphLines, TEXT(" "))));
		Doc.Blocks.Add(MoveTemp(Block));
		ParagraphLines.Reset();
	};

	for (int32 LineIdx = 0; LineIdx < Lines.Num(); ++LineIdx)
	{
		const FString& Line = Lines[LineIdx];

		// ---- Fenced code ----
		FString Language;
		int32   FenceLen = 0;
		TCHAR   FenceChar = TEXT('`');
		if (TryParseFenceOpen(Line, Language, FenceLen, FenceChar))
		{
			FlushParagraph();

			TArray<FString> CodeLines;
			int32 j = LineIdx + 1;
			for (; j < Lines.Num(); ++j)
			{
				if (IsFenceClose(Lines[j], FenceLen, FenceChar)) { break; }
				CodeLines.Add(Lines[j]);
			}

			FMarkdownBlock Block;
			Block.Type         = FMarkdownBlock::EType::CodeFence;
			Block.CodeLanguage = Language.ToLower();
			Block.CodeText     = FString::Join(CodeLines, TEXT("\n"));
			Block.bIsDiff      = (Block.CodeLanguage == TEXT("diff"))
			                     || (Block.CodeLanguage.IsEmpty() && LooksLikeUnifiedDiff(Block.CodeText));
			Doc.Blocks.Add(MoveTemp(Block));

			LineIdx = j;   // skip past the closing fence (or run to EOF)
			continue;
		}

		// ---- Blank ----
		if (IsBlank(Line))
		{
			FlushParagraph();
			continue;
		}

		// ---- Horizontal rule (before list: "---" is also a valid bullet char) ----
		if (IsHorizontalRule(Line))
		{
			FlushParagraph();
			FMarkdownBlock Block;
			Block.Type = FMarkdownBlock::EType::Rule;
			Doc.Blocks.Add(MoveTemp(Block));
			continue;
		}

		// ---- Heading ----
		int32 Level = 0;
		FString HeadingText;
		if (TryParseHeading(Line, Level, HeadingText))
		{
			FlushParagraph();
			FMarkdownBlock Block;
			Block.Type         = FMarkdownBlock::EType::Heading;
			Block.HeadingLevel = Level;
			Block.Inlines      = LinkifyUnrealReferences(ParseInlines(HeadingText));
			Doc.Blocks.Add(MoveTemp(Block));
			continue;
		}

		// ---- Table: header row followed by a delimiter row ----
		if (Line.Contains(TEXT("|")) && LineIdx + 1 < Lines.Num()
			&& TryParseTableDelimiterRow(Lines[LineIdx + 1]))
		{
			FlushParagraph();

			bool bHeader = true;
			int32 j = LineIdx;
			while (j < Lines.Num() && Lines[j].Contains(TEXT("|")))
			{
				if (TryParseTableDelimiterRow(Lines[j])) { ++j; continue; }

				FMarkdownBlock Row;
				Row.Type         = FMarkdownBlock::EType::TableRow;
				Row.bTableHeader = bHeader;
				for (const FString& Cell : SplitTableRow(Lines[j]))
				{
					Row.Cells.Add(LinkifyUnrealReferences(ParseInlines(Cell)));
				}
				Doc.Blocks.Add(MoveTemp(Row));

				bHeader = false;
				++j;
			}
			LineIdx = j - 1;
			continue;
		}

		// ---- Blockquote ----
		{
			int32 First = 0;
			CountIndent(Line, First);
			if (First < Line.Len() && Line[First] == TEXT('>'))
			{
				FlushParagraph();
				FString Text = Line.Mid(First + 1).TrimStart();

				FMarkdownBlock Block;
				Block.Type    = FMarkdownBlock::EType::Quote;
				Block.Inlines = LinkifyUnrealReferences(ParseInlines(Text));
				Doc.Blocks.Add(MoveTemp(Block));
				continue;
			}
		}

		// ---- List item ----
		int32 Depth = 0, OrderedIndex = 0, TaskState = -1;
		bool  bOrdered = false;
		FString ItemText;
		if (TryParseListItem(Line, Depth, bOrdered, OrderedIndex, TaskState, ItemText))
		{
			FlushParagraph();

			// Standalone image on a list line is still an image block; handled below
			// by the generic image check on the item text.
			FMarkdownBlock Block;
			Block.Type         = FMarkdownBlock::EType::ListItem;
			Block.ListDepth    = Depth;
			Block.bOrdered     = bOrdered;
			Block.OrderedIndex = OrderedIndex;
			Block.TaskState    = TaskState;
			Block.Inlines      = LinkifyUnrealReferences(ParseInlines(ItemText));
			Doc.Blocks.Add(MoveTemp(Block));
			continue;
		}

		// ---- Standalone image: ![alt](url) alone on a line ----
		{
			const FString T = Line.TrimStartAndEnd();
			if (T.StartsWith(TEXT("![")) && T.EndsWith(TEXT(")")))
			{
				int32 CloseBracket = INDEX_NONE;
				if (T.FindChar(TEXT(']'), CloseBracket)
					&& CloseBracket + 1 < T.Len() && T[CloseBracket + 1] == TEXT('('))
				{
					FlushParagraph();
					FMarkdownBlock Block;
					Block.Type     = FMarkdownBlock::EType::Image;
					Block.ImageAlt = T.Mid(2, CloseBracket - 2);
					Block.ImageUrl = T.Mid(CloseBracket + 2, T.Len() - CloseBracket - 3);
					Doc.Blocks.Add(MoveTemp(Block));
					continue;
				}
			}
		}

		// ---- Plain paragraph line ----
		ParagraphLines.Add(Line.TrimStart());
	}

	FlushParagraph();
	return Doc;
}
