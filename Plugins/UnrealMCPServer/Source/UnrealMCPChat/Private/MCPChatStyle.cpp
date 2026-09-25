// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatStyle.h"
#include "MCPChatSettings.h"
#include "UnrealMCPChatModule.h"

#include "Brushes/SlateColorBrush.h"
// FSlateVectorImageBrush is declared here too, not in a header of its own.
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"
#include "Styling/StyleDefaults.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "MCPChatStyle"

TSharedPtr<FSlateStyleSet> FMCPChatStyle::StyleInstance;
FMCPChatTheme              FMCPChatStyle::LoadedTheme;
FMCPChatStyle::FOnStyleReloaded FMCPChatStyle::StyleReloadedDelegate;

namespace
{
	/** Plugin root — Resources/ and Config/Themes/ both hang off this. */
	FString PluginBaseDir()
	{
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealMCPServer")))
		{
			return Plugin->GetBaseDir();
		}
		// Fallback for an unusual install layout; keeps the panel usable rather than
		// crashing on a null plugin descriptor.
		return FPaths::ProjectPluginsDir() / TEXT("UnrealMCPServer");
	}
}

FName FMCPChatStyle::GetStyleSetName()
{
	static const FName Name(TEXT("MCPChatStyle"));
	return Name;
}

const ISlateStyle& FMCPChatStyle::Get()
{
	// Initialize() runs in StartupModule, but a stray early accessor shouldn't crash.
	if (!StyleInstance.IsValid())
	{
		Initialize();
	}
	return *StyleInstance;
}

const FMCPChatTheme& FMCPChatStyle::GetTheme()
{
	return LoadedTheme;
}

FMCPChatStyle::FOnStyleReloaded& FMCPChatStyle::OnStyleReloaded()
{
	return StyleReloadedDelegate;
}

FString FMCPChatStyle::GetThemeDirectory()
{
	return PluginBaseDir() / TEXT("Config") / TEXT("Themes");
}

TArray<FString> FMCPChatStyle::DiscoverThemeNames()
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(GetThemeDirectory() / TEXT("*.json")), true, false);

	TArray<FString> Names;
	Names.Reserve(Files.Num());
	for (const FString& File : Files)
	{
		Names.Add(FPaths::GetBaseFilename(File));
	}
	Names.Sort();
	return Names;
}

void FMCPChatStyle::Initialize()
{
	if (StyleInstance.IsValid())
	{
		return;
	}

	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	const FString ThemeName = (Settings && !Settings->ThemeName.IsEmpty()) ? Settings->ThemeName : TEXT("Editor");

	bool bFileRead = false;
	LoadedTheme = FMCPChatThemeParser::ParseFromFile(GetThemeDirectory() / (ThemeName + TEXT(".json")), bFileRead);
	if (!bFileRead)
	{
		UE_LOG(LogUnrealMCPChat, Warning,
			TEXT("Theme '%s' not found in %s — using the built-in fallback (pure FStyleColors, follows the editor theme)."),
			*ThemeName, *GetThemeDirectory());
	}

	StyleInstance = BuildStyleSet(LoadedTheme);
	FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);

	for (const FString& Warning : LoadedTheme.ParseWarnings)
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("Theme '%s': %s"), *ThemeName, *Warning);
	}
	UE_LOG(LogUnrealMCPChat, Log, TEXT("Chat style initialized from theme '%s' (%d colours, %d brushes, %d warnings)"),
		*ThemeName, LoadedTheme.Colors.Num(), LoadedTheme.Brushes.Num(), LoadedTheme.ParseWarnings.Num());
}

void FMCPChatStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		ensureMsgf(StyleInstance.IsUnique(),
			TEXT("MCPChatStyle still referenced at shutdown — a widget is holding the style set alive."));
		StyleInstance.Reset();
	}
	StyleReloadedDelegate.Clear();
}

bool FMCPChatStyle::ReloadTheme(const FString& InThemeName)
{
	check(IsInGameThread());

	FString ThemeName = InThemeName;
	if (ThemeName.IsEmpty())
	{
		const UMCPChatSettings* Settings = UMCPChatSettings::Get();
		ThemeName = (Settings && !Settings->ThemeName.IsEmpty()) ? Settings->ThemeName : TEXT("Editor");
	}

	bool bFileRead = false;
	FMCPChatTheme NewTheme = FMCPChatThemeParser::ParseFromFile(
		GetThemeDirectory() / (ThemeName + TEXT(".json")), bFileRead);

	if (!bFileRead)
	{
		UE_LOG(LogUnrealMCPChat, Warning,
			TEXT("Reload failed: theme '%s' could not be read. Keeping the current style."), *ThemeName);
		return false;
	}

	ApplyTheme(NewTheme);

	for (const FString& Warning : NewTheme.ParseWarnings)
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("Theme '%s': %s"), *ThemeName, *Warning);
	}
	UE_LOG(LogUnrealMCPChat, Log, TEXT("Chat theme reloaded: '%s' (%d warnings)"),
		*ThemeName, NewTheme.ParseWarnings.Num());
	return true;
}

void FMCPChatStyle::ApplyTheme(const FMCPChatTheme& Theme)
{
	// Unregister before dropping the old set: FSlateStyleRegistry holds a raw
	// reference, and destroying a still-registered style set leaves a dangling entry.
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		StyleInstance.Reset();
	}

	LoadedTheme = Theme;
	StyleInstance = BuildStyleSet(LoadedTheme);
	FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);

	// Brush pointers handed out before the swap are now stale. Tell listeners first
	// so they can re-fetch, then force a full repaint.
	StyleReloadedDelegate.Broadcast();

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->FlushCommands();
		FSlateApplication::Get().InvalidateAllWidgets(/*bClearResourceCaches*/ true);
	}
}

TSharedRef<FSlateStyleSet> FMCPChatStyle::BuildStyleSet(const FMCPChatTheme& Theme)
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());
	Style->SetContentRoot(PluginBaseDir() / TEXT("Resources"));

	// ---- Brushes declared by the theme ----
	for (const TPair<FName, FMCPChatBrushSpec>& Pair : Theme.Brushes)
	{
		const FMCPChatBrushSpec& Spec = Pair.Value;
		const FName BrushName = FName(*FString::Printf(TEXT("Chat.Brush.%s"), *Pair.Key.ToString()));

		switch (Spec.Kind)
		{
		case FMCPChatBrushSpec::EKind::RoundedBox:
			// FSlateRoundedBoxBrush takes FSlateColor, so a brush built from a pure
			// semantic token keeps following the editor theme at paint time.
			Style->Set(BrushName, new FSlateRoundedBoxBrush(
				Spec.Fill, Spec.Radius, Spec.Outline, Spec.OutlineWidth));
			break;

		case FMCPChatBrushSpec::EKind::Box:
			// FSlateColorBrush constructs from a concrete colour, not an FSlateColor,
			// so a semantic token is resolved here rather than at paint time. Flat
			// backgrounds are refreshed by the reload path, which is enough — use a
			// roundedBox with radius 0 if you need live theme following on a fill.
			Style->Set(BrushName, new FSlateColorBrush(Spec.Fill.GetSpecifiedColor()));
			break;

		case FMCPChatBrushSpec::EKind::Image:
			Style->Set(BrushName, new FSlateImageBrush(
				Style->RootToContentDir(Spec.ResourcePath), FVector2D(Spec.ImageSize)));
			break;

		case FMCPChatBrushSpec::EKind::Vector:
			Style->Set(BrushName, new FSlateVectorImageBrush(
				Style->RootToContentDir(Spec.ResourcePath), FVector2D(Spec.ImageSize)));
			break;
		}
	}

	// ---- Colours ----
	for (const TPair<FName, FSlateColor>& Pair : Theme.Colors)
	{
		Style->Set(FName(*FString::Printf(TEXT("Chat.Color.%s"), *Pair.Key.ToString())), Pair.Value);
	}

	// ---- Fonts + text styles ----
	const FSlateColor Fg        = Theme.GetColor(TEXT("fg"), FSlateColor(EStyleColor::Foreground));
	const FSlateColor FgSubdued = Theme.GetColor(TEXT("fgSubdued"), FSlateColor(EStyleColor::ForegroundHover));

	auto MakeText = [&](const TCHAR* Name, const FString& Family, int32 Size, const FSlateColor& Colour)
	{
		Style->Set(FName(Name), FTextBlockStyle(FTextBlockStyle::GetDefault())
			.SetFont(FCoreStyle::GetDefaultFontStyle(*Family, Size))
			.SetColorAndOpacity(Colour));
	};

	MakeText(TEXT("Chat.Text.Body"),   Theme.Font.Family,     Theme.Font.Body,  Fg);
	MakeText(TEXT("Chat.Text.Bold"),   TEXT("Bold"),          Theme.Font.Body,  Fg);
	MakeText(TEXT("Chat.Text.Italic"), TEXT("Italic"),        Theme.Font.Body,  Fg);
	MakeText(TEXT("Chat.Text.Small"),  Theme.Font.Family,     Theme.Font.Small, FgSubdued);
	MakeText(TEXT("Chat.Text.Mono"),   Theme.Font.MonoFamily, Theme.Font.Mono,  Fg);
	MakeText(TEXT("Chat.Text.H1"),     TEXT("Bold"),          Theme.Font.H1,    Fg);
	MakeText(TEXT("Chat.Text.H2"),     TEXT("Bold"),          Theme.Font.H2,    Fg);
	MakeText(TEXT("Chat.Text.H3"),     TEXT("Bold"),          Theme.Font.H3,    Fg);

	// ---- Phase 3: markdown inline styles ----
	// These names are also the RICH TEXT TAG NAMES emitted by FMCPMarkdownRenderer
	// (<Chat.Md.Bold>…</>), so SRichTextBlock resolves them out of this style set.
	// Renaming one here silently stops that markup from styling — keep them in sync
	// with StyleNameForInline().
	MakeText(TEXT("Chat.Md.Normal"),     Theme.Font.Family,     Theme.Font.Body, Fg);
	MakeText(TEXT("Chat.Md.Bold"),       TEXT("Bold"),          Theme.Font.Body, Fg);
	MakeText(TEXT("Chat.Md.Italic"),     TEXT("Italic"),        Theme.Font.Body, Fg);
	MakeText(TEXT("Chat.Md.BoldItalic"), TEXT("BoldItalic"),    Theme.Font.Body, Fg);
	MakeText(TEXT("Chat.Md.Code"),       Theme.Font.MonoFamily, Theme.Font.Mono,
		Theme.GetColor(TEXT("codeInline"), Fg));
	MakeText(TEXT("Chat.Md.Link"),       Theme.Font.Family,     Theme.Font.Body,
		Theme.GetColor(TEXT("accent"), FSlateColor(EStyleColor::Primary)));
	MakeText(TEXT("Chat.Md.UnrealRef"),  Theme.Font.Family,     Theme.Font.Body,
		Theme.GetColor(TEXT("unrealRef"), FSlateColor(EStyleColor::AccentBlue)));

	// Strikethrough needs an actual brush; FTextBlockStyle has no "strike" flag.
	{
		FTextBlockStyle Strike = FTextBlockStyle(FTextBlockStyle::GetDefault())
			.SetFont(FCoreStyle::GetDefaultFontStyle(*Theme.Font.Family, Theme.Font.Body))
			.SetColorAndOpacity(FgSubdued)
			.SetStrikeBrush(*FAppStyle::Get().GetBrush("WhiteBrush"));
		Style->Set(TEXT("Chat.Md.Strike"), Strike);
	}

	// Hyperlink styles. FHyperlinkDecorator resolves the `style` attribute as an
	// FHyperlinkStyle — NOT an FTextBlockStyle — so links need their own entries or
	// they render unstyled and unclickable.
	{
		auto MakeHyperlink = [&](const TCHAR* Name, const FSlateColor& Colour)
		{
			FTextBlockStyle LinkText = FTextBlockStyle(FTextBlockStyle::GetDefault())
				.SetFont(FCoreStyle::GetDefaultFontStyle(*Theme.Font.Family, Theme.Font.Body))
				.SetColorAndOpacity(Colour);

			// The "underline" is a button style whose Hovered state draws the rule, so
			// links underline on hover only — quieter in a dense transcript.
			FButtonStyle Underline = FButtonStyle()
				.SetNormal(FSlateNoResource())
				.SetHovered(*FAppStyle::Get().GetBrush("WhiteBrush"))
				.SetPressed(*FAppStyle::Get().GetBrush("WhiteBrush"))
				.SetNormalPadding(FMargin(0.f))
				.SetPressedPadding(FMargin(0.f));

			Style->Set(FName(Name), FHyperlinkStyle()
				.SetUnderlineStyle(Underline)
				.SetTextStyle(LinkText)
				.SetPadding(FMargin(0.f)));
		};

		MakeHyperlink(TEXT("Chat.Md.Hyperlink"),
			Theme.GetColor(TEXT("accent"), FSlateColor(EStyleColor::Primary)));
		MakeHyperlink(TEXT("Chat.Md.UnrealRefLink"),
			Theme.GetColor(TEXT("unrealRef"), FSlateColor(EStyleColor::AccentBlue)));
	}

	// Code-token colours (codeKeyword, codeString, …) need no registration here:
	// the loop above already exposes every theme colour as Chat.Color.<token>, and
	// FMCPCodeSyntaxHighlighter reads them through FMCPChatStyle::Color(), which
	// resolves against the loaded theme directly.

	return Style;
}

// ============================================================================
// Convenience accessors
// ============================================================================

const FSlateBrush* FMCPChatStyle::Brush(FName Token)
{
	const FName Full(*FString::Printf(TEXT("Chat.Brush.%s"), *Token.ToString()));
	// GetOptionalBrush returns the "no brush" default rather than asserting, so a
	// theme missing a brush renders as nothing instead of taking the editor down.
	return Get().GetOptionalBrush(Full, nullptr, FStyleDefaults::GetNoBrush());
}

FSlateColor FMCPChatStyle::Color(FName Token, const FSlateColor& Fallback)
{
	return LoadedTheme.GetColor(Token, Fallback);
}

float FMCPChatStyle::Space(FName Token, float Fallback)
{
	return LoadedTheme.GetSpace(Token, Fallback);
}

FMargin FMCPChatStyle::Padding(FName Token)
{
	const float V = LoadedTheme.GetSpace(Token, 8.f);
	return FMargin(V);
}

FMargin FMCPChatStyle::Padding(FName Horizontal, FName Vertical)
{
	return FMargin(
		LoadedTheme.GetSpace(Horizontal, 8.f),
		LoadedTheme.GetSpace(Vertical, 8.f));
}

FSlateFontInfo FMCPChatStyle::Font(FName SizeToken)
{
	const FMCPChatFontSpec& F = LoadedTheme.Font;
	int32 Size = F.Body;
	const FString Token = SizeToken.ToString();
	if      (Token == TEXT("small")) { Size = F.Small; }
	else if (Token == TEXT("h1"))    { Size = F.H1; }
	else if (Token == TEXT("h2"))    { Size = F.H2; }
	else if (Token == TEXT("h3"))    { Size = F.H3; }
	return FCoreStyle::GetDefaultFontStyle(*F.Family, Size);
}

FSlateFontInfo FMCPChatStyle::MonoFont()
{
	return FCoreStyle::GetDefaultFontStyle(*LoadedTheme.Font.MonoFamily, LoadedTheme.Font.Mono);
}

const FTextBlockStyle& FMCPChatStyle::TextStyle(FName Token)
{
	return Get().GetWidgetStyle<FTextBlockStyle>(Token);
}

#undef LOCTEXT_NAMESPACE
