// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SChatModelPicker.h"
#include "MCPChatController.h"
#include "MCPChatModelCatalog.h"
#include "MCPChatSecretStore.h"
#include "MCPChatStyle.h"
#include "UnrealMCPChatModule.h"

#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SChatModelPicker"

namespace
{
	/** Filled = ready, hollow = something is missing. Two glyphs rather than colour
	 *  alone, so the state survives a monochrome theme and colour-blind vision. */
	const TCHAR* ReadyGlyph   = TEXT("●");
	const TCHAR* MissingGlyph = TEXT("○");

	bool MatchesSearch(const FString& Search, const FString& A, const FString& B)
	{
		if (Search.IsEmpty()) { return true; }
		return A.Contains(Search) || B.Contains(Search);
	}
}

void SChatModelPicker::Construct(const FArguments& InArgs)
{
	Controller  = InArgs._Controller;
	CompactMode = InArgs._CompactMode;

	ChildSlot
	[
		SAssignNew(ComboButton, SComboButton)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.ContentPadding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
		.ToolTipText(this, &SChatModelPicker::GetButtonTooltip)
		.OnGetMenuContent(this, &SChatModelPicker::BuildMenuContent)
		.ButtonContent()
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.Text(this, &SChatModelPicker::GetButtonText)
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		]
	];
}

void SChatModelPicker::OpenMenu()
{
	if (ComboButton.IsValid()) { ComboButton->SetIsOpen(true); }
}

FText SChatModelPicker::GetButtonText() const
{
	if (!Controller.IsValid()) { return LOCTEXT("NoController", "—"); }

	const FChatSessionPtr Session = Controller->GetActiveSession();
	if (!Session.IsValid()) { return LOCTEXT("NoSession", "—"); }

	FString Label;
	if (const FChatModelInfo* Model = FMCPChatModelCatalog::Get().FindModel(Session->ModelId))
	{
		Label = Model->DisplayName;
	}
	else if (const FChatBackendPtr Backend = Controller->FindBackend(Session->BackendId))
	{
		Label = Backend->GetDisplayName().ToString();
	}
	if (Label.IsEmpty()) { Label = LOCTEXT("PickModel", "Choose a model").ToString(); }

	if (CompactMode.Get(false))
	{
		// Initials of the first two words: "Claude Opus 5" → "CO". Enough to tell two
		// models apart at a glance, which is all the narrow layout has room for.
		TArray<FString> Words;
		Label.ParseIntoArray(Words, TEXT(" "), true);
		FString Badge;
		for (int32 i = 0; i < FMath::Min(2, Words.Num()); ++i)
		{
			if (!Words[i].IsEmpty()) { Badge.AppendChar(FChar::ToUpper(Words[i][0])); }
		}
		return FText::FromString(Badge.IsEmpty() ? TEXT("··") : Badge);
	}

	return FText::FromString(Label);
}

FText SChatModelPicker::GetButtonTooltip() const
{
	if (!Controller.IsValid()) { return FText::GetEmpty(); }

	const FChatSessionPtr Session = Controller->GetActiveSession();
	if (!Session.IsValid()) { return FText::GetEmpty(); }

	if (const FChatBackendPtr Backend = Controller->FindBackend(Session->BackendId))
	{
		FText Reason;
		if (!Backend->IsAvailable(Reason))
		{
			return FText::Format(LOCTEXT("BackendUnavailableTip", "{0} is not usable: {1}"),
				Backend->GetDisplayName(), Reason);
		}
	}

	return FText::Format(LOCTEXT("ModelTip", "{0}\nClick to switch model or agent."),
		FText::FromString(Session->ModelId.IsEmpty() ? Session->BackendId : Session->ModelId));
}

FString SChatModelPicker::DescribeModel(const FChatModelInfo& Model)
{
	FString Context;
	if (Model.ContextTokens >= 1'000'000)  { Context = FString::Printf(TEXT("%dM"), Model.ContextTokens / 1'000'000); }
	else if (Model.ContextTokens >= 1000)  { Context = FString::Printf(TEXT("%dK"), Model.ContextTokens / 1000); }

	if (Model.PriceInPerMillion <= 0.0 && Model.PriceOutPerMillion <= 0.0)
	{
		// Zero price means locally hosted, not free-of-charge-from-a-provider. Saying
		// "free" would be misleading for an Ollama model that costs you a GPU.
		return Context.IsEmpty() ? TEXT("local") : (Context + TEXT("  ·  local"));
	}

	const FString Price = FString::Printf(TEXT("$%g/$%g"), Model.PriceInPerMillion, Model.PriceOutPerMillion);
	return Context.IsEmpty() ? Price : (Context + TEXT("  ·  ") + Price);
}

TSharedRef<SWidget> SChatModelPicker::BuildMenuContent()
{
	SearchText.Reset();

	TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

	Root->AddSlot().AutoHeight()
	.Padding(FMCPChatStyle::Padding(TEXT("sm")))
	[
		SAssignNew(SearchBox, SEditableTextBox)
		.HintText(LOCTEXT("SearchModels", "Search models…"))
		.OnTextChanged_Lambda([this](const FText& NewText)
		{
			SearchText = NewText.ToString();
			RebuildRows();
		})
	];

	Root->AddSlot().FillHeight(1.f)
	[
		SNew(SBox)
		.MaxDesiredHeight(FMCPChatStyle::Space(TEXT("popupMaxH"), 420.f))
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(RowContainer, SVerticalBox)
			]
		]
	];

	Root->AddSlot().AutoHeight()
	[
		SNew(SSeparator).Thickness(1.f)
	];

	Root->AddSlot().AutoHeight()
	.Padding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.HAlign(HAlign_Left)
		.OnClicked_Lambda([]()
		{
			// The chat settings TAB, not the Project Settings page. That page is
			// deliberately key-free — sending someone there to add a key is the exact
			// dead end this button existed to avoid.
			FGlobalTabmanager::Get()->TryInvokeTab(FUnrealMCPChatModule::SettingsTabId);
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.Text(LOCTEXT("ManageProviders", "Manage providers and keys…"))
		]
	];

	RebuildRows();

	return SNew(SBorder)
		.BorderImage(FMCPChatStyle::Brush(TEXT("popupBg")))
		.Padding(0.f)
		[
			SNew(SBox).MinDesiredWidth(FMCPChatStyle::Space(TEXT("popupMinW"), 340.f))
			[
				Root
			]
		];
}

void SChatModelPicker::RebuildRows()
{
	if (!RowContainer.IsValid() || !Controller.IsValid()) { return; }

	RowContainer->ClearChildren();

	// ---- Local agents first ----
	// They cost nothing per message and need no key, so they are the right default
	// for someone who has just installed the plugin and has no account anywhere.
	bool bAnyAgent = false;
	for (const FChatBackendPtr& Backend : Controller->GetBackends())
	{
		if (!Backend.IsValid() || Backend->GetKind() != EChatBackendKind::Agent) { continue; }

		const FString AgentName = Backend->GetDisplayName().ToString();
		const TArray<FChatModelInfo> AgentModels = Backend->GetModels();

		// An agent matches if its own name matches OR one of its models does, so
		// searching "opus" finds Claude Code's Opus row.
		bool bMatches = MatchesSearch(SearchText, Backend->GetId(), AgentName);
		if (!bMatches)
		{
			for (const FChatModelInfo& Model : AgentModels)
			{
				if (MatchesSearch(SearchText, Model.ModelId, Model.DisplayName)) { bMatches = true; break; }
			}
		}
		if (!bMatches) { continue; }

		if (!bAnyAgent)
		{
			RowContainer->AddSlot().AutoHeight()
			[ BuildProviderHeading(FString(), LOCTEXT("LocalAgents", "LOCAL AGENTS")) ];
			bAnyAgent = true;
		}

		// The agent itself is always a row: picking it means "you choose the model",
		// which is a legitimate answer and the only one for agents with no list.
		RowContainer->AddSlot().AutoHeight()[ BuildAgentRow(Backend) ];

		// Its models are indented beneath it. Rendering them as siblings of the
		// agent would read as five separate agents.
		for (const FChatModelInfo& Model : AgentModels)
		{
			if (!MatchesSearch(SearchText, Model.ModelId, Model.DisplayName)
				&& !MatchesSearch(SearchText, Backend->GetId(), AgentName))
			{
				continue;
			}
			RowContainer->AddSlot().AutoHeight()
			.Padding(FMCPChatStyle::Space(TEXT("md")), 0.f, 0.f, 0.f)
			[ BuildModelRow(Model, Backend->GetId()) ];
		}
	}

	// ---- Provider models, grouped ----
	const FMCPChatModelCatalog& Catalog = FMCPChatModelCatalog::Get();
	for (const FMCPChatProviderInfo& Provider : Catalog.GetProviders())
	{
		const TArray<FChatModelInfo> Models = Catalog.GetModelsForProvider(Provider.ProviderId);

		TArray<FChatModelInfo> Visible;
		for (const FChatModelInfo& M : Models)
		{
			if (MatchesSearch(SearchText, M.ModelId, M.DisplayName)) { Visible.Add(M); }
		}
		if (Visible.Num() == 0) { continue; }

		RowContainer->AddSlot().AutoHeight()
		[ BuildProviderHeading(Provider.ProviderId, FText::FromString(Provider.DisplayName.ToUpper())) ];

		for (const FChatModelInfo& M : Visible)
		{
			RowContainer->AddSlot().AutoHeight()[ BuildModelRow(M, Provider.ProviderId) ];
		}
	}

	if (RowContainer->NumSlots() == 0)
	{
		RowContainer->AddSlot().AutoHeight()
		.Padding(FMCPChatStyle::Padding(TEXT("md")))
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
			.Text(LOCTEXT("NoModelsMatch", "Nothing matches."))
		];
	}
}

TSharedRef<SWidget> SChatModelPicker::BuildProviderHeading(const FString& ProviderId, const FText& Label) const
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	Row->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
		.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
		.Text(Label)
	];

	// Key state belongs on the GROUP, not on every model in it — the key is per
	// provider, and repeating "no key" twelve times is noise.
	if (!ProviderId.IsEmpty())
	{
		const FMCPChatProviderInfo* Provider = FMCPChatModelCatalog::Get().FindProvider(ProviderId);
		const bool bNeedsKey = !Provider || Provider->bRequiresKey;
		if (bNeedsKey)
		{
			FString Unused;
			const bool bHasKey = FMCPChatSecretStore::Get().GetSecret(ProviderId, Unused) && !Unused.IsEmpty();

			Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(bHasKey ? TEXT("success") : TEXT("readoutWarn")))
				.Text(bHasKey ? LOCTEXT("KeyPresent", "key ✓") : LOCTEXT("KeyMissing", "no key"))
			];
		}
	}

	return SNew(SBox)
		.Padding(FMCPChatStyle::Padding(TEXT("sm"), TEXT("xs")))
		[
			Row
		];
}

TSharedRef<SWidget> SChatModelPicker::BuildModelRow(const FChatModelInfo& Model, const FString& BackendId)
{
	// An agent's models are reached through the agent's own sign-in, so there is no
	// provider row and no key to check. Treating a missing provider as "needs a key"
	// would put a hollow dot and a bogus warning on every agent model.
	const FChatBackendPtr OwningBackend = Controller.IsValid() ? Controller->FindBackend(BackendId) : nullptr;
	const bool bIsAgentModel = OwningBackend.IsValid() && OwningBackend->GetKind() == EChatBackendKind::Agent;

	const FMCPChatProviderInfo* Provider = FMCPChatModelCatalog::Get().FindProvider(BackendId);
	const bool bNeedsKey = !bIsAgentModel && (!Provider || Provider->bRequiresKey);

	bool bReady = true;
	FText NotReadyReason;
	if (bIsAgentModel)
	{
		bReady = OwningBackend->IsAvailable(NotReadyReason);
	}
	else if (bNeedsKey)
	{
		FString Key;
		bReady = FMCPChatSecretStore::Get().GetSecret(BackendId, Key) && !Key.IsEmpty();
		if (!bReady)
		{
			NotReadyReason = FText::Format(
				LOCTEXT("NoKeyFor", "No API key for {0}. Add one in Settings, or set the provider's environment variable."),
				FText::FromString(Provider ? Provider->DisplayName : BackendId));
		}
	}

	const FString ModelId = Model.ModelId;
	const FString Description = bIsAgentModel
		? LOCTEXT("ViaAgent", "via your sign-in").ToString()
		: DescribeModel(Model);

	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMCPChatStyle::Padding(TEXT("md"), TEXT("xs")))
		.ToolTipText(bReady ? FText::FromString(ModelId) : NotReadyReason)
		// A model with no key stays SELECTABLE. Choosing it and being told what is
		// missing is a better path than a greyed-out row with no explanation — and
		// the key may arrive from an environment variable before the next send.
		.OnClicked_Lambda([this, BackendId, ModelId]()
		{
			SelectModel(BackendId, ModelId);
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			.Padding(0.f, 0.f, FMCPChatStyle::Space(TEXT("sm")), 0.f)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(bReady ? TEXT("success") : TEXT("fgSubdued")))
				.Text(FText::FromString(bReady ? ReadyGlyph : MissingGlyph))
			]

			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
				.Text(FText::FromString(Model.DisplayName))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			.Padding(FMCPChatStyle::Space(TEXT("md")), 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
				.Text(FText::FromString(Description))
			]
		];
}

TSharedRef<SWidget> SChatModelPicker::BuildAgentRow(const FChatBackendPtr& Backend)
{
	FText Reason;
	const bool bReady = Backend->IsAvailable(Reason);
	const FString BackendId = Backend->GetId();

	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMCPChatStyle::Padding(TEXT("md"), TEXT("xs")))
		.ToolTipText(bReady ? Backend->GetDisplayName() : Reason)
		.OnClicked_Lambda([this, BackendId]()
		{
			SelectModel(BackendId, FString());
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			.Padding(0.f, 0.f, FMCPChatStyle::Space(TEXT("sm")), 0.f)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(bReady ? TEXT("success") : TEXT("fgSubdued")))
				.Text(FText::FromString(bReady ? ReadyGlyph : MissingGlyph))
			]

			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
				.Text(Backend->GetDisplayName())
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
				.Text(bReady ? LOCTEXT("Subscription", "subscription") : LOCTEXT("NotFound", "not found"))
			]
		];
}

void SChatModelPicker::SelectModel(const FString& BackendId, const FString& ModelId)
{
	if (Controller.IsValid())
	{
		Controller->SwitchModel(BackendId, ModelId);
	}
	if (ComboButton.IsValid())
	{
		ComboButton->SetIsOpen(false);
	}
}

#undef LOCTEXT_NAMESPACE
