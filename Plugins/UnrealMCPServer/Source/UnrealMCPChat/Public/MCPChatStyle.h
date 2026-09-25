// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPChatTheme.h"
#include "Styling/SlateStyle.h"

/**
 * Phase 1 — the panel's style set, rebuilt from a theme file on demand.
 *
 * Contract: widget code NEVER writes a literal colour, padding or font size. It
 * asks this class. That is what makes the panel restylable without a recompile
 * and is why this has to exist before any widget does (docs/04 §2, Phase 1).
 *
 * Reload path:
 *   theme .json changes  →  FMCPChatThemeWatcher  →  ReloadTheme()
 *                        →  UnRegister + rebuild + Register  →  Invalidate()
 *
 * Widgets that cache a brush pointer must instead re-fetch inside their paint or
 * attribute lambdas, or subscribe to OnStyleReloaded.
 */
class FMCPChatStyle
{
public:
	/** Build the style set from the configured theme and register it. Idempotent. */
	static void Initialize();

	/** Unregister and destroy. Safe to call when not initialized. */
	static void Shutdown();

	static bool IsInitialized() { return StyleInstance.IsValid(); }

	static const ISlateStyle& Get();
	static FName GetStyleSetName();

	/**
	 * Re-read the theme from disk and rebuild every resource in place.
	 * @param ThemeName  file stem under Config/Themes (e.g. "Editor"). Empty = the
	 *                   name in UMCPChatSettings.
	 * @return false when the file could not be read (the previous style is kept).
	 */
	static bool ReloadTheme(const FString& ThemeName = FString());

	/** Currently loaded theme — including any ParseWarnings, which the settings UI surfaces. */
	static const FMCPChatTheme& GetTheme();

	/** Absolute path of Config/Themes inside the plugin. */
	static FString GetThemeDirectory();

	/** File stems of every .json in the theme directory. */
	static TArray<FString> DiscoverThemeNames();

	/** Fired after a successful rebuild, on the game thread. Widgets that cached
	 *  anything style-derived should refresh here. */
	DECLARE_MULTICAST_DELEGATE(FOnStyleReloaded);
	static FOnStyleReloaded& OnStyleReloaded();

	// ---- Convenience accessors (all resolve through the loaded theme) ----

	static const FSlateBrush* Brush(FName Token);
	static FSlateColor        Color(FName Token, const FSlateColor& Fallback = FSlateColor(EStyleColor::Foreground));
	static float              Space(FName Token, float Fallback = 8.f);
	static FMargin            Padding(FName Token);                       // uniform
	static FMargin            Padding(FName Horizontal, FName Vertical);  // per-axis
	static FSlateFontInfo     Font(FName SizeToken);                      // body|small|h1|h2|h3
	static FSlateFontInfo     MonoFont();
	static const FTextBlockStyle& TextStyle(FName Token);                 // Text.Body|Small|Bold|H1..

private:
	static TSharedRef<FSlateStyleSet> BuildStyleSet(const FMCPChatTheme& Theme);
	static void ApplyTheme(const FMCPChatTheme& Theme);

	static TSharedPtr<FSlateStyleSet> StyleInstance;
	static FMCPChatTheme              LoadedTheme;
	static FOnStyleReloaded           StyleReloadedDelegate;
};
