#include "Tools/MCPMetaSoundTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "FileHelpers.h"

// ---------------------------------------------------------------------------
// MetaSound module availability check
//
// MetaSound classes (UMetaSoundSource, UMetaSoundPatch) live inside the
// "MetasoundEngine" module which ships with the engine but may not be
// loaded when the MetaSound plugin is disabled. We intentionally avoid
// including MetaSound headers directly so that this translation unit
// compiles even when the plugin is absent; all class references are
// resolved at runtime via UE's object/class lookup system.
// ---------------------------------------------------------------------------

namespace MCPMetaSoundTools
{

// ============================================================================
// Internal helpers
// ============================================================================

/** Check whether the MetaSound plugin classes are available at runtime. */
static bool IsMetaSoundAvailable()
{
	// UMetaSoundSource is the canonical class. Finding it confirms the plugin.
	UClass* MSClass = FindFirstObject<UClass>(TEXT("MetaSoundSource"), EFindFirstObjectOptions::ExactClass);
	return (MSClass != nullptr);
}

/**
 * Try multiple lookup strategies to find a MetaSound class by short name.
 * Checks "MetaSoundSource", "MetaSoundPatch", etc.
 */
static UClass* FindMetaSoundClass(const FString& ClassName)
{
	// 1. Direct O(1) lookup in the UObject class registry
	UClass* Found = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	if (Found) return Found;

	// 2. Attempt loading from the MetasoundEngine module path
	Found = LoadClass<UObject>(nullptr, *FString::Printf(TEXT("/Script/MetasoundEngine.%s"), *ClassName));
	if (Found) return Found;

	// 3. Attempt loading from the Metasound (non-Engine) module
	Found = LoadClass<UObject>(nullptr, *FString::Printf(TEXT("/Script/Metasound.%s"), *ClassName));
	if (Found) return Found;

	return nullptr;
}

/**
 * Parse the package path and short asset name from a content path string.
 * Accepts both "/Game/Audio/MS_Foo" and "/Game/Audio/MS_Foo.MS_Foo" forms.
 */
static void SplitAssetPath(const FString& InPath, FString& OutPackagePath, FString& OutAssetName)
{
	// Strip object suffix if present (e.g., "/Game/X.X" -> "/Game/X")
	FString CleanPath = InPath;
	int32 DotIdx;
	if (CleanPath.FindLastChar(TEXT('.'), DotIdx))
	{
		CleanPath = CleanPath.Left(DotIdx);
	}

	OutPackagePath = CleanPath;
	OutAssetName = FPackageName::GetShortName(CleanPath);
}

/**
 * Read a named UObject property as a string via the reflection system.
 * Returns true and fills OutValue when the property is found.
 */
static bool GetPropertyValueAsString(UObject* Obj, const FString& PropName, FString& OutValue)
{
	if (!IsValid(Obj)) return false;

	FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(*PropName));
	if (!Prop) return false;

	const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Obj);
	Prop->ExportTextItem_Direct(OutValue, ValuePtr, nullptr, nullptr, PPF_None);
	return true;
}

/**
 * Set a named UObject property from a string value via the reflection system.
 * Returns true on success.
 */
static bool SetPropertyValueFromString(UObject* Obj, const FString& PropName, const FString& ValueStr)
{
	if (!IsValid(Obj)) return false;

	FProperty* Prop = Obj->GetClass()->FindPropertyByName(FName(*PropName));
	if (!Prop) return false;

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Obj);
	return Prop->ImportText_Direct(*ValueStr, ValuePtr, nullptr, PPF_None) != nullptr;
}

// ============================================================================
// RegisterAll
// ============================================================================

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_metasound_source
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"),
			TEXT("Content path for the new MetaSound Source asset (e.g., '/Game/Audio/MS_MySound')."),
			true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_metasound_source");
		Def.Description = TEXT(
			"Create a new MetaSound Source asset at the specified content path. "
			"The MetaSound plugin must be enabled in Edit > Plugins. "
			"The asset is saved to disk immediately after creation.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			// --- Validate MetaSound availability ---
			UClass* MetaSoundSourceClass = FindMetaSoundClass(TEXT("MetaSoundSource"));
			if (!MetaSoundSourceClass)
			{
				return FMCPToolResult::Error(
					TEXT("MetaSound plugin is not loaded. Enable it in Edit > Plugins > MetaSound."));
			}

			// --- Parse arguments ---
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required."));

			FString PackagePath, AssetName;
			SplitAssetPath(AssetPath, PackagePath, AssetName);

			if (AssetName.IsEmpty())
				return FMCPToolResult::Error(TEXT("asset_path must contain a valid asset name."));

			// --- Prevent collision: evict stale object if it exists ---
			UPackage* ExistingPackage = FindPackage(nullptr, *PackagePath);
			if (ExistingPackage)
			{
				UObject* ExistingObj = StaticFindObjectFast(
					UObject::StaticClass(), ExistingPackage, FName(*AssetName));
				if (ExistingObj)
				{
					// Rename to transient so CreatePackage can reuse the name safely
					ExistingObj->Rename(
						*FString::Printf(TEXT("TRASH_%s_%u"), *AssetName, FPlatformTime::Cycles()),
						GetTransientPackage(),
						REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
					ExistingObj->MarkAsGarbage();
				}
			}

			// --- Create package and asset ---
			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to create package at path: %s"), *PackagePath));

			Package->FullyLoad();

			UObject* NewAsset = NewObject<UObject>(
				Package,
				MetaSoundSourceClass,
				FName(*AssetName),
				RF_Public | RF_Standalone);

			if (!IsValid(NewAsset))
				return FMCPToolResult::Error(TEXT("Failed to instantiate MetaSoundSource object."));

			// --- Register with Asset Registry and save ---
			FAssetRegistryModule::AssetCreated(NewAsset);
			Package->MarkPackageDirty();

			FString PackageFilename = FPackageName::LongPackageNameToFilename(
				PackagePath, FPackageName::GetAssetPackageExtension());

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			bool bSaved = UPackage::SavePackage(Package, NewAsset, *PackageFilename, SaveArgs);

			if (!bSaved)
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Asset created in memory but failed to save to disk at: %s"), *PackageFilename));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), AssetName);
			Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
			Result->SetStringField(TEXT("class"), MetaSoundSourceClass->GetName());
			Result->SetStringField(TEXT("package"), PackagePath);
			Result->SetStringField(TEXT("saved_to"), PackageFilename);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_metasound_info
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"),
			TEXT("Content path of the MetaSound Source or Patch asset (e.g., '/Game/Audio/MS_MySound')."),
			true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_metasound_info");
		Def.Description = TEXT(
			"Return information about a MetaSound asset: asset name, class, output format (channels), "
			"duration, and looping flag. Properties are read via UObject reflection.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			if (!IsMetaSoundAvailable())
				return FMCPToolResult::Error(
					TEXT("MetaSound plugin is not loaded. Enable it in Edit > Plugins > MetaSound."));

			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required."));

			// Load without assuming a specific C++ type
			UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
			if (!IsValid(Asset))
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to load asset at path: %s"), *AssetPath));

			UClass* AssetClass = Asset->GetClass();

			// Confirm this is a MetaSound type
			UClass* MetaSoundSourceClass = FindMetaSoundClass(TEXT("MetaSoundSource"));
			UClass* MetaSoundPatchClass  = FindMetaSoundClass(TEXT("MetaSoundPatch"));

			bool bIsSource = MetaSoundSourceClass && AssetClass->IsChildOf(MetaSoundSourceClass);
			bool bIsPatch  = MetaSoundPatchClass  && AssetClass->IsChildOf(MetaSoundPatchClass);

			if (!bIsSource && !bIsPatch)
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Asset '%s' is not a MetaSound type (found class: %s)."),
					*AssetPath, *AssetClass->GetName()));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset_name"), Asset->GetName());
			Result->SetStringField(TEXT("class_name"), AssetClass->GetName());
			Result->SetStringField(TEXT("path"), Asset->GetPathName());

			// --- Read output format via reflection ---
			// UMetaSoundSource exposes a property named "OutputFormat" which is
			// an enum (EMetaSoundOutputAudioFormat: Mono=0, Stereo=1, Quad=2, etc.)
			// We read it as text and also map it to a channel count.
			FString OutputFormatStr;
			if (GetPropertyValueAsString(Asset, TEXT("OutputFormat"), OutputFormatStr))
			{
				Result->SetStringField(TEXT("output_format"), OutputFormatStr);

				// Derive a numeric channel count from the enum display text
				int32 NumChannels = 0;
				if (OutputFormatStr.Contains(TEXT("Mono")))        NumChannels = 1;
				else if (OutputFormatStr.Contains(TEXT("Stereo"))) NumChannels = 2;
				else if (OutputFormatStr.Contains(TEXT("Quad")))   NumChannels = 4;
				else if (OutputFormatStr.Contains(TEXT("5_1"))
					  || OutputFormatStr.Contains(TEXT("5.1")))    NumChannels = 6;
				else if (OutputFormatStr.Contains(TEXT("7_1"))
					  || OutputFormatStr.Contains(TEXT("7.1")))    NumChannels = 8;

				if (NumChannels > 0)
					Result->SetNumberField(TEXT("output_channels"), NumChannels);
			}
			else
			{
				Result->SetStringField(TEXT("output_format"), TEXT("N/A (MetaSoundPatch)"));
			}

			// --- Duration ---
			// UMetaSoundSource may expose "Duration" or "DurationSeconds" (version-dependent).
			FString DurationStr;
			if (!GetPropertyValueAsString(Asset, TEXT("Duration"), DurationStr))
				GetPropertyValueAsString(Asset, TEXT("DurationSeconds"), DurationStr);

			if (!DurationStr.IsEmpty())
				Result->SetStringField(TEXT("duration"), DurationStr);
			else
				Result->SetStringField(TEXT("duration"), TEXT("N/A"));

			// --- Looping ---
			// Exposed as "bIsOneShot" (inverted) or "bLooping" depending on UE version.
			FString LoopingStr;
			bool bLooping = false;
			if (GetPropertyValueAsString(Asset, TEXT("bLooping"), LoopingStr))
			{
				bLooping = LoopingStr.ToBool();
			}
			else if (GetPropertyValueAsString(Asset, TEXT("bIsOneShot"), LoopingStr))
			{
				// bIsOneShot is the inverse of looping
				bLooping = !LoopingStr.ToBool();
			}
			Result->SetBoolField(TEXT("is_looping"), bLooping);

			// --- Source or Patch classification ---
			Result->SetBoolField(TEXT("is_source"), bIsSource);
			Result->SetBoolField(TEXT("is_patch"), bIsPatch);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_metasound_parameter
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"),
			TEXT("Content path of the MetaSound asset (e.g., '/Game/Audio/MS_MySound')."), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("parameter_name"),
			TEXT("Name of the property to set on the MetaSound asset as exposed via UObject reflection."), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("value"),
			TEXT("New value expressed as a string. Booleans: 'true'/'false'. Numbers: '1.5'. Enums: display name string."), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("type"),
			TEXT("Hint for value interpretation (Float, Int, Bool, String). Default: Float."),
			{ TEXT("Float"), TEXT("Int"), TEXT("Bool"), TEXT("String") });

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_metasound_parameter");
		Def.Description = TEXT(
			"Set a default parameter value on a MetaSound asset using UObject property reflection. "
			"The parameter_name must match a UPROPERTY on the MetaSound class. "
			"Use get_metasound_info to discover the asset's class and then consult the MetaSound "
			"documentation for available properties (e.g., 'OutputFormat', 'bLooping').");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			if (!IsMetaSoundAvailable())
				return FMCPToolResult::Error(
					TEXT("MetaSound plugin is not loaded. Enable it in Edit > Plugins > MetaSound."));

			FString AssetPath, ParamName, ValueStr;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required."));
			if (!Args->TryGetStringField(TEXT("parameter_name"), ParamName))
				return FMCPToolResult::Error(TEXT("parameter_name is required."));
			if (!Args->TryGetStringField(TEXT("value"), ValueStr))
				return FMCPToolResult::Error(TEXT("value is required."));

			FString TypeStr = TEXT("Float");
			Args->TryGetStringField(TEXT("type"), TypeStr);

			UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
			if (!IsValid(Asset))
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to load asset at path: %s"), *AssetPath));

			// Locate the property via reflection
			FProperty* Prop = Asset->GetClass()->FindPropertyByName(FName(*ParamName));
			if (!Prop)
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Property '%s' not found on class '%s'. "
					     "Use get_metasound_info to check the class, then consult UE docs for valid property names."),
					*ParamName, *Asset->GetClass()->GetName()));

			// Apply type coercion based on the caller's hint
			FString CoercedValue = ValueStr;

			if (TypeStr == TEXT("Bool"))
			{
				// Normalize to the format UE's text importer expects
				bool bParsed = ValueStr.ToBool();
				CoercedValue = bParsed ? TEXT("True") : TEXT("False");
			}
			else if (TypeStr == TEXT("Int"))
			{
				// Round-trip through int32 to strip non-numeric content
				int32 IntVal = FCString::Atoi(*ValueStr);
				CoercedValue = FString::FromInt(IntVal);
			}
			else if (TypeStr == TEXT("Float"))
			{
				// Round-trip through float to normalize format
				float FloatVal = FCString::Atof(*ValueStr);
				CoercedValue = FString::SanitizeFloat(FloatVal);
			}
			// TypeStr == "String": pass ValueStr as-is

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set MetaSound Parameter")));
			Asset->Modify();

			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Asset);
			const TCHAR* ImportResult = Prop->ImportText_Direct(*CoercedValue, ValuePtr, nullptr, PPF_None);

			if (!ImportResult)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to set property '%s' to value '%s'. "
					     "Check that the value format matches the property type (%s)."),
					*ParamName, *CoercedValue, *Prop->GetCPPType()));
			}

			Asset->MarkPackageDirty();
			GEditor->EndTransaction();

			// Persist to disk
			UPackage* Package = Asset->GetOutermost();
			FString PackageFilename = FPackageName::LongPackageNameToFilename(
				Package->GetName(), FPackageName::GetAssetPackageExtension());

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset"), Asset->GetName());
			Result->SetStringField(TEXT("property"), ParamName);
			Result->SetStringField(TEXT("type"), TypeStr);
			Result->SetStringField(TEXT("value_set"), CoercedValue);
			Result->SetStringField(TEXT("cpp_type"), Prop->GetCPPType());

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// list_metasound_assets
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"),
			TEXT("Content path to search recursively (default: '/Game/')."));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_filter"),
			TEXT("Optional substring filter applied to the asset name."));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("limit"),
			TEXT("Maximum number of results to return (default: 100)."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_metasound_assets");
		Def.Description = TEXT(
			"List all MetaSound Source and MetaSound Patch assets in the project using the Asset Registry. "
			"Returns name, content path, and class for each asset found.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			if (!IsMetaSoundAvailable())
				return FMCPToolResult::Error(
					TEXT("MetaSound plugin is not loaded. Enable it in Edit > Plugins > MetaSound."));

			FString SearchPath = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), SearchPath);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 2000);

			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AR = ARM.Get();

			// Collect assets of both MetaSound class types
			TArray<FAssetData> AllAssets;

			UClass* MetaSoundSourceClass = FindMetaSoundClass(TEXT("MetaSoundSource"));
			UClass* MetaSoundPatchClass  = FindMetaSoundClass(TEXT("MetaSoundPatch"));

			if (MetaSoundSourceClass)
			{
				TArray<FAssetData> SourceAssets;
				AR.GetAssetsByClass(MetaSoundSourceClass->GetClassPathName(), SourceAssets, true);
				AllAssets.Append(SourceAssets);
			}

			if (MetaSoundPatchClass)
			{
				TArray<FAssetData> PatchAssets;
				AR.GetAssetsByClass(MetaSoundPatchClass->GetClassPathName(), PatchAssets, true);
				AllAssets.Append(PatchAssets);
			}

			TArray<TSharedPtr<FJsonValue>> Results;
			int32 TotalMatching = 0;

			for (const FAssetData& Asset : AllAssets)
			{
				// Path filter
				if (!Asset.PackagePath.ToString().StartsWith(SearchPath))
					continue;

				// Name filter
				if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(NameFilter))
					continue;

				TotalMatching++;

				if (Results.Num() >= Limit)
					continue;

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
				Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
				Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
				Entry->SetStringField(TEXT("package"), Asset.PackageName.ToString());

				Results.Add(MakeShared<FJsonValueObject>(Entry));
			}

			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();
			Output->SetStringField(TEXT("search_path"), SearchPath);
			Output->SetNumberField(TEXT("total_matching"), TotalMatching);
			Output->SetNumberField(TEXT("returned"), Results.Num());
			Output->SetArrayField(TEXT("assets"), Results);

			return FMCPToolResult::Success(JsonToString(Output));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// duplicate_metasound
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("source_path"),
			TEXT("Content path of the MetaSound asset to duplicate (e.g., '/Game/Audio/MS_Base')."), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("dest_path"),
			TEXT("Destination content folder for the duplicate (e.g., '/Game/Audio/Variants/')."), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("new_name"),
			TEXT("Name for the new duplicate asset (e.g., 'MS_BaseVariant')."), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("duplicate_metasound");
		Def.Description = TEXT(
			"Duplicate an existing MetaSound Source or Patch asset to create a variant. "
			"Uses IAssetTools::DuplicateAsset so that all internal MetaSound graph data is "
			"properly deep-copied. The duplicate is saved to disk immediately.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			if (!IsMetaSoundAvailable())
				return FMCPToolResult::Error(
					TEXT("MetaSound plugin is not loaded. Enable it in Edit > Plugins > MetaSound."));

			FString SourcePath, DestPath, NewName;
			if (!Args->TryGetStringField(TEXT("source_path"), SourcePath))
				return FMCPToolResult::Error(TEXT("source_path is required."));
			if (!Args->TryGetStringField(TEXT("dest_path"), DestPath))
				return FMCPToolResult::Error(TEXT("dest_path is required."));
			if (!Args->TryGetStringField(TEXT("new_name"), NewName))
				return FMCPToolResult::Error(TEXT("new_name is required."));

			UObject* SourceAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *SourcePath);
			if (!IsValid(SourceAsset))
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to load source asset at path: %s"), *SourcePath));

			// Confirm the source is a MetaSound type
			UClass* MetaSoundSourceClass = FindMetaSoundClass(TEXT("MetaSoundSource"));
			UClass* MetaSoundPatchClass  = FindMetaSoundClass(TEXT("MetaSoundPatch"));
			bool bIsMetaSound =
				(MetaSoundSourceClass && SourceAsset->GetClass()->IsChildOf(MetaSoundSourceClass)) ||
				(MetaSoundPatchClass  && SourceAsset->GetClass()->IsChildOf(MetaSoundPatchClass));

			if (!bIsMetaSound)
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Source asset '%s' is not a MetaSound type (class: %s)."),
					*SourcePath, *SourceAsset->GetClass()->GetName()));

			// Ensure dest path ends with a slash for IAssetTools::DuplicateAsset
			FString NormalizedDestPath = DestPath;
			if (!NormalizedDestPath.EndsWith(TEXT("/")))
				NormalizedDestPath += TEXT("/");

			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			UObject* Duplicate = AssetTools.DuplicateAsset(NewName, NormalizedDestPath, SourceAsset);

			if (!IsValid(Duplicate))
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to duplicate '%s' to '%s%s'. "
					     "Ensure the destination path is valid and the asset name is unique."),
					*SourcePath, *NormalizedDestPath, *NewName));

			// Save the newly created duplicate
			UPackage* DupPackage = Duplicate->GetOutermost();
			DupPackage->MarkPackageDirty();

			FString PackageFilename = FPackageName::LongPackageNameToFilename(
				DupPackage->GetName(), FPackageName::GetAssetPackageExtension());

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			bool bSaved = UPackage::SavePackage(DupPackage, Duplicate, *PackageFilename, SaveArgs);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("source_path"), SourcePath);
			Result->SetStringField(TEXT("duplicate_name"), Duplicate->GetName());
			Result->SetStringField(TEXT("duplicate_path"), Duplicate->GetPathName());
			Result->SetStringField(TEXT("class"), Duplicate->GetClass()->GetName());
			Result->SetBoolField(TEXT("saved"), bSaved);

			if (!bSaved)
				Result->SetStringField(TEXT("warning"),
					TEXT("Duplicate was created in memory but could not be saved to disk."));

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_metasound_quality
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"),
			TEXT("Content path of the MetaSound Source asset (e.g., '/Game/Audio/MS_MySound')."), true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("is_looping"),
			TEXT("Set whether the MetaSound plays in a continuous loop."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("duration"),
			TEXT("Override the duration in seconds (if the MetaSound asset exposes a duration property)."));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("output_channels"),
			TEXT("Set the output channel count: 1 = Mono, 2 = Stereo, 4 = Quad, 6 = 5.1, 8 = 7.1. "
			     "Maps to the OutputFormat enum on UMetaSoundSource."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_metasound_quality");
		Def.Description = TEXT(
			"Set quality and output settings on a MetaSound Source asset using UObject reflection. "
			"Supports looping, duration, and output channel count (mapped to the OutputFormat enum). "
			"Only parameters that are explicitly provided are modified.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			if (!IsMetaSoundAvailable())
				return FMCPToolResult::Error(
					TEXT("MetaSound plugin is not loaded. Enable it in Edit > Plugins > MetaSound."));

			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required."));

			UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
			if (!IsValid(Asset))
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to load asset at path: %s"), *AssetPath));

			// Must be a MetaSoundSource (not Patch) for these quality settings
			UClass* MetaSoundSourceClass = FindMetaSoundClass(TEXT("MetaSoundSource"));
			if (!MetaSoundSourceClass || !Asset->GetClass()->IsChildOf(MetaSoundSourceClass))
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Asset '%s' is not a MetaSound Source (class: %s). "
					     "Quality settings only apply to MetaSoundSource assets."),
					*AssetPath, *Asset->GetClass()->GetName()));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set MetaSound Quality")));
			Asset->Modify();

			TSharedPtr<FJsonObject> ChangesApplied = MakeShared<FJsonObject>();
			TArray<FString> Warnings;
			int32 NumChanges = 0;

			// --- is_looping ---
			bool bLooping = false;
			if (Args->TryGetBoolField(TEXT("is_looping"), bLooping))
			{
				// Try "bLooping" first (UE 5.3+), then fall back to "bIsOneShot" (inverted)
				FString LoopValue = bLooping ? TEXT("True") : TEXT("False");
				FString OneShotValue = bLooping ? TEXT("False") : TEXT("True"); // inverted

				if (SetPropertyValueFromString(Asset, TEXT("bLooping"), LoopValue))
				{
					ChangesApplied->SetBoolField(TEXT("is_looping"), bLooping);
					NumChanges++;
				}
				else if (SetPropertyValueFromString(Asset, TEXT("bIsOneShot"), OneShotValue))
				{
					ChangesApplied->SetBoolField(TEXT("is_looping"), bLooping);
					NumChanges++;
				}
				else
				{
					Warnings.Add(TEXT("Property 'bLooping'/'bIsOneShot' not found on this MetaSoundSource class."));
				}
			}

			// --- duration ---
			if (Args->HasField(TEXT("duration")))
			{
				float DurationVal = (float)Args->GetNumberField(TEXT("duration"));
				FString DurationStr = FString::SanitizeFloat(DurationVal);

				// Try "Duration" then "DurationSeconds"
				if (SetPropertyValueFromString(Asset, TEXT("Duration"), DurationStr))
				{
					ChangesApplied->SetNumberField(TEXT("duration"), DurationVal);
					NumChanges++;
				}
				else if (SetPropertyValueFromString(Asset, TEXT("DurationSeconds"), DurationStr))
				{
					ChangesApplied->SetNumberField(TEXT("duration"), DurationVal);
					NumChanges++;
				}
				else
				{
					Warnings.Add(TEXT("Property 'Duration'/'DurationSeconds' not found on this MetaSoundSource class."));
				}
			}

			// --- output_channels -> OutputFormat enum ---
			if (Args->HasField(TEXT("output_channels")))
			{
				int32 Channels = (int32)Args->GetNumberField(TEXT("output_channels"));

				// EMetaSoundOutputAudioFormat enum numeric values (stable across UE 5.x):
				// 0 = Mono, 1 = Stereo, 2 = Quad, 3 = FiveDotOne, 4 = SevenDotOne
				int32 EnumValue = 1; // Default: Stereo
				FString EnumLabel;

				switch (Channels)
				{
					case 1:  EnumValue = 0; EnumLabel = TEXT("Mono");        break;
					case 2:  EnumValue = 1; EnumLabel = TEXT("Stereo");      break;
					case 4:  EnumValue = 2; EnumLabel = TEXT("Quad");        break;
					case 6:  EnumValue = 3; EnumLabel = TEXT("FiveDotOne");  break;
					case 8:  EnumValue = 4; EnumLabel = TEXT("SevenDotOne"); break;
					default:
						Warnings.Add(FString::Printf(
							TEXT("output_channels=%d is not a supported MetaSound output format. "
							     "Valid values: 1 (Mono), 2 (Stereo), 4 (Quad), 6 (5.1), 8 (7.1)."),
							Channels));
						EnumValue = -1;
						break;
				}

				if (EnumValue >= 0)
				{
					// Try to set via the numeric enum value string
					FString EnumValueStr = FString::FromInt(EnumValue);
					if (SetPropertyValueFromString(Asset, TEXT("OutputFormat"), EnumValueStr))
					{
						ChangesApplied->SetNumberField(TEXT("output_channels"), Channels);
						ChangesApplied->SetStringField(TEXT("output_format"), EnumLabel);
						NumChanges++;
					}
					else
					{
						// Fallback: try setting via the enum label name
						if (SetPropertyValueFromString(Asset, TEXT("OutputFormat"), EnumLabel))
						{
							ChangesApplied->SetNumberField(TEXT("output_channels"), Channels);
							ChangesApplied->SetStringField(TEXT("output_format"), EnumLabel);
							NumChanges++;
						}
						else
						{
							Warnings.Add(TEXT("Property 'OutputFormat' not found on this MetaSoundSource class."));
						}
					}
				}
			}

			if (NumChanges == 0 && Warnings.Num() > 0)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(
					TEXT("No changes could be applied. Warnings: %s"),
					*FString::Join(Warnings, TEXT("; "))));
			}

			Asset->MarkPackageDirty();
			GEditor->EndTransaction();

			// Save
			UPackage* Package = Asset->GetOutermost();
			FString PackageFilename = FPackageName::LongPackageNameToFilename(
				Package->GetName(), FPackageName::GetAssetPackageExtension());

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset"), Asset->GetName());
			Result->SetNumberField(TEXT("changes_applied"), NumChanges);
			Result->SetObjectField(TEXT("changes"), ChangesApplied);

			if (Warnings.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> WarnArray;
				for (const FString& W : Warnings)
					WarnArray.Add(MakeShared<FJsonValueString>(W));
				Result->SetArrayField(TEXT("warnings"), WarnArray);
			}

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPMetaSoundTools
