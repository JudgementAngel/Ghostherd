// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUnrealMCPChat, Log, All);

class FMCPChatThemeWatcher;
class FUICommandList;
class SDockTab;
class FSpawnTabArgs;

class FUnrealMCPChatModule : public IModuleInterface
{
public:
	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	/** Tab id used by the editor layout. Stable — changing it orphans saved layouts. */
	static const FName ChatTabId;
	/** Phase 8 — the settings page. A tab, not a modal: configuring a provider while
	 *  reading the error that sent you there is the normal case. */
	static const FName SettingsTabId;

	/** Open (or focus) the chat tab. Safe to call before the tab has ever been shown. */
	static void OpenChatTab();

	/** Open (or focus) the chat settings tab — the only page that holds API keys. */
	static void OpenSettingsTab();

private:
	void RegisterTabSpawner();
	void UnregisterTabSpawner();
	void RegisterCommands();
	void RegisterMenus();
	void RegisterConsoleCommands();
	void MaybeShowFirstRunNotification();

	TSharedRef<SDockTab> OnSpawnChatTab(const FSpawnTabArgs& Args);

	TUniquePtr<FMCPChatThemeWatcher> ThemeWatcher;
	TSharedPtr<FUICommandList>       CommandList;
	TArray<IConsoleObject*>          ConsoleCommands;
};
