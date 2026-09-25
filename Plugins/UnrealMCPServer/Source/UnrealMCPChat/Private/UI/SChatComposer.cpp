// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SChatComposer.h"
#include "Core/MCPChatDrafts.h"
#include "MCPChatController.h"
#include "MCPChatModelCatalog.h"
#include "MCPChatSettings.h"
#include "MCPChatStore.h"
#include "MCPChatStyle.h"
#include "UI/SChatArgumentForm.h"
#include "UI/SChatAttachmentStrip.h"
#include "UI/SChatModelPicker.h"
#include "UnrealMCPChatModule.h"

#include "MCPPromptProvider.h"
#include "MCPToolRegistry.h"

#include "DesktopPlatformModule.h"
#include "DragAndDrop/ActorDragDropOp.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "IDesktopPlatform.h"
#include "Input/DragAndDrop.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SChatComposer"

namespace
{
	/** Composer height: one line minimum, roughly eight before it scrolls. Eight is
	 *  where a message stops being a sentence and starts being a document, and past
	 *  that the transcript matters more than the draft.
	 *
	 *  Themable like everything else — a theme with a larger body font needs taller
	 *  rows, and hardcoding these would clip its text. */
	constexpr float ComposerMinHeightFallback = 24.f;
	constexpr float ComposerMaxHeightFallback = 168.f;

	TCHAR TriggerCharFor(EChatTriggerMode Mode)
	{
		switch (Mode)
		{
		case EChatTriggerMode::Context: return TEXT('@');
		case EChatTriggerMode::Action:  return TEXT('#');
		case EChatTriggerMode::Command: return TEXT('/');
		default:                        return TEXT('@');
		}
	}
}

// ============================================================================
// Construction
// ============================================================================

void SChatComposer::Construct(const FArguments& InArgs)
{
	Controller      = InArgs._Controller;
	NarrowMode      = InArgs._NarrowMode;
	OnCommandResult = InArgs._OnCommandResult;

	FMCPChatDrafts::Get().Load();

	ChildSlot
	[
		SNew(SBox)
		.Padding(FMCPChatStyle::Padding(TEXT("md")))
		[
			SNew(SBorder)
			.BorderImage_Lambda([this]()
			{
				return FMCPChatStyle::Brush(bDragHighlight ? TEXT("composerFocused") : TEXT("composer"));
			})
			.Padding(FMCPChatStyle::Padding(TEXT("sm")))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(AttachmentStrip, SChatAttachmentStrip)
					.Attachments(&Attachments)
					.OnRemoved(FOnAttachmentRemoved::CreateSP(this, &SChatComposer::RemoveAttachment))
					.OnPinToggled(FOnAttachmentPinToggled::CreateSP(this, &SChatComposer::TogglePin))
				]

				+ SVerticalBox::Slot().AutoHeight()
				[
					BuildTextRow()
				]

				+ SVerticalBox::Slot().AutoHeight()
				.Padding(0.f, FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f)
				[
					BuildToolbar()
				]
			]
		]
	];

	RefreshForSession();
}

SChatComposer::~SChatComposer()
{
	// Persist the in-progress draft. Closing the tab mid-sentence and losing it is a
	// small, constant tax that nobody should have to pay twice.
	if (Controller.IsValid() && TextBox.IsValid())
	{
		if (const FChatSessionPtr Session = Controller->GetActiveSession())
		{
			FMCPChatDrafts::Get().SetDraft(Session->Id, TextBox->GetText().ToString());
		}
	}
	FMCPChatDrafts::Get().Flush();
}

TSharedRef<SWidget> SChatComposer::BuildTextRow()
{
	return SAssignNew(TriggerAnchor, SMenuAnchor)
		// Above the composer, not below: below would put the palette off the bottom
		// of a bottom-docked panel, where half of it is unreachable.
		.Placement(MenuPlacement_AboveAnchor)
		.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
		{
			return SAssignNew(TriggerPopup, SChatTriggerPopup)
				.OnChosen(FOnTriggerEntryChosen::CreateSP(this, &SChatComposer::HandleTriggerChosen))
				.OnDismissed(FOnTriggerDismissed::CreateSP(this, &SChatComposer::CloseTrigger));
		})
		[
			SNew(SBox)
			.MinDesiredHeight(FMCPChatStyle::Space(TEXT("composerMinH"), ComposerMinHeightFallback))
			.MaxDesiredHeight(FMCPChatStyle::Space(TEXT("composerMaxH"), ComposerMaxHeightFallback))
			[
				SAssignNew(TextBox, SMultiLineEditableTextBox)
				.HintText(this, &SChatComposer::GetHintText)
				.AutoWrapText(true)
				.AlwaysShowScrollbars(false)
				.OnTextChanged(this, &SChatComposer::OnComposerTextChanged)
				.OnKeyDownHandler(this, &SChatComposer::HandleKeyDown)
			]
		];
}

TSharedRef<SWidget> SChatComposer::BuildToolbar()
{
	TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);

	// ---- Triggers ----
	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center)
	[
		BuildTriggerButton(TEXT("@"), EChatTriggerMode::Context,
			LOCTEXT("AtTip", "Attach editor context — selection, an asset, the level"))
	];
	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center)
	[
		BuildTriggerButton(TEXT("#"), EChatTriggerMode::Action,
			LOCTEXT("HashTip", "Run a tool or a workflow directly"))
	];
	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center)
	[
		BuildTriggerButton(TEXT("/"), EChatTriggerMode::Command,
			LOCTEXT("SlashTip", "Local commands — these never reach a model"))
	];

	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center)
	.Padding(FMCPChatStyle::Space(TEXT("sm")), 0.f, FMCPChatStyle::Space(TEXT("sm")), 0.f)
	[
		BuildAttachButtons()
	];

	// ---- Model + effort ----
	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center)
	[
		SAssignNew(ModelPicker, SChatModelPicker)
		.Controller(Controller)
		.CompactMode(NarrowMode)
	];

	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center)
	[
		BuildEffortPicker()
	];

	// ---- Readout + send ----
	Bar->AddSlot().FillWidth(1.f).VAlign(VAlign_Center).HAlign(HAlign_Right)
	[
		BuildReadout()
	];

	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center)
	.Padding(FMCPChatStyle::Space(TEXT("sm")), 0.f, 0.f, 0.f)
	[
		BuildSendButton()
	];

	return Bar;
}

TSharedRef<SWidget> SChatComposer::BuildTriggerButton(const FString& Glyph, EChatTriggerMode Mode, const FText& Tooltip)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMCPChatStyle::Padding(TEXT("xs")))
		.ToolTipText(Tooltip)
		.OnClicked_Lambda([this, Mode]()
		{
			BeginTriggerFromButton(Mode);
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Bold")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(FText::FromString(Glyph))
		];
}

TSharedRef<SWidget> SChatComposer::BuildAttachButtons()
{
	auto MakeButton = [this](const FString& Glyph, const FText& Tooltip, TFunction<FReply()> Action)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMCPChatStyle::Padding(TEXT("xs")))
			.ToolTipText(Tooltip)
			.OnClicked_Lambda(Action)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
				.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
				.Text(FText::FromString(Glyph))
			];
	};

	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeButton(TEXT("⎘"), LOCTEXT("AttachFileTip", "Attach a file"),
				[this]() { return OnAttachFileClicked(false); })
		]

		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeButton(TEXT("▣"), LOCTEXT("AttachImageTip", "Attach an image"),
				[this]() { return OnAttachFileClicked(true); })
		]

		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeButton(TEXT("◉"), LOCTEXT("CaptureTip", "Capture the viewport and attach it"),
				[this]() { return OnCaptureViewportClicked(); })
		];
}

TSharedRef<SWidget> SChatComposer::BuildEffortPicker()
{
	return SNew(SComboButton)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.ContentPadding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
		.Visibility(this, &SChatComposer::GetEffortVisibility)
		.ToolTipText(LOCTEXT("EffortTip",
			"How much thinking the model spends before answering. This is the honest "
			"replacement for a temperature slider on models that reject sampling parameters."))
		.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
		{
			TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
			if (!Controller.IsValid()) { return Menu; }

			const FChatSessionPtr Session = Controller->GetActiveSession();
			if (!Session.IsValid()) { return Menu; }

			const FChatModelInfo* Model = FMCPChatModelCatalog::Get().FindModel(Session->ModelId);
			if (!Model) { return Menu; }

			for (const FString& Level : Model->EffortLevels)
			{
				Menu->AddSlot().AutoHeight()
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMCPChatStyle::Padding(TEXT("md"), TEXT("xs")))
					.HAlign(HAlign_Left)
					.OnClicked_Lambda([this, Level]()
					{
						if (Controller.IsValid())
						{
							if (const FChatSessionPtr S = Controller->GetActiveSession())
							{
								S->Params.Effort = Level;
								FMCPChatStore::Get().MarkDirty(S->Id);
							}
						}
						FSlateApplication::Get().DismissAllMenus();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
						.Text(FText::FromString(Level))
					]
				];
			}
			return SNew(SBorder)
				.BorderImage(FMCPChatStyle::Brush(TEXT("popupBg")))
				[ Menu ];
		})
		.ButtonContent()
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(this, &SChatComposer::GetEffortText)
		];
}

TSharedRef<SWidget> SChatComposer::BuildReadout()
{
	return SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
		.ColorAndOpacity(this, &SChatComposer::GetReadoutColor)
		.Text(this, &SChatComposer::GetReadoutText)
		.ToolTipText(this, &SChatComposer::GetReadoutTooltip)
		// Hidden at the narrow breakpoint: there is no room, and the number is a
		// background awareness aid rather than something you act on mid-sentence.
		.Visibility_Lambda([this]() { return IsNarrow() ? EVisibility::Collapsed : EVisibility::Visible; });
}

TSharedRef<SWidget> SChatComposer::BuildSendButton()
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ToolTipText(this, &SChatComposer::GetSendTooltip)
		.OnClicked(this, &SChatComposer::OnSendClicked)
		.ContentPadding(0.f)
		[
			SNew(SBox)
			.WidthOverride(FMCPChatStyle::Space(TEXT("controlW"), 28.f))
			.HeightOverride(FMCPChatStyle::Space(TEXT("controlH"), 20.f))
			[
				SNew(SBorder)
				.BorderImage(FMCPChatStyle::Brush(TEXT("sendButton")))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Bold")))
					.ColorAndOpacity(FMCPChatStyle::Color(TEXT("onAccent")))
					.Text(this, &SChatComposer::GetSendGlyph)
				]
			]
		];
}

// ============================================================================
// Session / draft
// ============================================================================

void SChatComposer::RefreshForSession()
{
	if (!Controller.IsValid() || !TextBox.IsValid()) { return; }

	const FChatSessionPtr Session = Controller->GetActiveSession();
	const FGuid NewId = Session.IsValid() ? Session->Id : FGuid();
	if (NewId == LoadedDraftSession) { InvalidateEstimate(); return; }

	// Save the outgoing draft before loading the incoming one, or switching
	// conversations quietly destroys whatever was half-written.
	if (LoadedDraftSession.IsValid())
	{
		FMCPChatDrafts::Get().SetDraft(LoadedDraftSession, TextBox->GetText().ToString());
	}

	LoadedDraftSession = NewId;
	CloseTrigger();
	RecallIndex = INDEX_NONE;

	// Attachments belong to the message being written, not to the panel. Carrying
	// them into a different conversation would attach them to the wrong thing.
	Attachments.Reset();
	if (AttachmentStrip.IsValid()) { AttachmentStrip->Refresh(); }

	TextBox->SetText(FText::FromString(NewId.IsValid() ? FMCPChatDrafts::Get().GetDraft(NewId) : FString()));
	InvalidateEstimate();
}

void SChatComposer::FocusTextBox()
{
	if (TextBox.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(TextBox, EFocusCause::SetDirectly);
	}
}

void SChatComposer::OpenModelPicker()
{
	if (ModelPicker.IsValid()) { ModelPicker->OpenMenu(); }
}

void SChatComposer::SetComposerText(const FString& Text)
{
	if (!TextBox.IsValid()) { return; }

	// Clearing goes through SetText: inserting an empty string over a selection is
	// not specified to remove it, and a "cleared" box that still holds the sent
	// message is the worst possible failure here.
	if (Text.IsEmpty())
	{
		TextBox->SetText(FText::GetEmpty());
	}
	else
	{
		// SelectAll + insert rather than SetText: it leaves the caret at the END of the
		// inserted text, which is where someone about to keep typing expects it.
		// SetText would leave it at the start.
		TextBox->SelectAllText();
		TextBox->InsertTextAtCursor(Text);
	}
	InvalidateEstimate();
	FocusTextBox();
}

// ============================================================================
// Text and triggers
// ============================================================================

void SChatComposer::OnComposerTextChanged(const FText& NewText)
{
	InvalidateEstimate();
	UpdateTriggerState();

	// Typing anything ends a recall walk — the recalled text is now the user's
	// draft, not a position in the history.
	if (RecallIndex != INDEX_NONE && TextBox.IsValid())
	{
		FString Recalled;
		if (!FMCPChatDrafts::Get().GetRecalled(LoadedDraftSession, RecallIndex, Recalled)
			|| Recalled != NewText.ToString())
		{
			RecallIndex = INDEX_NONE;
		}
	}

	if (LoadedDraftSession.IsValid())
	{
		FMCPChatDrafts::Get().SetDraft(LoadedDraftSession, NewText.ToString());
	}
}

void SChatComposer::UpdateTriggerState()
{
	if (!TextBox.IsValid() || !TriggerAnchor.IsValid()) { return; }

	const FString Text = TextBox->GetText().ToString();

	// Walk back from the end to the trigger character. A space or a newline ends the
	// token, which is what makes "email me @ 5pm" not open a mention popup.
	int32 Start = INDEX_NONE;
	for (int32 i = Text.Len() - 1; i >= 0; --i)
	{
		const TCHAR C = Text[i];
		if (C == TEXT(' ') || C == TEXT('\n') || C == TEXT('\t')) { break; }
		if (C == TEXT('@') || C == TEXT('#') || C == TEXT('/'))
		{
			// A trigger must START a word. Without this, "name@studio.com" opens the
			// context palette and "C#" opens the tool palette — both while the user is
			// typing something entirely ordinary.
			if (i > 0 && !FChar::IsWhitespace(Text[i - 1])) { break; }

			// `/` additionally only triggers at the very start of the message, or every
			// "/Game/Maps/Arena" would open the command palette.
			if (C == TEXT('/') && i != 0) { break; }

			Start = i;
			break;
		}
	}

	if (Start == INDEX_NONE)
	{
		CloseTrigger();
		return;
	}

	const TCHAR TriggerChar = Text[Start];
	EChatTriggerMode Mode = EChatTriggerMode::Context;
	if (TriggerChar == TEXT('#'))      { Mode = EChatTriggerMode::Action; }
	else if (TriggerChar == TEXT('/')) { Mode = EChatTriggerMode::Command; }

	TriggerMode  = Mode;
	TriggerStart = Start;

	const FString Query = Text.Mid(Start + 1);

	if (!TriggerAnchor->IsOpen())
	{
		TriggerAnchor->SetIsOpen(true, /*bFocusMenu*/ false);
	}
	if (TriggerPopup.IsValid())
	{
		TriggerPopup->SetQuery(Mode, Query);
	}
	bTriggerActive = true;
}

void SChatComposer::CloseTrigger()
{
	bTriggerActive = false;
	TriggerStart   = INDEX_NONE;
	if (TriggerAnchor.IsValid() && TriggerAnchor->IsOpen())
	{
		TriggerAnchor->SetIsOpen(false);
	}
}

void SChatComposer::BeginTriggerFromButton(EChatTriggerMode Mode)
{
	if (!TextBox.IsValid()) { return; }

	FString Text = TextBox->GetText().ToString();

	// A trigger has to be its own token. Append a space first when the text does not
	// already end in whitespace, so clicking `@` after "add cover" gives
	// "add cover @" rather than "add cover@".
	if (!Text.IsEmpty() && !FChar::IsWhitespace(Text[Text.Len() - 1]))
	{
		Text.AppendChar(TEXT(' '));
	}

	// `/` is only a command at position 0, so a button press mid-message would open a
	// palette whose selection could not be inserted. Clear the box instead — the user
	// asked for a command, and a command replaces the message by definition.
	if (Mode == EChatTriggerMode::Command) { Text.Reset(); }

	Text.AppendChar(TriggerCharFor(Mode));
	SetComposerText(Text);
	UpdateTriggerState();
}

void SChatComposer::ReplaceTriggerToken(const FString& Replacement)
{
	if (!TextBox.IsValid()) { return; }

	const FString Text = TextBox->GetText().ToString();
	const int32 Start = (TriggerStart != INDEX_NONE && TriggerStart <= Text.Len()) ? TriggerStart : Text.Len();

	SetComposerText(Text.Left(Start) + Replacement);
}

void SChatComposer::HandleTriggerChosen(const FChatTriggerEntry& Entry)
{
	switch (Entry.Kind)
	{
	case FChatTriggerEntry::EKind::Context:
	{
		if (!Entry.bIsTerminal)
		{
			// A category, not a target: type the prefix and keep the palette open so
			// the next keystroke searches inside it.
			ReplaceTriggerToken(FString::Printf(TEXT("@%s"), *Entry.Label));
			UpdateTriggerState();
			return;
		}

		// The chip lives in the attachment strip rather than inline in the text.
		// Slate has no editable-text atom that can hold a non-editable run, and a
		// fake one made of plain text is deletable one character at a time — which
		// silently produces "@Selectio", a mention that resolves to nothing.
		AddAttachment(FMCPChatAttachments::FromContext(Entry.ContextKind, Entry.ContextTarget));
		ReplaceTriggerToken(FString());
		CloseTrigger();
		return;
	}

	case FChatTriggerEntry::EKind::Tool:
	case FChatTriggerEntry::EKind::Prompt:
	{
		CloseTrigger();
		if (Entry.bNeedsArguments)
		{
			ShowArgumentForm(Entry);
		}
		else if (Entry.Kind == FChatTriggerEntry::EKind::Tool)
		{
			ReplaceTriggerToken(FString());
			RunToolWithArgs(Entry.Id, MakeShared<FJsonObject>());
		}
		else
		{
			ReplaceTriggerToken(FString());
			ExpandPromptTemplate(Entry.Id, MakeShared<FJsonObject>());
		}
		return;
	}

	case FChatTriggerEntry::EKind::EditorCommand:
	{
		CloseTrigger();
		ReplaceTriggerToken(FString());
		if (GEditor)
		{
			// Through the editor's own exec path, so it behaves exactly as it would
			// typed into the console — including its logging and its undo behaviour.
			GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Entry.Id);
		}
		FChatCommandResult Result;
		Result.bHandled = true;
		Result.Message = FText::Format(LOCTEXT("RanCommand", "Ran '{0}'."), FText::FromString(Entry.Label));
		OnCommandResult.ExecuteIfBound(Result);
		return;
	}

	case FChatTriggerEntry::EKind::SlashCommand:
	{
		CloseTrigger();
		// Fill in the command and let the user add its argument, rather than running
		// it on selection: half of them take one, and running `/title` with no text
		// would be a confusing no-op.
		SetComposerText(FString::Printf(TEXT("/%s "), *Entry.Id));
		return;
	}
	}
}

// ============================================================================
// Keyboard
// ============================================================================

bool SChatComposer::EnterSends()
{
	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	return !Settings || !Settings->bEnterInsertsNewline;
}

FReply SChatComposer::HandleKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	const FKey Key = Event.GetKey();

	// ---- Trigger palette navigation ----
	if (bTriggerActive && TriggerPopup.IsValid() && TriggerPopup->HasResults())
	{
		if (Key == EKeys::Up)   { TriggerPopup->MoveSelection(-1); return FReply::Handled(); }
		if (Key == EKeys::Down) { TriggerPopup->MoveSelection(+1); return FReply::Handled(); }

		if (Key == EKeys::Tab || (Key == EKeys::Enter && !Event.IsShiftDown()))
		{
			if (TriggerPopup->CommitSelection()) { return FReply::Handled(); }
		}

		if (Key == EKeys::Escape)
		{
			// Esc closes the palette but keeps the typed text. Deleting what was typed
			// would punish the common case of "I meant this literally after all".
			CloseTrigger();
			return FReply::Handled();
		}
	}

	// ---- History recall ----
	// Only from an empty box, and only when no palette is open — otherwise ↑ would
	// fight with palette navigation and with ordinary caret movement.
	if (Key == EKeys::Up && !bTriggerActive && TextBox.IsValid())
	{
		const FString Current = TextBox->GetText().ToString();
		const bool bCanStart = (RecallIndex == INDEX_NONE) && Current.IsEmpty();
		if (bCanStart || RecallIndex != INDEX_NONE)
		{
			if (bCanStart) { RecallStash = Current; }

			FString Recalled;
			if (FMCPChatDrafts::Get().GetRecalled(LoadedDraftSession, RecallIndex + 1, Recalled))
			{
				++RecallIndex;
				SetComposerText(Recalled);
				return FReply::Handled();
			}
			// Past the oldest: stop rather than wrap. Wrapping through your own
			// history is disorienting.
			if (RecallIndex != INDEX_NONE) { return FReply::Handled(); }
		}
	}

	if (Key == EKeys::Down && RecallIndex != INDEX_NONE)
	{
		FString Recalled;
		if (RecallIndex > 0 && FMCPChatDrafts::Get().GetRecalled(LoadedDraftSession, RecallIndex - 1, Recalled))
		{
			--RecallIndex;
			SetComposerText(Recalled);
		}
		else
		{
			RecallIndex = INDEX_NONE;
			SetComposerText(RecallStash);
		}
		return FReply::Handled();
	}

	// ---- Paste ----
	if (Key == EKeys::V && Event.IsControlDown() && !Event.IsShiftDown())
	{
		// Only intercept when the clipboard actually holds a bitmap; otherwise the
		// text box's own paste has to run, or Ctrl+V stops working for text.
		if (TryAttachClipboardImage()) { return FReply::Handled(); }
	}

	// ---- Send ----
	const bool bSendChord = EnterSends() ? (Key == EKeys::Enter && !Event.IsShiftDown())
	                                     : (Key == EKeys::Enter && (Event.IsControlDown() || Event.IsShiftDown()));
	if (bSendChord)
	{
		SendCurrentText();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

// ============================================================================
// Sending
// ============================================================================

FReply SChatComposer::OnSendClicked()
{
	SendCurrentText();
	return FReply::Handled();
}

void SChatComposer::SendCurrentText()
{
	if (!Controller.IsValid() || !TextBox.IsValid()) { return; }

	// Send doubles as Stop while a turn runs — one button, one place to look.
	if (Controller->IsTurnInFlight())
	{
		Controller->CancelTurn();
		return;
	}

	CloseTrigger();

	const FString Text = TextBox->GetText().ToString().TrimStartAndEnd();

	// ---- Local commands ----
	if (FMCPChatSlashCommands::LooksLikeCommand(Text))
	{
		const FChatCommandResult Result = FMCPChatSlashCommands::Execute(Text, Controller);
		if (Result.bHandled)
		{
			SetComposerText(FString());
			RecallIndex = INDEX_NONE;
			OnCommandResult.ExecuteIfBound(Result);
			return;
		}
	}

	if (Text.IsEmpty() && Attachments.Num() == 0) { return; }

	const FChatSessionPtr Session = Controller->GetActiveSession();
	if (!Session.IsValid()) { return; }

	// ---- Attachments ----
	// Commit writes pending bytes into the session folder and fills StoredPath. A
	// failure here does not block the message: the failed chip is reported and the
	// rest goes.
	FMCPChatAttachments::Commit(Attachments, Session->Id);

	TArray<FChatContentBlock> Blocks;
	TArray<FChatAttachmentPtr> KeepPinned;
	for (const FChatAttachmentPtr& A : Attachments)
	{
		if (!A.IsValid()) { continue; }

		if (!A->Error.IsEmpty())
		{
			UE_LOG(LogUnrealMCPChat, Warning, TEXT("Attachment '%s' skipped: %s"),
				*A->DisplayName, *A->Error.ToString());
			continue;
		}

		// Pinned context is stored on the session and re-resolved every turn by the
		// controller, so it must NOT also be added here — that would send it twice.
		if (A->IsContext() && A->bPinned) { KeepPinned.Add(A); continue; }

		Blocks.Add(FMCPChatAttachments::ToContentBlock(*A));
	}

	if (!Text.IsEmpty())
	{
		FMCPChatDrafts::Get().PushSent(Session->Id, Text);
	}

	Controller->SendMessage(Text, Blocks);

	// Pinned chips survive the send; everything else has been consumed.
	Attachments = MoveTemp(KeepPinned);
	if (AttachmentStrip.IsValid()) { AttachmentStrip->Refresh(); }

	SetComposerText(FString());
	FMCPChatDrafts::Get().ClearDraft(Session->Id);
	RecallIndex = INDEX_NONE;
	InvalidateEstimate();
	FocusTextBox();
}

// ============================================================================
// Attachments
// ============================================================================

void SChatComposer::AddAttachment(const FChatAttachmentPtr& Attachment)
{
	if (!Attachment.IsValid()) { return; }

	if (Attachments.Num() >= FMCPChatAttachments::MaxAttachmentsPerMessage)
	{
		FChatCommandResult Notice;
		Notice.bHandled = true;
		Notice.bIsError = true;
		Notice.Message = FText::Format(
			LOCTEXT("TooManyAttachments", "A message can carry {0} attachments. Send this one first."),
			FText::AsNumber(FMCPChatAttachments::MaxAttachmentsPerMessage));
		OnCommandResult.ExecuteIfBound(Notice);
		return;
	}

	// Attaching the same asset twice is almost always a double-drop, not intent.
	if (Attachment->IsContext())
	{
		for (const FChatAttachmentPtr& Existing : Attachments)
		{
			if (Existing.IsValid() && Existing->IsContext()
				&& Existing->ContextKind == Attachment->ContextKind
				&& Existing->ContextTarget == Attachment->ContextTarget)
			{
				return;
			}
		}
	}

	Attachments.Add(Attachment);
	if (AttachmentStrip.IsValid()) { AttachmentStrip->Refresh(); }
	InvalidateEstimate();
}

void SChatComposer::RemoveAttachment(const FGuid& Id)
{
	const int32 Index = Attachments.IndexOfByPredicate(
		[&Id](const FChatAttachmentPtr& A) { return A.IsValid() && A->Id == Id; });
	if (Index == INDEX_NONE) { return; }

	// Removing a pinned chip also unpins it from the session, or it would keep being
	// sent by a chip the user can no longer see.
	if (Attachments[Index]->IsContext() && Attachments[Index]->bPinned && Controller.IsValid())
	{
		Controller->RemovePinnedContext(FString::Printf(TEXT("%s:%s"),
			FMCPChatContextResolver::KindToString(Attachments[Index]->ContextKind),
			*Attachments[Index]->ContextTarget));
	}

	Attachments.RemoveAt(Index);
	if (AttachmentStrip.IsValid()) { AttachmentStrip->Refresh(); }
	InvalidateEstimate();
}

void SChatComposer::TogglePin(const FGuid& Id)
{
	if (!Controller.IsValid()) { return; }

	for (const FChatAttachmentPtr& A : Attachments)
	{
		if (!A.IsValid() || A->Id != Id || !A->IsContext()) { continue; }

		A->bPinned = !A->bPinned;
		const FString Key = FString::Printf(TEXT("%s:%s"),
			FMCPChatContextResolver::KindToString(A->ContextKind), *A->ContextTarget);

		if (A->bPinned) { Controller->AddPinnedContext(Key); }
		else            { Controller->RemovePinnedContext(Key); }
		break;
	}

	if (AttachmentStrip.IsValid()) { AttachmentStrip->Refresh(); }
	InvalidateEstimate();
}

FReply SChatComposer::OnAttachFileClicked(bool bImagesOnly)
{
	IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
	if (!Platform) { return FReply::Handled(); }

	const void* ParentHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
	const FString Filter = bImagesOnly
		? TEXT("Images (*.png;*.jpg;*.jpeg;*.gif;*.webp)|*.png;*.jpg;*.jpeg;*.gif;*.webp")
		: TEXT("All files (*.*)|*.*");

	TArray<FString> Chosen;
	if (!Platform->OpenFileDialog(ParentHandle,
		bImagesOnly ? LOCTEXT("PickImage", "Attach an image").ToString()
		            : LOCTEXT("PickFile", "Attach a file").ToString(),
		FPaths::ProjectDir(), FString(), Filter, EFileDialogFlags::Multiple, Chosen))
	{
		return FReply::Handled();
	}

	for (const FString& Path : Chosen)
	{
		FChatAttachmentPtr A = FMCPChatAttachments::FromFile(Path);
		// Over the size limit: offer the path instead of refusing outright. The model
		// can still read the file with a tool, which is usually what was wanted.
		if (A.IsValid() && !A->Error.IsEmpty() && A->SizeBytes > FMCPChatAttachments::MaxAttachmentBytes)
		{
			A = FMCPChatAttachments::AsPathReference(Path);
		}
		AddAttachment(A);
	}

	FocusTextBox();
	return FReply::Handled();
}

FReply SChatComposer::OnCaptureViewportClicked()
{
	// Called through the registry rather than the bridge: the bridge's result type
	// carries text and structured content only, and this needs the image bytes. The
	// tool is read-only, so there is no gate to bypass by doing so.
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	const FMCPToolResult Result = FMCPToolRegistry::Get().ExecuteTool(TEXT("take_screenshot"), Args);

	if (Result.bIsError)
	{
		FChatCommandResult Notice;
		Notice.bHandled = true;
		Notice.bIsError = true;
		Notice.Message = LOCTEXT("CaptureFailed", "Could not capture the viewport. Is a level editor viewport open?");
		OnCommandResult.ExecuteIfBound(Notice);
		return FReply::Handled();
	}

	for (const FMCPContentBlock& Block : Result.Content)
	{
		if (Block.Type != TEXT("image") || Block.ImageData.IsEmpty()) { continue; }

		TArray<uint8> Bytes;
		if (!FBase64::Decode(Block.ImageData, Bytes)) { continue; }

		const FString Extension = Block.MimeType.Contains(TEXT("png")) ? TEXT("png") : TEXT("jpg");
		AddAttachment(FMCPChatAttachments::FromImageBytes(MoveTemp(Bytes),
			FString::Printf(TEXT("viewport.%s"), *Extension)));
		break;
	}

	FocusTextBox();
	return FReply::Handled();
}

bool SChatComposer::TryAttachClipboardImage()
{
	if (!FMCPChatAttachments::ClipboardHasImage()) { return false; }

	const FChatAttachmentPtr A = FMCPChatAttachments::FromClipboardImage();
	if (!A.IsValid()) { return false; }

	AddAttachment(A);
	return true;
}

// ============================================================================
// Drag and drop
// ============================================================================

void SChatComposer::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	const TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	bDragHighlight = Operation.IsValid()
		&& (Operation->IsOfType<FAssetDragDropOp>()
			|| Operation->IsOfType<FActorDragDropOp>()
			|| Operation->IsOfType<FExternalDragOperation>());
}

void SChatComposer::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	bDragHighlight = false;
}

FReply SChatComposer::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	return bDragHighlight ? FReply::Handled() : FReply::Unhandled();
}

FReply SChatComposer::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	bDragHighlight = false;

	const TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (!Operation.IsValid()) { return FReply::Unhandled(); }

	// ---- Content Browser ----
	// A reference chip, not the .uasset bytes. Attaching a 200 MB mesh helps nobody;
	// what the model can act on is the path.
	if (Operation->IsOfType<FAssetDragDropOp>())
	{
		const TSharedPtr<FAssetDragDropOp> AssetOp = StaticCastSharedPtr<FAssetDragDropOp>(Operation);
		for (const FAssetData& Asset : AssetOp->GetAssets())
		{
			AddAttachment(FMCPChatAttachments::FromAssetPath(
				Asset.GetObjectPathString(), Asset.AssetName.ToString()));
		}
		return FReply::Handled();
	}

	// ---- World Outliner ----
	if (Operation->IsOfType<FActorDragDropOp>())
	{
		const TSharedPtr<FActorDragDropOp> ActorOp = StaticCastSharedPtr<FActorDragDropOp>(Operation);
		for (const TWeakObjectPtr<AActor>& Weak : ActorOp->Actors)
		{
			if (const AActor* Actor = Weak.Get())
			{
				AddAttachment(FMCPChatAttachments::FromActorLabel(Actor->GetActorLabel()));
			}
		}
		return FReply::Handled();
	}

	// ---- OS files ----
	if (Operation->IsOfType<FExternalDragOperation>())
	{
		const TSharedPtr<FExternalDragOperation> FileOp = StaticCastSharedPtr<FExternalDragOperation>(Operation);
		if (FileOp->HasFiles())
		{
			for (const FString& Path : FileOp->GetFiles())
			{
				FChatAttachmentPtr A = FMCPChatAttachments::FromFile(Path);
				if (A.IsValid() && !A->Error.IsEmpty() && A->SizeBytes > FMCPChatAttachments::MaxAttachmentBytes)
				{
					A = FMCPChatAttachments::AsPathReference(Path);
				}
				AddAttachment(A);
			}
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

// ============================================================================
// Tools and prompts
// ============================================================================

void SChatComposer::ShowArgumentForm(const FChatTriggerEntry& Entry)
{
	const bool bIsPrompt = (Entry.Kind == FChatTriggerEntry::EKind::Prompt);

	TSharedPtr<FJsonObject> Schema;
	FText Description;
	if (bIsPrompt)
	{
		Schema = SChatArgumentForm::SchemaFromPromptArguments(Entry.Id);
	}
	else if (const FMCPToolDefinition* Tool = FMCPToolRegistry::Get().FindTool(Entry.Id))
	{
		Schema = Tool->InputSchema;
		Description = FText::FromString(Tool->Description);
	}

	const FString EntryId = Entry.Id;

	TSharedRef<SWidget> Form = SNew(SChatArgumentForm)
		.Title(FText::FromString(Entry.Label))
		.Description(bIsPrompt ? FText::FromString(Entry.Detail) : Description)
		.Schema(Schema)
		.OnSubmitted_Lambda([this, EntryId, bIsPrompt](TSharedPtr<FJsonObject> Values)
		{
			FSlateApplication::Get().DismissAllMenus();
			ReplaceTriggerToken(FString());
			if (bIsPrompt) { ExpandPromptTemplate(EntryId, Values); }
			else           { RunToolWithArgs(EntryId, Values); }
		})
		.OnCancelled_Lambda([this]()
		{
			FSlateApplication::Get().DismissAllMenus();
			// The trigger token stays in the box on cancel. The user typed it; deciding
			// not to fill in the arguments is not a reason to delete what they wrote.
			FocusTextBox();
		});

	// The form is a menu rather than a modal window: a modal steals focus from the
	// editor, and this is a step inside composing a message, not a separate task.
	FSlateApplication::Get().PushMenu(
		AsShared(), FWidgetPath(), Form, FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

void SChatComposer::RunToolWithArgs(const FString& ToolName, const TSharedPtr<FJsonObject>& Args)
{
	if (Controller.IsValid())
	{
		Controller->RunToolDirectly(ToolName, Args);
	}
	FocusTextBox();
}

void SChatComposer::ExpandPromptTemplate(const FString& PromptName, const TSharedPtr<FJsonObject>& Args)
{
	TMap<FString, FString> ArgMap;
	if (Args.IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Args->Values)
		{
			if (Pair.Value.IsValid()) { ArgMap.Add(Pair.Key, Pair.Value->AsString()); }
		}
	}

	const TArray<FMCPPromptMessage> Messages = FMCPPromptProvider::Get().GetPrompt(PromptName, ArgMap);

	FString Expanded;
	for (const FMCPPromptMessage& M : Messages)
	{
		// Only the user-role parts belong in the composer. An assistant-role priming
		// message is not something the user wrote, and putting it in an editable box
		// would let it be sent as if they had.
		if (M.Role == TEXT("user") && !M.Content.Text.IsEmpty())
		{
			if (!Expanded.IsEmpty()) { Expanded.Append(TEXT("\n\n")); }
			Expanded.Append(M.Content.Text);
		}
	}

	if (Expanded.IsEmpty())
	{
		FChatCommandResult Notice;
		Notice.bHandled = true;
		Notice.bIsError = true;
		Notice.Message = FText::Format(LOCTEXT("PromptEmpty", "'{0}' produced nothing to send."),
			FText::FromString(PromptName));
		OnCommandResult.ExecuteIfBound(Notice);
		return;
	}

	// Expanded as EDITABLE TEXT, not sent. The whole value of a workflow prompt is
	// seeing exactly what is about to be asked and adjusting it first.
	SetComposerText(Expanded);
}

// ============================================================================
// Readout
// ============================================================================

void SChatComposer::InvalidateEstimate()
{
	bEstimateValid = false;
}

const FMCPChatTokenEstimator::FEstimate& SChatComposer::GetEstimate() const
{
	if (!bEstimateValid)
	{
		const FChatSessionPtr Session = Controller.IsValid() ? Controller->GetActiveSession() : nullptr;
		const FString Draft = TextBox.IsValid() ? TextBox->GetText().ToString() : FString();
		CachedEstimate = FMCPChatTokenEstimator::ForOutgoing(Session, Draft, Attachments);
		bEstimateValid = true;
	}
	return CachedEstimate;
}

FText SChatComposer::GetReadoutText() const
{
	const FMCPChatTokenEstimator::FEstimate& E = GetEstimate();
	return FText::FromString(FString::Printf(TEXT("%s  ·  %s"),
		*FMCPChatTokenEstimator::FormatTokens(E.Total),
		*FMCPChatTokenEstimator::FormatCost(E.CostUsd)));
}

FSlateColor SChatComposer::GetReadoutColor() const
{
	const FMCPChatTokenEstimator::FEstimate& E = GetEstimate();
	if (E.ContextFraction >= FMCPChatTokenEstimator::RedFraction)   { return FMCPChatStyle::Color(TEXT("readoutOver")); }
	if (E.ContextFraction >= FMCPChatTokenEstimator::AmberFraction) { return FMCPChatStyle::Color(TEXT("readoutWarn")); }
	return FMCPChatStyle::Color(TEXT("fgSubdued"));
}

FText SChatComposer::GetReadoutTooltip() const
{
	const FMCPChatTokenEstimator::FEstimate& E = GetEstimate();

	FText Warning = FText::GetEmpty();
	if (E.ContextFraction >= FMCPChatTokenEstimator::RedFraction)
	{
		Warning = LOCTEXT("NearLimit",
			"\n\n⚠ This is close to the model's context limit. Run /compact to shorten the history.");
	}

	return FText::Format(
		LOCTEXT("ReadoutTip",
			"Estimated size of the next request:\n"
			"  conversation  {0}\n"
			"  this message  {1}\n"
			"  attachments   {2}\n"
			"  tool schemas  {3}\n"
			"  ─────────────────\n"
			"  total         {4} of {5}\n\n"
			"An estimate, not a token count — the provider's exact usage is reconciled after each turn.{6}"),
		FText::FromString(FMCPChatTokenEstimator::FormatTokens(E.HistoryTokens)),
		FText::FromString(FMCPChatTokenEstimator::FormatTokens(E.DraftTokens)),
		FText::FromString(FMCPChatTokenEstimator::FormatTokens(E.AttachmentTokens)),
		FText::FromString(FMCPChatTokenEstimator::FormatTokens(E.ToolSchemaTokens)),
		FText::FromString(FMCPChatTokenEstimator::FormatTokens(E.Total)),
		FText::FromString(E.ContextTokens > 0
			? FMCPChatTokenEstimator::FormatTokens(E.ContextTokens) : TEXT("?")),
		Warning);
}

// ============================================================================
// Small accessors
// ============================================================================

FText SChatComposer::GetSendGlyph() const
{
	const bool bInFlight = Controller.IsValid() && Controller->IsTurnInFlight();
	return FText::FromString(bInFlight ? TEXT("■") : TEXT("▶"));
}

FText SChatComposer::GetSendTooltip() const
{
	if (Controller.IsValid() && Controller->IsAwaitingApproval())
	{
		return LOCTEXT("AwaitingApprovalTip", "Waiting for your decision on a tool call above");
	}
	if (Controller.IsValid() && Controller->IsTurnInFlight())
	{
		return LOCTEXT("StopTip", "Stop generating");
	}
	return EnterSends()
		? LOCTEXT("SendTipEnter", "Send  (Enter)")
		: LOCTEXT("SendTipCtrl", "Send  (Ctrl+Enter)");
}

FText SChatComposer::GetHintText() const
{
	// The placeholder states the binding that is actually in force, because it is
	// reversible in settings and a wrong hint is worse than none.
	return EnterSends()
		? LOCTEXT("HintEnter", "Ask to make changes…   @ context · # tools · / commands   (Enter to send, Shift+Enter for a new line)")
		: LOCTEXT("HintCtrl",  "Ask to make changes…   @ context · # tools · / commands   (Ctrl+Enter to send, Enter for a new line)");
}

FText SChatComposer::GetEffortText() const
{
	if (!Controller.IsValid()) { return FText::GetEmpty(); }
	const FChatSessionPtr Session = Controller->GetActiveSession();
	if (!Session.IsValid() || Session->Params.Effort.IsEmpty()) { return FText::GetEmpty(); }

	return FText::FromString(FString::Printf(TEXT("⚡ %s"), *Session->Params.Effort));
}

EVisibility SChatComposer::GetEffortVisibility() const
{
	// Hidden — not disabled — for models with no effort control. A greyed-out
	// setting invites "how do I turn this on?"; an absent one does not.
	if (!Controller.IsValid() || IsNarrow()) { return EVisibility::Collapsed; }

	const FChatSessionPtr Session = Controller->GetActiveSession();
	if (!Session.IsValid()) { return EVisibility::Collapsed; }

	const FChatModelInfo* Model = FMCPChatModelCatalog::Get().FindModel(Session->ModelId);
	return (Model && Model->EffortLevels.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
