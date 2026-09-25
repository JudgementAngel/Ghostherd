// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SChatCodeBlock.h"
#include "Markdown/MCPCodeSyntaxHighlighter.h"
#include "MCPChatStyle.h"
#include "UnrealMCPChatModule.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDesktopPlatform.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SChatCodeBlock"

void SChatCodeBlock::Construct(const FArguments& InArgs)
{
	Code     = InArgs._Code;
	Language = InArgs._Language;
	bIsDiff  = InArgs._IsDiff;

	LineCount = 1;
	for (TCHAR C : Code)
	{
		if (C == TEXT('\n')) { ++LineCount; }
	}
	bCollapsed = (LineCount > CollapseThresholdLines);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("codeBlock")))
		.Padding(0.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildHeader()
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.MaxDesiredHeight(this, &SChatCodeBlock::GetBodyMaxHeight)
				[
					bIsDiff ? BuildDiffBody() : BuildBody()
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.HAlign(HAlign_Center)
				.Visibility(this, &SChatCodeBlock::GetToggleVisibility)
				.OnClicked(this, &SChatCodeBlock::OnToggleCollapsed)
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.Text(this, &SChatCodeBlock::GetToggleText)
				]
			]
		]
	];
}

TSharedRef<SWidget> SChatCodeBlock::BuildHeader()
{
	const FString LangLabel = Language.IsEmpty()
		? (bIsDiff ? TEXT("diff") : TEXT("text"))
		: Language;

	return SNew(SBox)
		.Padding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.Text(FText::FromString(LangLabel))
			]

			+ SHorizontalBox::Slot().FillWidth(1.f) [ SNullWidget::NullWidget ]

			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("CopyCode", "Copy this block"))
				.OnClicked(this, &SChatCodeBlock::OnCopyClicked)
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.Text(LOCTEXT("Copy", "Copy"))
				]
			]

			+ SHorizontalBox::Slot().AutoWidth()
			.Padding(FMCPChatStyle::Space(TEXT("sm")), 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("SaveCode", "Save this block to a file"))
				.OnClicked(this, &SChatCodeBlock::OnSaveClicked)
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.Text(LOCTEXT("Save", "Save"))
				]
			]
		];
}

TSharedRef<SWidget> SChatCodeBlock::BuildBody()
{
	// Horizontal scroll, never wrap: wrapped code is unreadable and misleading
	// about line structure.
	return SNew(SScrollBox)
		.Orientation(Orient_Horizontal)

		+ SScrollBox::Slot()
		.Padding(FMCPChatStyle::Padding(TEXT("sm")))
		[
			SNew(SMultiLineEditableText)
			.Text(FText::FromString(Code))
			.IsReadOnly(true)
			.AutoWrapText(false)
			.Marshaller(FMCPCodeSyntaxHighlighter::Create(Language))
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Mono")))
		];
}

TSharedRef<SWidget> SChatCodeBlock::BuildDiffBody()
{
	// Per-line colouring, so the +/- fills read at a glance. The prefix characters
	// are kept — colour must never be the only signal (03 §10).
	TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);

	TArray<FString> DiffLines;
	Code.ParseIntoArray(DiffLines, TEXT("\n"), false);

	for (const FString& Line : DiffLines)
	{
		FName FillToken = NAME_None;
		FName TextToken = TEXT("fg");

		if (Line.StartsWith(TEXT("+++")) || Line.StartsWith(TEXT("---")))
		{
			TextToken = TEXT("fgSubdued");           // file headers, not content
		}
		else if (Line.StartsWith(TEXT("@@")))
		{
			TextToken = TEXT("accent");              // hunk header
		}
		else if (Line.StartsWith(TEXT("+")))
		{
			FillToken = TEXT("diffAdd");
			TextToken = TEXT("success");
		}
		else if (Line.StartsWith(TEXT("-")))
		{
			FillToken = TEXT("diffDel");
			TextToken = TEXT("error");
		}

		TSharedRef<STextBlock> LineText = SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Mono")))
			.ColorAndOpacity(FMCPChatStyle::Color(TextToken))
			.Text(FText::FromString(Line.IsEmpty() ? TEXT(" ") : Line));

		if (FillToken.IsNone())
		{
			Lines->AddSlot().AutoHeight()
			[
				SNew(SBox).Padding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs"))) [ LineText ]
			];
		}
		else
		{
			Lines->AddSlot().AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FMCPChatStyle::Color(FillToken))
				.Padding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
				[ LineText ]
			];
		}
	}

	return SNew(SScrollBox)
		.Orientation(Orient_Horizontal)
		+ SScrollBox::Slot() [ Lines ];
}

// ============================================================================
// Actions
// ============================================================================

FReply SChatCodeBlock::OnCopyClicked()
{
	FPlatformApplicationMisc::ClipboardCopy(*Code);
	return FReply::Handled();
}

FReply SChatCodeBlock::OnSaveClicked()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop) { return FReply::Handled(); }

	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());

	// Default the extension from the fence language so "Save" lands on something
	// sensible without the user retyping it.
	FString Extension = TEXT("txt");
	const FString Lang = Language.ToLower();
	if (Lang == TEXT("cpp") || Lang == TEXT("c++"))       { Extension = TEXT("cpp"); }
	else if (Lang == TEXT("h") || Lang == TEXT("hpp"))    { Extension = TEXT("h"); }
	else if (Lang == TEXT("python") || Lang == TEXT("py")){ Extension = TEXT("py"); }
	else if (Lang == TEXT("json"))                        { Extension = TEXT("json"); }
	else if (Lang == TEXT("hlsl"))                        { Extension = TEXT("usf"); }
	else if (Lang == TEXT("ini"))                         { Extension = TEXT("ini"); }
	else if (Lang == TEXT("bash") || Lang == TEXT("sh"))  { Extension = TEXT("sh"); }

	TArray<FString> OutFiles;
	const bool bPicked = Desktop->SaveFileDialog(
		ParentWindow,
		LOCTEXT("SaveDialogTitle", "Save code block").ToString(),
		FPaths::ProjectDir(),
		FString::Printf(TEXT("snippet.%s"), *Extension),
		FString::Printf(TEXT("Code (*.%s)|*.%s|All files (*.*)|*.*"), *Extension, *Extension),
		EFileDialogFlags::None,
		OutFiles);

	if (bPicked && OutFiles.Num() > 0)
	{
		if (!FFileHelper::SaveStringToFile(Code, *OutFiles[0]))
		{
			UE_LOG(LogUnrealMCPChat, Error, TEXT("Could not write '%s'."), *OutFiles[0]);
		}
	}
	return FReply::Handled();
}

FReply SChatCodeBlock::OnToggleCollapsed()
{
	bCollapsed = !bCollapsed;
	Invalidate(EInvalidateWidgetReason::Layout);
	return FReply::Handled();
}

FText SChatCodeBlock::GetToggleText() const
{
	return bCollapsed
		? FText::Format(LOCTEXT("ShowAll", "Show all {0} lines"), FText::AsNumber(LineCount))
		: LOCTEXT("ShowLess", "Show less");
}

EVisibility SChatCodeBlock::GetToggleVisibility() const
{
	return (LineCount > CollapseThresholdLines) ? EVisibility::Visible : EVisibility::Collapsed;
}

FOptionalSize SChatCodeBlock::GetBodyMaxHeight() const
{
	if (!bCollapsed) { return FOptionalSize(); }

	// Derived from the theme's mono size so the preview stays ~20 lines whatever
	// the user set the font to.
	const float LineHeight = FMath::Max(8.f, static_cast<float>(FMCPChatStyle::MonoFont().Size) * 1.5f);
	return FOptionalSize(LineHeight * CollapsedPreviewLines);
}

#undef LOCTEXT_NAMESPACE
