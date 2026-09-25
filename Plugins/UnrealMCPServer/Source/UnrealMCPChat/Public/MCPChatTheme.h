// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"

class FJsonObject;

/**
 * Phase 1 — the parsed contents of a theme JSON file.
 *
 * This type deliberately holds NO Slate brushes. It is a pure token bag; brush
 * construction happens in FMCPChatStyle so the theme can be reloaded and diffed
 * without touching the live style set.
 *
 * Design contract (docs/03_UIUX_SPEC.md §3.5): every colour, radius, spacing
 * stop, font size and brush in the panel resolves through here. No literal
 * appears in widget code.
 */

/** How a brush should be built. Mirrors the "brush" section of the theme JSON. */
struct FMCPChatBrushSpec
{
	enum class EKind : uint8
	{
		RoundedBox,   // FSlateRoundedBoxBrush — fill + per-corner radius + optional outline
		Box,          // FSlateColorBrush — flat fill, no radius
		Image,        // FSlateImageBrush — raster from Resources/
		Vector,       // FSlateVectorImageBrush — SVG from Resources/
	};

	EKind       Kind = EKind::Box;
	FSlateColor Fill = FSlateColor(FLinearColor::Transparent);
	FVector4    Radius = FVector4(0.f, 0.f, 0.f, 0.f);   // TL, TR, BL, BR
	FSlateColor Outline = FSlateColor(FLinearColor::Transparent);
	float       OutlineWidth = 0.f;
	FString     ResourcePath;                             // Image/Vector only, relative to Resources/
	FVector2f   ImageSize = FVector2f(16.f, 16.f);
};

/** Font family + the size ramp. Sizes are separate tokens so a user can scale text
 *  without touching families. */
struct FMCPChatFontSpec
{
	FString Family = TEXT("Regular");        // resolved via FCoreStyle::GetDefaultFontStyle
	FString MonoFamily = TEXT("Mono");
	int32   Body = 10;
	int32   Small = 8;
	int32   Mono = 9;
	int32   H1 = 14;
	int32   H2 = 12;
	int32   H3 = 11;
};

/** Motion + effect knobs. Kept in the theme so "reduce motion" is a theme edit. */
struct FMCPChatEffectSpec
{
	float BackgroundBlurStrength = 0.f;   // 0 = off
	int32 MessageEnterMs = 120;           // 0 = no enter animation
	FString Easing = TEXT("cubicOut");    // linear | cubicIn | cubicOut | cubicInOut
};

struct FMCPChatTheme
{
	int32   Version = 1;
	FString Name;
	FString SourceFile;

	TMap<FName, FSlateColor>       Colors;
	TMap<FName, float>             Radii;
	TMap<FName, float>             Space;
	TMap<FName, FMCPChatBrushSpec> Brushes;
	FMCPChatFontSpec               Font;
	FMCPChatEffectSpec             Effects;

	/** Populated with human-readable problems found while parsing. Non-fatal:
	 *  a bad token falls back to a documented default and is reported here so the
	 *  panel can surface "3 problems in Custom.json" rather than looking broken. */
	TArray<FString> ParseWarnings;

	// ---- Lookup with defaults. Never assert — a hand-edited theme is untrusted input. ----

	FSlateColor GetColor(FName Token, const FSlateColor& Fallback) const
	{
		const FSlateColor* Found = Colors.Find(Token);
		return Found ? *Found : Fallback;
	}

	float GetRadius(FName Token, float Fallback = 0.f) const
	{
		const float* Found = Radii.Find(Token);
		return Found ? *Found : Fallback;
	}

	float GetSpace(FName Token, float Fallback = 0.f) const
	{
		const float* Found = Space.Find(Token);
		return Found ? *Found : Fallback;
	}

	const FMCPChatBrushSpec* FindBrush(FName Token) const { return Brushes.Find(Token); }
};

/**
 * Parses theme JSON into FMCPChatTheme.
 *
 * Token grammar (see docs/03_UIUX_SPEC.md §3.5):
 *   "#RRGGBBAA"                       literal colour, opts out of editor theming
 *   "@FStyleColors::Recessed"         semantic reference — follows the editor theme live
 *   "@panel"                          reference to another colour token in this file
 *   {"ref": "@FStyleColors::Primary", "alpha": 0.5}
 *                                     snapshot of a semantic colour with alpha applied
 *   "md" | 6 | [6,6,0,0]              radius: token name, uniform value, or per-corner
 */
class FMCPChatThemeParser
{
public:
	/** Parse from a JSON string. Always returns a usable theme; problems land in ParseWarnings. */
	static FMCPChatTheme ParseFromString(const FString& JsonText, const FString& SourceFileForDiagnostics);

	/** Load and parse a file. bOutFileRead reports IO success separately from parse success. */
	static FMCPChatTheme ParseFromFile(const FString& AbsolutePath, bool& bOutFileRead);

	/** The theme used when no file can be read at all — pure FStyleColors references,
	 *  so the panel is always usable and always matches the editor. */
	static FMCPChatTheme MakeBuiltInFallback();

	/** "@FStyleColors::Recessed" → FSlateColor(EStyleColor::Recessed).
	 *  Returns false for an unknown slot name. */
	static bool ResolveStyleColorName(const FString& SlotName, FSlateColor& OutColor);

private:
	static void ParseColors(const TSharedPtr<FJsonObject>& Root, FMCPChatTheme& Theme);
	static void ParseScalarMap(const TSharedPtr<FJsonObject>& Root, const FString& Section,
	                           TMap<FName, float>& OutMap, FMCPChatTheme& Theme);
	static void ParseFont(const TSharedPtr<FJsonObject>& Root, FMCPChatTheme& Theme);
	static void ParseEffects(const TSharedPtr<FJsonObject>& Root, FMCPChatTheme& Theme);
	static void ParseBrushes(const TSharedPtr<FJsonObject>& Root, FMCPChatTheme& Theme);

	/** Resolve any colour-valued JSON node (string or {ref,alpha} object). */
	static bool ResolveColorValue(const TSharedPtr<class FJsonValue>& Value, const FMCPChatTheme& Theme,
	                              FSlateColor& OutColor, FString& OutError);
};
