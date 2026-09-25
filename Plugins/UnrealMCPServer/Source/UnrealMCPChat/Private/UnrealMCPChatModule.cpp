// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UnrealMCPChatModule.h"
#include "MCPChatStyle.h"
#include "MCPChatSettings.h"
#include "Agents/MCPChatPidRegistry.h"
#include "Core/MCPChatDrafts.h"
#include "MCPChatStore.h"
#include "MCPChatThemeWatcher.h"
#include "UI/SChatSettingsPanel.h"
#include "UI/SMCPChatPanel.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ToolMenus.h"
#include "Styling/AppStyle.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"

#define LOCTEXT_NAMESPACE "FUnrealMCPChatModule"

DEFINE_LOG_CATEGORY(LogUnrealMCPChat);

IMPLEMENT_MODULE(FUnrealMCPChatModule, UnrealMCPChat)

const FName FUnrealMCPChatModule::ChatTabId(TEXT("MCPChat"));
const FName FUnrealMCPChatModule::SettingsTabId(TEXT("MCPChatSettings"));

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

class FMCPChatCommands : public TCommands<FMCPChatCommands>
{
public:
	FMCPChatCommands()
		: TCommands<FMCPChatCommands>(
			TEXT("MCPChat"),
			LOCTEXT("MCPChatCommands", "Unreal MCP Chat"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{}

	virtual void RegisterCommands() override
	{
		UI_COMMAND(OpenChatPanel, "Chat", "Open the Unreal MCP chat panel",
			EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::A));
	}

	TSharedPtr<FUICommandInfo> OpenChatPanel;
};

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

void FUnrealMCPChatModule::StartupModule()
{
	UE_LOG(LogUnrealMCPChat, Log, TEXT("=== Unreal MCP Chat starting up ==="));

	// Style first: the tab spawner and every widget below it resolve brushes
	// through it, so it must exist before anything can be constructed.
	FMCPChatStyle::Initialize();

	// Store second: the panel resumes the most recent session on construction, so
	// the index has to be readable before a tab can be spawned.
	FMCPChatStore::Get().Initialize();

	// Gotcha G4, defence 3: reap agent processes orphaned by a previous editor that
	// crashed. Must run BEFORE any agent can be launched, or a fresh PID could be
	// swept by a record from the last run.
	FMCPChatPidRegistry::Get().SweepStaleProcesses();

	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	if (Settings && Settings->bHotReloadThemes)
	{
		ThemeWatcher = MakeUnique<FMCPChatThemeWatcher>();
		ThemeWatcher->Start();
	}

	RegisterCommands();
	RegisterTabSpawner();
	RegisterConsoleCommands();

	// Menus need ToolMenus to have finished its own startup.
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealMCPChatModule::RegisterMenus));

	MaybeShowFirstRunNotification();

	UE_LOG(LogUnrealMCPChat, Log, TEXT("=== Unreal MCP Chat ready (theme '%s') ==="),
		*FMCPChatStyle::GetTheme().Name);
}

void FUnrealMCPChatModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	for (IConsoleObject* Command : ConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Command);
	}
	ConsoleCommands.Empty();

	if (ThemeWatcher.IsValid())
	{
		ThemeWatcher->Stop();
		ThemeWatcher.Reset();
	}

	UnregisterTabSpawner();

	// Flush transcripts synchronously before anything else tears down — a debounced
	// save that never fires is a lost conversation.
	FMCPChatStore::Get().Shutdown();

	// Same reasoning, smaller stakes: an unsent draft the user is mid-way through.
	FMCPChatDrafts::Get().Flush();

	// Gotcha G4, defences 1 and 2. Backends stop their own children on destruction,
	// but module teardown order is not ours to control and a panel that was never
	// opened still has nothing to destroy — so sweep by PID as well.
	FMCPChatPidRegistry::Get().KillAllTracked();

	if (FMCPChatCommands::IsRegistered())
	{
		FMCPChatCommands::Unregister();
	}
	CommandList.Reset();

	// Last: widgets built from the style set must be gone before it is destroyed.
	FMCPChatStyle::Shutdown();
}

void FUnrealMCPChatModule::RegisterCommands()
{
	FMCPChatCommands::Register();

	CommandList = MakeShared<FUICommandList>();
	CommandList->MapAction(
		FMCPChatCommands::Get().OpenChatPanel,
		FExecuteAction::CreateStatic(&FUnrealMCPChatModule::OpenChatTab),
		FCanExecuteAction());
}

void FUnrealMCPChatModule::RegisterTabSpawner()
{
	// Nomad tab: survives layout resets and can be docked anywhere, including the
	// status-bar drawer. A major tab would be pinned to one layout area.
	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(ChatTabId,
			FOnSpawnTab::CreateRaw(this, &FUnrealMCPChatModule::OnSpawnChatTab))
		.SetDisplayName(LOCTEXT("ChatTabTitle", "Chat"))
		.SetTooltipText(LOCTEXT("ChatTabTooltip", "AI chat connected to this project's Unreal MCP tools"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

	// Settings gets its own nomad tab so it can be docked beside the chat panel —
	// reading an error and fixing the key that caused it should not be two modes.
	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(SettingsTabId,
			FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
			{
				return SNew(SDockTab)
					.TabRole(ETabRole::NomadTab)
					.Label(LOCTEXT("ChatSettingsTabTitle", "Chat Settings"))
					[
						SNew(SChatSettingsPanel)
					];
			}))
		.SetDisplayName(LOCTEXT("ChatSettingsTabTitle", "Chat Settings"))
		.SetTooltipText(LOCTEXT("ChatSettingsTabTooltip", "Providers, AI settings, permissions and appearance"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"))
		// NOT hidden. Hiding it meant the only page that can hold an API key was
		// reachable from nowhere except a `/settings` command nobody knew to type —
		// while Project Settings ▸ Plugins ▸ Unreal MCP Chat, which people DO find,
		// deliberately has no key field on it. That was a dead end.
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

void FUnrealMCPChatModule::UnregisterTabSpawner()
{
	if (FGlobalTabmanager::Get()->HasTabSpawner(ChatTabId))
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ChatTabId);
	}
	if (FGlobalTabmanager::Get()->HasTabSpawner(SettingsTabId))
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SettingsTabId);
	}
}

TSharedRef<SDockTab> FUnrealMCPChatModule::OnSpawnChatTab(const FSpawnTabArgs& /*Args*/)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("ChatTabTitle", "Chat"))
		[
			SNew(SMCPChatPanel)
		];
}

void FUnrealMCPChatModule::OpenChatTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ChatTabId);
}

void FUnrealMCPChatModule::OpenSettingsTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(SettingsTabId);
}

void FUnrealMCPChatModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Window ▸ Unreal MCP ▸ Chat
	if (UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window"))
	{
		FToolMenuSection& Section = WindowMenu->FindOrAddSection("MCP");
		Section.Label = LOCTEXT("MCPSection", "Unreal MCP");
		Section.AddMenuEntry(
			"OpenMCPChat",
			LOCTEXT("OpenChat", "Chat"),
			LOCTEXT("OpenChatTip", "Open the Unreal MCP chat panel"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"),
			FUIAction(FExecuteAction::CreateStatic(&FUnrealMCPChatModule::OpenChatTab)));

		Section.AddMenuEntry(
			"OpenMCPChatSettings",
			LOCTEXT("OpenChatSettings", "Chat Settings"),
			LOCTEXT("OpenChatSettingsTip", "API keys, local agents, AI settings and permissions"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
			FUIAction(FExecuteAction::CreateStatic(&FUnrealMCPChatModule::OpenSettingsTab)));
	}

	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	if (Settings && Settings->bShowToolbarButton)
	{
		if (UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar"))
		{
			FToolMenuSection& Section = Toolbar->FindOrAddSection("MCPChat");
			Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				"MCPChatButton",
				FUIAction(FExecuteAction::CreateStatic(&FUnrealMCPChatModule::OpenChatTab)),
				LOCTEXT("ChatToolbar", "Chat"),
				LOCTEXT("ChatToolbarTip", "Open the Unreal MCP chat panel"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment")));
		}
	}
}

void FUnrealMCPChatModule::RegisterConsoleCommands()
{
	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("MCPChat.ReloadTheme"),
		TEXT("Reload the chat panel theme from Config/Themes. Optional argument: theme name (file stem)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const FString ThemeName = Args.Num() > 0 ? Args[0] : FString();
			if (FMCPChatStyle::ReloadTheme(ThemeName))
			{
				UE_LOG(LogUnrealMCPChat, Display, TEXT("Theme reloaded."));
			}
		}),
		ECVF_Default));

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("MCPChat.ListThemes"),
		TEXT("List the theme files available in Config/Themes."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const TArray<FString> Names = FMCPChatStyle::DiscoverThemeNames();
			UE_LOG(LogUnrealMCPChat, Display, TEXT("%d theme(s) in %s:"),
				Names.Num(), *FMCPChatStyle::GetThemeDirectory());
			for (const FString& Name : Names)
			{
				UE_LOG(LogUnrealMCPChat, Display, TEXT("  %s%s"), *Name,
					Name == FMCPChatStyle::GetTheme().Name ? TEXT("  (active)") : TEXT(""));
			}
		}),
		ECVF_Default));

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("MCPChat.Open"),
		TEXT("Open the Unreal MCP chat panel."),
		FConsoleCommandDelegate::CreateStatic(&FUnrealMCPChatModule::OpenChatTab),
		ECVF_Default));
}

void FUnrealMCPChatModule::MaybeShowFirstRunNotification()
{
	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	if (!Settings || !Settings->bShowFirstRunNotification)
	{
		return;
	}

	// Deliberately does NOT open the tab. An editor layout belongs to the user;
	// a plugin that rearranges it on install is a plugin people uninstall.
	FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
	{
		FNotificationInfo Info(LOCTEXT("FirstRun", "Unreal MCP Chat is installed"));
		Info.SubText = LOCTEXT("FirstRunSub", "Open it from Window ▸ Unreal MCP ▸ Chat, or press Ctrl+Shift+A.");
		Info.bFireAndForget = true;
		Info.ExpireDuration = 8.f;
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("FirstRunOpen", "Open Chat"),
			FText::GetEmpty(),
			FSimpleDelegate::CreateStatic(&FUnrealMCPChatModule::OpenChatTab)));
		FSlateNotificationManager::Get().AddNotification(Info);
	});
}

#undef LOCTEXT_NAMESPACE
