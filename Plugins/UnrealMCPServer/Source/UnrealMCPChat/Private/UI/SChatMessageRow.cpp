// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SChatMessageRow.h"
#include "MCPChatController.h"
#include "MCPChatModelCatalog.h"
#include "MCPChatStore.h"
#include "MCPChatToolBridge.h"
#include "Markdown/MCPMarkdownRenderer.h"
#include "MCPChatStyle.h"
#include "UI/SChatImageBlock.h"
#include "UnrealMCPChatModule.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SChatMessageRow"

void SChatMessageRow::Construct(const FArguments& InArgs)
{
	Message    = InArgs._Message;
	Controller = InArgs._Controller;
	bIsLiveTail = InArgs._IsLiveTail;

	ChildSlot
	[
		SNew(SHorizontalBox)

		// Role rule — 2 px, full height. Cheaper to read than a boxed card and it
		// costs no horizontal space at the Narrow breakpoint.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(2.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(this, &SChatMessageRow::GetRuleColor)
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.Padding(FMCPChatStyle::Space(TEXT("md")), FMCPChatStyle::Space(TEXT("sm")),
		         FMCPChatStyle::Space(TEXT("md")), FMCPChatStyle::Space(TEXT("sm")))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildHeader()
			]

			+ SVerticalBox::Slot().AutoHeight()
			.Padding(0.f, FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f)
			[
				SAssignNew(BlockContainer, SVerticalBox)
			]

			+ SVerticalBox::Slot().AutoHeight()
			.Padding(0.f, FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.Text(this, &SChatMessageRow::GetUsageText)
				.Visibility(this, &SChatMessageRow::GetUsageVisibility)
			]
		]
	];

	RefreshContent();
}

// ============================================================================
// Header
// ============================================================================

TSharedRef<SWidget> SChatMessageRow::BuildHeader()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Bold")))
			.Text(this, &SChatMessageRow::GetRoleLabel)
		]

		// Sibling stepper — only present when a retry created a branch here.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		.Padding(FMCPChatStyle::Space(TEXT("sm")), 0.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SChatMessageRow::GetSiblingStepperVisibility)

			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("PrevBranch", "Previous version of this reply"))
				.OnClicked(this, &SChatMessageRow::OnPreviousSibling)
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.Text(FText::FromString(TEXT("‹")))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.Text(this, &SChatMessageRow::GetSiblingText)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("NextBranch", "Next version of this reply"))
				.OnClicked(this, &SChatMessageRow::OnNextSibling)
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.Text(FText::FromString(TEXT("›")))
				]
			]
		]

		+ SHorizontalBox::Slot().FillWidth(1.f) [ SNullWidget::NullWidget ]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SChatMessageRow::GetHoverActionsVisibility)

			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("CopyMessage", "Copy this message as Markdown"))
				.OnClicked(this, &SChatMessageRow::OnCopyClicked)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("GenericCommands.Copy"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("RetryMessage", "Generate another reply, keeping this one"))
				.Visibility_Lambda([this]()
				{
					return (Message.IsValid() && Message->Role == EChatRole::Assistant)
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				.OnClicked(this, &SChatMessageRow::OnRetryClicked)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Refresh"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		.Padding(FMCPChatStyle::Space(TEXT("sm")), 0.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.Text(this, &SChatMessageRow::GetTimestampText)
		];
}

// ============================================================================
// Blocks
// ============================================================================

void SChatMessageRow::RefreshContent()
{
	if (!BlockContainer.IsValid() || !Message.IsValid()) { return; }

	BlockContainer->ClearChildren();

	ThinkingExpanded.SetNumZeroed(Message->Blocks.Num());
	ToolExpanded.SetNumZeroed(Message->Blocks.Num());

	FMCPChatToolBridge& Bridge = FMCPChatToolBridge::Get();

	for (int32 i = 0; i < Message->Blocks.Num(); ++i)
	{
		const FChatContentBlock& B = Message->Blocks[i];
		TSharedPtr<SWidget> BlockWidget;

		// A single turn can emit thirty tool cards and bury the actual answer.
		// Runs of SUCCESSFUL READ-ONLY calls collapse into one summary row; anything
		// that failed, mutated, or is still waiting keeps its own card.
		if (B.Type == FChatContentBlock::EType::ToolCall && B.bIsComplete && !B.bToolIsError
			&& !B.bToolAwaitingApproval && Bridge.IsToolReadOnly(B.ToolName))
		{
			TArray<int32> Run;
			int32 j = i;
			while (j < Message->Blocks.Num())
			{
				const FChatContentBlock& Candidate = Message->Blocks[j];
				if (Candidate.Type != FChatContentBlock::EType::ToolCall
					|| !Candidate.bIsComplete || Candidate.bToolIsError
					|| Candidate.bToolAwaitingApproval || !Bridge.IsToolReadOnly(Candidate.ToolName))
				{
					break;
				}
				Run.Add(j);
				++j;
			}

			if (Run.Num() >= 3)   // two cards are not clutter; three start to be
			{
				BlockContainer->AddSlot().AutoHeight()
					.Padding(0.f, 0.f, 0.f, FMCPChatStyle::Space(TEXT("xs")))
					[
						BuildCollapsedToolSummary(Run)
					];
				i = j - 1;
				continue;
			}
		}

		switch (B.Type)
		{
		case FChatContentBlock::EType::Text:       BlockWidget = BuildTextBlock(i); break;
		case FChatContentBlock::EType::Thinking:   BlockWidget = BuildThinkingBlock(i); break;
		case FChatContentBlock::EType::ToolCall:   BlockWidget = BuildToolCard(i); break;
		case FChatContentBlock::EType::Refusal:    BlockWidget = BuildNoticeBlock(i, false); break;
		case FChatContentBlock::EType::Error:      BlockWidget = BuildNoticeBlock(i, true); break;
		case FChatContentBlock::EType::Image:
		case FChatContentBlock::EType::File:       BlockWidget = BuildAttachmentBlock(i); break;
		case FChatContentBlock::EType::ContextRef: BlockWidget = BuildContextChip(i); break;
		case FChatContentBlock::EType::Plan:       BlockWidget = BuildPlanBlock(i); break;
		case FChatContentBlock::EType::Divider:    BlockWidget = BuildDivider(i); break;
		case FChatContentBlock::EType::ToolResult: continue;  // folded into the call card
		}

		if (BlockWidget.IsValid())
		{
			BlockContainer->AddSlot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, FMCPChatStyle::Space(TEXT("xs")))
				[
					BlockWidget.ToSharedRef()
				];
		}
	}

	LastBlockCount = Message->Blocks.Num();
}

FText SChatMessageRow::GetBlockText(int32 BlockIndex) const
{
	if (!Message.IsValid() || !Message->Blocks.IsValidIndex(BlockIndex))
	{
		return FText::GetEmpty();
	}
	return FText::FromString(Message->Blocks[BlockIndex].Text);
}

TSharedRef<SWidget> SChatMessageRow::BuildTextBlock(int32 BlockIndex)
{
	if (!Message.IsValid() || !Message->Blocks.IsValidIndex(BlockIndex))
	{
		return SNullWidget::NullWidget;
	}

	// While a message is STREAMING we deliberately do NOT parse markdown:
	//
	//  - the text is syntactically incomplete by definition (a half-typed ``` fence
	//    or **bold would flip the whole remainder of the paragraph's styling on and
	//    off as tokens arrive, which looks broken)
	//  - parsing + rebuilding a widget tree per delta is exactly the per-token cost
	//    the live-tail split exists to avoid
	//
	// So: plain attribute-bound text while streaming, full markdown once committed.
	// The visual switch happens at the same moment the message moves from the live
	// tail into the list, so it reads as the reply "settling", not as a glitch.
	if (bIsLiveTail || Message->bIsStreaming)
	{
		return SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
			.Text(this, &SChatMessageRow::GetBlockText, BlockIndex)
			.AutoWrapText(true);
	}

	return FMCPMarkdownRenderer::BuildFromMarkdown(Message->Blocks[BlockIndex].Text);
}

TSharedRef<SWidget> SChatMessageRow::BuildThinkingBlock(int32 BlockIndex)
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.HAlign(HAlign_Left)
			.OnClicked_Lambda([this, BlockIndex]()
			{
				if (ThinkingExpanded.IsValidIndex(BlockIndex))
				{
					ThinkingExpanded[BlockIndex] = !ThinkingExpanded[BlockIndex];
					Invalidate(EInvalidateWidgetReason::Layout);
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.Text_Lambda([this, BlockIndex]()
				{
					const bool bOpen = ThinkingExpanded.IsValidIndex(BlockIndex) && ThinkingExpanded[BlockIndex];
					return FText::Format(LOCTEXT("ThinkingToggle", "{0} Thinking"),
						FText::FromString(bOpen ? TEXT("▾") : TEXT("▸")));
				})
			]
		]

		+ SVerticalBox::Slot().AutoHeight()
		.Padding(FMCPChatStyle::Space(TEXT("md")), 0.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.Text(this, &SChatMessageRow::GetBlockText, BlockIndex)
			.AutoWrapText(true)
			.Visibility_Lambda([this, BlockIndex]()
			{
				return (ThinkingExpanded.IsValidIndex(BlockIndex) && ThinkingExpanded[BlockIndex])
					? EVisibility::Visible : EVisibility::Collapsed;
			})
		];
}

TSharedRef<SWidget> SChatMessageRow::BuildToolCard(int32 BlockIndex)
{
	if (!Message.IsValid() || !Message->Blocks.IsValidIndex(BlockIndex))
	{
		return SNullWidget::NullWidget;
	}

	const FChatContentBlock& B = Message->Blocks[BlockIndex];

	// Errors and pending calls open themselves — those are the states a user needs
	// to see without hunting for a disclosure triangle.
	if (ToolExpanded.IsValidIndex(BlockIndex))
	{
		// Errors, pending calls and anything asking for permission open themselves:
		// those are exactly the states where the arguments matter.
		ToolExpanded[BlockIndex] = B.bToolIsError || !B.bIsComplete || B.bToolAwaitingApproval;
	}

	const FName CardBrush = B.bToolAwaitingApproval ? TEXT("toolCardWarn")
	                      : B.bToolIsError          ? TEXT("toolCardError")
	                                                : TEXT("toolCard");

	return SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(CardBrush))
		.Padding(FMCPChatStyle::Padding(TEXT("sm")))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.HAlign(HAlign_Left)
				.OnClicked_Lambda([this, BlockIndex]()
				{
					if (ToolExpanded.IsValidIndex(BlockIndex))
					{
						ToolExpanded[BlockIndex] = !ToolExpanded[BlockIndex];
						Invalidate(EInvalidateWidgetReason::Layout);
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Bold")))
					.Text_Lambda([this, BlockIndex]() -> FText
					{
						if (!Message.IsValid() || !Message->Blocks.IsValidIndex(BlockIndex))
						{
							return FText::GetEmpty();
						}
						const FChatContentBlock& Blk = Message->Blocks[BlockIndex];
						const bool bOpen = ToolExpanded.IsValidIndex(BlockIndex) && ToolExpanded[BlockIndex];

						FString Status;
						if (!Blk.bIsComplete)      { Status = TEXT("  ⧗ running…"); }
						else if (Blk.bToolIsError) { Status = TEXT("  ✕ failed"); }
						else
						{
							Status = FString::Printf(TEXT("  ✓ %.2fs"), Blk.DurationSeconds);
						}

						return FText::FromString(FString::Printf(TEXT("%s %s%s"),
							bOpen ? TEXT("▾") : TEXT("▸"), *Blk.ToolName, *Status));
					})
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildApprovalPrompt(BlockIndex)
			]

			+ SVerticalBox::Slot().AutoHeight()
			.Padding(FMCPChatStyle::Space(TEXT("md")), FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Mono")))
				.AutoWrapText(true)
				.Visibility_Lambda([this, BlockIndex]()
				{
					return (ToolExpanded.IsValidIndex(BlockIndex) && ToolExpanded[BlockIndex])
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text_Lambda([this, BlockIndex]() -> FText
				{
					if (!Message.IsValid() || !Message->Blocks.IsValidIndex(BlockIndex))
					{
						return FText::GetEmpty();
					}
					const FChatContentBlock& Blk = Message->Blocks[BlockIndex];

					FString Out;
					if (Blk.ToolArgs.IsValid())
					{
						FString Args;
						const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W =
							TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Args);
						FJsonSerializer::Serialize(Blk.ToolArgs.ToSharedRef(), W);
						Out += Args;
					}
					else if (!Blk.PartialArgsBuffer.IsEmpty())
					{
						// Still streaming, or the JSON never parsed — show it verbatim
						// rather than implying the call had no arguments.
						Out += Blk.PartialArgsBuffer;
					}

					if (!Blk.ToolResultText.IsEmpty())
					{
						Out += TEXT("\n\n") + Blk.ToolResultText;
					}
					return FText::FromString(Out);
				})
			]
		];
}

TSharedRef<SWidget> SChatMessageRow::BuildApprovalPrompt(int32 BlockIndex)
{
	if (!Message.IsValid() || !Message->Blocks.IsValidIndex(BlockIndex))
	{
		return SNullWidget::NullWidget;
	}

	const FGuid CallId = Message->Blocks[BlockIndex].ToolCallId;

	auto Respond = [this, CallId](EChatPermissionResult Result)
	{
		if (Controller.IsValid())
		{
			Controller->RespondToPermission(CallId, Result);
		}
		return FReply::Handled();
	};

	auto MakeChoice = [&](const FText& Label, const FText& Tip, EChatPermissionResult Result)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(Tip)
			.OnClicked_Lambda([Respond, Result]() { return Respond(Result); })
			.ContentPadding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.Text(Label)
			];
	};

	return SNew(SVerticalBox)
		.Visibility_Lambda([this, BlockIndex]()
		{
			return (Message.IsValid() && Message->Blocks.IsValidIndex(BlockIndex)
				&& Message->Blocks[BlockIndex].bToolAwaitingApproval)
				? EVisibility::Visible : EVisibility::Collapsed;
		})

		+ SVerticalBox::Slot().AutoHeight()
		.Padding(0.f, FMCPChatStyle::Space(TEXT("xs")), 0.f, FMCPChatStyle::Space(TEXT("xs")))
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("warning")))
			.AutoWrapText(true)
			.Text_Lambda([this, BlockIndex]() -> FText
			{
				if (!Message.IsValid() || !Message->Blocks.IsValidIndex(BlockIndex))
				{
					return FText::GetEmpty();
				}
				const FString& Reason = Message->Blocks[BlockIndex].ToolResultText;
				return Reason.IsEmpty()
					? LOCTEXT("NeedsApproval", "This tool needs your permission before it runs.")
					: FText::FromString(Reason);
			})
		]

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)

			// Deny first: the safe choice is the one closest to the reading start,
			// and Esc maps to it.
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeChoice(LOCTEXT("Deny", "Deny"),
					LOCTEXT("DenyTip", "Refuse this call. The model is told, and can try another approach."),
					EChatPermissionResult::Deny)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			.Padding(FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f, 0.f)
			[
				MakeChoice(LOCTEXT("AllowOnce", "Allow once"),
					LOCTEXT("AllowOnceTip", "Run it this one time."),
					EChatPermissionResult::AllowOnce)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			.Padding(FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f, 0.f)
			[
				MakeChoice(LOCTEXT("AllowSession", "Allow this session"),
					LOCTEXT("AllowSessionTip", "Stop asking for this tool until you switch conversations."),
					EChatPermissionResult::AllowForSession)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			.Padding(FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f, 0.f)
			[
				MakeChoice(LOCTEXT("AllowAlways", "Always allow"),
					LOCTEXT("AllowAlwaysTip",
						"Never ask for this tool again, in any conversation. Revocable in Settings ▸ Permissions."),
					EChatPermissionResult::AllowAlways)
			]
		];
}

TSharedRef<SWidget> SChatMessageRow::BuildCollapsedToolSummary(const TArray<int32>& BlockIndices)
{
	if (BlockIndices.Num() == 0 || !Message.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	const int32 RunKey = BlockIndices[0];

	double TotalSeconds = 0.0;
	for (int32 Index : BlockIndices)
	{
		if (Message->Blocks.IsValidIndex(Index))
		{
			TotalSeconds += Message->Blocks[Index].DurationSeconds;
		}
	}

	TSharedRef<SVerticalBox> Expanded = SNew(SVerticalBox);
	for (int32 Index : BlockIndices)
	{
		Expanded->AddSlot().AutoHeight()
			.Padding(0.f, 0.f, 0.f, FMCPChatStyle::Space(TEXT("xs")))
			[
				BuildToolCard(Index)
			];
	}

	const int32 Count = BlockIndices.Num();

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.HAlign(HAlign_Left)
			.OnClicked_Lambda([this, RunKey]()
			{
				if (ExpandedToolRuns.Contains(RunKey)) { ExpandedToolRuns.Remove(RunKey); }
				else                                    { ExpandedToolRuns.Add(RunKey); }
				Invalidate(EInvalidateWidgetReason::Layout);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.Text_Lambda([this, RunKey, Count, TotalSeconds]()
				{
					const bool bOpen = ExpandedToolRuns.Contains(RunKey);
					return FText::FromString(FString::Printf(
						TEXT("%s  %d read-only tools · %.1fs"),
						bOpen ? TEXT("▾") : TEXT("▸"), Count, TotalSeconds));
				})
			]
		]

		+ SVerticalBox::Slot().AutoHeight()
		.Padding(FMCPChatStyle::Space(TEXT("md")), 0.f, 0.f, 0.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this, RunKey]()
			{
				return ExpandedToolRuns.Contains(RunKey) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				Expanded
			]
		];
}

TSharedRef<SWidget> SChatMessageRow::BuildNoticeBlock(int32 BlockIndex, bool bIsError)
{
	return SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(bIsError ? TEXT("toolCardError") : TEXT("toolCardWarn")))
		.Padding(FMCPChatStyle::Padding(TEXT("sm")))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			.Padding(0.f, 0.f, FMCPChatStyle::Space(TEXT("sm")), 0.f)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Bold")))
				.ColorAndOpacity(FMCPChatStyle::Color(bIsError ? TEXT("error") : TEXT("warning")))
				.Text(FText::FromString(bIsError ? TEXT("✕") : TEXT("⚠")))
			]

			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
				.Text(this, &SChatMessageRow::GetBlockText, BlockIndex)
				.AutoWrapText(true)
			]
		];
}

TSharedRef<SWidget> SChatMessageRow::BuildAttachmentBlock(int32 BlockIndex)
{
	if (!Message.IsValid() || !Message->Blocks.IsValidIndex(BlockIndex))
	{
		return SNullWidget::NullWidget;
	}
	const FChatContentBlock& B = Message->Blocks[BlockIndex];
	const FString Label = B.DisplayName.IsEmpty() ? B.StoredPath : B.DisplayName;

	// Images render as images (Phase 6). Seeing what you sent is most of the value of
	// having sent it — a chip saying "screenshot.png" tells you nothing about whether
	// you attached the right frame.
	if (B.Type == FChatContentBlock::EType::Image && !B.StoredPath.IsEmpty() && Message.IsValid())
	{
		FString SessionDir;
		if (Controller.IsValid())
		{
			if (const FChatSessionPtr Session = Controller->GetActiveSession())
			{
				SessionDir = FMCPChatStore::GetAttachmentsDirectory(Session->Id);
			}
		}

		if (!SessionDir.IsEmpty())
		{
			return SNew(SChatImageBlock)
				.Source(SessionDir / B.StoredPath)
				.AltText(Label);
		}
	}

	return SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("chip")))
		.Padding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.Text(FText::FromString(FString::Printf(TEXT("\u25A4 %s"), *Label)))
		];
}

TSharedRef<SWidget> SChatMessageRow::BuildPlanBlock(int32 BlockIndex)
{
	if (!Message.IsValid() || !Message->Blocks.IsValidIndex(BlockIndex))
	{
		return SNullWidget::NullWidget;
	}

	// Attribute-bound, not rebuilt: an agent republishes its whole plan every time
	// one item ticks over, and rebuilding a widget per republish would make a
	// twelve-step task rebuild this card twelve times.
	return SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("toolCard")))
		.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("sm")))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
				.Text(LOCTEXT("PlanHeading", "PLAN"))
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
				.Text(this, &SChatMessageRow::GetBlockText, BlockIndex)
				.AutoWrapText(true)
			]
		];
}

TSharedRef<SWidget> SChatMessageRow::BuildContextChip(int32 BlockIndex)
{
	if (!Message.IsValid() || !Message->Blocks.IsValidIndex(BlockIndex))
	{
		return SNullWidget::NullWidget;
	}
	const FChatContentBlock& B = Message->Blocks[BlockIndex];

	// The RESOLVED payload can be a hundred lines of level dump. It is real content
	// the model received, so it must be inspectable — but showing it inline would
	// bury the message the user actually wrote. Chip in the transcript, full text in
	// the tooltip.
	const FString Label = B.DisplayName.IsEmpty()
		? FString::Printf(TEXT("@%s"), *B.ContextKind) : B.DisplayName;

	FString Preview = B.Text.Left(2000);
	if (B.Text.Len() > Preview.Len()) { Preview.Append(TEXT("\n\n\u2026")); }

	return SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(B.bContextPinned ? TEXT("chipPinned") : TEXT("chip")))
		.Padding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
		.HAlign(HAlign_Left)
		.ToolTipText(FText::FromString(Preview))
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(FText::FromString(B.bContextPinned
				? FString::Printf(TEXT("\u25CE %s"), *Label) : Label))
		];
}

TSharedRef<SWidget> SChatMessageRow::BuildDivider(int32 BlockIndex)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[ SNew(SSeparator).Thickness(1.f) ]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		.Padding(FMCPChatStyle::Space(TEXT("sm")), 0.f)
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.Text(this, &SChatMessageRow::GetBlockText, BlockIndex)
		]

		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[ SNew(SSeparator).Thickness(1.f) ];
}

// ============================================================================
// Accessors
// ============================================================================

FText SChatMessageRow::GetRoleLabel() const
{
	if (!Message.IsValid()) { return FText::GetEmpty(); }

	switch (Message->Role)
	{
	case EChatRole::User:   return LOCTEXT("RoleUser", "You");
	case EChatRole::System: return LOCTEXT("RoleSystem", "System");
	case EChatRole::Tool:   return LOCTEXT("RoleTool", "Tool");
	case EChatRole::Assistant:
	default:
	{
		// A raw model id makes a poor byline. An agent's ids are short words ("opus",
		// "sonnet") that name nothing on their own, so resolve to a display name:
		// the catalogue first, then the backend that produced the message.
		if (const FChatModelInfo* Model = FMCPChatModelCatalog::Get().FindModel(Message->ModelId))
		{
			return FText::FromString(Model->DisplayName);
		}
		if (Controller.IsValid() && !Message->BackendId.IsEmpty())
		{
			if (const FChatBackendPtr Backend = Controller->FindBackend(Message->BackendId))
			{
				return Message->ModelId.IsEmpty()
					? Backend->GetDisplayName()
					: FText::Format(LOCTEXT("AgentWithModel", "{0} · {1}"),
						Backend->GetDisplayName(), FText::FromString(Message->ModelId));
			}
		}
		return Message->ModelId.IsEmpty()
			? LOCTEXT("RoleAssistant", "Assistant")
			: FText::FromString(Message->ModelId);
	}
	}
}

FText SChatMessageRow::GetTimestampText() const
{
	if (!Message.IsValid()) { return FText::GetEmpty(); }
	// Stored UTC; shown local, because a user comparing a transcript against their
	// own editor session thinks in local time.
	const FDateTime Local = Message->Timestamp + (FDateTime::Now() - FDateTime::UtcNow());
	return FText::FromString(Local.ToString(TEXT("%H:%M")));
}

FText SChatMessageRow::GetUsageText() const
{
	if (!Message.IsValid()) { return FText::GetEmpty(); }
	const FChatUsage& U = Message->Usage;

	FString Out = FString::Printf(TEXT("%d in · %d out"), U.InputTokens, U.OutputTokens);
	if (U.CacheReadTokens > 0)
	{
		Out += FString::Printf(TEXT(" · %d cached"), U.CacheReadTokens);
	}
	if (U.EstimatedCostUsd > 0.0)
	{
		Out += FString::Printf(TEXT(" · ~$%.4f"), U.EstimatedCostUsd);
	}
	return FText::FromString(Out);
}

EVisibility SChatMessageRow::GetUsageVisibility() const
{
	return (Message.IsValid() && !Message->Usage.IsEmpty() && !bIsLiveTail)
		? EVisibility::Visible : EVisibility::Collapsed;
}

FSlateColor SChatMessageRow::GetRuleColor() const
{
	if (!Message.IsValid()) { return FMCPChatStyle::Color(TEXT("separator")); }

	switch (Message->Role)
	{
	case EChatRole::User:      return FMCPChatStyle::Color(TEXT("ruleUser"));
	case EChatRole::Assistant: return FMCPChatStyle::Color(TEXT("ruleAsst"));
	default:                   return FMCPChatStyle::Color(TEXT("separator"));
	}
}

EVisibility SChatMessageRow::GetHoverActionsVisibility() const
{
	// Nothing to copy or retry while the reply is still arriving.
	if (bIsLiveTail) { return EVisibility::Collapsed; }
	return IsHovered() ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SChatMessageRow::GetSiblingStepperVisibility() const
{
	if (bIsLiveTail || !Message.IsValid() || !Controller.IsValid()) { return EVisibility::Collapsed; }

	const FChatSessionPtr Session = Controller->GetActiveSession();
	if (!Session.IsValid()) { return EVisibility::Collapsed; }

	return Session->GetSiblings(Message->Id).Num() > 1 ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SChatMessageRow::GetSiblingText() const
{
	if (!Message.IsValid() || !Controller.IsValid()) { return FText::GetEmpty(); }
	const FChatSessionPtr Session = Controller->GetActiveSession();
	if (!Session.IsValid()) { return FText::GetEmpty(); }

	const TArray<FChatMessagePtr> Siblings = Session->GetSiblings(Message->Id);
	const int32 Index = Siblings.IndexOfByPredicate(
		[this](const FChatMessagePtr& S) { return S.IsValid() && S->Id == Message->Id; });

	return FText::FromString(FString::Printf(TEXT(" %d/%d "), Index + 1, Siblings.Num()));
}

// ============================================================================
// Actions
// ============================================================================

FReply SChatMessageRow::OnCopyClicked()
{
	if (Message.IsValid())
	{
		FPlatformApplicationMisc::ClipboardCopy(*Message->GetPlainText(false));
	}
	return FReply::Handled();
}

FReply SChatMessageRow::OnRetryClicked()
{
	if (Controller.IsValid())
	{
		Controller->RetryLastTurn();
	}
	return FReply::Handled();
}

FReply SChatMessageRow::OnPreviousSibling()
{
	if (!Message.IsValid() || !Controller.IsValid()) { return FReply::Handled(); }
	const FChatSessionPtr Session = Controller->GetActiveSession();
	if (!Session.IsValid()) { return FReply::Handled(); }

	const TArray<FChatMessagePtr> Siblings = Session->GetSiblings(Message->Id);
	const int32 Index = Siblings.IndexOfByPredicate(
		[this](const FChatMessagePtr& S) { return S.IsValid() && S->Id == Message->Id; });

	if (Index > 0)
	{
		Session->SetActiveBranch(Siblings[Index - 1]->Id);
		Controller->OnTranscriptChanged.Broadcast();
	}
	return FReply::Handled();
}

FReply SChatMessageRow::OnNextSibling()
{
	if (!Message.IsValid() || !Controller.IsValid()) { return FReply::Handled(); }
	const FChatSessionPtr Session = Controller->GetActiveSession();
	if (!Session.IsValid()) { return FReply::Handled(); }

	const TArray<FChatMessagePtr> Siblings = Session->GetSiblings(Message->Id);
	const int32 Index = Siblings.IndexOfByPredicate(
		[this](const FChatMessagePtr& S) { return S.IsValid() && S->Id == Message->Id; });

	if (Index != INDEX_NONE && Index + 1 < Siblings.Num())
	{
		Session->SetActiveBranch(Siblings[Index + 1]->Id);
		Controller->OnTranscriptChanged.Broadcast();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
