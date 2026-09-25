// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Markdown/MCPCodeSyntaxHighlighter.h"
#include "MCPChatStyle.h"

#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/TextLayout.h"
#include "Styling/CoreStyle.h"

namespace
{
	const TCHAR* CppKeywords[] = {
		TEXT("alignas"), TEXT("auto"), TEXT("bool"), TEXT("break"), TEXT("case"), TEXT("catch"),
		TEXT("char"), TEXT("class"), TEXT("const"), TEXT("constexpr"), TEXT("continue"),
		TEXT("decltype"), TEXT("default"), TEXT("delete"), TEXT("do"), TEXT("double"),
		TEXT("else"), TEXT("enum"), TEXT("explicit"), TEXT("extern"), TEXT("false"), TEXT("float"),
		TEXT("for"), TEXT("friend"), TEXT("if"), TEXT("inline"), TEXT("int"), TEXT("long"),
		TEXT("mutable"), TEXT("namespace"), TEXT("new"), TEXT("nullptr"), TEXT("operator"),
		TEXT("override"), TEXT("private"), TEXT("protected"), TEXT("public"), TEXT("return"),
		TEXT("short"), TEXT("signed"), TEXT("sizeof"), TEXT("static"), TEXT("struct"),
		TEXT("switch"), TEXT("template"), TEXT("this"), TEXT("throw"), TEXT("true"), TEXT("try"),
		TEXT("typedef"), TEXT("typename"), TEXT("union"), TEXT("unsigned"), TEXT("using"),
		TEXT("virtual"), TEXT("void"), TEXT("while"),
		// Unreal vocabulary — these are what actually appear in plugin code.
		TEXT("UCLASS"), TEXT("USTRUCT"), TEXT("UENUM"), TEXT("UFUNCTION"), TEXT("UPROPERTY"),
		TEXT("GENERATED_BODY"), TEXT("TSharedPtr"), TEXT("TSharedRef"), TEXT("TArray"),
		TEXT("TMap"), TEXT("TSet"), TEXT("FString"), TEXT("FName"), TEXT("FText"),
		TEXT("UObject"), TEXT("AActor"), TEXT("check"), TEXT("ensure"),
	};

	const TCHAR* PythonKeywords[] = {
		TEXT("and"), TEXT("as"), TEXT("assert"), TEXT("async"), TEXT("await"), TEXT("break"),
		TEXT("class"), TEXT("continue"), TEXT("def"), TEXT("del"), TEXT("elif"), TEXT("else"),
		TEXT("except"), TEXT("False"), TEXT("finally"), TEXT("for"), TEXT("from"), TEXT("global"),
		TEXT("if"), TEXT("import"), TEXT("in"), TEXT("is"), TEXT("lambda"), TEXT("None"),
		TEXT("nonlocal"), TEXT("not"), TEXT("or"), TEXT("pass"), TEXT("raise"), TEXT("return"),
		TEXT("True"), TEXT("try"), TEXT("while"), TEXT("with"), TEXT("yield"),
	};

	const TCHAR* JsonKeywords[] = { TEXT("true"), TEXT("false"), TEXT("null") };

	const TCHAR* HlslKeywords[] = {
		TEXT("float"), TEXT("float2"), TEXT("float3"), TEXT("float4"), TEXT("half"),
		TEXT("int"), TEXT("uint"), TEXT("bool"), TEXT("void"), TEXT("return"), TEXT("if"),
		TEXT("else"), TEXT("for"), TEXT("while"), TEXT("struct"), TEXT("cbuffer"),
		TEXT("Texture2D"), TEXT("SamplerState"), TEXT("float4x4"), TEXT("saturate"),
		TEXT("lerp"), TEXT("dot"), TEXT("normalize"), TEXT("clamp"), TEXT("pow"),
	};

	const TCHAR* BashKeywords[] = {
		TEXT("if"), TEXT("then"), TEXT("else"), TEXT("elif"), TEXT("fi"), TEXT("for"),
		TEXT("while"), TEXT("do"), TEXT("done"), TEXT("case"), TEXT("esac"), TEXT("function"),
		TEXT("return"), TEXT("export"), TEXT("local"), TEXT("echo"), TEXT("cd"), TEXT("sudo"),
	};

	template <int32 N>
	void AddRules(TArray<FSyntaxTokenizer::FRule>& Out, const TCHAR* const (&Keywords)[N])
	{
		for (int32 i = 0; i < N; ++i)
		{
			Out.Add(FSyntaxTokenizer::FRule(Keywords[i]));
		}
	}

	bool IsIdentChar(TCHAR C)
	{
		return FChar::IsAlnum(C) || C == TEXT('_');
	}
}

// ============================================================================
// Construction
// ============================================================================

FMCPCodeSyntaxHighlighter::FMCPCodeSyntaxHighlighter(TSharedPtr<FSyntaxTokenizer> InTokenizer,
	const FStyle& InStyle, const FLanguageRules& InRules)
	: FSyntaxHighlighterTextLayoutMarshaller(InTokenizer)
	, Style(InStyle)
	, Rules(InRules)
{
}

bool FMCPCodeSyntaxHighlighter::IsLanguageSupported(const FString& LanguageId)
{
	static const TSet<FString> Supported = {
		TEXT("cpp"), TEXT("c++"), TEXT("c"), TEXT("h"), TEXT("hpp"),
		TEXT("python"), TEXT("py"),
		TEXT("json"),
		TEXT("hlsl"), TEXT("usf"), TEXT("ush"),
		TEXT("ini"), TEXT("cfg"), TEXT("toml"),
		TEXT("bash"), TEXT("sh"), TEXT("shell"), TEXT("zsh"),
	};
	return Supported.Contains(LanguageId.ToLower());
}

TSharedRef<FMCPCodeSyntaxHighlighter> FMCPCodeSyntaxHighlighter::Create(const FString& LanguageId)
{
	const FString Lang = LanguageId.ToLower();

	// Styles come from the chat theme, so a user theme restyles code too.
	FStyle S;
	const FSlateFontInfo MonoFont = FMCPChatStyle::MonoFont();

	auto MakeStyle = [&MonoFont](const FSlateColor& Colour)
	{
		return FTextBlockStyle(FTextBlockStyle::GetDefault())
			.SetFont(MonoFont)
			.SetColorAndOpacity(Colour);
	};

	S.Normal       = MakeStyle(FMCPChatStyle::Color(TEXT("codeNormal")));
	S.Keyword      = MakeStyle(FMCPChatStyle::Color(TEXT("codeKeyword")));
	S.String       = MakeStyle(FMCPChatStyle::Color(TEXT("codeString")));
	S.Comment      = MakeStyle(FMCPChatStyle::Color(TEXT("codeComment")));
	S.Number       = MakeStyle(FMCPChatStyle::Color(TEXT("codeNumber")));
	S.Preprocessor = MakeStyle(FMCPChatStyle::Color(TEXT("codePreproc")));
	S.Operator     = MakeStyle(FMCPChatStyle::Color(TEXT("codeOperator")));

	TArray<FSyntaxTokenizer::FRule> Rules;
	FLanguageRules Lang_;

	if (Lang == TEXT("cpp") || Lang == TEXT("c++") || Lang == TEXT("c")
		|| Lang == TEXT("h") || Lang == TEXT("hpp"))
	{
		AddRules(Rules, CppKeywords);
		Lang_.LineCommentA      = TEXT("//");
		Lang_.BlockCommentOpen  = TEXT("/*");
		Lang_.BlockCommentClose = TEXT("*/");
		Lang_.bSingleQuoteStrings = true;
		Lang_.bPreprocessorHash   = true;
	}
	else if (Lang == TEXT("python") || Lang == TEXT("py"))
	{
		AddRules(Rules, PythonKeywords);
		Lang_.LineCommentA        = TEXT("#");
		Lang_.bSingleQuoteStrings = true;
	}
	else if (Lang == TEXT("json"))
	{
		AddRules(Rules, JsonKeywords);
		// JSON has no comments; leaving the fields empty disables that scanning.
	}
	else if (Lang == TEXT("hlsl") || Lang == TEXT("usf") || Lang == TEXT("ush"))
	{
		AddRules(Rules, HlslKeywords);
		Lang_.LineCommentA      = TEXT("//");
		Lang_.BlockCommentOpen  = TEXT("/*");
		Lang_.BlockCommentClose = TEXT("*/");
		Lang_.bPreprocessorHash = true;
	}
	else if (Lang == TEXT("ini") || Lang == TEXT("cfg") || Lang == TEXT("toml"))
	{
		Lang_.LineCommentA = TEXT(";");
		Lang_.LineCommentB = TEXT("#");
	}
	else if (Lang == TEXT("bash") || Lang == TEXT("sh") || Lang == TEXT("shell") || Lang == TEXT("zsh"))
	{
		AddRules(Rules, BashKeywords);
		Lang_.LineCommentA        = TEXT("#");
		Lang_.bSingleQuoteStrings = true;
	}
	// else: no rules and no comment conventions — everything renders as Normal,
	// which is the honest outcome for an unknown language.

	return MakeShareable(new FMCPCodeSyntaxHighlighter(
		FSyntaxTokenizer::Create(Rules), S, Lang_));
}

// ============================================================================
// Tokenising
// ============================================================================

void FMCPCodeSyntaxHighlighter::ParseTokens(const FString& SourceString, FTextLayout& TargetTextLayout,
	TArray<FSyntaxTokenizer::FTokenizedLine> TokenizedLines)
{
	TArray<FTextLayout::FNewLineData> LinesToAdd;
	LinesToAdd.Reserve(TokenizedLines.Num());

	bInBlockComment = false;

	for (const FSyntaxTokenizer::FTokenizedLine& TokenizedLine : TokenizedLines)
	{
		TSharedRef<FString> ModelString = MakeShared<FString>();
		TArray<TSharedRef<IRun>> Runs;

		// Emit a styled run for [Start, End) of the source line.
		auto EmitRun = [&](int32 Start, int32 End, const FTextBlockStyle& RunStyle)
		{
			if (End <= Start) { return; }

			const FString Fragment = SourceString.Mid(Start, End - Start);
			const int32 ModelOffset = ModelString->Len();
			ModelString->Append(Fragment);

			FRunInfo RunInfo(TEXT("SyntaxHighlight"));
			Runs.Add(FSlateTextRun::Create(RunInfo, ModelString, RunStyle,
				FTextRange(ModelOffset, ModelOffset + Fragment.Len())));
		};

		for (const FSyntaxTokenizer::FToken& Token : TokenizedLine.Tokens)
		{
			const int32 TokenStart = Token.Range.BeginIndex;
			const int32 TokenEnd   = Token.Range.EndIndex;

			// A Syntax token is a keyword the tokenizer matched. Guard against a
			// keyword that is really part of a longer identifier ("interface" in
			// "interfaces") by checking the surrounding characters.
			if (Token.Type == FSyntaxTokenizer::ETokenType::Syntax && !bInBlockComment)
			{
				const bool bLeftOk  = (TokenStart == 0) || !IsIdentChar(SourceString[TokenStart - 1]);
				const bool bRightOk = (TokenEnd >= SourceString.Len()) || !IsIdentChar(SourceString[TokenEnd]);
				if (bLeftOk && bRightOk)
				{
					EmitRun(TokenStart, TokenEnd, Style.Keyword);
					continue;
				}
			}

			// Literal span: scan for comments, strings and numbers.
			int32 Cursor = TokenStart;
			int32 PlainStart = TokenStart;

			auto FlushPlain = [&](int32 UpTo)
			{
				EmitRun(PlainStart, UpTo, bInBlockComment ? Style.Comment : Style.Normal);
			};

			while (Cursor < TokenEnd)
			{
				// --- inside a block comment: look only for the terminator ---
				if (bInBlockComment)
				{
					if (!Rules.BlockCommentClose.IsEmpty()
						&& SourceString.Mid(Cursor, Rules.BlockCommentClose.Len()) == Rules.BlockCommentClose)
					{
						Cursor += Rules.BlockCommentClose.Len();
						FlushPlain(Cursor);
						PlainStart = Cursor;
						bInBlockComment = false;
						continue;
					}
					++Cursor;
					continue;
				}

				// --- block comment open ---
				if (!Rules.BlockCommentOpen.IsEmpty()
					&& SourceString.Mid(Cursor, Rules.BlockCommentOpen.Len()) == Rules.BlockCommentOpen)
				{
					FlushPlain(Cursor);
					PlainStart = Cursor;
					bInBlockComment = true;
					Cursor += Rules.BlockCommentOpen.Len();
					continue;
				}

				// --- line comment: consumes the rest of the line ---
				const bool bLineA = !Rules.LineCommentA.IsEmpty()
					&& SourceString.Mid(Cursor, Rules.LineCommentA.Len()) == Rules.LineCommentA;
				const bool bLineB = !Rules.LineCommentB.IsEmpty()
					&& SourceString.Mid(Cursor, Rules.LineCommentB.Len()) == Rules.LineCommentB;

				if (bLineA || bLineB)
				{
					// '#' at the start of a C-family line is a preprocessor directive,
					// not a comment.
					const bool bIsPreproc = Rules.bPreprocessorHash && SourceString[Cursor] == TEXT('#');
					FlushPlain(Cursor);
					EmitRun(Cursor, TokenEnd, bIsPreproc ? Style.Preprocessor : Style.Comment);
					PlainStart = TokenEnd;
					Cursor = TokenEnd;
					break;
				}

				// --- string literal ---
				const TCHAR C = SourceString[Cursor];
				const bool bStringStart =
					(Rules.bDoubleQuoteStrings && C == TEXT('"')) ||
					(Rules.bSingleQuoteStrings && C == TEXT('\''));

				if (bStringStart)
				{
					FlushPlain(Cursor);

					const TCHAR Quote = C;
					int32 Scan = Cursor + 1;
					while (Scan < TokenEnd)
					{
						if (SourceString[Scan] == TEXT('\\')) { Scan += 2; continue; }
						if (SourceString[Scan] == Quote)      { ++Scan; break; }
						++Scan;
					}
					EmitRun(Cursor, FMath::Min(Scan, TokenEnd), Style.String);
					Cursor = FMath::Min(Scan, TokenEnd);
					PlainStart = Cursor;
					continue;
				}

				// --- number ---
				if (FChar::IsDigit(C)
					&& (Cursor == 0 || !IsIdentChar(SourceString[Cursor - 1])))
				{
					FlushPlain(Cursor);
					int32 Scan = Cursor;
					while (Scan < TokenEnd
						&& (FChar::IsDigit(SourceString[Scan]) || SourceString[Scan] == TEXT('.')
							|| SourceString[Scan] == TEXT('x') || SourceString[Scan] == TEXT('f')
							|| FChar::IsHexDigit(SourceString[Scan])))
					{
						++Scan;
					}
					EmitRun(Cursor, Scan, Style.Number);
					Cursor = Scan;
					PlainStart = Cursor;
					continue;
				}

				++Cursor;
			}

			FlushPlain(TokenEnd);
		}

		// A blank source line still needs a run, or the layout collapses it.
		if (Runs.Num() == 0)
		{
			Runs.Add(FSlateTextRun::Create(FRunInfo(TEXT("SyntaxHighlight")), ModelString,
				Style.Normal, FTextRange(0, 0)));
		}

		LinesToAdd.Emplace(MoveTemp(ModelString), MoveTemp(Runs));
	}

	TargetTextLayout.AddLines(LinesToAdd);
}
