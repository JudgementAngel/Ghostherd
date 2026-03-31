#include "Tools/MCPAssetTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Misc/PackageName.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Sound/SoundWave.h"
#include "Engine/Blueprint.h"
#include "Animation/AnimSequence.h"

namespace MCPAssetTools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// list_assets - Browse content browser
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"), TEXT("Content path to browse (e.g., '/Game/', '/Game/Blueprints/'). Default: '/Game/'"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("class_filter"), TEXT("Filter by asset class (e.g., 'StaticMesh', 'Material', 'Blueprint', 'Texture2D')"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_filter"), TEXT("Filter by asset name (substring match)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("recursive"), TEXT("Search subdirectories recursively (default: true)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("limit"), TEXT("Maximum number of results (default: 100)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_assets");
		Def.Description = TEXT("List assets in the content browser with filtering by path, class, and name. Returns asset paths, classes, and file sizes.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString ClassFilter, NameFilter;
			Args->TryGetStringField(TEXT("class_filter"), ClassFilter);
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			bool bRecursive = true;
			Args->TryGetBoolField(TEXT("recursive"), bRecursive);

			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 5000);
			}

			TArray<FAssetData> Assets;
			AssetRegistry.GetAssetsByPath(FName(*Path), Assets, bRecursive);

			TArray<FString> Results;
			int32 TotalMatching = 0;

			for (const FAssetData& Asset : Assets)
			{
				// Class filter
				if (!ClassFilter.IsEmpty())
				{
					FString AssetClassName = Asset.AssetClassPath.GetAssetName().ToString();
					if (!AssetClassName.Contains(ClassFilter)) continue;
				}

				// Name filter
				if (!NameFilter.IsEmpty())
				{
					if (!Asset.AssetName.ToString().Contains(NameFilter)) continue;
				}

				TotalMatching++;
				if (Results.Num() < Limit)
				{
					TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
					Obj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
					Obj->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
					Obj->SetStringField(TEXT("package"), Asset.PackageName.ToString());

					Results.Add(JsonToString(Obj));
				}
			}

			return FMCPToolResult::Success(FString::Printf(TEXT("Found %d assets (showing %d):\n[%s]"),
				TotalMatching, Results.Num(), *FString::Join(Results, TEXT(",\n"))));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_asset_info - Get detailed asset information
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Full asset path (e.g., '/Game/Meshes/SM_Chair.SM_Chair')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_asset_info");
		Def.Description = TEXT("Get detailed metadata about an asset: class, package, tags, size, references, and key properties.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));

			if (!AssetData.IsValid())
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
			}

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
			Info->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
			Info->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
			Info->SetStringField(TEXT("package"), AssetData.PackageName.ToString());

			// Tags
			TSharedPtr<FJsonObject> Tags = MakeShared<FJsonObject>();
			AssetData.EnumerateTags([&Tags](const TPair<FName, FAssetTagValueRef>& TagPair)
			{
				Tags->SetStringField(TagPair.Key.ToString(), TagPair.Value.AsString());
			});
			Info->SetObjectField(TEXT("tags"), Tags);

			// Dependencies
			TArray<FName> Deps;
			AssetRegistryModule.Get().GetDependencies(AssetData.PackageName, Deps);
			TArray<TSharedPtr<FJsonValue>> DepsArray;
			for (const FName& Dep : Deps)
			{
				DepsArray.Add(MakeShared<FJsonValueString>(Dep.ToString()));
			}
			Info->SetArrayField(TEXT("dependencies"), DepsArray);

			// Referencers
			TArray<FName> Refs;
			AssetRegistryModule.Get().GetReferencers(AssetData.PackageName, Refs);
			TArray<TSharedPtr<FJsonValue>> RefsArray;
			for (const FName& Ref : Refs)
			{
				RefsArray.Add(MakeShared<FJsonValueString>(Ref.ToString()));
			}
			Info->SetArrayField(TEXT("referencers"), RefsArray);

			return FMCPToolResult::Success(JsonToString(Info));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// import_asset - Import a file from disk
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("source_path"), TEXT("Absolute filesystem path to import (e.g., 'C:/Models/chair.fbx')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("destination_path"), TEXT("Content path destination (e.g., '/Game/Meshes/')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("import_asset");
		Def.Description = TEXT("Import a file from the filesystem into the content browser. Supports FBX, OBJ, PNG, JPG, WAV, and other standard formats.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SourcePath, DestPath;
			if (!Args->TryGetStringField(TEXT("source_path"), SourcePath)) return FMCPToolResult::Error(TEXT("source_path required"));
			if (!Args->TryGetStringField(TEXT("destination_path"), DestPath)) return FMCPToolResult::Error(TEXT("destination_path required"));

			if (!FPaths::FileExists(SourcePath))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Source file not found: %s"), *SourcePath));
			}

			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

			TArray<FString> Files;
			Files.Add(SourcePath);

			TArray<UObject*> ImportedObjects = AssetTools.ImportAssets(Files, DestPath);

			if (ImportedObjects.Num() == 0)
			{
				return FMCPToolResult::Error(TEXT("Import failed - no assets were created"));
			}

			TArray<FString> ImportedNames;
			for (UObject* Obj : ImportedObjects)
			{
				if (Obj)
				{
					ImportedNames.Add(FString::Printf(TEXT("%s (%s)"), *Obj->GetName(), *Obj->GetClass()->GetName()));
				}
			}

			return FMCPToolResult::Success(FString::Printf(TEXT("Imported %d asset(s): %s"),
				ImportedNames.Num(), *FString::Join(ImportedNames, TEXT(", "))));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// delete_asset - Delete an asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the asset to delete"), true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("check_references"), TEXT("Check for references before deleting (default: true)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("delete_asset");
		Def.Description = TEXT("Delete an asset from the content browser. By default checks for references to prevent breaking dependencies.");
		Def.InputSchema = Schema;
		Def.bDestructiveHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			bool bCheckRefs = true;
			Args->TryGetBoolField(TEXT("check_references"), bCheckRefs);

			UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
			if (!Asset)
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
			}

			// Check references
			if (bCheckRefs)
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				TArray<FName> Referencers;
				AssetRegistryModule.Get().GetReferencers(FName(*Asset->GetOutermost()->GetName()), Referencers);

				if (Referencers.Num() > 0)
				{
					TArray<FString> RefNames;
					for (const FName& Ref : Referencers)
					{
						RefNames.Add(Ref.ToString());
					}
					return FMCPToolResult::Error(FString::Printf(
						TEXT("Cannot delete '%s' - referenced by: %s. Set check_references=false to force delete."),
						*AssetPath, *FString::Join(RefNames, TEXT(", "))));
				}
			}

			TArray<UObject*> AssetsToDelete;
			AssetsToDelete.Add(Asset);

			int32 Deleted = ObjectTools::DeleteObjects(AssetsToDelete, false);

			return FMCPToolResult::Success(FString::Printf(TEXT("Deleted %d asset(s)"), Deleted));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// duplicate_asset - Copy an asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("source_path"), TEXT("Content path of the asset to duplicate"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("dest_path"), TEXT("Content path for the duplicate (e.g., '/Game/Meshes/')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("new_name"), TEXT("Name for the duplicate"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("duplicate_asset");
		Def.Description = TEXT("Create a copy of an existing asset at a new location with a new name.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SourcePath, DestPath, NewName;
			if (!Args->TryGetStringField(TEXT("source_path"), SourcePath)) return FMCPToolResult::Error(TEXT("source_path required"));
			if (!Args->TryGetStringField(TEXT("dest_path"), DestPath)) return FMCPToolResult::Error(TEXT("dest_path required"));
			if (!Args->TryGetStringField(TEXT("new_name"), NewName)) return FMCPToolResult::Error(TEXT("new_name required"));

			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			UObject* Duplicate = AssetTools.DuplicateAsset(NewName, DestPath, LoadObject<UObject>(nullptr, *SourcePath));

			if (!Duplicate)
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to duplicate '%s'"), *SourcePath));
			}

			return FMCPToolResult::Success(FString::Printf(TEXT("Duplicated to: %s"), *Duplicate->GetPathName()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// rename_asset - Rename or move an asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Current asset path"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("new_path"), TEXT("New path/name for the asset (e.g., '/Game/NewFolder/NewName')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("rename_asset");
		Def.Description = TEXT("Rename or move an asset to a new path. Automatically fixes up references.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, NewPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("new_path"), NewPath)) return FMCPToolResult::Error(TEXT("new_path required"));

			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

			TArray<FAssetRenameData> RenameData;
			FString PackagePath = FPackageName::ObjectPathToPackageName(NewPath);
			FString AssetName = FPackageName::GetShortName(NewPath);

			FAssetRenameData Data;
			Data.Asset = LoadObject<UObject>(nullptr, *AssetPath);
			if (!Data.Asset.IsValid())
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
			}
			Data.NewPackagePath = FPackageName::GetLongPackagePath(PackagePath);
			Data.NewName = AssetName;
			RenameData.Add(Data);

			bool bSuccess = AssetTools.RenameAssets(RenameData);

			if (bSuccess)
			{
				return FMCPToolResult::Success(FString::Printf(TEXT("Renamed '%s' to '%s'"), *AssetPath, *NewPath));
			}
			return FMCPToolResult::Error(TEXT("Rename failed"));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPAssetTools
