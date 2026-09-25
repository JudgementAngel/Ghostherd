// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPChatToolBridge.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FMCPChatController;
class SEditableTextBox;
class SVerticalBox;

/** Which tab is showing. */
enum class EChatSettingsTab : uint8
{
	Providers,
	AI,
	Permissions,
	Appearance,
};

/**
 * Phase 8 — settings (docs/03_UIUX_SPEC.md §7).
 *
 * A tab, not a modal. Configuring a provider while reading the error that sent you
 * here is the normal case, and a modal makes that impossible.
 *
 * Two rules this page exists to enforce:
 *
 *  1. **Keys are never shown, and never stored where settings are stored.** A row
 *     renders `sk-ant-…••••…4f2a`, names the SOURCE the key came from
 *     (environment / keychain / encrypted file), and requires a deliberate second
 *     action to reveal. `UMCPChatSettings` is a config UObject written verbatim to
 *     an .ini that people commit — nothing sensitive may reach it.
 *
 *  2. **The AI tab renders itself from the model catalogue's capability flags.**
 *     A model that rejects `temperature` has no temperature slider, rather than a
 *     slider that produces a 400 nobody can explain. The flags are data, so this
 *     stays correct when a provider changes its rules.
 */
class SChatSettingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatSettingsPanel)
		: _InitialTab(EChatSettingsTab::Providers)
	{}
		SLATE_ARGUMENT(TSharedPtr<FMCPChatController>, Controller)
		SLATE_ARGUMENT(EChatSettingsTab, InitialTab)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SelectTab(EChatSettingsTab Tab);

private:
	TSharedRef<SWidget> BuildTabStrip();
	TSharedRef<SWidget> BuildTabButton(EChatSettingsTab Tab, const FText& Label);

	void RebuildBody();
	TSharedRef<SWidget> BuildProvidersTab();
	TSharedRef<SWidget> BuildAITab();
	TSharedRef<SWidget> BuildPermissionsTab();
	TSharedRef<SWidget> BuildAppearanceTab();

	// ---- Shared row builders ----
	TSharedRef<SWidget> BuildSectionHeading(const FText& Text) const;
	TSharedRef<SWidget> BuildHelpText(const FText& Text) const;
	TSharedRef<SWidget> BuildBoolRow(const FText& Label, const FText& Help,
	                                 TFunction<bool()> Get, TFunction<void(bool)> Set) const;
	TSharedRef<SWidget> BuildIntRow(const FText& Label, const FText& Help, int32 Min, int32 Max,
	                                TFunction<int32()> Get, TFunction<void(int32)> Set) const;
	TSharedRef<SWidget> BuildStringListRow(const FText& Label, const FText& Help,
	                                       TFunction<TArray<FString>()> Get,
	                                       TFunction<void(const FString&)> Remove);

	/** One remote provider: key state, source, masked preview, reveal, test. */
	TSharedRef<SWidget> BuildProviderRow(const FString& ProviderId);
	/** One local agent: binary resolution and the MCP-exposure toggle. */
	TSharedRef<SWidget> BuildAgentRow(const FString& AgentId);

	FReply OnRevealKey(FString ProviderId);
	FReply OnSaveKey(FString ProviderId);
	FReply OnClearKey(FString ProviderId);

	TSharedPtr<FMCPChatController> Controller;
	EChatSettingsTab CurrentTab = EChatSettingsTab::Providers;

	TSharedPtr<SVerticalBox> Body;

	/** Per-provider entry box. Held so Save can read it; cleared immediately after,
	 *  because a key sitting in a widget is a key in a screenshot. */
	TMap<FString, TSharedPtr<SEditableTextBox>> KeyBoxes;

	/** Providers whose key is currently revealed. Reset on every tab rebuild — a
	 *  revealed key must not survive navigating away and back. */
	TSet<FString> RevealedProviders;

	/** Result of the last "Test" per provider, so the row can report it. */
	TMap<FString, FText> TestResults;
};
