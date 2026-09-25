// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/SyntaxHighlighterTextLayoutMarshaller.h"
#include "Framework/Text/SyntaxTokenizer.h"
#include "Styling/SlateTypes.h"

/**
 * Phase 3 — syntax highlighting for fenced code blocks.
 *
 * Subclasses the engine's own FSyntaxHighlighterTextLayoutMarshaller (the base the
 * editor's text editors use) rather than writing a lexer framework. The tokenizer
 * matches keyword literals; ParseTokens layers on the stateful things a literal
 * matcher cannot do — strings, comments, numbers, preprocessor lines.
 *
 * Languages are data, not subclasses: a rule set is a keyword list plus a few
 * comment/string conventions, so adding one is a table entry.
 */
class FMCPCodeSyntaxHighlighter : public FSyntaxHighlighterTextLayoutMarshaller
{
public:
	/** Colours pulled from the active chat theme, so code follows the theme too. */
	struct FStyle
	{
		FTextBlockStyle Normal;
		FTextBlockStyle Keyword;
		FTextBlockStyle String;
		FTextBlockStyle Comment;
		FTextBlockStyle Number;
		FTextBlockStyle Preprocessor;
		FTextBlockStyle Operator;
	};

	/** Language id is the markdown fence info string, lowercased.
	 *  Unknown ids fall back to a plain (unhighlighted) marshaller — better than
	 *  colouring C++ keywords inside a YAML block. */
	static TSharedRef<FMCPCodeSyntaxHighlighter> Create(const FString& LanguageId);

	/** Languages with a rule set. Used by the code-block header and by tests. */
	static bool IsLanguageSupported(const FString& LanguageId);

	virtual ~FMCPCodeSyntaxHighlighter() override = default;

protected:
	virtual void ParseTokens(const FString& SourceString, FTextLayout& TargetTextLayout,
	                         TArray<FSyntaxTokenizer::FTokenizedLine> TokenizedLines) override;

private:
	/** Per-language conventions the literal tokenizer can't express. */
	struct FLanguageRules
	{
		FString LineCommentA;      // "//"  or "#"
		FString LineCommentB;      // secondary, e.g. ";" in ini
		FString BlockCommentOpen;  // "/*"
		FString BlockCommentClose; // "*/"
		bool    bDoubleQuoteStrings = true;
		bool    bSingleQuoteStrings = false;
		bool    bPreprocessorHash = false;   // '#' at line start is a directive, not a comment
	};

	FMCPCodeSyntaxHighlighter(TSharedPtr<FSyntaxTokenizer> InTokenizer, const FStyle& InStyle,
	                          const FLanguageRules& InRules);

	FStyle         Style;
	FLanguageRules Rules;

	/** Carried across lines so a multi-line /* … *\/ stays comment-coloured. */
	bool bInBlockComment = false;
};
