// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SChatAttachmentStrip.h"
#include "MCPChatStyle.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SChatAttachmentStrip"

void SChatAttachmentStrip::Construct(const FArguments& InArgs)
{
	Attachments  = InArgs._Attachments;
	OnRemoved    = InArgs._OnRemoved;
	OnPinToggled = InArgs._OnPinToggled;

	ChildSlot
	[
		SNew(SBox)
		.Visibility(this, &SChatAttachmentStrip::GetStripVisibility)
		.Padding(0.f, 0.f, 0.f, FMCPChatStyle::Space(TEXT("xs")))
		[
			SAssignNew(Box, SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(FMCPChatStyle::Space(TEXT("xs")), FMCPChatStyle::Space(TEXT("xs"))))
		]
	];

	Refresh();
}

EVisibility SChatAttachmentStrip::GetStripVisibility() const
{
	return (Attachments && Attachments->Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed;
}

void SChatAttachmentStrip::Refresh()
{
	if (!Box.IsValid()) { return; }

	Box->ClearChildren();
	if (!Attachments) { return; }

	for (const FChatAttachmentPtr& A : *Attachments)
	{
		if (!A.IsValid()) { continue; }
		Box->AddSlot()[ BuildChip(A) ];
	}
}

FText SChatAttachmentStrip::GlyphFor(const FChatAttachment& Attachment)
{
	if (Attachment.IsImage()) { return FText::FromString(TEXT("▣")); }   // ▣

	if (Attachment.IsContext())
	{
		switch (Attachment.ContextKind)
		{
		case EChatContextKind::Asset:
		case EChatContextKind::Blueprint: return FText::FromString(TEXT("◆"));  // ◆
		case EChatContextKind::Actor:     return FText::FromString(TEXT("▲"));  // ▲
		case EChatContextKind::Selection: return FText::FromString(TEXT("■"));  // ■
		default:                          return FText::FromString(TEXT("●"));  // ●
		}
	}

	return FText::FromString(TEXT("▤"));   // ▤
}

TSharedRef<SWidget> SChatAttachmentStrip::BuildChip(const FChatAttachmentPtr& Attachment)
{
	const FGuid Id = Attachment->Id;
	const bool bHasError = !Attachment->Error.IsEmpty();

	// The chip's own tooltip carries the full path and the error text — a chip is
	// too small to say everything, but everything has to be reachable.
	FText Tooltip = FText::FromString(
		Attachment->AbsolutePath.IsEmpty() ? Attachment->ContextTarget : Attachment->AbsolutePath);
	if (bHasError) { Tooltip = Attachment->Error; }

	const FName BrushToken = bHasError ? TEXT("chipError")
		: (Attachment->bPinned ? TEXT("chipPinned") : TEXT("chip"));

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
	.Padding(0.f, 0.f, FMCPChatStyle::Space(TEXT("xs")), 0.f)
	[
		SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
		.ColorAndOpacity(FMCPChatStyle::Color(bHasError ? TEXT("readoutWarn") : TEXT("fgSubdued")))
		.Text(GlyphFor(*Attachment))
	];

	Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
		.Text(FText::FromString(Attachment->DisplayName))
		.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
	];

	Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
	.Padding(FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f, 0.f)
	[
		SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
		.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
		.Text(FText::FromString(Attachment->GetDetailText()))
	];

	// Pin is offered only for context chips: pinning a screenshot to every future
	// turn would quietly re-upload it forever.
	if (Attachment->IsContext() && OnPinToggled.IsBound())
	{
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
		.Padding(FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(0.f)
			.ToolTipText(Attachment->bPinned
				? LOCTEXT("UnpinTip", "Stop sending this with every message")
				: LOCTEXT("PinTip", "Send this with every message in this conversation"))
			.OnClicked_Lambda([this, Id]() { OnPinToggled.ExecuteIfBound(Id); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(Attachment->bPinned ? TEXT("accent") : TEXT("fgSubdued")))
				.Text(FText::FromString(TEXT("◎")))   // ◎
			]
		];
	}

	Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
	.Padding(FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f, 0.f)
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(0.f)
		.ToolTipText(LOCTEXT("RemoveTip", "Remove"))
		.OnClicked_Lambda([this, Id]() { OnRemoved.ExecuteIfBound(Id); return FReply::Handled(); })
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(FText::FromString(TEXT("×")))   // ×
		]
	];

	return SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(BrushToken))
		.Padding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
		.ToolTipText(Tooltip)
		[
			SNew(SBox).MaxDesiredWidth(FMCPChatStyle::Space(TEXT("chipMaxW"), 240.f))
			[
				Row
			]
		];
}

#undef LOCTEXT_NAMESPACE
