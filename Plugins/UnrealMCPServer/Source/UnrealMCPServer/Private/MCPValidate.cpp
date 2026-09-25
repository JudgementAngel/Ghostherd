// Copyright StraySpark Studio 2026. All Rights Reserved.
#include "MCPValidate.h"
#include "MCPSearchIndex.h"
#include "Misc/PackageName.h"
#include "UObject/NameTypes.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

FMCPValidateResult FMCPValidate::Args(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FMCPValidateResult::FailCoded(EMCPError::InvalidPath, TEXT("Missing arguments object"));
	}
	return FMCPValidateResult::Ok();
}

FMCPValidateResult FMCPValidate::RequiredString(const TSharedPtr<FJsonObject>& Args, const FString& Field, FString& OutValue)
{
	if (!Args.IsValid() || !Args->TryGetStringField(Field, OutValue) || OutValue.IsEmpty())
	{
		return FMCPValidateResult::FailCoded(EMCPError::InvalidName,
			FString::Printf(TEXT("Missing or empty required field '%s'"), *Field));
	}
	return FMCPValidateResult::Ok();
}

FMCPValidateResult FMCPValidate::PackagePath(const FString& Path)
{
	FText Reason;
	if (!FPackageName::IsValidLongPackageName(Path, /*bIncludeReadOnlyRoots*/ false, &Reason))
	{
		return FMCPValidateResult::FailCoded(EMCPError::InvalidPath,
			FString::Printf(TEXT("Invalid package path '%s': %s"), *Path, *Reason.ToString()),
			TEXT("Use a long package name like /Game/Folder/AssetName."));
	}
	return FMCPValidateResult::Ok();
}

FMCPValidateResult FMCPValidate::AssetName(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return FMCPValidateResult::FailCoded(EMCPError::InvalidName, TEXT("Asset name is empty"));
	}
	FText Reason;
	if (!FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS, &Reason))
	{
		return FMCPValidateResult::FailCoded(EMCPError::InvalidName,
			FString::Printf(TEXT("Invalid asset name '%s': %s"), *Name, *Reason.ToString()),
			TEXT("Use letters, digits, and underscores. No '/', '.', spaces, or other punctuation."));
	}
	return FMCPValidateResult::Ok();
}

FMCPValidateResult FMCPValidate::AssetDoesNotExist(const FString& InPath)
{
	// Callers pass either a package path or a full object path; normalise.
	const FString PackagePath = FPackageName::ObjectPathToPackageName(InPath);

	auto AlreadyExists = [&PackagePath](const TCHAR* Where)
	{
		return FMCPValidateResult::FailCoded(EMCPError::AlreadyExists,
			FString::Printf(TEXT("Asset already exists at '%s' (%s)."), *PackagePath, Where),
			TEXT("Pick a different name, or delete the existing asset first."));
	};

	// On disk. Creating over this would silently overwrite the .uasset, because
	// CreatePackage happily mints a fresh package for a path it has not loaded.
	if (FPackageName::DoesPackageExist(PackagePath))
	{
		return AlreadyExists(TEXT("on disk"));
	}

	// In memory only — an asset created earlier this session whose save failed,
	// or one that was never saved. DoesPackageExist misses these, and they are
	// the ones that make FKismetEditorUtilities::CreateBlueprint assert.
	if (const UPackage* Existing = FindPackage(nullptr, *PackagePath))
	{
		const FString LeafName = FPackageName::GetShortName(PackagePath);
		if (StaticFindObjectFast(UObject::StaticClass(), const_cast<UPackage*>(Existing), FName(*LeafName)))
		{
			return AlreadyExists(TEXT("loaded in memory"));
		}
	}

	return FMCPValidateResult::Ok();
}

FMCPValidateResult FMCPValidate::AssetExists(const FString& PackagePath)
{
	if (!FPackageName::DoesPackageExist(PackagePath))
	{
		// Phase C / C2: surface fuzzy suggestions from the search index.
		const FString LeafName = FPackageName::GetShortName(PackagePath);
		TArray<FString> DidYouMean = FMCPSearchIndex::Get().SuggestSimilar(LeafName, TEXT("Asset"), /*Limit*/ 3);
		return FMCPValidateResult::FailCoded(EMCPError::NotFound,
			FString::Printf(TEXT("Asset not found at '%s'"), *PackagePath),
			TEXT("Use search_project to discover the right path, or check_asset_exists before depending on it."),
			DidYouMean);
	}
	return FMCPValidateResult::Ok();
}

FMCPValidateResult FMCPValidate::InRangeI(int32 Value, int32 Min, int32 Max, const FString& FieldName)
{
	if (Value < Min || Value > Max)
	{
		return FMCPValidateResult::FailCoded(EMCPError::OutOfRange,
			FString::Printf(TEXT("Field '%s' = %d is out of range [%d, %d]"), *FieldName, Value, Min, Max));
	}
	return FMCPValidateResult::Ok();
}

FMCPValidateResult FMCPValidate::InRangeF(double Value, double Min, double Max, const FString& FieldName)
{
	if (Value < Min || Value > Max)
	{
		return FMCPValidateResult::FailCoded(EMCPError::OutOfRange,
			FString::Printf(TEXT("Field '%s' = %f is out of range [%f, %f]"), *FieldName, Value, Min, Max));
	}
	return FMCPValidateResult::Ok();
}

FMCPValidateResult FMCPValidate::OneOf(const FString& Value, const TArray<FString>& AllowedValues, const FString& FieldName)
{
	if (!AllowedValues.Contains(Value))
	{
		return FMCPValidateResult::FailCoded(EMCPError::OutOfRange,
			FString::Printf(TEXT("Field '%s' = '%s' is not one of: %s"),
				*FieldName, *Value, *FString::Join(AllowedValues, TEXT(", "))),
			FString(), AllowedValues);
	}
	return FMCPValidateResult::Ok();
}

FMCPValidateResult FMCPValidate::RequiredNumber(const TSharedPtr<FJsonObject>& Args, const FString& Field, double& OutValue)
{
	if (!Args.IsValid() || !Args->HasTypedField<EJson::Number>(Field))
	{
		return FMCPValidateResult::FailCoded(EMCPError::OutOfRange,
			FString::Printf(TEXT("'%s' is required and must be a number."), *Field));
	}
	OutValue = Args->GetNumberField(Field);
	return FMCPValidateResult::Ok();
}

FMCPValidateResult FMCPValidate::RequiredStringArray(const TSharedPtr<FJsonObject>& Args, const FString& Field, TArray<FString>& OutValues)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Args.IsValid() || !Args->TryGetArrayField(Field, Arr) || Arr->Num() == 0)
	{
		return FMCPValidateResult::FailCoded(EMCPError::OutOfRange,
			FString::Printf(TEXT("'%s' is required and must be a non-empty array of strings."), *Field));
	}
	TArray<FString> Validated;
	for (int32 Index = 0; Index < Arr->Num(); ++Index)
	{
		const auto& Value = (*Arr)[Index];
		if (!Value.IsValid() || Value->Type != EJson::String)
		{
			return FMCPValidateResult::FailCoded(EMCPError::OutOfRange,
				FString::Printf(TEXT("'%s[%d]' must be a string."), *Field, Index));
		}
		Validated.Add(Value->AsString());
	}
	OutValues = MoveTemp(Validated);
	return FMCPValidateResult::Ok();
}
