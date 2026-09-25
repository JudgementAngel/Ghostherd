// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SChatSettingsPanel.h"
#include "Agents/MCPChatProcessRunner.h"
#include "MCPChatAgentCatalog.h"
#include "MCPChatController.h"
#include "MCPChatModelCatalog.h"
#include "MCPChatSecretStore.h"
#include "MCPChatSettings.h"
#include "MCPChatStore.h"
#include "MCPChatStyle.h"
#include "MCPChatToolBridge.h"
#include "UnrealMCPChatModule.h"

#include "HAL/PlatformProcess.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SChatSettingsPanel"

void SChatSettingsPanel::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	CurrentTab = InArgs._InitialTab;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("transcriptBg")))
		.Padding(0.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildTabStrip()
			]

			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				.Padding(FMCPChatStyle::Padding(TEXT("lg")))
				[
					SNew(SBox)
					.MaxDesiredWidth(FMCPChatStyle::Space(TEXT("settingsMaxW"), 720.f))
					.HAlign(HAlign_Left)
					[
						SAssignNew(Body, SVerticalBox)
					]
				]
			]
		]
	];

	RebuildBody();
}

void SChatSettingsPanel::SelectTab(EChatSettingsTab Tab)
{
	if (CurrentTab == Tab) { return; }
	CurrentTab = Tab;

	// A revealed key must not survive navigating away and back — the reveal is a
	// deliberate act and it should have to be repeated.
	RevealedProviders.Reset();
	RebuildBody();
}

TSharedRef<SWidget> SChatSettingsPanel::BuildTabStrip()
{
	return SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("headerBg")))
		.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("sm")))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth()
			[ BuildTabButton(EChatSettingsTab::Providers, LOCTEXT("TabProviders", "Providers")) ]

			+ SHorizontalBox::Slot().AutoWidth()
			[ BuildTabButton(EChatSettingsTab::AI, LOCTEXT("TabAI", "AI settings")) ]

			+ SHorizontalBox::Slot().AutoWidth()
			[ BuildTabButton(EChatSettingsTab::Permissions, LOCTEXT("TabPermissions", "Permissions")) ]

			+ SHorizontalBox::Slot().AutoWidth()
			[ BuildTabButton(EChatSettingsTab::Appearance, LOCTEXT("TabAppearance", "Appearance & data")) ]
		];
}

TSharedRef<SWidget> SChatSettingsPanel::BuildTabButton(EChatSettingsTab Tab, const FText& Label)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMCPChatStyle::Padding(TEXT("md"), TEXT("xs")))
		.OnClicked_Lambda([this, Tab]() { SelectTab(Tab); return FReply::Handled(); })
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
			.ColorAndOpacity_Lambda([this, Tab]()
			{
				return FMCPChatStyle::Color(CurrentTab == Tab ? TEXT("fg") : TEXT("fgSubdued"));
			})
			.Text(Label)
		];
}

void SChatSettingsPanel::RebuildBody()
{
	if (!Body.IsValid()) { return; }

	KeyBoxes.Reset();
	Body->ClearChildren();

	TSharedRef<SWidget> Content = SNullWidget::NullWidget;
	switch (CurrentTab)
	{
	case EChatSettingsTab::Providers:   Content = BuildProvidersTab(); break;
	case EChatSettingsTab::AI:          Content = BuildAITab(); break;
	case EChatSettingsTab::Permissions: Content = BuildPermissionsTab(); break;
	case EChatSettingsTab::Appearance:  Content = BuildAppearanceTab(); break;
	}

	Body->AddSlot().AutoHeight()[ Content ];
}

// ============================================================================
// Shared row builders
// ============================================================================

TSharedRef<SWidget> SChatSettingsPanel::BuildSectionHeading(const FText& Text) const
{
	return SNew(SBox)
		.Padding(0.f, FMCPChatStyle::Space(TEXT("lg")), 0.f, FMCPChatStyle::Space(TEXT("sm")))
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.H3")))
			.Text(Text)
		];
}

TSharedRef<SWidget> SChatSettingsPanel::BuildHelpText(const FText& Text) const
{
	return SNew(SBox)
		.Padding(0.f, 0.f, 0.f, FMCPChatStyle::Space(TEXT("sm")))
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(Text)
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> SChatSettingsPanel::BuildBoolRow(const FText& Label, const FText& Help,
                                                     TFunction<bool()> Get, TFunction<void(bool)> Set) const
{
	return SNew(SBox)
		.Padding(0.f, FMCPChatStyle::Space(TEXT("xs")))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Get]() { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([Set](ECheckBoxState State) { Set(State == ECheckBoxState::Checked); })
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
					.Text(Label)
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			.Padding(FMCPChatStyle::Space(TEXT("lg")), 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
				.Text(Help)
				.AutoWrapText(true)
				.Visibility(Help.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
			]
		];
}

TSharedRef<SWidget> SChatSettingsPanel::BuildIntRow(const FText& Label, const FText& Help,
                                                    int32 Min, int32 Max,
                                                    TFunction<int32()> Get, TFunction<void(int32)> Set) const
{
	return SNew(SBox)
		.Padding(0.f, FMCPChatStyle::Space(TEXT("xs")))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
					.Text(Label)
				]

				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(FMCPChatStyle::Space(TEXT("fieldW"), 140.f))
					[
						SNew(SSpinBox<int32>)
						.MinValue(Min).MaxValue(Max)
						.MinSliderValue(Min).MaxSliderValue(Max)
						.Value_Lambda([Get]() { return Get(); })
						.OnValueChanged_Lambda([Set](int32 NewValue) { Set(NewValue); })
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
				.Text(Help)
				.AutoWrapText(true)
				.Visibility(Help.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
			]
		];
}

TSharedRef<SWidget> SChatSettingsPanel::BuildStringListRow(const FText& Label, const FText& Help,
                                                           TFunction<TArray<FString>()> Get,
                                                           TFunction<void(const FString&)> Remove)
{
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);

	Column->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
		.Text(Label)
	];

	Column->AddSlot().AutoHeight()[ BuildHelpText(Help) ];

	const TArray<FString> Entries = Get();
	if (Entries.Num() == 0)
	{
		Column->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(LOCTEXT("ListEmpty", "(none)"))
		];
	}

	for (const FString& Entry : Entries)
	{
		Column->AddSlot().AutoHeight()
		.Padding(FMCPChatStyle::Space(TEXT("md")), FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.Text(FText::FromString(Entry))
			]

			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("RemoveEntry", "Remove"))
				.OnClicked_Lambda([this, Entry, Remove]()
				{
					Remove(Entry);
					RebuildBody();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.Text(FText::FromString(TEXT("×")))
				]
			]
		];
	}

	return SNew(SBox).Padding(0.f, FMCPChatStyle::Space(TEXT("xs")))[ Column ];
}

// ============================================================================
// Providers
// ============================================================================

TSharedRef<SWidget> SChatSettingsPanel::BuildProvidersTab()
{
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);

	Column->AddSlot().AutoHeight()[ BuildSectionHeading(LOCTEXT("LocalAgents", "Local agents")) ];
	Column->AddSlot().AutoHeight()
	[
		BuildHelpText(LOCTEXT("LocalAgentsHelp",
			"These use the sign-in you already have — no API key, and nothing billed through this "
			"plugin. Each is handed this project's MCP endpoint at launch, which is how it gets all "
			"of the editor tools."))
	];

	for (const FMCPChatAgentInfo& AgentInfo : FMCPChatAgentCatalog::Get().GetAgents())
	{
		if (!AgentInfo.bEnabled) { continue; }
		Column->AddSlot().AutoHeight()[ BuildAgentRow(AgentInfo.AgentId) ];
	}

	Column->AddSlot().AutoHeight()
	[
		BuildHelpText(FText::Format(LOCTEXT("AddAgentHelp",
			"To add an agent that is not listed, copy an entry from the plugin's "
			"Config/DefaultChatAgents.json into {0} and give it a new id. Any binary that speaks the "
			"Agent Client Protocol works; no plugin update is needed."),
			FText::FromString(FMCPChatAgentCatalog::GetUserCatalogPath())))
	];

	Column->AddSlot().AutoHeight()[ SNew(SSeparator).Thickness(1.f) ];
	Column->AddSlot().AutoHeight()[ BuildSectionHeading(LOCTEXT("RemoteProviders", "API providers")) ];
	Column->AddSlot().AutoHeight()
	[
		BuildHelpText(LOCTEXT("RemoteProvidersHelp",
			"Keys are stored in your OS keychain, or in an encrypted file if none is available. "
			"They are never written to a .ini and never appear in the Output Log. An environment "
			"variable, when set, always wins — so a key you export stays under your control."))
	];

	for (const FMCPChatProviderInfo& Provider : FMCPChatModelCatalog::Get().GetProviders())
	{
		Column->AddSlot().AutoHeight()[ BuildProviderRow(Provider.ProviderId) ];
	}

	return Column;
}

TSharedRef<SWidget> SChatSettingsPanel::BuildAgentRow(const FString& AgentId)
{
	const FMCPChatAgentInfo* AgentInfo = FMCPChatAgentCatalog::Get().FindAgent(AgentId);
	if (!AgentInfo) { return SNullWidget::NullWidget; }

	const FString Resolved = FMCPChatProcessRunner::ResolveExecutable(AgentInfo->Command);
	const bool bFound = !Resolved.IsEmpty();

	return SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("toolCard")))
		.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("sm")))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(0.f, 0.f, FMCPChatStyle::Space(TEXT("sm")), 0.f)
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.ColorAndOpacity(FMCPChatStyle::Color(bFound ? TEXT("success") : TEXT("fgSubdued")))
					.Text(FText::FromString(bFound ? TEXT("●") : TEXT("○")))
				]

				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Bold")))
					.Text(FText::FromString(AgentInfo->DisplayName))
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
				.Text(bFound
					? FText::FromString(Resolved)
					: FText::FromString(AgentInfo->InstallHint.IsEmpty()
						? FString::Printf(TEXT("'%s' is not on PATH."), *AgentInfo->Command)
						: AgentInfo->InstallHint))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
				.Text(FText::FromString(AgentInfo->Notes))
				.AutoWrapText(true)
				.Visibility(AgentInfo->Notes.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
			]
		];
}

TSharedRef<SWidget> SChatSettingsPanel::BuildProviderRow(const FString& ProviderId)
{
	const FMCPChatProviderInfo* Provider = FMCPChatModelCatalog::Get().FindProvider(ProviderId);
	if (!Provider) { return SNullWidget::NullWidget; }

	const FString DisplayName = Provider->DisplayName;

	// Local servers have nothing to configure here beyond their URL, and showing a
	// key field for them implies one is needed.
	if (!Provider->bRequiresKey)
	{
		return SNew(SBorder)
			.BorderImage(FMCPChatStyle::Brush(TEXT("toolCard")))
			.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("sm")))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Bold")))
					.Text(FText::FromString(DisplayName))
				]

				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
					.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
					.Text(FText::Format(LOCTEXT("LocalNoKey", "{0}  ·  no key needed"),
						FText::FromString(Provider->BaseUrl)))
				]
			];
	}

	EMCPSecretSource Source = EMCPSecretSource::None;
	FString Key;
	const bool bHasKey = FMCPChatSecretStore::Get().GetSecret(ProviderId, Key, &Source) && !Key.IsEmpty();
	const bool bRevealed = RevealedProviders.Contains(ProviderId);

	// Masked unless explicitly revealed. The full value never renders by default,
	// including in a screen share the user forgot they were in.
	const FText KeyText = bHasKey
		? FText::FromString(bRevealed ? Key : FMCPChatSecretStore::MaskSecret(Key))
		: LOCTEXT("NoKeySet", "not set");

	TSharedPtr<SEditableTextBox> KeyBox;

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);

	Column->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		.Padding(0.f, 0.f, FMCPChatStyle::Space(TEXT("sm")), 0.f)
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(bHasKey ? TEXT("success") : TEXT("fgSubdued")))
			.Text(FText::FromString(bHasKey ? TEXT("●") : TEXT("○")))
		]

		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Bold")))
			.Text(FText::FromString(DisplayName))
		]

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(bHasKey
				? FText::Format(LOCTEXT("KeyFromSource", "{0}  ·  from {1}"),
					KeyText, FMCPChatSecretStore::DescribeSource(Source))
				: KeyText)
		]
	];

	Column->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
		.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
		.Text(FText::Format(LOCTEXT("ProviderUrlAndEnv", "{0}  ·  environment variable {1}"),
			FText::FromString(Provider->BaseUrl),
			FText::FromString(FMCPChatSecretStore::GetEnvironmentVariableName(ProviderId))))
	];

	// ---- Entry + actions ----
	Column->AddSlot().AutoHeight()
	.Padding(0.f, FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SAssignNew(KeyBox, SEditableTextBox)
			.HintText(LOCTEXT("PasteKey", "Paste a key to replace…"))
			.IsPassword(true)
		]

		+ SHorizontalBox::Slot().AutoWidth()
		.Padding(FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("SaveKey", "Save"))
			.OnClicked(FOnClicked::CreateSP(this, &SChatSettingsPanel::OnSaveKey, ProviderId))
		]

		+ SHorizontalBox::Slot().AutoWidth()
		.Padding(FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.Text(bRevealed ? LOCTEXT("HideKey", "Hide") : LOCTEXT("RevealKey", "Reveal"))
			.IsEnabled(bHasKey)
			.OnClicked(FOnClicked::CreateSP(this, &SChatSettingsPanel::OnRevealKey, ProviderId))
		]

		+ SHorizontalBox::Slot().AutoWidth()
		.Padding(FMCPChatStyle::Space(TEXT("xs")), 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ClearKey", "Remove"))
			.IsEnabled(bHasKey && Source != EMCPSecretSource::Environment)
			.ToolTipText(Source == EMCPSecretSource::Environment
				? LOCTEXT("CannotRemoveEnv",
					"This key comes from an environment variable. Unset it in your shell or system "
					"settings — it is not ours to remove.")
				: LOCTEXT("RemoveKeyTip", "Delete this key from the keychain and the encrypted file."))
			.OnClicked(FOnClicked::CreateSP(this, &SChatSettingsPanel::OnClearKey, ProviderId))
		]
	];

	KeyBoxes.Add(ProviderId, KeyBox);

	if (const FText* Result = TestResults.Find(ProviderId))
	{
		Column->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(*Result)
			.AutoWrapText(true)
		];
	}

	return SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("toolCard")))
		.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("sm")))
		[
			Column
		];
}

FReply SChatSettingsPanel::OnRevealKey(FString ProviderId)
{
	if (RevealedProviders.Contains(ProviderId)) { RevealedProviders.Remove(ProviderId); }
	else                                        { RevealedProviders.Add(ProviderId); }

	// Rebuilt rather than toggled in place, so what is on screen cannot drift from
	// the reveal state.
	RebuildBody();
	return FReply::Handled();
}

FReply SChatSettingsPanel::OnSaveKey(FString ProviderId)
{
	const TSharedPtr<SEditableTextBox>* Box = KeyBoxes.Find(ProviderId);
	if (!Box || !Box->IsValid()) { return FReply::Handled(); }

	const FString NewKey = (*Box)->GetText().ToString().TrimStartAndEnd();
	if (NewKey.IsEmpty()) { return FReply::Handled(); }

	const bool bSaved = FMCPChatSecretStore::Get().SetSecret(ProviderId, NewKey);

	// Clear the field immediately. A key left sitting in a widget is a key in the
	// next screenshot.
	(*Box)->SetText(FText::GetEmpty());

	TestResults.Add(ProviderId, bSaved
		? LOCTEXT("KeySaved", "Saved.")
		: LOCTEXT("KeySaveFailed", "Could not store the key. Check the Output Log."));

	RebuildBody();
	return FReply::Handled();
}

FReply SChatSettingsPanel::OnClearKey(FString ProviderId)
{
	FMCPChatSecretStore::Get().ClearSecret(ProviderId);
	TestResults.Add(ProviderId, LOCTEXT("KeyRemoved", "Removed."));
	RebuildBody();
	return FReply::Handled();
}

// ============================================================================
// AI settings
// ============================================================================

TSharedRef<SWidget> SChatSettingsPanel::BuildAITab()
{
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);

	const FChatSessionPtr Session = Controller.IsValid() ? Controller->GetActiveSession() : nullptr;
	if (!Session.IsValid())
	{
		Column->AddSlot().AutoHeight()
		[ BuildHelpText(LOCTEXT("NoSessionForAI", "Open a conversation to change its AI settings.")) ];
		return Column;
	}

	const FChatModelInfo* Model = FMCPChatModelCatalog::Get().FindModel(Session->ModelId);

	Column->AddSlot().AutoHeight()[ BuildSectionHeading(LOCTEXT("ForThisChat", "For this conversation")) ];
	Column->AddSlot().AutoHeight()
	[
		BuildHelpText(FText::Format(LOCTEXT("AIModelLine", "Model: {0}"),
			FText::FromString(Model ? Model->DisplayName : Session->ModelId)))
	];

	// ---- Output length ----
	{
		const int32 Ceiling = (Model && Model->MaxOutputTokens > 0) ? Model->MaxOutputTokens : 128000;
		FChatSessionPtr Captured = Session;
		Column->AddSlot().AutoHeight()
		[
			BuildIntRow(LOCTEXT("MaxOutput", "Maximum reply length (tokens)"),
				LOCTEXT("MaxOutputHelp",
					"On models where thinking is on, this caps thinking AND the answer together, so a "
					"value sized for the answer alone can truncate mid-sentence."),
				256, Ceiling,
				[Captured]() { return Captured->Params.MaxOutputTokens; },
				[Captured](int32 V) { Captured->Params.MaxOutputTokens = V; FMCPChatStore::Get().MarkDirty(Captured->Id); })
		];
	}

	// ---- Thinking ----
	{
		FChatSessionPtr Captured = Session;
		Column->AddSlot().AutoHeight()
		[
			BuildBoolRow(LOCTEXT("Thinking", "Let the model think before answering"),
				LOCTEXT("ThinkingHelp", "Slower and more expensive, and noticeably better on multi-step editor work."),
				[Captured]() { return Captured->Params.bThinkingEnabled; },
				[Captured](bool V) { Captured->Params.bThinkingEnabled = V; FMCPChatStore::Get().MarkDirty(Captured->Id); })
		];
		Column->AddSlot().AutoHeight()
		[
			BuildBoolRow(LOCTEXT("ThinkingVisible", "Show the thinking in the transcript"),
				LOCTEXT("ThinkingVisibleHelp",
					"With this off, providers stream thinking blocks with empty text, so the section "
					"would render blank rather than being hidden."),
				[Captured]() { return Captured->Params.bThinkingVisible; },
				[Captured](bool V) { Captured->Params.bThinkingVisible = V; FMCPChatStore::Get().MarkDirty(Captured->Id); })
		];
	}

	// ---- Sampling: shown ONLY when the catalogue says the model accepts it ----
	if (Model && Model->bSampling)
	{
		FChatSessionPtr Captured = Session;
		Column->AddSlot().AutoHeight()
		[
			BuildBoolRow(LOCTEXT("UseTemperature", "Send a temperature value"),
				LOCTEXT("UseTemperatureHelp", "Leave off to use the provider's default."),
				[Captured]() { return Captured->Params.bHasSampling; },
				[Captured](bool V) { Captured->Params.bHasSampling = V; FMCPChatStore::Get().MarkDirty(Captured->Id); })
		];
	}
	else
	{
		// Said out loud rather than left as a missing control, or the absence reads
		// as a bug in the settings page.
		Column->AddSlot().AutoHeight()
		[
			BuildHelpText(LOCTEXT("NoSampling",
				"This model rejects temperature and top-p, so there is nothing to set. The controls are "
				"hidden rather than disabled — sending those values would fail the request."))
		];
	}

	// ---- Tool exposure, with the real cost of each option ----
	{
		FMCPChatToolBridge& Bridge = FMCPChatToolBridge::Get();
		const int32 CatalogTokens = Bridge.EstimateSchemaTokens(true);
		const int32 FullTokens    = Bridge.EstimateSchemaTokens(false);

		FChatSessionPtr Captured = Session;
		Column->AddSlot().AutoHeight()[ BuildSectionHeading(LOCTEXT("Tools", "Editor tools")) ];
		Column->AddSlot().AutoHeight()
		[
			BuildBoolRow(
				FText::Format(LOCTEXT("CatalogMode", "Send a starter set of tools (about {0} tokens)"),
					FText::AsNumber(CatalogTokens)),
				FText::Format(LOCTEXT("CatalogModeHelp",
					"On (recommended): the model gets the discovery tools plus a high-frequency core, and "
					"finds the rest on demand. Off: every tool schema is sent on every message, about {0} "
					"tokens — more than most whole conversations."),
					FText::AsNumber(FullTokens)),
				[Captured]() { return Captured->Params.bCatalogToolExposure; },
				[Captured](bool V) { Captured->Params.bCatalogToolExposure = V; FMCPChatStore::Get().MarkDirty(Captured->Id); })
		];

		Column->AddSlot().AutoHeight()
		[
			BuildIntRow(LOCTEXT("MaxIterations", "Maximum tool rounds per message"),
				LOCTEXT("MaxIterationsHelp", "Guards against a model looping. This is the per-conversation value."),
				1, 100,
				[Captured]() { return Captured->Params.MaxToolIterations; },
				[Captured](int32 V) { Captured->Params.MaxToolIterations = V; FMCPChatStore::Get().MarkDirty(Captured->Id); })
		];
	}

	// ---- Caching ----
	{
		FChatSessionPtr Captured = Session;
		Column->AddSlot().AutoHeight()
		[
			BuildBoolRow(LOCTEXT("PromptCaching", "Cache the unchanging part of each request"),
				LOCTEXT("PromptCachingHelp",
					"The tool schemas and the earlier conversation are identical from one message to the "
					"next. Caching them is the single largest cost saving available; leave it on unless a "
					"provider misbehaves."),
				[Captured]() { return Captured->Params.bPromptCaching; },
				[Captured](bool V) { Captured->Params.bPromptCaching = V; FMCPChatStore::Get().MarkDirty(Captured->Id); })
		];
	}

	return Column;
}

// ============================================================================
// Permissions
// ============================================================================

TSharedRef<SWidget> SChatSettingsPanel::BuildPermissionsTab()
{
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	UMCPChatSettings* Settings = GetMutableDefault<UMCPChatSettings>();
	if (!Settings) { return Column; }

	Column->AddSlot().AutoHeight()[ BuildSectionHeading(LOCTEXT("ApprovalHeading", "When to ask")) ];

	// The four modes as explicit choices rather than a combo, because each carries a
	// consequence worth reading before choosing.
	struct FModeOption { EMCPChatApprovalMode Mode; FText Label; FText Help; };
	const FModeOption Options[] =
	{
		{ EMCPChatApprovalMode::AskDestructive,
		  LOCTEXT("ModeDestructive", "Ask before destructive tools (recommended)"),
		  LOCTEXT("ModeDestructiveHelp", "Deletes and overwrites ask; ordinary edits run.") },
		{ EMCPChatApprovalMode::AskWrites,
		  LOCTEXT("ModeWrites", "Ask before every change"),
		  LOCTEXT("ModeWritesHelp", "Anything that modifies the project stops and asks.") },
		{ EMCPChatApprovalMode::ReadOnly,
		  LOCTEXT("ModeRead", "Read-only"),
		  LOCTEXT("ModeReadHelp", "Changes are refused outright, so there is nothing to approve.") },
		{ EMCPChatApprovalMode::AllowAll,
		  LOCTEXT("ModeAll", "Never ask"),
		  LOCTEXT("ModeAllHelp",
			  "⚠ Nothing stops to confirm, including deletes. Only sensible with source control "
			  "and a recent commit. The status bar shows a warning while this is on.") },
	};

	for (const FModeOption& Option : Options)
	{
		const EMCPChatApprovalMode Mode = Option.Mode;
		Column->AddSlot().AutoHeight()
		[
			BuildBoolRow(Option.Label, Option.Help,
				[Settings, Mode]() { return Settings->ApprovalMode == Mode; },
				[Settings, Mode](bool bChecked)
				{
					// A radio group made of checkboxes: unchecking the selected one would
					// leave no mode at all, so only the check direction acts.
					if (bChecked)
					{
						Settings->ApprovalMode = Mode;
						Settings->SaveConfig();
					}
				})
		];
	}

	Column->AddSlot().AutoHeight()[ BuildSectionHeading(LOCTEXT("ToolLists", "Per-tool decisions")) ];

	Column->AddSlot().AutoHeight()
	[
		BuildStringListRow(LOCTEXT("AlwaysAllowed", "Always allowed"),
			LOCTEXT("AlwaysAllowedHelp",
				"Added by choosing \"Always allow\" on an approval card. These run without asking, in "
				"every conversation. Remove one to start being asked again."),
			[Settings]() { return Settings->AlwaysAllowedTools; },
			[Settings](const FString& Tool) { FMCPChatToolBridge::Get().RevokeAlways(Tool); })
	];

	Column->AddSlot().AutoHeight()
	[
		BuildStringListRow(LOCTEXT("Blocked", "Never allowed"),
			LOCTEXT("BlockedHelp",
				"These can never run, whatever the mode above says. This list wins over \"Never ask\"."),
			[Settings]() { return Settings->BlockedTools; },
			[Settings](const FString& Tool)
			{
				Settings->BlockedTools.Remove(Tool);
				Settings->SaveConfig();
			})
	];

	Column->AddSlot().AutoHeight()[ BuildSectionHeading(LOCTEXT("UndoHeading", "Undo")) ];
	Column->AddSlot().AutoHeight()
	[
		BuildBoolRow(LOCTEXT("SingleUndo", "One undo step per message"),
			LOCTEXT("SingleUndoHelp",
				"A message that spawns twenty actors becomes a single Ctrl+Z. Turn this off only if you "
				"want each tool to be its own undo step."),
			[Settings]() { return Settings->bSingleUndoPerTurn; },
			[Settings](bool V) { Settings->bSingleUndoPerTurn = V; Settings->SaveConfig(); })
	];

	Column->AddSlot().AutoHeight()
	[
		BuildIntRow(LOCTEXT("GlobalMaxIterations", "Default maximum tool rounds per message"),
			LOCTEXT("GlobalMaxIterationsHelp", "Applies to new conversations."),
			1, 100,
			[Settings]() { return Settings->MaxToolIterationsPerTurn; },
			[Settings](int32 V) { Settings->MaxToolIterationsPerTurn = V; Settings->SaveConfig(); })
	];

	// ---- The agent seam, stated in the place people come to reason about safety ----
	Column->AddSlot().AutoHeight()[ BuildSectionHeading(LOCTEXT("AgentsHeading", "Local agents")) ];
	Column->AddSlot().AutoHeight()
	[
		BuildHelpText(LOCTEXT("AgentSeamHelp",
			"A local agent runs its own tool loop against this project's MCP endpoint. The settings "
			"above govern what it ASKS this panel about — where it asks, they are applied and can deny "
			"it. What it does without asking is governed by the MCP server's own scope setting, in "
			"Project Settings ▸ Plugins ▸ Unreal MCP Server."))
	];

	return Column;
}

// ============================================================================
// Appearance & data
// ============================================================================

TSharedRef<SWidget> SChatSettingsPanel::BuildAppearanceTab()
{
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	UMCPChatSettings* Settings = GetMutableDefault<UMCPChatSettings>();
	if (!Settings) { return Column; }

	Column->AddSlot().AutoHeight()[ BuildSectionHeading(LOCTEXT("ThemeHeading", "Theme")) ];
	Column->AddSlot().AutoHeight()
	[
		BuildHelpText(FText::Format(LOCTEXT("ThemeHelp",
			"Themes are JSON files in {0}. Edit one and save: the panel restyles within a second, with "
			"no recompile and no restart. The default theme uses semantic colours, so it follows the "
			"editor's own Light/Dark setting."),
			FText::FromString(FMCPChatStyle::GetThemeDirectory())))
	];

	for (const FString& ThemeName : FMCPChatStyle::DiscoverThemeNames())
	{
		Column->AddSlot().AutoHeight()
		[
			BuildBoolRow(FText::FromString(ThemeName), FText::GetEmpty(),
				[Settings, ThemeName]() { return Settings->ThemeName == ThemeName; },
				[Settings, ThemeName](bool bChecked)
				{
					if (!bChecked) { return; }
					Settings->ThemeName = ThemeName;
					Settings->SaveConfig();
					FMCPChatStyle::ReloadTheme(ThemeName);
				})
		];
	}

	Column->AddSlot().AutoHeight()
	[
		BuildBoolRow(LOCTEXT("HotReload", "Reload a theme automatically when its file changes"),
			LOCTEXT("HotReloadHelp", "Turn off if you edit themes on a network drive and the watcher is noisy."),
			[Settings]() { return Settings->bHotReloadThemes; },
			[Settings](bool V) { Settings->bHotReloadThemes = V; Settings->SaveConfig(); })
	];

	Column->AddSlot().AutoHeight()[ BuildSectionHeading(LOCTEXT("ComposerHeading", "Composer")) ];
	Column->AddSlot().AutoHeight()
	[
		BuildBoolRow(LOCTEXT("EnterNewline", "Enter starts a new line; Ctrl+Enter sends"),
			LOCTEXT("EnterNewlineHelp",
				"Off (default): Enter sends and Shift+Enter starts a new line. The composer's placeholder "
				"always states whichever binding is in force."),
			[Settings]() { return Settings->bEnterInsertsNewline; },
			[Settings](bool V) { Settings->bEnterInsertsNewline = V; Settings->SaveConfig(); })
	];

	Column->AddSlot().AutoHeight()[ BuildSectionHeading(LOCTEXT("LayoutHeading", "Layout")) ];
	Column->AddSlot().AutoHeight()
	[
		BuildIntRow(LOCTEXT("MaxContentWidth", "Maximum message width"),
			LOCTEXT("MaxContentWidthHelp",
				"Lines longer than roughly 90 characters are measurably harder to read, so the transcript "
				"centres its content rather than filling a very wide panel."),
			400, 1400,
			[Settings]() { return Settings->TranscriptMaxContentWidth; },
			[Settings](int32 V) { Settings->TranscriptMaxContentWidth = V; Settings->SaveConfig(); })
	];

	Column->AddSlot().AutoHeight()
	[
		BuildIntRow(LOCTEXT("RailWidth", "History rail width"), FText::GetEmpty(), 160, 600,
			[Settings]() { return Settings->HistoryRailWidth; },
			[Settings](int32 V) { Settings->HistoryRailWidth = V; Settings->SaveConfig(); })
	];

	Column->AddSlot().AutoHeight()[ BuildSectionHeading(LOCTEXT("DataHeading", "Your data")) ];
	Column->AddSlot().AutoHeight()
	[
		BuildHelpText(FText::Format(LOCTEXT("DataHelp",
			"Conversations are plain JSON in {0} — greppable, diffable, and yours. Nothing is sent "
			"anywhere except the provider you chose for a given message."),
			FText::FromString(FMCPChatStore::GetRootDirectory())))
	];

	Column->AddSlot().AutoHeight()
	.Padding(0.f, FMCPChatStyle::Space(TEXT("sm")), 0.f, 0.f)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("OpenFolder", "Open the conversations folder"))
			.OnClicked_Lambda([]()
			{
				FPlatformProcess::ExploreFolder(*FMCPChatStore::GetRootDirectory());
				return FReply::Handled();
			})
		]
	];

	return Column;
}

#undef LOCTEXT_NAMESPACE
