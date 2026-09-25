// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatTheme.h"
#include "UnrealMCPChatModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Styling/StyleColors.h"

namespace
{
	/** Name → EStyleColor. Covers the semantic slots a chat panel plausibly needs;
	 *  unknown names are reported as warnings rather than silently swallowed so a
	 *  typo in a hand-edited theme is discoverable. */
	const TMap<FString, EStyleColor>& GetStyleColorTable()
	{
		static const TMap<FString, EStyleColor> Table = {
			{ TEXT("Black"),              EStyleColor::Black },
			{ TEXT("Background"),         EStyleColor::Background },
			{ TEXT("Title"),              EStyleColor::Title },
			{ TEXT("WindowBorder"),       EStyleColor::WindowBorder },
			{ TEXT("Foldout"),            EStyleColor::Foldout },
			{ TEXT("Input"),              EStyleColor::Input },
			{ TEXT("InputOutline"),       EStyleColor::InputOutline },
			{ TEXT("Recessed"),           EStyleColor::Recessed },
			{ TEXT("Panel"),              EStyleColor::Panel },
			{ TEXT("Header"),             EStyleColor::Header },
			{ TEXT("Dropdown"),           EStyleColor::Dropdown },
			{ TEXT("DropdownOutline"),    EStyleColor::DropdownOutline },
			{ TEXT("Hover"),              EStyleColor::Hover },
			{ TEXT("Hover2"),             EStyleColor::Hover2 },
			{ TEXT("White"),              EStyleColor::White },
			{ TEXT("White25"),            EStyleColor::White25 },
			{ TEXT("Highlight"),          EStyleColor::Highlight },
			{ TEXT("Primary"),            EStyleColor::Primary },
			{ TEXT("PrimaryHover"),       EStyleColor::PrimaryHover },
			{ TEXT("PrimaryPress"),       EStyleColor::PrimaryPress },
			{ TEXT("Secondary"),          EStyleColor::Secondary },
			{ TEXT("Foreground"),         EStyleColor::Foreground },
			{ TEXT("ForegroundHover"),    EStyleColor::ForegroundHover },
			{ TEXT("ForegroundInverted"), EStyleColor::ForegroundInverted },
			{ TEXT("ForegroundHeader"),   EStyleColor::ForegroundHeader },
			{ TEXT("Select"),             EStyleColor::Select },
			{ TEXT("SelectInactive"),     EStyleColor::SelectInactive },
			{ TEXT("SelectParent"),       EStyleColor::SelectParent },
			{ TEXT("SelectHover"),        EStyleColor::SelectHover },
			{ TEXT("Notifications"),      EStyleColor::Notifications },
			{ TEXT("AccentBlue"),         EStyleColor::AccentBlue },
			{ TEXT("AccentPurple"),       EStyleColor::AccentPurple },
			{ TEXT("AccentPink"),         EStyleColor::AccentPink },
			{ TEXT("AccentRed"),          EStyleColor::AccentRed },
			{ TEXT("AccentOrange"),       EStyleColor::AccentOrange },
			{ TEXT("AccentYellow"),       EStyleColor::AccentYellow },
			{ TEXT("AccentGreen"),        EStyleColor::AccentGreen },
			{ TEXT("AccentBrown"),        EStyleColor::AccentBrown },
			{ TEXT("AccentBlack"),        EStyleColor::AccentBlack },
			{ TEXT("AccentGray"),         EStyleColor::AccentGray },
			{ TEXT("AccentWhite"),        EStyleColor::AccentWhite },
			{ TEXT("AccentFolder"),       EStyleColor::AccentFolder },
			{ TEXT("Warning"),            EStyleColor::Warning },
			{ TEXT("Error"),              EStyleColor::Error },
			{ TEXT("Success"),            EStyleColor::Success },
		};
		return Table;
	}

	/** "#RGB", "#RRGGBB", "#RRGGBBAA". Validated before handing to FColor::FromHex,
	 *  which is not defensive about malformed input. */
	bool TryParseHexColor(const FString& In, FLinearColor& Out)
	{
		FString Hex = In;
		Hex.RemoveFromStart(TEXT("#"));
		if (Hex.Len() != 3 && Hex.Len() != 6 && Hex.Len() != 8)
		{
			return false;
		}
		for (TCHAR C : Hex)
		{
			if (!FChar::IsHexDigit(C))
			{
				return false;
			}
		}
		// Theme files are authored in sRGB (what a colour picker gives you), so convert.
		Out = FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
		return true;
	}
}

bool FMCPChatThemeParser::ResolveStyleColorName(const FString& SlotName, FSlateColor& OutColor)
{
	if (const EStyleColor* Found = GetStyleColorTable().Find(SlotName))
	{
		OutColor = FSlateColor(*Found);
		return true;
	}
	return false;
}

bool FMCPChatThemeParser::ResolveColorValue(const TSharedPtr<FJsonValue>& Value, const FMCPChatTheme& Theme,
	FSlateColor& OutColor, FString& OutError)
{
	if (!Value.IsValid())
	{
		OutError = TEXT("null colour value");
		return false;
	}

	// Form A: plain string — hex literal, @FStyleColors::Slot, or @otherToken.
	FString AsString;
	if (Value->TryGetString(AsString))
	{
		AsString.TrimStartAndEndInline();

		if (AsString.StartsWith(TEXT("@FStyleColors::")))
		{
			const FString Slot = AsString.RightChop(15);
			if (ResolveStyleColorName(Slot, OutColor))
			{
				return true;
			}
			OutError = FString::Printf(TEXT("unknown FStyleColors slot '%s'"), *Slot);
			return false;
		}

		if (AsString.StartsWith(TEXT("@")))
		{
			const FName Token(*AsString.RightChop(1));
			if (const FSlateColor* Found = Theme.Colors.Find(Token))
			{
				OutColor = *Found;
				return true;
			}
			OutError = FString::Printf(TEXT("colour token '@%s' is not defined (declare it before use)"), *Token.ToString());
			return false;
		}

		FLinearColor Literal;
		if (TryParseHexColor(AsString, Literal))
		{
			OutColor = FSlateColor(Literal);
			return true;
		}

		OutError = FString::Printf(TEXT("'%s' is neither a hex colour nor an @reference"), *AsString);
		return false;
	}

	// Form B: {"ref": "...", "alpha": 0.5}
	const TSharedPtr<FJsonObject>* AsObject = nullptr;
	if (Value->TryGetObject(AsObject) && AsObject)
	{
		FString Ref;
		if (!(*AsObject)->TryGetStringField(TEXT("ref"), Ref))
		{
			OutError = TEXT("colour object is missing the 'ref' field");
			return false;
		}

		FSlateColor Base;
		FString InnerError;
		if (!ResolveColorValue(MakeShared<FJsonValueString>(Ref), Theme, Base, InnerError))
		{
			OutError = InnerError;
			return false;
		}

		double Alpha = 1.0;
		(*AsObject)->TryGetNumberField(TEXT("alpha"), Alpha);

		// NOTE (deliberate tradeoff, documented in 03 §3.5): applying alpha forces a
		// SNAPSHOT of the semantic colour, because FSlateColor cannot carry
		// "EStyleColor slot × alpha". GetSpecifiedColor() resolves a style-table slot
		// against the editor's CURRENT theme; FMCPChatStyle rebuilds the whole style
		// set when the editor theme changes, so the snapshot is refreshed rather than
		// going stale. A theme that needs live-following alpha should reference a
		// pre-dimmed FStyleColors slot instead.
		FLinearColor Resolved = Base.GetSpecifiedColor();
		Resolved.A *= static_cast<float>(FMath::Clamp(Alpha, 0.0, 1.0));
		OutColor = FSlateColor(Resolved);
		return true;
	}

	OutError = TEXT("colour must be a string or a {ref, alpha} object");
	return false;
}

void FMCPChatThemeParser::ParseColors(const TSharedPtr<FJsonObject>& Root, FMCPChatTheme& Theme)
{
	const TSharedPtr<FJsonObject>* Section = nullptr;
	if (!Root->TryGetObjectField(TEXT("color"), Section) || !Section)
	{
		return;
	}

	// Two passes so "@otherToken" forward references still resolve if the author
	// happened to order keys inconveniently. Pass 1 takes everything that resolves
	// without a local reference; pass 2 retries the rest.
	TArray<TPair<FString, TSharedPtr<FJsonValue>>> Deferred;

	for (const auto& Pair : (*Section)->Values)
	{
		FSlateColor Resolved;
		FString Error;
		if (ResolveColorValue(Pair.Value, Theme, Resolved, Error))
		{
			Theme.Colors.Add(FName(*Pair.Key), Resolved);
		}
		else
		{
			Deferred.Add(Pair);
		}
	}

	for (const auto& Pair : Deferred)
	{
		FSlateColor Resolved;
		FString Error;
		if (ResolveColorValue(Pair.Value, Theme, Resolved, Error))
		{
			Theme.Colors.Add(FName(*Pair.Key), Resolved);
		}
		else
		{
			Theme.ParseWarnings.Add(FString::Printf(TEXT("color.%s: %s"), *Pair.Key, *Error));
		}
	}
}

void FMCPChatThemeParser::ParseScalarMap(const TSharedPtr<FJsonObject>& Root, const FString& SectionName,
	TMap<FName, float>& OutMap, FMCPChatTheme& Theme)
{
	const TSharedPtr<FJsonObject>* Section = nullptr;
	if (!Root->TryGetObjectField(SectionName, Section) || !Section)
	{
		return;
	}

	for (const auto& Pair : (*Section)->Values)
	{
		double Number = 0.0;
		if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(Number))
		{
			OutMap.Add(FName(*Pair.Key), static_cast<float>(Number));
		}
		else
		{
			Theme.ParseWarnings.Add(FString::Printf(TEXT("%s.%s: expected a number"), *SectionName, *Pair.Key));
		}
	}
}

void FMCPChatThemeParser::ParseFont(const TSharedPtr<FJsonObject>& Root, FMCPChatTheme& Theme)
{
	const TSharedPtr<FJsonObject>* Section = nullptr;
	if (!Root->TryGetObjectField(TEXT("font"), Section) || !Section)
	{
		return;
	}

	(*Section)->TryGetStringField(TEXT("family"), Theme.Font.Family);
	(*Section)->TryGetStringField(TEXT("monoFamily"), Theme.Font.MonoFamily);

	auto ReadSize = [&](const TCHAR* Key, int32& Out)
	{
		double N = 0.0;
		if ((*Section)->TryGetNumberField(Key, N))
		{
			// Clamp so a fat-fingered "body": 400 can't make the panel unusable.
			Out = FMath::Clamp(static_cast<int32>(N), 6, 48);
		}
	};
	ReadSize(TEXT("body"),  Theme.Font.Body);
	ReadSize(TEXT("small"), Theme.Font.Small);
	ReadSize(TEXT("mono"),  Theme.Font.Mono);
	ReadSize(TEXT("h1"),    Theme.Font.H1);
	ReadSize(TEXT("h2"),    Theme.Font.H2);
	ReadSize(TEXT("h3"),    Theme.Font.H3);
}

void FMCPChatThemeParser::ParseEffects(const TSharedPtr<FJsonObject>& Root, FMCPChatTheme& Theme)
{
	const TSharedPtr<FJsonObject>* Section = nullptr;
	if (!Root->TryGetObjectField(TEXT("effects"), Section) || !Section)
	{
		return;
	}

	double N = 0.0;
	if ((*Section)->TryGetNumberField(TEXT("backgroundBlur"), N))
	{
		Theme.Effects.BackgroundBlurStrength = FMath::Clamp(static_cast<float>(N), 0.f, 100.f);
	}
	if ((*Section)->TryGetNumberField(TEXT("messageEnterMs"), N))
	{
		Theme.Effects.MessageEnterMs = FMath::Clamp(static_cast<int32>(N), 0, 2000);
	}
	(*Section)->TryGetStringField(TEXT("easing"), Theme.Effects.Easing);
}

void FMCPChatThemeParser::ParseBrushes(const TSharedPtr<FJsonObject>& Root, FMCPChatTheme& Theme)
{
	const TSharedPtr<FJsonObject>* Section = nullptr;
	if (!Root->TryGetObjectField(TEXT("brush"), Section) || !Section)
	{
		return;
	}

	for (const auto& Pair : (*Section)->Values)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Obj) || !Obj)
		{
			Theme.ParseWarnings.Add(FString::Printf(TEXT("brush.%s: expected an object"), *Pair.Key));
			continue;
		}

		FMCPChatBrushSpec Spec;

		FString KindStr = TEXT("box");
		(*Obj)->TryGetStringField(TEXT("type"), KindStr);
		if      (KindStr.Equals(TEXT("roundedBox"), ESearchCase::IgnoreCase)) { Spec.Kind = FMCPChatBrushSpec::EKind::RoundedBox; }
		else if (KindStr.Equals(TEXT("box"),        ESearchCase::IgnoreCase)) { Spec.Kind = FMCPChatBrushSpec::EKind::Box; }
		else if (KindStr.Equals(TEXT("image"),      ESearchCase::IgnoreCase)) { Spec.Kind = FMCPChatBrushSpec::EKind::Image; }
		else if (KindStr.Equals(TEXT("vector"),     ESearchCase::IgnoreCase)) { Spec.Kind = FMCPChatBrushSpec::EKind::Vector; }
		else
		{
			Theme.ParseWarnings.Add(FString::Printf(
				TEXT("brush.%s: unknown type '%s' (expected roundedBox|box|image|vector); using box"), *Pair.Key, *KindStr));
		}

		// Fill / outline
		if (const TSharedPtr<FJsonValue>* FillVal = (*Obj)->Values.Find(TEXT("fill")))
		{
			FString Error;
			if (!ResolveColorValue(*FillVal, Theme, Spec.Fill, Error))
			{
				Theme.ParseWarnings.Add(FString::Printf(TEXT("brush.%s.fill: %s"), *Pair.Key, *Error));
			}
		}
		if (const TSharedPtr<FJsonValue>* OutlineVal = (*Obj)->Values.Find(TEXT("outline")))
		{
			FString Error;
			if (!ResolveColorValue(*OutlineVal, Theme, Spec.Outline, Error))
			{
				Theme.ParseWarnings.Add(FString::Printf(TEXT("brush.%s.outline: %s"), *Pair.Key, *Error));
			}
		}

		double OutlineW = 0.0;
		if ((*Obj)->TryGetNumberField(TEXT("outlineWidth"), OutlineW))
		{
			Spec.OutlineWidth = FMath::Clamp(static_cast<float>(OutlineW), 0.f, 16.f);
		}

		// Radius: token name ("md"), uniform number (6), or per-corner array.
		if (const TSharedPtr<FJsonValue>* RadiusValPtr = (*Obj)->Values.Find(TEXT("radius")))
		{
			const TSharedPtr<FJsonValue>& RadiusVal = *RadiusValPtr;
			FString RadiusToken;
			double  RadiusNumber = 0.0;
			const TArray<TSharedPtr<FJsonValue>>* RadiusArray = nullptr;

			if (RadiusVal->TryGetArray(RadiusArray) && RadiusArray && RadiusArray->Num() == 4)
			{
				Spec.Radius = FVector4(
					(*RadiusArray)[0]->AsNumber(), (*RadiusArray)[1]->AsNumber(),
					(*RadiusArray)[2]->AsNumber(), (*RadiusArray)[3]->AsNumber());
			}
			else if (RadiusVal->TryGetNumber(RadiusNumber))
			{
				const float R = static_cast<float>(RadiusNumber);
				Spec.Radius = FVector4(R, R, R, R);
			}
			else if (RadiusVal->TryGetString(RadiusToken))
			{
				const float R = Theme.GetRadius(FName(*RadiusToken), -1.f);
				if (R < 0.f)
				{
					Theme.ParseWarnings.Add(FString::Printf(
						TEXT("brush.%s.radius: unknown radius token '%s'"), *Pair.Key, *RadiusToken));
				}
				else
				{
					Spec.Radius = FVector4(R, R, R, R);
				}
			}
		}

		(*Obj)->TryGetStringField(TEXT("path"), Spec.ResourcePath);

		const TArray<TSharedPtr<FJsonValue>>* SizeArray = nullptr;
		if ((*Obj)->TryGetArrayField(TEXT("size"), SizeArray) && SizeArray && SizeArray->Num() == 2)
		{
			Spec.ImageSize = FVector2f(
				static_cast<float>((*SizeArray)[0]->AsNumber()),
				static_cast<float>((*SizeArray)[1]->AsNumber()));
		}

		if ((Spec.Kind == FMCPChatBrushSpec::EKind::Image || Spec.Kind == FMCPChatBrushSpec::EKind::Vector)
			&& Spec.ResourcePath.IsEmpty())
		{
			Theme.ParseWarnings.Add(FString::Printf(
				TEXT("brush.%s: image/vector brush needs a 'path'; skipped"), *Pair.Key));
			continue;
		}

		Theme.Brushes.Add(FName(*Pair.Key), Spec);
	}
}

FMCPChatTheme FMCPChatThemeParser::ParseFromString(const FString& JsonText, const FString& SourceFileForDiagnostics)
{
	FMCPChatTheme Theme = MakeBuiltInFallback();
	Theme.SourceFile = SourceFileForDiagnostics;

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Theme.ParseWarnings.Add(FString::Printf(
			TEXT("%s is not valid JSON; using the built-in fallback theme."), *SourceFileForDiagnostics));
		return Theme;
	}

	double VersionNumber = 1.0;
	if (Root->TryGetNumberField(TEXT("version"), VersionNumber))
	{
		Theme.Version = static_cast<int32>(VersionNumber);
	}
	if (Theme.Version > 1)
	{
		Theme.ParseWarnings.Add(FString::Printf(
			TEXT("%s declares version %d but this build understands version 1; unknown keys are ignored."),
			*SourceFileForDiagnostics, Theme.Version));
	}
	Root->TryGetStringField(TEXT("name"), Theme.Name);

	// Order matters: scalars first so brushes can reference radius tokens, colours
	// before brushes so brushes can reference colour tokens.
	ParseScalarMap(Root, TEXT("radius"), Theme.Radii, Theme);
	ParseScalarMap(Root, TEXT("space"),  Theme.Space, Theme);
	ParseColors(Root, Theme);
	ParseFont(Root, Theme);
	ParseEffects(Root, Theme);
	ParseBrushes(Root, Theme);

	return Theme;
}

FMCPChatTheme FMCPChatThemeParser::ParseFromFile(const FString& AbsolutePath, bool& bOutFileRead)
{
	FString Contents;
	bOutFileRead = FFileHelper::LoadFileToString(Contents, *AbsolutePath);
	if (!bOutFileRead)
	{
		FMCPChatTheme Fallback = MakeBuiltInFallback();
		Fallback.SourceFile = AbsolutePath;
		Fallback.ParseWarnings.Add(FString::Printf(TEXT("Could not read '%s'."), *AbsolutePath));
		return Fallback;
	}
	return ParseFromString(Contents, FPaths::GetCleanFilename(AbsolutePath));
}

FMCPChatTheme FMCPChatThemeParser::MakeBuiltInFallback()
{
	FMCPChatTheme Theme;
	Theme.Name = TEXT("Built-in");

	// Pure semantic references: this theme is correct in Dark, Light and any custom
	// editor theme without a single literal. It is also the schema reference — every
	// token the widget layer may ask for is defined here, so a user theme that omits
	// keys degrades to sensible values instead of transparent holes.
	Theme.Colors.Add(TEXT("bg"),          FSlateColor(EStyleColor::Recessed));
	Theme.Colors.Add(TEXT("panel"),       FSlateColor(EStyleColor::Panel));
	Theme.Colors.Add(TEXT("header"),      FSlateColor(EStyleColor::Header));
	Theme.Colors.Add(TEXT("input"),       FSlateColor(EStyleColor::Input));
	Theme.Colors.Add(TEXT("fg"),          FSlateColor(EStyleColor::Foreground));
	Theme.Colors.Add(TEXT("fgSubdued"),   FSlateColor(EStyleColor::ForegroundHover));
	Theme.Colors.Add(TEXT("accent"),      FSlateColor(EStyleColor::Primary));
	Theme.Colors.Add(TEXT("accentHover"), FSlateColor(EStyleColor::PrimaryHover));
	Theme.Colors.Add(TEXT("ruleUser"),    FSlateColor(EStyleColor::Primary));
	Theme.Colors.Add(TEXT("ruleAsst"),    FSlateColor(EStyleColor::AccentBlue));
	Theme.Colors.Add(TEXT("success"),     FSlateColor(EStyleColor::Success));
	Theme.Colors.Add(TEXT("warning"),     FSlateColor(EStyleColor::Warning));
	Theme.Colors.Add(TEXT("error"),       FSlateColor(EStyleColor::Error));
	Theme.Colors.Add(TEXT("codeBg"),      FSlateColor(EStyleColor::Background));
	Theme.Colors.Add(TEXT("separator"),   FSlateColor(EStyleColor::DropdownOutline));
	Theme.Colors.Add(TEXT("onAccent"),    FSlateColor(EStyleColor::ForegroundInverted));

	Theme.Radii.Add(TEXT("sm"), 3.f);
	Theme.Radii.Add(TEXT("md"), 6.f);
	Theme.Radii.Add(TEXT("lg"), 10.f);
	Theme.Radii.Add(TEXT("pill"), 999.f);

	Theme.Space.Add(TEXT("xs"),  4.f);
	Theme.Space.Add(TEXT("sm"),  8.f);
	Theme.Space.Add(TEXT("md"),  12.f);
	Theme.Space.Add(TEXT("lg"),  16.f);
	Theme.Space.Add(TEXT("xl"),  24.f);
	Theme.Space.Add(TEXT("2xl"), 32.f);
	// Control sizes live in the space map rather than a separate section — one
	// scale, one place to override.
	Theme.Space.Add(TEXT("controlW"), 28.f);
	Theme.Space.Add(TEXT("controlH"), 20.f);

	auto AddRounded = [&Theme](const TCHAR* Name, FName FillToken, FName RadiusToken)
	{
		FMCPChatBrushSpec Spec;
		Spec.Kind = FMCPChatBrushSpec::EKind::RoundedBox;
		Spec.Fill = Theme.GetColor(FillToken, FSlateColor(EStyleColor::Panel));
		const float R = Theme.GetRadius(RadiusToken, 4.f);
		Spec.Radius = FVector4(R, R, R, R);
		Theme.Brushes.Add(FName(Name), Spec);
	};

	AddRounded(TEXT("composer"),   TEXT("input"), TEXT("md"));
	AddRounded(TEXT("toolCard"),   TEXT("panel"), TEXT("sm"));
	AddRounded(TEXT("sendButton"), TEXT("accent"), TEXT("pill"));
	AddRounded(TEXT("chip"),       TEXT("panel"), TEXT("pill"));

	{
		FMCPChatBrushSpec PanelBg;
		PanelBg.Kind = FMCPChatBrushSpec::EKind::Box;
		PanelBg.Fill = FSlateColor(EStyleColor::Recessed);
		Theme.Brushes.Add(TEXT("transcriptBg"), PanelBg);

		FMCPChatBrushSpec RailBg;
		RailBg.Kind = FMCPChatBrushSpec::EKind::Box;
		RailBg.Fill = FSlateColor(EStyleColor::Panel);
		Theme.Brushes.Add(TEXT("railBg"), RailBg);
	}

	return Theme;
}
