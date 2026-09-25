// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IDirectoryWatcher.h"

/**
 * Phase 1 — watches Config/Themes and rebuilds the style set when a theme file
 * changes. This is the mechanism that gives Slate web-grade iteration speed:
 * save the JSON, see the panel restyle.
 *
 * Editors and IDEs write files in bursts (temp file → rename → attribute touch),
 * so raw watcher callbacks are debounced through a ticker rather than reloading
 * once per filesystem event.
 */
class FMCPChatThemeWatcher
{
public:
	void Start();
	void Stop();

	bool IsWatching() const { return DelegateHandle.IsValid(); }

private:
	void OnDirectoryChanged(const TArray<FFileChangeData>& Changes);
	bool TickDebounce(float DeltaTime);

	FString WatchedDirectory;
	FDelegateHandle DelegateHandle;
	FTSTicker::FDelegateHandle TickerHandle;

	/** Set by the watcher thread/callback, consumed by the ticker on the game thread. */
	TAtomic<bool> bReloadPending{ false };
	double        LastChangeSeconds = 0.0;

	/** Quiet period a theme file must survive before we act on it. */
	static constexpr double DebounceSeconds = 0.25;
};
