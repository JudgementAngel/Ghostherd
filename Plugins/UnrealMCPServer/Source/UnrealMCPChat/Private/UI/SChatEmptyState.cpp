// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SChatEmptyState.h"
#include "MCPChatController.h"
#include "MCPChatStyle.h"
#include "UnrealMCPChatModule.h"

#include "MCPHttpServer.h"
#include "MCPToolRegistry.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Selection.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SChatEmptyState"

void SChatEmptyState::Construct(const FArguments& InArgs)
{
	Controller         = InArgs._Controller;
	OnSuggestionChosen = InArgs._OnSuggestionChosen;
	OnOpenSettings     = InArgs._OnOpenSettings;

	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.MaxDesiredWidth(FMCPChatStyle::Space(TEXT("emptyStateW"), 520.f))
		[
			SAssignNew(Root, SVerticalBox)
		]
	];

	Refresh();
}

bool SChatEmptyState::IsAnythingConfigured() const
{
	if (!Controller.IsValid()) { return false; }

	for (const FChatBackendPtr& Backend : Controller->GetBackends())
	{
		if (!Backend.IsValid() || Backend->GetKind() == EChatBackendKind::Mock) { continue; }

		FText Reason;
		if (Backend->IsAvailable(Reason)) { return true; }
	}
	return false;
}

void SChatEmptyState::Refresh()
{
	if (!Root.IsValid()) { return; }

	Root->ClearChildren();
	Root->AddSlot().AutoHeight()
	[
		IsAnythingConfigured() ? BuildReadyState() : BuildFirstRun()
	];
}

// ============================================================================
// First run
// ============================================================================

TSharedRef<SWidget> SChatEmptyState::BuildFirstRun()
{
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);

	Column->AddSlot().AutoHeight()
	.Padding(0.f, 0.f, 0.f, FMCPChatStyle::Space(TEXT("sm")))
	[
		SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.H2")))
		.Text(LOCTEXT("FirstRunTitle", "Chat with your project"))
		.Justification(ETextJustify::Center)
	];

	Column->AddSlot().AutoHeight()
	.Padding(0.f, 0.f, 0.f, FMCPChatStyle::Space(TEXT("lg")))
	[
		SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
		.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
		.Text(FText::Format(LOCTEXT("FirstRunBody",
			"Ask for changes in plain language and they happen in the editor. {0} tools are wired up "
			"and ready. Pick how you want to connect:"),
			FText::AsNumber(FMCPToolRegistry::Get().GetToolCount())))
		.AutoWrapText(true)
		.Justification(ETextJustify::Center)
	];

	auto MakeChoice = [this](const FText& Title, const FText& Body, const FText& Cost)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(0.f)
			.OnClicked_Lambda([this]() { OnOpenSettings.ExecuteIfBound(); return FReply::Handled(); })
			[
				SNew(SBorder)
				.BorderImage(FMCPChatStyle::Brush(TEXT("toolCard")))
				.Padding(FMCPChatStyle::Padding(TEXT("md")))
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Bold")))
						.Text(Title)
					]

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
						.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
						.Text(Body)
						.AutoWrapText(true)
					]

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
						.ColorAndOpacity(FMCPChatStyle::Color(TEXT("success")))
						.Text(Cost)
					]
				]
			];
	};

	// The free option first, deliberately. Someone who already has Claude Code
	// installed should not be asked for a credit card to try this.
	Column->AddSlot().AutoHeight()
	.Padding(0.f, 0.f, 0.f, FMCPChatStyle::Space(TEXT("sm")))
	[
		MakeChoice(
			LOCTEXT("ChoiceAgent", "Use a local agent"),
			LOCTEXT("ChoiceAgentBody",
				"Claude Code, Codex, Gemini CLI, Kimi, OpenCode or iFlow. The panel starts it for you "
				"and hands it this project's editor tools."),
			LOCTEXT("ChoiceAgentCost", "Uses the sign-in you already have"))
	];

	Column->AddSlot().AutoHeight()
	.Padding(0.f, 0.f, 0.f, FMCPChatStyle::Space(TEXT("sm")))
	[
		MakeChoice(
			LOCTEXT("ChoiceKey", "Use an API key"),
			LOCTEXT("ChoiceKeyBody",
				"Anthropic, OpenAI, OpenRouter, Groq, DeepSeek — or a local server like Ollama or "
				"LM Studio, which needs no key at all."),
			LOCTEXT("ChoiceKeyCost", "Billed by the provider, never through this plugin"))
	];

	// The server backs everything above; if it is down, saying so here saves a
	// confusing first conversation.
	if (!FMCPHttpServer::Get().IsRunning())
	{
		Column->AddSlot().AutoHeight()
		.Padding(0.f, FMCPChatStyle::Space(TEXT("md")), 0.f, 0.f)
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.ColorAndOpacity(FMCPChatStyle::Color(TEXT("readoutWarn")))
			.Text(LOCTEXT("ServerDown",
				"The MCP server is not running, so local agents will not see the editor tools. "
				"Check Project Settings ▸ Plugins ▸ Unreal MCP Server."))
			.AutoWrapText(true)
		];
	}

	return Column;
}

// ============================================================================
// Configured, empty
// ============================================================================

TArray<TPair<FText, FString>> SChatEmptyState::GatherSuggestions() const
{
	TArray<TPair<FText, FString>> Out;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

	// Selection first — it is the most specific thing we can know, and a suggestion
	// naming the user's own actor demonstrates the panel can see the project in a
	// way a generic prompt never does.
	if (GEditor)
	{
		if (USelection* Selection = GEditor->GetSelectedActors())
		{
			const int32 Count = Selection->Num();
			if (Count == 1)
			{
				if (const AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(0)))
				{
					Out.Emplace(
						FText::Format(LOCTEXT("SuggestSelected", "Explain {0}"),
							FText::FromString(Actor->GetActorLabel())),
						FString::Printf(TEXT("Explain what %s is and how it is set up."),
							*Actor->GetActorLabel()));
				}
			}
			else if (Count > 1)
			{
				Out.Emplace(
					FText::Format(LOCTEXT("SuggestSelection", "Work with my {0} selected actors"),
						FText::AsNumber(Count)),
					TEXT("Describe the actors I have selected and suggest improvements."));
			}
		}
	}

	if (World)
	{
		Out.Emplace(LOCTEXT("SuggestLevel", "Analyse this level"),
			TEXT("Analyse the current level and tell me what stands out — lighting, "
			     "performance risks, anything obviously missing."));
	}

	Out.Emplace(LOCTEXT("SuggestPerf", "Why is my frame rate low?"),
		TEXT("Check the current render statistics and tell me what is costing the most."));

	Out.Emplace(LOCTEXT("SuggestTools", "What can you do here?"),
		TEXT("List the kinds of things you can do in this project, with a few concrete examples."));

	// Three fit on one row at every breakpoint; four wrap and stop reading as a set.
	while (Out.Num() > 3) { Out.Pop(); }
	return Out;
}

TSharedRef<SWidget> SChatEmptyState::BuildSuggestionChip(const FText& Label, const FString& Prompt)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(0.f)
		.ToolTipText(FText::FromString(Prompt))
		.OnClicked_Lambda([this, Prompt]()
		{
			OnSuggestionChosen.ExecuteIfBound(Prompt);
			return FReply::Handled();
		})
		[
			SNew(SBorder)
			.BorderImage(FMCPChatStyle::Brush(TEXT("chip")))
			.Padding(FMCPChatStyle::Padding(TEXT("md"), TEXT("xs")))
			[
				SNew(STextBlock)
				.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
				.Text(Label)
			]
		];
}

TSharedRef<SWidget> SChatEmptyState::BuildReadyState()
{
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);

	FString ModelLabel;
	if (Controller.IsValid())
	{
		if (const FChatSessionPtr Session = Controller->GetActiveSession())
		{
			if (const FChatBackendPtr Backend = Controller->FindBackend(Session->BackendId))
			{
				ModelLabel = Backend->GetDisplayName().ToString();
			}
		}
	}

	Column->AddSlot().AutoHeight()
	.Padding(0.f, 0.f, 0.f, FMCPChatStyle::Space(TEXT("md")))
	[
		SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Body")))
		.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
		.Text(ModelLabel.IsEmpty()
			? LOCTEXT("ReadyGeneric", "Ask for a change, or start with one of these:")
			: FText::Format(LOCTEXT("ReadyWithModel", "{0} is connected. Ask for a change, or start with one of these:"),
				FText::FromString(ModelLabel)))
		.AutoWrapText(true)
		.Justification(ETextJustify::Center)
	];

	TSharedRef<SWrapBox> Chips = SNew(SWrapBox)
		.UseAllottedSize(true)
		.HAlign(HAlign_Center)
		.InnerSlotPadding(FVector2D(FMCPChatStyle::Space(TEXT("xs")), FMCPChatStyle::Space(TEXT("xs"))));

	for (const TPair<FText, FString>& Suggestion : GatherSuggestions())
	{
		Chips->AddSlot()[ BuildSuggestionChip(Suggestion.Key, Suggestion.Value) ];
	}

	Column->AddSlot().AutoHeight()[ Chips ];

	Column->AddSlot().AutoHeight()
	.Padding(0.f, FMCPChatStyle::Space(TEXT("lg")), 0.f, 0.f)
	[
		SNew(STextBlock)
		.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
		.ColorAndOpacity(FMCPChatStyle::Color(TEXT("fgSubdued")))
		.Text(LOCTEXT("ReadyHint", "@ for editor context   ·   # for tools   ·   / for commands"))
		.Justification(ETextJustify::Center)
	];

	return Column;
}

#undef LOCTEXT_NAMESPACE
