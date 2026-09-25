// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatThemeWatcher.h"
#include "MCPChatStyle.h"
#include "UnrealMCPChatModule.h"

#include "DirectoryWatcherModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

void FMCPChatThemeWatcher::Start()
{
	if (DelegateHandle.IsValid())
	{
		return;
	}

	WatchedDirectory = FMCPChatStyle::GetThemeDirectory();
	if (!IFileManager::Get().DirectoryExists(*WatchedDirectory))
	{
		UE_LOG(LogUnrealMCPChat, Verbose,
			TEXT("Theme directory '%s' does not exist; hot reload disabled."), *WatchedDirectory);
		return;
	}

	FDirectoryWatcherModule& Module =
		FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	IDirectoryWatcher* Watcher = Module.Get();
	if (!Watcher)
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("DirectoryWatcher unavailable; theme hot reload disabled."));
		return;
	}

	Watcher->RegisterDirectoryChangedCallback_Handle(
		WatchedDirectory,
		IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FMCPChatThemeWatcher::OnDirectoryChanged),
		DelegateHandle,
		IDirectoryWatcher::WatchOptions::IgnoreChangesInSubtree);

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FMCPChatThemeWatcher::TickDebounce), 0.1f);

	UE_LOG(LogUnrealMCPChat, Log, TEXT("Watching '%s' for theme changes."), *WatchedDirectory);
}

void FMCPChatThemeWatcher::Stop()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	if (DelegateHandle.IsValid())
	{
		// The module may already be unloaded during editor shutdown — check rather
		// than force-load it just to unregister.
		if (FDirectoryWatcherModule* Module =
			FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
		{
			if (IDirectoryWatcher* Watcher = Module->Get())
			{
				Watcher->UnregisterDirectoryChangedCallback_Handle(WatchedDirectory, DelegateHandle);
			}
		}
		DelegateHandle.Reset();
	}
}

void FMCPChatThemeWatcher::OnDirectoryChanged(const TArray<FFileChangeData>& Changes)
{
	// Only .json files matter; an editor's swap/backup files would otherwise cause
	// a reload per keystroke-save.
	for (const FFileChangeData& Change : Changes)
	{
		if (FPaths::GetExtension(Change.Filename).Equals(TEXT("json"), ESearchCase::IgnoreCase))
		{
			LastChangeSeconds = FPlatformTime::Seconds();
			bReloadPending.Store(true);
			return;
		}
	}
}

bool FMCPChatThemeWatcher::TickDebounce(float /*DeltaTime*/)
{
	if (!bReloadPending.Load())
	{
		return true;
	}

	// Wait for the write burst to settle. Reloading mid-write reads a truncated
	// file and produces a spurious "not valid JSON" warning.
	if (FPlatformTime::Seconds() - LastChangeSeconds < DebounceSeconds)
	{
		return true;
	}

	bReloadPending.Store(false);
	FMCPChatStyle::ReloadTheme();   // logs its own success/failure
	return true;
}
