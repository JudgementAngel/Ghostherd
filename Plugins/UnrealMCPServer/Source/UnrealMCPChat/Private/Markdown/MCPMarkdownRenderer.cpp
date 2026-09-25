// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Markdown/MCPMarkdownRenderer.h"
#include "MCPChatStyle.h"
#include "UI/SChatCodeBlock.h"
#include "UI/SChatImageBlock.h"
#include "UnrealMCPChatModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "IContentBrowserSingleton.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MCPMarkdownRenderer"

namespace
{
	const TCHAR* StyleNameForInline(FMarkdownInline::EType Type)
	{
		switch (Type)
		{
		case FMarkdownInline::EType::Bold:       return TEXT("Chat.Md.Bold");
		case FMarkdownInline::EType::Italic:     return TEXT("Chat.Md.Italic");
		case FMarkdownInline::EType::BoldItalic: return TEXT("Chat.Md.BoldItalic");
		case FMarkdownInline::EType::Code:       return TEXT("Chat.Md.Code");
		case FMarkdownInline::EType::Strike:     return TEXT("Chat.Md.Strike");
		case FMarkdownInline::EType::Text:
		default:                                 return TEXT("Chat.Md.Normal");
		}
	}
}

// ============================================================================
// Rich-text markup emission
// ============================================================================

FString FMCPMarkdownRenderer::EscapeForRichText(const FString& In)
{
	// See the header for why this is the only substitution needed. A zero-width
	// space keeps the glyphs the user typed while breaking the parser's close tag.
	if (!In.Contains(TEXT("</>")))
	{
		return In;
	}
	return In.Replace(TEXT("</>"), TEXT("</​>"), ESearchCase::CaseSensitive);
}

FString FMCPMarkdownRenderer::InlinesToRichText(const TArray<FMarkdownInline>& Inlines)
{
	FString Out;
	Out.Reserve(256);

	for (const FMarkdownInline& Run : Inlines)
	{
		const FString Safe = EscapeForRichText(Run.Text);

		switch (Run.Type)
		{
		case FMarkdownInline::EType::Link:
		case FMarkdownInline::EType::UnrealRef:
		{
			// FHyperlinkDecorator::Supports() requires the tag name to be "a" AND the
			// run's `id` metadata to equal the decorator's own id — so `id` must be
			// the literal "a" we register the decorator with, not a style name. The
			// `style` attribute is looked up as an FHyperlinkStyle (not FTextBlockStyle)
			// in the decorator style set. Getting either wrong makes links render as
			// plain text with no click handler, silently.
			FString Href = Run.Url;
			Href.ReplaceInline(TEXT("\""), TEXT("%22"), ESearchCase::CaseSensitive);

			const TCHAR* LinkStyle = (Run.Type == FMarkdownInline::EType::UnrealRef)
				? TEXT("Chat.Md.UnrealRefLink") : TEXT("Chat.Md.Hyperlink");

			Out += FString::Printf(TEXT("<a id=\"a\" href=\"%s\" style=\"%s\">%s</>"),
				*Href, LinkStyle, *Safe);
			break;
		}

		default:
			Out += FString::Printf(TEXT("<%s>%s</>"), StyleNameForInline(Run.Type), *Safe);
			break;
		}
	}
	return Out;
}

// ============================================================================
// Link handling
// ============================================================================

void FMCPMarkdownRenderer::HandleLinkClicked(const FString& Href)
{
	if (Href.IsEmpty()) { return; }

	if (Href.StartsWith(TEXT("http://")) || Href.StartsWith(TEXT("https://")))
	{
		FPlatformProcess::LaunchURL(*Href, nullptr, nullptr);
		return;
	}

	if (Href.StartsWith(TEXT("unreal://")))
	{
		// Phase 6 wires these to the resource providers (selection, viewport,
		// level analysis). Logged rather than silently ignored so the gap is visible.
		UE_LOG(LogUnrealMCPChat, Log,
			TEXT("Resource link '%s' clicked — resource navigation lands in Phase 6."), *Href);
		return;
	}

	if (Href.StartsWith(TEXT("/Game/")) || Href.StartsWith(TEXT("/Engine/")) || Href.StartsWith(TEXT("/Script/")))
	{
		// Sync the Content Browser rather than opening the editor: selecting is
		// reversible and cheap, opening a heavy asset from a stray click is not.
		const FAssetRegistryModule& Registry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		FString ObjectPath = Href;
		// "/Game/Foo/Bar" → "/Game/Foo/Bar.Bar" when no object name is present.
		if (!ObjectPath.Contains(TEXT(".")))
		{
			FString Left, Right;
			if (ObjectPath.Split(TEXT("/"), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			{
				ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Left, *Right, *Right);
			}
		}

		const FAssetData Asset = Registry.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
		if (!Asset.IsValid())
		{
			UE_LOG(LogUnrealMCPChat, Verbose, TEXT("No asset found at '%s'."), *Href);
			return;
		}

		FContentBrowserModule& ContentBrowser =
			FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		ContentBrowser.Get().SyncBrowserToAssets({ Asset });
		return;
	}

	UE_LOG(LogUnrealMCPChat, Verbose, TEXT("Unhandled link target '%s'."), *Href);
}

// ============================================================================
// Block builders
// ============================================================================

TSharedRef<SWidget> FMCPMarkdownRenderer::BuildParagraph(const FMarkdownBlock& Block, const FString& StyleName)
{
	return SNew(SRichTextBlock)
		.Text(FText::FromString(InlinesToRichText(Block.Inlines)))
		.TextStyle(&FMCPChatStyle::TextStyle(FName(*StyleName)))
		.DecoratorStyleSet(&FMCPChatStyle::Get())
		.AutoWrapText(true)
		+ SRichTextBlock::HyperlinkDecorator(TEXT("a"),
			FSlateHyperlinkRun::FOnClick::CreateLambda([](const FSlateHyperlinkRun::FMetadata& Metadata)
			{
				if (const FString* Href = Metadata.Find(TEXT("href")))
				{
					FMCPMarkdownRenderer::HandleLinkClicked(*Href);
				}
			}));
}

TSharedRef<SWidget> FMCPMarkdownRenderer::BuildHeading(const FMarkdownBlock& Block)
{
	// Clamp to three levels: a chat message with an h5 is a formatting accident,
	// and four visually distinct heading sizes in a 400px column is noise.
	const int32 Level = FMath::Clamp(Block.HeadingLevel, 1, 3);
	const FString StyleName = FString::Printf(TEXT("Chat.Text.H%d"), Level);

	return SNew(SBox)
		.Padding(0.f, FMCPChatStyle::Space(TEXT("sm")), 0.f, FMCPChatStyle::Space(TEXT("xs")))
		[
			BuildParagraph(Block, StyleName)
		];
}

TSharedRef<SWidget> FMCPMarkdownRenderer::BuildListItem(const FMarkdownBlock& Block)
{
	FString Marker;
	if (Block.TaskState >= 0)
	{
		Marker = (Block.TaskState == 1) ? TEXT("☑") : TEXT("☐");
	}
	else if (Block.bOrdered)
	{
		Marker = FString::Printf(TEXT("%d."), Block.OrderedIndex);
	}
	else
	{
		// Vary the glyph by depth so nesting reads without relying on indent alone.
		static const TCHAR* Bullets[] = { TEXT("•"), TEXT("◦"), TEXT("▪") };
		Marker = Bullets[FMath::Clamp(Block.ListDepth, 0, 2)];
	}

	const float Indent = FMCPChatStyle::Space(TEXT("md")) * FMath::Clamp(Block.ListDepth, 0, 6);

	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
		.Padding(Indent, 0.f, FMCPChatStyle::Space(TEXT("sm")), 0.f)
		[
			SNew(SBox)
			.MinDesiredWidth(FMCPChatStyle::Space(TEXT("md")))
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
				.Text(FText::FromString(Marker))
			]
		]

		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			BuildParagraph(Block, TEXT("Chat.Text.Body"))
		];
}

TSharedRef<SWidget> FMCPMarkdownRenderer::BuildQuote(const FMarkdownBlock& Block)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(2.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FMCPChatStyle::Color(TEXT("fgSubdued")))
			]
		]

		+ SHorizontalBox::Slot().FillWidth(1.f)
		.Padding(FMCPChatStyle::Space(TEXT("sm")), 0.f, 0.f, 0.f)
		[
			BuildParagraph(Block, TEXT("Chat.Text.Small"))
		];
}

TSharedRef<SWidget> FMCPMarkdownRenderer::BuildRule()
{
	return SNew(SBox)
		.Padding(0.f, FMCPChatStyle::Space(TEXT("sm")))
		[
			SNew(SSeparator).Thickness(1.f)
		];
}

TSharedRef<SWidget> FMCPMarkdownRenderer::BuildTable(const TArray<FMarkdownBlock>& Rows)
{
	TSharedRef<SGridPanel> Grid = SNew(SGridPanel);

	for (int32 R = 0; R < Rows.Num(); ++R)
	{
		const FMarkdownBlock& Row = Rows[R];
		for (int32 C = 0; C < Row.Cells.Num(); ++C)
		{
			FMarkdownBlock CellBlock;
			CellBlock.Inlines = Row.Cells[C];

			Grid->AddSlot(C, R)
				.Padding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
				[
					BuildParagraph(CellBlock, Row.bTableHeader ? TEXT("Chat.Text.Bold") : TEXT("Chat.Text.Body"))
				];
		}
	}

	// Tables scroll rather than squeeze — a squeezed table is unreadable, and the
	// panel body must never scroll horizontally itself (03 §2).
	return SNew(SBox)
		.Padding(0.f, FMCPChatStyle::Space(TEXT("xs")))
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			+ SScrollBox::Slot() [ Grid ]
		];
}

// ============================================================================
// Document
// ============================================================================

TSharedRef<SWidget> FMCPMarkdownRenderer::BuildDocument(const FMarkdownDocument& Doc, float MaxImageWidth)
{
	TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

	const float BlockGap = FMCPChatStyle::Space(TEXT("xs"));

	for (int32 i = 0; i < Doc.Blocks.Num(); ++i)
	{
		const FMarkdownBlock& Block = Doc.Blocks[i];

		// Table rows arrive as consecutive blocks; gather the run into one grid.
		if (Block.Type == FMarkdownBlock::EType::TableRow)
		{
			TArray<FMarkdownBlock> Rows;
			int32 j = i;
			while (j < Doc.Blocks.Num() && Doc.Blocks[j].Type == FMarkdownBlock::EType::TableRow)
			{
				Rows.Add(Doc.Blocks[j]);
				++j;
			}
			Root->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, BlockGap)
			[
				BuildTable(Rows)
			];
			i = j - 1;
			continue;
		}

		TSharedPtr<SWidget> Widget;
		switch (Block.Type)
		{
		case FMarkdownBlock::EType::Paragraph:
			Widget = BuildParagraph(Block, TEXT("Chat.Text.Body"));
			break;

		case FMarkdownBlock::EType::Heading:
			Widget = BuildHeading(Block);
			break;

		case FMarkdownBlock::EType::CodeFence:
			Widget = SNew(SChatCodeBlock)
				.Code(Block.CodeText)
				.Language(Block.CodeLanguage)
				.IsDiff(Block.bIsDiff);
			break;

		case FMarkdownBlock::EType::ListItem:
			Widget = BuildListItem(Block);
			break;

		case FMarkdownBlock::EType::Quote:
			Widget = BuildQuote(Block);
			break;

		case FMarkdownBlock::EType::Rule:
			Widget = BuildRule();
			break;

		case FMarkdownBlock::EType::Image:
			Widget = SNew(SChatImageBlock)
				.Source(Block.ImageUrl)
				.AltText(Block.ImageAlt)
				.MaxDisplayWidth(MaxImageWidth);
			break;

		default:
			break;
		}

		if (Widget.IsValid())
		{
			Root->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, BlockGap)
			[
				Widget.ToSharedRef()
			];
		}
	}

	return Root;
}

TSharedRef<SWidget> FMCPMarkdownRenderer::BuildFromMarkdown(const FString& Source, float MaxImageWidth)
{
	return BuildDocument(FMCPMarkdownParser::Parse(Source), MaxImageWidth);
}

#undef LOCTEXT_NAMESPACE
