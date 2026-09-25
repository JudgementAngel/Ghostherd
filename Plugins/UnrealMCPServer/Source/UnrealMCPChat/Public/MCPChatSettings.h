// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MCPChatToolBridge.h"
#include "MCPChatSettings.generated.h"

/** Where the panel prefers to open. */
UENUM()
enum class EMCPChatDefaultDock : uint8
{
	RightSidebar UMETA(DisplayName = "Right Sidebar"),
	LeftSidebar  UMETA(DisplayName = "Left Sidebar"),
	BottomDrawer UMETA(DisplayName = "Bottom Drawer"),
	Floating     UMETA(DisplayName = "Floating Window"),
};

/**
 * Chat settings that are safe to commit.
 *
 * ▸ Looking for API keys or local agents? They are NOT on this page. Open
 *   Window ▸ Chat Settings (or the gear in the chat panel's header).
 *
 * That separation is deliberate, not an oversight. This is a `config` UObject:
 * every property here is written verbatim into Config/DefaultUnrealMCPChat.ini,
 * which people commit to source control. A key stored here would be a key pushed
 * to your repository. Keys go to the OS keychain, an environment variable, or an
 * encrypted file instead — see docs/02_ARCHITECTURE.md §8.
 */
UCLASS(config = UnrealMCPChat, defaultconfig, meta = (DisplayName = "Unreal MCP Chat"))
class UNREALMCPCHAT_API UMCPChatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMCPChatSettings();

	// ================================================================
	// Appearance
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Appearance",
		meta = (ToolTip = "Theme file to load from the plugin's Config/Themes folder (without the .json extension). Edit that file and the panel restyles live — no recompile, no restart. 'Editor' follows your editor theme."))
	FString ThemeName;

	UPROPERTY(config, EditAnywhere, Category = "Appearance",
		meta = (ToolTip = "Watch Config/Themes for changes and reload automatically. Turn off if you are editing themes on a network drive and the watcher is noisy. You can always reload manually with the console command 'MCPChat.ReloadTheme'."))
	bool bHotReloadThemes;

	// ================================================================
	// Layout
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Layout",
		meta = (ToolTip = "Where the chat tab opens the first time. Afterwards the editor's saved layout wins."))
	EMCPChatDefaultDock DefaultDock;

	UPROPERTY(config, EditAnywhere, Category = "Layout",
		meta = (ClampMin = "160", ClampMax = "600",
			ToolTip = "Width of the conversation history rail, in Slate units."))
	int32 HistoryRailWidth;

	UPROPERTY(config, EditAnywhere, Category = "Layout",
		meta = (ClampMin = "400", ClampMax = "2000",
			ToolTip = "Panel width at or above which the history rail is pinned open and the transcript is centred (the 'Wide' breakpoint)."))
	int32 WideBreakpoint;

	UPROPERTY(config, EditAnywhere, Category = "Layout",
		meta = (ClampMin = "240", ClampMax = "900",
			ToolTip = "Panel width below which the composer drops to icon-only controls (the 'Narrow' breakpoint)."))
	int32 NarrowBreakpoint;

	UPROPERTY(config, EditAnywhere, Category = "Layout",
		meta = (ClampMin = "400", ClampMax = "1400",
			ToolTip = "Maximum width of message text at the Wide breakpoint. Lines longer than roughly 90 characters are measurably harder to read, so the transcript centres its content rather than filling a very wide panel."))
	int32 TranscriptMaxContentWidth;

	// ================================================================
	// Permissions
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Permissions",
		meta = (ToolTip = "When the panel should stop and ask before running a tool that changes your project.\n\nAsk for destructive tools (default) — deletes and overwrites ask; ordinary edits run.\nAsk for every change — anything that mutates asks.\nNever ask — nothing asks. Only sensible with source control and a recent commit.\nRead-only — refuses every change outright; nothing to approve."))
	EMCPChatApprovalMode ApprovalMode;

	UPROPERTY(config, EditAnywhere, Category = "Permissions",
		meta = (ToolTip = "Tools you chose 'Always allow' for. They run without asking, in every session. Remove an entry here to start being asked again."))
	TArray<FString> AlwaysAllowedTools;

	UPROPERTY(config, EditAnywhere, Category = "Permissions",
		meta = (ToolTip = "Tools that may never run, whatever the approval mode says. This list wins over 'Never ask'."))
	TArray<FString> BlockedTools;

	UPROPERTY(config, EditAnywhere, Category = "Permissions",
		meta = (ToolTip = "Wrap every turn's tool calls in a single undo transaction, so a whole multi-step change is one Ctrl+Z. Turn off only if you want each tool to be its own undo step."))
	bool bSingleUndoPerTurn;

	UPROPERTY(config, EditAnywhere, Category = "Permissions",
		meta = (ClampMin = "1", ClampMax = "100",
			ToolTip = "Maximum tool round-trips in one turn before the panel stops and hands control back. Guards against a model looping."))
	int32 MaxToolIterationsPerTurn;

	// ================================================================
	// Behaviour
	// ================================================================

	UPROPERTY(config, EditAnywhere, Category = "Behaviour",
		meta = (ToolTip = "Show a one-time notification pointing at the chat panel after the editor loads. The panel is never opened automatically — an editor layout is the user's, not ours."))
	bool bShowFirstRunNotification;

	UPROPERTY(config, EditAnywhere, Category = "Behaviour",
		meta = (ToolTip = "Add a Chat button to the Level Editor toolbar. Off by default; the Window menu and Ctrl+Shift+A already reach it."))
	bool bShowToolbarButton;

	UPROPERTY(config, EditAnywhere, Category = "Behaviour",
		meta = (ToolTip = "Swap the composer's send and newline keys.\n\nOff (default) — Enter sends, Shift+Enter starts a new line.\nOn — Enter starts a new line, Ctrl+Enter sends.\n\nThe composer's placeholder text always states whichever binding is in force, so this can never be ambiguous."))
	bool bEnterInsertsNewline;

	// UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Unreal MCP Chat"); }

	static const UMCPChatSettings* Get();
};
